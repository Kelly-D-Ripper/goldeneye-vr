# Tactical parity: the native build against the mod pack

**The mod pack is not Tactical yet.** It's a partial port of the rules. The
native 0.18.4 build is still the full game, and its source is on branch
[`tactical/native-0.18.4`](https://github.com/Kelly-D-Ripper/goldeneye-vr/tree/tactical/native-0.18.4).

Rule for this port: **nothing in the native build is replaced by a
different system that plays or looks differently.** Where the API offers a
stand-in, like the game's own scope, it isn't used. The feature is listed as
missing until the real one can run.

Parity needs two things:

1. **New API calls (section B).** Each one is an adapter over a system the game
   already has, so they fit the spec's rule for API requests. Two are built
   (B2, B10), one is in progress (B1), and the rest are asked for.
2. **New game features (section C).** These are Tactical's renderer, guard AI,
   Workshop, effects and items. The spec says a new game system joins only as
   a game feature: useful beyond one mod, working on every platform, with
   tests. **Without them the mod pack can't reach parity, whatever API calls
   are added.** Each one needs MrSco's agreement first.

This audit was made on 2026-11-24 from the native source: every Tactical
file, every `--tactical-*` option and every function the native build added
to the game. The comparison point is `tactical/mod` with MrSco's `t3/mod-scope`
(`bc25f30`) merged in. The pack is tested in the game's script host. It hasn't
run in a full game build or on a headset yet.

## A. Ported (works through the API today)

| Native feature | Mod pack | Difference from native |
|---|---|---|
| 42 guns: magazine sizes, rates, fire modes, damage | Yes | Each gun is drawn as a cartridge gun until B1. The Glock 18C can't fire full-auto (B9). |
| Carrying guns that share a cartridge gun | Yes | A gun borrows a free cartridge gun, at most 15 in all. Native had 32 items of its own (C9). |
| Magazines and the chamber | Yes. The reserve is pinned to the next magazine, and `reload_attempt` declines a reload with none left. | No magazine models, no rounds shown in the magazine, no rack gesture (B1, C4) |
| Rig: 3 primary + 2 pistol pouches | Yes, as counts | Pouches aren't drawn. Native grabbed magazines from where they sat (B1, C4). |
| Sights | The game's own scopes are taken off every Tactical gun, as native does | **No Tactical optics at all yet**, including the SVD's PSO-1 and every variable-power scope (C2) |
| Fire modes | Yes | Switched by a chest grip, not native's selector |
| Progression: question, PPK start, bring it home, reserve, magazines by clears, stats | Yes | One file per install, not per save folder. No per-difficulty rewards, and "unlock all" can't open missions (B6). |
| Attachment unlocks (294 kits) | **No** | Nothing to unlock until attachments can be drawn (B1) and have stats (B9) |
| Guard weapon rosters | Yes | Guards' guns look like cartridge guns (B1) |
| Reinforcements | 7 maps with approach pads | Native used every map and picked safe entries out of sight (B5) |
| Loadout | 2 pages on the in-game screen | No Workshop (C5) |
| Co-op | Damage, hits and kills | No shared world loot (B3, B4) |
| An empty gun stays in hand | **No** | The game switches to the next weapon when both magazine and reserve are empty (B12) |

## B. API calls (adapters over existing systems)

| # | Need | Call | Existing system | Status | Brings back |
|---|---|---|---|---|---|
| B1 | Tactical's models | A gun model per hand: `mod.weapon.model(hand, item, file)`, over each hand's own gun model, the way GoldenEye X swaps it. Then attachments bound to the gun, a hand or a body slot, placed by the engine every frame. Also a guard attachment through the hat path (`PROPDEF_HAT`). | Per-hand gun models, body slots, guards' hats | **Gun model in progress; attachments planned.** We need the details below. | Guns that look like themselves; optics, muzzles, grips, stocks and magazines; guard vests and helmets |
| B2 | Turning the game's own scopes off | `mod.weapon.scope(hand, false)` | Per-hand VR scope (issue #40) | **Built (#312), used** | Native's behaviour: no game scope on a Tactical gun. The call's lens mode (`{at, radius, zoom}`) is **not** a substitute for Tactical's optics and isn't used (C2). |
| B3 | Loot on the floor | Spawn a weapon, ammo or armour pickup during a mission | The game's drop when a guard dies | Asked; in the spec as not built | Dropped magazines that stay, magazines and armour on corpses, stocked mission magazines |
| B4 | Which gun a pickup is | A pickup id in `item_pickup` and in the unbuilt `weapon_dropped`, matching `pickup_spawning`'s index | Prop records | Asked | Loot keeps its exact gun and build; shared loot in co-op |
| B5 | Guards placed and moved | Read the setup's pads (position, room); send a guard to a pad and face a pad | `g_CurrentSetup.pads`; the AI's go-to-pad commands | Asked | Reinforcement entries on every map; 0.18's extra sentries, pairs, patrols, jungle squads and perched marksmen |
| B6 | Mission facts | Difficulty, save folder, a day number; mark a mission complete in the mod campaign's save | The difficulty setting; the folder `file.c` loads; the save's mission flags | Asked | Per-difficulty rewards, progress per folder, daily challenges, "unlock all" opening missions |
| B7 | A message on screen | A short note | The note a stopped mod already shows | Asked | "Magazine dropped", "M4A1 unlocked", the fire mode |
| B8 | Weight slows you | Scale the player's move speed | The player's walk speed | Asked | Native's carried-mass handling |
| B9 | Handling, sound and noise per gun | More `setStats` fields: accuracy, recoil kick, fire sound (ROM id), noise, and automatic fire on any gun | `WeaponStats`, the per-gun recoil option (`VrPerWeaponRecoil`), the shot sound, guards hearing shots | Asked | Attachment stats, suppressors guards hear less, the Glock 18C's full-auto, per-gun sounds |
| B10 | Refusing a reload | `reload_attempt` | Native reload start and the belt's takes | **Built (#298), used** | Correct reloads for every reload path |
| B11 | Throws | Throw velocity and a primed state | `gevrMotionThrowTick()` | Asked | Native's guided throws |
| B12 | An empty gun stays in hand | Decline the out-of-ammo switch | `gevrAutoAdvanceHand()` | **MrSco offered it; we're asking** | Native's behaviour: you reload an empty gun, you don't lose it |

**What B1 needs to give Tactical its look.** This answers MrSco's question on #291.
- **A fixed offset from the gun's origin** is enough for anything rigid: the
  optic, the muzzle device, the foregrip, the stock, lasers, lights, and the
  magazine while it's seated. The script can attach and detach the magazine
  at `reload_start` and `reload_end`, so its swap needs nothing more.
- **Parts that move with the action need more than a fixed offset.** Examples
  are the AK's charging handle, the M4's bolt and charging handle, a slide
  that locks back on empty, and the RPG's rocket, which leaves when fired.
  These need either a binding to a node of the gun model that the game
  already animates, or a way for the script to change a part's offset or
  visibility at an event (a slide locked back after the last shot).
- **The muzzle point and grip places per mod model.** A PKM or SVD model is
  longer than the cartridge gun it replaces. With the ROM gun's muzzle point,
  flashes and shots would start inside the barrel.
- **Our models, as they are.** Tactical's models are in its own format,
  `GEVRWPN1`/`GEVRWPN2` files drawn by `gevr_tactical_mesh.c`. They carry
  full-colour textures (the SVD has 14, up to 512 by 512, 17 MB in all), tag
  the bolt and charging handle so they move, and come with glove variants. The
  API's only model path today replaces a cartridge model with another
  GoldenEye-format model: at most 4 MB, and it has to fit the replaced
  model's own header (its texture count and skeleton). Converting to that
  format would lose texture detail and the moving parts, and change how
  Tactical looks. **So the ask is for the game to load our files as they are**
  (C10), not for us to convert them.

## C. Game features (outside the API's current rules)

The spec lists new rendering, new menus, new AI behaviour and new items as
game features, not mod requests. These native systems are exactly that. Each
needs MrSco to agree to build it into the game, for every platform, with
tests. Otherwise it stays out of the mod for good.

| # | Native system | Native code | What it does | Why the API can't do it |
|---|---|---|---|---|
| C1 | Tactical mesh renderer | `gevr_tactical_mesh.c` (3,136 lines), `gevr_tactical_gear.c` | Assembles guns from parts by mount tree, with finishes and variants | B1 places whole models. Assembly by mount tree is a renderer. (Converting each assembled build to one model could stand in for it, at the cost of every combination being its own file.) |
| C2 | **Tactical's optics, unchanged** | `gevr_tactical_optic_render.inc.h`, `gevr_tactical_optics.c`, `gevrTacticalOpticInput` | Each optic's lens shape and reticle (first and second focal plane), variable zoom on the stick, eye-relief parallax, red dots and holographics, night and thermal views, zeroing, flip-up magnifiers | Tactical's lens renderer draws the world through the same per-hand pass the game's scope uses (`gfx_vr_scope_render()`), with its own lens on top. The ask is to bring **that renderer** into the game, driven by each optic's data from the pack, plus a hook for the zoom input. It's not to approximate it with `mod.weapon.scope`'s lens, which draws the game's red sight at one fixed zoom per call. |
| C3 | Guard combat AI | `gevr_tactical_guard_ai.inc.h`, `gevr_tactical_cover_ai.inc.h` | Roles (assault, support, cover, flank), bounding cover to cover, peeks, squads | The spec excludes new AI behaviour. B5 can place and move guards, not make them fight this way. |
| C4 | Physical magazine handling | `gevr_tactical_inventory.c`, `gevr_tactical_session.c`, the rig code in `bondview2.c` | Grabbing magazines from pouches, seating, racking by hand, a held magazine showing its rounds | The game's hand reload is a different system. MrSco confirmed a mod's model runs the game's own reload gestures. |
| C5 | Workshop and presets | `gevr_tactical_gear_menu.inc.h` (1,672 lines), the Loadout Editor | Building a gun from its mount tree; presets shared with the desktop editor | `mod.ui` allows 4 pages of 8 rows each. A Workshop is a new kind of menu. |
| C6 | Gore and blade cuts | Tactical's parts of `gevr_ragdoll.c`, `gevr_hollywood_fx.c`, `chrDarkenWound`, the co-op gore messages | Blade cuts, blood stains, wound darkening, the HollywoodFX and BloodyCombat effects | `mod.effect` has 7 smokes, and bullet impacts aren't built. This is new rendering. |
| C7 | Melee weapons | Katana, knife and Huntsman code in `bondview2.c` and `gevr_ragdoll.c` | Swords and knives swung by hand, scabbards on the body | New items, plus C6 |
| C8 | Gadgets and the watch | `gevrTacticalWatchFrame`, the decoder and detonator code | A wrist watch with gauges, the Facility decoder used by hand, a handheld detonator, mine counts | New hand-held items and new drawing |
| C10 | **Tactical's model files, as they are** | `gevr_tactical_mesh.c` (the loader and its display lists), the `GEVRWPN1`/`GEVRWPN2` files | Full-colour, high-detail guns, hands, gloves, vests, helmets and blades, with moving action parts | The API only takes GoldenEye-format models that fit a cartridge model's header (4 MB at most). A loader for a model format with its own header would serve any mod. MrSco noted GoldenEye's files have no header of their own. |
| C9 | Tactical's own items | 32 `ITEM_TACTICAL_*` ids in `bondconstants.h` | Each gun is its own item, with its own stats, carried and dropped like any other | Mods can't add items. Borrowing cartridge guns caps it at 15, and B1's per-hand models keep the ROM gun's stats and sounds (B9 can move some of them). |

Also not portable today:
- **Firing range:** a private practice stage (`gevr_tactical_range.c`).
- **Guard smoking:** guards smoking while idle (`gevr_guard_cigarettes.c`).
- **Sounds from the pack:** the spec has ROM sounds only in v1. MrSco has
  said a later version would build on the voice mixer.

*Corrected 2026-11-24: an earlier version also listed avatar IK, ejected
casings and recoil as missing. Upstream already has `gevr_avatar.c`, casings
and a per-gun recoil option.*

## What parity takes, in order

1. **B1 with the details above, B12, and C10.** C10 lets our model files
   load as they are. This restores how Tactical looks without remaking any
   model.
2. **B3 to B9 and B11:** loot, guard placement, mission facts, notes, speed and
   stats. These restore loot, encounters, rewards and per-gun handling.
3. **A decision from MrSco on C1 to C10.** C2 (Tactical's optics) and C10
   (our model files) come first, because without them the game would look
   and play differently. For each one, either it joins the
   game as a general feature that Tactical uses through the API, or it's out
   of scope for mods and Tactical keeps a native build for it.

Until step 3 is settled, plan on **two Tactical builds**: the native build is
the full game, and the mod pack is a partial port through the launcher.
