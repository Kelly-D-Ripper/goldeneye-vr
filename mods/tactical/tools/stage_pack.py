"""Stage the installable Tactical pack and build its zip.

    python tools/stage_pack.py <native gear/meshes folder> <stage folder> [zip]

Copies this pack (mod.json, scripts/, the top-level .md/.txt) and the native
meshes listed in tools/assets-manifest.txt (generate_assemblies.py) into
<stage>/assets/models/, unchanged. Each mesh gets <file>.opaque listing all
its pictures: the native renderer never treats texture alpha as opacity, only
corner alpha (gevr_tactical_mesh.c draws untinted pictures in the opaque
pass), and docs/mod-system.md's .opaque list gives C10 the same rule.

The meshes are Tactical's assets and stay out of Git; this tool reads them
from the native build's folder. With [zip], it also writes files.sha256 and
the zip the launcher installs (validate_pack.build_zip).
"""
import shutil
import sys
from pathlib import Path

PACK = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PACK / "tools"))
from validate_pack import build_zip, shipped, validate  # noqa: E402


def main():
    meshes, stage = Path(sys.argv[1]), Path(sys.argv[2])
    out = Path(sys.argv[3]) if len(sys.argv) > 3 else None
    problems = validate(PACK)
    if problems:
        raise SystemExit("\n".join(problems))
    if stage.exists():
        shutil.rmtree(stage)
    for f in shipped(PACK):
        dest = stage / f.relative_to(PACK)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(f, dest)
    models = stage / "assets" / "models"
    models.mkdir(parents=True, exist_ok=True)
    total = 0
    for line in (PACK / "tools" / "assets-manifest.txt").read_text().splitlines():
        name, pictures = line.split()
        shutil.copyfile(meshes / name, models / name)
        total += (models / name).stat().st_size
        (models / (name + ".opaque")).write_text(" ".join(str(i) for i in range(int(pictures))) + "\n",
                                                  encoding="ascii", newline="\n")
    print(f"staged {stage}: {len(list(models.glob('*.bin')))} meshes, {total / 2**20:.0f} MiB")
    problems = validate(stage)
    if problems:
        raise SystemExit("\n".join(problems))
    if out:
        build_zip(out, stage)


if __name__ == "__main__":
    main()
