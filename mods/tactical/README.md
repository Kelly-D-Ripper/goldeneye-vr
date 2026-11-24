# GoldenEye Tactical (mod pack)

GoldenEye Tactical's rules as a GoldenEye VR mod pack, written in Lua against
mod API v1 (`docs/mod-system.md`). It is a `campaign` mod on the retail
cartridge, with its own saves, and it supports co-op.

This is the port of the native Tactical builds (0.05 to 0.18.4) to the mod
system. Everything here goes through the API, so it runs on every platform
the game ships on. The native builds' extra models, attachments and guard AI
can't be ported until the API can draw them (see **Not in this version**).

## What it does

**Guns.** All 42 Tactical guns, each riding on the cartridge gun closest to
it. The M4A1, HK416, L85A2, AUG, FAMAS and KAC PDW all use the AR33, for
example. The table is `scripts/tactical/guns.lua`, generated from 0.18.4's
reviewed unlock tables and Workshop stats by `tools/generate_data.py`. Each gun
brings its magazine sizes, rate of fire, fire modes and damage.

The game's inventory holds only one of each cartridge gun, so Tactical guns
that share one are given separate cartridge guns. For example, with an M4A1 in
your hands you can loot an HK416 and carry it on your back:
- The HK416 borrows a free cartridge gun, here the KF7.
- The borrowed gun fires the same way and, where possible, takes the same
  ammo.
- Both guns share the rig's STANAG magazines.
- The HK416 keeps its own damage, rate of fire and magazine size.

Two pistols work the same way. A looted PPK next to a Glock 17 takes the
silenced PPK, for example. A borrowed gun looks like the cartridge gun it
borrows until the API can attach models.

**Magazines and the chamber.** The game's reload runs on a spare-round
reserve. The mod keeps that reserve equal to the next magazine in your rig, so
both the button reload and the VR belt reload seat real magazines:
- The rig has 3 primary pouches and 2 pistol pouches. A PKM box takes all of a
  primary's.
- A reload keeps the part-used magazine in the pouch the fresh one came from.
  An empty magazine is discarded.
- A round in the chamber stays with the gun (+1).
- Ammo boxes, looted guns and the mission's starting rounds become loose rounds.
  Loose rounds are packed into empty pouches.

**Fire modes.** Semi, burst and auto, per gun. To switch the gun in your gun
hand, grip with your off hand at your chest, or use the Loadout page. A
declined shot clicks dry.

**Damage.** Each gun's damage multiplier applies to your shots only. Guards
carrying the same cartridge gun keep its own damage.

**Progression** (the agreed 0.18.4 rules):
- **One-time question.** At the first mission you're asked once whether to
  unlock everything. It's never asked again.
- **Starting kit.** A progression save starts with the Walther PPK only.
- **Bring it home.** A gun in your hands or on your back when a mission
  completes unlocks. Dying or leaving brings nothing home.
- **Reserve.** Each first clear of a mission earns one reserve, and so does
  every third replay. Reserve buys a locked gun once the campaign has 2 clears
  (pistols), 4 (most guns) or 6 (PKM, M32A1, RPG).
- **Magazines by clears.** Clears with a gun you owned at the start of the
  mission unlock its magazines at that gun's counts: extended at 2, 60-round
  and larger at 5 or 8. The PKM keeps its 100-round box from the start and has
  no magazine unlocks.
- **Stats.** Kills, headshots, shots, hits and clears are kept per gun.

**Guards.** Setup guards draw guns from 0.18.4's rosters:
- Russian troops carry the AK family.
- Janus marines draw from the marine SMG and rifle mix.
- Janus special forces draw from the elite roster.
- Jungle commandos carry M4s.

The roll comes from the mission and the guard, so every headset and every
restart gives the same result. On Train, 0.18.4's ten vested marines take half
damage to the body.

**Reinforcements.** A guard who spots you calls a wave, using 0.17.1's sizes and
timings. Waves arrive at the approach pads of Dam, Runway, Surface 1 and 2,
Streets, Depot and Jungle. They run where the guards run: solo, or on the co-op
host.

**Pages.** The in-game screen's Mods tab gets two pages:
- **Loadout:** primary, pistol, magazines and fire mode. Save the loadout, or
  apply it in a mission's first 30 seconds.
- **Progress:** stats per gun, totals and reserve.

**Co-op.** Each player's guns, magazines and progression stay on their own
headset. Clients tell the host which Tactical gun each of their cartridge guns
is, so the host applies its damage. The host sends each player their own hits
and kills.

## Settings

Settings are in `settings.txt` in the mod's data folder, as `name=value`
lines. Each one defaults to on.

| Setting | What it does |
|---|---|
| `starting_kit` | Issue the loadout in place of the mission's guns |
| `carry_limit` | Carry 2 primaries and 2 pistols; picking up more drops the oldest that isn't in a hand |
| `guard_rosters` | Arm setup guards from the Tactical rosters |
| `reinforcements` | Waves on maps with approach pads |

## Not in this version

These parts need API calls that don't exist yet. They were requested on
[MrSco/goldeneye-vr#291](https://github.com/MrSco/goldeneye-vr/pull/291).
- **Models and attachments.** The Tactical guns look like the cartridge gun
  they ride on. Optics, muzzles, grips and stocks need a model attached to a
  gun, hand or body slot.
- **Magnified scopes.** These need the game's per-hand scope turned on for any
  gun.
- **Physical magazines.** Magazines aren't drawn on the rig or in the hand, and a
  dropped magazine is lost. A mod can't spawn a pickup while a mission is
  running.
- **Guard AI.** Squads, cover, pairs and perches aren't ported, because the API
  can't add new AI behaviour. Vests and helmets need attached models.
- **Missions.** "Unlock all" covers guns and magazines but not missions, which
  the game's own save controls. Per-difficulty rewards and daily challenges
  aren't possible either, because a script can't read the difficulty or the
  date.
- **Save folders.** Progress is one file per install. The 0.18.4 build kept
  one per save folder, but a script can't read which folder is in use.
- **Reinforcements on enclosed maps.** The API gives no pad positions to
  choose from, so maps without approach pads get no waves.

## Testing

```
python mods/tactical/tests/test_tactical.py
```

This builds the game's real script host (`gevr_modscript.c`, `gevr_modapi.c`,
Lua 5.4) around upstream's fake game (`port/tests/modapi_native.c`). It then
plays missions through the pack: loading, the question, the kit, magazines and
reloads, fire modes, rosters, damage, co-op, extraction, reserve, magazine
unlocks and reinforcements. It needs `gcc` or `cc` on `PATH`, or `CC` set.
llvm-mingw works on Windows.

## Building the zip

```
python mods/tactical/tools/validate_pack.py --zip tactical-0.19.0.zip
```

This checks the pack against the format, writes `files.sha256` and builds the
zip the launcher installs.
