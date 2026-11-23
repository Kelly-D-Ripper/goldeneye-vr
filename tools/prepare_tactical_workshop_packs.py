"""Add supplied SPT Workshop parts to the matching Tactical catalog sockets."""
import argparse
from collections import defaultdict
from dataclasses import dataclass
from functools import lru_cache
import math
import json
from pathlib import Path
import re
import struct
import sys
from zipfile import ZipFile

from prepare_tactical_ar_full import read_catalog
from prepare_tactical_expanded import save_mesh
from prepare_tactical_gear import ascii_label
from prepare_tactical_roster import CORNER, read_mesh, stable_id
from prepare_tactical_workshop_ease import COLOUR_MASK, COLOUR_SHIFT, MESH, OPTIC, SUPPRESSOR, colour, suppressor


@dataclass
class Socket:
    key: str
    label: str
    required: int
    frame: list
    parts: list
    offset: list
    fits: dict

    @classmethod
    def parse(cls, line):
        key, label, required, count, frame, choices = line.split("\t", 5)
        count = int(count)
        fields = choices.split()
        parts = list(map(int, fields[:count]))
        rest = fields[count:]
        offset, fits = [], {}
        if rest and rest[0] == "offset":
            offset, rest = rest[1:5], rest[5:]
        if rest and rest[0] == "fits":
            for i in range(int(rest[1])):
                start = 2 + 13*i
                fits[int(rest[start])] = list(map(float, rest[start+1:start+13]))
        elif rest:
            raise ValueError(f"Unknown socket data {rest}")
        return cls(key, label, int(required), list(map(float, frame.split())), parts, offset, fits)

    def pose(self, index):
        if index in self.fits:
            return self.fits[index]
        frame = self.frame.copy()
        if self.offset and index >= int(self.offset[0]):
            for axis in range(3):
                frame[3+4*axis] += float(self.offset[axis+1])
        return frame

    def add(self, part, pose):
        if part in self.parts:
            return
        if self.offset or pose != self.frame:
            self.fits[len(self.parts)] = pose
        self.parts.append(part)

    def prefer(self, part):
        placed = [(choice, self.pose(i)) for i, choice in enumerate(self.parts)]
        placed.sort(key=lambda entry: entry[0] != part)
        self.parts, self.offset, self.fits = [], [], {}
        for choice, pose in placed:
            self.add(choice, pose)

    def line(self):
        frame = " ".join(f"{v:.9g}" for v in self.frame)
        tail = [" ".join(map(str, self.parts))]
        if self.offset:
            tail.append("offset " + " ".join(self.offset))
        if self.fits:
            tail.append(f"fits {len(self.fits)} " + " ".join(
                f"{index} " + " ".join(f"{v:.9g}" for v in pose)
                for index, pose in self.fits.items()))
        line = "\t".join((self.key, self.label, str(self.required), str(len(self.parts)), frame, *tail))
        if len(line) >= 8191:
            raise ValueError(f"Choice list exceeds the game limit for {self.key}")
        return line


class Pack:
    def __init__(self, source):
        self.source = source
        self.archive = ZipFile(source) if source.suffix == ".zip" else None
        if self.archive:
            read = self.zip_json
            self.code = source.stem
            self.items = read("/database/items.json")
            manifest = read("/bundles.json")
            self.bundles = {n.split("/bundles/", 1)[1]: n for n in self.archive.namelist()
                            if "/bundles/" in n and n.endswith(".bundle")}
        else:
            self.code = "Painter"
            self.items = read_json(source / "db/CustomItems/items.json")
            manifest = read_json(source / "bundles.json")
            self.bundles = {str(path.relative_to(source / "bundles")).replace("\\", "/"): path
                            for path in (source / "bundles").rglob("*.bundle")}
        entries = manifest.get("manifest", manifest) if isinstance(manifest, dict) else manifest
        self.dependencies = {item["key"]: item.get("dependencyKeys", []) for item in entries}

    def zip_json(self, suffix):
        member = next((n for n in self.archive.namelist() if n.endswith(suffix)), None)
        return json.loads(self.archive.read(member)) if member else {}

    def item(self, value):
        if self.archive:
            props = value["items"].get("_props", {})
            return value["clone"], value["locales"]["Name"], value["locales"]["Shortname"], props
        names = value.get("locales", {}).get("en", {})
        return (value.get("itemTplToClone", ""), names.get("name", ""), names.get("shortName", ""),
                value.get("overrideProperties", {}))

    def files(self, key):
        pending, visited = [key], set()
        if "container.bundle" in key:
            folder = key.rsplit("/", 1)[0] + "/"
            pending.extend(n for n in self.bundles if n.startswith(folder))
        while pending:
            item = pending.pop(0)
            if item in visited:
                continue
            visited.add(item)
            if item in self.bundles:
                yield item
            pending.extend(self.dependencies.get(item, []))

    @lru_cache(maxsize=12)
    def textures(self, key):
        bundle = self.bundles[key]
        env = UnityPy.load(self.archive.read(bundle) if self.archive else str(bundle))
        textures = {}
        for reader in env.objects:
            if reader.type.name != "Texture2D":
                continue
            tex = reader.read()
            name = tex.m_Name.lower()
            if min(tex.m_Width, tex.m_Height) < 64 or re.search(
                    r"gloss|spec|normal|nrm|mask|icon|reticle|marks|thermal|roughness", name):
                continue
            image = tex.image.convert("RGBA")
            image.thumbnail((512, 512), Image.Resampling.LANCZOS)
            textures[name] = image
        return textures


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def fingerprint(picture):
    a = np.asarray(picture.convert("L").resize((48, 48), Image.Resampling.BILINEAR), dtype=np.float32)
    a -= a.mean()
    norm = np.linalg.norm(a)
    return a / norm if norm else a


def bake_paint(mesh, output, textures, threshold):
    materials, faces = read_mesh(mesh)
    baked = []
    for data in materials:
        size = struct.unpack_from("<HH", data)
        base = Image.frombytes("RGBA", size, data[4:])
        signal = fingerprint(base)
        matches = []
        for name, image in textures.items():
            weight = .04 if ("diff" in name or "albedo" in name or "basecolor" in name) else 0
            matches.append((float(np.sum(signal * fingerprint(image))) + weight, name, image))
        score, _, paint = max(matches, key=lambda match: match[0])
        if score < threshold:
            raise ValueError(f"texture layout differs (score {score:.2f})")
        paint = paint.resize(size, Image.Resampling.LANCZOS)
        paint.putalpha(base.getchannel("A"))
        baked.append(struct.pack("<HH", *size) + paint.tobytes())
    save_mesh(output, baked, bytearray().join(CORNER.pack(*v) for v in faces))


def reflex_profile(models):
    modes = []
    for _, points, _, _, _, texture in sorted(models, key=lambda model: model[0], reverse=True):
        fixed = points[:, (0, 2, 1)] * (1, 1, -1)
        low, high = fixed.min(axis=0), fixed.max(axis=0)
        centre, half = (low + high) * .5, (high-low) * .5
        if min(half[0], half[2]) < .003:
            continue
        image = texture.image.convert("RGBA")
        image.thumbnail((256, 256), Image.Resampling.LANCZOS)
        texels = np.array(image)
        if texels[:, :, 3].min() == 255:
            texels[:, :, 3] = np.max(texels[:, :, :3], axis=2)
        texels[:, :, :3] = [255, 48, 24]
        angles = np.linspace(0, 2*math.pi, 32, endpoint=False)
        boundary = np.column_stack((np.cos(angles), np.sin(angles))).astype("<f4")
        values = (*centre, 1., 0., 0., 0., 0., -1., 0., 1., 0.,
                  half[0], half[2], .5, .5, 17., 1., 1.)
        modes.append(struct.pack("<4I19f", 1, len(boundary), *image.size, *values)
                     + boundary.tobytes() + texels.tobytes())
    if not modes:
        raise ValueError("reflex model includes no usable lens")
    return b"GEVROPT4" + struct.pack("<I", len(modes)) + b"".join(modes)


def bake_custom(pack, key, donor_mesh, output, fitting):
    # Imported models retain their authored local origin. The custom reflex
    # meshes provide separate lens and reticle geometry in that same frame.
    models, anchors = prefabs(pack.bundles[key], skip_unmapped=True)
    optical = None
    if fitting & OPTIC:
        lenses = [model for model in models if model[0].lower().startswith("linza")]
        optical = reflex_profile(lenses)
        models = [model for model in models if not model[0].lower().startswith("linza")]
    levels = defaultdict(list)
    for model in models:
        tag = re.search(r"lod[_ ]?([0-9])\b", model[0], re.I)
        levels[int(tag[1]) if tag else 0].append(model)
    models = next((group for _, group in sorted(levels.items())
                   if sum(len(model[4]) for model in group) <= 24000), None)
    if not models:
        raise ValueError("custom part has no supported textured geometry")
    bounds = np.asarray([v[:3] for v in read_mesh(donor_mesh)[1]])
    pictures, indices, faces = [], {}, bytearray()
    for _, points, normals, uv, triangles, texture in models:
        name = texture.m_Name
        if name not in indices:
            indices[name] = len(pictures)
            pictures.append(rgba(texture.image.convert("RGBA")))
        positions = points[:, (0, 2, 1)] * (1, 1, -1)
        if np.max(np.abs(positions)) > max(.23, 3.5 * np.max(np.abs(bounds))):
            raise ValueError("custom part origin differs from the fitted donor")
        shades = (255 * (.66 + .34 * np.maximum(0, normals @ (.28, .87, -.4)))).clip(0, 255)
        texture_index = indices[name]
        for triangle in triangles:
            for v in triangle:
                s, t = uv[v][:2]
                shade = int(shades[v])
                faces.extend(CORNER.pack(*positions[v], s, 1-t, shade, shade, shade, 255, texture_index))
    save_mesh(output, pictures, faces)
    return anchors, optical


def cloned_sockets(block, anchors):
    choices = []
    for line in block[2:]:
        socket = Socket.parse(line)
        if anchors is not None:
            position = anchors.get(socket.key)
            if position is not None:
                authored = [position[0], position[2], position[1]]
                for axis, offset in enumerate(authored):
                    i = 3 + 4*axis
                    shift = offset - socket.frame[i]
                    socket.frame[i] += shift
                    for frame in socket.fits.values():
                        frame[i] += shift
            elif socket.key in ("mod_muzzle", "mod_gas_block", "mod_handguard") and socket.required:
                raise ValueError(f"custom part is missing its {socket.key} anchor")
        choices.append(socket.line())
    return [block[0], str(len(choices)), *choices]


def write_catalog(path, rows, sockets, builds):
    lines = ["GEVRGEAR3", str(len(rows)), *rows, *(line for block in sockets for line in block)]
    for key, (root, defaults) in builds.items():
        lines.extend((f"{key} {root} {len(defaults)}", *defaults))
    path.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")


def integrate(source, destination, packs, report):
    rows, sockets, builds = read_catalog(source / "catalog.txt")
    by_id = {row.split("\t", 1)[0]: i for i, row in enumerate(rows)}
    # These two root models were rebaked as split AR receiver meshes.
    by_id["5447a9cd4bdc2dbd208b4567"] = builds[16][0]
    by_id["5bb2475ed4351e00853264e3"] = builds[17][0]
    roots = {index for index, _ in builds.values()}
    available = roots | {part for block in sockets for line in block[2:] for part in Socket.parse(line).parts}
    variants, origin, finish, local = defaultdict(list), {}, defaultdict(list), defaultdict(list)
    results = []
    for pack in packs:
        for ident, item in pack.items.items():
            if not item.get("enable", True):
                continue
            donor, title, short, props = pack.item(item)
            if not title or not donor:
                continue
            key = props.get("Prefab", {}).get("path", "")
            source_ids = (donor, stable_id(donor + ":full AR"))
            matches = local.get(donor) or list(dict.fromkeys(
                by_id[key] for key in source_ids if key in by_id and by_id[key] in available
            ))
            if not matches:
                continue
            textures = None
            for base in matches:
                base_id, capacity, old_flags, old_name = rows[base].split("\t", 3)
                if base in roots and not re.search(r"Colt M4A1|HK 416A5", title):
                    continue
                flags = int(old_flags)
                if not flags & MESH or key not in pack.bundles:
                    results.append((pack.code, title, "bundle or painted base unavailable"))
                    continue
                imported = ident if not local[ident] else stable_id(f"{ident}:workshop:{base_id}")
                out_mesh = destination / "meshes" / (imported + ".bin")
                donor_dir = destination if base in origin else source
                donor_mesh = donor_dir / "meshes" / (base_id + ".bin")
                custom = not pack.archive and key.startswith("tgc/")
                calibrated = None
                try:
                    if not out_mesh.is_file() or custom and flags & OPTIC:
                        if custom:
                            anchors, calibrated = bake_custom(pack, key, donor_mesh, out_mesh, flags)
                        else:
                            if textures is None:
                                textures = {name: img for path in pack.files(key)
                                            for name, img in pack.textures(path).items()}
                            if not textures:
                                raise ValueError("no body texture in shipped bundles")
                            bake_paint(donor_mesh, out_mesh, textures, .19 if custom else .05)
                            anchors = None
                    else:
                        anchors = None
                        if custom and not flags & OPTIC:
                            _, anchors = prefabs(pack.bundles[key], skip_unmapped=True)
                    block = ["0", "0"] if base in roots else cloned_sockets(sockets[base], anchors)
                except (OSError, ValueError, RuntimeError, KeyError, TypeError, IndexError, AttributeError) as exc:
                    if out_mesh.is_file():
                        out_mesh.unlink()
                    results.append((pack.code, title, str(exc)[:180]))
                    continue
                if calibrated is not None:
                    (destination / "optics" / (imported + ".opt")).write_bytes(calibrated)
                elif (donor_dir / "optics" / (base_id + ".opt")).is_file():
                    optic = (donor_dir / "optics" / (base_id + ".opt")).read_bytes()
                    (destination / "optics" / (imported + ".opt")).write_bytes(optic)
                    for variant in (1, 2):
                        folded = donor_dir / "meshes" / f"{base_id}.fold{variant}.bin"
                        if folded.is_file():
                            (destination / "meshes" / f"{imported}.fold{variant}.bin").write_bytes(folded.read_bytes())
                part = len(rows)
                group = colour(title)
                flags = (flags & ~(COLOUR_MASK << COLOUR_SHIFT)) | (group << COLOUR_SHIFT)
                if suppressor(title):
                    flags |= SUPPRESSOR
                finish_hint = re.search(r"\(([^()]+)\)\s*$", title)
                label = short or old_name
                if finish_hint and finish_hint[1].lower() not in label.lower():
                    label += f" ({finish_hint[1]})"
                if base in roots:
                    label = finish_hint[1] if finish_hint else label
                    finish[base].append(part)
                else:
                    variants[base].append(part)
                if props.get("Cartridges"):
                    capacity = str(props["Cartridges"][0].get("_max_count", capacity))
                rows.append("\t".join((imported, capacity, str(flags), ascii_label(label)[:89])))
                sockets.append(block)
                local[ident].append(part)
                origin[part] = (base, pack.code)
                results.append((pack.code, title, "imported"))
    for parent, parts in finish.items():
        sock = Socket("mod_finish", "Finish", 0,
                      [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0], parts, [], {})
        sockets[parent].append(sock.line())
        sockets[parent][1] = str(len(sockets[parent])-2)
    for base in reversed(list(variants)):
        variants[base] += [relative for child in variants[base] for relative in variants.get(child, ())]
    for owner, block in enumerate(sockets):
        conflicts = list(map(int, block[0].split()[1:]))
        conflicts += [variant for part in conflicts for variant in variants.get(part, ())]
        block[0] = " ".join(map(str, (len(conflicts), *conflicts)))
        for i, line in enumerate(block[2:], 2):
            socket = Socket.parse(line)
            for pos, part in enumerate(socket.parts.copy()):
                for variant in variants.get(part, ()):
                    socket.add(variant, socket.pose(pos))
            if socket.required == 2 and owner in origin:
                desired = next((part for part in socket.parts if part in origin
                                and origin[part][1] == origin[owner][1]
                                and (int(rows[owner].split("\t")[2]) >> COLOUR_SHIFT & COLOUR_MASK)
                                == (int(rows[part].split("\t")[2]) >> COLOUR_SHIFT & COLOUR_MASK)), None)
                if desired is not None:
                    socket.prefer(desired)
            block[i] = socket.line()
    if len(rows) > 1536:
        raise ValueError("Compiled Workshop part limit exceeded")
    write_catalog(destination / "catalog.txt", rows, sockets, builds)
    report.write_text(json.dumps({"local": local, "parts": results}, indent=2), encoding="utf-8")
    print(f"Imported {len(origin)} fitted choices ({len(local)} source items), {len(rows)} catalog entries")
    for pack in packs:
        good = sum(code == pack.code and state == "imported" for code, _, state in results)
        skipped = sum(code == pack.code and state != "imported" for code, _, state in results)
        print(f"{pack.code}: {good} choices, {skipped} applicable assets need review")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("report", type=Path)
    parser.add_argument("--unity-python", type=Path, required=True)
    parser.add_argument("packs", type=Path, nargs="+")
    args = parser.parse_args()
    sys.path.insert(0, str(args.unity_python))
    global np, UnityPy, Image, prefabs, rgba
    import numpy as np
    import UnityPy
    from PIL import Image
    from prepare_tactical_svd import prefabs, rgba
    integrate(args.source, args.destination, [Pack(path) for path in args.packs], args.report)


if __name__ == "__main__":
    main()
