#include "gevr_tactical_gear.h"
#include "gevr_tactical_session.h"
#include "gevr_tactical_asset.h"
#include "system.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

enum { MAX_PART_SOCKETS = 48, MAX_PACK_LINE = 8192 };
typedef struct GearChoice {
    char path[GEVR_GEAR_PATH];
    int part;
} GearChoice;

typedef struct GearBuild {
    int root, defaultsCount, changesCount;
    GearChoice defaults[GEVR_GEAR_MOUNTS];
    GearChoice changes[GEVR_GEAR_MOUNTS];
    GevrGearAssembly view;
} GearBuild;

static GevrGearPart *s_parts;
static GearBuild s_builds[GEVR_GEAR_BUILD_COUNT];
static GevrGearAssembly s_stock[GEVR_GEAR_BUILD_COUNT];
static int s_partCount, s_catalogStatus;
static uint64_t s_catalogHash;
static int s_launchStage = -1, s_primary, s_secondary, s_deployedBuild[2] = {-1, -1};
static int s_melee = GEVR_GEAR_MELEE_DEFAULT;
static int s_issuedLaunch;
static GevrGearAssembly s_deployed[2];
extern int gevrTacticalCampaignRules(void);

int gevrTacticalGearEnabled(void)
{
    return sysArgCheck("--tactical-gear") && sysArgCheck("--tactical-physical")
        && sysArgCheck("--tactical-mesh") && gevrTacticalCampaignRules();
}

static int readInt(char **cursor, int *out, int min, int max)
{
    char *end;
    long value = strtol(*cursor, &end, 10);
    if (end == *cursor || value < min || value > max) return 0;
    *cursor = end;
    *out = (int)value;
    return 1;
}

static int readFrame(char **cursor, float frame[4][4])
{
    /* Convert the pack's column-vector pose in metres to engine row vectors in mm. */
    const float sign[3] = {1, 1, -1};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            char *end;
            const float value = strtof(*cursor, &end);
            if (end == *cursor || !isfinite(value) || fabsf(value) > 10) return 0;
            *cursor = end;
            frame[col][row] = value * sign[row] * (col == 3 ? 1000 : sign[col]);
        }
    }
    frame[3][3] = 1;
    return 1;
}

static int readFits(char **cursor, GevrGearSocket *socket)
{
    if (!readInt(cursor, &socket->fitCount, 1, socket->count)) return 0;
    socket->fits = calloc(socket->fitCount, sizeof(*socket->fits));
    if (!socket->fits) return 0;
    int previous = -1;
    for (int i = 0; i < socket->fitCount; ++i) {
        GevrGearFit *fit = &socket->fits[i];
        if (!readInt(cursor, &fit->option, previous + 1, socket->count - 1)
            || !readFrame(cursor, fit->frame)) return 0;
        previous = fit->option;
    }
    return 1;
}

static int readSockets(FILE *file, GevrGearPart *part, char *line)
{
    if (!fgets(line, MAX_PACK_LINE, file) || sscanf(line, "%d", &part->socketCount) != 1
        || part->socketCount < 0 || part->socketCount > MAX_PART_SOCKETS) return 0;
    if (!part->socketCount) return 1;
    part->sockets = calloc(part->socketCount, sizeof(*part->sockets));
    if (!part->sockets) return 0;
    for (int i = 0; i < part->socketCount; ++i) {
        GevrGearSocket *socket = &part->sockets[i];
        int offset = 0;
        if (!fgets(line, MAX_PACK_LINE, file) ||
            sscanf(line, "%32[^\t]\t%31[^\t]\t%d\t%d\t%n", socket->key, socket->label,
                &socket->required, &socket->count, &offset) != 4 || !offset
            || socket->count < 0 || socket->count > s_partCount) return 0;
        char *cursor = line + offset;
        if (!readFrame(&cursor, socket->frame)) return 0;
        if (socket->count) {
            socket->allowed = malloc(socket->count * sizeof(*socket->allowed));
            if (!socket->allowed) return 0;
            for (int j = 0; j < socket->count; ++j)
                if (!readInt(&cursor, &socket->allowed[j], 0, s_partCount - 1)) return 0;
        }
        socket->offsetFrom = socket->count;
        cursor += strspn(cursor, " \t\r\n");
        if (!strncmp(cursor, "offset ", 7)) {
            int consumed = 0;
            float *offset = socket->alternateOffset;
            if (sscanf(cursor, "offset %d %f %f %f%n", &socket->offsetFrom, &offset[0], &offset[1], &offset[2], &consumed) != 4
                || socket->offsetFrom < 0 || socket->offsetFrom >= socket->count) return 0;
            for (int axis = 0; axis < 3; ++axis) {
                if (!isfinite(offset[axis]) || fabsf(offset[axis]) > 1.f) return 0;
                offset[axis] *= axis == 2 ? -1000.f : 1000.f;
            }
            cursor += consumed + strspn(cursor + consumed, " \t\r\n");
        }
        if (!strncmp(cursor, "fits ", 5)) {
            cursor += 5;
            if (!readFits(&cursor, socket)) return 0;
            cursor += strspn(cursor, " \t\r\n");
        }
        if (*cursor) return 0;
    }
    return 1;
}

static int readConflicts(FILE *file, GevrGearPart *part, char *line)
{
    char *cursor = line;
    if (!fgets(line, MAX_PACK_LINE, file) || !readInt(&cursor, &part->conflictCount, 0, s_partCount)) return 0;
    if (!part->conflictCount) return 1;
    part->conflicts = malloc(part->conflictCount * sizeof(*part->conflicts));
    if (!part->conflicts) return 0;
    for (int i = 0; i < part->conflictCount; ++i)
        if (!readInt(&cursor, &part->conflicts[i], 0, s_partCount - 1)) return 0;
    return 1;
}

static int readCatalog(FILE *file)
{
    char line[MAX_PACK_LINE];
    if (!fgets(line, sizeof(line), file)) return 0;
    const int keyed = strcmp(line, "GEVRGEAR3\n") == 0;
    if ((!keyed && strcmp(line, "GEVRGEAR2\n") != 0)
        || !fgets(line, sizeof(line), file) || sscanf(line, "%d", &s_partCount) != 1
        || s_partCount < 2 || s_partCount > GEVR_GEAR_PARTS) return 0;
    s_parts = calloc(s_partCount, sizeof(*s_parts));
    if (!s_parts) return 0;
    for (int i = 0; i < s_partCount; ++i) {
        GevrGearPart *part = &s_parts[i];
        if (!fgets(line, sizeof(line), file)
            || sscanf(line, "%24s\t%d\t%d\t%89[^\r\n]", part->id, &part->capacity, &part->art, part->name) != 4
            || strspn(part->id, "0123456789abcdef") != 24) return 0;
    }
    for (int i = 0; i < s_partCount; ++i)
        if (!readConflicts(file, &s_parts[i], line) || !readSockets(file, &s_parts[i], line)) return 0;
    static const int legacy[] = {TACTICAL_GUN_AK74N, TACTICAL_GUN_AK, TACTICAL_GUN_SVD};
    for (int i = 0; i < GEVR_GEAR_BUILD_COUNT; ++i) s_builds[i].root = -1;
    const int count = keyed ? GEVR_GEAR_BUILD_COUNT : (int)(sizeof(legacy) / sizeof(legacy[0]));
    for (int i = 0; i < count; ++i) {
        if (!fgets(line, sizeof(line), file)) {
            if (!feof(file)) return 0;
            if (!keyed) return i == 2;
            for (int j = 0; j <= GEVR_GEAR_PPK_SUPPRESSED; ++j)
                if (s_builds[j].root < 0) return 0;
            return 1;
        }
        int key, root, defaults;
        if (keyed) {
            if (sscanf(line, "%d %d %d", &key, &root, &defaults) != 3
                || key < 0 || key >= GEVR_GEAR_BUILD_COUNT) return 0;
        } else {
            key = legacy[i];
            if (sscanf(line, "%d %d", &root, &defaults) != 2) return 0;
        }
        GearBuild *build = &s_builds[key];
        if (build->root >= 0 || root < 0 || root >= s_partCount
            || defaults < 0 || defaults >= GEVR_GEAR_MOUNTS) return 0;
        build->root = root;
        build->defaultsCount = defaults;
        for (int j = 0; j < build->defaultsCount; ++j) {
            GearChoice *choice = &build->defaults[j];
            if (!fgets(line, sizeof(line), file) || sscanf(line, "%159s %d", choice->path, &choice->part) != 2
                || choice->part < 0 || choice->part >= s_partCount) return 0;
        }
    }
    return 1;
}

static int loadCatalog(void)
{
    if (s_catalogStatus) return s_catalogStatus > 0;
    FILE *file = gevrTacticalOpenAsset("mods/tactical/gear/catalog.txt", "r");
    const int ok = file && readCatalog(file);
    if (file) {
        if (ok) {
            /* Text mode normalizes CRLF and the explicit check covers assets on Android. */
            rewind(file);
            uint64_t hash = UINT64_C(14695981039346656037);
            for (int c; (c = fgetc(file)) != EOF;) {
                if (c != '\r') hash = (hash ^ (unsigned)c) * UINT64_C(1099511628211);
            }
            if (!ferror(file)) s_catalogHash = hash;
        }
        fclose(file);
    }
    if (!ok && s_parts) {
        for (int i = 0; i < s_partCount; ++i) {
            GevrGearPart *part = &s_parts[i];
            free(part->conflicts);
            if (part->sockets) {
                for (int j = 0; j < part->socketCount; ++j) free(part->sockets[j].allowed);
                free(part->sockets);
            }
        }
        free(s_parts);
        s_parts = NULL;
    }
    s_catalogStatus = ok ? 1 : -1;
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING, ok ? "tactical gear: loaded %d catalog entries" :
        "tactical gear: catalog unavailable (%d entries read)", s_partCount);
    return ok;
}

uint64_t gevrTacticalGearCatalogHash(void)
{
    return loadCatalog() ? s_catalogHash : 0;
}

int gevrTacticalExpandedEnabled(void)
{
    return sysArgCheck("--tactical-expanded") && sysArgCheck("--tactical-mesh") && sysArgCheck("--tactical-gear")
        && loadCatalog() && s_builds[gevrTacticalGearKey(TACTICAL_GUN_PKM)].root >= 0;
}

int gevrTacticalArEnabled(void)
{
    return gevrTacticalExpandedEnabled() && s_builds[gevrTacticalGearKey(TACTICAL_GUN_M4A1)].root >= 0
        && s_builds[gevrTacticalGearKey(TACTICAL_GUN_HK416)].root >= 0;
}

int gevrTacticalMuzzleCategory(int part)
{
    const GevrGearPart *muzzle = gevrTacticalGearPart(part);
    if (!muzzle) return 0;
    if ((muzzle->art & GEVR_GEAR_ART_SUPPRESSOR)
        || !strcmp(muzzle->id, "5a9fbb84a2750c00137fa685")) return GEVR_MUZZLE_SUPPRESSOR;
    if (muzzle->art & GEVR_GEAR_ART_MUZZLE_ADAPTER) return GEVR_MUZZLE_ADAPTER;

    char name[sizeof(muzzle->name)];
    size_t i;
    for (i = 0; muzzle->name[i]; ++i) name[i] = (char)tolower((unsigned char)muzzle->name[i]);
    name[i] = '\0';
    if (strstr(name, "adapter") || strstr(name, "thread protector") || strstr(name, "mount"))
        return GEVR_MUZZLE_ADAPTER;
    if (!strstr(name, "flash") && !strstr(name, "cover")
        && (strstr(name, "suppressor") || strstr(name, "silencer") || strstr(name, "sound moderator")))
        return GEVR_MUZZLE_SUPPRESSOR;
    return GEVR_MUZZLE_BRAKE;
}

int gevrTacticalMuzzleIsSuppressor(int part)
{
    return gevrTacticalMuzzleCategory(part) == GEVR_MUZZLE_SUPPRESSOR;
}

int gevrTacticalGearAssemblySuppressed(const GevrGearAssembly *assembly)
{
    if (!assembly) return 0;
    for (int i = 0; i < assembly->count; ++i) {
        const GevrGearMount *mount = &assembly->mounts[i];
        const GevrGearPart *part = gevrTacticalGearPart(mount->part);
        if (!part) continue;
        if (part->art & GEVR_GEAR_ART_SUPPRESSOR) return 1;
        const char *key = mount->socket ? mount->socket->key : "";
        if ((!strncmp(key, "mod_muzzle", 10) || !strcmp(key, "mod_barrel"))
            && gevrTacticalMuzzleIsSuppressor(mount->part)) return 1;
    }
    return 0;
}

float gevrTacticalGearFlashScale(const GevrGearAssembly *assembly)
{
    const GevrGearPart *device = NULL;
    int depth = 0;
    for (int i = 1; assembly && i < assembly->count; ++i) {
        const GevrGearMount *mount = &assembly->mounts[i];
        if (!mount->socket || strncmp(mount->socket->key, "mod_muzzle", 10)
            || gevrTacticalMuzzleCategory(mount->part) != GEVR_MUZZLE_BRAKE) continue;
        int nesting = 0;
        for (const char *c = mount->path; *c; ++c) nesting += *c == '/';
        if (device && nesting < depth) continue;
        device = gevrTacticalGearPart(mount->part);
        depth = nesting;
    }
    if (!device) return 1.f;
    /* The PWS CQB 74 has a deliberately exaggerated flash in Tactical. */
    if (!strcmp(device->id, "5943eeeb86f77412d6384f6b")) return 1.85f;
    char name[sizeof(device->name)];
    size_t i;
    for (i = 0; device->name[i]; ++i) name[i] = (char)tolower((unsigned char)device->name[i]);
    name[i] = '\0';
    if (strstr(name, "cqb") || strstr(name, "kx3")) return 1.35f;
    if (strstr(name, "flash hider") || strstr(name, "flash suppressor")
        || !strcmp(name, "usgi a2") || !strcmp(name, "6p26")) return .72f;
    return 1.08f;
}

static GearBuild *getBuild(int kind)
{
    return kind >= 0 && kind < GEVR_GEAR_BUILD_COUNT && loadCatalog() && s_builds[kind].root >= 0
        ? &s_builds[kind] : NULL;
}

int gevrTacticalGearBuildAvailable(int kind)
{
    return kind == GEVR_GEAR_RPG ? gevrTacticalGearEnabled() : getBuild(kind) != NULL;
}

const GevrGearPart *gevrTacticalGearPart(int part)
{
    return s_parts && part >= 0 && part < s_partCount ? &s_parts[part] : NULL;
}

static int socketAccepts(const GevrGearSocket *socket, int part)
{
    if (part == -1) return !socket->required;
    for (int i = 0; i < socket->count; ++i) if (socket->allowed[i] == part) return 1;
    return 0;
}

static int selectedPart(const GearBuild *build, const GevrGearMount *mount)
{
    for (int i = 0; i < build->changesCount; ++i)
        if (!strcmp(build->changes[i].path, mount->path) && socketAccepts(mount->socket, build->changes[i].part))
            return build->changes[i].part;
    for (int i = 0; i < build->defaultsCount; ++i)
        if (!strcmp(build->defaults[i].path, mount->path) && socketAccepts(mount->socket, build->defaults[i].part))
            return build->defaults[i].part;
    return mount->socket->required && mount->socket->count ? mount->socket->allowed[0] : -1;
}

static void mountFrame(const GevrGearSocket *socket, int part, float local[4][4])
{
    for (int i = 0; i < socket->fitCount; ++i) {
        const GevrGearFit *fit = &socket->fits[i];
        if (socket->allowed[fit->option] != part) continue;
        memcpy(local, fit->frame, sizeof(fit->frame));
        return;
    }
    memcpy(local, socket->frame, sizeof(socket->frame));
    for (int option = socket->offsetFrom; option < socket->count; ++option) {
        if (socket->allowed[option] != part) continue;
        for (int axis = 0; axis < 3; ++axis) local[3][axis] += socket->alternateOffset[axis];
        break;
    }
}

static const char launcherKey[] = "mod_launcher";
static const char gasKey[] = "mod_gas_block";

enum { FRONT_OTHER, FRONT_BARREL, FRONT_HANDGUARD, FRONT_GAS, FRONT_MUZZLE };

static int frontFitting(const GevrGearSocket *socket)
{
    if (!socket) return FRONT_OTHER;
    const char *key = socket->key;
    if (!strcmp(key, "mod_barrel")) return FRONT_BARREL;
    if (!strncmp(key, "mod_handguard", 13)) return FRONT_HANDGUARD;
    if (!strcmp(key, gasKey)) return FRONT_GAS;
    if (!strncmp(key, "mod_muzzle", 10)) return FRONT_MUZZLE;
    return FRONT_OTHER;
}

static bool frontClearanceConflict(const GevrGearSocket *first, const GevrGearSocket *second)
{
    const int a = frontFitting(first), b = frontFitting(second);
    return a != FRONT_OTHER && b != FRONT_OTHER && a != b;
}

typedef struct GearReadSelection {
    const GevrGearSelection *selection;
    int failed;
} GearReadSelection;

static int viewPart(GearBuild *build, const GevrGearMount *mount, GearReadSelection *remote)
{
    if (!remote) return selectedPart(build, mount);

    const int index = build->view.count - 2;
    if (!remote->failed && index < remote->selection->count) {
        const int part = remote->selection->parts[index] - 1;
        if (part < 0) return -1; /* Auto fittings and some stock builds leave a visible mount empty. */
        for (int i = 0; i < mount->socket->count; ++i)
            if (mount->socket->allowed[i] == part) return part;
    }
    remote->failed = 1;
    return -1;
}

static void appendMounts(GearBuild *build, const GevrGearMount *parent, int depth,
    const GevrGearSocket *autoGas, int gasPart, GearReadSelection *remote)
{
    const GevrGearPart *part = gevrTacticalGearPart(parent->part);
    if (!part || depth >= 9) return;
    const int childrenStart = build->view.count;
    for (int i = 0; i < part->socketCount && build->view.count < GEVR_GEAR_MOUNTS; ++i) {
        const GevrGearSocket *socket = &part->sockets[i];
        const size_t parentLen = strlen(parent->path), keyLen = strlen(socket->key);
        if (!socket->count || parentLen + keyLen + 2 > GEVR_GEAR_PATH || !strcmp(socket->key, launcherKey)
            || !strcmp(socket->key, GEVR_GEAR_VARIANT_SLOT))
            continue;
        GevrGearMount *mount = &build->view.mounts[build->view.count++];
        mount->socket = socket;
        memcpy(mount->path, parent->path, parentLen);
        size_t start = parentLen;
        if (start) mount->path[start++] = '/';
        memcpy(mount->path + start, socket->key, keyLen + 1);
        mount->part = socket == autoGas ? gasPart : viewPart(build, mount, remote);
        float local[4][4];
        mountFrame(socket, mount->part, local);
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) {
                float value = 0;
                for (int j = 0; j < 4; ++j) value += local[r][j] * parent->frame[j][c];
                mount->frame[r][c] = value;
            }
    }
    /* Add siblings first so the main fittings stay together on the first page. */
    const int childrenEnd = build->view.count;
    for (int i = childrenStart; i < childrenEnd; ++i)
        appendMounts(build, &build->view.mounts[i], depth + 1, autoGas, gasPart, remote);
}

static void buildView(GearBuild *build, const GevrGearSocket *autoGas, int gasPart, GearReadSelection *remote)
{
    static unsigned revision;
    build->view.revision = ++revision;
    GevrGearMount *root = &build->view.mounts[0];
    *root = (GevrGearMount){.part = build->root};
    for (int i = 0; i < 4; ++i) root->frame[i][i] = 1;
    build->view.count = 1;
    const GevrGearPart *base = gevrTacticalGearPart(build->root);
    for (int i = 0; base && i < base->socketCount; ++i) {
        const GevrGearSocket *socket = &base->sockets[i];
        if (strcmp(socket->key, GEVR_GEAR_VARIANT_SLOT) || !socket->count) continue;
        GevrGearMount *variant = &build->view.mounts[build->view.count++];
        *variant = *root;
        variant->socket = socket;
        strcpy(variant->path, GEVR_GEAR_VARIANT_SLOT);
        variant->part = viewPart(build, variant, remote);
        if (variant->part >= 0) {
            root->part = variant->part;
            mountFrame(socket, variant->part, root->frame);
        }
        break;
    }
    appendMounts(build, root, 0, autoGas, gasPart, remote);
    const GevrGearMount *finish = gevrTacticalGearFindMount(&build->view, GEVR_GEAR_FINISH_SLOT);
    if (finish && finish->part >= 0) root->part = finish->part;
}

static int partListsConflict(const GevrGearPart *part, int other)
{
    if (part)
        for (int i = 0; i < part->conflictCount; ++i)
            if (part->conflicts[i] == other) return 1;
    return 0;
}

static int gasConflicts(const GevrGearAssembly *assembly, const GevrGearSocket *socket, int gasPart)
{
    const GevrGearPart *gas = gevrTacticalGearPart(gasPart);
    for (int i = 0; i < assembly->count; ++i) {
        const GevrGearMount *other = &assembly->mounts[i];
        if (other->socket == socket || other->part < 0) continue;
        if (frontClearanceConflict(socket, other->socket)) continue;
        if (partListsConflict(gas, other->part)
            || partListsConflict(gevrTacticalGearPart(other->part), gasPart)) return 1;
    }
    return 0;
}

static void rebuild(GearBuild *build)
{
    buildView(build, NULL, -1, NULL);
    for (int i = 1; i < build->view.count; ++i) {
        const GevrGearMount *mount = &build->view.mounts[i];
        const GevrGearSocket *socket = mount->socket;
        /* Root AK gas assemblies are chosen by the Handguard picker. Barrel
         * gas blocks follow the fitted barrel and handguard automatically. */
        if (strcmp(socket->key, gasKey) || !strcmp(mount->path, gasKey)
            || !gasConflicts(&build->view, socket, mount->part)) continue;

        for (int j = 0; j < socket->count; ++j) {
            if (gasConflicts(&build->view, socket, socket->allowed[j])) continue;
            buildView(build, socket, socket->allowed[j], NULL);
            return;
        }
        /* The optional gas-block mesh need not restrict a Workshop build. */
        buildView(build, socket, -1, NULL);
        return;
    }
}

const GevrGearAssembly *gevrTacticalGearAssembly(int kind)
{
    GearBuild *build = getBuild(kind);
    if (!build) return NULL;
    if (!build->view.count) rebuild(build);
    return &build->view;
}

static const GevrGearAssembly *gevrJanusM4(void);
static const GevrGearAssembly *issuedAssembly(int build);
static const GevrGearAssembly *remoteAssembly(int build);

const GevrGearAssembly *gevrTacticalGearDeployedAssembly(int kind)
{
    if (kind == GEVR_GEAR_JANUS_M4) return gevrJanusM4();
    if (kind < 0) return NULL;
    if (kind >= GEVR_GEAR_BUILD_COUNT) {
        const GevrGearAssembly *remote = remoteAssembly(kind);
        return remote ? remote : issuedAssembly(kind);
    }
    for (int slot = 0; slot < 2; ++slot)
        if (s_deployedBuild[slot] == kind) return &s_deployed[slot];
    return NULL;
}

const GevrGearAssembly *gevrTacticalGearStockAssembly(int kind)
{
    GearBuild *build = getBuild(kind);
    if (!build) return NULL;
    if (!s_stock[kind].count) {
        GearBuild stock = *build;
        stock.changesCount = 0;
        rebuild(&stock);
        s_stock[kind] = stock.view;
    }
    return &s_stock[kind];
}

const GevrGearMount *gevrTacticalGearFindMount(const GevrGearAssembly *assembly, const char *path)
{
    if (!assembly) return NULL;
    for (int i = 1; i < assembly->count; ++i)
        if (!strcmp(assembly->mounts[i].path, path)) return &assembly->mounts[i];
    return NULL;
}

static const char arOpticPath[] = "mod_reciever/mod_scope";
static const char arRearPath[] = "mod_reciever/mod_sight_rear";

static int carryHandle(int part)
{
    const GevrGearPart *info = gevrTacticalGearPart(part);
    return info && (info->art & GEVR_GEAR_ART_CARRY_HANDLE);
}

static int sameMountOrChild(const char *path, const char *mount)
{
    const size_t length = strlen(mount);
    return !strncmp(path, mount, length) && (path[length] == '\0' || path[length] == '/');
}

static int findConflict(const GearBuild *build, int chosen)
{
    const GevrGearAssembly *assembly = &build->view;
    const GevrGearMount *rear = gevrTacticalGearFindMount(assembly, arRearPath);
    const GevrGearMount *optic = gevrTacticalGearFindMount(assembly, arOpticPath);
    if (rear && optic && optic->part >= 0 && carryHandle(rear->part))
        return chosen == optic->part ? rear->part : optic->part;
    for (int i = 0; i < assembly->count; ++i) {
        const int fitted = assembly->mounts[i].part;
        const GevrGearPart *part = gevrTacticalGearPart(fitted);
        if (!part) continue;
        for (int j = 0; j < assembly->count; ++j) {
            if (i == j || assembly->mounts[j].part < 0) continue;
            const int other = assembly->mounts[j].part;
            if (!partListsConflict(part, other)) continue;
            /* Workshop permits the front of the gun to overlap. Parts must
             * still be in the proper socket's own list. */
            if (frontClearanceConflict(assembly->mounts[i].socket, assembly->mounts[j].socket)) continue;
            return other == chosen ? fitted : other;
        }
    }
    return -1;
}

static const char akGuardPath[] = "mod_gas_block/mod_handguard";

static int isAkHandguard(int kind, const GevrGearMount *mount)
{
    return (kind == TACTICAL_GUN_AK || kind == TACTICAL_GUN_AK74N) && !strcmp(mount->path, gasKey);
}

static const GevrGearSocket *partSocket(int owner, const char *key)
{
    const GevrGearPart *part = gevrTacticalGearPart(owner);
    if (part)
        for (int i = 0; i < part->socketCount; ++i)
            if (!strcmp(part->sockets[i].key, key)) return &part->sockets[i];
    return NULL;
}

static int mountedPart(int kind, const GevrGearAssembly *assembly, const GevrGearMount *mount)
{
    if (isAkHandguard(kind, mount)) {
        const GevrGearMount *guard = gevrTacticalGearFindMount(assembly, akGuardPath);
        if (guard) return guard->part;
    }
    return mount->part;
}

int gevrTacticalGearMountPart(int kind, int index)
{
    const GevrGearAssembly *assembly = gevrTacticalGearAssembly(kind);
    if (!assembly || index < 1 || index >= assembly->count) return -1;
    return mountedPart(kind, assembly, &assembly->mounts[index]);
}

int gevrTacticalGearOptions(int kind, int index, int options[GEVR_GEAR_PARTS + 1])
{
    const GevrGearAssembly *assembly = gevrTacticalGearAssembly(kind);
    if (!assembly || index < 1 || index >= assembly->count) return 0;
    const GevrGearMount *mount = &assembly->mounts[index];
    const GevrGearSocket *socket = mount->socket;
    if (socket->required == GEVR_GEAR_SOCKET_AUTO) return 0;
    int count = 0;
    if (!socket->required) options[count++] = -1;
    if (!isAkHandguard(kind, mount)) {
        memcpy(options + count, socket->allowed, socket->count * sizeof(*options));
        return count + socket->count;
    }
    /* The full handguard picker includes assemblies with an integrated tube.
     * A separate tube remains an internal choice for the renderer and old presets. */
    unsigned char seen[GEVR_GEAR_PARTS] = {0};
    for (int i = 0; i < socket->count; ++i) {
        const int gas = socket->allowed[i];
        const GevrGearSocket *guard = partSocket(gas, "mod_handguard");
        for (int j = 0; j < (guard ? guard->count : 1); ++j) {
            const int part = guard ? guard->allowed[j] : gas;
            if (seen[part]) continue;
            seen[part] = 1;
            options[count++] = part;
        }
    }
    return count;
}

static int setGearChoice(GearBuild *build, const char *path, int part, const char *clear)
{
    int keep = 0;
    for (int i = 0; i < build->changesCount; ++i) {
        const GearChoice *change = &build->changes[i];
        if (sameMountOrChild(change->path, path) || (clear && sameMountOrChild(change->path, clear))) continue;
        build->changes[keep++] = *change;
    }
    if (keep + 1 + (clear != NULL) > GEVR_GEAR_MOUNTS) return 0;
    if (clear) {
        GearChoice *removed = &build->changes[keep++];
        strcpy(removed->path, clear);
        removed->part = -1;
    }
    GearChoice *change = &build->changes[keep++];
    strcpy(change->path, path);
    change->part = part;
    build->changesCount = keep;
    return 1;
}

static void retainBarrelFittings(GearBuild *trial, const GearBuild *previous, const char *barrel)
{
    const size_t prefix = strlen(barrel);
    for (int i = 1; i < previous->view.count; ++i) {
        const GevrGearMount *old = &previous->view.mounts[i];
        const int fitting = frontFitting(old->socket);
        if (old->part < 0 || (fitting != FRONT_MUZZLE && fitting != FRONT_GAS)
            || strncmp(old->path, barrel, prefix) || old->path[prefix] != '/') continue;
        const GevrGearMount *current = gevrTacticalGearFindMount(&trial->view, old->path);
        if (!current || current->part == old->part || !socketAccepts(current->socket, old->part)) continue;
        if (setGearChoice(trial, old->path, old->part, NULL)) rebuild(trial);
    }
}

static int fitStockAkCover(GearBuild *trial, int kind, int handguard, int gas)
{
    static const char coverPath[] = "mod_reciever";
    const GevrGearMount *cover = gevrTacticalGearFindMount(&trial->view, coverPath);
    if (!cover || cover->part < 0) return 0;
    const GevrGearPart *fitted = gevrTacticalGearPart(cover->part);
    if (!partListsConflict(fitted, handguard) && !partListsConflict(fitted, gas)
        && !partListsConflict(gevrTacticalGearPart(handguard), cover->part)
        && !partListsConflict(gevrTacticalGearPart(gas), cover->part)) return 0;

    const GevrGearMount *stock = gevrTacticalGearFindMount(gevrTacticalGearStockAssembly(kind), coverPath);
    if (!stock || stock->part == cover->part || !socketAccepts(cover->socket, stock->part)) return 0;
    return setGearChoice(trial, coverPath, stock->part, NULL);
}

static int fitAkHandguard(GearBuild *trial, const GearBuild *build, int kind,
    const GevrGearMount *mount, int part, int *conflict)
{
    const GevrGearSocket *gasSocket = mount->socket;
    const GevrGearPart *handguard = gevrTacticalGearPart(part);
    const bool woodGrip = handguard && !strcmp(handguard->id, "5d2c829448f0353a5c7d6674");

    /* Try every compatible tube with the current cover before replacing a
     * rail that depends on the old handguard. Leave other slots in place. */
    for (int useStockCover = 0; useStockCover <= woodGrip; ++useStockCover) {
        for (int i = -1; i < gasSocket->count; ++i) {
            const int gas = i < 0 ? mount->part : gasSocket->allowed[i];
            if (i >= 0 && gas == mount->part) continue;
            const GevrGearSocket *guard = partSocket(gas, "mod_handguard");
            if (guard ? !socketAccepts(guard, part) : (part < 0 || gas != part)) continue;
            *trial = *build;
            if (gas != mount->part && !setGearChoice(trial, gasKey, gas, NULL)) continue;
            if (guard && !setGearChoice(trial, akGuardPath, part, NULL)) continue;
            if (useStockCover && !fitStockAkCover(trial, kind, part, gas)) continue;
            rebuild(trial);
            *conflict = findConflict(trial, part);
            if (*conflict < 0) return 1;
        }
    }
    return 0;
}

static int testGearFit(GearBuild *trial, const GearBuild *build, int kind, int index, int part, int *conflict)
{
    *conflict = -1;
    if (!build || index < 1 || index >= build->view.count) return 0;
    const GevrGearMount *mount = &build->view.mounts[index];
    if (mount->socket->required == GEVR_GEAR_SOCKET_AUTO) return 0;
    *trial = *build;
    if (part == mountedPart(kind, &build->view, mount)) return 1;
    if (isAkHandguard(kind, mount)) return fitAkHandguard(trial, build, kind, mount, part, conflict);
    if (!socketAccepts(mount->socket, part)) return 0;

    const char *clear = NULL;
    if (part >= 0 && !strcmp(mount->path, arOpticPath)) {
        const GevrGearMount *rear = gevrTacticalGearFindMount(&build->view, arRearPath);
        if (rear && carryHandle(rear->part)) clear = rear->path;
    } else if (carryHandle(part) && !strcmp(mount->path, arRearPath)) {
        const GevrGearMount *optic = gevrTacticalGearFindMount(&build->view, arOpticPath);
        if (optic && optic->part >= 0) clear = optic->path;
    }
    if (!setGearChoice(trial, mount->path, part, clear)) return 0;
    rebuild(trial);
    if (frontFitting(mount->socket) == FRONT_BARREL)
        retainBarrelFittings(trial, build, mount->path);
    *conflict = findConflict(trial, part);
    if (*conflict >= 0 && !strcmp(mount->path, GEVR_GEAR_VARIANT_SLOT)) {
        trial->changesCount = 0;
        if (!setGearChoice(trial, mount->path, part, NULL)) return 0;
        rebuild(trial);
        *conflict = findConflict(trial, part);
    }
    return *conflict < 0;
}

int gevrTacticalGearCanInstall(int kind, int index, int part, int *conflict)
{
    GearBuild trial;
    return testGearFit(&trial, getBuild(kind), kind, index, part, conflict);
}

int gevrTacticalGearPreviewVariant(int kind, int index, int part, GevrGearAssembly *out)
{
    GearBuild trial;
    int conflict;
    if (!testGearFit(&trial, getBuild(kind), kind, index, part, &conflict)) return 0;
    *out = trial.view;
    return 1;
}

int gevrTacticalGearInstall(int kind, int index, int part, int *conflict)
{
    GearBuild *build = getBuild(kind);
    GearBuild trial;
    if (!testGearFit(&trial, build, kind, index, part, conflict)) return 0;
    if (part == mountedPart(kind, &build->view, &build->view.mounts[index])) return 1;
    sysLogPrintf(LOG_NOTE, "tactical gear: %s set to %s", build->view.mounts[index].path,
        part >= 0 ? s_parts[part].name : "Empty");
    *build = trial;
    return 1;
}

void gevrTacticalGearReset(int kind)
{
    GearBuild *build = getBuild(kind);
    if (!build) return;
    build->changesCount = 0;
    rebuild(build);
}

enum { MAX_USER_PRESETS = 48, MAX_BUNDLED_PRESETS = 48 };
typedef struct GearPreset {
    char root[25], name[GEVR_GEAR_PRESET_NAME + 1];
    int count, checked, compatible, bundled, removed;
    struct { char path[GEVR_GEAR_PATH], part[25]; } choices[GEVR_GEAR_MOUNTS];
} GearPreset;

static const char presetFile[] = "tactical-presets.txt";
static const char bundledPresetFile[] = "mods/tactical/gear/presets.txt";
static const char removedPresetFile[] = "tactical-presets-removed.txt";
static GearPreset s_presets[MAX_USER_PRESETS + MAX_BUNDLED_PRESETS];
static int s_presetCount, s_userPresetCount, s_presetStatus;
enum { ISSUED_BUILD_BASE = GEVR_GEAR_JANUS_M4 + 1, MAX_ISSUED_BUILDS = 2 * (MAX_USER_PRESETS + MAX_BUNDLED_PRESETS) };
static struct IssuedBuild {
    int key;
    char name[GEVR_GEAR_PRESET_NAME + 1];
    GevrGearAssembly assembly;
} s_issuedBuilds[MAX_ISSUED_BUILDS];
static int s_issuedCount;

static int presetNameValid(const char *name)
{
    const size_t len = strlen(name);
    if (len < 1 || len > GEVR_GEAR_PRESET_NAME || name[0] == ' ' || name[len - 1] == ' ') return 0;
    return strspn(name, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -_") == len;
}

static int presetIdValid(const char *id)
{
    return strspn(id, "0123456789abcdef") == 24 && id[24] == '\0';
}

static int readPreset(FILE *file, const char *line, GearPreset *preset)
{
    char choice[256], extra;
    int end;
    if (sscanf(line, "P %24s %d %24[^\r\n]%n", preset->root, &preset->count, preset->name, &end) != 3
        || (line[end] && line[end] != '\r' && line[end] != '\n')
        || !presetIdValid(preset->root) || !presetNameValid(preset->name)
        || preset->count < 0 || preset->count >= GEVR_GEAR_MOUNTS) return 0;
    for (int i = 0; i < preset->count; ++i) {
        if (!fgets(choice, sizeof(choice), file)
            || sscanf(choice, "%159s %24s %c", preset->choices[i].path, preset->choices[i].part, &extra) != 2
            || (!presetIdValid(preset->choices[i].part) && strcmp(preset->choices[i].part, "-"))) return 0;
    }
    return 1;
}

static int readPresetHeader(FILE *file)
{
    char line[32];
    return fgets(line, sizeof(line), file)
        && (!strcmp(line, "GEVRPRESETS1\n") || !strcmp(line, "GEVRPRESETS1\r\n"));
}

static int readPresets(FILE *file, int bundled)
{
    char line[256];
    if (!readPresetHeader(file)) return 0;
    const int limit = s_presetCount + (bundled ? MAX_BUNDLED_PRESETS : MAX_USER_PRESETS);
    while (fgets(line, sizeof(line), file)) {
        if (s_presetCount >= limit) return 0;
        GearPreset *p = &s_presets[s_presetCount];
        *p = (GearPreset){.bundled = bundled};
        if (!readPreset(file, line, p)) return 0;
        ++s_presetCount;
    }
    return !ferror(file);
}

static void readRemovedPresets(void)
{
    FILE *file = fopen(removedPresetFile, "r");
    if (!file) {
        if (errno != ENOENT) sysLogPrintf(LOG_WARNING, "workshop: could not read removed presets");
        return;
    }
    char line[128], root[25], name[GEVR_GEAR_PRESET_NAME + 1];
    while (fgets(line, sizeof(line), file)) {
        int end;
        if (sscanf(line, "%24s %24[^\r\n]%n", root, name, &end) != 2
            || (line[end] && line[end] != '\r' && line[end] != '\n')
            || !presetIdValid(root) || !presetNameValid(name)) continue;
        for (int i = s_userPresetCount; i < s_presetCount; ++i)
            if (!strcmp(root, s_presets[i].root) && !strcmp(name, s_presets[i].name))
                s_presets[i].removed = 1;
    }
    if (ferror(file)) sysLogPrintf(LOG_WARNING, "workshop: could not read all removed presets");
    fclose(file);
}

static int loadPresets(void)
{
    if (s_presetStatus) return s_presetStatus > 0;
    FILE *file = fopen(presetFile, "r");
    const int good = file ? readPresets(file, 0) : errno == ENOENT;
    if (file) fclose(file);
    s_presetStatus = good ? 1 : -1;
    if (!good) {
        sysLogPrintf(LOG_WARNING, "workshop: could not read tactical presets");
        return 0;
    }
    s_userPresetCount = s_presetCount;
    file = gevrTacticalOpenAsset(bundledPresetFile, "r");
    if (file) {
        if (!readPresets(file, 1)) {
            s_presetCount = s_userPresetCount;
            sysLogPrintf(LOG_WARNING, "workshop: could not read bundled presets");
        }
        fclose(file);
    }
    readRemovedPresets();
    return 1;
}

void gevrTacticalGearRefreshPresets(void)
{
    s_presetCount = s_userPresetCount = s_presetStatus = 0;
}

static void writePreset(FILE *file, const GearPreset *preset)
{
    fprintf(file, "P %s %d %s\n", preset->root, preset->count, preset->name);
    for (int i = 0; i < preset->count; ++i)
        fprintf(file, "%s %s\n", preset->choices[i].path, preset->choices[i].part);
}

enum { PRESET_TEMP_PATH = 64 };

static FILE *openPresetTemp(const char *target, char path[PRESET_TEMP_PATH])
{
#ifdef _WIN32
    snprintf(path, PRESET_TEMP_PATH, "%s.%lu.tmp", target, (unsigned long)GetCurrentProcessId());
#else
    snprintf(path, PRESET_TEMP_PATH, "%s.%ld.tmp", target, (long)getpid());
#endif
    return fopen(path, "w");
}

static int commitPresetFile(FILE *file, const char *path, const char *target, int failed)
{
    failed |= ferror(file);
    const int closed = fclose(file);
    if (!failed && !closed) {
#ifdef _WIN32
        if (MoveFileExA(path, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 1;
#else
        if (rename(path, target) == 0) return 1;
#endif
    }
    remove(path);
    return 0;
}

static int writePresets(void)
{
    char path[PRESET_TEMP_PATH];
    FILE *file = openPresetTemp(presetFile, path);
    if (!file) return 0;
    fputs("GEVRPRESETS1\n", file);
    for (int i = 0; i < s_userPresetCount; ++i) writePreset(file, &s_presets[i]);
    return commitPresetFile(file, path, presetFile, 0);
}

static int writeRemovedPreset(const GearPreset *preset)
{
    FILE *previous = fopen(removedPresetFile, "r");
    if (!previous && errno != ENOENT) return 0;
    char path[PRESET_TEMP_PATH];
    FILE *file = openPresetTemp(removedPresetFile, path);
    if (!file) {
        if (previous) fclose(previous);
        return 0;
    }
    int failed = 0;
    if (previous) {
        char buffer[512], last = '\n';
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), previous)) > 0) {
            if (fwrite(buffer, 1, bytes, file) != bytes) { failed = 1; break; }
            last = buffer[bytes - 1];
        }
        failed |= ferror(previous);
        fclose(previous);
        if (last != '\n') fputc('\n', file);
    }
    fprintf(file, "%s\t%s\n", preset->root, preset->name);
    return commitPresetFile(file, path, removedPresetFile, failed);
}

static int pathHasSlot(const char *path, const char *key)
{
    size_t length = strlen(key);
    while (path) {
        if (!strncmp(path, key, length) && (path[length] == '/' || !path[length])) return 1;
        const char *slash = strchr(path, '/');
        path = slash ? slash + 1 : NULL;
    }
    return 0;
}

static int applyPreset(GearBuild *build, const GearPreset *preset, int commit)
{
    GearBuild trial = *build;
    trial.changesCount = 0;
    for (int i = 0; i < preset->count; ++i) {
        const char *path = preset->choices[i].path;
        if (pathHasSlot(path, launcherKey)) continue;
        if (!strcmp(path, "mod_muzzle") && build->root == s_builds[gevrTacticalGearKey(TACTICAL_GUN_EVO3)].root
            && !partSocket(build->root, "mod_muzzle"))
            path = "mod_barrel/mod_muzzle";
        GearChoice *choice = &trial.changes[trial.changesCount++];
        const char *id = preset->choices[i].part;
        choice->part = -1;
        if (strcmp(id, "-")) {
            for (int j = 0; j < s_partCount; ++j) {
                if (!strcmp(s_parts[j].id, id)) { choice->part = j; break; }
            }
            if (choice->part < 0) return 0;
        }
        strcpy(choice->path, path);
    }
    rebuild(&trial);
    for (int i = 0; i < trial.changesCount; ++i) {
        const GearChoice *choice = &trial.changes[i];
        int found = 0;
        for (int j = 1; j < trial.view.count; ++j) {
            const GevrGearMount *mount = &trial.view.mounts[j];
            if (strcmp(mount->path, choice->path)) continue;
            /* Older presets could explicitly leave one half of an assembly empty. */
            found = mount->part == choice->part
                || (choice->part < 0 && mount->socket->required == GEVR_GEAR_SOCKET_AUTO);
            break;
        }
        if (!found) return 0;
    }
    if (findConflict(&trial, -1) >= 0) return 0;
    if (commit) *build = trial;
    return 1;
}

static int presetMatches(GearBuild *build, GearPreset *preset)
{
    if (preset->removed || strcmp(preset->root, s_parts[build->root].id)) return 0;
    if (!preset->checked) {
        preset->compatible = applyPreset(build, preset, 0);
        preset->checked = 1;
    }
    return preset->compatible;
}

static int samePresetName(const GearPreset *left, const GearPreset *right)
{
    return !strcmp(left->root, right->root) && !strcmp(left->name, right->name);
}

static int presetAvailable(GearBuild *build, GearPreset *preset)
{
    if (!presetMatches(build, preset)) return 0;
    if (preset->bundled) {
        for (int i = 0; i < s_userPresetCount; ++i)
            if (samePresetName(preset, &s_presets[i]) && presetMatches(build, &s_presets[i])) return 0;
    }
    return 1;
}

static GearPreset *presetAt(GearBuild *build, int index)
{
    if (!build || index < 0 || !loadPresets()) return NULL;
    for (int i = 0; i < s_presetCount; ++i)
        if (presetAvailable(build, &s_presets[i]) && index-- == 0) return &s_presets[i];
    return NULL;
}

static GearPreset *presetNamed(GearBuild *build, const char *name)
{
    if (!build || !loadPresets()) return NULL;
    for (int i = 0; i < s_presetCount; ++i)
        if (!strcmp(name, s_presets[i].name) && presetMatches(build, &s_presets[i])) return &s_presets[i];
    return NULL;
}

/* Fingerprint the saved choices so another process's edit cannot be overwritten
 * by a build loaded earlier. Include separators between the variable-length fields. */
static uint64_t hashPresetField(uint64_t hash, const char *field)
{
    do {
        hash = (hash ^ (unsigned char)*field) * UINT64_C(1099511628211);
    } while (*field++);
    return hash;
}

static uint64_t presetRevision(const GearPreset *preset)
{
    uint64_t hash = hashPresetField(UINT64_C(14695981039346656037), preset->root);
    hash = hashPresetField(hash, preset->name);
    for (int i = 0; i < preset->count; ++i) {
        hash = hashPresetField(hash, preset->choices[i].path);
        hash = hashPresetField(hash, preset->choices[i].part);
    }
    return hash ? hash : 1;
}

uint64_t gevrTacticalGearPresetRevision(int kind, const char *name)
{
    const GearPreset *preset = presetNamed(getBuild(kind), name);
    return preset ? presetRevision(preset) : 0;
}

int gevrTacticalGearLoadNamedPreset(int kind, const char *name)
{
    GearBuild *build = getBuild(kind);
    const GearPreset *preset = presetNamed(build, name);
    return preset && applyPreset(build, preset, 1);
}

int gevrTacticalGearNamedBuild(int kind, const char *name)
{
    if (!name || !name[0] || !strcmp(name, "-")) return -1;
    for (int i = 0; i < s_issuedCount; ++i)
        if (s_issuedBuilds[i].key == kind && !strcmp(s_issuedBuilds[i].name, name)) return ISSUED_BUILD_BASE + i;
    GearBuild *build = getBuild(kind);
    const GearPreset *preset = presetNamed(build, name);
    if (!preset || s_issuedCount == MAX_ISSUED_BUILDS) return -1;
    GearBuild trial = *build;
    if (!applyPreset(&trial, preset, 1)) return -1;
    struct IssuedBuild *issued = &s_issuedBuilds[s_issuedCount];
    issued->key = kind;
    strcpy(issued->name, preset->name);
    issued->assembly = trial.view;
    return ISSUED_BUILD_BASE + s_issuedCount++;
}

static const GevrGearAssembly *issuedAssembly(int build)
{
    const int i = build - ISSUED_BUILD_BASE;
    return i >= 0 && i < s_issuedCount ? &s_issuedBuilds[i].assembly : NULL;
}

enum { REMOTE_BUILD_BASE = ISSUED_BUILD_BASE + MAX_ISSUED_BUILDS, MAX_REMOTE_BUILDS = 192 };

typedef struct RemoteBuild {
    GevrGearSelection selection;
    GevrGearAssembly assembly;
} RemoteBuild;

static RemoteBuild s_remoteBuilds[MAX_REMOTE_BUILDS];
static int s_remoteBuildCount;

static const GevrGearAssembly *remoteAssembly(int build)
{
    const int index = build - REMOTE_BUILD_BASE;
    return index >= 0 && index < s_remoteBuildCount ? &s_remoteBuilds[index].assembly : NULL;
}

void gevrTacticalGearResetRemoteBuilds(void)
{
    s_remoteBuildCount = 0;
}

void gevrTacticalGearPackSelection(int build, int stock, GevrGearSelection *selection)
{
    *selection = (GevrGearSelection){0};
    const GevrGearAssembly *assembly = stock ? gevrTacticalGearStockAssembly(build)
        : gevrTacticalGearDeployedAssembly(build);
    if (!assembly || assembly->count < 2) return;
    selection->key = gevrTacticalGearBaseBuild(build);
    selection->count = assembly->count - 1;
    for (int i = 0; i < selection->count; ++i)
        selection->parts[i] = assembly->mounts[i + 1].part + 1;
}

int gevrTacticalGearReceiveSelection(const GevrGearSelection *selection)
{
    if (!selection->count || selection->count >= GEVR_GEAR_MOUNTS) return -1;
    for (int i = 0; i < s_remoteBuildCount; ++i) {
        const GevrGearSelection *known = &s_remoteBuilds[i].selection;
        if (known->key == selection->key && known->count == selection->count
            && !memcmp(known->parts, selection->parts, selection->count * sizeof(known->parts[0])))
            return REMOTE_BUILD_BASE + i;
    }

    const GearBuild *base = getBuild(selection->key);
    if (!base || s_remoteBuildCount == MAX_REMOTE_BUILDS) return -1;
    GearBuild trial;
    trial.root = base->root;
    GearReadSelection remote = {selection, 0};
    buildView(&trial, NULL, -1, &remote);
    if (remote.failed || trial.view.count != selection->count + 1) return -1;

    RemoteBuild *received = &s_remoteBuilds[s_remoteBuildCount];
    received->selection = *selection;
    received->assembly = trial.view;
    return REMOTE_BUILD_BASE + s_remoteBuildCount++;
}

int gevrTacticalGearBaseBuild(int build)
{
    if (remoteAssembly(build)) return s_remoteBuilds[build - REMOTE_BUILD_BASE].selection.key;
    if (build == GEVR_GEAR_JANUS_M4) return gevrTacticalGearKey(TACTICAL_GUN_M4A1);
    if (issuedAssembly(build)) return s_issuedBuilds[build - ISSUED_BUILD_BASE].key;
    return build >= 0 && build < GEVR_GEAR_BUILD_COUNT ? build : -1;
}

void gevrTacticalGearResetIssuedBuilds(void)
{
    s_issuedCount = 0;
    if (!s_issuedLaunch) return;
    s_launchStage = s_deployedBuild[0] = s_deployedBuild[1] = -1;
    s_issuedLaunch = 0;
}

static const GevrGearAssembly *gevrJanusM4(void)
{
    static GevrGearAssembly assembly;
    static int checked;
    if (!checked) {
        checked = 1;
        const GearBuild *m4 = getBuild(gevrTacticalGearKey(TACTICAL_GUN_M4A1));
        if (m4 && loadPresets()) {
            for (int i = 0; i < s_presetCount; ++i) {
                const GearPreset *preset = &s_presets[i];
                if (!preset->bundled || strcmp(preset->name, "Black Tape")
                    || strcmp(preset->root, s_parts[m4->root].id)) continue;
                GearPreset issued = *preset;
                /* The preset's ZipLine shrouds the suppressor. Keep the
                 * shorter MCMR on the guard's copy of the assembly. */
                for (int choice = 0; choice < issued.count; ++choice) {
                    if (strcmp(issued.choices[choice].path, "mod_reciever/mod_handguard")) continue;
                    strcpy(issued.choices[choice].part, "672e2e75bd4a44818c26b1b0");
                    break;
                }
                GearBuild trial = *m4;
                if (applyPreset(&trial, &issued, 1) || applyPreset(&trial, preset, 1)) {
                    assembly = trial.view;
                    break;
                }
            }
        }
    }
    return assembly.count ? &assembly : NULL;
}

int gevrTacticalGearPresetCount(int kind)
{
    GearBuild *build = getBuild(kind);
    if (!build || !loadPresets()) return 0;
    int count = 0;
    for (int i = 0; i < s_presetCount; ++i) count += presetAvailable(build, &s_presets[i]);
    return count;
}

const char *gevrTacticalGearPresetName(int kind, int index)
{
    const GearPreset *preset = presetAt(getBuild(kind), index);
    return preset ? preset->name : NULL;
}

int gevrTacticalGearLoadPreset(int kind, int index)
{
    GearBuild *build = getBuild(kind);
    const GearPreset *p = presetAt(build, index);
    if (!p || !applyPreset(build, p, 1)) return 0;
    sysLogPrintf(LOG_NOTE, "workshop: loaded preset %s for %s", p->name, s_parts[build->root].name);
    return 1;
}

static int buildPreset(int kind, const char *name, GearPreset *preset)
{
    GearBuild *build = getBuild(kind);
    if (!build || !presetNameValid(name)) return 0;
    *preset = (GearPreset){.count = build->changesCount};
    strcpy(preset->root, s_parts[build->root].id);
    strcpy(preset->name, name);
    for (int i = 0; i < preset->count; ++i) {
        const GearChoice *change = &build->changes[i];
        strcpy(preset->choices[i].path, change->path);
        strcpy(preset->choices[i].part, change->part < 0 ? "-" : s_parts[change->part].id);
    }
    return 1;
}

static int removePreset(const GearPreset *preset)
{
    /* Record the bundled removal first. If the personal copy cannot be
     * written, its existing override remains visible and deletion can retry. */
    for (int i = s_userPresetCount; i < s_presetCount; ++i) {
        if (s_presets[i].removed || !samePresetName(preset, &s_presets[i])) continue;
        if (!writeRemovedPreset(preset)) return 0;
        break;
    }
    int kept = 0;
    for (int i = 0; i < s_userPresetCount; ++i) {
        if (samePresetName(preset, &s_presets[i])) continue;
        if (i != kept) s_presets[kept] = s_presets[i];
        ++kept;
    }
    if (kept == s_userPresetCount) return 1;
    const int bundledCount = s_presetCount - s_userPresetCount;
    memmove(s_presets + kept, s_presets + s_userPresetCount, bundledCount * sizeof(*s_presets));
    s_userPresetCount = kept;
    s_presetCount = kept + bundledCount;
    return writePresets();
}

static int storePreset(int kind, const GearPreset *preset, uint64_t revision, bool deleting)
{
    gevrTacticalGearRefreshPresets();
    if (!loadPresets()) return 0;
    if (deleting) {
        const GearPreset *visible = presetNamed(getBuild(kind), preset->name);
        if (!visible || presetRevision(visible) != revision) return GEVR_PRESET_CHANGED;
        const int removed = removePreset(preset);
        gevrTacticalGearRefreshPresets();
        if (!removed) return 0;
        sysLogPrintf(LOG_NOTE, "workshop: deleted preset %s", preset->name);
        return GEVR_PRESET_SAVED;
    }
    int index = 0;
    while (index < s_presetCount && (s_presets[index].removed || !samePresetName(preset, &s_presets[index]))) ++index;
    if (revision) {
        if (index == s_presetCount || presetRevision(&s_presets[index]) != revision)
            return GEVR_PRESET_CHANGED;
    } else if (index < s_presetCount) {
        return GEVR_PRESET_DUPLICATE;
    }

    if (index >= s_userPresetCount) {
        if (s_userPresetCount == MAX_USER_PRESETS) return GEVR_PRESET_FULL;
        /* A customized bundled build lives in the user file under the same name. */
        index = s_userPresetCount;
        const int bundledCount = s_presetCount - s_userPresetCount;
        if (bundledCount)
            memmove(s_presets + index + 1, s_presets + index, bundledCount * sizeof(*s_presets));
        ++s_userPresetCount;
        ++s_presetCount;
    }
    s_presets[index] = *preset;
    if (!writePresets()) {
        gevrTacticalGearRefreshPresets();
        return 0;
    }
    sysLogPrintf(LOG_NOTE, "workshop: %s preset %s", revision ? "updated" : "saved", preset->name);
    return GEVR_PRESET_SAVED;
}

static int storePresetExclusive(int kind, const GearPreset *preset, uint64_t revision, bool deleting)
{
#ifdef _WIN32
    HANDLE lock = CreateFileA("tactical-presets.lock", GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (lock == INVALID_HANDLE_VALUE) return 0;
#endif
    const int result = storePreset(kind, preset, revision, deleting);
#ifdef _WIN32
    CloseHandle(lock);
#endif
    return result;
}

int gevrTacticalGearSavePreset(int kind, const char *name)
{
    GearPreset preset;
    return buildPreset(kind, name, &preset) ? storePresetExclusive(kind, &preset, 0, false) : 0;
}

int gevrTacticalGearUpdatePreset(int kind, const char *name, uint64_t revision)
{
    GearPreset preset;
    if (!revision || !buildPreset(kind, name, &preset)) return 0;
    return storePresetExclusive(kind, &preset, revision, false);
}

int gevrTacticalGearDeletePreset(int kind, const char *name, uint64_t revision)
{
    GearBuild *build = getBuild(kind);
    if (!revision || !build || !presetNameValid(name)) return 0;
    GearPreset preset = {0};
    strcpy(preset.root, s_parts[build->root].id);
    strcpy(preset.name, name);
    return storePresetExclusive(kind, &preset, revision, true);
}

int gevrTacticalGearExportPreset(int kind, const char *name, FILE *file)
{
    GearPreset preset;
    if (!file || !buildPreset(kind, name, &preset)) return 0;
    fputs("GEVRPRESETS1\n", file);
    writePreset(file, &preset);
    return !ferror(file);
}

int gevrTacticalGearImportPreset(int kind, FILE *file)
{
    GearBuild *build = getBuild(kind);
    GearPreset preset = {0};
    char line[256];
    if (!file || !build || !readPresetHeader(file) || !fgets(line, sizeof(line), file)
        || !readPreset(file, line, &preset) || fgets(line, sizeof(line), file) || ferror(file)
        || !presetMatches(build, &preset)) return 0;
    const int result = storePresetExclusive(kind, &preset, 0, false);
    if (result == GEVR_PRESET_SAVED) applyPreset(build, &preset, 1);
    return result;
}

void gevrTacticalGearCommit(int stage, int primary, int secondary, int primaryBuild, int secondaryBuild)
{
    s_issuedLaunch = 0;
    s_melee = GEVR_GEAR_MELEE_DEFAULT;
    const int keys[] = {primaryBuild, secondaryBuild};
    for (int slot = 0; slot < 2; ++slot) {
        GearBuild *build = getBuild(keys[slot]);
        s_deployedBuild[slot] = -1;
        if (build && build->changesCount > 0) {
            s_deployed[slot] = *gevrTacticalGearAssembly(keys[slot]);
            s_deployedBuild[slot] = keys[slot];
        }
    }
    s_launchStage = stage;
    s_primary = primary;
    s_secondary = secondary;
    sysLogPrintf(LOG_NOTE, "tactical gear: deploy stage %d primary %d secondary %d", stage, primary, secondary);
}

int gevrTacticalGearDeployedBuild(int secondary) { return s_deployedBuild[!!secondary]; }
void gevrTacticalGearBindGuestStage(int titleStage, int stage)
{
    if (s_launchStage != titleStage || stage == titleStage) return;
    s_launchStage = stage;
    sysLogPrintf(LOG_NOTE, "tactical co-op kit: deployed guest kit stage %d primary %d secondary %d",
        stage, s_primary, s_secondary);
}

void gevrTacticalGearSetMelee(int item) { s_melee = item; }
int gevrTacticalGearMelee(int stage) { return stage == s_launchStage ? s_melee : GEVR_GEAR_MELEE_DEFAULT; }

void gevrTacticalGearCommitIssued(int stage, int primary, int secondary, int primaryBuild, int secondaryBuild)
{
    s_issuedLaunch = 1;
    s_melee = GEVR_GEAR_MELEE_DEFAULT;
    s_deployedBuild[0] = primaryBuild;
    s_deployedBuild[1] = secondaryBuild;
    s_launchStage = stage;
    s_primary = primary;
    s_secondary = secondary;
}

int gevrTacticalGearLoadout(int stage, int *primary, int *secondary)
{
    if (stage != s_launchStage || !gevrTacticalGearEnabled()) return 0;
    *primary = s_primary;
    *secondary = s_secondary;
    return 1;
}
