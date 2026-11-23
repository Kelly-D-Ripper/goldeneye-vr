#ifndef GEVR_RAGDOLL_H
#define GEVR_RAGDOLL_H
#include <ultra64.h>
#ifdef __cplusplus
extern "C" {
#endif
struct ChrRecord;
struct Model;
struct ShotData;
struct GevrCoopCorpse;

/* Cosmetic snapshots own their runtime model data and survive actor reuse.
 * Never change authoritative health, position, hitboxes or network messages. */
void gevrRagdollReset(void);
void gevrRagdollTick(int ticks60);
int gevrRagdollNeedsPose(struct ChrRecord *chr);
void gevrRagdollCapture(struct ChrRecord *chr);
void gevrRagdollCapturePlayer(struct ChrRecord *chr);
int gevrRagdollSuppress(struct ChrRecord *chr);
int gevrRagdollHidesHead(const struct Model *model);
void gevrRagdollForgetChr(struct ChrRecord *chr);
void gevrRagdollNoteHit(struct ChrRecord *chr, int matrix, int hitpart, const float dir[3], int weapon);
void gevrRagdollNoteShotDeath(struct ChrRecord *chr, int hitpart, int firearm);
void gevrRagdollPrepareCoopCorpse(struct ChrRecord *chr, const struct GevrCoopCorpse *corpse);
int gevrRagdollApplyCoopCuts(unsigned id, unsigned cuts);
int gevrRagdollReceiveCoopPose(unsigned id, unsigned sequence, int count,
    const float (*joints)[16], int final);
int gevrBladeHitRegion(struct ChrRecord *chr, const float from[3], const float to[3],
    float reach, float contact[3]);
void gevrRagdollNoteKatanaDeath(struct ChrRecord *chr, int part, const float direction[3]);
void gevrRagdollNoteRemoteStab(struct ChrRecord *chr, int weapon, int part, const float direction[3]);
int gevrRagdollBladePose(int controller, int item, const float base[3], const float tip[3]);
void gevrRagdollReleaseBlade(int controller);
void gevrRagdollNoteBladeStab(struct ChrRecord *chr, int controller, int weapon, int part,
    const float direction[3], const float contact[3]);
void gevrRagdollShot(struct ShotData *shot);
void gevrRagdollBloodStain(int handle, int part, const float point[3], const float dir[3]);
void gevrRagdollVolley(int begin);
struct coord3d;
/* -1 ends the scan, 0 means no guard body in this slot, 1 returns its torso. */
int gevrRagdollGuardBody(int slot, int *actorId, struct coord3d *point);
int gevrRagdollHasBody(struct ChrRecord *chr);
void gevrRagdollExplosionChr(struct ChrRecord *chr, const struct coord3d *centre, float radius, float damage);
void gevrRagdollBlast(const struct coord3d *centre, float radius, float damage);
Gfx *gevrRagdollRender(Gfx *gdl);

/* Bean's cut triangles are posed on the CPU. Map cross-cut influences into
 * their triangle's chosen piece at the captured pose, so skin cannot stretch. */
int gevrRagdollModelCut(const struct Model *model);
unsigned gevrRagdollModelBody(const struct Model *model);
int gevrRagdollPiece(const struct Model *model, int matrix);
int gevrRagdollRemap(const struct Model *model, int owner, int matrix, float p[3], float n[3]);
void gevrRagdollBeanCap(const struct Model *model, int matrix, const s16 (*rim)[2][3], int nedges, int piece, int other);
#ifdef __cplusplus
}
#endif
#endif
