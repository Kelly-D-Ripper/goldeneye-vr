"""Expose authored suppressor fits directly and group the Workshop's part finishes."""
import argparse
import json
import re
import shutil
import struct
from collections import Counter
from pathlib import Path
from zipfile import ZipFile

from prepare_tactical_ar_full import read_catalog
from prepare_tactical_ar_rifles import compose, stable_id
from prepare_tactical_expanded import save_mesh
from prepare_tactical_roster import CORNER, read_mesh


MESH, SUPPRESSOR, SCOPE_MOUNT = 1, 2, 8
LEGACY_CHOICE, OPTIC = 128, 256
AUTOMATIC = "2"
COLOUR_SHIFT, COLOUR_MASK = 4, 7
COLOURS = ("Black", "Tan", "Green", "Red", "Other")
MUZZLE = "mod_muzzle"
MUZZLE_PATHS = {MUZZLE, "mod_mount"}


def part_names(archive):
    prefix = "EscapeFromZomboidFirearms.P_"
    with ZipFile(archive) as bundle:
        translation = next(name for name in bundle.namelist()
                           if name.endswith("/42/media/lua/shared/Translate/EN/ItemName.json"))
        names = {key[len(prefix):]: name for key, name in json.loads(bundle.read(translation)).items()
                 if key.startswith(prefix)}
    return {**names, **{stable_id(key + ":full AR"): name for key, name in names.items()}}


def colour(title):
    title = title.lower()
    finish = re.search(r"\(([^()]+)\)\s*$", title)
    if finish:
        shade = finish[1]
        if shade == "black":
            return 0
        if re.search(r"\b(?:tan|fde|ddc|sand|khaki|earth|coyote|bronze|ral 8000)\b", shade):
            return 1
        if re.search(r"\b(?:green|olive|od|o\.d\.)\b", shade):
            return 2
        if re.search(r"\bred\b", shade):
            return 3
        if re.search(r"\b(?:white|grey|gray|plum|yellow|brown|wood)\b", shade):
            return 4
    if re.search(r"\b(?:tan|fde|ddc|desert|khaki|coyote|flat dark earth|sand)\b", title):
        return 1
    if re.search(r"\b(?:olive drab|foliage green|forest green|ranger green|green (?:polymer|wood|stock|grip))\b", title):
        return 2
    if re.search(r"\b(?:anodi[sz]ed red|red (?:polymer|handguard|stock|grip|mount))\b", title):
        return 3
    if re.search(r"\b(?:wood|wooden|laminated|plum|white|grey|gray|brown factory grip)\b", title):
        return 4
    return 0


def suppressor(title):
    return (
        re.search(r"\b(?:suppressor|silencer|sound moderator)\b", title, re.IGNORECASE)
        and not re.search(r"\b(?:flash suppressor|suppressor adapter|suppressor cover)\b", title, re.IGNORECASE)
    )


def muzzle_socket(line):
    key, label, required, count, frame, options = line.split("\t", 5)
    fields = options.split()
    count = int(count)
    allowed = list(map(int, fields[:count]))
    if len(allowed) != count:
        raise ValueError(f"Incomplete muzzle choice list in {line}")
    if fields[count:]:
        raise ValueError(f"Unexpected muzzle fit data in {line}")
    return key, label, required, tuple(map(float, frame.split())), allowed


def direct_suppressors(sockets, suppressors, titles, ak_roots):
    branches = [
        [muzzle_socket(line) for line in block[2:] if line.split("\t", 1)[0] in MUZZLE_PATHS]
        for block in sockets
    ]

    def descendants(part, parent, seen):
        for key, _, _, frame, allowed in branches[part]:
            local = compose(parent, frame)
            for choice in allowed:
                if choice in seen:
                    continue
                if choice in suppressors and key == MUZZLE:
                    yield choice, local
                elif choice not in suppressors:
                    yield from descendants(choice, local, seen | {choice})

    total = 0
    for owner, block in enumerate(sockets):
        for index, line in enumerate(block[2:], 2):
            if not line.startswith(MUZZLE + "\t"):
                continue
            key, label, required, frame, allowed = muzzle_socket(line)
            fits = {}
            for part in allowed:
                if part in suppressors:
                    continue
                for child, pose in descendants(part, frame, {owner, part}):
                    # Keep conversion-only AR devices on their original adapter
                    # branch. Only authored shared models get a direct AK choice.
                    conversion = owner in ak_roots and "CNC Warrior" in titles[part]
                    shared = "multi-caliber" in titles[child].lower()
                    if not conversion or shared:
                        fits.setdefault(child, pose)
            additions = [(part, frame) for part, frame in fits.items() if part not in allowed]
            if not additions:
                continue
            first = len(allowed)
            allowed.extend(part for part, _ in additions)
            fitdata = " ".join(
                f"{first + offset} " + " ".join(f"{value:.8g}" for value in pose)
                for offset, (_, pose) in enumerate(additions)
            )
            block[index] = "\t".join((key, label, required, str(len(allowed)), line.split("\t")[4],
                                      " ".join(map(str, allowed)), f"fits {len(additions)} {fitdata}"))
            if len(block[index]) >= 8191:
                raise ValueError(f"Too many direct fits for part {owner}")
            total += len(additions)
    return total


def slot_options(line):
    key, _, _, count, _, options = line.split("\t", 5)
    return key, list(map(int, options.split()[:int(count)]))


def scope_mounts(sockets, optics):
    candidates = {part for block in sockets for line in block[2:]
                  for key, options in [slot_options(line)]
                  if key.startswith(("mod_scope", "mod_mount")) for part in options}
    scope_hosts = {part for part, block in enumerate(sockets)
                   if any(line.startswith("mod_scope") for line in block[2:])}
    # Lower dovetails and risers can provide a further mounting slot instead
    # of a direct scope seat. Follow mount-only edges to classify them too.
    mounts = scope_hosts - optics
    while True:
        additions = {part for part in candidates - mounts - optics for line in sockets[part][2:]
                     for key, options in [slot_options(line)] if key.startswith("mod_mount")
                     and any(choice in mounts or choice in optics for choice in options)}
        if not additions:
            return mounts & candidates
        mounts.update(additions)


def with_automatic_child(line, first, keep_alternates):
    key, label, _, frame, options = muzzle_socket(line)
    remaining = [part for part in options if part != first] if keep_alternates else []
    allowed = [first, *remaining]
    return "\t".join((key, label, AUTOMATIC, str(len(allowed)), " ".join(map(str, frame)),
                      " ".join(map(str, allowed))))


def assemble_choices(rows, sockets, titles, source_meshes, destination_meshes):
    handguards = {part for block in sockets for line in block[2:]
                  for key, options in [slot_options(line)] if key == "mod_handguard" for part in options}
    aliases = {}

    def clone(owner, slot, child, label):
        ident, capacity, flags, _ = rows[owner].split("\t", 3)
        child_id = rows[child].split("\t", 1)[0]
        new_id = stable_id(f"{ident}:assembly:{child_id}")
        flag_bits = int(flags) & ~LEGACY_CHOICE
        rows.append("\t".join((new_id, capacity, str(flag_bits), label)))
        new_sockets = list(sockets[owner])
        new_sockets[slot] = with_automatic_child(new_sockets[slot], child, False)
        sockets.append(new_sockets)
        shutil.copyfile(source_meshes / (ident + ".bin"), destination_meshes / (new_id + ".bin"))
        aliases.setdefault(owner, []).append(len(rows) - 1)

    for owner, title in enumerate(titles):
        _, _, flags, label = rows[owner].split("\t", 3)
        group = colour(title)
        for slot, original in enumerate(sockets[owner][2:], 2):
            key, choices = slot_options(original)
            if owner in handguards and key == "mod_handguard":
                if "GRIDLOK base" in title:
                    for child in choices:
                        length = re.search(r"([\d.]+) inch", titles[child])[1]
                        finish = re.search(r"\(([^()]+)\)$", title)[1]
                        clone(owner, slot, child, f'GRIDLOK {length}" ({finish})')
                    rows[owner] = rows[owner].replace(f"\t{flags}\t", f"\t{int(flags) | LEGACY_CHOICE}\t", 1)
                matching = [part for part in choices if colour(titles[part]) == group]
                sockets[owner][slot] = with_automatic_child(original, (matching or choices)[0], True)
            elif "Geissele Super Precision" in title and "ring scope mount" in title and key == "mod_mount":
                matching = [part for part in choices if colour(titles[part]) == group]
                cap = next(part for part in matching if "top ring cap" in titles[part])
                piggyback = next(part for part in matching if "ROF-90" in titles[part])
                clone(owner, slot, piggyback, label + " + RMR")
                sockets[owner][slot] = with_automatic_child(original, cap, True)

    # Make the complete choices participate in the same authored fit and
    # conflict rules as their original mounts and handguard bases.
    for block in sockets:
        conflicts = list(map(int, block[0].split()[1:]))
        conflicts += [alias for part in conflicts for alias in aliases.get(part, ())]
        block[0] = " ".join(map(str, (len(conflicts), *conflicts)))
        for index, original in enumerate(block[2:], 2):
            key, choices = slot_options(original)
            if key != "mod_handguard" and not key.startswith(("mod_scope", "mod_mount")):
                continue
            additions = [alias for part in choices for alias in aliases.get(part, ())]
            if additions:
                fields = original.split("\t", 5)
                # Keep alternate offsets (such as an SVD rail's secondary seat)
                # associated with the inherited choices.
                tokens = fields[-1].split()
                extra = tokens[len(choices):]
                if extra and extra[0] != "offset":
                    raise ValueError(f"Cannot inherit socket fits in {original}")
                if extra:
                    start = int(extra[1])
                    primary = choices[:start]
                    secondary = choices[start:]
                    primary += [alias for part in primary for alias in aliases.get(part, ())]
                    secondary += [alias for part in secondary for alias in aliases.get(part, ())]
                    extra[1] = str(len(primary))
                    choices = primary + secondary
                else:
                    choices += additions
                fields[3] = str(len(choices))
                fields[-1] = " ".join([*map(str, choices), *extra])
                block[index] = "\t".join(fields)
    return sum(map(len, aliases.values()))


def fit_ak_wood_grip(rows, sockets, source_meshes, destination_meshes):
    wasr = "5d2c829448f0353a5c7d6674"
    short_stock = "57dc32dc245977596d4ef3d3"
    short_tube = "59d36a0086f7747e673f3946"
    fitted = stable_id(wasr + ":AKS-74U")
    indices = {row.split("\t", 1)[0]: i for i, row in enumerate(rows)}
    for ident, name in ((wasr, "WASR wood grip"), (short_stock, "AKS-74U wood")):
        i = indices[ident]
        rows[i] = rows[i].rsplit("\t", 1)[0] + "\t" + name
    if fitted in indices:
        return

    textures, vertices = read_mesh(source_meshes / (wasr + ".bin"))
    _, stock_vertices = read_mesh(source_meshes / (short_stock + ".bin"))
    # The baked parts share the same origin convention. Match the compact
    # stock handguard's fore-aft limits and retain the grip's width and depth.
    old_back = min(v[1] for v in vertices)
    old_front = max(v[1] for v in vertices)
    new_back = min(v[1] for v in stock_vertices)
    new_front = max(v[1] for v in stock_vertices)
    length_scale = (new_front - new_back) / (old_front - old_back)
    corners = b"".join(CORNER.pack(v[0], new_back + (v[1] - old_back) * length_scale, *v[2:])
                       for v in vertices)
    save_mesh(destination_meshes / (fitted + ".bin"), textures, corners)

    tube = sockets[indices[short_tube]]
    slot = next(i for i, entry in enumerate(tube) if entry.startswith("mod_handguard\t"))
    fields = tube[slot].split("\t", 5)
    options = fields[5].split()
    options.insert(int(fields[3]), str(len(rows)))
    fields[3] = str(int(fields[3]) + 1)
    fields[5] = " ".join(options)
    tube[slot] = "\t".join(fields)

    flags = rows[indices[wasr]].split("\t")[2]
    rows.append(f"{fitted}\t0\t{flags}\tWASR wood grip")
    sockets.append(["0", "0"])


def fix_nxs_zoom(source, destination):
    # The pack labels this variable scope 2.5-10x but supplies a 1x fallback
    # in its camera data. Keep lens and reticle calibration from the bake.
    filename = "544a3d0a4bdc2d1b388b4567.opt"
    src = source / "optics" / filename
    if not src.is_file():
        return
    profile = bytearray(src.read_bytes())
    mode_header = struct.Struct("<4I19f")
    if profile[:12] != b"GEVROPT4" + struct.pack("<I", 1):
        raise ValueError("Unexpected Nightforce optics profile format")
    fields = list(mode_header.unpack_from(profile, 12))
    if fields[0] & 255 != 2:
        raise ValueError("Expected a magnifying Nightforce scope")
    if fields[-2:] == [1., 1.]:
        fields[-2:] = 2.5, 10.
        mode_header.pack_into(profile, 12, *fields)
    (destination / "optics" / filename).write_bytes(profile)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path, help="catalog.txt from the full attachment build")
    parser.add_argument("archive", type=Path, help="original pack with the full attachment names")
    parser.add_argument("destination", type=Path, help="new Workshop catalog.txt")
    args = parser.parse_args()
    rows, sockets, builds = read_catalog(args.baseline)
    names = part_names(args.archive)
    source = args.baseline.parent
    destination = args.destination.parent
    optics = {i for i, row in enumerate(rows)
              if (source / "optics" / (row.split("\t", 1)[0] + ".opt")).is_file()}
    mounts = scope_mounts(sockets, optics)
    titles = []
    suppressors = set()
    counts = Counter()
    for index, row in enumerate(rows):
        ident, capacity, art, title = row.split("\t", 3)
        flags = int(art)
        full = names.get(ident, title)
        titles.append(full)
        if index in optics:
            flags |= OPTIC
        if index in mounts:
            flags |= SCOPE_MOUNT
        if flags & MESH and (flags & SUPPRESSOR or suppressor(full)):
            flags |= SUPPRESSOR
            suppressors.add(index)
        group = colour(full)
        flags = (flags & ~(COLOUR_MASK << COLOUR_SHIFT)) | (group << COLOUR_SHIFT)
        counts[COLOURS[group]] += 1
        rows[index] = "\t".join((ident, capacity, str(flags), title))

    ak_roots = {builds[key][0] for key in (0, 4)}
    additions = direct_suppressors(sockets, suppressors, titles, ak_roots)
    variants = assemble_choices(rows, sockets, titles, source / "meshes", destination / "meshes")
    fit_ak_wood_grip(rows, sockets, source / "meshes", destination / "meshes")
    if len(rows) > 1536:
        raise ValueError("Compiled Workshop part limit exceeded")
    output = ["GEVRGEAR3", str(len(rows)), *rows, *(row for block in sockets for row in block)]
    for key, (root, defaults) in builds.items():
        output.extend((f"{key} {root} {len(defaults)}", *defaults))
    fix_nxs_zoom(source, destination)
    args.destination.write_text("\n".join(output) + "\n", encoding="ascii", newline="\n")
    print(f"{len(rows)} entries, {len(suppressors)} suppressor models, {additions} direct socket fits")
    print(f"{len(optics)} optical profiles, {len(mounts)} mount parts, {variants} finished assemblies")
    print(", ".join(f"{name} {counts[name]}" for name in COLOURS))


if __name__ == "__main__":
    main()
