# SPDX-License-Identifier: MIT
"""Optional real-toolchain build matrix; installed cores required, no uploads."""
import argparse
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MATRIX = [
    ("uno-basic", "arduino:avr:uno", "examples/BasicTags"),
    ("uno-types", "arduino:avr:uno", "examples/AllScalarTypes"),
    ("uno-timing", "arduino:avr:uno", "examples/TimingProbe"),
    ("uno-capacity", "arduino:avr:uno", "tests/arduino/FullCapacity"),
    ("mega-basic", "arduino:avr:mega", "examples/BasicTags"),
    ("esp32-serial", "esp32:esp32:esp32", "examples/ESP32Serial"),
    ("esp32-timing", "esp32:esp32:esp32", "examples/ESP32Timing"),
    ("esp32s3-serial", "esp32:esp32:esp32s3", "examples/ESP32Serial"),
    ("esp32c3-serial", "esp32:esp32:esp32c3", "examples/ESP32Serial"),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", default="arduino-cli")
    parser.add_argument("--config-file", type=Path)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--target", action="append", choices=[x[0] for x in MATRIX])
    args = parser.parse_args()
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    results = []
    for name, fqbn, sketch in MATRIX:
        if args.target and name not in args.target:
            continue
        command = [args.cli]
        if args.config_file:
            command += ["--config-file", str(args.config_file.resolve())]
        command += ["compile", "--jobs", "2", "--fqbn", fqbn, "--library", str(ROOT),
                    "--build-path", str(build / name), str(ROOT / sketch)]
        print("Building", name, fqbn, flush=True)
        result = subprocess.run(command, capture_output=True, text=True, timeout=600)
        output = result.stdout + result.stderr
        (build / (name + ".log")).write_text(output)
        print(output, flush=True)
        if result.returncode:
            raise SystemExit(result.returncode)
        flash = re.search(r"Sketch uses ([\d,]+) bytes", output)
        ram = re.search(r"Global variables use ([\d,]+) bytes", output)
        available = re.search(r"leaving ([\d,]+) bytes", output)
        entry = {"target": name, "fqbn": fqbn, "sketch": sketch}
        for field, match in (("flash", flash), ("static_ram", ram), ("remaining_ram", available)):
            if match:
                entry[field] = int(match[1].replace(",", ""))
        results.append(entry)
        (build / "results.json").write_text(json.dumps(results, indent=2))
    print("Build checks passed:", len(results), flush=True)


if __name__ == "__main__":
    main()
