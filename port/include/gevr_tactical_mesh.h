#ifndef GEVR_TACTICAL_MESH_H
#define GEVR_TACTICAL_MESH_H
#include <PR/ultratypes.h>
#ifdef __cplusplus
extern "C" {
#endif
struct ModelFileHeader;
struct ModelNode;
struct Model;
struct TacticalMagazine;
int gevrTacticalCampaignRules(void);
int gevrTacticalLocalPlayer(void);
int gevrTacticalItem(int item);
int gevrTacticalWeaponSuppressed(int item, s32 hand);
int gevrTacticalSkorpionEnabled(void);
int gevrTacticalSvdEnabled(void);
int gevrTacticalRpgEnabled(void);
int gevrTacticalKnifeEnabled(void);
int gevrTacticalKatanaEnabled(void);
void *gevrTacticalBladePreview(int item, float low[3], float high[3]);
void *gevrTacticalKatanaScabbard(void);
void *gevrTacticalKnifeSheathList(void);
int gevrTacticalRpgHasRound(const struct Model *model);
int gevrTacticalUnmirroredItem(int item);
void gevrTacticalMeshLoaded(struct ModelFileHeader *header, const char *name);
int gevrTacticalMeshNode(struct ModelNode *node, struct Model *model, void **gdl);
int gevrTacticalMeshTexture(const void *addr, const u8 **rgba, int *w, int *h);
/* The detached magazine list omits a matrix; its caller sets the hand matrix. */
void *gevrTacticalHeldMagList(int asset);
/* Bounds of the exact detached magazine mesh, in its unscaled model units. */
int gevrTacticalHeldMagBounds(int asset, float low[3], float high[3]);
void *gevrTacticalHandMagList(int asset);
void *gevrTacticalDrawMagazineRounds(void *gdl, int asset, unsigned rounds, unsigned displayCapacity,
    int inHand, unsigned part);
void *gevrTacticalMountedGunList(int asset, int suppressed, int withMagazine);
void *gevrTacticalMountedGearList(int kind, int build, int suppressed, const struct TacticalMagazine *mag);
int gevrTacticalMountedGunBounds(int asset, int suppressed, float low[3], float high[3]);
void *gevrTacticalGearPartList(int part, float low[3], float high[3]);
void *gevrTacticalGearLooseMagList(unsigned int part, int kind);
int gevrTacticalLooseMagBounds(unsigned int part, int kind, float low[3], float high[3]);
struct GevrGearAssembly;
int gevrTacticalMeshMuzzleFrame(const struct Model *model, int item, float out[4][4],
    const struct GevrGearAssembly **assembly);
void gevrTacticalFitGuardWeapon(struct Model *model, int item);
int gevrTacticalGuardSupportContact(const struct Model *model, int item, float out[3]);
void gevrTacticalGuardAccessories(const struct Model *model, int item, int room);
void gevrTacticalPlayerAccessories(const struct GevrGearAssembly *assembly, int hand, int kind,
    const float weapon[4][4], int room);
int gevrTacticalAccessoryTap(const struct GevrGearAssembly *assembly, int hand, int kind,
    const float weapon[4][4], const float finger[3], float reach2);
int gevrTacticalGearMagazineContact(int kind, const struct GevrGearAssembly *assembly, unsigned int part, float out[3]);
typedef enum GevrTacticalVest {
    GEVR_VEST_NONE,
#define TACTICAL_VEST(id, file, label) GEVR_VEST_##id,
#include "gevr_tactical_vests.inc.h"
#undef TACTICAL_VEST
    GEVR_VEST_COUNT
} GevrTacticalVest;

/* Resolve the active guard vest without rewriting older preset files. */
static inline int gevrTacticalGuardVest(int vest)
{
    switch (vest) {
    case GEVR_VEST_KIRASA_BLUE:
    case GEVR_VEST_KIRASA_SLATE:
    case GEVR_VEST_KIRASA_KAMYSH:
        return GEVR_VEST_GZHEL_GREY;
    default:
        return vest;
    }
}
typedef enum GevrTacticalHelmet {
#define TACTICAL_HELMET(id, file, label) GEVR_HELMET_##id,
#include "gevr_tactical_helmets.inc.h"
#undef TACTICAL_HELMET
    GEVR_HELMET_COUNT
} GevrTacticalHelmet;
int gevrTacticalGearCacheEnabled(void);
void gevrTacticalCacheBegin(void);
void gevrTacticalCacheEnd(int stage, u64 begin, int actors);
void gevrTacticalCacheVest(GevrTacticalVest vest, int body);
void gevrTacticalCacheModel(const struct Model *model, int kind);
void gevrTacticalCachePlayerWeapon(int kind, int buildId, int suppressed);
void gevrTacticalCachePlayerHands(void);
void *gevrTacticalVestRenderList(GevrTacticalVest vest, int body, int lowDetail);
void *gevrTacticalVestArmList(GevrTacticalVest vest, int side, int lowDetail);
const char *gevrTacticalVestName(GevrTacticalVest vest);
float gevrTacticalVestRigFront(GevrTacticalVest vest, int body, int pouch);
float gevrTacticalVestRaycast(GevrTacticalVest vest, int body,
    const float origin[3], const float direction[3], float limit);
float gevrTacticalVestArmRaycast(GevrTacticalVest vest, int side,
    const float origin[3], const float direction[3], float limit);
int gevrTacticalHelmetAvailable(GevrTacticalHelmet helmet);
int gevrTacticalHelmetKind(const struct Model *model);
int gevrTacticalHelmetModel(const struct Model *model);
int gevrTacticalHelmetAttach(struct Model *model, const void *hat, GevrTacticalHelmet helmet);
void gevrTacticalHelmetForget(const void *hat);
void gevrTacticalHelmetResetAttachments(void);
float gevrTacticalHelmetRaycast(const struct Model *model, const float origin[3], const float direction[3], float limit);
void *gevrTacticalHandList(int pose);
void *gevrTacticalPistolSupportList(void);
void *gevrTacticalMakarovSupportList(void);
void *gevrTacticalPistolMagazineHandList(void);
void *gevrTacticalPistolOpenHandList(void);
void *gevrTacticalPinchHandList(void);
void *gevrTacticalPistolFiringHandList(int gun, int trigger, int mirrored);
typedef enum TacticalGlovePose {
    TACTICAL_GLOVE_READY,
    TACTICAL_GLOVE_TRIGGER,
    TACTICAL_GLOVE_SUPPORT,
    TACTICAL_GLOVE_OPEN,
    TACTICAL_GLOVE_PINCH,
    TACTICAL_GLOVE_POSE_COUNT
} TacticalGlovePose;
typedef struct GevrTacticalWatchMount {
    float along[3], normal[3], centre[3];
} GevrTacticalWatchMount;
int gevrTacticalGloveWatchMount(int gun, TacticalGlovePose pose, int mirrored, GevrTacticalWatchMount *out);
int gevrTacticalPistolWatchMount(struct Model *model, GevrTacticalWatchMount *out);
int gevrTacticalOriginalWatchReady(void);
int gevrTacticalMagnifierFlick(int kind, const float gun[4][4], const float tip[3], float cm, unsigned tick);
/* Matrix-free pose. The watch has its own material so it can follow the left wrist. */
void *gevrTacticalFittedGloveList(int gun, TacticalGlovePose pose, int mirrored, int watch);
void *gevrTacticalAkHandList(int pose);
void *gevrTacticalAkSupportHandList(void);
void *gevrTacticalAkFreeHandList(void);
void *gevrTacticalAkOpenHandList(void);
#ifdef __cplusplus
}
#endif
#endif
