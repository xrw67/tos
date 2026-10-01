#!/usr/bin/env python3
"""Run a command without a shell, preserving its output and failure status."""
from pathlib import Path
import subprocess
import sys


def main():
    if len(sys.argv) < 3:
        raise SystemExit("usage: run_logged.py LOG COMMAND [ARG ...]")
    log = Path(sys.argv[1])
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w") as output:
        with subprocess.Popen(sys.argv[2:], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True) as child:
            for line in child.stdout:
                output.write(line)
                output.flush()
                print(line, end="", flush=True)
            raise SystemExit(child.wait())


if __name__ == "__main__":
    main()
