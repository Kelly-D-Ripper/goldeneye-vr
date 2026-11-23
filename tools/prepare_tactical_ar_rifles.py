"""Build the M4A1 and HK416A5 Workshop packages from the supplied rifle pack."""
import argparse
import json
from pathlib import Path
from zipfile import ZipFile

from prepare_tactical_expanded import merge_catalog, save_mesh
from prepare_tactical_gear import extract_parts
from prepare_tactical_roster import CORNER, read_mesh, stable_id


# Workshop keys are persisted. Keep the previous roster's keys intact.
RIFLES = {
    16: dict(name="M4A1", root="5447a9cd4bdc2dbd208b4567",
             upper="55d355e64bdc2d962f8b4569", barrel="55d3632e4bdc2d972f8b4569",
             gas="5ae30e795acfc408fb139a0b", tube="5649be884bdc2d79388b4577",
             charge="55d44fd14bdc2d962f8b456e", rear="5ae30bad5acfc400185c2dc4",
             stock="55d4ae6c4bdc2d8b2f8b456e", grip="55d4b9964bdc2d1d4e8b456e",
             handguards=("5ae30db85acfc408fb139a05", "55d459824bdc2d892f8b4573")),
    17: dict(name="HK416A5", root="5bb2475ed4351e00853264e3",
             upper="5bb20d53d4351e4502010a69", barrel="5bb20d9cd4351e00334c9d8a",
             gas="5bb20dcad4351e3bac1212da", tube="5bb20e58d4351e00320205d7",
             charge="5bb20dbcd4351e44f824c04e", rear="5bb20e49d4351e3bac1212de",
             stock="5bb20e70d4351e0035629f8f", grip="5bb20e18d4351e00320205d5",
             handguards=("5bb20de5d4351e0035629e59", "5c6d10fa2e221600106f3f23")),
}
OPTICS = ("570fd79bd2720bc7458b4583", "558022b54bdc2dac148b458d", "57ac965c24597706be5f975c")
MUZZLES = ("544a38634bdc2d58388b4568", "5a9fbb84a2750c00137fa685")
MAGAZINES = ("55d4887d4bdc2d962f8b4570", "5aaa5dfee5b5b000140293d3", "544a378f4bdc2d30388b4567")
STOCKS = ("5ae30c9a5acfc408fb139a03", "56eabf3bd2720b75698b4569")
GRIP = "55802f5d4bdc2dac148b458f"
LABELS = {
    **dict(zip(MAGAZINES, ("Steel 30", "PMAG 30", "PMAG 40"))),
    **dict(zip(STOCKS, ("SOPMOD", "MOE Carbine"))),
    GRIP: "MOE",
    "55d4ae6c4bdc2d8b2f8b456e": "M4 Carbine",
    "5bb20e70d4351e0035629f8f": "HK Slim Line",
    "55d4b9964bdc2d1d4e8b456e": "Colt A2",
    "5bb20e18d4351e00320205d5": "HK Battle Grip",
    "5ae30db85acfc408fb139a05": "Colt Carbine",
    "55d459824bdc2d892f8b4573": "KAC RIS",
    "5bb20de5d4351e0035629e59": "HK Quad Rail",
    "5c6d10fa2e221600106f3f23": "MI M-LOK",
}
IDENTITY = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0)


def extract(archive, work):
    ids = dict.fromkeys((*MUZZLES, *MAGAZINES, *STOCKS, GRIP))
    for rifle in RIFLES.values():
        ids.update(dict.fromkeys(rifle[k] for k in ("root", "upper", "barrel", "gas", "tube", "charge", "rear", "stock", "grip")))
        ids.update(dict.fromkeys(rifle["handguards"]))
    sources, _ = extract_parts(archive, work, ids, include_children=False)
    parts = {part["id"]: part for part in sources}
    for part in sources:
        if not part["mesh"] or not part["texture"]:
            raise ValueError(f"No stock asset for {part['id']} ({part['name']})")
        part["texture_size"] = 384
        part["triangle_limit"] = 3600
    # Author-split body and bolt avoid animating the whole receiver.
    with ZipFile(archive) as bundle:
        names = tuple(bundle.namelist())
        for rifle in RIFLES.values():
            root = rifle["root"]
            part = parts[root]
            part["triangle_limit"] = 5400
            for variant in ("body", "weapon_bolt"):
                tail = f"models_X/TW/Moving/{root}_{variant}.fbx"
                member = next(name for name in names if name.endswith("/42/media/" + tail))
                target = work / tail
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(bundle.read(member))
                if variant == "body":
                    part["mesh"] = tail
                else:
                    bolt = dict(part, id=stable_id(root + ":bolt"), mesh=tail, name="Bolt", sockets=[])
                    parts[bolt["id"]] = bolt
    (work / "parts.json").write_text(json.dumps(list(parts.values()), indent=2), encoding="ascii")
    print(f"Staged {len(parts)} authoring meshes including separate bolts")


def compose(parent, child):
    """Compose authored column-vector frames, storing rows consecutively."""
    return tuple(sum(parent[4 * row + axis] * child[4 * axis + col] for axis in range(3))
                 + (parent[4 * row + 3] if col == 3 else 0)
                 for row in range(3) for col in range(4))


def inverse_rigid(frame):
    return tuple(frame[4 * col + row] if col < 3
                 else -sum(frame[4 * axis + row] * frame[4 * axis + 3] for axis in range(3))
                 for row in range(3) for col in range(4))


def assemble_mesh(baked, destination, components):
    materials, textures, geometry = {}, [], bytearray()
    for ident, frame in components:
        pictures, faces = read_mesh(baked / (ident + ".bin"))
        indices = []
        for picture in pictures:
            if picture not in materials:
                materials[picture] = len(textures)
                textures.append(picture)
            indices.append(materials[picture])
        for x, y, z, s, t, red, green, blue, alpha, layer in faces:
            p = (x, y, -z, 1)
            world = tuple(sum(frame[4 * row + i] * p[i] for i in range(4)) for row in range(3))
            geometry.extend(CORNER.pack(world[0], world[1], -world[2], s, t, red, green, blue, alpha, indices[layer]))
    save_mesh(destination, textures, geometry)


def assemble(work, destination, catalog):
    gear = destination / "meshes"
    baked = work / "meshes"
    parts = {part["id"]: part for part in json.loads((work / "parts.json").read_text(encoding="ascii"))}
    rows = catalog.read_text(encoding="ascii").splitlines()
    installed = {row.split("\t", 1)[0] for row in rows[2:2 + int(rows[1])]}
    definitions, builds = {}, {}

    def socket(part, key, options):
        authored = next(slot for slot in parts[part]["sockets"] if slot["name"] == key)
        if set(options) - set(authored["parts"]):
            raise ValueError(f"Unexpected source fit: {part} {key} {options}")
        return authored["frame"]

    def register(ident, title, capacity=0, sockets=()):
        definitions[ident] = dict(id=ident, name=title, capacity=capacity, sockets=sockets)
        return ident

    def leaf(ident):
        if ident not in installed and ident not in definitions:
            (gear / (ident + ".bin")).write_bytes((baked / (ident + ".bin")).read_bytes())
            register(ident, LABELS.get(ident, parts[ident]["name"]), parts[ident]["capacity"])
        return ident

    for ident in OPTICS:
        if ident not in installed or not (destination / "optics" / f"{ident}.opt").is_file():
            raise ValueError(f"Missing working optic profile in baseline: {ident}")

    for key, rifle in RIFLES.items():
        name, root, upper, barrel, gas, tube, charge, rear, stock, grip, handguards = (
            rifle[field] for field in ("name","root","upper","barrel","gas","tube","charge","rear","stock","grip","handguards"))
        upper_pose = socket(root, "mod_reciever", [upper])
        barrel_pose = compose(upper_pose, socket(upper, "mod_barrel", [barrel]))
        tube_pose = socket(root, "mod_stock", [tube])
        body = [(root, IDENTITY), (upper, upper_pose), (barrel, barrel_pose),
                (gas, compose(barrel_pose, socket(barrel, "mod_gas_block", [gas]))),
                (tube, tube_pose)]
        gun = stable_id(root + ":workshop chassis")
        assemble_mesh(baked, gear / (gun + ".bin"), body)

        optic = compose(upper_pose, socket(upper, "mod_scope", OPTICS))
        iron = compose(upper_pose, socket(upper, "mod_sight_rear", [rear]))
        iron_id = stable_id(rear + ":receiver rail")
        assemble_mesh(baked, gear / (iron_id + ".bin"), [(rear, compose(inverse_rigid(optic), iron))])
        register(iron_id, "Carry handle" if key == 16 else "Flip-up irons")

        slots, defaults = [], {}

        def choice(path, title, pose, options):
            slots.append(dict(name=path, label=title, frame=pose, parts=options, required=True))
            defaults[path] = options[0]

        choice("mod_scope", "Sight", optic, (iron_id, *OPTICS))
        choice("mod_muzzle", "Muzzle", compose(barrel_pose, socket(barrel, "mod_muzzle", MUZZLES)), tuple(map(leaf, MUZZLES)))
        choice("mod_handguard", "Handguard", compose(upper_pose, socket(upper, "mod_handguard", handguards)), tuple(map(leaf, handguards)))
        stocks = (stock, *STOCKS)
        choice("mod_stock", "Stock", compose(tube_pose, socket(tube, "mod_stock_000", stocks)), tuple(map(leaf, stocks)))
        grips = (grip, GRIP)
        choice("mod_pistol_grip", "Grip", socket(root, "mod_pistol_grip", grips), tuple(map(leaf, grips)))
        choice("mod_magazine", "Magazine", socket(root, "mod_magazine", MAGAZINES), tuple(map(leaf, MAGAZINES)))
        choice("mod_charge", "Charging handle", socket(root, "mod_charge", [charge]), (leaf(charge),))
        choice("mod_bolt", "Bolt", IDENTITY, (leaf(stable_id(root + ":bolt")),))
        builds[key] = register(gun, name, sockets=slots), defaults
        print(f"{name}: {len(slots) - 2} editable groups; all options checked against the pack's fit lists")

    merge_catalog(catalog, destination, definitions, builds)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("work", type=Path)
    cmd = parser.add_subparsers(dest="mode", required=True)
    extract_cmd = cmd.add_parser("extract")
    extract_cmd.add_argument("archive", type=Path)
    assemble_cmd = cmd.add_parser("assemble")
    assemble_cmd.add_argument("destination", type=Path)
    assemble_cmd.add_argument("catalog", type=Path)
    args = parser.parse_args()
    if args.mode == "extract":
        extract(args.archive, args.work)
    else:
        assemble(args.work, args.destination, args.catalog)

