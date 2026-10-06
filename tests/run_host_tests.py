"""Compile and run the dependency-free C++ core tests; run in WSL/Linux with g++."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / ".build"
BUILD.mkdir(exist_ok=True)
compiler = shutil.which("g++") or shutil.which("clang++")
if not compiler:
    raise SystemExit("Install g++ or clang++, or run this script in WSL.")
sources = [ROOT / "components/casita_status/casita_core.cpp", ROOT / "tests/main.cpp"]
for suite in ("mapping", "lookups", "state", "renderer"):
    sources.extend(sorted((ROOT / "tests" / suite).glob("*.cpp")))
binary = BUILD / "host-tests"
subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2",
                "-I", str(ROOT / "components/casita_status"), "-I", str(ROOT / "tests"),
                *map(str, sources), "-o", str(binary)], check=True)
subprocess.run([str(binary)], check=True)
