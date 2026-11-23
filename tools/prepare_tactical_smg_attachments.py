"""Update the MP9-N and EVO 3 fits in an existing Tactical Workshop catalogue."""
import argparse
import json
from functools import cache
from pathlib import Path
from shutil import copy2
import subprocess
from tempfile import TemporaryDirectory

import numpy as np

from prepare_tactical_ar_full import read_catalog
from prepare_tactical_expanded import save_mesh
from prepare_tactical_gear import ascii_label
from prepare_tactical_roster import CORNER, read_mesh, stable_id
from prepare_tactical_svd import prefabs, rgba
from prepare_tactical_workshop_ease import colour
from prepare_tactical_workshop_packs import write_catalog


MP9_FAMILY = "5e00903ae9dc277128008b87"
MP9_BLACK = "5de7bd7bfd6b4e6e2276dc25"
MP9_UPPER = "5de8e67c4a9f347bc92edbd7"
MP9_FDE_UPPER = "5e0090f7e9dc277128008b93"
MP9_MOUNT = "5de8f237bbaf010b10528a70"
MP9_CAN = "5de8f2d5b74cd90030650c72"
MP9_KIT = stable_id(MP9_CAN + ":MP9 suppressor kit")
MP9_REAR = "5de8fb539f98ac2bc659513a"
MP9_SIDE = "5de8fc0b205ddc616a6bc51b"
MP9_GRIP = "5de8fbad2fbe23140d3ee9c4"
MP9_MAG = "5de8eac42a78646d96665d91"
EVO_SOURCE = "687afda52dc9fd6c0e14c602"
EVO_FAMILY = stable_id("CZ Scorpion EVO 3:receiver")
EVO_BOLT = stable_id("CZ Scorpion EVO 3:bolt")
EVO_BARREL = "687fb6c8734e9a8e27d6b054"
EVO_HANDGUARD = "687fb46ac2f7551f2ce2e4b1"
EVO_FRONT = "687fb51f2756d21d3145a094"
EVO_REAR = "687fb5224b7bf5a5702898ee"
EVO_FLASH = "687fb49322e3cb02683967bf"
EVO_SUPPRESSOR = "6884465f8615db2a21955aa1"
EVO_CLEAR_MAG = "6886543e0db4a816ef78a062"
EVO_LEGACY_IDS = {
    "6881c0fa3be04213015b7e34": stable_id("cz_scorpion_magazine_30"),
    "6881c0dd2f0aaea90068aa12": stable_id("cz_scorpion_magazine_20"),
    "687b00468b4fafd5ebb3a763": stable_id("CZ Scorpion EVO 3:action"),
    "687aff780a062c4f04628738": stable_id("cz_standard"),
    EVO_FLASH: stable_id("cz_scorpion_muzzle"),
}

LABELS = {
    "magazine": "MAGAZINE", "scope": "OPTICS", "reciever": "UPPER RECEIVER",
    "muzzle": "MUZZLE", "charge": "CH. HANDLE", "pistol_grip": "PISTOL GRIP",
    "sight_front": "FRONT SIGHT", "sight_rear": "REAR SIGHT", "foregrip": "FOREGRIP",
    "mount": "SIDE RAIL", "tactical_001": "LEFT RAIL", "tactical_002": "RIGHT RAIL",
    "tactical_003": "ACCESSORY RAIL", "tactical_004": "HAND STOP",
    "tactical_005": "TOP ACCESSORY RAIL", "stock_000": "FITTED STOCK",
}
MESH, SUPPRESSOR, SCOPE_MOUNT, OPTIC, ADAPTER = 1, 2, 8, 256, 512
AXES = [0, 2, 1]
WTT_SLOT_BASIS = np.array(((1, 0, 0), (0, 0, 1), (0, -1, 0)))


def socket_frame(origin, target, orient=False):
    # Catalogue frames use right, forward, up. WTT socket axes are right,
    # backward, up, so remove the common socket rotation before remapping.
    frame = np.column_stack((np.eye(3), (target[:3, 3] - origin)[AXES]))
    if orient:
        rotation = target[:3, :3] @ WTT_SLOT_BASIS.T
        frame[:, :3] = rotation[np.ix_(AXES, AXES)]
    return tuple(frame.flat)


def slot(key, parts, frame, required=False, fits=()):
    return key, parts, frame, int(required), fits


class Catalogue:
    def __init__(self, source, output, baked):
        self.source = source
        self.output = output
        self.baked = baked
        self.rows, self.sockets, self.builds = read_catalog(source / "catalog.txt")
        self.index = {row.split("\t", 1)[0]: i for i, row in enumerate(self.rows)}
        (output / "meshes").mkdir(parents=True, exist_ok=True)

    def part(self, ident, label, capacity=0, flags=MESH):
        label = ascii_label(label)[:89]
        index = self.index.get(ident)
        row = f"{ident}\t{capacity}\t{flags | colour(label) << 4}\t{label}"
        if index is None:
            self.index[ident] = len(self.rows)
            self.rows.append(row)
            self.sockets.append(["0", "0"])
        else:
            self.rows[index] = row

    def mesh(self, ident):
        return self.output / "meshes" / (ident + ".bin")

    def usable(self, ident, key, extras):
        if ident not in self.index and (self.baked / (ident + ".bin")).is_file():
            part = extras[ident]
            self.part(ident, part["name"], part["capacity"])
            copy2(self.baked / (ident + ".bin"), self.mesh(ident))
        if ident not in self.index:
            return False
        flags = int(self.rows[self.index[ident]].split("\t")[2])
        if not flags & MESH:
            return False
        if key.startswith("mod_scope"):
            profile = ident + ".opt"
            return bool(flags & SCOPE_MOUNT) or (bool(flags & OPTIC) and any(
                (folder / "optics" / profile).is_file() for folder in (self.source, self.output)))
        return True

    def sockets_for(self, ident, slots, conflicts=()):
        conflict_ids = [self.index[other] for other in conflicts if other in self.index]
        lines = []
        for key, allowed, frame, required, fits in slots:
            if not allowed:
                continue
            label = LABELS.get(key[4:], key[4:].replace("_", " ").upper())
            values = [key, label, str(required), str(len(allowed)),
                      " ".join(f"{x:.8g}" for x in frame),
                      " ".join(str(self.index[other]) for other in allowed)]
            if fits:
                extra = " ".join(f"{offset} " + " ".join(f"{x:.8g}" for x in pose)
                                 for offset, pose in fits)
                values.append(f"fits {len(fits)} {extra}")
            lines.append("\t".join(values))
        self.sockets[self.index[ident]] = [
            " ".join(map(str, [len(conflict_ids), *conflict_ids])), str(len(lines)), *lines]

    def stock(self, key, root, parts):
        self.builds[key] = self.index[root], [f"{path} {self.index[ident]}" for path, ident in parts.items()]


def fit_mp9(catalogue, parts):
    catalogue.part(MP9_FAMILY, "B&T MP9-N")
    copy2(catalogue.baked / (MP9_BLACK + ".bin"), catalogue.mesh(MP9_FAMILY))
    labels = {
        MP9_UPPER: "MP9-N upper (Black)", MP9_FDE_UPPER: "MP9 upper (FDE)",
        MP9_REAR: "MP9 rear sight", MP9_SIDE: "MP9 side rail",
        MP9_GRIP: "MP9-N vertical grip", MP9_MOUNT: "MP9 suppressor mount",
        "5de910da8b6c4240ba2651b5": "MP9 folding stock",
        "5de922d4b11454561e39239f": "MP9 charging handle",
    }
    magazines = next(s["parts"] for s in parts[MP9_BLACK]["sockets"] if s["name"] == "mod_magazine")
    for ident in magazines:
        labels[ident] = f"MP9 {parts[ident]['capacity']}-round magazine"
    for ident, label in labels.items():
        catalogue.part(ident, label, parts[ident]["capacity"], MESH | (ADAPTER if ident == MP9_MOUNT else 0))
        if not (catalogue.source / "meshes" / (ident + ".bin")).is_file():
            copy2(catalogue.baked / (ident + ".bin"), catalogue.mesh(ident))

    mount_tex, mount_vertices = read_mesh(catalogue.baked / (MP9_MOUNT + ".bin"))
    can_tex, can_vertices = read_mesh(catalogue.baked / (MP9_CAN + ".bin"))
    frame = next(s["frame"] for s in parts[MP9_MOUNT]["sockets"] if s["name"] == "mod_muzzle")
    corners = bytearray().join(CORNER.pack(*vertex) for vertex in mount_vertices)
    shift = (frame[3], frame[7], -frame[11])
    for x, y, z, u, v, r, g, b, a, texture in can_vertices:
        corners += CORNER.pack(x + shift[0], y + shift[1], z + shift[2], u, v, r, g, b, a,
                               texture + len(mount_tex))
    catalogue.part(MP9_KIT, "B&T MP9 sound suppressor", flags=MESH | SUPPRESSOR)
    save_mesh(catalogue.mesh(MP9_KIT), mount_tex + can_tex, corners)

    def mp9_slots(source):
        result = []
        for mount in parts[source]["sockets"]:
            key = mount["name"]
            if source == MP9_MOUNT and key == "mod_muzzle":
                continue
            options = [MP9_KIT, MP9_MOUNT] if source == MP9_BLACK and key == "mod_muzzle" else mount["parts"]
            allowed = [ident for ident in options if catalogue.usable(ident, key, parts)]
            result.append(slot(key, allowed, mount["frame"], mount["required"]))
        return result

    for ident in (MP9_UPPER, MP9_FDE_UPPER, MP9_MOUNT, MP9_SIDE):
        catalogue.sockets_for(ident, mp9_slots(ident))
    catalogue.sockets_for(MP9_KIT, mp9_slots(MP9_MOUNT))
    catalogue.sockets_for(MP9_FAMILY, mp9_slots(MP9_BLACK))
    catalogue.stock(11, MP9_FAMILY, {
        "mod_magazine": MP9_MAG, "mod_stock": "5de910da8b6c4240ba2651b5",
        "mod_foregrip": MP9_GRIP, "mod_reciever": MP9_UPPER,
        "mod_charge": "5de922d4b11454561e39239f", "mod_reciever/mod_sight_rear": MP9_REAR,
        "mod_reciever/mod_mount": MP9_SIDE,
    })


def optimise_models(prefab, source, directory, blender):
    models = {ident: list(prefab(ident)[0]) for ident in source}
    reductions = []
    for parts in models.values():
        for index, (_, points, _, uv, triangles, _) in enumerate(parts):
            if len(triangles) <= 8000:
                continue
            path = directory / f"part-{len(reductions)}.npz"
            np.savez(path, points=points, uv=uv, indices=triangles)
            reductions.append((parts, index, path))
    if reductions:
        tool = Path(__file__).with_name("blender") / "gevr_decimate_parts.py"
        subprocess.run([str(blender), "--background", "--factory-startup",
                        "--python-exit-code", "1", "--python", str(tool), "--", str(directory)], check=True)
        for parts, index, path in reductions:
            name, _, _, _, _, texture = parts[index]
            with np.load(path.with_name("reduced-" + path.name)) as model:
                parts[index] = (name, model["points"], model["normals"], model["uv"], model["indices"], texture)
    return models


def bake_unity_mesh(path, models, origin):
    textures, materials, faces = [], {}, bytearray()
    for _, points, normals, uv, triangles, texture in models:
        if texture.m_Name not in materials:
            materials[texture.m_Name] = len(textures)
            textures.append(rgba(texture.image.convert("RGBA")))
        material = materials[texture.m_Name]
        positions = (points - origin)[:, AXES] * (1, 1, -1)
        shades = np.rint(255 * (.66 + .34 * np.maximum(0, normals @ (.28, .87, -.4)))).clip(0, 255)
        for vertex in triangles.flat:
            u, v = uv[vertex][:2]
            shade = int(shades[vertex])
            faces += CORNER.pack(*positions[vertex], u, 1 - v, shade, shade, shade, 255, material)
    save_mesh(path, textures, faces)


def fit_evo(catalogue, parts, wtt, blender):
    source = json.loads((wtt / "db/CustomItems/WeaponCZScorpion.json").read_bytes())
    props = {ident: row["overrideProperties"] for ident, row in source.items()}

    @cache
    def prefab(ident):
        return prefabs(wtt / "bundles" / props[ident]["Prefab"]["path"], anchor_frames=True)

    def choices(mount):
        return list(dict.fromkeys(ident for group in mount["_props"]["filters"] for ident in group["Filter"]))

    def part_id(ident):
        return EVO_LEGACY_IDS.get(ident, EVO_FAMILY if ident == EVO_SOURCE else ident)

    _, root_frames = prefab(EVO_SOURCE)
    origins = {EVO_SOURCE: np.array((.1327, -.164, .310))}
    for mount in props[EVO_SOURCE]["Slots"]:
        for ident in choices(mount):
            if ident in source:
                origins[ident] = root_frames[mount["_name"]][:3, 3]
    # These meshes were exported on a particular variant. Their installed
    # frame is relative to the receiving barrel or handguard, not that variant.
    reference_seats = {
        "687b03bc8b8590063f73f01c": ("687afeb41db9f3580324752a", "mod_muzzle"),
        EVO_FLASH: ("6881bc7a6b3db974a22e848d", "mod_muzzle"),
        "6884464a25d2c3593b5ae0e4": ("688445ff01082f16b3c42d50", "mod_muzzle"),
        EVO_SUPPRESSOR: ("688445ff01082f16b3c42d50", "mod_muzzle"),
        EVO_FRONT: (EVO_HANDGUARD, "mod_sight_front"),
        "687fb676e4996d45517fe236": (EVO_HANDGUARD, "mod_tactical_004"),
        "68827aaba06fa1cf759ec88d": ("6884454536540bf509854155", "mod_tactical_004"),
    }
    for ident, (parent, key) in reference_seats.items():
        origins[ident] = prefab(parent)[1][key][:3, 3]

    def bake(ident, name, models, origin, capacity=0, flags=MESH):
        catalogue.part(ident, name, capacity, flags)
        bake_unity_mesh(catalogue.mesh(ident), models, origin)

    with TemporaryDirectory(dir=catalogue.output) as tmp:
        models = optimise_models(prefab, source, Path(tmp), blender)
    root = models[EVO_SOURCE]
    bake(EVO_FAMILY, "CZ Scorpion EVO 3", [model for model in root if model[0] != "cz_scorpion_bolt"],
         origins[EVO_SOURCE])
    bake(EVO_BOLT, "Bolt", [model for model in root if model[0] == "cz_scorpion_bolt"],
         root_frames["patron_in_weapon"][:3, 3])
    for ident, row in source.items():
        if ident == EVO_SOURCE:
            continue
        name = row["locales"]["en"]["name"].replace("CZ Scorpion EVO 3 A1 ", "").replace("CZ Scorpion EVO 3 ", "")
        cap = props[ident].get("Cartridges", [{}])[0].get("_max_count", 0)
        if ident == "6888e4dd5a5e2e5dd16409f9":
            cap = 50
        # The transparent magazine already uses its attachment point as origin.
        origin = np.zeros(3) if ident == EVO_CLEAR_MAG else origins[ident]
        bake(part_id(ident), name, models[ident], origin, cap, MESH | (SUPPRESSOR if ident == EVO_SUPPRESSOR else 0))

    def wtt_slots(ident):
        _, frames = prefab(ident)
        origin = origins[ident]
        result = []
        for mount in props[ident].get("Slots", []):
            key = mount["_name"]
            allowed = [option for option in choices(mount)
                       if option in source or catalogue.usable(option, key, parts)]
            if not allowed:
                continue
            frame = socket_frame(origin, frames[key], orient=any(option not in source for option in allowed))
            authored = socket_frame(origin, frames[key])
            fits = [(i, authored) for i, option in enumerate(allowed) if option in source and authored != frame]
            result.append(slot(key, [part_id(option) for option in allowed], frame, mount["_required"], fits))
        if ident == EVO_SOURCE:
            result.append(slot("mod_bolt", [EVO_BOLT], socket_frame(origin, root_frames["patron_in_weapon"]), True))
        return result

    # Resolve all generic additions before writing conflicts keyed by part ID.
    mounts = {ident: wtt_slots(ident) for ident in source}
    for ident, slots in mounts.items():
        conflicts = props[ident].get("ConflictingItems", [])
        catalogue.sockets_for(part_id(ident), slots, [part_id(part) for part in conflicts])
    catalogue.stock(12, EVO_FAMILY, {
        "mod_magazine": part_id("6881c0fa3be04213015b7e34"), "mod_handguard": EVO_HANDGUARD,
        "mod_stock": part_id("687aff780a062c4f04628738"), "mod_barrel": EVO_BARREL,
        "mod_pistol_grip": "687b001103b1d64f98f261e7", "mod_sight_rear": EVO_REAR,
        "mod_charge": part_id("687b00468b4fafd5ebb3a763"), "mod_bolt": EVO_BOLT,
        "mod_handguard/mod_sight_front": EVO_FRONT, "mod_barrel/mod_muzzle": part_id(EVO_FLASH),
    })


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("gear", type=Path)
    parser.add_argument("zomboid", type=Path, help="Extracted parts.json and baked meshes")
    parser.add_argument("wtt", type=Path, help="Extracted WTT Armory mod directory")
    parser.add_argument("output", type=Path)
    parser.add_argument("--blender", default="blender", help="Blender executable for attachment LOD conversion")
    args = parser.parse_args()
    parts = {part["id"]: part for part in json.loads((args.zomboid / "parts.json").read_bytes())}
    catalogue = Catalogue(args.gear, args.output, args.zomboid / "meshes")
    fit_mp9(catalogue, parts)
    fit_evo(catalogue, parts, args.wtt, args.blender)
    if len(catalogue.rows) > 1536:
        raise ValueError("Catalogue exceeds the engine's part limit")
    write_catalog(args.output / "catalog.txt", catalogue.rows, catalogue.sockets, catalogue.builds)


if __name__ == "__main__":
    main()
