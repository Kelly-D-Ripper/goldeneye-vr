"""Bake game-ready weapon profiles and attachment modifiers from local pack metadata."""
import argparse
import json
from pathlib import Path
from zipfile import ZipFile

from prepare_tactical_ar_full import read_catalog
from prepare_tactical_roster import stable_id


# Stock weapon balance in Tactical. Mass includes the stock fittings and an
# empty magazine. Recoil and spread are VR tuning values, not SPT engine units.
# key: modes, fallback cyclic RPM, semi limit, kg, ergo, recoil, spread, damage, flash
STOCK = {
    0: (5, 650, 450, 2.70, 55, 1.00, .26, 1.00, 1.00),  # AKS-74UN
    1: (1, 0, 420, .60, 78, .72, .31, .92, .62),         # PPK
    2: (7, 800, 450, 2.72, 68, .65, .28, 1.00, .74),     # MP5
    3: (7, 900, 450, 2.00, 73, .73, .35, 1.00, .79),     # MP5K
    4: (5, 650, 450, 3.35, 50, .92, .20, 1.00, 1.02),    # AK-74N
    5: (1, 0, 420, .73, 74, .75, .33, .92, .66),         # Makarov PM
    6: (5, 850, 450, 1.37, 68, .70, .38, .90, .76),      # Skorpion
    7: (1, 0, 300, 4.45, 43, 1.50, .10, 1.65, 1.28),    # SVD
    8: (1, 0, 420, .78, 72, .68, .31, .92, .62),         # Suppressed PPK
    9: (5, 600, 450, 3.50, 55, .67, .32, 1.00, .79),    # Uzi
    10: (5, 950, 450, 1.68, 66, .83, .34, 1.08, .82),   # SR-2M
    11: (5, 1100, 450, 1.48, 72, .72, .35, 1.00, .85),  # MP9-N
    12: (7, 1150, 450, 2.65, 69, .78, .29, 1.00, .80),  # Scorpion EVO
    13: (4, 1100, 450, 2.82, 64, .87, .41, 1.00, .86),  # PM-9
    14: (1, 0, 420, 1.20, 66, .85, .23, 1.14, .85),     # Mk 23
    15: (4, 650, 450, 7.50, 28, 1.56, .36, 1.60, 1.38), # PKM
    16: (5, 800, 450, 2.94, 64, .94, .20, 1.15, 1.03),  # M4A1
    17: (5, 850, 450, 3.49, 60, .90, .19, 1.15, 1.04),  # HK416A5
    18: (5, 900, 450, 2.96, 59, .93, .26, 1.24, .75),   # AS VAL
    19: (1, 0, 420, 1.10, 68, .98, .27, 1.14, .90),     # M1911A1
    20: (1, 0, 450, 1.00, 75, .76, .26, 1.00, .76),     # CZ 75B
    21: (5, 900, 450, 2.59, 73, .60, .27, 1.05, .70),   # P90
    22: (1, 0, 450, .71, 80, .75, .28, 1.00, .73),      # Glock 17
    23: (5, 1200, 450, .73, 78, .92, .32, 1.00, .86),   # Glock 18C
    24: (1, 0, 450, .70, 82, .74, .27, 1.00, .73),      # Glock 19X
    25: (1, 0, 300, 1.99, 57, 1.65, .25, 1.65, 1.35),   # Desert Eagle
    26: (1, 0, 150, 5.30, 37, 1.60, .26, 1.45, .95),   # M32A1
    27: (1, 0, 300, 3.65, 53, 1.48, 2.4, .23, 1.30),   # Saiga, per pellet
    28: (1, 0, 45, 6.30, 29, 1.60, .22, 1.00, 1.28),   # RPG, native projectile
    29: (1, 0, 450, .65, 78, .66, .26, .93, .72),       # Five-seveN MK2
    30: (5, 950, 450, 2.05, 70, .68, .31, .93, .74),   # MP7A2
    31: (5, 950, 450, 1.90, 73, .68, .31, .93, .74),   # MP7A1
    32: (5, 800, 450, 2.30, 69, .91, .26, 1.24, .97),  # KAC PDW
    33: (5, 700, 450, 2.90, 57, .73, .29, 1.00, .77),  # PP-19-01
    34: (5, 850, 450, 2.75, 67, .59, .23, 1.00, .75),  # MPX
    35: (1, 0, 300, 4.20, 42, 1.48, .14, 1.62, 1.34), # M1A
    36: (5, 900, 450, 2.40, 58, 1.02, .32, 1.24, .89), # SR-3M
    37: (5, 715, 450, 3.60, 56, .86, .19, 1.15, 1.04), # AUG A3
    38: (1, 0, 420, .86, 66, .85, .34, .94, .76),       # TT-33
    39: (1, 0, 450, 2.45, 70, .57, .24, 1.00, .69),    # STM-9
    40: (5, 650, 450, 4.10, 53, .91, .19, 1.15, 1.04), # L85A2
    41: (7, 950, 450, 3.60, 59, .91, .22, 1.16, 1.06), # FAMAS
}
MODE = {"single": 1, "burst": 2, "fullauto": 4}


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def pack_items(path):
    if path.suffix == ".zip":
        with ZipFile(path) as archive:
            member = next(name for name in archive.namelist() if name.endswith("/database/items.json"))
            data = json.loads(archive.read(member))
        for ident, row in data.items():
            yield ident, (row.get("clone", ""), row.get("items", {}).get("_props", {}))
    else:
        for ident, row in read(path).items():
            if isinstance(row, dict) and "overrideProperties" in row:
                yield ident, (row.get("itemTplToClone", ""), row["overrideProperties"])


def part_modifiers(info, art):
    if not info:
        return (.35, -5, -5, 0, 0) if art & 2 else (0, 0, 0, 0, 0)
    # A data-driven, bounded gameplay damage modifier for velocity-bearing
    # parts. Ordinary cosmetics and optics contribute zero damage.
    return (info.get("Weight", 0), info.get("Ergonomics", 0), info.get("Recoil", 0),
            info.get("Accuracy", 0), .20 * info.get("Velocity", 0))


def bake(catalog, database, packs):
    rows, _, builds = read_catalog(catalog)
    templates = read(database / "templates/items.json")
    locale = read(database / "locales/global/en.json")
    parts = {ident: item.get("_props", {}) for ident, item in templates.items()}
    overrides = dict(entry for path in packs for entry in pack_items(path))
    for ident, (base, props) in overrides.items():
        parts[ident] = {**parts.get(base, {}), **props}

    # These gear IDs keep existing Workshop saves tied to their original parts.
    evo_aliases = {
        "cz_scorpion_magazine_30": "6881c0fa3be04213015b7e34",
        "cz_scorpion_magazine_20": "6881c0dd2f0aaea90068aa12",
        "CZ Scorpion EVO 3:action": "687b00468b4fafd5ebb3a763",
        "cz_standard": "687aff780a062c4f04628738",
        "cz_scorpion_muzzle": "687fb49322e3cb02683967bf",
    }
    for legacy, source in evo_aliases.items():
        if source in parts:
            parts[stable_id(legacy)] = parts[source]
    mp9_can = parts["5de8f2d5b74cd90030650c72"]
    mp9_mount = parts["5de8f237bbaf010b10528a70"]
    parts[stable_id("5de8f2d5b74cd90030650c72:MP9 suppressor kit")] = {
        **mp9_can, "Weight": mp9_can["Weight"] + mp9_mount["Weight"],
        "Ergonomics": mp9_can["Ergonomics"] + mp9_mount["Ergonomics"],
    }

    def find_weapon(text):
        return next((ident for ident, info in parts.items()
                     if "weapFireType" in info and text in locale.get(ident + " Name", "")), "")

    aliases = {7: find_weapon("SVDS"), 11: "5de7bd7bfd6b4e6e2276dc25",
               12: "687afda52dc9fd6c0e14c602", 16: find_weapon("Colt M4A1"), 17: find_weapon("HK416A5")}
    catalogue = [row.split("\t", 3) for row in rows]
    lines = ["GEVRSTATS1"]
    for key, (mask, rpm, semi, mass, ergo, recoil, spread, damage, flash) in STOCK.items():
        root = catalogue[builds[key][0]][0] if key in builds else ""
        info = parts.get(aliases.get(key) or root, {})
        modes = sum(MODE[tag] for tag in set(info.get("weapFireType", [])) if tag in MODE)
        if modes:
            mask = modes
        if mask & 6:
            rate = info.get("bFirerate", rpm)
            rpm = rate if rate >= 300 else rpm
        else:
            rpm = 0
        if key in (7, 25, 26, 27, 28, 35):
            semi_rate = semi
        else:
            semi_rate = info.get("SingleFireRate", semi) or semi
        burst = info.get("BurstShotsCount", 3) if mask & 2 else 1
        lines.append(f"W {key} {mask} {rpm} {semi_rate} {burst} {mass:g} {ergo:g} "
                     f"{recoil:g} {spread:g} {damage:g} {flash:g}")
    total = 0
    for index, (ident, _, art, _) in enumerate(catalogue):
        values = part_modifiers(parts.get(ident), int(art))
        if any(values):
            lines.append(f"P {index} {ident} " + " ".join(f"{value:g}" for value in values))
            total += 1
    print(f"{len(STOCK)} weapon profiles and {total} attachment records")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument("database", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("packs", type=Path, nargs="*")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(bake(args.catalog, args.database, args.packs), encoding="ascii")
