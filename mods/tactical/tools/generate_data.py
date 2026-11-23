"""Generate scripts/tactical/guns.lua from Tactical 0.18.4's reviewed tables.

Usage:
    python tools/generate_data.py <unlock-review-folder> <stats.txt> [output]

<unlock-review-folder> holds gun-unlocks.tsv, part-unlocks.tsv and
mastery-magazines.tsv (the 0.18.4 unlock review). <stats.txt> is the
Workshop's gear/stats.txt (GEVRSTATS1). The output defaults to
scripts/tactical/guns.lua beside this folder. Run it again whenever either
input changes; the result is committed, so the mod never reads the inputs.
"""
import csv
import sys
from pathlib import Path

# Each Tactical gun rides on one of the cartridge's guns (mod.items names).
# The 0.18.4 build used its own extra items; a mod can only use the ROM's, so
# each gun takes the cartridge gun closest to it.
NATIVE = {
    'AKS-74UN': 'ak47', 'Walther PPK': 'wppk', 'MP5': 'spectre', 'MP5K': 'mp5k',
    'AK-74N': 'ak47', 'Makarov PM': 'tt33', 'Skorpion vz. 61': 'skorpion',
    'SVD Dragunov': 'sniperrifle', 'Uzi': 'uzi', 'SR-2M Veresk': 'uzi',
    'B&T MP9-N': 'uzi', 'CZ Scorpion EVO 3': 'uzi', 'Minebea PM-9': 'uzi',
    'HK Mk 23': 'wppk', 'PKM': 'ak47', 'M4A1': 'm16', 'HK416A5': 'm16',
    'AS VAL': 'mp5ksil', 'Colt M1911A1': 'wppk', 'CZ 75B': 'wppk', 'FN P90': 'fnp90',
    'Glock 17': 'wppk', 'Glock 18C': 'wppk', 'Glock 19X': 'wppk', 'Desert Eagle': 'ruger',
    'Milkor M32A1': 'grenadelaunch', 'Saiga-12K ver.10': 'autoshot', 'RPG-7V2': 'rocketlaunch',
    'FN Five-seveN MK2': 'wppk', 'HK MP7A2': 'uzi', 'HK MP7A1': 'uzi',
    'KAC PDW .300 Blackout': 'm16', 'PP-19-01 Vityaz': 'ak47', 'SIG MPX': 'uzi',
    'Springfield M1A': 'ruger', 'SR-3M': 'ak47', 'Steyr AUG A3': 'm16',
    'TT-33 Tokarev': 'tt33', 'Soyuz-TM STM-9 Gen.2': 'uzi', 'L85A2': 'm16',
    'FAMAS F1': 'm16', 'Simonov SKS': 'ruger',
}
PISTOLS = {'Walther PPK', 'Makarov PM', 'HK Mk 23', 'Colt M1911A1', 'CZ 75B', 'Glock 17',
           'Glock 18C', 'Glock 19X', 'Desert Eagle', 'FN Five-seveN MK2', 'TT-33 Tokarev'}
# The loadout page's lists: a choice row holds at most 12 names.
CATEGORIES = {
    'Rifles': ['AKS-74UN', 'AK-74N', 'M4A1', 'HK416A5', 'AS VAL', 'KAC PDW .300 Blackout',
               'SR-3M', 'Steyr AUG A3', 'L85A2', 'FAMAS F1'],
    'SMGs I': ['MP5', 'MP5K', 'Skorpion vz. 61', 'Uzi', 'SR-2M Veresk', 'B&T MP9-N', 'CZ Scorpion EVO 3'],
    'SMGs II': ['Minebea PM-9', 'FN P90', 'HK MP7A2', 'HK MP7A1', 'PP-19-01 Vityaz', 'SIG MPX',
                'Soyuz-TM STM-9 Gen.2'],
    'Heavy': ['SVD Dragunov', 'Springfield M1A', 'Simonov SKS', 'PKM', 'Milkor M32A1',
              'Saiga-12K ver.10', 'RPG-7V2'],
    'Pistols': sorted(PISTOLS, key=lambda n: (n != 'Walther PPK', n)),
}
# Launchers keep the cartridge's own loading: no magazines.
NO_MAGAZINE = {'Milkor M32A1', 'RPG-7V2'}
# Stock capacities (gevr_tactical_session.h tacticalGunCapacity and the expanded list).
STOCK = {
    'AKS-74UN': 30, 'AK-74N': 30, 'MP5': 30, 'Walther PPK': 7, 'Makarov PM': 8, 'MP5K': 20, 'Skorpion vz. 61': 20, 'SVD Dragunov': 10,
    'RPG-7V2': 1, 'Uzi': 32, 'SR-2M Veresk': 30, 'B&T MP9-N': 30, 'CZ Scorpion EVO 3': 30,
    'Minebea PM-9': 25, 'HK Mk 23': 12, 'PKM': 100, 'M4A1': 30, 'HK416A5': 30, 'AS VAL': 20,
    'Colt M1911A1': 7, 'CZ 75B': 16, 'FN P90': 50, 'Glock 17': 17, 'Glock 18C': 17,
    'Glock 19X': 19, 'Desert Eagle': 7, 'Milkor M32A1': 6, 'Saiga-12K ver.10': 5,
    'FN Five-seveN MK2': 20, 'HK MP7A2': 20, 'HK MP7A1': 20, 'KAC PDW .300 Blackout': 30,
    'PP-19-01 Vityaz': 30, 'SIG MPX': 30, 'Springfield M1A': 10, 'SR-3M': 30,
    'Steyr AUG A3': 30, 'TT-33 Tokarev': 8, 'Soyuz-TM STM-9 Gen.2': 33, 'L85A2': 30,
    'FAMAS F1': 25, 'Simonov SKS': 20,
}
# User, 2026-11-23: the PKM keeps its factory 100-round box and has no magazine unlocks.
FIXED_MAGAZINE = {'PKM'}


def read(path):
    with path.open(encoding='utf-8-sig', newline='') as f:
        return list(csv.DictReader(f, delimiter='\t'))


def names(field):
    return [n.strip() for n in (field or '').split(';') if n.strip()]


def lua(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def main():
    review, stats_path = Path(sys.argv[1]), Path(sys.argv[2])
    out = Path(sys.argv[3]) if len(sys.argv) > 3 else Path(__file__).resolve().parent.parent / 'scripts' / 'tactical' / 'guns.lua'
    guns = read(review / 'gun-unlocks.tsv')
    parts = read(review / 'part-unlocks.tsv')
    mastery = read(review / 'mastery-magazines.tsv')

    stats = {}
    lines = stats_path.read_text().splitlines()
    assert lines[0].startswith('GEVRSTATS1')
    for line in lines[1:]:
        f = line.split()
        if f and f[0] == 'W':
            stats[int(f[1])] = dict(modes=int(f[2]), rpm=int(f[3]), semi=int(f[4]), burst=int(f[5]),
                                    damage=float(f[10]))

    by_name = {g['Gun']: int(g['Workshop key']) for g in guns}
    assert set(by_name) == set(NATIVE), set(by_name) ^ set(NATIVE)
    assert sorted(sum(CATEGORIES.values(), [])) == sorted(NATIVE)
    assert all(len(v) <= 12 for v in CATEGORIES.values())

    # Magazines each gun can take, by capacity: the fewest clears that earn one.
    mags = {n: {} for n in NATIVE}
    part_guns = {}

    def offer(gun, cap, clears, part):
        if gun not in mags or cap <= 0:
            return
        if gun in FIXED_MAGAZINE and cap != STOCK[gun]:
            return
        if gun in FIXED_MAGAZINE:
            clears = 0
        old = mags[gun].get(cap)
        mags[gun][cap] = clears if old is None else min(old, clears)
        part_guns.setdefault(part, set()).add(gun)

    for p in parts:
        cap = int(p['Capacity'] or 0)
        if not cap:
            continue
        route = p['Primary route']
        if route == 'Factory':
            for g in names(p['Workshop socket-choice guns']) + names(p['Stock on']):
                offer(g, cap, 0, p['Stable catalogue ID'])
        elif route == 'Standard kit':
            for g in names(p['Eligible kit host guns']):
                offer(g, cap, int(p['Successful clears (proposed)']), p['Stable catalogue ID'])
    for m in mastery:
        for g in names(m['Compatible guns']):
            offer(g, int(m['Capacity']), int(m['Successful clears on this exact gun (proposed)']),
                  m['Stable catalogue ID'])
    for g, cap in STOCK.items():
        if g not in NO_MAGAZINE:
            offer(g, cap, 0, 'stock:' + g)

    # Magazine families: guns that share any magazine share a family.
    parent = {n: n for n in NATIVE}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for members in part_guns.values():
        members = sorted(members)
        for other in members[1:]:
            parent[find(other)] = find(members[0])
    families = {}
    for n in sorted(NATIVE, key=lambda n: by_name[n]):
        families.setdefault(find(n), len(families) + 1)

    gun_rows = {g['Gun']: g for g in guns}
    out_lines = [
        '-- Generated by tools/generate_data.py from Tactical 0.18.4\'s reviewed unlock',
        '-- tables and Workshop stats. Do not edit by hand: change the inputs and run it.',
        '--',
        '-- key: the Workshop key (saves name guns by it). item: the cartridge gun it',
        '-- rides on. family: guns with the same family share magazines. magazines:',
        '-- capacity -> successful clears with this exact gun that earn it (0: from the',
        '-- start). modes: 1 semi, 2 burst, 4 auto. damage: a multiplier on the',
        '-- cartridge gun\'s own. reserveAt: total clears before reserve can buy it.',
        'return {',
    ]
    for n in sorted(NATIVE, key=lambda n: by_name[n]):
        key = by_name[n]
        st = stats[key]
        row = gun_rows[n]
        caps = ', '.join(f'[{c}] = {mags[n][c]}' for c in sorted(mags[n]))
        cat = next(c for c, members in CATEGORIES.items() if n in members)
        out_lines.append(
            f'  {{ key = {key}, name = {lua(n)}, item = {lua(NATIVE[n])}, '
            f'slot = {lua("pistol" if n in PISTOLS else "primary")}, category = {lua(cat)}, '
            f'family = {families[find(n)]}, stock = {STOCK.get(n, 0)}, '
            f'magazines = {{ {caps} }}, '
            f'modes = {st["modes"]}, rpm = {st["rpm"]}, semiRpm = {st["semi"]}, burst = {st["burst"]}, '
            f'damage = {st["damage"]:g}, reserveAt = {int(row["Minimum total clears for reserve eligibility"])} }},')
    out_lines.append('}')
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text('\n'.join(out_lines) + '\n', encoding='ascii', newline='\n')
    print(f'wrote {out} ({len(NATIVE)} guns, {len(families)} magazine families)')


if __name__ == '__main__':
    main()
