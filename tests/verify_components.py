#!/usr/bin/env python3
"""Offline component, source-consumer and relocated-install checks (stdlib only).

Use a dedicated --work-dir. Each combination owns its build, install and consumer directories.
Third-party development packages must already be available via --prefix-path or the environment.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

SOURCE = Path(__file__).resolve().parents[1]
COMBINATIONS = {
    "base": [], "app": ["app"], "crypto": ["crypto"], "http": ["http"],
    "full": ["app", "crypto", "http"],
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--combinations", nargs="+", choices=COMBINATIONS, default=list(COMBINATIONS))
    parser.add_argument("--generator", default=os.environ.get("CMAKE_GENERATOR", "Ninja"))
    parser.add_argument("--config", default="Release")
    parser.add_argument("--prefix-path", default=os.environ.get("CMAKE_PREFIX_PATH", ""))
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    root = args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    counter = 0

    def run(command, expect=None):
        nonlocal counter
        counter += 1
        log = root / f"{counter:03d}.log"
        print(f"[{counter}] {' '.join(map(str, command))}", flush=True)
        with log.open("w") as stream:
            result = subprocess.run(list(map(str, command)), stdout=stream, stderr=subprocess.STDOUT)
        output = log.read_text(errors="replace")
        if expect is None and result.returncode != 0:
            raise RuntimeError(f"Command failed; log: {log}\n{output[-18000:]}")
        if expect is not None and (result.returncode == 0 or expect not in output):
            raise RuntimeError(f"Expected failure containing {expect!r}; log: {log}\n{output}")

    common = ["-G", args.generator, f"-DCMAKE_BUILD_TYPE={args.config}"]
    if args.prefix_path:
        common.append(f"-DCMAKE_PREFIX_PATH={args.prefix_path}")

    def switches(components):
        return [f"-DTOS_ENABLE_{name.upper()}={'ON' if name in components else 'OFF'}"
                for name in ("app", "crypto", "http")]

    def forbid(components):
        # HTTP itself must not find OpenSSL, but curl's chosen TLS backend may require it.
        # The consumer additionally checks that HTTP has no direct crypto/OpenSSL link.
        return [f"-DCMAKE_DISABLE_FIND_PACKAGE_{package}=ON" for name, package in
                (("crypto", "OpenSSL"), ("http", "CURL")) if name not in components and not (name == "crypto" and "http" in components)]

    def build_test(directory):
        run(["cmake", "--build", directory, "--config", args.config, "--parallel", args.jobs])
        run(["ctest", "--test-dir", directory, "-C", args.config,
             "--output-on-failure", "--no-tests=error", "--timeout", "30"])

    for name in args.combinations:
        components = COMBINATIONS[name]
        case = root / name
        build = case / "build"
        absent = [c for c in ("app", "crypto", "http") if c not in components]
        run(["cmake", "-S", SOURCE, "-B", build, *common, *switches(components),
             *forbid(components), "-DBUILD_TESTING=ON", "-DTOS_BUILD_EXAMPLES=ON", "-DTOS_INSTALL=ON"])
        build_test(build)
        original, relocated = case / "install-original", case / "install-relocated"
        for prefix in (original, relocated):
            if prefix.exists():
                shutil.rmtree(prefix)
        run(["cmake", "--install", build, "--config", args.config, "--prefix", original])
        original.rename(relocated)
        configs = list(relocated.rglob("tosConfig.cmake"))
        if len(configs) != 1:
            raise RuntimeError(f"Expected one installed config: {configs}")
        package_dir = configs[0].parent
        for exported in package_dir.glob("*.cmake"):
            text = exported.read_text()
            if any(str(path).replace("\\", "/") in text.replace("\\", "/")
                   for path in (SOURCE, build, original)):
                raise RuntimeError(f"Non-relocatable path in {exported}")
        for component, header in (("crypto", "base/crypto.h"), ("crypto", "base/certificate.h"),
                                  ("http", "base/http.h"), ("app", "app/app.h")):
            if (relocated / "include/tos" / header).exists() != (component in components):
                raise RuntimeError(f"Incorrect installed header availability: {header}")
        if list(relocated.rglob("*gtest*")) or list(relocated.rglob("atomic_file_writer.h")):
            raise RuntimeError("Installation leaked tests or private headers")

        def consumer(label, requested, extra=(), expected=None, source=False):
            directory = case / label
            command = ["cmake", "-S", SOURCE / "tests/package_consumer", "-B", directory,
                       *common, f"-DTOS_TEST_COMPONENTS={';'.join(requested)}", *extra]
            if source:
                command += [f"-DTOS_SOURCE_DIR={SOURCE}", *switches(components), *forbid(components),
                            f"-DTOS_TEST_ABSENT={';'.join(absent)}"]
            else:
                command += [f"-Dtos_DIR={package_dir}"]
            run(command, expected)
            if expected is None:
                build_test(directory)

        consumer("source-consumer", components, source=True)
        consumer("installed-consumer", components,
                 [*forbid(components), "-DTOS_REPEAT_FIND=ON", "-DTOS_TEST_VERSION=0.1",
                  f"-DTOS_TEST_ABSENT={';'.join(absent)}"])
        consumer("base-consumer", [], [*forbid([]), "-DTOS_TEST_ABSENT=app;crypto;http"])
        consumer("unknown-required", ["unknown"], expected="Unknown tos component")
        consumer("unknown-optional", [], ["-DTOS_TEST_OPTIONAL=unknown", *forbid([])])
        consumer("incompatible-version", [], ["-DTOS_TEST_VERSION=0.2"], expected="compatible")
        if absent:
            consumer("missing-required", [absent[0]], expected="was not installed")
            consumer("missing-optional", [], [f"-DTOS_TEST_OPTIONAL={';'.join(absent)}", *forbid([])])
        if name == "full":
            consumer("app-consumer", ["app"], [*forbid([]), "-DTOS_TEST_ABSENT=crypto;http"])
            consumer("deps-optional", [], [*forbid([]), "-DTOS_TEST_OPTIONAL=crypto;http"])
            consumer("deps-required", ["crypto"], [*forbid([])], expected="requires OpenSSL")
    print(f"All component checks passed: {', '.join(args.combinations)}", flush=True)


if __name__ == "__main__":
    main()
