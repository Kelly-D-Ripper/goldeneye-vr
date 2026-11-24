"""The Tactical mod pack, end to end in the game's real script host.

Builds port/tests/modapi_native.c (the production gevr_modapi.c and
gevr_modscript.c over a fake game) with three extra commands for these
scenarios, copies this pack's scripts in, and drives missions:

    complete                       the debrief (gevrModScriptMissionComplete)
    stats <item> <mag> <rate> <dmg>  give a cartridge gun stats (rate -1: not automatic)
    setammo <type> <rounds>        the game changes a spare-round count

Run from anywhere: python mods/tactical/tests/test_tactical.py
Needs a C compiler on PATH (gcc or cc; llvm-mingw's works on Windows).
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

PACK = Path(__file__).resolve().parent.parent
ROOT = PACK.parent.parent
sys.path.insert(0, str(PACK / "tools"))
from validate_pack import validate  # noqa: E402

LUA = ["lapi", "lcode", "lctype", "ldebug", "ldo", "ldump", "lfunc", "lgc", "llex", "lmem", "lobject", "lopcodes",
       "lparser", "lstate", "lstring", "ltable", "ltm", "lundump", "lvm", "lzio", "lauxlib", "lbaselib", "lcorolib",
       "lmathlib", "lstrlib", "ltablib", "lutf8lib"]
EXTRA = r'''        } else if (!strcmp(cmd, "complete")) {
            gevrModScriptMissionComplete();
        } else if (!strcmp(cmd, "stats") && i + 4 < argc) {
            stats(atoi(argv[i + 1]), atoi(argv[i + 2]), atoi(argv[i + 3]), (float)atof(argv[i + 4]));
            i += 4;
        } else if (!strcmp(cmd, "setammo") && i + 2 < argc) {
            s_ammo[atoi(argv[i + 1])] = atoi(argv[i + 2]);
            i += 2;
'''
FAILS = []

# cartridge items (mod.items) and ammo types (mod.ammo) the scenarios use
WPPK, AK47, UZI, SPECTRE, M16 = 4, 8, 9, 12, 13
NINE, RIFLE = 1, 3


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        FAILS.append(what)


def build(temp):
    cc = os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc")
    src = (ROOT / "port/tests/modapi_native.c").read_text()
    anchor = '        } else if (!strcmp(cmd, "ammois")'
    assert anchor in src
    harness = temp / "tactical_harness.c"
    harness.write_text(src.replace(anchor, EXTRA + anchor, 1))
    flags = ["-std=gnu11", "-O1", "-funsigned-char", "-fwrapv", "-fno-strict-aliasing", "-D_LANGUAGE_C=1", "-DGEVR=1"]
    objects = []
    for name in LUA:
        obj = temp / (name + ".o")
        subprocess.run([cc, *flags, "-c", str(ROOT / "port/external/lua" / (name + ".c")), "-o", str(obj)], check=True)
        objects.append(str(obj))
    exe = temp / ("harness.exe" if os.name == "nt" else "harness")
    subprocess.run([cc, *flags, "-I" + str(ROOT / "include"), "-I" + str(ROOT / "port/include"),
                    "-I" + str(ROOT / "port/tests"), str(harness), str(ROOT / "port/src/gevr_modapi.c"),
                    str(ROOT / "port/src/gevr_modscript.c"), *objects, "-lm", "-o", str(exe)], check=True)
    return exe


class Run:
    def __init__(self, exe, temp, name, save=None, settings=None):
        self.exe, self.dir = exe, temp / name
        shutil.copytree(PACK / "scripts", self.dir / "mods/tactical/scripts")
        (self.dir / "save").mkdir(parents=True)
        self.savedir = None
        self.save, self.settings = save, settings

    def __call__(self, *commands):
        # the save folder the host gives a mod: find it after the first run
        if self.save is not None or self.settings is not None:
            for d in [self.dir / "save" / "mod-data" / "tactical"]:
                d.mkdir(parents=True, exist_ok=True)
                if self.save is not None:
                    (d / "progress.txt").write_text(self.save)
                if self.settings is not None:
                    (d / "settings.txt").write_text(self.settings)
            self.save = self.settings = None
        out = subprocess.run([str(self.exe), *map(str, commands)], cwd=self.dir, capture_output=True, text=True,
                             timeout=120)
        assert out.returncode == 0, out.stderr + out.stdout
        return out.stdout

    def progress(self):
        f = self.dir / "save" / "mod-data" / "tactical" / "progress.txt"
        return f.read_text() if f.exists() else ""


def logs(out):
    return [l.split(" ", 2)[2] for l in out.splitlines() if l.startswith("LOG ")]


def lines(out, prefix):
    return [l for l in out.splitlines() if l.startswith(prefix)]


def errors(out):
    return [l for l in logs(out) if "error" in l.lower() or "stopped" in l.lower() or "traceback" in l.lower()]


ALL = "mode 2\nloadout {p} {s}\n"
GUNS = ["stats", M16, 30, 4, 1.5, "stats", SPECTRE, 30, 4, 1.0, "stats", UZI, 32, 3, 1.0]


def draw(stage, gid):
    """scripts/tactical/util.lua util.draw: the 0.18.4 roster roll."""
    m = 0xffffffff
    d = ((stage & 0xff) << 16) | (gid & 0xffff)
    d = ((d ^ (d >> 16)) * 0x7feb352d) & m
    d = ((d ^ (d >> 15)) * 0x846ca68b) & m
    return (d ^ (d >> 16)) & m


def elite_hk416(stage):
    """A Janus special forces guard id whose roster gun is the HK416."""
    for gid in range(1, 500):
        d = draw(stage, gid)
        if d % 100 >= 18 and d % 8 in (4, 5):
            return gid
    raise AssertionError("no HK416 guard")


def scenarios(exe, temp):
    # 1. It loads in the real sandbox and asks the one-time question.
    r = Run(exe, temp, "load")
    out = r("start", "prepare", 34, "begin", 34, "frame", "ui")
    check(not errors(out), "loads and runs a mission without a script error: %s" % errors(out)[:3])
    check(any("42 guns" in l for l in logs(out)), "reports its 42 guns")
    check("PANEL open" in out, "opens the screen on the unlock question at the first mission")
    check(any("tac_progress Tactical|Unlock every gun" in l for l in lines(out, "UIPAGE")), "the question's page")
    check(any("tac_loadout Loadout" in l for l in lines(out, "UIPAGE")), "the loadout page")
    # answer No: progression, then the question never shows again
    page = next(int(l.split()[1]) for l in lines(out, "UIPAGE") if "tac_progress" in l)
    out = r("start", "prepare", 34, "begin", 34, "uiclick", page, 1, "frame", "ui")
    check("mode 1" in r.progress(), "No saves a progression save")
    out = r("start", "prepare", 34, "begin", 34, "frame", "ui")
    check("PANEL open" not in out and not any("Unlock every gun" in l for l in lines(out, "UIPAGE")),
          "the question isn't asked again")

    # 2. A progression save starts with the PPK alone: the mission's guns go.
    r = Run(exe, temp, "kit", save="mode 1\nloadout -1 1\n")
    out = r("start", "prepare", 34, "own", AK47, "own", UZI, "hold", 0, AK47, 30, "begin", 34, "inv")
    inv = lines(out, "INV")[-1]
    check(" 4 " in inv + " " and " 8 " not in inv + " " and " 9 " not in inv + " ",
          "progression kit: the PPK in place of the mission's guns (%s)" % inv)

    # 3. Magazines: a fresh rig, the chamber, a reload, the reserve pinned.
    r = Run(exe, temp, "mags", save=ALL.format(p=4, s=1))   # AK-74N on the AK, PPK
    out = r("start", "prepare", 34, "begin", 34, "setammo", RIFLE, 0,
            "draw", 0, AK47, "loadedis", 0, "show", AK47, "frame", "ammois", RIFLE,
            "fire", 0, "fire", 0, "reload", 0, 99, "loadedis", 0, "ammois", RIFLE)
    check(not errors(out), "magazine scenario without errors: %s" % errors(out)[:3])
    loaded = [int(l.split()[1]) for l in lines(out, "LOADED")]
    check(loaded[:1] == [31], "the gun comes up with a full 30-round magazine and one chambered (%s)" % loaded)
    check(lines(out, "STATS %d " % AK47)[0].split()[2] == "31", "the AK's magazine size is the seated magazine's + 1")
    ammo = [int(l.split()[2]) for l in lines(out, "AMMO %d " % RIFLE)]
    check(ammo[0] == 30, "the reserve is pinned to the next magazine's rounds (%s)" % ammo)
    check(loaded[1:] == [31], "a reload seats a full magazine and keeps the chambered round (%s)" % loaded)
    check(not any("dropped" in l for l in logs(out)),
          "the part-used magazine goes into the pouch the fresh one came from, not the floor")
    check(ammo[-1] == 30, "the reserve is pinned to the next full magazine after the reload (%s)" % ammo)

    # emptied magazines (a hand reload of a dry gun) aren't kept: the rig runs out
    empty = []
    for _ in range(3):
        empty += ["hold", 0, AK47, 0, "frame", "reload", 0, 0, "frame"]
    out = r("start", "prepare", 34, "begin", 34, "draw", 0, AK47, "frame", *empty, "ammois", RIFLE,
            "frame", "frame", "setammo", RIFLE, 45, "frame", "ammois", RIFLE)
    ammo = [int(l.split()[2]) for l in lines(out, "AMMO %d " % RIFLE)]
    check(ammo[0] == 0, "with the pouches empty the reserve is 0, so no reload can start (%s)" % ammo)
    check(ammo[1] == 30, "45 loose rounds the game adds pack a full magazine next (%s)" % ammo)

    # 4. Fire modes on an automatic: semi declines the second shot of a pull.
    r = Run(exe, temp, "modes", save=ALL.format(p=4, s=1) + "firemode 4 semi\n")
    out = r("start", "prepare", 34, "begin", 34, "draw", 0, AK47, "frame",
            "fire", 0, "fire", 0, "fire", 0)
    shots = [l.split()[0] for l in out.splitlines() if l.startswith(("FIRED", "DECLINED"))]
    check(shots == ["FIRED", "DECLINED", "FIRED"], "semi: one shot a pull (%s)" % shots)
    # in time: an automatic's ticks 6 frames apart are one pull; a pause of half a second is a new one
    out = r("start", "prepare", 34, "begin", 34, "draw", 0, AK47, "frame",
            "fire", 0, *(["frame"] * 6), "fire", 0, *(["frame"] * 30), "fire", 0)
    shots = [l.split()[0] for l in out.splitlines() if l.startswith(("FIRED", "DECLINED"))]
    check(shots == ["FIRED", "DECLINED", "FIRED"], "semi in time: held trigger declined, a new pull fires (%s)" % shots)
    out = r("start", "prepare", 34, "begin", 34, "draw", 0, AK47, "show", AK47)
    check(lines(out, "STATS %d " % AK47)[0].split()[3] == "6", "the AK-74N's 650 rpm is 6 ticks a shot")
    r = Run(exe, temp, "burst", save=ALL.format(p=2, s=1) + "firemode 2 burst\n")   # MP5 on the Phantom
    out = r("start", "prepare", 34, *GUNS, "begin", 34, "draw", 0, SPECTRE, "frame",
            "fire", 0, "fire", 0, "fire", 0, "fire", 0, "fire", 0)
    shots = [l.split()[0] for l in out.splitlines() if l.startswith(("FIRED", "DECLINED"))]
    check(shots == ["FIRED", "FIRED", "FIRED", "DECLINED", "FIRED"], "burst: three shots a pull (%s)" % shots)
    check(not errors(out), "burst scenario without errors: %s" % errors(out)[:3])
    # the off hand's grip at the chest switches the mode
    out = r("start", "prepare", 34, *GUNS, "begin", 34, "draw", 0, SPECTRE, "frame", "grip", 0, 4, 0, "frame")
    check("firemode 2 auto" in r.progress(), "a chest grip cycles burst to auto")
    check(len(lines(out, "HAPTIC")) >= 1, "the switch buzzes")

    # 5. Guard rosters: the 0.18.4 roll, the same everywhere.
    r = Run(exe, temp, "roster")
    out = r("start", "prepare", 34,
            "spawning", 7, 0, 2, 50, AK47,      # a Russian soldier with an AK: stays an AK
            "spawning", 11, 1, 20, 50, AK47,    # a Janus marine: the marine roster
            "spawning", 12, 2, 4, 50, AK47)     # Janus special forces: the elite roster
    guards = {int(l.split()[1]): int(l.split()[7]) for l in lines(out, "GUARD")}
    check(guards[7] == AK47, "Russian soldiers keep AK-pattern rifles")
    check(guards[11] in (SPECTRE, 10, UZI, 14, AK47) and guards[12] in (11, AK47, M16, UZI),
          "marines and special forces draw from their rosters (%s)" % guards)
    again = r("start", "prepare", 34, "spawning", 11, 1, 20, 50, AK47, "spawning", 12, 2, 4, 50, AK47)
    check([l for l in lines(again, "GUARD")] == lines(out, "GUARD")[1:], "the same roll on every load")

    # 6. Damage: the gun's multiplier on the player's own shots, not guards'.
    r = Run(exe, temp, "damage", save=ALL.format(p=15, s=1))   # PKM on the AK: 1.6x
    out = r("start", "prepare", 34, "begin", 34, "draw", 0, AK47, "frame",
            "hurt", 1, 3, 1.0, 1, 2, AK47, 0, "hurt", 1, 3, 1.0, 1, 2, AK47, -1)
    dmg = [l.split()[1] for l in lines(out, "DAMAGE")]
    check(dmg == ["1.60", "1.00"], "PKM rounds hit 1.6x; a guard's own AK doesn't change (%s)" % dmg)

    # co-op: a client tells the host its guns; the host applies them and sends the client its hits
    r = Run(exe, temp, "client", save=ALL.format(p=15, s=1))
    out = r("start", "online", 1, 0, 0, 1, "prepare", 34, "begin", 34, "draw", 0, AK47, "frame")
    sends = [l for l in lines(out, "NETSEND") if " guns " in l]
    text = bytes(int(b, 16) for b in sends[-1].split()[6:]).decode() if sends else ""
    check("8:15" in text, "a client tells the host its AK is a PKM (%s)" % text)
    r = Run(exe, temp, "host", save=ALL.format(p=4, s=1))
    out = r("start", "online", 1, 1, 0, 0, "prepare", 34, "begin", 34, "netrecv", "tactical", 1, "guns", "8:15",
            "frame", "hurt", 1, 3, 1.0, 1, 1, AK47, 1, "hurt", 1, 3, 1.0, 1, 1, AK47, 0)
    dmg = [l.split()[1] for l in lines(out, "DAMAGE")]
    check(dmg == ["1.60", "1.00"], "the host hits with the client's PKM and its own AK-74N (%s)" % dmg)
    stat = [l for l in lines(out, "NETSEND") if " stat " in l]
    check(len(stat) == 1 and stat[0].split()[2] == "1", "the client's headshot is sent to it alone")

    # 7. Progression: bring a gun home, earn reserve, unlock magazines by clears.
    r = Run(exe, temp, "extract", save="mode 1\nloadout -1 1\n")
    out = r("start", "prepare", 34, "begin", 34, "frame",
            "own", AK47, "took", "weapon", AK47, "draw", 0, AK47, "frame", "complete", "title")
    p = r.progress()
    check(re.search(r"^gun 4 1 ", p, re.M) is not None, "the AK-74N carried home unlocks:\n" + p)
    check("reserve 1 0" in p and "cleared 34" in p, "a first clear earns one reserve")
    check(any("Unlocked: AK-74N" in l for l in logs(out)), "the debrief names it")
    # a death loses it
    r = Run(exe, temp, "death", save="mode 1\nloadout -1 1\n")
    r("start", "prepare", 34, "begin", 34, "own", AK47, "draw", 0, AK47, "frame", "died", 0, "title")
    check(re.search(r"^gun 4 1 ", r.progress(), re.M) is None, "dying or leaving brings nothing home")
    # clears with an owned gun unlock its magazines: the 45s at two
    r = Run(exe, temp, "clears", save="mode 1\nloadout 4 1\ngun 4 1 0 0 0 0 1\n")
    out = r("start", "prepare", 35, "begin", 35, "draw", 0, AK47, "frame", "complete", "title")
    check(re.search(r"^gun 4 1 0 0 \d+ 0 2$", r.progress(), re.M) is not None, "a clear counts for the gun carried")
    check(any("AK-74N 45-rd mag" in l for l in logs(out)), "two clears unlock the 45-round magazines")
    # the PKM's box: no magazine unlocks, the 100 from the start
    r = Run(exe, temp, "pkm", save="mode 1\nloadout 15 1\ngun 15 1 0 0 0 0 0\n")
    out = r("start", "prepare", 34, "begin", 34, "draw", 0, AK47, "loadedis", 0)
    check(lines(out, "LOADED")[0] == "LOADED 101", "the PKM's 100-round box from the start (+1 chambered)")

    # 8. Reserve buys a gun once the campaign has enough clears.
    r = Run(exe, temp, "reserve", save="mode 1\nreserve 2 0\ncleared 33 34 35 36\nloadout -1 1\n")
    out = r("start", "prepare", 34, "begin", 34, "ui")
    page = next(int(l.split()[1]) for l in lines(out, "UIPAGE") if "tac_loadout" in l)
    # Rifles, then M4A1 (the screen counts choices from 0: None, AKS-74UN, AK-74N, M4A1)
    out = r("start", "prepare", 34, "begin", 34, "uiset", page, 1, 3, "frame", "uiclick", page, 2, "frame", "ui")
    check(re.search(r"^gun 16 1 ", r.progress(), re.M) is not None and "reserve 1 0" in r.progress(),
          "a reserve token buys the M4A1 after four clears")

    # 9. Reinforcements: a sighting on an open map calls a wave at its approach pads.
    r = Run(exe, temp, "reinforce", save=ALL.format(p=4, s=1))
    cmds = ["start", "prepare", 35, "begin", 35, "addguard", 3, 2, 50, 1, AK47, 0, 1, 1, 0, 0, 0]
    cmds += ["frame"] * 3
    # the first call waits 8 seconds of play; the fake clock moves 1 us a read, so use many frames
    out = r(*cmds, "alerted", 3, 1, 0, *(["frame"] * 5))
    check(not errors(out), "reinforcement scenario without errors: %s" % errors(out)[:3])
    check(not lines(out, "SPAWNGUARD"), "no wave in the first seconds of a mission")
    out = r(*cmds, *(["frame"] * 500), "alerted", 3, 1, 0, *(["frame"] * 160))
    spawns = lines(out, "SPAWNGUARD")
    pads = [int(l.split()[2]) for l in spawns]
    check(len(spawns) == 5, "after 8 seconds a sighting on Runway calls a wave of 5 (%d)" % len(spawns))
    check(pads == [90, 148, 125, 90, 148], "the wave comes in from Runway's approach pads (%s)" % pads)
    check(all(" body 2 " in l for l in spawns), "reinforcements wear the caller's uniform")
    again = r(*cmds, *(["frame"] * 500), "alerted", 3, 1, 0, *(["frame"] * 30), "alerted", 3, 1, 0, *(["frame"] * 160))
    check(len(lines(again, "SPAWNGUARD")) == 5, "a second sighting mid-wave calls nothing more")
    out = r("start", "prepare", 34, "begin", 34, "addguard", 3, 2, 50, 1, AK47, 0, 1, 1, 0, 0, 0,
            *(["frame"] * 500), "alerted", 3, 1, 0, *(["frame"] * 160))
    check(not lines(out, "SPAWNGUARD"), "Facility has no approach pads, so no wave")

    # 10. Guns that share a cartridge gun are carried together, each on its own.
    gid = elite_hk416(34)
    r = Run(exe, temp, "pair", save="mode 1\nloadout 16 1\ngun 16 1 0 0 0 0 0\n")   # M4A1 owned
    out = r("start", "prepare", 34, "stats", M16, 30, 4, 2.0,
            "spawning", gid, 0, 4, 50, M16, "begin", 34,
            "addguard", gid, 4, 50, 1, M16, 0, 1, 1, 0, 0, 0, "killed", gid, 1, M16, 0,
            "took", "weapon", M16, "inv",
            "hold", 0, AK47, 0, "draw", 0, AK47, "frame", "ammois", RIFLE, "reload", 0, 0, "loadedis", 0,
            "show", AK47, "hurt", 1, 9, 1.0, 1, 2, AK47, 0,
            "draw", 0, M16, "frame", "complete", "title")
    check(not errors(out), "pair scenario without errors: %s" % errors(out)[:3])
    inv = lines(out, "INV")[0] + " "
    check(" 13 " in inv and " 8 " in inv, "a looted HK416 joins the M4A1, on the KF7 (%s)" % inv.strip())
    check(any("HK416A5 carried on the ak47" in l for l in logs(out)), "the log says where it went")
    check(lines(out, "AMMO %d " % RIFLE)[0] == "AMMO 3 30", "the HK416 draws on the M4's STANAG magazines")
    check(lines(out, "LOADED")[0] == "LOADED 30", "the HK416 seats a STANAG magazine from the shared rig")
    check(lines(out, "STATS %d " % AK47)[0].split()[2] == "31", "the KF7 carries the STANAG's size while it's the HK416")
    check(lines(out, "DAMAGE")[0] == "DAMAGE 1.53",
          "the HK416 hits as on the AR33: 1.15 x 2.0 / 1.5 (%s)" % lines(out, "DAMAGE")[0])
    p = r.progress()
    check(re.search(r"^gun 17 1 ", p, re.M) is not None, "the HK416 on the back comes home and unlocks:\n" + p)
    check(re.search(r"^gun 16 1 \d+ \d+ \d+ \d+ 1$", p, re.M) is not None, "and the M4A1 in hand gets its clear")

    # two pistols on the PPK: the second takes the silenced PPK
    r = Run(exe, temp, "pistols", save=ALL.format(p=-1, s=22))   # Glock 17
    out = r("start", "prepare", 34, "begin", 34, "addguard", 6, 2, 50, 1, WPPK, 0, 1, 1, 0, 0, 0,
            "killed", 6, 1, WPPK, 0, "took", "weapon", WPPK, "inv")
    inv = lines(out, "INV")[0] + " "
    check(" 4 " in inv and " 5 " in inv, "a Glock 17 and a looted PPK are both carried (%s)" % inv.strip())


def main():
    problems = validate(PACK)
    check(not problems, "the pack follows docs/mod-system.md's format: %s" % problems)
    with tempfile.TemporaryDirectory() as t:
        temp = Path(t)
        exe = build(temp)
        scenarios(exe, temp)
    if FAILS:
        print("%d FAILED" % len(FAILS))
        sys.exit(1)
    print("PASS: Tactical mod pack")


if __name__ == "__main__":
    main()
