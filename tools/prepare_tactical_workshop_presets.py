"""Seed Tactical Workshop presets from supplied SPT and pack item trees."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re

from prepare_tactical_workshop_packs import Pack, Socket, read_catalog, read_json, stable_id


M4 = "5447a9cd4bdc2dbd208b4567"
HK416 = "5bb2475ed4351e00853264e3"
AK74M = "5ac4cd105acfc40016339859"
AK105 = "5ac66d9b5acfc4001633997a"
AK74U = "57dc2fa62459775949412633"
AKS74UN = "583990e32459771419544dd2"
AK74N = "5644bd2b4bdc2d3b4c8b4572"
SVD_S = "5c46fbd72e2216398b5a8c9c"
MP9N = "5de7bd7bfd6b4e6e2276dc25"
ADAPTED = {AK74M: (4, "74M Adapted"), AK105: (4, "105 Adapted"),
           AK74U: (0, "74U Adapted"), SVD_S: (7, "SVDS Adapted"), MP9N: (11, "MP9N Adapted")}
CAPS = {0: 4, 4: 4, 16: 5, 17: 4}
BUNDLED_PRESET_NAMES = {
    "Prapor 74U Adapted": "Checkpoint",
    "Prapor 74U Adapted 2": "Close Quarters",
    "AKSU eft book": "Field Manual",
    "AKS74U_Vympel": "Vympel",
    "Mechanic MP5": "SAS Entry",
    "MP5_SilentOps": "Blackout",
    "Sand 105 Adapted": "Sandstorm",
    "Prapor 105 Adapted": "Border Patrol",
    "Prapor 74M Adapted": "Red Banner",
    "Mechanic 74M Adapted": "Night Watch",
    "Prapor SR-2M Veresk": "Tunnel Runner",
    "Prapor SR-2M Veresk 2": "Close Guard",
    "M4A1 Sand": "Dust Trail",
    "Green M4A1": "Pine Watch",
    "White M4A1": "Whiteout",
    "Painter M4A1": "Black Tape",
    "Peacekeeper M4A1": "Long Patrol",
    "HK416 Sand": "Dune Patrol",
    "Peacekeeper HK416A5": "Night Detail",
}


def trees(items):
    children = defaultdict(list)
    ids = {item["_id"] for item in items}
    for part in items:
        children[part.get("parentId")].append(part)
    for parent in set(children) - ids:
        for item in children[parent]:
            if item["_id"] in children:
                yield item, children


def load_offers(db, packs, wtt_path):
    for pack in packs:
        if pack.archive:
            assortment = pack.zip_json("/assort.json")
            title = pack.code.rsplit("-", 1)[-1].removesuffix("Core")
            globals_ = pack.zip_json("/globals.json")
        else:
            assortment = read_json(pack.source / "db/assort.json")
            title = "Painter"
            globals_ = {"ItemPresets": read_json(pack.source / "db/CustomWeaponPresets/itemglobal.json")}
        for root, children in trees(assortment.get("items", [])):
            item = pack.items.get(root["_tpl"])
            name = pack.item(item)[2] if item else "M4A1"
            yield (name if title.lower() in name.lower() else f"{title} {name}"), root, children, pack.code, 0
        for preset in globals_.get("ItemPresets", {}).values():
            for root, children in trees(preset["_items"]):
                yield f"{title} {preset.get('_name', 'Kit')}", root, children, pack.code, 1

    for path in sorted((db / "traders").glob("*/assort.json")):
        trader = read_json(path.with_name("base.json")).get("nickname", "")
        if trader not in ("Prapor", "Peacekeeper", "Mechanic", "Skier", "Jaeger", "Therapist"):
            continue
        data = read_json(path)
        for root, children in trees(data["items"]):
            if root.get("parentId") == "hideout":
                yield trader, root, children, f"{trader} SPT 3.9.5", 2

    if wtt_path and wtt_path.is_file():
        for preset in read_json(wtt_path).values():
            for root, children in trees(preset["_items"]):
                yield f"WTT {preset.get('_name', 'Kit')}", root, children, "WTT Armory", 1

    for preset in read_json(db / "globals.json")["ItemPresets"].values():
        for root, children in trees(preset["_items"]):
            yield preset.get("_name", "Factory"), root, children, "SPT 3.9.5 factory", 3


def source_name(raw):
    return re.sub(r" +", " ", re.sub(r"[^-a-zA-Z0-9_ ]", " ", raw)).strip()[:24].rstrip() or "Trader kit"


class Workshop:
    def __init__(self, catalog, imported):
        self.rows, blocks, self.builds = read_catalog(catalog)
        self.ids = {row.split("\t", 1)[0]: i for i, row in enumerate(self.rows)}
        self.sockets = [[Socket.parse(line) for line in block[2:]] for block in blocks]
        self.conflicts = [set(map(int, block[0].split()[1:])) for block in blocks]
        self.imported = imported
        self.roots = {self.id(index): key for key, (index, _) in self.builds.items()}
        self.roots.update({M4: 16, HK416: 17})
        self.title = {key: self.rows[part].split("\t", 3)[-1] for key, (part, _) in self.builds.items()}

    def id(self, part):
        return self.rows[part].split("\t", 1)[0]

    def candidates(self, ident):
        return set(self.imported.get(ident, [])) | {
            self.ids[key] for key in (ident, stable_id(ident + ":full AR")) if key in self.ids
        }

    def clear_blocking_defaults(self, base, choices, defaults):
        active = {}

        def visit(part, prefix, depth):
            if depth == 9:
                return
            for socket in self.sockets[part]:
                if not socket.parts or socket.key == "mod_launcher":
                    continue
                path = prefix + socket.key
                choice = choices.get(path, defaults.get(path, -1))
                if choice not in socket.parts:
                    choice = socket.parts[0] if socket.required else -1
                if choice >= 0:
                    active[path] = (socket, choice)
                    visit(choice, path + "/", depth+1)

        visit(base, "", 0)
        for path, (_, part) in active.items():
            if path not in choices:
                continue
            for blocker, (socket, other) in active.items():
                if blocker == path or blocker in choices or socket.required:
                    continue
                if other not in self.conflicts[part] and part not in self.conflicts[other]:
                    continue
                if any(child.startswith(blocker + "/") for child in choices):
                    continue
                # Trader kits can omit a factory fitting that clashes with
                # their chosen rail, such as the AK rear sight under Dog Leg.
                choices[blocker] = -1

    def read_build(self, key, root, children):
        base, stock = self.builds[key]
        defaults = {path: int(part) for path, part in (entry.split() for entry in stock)}
        choices, skipped = {}, []

        finish = next((part for sock in self.sockets[base] if sock.key == "mod_finish"
                       for part in sock.parts if part in self.candidates(root["_tpl"])), None)
        if finish is not None:
            choices["mod_finish"] = finish

        def walk(node, fitted, prefix):
            sockets = self.sockets[fitted]
            for item in children[node["_id"]]:
                key = item.get("slotId", "")
                if not key.startswith("mod_") or key == "mod_launcher":
                    continue
                ids = self.candidates(item["_tpl"])
                possible = [(socket, part) for socket in sockets for part in socket.parts if part in ids]
                exact = [pair for pair in possible if pair[0].key == key]
                if not exact:
                    slots = {socket.key for socket, _ in possible}
                    if len(slots) != 1:
                        skipped.append(item["_tpl"])
                        continue
                socket, part = (exact or possible)[0]
                path = prefix + socket.key
                if path in choices:
                    skipped.append(item["_tpl"])
                    continue
                choices[path] = part
                walk(item, part, path + "/")

        walk(root, base, "")
        if len(choices) < 2 or len(choices) < 1.2 * len(skipped):
            return None
        self.clear_blocking_defaults(base, choices, defaults)
        # Keep chosen ancestors even when they match the stock build so child
        # paths keep their original fitting. A gas block can be selected by
        # the engine unless it carries the entire AK handguard.
        for path in list(choices):
            if path.endswith("/mod_gas_block") and choices[path] == defaults.get(path):
                del choices[path]
        meaningful = sum(defaults.get(path) != part and
                         not path.endswith(("mod_bolt", "mod_charge", "mod_gas_block"))
                         for path, part in choices.items())
        if not meaningful:
            return None
        return choices, meaningful, len(skipped)


def prepare(workshop, candidates, pack_roots):
    grouped = defaultdict(list)
    for raw_name, root, child, source, priority in candidates:
        raw_id = root["_tpl"]
        item = pack_roots.get(raw_id, raw_id)
        if item in workshop.roots:
            key = workshop.roots[item]
            adapted = ""
        elif item in ADAPTED:
            key, adapted = ADAPTED[item]
        else:
            continue
        build = workshop.read_build(key, root, child)
        if not build:
            continue
        choices, variety, skipped = build
        name = raw_name
        if priority == 2:
            name += " " + (adapted or workshop.title[key])
        elif adapted:
            name = ("Sand " if "MoxoPixel-Sand" in source else "") + adapted
        grouped[key].append((priority, skipped, -variety, source_name(name), source, choices))

    rows, notes = ["GEVRPRESETS1"], []
    for key, group in sorted(grouped.items()):
        fingerprints, used = set(), set()
        for _, skipped, __, title, source, choices in sorted(group, key=lambda item: item[:5]):
            # Distinct catalog layouts only, irrespective of trader or listing price.
            signature = tuple(sorted(choices.items()))
            if signature in fingerprints:
                continue
            fingerprints.add(signature)
            name, suffix = title, 2
            while name in used:
                extra = f" {suffix}"
                name, suffix = title[:24-len(extra)].rstrip() + extra, suffix+1
            used.add(name)
            root = workshop.id(workshop.builds[key][0])
            name = BUNDLED_PRESET_NAMES.get(name, name)
            rows.append(f"P {root} {len(choices)} {name}")
            rows.extend(f"{path} {workshop.id(part) if part >= 0 else '-'}" for path, part in choices.items())
            notes.append((key, name, source, skipped))
            if len(used) >= CAPS.get(key, 2):
                break
    return rows, notes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("gear", type=Path)
    parser.add_argument("report", type=Path)
    parser.add_argument("spt_db", type=Path)
    parser.add_argument("--wtt", type=Path)
    parser.add_argument("packs", type=Path, nargs="+")
    args = parser.parse_args()
    packs = [Pack(path) for path in args.packs]
    source = {ident: pack.item(item)[0] for pack in packs for ident, item in pack.items.items()}
    report = read_json(args.report)
    workshop = Workshop(args.gear / "catalog.txt", report["local"])
    offers = load_offers(args.spt_db, packs, args.wtt)
    rows, notes = prepare(workshop, offers, source)
    (args.gear / "presets.txt").write_text("\n".join(rows) + "\n", encoding="ascii", newline="\n")
    (args.report.parent / "preset-sources.json").write_text(json.dumps(notes, indent=2), encoding="utf-8")
    print("Preset candidates", {workshop.title[key]: count for key, count in Counter(n[0] for n in notes).items()})
    for key, name, src, skipped in notes:
        print(f"{workshop.title[key]} | {name} | {src} | {skipped} unmatched stages")


if __name__ == "__main__":
    main()
