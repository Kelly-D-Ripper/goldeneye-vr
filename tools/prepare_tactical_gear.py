"""Extract the two Tactical AKs' compatible part graph from the local Zomboid archive.

Reads the generated Lua tables as data. The output directory contains private
source meshes, a Blender conversion manifest, and the game's catalog.
"""
import argparse
import json
import re
import unicodedata
from pathlib import Path
from zipfile import ZipFile


ROOTS = {
    "AK74N": "5644bd2b4bdc2d3b4c8b4572",
    "AKS74UN": "583990e32459771419544dd2",
}
ITEM = re.compile(r'\["([0-9a-f]{24})"\]=\{')
IDENTIFIER = re.compile(r'"([0-9a-f]{24})"')
FRAME = re.compile(r'(?:\["([^"]+)"\]|(\w+))=\{\{([^{}]+)\},\{([^{}]+)\},\{([^{}]+)\}\}')
SLOT = re.compile(r'\{allowed=(\d+),name="([^"]+)",required=(true|false)\}')
MODEL = re.compile(r'\bmodel\s+M_([0-9a-f]{24})(?:_P00)?\s*\{([^{}]*)')


def table_body(text, start):
    """Read a generated table without treating braces in strings as tokens."""
    depth, quoted, escaped = 1, False, False
    for end in range(start, len(text)):
        char = text[end]
        if quoted:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
        elif char == '"':
            quoted = True
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if not depth:
                return text[start:end]
    raise ValueError("Unclosed table in generated data")


def frames(item):
    result = {}
    start = item.find("frames={")
    if start >= 0:
        for match in FRAME.finditer(table_body(item, start + 8)):
            result[match[1] or match[2]] = [float(n) for row in match.group(3, 4, 5) for n in row.split(",")]
    return result


def fallback_frame(item, name):
    """Some catalog sockets have part filters but no source preview transform."""
    box = re.search(r"bounds=\{max=\{([^{}]+)\},min=\{([^{}]+)\}\}", item)
    if not box:
        raise ValueError(f"Missing bounds for {name}")
    hi, lo = ([float(n) for n in row.split(",")] for row in box.groups())
    y = hi[1] if name == "mod_muzzle" else (lo[1] + hi[1]) * .5
    return [1, 0, 0, 0, 0, 1, 0, y, 0, 0, 1, hi[2]]


def ascii_label(value):
    return unicodedata.normalize("NFKD", value).encode("ascii", "ignore").decode().replace("\t", " ").replace("\n", " ")


def extract_parts(archive, output, initial, include_children=True):
    output.mkdir(parents=True, exist_ok=True)
    with ZipFile(archive) as pack:
        names = sorted(pack.namelist())
        data = next(n for n in names if n.endswith("/42/media/lua/shared/TW/Data.lua"))
        prefix = data.split("/lua/")[0] + "/"
        names = [n for n in names if n.startswith(prefix)]
        items, filters = {}, [[]]  # Lua's allowed indices start at one.
        for name in names:
            if re.search(r"/TW/Data/Items\d+\.lua$", name):
                content = pack.read(name).decode("utf-8")
                for match in ITEM.finditer(content):
                    items[match[1]] = table_body(content, match.end())
            elif re.search(r"/TW/Data/Filters\d+\.lua$", name):
                filters.extend(IDENTIFIER.findall(row) for row in
                               re.findall(r"\{([^{}]*)\}", pack.read(name).decode("utf-8")))
        ui = json.loads(pack.read(prefix + "lua/shared/Translate/EN/IG_UI.json").decode("utf-8-sig"))
        item_names = json.loads(pack.read(prefix + "lua/shared/Translate/EN/ItemName.json").decode("utf-8-sig"))
        models = {}
        for match in MODEL.finditer(pack.read(prefix + "scripts/TW_Models.txt").decode("utf-8-sig")):
            fields = dict(re.findall(r"^\s*(mesh|texture)\s*=\s*([^,\r\n]+)", match[2], re.M))
            if "mesh" in fields and "texture" in fields:
                models.setdefault(match[1], ("models_X/" + fields["mesh"].strip() + ".fbx",
                                             "textures/" + fields["texture"].strip() + ".png"))

        ready, seen, parts = list(initial), set(initial), []
        approximate = 0
        for part_id in ready:
            item = items[part_id]
            options = []
            sockets = frames(item)
            for match in SLOT.finditer(item):
                allowed, name, required = int(match[1]), match[2], match[3] == "true"
                frame = sockets.get(name)
                if frame is None:
                    frame = fallback_frame(item, name)
                    approximate += 1
                compatible = [candidate for candidate in filters[allowed] if candidate in items]
                for candidate in compatible:
                    if include_children and candidate not in seen:
                        ready.append(candidate)
                        seen.add(candidate)
                options.append({"name": name, "required": required,
                    "label": ascii_label(ui.get("IGUI_TW_SlotShort_" + name, name.removeprefix("mod_").replace("_", " ").upper())),
                    "frame": frame, "parts": compatible})

            label = ui.get("IGUI_TW_Short_" + part_id) or item_names.get("EscapeFromZomboidFirearms.P_" + part_id) or part_id
            capacity = re.search(r"\bcapacity=(\d+)", item)
            conflicts = re.search(r"\bConflictingItems=\{([^{}]*)\}", item)
            mesh, texture = models.get(part_id, ("", ""))
            if mesh and all(prefix + path in pack.NameToInfo for path in (mesh, texture)):
                for path in (mesh, texture):
                    target = output / path
                    if not target.exists():
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(pack.read(prefix + path))
            else:
                mesh = texture = ""
            parts.append({"id": part_id, "name": ascii_label(label),
                          "capacity": int(capacity[1]) if capacity else 0,
                          "mesh": mesh, "texture": texture, "sockets": options,
                          "conflicts": IDENTIFIER.findall(conflicts[1]) if conflicts else []})

    return parts, approximate


def prepare(archive, assemblies, output):
    parts, approximate = extract_parts(archive, output, ROOTS.values())
    defaults = {key: {p["path"]: p["id"] for p in assemblies[key]["parts"]
                      if not p["path"].startswith("root")} for key in ROOTS}
    indices = {part["id"]: i for i, part in enumerate(parts)}
    lines = ["GEVRGEAR2\n", f"{len(parts)}\n"]
    for part in parts:
        lines.append(f"{part['id']}\t{part['capacity']}\t{int(bool(part['mesh']))}\t{part['name']}\n")
    for part in parts:
        conflicts = [indices[other] for other in part["conflicts"] if other in indices]
        lines.append(" ".join(map(str, [len(conflicts), *conflicts])) + "\n")
        lines.append(f"{len(part['sockets'])}\n")
        for socket in part["sockets"]:
            frame = " ".join(f"{n:.9g}" for n in socket["frame"])
            allowed = " ".join(str(indices[p]) for p in socket["parts"])
            lines.append(f"{socket['name']}\t{socket['label']}\t{int(socket['required'])}\t{len(socket['parts'])}\t{frame}\t{allowed}\n")
    for key, root in ROOTS.items():
        lines.append(f"{indices[root]} {len(defaults[key])}\n")
        for path, part in defaults[key].items():
            lines.append(f"{path} {indices[part]}\n")
    (output / "catalog.txt").write_text("".join(lines), encoding="ascii", newline="\n")
    (output / "parts.json").write_text(json.dumps(parts, indent=2), encoding="utf-8")
    missing = [p for p in parts if not p["mesh"]]
    print(f"{len(parts)} entries, {sum(len(p['sockets']) for p in parts)} slots, "
          f"{len(missing)} missing meshes, {approximate} preview frames inferred from mesh bounds")
    for part in missing:
        print("Missing mesh:", part["name"], part["id"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("assembly_manifest", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    prepare(args.archive, json.loads(args.assembly_manifest.read_text(encoding="utf-8")), args.output)
