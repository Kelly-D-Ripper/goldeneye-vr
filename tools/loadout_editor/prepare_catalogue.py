"""Install fitted armour and headgear and generate the editor's campaign index."""

import argparse
from pathlib import Path
import re
import shutil
import struct
import sys

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parents[1]
sys.path.insert(0, str(SOURCE / "tools"))
from gevr_campaign_audit import assets, missions
from gevr_rom_probe import load_rom


def actor_definitions():
    constants = (SOURCE / "src/bondconstants.h").read_text(encoding="utf-8")
    definitions = constants.split("typedef enum BODIES", 1)[1].split("BODIES_MAX", 1)[0]
    definitions = re.sub(r"#ifdef ALL_BONDS.*?#endif", "", definitions, flags=re.S)
    bodies = re.findall(r"\bBODY_(\w+)\b", definitions)
    head_start = bodies.index("Male_Karl")
    labels = {
        "Boris": "Boris Grishenko", "Ourumov": "General Ourumov",
        "Valentin_": "Valentin Zukovsky", "Xenia": "Xenia Onatopp",
        "Siberian_Guard_1_Mishkin": "Siberian Guard 1 / Mishkin",
        "St_Petersburg_Guard": "St Petersburg Guard",
    }
    heads = {
        bodies.index("Male_Dave_Dr_Doak"): "Dr. Doak",
        bodies.index("Male_Mishkin"): "Defence Minister Mishkin",
        bodies.index("Female_Marion_Rosika"): "Rosika",
    }
    return {i: labels.get(body, body.replace("_", " ").strip()) for i, body in enumerate(bodies)
            if (i < head_start and i != 5 and not body.startswith(("Left_Suit", "Unused")))
            or body == "Natalya_Jungle_Fatigues"}, heads


def mission_actors(rom, bodies, heads):
    sizes_text = (SOURCE / "port/src/gevr_setup.c").read_text().split("static const int words[] = {")[1].split("};")[0]
    sizes_text = re.sub(r"/\*.*?\*/", "", sizes_text, flags=re.S)
    sizes = [int(n, 0) * 4 for n in re.findall(r"0x[\da-f]+|\d+", sizes_text)]
    mission_table = (SOURCE / "port/src/net/net_match.c").read_text().split("s_coop_missions[] = {")[1].split("};")[0]
    stages = {name: int(stage) for name, stage in re.findall(r'\{ "([^"]+)", (\d+) \}', mission_table)}
    rom_assets = assets(rom)
    heavy_guards = (SOURCE / "src/game/gevr_tactical_heavy_guards.h").read_text()
    janus_guard = int(re.search(r"GEVR_DAM_JANUS_GUARD = (\d+)", heavy_guards)[1])
    by_name = {label: key for key, label in bodies.items()}
    for name, setup, floor, _ in missions():
        data = rom_assets[setup]
        stand = rom_assets[floor]
        actors = []
        p, = struct.unpack_from(">I", data, 12)
        pads, = struct.unpack_from(">I", data, 24)
        tiles = stand_tiles(stand)
        while data[p + 3] != 48:
            kind = data[p + 3]
            if kind == 9:
                actor, pad, body, *_, head = struct.unpack_from(">9Hh", data, p + 4)
                if name == "Dam" and actor == janus_guard and body == by_name["Russian Soldier"]:
                    body = by_name["Janus Special Forces"]
                if body not in bodies:
                    raise ValueError(f"Missing NPC type {body} in {name}")
                position = struct.unpack_from(">3f", data, pads + 44 * pad)
                link, = struct.unpack_from(">I", data, pads + 44 * pad + 36)
                if link and link < len(data):
                    text = data[link:data.index(0, link)].decode("ascii")
                    match = re.fullmatch(r"([pq])(\d+)([a-z])([0-7]?)", text)
                    if match:
                        area, number, letter, sub = match.groups()
                        key = (ord(area) - 112 << 23) | (int(number) << 8) | (ord(letter) - 97 << 3) | int(sub or 0)
                        vertices = tiles.get(key)
                        if vertices:
                            position = (position[0], sum(v[1] for v in vertices) / len(vertices) * 100, position[2])
                xyz = [round(value * .01, 4) for value in position]
                actors.append((actor, pad, body, heads.get(head, bodies[body]), xyz))
            p += sizes[kind]
        yield name, stages[name], sorted(actors), tiles


def stand_tiles(stand):
    p, = struct.unpack_from(">I", stand, 4)
    tiles = {}
    while struct.unpack_from(">I", stand, p)[0]:
        count = struct.unpack_from(">H", stand, p + 6)[0] >> 12
        vertices = [tuple(c * .01 for c in struct.unpack_from(">3h", stand, p + 8 + i * 8)) for i in range(count)]
        if count >= 3:
            tiles[int.from_bytes(stand[p:p + 3], "big")] = vertices
        p += 8 + 8 * max(3, count)
    return tiles


def preview_mesh(path, triangles):
    packed = bytearray()
    for triangle, rgb in triangles:
        for point in triangle:
            packed.extend(struct.pack("<5f4BB3x", *point, 0, 0, *rgb, 255, 0))
    if not 0 < len(packed) <= 50000 * 84:
        raise ValueError(f"Unexpected mission preview size for {path}")
    header = b"GEVRWPN1" + struct.pack("<IHH4BI", 1, 1, 1, 255, 255, 255, 255, len(packed) // 84)
    path.write_bytes(header + packed)


def mission_triangles(tiles):
    low = min(v[1] for verts in tiles.values() for v in verts)
    high = max(v[1] for verts in tiles.values() for v in verts)
    for key, vertices in tiles.items():
        elevation = (sum(v[1] for v in vertices) / len(vertices) - low) / max(1, high - low)
        shade = round(27 * elevation) + (key * 17 % 9)
        color = (69 + shade, 90 + shade, 79 + shade)
        for i in range(1, len(vertices) - 1):
            yield (vertices[0], vertices[i], vertices[i + 1]), color


def marker_triangles():
    top, bottom = (0, 1, 0), (0, .1, 0)
    points = [(-.43, .55, 0), (0, .55, -.43), (.43, .55, 0), (0, .55, .43)]
    for i, point in enumerate(points):
        yield (bottom, points[i - 1], point), (168, 119, 40)
        yield (top, points[i - 1], point), (243, 187, 84)


def seed_equipment(target, donor_folders):
    if target.is_file():
        return
    donor = next((folder / target.name for folder in donor_folders if (folder / target.name).is_file()), None)
    if donor is None:
        raise FileNotFoundError(target)
    target.parent.mkdir(exist_ok=True, parents=True)
    shutil.copy2(donor, target)


def equipment(tag, macro, filename, candidate, donor_folders):
    directory = candidate / "mods/tactical"
    data = (SOURCE / "port/include" / filename).read_text(encoding="utf-8")
    for index, (asset, name) in enumerate(re.findall(rf'{macro}\(\w+, "([^"]+)", "([^"]+)"\)', data)):
        seed_equipment(directory / asset, donor_folders)
        yield tag, index + (tag == "V"), name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("workstreams", type=Path)
    args = parser.parse_args()
    candidate = args.candidate
    rom, _ = load_rom(candidate / "ge.z64")
    folders = [args.workstreams / "gear-fitment/meshes", args.workstreams / "armor-previews/curated-helmets"]
    bodies, heads = actor_definitions()
    rows = []
    rows.extend(("N", body, name) for body, name in bodies.items())
    rows.extend(equipment("V", "TACTICAL_VEST", "gevr_tactical_vests.inc.h", candidate, folders))
    rows.extend(equipment("H", "TACTICAL_HELMET", "gevr_tactical_helmets.inc.h", candidate, folders))
    fits = (SOURCE / "port/include/gevr_tactical_vest_fits.inc.h").read_text(encoding="utf-8")
    for asset in re.findall(r'TACTICAL_VEST_FIT\(\w+, \w+, "([^"]+)"\)', fits):
        seed_equipment(candidate / "mods/tactical" / asset, folders)
    count = 0
    models = candidate / "loadout-models"
    models.mkdir(exist_ok=True)
    preview_mesh(models / "mission-marker.bin", marker_triangles())
    for name, stage, actors, tiles in mission_actors(rom, bodies, heads):
        points = [v for tile in tiles.values() for v in tile]
        span = max(max(v[i] for v in points) - min(v[i] for v in points) for i in (0, 2))
        rows.append(("M", stage, name, round(span, 3)))
        rows.extend(("A", stage, body, actor, pad, label, *position) for actor, pad, body, label, position in actors)
        preview_mesh(models / f"mission-{stage}.bin", mission_triangles(tiles))
        count += len(actors)
    (candidate / "tactical-editor-catalogue.tsv").write_text(
        "GEVREDITOR1\n" + "".join("\t".join(map(str, row)) + "\n" for row in rows), encoding="utf-8"
    )
    print(f"Wrote catalogue for {len(bodies)} NPC types, {count} placed actors and {len(missions())} missions")


if __name__ == "__main__":
    main()
