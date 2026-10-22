# SPDX-License-Identifier: MIT
"""Reproducible native and cross-language suite. No Arduino hardware required."""
import argparse
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(args, **kwargs):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, timeout=120, **kwargs)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--sanitize", action="store_true", help="Requires compiler ASan/UBSan runtimes")
    args = parser.parse_args()
    if not shutil.which(args.compiler):
        parser.error("A C++11-capable g++/clang++ compiler is required")
    temp = tempfile.TemporaryDirectory(prefix="lightyak-tests-") if not args.build_dir else None
    build = args.build_dir.resolve() if args.build_dir else Path(temp.name)
    build.mkdir(parents=True, exist_ok=True)
    suffix = ".exe" if os.name == "nt" else ""
    common = [args.compiler, "-std=c++11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
              "-O2", "-g", "-I", ROOT / "src"]
    if args.sanitize:
        common += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    try:
        for budget in (4, 1, 8):
            executable = build / (f"test_lightyak_{budget}" + suffix)
            run(common + [f"-DLIGHTYAK_RX_BUDGET={budget}", f"-DLIGHTYAK_TX_BUDGET={budget}",
                          f"-DLIGHTYAK_PREP_BUDGET={budget}", ROOT / "src/LightYak.cpp",
                          ROOT / "tests/test_lightyak.cpp", "-o", executable])
            run([executable])
        # Compare against the original capacity with identical default work budgets.
        baseline = build / ("test_capacity_24" + suffix)
        run(common + ["-DLIGHTYAK_MAX_TAGS=24", ROOT / "src/LightYak.cpp",
                      ROOT / "tests/test_lightyak.cpp", "-o", baseline])
        run([baseline])
        # Exercise the normal user workflow: edit ONLY the shared header, then
        # compile sketch/client and implementation as distinct translation units.
        copied_src = build / "shared-config-src"
        copied_src.mkdir(exist_ok=True)
        for name in ("LightYak.h", "LightYak.cpp", "LightYakConfig.h"):
            shutil.copyfile(ROOT / "src" / name, copied_src / name)
        config = copied_src / "LightYakConfig.h"
        text = config.read_text()
        text, replacements = re.subn(r"(#define LIGHTYAK_MAX_TAGS )\d+", r"\g<1>64", text)
        if replacements != 1:
            raise RuntimeError("Expected exactly one shared tag-capacity default")
        config.write_text(text)
        configured_common = [copied_src if arg == ROOT / "src" else arg for arg in common]
        configured = build / ("test_shared_config_64" + suffix)
        run(configured_common + [copied_src / "LightYak.cpp", ROOT / "tests/test_lightyak.cpp",
                                 "-o", configured])
        run([configured])
        simulator = build / ("simulator" + suffix)
        run(common + [ROOT / "src/LightYak.cpp", ROOT / "tests/simulator.cpp", "-o", simulator])
        run([sys.executable, ROOT / "tests/test_client.py", simulator])
        # Compile and exercise the real ESP32 code paths against small IDF/HAL
        # fakes. Blocking TX APIs are deliberately absent from their headers.
        esp_common = common + ["-DARDUINO=10819", "-DARDUINO_ARCH_ESP32",
                               "-I", ROOT / "tests/esp32_stubs"]
        esp_sources = [ROOT / "src/LightYak.cpp", ROOT / "src/LightYakEsp32.cpp",
                       ROOT / "tests/esp32_stubs/Esp32Mock.cpp"]
        for budget in (4, 1, 8):
            flags = [f"-DLIGHTYAK_RX_BUDGET={budget}", f"-DLIGHTYAK_TX_BUDGET={budget}",
                     f"-DLIGHTYAK_PREP_BUDGET={budget}"]
            executable = build / (f"test_esp32_core_{budget}" + suffix)
            run(esp_common + flags + esp_sources + [ROOT / "tests/test_lightyak.cpp", "-o", executable])
            run([executable])
        esp_adapter = build / ("test_esp32_adapter" + suffix)
        run(esp_common + esp_sources + [ROOT / "tests/test_esp32.cpp", "-o", esp_adapter])
        run([esp_adapter])
        esp_simulator = build / ("simulator_esp32" + suffix)
        run(esp_common + esp_sources + [ROOT / "tests/simulator_esp32.cpp", "-o", esp_simulator])
        run([sys.executable, ROOT / "tests/test_client.py", esp_simulator])
        # Prevent accidentally using HardwareSerial/USB streams with waiting HAL
        # locks, and prevent silently registering native 64-bit double values.
        for name, body, diagnostic in [
            ("esp32_hardware_serial", 'd.Listen(Serial, 115200, SERIAL_8N1, "Bad");', "Use lightyak::Esp32Uart"),
            ("esp32_native_double", 'double value=1.0; d.AddTag(value, "Bad");', "LightYak supports mutable"),
        ]:
            source = build / (name + ".cpp")
            source.write_text('#include "LightYak.h"\nvoid test() { lightyak::Device d; ' + body + ' }\n')
            result = subprocess.run(list(map(str, esp_common + ["-fsyntax-only", source])),
                                    capture_output=True, text=True, timeout=30)
            if result.returncode == 0 or diagnostic not in result.stderr:
                raise RuntimeError("Expected ESP32 rejection: " + name + "\n" + result.stderr)
            print("PASS ESP32 compile-time rejection:", name, flush=True)
        for name, declaration, expression in [
            ("character_buffer", 'char value[8] = "hello";', "value"),
            ("const_scalar", "const int value = 1;", "value"),
            ("custom_struct", "struct Custom { int a; }; Custom value = {1};", "value"),
            ("wide_integer", "long long value = 1;", "value"),
        ]:
            source = build / (name + ".cpp")
            source.write_text('#include "LightYak.h"\nvoid test() { lightyak::Device d; '
                              + declaration + ' d.AddTag(' + expression + ', "Bad"); }\n')
            result = subprocess.run(list(map(str, common + ["-fsyntax-only", source])),
                                    capture_output=True, text=True, timeout=30)
            if result.returncode == 0 or "LightYak supports mutable" not in result.stderr:
                raise RuntimeError("Expected clear compile-time rejection: " + name + "\n" + result.stderr)
            print("PASS compile-time rejection:", name, flush=True)
        for flag, diagnostic in [
            ("LIGHTYAK_MAX_TAGS=0", "tag capacity must be 1..255"),
            ("LIGHTYAK_MAX_TAGS=256", "tag capacity must be 1..255"),
            ("LIGHTYAK_RX_BUDGET=0", "RX budget must be 1..32"),
            ("LIGHTYAK_TX_BUDGET=33", "TX budget must be 1..32"),
            ("LIGHTYAK_PREP_BUDGET=17", "prepare budget must be 1..16"),
        ]:
            source = build / "invalid_config.cpp"
            source.write_text('#include "LightYak.h"\n')
            result = subprocess.run(list(map(str, common + ["-D" + flag, "-fsyntax-only", source])),
                                    capture_output=True, text=True, timeout=30)
            if result.returncode == 0 or diagnostic not in result.stderr:
                raise RuntimeError("Expected configuration rejection: " + flag + "\n" + result.stderr)
            print("PASS invalid configuration:", flag, flush=True)
        print("All suites passed: native/ESP32 work budgets, shared configuration, both client simulators, ESP32 UART adapter, and compile-time checks.")
    finally:
        if temp:
            temp.cleanup()


if __name__ == "__main__":
    main()
