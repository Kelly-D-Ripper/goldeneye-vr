"""Bake local 3D NPC previews using the installed ROM and desktop renderer."""

from argparse import ArgumentParser
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time


def capture(candidate, executable, work, body):
    directory = work / str(body)
    directory.mkdir(parents=True, exist_ok=True)
    for name in ("ge.z64", "phonon.dll", "openxr_loader.dll", "gevr_icon.rgba", "gevr_logo.rgba"):
        link = directory / name
        if not link.exists():
            os.link(candidate / name, link)
    # Reuse checked canonical assets and create private writable config and save files.
    image = directory / executable.name
    if not image.exists() or not os.path.samefile(executable, image):
        image.unlink(missing_ok=True)
        os.link(executable, image)
    if not (directory / "mods").exists():
        subprocess.run(["cmd.exe", "/d", "/c", "mklink", "/J", str(directory / "mods"), str(candidate / "mods")],
                       stdout=subprocess.DEVNULL, check=True)
    for name in ("goldeneye.ini", "goldeneye-vr.ini", "gevr_player.json", "eeprom.bin"):
        shutil.copy2(candidate / name, directory)
    for name in ("gevr.log", "npc-preview.bin", "npc-preview-fit.txt", "gevr_cheat.txt"):
        (directory / name).unlink(missing_ok=True)
    (directory / "capture.txt").write_text("950 key Space 125\n4980 key E 125\n9200 quit\n", encoding="ascii")
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    args = [str(image), "--no-launcher", "--boot-stage", "33", "--input-script", "capture.txt", "--log",
            "--tactical-editor-export-npc", str(body)]
    process = subprocess.Popen(args, cwd=directory, env=dict(os.environ, SDL_AUDIODRIVER="dummy"),
                               startupinfo=startup, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        log = directory / "gevr.log"
        mesh = directory / "npc-preview.bin"
        deadline = time.monotonic() + 15
        sent = False
        while process.poll() is None and time.monotonic() < deadline:
            contents = log.read_text(errors="replace") if log.exists() else ""
            if "loadout editor preview: exported " in contents:
                packed = mesh.read_bytes()
                if not packed.startswith(b"GEVRWPN1") or struct.unpack_from("<I", packed, 8)[0] < 2:
                    raise ValueError(f"NPC {body} capture has no material")
                out = candidate / "loadout-models"
                out.mkdir(exist_ok=True)
                (out / f"npc-{body}.bin").write_bytes(packed)
                shutil.copy2(directory / "npc-preview-fit.txt", out / f"npc-{body}.txt")
                print(f"NPC {body:2d}  {len(packed):7d} bytes", flush=True)
                return
            if not sent and "intro-mode: 0 -> 4" in contents:
                (directory / "gevr_cheat.txt").write_text("10 invincible spawn 1", encoding="ascii")
                sent = True
            time.sleep(.04)
        raise RuntimeError(f"NPC {body} failed to export. See {log}")
    finally:
        if process.poll() is None:
            process.terminate()
        process.wait(timeout=5)


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("executable", type=Path)
    parser.add_argument("work", type=Path)
    parser.add_argument("body", nargs="*", type=int)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if not args.body:
        rows = (args.candidate / "tactical-editor-catalogue.tsv").read_text().splitlines()
        args.body = [int(row.split("\t")[1]) for row in rows if row.startswith("N\t")]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(lambda body: capture(args.candidate, args.executable, args.work, body), args.body))


if __name__ == "__main__":
    main()
