"""Bake the thirteen-weapon roster into the current Tactical catalogue."""
import argparse
import json
from pathlib import Path
import re
from shutil import copy2
from tempfile import TemporaryDirectory
from zipfile import ZipFile

from prepare_tactical_ar_full import read_catalog
from prepare_tactical_ar_rifles import IDENTITY, compose
from prepare_tactical_gear import ITEM, extract_parts
from prepare_tactical_roster import CORNER, read_mesh, stable_id
from prepare_tactical_expanded import save_mesh


ZOMBOID = {
    29: ("FN57", "5d3eb3b0a4b93615055e84d2", "FN Five-seveN MK2", r"Five-seveN|5-7"),
    30: ("MP7A2", "5bd70322209c4d00d7167b8f", "HK MP7A2", r"MP7"),
    31: ("MP7A1", "5ba26383d4351e00334c93d9", "HK MP7A1", r"MP7"),
    33: ("VITYAZ", "59984ab886f7743e98271174", "PP-19-01 Vityaz", r"PP-19|Vityaz"),
    34: ("MPX", "58948c8e86f77409493f7266", "SIG MPX", r"\bMPX\b"),
    35: ("M1A", "5aafa857e5b5b00018480968", "Springfield M1A", r"\bM1A\b|\bM14\b|SOCOM16"),
    36: ("SR3M", "651450ce0e00edc794068371", "SR-3M", r"SR-3M|9x39 (?:30|20)-round"),
    37: ("AUGA3", "63171672192e68c5460cebc5", "Steyr AUG A3", r"\bAUG\b"),
    38: ("TOKAREV", "571a12c42459771f627b58a0", "TT-33 Tokarev", r"\bTT\b|TT-33"),
    39: ("STM9", "60339954d62c9b14ed777c06", "Soyuz-TM STM-9 Gen.2", r"\bSTM-?9\b|Glock 9x19"),
}
BUNDLES = {
    32: ("KACPDW", "a75a2b3a43e2becc20981c5b", "KAC PDW .300 Blackout", "WeaponKACPDW.json"),
    40: ("L85A2", "66c549879f8f2447575393cc", "L85A2", "L85A2.json"),
    41: ("FAMAS", "66ea1ab604d256f0019d67f8", "FAMAS F1", "FAMAS.json"),
}
EMBEDDED_ACTIONS = {
    30: ((-.016, -.026, -.019), (.016, .007, -.004)),
    31: ((-.016, -.026, -.019), (.016, .007, -.004)),
    33: ((-.024, .058, -.015), (-.0125, .076, .008)),
    35: ((-.027, .151, -.034), (-.012, .211, -.001)),
    36: ((-.024, .058, -.019), (-.012, .096, .020)),
    38: ((-.017, -.031, -.030), (.016, .094, -.002)),
}
MOVING_COMPONENTS = {
    30: {"mod_charge": ("weapon_charghing_handle",), "mod_bolt": ("weapon_bolt", "weapon_bolt_carrier")},
    31: {"mod_charge": ("weapon_charghing_handle",), "mod_bolt": ("weapon_bolt", "weapon_bolt_carrier")},
    33: {"mod_bolt": ("weapon_bolt",)},
    34: {"mod_bolt": ("weapon_bolt", "weapon_bolt_carrier")},
    36: {"mod_bolt": ("weapon_bolt", "weapon_bolt_rotate")},
    37: {"mod_bolt": ("weapon_bolt_assembly", "weapon_bolt_carrier")},
    38: {"mod_charge": ("weapon_slide",)},
    39: {"mod_bolt": ("weapon_bolt",)},
}
# Hand placement relative to the baked part origin, in game model units.
ZOMBOID_FIT = {
    29: (1.80, 31, -96), 30: (1.84, 23, -61), 31: (1.84, 23, -61),
    33: (1.82, 24, 12), 34: (1.82, 26, 33), 35: (1.82, 28, -75),
    36: (1.82, 24, 14), 37: (1.82, 21, 20), 38: (1.80, 29, 13),
    39: (1.82, 28, 27),
}


def read(path):
    return json.loads(path.read_bytes())


def pick_defaults(preset):
    paths = {row["_id"]: row for row in preset["_items"]}

    def slot_path(row):
        parent = paths[row["parentId"]]
        return (slot_path(parent) + "/" if "parentId" in parent else "") + row["slotId"]

    rows = [row for row in preset["_items"] if row.get("slotId", "").startswith("mod_")]
    return dict(sorted(((slot_path(row), row["_tpl"]) for row in rows), key=lambda row: row[0].count("/")))


def gather_zomboid_movement(archive_path, output, parts):
    with ZipFile(archive_path) as archive:
        members = {path.split("/42/media/", 1)[1]: path for path in archive.namelist()
                   if "/42/media/models_X/TW/Moving/" in path and path.endswith(".fbx")}
        for key, slots in MOVING_COMPONENTS.items():
            root = ZOMBOID[key][1]
            components = ("body", *(mesh for models in slots.values() for mesh in models))
            for component in components:
                relative = f"models_X/TW/Moving/{root}_{component}.fbx"
                path = output / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(archive.read(members[relative]))
                ident = stable_id(f"{root}:{component}")
                parts[ident] = dict(parts[root], id=ident, mesh=relative, sockets=[], conflicts=[],
                                    moving_source=True, triangle_limit=5200 if component == "body" else 2200)


def gather_zomboid(args):
    gear_rows, _, _ = read_catalog(args.gear / "catalog.txt")
    existing = {row.split("\t")[0] for row in gear_rows if int(row.split("\t")[2]) & 1}
    names = read(args.database / "locales/global/en.json")
    presets = list(read(args.globals)["ItemPresets"].values())
    defaults, wanted = {}, set()
    with ZipFile(args.zomboid) as archive:
        available = {match[1] for name in archive.namelist() if
                     "/42/media/lua/shared/TW/Data/Items" in name and name.endswith(".lua")
                     for match in ITEM.finditer(archive.read(name).decode("utf8"))}
    for key, (_, ident, _, _) in ZOMBOID.items():
        found = next((preset for preset in presets if preset.get("_encyclopedia") == ident), None)
        choices = pick_defaults(found) if found else {}
        defaults[key] = choices
        wanted.update((ident, *choices.values()))
    if wanted - available:
        raise ValueError(f"Factory parts missing from the archive: {wanted - available}")
    path = args.work / "zomboid"
    raw, _ = extract_parts(args.zomboid, path, dict.fromkeys(sorted(wanted)), include_children=False)
    parts = {part["id"]: part for part in raw}

    # Add native variants around each stock assembly and reuse shared
    # catalogue models where the author's fit lists contain them.
    for key, (_, root, _, marker) in ZOMBOID.items():
        own = re.compile(marker, re.I)
        queue = [(root, "")]
        visited = set()
        for ident, prefix in queue:
            if ident in visited:
                continue
            if ident not in parts:
                data, _ = extract_parts(args.zomboid, path, {ident: None}, include_children=False)
                parts[ident] = data[0]
            visited.add(ident)
            for socket in parts[ident]["sockets"]:
                seat = prefix + socket["name"]
                authored = [x for x in socket["parts"] if x in available]
                stock = defaults[key].get(seat)
                if key == 36 and not stock and socket["name"] in (
                     "mod_charge", "mod_handguard", "mod_magazine", "mod_muzzle",
                     "mod_pistol_grip", "mod_reciever", "mod_stock"):
                    options = [i for i in authored if own.search(names.get(i + " Name", ""))]
                    if socket["name"] == "mod_magazine":
                        options.sort(key=lambda i: "30-round" not in names.get(i + " Name", ""))
                    if socket["name"] == "mod_muzzle":
                        options.sort(key=lambda i: "flash" not in names.get(i + " Name", "").lower())
                    if authored:
                        stock = defaults[key][seat] = (options or authored)[0]
                if stock in available and stock != ident:
                    queue.append((stock, seat + "/"))
                wanted.update(i for i in authored if i not in existing and
                    own.search(names.get(i + " Name", "")))
    extra = wanted - parts.keys()
    if extra:
        raw, _ = extract_parts(args.zomboid, path, dict.fromkeys(sorted(extra)), include_children=False)
        parts.update({part["id"]: part for part in raw})
    for _, root, label, _ in ZOMBOID.values():
        parts[root].update(name=label, triangle_limit=5200, texture_size=320)
    gather_zomboid_movement(args.zomboid, path, parts)
    for ident, part in parts.items():
        real = names.get(ident + " Name", "")
        if part["capacity"] and "magazine" in real.lower():
            part["name"] = real
        elif real and part["name"] != real:
            part["full_name"] = real
    (path / "parts.json").write_text(json.dumps(list(parts.values()), indent=2), encoding="utf8")
    (path / "defaults.json").write_text(json.dumps(defaults, indent=2), encoding="utf8")
    print(f"Prepared {len(parts)} model records and {len(defaults)} factory assemblies.")


def add_zomboid(catalogue, source, fits):
    from prepare_tactical_smg_attachments import MESH, SUPPRESSOR, SCOPE_MOUNT, OPTIC, ADAPTER, slot
    from prepare_tactical_workshop_ease import suppressor

    definitions = {p["id"]: p for p in read(source / "parts.json")}
    defaults = read(source / "defaults.json")
    new = {ident for ident, part in definitions.items()
           if ident not in catalogue.index and not part.get("moving_source")}
    for ident in sorted(new):
        part = definitions[ident]
        source_mesh = source / "meshes" / (ident + ".bin")
        if not source_mesh.is_file():
            continue
        label = part.get("full_name", part["name"])
        flags = MESH
        if (catalogue.output / "optics" / (ident + ".opt")).is_file():
            flags |= OPTIC
        if "suppressor mount" in label.lower() or "suppressor adapter" in label.lower():
            flags |= ADAPTER
        elif suppressor(label):
            flags |= SUPPRESSOR
        if any(s["name"] == "mod_scope" for s in part["sockets"]) and re.search(r"mount|rail", label, re.I):
            flags |= SCOPE_MOUNT
        catalogue.part(ident, part["name"], part["capacity"], flags)
        copy2(source_mesh, catalogue.mesh(ident))
    for ident in sorted(new & catalogue.index.keys()):
        part = definitions[ident]
        options = []
        for raw in part["sockets"]:
            key = raw["name"]
            if key in ("mod_launcher", "mod_grenade"):
                continue
            allowed = [choice for choice in raw["parts"] if catalogue.usable(choice, key, definitions)]
            options.append(slot(key, allowed, raw["frame"], raw["required"]))
        catalogue.sockets_for(ident, options, part["conflicts"])

    def mesh(source_mesh, positive_up):
        textures, vertices = read_mesh(source_mesh)
        if positive_up:
            vertices = [(v[0], v[1], -v[2], *v[3:]) for i in range(0, len(vertices), 3)
                        for v in reversed(vertices[i:i + 3])]
        return textures, vertices

    def fit_moving_part(root, stock, name, label, textures, vertices):
        ident = stable_id(f"{root}:{name}:moving")
        catalogue.part(ident, label)
        save_mesh(catalogue.mesh(ident), textures, vertices)
        block = catalogue.sockets[catalogue.index[root]]
        block[2:] = [row for row in block[2:] if not row.startswith(name + "\t")]
        frame = " ".join(map(str, IDENTITY))
        block.append(f"{name}\t{label}\t2\t1\t{frame}\t{catalogue.index[ident]}")
        block[1] = str(len(block) - 2)
        stock[name] = ident

    for key, (symbol, root, _, _) in ZOMBOID.items():
        stock = defaults[str(key)]
        # FN and Tokarev frames have a positive-up source basis. Their
        # compatible magazines, barrels and slides are already in game axes.
        positive_up = key in (29, 38)
        textures, vertices = mesh(catalogue.mesh(root), positive_up)
        moving = MOVING_COMPONENTS.get(key, {})
        if moving:
            def component(part):
                return mesh(source / "meshes" / (stable_id(f"{root}:{part}") + ".bin"), positive_up)

            body_tex, body = component("body")
            save_mesh(catalogue.mesh(root), body_tex, bytearray().join(CORNER.pack(*v) for v in body))
            for name, components in moving.items():
                moving_tex, packed = [], bytearray()
                for filename in components:
                    group_tex, group = component(filename)
                    for v in group:
                        packed.extend(CORNER.pack(*v[:-1], v[-1] + len(moving_tex)))
                    moving_tex.extend(group_tex)
                label = "Bolt" if name == "mod_bolt" else "Slide" if key == 38 else "Charging handle"
                fit_moving_part(root, stock, name, label, moving_tex, packed)
        action = None
        if key in EMBEDDED_ACTIONS:
            low, high = EMBEDDED_ACTIONS[key]
            body, handle, points = bytearray(), bytearray(), []
            for i in range(0, len(vertices), 3):
                face = vertices[i:i + 3]
                center = [sum(v[d] for v in face) / 3 for d in range(3)]
                moving = all(lo <= v <= hi for lo, v, hi in zip(low, center, high))
                (handle if moving else body).extend(bytearray().join(CORNER.pack(*v) for v in face))
                if moving:
                    points.append(center)
            if not points:
                raise ValueError(f"Missing {symbol} action geometry")
            action = tuple(sum(v[d] for v in points) / len(points) for d in range(3))
            if not moving:
                save_mesh(catalogue.mesh(root), textures, body)
                fit_moving_part(root, stock, "mod_charge", "Charging handle", textures, handle)
        elif key == 29:
            save_mesh(catalogue.mesh(root), textures, bytearray().join(CORNER.pack(*v) for v in vertices))
            action = (0.0, .068, -.020)
        if not action:
            def seat(parent, name, local=IDENTITY):
                found = next(s for s in definitions[parent]["sockets"] if s["name"] == name)
                return compose(local, found["frame"])
            action = seat(root, "mod_charge")[3::4]
            action = (action[0], action[1], -action[2])

        stock = {path: ident for path, ident in stock.items() if ident in catalogue.index}
        catalogue.stock(key, root, stock)
        scale, lift, forward = ZOMBOID_FIT[key]
        rear = min(v[1] for v in vertices)
        mag = next(s["frame"] for s in definitions[root]["sockets"] if s["name"] == "mod_magazine")
        fore = max(v[1] for v in vertices)
        muzzle = (0, max(fore, mag[7] + .05), -.012)
        support = (0, (.55 * fore + .45 * mag[7]), .018)
        ejection = (-.017, max(rear + .04, mag[7]), -.018)
        fits[symbol] = scale, 1, lift, forward, action, ejection, muzzle, support


def add_bundle(catalogue, directory, plan, key, fits, blender):
    import numpy as np
    from prepare_tactical_smg_attachments import (
        MESH, SCOPE_MOUNT, bake_unity_mesh, optimise_models, slot, socket_frame)
    from prepare_tactical_svd import prefabs

    symbol, root, label, filename = plan
    records = read(directory / "db/Items" / filename)
    props = {k: row["overrideProperties"] for k, row in records.items()}
    extras = {}
    resources = [path for path in (directory / "bundles").rglob("*.bundle")
                 if "materials" in path.stem or "textures" in path.stem or path.stem == "client_assets"]
    sam = key != 32
    presets = records[root].get("weaponpresets") or read(directory / "db/CustomWeaponPresets/WeaponPresets.json")
    if isinstance(presets, dict):
        presets = presets.values()
    factory = next(row for row in presets if row["_items"][0]["_tpl"] == root)
    stock = pick_defaults(factory)

    from functools import cache

    @cache
    def prefab(ident):
        return prefabs(directory / "bundles" / props[ident]["Prefab"]["path"],
                       resources=resources, anchor_frames=True, lod0=True, skip_unmapped=True,
                       min_height=1.0 if ident == root and sam else None)

    _, anchors = prefab(root)
    zero = np.zeros(3)
    grip = anchors["mod_pistol_grip"][:3, 3] if key != 41 else np.array((.15955, 1.370, .400))
    origins = {root: grip + np.array((0, -.041, -.019))}

    def allowed(raw):
        return list(dict.fromkeys(value for entry in raw["_props"]["filters"] for value in entry["Filter"]))

    def frame_for(frames, key):
        alternatives = {
            "mod_tactical_000": ("mod_tactical_003",),
            "mod_tactical_001": ("mod_tactical_000",),
            "mod_tactical_002": ("mod_tactical_001",),
        }
        if key in frames:
            return frames[key]
        return next((frames[alias] for alias in alternatives.get(key, ()) if alias in frames), None)

    queue = [root]
    for ident in queue:
        _, frames = prefab(ident)
        for raw in props[ident].get("Slots", []):
            frame = frame_for(frames, raw["_name"])
            if frame is None:
                continue
            for child in allowed(raw):
                if child in props and child not in origins:
                    origins[child] = zero if sam else frame[:3, 3]
                    queue.append(child)

    with TemporaryDirectory(dir=catalogue.output) as temp:
        models = optimise_models(prefab, queue, Path(temp), blender)
    for ident, group in models.items():
        # The SamSWAT source contains both inventory and first-person
        # copies. Deduplicate coincident meshes in the selected pose.
        unique = {}
        for model in group:
            bucket = (model[0], tuple(np.round(model[1].min(0), 4)))
            unique.setdefault(bucket, model)
        models[ident] = list(unique.values())

    action_meshes = [m for m in models[root] if m[0].lower() in (
        ("bolt_lod0", "bolt_rotation_lod0") if sam else ("bolt",))]
    moving_key = "mod_charge" if sam else "mod_bolt"
    action_id = stable_id(root + ":action")
    if not action_meshes:
        raise ValueError(f"Missing {label} bolt geometry")
    action_points = np.concatenate([m[1] for m in action_meshes])
    models[root] = [m for m in models[root] if all(m is not other for other in action_meshes)]
    catalogue.part(action_id, "Charging handle" if sam else "Bolt")
    bake_unity_mesh(catalogue.mesh(action_id), action_meshes, origins[root])

    for ident, group in models.items():
        name_info = records[ident].get("locales", {}).get("en", {})
        name = name_info.get("ShortName") or name_info.get("shortName") or name_info.get("Name") or name_info.get("name")
        cap = props[ident].get("Cartridges", [{}])[0].get("_max_count", 0)
        has_scope = any(s["_name"] == "mod_scope" for s in props[ident].get("Slots", []))
        flags = MESH | (SCOPE_MOUNT if ident != root and has_scope and "mount" in props[ident]["Prefab"]["path"].lower() else 0)
        catalogue.part(ident, label if ident == root else name or Path(props[ident]["Prefab"]["path"]).stem, cap, flags)
        bake_unity_mesh(catalogue.mesh(ident), group, origins[ident])

    for ident in queue:
        _, frames = prefab(ident)
        options = []
        for raw in props[ident].get("Slots", []):
            path = raw["_name"]
            frame = frame_for(frames, path)
            if frame is None or path == "mod_launcher":
                continue
            choices = [x for x in allowed(raw)
                       if x in origins or catalogue.usable(x, path, extras)]
            pose = socket_frame(origins[ident], frame, orient=any(x not in origins for x in choices))
            neutral = socket_frame(origins[ident], frame)
            adjustments = [(i, neutral) for i, x in enumerate(choices) if x in origins and neutral != pose]
            options.append(slot(path, choices, pose, raw.get("_required", False), adjustments))
        if ident == root:
            options.append(slot(moving_key, [action_id], IDENTITY, 2))
        catalogue.sockets_for(ident, options, props[ident].get("ConflictingItems", ()))
    stock[moving_key] = action_id
    catalogue.stock(key, root, {path: ident for path, ident in stock.items() if ident in catalogue.index})
    center = tuple(np.mean(action_points, axis=0) - origins[root])
    if not sam:
        center = tuple(anchors["mod_charge"][:3, 3] - origins[root] + np.array((-.023, 0, -.003)))
    elif key == 40:
        center = tuple(np.array((action_points[:, 0].max(), action_points[:, 1].mean(),
                                 action_points[:, 2].mean())) - origins[root])
    ejection = tuple(anchors.get("patron_in_weapon", anchors["mod_magazine"])[:3, 3] - origins[root])
    muzzle = (0., .080, .32 if key == 32 else .34)
    support = (0., .008, .25 if key == 32 else .21)
    # Source is right/up/forward. The packed mesh uses right/forward/down.
    convert = lambda p: (p[0], p[2], -p[1])
    fits[symbol] = 1, -1, 0, 4, *map(convert, (center, ejection, muzzle, support))


def integrate(args):
    from prepare_tactical_smg_attachments import Catalogue, write_catalog

    destination = args.work / "gear"
    catalogue = Catalogue(args.gear, destination, args.work / "zomboid/meshes")
    fits = {}
    add_zomboid(catalogue, args.work / "zomboid", fits)
    for key, path in zip((32, 40, 41), args.bundles, strict=True):
        add_bundle(catalogue, path, BUNDLES[key], key, fits, args.blender)
    if len(catalogue.rows) > 2048:
        raise ValueError("Roster exceeds the engine catalogue limit")
    write_catalog(destination / "catalog.txt", catalogue.rows, catalogue.sockets, catalogue.builds)
    lines = []
    for symbol, (scale, lateral, lift, forward, *points) in fits.items():
        coords = ", ".join("{" + ", ".join(map(str, (round(1000 * scale * lateral * x),
                   round(lift - 1000 * scale * z), round(forward + 1000 * scale * y)))) + "}" for x, y, z in points)
        lines.append(f"        [TACTICAL_GUN_{symbol} - TACTICAL_GUN_UZI] = "
                     f"{{{scale}, {lateral}, {lift}, {forward}, {coords}}},")
    (args.work / "roster-fits.inc").write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"Baked {len(fits)} weapons, catalogue size {len(catalogue.rows)}.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("gear", type=Path, help="Current gear folder")
    parser.add_argument("work", type=Path)
    parser.add_argument("--zomboid", type=Path)
    parser.add_argument("--database", type=Path)
    parser.add_argument("--globals", type=Path)
    parser.add_argument("--bundles", type=Path, nargs=3, metavar=("KAC", "L85", "FAMAS"))
    parser.add_argument("--blender", default="blender")
    args = parser.parse_args()
    if args.bundles:
        integrate(args)
    else:
        gather_zomboid(args)


if __name__ == "__main__":
    main()
