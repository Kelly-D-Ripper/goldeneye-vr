"""Check the pack against docs/mod-system.md's format, and build its zip.

    python tools/validate_pack.py            check only
    python tools/validate_pack.py --zip OUT  check, write files.sha256, build OUT

The zip holds mod.json, scripts/, assets/ (when there is one), files.sha256
and the top-level .md/.txt files, as the installer takes them.
"""
import hashlib
import json
import re
import sys
import zipfile
from pathlib import Path

PACK = Path(__file__).resolve().parent.parent
DEVICES = {"con", "prn", "aux", "nul", *(f"com{i}" for i in range(1, 10)), *(f"lpt{i}" for i in range(1, 10))}


def shipped(pack):
    files = [pack / "mod.json"]
    files += sorted(p for p in (pack / "scripts").rglob("*") if p.is_file())
    if (pack / "assets").is_dir():
        files += sorted(p for p in (pack / "assets").rglob("*") if p.is_file())
    files += sorted(p for p in pack.iterdir() if p.is_file() and p.suffix in (".md", ".txt"))
    return files


def validate(pack=PACK):
    problems = []
    m = json.loads((pack / "mod.json").read_text())
    if m.get("format") != 1: problems.append("format must be 1")
    if not re.fullmatch(r"[a-z0-9](?:[a-z0-9-]{0,30}[a-z0-9])?", m.get("id", "")): problems.append("bad id")
    for k in ("title", "by"):
        v = m.get(k, "")
        if not v or len(v) > 48 or not v.isascii(): problems.append("bad " + k)
    if not m.get("version") or len(m["version"]) > 24 or " " in m["version"]: problems.append("bad version")
    if m.get("kind") not in ("campaign", "overlay"): problems.append("bad kind")
    if m.get("base") not in ("ge", "gf", "tnd", "mirror", "mario"): problems.append("bad base")
    if m.get("site") and not m["site"].startswith("https://"): problems.append("site must be https")
    if not (pack / "scripts" / "main.lua").is_file(): problems.append("no scripts/main.lua")
    seen = set()
    for f in shipped(pack):
        rel = f.relative_to(pack).as_posix()
        if rel.startswith("scripts/") and f.suffix != ".lua": problems.append("not .lua under scripts: " + rel)
        for part in rel.split("/"):
            if (not part.isascii() or any(c in part for c in '\\:*?"<>|') or part in (".", "..")
                    or part.endswith((".", " ")) or part.split(".")[0].lower() in DEVICES):
                problems.append("bad name: " + rel)
        if rel.lower() in seen: problems.append("names differ only in case: " + rel)
        seen.add(rel.lower())
    return problems


def build_zip(out, pack=PACK):
    files = shipped(pack)
    lines = []
    for f in files:
        lines.append(hashlib.sha256(f.read_bytes()).hexdigest() + "  " + f.relative_to(pack).as_posix())
    sums = "\n".join(lines) + "\n"
    (pack / "files.sha256").write_text(sums, encoding="ascii", newline="\n")
    m = json.loads((pack / "mod.json").read_text())
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for f in files:
            z.write(f, f.relative_to(pack).as_posix())
        z.writestr("files.sha256", sums)
    content_id = hashlib.sha256(sums.encode()).hexdigest()
    print(f"{out}: {m['id']} {m['version']}, {len(files)} files, content ID {content_id}")
    return content_id


if __name__ == "__main__":
    problems = validate()
    if problems:
        print("\n".join(problems))
        sys.exit(1)
    if len(sys.argv) > 2 and sys.argv[1] == "--zip":
        build_zip(Path(sys.argv[2]))
    else:
        print("pack OK")
