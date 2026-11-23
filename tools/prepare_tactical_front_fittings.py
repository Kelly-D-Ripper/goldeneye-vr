"""Repair missing native parts and share in-game front fittings within each receiver."""

import argparse
import json
from pathlib import Path
from shutil import copy2
import subprocess

from prepare_tactical_attachment_pass import muzzle_pose_from_mesh, options, set_socket, socket_text, socket_with
from prepare_tactical_gear import extract_parts
from prepare_tactical_smg_attachments import ADAPTER, MESH, SCOPE_MOUNT, SUPPRESSOR, Catalogue, slot, write_catalog
from prepare_tactical_workshop_ease import suppressor


MPX_SD_BARRELS = ("58aeaaa886f7744fc1560f81", "67614994e889e1972605d6bb", "676149a3e2cf1419500357eb")
MPX_SD_CANS = {"58aeac1b86f77457c419f475", "676149c5062e6212f5058c36", "676149d8e889e1972605d6be"}
M1A_CHASSIS = "5addc7005acfc4001669f275"
M1A_SAGE_STOCK = "5addc7ac5acfc400194dbd90"
M1A_SAGE_RAIL = "5addbffe5acfc4001714dfac"
L85 = "66c549879f8f2447575393cc"
# Derived finishes use the same emitters as the underlying attachment.
DEVICE_MODES = {
    1024: ("AN/PEQ-15", "AN/PEQ-2", "LA-5B/PEQ", "LS321", "Perst-3", "RAPTAR",
           "LAS/TAC 2", "TBL", "TT PLS", "MAWL-C1+", "NGAL"),
    2048: ("XC1", "Klesch-2U", "WMX200", "WF501B", "2D", "X300", "M600", "M300",
           "TLR-1", "M340", "PL-Pro"),
    3072: ("Klesch-2P", "X400U", "K-2IKS", "GTL 21", "DBAL-PL", "BaldrPro", "Baldr Pro",
           "XVL2", "TLR-2", "LCAM"),
}
AUTHORED_REPAIRS = {
    **{part: ("mod_muzzle",) for part in MPX_SD_BARRELS},
    M1A_CHASSIS: ("mod_stock", "mod_scope"),
    "5ab372a310e891001717f0d8": ("mod_stock", "mod_pistolgrip"),
    "5aafa1c2e5b5b00015042a56": ("mod_sight_front",),
    "5ab3afb2d8ce87001660304d": ("mod_muzzle",),
    "5addbac75acfc400194dbc56": ("mod_muzzle",),
}


def meshes_for_repairs(catalogue, archive, work, blender):
    folder = work / "source-parts"
    owners, _ = extract_parts(archive, folder, AUTHORED_REPAIRS, include_children=False)
    needed = {
        part for owner in owners for s in owner["sockets"] for part in s["parts"]
        if s["name"] in AUTHORED_REPAIRS[owner["id"]] and part not in catalogue.index
        and (s["name"] != "mod_scope" or part == M1A_SAGE_RAIL)
    }
    raw, _ = extract_parts(archive, folder, sorted(needed), include_children=False)
    required = {
        part for item in raw for s in item["sockets"] for part in s["parts"]
        if s["required"] and part not in catalogue.index and part not in needed
    }
    if required:
        children, _ = extract_parts(archive, folder, sorted(required), include_children=False)
        raw += children
    parts = [p for p in raw if p["mesh"]]
    if (MPX_SD_CANS | {M1A_SAGE_STOCK, M1A_SAGE_RAIL}) - {p["id"] for p in parts}:
        raise ValueError("Missing an MPX or M1A prefab in the supplied pack")
    (folder / "parts.json").write_text(json.dumps(parts), encoding="utf8")
    subprocess.run([str(blender), "-b", "--factory-startup", "--python-exit-code", "1", "--python",
                    str(Path(__file__).with_name("bake_tactical_gear.py")), "--", str(folder), str(catalogue.baked)],
                   check=True)
    for p in parts:
        ident = p["id"]
        label = p.get("full_name", p["name"])
        flags = MESH
        if ident in MPX_SD_CANS or suppressor(label):
            flags |= SUPPRESSOR
        if ("thread" in label.lower() or "adapter" in label.lower()) and p["sockets"]:
            flags |= ADAPTER
        if ident == M1A_SAGE_RAIL:
            flags |= SCOPE_MOUNT
        name = "M14ALCS buttstock" if ident == M1A_SAGE_STOCK else p["name"]
        catalogue.part(ident, name, p["capacity"], flags)
        copy2(catalogue.baked / (ident + ".bin"), catalogue.mesh(ident))
    return owners, parts


def extend_socket(catalogue, ident, key, extra, fallback=None, required=None):
    old = socket_with(catalogue, ident, key)
    if old:
        fields = old.split("\t")
        parts = list(dict.fromkeys((*options(old), *extra)))
        fields[3] = str(len(parts))
        fields[5] = " ".join(map(str, parts))
        if required is not None:
            fields[2] = str(int(required))
        line = "\t".join(fields)
    else:
        label, pose, default_required = fallback
        line = socket_text(key, label, default_required if required is None else required, pose, extra)
    set_socket(catalogue, ident, key, line)


def restore_authored_slots(catalogue, owners, parts):
    for part in parts:
        opts = [slot(s["name"], [x for x in s["parts"] if catalogue.usable(x, s["name"], {})],
                     s["frame"], s["required"]) for s in part["sockets"]
                if not s["name"].startswith(("mod_launcher", "mod_grenade"))]
        catalogue.sockets_for(part["id"], opts, part["conflicts"])
    for part in owners:
        ident = part["id"]
        for s in part["sockets"]:
            name = s["name"]
            if name not in AUTHORED_REPAIRS[ident]:
                continue
            fit = [catalogue.index[x] for x in s["parts"] if catalogue.usable(x, name, {})]
            if fit:
                extend_socket(catalogue, ident, name, fit, (s["label"], s["frame"], s["required"]))


def shared_front_fittings(catalogue):
    ids = list(catalogue.index)
    for _, block in enumerate(catalogue.sockets):
        barrels = next((row for row in block[2:] if row.startswith("mod_barrel\t")), None)
        if not barrels or len(options(barrels)) < 2 or not any(
                row.startswith("mod_handguard\t") for row in block[2:]):
            continue
        parts = [ids[i] for i in options(barrels)]
        for key in ("mod_muzzle", "mod_gas_block"):
            sockets = {ident: socket_with(catalogue, ident, key) for ident in parts}
            # A barrel with two independently authored muzzle seats keeps
            # those paths, including the parts saved by old loadouts.
            union = list(dict.fromkeys(part for row in sockets.values() if row for part in options(row)))
            if not union:
                continue
            for ident, row in sockets.items():
                if row:
                    extend_socket(catalogue, ident, key, union)
                elif key == "mod_muzzle" and not any(
                        s.startswith("mod_muzzle_") for s in catalogue.sockets[catalogue.index[ident]][2:]):
                    pose = muzzle_pose_from_mesh(catalogue.source, ident)
                    extend_socket(catalogue, ident, key, union, ("MUZZLE", pose, False))


def fit_l85_magazines(catalogue):
    # The imported L85 is at native scale. The shared STANAG meshes were
    # authored for the AR receiver's 1.82 scale. Scale about the L85 mag well.
    row = socket_with(catalogue, L85, "mod_magazine")
    fields = row.split("\t")
    if len(fields) != 6:
        raise ValueError("L85 magazine has a separate fit")
    pose = [float(value) * (1.82 if i % 4 < 3 else 1.0) for i, value in enumerate(fields[4].split())]
    set_socket(catalogue, L85, "mod_magazine",
               socket_text(fields[0], fields[1], int(fields[2]), pose, options(row)))


def mark_emitters(catalogue):
    possible = {index for block in catalogue.sockets for row in block[2:]
                if row.startswith("mod_tactical") for index in options(row)}
    for index in possible:
        fields = catalogue.rows[index].split("\t")
        name = fields[3].lower()
        for mask, models in DEVICE_MODES.items():
            if any(name == model.lower() or name.startswith(model.lower() + " ") for model in models):
                fields[2] = str(int(fields[2]) | mask)
                catalogue.rows[index] = "\t".join(fields)
                break


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("gear", type=Path)
    parser.add_argument("archive", type=Path)
    parser.add_argument("work", type=Path)
    parser.add_argument("blender", type=Path)
    args = parser.parse_args()
    catalogue = Catalogue(args.gear, args.work / "gear", args.work / "baked")
    owners, parts = meshes_for_repairs(catalogue, args.archive, args.work, args.blender)
    restore_authored_slots(catalogue, owners, parts)
    shared_front_fittings(catalogue)
    fit_l85_magazines(catalogue)
    mark_emitters(catalogue)
    write_catalog(catalogue.output / "catalog.txt", catalogue.rows, catalogue.sockets, catalogue.builds)
    print(f"Prepared {len(catalogue.builds)} Workshop assemblies and {len(parts)} restored meshes.")


if __name__ == "__main__":
    main()
