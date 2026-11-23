/* Included by front.c so the dossier keeps its controller, screen pointer and cursor. */
#include "gevr_tactical_fit.h"

enum { GEAR_CAROUSEL = 1, GEAR_WORKBENCH };
enum { GEAR_COLUMNS = 5, GEAR_CARDS = 2 * GEAR_COLUMNS, GEAR_OPTIONS = 8, GEAR_LIBRARY_ROWS = 9 };
enum { GEAR_SAVE_PRESET = 1, GEAR_LOAD_PRESET };
enum {
    GEAR_BG = 0x4c4d36, GEAR_PAPER = 0xd7cca3, GEAR_PANEL = 0xe2d7af,
    GEAR_CARD = 0xc0b289, GEAR_WELL = 0xc7bd96, GEAR_LINE = 0x8b7c58,
    GEAR_TEXT = 0x2d2a1d, GEAR_MUTED = 0x60543a, GEAR_ACCENT = 0x842c23
};
typedef struct GearWeapon {
    int item, kind;
    const char *name;
} GearWeapon;
typedef struct GearRect { int x, y, w, h; } GearRect;

enum { GEAR_ZOOM_1X = 1, GEAR_ZOOM_FIXED = 2, GEAR_ZOOM_RANGE = 4 };
enum { GEAR_FITTINGS = 1, GEAR_OPTICS = 2, GEAR_SCOPE_MOUNTS = 4, GEAR_VISIBLE_MOUNTS = GEVR_GEAR_MOUNTS * 3 };
typedef struct GearSlotChoice { int mount, category; } GearSlotChoice;
typedef struct GearOption {
    int part, fits, conflict, colour;
    unsigned muzzle;
    unsigned zoom;
    f32 low, high;
} GearOption;

static struct {
    GearOption choices[GEVR_GEAR_PARTS + 1];
    int visible[GEVR_GEAR_PARTS + 1];
    int count, shown, hasZoom, hasColours;
    unsigned muzzleGroups;
    GearOption equipped;
} gearLibrary;

typedef struct GearCheck {
    GearRect box;
    const char *label;
    unsigned bit;
} GearCheck;

static const GearCheck gearIncompatible = {{250, 44, 150, 16}, "Show incompatible", 1};
static const GearCheck gearColours[] = {
    {{90, 65, 55, 16}, "Black", 1u << GEVR_GEAR_BLACK},
    {{150, 65, 50, 16}, "Tan", 1u << GEVR_GEAR_TAN},
    {{205, 65, 58, 16}, "Green", 1u << GEVR_GEAR_GREEN},
    {{270, 65, 67, 16}, "Other", 1u << GEVR_GEAR_OTHER},
};
static const GearCheck gearZooms[] = {
    {{90, 65, 43, 16}, "1x", GEAR_ZOOM_1X},
    {{140, 65, 90, 16}, "Fixed >1x", GEAR_ZOOM_FIXED},
    {{237, 65, 147, 16}, "Variable / flip", GEAR_ZOOM_RANGE},
};
static const GearCheck gearMuzzles[] = {
    {{38, 63, 117, 19}, "Suppressors", GEVR_MUZZLE_SUPPRESSOR},
    {{160, 63, 117, 19}, "Brakes / hiders", GEVR_MUZZLE_BRAKE},
    {{282, 63, 117, 19}, "Adapters", GEVR_MUZZLE_ADAPTER},
};


static const GearWeapon gearPrimary[] = {
    {ITEM_NOTHING, -1, "Empty"},
    {ITEM_TACTICAL_AK74N, TACTICAL_GUN_AK74N, "AK-74N"},
    {ITEM_AK47, TACTICAL_GUN_AK, "AKS-74UN"},
    {ITEM_SPECTRE, TACTICAL_GUN_MP5, "MP5"},
    {ITEM_MP5K, TACTICAL_GUN_MP5K, "MP5K"},
    {ITEM_SKORPION, TACTICAL_GUN_SKORPION, "Skorpion vz. 61"},
    {ITEM_SNIPERRIFLE, TACTICAL_GUN_SVD, "SVD Dragunov"},
    {ITEM_ROCKETLAUNCH, TACTICAL_GUN_RPG, "RPG-7V2"},
#define TACTICAL_PRIMARY(id, name) {ITEM_TACTICAL_##id, TACTICAL_GUN_##id, name},
#define TACTICAL_SECONDARY(id, name)
#define TACTICAL_EXPANDED(id, group, native, gun, prop, capacity, cadence, name) TACTICAL_##group(id, name)
#include "gevr_tactical_expanded.inc.h"
#undef TACTICAL_EXPANDED
#undef TACTICAL_SECONDARY
#undef TACTICAL_PRIMARY
};
static const GearWeapon gearSecondary[] = {
    {ITEM_NOTHING, -1, "Empty"},
    {ITEM_WPPKSIL, TACTICAL_GUN_USP, "Suppressed PPK"},
    {ITEM_TT33, TACTICAL_GUN_MAKAROV, "Makarov PM"},
    {ITEM_WPPK, TACTICAL_GUN_USP, "Walther PPK"},
#define TACTICAL_PRIMARY(id, name)
#define TACTICAL_SECONDARY(id, name) {ITEM_TACTICAL_##id, TACTICAL_GUN_##id, name},
#define TACTICAL_EXPANDED(id, group, native, gun, prop, capacity, cadence, name) TACTICAL_##group(id, name)
#include "gevr_tactical_expanded.inc.h"
#undef TACTICAL_EXPANDED
#undef TACTICAL_SECONDARY
#undef TACTICAL_PRIMARY
};
static const GearWeapon gearMelee[] = {
    {ITEM_NOTHING, -1, "Empty"},
    {ITEM_KNIFE, -1, "Huntsman"},
    {ITEM_TACTICAL_KATANA, -1, "Tsuki no Hana"},
};
static struct {
    int page, tab, browse[3], selected[3], last, direction;
    f32 slide, time;
    int weapon, socketPage, option, optionCategory, optionPage;
    unsigned showIncompatible, colours, zooms, muzzle;
    int workshop, preset, presetPage, rangePage, rangeTab, stats;
    char presetName[GEVR_GEAR_PRESET_NAME + 1];
    struct {
        char name[GEVR_GEAR_PRESET_NAME + 1];
        uint64_t revision;
    } loaded[GEVR_GEAR_BUILD_COUNT];
    const char *notice;
} gear = {.selected = {1, 1, 1}, .option = -1};

static const GearRect gearBack = {328, 15, 96, 25};
static const GearRect gearPrimaryTab = {16, 47, 105, 26}, gearSecondaryTab = {125, 47, 105, 26};
static const GearRect gearMeleeTab = {234, 47, 105, 26};
static const GearRect gearPrev = {16, 155, 30, 39}, gearNext = {394, 155, 30, 39};
static const GearRect gearPreview = {47, 105, 346, 128};
static const GearRect gearChoose = {139, 243, 162, 30}, gearDeploy = {310, 287, 114, 31};
static const GearRect gearPrevPage = {311, 47, 22, 24}, gearNextPage = {401, 47, 23, 24};
static const GearRect gearReset = {16, 288, 105, 29}, gearDone = {319, 288, 105, 29};
static const GearRect gearOptionBack = {38, 259, 90, 30};
static const GearRect gearOptionPrev = {262, 259, 30, 30}, gearOptionNext = {371, 259, 31, 30};
static const GearRect gearSavePreset = {16, 47, 87, 25}, gearLoadPreset = {107, 47, 87, 25};
static const GearRect gearRange = {198, 47, 106, 25};
static const GearRect gearStats = {133, 288, 78, 29};
static const GearRect gearBrowseStats = {343, 47, 81, 26};
static const GearRect gearPartPreview = {221, 89, 180, 157};
static const GearRect gearPresetClear = {38, 224, 88, 26};
static const GearRect gearPresetSpace = {134, 224, 174, 26};
static const GearRect gearPresetDelete = {316, 224, 86, 26};
static const GearRect gearPresetConfirm = {302, 259, 100, 30};
static const char gearPresetKeys[] = "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM_";

static int gearBuildKey(const GearWeapon *weapon)
{
    if (weapon->kind < 0) return -1;
    return weapon->item == ITEM_WPPKSIL ? GEVR_GEAR_PPK_SUPPRESSED : gevrTacticalGearKey(weapon->kind);
}

static int gearCount(int tab)
{
    if (tab == 2) return ARRAYCOUNT(gearMelee);
    if (!gevrTacticalExpandedEnabled()) return tab ? 4 : 8;
    return tab ? (int)ARRAYCOUNT(gearSecondary) : (int)ARRAYCOUNT(gearPrimary);
}

static const GearWeapon *gearWeapon(int tab, int index)
{
    if (tab == 2) return &gearMelee[index];
    return tab ? &gearSecondary[index] : &gearPrimary[index];
}

static int gearWeaponAvailable(const GearWeapon *weapon)
{
    if (weapon->item == ITEM_NOTHING) return TRUE;
    if (weapon->item == ITEM_KNIFE) return gevrTacticalKnifeEnabled();
    if (weapon->item == ITEM_TACTICAL_KATANA) return gevrTacticalKatanaEnabled();
    return gevrTacticalItem(weapon->item) >= 0;
}

static int gearHit(GearRect rect)
{
    return cursor_h_pos >= rect.x && cursor_h_pos < rect.x + rect.w
        && cursor_v_pos >= rect.y && cursor_v_pos < rect.y + rect.h;
}

static GearRect gearCard(int slot)
{
    return (GearRect){16 + (slot % GEAR_COLUMNS) * 83, slot < GEAR_COLUMNS ? 78 : 232, 76, 31};
}

static int gearArRifle(void)
{
    return gear.weapon == gevrTacticalGearKey(TACTICAL_GUN_M4A1)
        || gear.weapon == gevrTacticalGearKey(TACTICAL_GUN_HK416);
}

static int gearAkRifle(void)
{
    return gear.weapon == TACTICAL_GUN_AK || gear.weapon == TACTICAL_GUN_AK74N;
}

static int gearPartCategory(const GevrGearMount *mount, int id)
{
    const GevrGearPart *part = gevrTacticalGearPart(id);
    if (part && (part->art & GEVR_GEAR_ART_SCOPE_MOUNT)) return GEAR_SCOPE_MOUNTS;
    if ((part && (part->art & GEVR_GEAR_ART_OPTIC)) || !strncmp(mount->socket->key, "mod_scope", 9))
        return GEAR_OPTICS;
    return GEAR_FITTINGS;
}

static int gearOpticSeat(const GevrGearAssembly *assembly, const GevrGearMount *mount)
{
    const GevrGearPart *part = gevrTacticalGearPart(mount->part);
    if (!part || !(part->art & GEVR_GEAR_ART_SCOPE_MOUNT)) return -1;
    const size_t length = strlen(mount->path);
    for (int i = 1; i < assembly->count; ++i) {
        const GevrGearMount *child = &assembly->mounts[i];
        if (strncmp(child->path, mount->path, length) || child->path[length] != '/'
            || strncmp(child->socket->key, "mod_scope", 9)) continue;
        for (int j = 0; j < child->socket->count; ++j)
            if (gearPartCategory(child, child->socket->allowed[j]) == GEAR_OPTICS) return i;
    }
    return -1;
}

static int gearAddSlotChoices(GearSlotChoice *out, const GevrGearAssembly *assembly, int index,
    unsigned char shown[GEVR_GEAR_MOUNTS])
{
    const GevrGearMount *mount = &assembly->mounts[index];
    const GevrGearSocket *socket = mount->socket;
    if (socket->required == GEVR_GEAR_SOCKET_AUTO) return 0;
    if (!strcmp(socket->key, "mod_gas_block") && (!gearAkRifle() || strchr(mount->path, '/')))
        return 0;
    if (socket->required && socket->count <= 1 && strcmp(mount->path, "mod_gas_block"))
        return 0;

    const int opticSeat = gearOpticSeat(assembly, mount);
    int categories = opticSeat >= 0 ? GEAR_OPTICS : 0;
    for (int i = 0; i < socket->count; ++i) categories |= gearPartCategory(mount, socket->allowed[i]);
    int count = 0;
    for (int category = GEAR_FITTINGS; category <= GEAR_SCOPE_MOUNTS; category <<= 1) {
        if (!(categories & category) || (shown[index] & category)) continue;
        shown[index] |= category;
        const int seat = category == GEAR_OPTICS && opticSeat >= 0 ? opticSeat : index;
        if (seat != index && (shown[seat] & category)) continue;
        shown[seat] |= category;
        out[count++] = (GearSlotChoice){seat, category};
    }
    return count;
}

static int gearOrderMounts(const GevrGearAssembly *assembly, GearSlotChoice order[GEAR_VISIBLE_MOUNTS])
{
    static const char *const arPage[] = {
        "mod_reciever/mod_scope", "mod_reciever/mod_barrel/mod_muzzle", "mod_reciever/mod_handguard",
        "mod_stock/mod_stock_000", "mod_pistol_grip", "mod_magazine",
        "mod_reciever/mod_sight_rear", GEVR_GEAR_FINISH_SLOT
    };
    static const char *const firstPage[] = {
        GEVR_GEAR_VARIANT_SLOT, "mod_scope", "mod_reciever/mod_scope", "mod_mount_000",
        "mod_muzzle", "mod_barrel/mod_muzzle", "mod_reciever/mod_muzzle", "mod_magazine",
        "mod_reciever", "mod_handguard", "mod_stock", "mod_pistol_grip", "mod_sight_rear"
    };
    unsigned char shown[GEVR_GEAR_MOUNTS] = {0};
    const int ar = gearArRifle();
    int next = 0;
    const char *const *priority = ar ? arPage : firstPage;
    const int priorityCount = ar ? ARRAYCOUNT(arPage) : ARRAYCOUNT(firstPage);
    for (int card = 0; card < priorityCount; ++card) {
        const char *path = priority[card];
        if (gearAkRifle() && !strcmp(path, "mod_handguard")) path = "mod_gas_block";
        for (int i = 1; i < assembly->count; ++i) {
            if (strcmp(assembly->mounts[i].path, path)) continue;
            next += gearAddSlotChoices(&order[next], assembly, i, shown);
            break;
        }
    }
    for (int i = 1; i < assembly->count; ++i) {
        const char *path = assembly->mounts[i].path;
        if (ar && !strcmp(path, "mod_bolt")) continue;
        if (gearAkRifle() && !strcmp(path, "mod_gas_block/mod_handguard")) continue;
        next += gearAddSlotChoices(&order[next], assembly, i, shown);
    }
    return next;
}

static const char *gearSlotLabel(const GevrGearMount *mount, int category)
{
    if (category == GEAR_SCOPE_MOUNTS) return "Scope mounts";
    if (category == GEAR_OPTICS) return "Optics";
    if (!strcmp(mount->path, "mod_mount_000")) return "SIDE MOUNT";
    const char *slot = mount->socket->key;
    if (!strcmp(slot, "mod_handguard") || (gearAkRifle() && !strcmp(mount->path, "mod_gas_block")))
        return "Handguard";
    if (!strcmp(slot, "mod_magazine") && strcmp(mount->socket->label, "Cylinder")) return "Magazine";
    if (!strncmp(slot, "mod_muzzle", 10)) return "Muzzle";
    if (!strcmp(slot, "mod_pistol_grip")) return "Pistol grip";
    if (!strcmp(slot, "mod_foregrip")) return "Foregrip";
    if (!strcmp(slot, "mod_tactical")) return "Accessory";
    if (!strcmp(slot, "mod_tactical_000")) return "Rail 1";
    if (!strcmp(slot, "mod_tactical_001")) return "Rail 2";
    if (!strcmp(slot, "mod_tactical_002")) return "Rail 3";
    if (!strcmp(slot, "mod_tactical_003")) return "Rail 4";
    if (gearArRifle()) {
        if (!strcmp(slot, "mod_sight_rear")) return "Rear sight";
        if (!strcmp(slot, "mod_stock_000")) return "Stock";
        if (!strcmp(mount->path, "mod_stock")) return "Stock / tube";
    }
    return mount->socket->label;
}

static GearRect gearOptionRow(int row)
{
    return (GearRect){38, 76 + row * 22, 364, 20};
}

static GearRect gearLibraryRow(int row)
{
    return (GearRect){38, 89 + row * 18, 177, 17};
}

static int gearLibraryPages(void)
{
    return (gearLibrary.shown + GEAR_LIBRARY_ROWS - 1) / GEAR_LIBRARY_ROWS;
}

static const GearOption *gearLibraryOption(int row)
{
    const int index = gear.optionPage * GEAR_LIBRARY_ROWS + row;
    return index < gearLibrary.shown ? &gearLibrary.choices[gearLibrary.visible[index]] : NULL;
}

static void gearFilterLibrary(int revealEquipped)
{
    gearLibrary.shown = 0;
    gear.optionPage = 0;
    for (int i = 0; i < gearLibrary.count; ++i) {
        const GearOption *choice = &gearLibrary.choices[i];
        if (!choice->fits && !gear.showIncompatible) continue;
        if (choice->part >= 0) {
            if (gear.muzzle && choice->muzzle != gear.muzzle) continue;
            if (gearLibrary.hasColours && gear.colours && !(gear.colours & (1u << choice->colour))) continue;
            if (gearLibrary.hasZoom && gear.zooms && !(gear.zooms & choice->zoom)) continue;
        }
        if (revealEquipped && choice->part == gearLibrary.equipped.part)
            gear.optionPage = gearLibrary.shown / GEAR_LIBRARY_ROWS;
        gearLibrary.visible[gearLibrary.shown++] = i;
    }
}

static int gearFinishSlot(const GevrGearMount *mount, int category)
{
    if (category != GEAR_FITTINGS) return FALSE;
    const char *key = mount->socket->key;
    return !strncmp(key, "mod_stock", 9) || !strcmp(key, "mod_handguard")
        || !strcmp(key, "mod_foregrip") || !strncmp(key, "mod_pistol", 10)
        || !strcmp(key, GEVR_GEAR_FINISH_SLOT)
        || (gearAkRifle() && !strcmp(mount->path, "mod_gas_block"));
}

static void gearOpenLibrary(GearSlotChoice slot)
{
    const int index = slot.mount;
    gear.option = index;
    gear.optionCategory = slot.category;
    const GevrGearMount *mount = &gevrTacticalGearAssembly(gear.weapon)->mounts[index];
    int parts[GEVR_GEAR_PARTS + 1];
    const int installed = gevrTacticalGearMountPart(gear.weapon, index);
    const int ownsSlot = installed < 0 || gearPartCategory(mount, installed) == slot.category;
    const int total = gevrTacticalGearOptions(gear.weapon, index, parts);
    const int muzzle = !strncmp(mount->socket->key, "mod_muzzle", 10);
    const int finishes = gearFinishSlot(mount, slot.category);
    unsigned availableColours = 0;
    gearLibrary.count = gearLibrary.hasZoom = gearLibrary.hasColours = 0;
    gearLibrary.muzzleGroups = gear.muzzle = 0;
    gearLibrary.equipped = (GearOption){.part = ownsSlot ? installed : -1, .fits = TRUE, .conflict = -1};
    for (int i = 0; i < total; ++i) {
        const int part = parts[i];
        const GevrGearPart *info = gevrTacticalGearPart(part);
        if (part < 0 ? !ownsSlot : gearPartCategory(mount, part) != slot.category) continue;
        if (info && (info->art & GEVR_GEAR_ART_LEGACY_CHOICE) && part != installed) continue;
        GearOption *choice = &gearLibrary.choices[gearLibrary.count++];
        *choice = (GearOption){.part = part};
        choice->fits = gevrTacticalGearCanInstall(gear.weapon, index, part, &choice->conflict);
        if (info) {
            const unsigned colour = (info->art >> GEVR_GEAR_COLOUR_SHIFT) & GEVR_GEAR_COLOUR_MASK;
            choice->colour = colour < GEVR_GEAR_RED ? colour : GEVR_GEAR_OTHER;
            if (finishes) availableColours |= 1u << choice->colour;
            if (muzzle) {
                choice->muzzle = gevrTacticalMuzzleCategory(part);
                gearLibrary.muzzleGroups |= choice->muzzle;
            }
            if (slot.category == GEAR_OPTICS && gevrTacticalOpticMagnification(part, &choice->low, &choice->high)) {
                choice->zoom = choice->high - choice->low > .01f ? GEAR_ZOOM_RANGE :
                    (choice->high > 1.01f ? GEAR_ZOOM_FIXED : GEAR_ZOOM_1X);
                gearLibrary.hasZoom = 1;
            }
        }
        if (ownsSlot && part == installed) gearLibrary.equipped = *choice;
    }
    gearLibrary.hasColours = (availableColours & (availableColours - 1)) != 0;
    gear.muzzle = gearLibrary.equipped.muzzle;
    for (int i = 0; !gear.muzzle && i < ARRAYCOUNT(gearMuzzles); ++i)
        gear.muzzle = gearLibrary.muzzleGroups & gearMuzzles[i].bit;
    gearFilterLibrary(TRUE);
}

static int gearToggleChecks(const GearCheck *checks, int count, unsigned *mask)
{
    for (int i = 0; i < count; ++i) {
        if (!gearHit(checks[i].box)) continue;
        *mask ^= checks[i].bit;
        return TRUE;
    }
    return FALSE;
}

static int gearLibraryFilterInput(void)
{
    for (int i = 0; i < ARRAYCOUNT(gearMuzzles); ++i) {
        if ((gearLibrary.muzzleGroups & gearMuzzles[i].bit) && gearHit(gearMuzzles[i].box)) {
            gear.muzzle = gearMuzzles[i].bit;
            gearFilterLibrary(TRUE);
            return TRUE;
        }
    }
    if (gearToggleChecks(&gearIncompatible, 1, &gear.showIncompatible)
        || (gearLibrary.hasColours && gearToggleChecks(gearColours, ARRAYCOUNT(gearColours), &gear.colours))
        || (gearLibrary.hasZoom && gearToggleChecks(gearZooms, ARRAYCOUNT(gearZooms), &gear.zooms))) {
        gearFilterLibrary(FALSE);
        return TRUE;
    }
    return FALSE;
}

static void gearBrowse(int direction)
{
    int *index = &gear.browse[gear.tab];
    const int count = gearCount(gear.tab);
    gear.last = *index;
    for (int tries = 0; tries < count; ++tries) {
        *index = (*index + direction + count) % count;
        if (gearWeaponAvailable(gearWeapon(gear.tab, *index))) break;
    }
    gear.direction = direction;
    gear.slide = 18;
    sysLogPrintf(LOG_NOTE, "tactical gear: browsing %s", gearWeapon(gear.tab, *index)->name);
}

static void gearChooseWeapon(void)
{
    const int index = gear.browse[gear.tab];
    const GearWeapon *weapon = gearWeapon(gear.tab, index);
    if (!gearWeaponAvailable(weapon)) return;
    gear.selected[gear.tab] = index;
    const int build = gearBuildKey(weapon);
    const GevrGearAssembly *assembly = gevrTacticalGearAssembly(build);
    if (assembly && assembly->count > 1) {
        gear.page = GEAR_WORKBENCH;
        gear.weapon = build;
        gear.option = -1;
        gear.socketPage = 0;
    }
    sysLogPrintf(LOG_NOTE, "tactical gear: selected %s", weapon->name);
}

static void gearOpen(int workshop)
{
    gevrTacticalGearRefreshPresets();
    memset(gear.loaded, 0, sizeof(gear.loaded));
    GevrLoadoutGun guns[2];
    if (!workshop && !gevrCoopTacticalKitGuest() && gevrCampaignMissionGear(gevrBriefingLevel(), guns)) {
        for (int tab = 0; tab < 2; ++tab) {
            for (int i = 0; i < gearCount(tab); ++i) {
                if (gearBuildKey(gearWeapon(tab, i)) != guns[tab].key) continue;
                gevrTacticalGearReset(guns[tab].key);
                if (strcmp(guns[tab].preset, "-"))
                    gevrTacticalGearLoadNamedPreset(guns[tab].key, guns[tab].preset);
                gear.selected[tab] = i;
                break;
            }
        }
    }
    gear.page = GEAR_CAROUSEL;
    gear.workshop = workshop;
    gear.stats = FALSE;
    gear.showIncompatible = gear.colours = gear.zooms = 0;
    gear.preset = 0;
    gear.notice = NULL;
    gear.tab = 0;
    gear.browse[0] = gear.selected[0];
    gear.browse[1] = gear.selected[1];
    if (!gearWeaponAvailable(gearWeapon(2, gear.selected[2]))) gear.selected[2] = 0;
    gear.browse[2] = gear.selected[2];
    if (!gearWeaponAvailable(gearWeapon(0, gear.browse[0]))) {
        gearBrowse(1);
        gear.selected[0] = gear.browse[0];
    }
    gear.direction = 1;
    gear.last = -1;
    gear.slide = 18;
    gear.time = 0;
    cursor_h_pos = 220;
    cursor_v_pos = 258;
    sysLogPrintf(LOG_NOTE, workshop ? "workshop: opened from main menu" : "tactical gear: opened from briefing");
}

static void gearLeavePage(void)
{
    if (gear.stats) gear.stats = FALSE;
    else if (gear.preset) { gear.preset = 0; gear.notice = NULL; }
    else if (gear.option >= 0) gear.option = -1;
    else if (gear.page == GEAR_WORKBENCH) {
        gear.page = GEAR_CAROUSEL;
        cursor_h_pos = 220;
        cursor_v_pos = 258;
    } else {
        if (gevrCoopTacticalKitGuest()) return;
        gear.page = 0;
        viSetUseZBuf(FALSE);
        cursor_h_pos = NEXTTAB_CURSOR_HPOS;
        cursor_v_pos = STARTTAB_TEXT_HALF;
    }
}

static int gearSocketPages(int visible)
{
    const int pages = (visible + GEAR_CARDS - 1) / GEAR_CARDS;
    return pages > 0 ? pages : 1;
}

static void gearCommit(int stage)
{
    const GearWeapon *primary = gearWeapon(0, gear.selected[0]);
    const GearWeapon *secondary = gearWeapon(1, gear.selected[1]);
    gevrTacticalGearCommit(stage, primary->item, secondary->item, gearBuildKey(primary), gearBuildKey(secondary));
    gevrTacticalGearSetMelee(gearWeapon(2, gear.selected[2])->item);
}

static void gearMissionStart(void)
{
    if (gevrCoopTacticalKitGuest()) {
        gearCommit(LEVELID_TITLE);
        gevrCoopTacticalKitSetReady(TRUE);
        return;
    }
    int guests;
    const int ready = gevrCoopTacticalKitCount(&guests);
    if (ready != guests) return;
    gearCommit(gevrBriefingLevel());
    gear.page = 0;
    frontChangeMenu(MENU_RUN_STAGE, TRUE);
}

static void gearLaunchRange(void)
{
    gearCommit(LEVELID_RUNWAY);
    gevrTacticalRangeRequest();
    gear.rangePage = gear.page;
    gear.rangeTab = gear.tab;
    gear.page = 0;
    selected_stage = LEVELID_RUNWAY;
    selected_difficulty = DIFFICULTY_AGENT;
    gamemode = GAMEMODE_SOLO;
    frontChangeMenu(MENU_RUN_STAGE, TRUE);
}

static GearRect gearPresetKey(int index)
{
    return (GearRect){38 + (index % 10) * 37, 108 + (index / 10) * 28, 31, 24};
}

static void gearTrackPreset(const char *name)
{
    strcpy(gear.loaded[gear.weapon].name, name);
    gear.loaded[gear.weapon].revision = gevrTacticalGearPresetRevision(gear.weapon, name);
}

static int gearIsPresetUpdate(void)
{
    return gear.loaded[gear.weapon].revision
        && !strcmp(gear.presetName, gear.loaded[gear.weapon].name);
}

static void gearPresetInput(int ok, int step)
{
    if (ok && gearHit(gearOptionBack)) { gearLeavePage(); return; }
    if (gear.preset == GEAR_LOAD_PRESET) {
        const int count = gevrTacticalGearPresetCount(gear.weapon);
        const int pages = (count + GEAR_OPTIONS - 1) / GEAR_OPTIONS;
        if (ok && gearHit(gearOptionPrev)) step = -1;
        if (ok && gearHit(gearOptionNext)) step = 1;
        if (step && pages > 1) gear.presetPage = (gear.presetPage + step + pages) % pages;
        if (!ok || step) return;
        for (int row = 0; row < GEAR_OPTIONS; ++row) {
            const int index = gear.presetPage * GEAR_OPTIONS + row;
            if (index >= count || !gearHit(gearOptionRow(row))) continue;
            const char *name = gevrTacticalGearPresetName(gear.weapon, index);
            if (name && gevrTacticalGearLoadPreset(gear.weapon, index)) {
                gearTrackPreset(name);
                gearLeavePage();
                gear.socketPage = 0;
                gear.notice = "Preset loaded";
            }
            return;
        }
        return;
    }
    if (!ok) return;
    size_t length = strlen(gear.presetName);
    if (gearHit(gearPresetConfirm)) {
        while (length > 0 && gear.presetName[length - 1] == ' ') gear.presetName[--length] = '\0';
        if (!length) { gear.notice = "Enter a preset name"; return; }
        const int updating = gearIsPresetUpdate();
        const int result = updating
            ? gevrTacticalGearUpdatePreset(gear.weapon, gear.presetName, gear.loaded[gear.weapon].revision)
            : gevrTacticalGearSavePreset(gear.weapon, gear.presetName);
        if (result == GEVR_PRESET_SAVED) {
            gearTrackPreset(gear.presetName);
            gearLeavePage();
            gear.notice = updating ? "Preset updated" : "Preset saved";
        } else if (result == GEVR_PRESET_DUPLICATE) gear.notice = "Load that preset to edit it";
        else if (result == GEVR_PRESET_FULL) gear.notice = "Preset list is full";
        else if (result == GEVR_PRESET_CHANGED) gear.notice = "Preset changed. Reload or use a new name.";
        else gear.notice = "Could not save preset";
        return;
    }
    gear.notice = NULL;
    if (gearHit(gearPresetClear)) {
        gear.presetName[0] = '\0';
        return;
    }
    if (gearHit(gearPresetDelete)) {
        if (length) gear.presetName[length - 1] = '\0';
        return;
    }
    if (length >= GEVR_GEAR_PRESET_NAME) return;
    if (length && gearHit(gearPresetSpace)) {
        gear.presetName[length++] = ' ';
    } else {
        for (int i = 0; gearPresetKeys[i]; ++i) {
            if (!gearHit(gearPresetKey(i))) continue;
            gear.presetName[length++] = gearPresetKeys[i];
            break;
        }
    }
    gear.presetName[length] = '\0';
}

static void gearWorkbenchInput(int ok, int step)
{
    const GevrGearAssembly *assembly = gevrTacticalGearAssembly(gear.weapon);
    if (!assembly) return;
    GearSlotChoice order[GEAR_VISIBLE_MOUNTS];
    const int visible = gearOrderMounts(assembly, order);
    if (gear.option >= 0) {
        if (ok && gearHit(gearOptionBack)) { gearLeavePage(); return; }
        if (ok && gearLibraryFilterInput()) return;
        if (ok && gearHit(gearOptionPrev)) step = -1;
        if (ok && gearHit(gearOptionNext)) step = 1;
        const int pages = gearLibraryPages();
        if (step && pages > 1) gear.optionPage = (gear.optionPage + step + pages) % pages;
        if (!ok || step) return;
        for (int row = 0; row < GEAR_LIBRARY_ROWS; ++row) {
            const GearOption *choice = gearLibraryOption(row);
            if (!choice || !gearHit(gearLibraryRow(row))) continue;
            int conflict;
            if (choice->fits && gevrTacticalGearInstall(gear.weapon, gear.option, choice->part, &conflict)) {
                gearLeavePage();
                const int remaining = gearSocketPages(gearOrderMounts(assembly, order));
                if (gear.socketPage >= remaining) gear.socketPage = remaining - 1;
            }
            return;
        }
        return;
    }
    if (ok && (gearHit(gearBack) || gearHit(gearDone))) { gearLeavePage(); return; }
    if (ok && gearHit(gearStats)) { gear.stats = TRUE; return; }
    if (ok && gearHit(gearRange)) { gearLaunchRange(); return; }
    if (ok && (gearHit(gearSavePreset) || gearHit(gearLoadPreset))) {
        gevrTacticalGearRefreshPresets();
        gear.preset = gearHit(gearSavePreset) ? GEAR_SAVE_PRESET : GEAR_LOAD_PRESET;
        strcpy(gear.presetName, gear.loaded[gear.weapon].name);
        gear.presetPage = 0;
        gear.notice = NULL;
        return;
    }
    if (ok && gearHit(gearReset)) { gevrTacticalGearReset(gear.weapon); gear.socketPage = 0; return; }
    if (ok && gearHit(gearPrevPage)) step = -1;
    if (ok && gearHit(gearNextPage)) step = 1;
    const int pages = gearSocketPages(visible);
    if (pages > 1 && step) gear.socketPage = (gear.socketPage + step + pages) % pages;
    if (!ok || step) return;
    for (int slot = 0; slot < GEAR_CARDS; ++slot) {
        const int card = gear.socketPage * GEAR_CARDS + slot;
        if (card < visible && gearHit(gearCard(slot))) {
            gearOpenLibrary(order[card]);
            return;
        }
    }
}

static void gearInterface(void)
{
    viSetUseZBuf(TRUE);
    frontUpdateControlStickPosition();
    const int buttons = joyGetButtonsPressedThisFrame(PLAYER_1,
        START_BUTTON | A_BUTTON | B_BUTTON | Z_TRIG | L_JPAD | R_JPAD | L_CBUTTONS | R_CBUTTONS);
    const int ok = buttons & (A_BUTTON | Z_TRIG);
    if (gevrCoopTacticalKitGuest() && gevrCoopTacticalKitReady()) {
        if ((buttons & (B_BUTTON | START_BUTTON)) || (ok && gearHit(gearDeploy)))
            gevrCoopTacticalKitSetReady(FALSE);
        return;
    }
    int step = !!(buttons & (R_JPAD | R_CBUTTONS)) - !!(buttons & (L_JPAD | L_CBUTTONS));
    if (gear.slide > 0) {
        gear.slide -= g_GlobalTimerDelta;
        if (gear.slide < 0) gear.slide = 0;
    }
    gear.time = fmodf(gear.time + g_GlobalTimerDelta, 12000.f);
    if (buttons & B_BUTTON) gearLeavePage();
    else if (gear.stats) {
        if (ok && gearHit(gearBack)) gearLeavePage();
    }
    else if (gear.preset) gearPresetInput(ok, step);
    else if (gear.page == GEAR_WORKBENCH) gearWorkbenchInput(ok, step);
    else if (ok && gearHit(gearBrowseStats) && gearWeapon(gear.tab, gear.browse[gear.tab])->kind >= 0) gear.stats = TRUE;
    else if ((buttons & START_BUTTON) || (ok && gearHit(gearDeploy))) {
        if (gear.workshop) gearLaunchRange();
        else gearMissionStart();
    }
    else if (ok && gearHit(gearBack)) gearLeavePage();
    else if (ok && (gearHit(gearPrimaryTab) || gearHit(gearSecondaryTab) || gearHit(gearMeleeTab))) {
        const int tab = gearHit(gearMeleeTab) ? 2 : gearHit(gearSecondaryTab);
        if (tab != gear.tab) {
            gear.tab = tab;
            gear.direction = tab ? 1 : -1;
            gear.last = -1;
            gear.slide = 18;
        }
    } else if (ok && (gearHit(gearChoose) || gearHit(gearPreview))) gearChooseWeapon();
    else {
        if (ok && gearHit(gearPrev)) step = -1;
        if (ok && gearHit(gearNext)) step = 1;
        if (step) gearBrowse(step);
    }
    if (ok || (buttons & (B_BUTTON | START_BUTTON)) || step)
        sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, NULL);
}

static Gfx *gearFill(Gfx *g)
{
    gDPPipeSync(g++);
    gDPSetCycleType(g++, G_CYC_FILL);
    gDPSetRenderMode(g++, G_RM_NOOP, G_RM_NOOP2);
    return g;
}

static Gfx *gearRect(Gfx *g, GearRect rect, u32 color)
{
    const u32 packed = GPACK_RGBA5551(color >> 16, (color >> 8) & 255, color & 255, 1);
    gDPSetFillColor(g++, (packed << 16) | packed);
    gDPFillRectangle(g++, rect.x, rect.y, rect.x + rect.w - 1, rect.y + rect.h - 1);
    return g;
}

static Gfx *gearButton(Gfx *g, GearRect rect, int selected)
{
    g = gearRect(g, rect, selected || gearHit(rect) ? GEAR_ACCENT : GEAR_LINE);
    return gearRect(g, (GearRect){rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2},
        selected || gearHit(rect) ? GEAR_CARD : GEAR_PANEL);
}

static Gfx *gearText(Gfx *g, s32 x, s32 y, const char *label, u32 color, s32 center, s32 heading, s32 limit)
{
    struct fontchar *chars = heading ? ptrFontBankGothicChars : ptrFontZurichBoldChars;
    struct font *font = heading ? ptrFontBankGothic : ptrFontZurichBold;
    char text[100];
    snprintf(text, sizeof(text), "%s\n", label);
    s32 h, w;
    textMeasure(&h, &w, (u8 *)text, chars, font, 0);
    for (int n = (int)strlen(text) - 1; w > limit && n > 4; --n) {
        memcpy(text + n - 4, "...\n", 5);
        textMeasure(&h, &w, (u8 *)text, chars, font, 0);
    }
    if (center) x -= w / 2;
    return textRender(g, &x, &y, (u8 *)text, chars, font, color << 8 | 255, viGetX(), viGetY(), 0, 0);
}

static Gfx *gearLabel(Gfx *g, GearRect rect, const char *text)
{
    return gearText(g, rect.x + rect.w / 2, rect.y + (rect.h - 11) / 2,
        text, GEAR_TEXT, TRUE, FALSE, rect.w - 8);
}

static void gearCompose(const Mtxf *local, const Mtxf *parent, Mtxf *out)
{
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
        out->m[r][c] = 0;
        for (int k = 0; k < 4; ++k) out->m[r][c] += local->m[r][k] * parent->m[k][c];
    }
}

static Gfx *gearModel(Gfx *g, const Mtxf *model, Gfx *list)
{
    if (!list) return g;
    Mtx *fixed = dynAllocateMatrix();
    guMtxF2L((f32 (*)[4])model->m, fixed);
    gSPMatrix(g++, osVirtualToPhysical(fixed), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPDisplayList(g++, list);
    return g;
}

static Gfx *gearModelPass(Gfx *g)
{
    Mtx *projection = dynAllocateMatrix();
    guOrtho(projection, 0.f, 440.f, 0.f, 330.f, 1.f, 1100.f, 1.f);
    gDPPipeSync(g++);
    gDPParam(g++, 0x7E /* G_CLEAR_DEPTH_EXT */, 0);
    gSPMatrix(g++, osVirtualToPhysical(projection), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPPerspNormalize(g++, 0xffff);
    gDPSetCycleType(g++, G_CYC_1CYCLE);
    gDPSetRenderMode(g++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gDPSetTextureFilter(g++, G_TF_BILERP);
    gDPSetAlphaCompare(g++, G_AC_NONE);
    gSPSetGeometryMode(g++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    return g;
}

static Gfx *gearCarouselGun(Gfx *g, int index, f32 x)
{
    const GearWeapon *weapon = gearWeapon(gear.tab, index);
    const int suppressed = weapon->item == ITEM_WPPKSIL;
    f32 low[3], high[3];
    if (weapon->item == ITEM_NOTHING) return g;
    void *list;
    if (weapon->kind < 0) list = gevrTacticalBladePreview(weapon->item, low, high);
    else list = gevrTacticalMountedGunBounds(weapon->kind, suppressed, low, high)
        ? gevrTacticalMountedGunList(weapon->kind, suppressed, TRUE) : NULL;
    if (!list) return g;
    const f32 angle = .16f + .56f * sinf(gear.time * .018f);
    const f32 k = fminf(312.f / fmaxf(1, high[2] - low[2]), 119.f / fmaxf(1, high[1] - low[1]));
    const f32 s = sinf(angle) * k, c = cosf(angle) * k;
    Mtxf pose = {.m = {{-s,0,-c,0}, {0,k,0,0}, {c,0,-s,0}, {x,157,-550,1}}};
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) pose.m[3][j] -= (low[i] + high[i]) * .5f * pose.m[i][j];
    return gearModel(g, &pose, list);
}

static Gfx *gearCarouselDraw(Gfx *g)
{
    g = gearFill(g);
    g = gearRect(g, (GearRect){16, 79, 408, 154}, GEAR_PANEL);
    g = gearRect(g, (GearRect){48, 175, 344, 1}, 0xa3936f);
    g = gearRect(g, (GearRect){219, 102, 1, 130}, 0xa3936f);
    g = gearModelPass(g);
    gDPSetScissor(g++, G_SC_NON_INTERLACE, gearPreview.x, gearPreview.y,
        gearPreview.x + gearPreview.w, gearPreview.y + gearPreview.h);
    const f32 shift = gear.direction * 346.f * powf(gear.slide / 18.f, 3.f);
    if (gear.last >= 0 && gear.slide) g = gearCarouselGun(g, gear.last, 220.f + shift - gear.direction * 346.f);
    g = gearCarouselGun(g, gear.browse[gear.tab], 220.f + shift);
    gDPPipeSync(g++);
    gDPSetScissor(g++, G_SC_NON_INTERLACE, 0, 0, 440, 330);
    g = gearFill(g);
    g = gearButton(g, gearPrimaryTab, gear.tab == 0);
    g = gearButton(g, gearSecondaryTab, gear.tab == 1);
    g = gearButton(g, gearMeleeTab, gear.tab == 2);
    g = gearButton(g, gearPrev, FALSE);
    g = gearButton(g, gearNext, FALSE);
    g = gearButton(g, gearChoose, FALSE);
    g = gearButton(g, gearDeploy, TRUE);
    const GearWeapon *weapon = gearWeapon(gear.tab, gear.browse[gear.tab]);
    if (weapon->kind >= 0) g = gearButton(g, gearBrowseStats, FALSE);
    if (!gevrCoopTacticalKitGuest()) g = gearButton(g, gearBack, FALSE);
    g = gearRect(g, (GearRect){16, 281, 408, 1}, GEAR_LINE);
    g = microcode_constructor(g);
    g = gearText(g, 17, 21, gear.workshop ? "WORKSHOP" : "SELECT GEAR", GEAR_ACCENT, FALSE, TRUE, 210);
    g = gearLabel(g, gearPrimaryTab, "Primary");
    g = gearLabel(g, gearSecondaryTab, "Secondary");
    g = gearLabel(g, gearMeleeTab, "Melee");
    g = gearLabel(g, gearPrev, "<");
    g = gearLabel(g, gearNext, ">");
    g = gearText(g, 220, 90, weapon->name, GEAR_TEXT, TRUE, TRUE, 320);
    const GevrGearAssembly *assembly = gevrTacticalGearAssembly(gearBuildKey(weapon));
    g = gearLabel(g, gearChoose, assembly && assembly->count > 1 ? "Select and customise" :
        gear.browse[gear.tab] == gear.selected[gear.tab] ? "Selected" : "Select weapon");
    int guests;
    const int ready = gevrCoopTacticalKitCount(&guests);
    const int guest = gevrCoopTacticalKitGuest();
    g = gearLabel(g, gearDeploy, gear.workshop ? "Firing range" : guest
        ? (gevrCoopTacticalKitReady() ? "Edit loadout" : "Ready")
        : ready < guests ? "Waiting for team" : gevrCoopTacticalKitMenu() ? "Deploy" : "Start mission");
    if (weapon->kind >= 0) g = gearLabel(g, gearBrowseStats, "Stats");
    if (!guest) g = gearLabel(g, gearBack, gear.workshop ? "Main menu" : "Briefing");
    for (int slot = 0; slot < 3; ++slot)
        g = gearText(g, 17 + slot*98, 304, gearWeapon(slot, gear.selected[slot])->name,
            slot == gear.tab ? GEAR_ACCENT : GEAR_TEXT, FALSE, FALSE, 94);
    if (guest && gevrCoopTacticalKitReady())
        g = gearText(g, 16, 276, "Ready. Waiting for host.", GEAR_ACCENT, FALSE, FALSE, 230);
    else if (!gear.workshop && guests) {
        char status[40];
        snprintf(status, sizeof(status), "%d/%d teammates ready", ready, guests);
        g = gearText(g, 16, 276, status, GEAR_ACCENT, FALSE, FALSE, 230);
    }
    return g;
}

static void gearAssemblyPose(const GevrGearAssembly *assembly, Mtxf *pose)
{
    f32 low[2] = {10000.f, 10000.f}, high[2] = {-10000.f, -10000.f};
    for (int i = 0; i < assembly->count; ++i) {
        f32 bounds[3], top[3];
        const GevrGearMount *mount = &assembly->mounts[i];
        if (gevrTacticalGearIsRootChoice(mount->path) || !gevrTacticalGearPartList(mount->part, bounds, top)) continue;
        for (int corner = 0; corner < 8; ++corner) {
            for (int axis = 0; axis < 2; ++axis) {
                f32 extent = mount->frame[3][axis + 1];
                for (int j = 0; j < 3; ++j) {
                    extent += ((corner & (1 << j)) ? top[j] : bounds[j]) * mount->frame[j][axis + 1];
                }
                low[axis] = fminf(low[axis], extent);
                high[axis] = fmaxf(high[axis], extent);
            }
        }
    }
    const f32 scale = fminf(340.f / fmaxf(1, high[0] - low[0]), 105.f / fmaxf(1, high[1] - low[1]));
    const int side = gevrTacticalGearLateral(gearWeapon(gear.tab, gear.browse[gear.tab])->kind);
    *pose = (Mtxf){.m = {{0,0,-scale * side,0}, {scale,0,0,0}, {0,-scale,0,0},
        {220.f - (low[0] + high[0])*.5f*scale, 160.5f + (low[1] + high[1])*.5f*scale, -550.f, 1}}};
}

static Gfx *gearLine(Gfx *g, int x1, int y1, int x2, int y2, u32 color)
{
    Vtx *v = dynAllocateVertices(4);
    const int dx = x2 - x1, dy = y2 - y1;
    const int ox = abs(dy) > abs(dx) ? 1 : 0, oy = !ox;
    for (int i = 0; i < 4; ++i) {
        v[i] = (Vtx){0};
        v[i].v.ob[0] = (i & 2 ? x2 : x1) + (i & 1 ? ox : 0);
        v[i].v.ob[1] = 330 - (i & 2 ? y2 : y1) + (i & 1 ? oy : 0);
        v[i].v.ob[2] = -2;
        v[i].v.cn[0] = color >> 16; v[i].v.cn[1] = color >> 8; v[i].v.cn[2] = color; v[i].v.cn[3] = 255;
    }
    gSPVertex(g++, osVirtualToPhysical(v), 4, 0);
    gSP2Triangles(g++, 0, 1, 2, 0, 1, 3, 2, 0);
    return g;
}

static Gfx *gearWorkbenchDraw(Gfx *g)
{
    const GevrGearAssembly *assembly = gevrTacticalGearAssembly(gear.weapon);
    const int first = gear.socketPage * GEAR_CARDS;
    GearSlotChoice order[GEAR_VISIBLE_MOUNTS];
    const int visible = gearOrderMounts(assembly, order);
    const int pages = gearSocketPages(visible);
    g = gearFill(g);
    g = gearRect(g, (GearRect){16, 113, 408, 113}, GEAR_PANEL);
    g = gearRect(g, (GearRect){20, 170, 400, 1}, 0xa3936f);
    g = gearModelPass(g);
    Mtxf pose;
    gearAssemblyPose(assembly, &pose);
    for (int i = 0; i < assembly->count; ++i) {
        const GevrGearMount *mount = &assembly->mounts[i];
        if (gevrTacticalGearIsRootChoice(mount->path)) continue;
        Mtxf part, local;
        memcpy(local.m, mount->frame, sizeof(local.m));
        gearCompose(&local, &pose, &part);
        g = gearModel(g, &part, gevrTacticalGearPartList(mount->part, NULL, NULL));
    }
    Mtx *identity = dynAllocateMatrix();
    guMtxIdent(identity);
    gDPPipeSync(g++);
    gSPMatrix(g++, osVirtualToPhysical(identity), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH);
    gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetRenderMode(g++, G_RM_AA_OPA_SURF, G_RM_AA_OPA_SURF2);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    for (int i = 0; i < GEAR_CARDS; ++i) {
        if (first + i >= visible) break;
        const int index = order[first + i].mount;
        const GevrGearMount *mount = &assembly->mounts[index];
        const GearRect box = gearCard(i);
        if (!gearHit(box)) continue;
        int x = lroundf(pose.m[3][0] + mount->frame[3][1] * pose.m[1][0]);
        int y = 330 - lroundf(pose.m[3][1] + mount->frame[3][2] * pose.m[2][1]);
        const int tip = i < GEAR_COLUMNS ? box.y + box.h : box.y;
        g = gearLine(g, box.x + box.w / 2, tip, x, y, GEAR_ACCENT);
        g = gearLine(g, x - 2, y, x + 3, y, GEAR_ACCENT);
        g = gearLine(g, x, y - 2, x, y + 3, GEAR_ACCENT);
    }
    g = gearFill(g);
    for (int i = 0; i < GEAR_CARDS && first + i < visible; ++i)
        g = gearButton(g, gearCard(i), FALSE);
    g = gearButton(g, gearBack, FALSE);
    if (pages > 1) {
        g = gearButton(g, gearPrevPage, FALSE);
        g = gearButton(g, gearNextPage, FALSE);
    }
    g = gearButton(g, gearReset, FALSE);
    g = gearButton(g, gearDone, TRUE);
    g = gearButton(g, gearSavePreset, FALSE);
    g = gearButton(g, gearLoadPreset, FALSE);
    g = gearButton(g, gearRange, FALSE);
    g = gearButton(g, gearStats, FALSE);
    g = microcode_constructor(g);
    g = gearText(g, 16, 20, "WORKBENCH", GEAR_ACCENT, FALSE, TRUE, 230);
    g = gearText(g, 153, 24, gear.notice ? gear.notice :
        gearWeapon(gear.tab, gear.selected[gear.tab])->name, GEAR_MUTED, FALSE, FALSE, 163);
    g = gearLabel(g, gearStats, "Stats");
    g = gearLabel(g, gearSavePreset, "Save preset");
    g = gearLabel(g, gearLoadPreset, "Load preset");
    g = gearLabel(g, gearRange, "Take to range");
    if (pages > 1) {
        char status[30];
        snprintf(status, sizeof(status), "Slots %d/%d", gear.socketPage + 1, pages);
        g = gearText(g, 367, 55, status, GEAR_MUTED, TRUE, FALSE, 64);
        g = gearLabel(g, gearPrevPage, "<");
        g = gearLabel(g, gearNextPage, ">");
    }
    g = gearLabel(g, gearBack, "Select gear");
    g = gearLabel(g, gearReset, "Reset stock");
    g = gearLabel(g, gearDone, "Done");
    for (int i = 0; i < GEAR_CARDS; ++i) {
        if (first + i >= visible) break;
        const int index = order[first + i].mount;
        const GevrGearMount *mount = &assembly->mounts[index];
        const int category = order[first + i].category;
        const int id = gevrTacticalGearMountPart(gear.weapon, index);
        const GevrGearPart *part = gearPartCategory(mount, id) == category ? gevrTacticalGearPart(id) : NULL;
        const GearRect box = gearCard(i);
        const char *label = category == GEAR_SCOPE_MOUNTS ? "Mount" : gearSlotLabel(mount, category);
        g = gearText(g, box.x + 5, box.y + 4, label, GEAR_ACCENT, FALSE, FALSE, box.w - 10);
        g = gearText(g, box.x + 5, box.y + 17, part ? part->name : "Empty", GEAR_TEXT, FALSE, FALSE, box.w - 10);
    }
    return g;
}

static void gearStatsValues(const GevrWeaponStats *stats, int projectile, char values[8][40])
{
    snprintf(values[0], 40, "%s", gevrFireModeList(stats->modes));
    if (stats->rpm) snprintf(values[1], 40, "%d RPM", stats->rpm);
    else strcpy(values[1], "-");
    if (stats->modes & GEVR_FIRE_SEMI) snprintf(values[2], 40, "%d RPM", stats->semiRpm);
    else strcpy(values[2], "-");
    snprintf(values[3], 40, "%.2f kg", stats->mass);
    snprintf(values[4], 40, "%.0f / 100", stats->ergo);
    snprintf(values[5], 40, "%.0f", 100.f * stats->recoil);
    snprintf(values[6], 40, "%.2f deg", stats->spread);
    if (projectile) strcpy(values[7], "Projectile");
    else snprintf(values[7], 40, "%.2f", stats->damage);
}

static Gfx *gearStatsDraw(Gfx *g)
{
    static const char *const labels[] = {
        "Fire modes", "Cyclic rate", "Semi limit", "Fitted weight",
        "Ergonomics", "Recoil index", "Spread radius", "Base damage"
    };
    const GearWeapon *weapon = gearWeapon(gear.tab,
        gear.page == GEAR_WORKBENCH ? gear.selected[gear.tab] : gear.browse[gear.tab]);
    const int key = gearBuildKey(weapon);
    const GevrGearAssembly *assembly = gevrTacticalGearAssembly(key);
    GevrWeaponStats stats, stock;
    const int available = gevrWeaponStats(key, assembly, &stats)
        && gevrWeaponStats(key, gevrTacticalGearStockAssembly(key), &stock);
    g = gearFill(g);
    g = gearButton(g, gearBack, FALSE);
    g = gearRect(g, (GearRect){16, 48, 408, 237}, GEAR_PANEL);
    g = gearRect(g, (GearRect){22, 73, 396, 1}, GEAR_LINE);
    for (int row = 0; available && row < (int)ARRAYCOUNT(labels); ++row)
        if (!(row & 1)) g = gearRect(g, (GearRect){22, 78 + 25*row, 396, 23}, GEAR_WELL);
    g = microcode_constructor(g);
    g = gearText(g, 16, 20, "WEAPON STATS", GEAR_ACCENT, FALSE, TRUE, 290);
    g = gearLabel(g, gearBack, "Back");
    g = gearText(g, 29, 54, weapon->name, GEAR_ACCENT, FALSE, FALSE, 135);
    g = gearText(g, 171, 54, "Fitted", GEAR_TEXT, FALSE, FALSE, 140);
    g = gearText(g, 302, 54, "Stock", GEAR_MUTED, FALSE, FALSE, 110);
    if (!available) return gearText(g, 29, 89, "Weapon stats unavailable", GEAR_MUTED, FALSE, FALSE, 350);
    char fittedValues[8][40], stockValues[8][40];
    const int projectile = weapon->kind == TACTICAL_GUN_RPG || weapon->kind == TACTICAL_GUN_M32A1;
    gearStatsValues(&stats, projectile, fittedValues);
    gearStatsValues(&stock, projectile, stockValues);
    for (int row = 0; row < (int)ARRAYCOUNT(labels); ++row) {
        const int y = 83 + 25*row;
        const int changed = strcmp(fittedValues[row], stockValues[row]) != 0;
        const char *label = row == 7 && weapon->kind == TACTICAL_GUN_SAIGA12K ? "Damage per pellet" : labels[row];
        g = gearText(g, 29, y, label, GEAR_MUTED, FALSE, FALSE, 135);
        g = gearText(g, 171, y, fittedValues[row], changed ? GEAR_ACCENT : GEAR_TEXT, FALSE, FALSE, 123);
        g = gearText(g, 302, y, stockValues[row], GEAR_MUTED, FALSE, FALSE, 110);
    }
    return gearText(g, 22, 298, "Weight includes an empty magazine", GEAR_MUTED, FALSE, FALSE, 396);
}

typedef struct GearPreviewPiece {
    Gfx *list;
    Mtxf local;
} GearPreviewPiece;

static int gearPreviewPiece(GearPreviewPiece *piece, int part, const f32 frame[4][4],
    f32 low[3], f32 high[3])
{
    f32 bounds[3], top[3];
    Gfx *list = gevrTacticalGearPartList(part, bounds, top);
    if (!list) return 0;
    piece->list = list;
    memcpy(piece->local.m, frame, sizeof(piece->local.m));
    for (int axis = 0; axis < 3; ++axis) {
        f32 centre = frame[3][axis], radius = 0.f;
        for (int j = 0; j < 3; ++j) {
            centre += .5f * (bounds[j] + top[j]) * frame[j][axis];
            radius += .5f * (top[j] - bounds[j]) * fabsf(frame[j][axis]);
        }
        low[axis] = fminf(low[axis], centre - radius);
        high[axis] = fmaxf(high[axis], centre + radius);
    }
    return 1;
}

static int gearPreviewPieces(int part, GearPreviewPiece pieces[GEVR_GEAR_MOUNTS], f32 low[3], f32 high[3])
{
    static const f32 identity[4][4] = {{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}};
    const GevrGearPart *info = gevrTacticalGearPart(part);
    if (!info) return 0;

    const GevrGearAssembly *assembly = gevrTacticalGearAssembly(gear.weapon);
    const GevrGearMount *mount = &assembly->mounts[gear.option];
    int count = 0;
    if (!strcmp(mount->path, GEVR_GEAR_VARIANT_SLOT)) {
        GevrGearAssembly preview;
        if (gevrTacticalGearPreviewVariant(gear.weapon, gear.option, part, &preview)) {
            for (int i = 0; i < preview.count; ++i) {
                const GevrGearMount *fitting = &preview.mounts[i];
                if (gevrTacticalGearIsRootChoice(fitting->path)) continue;
                count += gearPreviewPiece(&pieces[count], fitting->part, fitting->frame, low, high);
            }
            return count;
        }
    }

    count += gearPreviewPiece(&pieces[count], part, identity, low, high);
    for (int i = 0; i < info->socketCount; ++i) {
        const GevrGearSocket *socket = &info->sockets[i];
        if (socket->required != GEVR_GEAR_SOCKET_AUTO || !socket->count) continue;
        int child = socket->allowed[0];
        if (part == mount->part) {
            char path[GEVR_GEAR_PATH];
            const int written = snprintf(path, sizeof(path), "%s/%s", mount->path, socket->key);
            if (written >= 0 && written < (int)sizeof(path)) {
                const GevrGearMount *fitted = gevrTacticalGearFindMount(assembly, path);
                if (fitted) child = fitted->part;
            }
        }
        count += gearPreviewPiece(&pieces[count], child, socket->frame, low, high);
    }
    return count;
}

static Gfx *gearPreviewPart(Gfx *g, int part, int *drawn)
{
    f32 low[3] = {INFINITY, INFINITY, INFINITY};
    f32 high[3] = {-INFINITY, -INFINITY, -INFINITY};
    GearPreviewPiece pieces[GEVR_GEAR_MOUNTS];
    const int count = gearPreviewPieces(part, pieces, low, high);
    *drawn = count > 0;
    if (!count) return g;
    const GearRect view = gearPartPreview;
    const f32 span = hypotf(high[0] - low[0], high[1] - low[1]);
    const f32 scale = fminf((view.w - 36.f) / fmaxf(1.f, span),
        (view.h - 42.f) / fmaxf(1.f, high[2] - low[2]));
    const f32 angle = .52f * sinf(gear.time * .023f);
    const f32 s = sinf(angle) * scale, c = cosf(angle) * scale;
    const int side = gevrTacticalGearLateral(gearWeapon(gear.tab, gear.browse[gear.tab])->kind);
    Mtxf pose = {.m = {{s * side,0,-c * side,0}, {c,0,s,0}, {0,-scale,0,0},
        {view.x + .5f*view.w, 330 - view.y - .5f*view.h, -550, 1}}};
    for (int axis = 0; axis < 3; ++axis)
        for (int basis = 0; basis < 3; ++basis)
            pose.m[3][axis] -= .5f * (low[basis] + high[basis]) * pose.m[basis][axis];
    g = gearModelPass(g);
    gDPSetScissor(g++, G_SC_NON_INTERLACE, view.x + 1, view.y + 1, view.x + view.w - 1, view.y + view.h - 1);
    for (int i = 0; i < count; ++i) {
        Mtxf matrix;
        gearCompose(&pieces[i].local, &pose, &matrix);
        g = gearModel(g, &matrix, pieces[i].list);
    }
    gDPPipeSync(g++);
    gDPSetScissor(g++, G_SC_NON_INTERLACE, 0, 0, 440, 330);
    return gearFill(g);
}

static Gfx *gearCheckBoxes(Gfx *g, const GearCheck *checks, int count, unsigned mask)
{
    for (int i = 0; i < count; ++i) {
        const GearRect box = checks[i].box;
        const int chosen = mask & checks[i].bit;
        const GearRect square = {box.x + 1, box.y + 3, 10, 10};
        g = gearRect(g, square, chosen || gearHit(box) ? GEAR_ACCENT : GEAR_LINE);
        g = gearRect(g, (GearRect){square.x + 1, square.y + 1, 8, 8}, GEAR_PAPER);
        if (chosen) {
            g = gearRect(g, (GearRect){square.x + 2, square.y + 5, 3, 2}, GEAR_ACCENT);
            g = gearRect(g, (GearRect){square.x + 4, square.y + 3, 2, 3}, GEAR_ACCENT);
            g = gearRect(g, (GearRect){square.x + 6, square.y + 2, 2, 3}, GEAR_ACCENT);
        }
    }
    return g;
}

static Gfx *gearCheckLabels(Gfx *g, const GearCheck *checks, int count, unsigned mask)
{
    for (int i = 0; i < count; ++i) {
        const GearRect box = checks[i].box;
        g = gearText(g, box.x + 15, box.y + 3, checks[i].label,
            mask & checks[i].bit ? GEAR_TEXT : GEAR_MUTED, FALSE, FALSE, box.w - 15);
    }
    return g;
}

static void gearZoomLabel(const GearOption *option, char label[24])
{
    if (option->zoom == GEAR_ZOOM_RANGE)
        snprintf(label, 24, "%.3g-%.3gx", option->low, option->high);
    else snprintf(label, 24, "%.3gx", option->high);
}

static Gfx *gearLibraryDraw(Gfx *g)
{
    const GevrGearAssembly *assembly = gevrTacticalGearAssembly(gear.weapon);
    const GevrGearMount *mount = &assembly->mounts[gear.option];
    const int pageCount = gearLibraryPages();
    g = gearFill(g);
    g = gearRect(g, (GearRect){28, 36, 384, 267}, GEAR_LINE);
    g = gearRect(g, (GearRect){29, 37, 382, 265}, GEAR_PANEL);
    g = gearRect(g, gearPartPreview, GEAR_LINE);
    g = gearRect(g, (GearRect){222, 90, 178, 155}, GEAR_WELL);
    g = gearCheckBoxes(g, &gearIncompatible, 1, gear.showIncompatible);
    for (int i = 0; i < ARRAYCOUNT(gearMuzzles); ++i)
        if (gearLibrary.muzzleGroups & gearMuzzles[i].bit)
            g = gearButton(g, gearMuzzles[i].box, gear.muzzle == gearMuzzles[i].bit);
    if (gearLibrary.hasColours) g = gearCheckBoxes(g, gearColours, ARRAYCOUNT(gearColours), gear.colours);
    if (gearLibrary.hasZoom) g = gearCheckBoxes(g, gearZooms, ARRAYCOUNT(gearZooms), gear.zooms);

    const GearOption *hovered = NULL;
    for (int row = 0; row < GEAR_LIBRARY_ROWS; ++row) {
        const GearOption *choice = gearLibraryOption(row);
        if (!choice) break;
        const GearRect box = gearLibraryRow(row);
        if (gearHit(box)) hovered = choice;
        g = gearButton(g, box, FALSE);
        if (choice->part == gearLibrary.equipped.part)
            g = gearRect(g, (GearRect){box.x + 2, box.y + 3, 2, box.h - 6}, GEAR_ACCENT);
    }
    g = gearButton(g, gearOptionBack, FALSE);
    if (pageCount > 1) {
        g = gearButton(g, gearOptionPrev, FALSE);
        g = gearButton(g, gearOptionNext, FALSE);
    }
    const GearOption *display = hovered ? hovered : &gearLibrary.equipped;
    if (!hovered && gear.muzzle && display->muzzle != gear.muzzle) {
        for (int row = 0; row < GEAR_LIBRARY_ROWS; ++row) {
            const GearOption *choice = gearLibraryOption(row);
            if (choice && choice->part >= 0) { display = choice; break; }
        }
    }
    int drawn;
    g = gearPreviewPart(g, display->part, &drawn);
    g = microcode_constructor(g);
    g = gearText(g, 39, 48, gearSlotLabel(mount, gear.optionCategory), GEAR_ACCENT, FALSE, TRUE, 201);
    g = gearCheckLabels(g, &gearIncompatible, 1, gear.showIncompatible);
    for (int i = 0; i < ARRAYCOUNT(gearMuzzles); ++i)
        if (gearLibrary.muzzleGroups & gearMuzzles[i].bit)
            g = gearLabel(g, gearMuzzles[i].box, gearMuzzles[i].label);
    if (gearLibrary.hasColours) {
        g = gearText(g, 39, 68, "Finish", GEAR_MUTED, FALSE, FALSE, 46);
        g = gearCheckLabels(g, gearColours, ARRAYCOUNT(gearColours), gear.colours);
    }
    if (gearLibrary.hasZoom) {
        g = gearText(g, 39, 68, "Zoom", GEAR_MUTED, FALSE, FALSE, 46);
        g = gearCheckLabels(g, gearZooms, ARRAYCOUNT(gearZooms), gear.zooms);
    }

    if (!gearLibrary.shown) g = gearText(g, 46, 121, "No matching parts", GEAR_MUTED, FALSE, FALSE, 150);
    for (int row = 0; row < GEAR_LIBRARY_ROWS; ++row) {
        const GearOption *choice = gearLibraryOption(row);
        if (!choice) break;
        const GearRect box = gearLibraryRow(row);
        const GevrGearPart *part = gevrTacticalGearPart(choice->part);
        const int capacity = part ? part->capacity : 0;
        char detail[24] = "";
        if (!choice->fits) snprintf(detail, sizeof(detail), "Conflict");
        else if (choice->zoom) gearZoomLabel(choice, detail);
        else if (capacity) snprintf(detail, sizeof(detail), "%d", capacity);
        g = gearText(g, box.x + 8, box.y + 3, part ? part->name : "Empty",
            choice->fits ? GEAR_TEXT : GEAR_MUTED, FALSE, FALSE, box.w - (*detail ? 64 : 16));
        if (*detail) g = gearText(g, box.x + box.w - 30, box.y + 3, detail,
            choice->fits ? GEAR_MUTED : GEAR_ACCENT, TRUE, FALSE, 56);
    }

    const GevrGearPart *preview = gevrTacticalGearPart(display->part);
    const int centre = gearPartPreview.x + gearPartPreview.w / 2;
    if (preview) {
        g = gearText(g, centre, 230, preview->name, GEAR_TEXT, TRUE, FALSE, gearPartPreview.w - 14);
        if (!drawn) g = gearText(g, centre, 166, "Preview unavailable", GEAR_MUTED, TRUE, FALSE, 164);
        char detail[24] = "";
        if (display->zoom) gearZoomLabel(display, detail);
        else if (preview->capacity) snprintf(detail, sizeof(detail), "%d rounds", preview->capacity);
        if (*detail) g = gearText(g, centre, 96, detail, GEAR_MUTED, TRUE, FALSE, 164);
    } else g = gearText(g, centre, 166, "Empty slot", GEAR_MUTED, TRUE, FALSE, 164);
    if (!display->fits) {
        const GevrGearPart *conflict = gevrTacticalGearPart(display->conflict);
        char status[100];
        if (conflict) snprintf(status, sizeof(status), "Conflicts with %s", conflict->name);
        else snprintf(status, sizeof(status), "Incompatible with current setup");
        g = gearText(g, centre, 249, status, GEAR_ACCENT, TRUE, FALSE, gearPartPreview.w);
    }
    g = gearLabel(g, gearOptionBack, "Back");
    if (pageCount <= 1) return g;
    char pages[24];
    snprintf(pages, sizeof(pages), "%d / %d", gear.optionPage + 1, pageCount);
    g = gearText(g, 331, 268, pages, GEAR_MUTED, TRUE, FALSE, 70);
    g = gearLabel(g, gearOptionPrev, "<");
    return gearLabel(g, gearOptionNext, ">");
}

static Gfx *gearPresetDraw(Gfx *g)
{
    const int saving = gear.preset == GEAR_SAVE_PRESET;
    const int count = saving ? 0 : gevrTacticalGearPresetCount(gear.weapon);
    g = gearFill(g);
    g = gearRect(g, (GearRect){28, 36, 384, 267}, GEAR_LINE);
    g = gearRect(g, (GearRect){29, 37, 382, 265}, GEAR_PANEL);
    g = gearButton(g, gearOptionBack, FALSE);
    if (saving) {
        g = gearRect(g, (GearRect){38, 75, 364, 24}, GEAR_WELL);
        for (int i = 0; gearPresetKeys[i]; ++i) g = gearButton(g, gearPresetKey(i), FALSE);
        g = gearButton(g, gearPresetClear, FALSE);
        g = gearButton(g, gearPresetSpace, FALSE);
        g = gearButton(g, gearPresetDelete, FALSE);
        g = gearButton(g, gearPresetConfirm, TRUE);
    } else {
        for (int row = 0; row < GEAR_OPTIONS && gear.presetPage * GEAR_OPTIONS + row < count; ++row)
            g = gearButton(g, gearOptionRow(row), FALSE);
        if (count > GEAR_OPTIONS) {
            g = gearButton(g, gearOptionPrev, FALSE);
            g = gearButton(g, gearOptionNext, FALSE);
        }
    }
    g = microcode_constructor(g);
    g = gearText(g, 39, 50, saving ? "NAME PRESET" : "LOAD PRESET", GEAR_ACCENT, FALSE, TRUE, 345);
    g = gearLabel(g, gearOptionBack, "Cancel");
    if (saving) {
        g = gearText(g, 47, 81, gear.presetName[0] ? gear.presetName : "Enter name", GEAR_TEXT, FALSE, FALSE, 346);
        for (int i = 0; gearPresetKeys[i]; ++i) {
            char label[] = {gearPresetKeys[i], '\0'};
            g = gearLabel(g, gearPresetKey(i), label);
        }
        g = gearLabel(g, gearPresetClear, "New name");
        g = gearLabel(g, gearPresetSpace, "Space");
        g = gearLabel(g, gearPresetDelete, "Delete");
        g = gearLabel(g, gearPresetConfirm, gearIsPresetUpdate() ? "Update" : "Save");
        return gear.notice ? gearText(g, 137, 268, gear.notice, GEAR_ACCENT, FALSE, FALSE, 160) : g;
    }
    if (!count) return gearText(g, 39, 90, "No compatible saved presets", GEAR_MUTED, FALSE, FALSE, 350);
    for (int row = 0; row < GEAR_OPTIONS && gear.presetPage * GEAR_OPTIONS + row < count; ++row) {
        const GearRect box = gearOptionRow(row);
        const char *name = gevrTacticalGearPresetName(gear.weapon, gear.presetPage * GEAR_OPTIONS + row);
        if (name) g = gearText(g, box.x + 10, box.y + 4, name, GEAR_TEXT, FALSE, FALSE, 340);
    }
    if (count > GEAR_OPTIONS) {
        char pages[24];
        snprintf(pages, sizeof(pages), "%d / %d", gear.presetPage + 1, (count + GEAR_OPTIONS - 1) / GEAR_OPTIONS);
        g = gearText(g, 331, 268, pages, GEAR_MUTED, TRUE, FALSE, 70);
        g = gearLabel(g, gearOptionPrev, "<");
        g = gearLabel(g, gearOptionNext, ">");
    }
    return g;
}

static Gfx *gearDraw(Gfx *g)
{
    g = gearFill(g);
    g = gearRect(g, (GearRect){0, 0, 440, 330}, GEAR_BG);
    g = gearRect(g, (GearRect){8, 5, 424, 320}, GEAR_LINE);
    g = gearRect(g, (GearRect){10, 7, 420, 316}, GEAR_PAPER);
    g = gearRect(g, (GearRect){16, 10, 408, 2}, GEAR_ACCENT);
    if (gear.stats) g = gearStatsDraw(g);
    else if (gear.page == GEAR_CAROUSEL) g = gearCarouselDraw(g);
    else if (gear.preset) g = gearPresetDraw(g);
    else if (gear.option >= 0) g = gearLibraryDraw(g);
    else g = gearWorkbenchDraw(g);
    return frontDrawCursor(g);
}

static int gearWorkshopOpen(void) { return gear.workshop && gear.page; }

static void gearRestoreRange(void)
{
    if (!gevrTacticalRangeReturning()) return;
    gevrTacticalRangeFinishReturn();
    const int page = gear.rangePage;
    gearOpen(TRUE);
    gear.page = page;
    gear.tab = gear.rangeTab;
    if (page == GEAR_WORKBENCH) cursor_v_pos = 64;
    sysLogPrintf(LOG_NOTE, "workshop: returned from firing range");
}
