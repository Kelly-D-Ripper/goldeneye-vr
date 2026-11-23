#ifdef GEVR
#include <ultra64.h>
#include <errno.h>
#include <stdlib.h>
#include "bondconstants.h"
#include "chr.h"
#include "player.h"
#include "propobj.h"
#include "system.h"
#include "net_core.h"
#include "gevr_tactical_net.h"
#include "net_match.h"
#include "gevr_tactical_facility.h"
#include "gevr_tactical_heavy_guards.h"
#include "gevr_tactical_loadouts.h"
#include "gevr_tactical_mesh.h"
#include "gevr_tactical_range.h"

enum { MAX_GUARD_PRESETS = 1024, MAX_STAGE_ASSIGNMENTS = 1024 };
typedef struct GuardPreset {
    char id[33];
    GevrGuardLoadout outfit;
} GuardPreset;
typedef struct GuardAssignment {
    char id[33];
    int body, actor;
    const GevrGuardLoadout *outfit;
} GuardAssignment;

static GuardPreset s_guards[MAX_GUARD_PRESETS];
static GuardAssignment s_assignments[MAX_STAGE_ASSIGNMENTS];
static const GevrGuardLoadout *s_byBody[BODIES_MAX];
static int s_numGuards, s_numAssignments, s_hasMissionGear, s_stage = -1;
static bool s_trainGear, s_campaignRoster;
static GevrLoadoutGun s_missionGear[2];

#define LOADOUT_SIDE_PRIMARY 0
#define LOADOUT_SIDE_SECONDARY 1
static const struct LoadoutGunDefinition {
    const char *name;
    int item, model, side;
} s_guns[GEVR_GEAR_BUILD_COUNT] = {
#define LOADOUT_GUN(key, side, item, model, label) [key] = {label, ITEM_##item, PROP_##model, LOADOUT_SIDE_##side},
#include "gevr_tactical_loadout_guns.inc.h"
#undef LOADOUT_GUN
};

static const GevrLoadoutGun s_russianRifles[] = {
    {TACTICAL_GUN_AK74N, "-"},
    {TACTICAL_GUN_AK, "-"},
    {TACTICAL_GUN_AK74N, "Guard Wooden Grip"},
    {TACTICAL_GUN_AK, "Guard Short Wooden Grip"},
};
static const GevrLoadoutGun s_russianRareRifles[] = {
    {TACTICAL_GUN_AK, "Suppressed Wood"},
    {TACTICAL_GUN_AK74N, "Border Patrol"},
};
static const GevrLoadoutGun s_guardP90 = {TACTICAL_GUN_P90 + 1, "P90 Default"};
static const GevrLoadoutGun s_eliteMp9 = {TACTICAL_GUN_MP9 + 1, "Tactical"};
static const GevrLoadoutGun s_eliteMp7 = {TACTICAL_GUN_MP7A2 + 1, "Tactical Tan"};
static const GevrLoadoutGun s_marineWeapons[] = {
    {TACTICAL_GUN_MP5, "-"},
    {TACTICAL_GUN_MP5K, "-"},
    {TACTICAL_GUN_EVO3 + 1, "-"},
    {TACTICAL_GUN_P90 + 1, "P90 Default"},
    {TACTICAL_GUN_AK, "Vympel"},
    {TACTICAL_GUN_MP5K, "P1"},
    {TACTICAL_GUN_MP5, "MP5SD Holo"},
    {TACTICAL_GUN_MP5, "SAS Entry"},
    {TACTICAL_GUN_MP5, "Blackout"},
    {TACTICAL_GUN_SKORPION, "-"},
};
static const GevrLoadoutGun s_eliteWeapons[] = {
    {TACTICAL_GUN_ASVAL + 1, "OPK Elite"},
    {TACTICAL_GUN_M4A1 + 1, "Elite Short"},
    {TACTICAL_GUN_HK416 + 1, "Night Detail"},
    {TACTICAL_GUN_HK416 + 1, "Dark Shorty"},
    {TACTICAL_GUN_AK74N, "Guard Elite"},
};
static const GevrLoadoutGun s_jungleWeapons[] = {
    {TACTICAL_GUN_M4A1 + 1, "Pine Watch"},
    {TACTICAL_GUN_M4A1 + 1, "Pine Watch Suppressed"},
};

static int parseNumber(const char *text, int lo, int hi, int *out)
{
    char *end;
    long value = strtol(text, &end, 10);
    if (end == text || *end || value < lo || value > hi) return 0;
    *out = (int)value;
    return 1;
}

static int validId(const char *text)
{
    return strlen(text) == 32 && strspn(text, "0123456789abcdef") == 32;
}

static int parseGun(char **fields, int minimum, GevrLoadoutGun *gun)
{
    if (!parseNumber(fields[0], minimum, GEVR_GEAR_BUILD_COUNT - 1, &gun->key)
        || strlen(fields[1]) > GEVR_GEAR_PRESET_NAME || !fields[1][0]
        || strspn(fields[1], "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -_") != strlen(fields[1]))
        return 0;
    strcpy(gun->preset, fields[1]);
    return 1;
}

static int splitLine(char *line, char **fields, int limit)
{
    int count = 0;
    char *end = strpbrk(line, "\r\n");
    if (end) *end = '\0';
    while (line && count < limit) {
        fields[count++] = line;
        char *next = strchr(line, '\t');
        if (next) *next++ = '\0';
        line = next;
    }
    return line ? 0 : count;
}

static int readMissionGear(char **field, GevrLoadoutGun guns[2])
{
    return parseGun(&field[2], 0, &guns[0]) && parseGun(&field[4], 0, &guns[1])
        && s_guns[guns[0].key].name && s_guns[guns[0].key].side == LOADOUT_SIDE_PRIMARY
        && s_guns[guns[1].key].name && s_guns[guns[1].key].side == LOADOUT_SIDE_SECONDARY;
}

static void readGuard(char **field)
{
    if (s_numGuards == MAX_GUARD_PRESETS || !validId(field[1])) return;
    GuardPreset *guard = &s_guards[s_numGuards];
    GevrGuardLoadout *outfit = &guard->outfit;
    if (!parseNumber(field[2], 0, BODIES_MAX - 1, &outfit->body)
        || !parseNumber(field[4], GEVR_LOADOUT_DEFAULT_VEST, GEVR_VEST_COUNT - 1, &outfit->vest)
        || !parseNumber(field[5], GEVR_LOADOUT_DEFAULT_HELMET, GEVR_HELMET_COUNT - 1, &outfit->helmet)
        || !parseGun(&field[6], -1, &outfit->gun)) return;
    strcpy(guard->id, field[1]);
    ++s_numGuards;
}

static void readAssignment(char **field)
{
    if (s_numAssignments == MAX_STAGE_ASSIGNMENTS || !validId(field[4])) return;
    GuardAssignment *assignment = &s_assignments[s_numAssignments];
    if (!parseNumber(field[2], 0, BODIES_MAX - 1, &assignment->body)
        || !parseNumber(field[3], -1, 32767, &assignment->actor)) return;
    strcpy(assignment->id, field[4]);
    assignment->outfit = NULL;
    ++s_numAssignments;
}

/* Reading a briefing preview leaves the current stage's rules and instances alone. */
static int readLoadouts(int stage, int readGuards, GevrLoadoutGun mission[2])
{
    if (!sysArgCheck("--tactical-loadout-editor") || !gevrTacticalGearEnabled()
        || (getPlayerCount() > 1 && !netCoopSession())) return 0;
    /* Starting gear belongs to this player. Only the host assigns shared guards. */
    if (!gevrTacticalCampaignRules() || (netCoopSession() && !netIsHost())) readGuards = 0;
    FILE *file = fopen("tactical-loadouts.txt", "r");
    if (!file) return 0;
    char line[512];
    const int readStartingKit = gevrTacticalMissionAllowsStartingKit(stage);
    int hasGear = 0;
    if (fgets(line, sizeof(line), file) && !strcmp(line, "GEVRLOADOUTS1\n")) {
        while (fgets(line, sizeof(line), file)) {
            if (!strchr(line, '\n') && !feof(file)) {
                int c;
                do { c = fgetc(file); } while (c != '\n' && c != EOF);
                continue;
            }
            char *field[9];
            const int n = splitLine(line, field, 9);
            if (readGuards && n == 9 && !strcmp(field[0], "G")) {
                readGuard(field);
                continue;
            }
            int rowStage;
            if (n < 2 || !parseNumber(field[1], 0, 255, &rowStage) || rowStage != stage) continue;
            if (readStartingKit && n == 6 && !strcmp(field[0], "M")) {
                GevrLoadoutGun trial[2];
                if (readMissionGear(field, trial)) {
                    hasGear = 1;
                    memcpy(mission, trial, sizeof(trial));
                }
            }
            if (readGuards && n == 5 && !strcmp(field[0], "A")) readAssignment(field);
        }
    } else {
        sysLogPrintf(LOG_WARNING, "loadout editor: unrecognised tactical-loadouts.txt");
    }
    fclose(file);
    return hasGear;
}

void gevrCampaignLoadoutsReset(int stage)
{
    s_stage = stage;
    s_campaignRoster = gevrTacticalCampaignRules()
        && stage >= 0 && stage <= UINT8_MAX && netCoopMissionIndexOf(stage) >= 0
        && gevrTacticalGearEnabled() && !gevrTacticalRangeActive()
        && sysArgCheck("--tactical-campaign-gear") && sysArgCheck("--tactical-guards");
    s_trainGear = stage == LEVELID_TRAIN && gevrTacticalCampaignRules()
        && sysArgCheck("--tactical-campaign-gear") && sysArgCheck("--tactical-enemy-rigs");
    s_numGuards = s_numAssignments = 0;
    memset(s_byBody, 0, sizeof(s_byBody));
    gevrTacticalHelmetResetAttachments();
    gevrTacticalGearResetIssuedBuilds();
    gevrTacticalGearRefreshPresets();
    s_hasMissionGear = readLoadouts(stage, 1, s_missionGear);
    for (int i = 0; i < s_numAssignments; ++i) {
        GuardAssignment *a = &s_assignments[i];
        for (int j = 0; j < s_numGuards; ++j) {
            const GuardPreset *g = &s_guards[j];
            if (g->outfit.body != a->body || strcmp(g->id, a->id)) continue;
            a->outfit = &g->outfit;
            if (a->actor < 0) s_byBody[a->body] = a->outfit;
            break;
        }
    }
    if (s_numAssignments || s_hasMissionGear)
        sysLogPrintf(LOG_NOTE, "loadout editor: stage %d read %d guard assignments, player loadout %d",
            stage, s_numAssignments, s_hasMissionGear);
}

int gevrCampaignMissionGear(int stage, GevrLoadoutGun guns[2])
{
    return readLoadouts(stage, 0, guns);
}

const GevrGuardLoadout *gevrCampaignGuardLoadout(const ChrRecord *chr)
{
    if (!chr || chr->chrnum < 0 || chr->bodynum >= BODIES_MAX || !s_numAssignments) return NULL;
    for (int i = s_numAssignments - 1; i >= 0; --i) {
        const GuardAssignment *a = &s_assignments[i];
        if (a->actor == chr->chrnum && a->body == chr->bodynum && a->outfit) return a->outfit;
    }
    return s_byBody[chr->bodynum];
}

static bool trainArmouredMarine(const ChrRecord *chr)
{
    if (chr->tacticalReinforcement) return false;
    /* Ten of the 34 regular Marines placed in the Train setup. Spread them
     * through the cars, with lightly equipped reinforcements. */
    switch (chr->chrnum) {
    case 2: case 5: case 14: case 18: case 29:
    case 31: case 36: case 43: case 46: case 51:
        return true;
    default:
        return false;
    }
}

static bool trainFinalCarElite(const ChrRecord *chr)
{
    return chr->bodynum == BODY_Janus_Special_Forces && (chr->chrnum == 66 || chr->chrnum == 68)
        && !chr->tacticalReinforcement;
}

static bool trainMaskedElite(const ChrRecord *chr)
{
    return trainFinalCarElite(chr) && chr->chrnum == 66;
}

int gevrTrainGuardVest(const ChrRecord *chr)
{
    if (!s_trainGear || !chr) return GEVR_LOADOUT_DEFAULT_VEST;
    if (chr->bodynum == BODY_Janus_Marine)
        return trainArmouredMarine(chr) ? GEVR_VEST_GZHEL_GREY : GEVR_VEST_NONE;
    return trainMaskedElite(chr) ? GEVR_VEST_SLICK : GEVR_LOADOUT_DEFAULT_VEST;
}

int gevrCampaignGuardHelmet(const ChrRecord *chr)
{
    const GevrGuardEquipment *remote = gevrTacticalRemoteGuardGear(chr);
    if (remote) return remote->helmet;
    const GevrGuardLoadout *outfit = gevrCampaignGuardLoadout(chr);
    if (outfit && outfit->helmet != GEVR_LOADOUT_DEFAULT_HELMET) return outfit->helmet;
    if (s_trainGear && chr) {
        if (chr->bodynum == BODY_Janus_Marine && trainArmouredMarine(chr)) return GEVR_HELMET_FAST_SIDE;
        if (trainMaskedElite(chr)) return GEVR_HELMET_FAST_SIDE_TROOPER;
    }
    return GEVR_LOADOUT_DEFAULT_HELMET;
}

int gevrTrainGuardWeapon(const ChrRecord *chr)
{
    if (!s_trainGear || !chr || !sysArgCheck("--tactical-guards")) return ITEM_NOTHING;
    if (trainFinalCarElite(chr) && gevrTacticalArEnabled()) return ITEM_TACTICAL_M4A1;
    if (chr->bodynum != BODY_Janus_Marine) return ITEM_NOTHING;
    /* Two placed sentries keep AKs. The remaining Marines share the SMGs. */
    if (chr->chrnum == 18 && !chr->tacticalReinforcement) return ITEM_TACTICAL_AK74N;
    if (chr->chrnum == 47 && !chr->tacticalReinforcement) return ITEM_AK47;
    switch ((unsigned)chr->chrnum % 3) {
    case 1: return ITEM_MP5K;
    case 2: return gevrTacticalExpandedEnabled() ? ITEM_TACTICAL_EVO3 : ITEM_SPECTRE;
    default: return ITEM_SPECTRE;
    }
}

static int resolveGun(const GevrLoadoutGun *gun, int *item, int *model, int *build)
{
    if (gun->key < 0 || gun->key >= GEVR_GEAR_BUILD_COUNT) return 0;
    const struct LoadoutGunDefinition *definition = &s_guns[gun->key];
    if (!definition->name || gevrTacticalItem(definition->item) < 0 || !gevrTacticalGearBuildAvailable(gun->key))
        return 0;
    *item = definition->item;
    *model = definition->model;
    *build = gevrTacticalGearNamedBuild(gun->key, gun->preset);
    if (*build < 0 && strcmp(gun->preset, "-")) {
        sysLogPrintf(LOG_WARNING, "loadout editor: preset %s unavailable for %s", gun->preset, definition->name);
        return 0;
    }
    if (*build >= 0 && (*item == ITEM_WPPK || *item == ITEM_WPPKSIL)) {
        const int silenced = gevrTacticalGearAssemblySuppressed(gevrTacticalGearDeployedAssembly(*build));
        *item = silenced ? ITEM_WPPKSIL : ITEM_WPPK;
        *model = silenced ? PROP_CHRWPPKSIL : PROP_CHRWPPK;
    }
    return 1;
}

int gevrCampaignGuardWeapon(const ChrRecord *chr, int *item, int *model, int *build)
{
    const GevrGuardLoadout *outfit = gevrCampaignGuardLoadout(chr);
    return outfit && resolveGun(outfit->gun.key == s_guardP90.key ? &s_guardP90 : &outfit->gun,
        item, model, build);
}

int gevrCampaignRosterActive(void)
{
    return s_campaignRoster;
}

static unsigned campaignWeaponDraw(const ChrRecord *chr)
{
    /* Derive the roll from the mission and actor; object load order and reloads
     * of the same checkpoint should give the same equipment. */
    uint32_t draw = ((uint32_t)s_stage << 16) | (uint16_t)chr->chrnum;
    draw = (draw ^ (draw >> 16)) * UINT32_C(0x7feb352d);
    draw = (draw ^ (draw >> 15)) * UINT32_C(0x846ca68b);
    return draw ^ (draw >> 16);
}

static const GevrLoadoutGun *campaignRosterGun(const ChrRecord *chr, int original)
{
    enum { RARE_PERCENT = 8 };
    const unsigned draw = campaignWeaponDraw(chr);
    static const unsigned char eliteChoices[] = {0, 0, 1, 1, 2, 3, 4, 4};
    switch (chr->bodynum) {
    case BODY_Russian_Soldier: case BODY_Russian_Infantry:
    case BODY_Siberian_Guard_1_Mishkin: case BODY_Siberian_Guard_2: {
        if (s_stage == LEVELID_DAM || original != ITEM_AK47) return NULL;
        if (chr->chrnum == GEVR_FACILITY_CORRIDOR_CENTER
            && gevrTacticalFacilityArmor() && gevrTacticalExpandedEnabled()) return NULL;
        const unsigned rate = draw % 100;
        if (rate < RARE_PERCENT) return &s_russianRareRifles[rate % ARRAYCOUNT(s_russianRareRifles)];
        return &s_russianRifles[(rate - RARE_PERCENT) * ARRAYCOUNT(s_russianRifles) / (100 - RARE_PERCENT)];
    }
    case BODY_Janus_Marine: case BODY_Arctic_Commando:
        return &s_marineWeapons[draw % ARRAYCOUNT(s_marineWeapons)];
    case BODY_Janus_Special_Forces:
        if (gevrTacticalJanusGuard(chr) || (s_trainGear && trainMaskedElite(chr)))
            return &s_eliteWeapons[1];
        return &s_eliteWeapons[eliteChoices[draw % ARRAYCOUNT(eliteChoices)]];
    case BODY_Jungle_Commando:
        return &s_jungleWeapons[draw % ARRAYCOUNT(s_jungleWeapons)];
    default:
        return original == ITEM_FNP90 || original == ITEM_TACTICAL_P90 ? &s_guardP90 : NULL;
    }
}

int gevrCampaignRosterWeapon(const ChrRecord *chr, int original, GevrGuardWeapon *weapon)
{
    if (!s_campaignRoster || !chr || chr->chrnum < 0 || chr->headnum == HEAD_Male_Mishkin
        || (chr->chrflags & (CHRFLAG_COUNT_DEATH_AS_CIVILIAN | CHRFLAG_CAN_SHOOT_CHRS))
        || original == ITEM_GRENADELAUNCH || original == ITEM_ROCKETLAUNCH)
        return false;
    const GevrLoadoutGun *gun = campaignRosterGun(chr, original);
    *weapon = (GevrGuardWeapon){0};
    if (chr->bodynum == BODY_Janus_Special_Forces
        && !gevrTacticalJanusGuard(chr) && !(s_trainGear && trainMaskedElite(chr))
        && campaignWeaponDraw(chr) % 100 < 18
        && resolveGun(&s_eliteMp9, &weapon->item, &weapon->model, &weapon->build)
        && resolveGun(&s_eliteMp7, &weapon->offhandItem, &weapon->offhandModel, &weapon->offhandBuild)) {
        weapon->preset = s_eliteMp9.preset;
        weapon->offhandPreset = s_eliteMp7.preset;
        return true;
    }
    *weapon = (GevrGuardWeapon){0};
    if (!gun || !resolveGun(gun, &weapon->item, &weapon->model, &weapon->build)) return false;
    weapon->preset = gun->preset;
    if (gun->key == TACTICAL_GUN_SKORPION) {
        weapon->offhandItem = weapon->item;
        weapon->offhandModel = weapon->model;
        weapon->offhandBuild = weapon->build;
        weapon->offhandPreset = weapon->preset;
    }
    return true;
}

int gevrTrainGuardSingleWeapon(const ChrRecord *chr)
{
    if (!s_trainGear || !trainFinalCarElite(chr)) return 0;
    int item, model, build;
    return gevrTrainGuardWeapon(chr) == ITEM_TACTICAL_M4A1
        || gevrCampaignGuardWeapon(chr, &item, &model, &build);
}

int gevrCampaignGunIsFirearm(int item)
{
    return (item >= ITEM_WPPK && item <= ITEM_ROCKETLAUNCH && item != ITEM_WATCHLASER)
        || gevrTacticalItem(item) >= 0;
}

int gevrCampaignGuardWeaponFallback(const ChrRecord *chr, int original)
{
    if (!chr || !chr->prop || chr->prop->type != PROP_TYPE_CHR || !gevrTacticalCampaignRules()
        || gevrTacticalRangeActive() || !sysArgCheck("--tactical-guards")
        || !gevrCampaignGunIsFirearm(original)) return original;

    if (original != ITEM_GRENADELAUNCH && original != ITEM_ROCKETLAUNCH
        && chr->headnum != HEAD_Male_Mishkin
        && !(chr->chrflags & (CHRFLAG_COUNT_DEATH_AS_CIVILIAN | CHRFLAG_CAN_SHOOT_CHRS))) {
        const int train = gevrTrainGuardWeapon(chr);
        if (train != ITEM_NOTHING && gevrTacticalItem(train) >= 0) return train;
        if (gevrTacticalJanusGuard(chr) && gevrTacticalItem(ITEM_TACTICAL_M4A1) >= 0)
            return ITEM_TACTICAL_M4A1;
        if (gevrTacticalFacilityArmor() && chr->chrnum == GEVR_FACILITY_CORRIDOR_CENTER
            && gevrTacticalItem(ITEM_TACTICAL_PKM) >= 0) return ITEM_TACTICAL_PKM;
        if (original == ITEM_AK47 && chr->bodynum == BODY_Russian_Soldier && (chr->chrnum & 1)
            && gevrTacticalItem(ITEM_TACTICAL_AK74N) >= 0) return ITEM_TACTICAL_AK74N;
    }

    /* These items already use their Tactical meshes and handling. Their
     * original item numbers still serve the campaign and its AI scripts. */
    if (gevrTacticalItem(original) >= 0) return original;
    int replacement;
    switch (original) {
    case ITEM_SKORPION:      replacement = ITEM_TACTICAL_EVO3; break;
    case ITEM_UZI:           replacement = ITEM_TACTICAL_UZI; break;
    case ITEM_MP5KSIL:       replacement = ITEM_TACTICAL_ASVAL; break;
    case ITEM_FNP90:         replacement = ITEM_TACTICAL_P90; break;
    case ITEM_M16:
    case ITEM_LASER:         replacement = ITEM_TACTICAL_M4A1; break;
    case ITEM_SHOTGUN:
    case ITEM_AUTOSHOT:      replacement = ITEM_TACTICAL_SAIGA12K; break;
    case ITEM_RUGER:         replacement = ITEM_TACTICAL_DEAGLE; break;
    case ITEM_GRENADELAUNCH: replacement = ITEM_TACTICAL_M32A1; break;
    case ITEM_SILVERWPPK:
    case ITEM_GOLDWPPK:      replacement = ITEM_WPPK; break;
    default: return original;
    }
    return gevrTacticalItem(replacement) >= 0 ? replacement : original;
}

void gevrCampaignFinishGuard(ChrRecord *chr, int equipUnarmed)
{
    if (!chr->model || !chr->prop) return;
    const int helmet = gevrCampaignGuardHelmet(chr);
    if (helmet >= 0 && !chr->handle_positiondata_hat && gevrTacticalHelmetAvailable(helmet))
        hatCreateForChr(chr, PROP_HATHELMET, 0);
    if (equipUnarmed && !chr->weapons_held[GUNRIGHT] && !chr->weapons_held[GUNLEFT]
        && gevrCampaignGuardLoadout(chr)) {
        int item, model, build;
        if (gevrCampaignGuardWeapon(chr, &item, &model, &build))
            something_with_generating_object(chr, model, item, 0, NULL, NULL);
    }
}

int gevrCampaignTryDeployGear(int stage)
{
    if (!s_hasMissionGear || stage != s_stage) return 0;
    int items[2], models[2], builds[2];
    for (int i = 0; i < 2; ++i)
        if (!resolveGun(&s_missionGear[i], &items[i], &models[i], &builds[i])) return 0;
    gevrTacticalGearCommitIssued(stage, items[0], items[1], builds[0], builds[1]);
    return 1;
}
#endif
