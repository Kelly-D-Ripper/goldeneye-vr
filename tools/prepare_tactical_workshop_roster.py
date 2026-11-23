"""Install the baked Workshop expansion and keep existing catalog IDs and presets."""
import argparse
import json
from pathlib import Path
import re
import shutil
import sys

from prepare_tactical_ar_rifles import IDENTITY, RIFLES, compose, inverse_rigid
from prepare_tactical_expanded import save_mesh
from prepare_tactical_roster import CORNER, read_mesh, stable_id
from prepare_tactical_workshop_ease import colour, part_names, suppressor
from prepare_tactical_workshop_packs import Socket, read_catalog, write_catalog
from prepare_tactical_workshop_presets import BUNDLED_PRESET_NAMES


ASVAL = "57c44b372459772d2b39b8ce"
M1911 = "5e81c3cbac2bb513793cdc75"
P90 = "5cc82d76e24e8d00134b4b83"
G17 = "5a7ae0c351dfba0017554310"
G18 = "5b1fa9b25acfc40018633c01"
G19 = "63088377b5cd696784087147"
EAGLE = "668fe5a998b5ad715703ddd6"
M32 = "6275303a9f372d6ea97f9ec7"
SAIGA = "576165642459773c7a400233"
SVDS = "5c46fbd72e2216398b5a8c9c"
CZ75 = stable_id("CZ 75B:receiver")
VARIANTS = {
    EAGLE: ("Mk XIX .50 AE", {}),
    "669fa39b48fc9f8db6035a0c": ("L6 .50 AE", {"mod_barrel": "669fa47da", "mod_reciever": "669fa4d97"}),
    "669fa3d876116c89840b1217": ("L6 WTS .50 AE", {"mod_barrel": "669fa48fa", "mod_reciever": "669fa5271"}),
    "669fa3f88abd2662d80eee77": ("L5 .50 AE", {}),
    "669fa409933e898cce0c2166": ("L5 .357", {}),
}
BUILDS = {
    18: ASVAL, 19: M1911, 20: CZ75, 21: P90, 22: G17, 23: G18,
    24: G19, 25: EAGLE, 26: M32, 27: SAIGA,
}
PREFER = {
    ASVAL: {"mod_muzzle": "57c44dd02", "mod_reciever": "57c44f4f2", "mod_magazine": "57838f9f2", "mod_stock": ""},
    P90: {"mod_reciever": "5cc70102e"},
    G17: {"mod_barrel": "5a6b5f868"},
    G19: {"mod_magazine": "63076701a"},
    SAIGA: {"mod_reciever": "", "mod_stock": "", "mod_sight_rear": "", "mod_muzzle": ""},
    SVDS: {"mod_stock": "", "mod_mount_002": "5c82342f2"},
    **{ident: choices for ident, (_, choices) in VARIANTS.items()},
}
MESH, SUPPRESSOR, OPTIC, MUZZLE_ADAPTER = 1, 2, 256, 512
PISTOL_FRAMES = {M1911, G17, G18, G19, *VARIANTS}


def authored_socket(key, label, options, point=(0, 0, 0), required=True):
    frame = list(IDENTITY)
    frame[3::4] = point
    return dict(name=key, label=label, parts=options, frame=frame, required=required)


def finish_flags(name):
    finish = colour(name)
    return (4 if finish == 3 else finish) << 4


def classify_muzzles(rows, sockets, names):
    choices = {part for slots in sockets for socket in slots if socket.key == "mod_muzzle"
               for part in socket.parts}
    for i in choices:
        ident, capacity, art, label = rows[i].split("\t", 3)
        full_name = names.get(ident, label)
        flags = int(art)
        if re.search(r"\b(?:adapter|thread protector|thread cap|brake protector)\b| mount$",
                     full_name, re.I):
            flags = (flags & ~SUPPRESSOR) | MUZZLE_ADAPTER
            for socket in sockets[i]:
                if socket.key == "mod_muzzle":
                    socket.required = 0
        elif suppressor(full_name):
            flags |= SUPPRESSOR
        rows[i] = f"{ident}\t{capacity}\t{flags}\t{label}"


def bake_cz75(directory, mesh_dir, unity_python, definitions):
    sys.path.insert(0, str(unity_python))
    import numpy as np
    from prepare_tactical_svd import prefabs, rgba

    def component(stem):
        return prefabs(directory / f"{stem}.bundle")

    body, anchors = component("weapon_cz75b_container")
    grip = anchors["mod_pistol_grip"] + np.array((0., .008, .002))
    def converted(point):
        return (float(point[0]), float(point[2]), -float(point[1]))

    def bake(ident, label, models, origin, capacity=0, sockets=()):
        materials, textures, corners = {}, [], bytearray()
        for _, points, normals, uv, triangles, texture in models:
            material = materials.get(texture.m_Name)
            if material is None:
                material = materials[texture.m_Name] = len(textures)
                textures.append(rgba(texture.image.convert("RGBA")))
            shade = np.rint(255 * (.64 + .36 * np.maximum(0, normals @ (.28, .87, -.4)))).clip(0, 255)
            for tri in triangles:
                for index in tri:
                    u, v = uv[index][:2]
                    level = int(shade[index])
                    corners += CORNER.pack(*converted(points[index] - origin), u, 1-v,
                                           level, level, level, 255, material)
        save_mesh(mesh_dir / f"{ident}.bin", textures, corners)
        definitions[ident] = dict(id=ident, name=label, full_name=label, capacity=capacity,
                                  sockets=list(sockets), conflicts=[], mesh=True)
        return ident

    slots = []
    def slot(key, label, stems, names, anchor, capacity=0):
        origin = anchors[anchor]
        choices = [bake(stable_id("CZ 75B:" + stem), name, component(stem)[0], origin, capacity)
                   for stem, name in zip(stems, names)]
        x, y, z = origin - grip
        slots.append(authored_socket(key, label, choices, (float(x), float(z), float(y))))

    slot("mod_charge", "Slide", ("cz75b_slide",), ("Factory slide",), "mod_reciever")
    slot("mod_barrel", "Barrel", ("cz75_barrel", "cz75_barrel_threaded"),
         ("Factory barrel", "Threaded barrel"), "mod_barrel")
    slot("mod_magazine", "Magazine", ("cz75_magazine",), ("Factory magazine",), "mod_magazine", 16)
    bake(CZ75, "CZ 75B", body, grip, sockets=slots)


def fit_variants(sockets, indices, root, choices, fits=None):
    sockets[indices[root]].insert(0, Socket("mod_variant", "Variant", 1, list(IDENTITY),
        [indices[ident] for ident in choices], [], fits or {}))


def bake_pistol_frame(source, target):
    # These pistol receivers face up in the imported FBX basis. Their sockets,
    # slides and magazines already use the catalog's correct mounting frames.
    textures, vertices = read_mesh(source)
    faces = bytearray()
    for i in range(0, len(vertices), 3):
        for x, y, z, *attributes in reversed(vertices[i:i+3]):
            faces += CORNER.pack(x, y, -z, *attributes)
    save_mesh(target, textures, faces)


def fit_ar_suppressors(rows, blocks, builds):
    """Give each rifle's barrels the same suppressor choices at their own muzzle."""
    suppressors = {i for i, row in enumerate(rows) if int(row.split("\t", 3)[2]) & SUPPRESSOR}

    def slot(part, key):
        index = next(i for i, line in enumerate(blocks[part][2:], 2) if line.startswith(key + "\t"))
        return index, Socket.parse(blocks[part][index])

    exempt = {}
    added = {}
    for key in RIFLES:
        root, defaults = builds[key]
        uppers = slot(root, "mod_reciever")[1].parts
        barrels = sorted({barrel for upper in uppers for barrel in slot(upper, "mod_barrel")[1].parts})
        muzzles = {barrel: slot(barrel, "mod_muzzle") for barrel in barrels}
        stock = dict(line.split() for line in defaults).get("mod_reciever/mod_barrel")
        relative_fits = {}
        for barrel in sorted(barrels, key=lambda barrel: str(barrel) != stock):
            muzzle = muzzles[barrel][1]
            local = inverse_rigid(muzzle.frame)
            for index, part in enumerate(muzzle.parts):
                if part in suppressors and part not in relative_fits:
                    relative_fits[part] = compose(local, muzzle.pose(index))

        added[key] = 0
        for barrel, (index, muzzle) in muzzles.items():
            missing = relative_fits.keys() - set(muzzle.parts)
            for part in sorted(missing):
                muzzle.add(part, list(compose(muzzle.frame, relative_fits[part])))
            if missing:
                blocks[barrel][index] = muzzle.line()
                added[key] += len(missing)
            exempt.setdefault(barrel, set()).update(relative_fits)
            for part in relative_fits:
                exempt.setdefault(part, set()).add(barrel)

    removed = 0
    for part, allowed in exempt.items():
        conflicts = list(map(int, blocks[part][0].split()[1:]))
        remaining = [other for other in conflicts if other not in allowed]
        if len(remaining) != len(conflicts):
            blocks[part][0] = " ".join(map(str, (len(remaining), *remaining)))
            removed += len(conflicts) - len(remaining)
    return added, removed


def install(gear, work, cz_source, unity_python, zomboid, optics):
    rows, blocks, builds = read_catalog(gear / "catalog.txt")
    indices = {row.split("\t", 1)[0]: index for index, row in enumerate(rows)}
    sockets = [[Socket.parse(line) for line in block[2:]] for block in blocks]
    definitions = {p["id"]: p for p in json.loads((work / "catalog-parts.json").read_text(encoding="utf-8"))}
    destination = gear / "meshes"
    bake_cz75(cz_source, destination, unity_python, definitions)
    stock_svd = rows[builds[7][0]].split("\t")[0]
    install_roots = set(BUILDS.values()) | set(VARIANTS) | {SVDS}
    for ident, part in list(definitions.items()):
        if ident not in install_roots or "bolt" not in part:
            continue
        bolt = stable_id(ident + ":workshop bolt")
        shutil.copyfile(work / "baked" / f"{ident}-bolt.bin", destination / f"{bolt}.bin")
        part["sockets"].append(authored_socket("mod_bolt", "Bolt", [bolt], required=2))
        definitions[bolt] = dict(id=bolt, name="Bolt", full_name="Bolt", capacity=0, sockets=[], conflicts=[],
                                 mesh=True)
    first_new = len(rows)
    for ident, part in definitions.items():
        name = VARIANTS.get(ident, (part["name"], {}))[0]
        profile = optics / f"{ident}.opt"
        flags = MESH | finish_flags(part["full_name"]) | (OPTIC if profile.is_file() else 0)
        if ident in indices:
            continue
        source = work / "baked" / f"{ident}.bin"
        target = destination / source.name
        if profile.is_file():
            shutil.copyfile(profile, gear / "optics" / profile.name)
            shutil.copyfile(optics / source.name, target)
        elif ident in PISTOL_FRAMES:
            bake_pistol_frame(source, target)
        elif source.is_file():
            shutil.copyfile(source, target)
        if not target.is_file():
            raise FileNotFoundError(f"Missing baked asset {ident} ({name})")
        indices[ident] = len(rows)
        rows.append(f"{ident}\t{part['capacity']}\t{flags}\t{name}")
        blocks.append(["0", "0"])
        sockets.append([])
    for ident, part in definitions.items():
        i = indices[ident]
        if i < first_new:
            continue
        wanted = PREFER.get(ident, {})
        conflicts = [indices[p] for p in part["conflicts"] if p in indices]
        blocks[i][0] = " ".join(map(str, (len(conflicts), *conflicts)))
        for raw in part["sockets"]:
            key = raw["name"]
            allowed = [p for p in raw["parts"] if p in indices]
            if not allowed or key == "mod_launcher":
                continue
            if key in wanted:
                choice = next(p for p in allowed if p.startswith(wanted[key]))
                allowed = [choice, *(p for p in allowed if p != choice)]
            required = 1 if ident in install_roots and (key == "mod_magazine" or key in wanted) else int(raw["required"])
            if key == "mod_gas_block":
                required = 2
            label = "Cylinder" if ident == M32 and key == "mod_magazine" else raw["label"]
            socket = Socket(key, label, required, raw["frame"], [indices[p] for p in allowed], [], {})
            sockets[i].append(socket)
    names = part_names(zomboid)
    names.update({ident: part["full_name"] for ident, part in definitions.items()})
    classify_muzzles(rows, sockets, names)

    # A suppressor that the pack fits to a muzzle adapter can be selected
    # directly alongside its standard brake, using the supplied fit frames.
    def descendants(current, pose, seen):
        for child in sockets[current]:
            if child.key != "mod_muzzle":
                continue
            for option, selected in enumerate(child.parts):
                if selected in seen:
                    continue
                fitted = compose(pose, child.pose(option))
                if int(rows[selected].split("\t")[2]) & SUPPRESSOR:
                    yield selected, list(fitted)
                else:
                    yield from descendants(selected, fitted, seen | {selected})
    for i in range(first_new, len(rows)):
        for socket in sockets[i]:
            if socket.key != "mod_muzzle":
                continue
            for option, part in enumerate(socket.parts.copy()):
                for can, frame in descendants(part, socket.pose(option), {i, part}):
                    socket.add(can, frame)
    fit_variants(sockets, indices, EAGLE, list(VARIANTS))
    row = rows[indices[stock_svd]].rsplit("\t", 1)[0]
    rows[indices[stock_svd]] = row + "\tSVD (wood)"
    # The original SVD and the new SVDS use different source authoring scales.
    svds_frame = [-1.76, 0, 0, 0, 0, 1.76, 0, 0, 0, 0, 1.76, 0]
    fit_variants(sockets, indices, stock_svd, [stock_svd, SVDS], {1: svds_frame})
    for key, ident in BUILDS.items():
        builds[key] = indices[ident], []
    if len(rows) > 1536:
        raise ValueError("Compiled Workshop part limit exceeded")
    for i, slots in enumerate(sockets):
        blocks[i][1:] = [str(len(slots)), *(slot.line() for slot in slots)]
    fit_ar_suppressors(rows, blocks, builds)
    target = gear / "catalog.txt"
    pending = target.with_suffix(".new")
    write_catalog(pending, rows, blocks, builds)
    pending.replace(target)

    presets = gear / "presets.txt"
    names = []
    for line in presets.read_text(encoding="ascii").splitlines():
        if line.startswith("P "):
            prefix, root, count, title = line.split(" ", 3)
            line = f"{prefix} {root} {count} {BUNDLED_PRESET_NAMES.get(title, title)}"
        names.append(line)
    presets.write_text("\n".join(names) + "\n", encoding="ascii")
    print(f"{len(BUILDS)} new weapons, {len(rows)} parts, {len(VARIANTS)} Desert Eagle variants")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("gear", type=Path)
    parser.add_argument("work", type=Path)
    parser.add_argument("cz_source", type=Path)
    parser.add_argument("unity_python", type=Path)
    parser.add_argument("zomboid", type=Path)
    parser.add_argument("optics", type=Path)
    install(**vars(parser.parse_args()))

