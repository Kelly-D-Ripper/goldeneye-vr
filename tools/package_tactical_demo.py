"""Package a fresh Windows x64 Tactical Demo using the normal VR build output."""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import zipfile


ROOT = Path(__file__).resolve().parent.parent
NAME = "GoldenEye-Tactical-0.03-PCVR"
EXE = "GoldenEyeVR-VR.exe"
EDITOR = "GoldenEye Tactical Loadout Editor.exe"
RUNTIME_ASSET_TYPES = {
    Path("."): {".bin"},
    Path("sounds"): {".wav"},
    Path("gear/meshes"): {".bin"},
    Path("gear/optics"): {".bin", ".opt"},
    Path("campaign"): {".bin"},
    Path("editor"): {".bin"},
}
GEAR_DATA = {Path("gear/catalog.txt"), Path("gear/presets.txt")}

README = """GoldenEye Tactical 0.03
Windows x64 PCVR, OpenXR, single player

1. Extract the entire ZIP to a folder you can write to.
2. Connect your headset and controllers and start your usual OpenXR runtime.
3. Double-click Play GoldenEye Tactical.exe. On the first run, select your
   GoldenEye 007 USA ROM (.z64, .v64 or .n64).
4. Choose Select Gear in a mission briefing or visit Workshop and its range.
   Missions that start without weapons take you straight into the mission.

Use Grip to pick up guns. Use the free hand's trigger to handle magazines
and work the action. Release a rifle at your right shoulder to sling it or
trade it for the slung rifle. Pistols stow at either hip. Use the menu button
to open the watch and choose mission gear.

GoldenEye Tactical Loadout Editor.exe runs on Windows without a headset.
It shares saved weapon presets with the in-game Workshop. Use it to fit
guard outfits and assign guard and weapon presets to missions.
"""


def write_text(path, text):
    path.write_text(text, encoding="utf-8", newline="\r\n")


def preset_blocks(path):
    blocks = {}
    with path.open(encoding="utf-8") as source:
        if source.readline().strip() != "GEVRPRESETS1":
            raise ValueError(f"Unexpected preset format in {path}")
        for header in source:
            if not header.strip():
                continue
            tag, root, count, name = header.rstrip("\r\n").split(" ", 3)
            if tag != "P":
                raise ValueError(f"Unexpected preset record in {path}: {header}")
            blocks[root, name.casefold()] = header + "".join(next(source) for _ in range(int(count)))
    return blocks


def bundle_presets(baseline, target):
    bundled = preset_blocks(target)
    campaign_path = ROOT / "src/game/gevr_tactical_loadouts.c"
    names = {
        name.casefold()
        for name in re.findall(r'\{TACTICAL_GUN_\w+(?: \+ 1)?, "([^"]+)"\}',
                               campaign_path.read_text(encoding="utf-8"))
        if name != "-"
    }
    local = baseline / "tactical-presets.txt"
    if local.is_file():
        bundled.update(preset_blocks(local))
    if missing := names - {key[1] for key in bundled}:
        raise ValueError(f"Missing campaign presets: {', '.join(sorted(missing))}")
    write_text(target, "GEVRPRESETS1\n" + "".join(bundled.values()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--objcopy", type=Path, required=True)
    parser.add_argument("--launcher-compiler", type=Path, required=True,
                        help="Windows x64 clang from llvm-mingw")
    parser.add_argument("--assets-from", type=Path, required=True,
                        help="test build containing the existing mods/tactical assets")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    baseline = args.assets_from.resolve()
    assets = baseline / "mods/tactical"
    for relative in ("ppk-black-suppressed.bin", "hand-pinch.bin", "gear/catalog.txt", "gear/presets.txt"):
        if not (assets / relative).is_file():
            parser.error(f"The Tactical asset pack is missing {assets / relative}")
    archive_dir = args.out.resolve()
    archive_dir.mkdir(parents=True, exist_ok=True)
    stage = archive_dir / NAME
    archive = archive_dir / (NAME + ".zip")
    if stage.exists() or archive.exists():
        parser.error(f"Use a fresh output location. {stage} or {archive} already exists.")

    game = stage / "Game"
    game.mkdir(parents=True)
    subprocess.run([str(args.objcopy.resolve()), "--strip-all", str(build / EXE),
                    str(game / EXE)], check=True)
    subprocess.run([str(args.launcher_compiler.resolve()), "-std=c11", "-Os", "-s",
                    "-municode", "-mwindows", "-Wall", "-Wextra",
                    str(ROOT / "tools/tactical_demo_launcher.c"), "-o", str(stage / "Play GoldenEye Tactical.exe"),
                    "-lcomdlg32", "-lshell32"], check=True)
    for dll in ("openxr_loader.dll", "phonon.dll"):
        shutil.copy2(build / dll, game / dll)

    for name in ("gevr_logo.rgba", "gevr_icon.rgba"):
        shutil.copy2(ROOT / "port" / "pc" / "package" / name, game / name)
    for name in (EDITOR, "TacticalWorkshopNative.dll"):
        shutil.copy2(baseline / name, stage / name)
    write_text(stage / "tactical-game-dir.txt", "Game\n")
    shutil.copy2(baseline / "tactical-editor-catalogue.tsv", game)
    editor_models = game / "loadout-models"
    editor_models.mkdir()
    for model in sorted((baseline / "loadout-models").iterdir()):
        if model.is_file() and re.fullmatch(r'(?:npc-\d+\.(?:bin|txt)|mission-(?:\d+|marker)\.bin)', model.name):
            shutil.copy2(model, editor_models)
    notices = {
        "GoldenEye VR license": ROOT / "LICENSE",
        "GoldenEye PC port MIT notice": ROOT / "docs/gepc-reference-LICENSE.txt",
        **{notice.name: notice for notice in sorted((ROOT / "port" / "licenses").glob("*.txt"))},
        "steam-audio.md": ROOT / "port/external/steamaudio/LICENSE.md",
        "libjuice.txt": ROOT / "port/external/libjuice/LICENSE",
        "HDiffPatch.txt": ROOT / "port/external/hdiffpatch/LICENSE",
        "fast3d.txt": ROOT / "port/fast3d/LICENSE.txt",
        "imgui.txt": ROOT / "port/vr/imgui/LICENSE.txt",
        "SDL2.txt": build / "_deps/sdl2-src/LICENSE.txt",
        "zlib.txt": build / "_deps/zlib-src/LICENSE",
        "opus.txt": build / "_deps/opus-src/COPYING",
    }
    sections = [f"----- {name} -----\n\n{source.read_text(encoding='utf-8')}"
                for name, source in notices.items()]
    enet_header = ROOT / "port/include/external/enet.h"
    enet_notice = enet_header.read_text(encoding="utf-8").split("*/", 1)[0] + "*/"
    sections.append(f"----- ENet -----\n\n{enet_notice}")
    write_text(game / "Software Notices.txt", "Software notices\n\n" + "\n\n".join(sections))

    write_text(stage / "Read Me.txt", README)
    write_text(game / "goldeneye-vr.ini", "[VR]\nFastReinforcements=1\n")
    aim_zero = baseline / "gevr_ak_zero.txt"
    if aim_zero.is_file():
        shutil.copy2(aim_zero, game / aim_zero.name)
    packed_assets = game / "mods/tactical"
    for asset in sorted(assets.rglob("*")):
        relative = asset.relative_to(assets)
        is_runtime_asset = asset.suffix in RUNTIME_ASSET_TYPES.get(relative.parent, ())
        if asset.is_file() and (is_runtime_asset or relative in GEAR_DATA):
            dest = packed_assets / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(asset, dest)
    bundle_presets(baseline, packed_assets / "gear/presets.txt")

    files = [path for path in sorted(stage.rglob("*")) if path.is_file()]

    with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as zipped:
        for path in files:
            zipped.write(path, path.relative_to(stage).as_posix())

    with zipfile.ZipFile(archive) as zipped:
        if bad_file := zipped.testzip():
            raise SystemExit(f"ZIP failed CRC verification for {bad_file}")
        for path in files:
            name = path.relative_to(stage).as_posix()
            with path.open("rb") as file:
                expected = hashlib.file_digest(file, "sha256").hexdigest()
            with zipped.open(name) as file:
                if hashlib.file_digest(file, "sha256").hexdigest() != expected:
                    raise SystemExit(f"Archive entry differs from the staged file at {name}")
    with archive.open("rb") as file:
        digest = hashlib.file_digest(file, "sha256").hexdigest()
    print(f"{archive}\n{archive.stat().st_size:,} bytes\n{len(files)} files verified\nSHA-256 {digest}")


if __name__ == "__main__":
    main()
