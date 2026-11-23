/* Tactical campaign progression: per-folder unlocks and stats (gevr_tactical_progress.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "gevr_tactical_progress.h"
#include "gevr_tactical_gear.h"
#include "gevr_tactical_range.h"
#include <stdbool.h>
#include "system.h"

extern bool netIsActive(void);
extern int g_AppendCheatSinglePlayer;   /* s32, front.c */
extern int gevrCheatsBlockProgress(void);   /* s32, front.c */

enum { FOLDERS = 4, GUNS = 96, PARTS = GEVR_GEAR_PARTS, PART_ID = 25, REPLAYS_PER_RESERVE = 3 };
typedef struct { const char *name; unsigned reserveAt; } ProgressGunRule;
typedef struct { const char *name; uint64_t hosts; unsigned short start, count; unsigned char clears; } ProgressKitRule;
#include "gevr_tactical_progression_data.inc.h"
#define RULES(a) (sizeof(a) / sizeof(*(a)))

static struct {
    int folder, loaded, dirty, mode;
    int gunCount, unlockedGunCount, partCount;
    GevrProgressGun guns[GUNS];
    char unlockedGuns[GUNS][GEVR_PROGRESS_NAME];
    char parts[PARTS][PART_ID];
    unsigned missions[GEVR_PROGRESS_DIFFICULTIES];
    uint64_t clearedMissions, ownedAtLaunch, carried;
    unsigned reserve, replays;
    int completing, missionRecorded;
    unsigned char partBits[GEVR_GEAR_PARTS];
    int partBitsValid;
} s = {.folder = -1};
static int s_folderMode[FOLDERS], s_folderModeKnown[FOLDERS];
static int s_extractedGuns, s_extractedParts;
static short s_partKit[PARTS], s_members[RULES(kitPartIds)];
static unsigned char s_internal[PARTS];
static uint64_t s_ruleHash;

static int keyForName(const char *name)
{
    if (!name) return -1;
    if (!strcmp(name, "Suppressed PPK")) return TACTICAL_GUN_USP;
    for (unsigned i = 0; i < RULES(gunRules); ++i)
        if (gunRules[i].name && !strcmp(name, gunRules[i].name)) return (int)i;
    return -1;
}

static int normalizeKey(int key)
{
    if (key == GEVR_GEAR_PPK_SUPPRESSED) key = TACTICAL_GUN_USP;
    return key >= 0 && (unsigned)key < RULES(gunRules) && gunRules[key].name ? key : -1;
}

static int hasGun(const char *name)
{
    const int key = keyForName(name);
    if (key < 0) return 0;
    for (int i = 0; i < s.unlockedGunCount; ++i)
        if (keyForName(s.unlockedGuns[i]) == key) return 1;
    return 0;
}

static void loadRules(void)
{
    const uint64_t hash = gevrTacticalGearCatalogHash();
    if (!hash || hash == s_ruleHash) return;
    memset(s_partKit, 0, sizeof(s_partKit));
    memset(s_internal, 0, sizeof(s_internal));
    for (unsigned i = 0; i < RULES(s_members); ++i) s_members[i] = -1;
    for (int i = 0; i < PARTS; ++i) {
        const GevrGearPart *part = gevrTacticalGearPart(i);
        if (!part) break;
        for (unsigned j = 0; j < RULES(internalIds); ++j) {
            if (!strcmp(part->id, internalIds[j])) { s_internal[i] = 1; break; }
        }
        for (unsigned k = 0; k < RULES(kitRules); ++k) {
            const ProgressKitRule *kit = &kitRules[k];
            for (unsigned j = kit->start; j < (unsigned)kit->start + kit->count; ++j) {
                if (strcmp(part->id, kitPartIds[j])) continue;
                s_partKit[i] = (short)(k + 1);
                s_members[j] = (short)i;
                break;
            }
            if (s_partKit[i]) break;
        }
    }
    s_ruleHash = hash;
    s.partBitsValid = 0;
}

static void fileName(int folder, char *out, size_t size)
{
    snprintf(out, size, "tactical-progress-%d.txt", folder + 1);
}

static void stripLine(char *line)
{
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
}

static GevrProgressGun *gunStats(const char *name, int create)
{
    for (int i = 0; i < s.gunCount; i++)
        if (!strcmp(s.guns[i].name, name)) return &s.guns[i];
    if (!create || s.gunCount >= GUNS) return NULL;
    GevrProgressGun *gun = &s.guns[s.gunCount++];
    memset(gun, 0, sizeof(*gun));
    snprintf(gun->name, sizeof(gun->name), "%s", name);
    return gun;
}

static int unlockGun(int key);
static void grantStock(int key);

static int readFolderMode(int folder)
{
    char path[64], line[256];
    fileName(folder, path, sizeof(path));
    FILE *file = fopen(path, "r");
    if (!file) return GEVR_PROGRESS_UNSET;
    int mode = GEVR_PROGRESS_UNSET;
    if (fgets(line, sizeof(line), file) && !strncmp(line, "GEVRPROGRESS1", 13)) {
        while (fgets(line, sizeof(line), file)) {
            if (strncmp(line, "mode\t", 5)) continue;
            stripLine(line);
            mode = !strcmp(line + 5, "all") ? GEVR_PROGRESS_ALL
                : !strcmp(line + 5, "earn") ? GEVR_PROGRESS_EARN : GEVR_PROGRESS_UNSET;
        }
    }
    fclose(file);
    return mode;
}

static int readFolder(int folder)
{
    char path[64], line[256];
    memset(&s, 0, sizeof(s));
    s.folder = folder;
    s.loaded = 1;
    fileName(folder, path, sizeof(path));
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    if (!fgets(line, sizeof(line), file) || strncmp(line, "GEVRPROGRESS1", 13) != 0) {
        fclose(file);
        sysLogPrintf(LOG_WARNING, "tactical progress: %s isn't a progress file, starting over", path);
        return 0;
    }
    int reserveData = 0;
    while (fgets(line, sizeof(line), file)) {
        stripLine(line);
        char *field = strchr(line, '\t');
        if (!field) continue;
        *field++ = '\0';
        if (!strcmp(line, "mode")) {
            s.mode = !strcmp(field, "all") ? GEVR_PROGRESS_ALL : !strcmp(field, "earn") ? GEVR_PROGRESS_EARN : 0;
        } else if (!strcmp(line, "gun") && s.unlockedGunCount < GUNS) {
            const int key = keyForName(field);
            if (key >= 0 && !hasGun(field))
                snprintf(s.unlockedGuns[s.unlockedGunCount++], GEVR_PROGRESS_NAME, "%s", gunRules[key].name);
        } else if (!strcmp(line, "part") && s.partCount < PARTS && strlen(field) == PART_ID - 1) {
            memcpy(s.parts[s.partCount++], field, PART_ID);
        } else if (!strcmp(line, "missions")) {
            sscanf(field, "%u %u %u %u", &s.missions[0], &s.missions[1], &s.missions[2], &s.missions[3]);
        } else if (!strcmp(line, "reserve")) {
            unsigned long long stages;
            if (sscanf(field, "%llx %u %u", &stages, &s.reserve, &s.replays) == 3) {
                s.clearedMissions = (uint64_t)stages;
                reserveData = 1;
            }
        } else if (!strcmp(line, "stat")) {
            char *numbers = strchr(field, '\t');
            if (!numbers) continue;
            *numbers++ = '\0';
            GevrProgressGun *gun = gunStats(field, 1);
            if (gun) sscanf(numbers, "%u %u %u %u %u", &gun->kills, &gun->headshots, &gun->shots, &gun->hits, &gun->clears);
        }
    }
    fclose(file);
    /* Old files have only aggregate counts. Credit one Reserve per three prior clears,
     * without guessing which missions or weapons supplied them. */
    if (!reserveData) s.reserve = gevrProgressTotalClears() / REPLAYS_PER_RESERVE;
    return 1;
}

void gevrProgressSave(void)
{
    if (!s.loaded || s.folder < 0 || !s.dirty) return;
    char target[64], temp[96];
    fileName(s.folder, target, sizeof(target));
#ifdef _WIN32
    snprintf(temp, sizeof(temp), "%s.%lu.tmp", target, (unsigned long)GetCurrentProcessId());
#else
    snprintf(temp, sizeof(temp), "%s.%ld.tmp", target, (long)getpid());
#endif
    FILE *file = fopen(temp, "w");
    if (!file) {
        sysLogPrintf(LOG_WARNING, "tactical progress: couldn't write %s", temp);
        return;
    }
    fprintf(file, "GEVRPROGRESS1\n");
    if (s.mode) fprintf(file, "mode\t%s\n", s.mode == GEVR_PROGRESS_ALL ? "all" : "earn");
    for (int i = 0; i < s.unlockedGunCount; i++) fprintf(file, "gun\t%s\n", s.unlockedGuns[i]);
    for (int i = 0; i < s.partCount; i++) fprintf(file, "part\t%s\n", s.parts[i]);
    fprintf(file, "missions\t%u %u %u %u\n", s.missions[0], s.missions[1], s.missions[2], s.missions[3]);
    fprintf(file, "reserve\t%llx %u %u\n", (unsigned long long)s.clearedMissions, s.reserve, s.replays);
    for (int i = 0; i < s.gunCount; i++) {
        const GevrProgressGun *gun = &s.guns[i];
        fprintf(file, "stat\t%s\t%u %u %u %u %u\n", gun->name, gun->kills, gun->headshots, gun->shots, gun->hits, gun->clears);
    }
    const int failed = ferror(file);
    if (fclose(file) || failed) {
        remove(temp);
        return;
    }
#ifdef _WIN32
    if (!MoveFileExA(temp, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { remove(temp); return; }
#else
    if (rename(temp, target) != 0) { remove(temp); return; }
#endif
    s.dirty = 0;
}

void gevrProgressSelectFolder(int folder)
{
    static int registered;
    if (folder < 0 || folder >= FOLDERS) return;
    if (!registered) {
        atexit(gevrProgressSave);   /* stats from a session that quits mid-mission */
        registered = 1;
    }
    if (s.loaded && s.folder == folder) return;
    gevrProgressSave();
    readFolder(folder);
    s_folderMode[folder] = s.mode;
    s_folderModeKnown[folder] = 1;
    sysLogPrintf(LOG_NOTE, "tactical progress: folder %d %s, %d guns and %d parts unlocked", folder + 1,
        s.mode == GEVR_PROGRESS_ALL ? "has everything unlocked" : s.mode == GEVR_PROGRESS_EARN ? "is a progression save"
        : "hasn't been asked yet", s.unlockedGunCount, s.partCount);
    if (gevrProgressLocks()) {
        unlockGun(TACTICAL_GUN_USP);
        for (int i = 0; i < s.unlockedGunCount; ++i) grantStock(keyForName(s.unlockedGuns[i]));
    }
}

int gevrProgressFolder(void)
{
    return s.loaded ? s.folder : -1;
}

int gevrProgressMode(void)
{
    return s.loaded ? s.mode : GEVR_PROGRESS_UNSET;
}

int gevrProgressLocks(void)
{
    return s.loaded && s.mode == GEVR_PROGRESS_EARN && gevrTacticalGearEnabled() && !netIsActive();
}

int gevrProgressAsk(void)
{
    return s.loaded && s.mode == GEVR_PROGRESS_UNSET && gevrTacticalGearEnabled() && !netIsActive();
}

void gevrProgressAnswer(int unlockAll)
{
    if (!s.loaded || s.mode != GEVR_PROGRESS_UNSET) return;
    s.mode = unlockAll ? GEVR_PROGRESS_ALL : GEVR_PROGRESS_EARN;
    if (!unlockAll) unlockGun(TACTICAL_GUN_USP);
    s_folderMode[s.folder] = s.mode;
    s_folderModeKnown[s.folder] = 1;
    s.dirty = 1;
    gevrProgressSave();
    sysLogPrintf(LOG_NOTE, "tactical progress: folder %d chose %s", s.folder + 1,
        unlockAll ? "everything unlocked" : "progression");
}

int gevrProgressAllMissions(int folder)
{
    if (folder < 0 || folder >= FOLDERS || !gevrTacticalGearEnabled()) return 0;
    if (!s_folderModeKnown[folder]) {
        /* Campaign menu queries must not discard the active mission's ownership snapshot. */
        s_folderMode[folder] = s.loaded && s.folder == folder ? s.mode : readFolderMode(folder);
        s_folderModeKnown[folder] = 1;
    }
    return s_folderMode[folder] == GEVR_PROGRESS_ALL;
}

int gevrProgressGunUnlocked(const char *name)
{
    return !gevrProgressLocks() || hasGun(name);
}

int gevrProgressUnlockedGuns(void) { return s.loaded ? s.unlockedGunCount : 0; }
unsigned gevrProgressReserve(void) { return gevrProgressLocks() ? s.reserve : 0; }
unsigned gevrProgressTotalClears(void)
{
    unsigned total = 0;
    for (int i = 0; i < GEVR_PROGRESS_DIFFICULTIES; ++i) total += s.missions[i];
    return total;
}
unsigned gevrProgressReserveThreshold(int build)
{
    const int key = normalizeKey(build);
    return key >= 0 ? gunRules[key].reserveAt : 0;
}
unsigned gevrProgressGunClears(int build)
{
    const int key = normalizeKey(build);
    const GevrProgressGun *g = key >= 0 ? gunStats(gunRules[key].name, 0) : NULL;
    return g ? g->clears : 0;
}

static void refreshPartBits(void)
{
    if (s.partBitsValid) return;
    if (!gevrTacticalGearPart(0)) return;
    memset(s.partBits, 0, sizeof(s.partBits));
    for (int part = 0; part < PARTS; part++) {
        const GevrGearPart *info = gevrTacticalGearPart(part);
        if (!info) break;
        for (int i = 0; i < s.partCount; i++) {
            if (!strcmp(s.parts[i], info->id)) { s.partBits[part] = 1; break; }
        }
    }
    s.partBitsValid = 1;
}

static int grantPart(int part)
{
    const GevrGearPart *info = gevrTacticalGearPart(part);
    if (!info || info->capacity >= 60 || s.partCount >= PARTS) return 0;
    refreshPartBits();
    if (s.partBits[part]) return 0;
    memcpy(s.parts[s.partCount++], info->id, PART_ID);
    s.partBits[part] = 1;
    s.dirty = 1;
    return 1;
}

static void grantStock(int key)
{
    const GevrGearAssembly *stock = gevrTacticalGearStockAssembly(key);
    loadRules();
    for (int i = 1; stock && i < stock->count; ++i) {
        const int part = stock->mounts[i].part;
        if (part >= 0 && !s_internal[part] && stock->mounts[i].socket->required != GEVR_GEAR_SOCKET_AUTO)
            grantPart(part);
    }
}

static int unlockGun(int key)
{
    if (key < 0 || hasGun(gunRules[key].name) || s.unlockedGunCount >= GUNS) return 0;
    snprintf(s.unlockedGuns[s.unlockedGunCount++], GEVR_PROGRESS_NAME, "%s", gunRules[key].name);
    grantStock(key);
    s.dirty = 1;
    return 1;
}

int gevrProgressRequisition(int build)
{
    const int key = normalizeKey(build);
    if (!gevrProgressLocks() || key < 0 || !gunRules[key].reserveAt || !s.reserve
        || gevrProgressTotalClears() < gunRules[key].reserveAt || !unlockGun(key)) return 0;
    --s.reserve;
    s.dirty = 1;
    gevrProgressSave();
    return 1;
}

static int isPkmBox(int key, const GevrGearPart *part)
{
    return key == gevrTacticalGearKey(TACTICAL_GUN_PKM) && !strcmp(part->id, PROGRESS_PKM_BOX);
}

static unsigned masteryAt(const GevrGearPart *part) { return part->capacity <= 75 ? 5 : 8; }

int gevrProgressPartAllowed(int build, int part)
{
    if (!gevrProgressLocks() || part < 0) return 1;
    const int key = normalizeKey(build);
    const GevrGearPart *info = gevrTacticalGearPart(part);
    if (!info || key < 0) return 0;
    /* Capacity precedes old ownership and stock. The PKM box is factory only. */
    if (key == gevrTacticalGearKey(TACTICAL_GUN_PKM) && info->capacity > 0) return isPkmBox(key, info);
    if (info->capacity >= 60) return gevrProgressGunClears(key) >= masteryAt(info);
    loadRules();
    refreshPartBits();
    if (s.partBits[part] || s_internal[part]) return 1;
    const GevrGearAssembly *stock = gevrTacticalGearStockAssembly(key);
    for (int i = 0; stock && i < stock->count; ++i)
        if (stock->mounts[i].part == part) return 1;
    return 0;
}

int gevrProgressMasteryAllowed(int build, const GevrGearMount *mount)
{
    const GevrGearPart *part = gevrTacticalGearPart(mount->part);
    if (!part) return 1;
    if (part->capacity >= 60 || (normalizeKey(build) == gevrTacticalGearKey(TACTICAL_GUN_PKM) && part->capacity > 0))
        return gevrProgressPartAllowed(build, mount->part);
    return 1;
}

int gevrProgressMountAllowed(int build, const GevrGearMount *mount)
{
    return gevrProgressMasteryAllowed(build, mount) && (mount->socket->required == GEVR_GEAR_SOCKET_AUTO
        || gevrProgressPartAllowed(build, mount->part));
}

void gevrProgressPartRequirement(int build, int part, char *text, unsigned size)
{
    const GevrGearPart *info = gevrTacticalGearPart(part);
    if (info && info->capacity > 0 && normalizeKey(build) == gevrTacticalGearKey(TACTICAL_GUN_PKM)) {
        snprintf(text, size, "PKM uses its factory 100-round box");
        return;
    }
    if (info && info->capacity >= 60) {
        snprintf(text, size, "Mastery: %u / %u clears with this gun", gevrProgressGunClears(build), masteryAt(info));
        return;
    }
    loadRules();
    const int k = part >= 0 && part < PARTS ? s_partKit[part] - 1 : -1;
    const int key = normalizeKey(build);
    if (k >= 0 && key >= 0 && (kitRules[k].hosts & (UINT64_C(1) << key)))
        snprintf(text, size, "Extract fitted or clear %u with this gun", (unsigned)kitRules[k].clears);
    else if (k >= 0) snprintf(text, size, "Kit: %s", kitRules[k].name);
    else snprintf(text, size, "Comes with its factory build or assembly");
}

static void awardKit(unsigned k)
{
    const ProgressKitRule *kit = &kitRules[k];
    for (unsigned i = kit->start; i < (unsigned)kit->start + kit->count; ++i) grantPart(s_members[i]);
}

static void awardFitted(int key, int part)
{
    const GevrGearPart *info = gevrTacticalGearPart(part);
    if (!info || info->capacity >= 60 || (key == gevrTacticalGearKey(TACTICAL_GUN_PKM) && info->capacity > 0)) return;
    const GevrGearAssembly *stock = gevrTacticalGearStockAssembly(key);
    for (int i = 1; stock && i < stock->count; ++i)
        if (stock->mounts[i].part == part) return;
    const int k = s_partKit[part] - 1;
    if (k >= 0 && (kitRules[k].hosts & (UINT64_C(1) << key))) awardKit((unsigned)k);
}

void gevrProgressBeginMission(void)
{
    if (!s.loaded && gevrTacticalGearEnabled() && !netIsActive()) gevrProgressSelectFolder(0);
    s.ownedAtLaunch = s.carried = 0;
    s.missionRecorded = 0;
    if (!gevrProgressLocks()) return;
    for (int i = 0; i < s.unlockedGunCount; ++i) {
        const int key = keyForName(s.unlockedGuns[i]);
        if (key >= 0) s.ownedAtLaunch |= UINT64_C(1) << key;
    }
}

/* Physical weapons are reported by slot. A second copy may provide distinct parts, but not a second clear. */
void gevrProgressExtractGun(int build, const char *slot, const GevrGearAssembly *assembly, int magazine)
{
    const int key = normalizeKey(build);
    if (!s.completing || key < 0) return;
    const int oldParts = s.partCount;
    const int newGun = unlockGun(key);
    s.carried |= UINT64_C(1) << key;
    loadRules();
    for (int i = 1; assembly && i < assembly->count; ++i) {
        const GevrGearMount *m = &assembly->mounts[i];
        if (m->socket->required != GEVR_GEAR_SOCKET_AUTO && (magazine == -2 || strcmp(m->path, "mod_magazine")))
            awardFitted(key, m->part);
    }
    if (magazine >= 0) awardFitted(key, magazine);
    sysLogPrintf(LOG_NOTE, "tactical extraction: %s carried %s (new gun %d, new parts %d)",
        slot, gunRules[key].name, newGun, s.partCount - oldParts);
}

static int counting(void)
{
    /* a mission started straight from the command line plays folder 1, the game's default */
    if (!s.loaded && gevrTacticalGearEnabled()) gevrProgressSelectFolder(0);
    return s.loaded && s.folder >= 0 && gevrTacticalGearEnabled() && !netIsActive() && !gevrTacticalRangeActive()
        && !(g_AppendCheatSinglePlayer && gevrCheatsBlockProgress());
}

void gevrProgressShot(int item)
{
    const char *name = gevrTacticalGearItemName(item);
    if (!counting() || !name) return;
    GevrProgressGun *gun = gunStats(name, 1);
    if (gun) { gun->shots++; s.dirty = 1; }
}

void gevrProgressHit(int item, int killed, int headshot)
{
    const char *name = gevrTacticalGearItemName(item);
    if (!counting() || !name) return;
    GevrProgressGun *gun = gunStats(name, 1);
    if (!gun) return;
    gun->hits++;
    if (killed) {
        gun->kills++;
        if (headshot) gun->headshots++;
    }
    s.dirty = 1;
}

void gevrProgressClearExtraction(void)
{
    s_extractedGuns = s_extractedParts = 0;
}

void gevrProgressLastExtraction(int *guns, int *parts)
{
    *guns = s_extractedGuns;
    *parts = s_extractedParts;
}

void gevrProgressMissionComplete(int mission, int difficulty)
{
    if (!counting() || s.missionRecorded || mission < 0 || mission >= 64
        || difficulty < 0 || difficulty >= GEVR_PROGRESS_DIFFICULTIES) return;
    s.missionRecorded = 1;
    s.missions[difficulty]++;
    if (gevrProgressLocks()) {
        const int guns = s.unlockedGunCount, parts = s.partCount;
        const uint64_t missionBit = UINT64_C(1) << mission;
        if (!(s.clearedMissions & missionBit)) {
            s.clearedMissions |= missionBit;
            ++s.reserve;
        } else if (++s.replays >= REPLAYS_PER_RESERVE) {
            s.replays = 0;
            ++s.reserve;
        }
        s.carried = 0;
        s.completing = 1;
        gevrTacticalProgressExtractCarried();
        s.completing = 0;
        const uint64_t eligible = s.carried & s.ownedAtLaunch;
        for (unsigned key = 0; key < RULES(gunRules); ++key) {
            if (!(eligible & (UINT64_C(1) << key))) continue;
            GevrProgressGun *gun = gunStats(gunRules[key].name, 1);
            if (!gun) continue;
            ++gun->clears;
            for (unsigned k = 0; k < RULES(kitRules); ++k)
                if (kitRules[k].clears == gun->clears && (kitRules[k].hosts & (UINT64_C(1) << key))) awardKit(k);
        }
        s_extractedGuns = s.unlockedGunCount - guns;
        s_extractedParts = s.partCount - parts;
        sysLogPrintf(LOG_NOTE, "tactical extraction: unlocked %d guns and %d parts", s_extractedGuns, s_extractedParts);
    }
    s.dirty = 1;
    gevrProgressSave();
}

int gevrProgressGunCount(void)
{
    return s.loaded ? s.gunCount : 0;
}

const GevrProgressGun *gevrProgressGun(int index)
{
    return s.loaded && index >= 0 && index < s.gunCount ? &s.guns[index] : NULL;
}

void gevrProgressTotals(unsigned *kills, unsigned *headshots, unsigned *shots, unsigned *hits)
{
    *kills = *headshots = *shots = *hits = 0;
    for (int i = 0; s.loaded && i < s.gunCount; i++) {
        *kills += s.guns[i].kills;
        *headshots += s.guns[i].headshots;
        *shots += s.guns[i].shots;
        *hits += s.guns[i].hits;
    }
}

unsigned gevrProgressMissions(int difficulty)
{
    return s.loaded && difficulty >= 0 && difficulty < GEVR_PROGRESS_DIFFICULTIES ? s.missions[difficulty] : 0;
}
