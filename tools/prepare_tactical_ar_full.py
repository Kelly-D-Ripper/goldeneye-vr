"""Add the full authored attachment trees for the Tactical AR rifles."""
import argparse
import json
from pathlib import Path
import shutil
from zipfile import ZipFile

from prepare_tactical_ar_rifles import IDENTITY, MAGAZINES, MUZZLES, RIFLES
from prepare_tactical_gear import extract_parts
from prepare_tactical_roster import stable_id


def extract(archive, work, baseline):
    roots = [rifle["root"] for rifle in RIFLES.values()]
    graph, approximated = extract_parts(archive, work, roots)
    parts = {part["id"]: part for part in graph}
    baked = work / "meshes"
    baked.mkdir(parents=True, exist_ok=True)
    with ZipFile(archive) as pack:
        names = tuple(pack.namelist())
        marker = "/42/media/lua/shared/Translate/EN/ItemName.json"
        translations = next(name for name in names if name.endswith(marker))
        full_names = json.loads(pack.read(translations).decode("utf-8-sig"))
        for root in roots:
            part = parts[root]
            for variant in ("body", "weapon_bolt"):
                relative = f"models_X/TW/Moving/{root}_{variant}.fbx"
                member = next(name for name in names if name.endswith("/42/media/" + relative))
                target = work / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(pack.read(member))
                if variant == "body":
                    part["mesh"] = relative
                else:
                    bolt = dict(part, id=stable_id(root + ":bolt"), name="Bolt", mesh=relative,
                                capacity=0, sockets=[], conflicts=[])
                    graph.append(bolt)
    for part in graph:
        ident = part["id"]
        name = full_names.get("EscapeFromZomboidFirearms.P_" + ident, part["name"])
        lowered = name.lower()
        part["quiet"] = "sound suppressor" in lowered or ("silencer" in lowered.split() and "adapter" not in lowered)
        target = baked / f"{ident}.bin"
        existing = baseline / "meshes" / target.name
        original = ident in roots
        if existing.is_file() and not original and not target.exists():
            shutil.copyfile(existing, target)
        if original:
            part.update(texture_size=384, triangle_limit=5400)
        else:
            part.update(texture_size=256, triangle_limit=3600)
    (work / "parts.json").write_text(json.dumps(graph, indent=2), encoding="utf-8")
    missing = [part for part in graph if not part["mesh"] and not (baked / (part["id"] + ".bin")).exists()]
    print(f"{len(graph)} reachable parts, {len(missing)} missing source meshes, {approximated} fallback socket frames")
    for part in missing:
        print(f"Missing mesh {part['id']} {part['name']}")
    print(f"{len(list(baked.glob('*.bin')))} parts already converted in the base library")


def read_catalog(path):
    lines = path.read_text(encoding="ascii").splitlines()
    if lines[0] != "GEVRGEAR3":
        raise ValueError("Use the preceding keyed AR catalog")
    count = int(lines[1])
    rows, sockets, builds = lines[2:2 + count], [], {}
    pos = 2 + count
    for _ in rows:
        end = pos + 2 + int(lines[pos + 1])
        sockets.append(lines[pos:end])
        pos = end
    while pos < len(lines):
        key, root, count = map(int, lines[pos].split())
        end = pos + 1 + count
        builds[key] = root, lines[pos + 1:end]
        pos = end
    return rows, sockets, builds


def rifle_defaults(rifle):
    return {
        "mod_pistol_grip": rifle["grip"],
        "mod_magazine": MAGAZINES[0],
        "mod_reciever": rifle["upper"],
        "mod_reciever/mod_handguard": rifle["handguards"][0],
        "mod_reciever/mod_barrel": rifle["barrel"],
        "mod_reciever/mod_barrel/mod_gas_block": rifle["gas"],
        "mod_reciever/mod_barrel/mod_muzzle": MUZZLES[0],
        "mod_reciever/mod_sight_rear": rifle["rear"],
        "mod_stock": rifle["tube"],
        "mod_stock/mod_stock_000": rifle["stock"],
        "mod_charge": rifle["charge"],
        "mod_bolt": stable_id(rifle["root"] + ":bolt"),
    }


def reachable(graph, root):
    seen, pending = set(), [root]
    while pending:
        ident = pending.pop()
        if ident in seen:
            continue
        seen.add(ident)
        pending.extend(part for socket in graph[ident]["sockets"] for part in socket["parts"])
    return seen


def order_defaults(graph, root, choices):
    """Reject a default that is unavailable in the pack's nested graph."""
    for full_path, ident in choices.items():
        part, prefix = root, ""
        for key in full_path.split("/"):
            prefix = "/".join(filter(None, (prefix, key)))
            slot = next(slot for slot in graph[part]["sockets"] if slot["name"] == key)
            selected = choices[prefix]
            if selected not in slot["parts"]:
                raise ValueError(f"{selected} is unavailable at {full_path}")
            part = selected
        if part != ident:
            raise ValueError(f"{full_path} resolved to {part}, expected {ident}")
    return choices


def migrate_presets(baseline, destination, mapped):
    source = baseline.parents[2] / "tactical-presets.txt"
    if not source.is_file():
        return 0
    lines = source.read_text(encoding="ascii").splitlines()
    if lines[0] != "GEVRPRESETS1":
        raise ValueError("Unrecognised preset format")
    rifles = {stable_id(rifle["root"] + ":workshop chassis"): rifle for rifle in RIFLES.values()}
    target = destination.parents[2] / source.name
    expanded = [lines[0]]
    paths = {"mod_muzzle": "mod_reciever/mod_barrel/mod_muzzle",
             "mod_handguard": "mod_reciever/mod_handguard",
             "mod_stock": "mod_stock/mod_stock_000"}
    pos, migrated = 1, 0
    while pos < len(lines):
        tag, root, count, name = lines[pos].split(maxsplit=3)
        count = int(count)
        raw_choices = lines[pos + 1:pos + 1 + count]
        pos += 1 + count
        if root not in rifles:
            expanded.extend([f"{tag} {root} {count} {name}", *raw_choices])
            continue
        rifle = rifles[root]
        choices = {}
        for path, part in (row.split() for row in raw_choices):
            if path == "mod_scope":
                if part == stable_id(rifle["rear"] + ":receiver rail"):
                    path, part = "mod_reciever/mod_sight_rear", rifle["rear"]
                else:
                    path = "mod_reciever/mod_scope"
                    if rifle["name"] == "M4A1":
                        choices["mod_reciever/mod_sight_rear"] = "-"
            choices[paths.get(path, path)] = mapped.get(part, part)
        expanded.append(f"{tag} {root} {len(choices)} {name}")
        expanded.extend(f"{path} {part}" for path, part in choices.items())
        migrated += 1
    target.write_text("\n".join(expanded) + "\n", encoding="ascii", newline="\n")
    return migrated


def assemble(work, baseline, destination):
    rows, sockets, builds = read_catalog(baseline / "catalog.txt")
    indices = {row.split("\t", 1)[0]: i for i, row in enumerate(rows)}
    graph = {part["id"]: part for part in json.loads((work / "parts.json").read_text(encoding="utf-8"))}
    root_ids = {rifle["root"]: stable_id(rifle["root"] + ":workshop chassis") for rifle in RIFLES.values()}
    restraint = RIFLES[16]["rear"]
    mapped = {}
    for ident, part in graph.items():
        existing = indices.get(ident)
        if ident in root_ids:
            mapped[ident] = root_ids[ident]
        elif existing is not None and (part["sockets"] or part["conflicts"] or part.get("quiet")
                                       or ident == restraint or len(sockets[existing]) > 2):
            mapped[ident] = stable_id(ident + ":full AR")
        else:
            mapped[ident] = ident
    for root in root_ids:
        bolt = stable_id(root + ":bolt")
        graph[root]["sockets"].append(dict(name="mod_bolt", label="Bolt", required=True, frame=IDENTITY, parts=[bolt]))
    original_count = len(rows)
    for ident in graph:
        target = mapped[ident]
        if target not in indices:
            indices[target] = len(rows)
            rows.append("")
            sockets.append([])
    if len(rows) > 1024:
        raise ValueError(f"Catalog requires {len(rows)} parts; engine limit is 1024")

    for ident, part in graph.items():
        target = mapped[ident]
        index = indices[target]
        if index < original_count and ident not in root_ids:
            continue
        flag = 1 | (2 if part.get("quiet") else 0) | (4 if ident == restraint else 0)
        name = "Carry handle" if ident == restraint else part["name"]
        if ident in root_ids:
            name = next(rifle["name"] for rifle in RIFLES.values() if rifle["root"] == ident)
        rows[index] = f"{target}\t{part['capacity']}\t{flag}\t{name[:89]}"
        clashes = [indices[mapped[conflict]] for conflict in part["conflicts"] if conflict in mapped]
        sockets[index] = [" ".join(map(str, (len(clashes), *clashes))), str(len(part["sockets"]))]
        for slot in part["sockets"]:
            options = [indices[mapped[candidate]] for candidate in slot["parts"]]
            frame = " ".join(f"{value:.9g}" for value in slot["frame"])
            sockets[index].append("\t".join((slot["name"], slot["label"][:31], str(int(slot["required"])),
                                            str(len(options)), frame, " ".join(map(str, options)))))
        source = work / "meshes" / f"{ident}.bin"
        if not source.is_file():
            raise ValueError(f"Missing converted attachment {ident} ({name})")
        shutil.copyfile(source, destination / "meshes" / f"{target}.bin")
        if target != ident:
            for suffix in (".opt", ".bin", "-folded.bin"):
                source = destination / "optics" / (ident + suffix)
                if source.is_file():
                    shutil.copyfile(source, destination / "optics" / (target + suffix))

    counts = set()
    for key, rifle in RIFLES.items():
        root = rifle["root"]
        choices = order_defaults(graph, root, rifle_defaults(rifle))
        builds[key] = indices[mapped[root]], [f"{path} {indices[mapped[part]]}" for path, part in choices.items()]
        entries = reachable(graph, root) - {root, stable_id(root + ":bolt")}
        counts.update(entries)
        print(f"{rifle['name']}: {len(entries)} compatible attachments across all slots")
    print(f"{len(counts)} distinct compatible attachments; {len(rows) - original_count} new catalog entries")

    lines = ["GEVRGEAR3", str(len(rows)), *rows, *(row for block in sockets for row in block)]
    for key, (root, choices) in builds.items():
        lines.extend([f"{key} {root} {len(choices)}", *choices])
    pending = destination / "catalog.tmp"
    pending.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
    pending.replace(destination / "catalog.txt")
    count = migrate_presets(baseline, destination, mapped)
    if count:
        print(f"Migrated {count} existing AR presets within the new preview")


if __name__ == "__main__":
    cmd = argparse.ArgumentParser(description=__doc__)
    cmd.add_argument("work", type=Path)
    phase = cmd.add_subparsers(dest="phase", required=True)
    extraction = phase.add_parser("extract")
    extraction.add_argument("archive", type=Path)
    extraction.add_argument("baseline", type=Path)
    assembly = phase.add_parser("assemble")
    assembly.add_argument("baseline", type=Path)
    assembly.add_argument("destination", type=Path)
    args = cmd.parse_args()
    if args.phase == "extract":
        extract(args.archive, args.work, args.baseline)
    else:
        assemble(args.work, args.baseline, args.destination)
