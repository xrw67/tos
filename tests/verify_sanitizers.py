#!/usr/bin/env python3
"""Check sanitizer detection, run the full suite, then repeat lifetime tests.

The selected preset must already be configured and built. Runtime/environment failures
are errors, not skips. Logs (including expected probe reports) live in the build directory.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess

SOURCE = Path(__file__).resolve().parents[1]
LIFETIME_TESTS = r"^tos\.(EventBusTest|ThreadPoolTest|DynamicLibraryTest|AppTest|ScopeExitTest|ScopeExitAllocationTest|ResourceAllocationTest|NativeResourceTest|ProcessTest)\."


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", required=True, choices=("asan-ubsan", "tsan"))
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    build = (args.build_dir or SOURCE / "build" / args.preset).resolve()
    logs = build / "sanitizer-logs"
    logs.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    presets = json.loads((SOURCE / "CMakePresets.json").read_text())
    preset = next(p for p in presets["testPresets"] if p["name"] == args.preset)
    env.update(preset["environment"])

    probes = {"address": "AddressSanitizer: heap-buffer-overflow",
              "undefined": "runtime error: signed integer overflow"}
    if args.preset == "tsan":
        probes = {"thread": "ThreadSanitizer: data race"}
    for scenario, report in probes.items():
        log = logs / f"probe-{scenario}.log"
        with log.open("w") as output:
            result = subprocess.run([str(build / "tests" / "tos_sanitizer_probe"), scenario],
                                    env=env, stdout=output, stderr=subprocess.STDOUT, timeout=30)
        text = log.read_text(errors="replace")
        if result.returncode == 0 or report not in text:
            raise RuntimeError(f"Sanitizer did not detect {scenario}; see {log}\n{text}")
        print(f"Detected expected {scenario} fault; report: {log}", flush=True)

    for name, extra in (("full", ["-L", "unit|example"]),
                        ("lifetime", ["-R", LIFETIME_TESTS, "--repeat", "until-fail:20"])):
        log = logs / f"{name}.log"
        command = ["ctest", "--test-dir", str(build), "-C", "Debug", "--output-on-failure",
                   "--no-tests=error", "--timeout", "120", "--parallel", "1", *extra]
        print(" ".join(command), flush=True)
        with log.open("w") as output:
            result = subprocess.run(command, env=env, stdout=output, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(f"{name} verification failed; see {log}\n"
                               f"{log.read_text(errors='replace')[-18000:]}")
        print(f"{name} verification passed; log: {log}", flush=True)


if __name__ == "__main__":
    main()
