"""Import the seven expansion guns from the supplied private asset packs."""
import argparse
import json
from pathlib import Path
import struct
import sys

from prepare_tactical_gear import extract_parts
from prepare_tactical_roster import CORNER, part_slot, read_mesh, stable_id


# Keys are persisted by Workshop. The suppressed PPK keeps key 8.
ZOMBOID = {
    9: ("Uzi", "66992b349950f5f4cd06029f", {
        "mod_magazine": ("669927203c4fda6471005cbe", "6699271b9950f5f4cd060299"),
        "mod_barrel": ("6698c8ab29e062525d0ad8ab",),
        "mod_stock": ("6698c9ed36ba38d291017713", "6698c9e07356874dfe0a0b88"),
        "mod_handguard": ("6698c8c736ba38d29101770b",),
        "mod_pistol_grip": ("669946c157df3e2b4e0a0dc5",),
    }),
    10: ("SR-2M Veresk", "62e14904c2699c0ec93adc47", {
        "mod_magazine": ("62e153bcdb1a5c41971c1b5b", "633a98eab8b0506e48497c1a"),
        "mod_muzzle": ("62e2a754b6c0ee2f230cee0f", "62e2a7138e1ac9380579c122"),
        "mod_stock": ("62e292e7b6c0ee2f230cee00",),
        "mod_reciever": ("62e27a7865f0b1592a49e17b",),
        "mod_handguard": ("62e15547db1a5c41971c1b5e", "637ba19df7ca6372bf2613d7"),
        "mod_pistolgrip": ("637b9c37b7e3bc41b21ce71a", "637ba29bf7ca6372bf2613db"),
    }),
    11: ("B&T MP9", "5e00903ae9dc277128008b87", {
        "mod_magazine": ("5de8eac42a78646d96665d91", "5de8ea8ffd6b4e6e2276dc35"),
        "mod_stock": ("5de910da8b6c4240ba2651b5",),
        "mod_reciever": ("5e0090f7e9dc277128008b93",),
        "mod_charge": ("5de922d4b11454561e39239f",),
    }),
    15: ("PKM", "64637076203536ad5600c990", {
        "mod_pistolgrip": ("5a0071d486f77404e23a12b2",),
        "mod_magazine": ("646372518610c40fc20204e8",),
        "mod_stock": ("646371a9f2404ab67905c8e6",),
        "mod_barrel": ("646371faf2404ab67905c8e9",),
        "mod_bipod": ("6464d870bb2c580352070cc4",),
        "mod_sight_rear": ("6492fb8253acae0af00a29b6",),
    }),
}

# These three receivers include their moving controls in the main FBX.
# Isolate the authored triangles in the pack's right/forward/down coordinates.
ACTION_BOUNDS = {
    9: ((-.020, .070, -.050), (.019, .095, -.026)),
    10: ((-.026, .000, -.016), (-.009, .013, .001)),
    15: ((.014, .088, -.018), (.027, .126, -.001)),
}

# WTT positions are baked in a common reference pose within each weapon.
# Use the manufacturer's supplied body, grip, stock and action meshes.
WTT = {
    12: dict(folder="WeaponCZScorpion", name="CZ Scorpion EVO 3",
             container="weapon_cz_scorpion_container", grip=(.1327, -.164, .310),
             body="cz_scorpion_trigger cz_scorpion_selector cz_scorpion_catch cz_scorpion_frame cz_scorpion_release",
             fixed="cz_scorpion_barrel_8 cz_scorpion_handguard cz_scorpion_irons_fs cz_scorpion_irons_rs cz_scorpion_pistolgrip",
             stock="cz_standard", action="cz_scorpion_charge", bolt="cz_scorpion_bolt",
             mags=(("cz_scorpion_magazine_30", 30), ("cz_scorpion_magazine_20", 20)),
             muzzle=("cz_scorpion_muzzle", "cz_scorpion_barrel_8", (.1326, -.0927, .794))),
    13: dict(folder="WeaponPM9", name="Minebea PM-9",
             container="weapon_minebea_pm9_container", grip=(.1333, -.129, .354),
             body="pm9_frame pm9_trigger", fixed="",
             stock="", action="pm9_charge", bolt="pm9_bolt",
             mags=(("pm9_magazine_25", 25), ("pm9_magazine_32", 32)),
             barrels=("pm9_barrel_flashhider", "pm9_integralsystem"), optional_stock="pm9_wirestock"),
    14: dict(folder="WeaponMK23", name="HK Mk 23",
             container="weapon_mk23_container", grip=(.1327, -.112, .435),
             body="mk23_frame mk23_catch mk23_release",
             fixed="mk23_grip mk23_hammer mk23_trigger",
             stock="", action="mk23_slide", action_parts="mk23_fs mk23_rs",
             mags=(("mk23_magazine", 12), ("mk23_magazine_ext", 17)),
             barrels=("mk23_barrel",),
             muzzle=("mk23_suppressor", "mk23_barrel", (0., 0., 0.)), protector="mk23_tp"),
}


def stock_parts(archive, work):
    selected = {}
    for _, root, slots in ZOMBOID.values():
        selected[root] = None
        for options in slots.values():
            selected.update(dict.fromkeys(options))
    parts, _ = extract_parts(archive, work, selected, include_children=False)
    for part in parts:
        if not part["mesh"]:
            raise ValueError(f"Missing source for {part['name']} ({part['id']})")
    return {part["id"]: part for part in parts}


def prepare_zomboid(archive, work):
    parts = stock_parts(archive, work)
    (work / "parts.json").write_text(json.dumps(list(parts.values()), indent=2), encoding="utf-8")
    print(f"Staged {len(parts)} Zomboid models")


def save_mesh(path, textures, faces):
    count = len(faces) // 84
    if not 0 < count <= 50000 or not 0 < len(textures) <= 32:
        raise ValueError(f"Unsupported mesh size for {path.name}: {count} triangles / {len(textures)} textures")
    path.write_bytes(b"GEVRWPN1" + struct.pack("<I", len(textures)) + b"".join(textures)
                     + struct.pack("<I", count) + faces)
    print(f"{path.name} {count} triangles", flush=True)


def split_action(key, source, mesh_dir, root, weapon):
    textures, vertices = read_mesh(source)
    low, high = ACTION_BOUNDS[key]
    fixed, moving = bytearray(), bytearray()
    for i in range(0, len(vertices), 3):
        face = vertices[i:i + 3]
        center = [sum(v[j] for v in face) / 3 for j in range(3)]
        target = moving if all(a <= p <= b for a, p, b in zip(low, center, high)) else fixed
        for corner in face:
            target.extend(CORNER.pack(*corner))
    if len(moving) < 5 * 84 or len(moving) > len(fixed) // 5:
        raise ValueError(f"Unexpected action geometry in {weapon}: {len(moving) // 84} triangles")
    ident = stable_id(weapon + ":action")
    save_mesh(mesh_dir / f"{root}.bin", textures, fixed)
    save_mesh(mesh_dir / f"{ident}.bin", textures, moving)
    return dict(id=ident, name="Charging handle", capacity=0, sockets=[], conflicts=[])


def bake_wtt(bundle_dir, mesh_dir, definitions, builds):
    import numpy as np
    from prepare_tactical_svd import prefabs, rgba

    for key, recipe in WTT.items():
        directory = bundle_dir / recipe["folder"]
        cache = {}

        def part(stem):
            if stem not in cache:
                cache[stem] = prefabs(directory / (stem + ".bundle"))
            return cache[stem]

        def meshes(stem, offset=None):
            return [(model, offset) for model in part(stem)[0]]

        def bake(ident, label, components, origin, capacity=0):
            textures, materials, faces = [], {}, bytearray()
            for model, offset in components:
                name, points, normals, uv, triangles, texture = model
                tex_key = texture.m_Name
                if tex_key not in materials:
                    materials[tex_key] = len(textures)
                    textures.append(rgba(texture.image.convert("RGBA")))
                index = materials[tex_key]
                pos = points - origin
                if offset is not None:
                    pos += offset
                pos = pos[:, (0, 2, 1)].copy()
                pos[:, 2] *= -1
                shades = np.rint(255 * (.66 + .34 * np.maximum(0, normals @ (.28, .87, -.4)))).clip(0, 255)
                for triangle in triangles:
                    for vertex in triangle:
                        s, t = uv[vertex][:2]
                        value = int(shades[vertex])
                        faces += CORNER.pack(*pos[vertex], s, 1 - t, value, value, value, 255, index)
            path = mesh_dir / f"{ident}.bin"
            save_mesh(path, textures, faces)
            definitions[ident] = dict(id=ident, name=label, capacity=capacity, sockets=[], conflicts=[])
            return ident

        root, anchors = part(recipe["container"])
        grip = np.array(recipe["grip"])
        weapon = recipe["name"]
        root_id = stable_id(weapon + ":receiver")
        static_parts = [(m, None) for m in root if m[0] in recipe["body"].split()]
        for stem in recipe["fixed"].split():
            static_parts += meshes(stem)
        bake(root_id, weapon, static_parts, grip)

        slots, fitted = [], {}

        def add_slot(name, title, options, anchor, required=True, selected=True):
            slots.append(part_slot(name, title, options, (anchor - grip)[(0, 2, 1),] * (1, 1, -1), required))
            if selected:
                fitted["mod_" + name] = options[0]

        mag_anchor = anchors["mod_magazine"]
        magazine_ids = [bake(stable_id(stem), f"{weapon} {size}-round magazine",
                             meshes(stem), mag_anchor, size) for stem, size in recipe["mags"]]
        add_slot("magazine", "MAGAZINE", magazine_ids, mag_anchor)

        handle = recipe["action"]
        charge_anchor = (anchors["mod_charge"] if "mod_charge" in anchors else
                         anchors["mod_reciever"] if key == 14 else np.array((.1333, -.015, .423)))
        action = (meshes(handle) if (directory / (handle + ".bundle")).is_file()
                  else [(m, None) for m in root if m[0] == handle])
        for stem in recipe.get("action_parts", "").split():
            action += meshes(stem)
        action_id = bake(stable_id(weapon + ":action"), "Slide" if key == 14 else "Charging handle",
                         action, charge_anchor)
        add_slot("charge", "SLIDE" if key == 14 else "CHARGING HANDLE", [action_id], charge_anchor)

        if recipe.get("bolt"):
            bolt = [(m, None) for m in root if m[0] == recipe["bolt"]]
            bolt_id = bake(stable_id(weapon + ":bolt"), "Bolt", bolt, anchors["patron_in_weapon"])
            add_slot("bolt", "BOLT", [bolt_id], anchors["patron_in_weapon"])

        stock = recipe["stock"] or recipe.get("optional_stock")
        if stock:
            anchor = anchors["mod_stock"]
            ident = bake(stable_id(stock), "Factory stock", meshes(stock), anchor)
            add_slot("stock", "STOCK", [ident], anchor, required=False, selected=bool(recipe["stock"]))

        if "barrels" in recipe:
            anchor = anchors["mod_barrel"]
            options = [bake(stable_id(stem), label, meshes(stem), anchor)
                       for stem, label in zip(recipe["barrels"], ("Factory barrel", "Integral suppressor"))]
            add_slot("barrel", "BARREL", options, anchor)

        if "muzzle" in recipe:
            stem, barrel, old_origin = recipe["muzzle"]
            muzzle = part(barrel)[1]["mod_muzzle"]
            ident = bake(stable_id(stem), "Factory muzzle" if key == 12 else "Mk 23 suppressor",
                         meshes(stem, muzzle - old_origin), muzzle)
            options = [ident]
            if recipe.get("protector"):
                stem = recipe["protector"]
                options.insert(0, bake(stable_id(stem), "Thread protector", meshes(stem), muzzle))
            add_slot("muzzle", "MUZZLE", options, muzzle, required=False)

        definitions[root_id]["sockets"] = slots
        builds[key] = root_id, fitted
        print(f"{weapon}: grip {grip}; sockets {[(s['name'], s['frame'][3::4]) for s in slots]}", flush=True)


def merge_catalog(catalog, destination, definitions, builds):
    path = destination / "catalog.txt"
    lines = catalog.read_text(encoding="ascii").splitlines()
    if lines[0] != "GEVRGEAR3":
        raise ValueError("Use a GEVRGEAR3 mission-gadgets baseline")
    count = int(lines[1])
    rows = lines[2:count + 2]
    indices = {row.split("\t", 1)[0]: i for i, row in enumerate(rows)}
    pos = count + 2
    for _ in rows:
        pos += 2 + int(lines[pos + 1])
    socket_rows, old_builds = lines[count + 2:pos], lines[pos:]
    for ident, part in definitions.items():
        if ident in indices:
            continue
        indices[ident] = len(rows)
        rows.append(f"{ident}\t{part['capacity']}\t1\t{part['name'][:89]}")
        socket_rows.extend(("0", str(len(part["sockets"]))))
        for slot in part["sockets"]:
            # Resolved after all indices have been assigned below.
            socket_rows.append(slot)
    for i, row in enumerate(socket_rows):
        if isinstance(row, str):
            continue
        slot = row
        allowed = [indices[p] for p in slot["parts"]]
        frame = " ".join(f"{v:.9g}" for v in slot["frame"])
        socket_rows[i] = "\t".join((slot["name"], slot["label"][:31], str(int(slot["required"])),
                                    str(len(allowed)), frame, " ".join(map(str, allowed))))
    for key, (root, fitted) in sorted(builds.items()):
        old_builds.append(f"{key} {indices[root]} {len(fitted)}")
        old_builds.extend(f"{slot} {indices[ident]}" for slot, ident in fitted.items())
    if len(rows) > 768:
        raise ValueError("Catalog exceeds engine part limit")
    temporary = path.with_suffix(".tmp")
    temporary.write_text("\n".join([lines[0], str(len(rows)), *rows, *socket_rows, *old_builds]) + "\n",
                         encoding="ascii", newline="\n")
    temporary.replace(path)


def finish(archive, work, bundles, destination, catalog):
    mesh_dir = destination / "meshes"
    definitions, builds = stock_parts(archive, work), {}
    catalog_lines = catalog.read_text(encoding="ascii").splitlines()
    stock_ids = {row.split("\t", 1)[0] for row in catalog_lines[2:2 + int(catalog_lines[1])]}
    bounds = {}
    for key, (weapon, root, slot_parts) in ZOMBOID.items():
        part = definitions[root]
        part["name"] = weapon
        slots = []
        for slot in part["sockets"]:
            options = slot_parts.get(slot["name"])
            if options:
                slots.append(dict(slot, parts=options, required=slot["required"] or slot["name"] == "mod_magazine"))
        part["sockets"] = slots
        builds[key] = root, {path: options[0] for path, options in slot_parts.items()}
    for ident, part in definitions.items():
        source = work / "meshes" / f"{ident}.bin"
        target = mesh_dir / source.name
        if ident not in stock_ids or not target.exists():
            target.write_bytes(source.read_bytes())
        _, faces = read_mesh(source)
        bounds[ident] = [tuple(f(row[axis] for row in faces) for axis in range(3)) for f in (min, max)]
        if all(ident != root for _, root, _ in ZOMBOID.values()):
            part["sockets"] = []
    (work / "bounds.json").write_text(json.dumps(bounds, indent=2), encoding="ascii")
    for key in ACTION_BOUNDS:
        name, root, _ = ZOMBOID[key]
        action = split_action(key, work / "meshes" / f"{root}.bin", mesh_dir, root, name)
        ident = action["id"]
        definitions[ident] = action
        definitions[root]["sockets"].append(part_slot("charge", "CHARGING HANDLE", [ident], (0, 0, 0)))
        builds[key][1]["mod_charge"] = ident
    bake_wtt(bundles, mesh_dir, definitions, builds)
    merge_catalog(catalog, destination, definitions, builds)
    print(f"Added {len(builds)} Workshop builds and {len(definitions)} source models")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("work", type=Path)
    parser.add_argument("--bundles", type=Path)
    parser.add_argument("--destination", type=Path)
    parser.add_argument("--catalog", type=Path)
    parser.add_argument("--unity-python", type=Path)
    args = parser.parse_args()
    if args.destination:
        if not (args.bundles and args.catalog and args.unity_python):
            parser.error("--destination requires --bundles, --catalog and --unity-python")
        sys.path.insert(0, str(args.unity_python))
        finish(args.archive, args.work, args.bundles, args.destination, args.catalog)
    else:
        prepare_zomboid(args.archive, args.work)

