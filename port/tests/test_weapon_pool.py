"""Exercise the production weapon allocator, game-mode gate and network indices."""

import argparse
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from test_reinforcements import block


ROOT = Path(__file__).resolve().parents[2]
SECTIONS = (
    ("port/src/net/net_core.c", ("bool netIsActive(void)", "int netCoopSession(void)",
        "int gevrTacticalCampaignRules(void)", "static bool netSnapshotObjectType(",
        "static ObjectRecord *netObjectByIndex(", "static int netObjectIndex(")),
    ("src/game/model.c", ("bool modelmgrCanSlotFitRwdata(", "void modelInit(",
        "Model *modelmgrInstantiateModel(", "void clear_model_obj(")),
    ("src/game/initunk_005520.c", ("#define MODEL_SPARE_RWDATALEN", "void modelmgrResetSlotCounts(",
        "void modelmgrAllocateModelSlots(")),
    ("src/game/loadobjectmodel.c", ("ObjectRecord *setupFindObjForReuse(",)),
    ("src/game/propobj.c", ("s32 weaponGetSlotCapacity(void)", "WeaponObjRecord* weaponCreate(")),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC") or shutil.which("clang") or shutil.which("gcc"))
    args = parser.parse_args()
    if not args.cc:
        parser.error("set --cc or CC to a C compiler supporting -fms-extensions")

    sections = []
    for filename, signatures in SECTIONS:
        source = (ROOT / filename).read_text(encoding="utf-8")
        for signature in signatures:
            if signature.startswith("#define "):
                sections.append(next(line for line in source.splitlines() if line.startswith(signature)))
            else:
                sections.append(block(source, signature))

    fixture = (ROOT / "port/tests/weapon_pool_native.c").read_text(encoding="utf-8")
    flags = ["-std=c11", "-O2", "-fms-extensions", "-Wno-missing-braces"]
    flags += ["-D" + value for value in ("GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1", "VERSION_US=1")]
    flags += ["-I" + str(ROOT / path) for path in (".", "include", "src", "src/game", "port/include", "port/src/net")]
    with tempfile.TemporaryDirectory(prefix="gevr-weapon-pool-") as folder:
        source = Path(folder) / "weapon_pool.c"
        exe = source.with_suffix(".exe" if os.name == "nt" else "")
        source.write_text(fixture.replace("/* PRODUCTION */", "\n\n".join(sections)), encoding="utf-8")
        build = subprocess.run([args.cc, *flags, str(source), "-o", str(exe)], capture_output=True, text=True)
        if build.returncode:
            raise RuntimeError(build.stdout + build.stderr)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
