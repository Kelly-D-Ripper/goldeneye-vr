"""Run the production cover decisions against a deterministic test room."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_reinforcements import block


ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC"))
    args = parser.parse_args()
    bundled = ROOT.parents[2] / "tools/llvm-mingw-20260922-ucrt-x86_64/bin/clang.exe"
    cc = args.cc or shutil.which("clang") or shutil.which("gcc")
    if not cc and bundled.is_file():
        cc = str(bundled)
    if not cc:
        parser.error("a C compiler is required; supply --cc")

    ai = (ROOT / "src/game/gevr_tactical_guard_ai.inc.h").read_text(encoding="utf-8")
    types = ai[ai.index("enum TacticalContact"):ai.index("typedef struct TacticalGunNoise")]
    geometry = "\n".join(block(ai, name) for name in (
        "static f32 tacticalDistance(", "static bool tacticalTrace(",
        "static bool tacticalSightLine(", "static f32 tacticalTargetHeight(",
    ))
    fixture = (ROOT / "port/tests/tactical_cover_native.c").read_text(encoding="utf-8")
    fixture = fixture.replace("/* PRODUCTION_TYPES */", types).replace("/* PRODUCTION_TRACE */", geometry)
    with tempfile.TemporaryDirectory(prefix="gevr-tactical-cover-") as work:
        work = Path(work)
        source, exe = work / "cover.c", work / "cover.exe"
        source.write_text(fixture, encoding="utf-8")
        flags = ["-std=c11", "-O2", "-fms-extensions"]
        flags += [f"-D{name}" for name in ("GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1", "VERSION_US=1")]
        flags += [f"-I{ROOT / path}" for path in (".", "include", "src", "src/game", "port/include")]
        compile_result = subprocess.run([cc, *flags, str(source), "-o", str(exe), "-lm"],
                                        capture_output=True, text=True)
        if compile_result.returncode:
            raise RuntimeError(compile_result.stderr)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
