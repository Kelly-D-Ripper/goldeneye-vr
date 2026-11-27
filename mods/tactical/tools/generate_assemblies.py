"""Generate scripts/tactical/assemblies.lua from the native Tactical build.

    python tools/generate_assemblies.py <assemblies.txt> <gear/meshes folder> [output]

<assemblies.txt> is tools/export_assemblies.c's output, built against the
native source (branch tactical/native-0.18.4) and run on the 0.18.4 pack.
Every gun build the native game can deploy (its stock assembly and each
named preset) becomes a list of pack meshes with the frame the native
renderer draws them at:

* frame = the mount's native frame times the gun's native axes
  (gevr_tactical_mesh.c gearComposeFrame and gearGunAxes, first person),
  converted to mod.attach's basis for C10's converted pack meshes. The
  conversion matches the Whiteout desktop replay to 2e-5.
* muzzle = native gearMuzzlePoint (held): the far end of the build's
  barrel and muzzle parts, in the body's own converted units, for
  mod.weapon.model's muzzle option (#344).

It also writes the list of meshes the pack ships (assets-manifest.txt), for
tools/stage_pack.py. Run it again whenever the native catalogue changes; the
result is committed, so the mod never reads the native files.
"""
import struct
import sys
from pathlib import Path

PACK = Path(__file__).resolve().parent.parent


def read_assemblies(path):
    builds, cur = [], None
    for line in Path(path).read_text().splitlines():
        f = line.split(" ")
        if f[0] == "BUILD":
            cur = {"key": int(f[1]), "preset": " ".join(f[2:]), "mounts": []}
            builds.append(cur)
        elif f[0] == "STATS":
            v = f[1:]
            cur["stats"] = dict(modes=int(v[0]), rpm=int(v[1]), semiRpm=int(v[2]), burst=int(v[3]),
                                mass=float(v[4]), ergo=float(v[5]), recoil=float(v[6]), spread=float(v[7]),
                                damage=float(v[8]), flash=float(v[9]), suppressed=v[10] == "1")
        elif f[0] == "AXES":
            cur["axes"] = [[float(x) for x in f[1 + r * 4:5 + r * 4]] for r in range(4)]
        elif f[0] == "ACTION":
            cur["action"] = (float(f[1]), int(f[2]))
        elif f[0] == "MOUNT":
            frame = [[float(x) for x in f[3 + r * 4:7 + r * 4]] for r in range(4)]
            cur["mounts"].append({"path": "" if f[1] == "-" else f[1], "part": f[2], "frame": frame, "motion": 0})
        elif f[0] == "MOTION":
            cur["mounts"][-1]["motion"] = int(f[1])
    return builds


def compose(frame, axes):
    return [[sum(frame[r][k] * axes[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def game_frame(m):
    """The native mesh frame to mod.attach's rows for a C10 converted mesh:
    C10 reads (x, y, z) as 1000 * (-x, z, y), so the converted X axis is the
    native -X, Y the native Z and Z the native Y; the origin is the same."""
    return [[-m[0][0], -m[0][1], -m[0][2]], m[2][:3], m[1][:3], m[3][:3]]


def mesh_info(path):
    """Picture count and converted-unit bounds of a GEVRWPN1/2 file."""
    b = path.read_bytes()
    if b[:8] not in (b"GEVRWPN1", b"GEVRWPN2"):
        raise ValueError(f"{path}: not a pack mesh")
    pictures = struct.unpack_from("<I", b, 8)[0]
    off = 12
    for _ in range(pictures):
        w, h = struct.unpack_from("<HH", b, off)
        off += 4 + w * h * 4
    tris = struct.unpack_from("<I", b, off)[0]
    off += 4
    lo, hi = [1e30] * 3, [-1e30] * 3
    for i in range(tris * 3):
        x, y, z = struct.unpack_from("<3f", b, off + i * 28)
        p = (-1000 * x, 1000 * z, 1000 * y)
        for a in range(3):
            lo[a], hi[a] = min(lo[a], p[a]), max(hi[a], p[a])
    return pictures, lo, hi


def transform_bounds(frame, lo, hi):
    """frame rows: X, Y, Z axes then origin (row vectors)."""
    out_lo, out_hi = list(frame[3]), list(frame[3])
    for c in range(3):
        for r in range(3):
            a, b = lo[r] * frame[r][c], hi[r] * frame[r][c]
            out_lo[c] += min(a, b)
            out_hi[c] += max(a, b)
    return out_lo, out_hi


def solve_local(frame, point):
    """The point in the frame's own units: point = origin + x X + y Y + z Z."""
    X, Y, Z, O = frame
    d = [point[i] - O[i] for i in range(3)]
    det = (X[0] * (Y[1] * Z[2] - Y[2] * Z[1]) - Y[0] * (X[1] * Z[2] - X[2] * Z[1])
           + Z[0] * (X[1] * Y[2] - X[2] * Y[1]))

    def col_det(c0, c1, c2):
        return (c0[0] * (c1[1] * c2[2] - c1[2] * c2[1]) - c1[0] * (c0[1] * c2[2] - c0[2] * c2[1])
                + c2[0] * (c0[1] * c1[2] - c0[2] * c1[1]))
    return [col_det(d, Y, Z) / det, col_det(X, d, Z) / det, col_det(X, Y, d) / det]


def num(v):
    s = f"{v:.6f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def lua_frame(f):
    return "{" + ", ".join("{" + ", ".join(num(v) for v in row) + "}" for row in f) + "}"


def lua_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    builds = read_assemblies(sys.argv[1])
    meshes = Path(sys.argv[2])
    out = Path(sys.argv[3]) if len(sys.argv) > 3 else PACK / "scripts" / "tactical" / "assemblies.lua"
    info, used = {}, set()

    def mesh(part):
        if part not in info:
            p = meshes / f"{part}.bin"
            info[part] = mesh_info(p) if p.exists() else None
        return info[part]

    lines = [
        "-- Generated by tools/generate_assemblies.py from the native Tactical build's",
        "-- gun assemblies (tools/export_assemblies.c). Do not edit by hand.",
        "--",
        "-- [key][preset] = { muzzle = {x, y, z} or nil, stats = {...}, parts = { {model, path, frame}, ... } }",
        "-- stats: gevr_tactical_stats.c gevrWeaponStats for the build (fire modes, rates, mass,",
        "-- ergonomics, recoil, spread, damage, flash) and whether it's suppressed.",
        "-- preset \"-\" is the stock build. parts[1] is the body (path \"\"): mod.weapon.model",
        "-- with this frame; the rest are mod.attach on the gun. frame rows: X, Y, Z axes",
        "-- then origin, in the gun model's units (mod.attach's basis). action: how far the",
        "-- moving parts go fully open (carrier units, back along the gun) and the shot's base",
        "-- return in ticks (gevrTacticalShotFired); a part's motion: 1 with each shot and the",
        "-- rack, 2 with the rack only (gevr_tactical_mesh.c buildGearGun).",
        "return {",
    ]
    by_key = {}
    for b in builds:
        by_key.setdefault(b["key"], []).append(b)
    skipped = 0
    for key in sorted(by_key):
        lines.append(f"  [{key}] = {{")
        for b in by_key[key]:
            parts, muzzle, body = [], None, None
            for m in b["mounts"]:
                # gevr_tactical_mesh.c draws neither root choice: the finish or
                # variant is already the body's part (gevr_tactical_gear.c buildView)
                if m["path"] in ("mod_finish", "mod_variant"):
                    continue
                mi = mesh(m["part"])
                if mi is None:
                    skipped += 1
                    continue
                fr = game_frame(compose(m["frame"], b["axes"]))
                if m["path"] == "":
                    body = fr
                parts.append((m["part"], m["path"], fr, m["motion"]))
                used.add(m["part"])
                socket = m["path"].rsplit("/", 1)[-1]
                if socket.startswith("mod_muzzle") or socket == "mod_barrel":
                    lo, hi = transform_bounds(fr, mi[1], mi[2])
                    if muzzle is None or hi[2] > muzzle[2]:
                        muzzle = [fr[3][0], fr[3][1], hi[2]]
            if not parts or parts[0][1] != "" or parts[0][3]:
                raise SystemExit(f"build {key} {b['preset']}: no body first")
            m_local = solve_local(body, muzzle) if muzzle else None
            lines.append(f"    [{lua_str(b['preset'])}] = {{")
            if m_local:
                lines.append(f"      muzzle = {{{', '.join(num(v) for v in m_local)}}},")
            st = b.get("stats")
            if st:
                lines.append("      stats = { " + ", ".join(
                    f"{k} = {('true' if v else 'false') if isinstance(v, bool) else num(v)}" for k, v in st.items()) + " },")
            if b.get("action") and any(motion for *_, motion in parts):
                shift, duration = b["action"]
                lines.append(f"      action = {{ travel = {num(shift)}, duration = {duration} }},")
            lines.append("      parts = {")
            for part, path, fr, motion in parts:
                moves = f", motion = {motion}" if motion else ""
                lines.append(f"        {{ model = {lua_str(part + '.bin')}, path = {lua_str(path)}, frame = {lua_frame(fr)}{moves} }},")
            lines.append("      },")
            lines.append("    },")
        lines.append("  },")
    lines.append("}")
    out.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
    manifest = PACK / "tools" / "assets-manifest.txt"
    manifest.write_text("".join(f"{p}.bin {info[p][0]}\n" for p in sorted(used)), encoding="ascii", newline="\n")
    print(f"wrote {out}: {len(builds)} builds, {len(used)} meshes, {skipped} mounts without a mesh")


if __name__ == "__main__":
    main()
