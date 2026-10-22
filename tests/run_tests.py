# SPDX-License-Identifier: MIT
"""Reproducible native and cross-language suite. No Arduino hardware required."""
import argparse
import os
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
        simulator = build / ("simulator" + suffix)
        run(common + [ROOT / "src/LightYak.cpp", ROOT / "tests/simulator.cpp", "-o", simulator])
        run([sys.executable, ROOT / "tests/test_client.py", simulator])
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
        print("All suites passed: 3 work-budget configurations, client interoperability, and unsupported-type checks.")
    finally:
        if temp:
            temp.cleanup()


if __name__ == "__main__":
    main()
