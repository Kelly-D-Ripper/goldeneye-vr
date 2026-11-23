#ifndef GEVR_TACTICAL_GEAR_H
#define GEVR_TACTICAL_GEAR_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gevr_tactical_session.h"

enum { GEVR_GEAR_MOUNTS = 96, GEVR_GEAR_PATH = 160, GEVR_GEAR_PARTS = 2048 };
enum { GEVR_GEAR_PPK_SUPPRESSED = TACTICAL_GUN_UZI, GEVR_GEAR_BUILD_COUNT = TACTICAL_GUN_COUNT + 1 };
enum { GEVR_GEAR_RPG = TACTICAL_GUN_RPG + 1, GEVR_GEAR_JANUS_M4 = GEVR_GEAR_BUILD_COUNT };
static inline int gevrTacticalGearKey(int gun)
{
    return gun >= TACTICAL_GUN_UZI ? gun + 1 : gun;
}
enum { GEVR_GEAR_PRESET_NAME = 24 };
enum {
    GEVR_GEAR_ART_MESH = 1, GEVR_GEAR_ART_SUPPRESSOR = 2, GEVR_GEAR_ART_CARRY_HANDLE = 4,
    GEVR_GEAR_ART_SCOPE_MOUNT = 8, GEVR_GEAR_COLOUR_SHIFT = 4, GEVR_GEAR_COLOUR_MASK = 7,
    GEVR_GEAR_ART_LEGACY_CHOICE = 128, GEVR_GEAR_ART_OPTIC = 256,
    GEVR_GEAR_ART_MUZZLE_ADAPTER = 512, GEVR_GEAR_ART_LASER = 1024, GEVR_GEAR_ART_LIGHT = 2048
};
enum { GEVR_GEAR_BLACK, GEVR_GEAR_TAN, GEVR_GEAR_GREEN, GEVR_GEAR_RED, GEVR_GEAR_OTHER, GEVR_GEAR_COLOURS };
enum {
    GEVR_PRESET_SAVED = 1, GEVR_PRESET_DUPLICATE = -1,
    GEVR_PRESET_FULL = -2, GEVR_PRESET_CHANGED = -3
};

#define GEVR_GEAR_FINISH_SLOT "mod_finish"
#define GEVR_GEAR_VARIANT_SLOT "mod_variant"
enum { GEVR_MUZZLE_SUPPRESSOR = 1, GEVR_MUZZLE_BRAKE = 2, GEVR_MUZZLE_ADAPTER = 4 };

static inline int gevrTacticalGearIsRootChoice(const char *path)
{
    return !strcmp(path, GEVR_GEAR_FINISH_SLOT) || !strcmp(path, GEVR_GEAR_VARIANT_SLOT);
}

enum { GEVR_GEAR_SOCKET_AUTO = 2 };

typedef struct GevrGearFit {
    int option;
    float frame[4][4];
} GevrGearFit;

typedef struct GevrGearSocket {
    char key[33], label[32];
    int required, count;
    int *allowed;
    float frame[4][4];
    int offsetFrom;           /* Optional alternate origin for later choices on a shared rail. */
    float alternateOffset[3];
    int fitCount;
    GevrGearFit *fits;
} GevrGearSocket;

typedef struct GevrGearPart {
    char id[25], name[90];
    int capacity, art, socketCount, conflictCount;
    GevrGearSocket *sockets;
    int *conflicts;
} GevrGearPart;

typedef struct GevrGearMount {
    const GevrGearSocket *socket;
    int part;
    char path[GEVR_GEAR_PATH];
    float frame[4][4];
} GevrGearMount;

typedef struct GevrGearAssembly {
    int count;
    unsigned revision;
    GevrGearMount mounts[GEVR_GEAR_MOUNTS];
} GevrGearAssembly;

typedef struct GevrGearSelection {
    uint8_t key, count;
    uint16_t parts[GEVR_GEAR_MOUNTS - 1];  /* One-based catalog index, zero is empty. */
} GevrGearSelection;

uint64_t gevrTacticalGearCatalogHash(void);
void gevrTacticalGearPackSelection(int build, int stock, GevrGearSelection *selection);
int gevrTacticalGearReceiveSelection(const GevrGearSelection *selection);
void gevrTacticalGearResetRemoteBuilds(void);

int gevrTacticalGearEnabled(void);
int gevrTacticalExpandedEnabled(void);
int gevrTacticalArEnabled(void);
const GevrGearPart *gevrTacticalGearPart(int part);
int gevrTacticalGearBuildAvailable(int kind);
int gevrTacticalMuzzleCategory(int part);
int gevrTacticalMuzzleIsSuppressor(int part);
int gevrTacticalGearAssemblySuppressed(const GevrGearAssembly *assembly);
float gevrTacticalGearFlashScale(const GevrGearAssembly *assembly);
int gevrTacticalGearMuzzlePoint(int kind, const GevrGearAssembly *assembly, float out[3]);
const GevrGearAssembly *gevrTacticalGearAssembly(int kind);
const GevrGearAssembly *gevrTacticalGearDeployedAssembly(int kind);
const GevrGearAssembly *gevrTacticalGearStockAssembly(int kind);
const GevrGearMount *gevrTacticalGearFindMount(const GevrGearAssembly *assembly, const char *path);
int gevrTacticalGearMountPart(int kind, int mount);
int gevrTacticalGearOptions(int kind, int mount, int options[GEVR_GEAR_PARTS + 1]);
int gevrTacticalGearCanInstall(int kind, int mount, int part, int *conflict);
int gevrTacticalGearPreviewVariant(int kind, int mount, int part, GevrGearAssembly *out);
int gevrTacticalGearInstall(int kind, int mount, int part, int *conflict);
void gevrTacticalGearReset(int kind);
/* Use an explicit policy so the authoring editor and NPC presets remain unrestricted. */
int gevrTacticalGearSanitize(int kind, int (*allowed)(int, const GevrGearMount *));
void gevrTacticalGearSanitizeDeployed(int (*allowed)(int, const GevrGearMount *));
int gevrTacticalGearPresetCount(int kind);
const char *gevrTacticalGearPresetName(int kind, int index);
uint64_t gevrTacticalGearPresetRevision(int kind, const char *name);
int gevrTacticalGearDeletePreset(int kind, const char *name, uint64_t revision);
int gevrTacticalGearSavePreset(int kind, const char *name);
int gevrTacticalGearUpdatePreset(int kind, const char *name, uint64_t revision);
int gevrTacticalGearLoadPreset(int kind, int index);
void gevrTacticalGearRefreshPresets(void);
int gevrTacticalGearLoadNamedPreset(int kind, const char *name);
int gevrTacticalGearExportPreset(int kind, const char *name, FILE *file);
int gevrTacticalGearImportPreset(int kind, FILE *file);
int gevrTacticalGearNamedBuild(int kind, const char *name);
int gevrTacticalGearBaseBuild(int build);
void gevrTacticalGearResetIssuedBuilds(void);
void gevrTacticalGearCommit(int stage, int primary, int secondary, int primaryBuild, int secondaryBuild);
void gevrTacticalGearBindGuestStage(int titleStage, int stage);
enum { GEVR_GEAR_MELEE_DEFAULT = -2 };
void gevrTacticalGearSetMelee(int item);
int gevrTacticalGearMelee(int stage);
void gevrTacticalGearCommitIssued(int stage, int primary, int secondary, int primaryBuild, int secondaryBuild);
int gevrTacticalGearDeployedBuild(int secondary);
int gevrTacticalGearLoadout(int stage, int *primary, int *secondary);

#endif
