"""Separate the Tactical viewmodels' moving bolts using the original mesh components."""
import argparse
import json
from pathlib import Path
from shutil import copy2
from zipfile import ZipFile

from prepare_tactical_ar_rifles import IDENTITY
from prepare_tactical_attachment_pass import set_socket, socket_text
from prepare_tactical_expanded import ACTION_BOUNDS, save_mesh
from prepare_tactical_gear import extract_parts
from prepare_tactical_roster import CORNER, read_mesh, stable_id
from prepare_tactical_smg_attachments import Catalogue, write_catalog


MOVING = {
    "66992b349950f5f4cd06029f": ("Uzi", ("weapon_bolt",), None),
    "62e14904c2699c0ec93adc47": ("SR-2M", ("weapon_bolt", "weapon_bolt_carrier"), "weapon_charging_handle"),
    "5e00903ae9dc277128008b87": ("MP9-N", ("weapon_bolt",), None),
    "64637076203536ad5600c990": ("PKM", ("weapon_bolt", "weapon_bolt_carrier"), "weapon_charge_handle"),
    "5aafa857e5b5b00018480968": ("M1A", ("weapon_bolt",), None),
    "5c46fbd72e2216398b5a8c9c": ("SVDS", ("weapon_bolt", "weapon_bolt_larva"), None),
}
WOOD = "6657bc8faeddd6b0a9b40224"


def source_parts(archive, work):
    parts, _ = extract_parts(archive, work, dict.fromkeys(MOVING), include_children=False)
    roots = {p["id"]: p for p in parts}
    records = []
    with ZipFile(archive) as pack:
        members = {name.rsplit("/", 1)[-1]: name for name in pack.namelist()
                   if "/models_X/TW/Moving/" in name and name.endswith(".fbx")}
        for root, (label, bolt, handle) in MOVING.items():
            for component in ("body", *bolt, *((handle,) if handle else ())):
                name = f"{root}_{component}.fbx"
                path = Path("models_X/TW/Moving") / name
                (work / path).parent.mkdir(parents=True, exist_ok=True)
                (work / path).write_bytes(pack.read(members[name]))
                records.append(dict(roots[root], id=stable_id(f"{root}:{component}"),
                                    name=f"{label} {component}", mesh=path.as_posix(),
                                    triangle_limit=5200 if component == "body" else 2200))
    (work / "parts.json").write_text(json.dumps(records, indent=2), encoding="utf-8")


def write_components(path, components):
    textures, data = [], bytearray()
    for materials, faces in components:
        for v in faces:
            data.extend(CORNER.pack(*v[:-1], v[-1] + len(textures)))
        textures.extend(materials)
    if not data:
        raise ValueError(f"No geometry for {path}")
    save_mesh(path, textures, data)


def reject_box(faces, bounds):
    low, high = bounds
    included = []
    for i in range(0, len(faces), 3):
        face = faces[i:i + 3]
        centre = [sum(v[axis] for v in face) / 3 for axis in range(3)]
        if not all(a <= p <= b for a, p, b in zip(low, centre, high, strict=True)):
            included.extend(face)
    return included


def integrate(tactical, work):
    source_meshes = work / "baked"
    catalogue = Catalogue(tactical / "gear", work / "gear", source_meshes)

    def add_bolt(root, components):
        ident = stable_id(f"{root}:mod_bolt:moving")
        catalogue.part(ident, "Bolt")
        write_components(catalogue.mesh(ident), components)
        set_socket(catalogue, root, "mod_bolt", socket_text("mod_bolt", "Bolt", 2, IDENTITY,
                                                           [catalogue.index[ident]]))

    for root, (label, bolt, handle) in MOVING.items():
        def component(name):
            return read_mesh(source_meshes / (stable_id(f"{root}:{name}") + ".bin"))

        material, body = component("body")
        if label == "Uzi":
            # Keep the original separate cocking knob used for manual pulls.
            body = reject_box(body, ACTION_BOUNDS[9])
        elif label == "M1A":
            # The existing operating handle lives in the same receiver cutout.
            body = reject_box(body, ((-.027, .151, -.034), (-.012, .211, -.001)))
        write_components(catalogue.mesh(root), [(material, body)])
        add_bolt(root, [component(part) for part in bolt])
        if handle:
            slot = next(row.split("\t") for row in catalogue.sockets[catalogue.index[root]][2:]
                        if row.startswith("mod_charge\t"))
            if slot[4].split() != [str(int(v)) for v in IDENTITY]:
                raise ValueError(f"Non-identity handle mount for {label}")
            index = int(slot[5].split()[0])
            ident = catalogue.rows[index].split("\t")[0]
            copy2(source_meshes / (stable_id(f"{root}:{handle}") + ".bin"), catalogue.mesh(ident))
        print(f"{label}: installed separate bolt", flush=True)

    # The wooden SVD already groups the moving geometry as material 1 in its
    # WTT mesh. The selected variant supplies its own bolt and rest pose.
    from prepare_tactical_roster import write_section
    wood = tactical / "svd-wood.bin"
    write_section(catalogue.mesh(WOOD), wood, set(range(2, 9)))
    ident = stable_id(f"{WOOD}:mod_bolt:moving")
    catalogue.part(ident, "Bolt")
    write_section(catalogue.mesh(ident), wood, {1})
    set_socket(catalogue, WOOD, "mod_bolt", socket_text("mod_bolt", "Bolt", 2, IDENTITY,
                                                       [catalogue.index[ident]]))
    write_catalog(catalogue.output / "catalog.txt", catalogue.rows, catalogue.sockets, catalogue.builds)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tactical", type=Path)
    parser.add_argument("work", type=Path)
    parser.add_argument("--archive", type=Path, help="Gather FBX input for bake_tactical_gear.py")
    args = parser.parse_args()
    if args.archive:
        source_parts(args.archive, args.work / "source-parts")
    else:
        integrate(args.tactical, args.work)
