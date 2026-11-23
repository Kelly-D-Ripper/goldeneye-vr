"""Extend an installed Tactical catalog with meshes from the local source packs."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import struct

from prepare_tactical_gear import extract_parts


ZOMBOID = {2: ("MP5", "5926bb2186f7744b1c6c6e60"),
           3: ("MP5K", "5d2f0d8048f0356c925bc3b0"),
           5: ("PM", "5448bd6b4bdc2dfc2f8b4569")}
LEGACY_KEYS = (4, 0, 7)  # AK-74N, AKS-74UN, SVD
CORNER = struct.Struct("<5f4BB3x")


def read_mesh(path):
    data = path.read_bytes()
    if data[:8] != b"GEVRWPN1":
        raise ValueError(f"Unsupported mesh {path}")
    count, = struct.unpack_from("<I", data, 8)
    textures, pos = [], 12
    for _ in range(count):
        width, height = struct.unpack_from("<HH", data, pos)
        end = pos + 4 + 4 * width * height
        textures.append(data[pos:end])
        pos = end
    triangles, = struct.unpack_from("<I", data, pos)
    faces = list(CORNER.iter_unpack(data[pos + 4:]))
    if len(faces) != 3 * triangles:
        raise ValueError(f"Invalid face count in {path}")
    return textures, faces


def write_section(destination, source, groups=None, axes=(1, 1, -1), anchor=(0, 0, 0)):
    """Retain the original mesh, UVs, and textures while isolating a fitted part."""
    textures, faces = read_mesh(source)
    if groups is not None:
        faces = [v for v in faces if v[-1] in groups]
    materials = {src: dest for dest, src in enumerate(sorted({v[-1] for v in faces}))}
    if not materials:
        raise ValueError(f"No mesh in groups {groups} of {source}")
    blob = bytearray()
    for v in faces:
        p = tuple(v[axis] * axes[axis] - anchor[axis] for axis in range(3))
        blob += CORNER.pack(*p, *v[3:-1], materials[v[-1]])
    destination.write_bytes(b"GEVRWPN1" + struct.pack("<I", len(materials))
                            + b"".join(textures[i] for i in materials)
                            + struct.pack("<I", len(faces) // 3) + blob)


def stable_id(name):
    return hashlib.sha256(f"goldeneye-tactical:{name}".encode()).hexdigest()[:24]


def part_slot(name, label, options, anchor, required=True):
    x, y, z = anchor
    return dict(name="mod_" + name, label=label, parts=options, required=required,
                frame=(1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, -z))


def add_baked_parts(parts, defaults, files, installed, work, wtt):
    meshes = work / "gear/meshes"
    nifs = work / "nif-parts"

    def part(key, name, source, groups=None, anchor=(0, 0, 0), capacity=0, axes=(1, 1, -1)):
        ident = stable_id(key)
        write_section(meshes / f"{ident}.bin", source, groups, axes, anchor)
        parts[ident] = dict(id=ident, name=name, capacity=capacity, sockets=[], conflicts=[])
        return ident

    def slots(root, entries, fitted):
        parts[root]["sockets"] = entries
        return root, {s["name"]: default for s, default in zip(entries, fitted) if default}

    ppk = installed / "ppk-black-suppressed.bin"
    grip_point, mag_point, muzzle_point = (0, .100, .061), (0, .104, .025), (0, .222, .019)
    black = part("ppk-grip-black", "Black factory grip", ppk, {1}, grip_point)
    brown = part("ppk-grip-brown", "Brown factory grip", nifs / "ppk-brown-grip.bin", anchor=grip_point)
    flush = part("ppk-mag-7", "Factory magazine", ppk, {3, 4}, mag_point, 7)
    extended = part("ppk-mag-8", "Extended magazine", nifs / "ppk-extended-mag.bin", anchor=mag_point, capacity=8)
    suppressor = part("ppk-suppressor", "Black suppressor", ppk, {9}, muzzle_point)
    for kind, name, default_muzzle in ((1, "Walther PPK", None), (8, "Suppressed PPK", suppressor)):
        root = part(f"ppk-{kind}", name, ppk, {0, 2, 5, 6, 7, 8})
        defaults[kind] = slots(root, [
            part_slot("pistol_grip", "Grip", [black, brown], grip_point),
            part_slot("magazine", "Magazine", [flush, extended], mag_point, False),
            part_slot("muzzle", "Muzzle", [suppressor], muzzle_point, False),
        ], [black, flush, default_muzzle])

    scorpio = installed / "scorpio61.bin"
    root = part("scorpio", "Skorpion vz. 61", nifs / "skorpion-core.bin")
    stock_point, mag_point = (0, -.051, -.010), (0, .064, .001)
    folded = part("scorpio-stock-folded", "Folded wire stock", nifs / "skorpion-folded-stock.bin", anchor=stock_point)
    unfolded = part("scorpio-stock-extended", "Extended wire stock", nifs / "skorpion-extended-stock.bin", anchor=stock_point)
    small = part("scorpio-mag-10", "Short magazine", nifs / "skorpion-short-mag.bin", anchor=mag_point, capacity=10)
    large = part("scorpio-mag-20", "Standard magazine", scorpio, {0}, mag_point, 20)
    defaults[6] = slots(root, [
        part_slot("stock", "Stock", [folded, unfolded], stock_point),
        part_slot("magazine", "Magazine", [large, small], mag_point, False),
    ], [folded, large])

    svd = installed / "svd-wood.bin"
    stock_point, handguard_point, muzzle_point, mag_point = (0, 0, -.010), (0, .474, 0), (0, .985, 0), (0, .206, 0)
    stock = part("svd-stock-wood", "Wood stock", svd, {10, 11}, stock_point)
    handguard = part("svd-handguard-wood", "Wood handguard", svd, {12, 13}, handguard_point)
    muzzle = part("svd-muzzle-standard", "Factory flash hider", svd, {9}, muzzle_point)
    magazine = part("svd-mag-10", "Standard magazine", svd, {0}, mag_point, 10)
    extra = svd_variants(wtt, work / "svd-parts")
    stocks = [stock] + [part(key, name, path, anchor=stock_point) for key, name, path in extra["stock"]]
    handguards = [handguard] + [part(key, name, path, anchor=handguard_point) for key, name, path in extra["handguard"]]
    muzzles = [muzzle] + [part(key, name, path, anchor=muzzle_point) for key, name, path in extra["muzzle"]]
    magazines = [magazine] + [part(key, name, path, anchor=mag_point, capacity=25) for key, name, path in extra["magazine"]]
    index, fitted = defaults[7]
    svd_root = files[index].split("\t")[0]
    write_section(meshes / f"{svd_root}.bin", svd, set(range(1, 9)))
    svd_slots = [part_slot("stock", "Stock", stocks, stock_point),
                 part_slot("handguard", "Handguard", handguards, handguard_point),
                 part_slot("muzzle", "Muzzle", muzzles, muzzle_point),
                 part_slot("magazine", "Magazine", magazines, mag_point, False)]
    defaults[7] = (index, fitted | {slot["name"]: slot["parts"][0] for slot in svd_slots})
    return index, svd_slots


def svd_variants(source, output):
    import numpy as np
    from prepare_tactical_svd import prefabs, write_mesh

    _, anchors = prefabs(source / "weapon_dragunov_container.bundle")
    _, barrel = prefabs(source / "svdshortenedbarrel.bundle")
    barrel_origin = next(v for k, v in barrel.items() if "565mm" in k)
    muzzle_socket = barrel["mod_muzzle"] + anchors["mod_barrel"] - barrel_origin + (0, 0, .145)
    inputs = {
        "stock": [("svdpolymerstock", "Black polymer stock"), ("svdpolymerstockgreen", "Green polymer stock")],
        "handguard": [("svdwoodenhandguardgreen", "Green wood handguard"),
                      ("svdwoodenhandguardgreenghillie", "Wrapped handguard")],
        "muzzle": [("dragunov2portmuzzlebrake", "Two-port brake"), ("dragunov3portmuzzlebrake", "Three-port brake")],
        "magazine": [("svd_magazinepoly25", "Polymer magazine")],
    }
    result = defaultdict(list)
    for slot, variants in inputs.items():
        for key, name in variants:
            meshes, frames = prefabs(source / (key + ".bundle"))
            offset = np.zeros(3)
            if slot == "muzzle":
                origin = next((v for k, v in frames.items() if k.lower() == key), np.zeros(3))
                offset = muzzle_socket - origin
            if slot == "magazine":
                offset = anchors["mod_magazine"]
            path = output / (key + ".bin")
            write_mesh(path, [(slot, mesh, offset, 0.) for mesh in meshes], False)
            result[slot].append((key, name, path))
    return result


def legacy_catalog(path):
    lines = path.read_text(encoding="ascii").splitlines()
    if lines[0] != "GEVRGEAR2":
        raise ValueError("Expected the installed Workshop GEVRGEAR2 catalog")
    count = int(lines[1])
    rows, blocks, defaults = lines[2:count + 2], [], {}
    pos = count + 2
    for _ in rows:
        end = pos + 2 + int(lines[pos + 1])
        blocks.append(lines[pos:end])
        pos = end
    for kind in LEGACY_KEYS:
        index, count = map(int, lines[pos].split())
        defaults[kind] = index, {path: int(part) for path, part in
                                 (line.split() for line in lines[pos + 1:pos + count + 1])}
        pos += count + 1
    if pos != len(lines):
        raise ValueError("Unexpected trailing catalog data")
    return rows, blocks, defaults


def merge(installed, work, assembly_file, wtt):
    gear = installed / "gear"
    rows, blocks, defaults = legacy_catalog(gear / "catalog.txt")
    index = {row.split("\t", 1)[0]: i for i, row in enumerate(rows)}
    mesh_dir = work / "gear/meshes"
    mesh_dir.mkdir(parents=True, exist_ok=True)
    raw = json.loads((work / "roster-graph.json").read_text(encoding="utf-8"))
    parts = {p["id"]: p for p in raw}
    assemblies = json.loads(assembly_file.read_text(encoding="utf-8"))
    for kind, (name, root) in ZOMBOID.items():
        fitted = {p["path"]: p["id"] for p in assemblies[name]["parts"] if not p["path"].startswith("root")}
        defaults[kind] = root, fitted
    for kind, filename, keep, scale in ((2, "mp5.bin", {7, 8}, 2.018),
                                        (3, "mp5k.bin", {6, 7}, 2.0532),
                                        (5, "makarov.bin", {4}, 1.660014)):
        root = defaults[kind][0]
        write_section(mesh_dir / f"{root}.bin", installed / filename, keep,
                      (-1 / scale, 1 / scale, -1 / scale))
    svd_index, svd_slots = add_baked_parts(parts, defaults, rows, installed, work, wtt)
    added = []
    for ident, part in parts.items():
        if ident in index:
            continue
        if not (mesh_dir / f"{ident}.bin").is_file():
            raise ValueError(f"Unbuilt roster mesh {ident} ({part['name']})")
        index[ident] = len(rows)
        rows.append(f"{ident}\t{part['capacity']}\t1\t{part['name']}")
        added.append(part)

    def slot_row(slot):
        allowed = [index[p] for p in slot["parts"] if p in index]
        label = slot["label"][:31]
        return "\t".join((slot["name"], label, str(int(slot["required"])), str(len(allowed)),
                          " ".join(f"{v:.9g}" for v in slot["frame"]), " ".join(map(str, allowed))))

    for part in added:
        conflicts = [index[p] for p in part["conflicts"] if p in index]
        sockets = [slot_row(slot) for slot in part["sockets"]]
        blocks.append([" ".join(map(str, [len(conflicts), *conflicts])), str(len(sockets)), *sockets])
    svd_block = blocks[svd_index]
    svd_block[1] = str(int(svd_block[1]) + len(svd_slots))
    svd_block.extend(map(slot_row, svd_slots))

    def part_index(part):
        return index[part] if isinstance(part, str) else part

    lines = ["GEVRGEAR3", str(len(rows)), *rows, *(line for block in blocks for line in block)]
    for kind, (root, fitted) in sorted(defaults.items()):
        lines.append(f"{kind} {part_index(root)} {len(fitted)}")
        lines.extend(f"{path} {part_index(part)}" for path, part in fitted.items())
    (work / "gear/catalog.txt").write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
    print(f"{len(rows)} parts ({len(added)} added), {len(defaults)} Workshop weapon entries")


def extract(installed, work, archive):
    parts, _ = extract_parts(archive, work, [root for _, root in ZOMBOID.values()])
    (work / "roster-graph.json").write_text(json.dumps(parts, indent=2), encoding="utf-8")
    mesh_dir = installed / "gear/meshes"
    to_bake = [p for p in parts if not (mesh_dir / (p["id"] + ".bin")).is_file()]
    (work / "parts.json").write_text(json.dumps(to_bake, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("installed", type=Path, help="Existing mods/tactical directory")
    parser.add_argument("work", type=Path, help="Staging directory for the added art and catalog")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("extract").add_argument("archive", type=Path)
    merge_args = commands.add_parser("merge")
    merge_args.add_argument("assemblies", type=Path)
    merge_args.add_argument("wtt", type=Path)
    args = parser.parse_args()
    if args.command == "extract":
        extract(args.installed, args.work, args.archive)
    else:
        merge(args.installed, args.work, args.assemblies, args.wtt)
