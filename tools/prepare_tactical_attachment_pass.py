"""Restore missing Workshop links while preserving existing part and preset IDs."""

import argparse
import json
from pathlib import Path
from shutil import copy2
import subprocess

from prepare_tactical_ar_rifles import compose
from prepare_tactical_gear import extract_parts
from prepare_tactical_roster import read_mesh
from prepare_tactical_smg_attachments import (ADAPTER, MESH, SUPPRESSOR, Catalogue,
                                             slot, write_catalog)


AUTHORED_SLOTS = {
    "5d3eb59ea4b9361c284bb4b2": ("mod_muzzle",),   # Five-seveN threaded barrel
    "571a279b24597720b4066566": ("mod_muzzle",),   # TT threaded barrel
    "66992b349950f5f4cd06029f": ("mod_barrel", "mod_stock", "mod_handguard"),
    "62e14904c2699c0ec93adc47": ("mod_stock",),
    "64637076203536ad5600c990": ("mod_stock", "mod_barrel", "mod_handguard"),
    "646371faf2404ab67905c8e9": ("mod_muzzle",),   # PKM barrel
}
SUPPRESSORS = {"571a28e524597720b4066567", "5d3ef698a4b9361182109872"}
NINE_MM_SUPPRESSORS = ("5a32a064c4a28200741e22de", "5c6165902e22160010261b28")
MP7_ROOTS = ("5ba26383d4351e00334c93d9", "5bd70322209c4d00d7167b8f")
MP7_ADAPTER = "5ba26acdd4351e003562908e"
ROTEX = "5ba26ae8d4351e00367f9bdb"


def options(row):
    fields = row.split("\t")
    return tuple(map(int, fields[5].split()[:int(fields[3])]))


def socket_with(catalogue, part, key):
    block = catalogue.sockets[catalogue.index[part]]
    return next((row for row in block[2:] if row.startswith(key + "\t")), None)


def socket_text(key, label, required, pose, allowed):
    return "\t".join((key, label, str(int(required)), str(len(allowed)),
                      " ".join(f"{x:.8g}" for x in pose), " ".join(map(str, allowed))))


def set_socket(catalogue, part, key, text):
    block = catalogue.sockets[catalogue.index[part]]
    for index, row in enumerate(block[2:], 2):
        if row.startswith(key + "\t"):
            block[index] = text
            return
    block.append(text)
    block[1] = str(len(block) - 2)


def authored_parts(catalogue, archive, work):
    folder = work / "source-parts"
    records, _ = extract_parts(archive, folder, AUTHORED_SLOTS, include_children=False)
    owners = {p["id"]: p for p in records}
    new = {part for ident, keys in AUTHORED_SLOTS.items() for s in owners[ident]["sockets"]
           if s["name"] in keys for part in s["parts"] if part not in catalogue.index}
    extra, _ = extract_parts(archive, folder, sorted(new), include_children=False)
    # Bring along the factory fittings for the new barrel choices. Existing
    # optics, grips and rail accessories in the shared catalogue are reused.
    required = {part for p in extra for s in p["sockets"] if s["required"]
                for part in s["parts"] if part not in catalogue.index and part not in new}
    if required:
        dependencies, _ = extract_parts(archive, folder, sorted(required), include_children=False)
        extra.extend(dependencies)
    return folder, owners, [p for p in extra if p["mesh"]]


def install_authored(catalogue, owners, extra):
    for part in extra:
        ident = part["id"]
        name = part["name"]
        if ident in SUPPRESSORS and "supp" not in name.lower():
            name += " suppressor"
        flags = MESH | (SUPPRESSOR if ident in SUPPRESSORS or "suppressor" in name.lower() else 0)
        if any(s["name"].startswith("mod_muzzle") for s in part["sockets"]) and "thread" in name.lower():
            flags |= ADAPTER
        catalogue.part(ident, name, part["capacity"], flags)
        copy2(catalogue.baked / (ident + ".bin"), catalogue.mesh(ident))
    for part in extra:
        # No new assembly may name a part that lacks its own model/profile.
        picks = [slot(s["name"], [p for p in s["parts"] if catalogue.usable(p, s["name"], {})],
                      s["frame"], s["required"])
                 for s in part["sockets"] if not s["name"].startswith(("mod_launcher", "mod_grenade"))]
        catalogue.sockets_for(part["id"], picks, part["conflicts"])
    for ident, keys in AUTHORED_SLOTS.items():
        for source in owners[ident]["sockets"]:
            key = source["name"]
            if key not in keys:
                continue
            old = socket_with(catalogue, ident, key)
            if old:
                fields = old.split("\t")
                if len(fields) != 6:
                    raise ValueError(f"Existing per-part fit data on {ident}:{key}")
                have = list(options(old))
                pose = tuple(map(float, fields[4].split()))
                required = int(fields[2])
                label = fields[1]
            else:
                have, pose, required, label = [], source["frame"], source["required"], source["label"]
            allowed = have + [catalogue.index[p] for p in source["parts"]
                              if catalogue.usable(p, key, {}) and catalogue.index[p] not in have]
            if allowed:
                set_socket(catalogue, ident, key, socket_text(key, label, required, pose, allowed))


def muzzle_pose_from_mesh(gear, part, bore=None):
    _, vertices = read_mesh(gear / "meshes" / (part + ".bin"))
    if bore is None:
        end = max(v[1] for v in vertices)
        ring = [v for v in vertices if v[1] > end - .0001]
        x, z = ((min(v[axis] for v in ring) + max(v[axis] for v in ring)) * .5 for axis in (0, 2))
    else:
        # MP5 and MP5K use the same bore height in the receiver's local axes.
        x, z = 0, bore
        end = max(v[1] for v in vertices if abs(v[2] - z) <= .004)
    return (1, 0, 0, x, 0, 1, 0, end - .004, 0, 0, 1, -z)


def expose_five_seven_muzzle(catalogue):
    factory = "5d3eb5b6a4b9361eab311902"
    threaded = "5d3eb59ea4b9361c284bb4b2"
    # The supplied SFN rear face starts at the thread seat, less than one
    # mesh millimetre ahead of the stock barrel. Reuse that seat in Workshop.
    muzzle = socket_with(catalogue, threaded, "mod_muzzle")
    if not muzzle or not options(muzzle):
        raise ValueError("The Five-seveN suppressor seat is missing")
    set_socket(catalogue, factory, "mod_muzzle", muzzle)
    mount = catalogue.index["5d7b6bafa4b93652786f4c76"]
    fields = catalogue.rows[mount].split("\t")
    fields[-1] = "5-7 RMR mount"
    catalogue.rows[mount] = "\t".join(fields)


def fix_muzzles(catalogue, gear):
    expose_five_seven_muzzle(catalogue)
    child = socket_with(catalogue, MP7_ADAPTER, "mod_muzzle")
    if options(child) != (catalogue.index[ROTEX],):
        raise ValueError("MP7 adapter changed")
    child_pose = tuple(map(float, child.split("\t")[4].split()))
    for part in MP7_ROOTS:
        old = socket_with(catalogue, part, "mod_muzzle")
        if options(old) != (catalogue.index[MP7_ADAPTER],):
            raise ValueError("MP7 factory muzzle changed")
        pose = tuple(map(float, old.split("\t")[4].split()))
        allowed = [catalogue.index[p] for p in (MP7_ADAPTER, ROTEX)]
        fit = " ".join(f"{value:.8g}" for value in compose(pose, child_pose))
        line = socket_text("mod_muzzle", "MUZZLE", False, pose, allowed) + f"\tfits 1 1 {fit}"
        set_socket(catalogue, part, "mod_muzzle", line)

    # Fit the in-game suppressor models to the two measured barrel mesh seats.
    for part, bore in (("5d2f261548f03576f500e7b7", -.0291186),  # MP5K receiver
                       ("460ae1cd7013c1c68a757c48", None)):       # CZ 75B threaded barrel
        pose = muzzle_pose_from_mesh(gear, part, bore)
        line = socket_text("mod_muzzle", "MUZZLE", False, pose,
                           [catalogue.index[p] for p in NINE_MM_SUPPRESSORS])
        set_socket(catalogue, part, "mod_muzzle", line)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("gear", type=Path)
    parser.add_argument("archive", type=Path)
    parser.add_argument("work", type=Path)
    parser.add_argument("blender", type=Path)
    args = parser.parse_args()
    output = args.work / "gear"
    baked = args.work / "baked"
    catalogue = Catalogue(args.gear, output, baked)
    folder, owners, extra = authored_parts(catalogue, args.archive, args.work)
    (folder / "parts.json").write_text(json.dumps(extra, indent=2), encoding="utf8")
    subprocess.run([str(args.blender), "-b", "--factory-startup", "--python-exit-code", "1", "--python",
                    str(Path(__file__).with_name("bake_tactical_gear.py")), "--", str(folder), str(baked)],
                   check=True)
    install_authored(catalogue, owners, extra)
    fix_muzzles(catalogue, args.gear)
    write_catalog(output / "catalog.txt", catalogue.rows, catalogue.sockets, catalogue.builds)
    print(f"{len(catalogue.builds)} Workshop assemblies, {len(extra)} restored part meshes.")


if __name__ == "__main__":
    main()
