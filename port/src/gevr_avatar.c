/* Remote VR avatar adapter. Arm geometry: PD CE's two-bone clutch (MIT);
 * independent torso following: Alex-LeTux's perfect_dark_VR (MIT).
 * See docs/multiplayer-ik.md and port/licenses/{perfect-dark-ce,perfect-dark-vr}.txt. */
#include <stdbool.h>
#include "gevr_avatar.h"
#include "gevr_guard_cigarettes.h"
#include "gevr_guard_cover.h"
#include "chr.h"
#include "lv.h"
#include "gevr_tactical_mesh.h"
#include "gevr_tactical_net.h"
#include "net/net_core.h"
#include "model.h"
#include "game/prop.h"
#include "objecthandler.h"
#include "player.h"
#include "bondview.h"
#include "matrixmath.h"
#include "dyn.h"
#include "system.h"
#include <math.h>
#include <string.h>

extern int g_gevrStereo, VrLeftHandedMode;
extern s32 g_gevrExtraPass; /* lv.c: a copy's or barrel's view pass, never drawn */
extern int gevrVrReady(void), gevrVrGripTracked(int hand), gevrVrGripUsable(int hand);
extern int gevrVrSessionFocused(void);
extern int gevrVrGripPoseCamera(int hand,float pos[3],float q[4]);
extern s32 gevrStereoShotWorld(s32 hand,coord3d *origin,coord3d *dir);
extern coord3d *gevrEyePosition(void);

static float dot(const float *a,const float *b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static int unit(float *v) {float d=sqrtf(dot(v,v));if(!isfinite(d)||d<1e-6f)return 0;for(int k=0;k<3;k++)v[k]/=d;return 1;}
static void cross(const float *a,const float *b,float *c) {c[0]=a[1]*b[2]-a[2]*b[1];c[1]=a[2]*b[0]-a[0]*b[2];c[2]=a[0]*b[1]-a[1]*b[0];}
static void rotate(const float m[3][3],const float *v,float *o) {
    for(int k=0;k<3;k++)o[k]=v[0]*m[0][k]+v[1]*m[1][k]+v[2]*m[2][k];
}
static void basis(const Mtxf *m,float out[3][3]) {
    for(int i=0;i<3;i++) {memcpy(out[i],m->m[i],3*sizeof(float));unit(out[i]);}
}
static void product(const float a[3][3],const float b[3][3],float out[3][3]) {
    for(int i=0;i<3;i++)rotate(a,b[i],out[i]);
}
static void worldMatrix(Model *m,ModelNode *node,Mtxf *out) {
    matrix_4x4_multiply(currentPlayerGetViewToWorldMtxf(),modelFindNodeMtx(m,node,0),out);
}
static void aimBone(const float from[3],const float to[3],const float original[3][3],float out[3][3]);

void gevrAvatarCapture(GevrAvatarPose *p,struct player *pl) {
    static uint64_t last;
    static float yaw,anchor[3];
    memset(p,0,sizeof(*p));
    if(!g_gevrStereo || !gevrVrReady() || !gevrVrSessionFocused() || !pl || !pl->prop || pl->bonddead || pl->cameramode==1) {last=0;return;}
    Mtxf *v2w=currentPlayerGetViewToWorldMtxf();
    if(!v2w)return;
    float view[3][3];basis(v2w,view);
    gevrPoseMatrixQuat(view,p->head.q);
    coord3d *eye=gevrEyePosition();
    for(int k=0;k<3;k++)p->head.pos[k]=eye->f[k]-pl->prop->pos.f[k];
    p->flags=GEVR_POSE_HEAD | (VrLeftHandedMode?GEVR_POSE_LEFT_HANDED:0);
    uint64_t now=sysGetMicroseconds();float moved[3];
    for(int k=0;k<3;k++)moved[k]=pl->prop->pos.f[k]-anchor[k];
    if(!last || now<last || now-last>250000 || dot(moved,moved)>10000)yaw=pl->vv_theta;
    float delta=remainderf(pl->vv_theta-yaw,360),dt=last && now>=last?(now-last)*1e-6f:0;
    /* A glance leaves the torso behind; sustained turns and walking follow.
     * Limit the head/body separation to 65 degrees, including snap turns. */
    if(fabsf(delta)>35 || fabsf(pl->speedforwards)+fabsf(pl->speedsideways)>0.1f)
        yaw+=delta*(1-expf(-3.0f*fminf(dt,0.1f)));
    delta=remainderf(pl->vv_theta-yaw,360);
    if(fabsf(delta)>65)yaw=pl->vv_theta-copysignf(65,delta);
    p->body_yaw=remainderf(yaw,360);last=now;memcpy(anchor,pl->prop->pos.f,sizeof(anchor));
    for(int role=0;role<2;role++) {
        int ctrl=role==GUNRIGHT?1:0;
        float q[4],pos[3],grip[3][3],world[3][3];
        if(!gevrVrGripUsable(ctrl) || !gevrVrGripTracked(ctrl) || !gevrVrGripPoseCamera(ctrl,pos,q))continue;
        for(int k=0;k<3;k++)pos[k]*=100; /* view metres -> unscaled world cm */
        rotate(view,pos,p->hand[role].pos);
        gevrPoseQuatMatrix(q,grip);product(view,grip,world);
        /* The two-handed aim line can differ from the trigger wrist. Carry
         * the rendered barrel's turn into that wrist, retaining its roll. */
        coord3d origin,dir;
        if(pl->hands[role].weaponnum>ITEM_FIST && gevrStereoShotWorld(role,&origin,&dir)) {
            float forward[3],aligned[3][3];
            for(int k=0;k<3;k++)forward[k]=-world[1][k];
            aimBone(forward,dir.f,world,aligned);memcpy(world,aligned,sizeof(world));
        }
        gevrPoseMatrixQuat(world,p->hand[role].q);
        p->flags|=1u<<(role+1);
    }
    if(!gevrPoseValidate(p))memset(p,0,sizeof(*p));
}

/* Per-build overrides use node identity, not matrix numbers shared by other rigs. */
typedef struct AvatarBuild {
    Model *model;
    ModelNode *node[16];
    float rotation[16][4];
    unsigned mask;
    float offset[3]; /* chest translation, view space */
} AvatarBuild;
static AvatarBuild s_build;
typedef struct AvatarCollision {Model *model;RenderPosView *posed,*base;} AvatarCollision;
static AvatarCollision s_collision[GEVR_MAX_PLAYERS];
static AvatarCollision s_guardCollision[256];

static ModelNode *nextNode(ModelNode *n) {
    if(n->Child)return n->Child;
    while(n && !n->Next)n=n->Parent;
    return n?n->Next:NULL;
}
static int findRig(Model *m,ModelNode **nodes) {
    memset(nodes,0,16*sizeof(*nodes));int visits=0;
    for(ModelNode *n=m->obj->RootNode;n;n=nextNode(n)) {
        if(++visits>512)return 0;
        if((n->Opcode&255)!=MODELNODE_OPCODE_GROUP)continue;
        int j=n->Data->Group.JointID;
        if(j>0 && j<16 && !nodes[j]) {
            int index=n->Data->Group.MatrixID0;
            if(index<0 || index>=m->obj->numMatrices)return 0;
            nodes[j]=n;
        }
    }
    for(int j=1;j<=9;j++)if(!nodes[j])return 0;
    /* The guard rig's shoulders, elbows and wrists must form these chains. */
    for(int side=0;side<2;side++) {
        int sh=4+side,el=6+side,wr=8+side;
        if(nodes[el]->Parent!=nodes[sh] || nodes[wr]->Parent!=nodes[el])return 0;
    }
    return nodes[3]->Parent==nodes[2] && (m->obj->RootNode->Opcode&255)==MODELNODE_OPCODE_HEADER;
}
static void desired(int joint,const float m[3][3],const float original[3][3],float weight) {
    float q[4],base[4];gevrPoseMatrixQuat(m,q);gevrPoseMatrixQuat(original,base);
    gevrPoseQuatLerp(base,q,weight,s_build.rotation[joint]);s_build.mask|=1u<<joint;
}
static void aimBone(const float from[3],const float to[3],const float original[3][3],float out[3][3]) {
    float a[3],b[3],axis[3],q[4],r[3][3];memcpy(a,from,sizeof(a));memcpy(b,to,sizeof(b));
    if(!unit(a)||!unit(b)) {memcpy(out,original,9*sizeof(float));return;}
    cross(a,b,axis);float d=fmaxf(-1,fminf(1,dot(a,b)));
    if(d<-.9999f) {
        float fallback[3]={0,1,0};if(fabsf(a[1])>.9f) {fallback[0]=1;fallback[1]=0;}
        cross(a,fallback,axis);unit(axis);for(int k=0;k<3;k++)q[k]=axis[k];q[3]=0;
    } else {
        float n=sqrtf(2*(1+d));for(int k=0;k<3;k++)q[k]=axis[k]/n;q[3]=n*.5f;
    }
    gevrPoseQuatMatrix(q,r);product(r,original,out);
}

int gevrAvatarJointRotation(Model *model,ModelNode *node,coord3d *rotation) {
    if(s_build.model!=model)return 0;
    int j=node->Data->Group.JointID;
    if(j<1 || j>15 || s_build.node[j]!=node || !(s_build.mask&(1u<<j)))return 0;
    Mtxf parent,local;float pb[3][3],want[3][3];
    if(!node->Parent)return 0;
    worldMatrix(model,node->Parent,&parent);basis(&parent,pb);
    gevrPoseQuatMatrix(s_build.rotation[j],want);
    matrix_4x4_set_identity(&local);
    for(int i=0;i<3;i++)for(int k=0;k<3;k++)local.m[i][k]=dot(want[i],pb[k]);
    matrix_4x4_get_rotation_around_xyz(&local,rotation);
    return 1;
}
void gevrAvatarJointOffset(Model *model,ModelNode *node) {
    if(s_build.model!=model || node!=s_build.node[2])return;
    for(int j=0;j<3;j++) {
        if(j && !(node->Opcode&(j==1?0x100:0x200)))continue;
        int i=node->Data->Group.MatrixIDs[j];
        if(i>=0 && i<model->obj->numMatrices)
            for(int k=0;k<3;k++)model->render_pos[i].pos.m[3][k]+=s_build.offset[k];
    }
}

void gevrAvatarApply(ChrRecord *chr,ModelRenderData *data) {
    if(!chr || !chr->prop || chr->prop->type!=PROP_TYPE_VIEWER || !chr->model)return;
    int slot=getPlayerPointerIndex(chr->prop);
    if(slot<0 || slot>=GEVR_MAX_PLAYERS || !g_playerPointers[slot])return;
    s_collision[slot].model=NULL;
    /* Extra passes only place bodies for shot traces; skip the extra matrix
     * builds and dyn memory there (up to ~15 passes a frame with eight players). */
    if(g_gevrExtraPass)return;
    if(chr->actiontype==ACT_DIE || chr->actiontype==ACT_DEAD)return;
    GevrAvatarPose pose;float weight[3];
    if(!netGetAvatarPose(slot,&pose,weight) || !chr->model->anim)return;
    Model *m=chr->model;ModelNode *nodes[16];
    if(!findRig(m,nodes))return;
    /* Preserve the game's animated body hitboxes. Visual IK has no authority
     * over hit testing and no feedback into player movement or the shot rays. */
    RenderPosView *baseline=m->render_pos;
    m->render_pos=dynAllocate(m->obj->numMatrices*sizeof(*m->render_pos));
    if(!m->render_pos) {m->render_pos=baseline;return;}
    s_collision[slot]=(AvatarCollision){m,m->render_pos,baseline};
    ModelRwData_HeaderRecord *root=&modelGetNodeRwData(m,m->obj->RootNode)->Header;
    ModelRwData_HeaderRecord savedRoot=*root;
    float oldYaw=root->unk14;
    float bodyYaw=(360-pose.body_yaw)*(3.14159265358979323846f/180);
    float diff=remainderf(bodyYaw-oldYaw,6.283185307179586f);
    root->unk14=oldYaw+diff*weight[0];root->unk18=0;
    void (*jointCallback)(s32,Mtxf *)=g_ModelJointPositionedFunc;
    g_ModelJointPositionedFunc=NULL;
    modelUpdateMatrices(data,m); /* base walking rig at the independent torso yaw */
    memset(&s_build,0,sizeof(s_build));memcpy(s_build.node,nodes,sizeof(nodes));
    float shift[3];
    for(int k=0;k<3;k++)shift[k]=fmaxf(-35,fminf(35,pose.head.pos[k]))*weight[0];
    Mtxf *w2v=camGetWorldToScreenMtxf();
    for(int k=0;k<3;k++)s_build.offset[k]=shift[0]*w2v->m[0][k]+shift[1]*w2v->m[1][k]+shift[2]*w2v->m[2][k];

    Mtxf head;worldMatrix(m,nodes[3],&head);
    float hb[3][3],tracked[3][3],level[3][3],inv[3][3],delta[3][3],want[3][3];
    basis(&head,hb);gevrPoseQuatMatrix(pose.head.q,tracked);
    float yaw=-pose.body_yaw*(3.14159265358979323846f/180);
    /* Camera right/up/back for this torso heading (the game faces +Z at 0). */
    level[0][0]=-cosf(yaw);level[0][1]=0;level[0][2]=sinf(yaw);
    level[1][0]=0;level[1][1]=1;level[1][2]=0;
    level[2][0]=-sinf(yaw);level[2][1]=0;level[2][2]=-cosf(yaw);
    for(int i=0;i<3;i++)for(int k=0;k<3;k++)inv[i][k]=level[k][i];
    product(tracked,inv,delta);product(delta,hb,want);desired(3,want,hb,weight[0]);

    for(int side=0;side<2;side++) { /* 0 anatomical left, 1 anatomical right */
        int role=(side==0?GUNLEFT:GUNRIGHT)^((pose.flags&GEVR_POSE_LEFT_HANDED)?1:0);
        if(weight[role+1]<=0)continue;
        int sh=4+side,el=6+side,wr=8+side;
        Mtxf sm,em,wm;worldMatrix(m,nodes[sh],&sm);worldMatrix(m,nodes[el],&em);worldMatrix(m,nodes[wr],&wm);
        float a[3],b[3],s[3],target[3],pole[3],ep[3],wp[3],fore[3];
        for(int k=0;k<3;k++) {
            s[k]=sm.m[3][k]+shift[k];a[k]=em.m[3][k]-sm.m[3][k];b[k]=wm.m[3][k]-em.m[3][k];
            /* chrTick temporarily places the viewer prop at its animated
             * pelvis. Movement packets and head offsets use the eye position. */
            target[k]=g_playerPointers[slot]->field_488.collision_position.f[k]
                +pose.head.pos[k]+pose.hand[role].pos[k];
            pole[k]=level[0][k]*(side==0?-.6f:.6f)+level[2][k]*.3f;
        }
        pole[1]-=.6f;
        /* The runtime's grip is at the palm, roughly 4 cm ahead of the wrist. */
        float grip[3][3];gevrPoseQuatMatrix(pose.hand[role].q,grip);
        for(int k=0;k<3;k++)target[k]+=grip[1][k]*4;
        if(!gevrArmSolve(s,target,sqrtf(dot(a,a)),sqrtf(dot(b,b)),pole,a,ep,wp))continue;
        float orig[3][3],aim[3][3];
        for(int k=0;k<3;k++)fore[k]=ep[k]-s[k];
        basis(&sm,orig);aimBone(a,fore,orig,aim);desired(sh,aim,orig,weight[role+1]);
        for(int k=0;k<3;k++)fore[k]=wp[k]-ep[k];
        basis(&em,orig);aimBone(b,fore,orig,aim);desired(el,aim,orig,weight[role+1]);
        /* Guard hand/Pchr frame: barrel -X, up +Z. Native left wrist is
         * turned 180 about Z before attaching its weapon (propobj.c). */
        for(int k=0;k<3;k++) {want[0][k]=grip[1][k]*(side==0?-1:1);want[1][k]=grip[0][k]*(side==0?-1:1);want[2][k]=-grip[2][k];}
        basis(&wm,orig);desired(wr,want,orig,weight[role+1]);
    }
    s_build.model=m;
    modelUpdateMatrices(data,m); /* regenerates auxiliary bend matrices and all descendants */
    s_build.model=NULL;g_ModelJointPositionedFunc=jointCallback;*root=savedRoot;
}

void gevrAvatarGuardForget(ChrRecord *chr) {
    if (!chr || !chr->model) return;
    for (int i=0;i<256;i++) if (s_guardCollision[i].model==chr->model) s_guardCollision[i].model=NULL;
}
void gevrAvatarGuardReset(void) { memset(s_guardCollision,0,sizeof(s_guardCollision)); }

void gevrAvatarGuardApply(ChrRecord *chr, ModelRenderData *data) {
    ModelNode *nodes[16];
    Mtxf sm,em,wm,rm,gunWorld;
    float shoulder[3],a[3],b[3],target[3],pole[3],elbow[3],wrist[3],dir[3],orig[3][3],aim[3][3];
    int slot=-1;
    const bool online = netIsActive();
    if (!chr || !data || !chr->prop || chr->prop->type!=PROP_TYPE_CHR || !chr->model || !chr->model->anim
        || chr->actiontype==ACT_DIE || chr->actiontype==ACT_DEAD || g_gevrExtraPass
        || !sysArgCheck("--tactical-enemy-rigs") || !sysArgCheck("--tactical-mesh")
        || !currentPlayerGetViewToWorldMtxf()) return;
    const bool grouped = gevrCampaignGuardIsIdleGroupMember(chr);
    if (online && !grouped) return;
    if (chr->weapons_held[GUNLEFT]) {
        if (grouped) gevrGuardCigarettePose(chr, NULL);
        return;
    }
    const PropRecord *held = chr->weapons_held[GUNRIGHT];
    if (!held || !held->weapon) return;
    Model *gun = held->weapon->model;
    float contact[3];
    const bool supportGrip = (held->flags & PROPFLAG_ONSCREEN) && gun && gun->render_pos && gun->attachedto
        && gevrTacticalGuardSupportContact(gun, held->weapon->weaponnum, contact);
    if (!grouped && !supportGrip) return;
    Model *m = chr->model;
    if (!findRig(m,nodes)) {
        if (grouped) gevrGuardCigarettePose(chr, NULL);
        return;
    }
    worldMatrix(m,nodes[4],&sm); worldMatrix(m,nodes[6],&em); worldMatrix(m,nodes[8],&wm);
    if (supportGrip) matrix_4x4_multiply(currentPlayerGetViewToWorldMtxf(),&gun->render_pos[0].pos,&gunWorld);
    for(int k=0;k<3;k++) {
        shoulder[k]=sm.m[3][k]; a[k]=em.m[3][k]-shoulder[k]; b[k]=wm.m[3][k]-em.m[3][k];
        target[k]=supportGrip ? gunWorld.m[3][k]+contact[0]*gunWorld.m[0][k]
            +contact[1]*gunWorld.m[1][k]+contact[2]*gunWorld.m[2][k] : wm.m[3][k];
        pole[k]=a[k];
    }
    const float smoking = gevrGuardCigarettePose(chr, target);
    if ((online || !supportGrip) && smoking <= 0.f) return;
    if(!gevrArmSolve(shoulder,target,sqrtf(dot(a,a)),sqrtf(dot(b,b)),pole,a,elbow,wrist)) return;
    for (int i=0;i<256;i++) if (!s_guardCollision[i].model) {slot=i;break;}
    if(slot<0)return;
    memset(&s_build,0,sizeof(s_build)); memcpy(s_build.node,nodes,sizeof(nodes));
    for(int k=0;k<3;k++)dir[k]=elbow[k]-shoulder[k];
    basis(&sm,orig); aimBone(a,dir,orig,aim); desired(4,aim,orig,1.0f);
    for(int k=0;k<3;k++)dir[k]=wrist[k]-elbow[k];
    basis(&em,orig); aimBone(b,dir,orig,aim); desired(6,aim,orig,1.0f);
    /* Keep the support palm oriented along the weapon, with the left wrist's
     * 180-degree hand-frame convention used by native weapon attachments. */
    worldMatrix(m,nodes[9],&rm);
    basis(&rm,aim);
    for(int k=0;k<3;k++) {aim[0][k]=-aim[0][k];aim[1][k]=-aim[1][k];}
    if (smoking > 0.f) {
        float relaxed[3][3];
        basis(&wm,orig);
        aimBone(b,dir,orig,relaxed);
        desired(8,relaxed,aim,smoking);
    } else {
        basis(&wm,orig);
        desired(8,aim,orig,1.f);
    }
    RenderPosView *baseline=m->render_pos;
    RenderPosView *posed=dynAllocate(m->obj->numMatrices*sizeof(*posed));
    if(!posed)return;
    m->render_pos=posed;
    s_guardCollision[slot]=(AvatarCollision){m,posed,baseline};
    void (*callback)(s32,Mtxf *)=g_ModelJointPositionedFunc;
    g_ModelJointPositionedFunc=NULL; s_build.model=m;
    modelUpdateMatrices(data,m);
    s_build.model=NULL; g_ModelJointPositionedFunc=callback;
    if (smoking > 0.f) {
        Mtxf posedWrist;
        worldMatrix(m,nodes[8],&posedWrist);
        gevrGuardCigaretteGrip(chr,target,posedWrist.m[3]);
    }
    if (sysArgCheck("--input-script") && sysArgCheck("--tactical-grip-debug") && chr->chrnum>=5000) {
        static int n;
        if(n==0 || n==500 || n==1500) {
            Mtxf after; float pre[3],post[3];
            worldMatrix(m,nodes[8],&after);
            for(int k=0;k<3;k++) {pre[k]=target[k]-wm.m[3][k];post[k]=target[k]-after.m[3][k];}
            sysLogPrintf(LOG_NOTE,"guard support IK: item %d sample %d before %.1f after %.1f",
                held->weapon->weaponnum,n,(double)sqrtf(dot(pre,pre)),(double)sqrtf(dot(post,post)));
        }
        n++;
    }
}

/* A cover animation is relative to the native pelvis. Keeps feet planted
 * and moves the original attachment and hittable matrices together. */
#define GUARD_COVER_BLEND_TICKS 8

static float guardCoverProgress(const ChrRecord *chr)
{
    const int elapsed = g_GlobalTimer - chr->tacticalCover.started;
    const float t = fmaxf(0.f, fminf(1.f, elapsed / (float)GUARD_COVER_BLEND_TICKS));
    return t * t * (3.f - 2.f * t);
}

static void guardCoverEvaluate(const ChrRecord *chr, float offset[2], float *blind, float t)
{
    for (int i = 0; i < 2; ++i)
        offset[i] = chr->tacticalCover.from[i] + (chr->tacticalCover.target[i] - chr->tacticalCover.from[i]) * t;
    const float target = chr->tacticalCover.kind >= GEVR_GUARD_COVER_BLIND_LEFT ? 1.f : 0.f;
    *blind = chr->tacticalCover.blindFrom + (target - chr->tacticalCover.blindFrom) * t;
}

void gevrGuardCoverPoseSet(ChrRecord *chr, int pose, float offsetX, float offsetZ)
{
    if (!chr) return;
    if (pose < GEVR_GUARD_COVER_NONE || pose > GEVR_GUARD_COVER_BLIND_RIGHT
        || !isfinite(offsetX) || !isfinite(offsetZ)) pose = GEVR_GUARD_COVER_NONE;
    if (pose < GEVR_GUARD_COVER_LEAN_LEFT) offsetX = offsetZ = 0.f;
    const float scale = fmaxf(1.f, hypotf(offsetX, offsetZ) / GEVR_GUARD_COVER_MAX_OFFSET);
    const s8 target[2] = {(s8)lroundf(offsetX / scale), (s8)lroundf(offsetZ / scale)};
    if (chr->tacticalCover.kind == pose && !memcmp(chr->tacticalCover.target, target, sizeof(target))) return;
    const float progress = guardCoverProgress(chr);
    guardCoverEvaluate(chr, chr->tacticalCover.from, &chr->tacticalCover.blindFrom, progress);
    memcpy(chr->tacticalCover.target, target, sizeof(target));
    chr->tacticalCover.kind = pose;
    chr->tacticalCover.started = g_GlobalTimer;
    chr->tacticalCover.muzzleMask = 0;
}

int gevrGuardCoverPoseGet(const ChrRecord *chr, float offset[2], float *blend)
{
    if (offset) offset[0] = offset[1] = 0.f;
    if (blend) *blend = 1.f;
    if (!chr || chr->actiontype == ACT_DEAD || chr->actiontype == ACT_DIE) return GEVR_GUARD_COVER_NONE;
    const float progress = guardCoverProgress(chr);
    if (offset) {
        float blind;
        guardCoverEvaluate(chr, offset, &blind, progress);
    }
    if (blend) *blend = progress;
    return chr->tacticalCover.kind;
}

int gevrGuardCoverMuzzle(const ChrRecord *chr, int hand, coord3d *muzzle)
{
    if (!chr || !muzzle || (hand != GUNRIGHT && hand != GUNLEFT)
        || chr->actiontype == ACT_DEAD || chr->actiontype == ACT_DIE
        || chr->tacticalCover.kind < GEVR_GUARD_COVER_LEAN_LEFT || !(chr->tacticalCover.muzzleMask & (1u << hand))) return false;
    const int age = g_GlobalTimer - chr->tacticalCover.muzzleTick;
    if (age < 0 || age > 5 || guardCoverProgress(chr) < 1.f) return false;
    *muzzle = chr->tacticalCover.muzzle[hand];
    return true;
}

static void coverRotate(const float axis[3], const float v[3], float sine, float cosine, float out[3])
{
    float tangent[3];
    cross(axis, v, tangent);
    const float parallel = dot(axis, v) * (1.f - cosine);
    for (int k = 0; k < 3; ++k) out[k] = v[k] * cosine + tangent[k] * sine + axis[k] * parallel;
}

static void coverBendMatrices(Model *model, RenderPosView *matrices, ModelNode *nodes[16],
                              const float shift[3], const float up[3], const float axis[3], float distance, float blind)
{
    const int pivotIndex = nodes[2]->Data->Group.MatrixID0;
    float pivot[3];
    memcpy(pivot, matrices[pivotIndex].pos.m[3], sizeof(pivot));
    const float leanAngle = fminf(.31f, distance * .0046f);
    const float sine[2] = {sinf(leanAngle * (1.f - .83f * blind)), sinf(leanAngle)};
    const float cosine[2] = {cosf(leanAngle * (1.f - .83f * blind)), cosf(leanAngle)};
    const float reach[2] = {.72f * (1.f - .78f * blind), .72f};
    const float lower[2] = {-distance * (.10f + .14f * blind), -distance * .10f};
    /* Group nodes identify the auxiliary skinning matrices as well as the
     * main joint. The same original subtree also feeds helmets and rigs. */
    int visits = 0;
    for (ModelNode *node = nodes[2]; node && visits++ < 512; node = nextNode(node)) {
        int upper = 0, arm = 0;
        for (ModelNode *parent = node; parent; parent = parent->Parent) {
            if (parent == nodes[4] || parent == nodes[5]) arm = 1;
            if (parent == nodes[2]) { upper = 1; break; }
        }
        if (!upper) break;
        if ((node->Opcode & 255) != MODELNODE_OPCODE_GROUP) continue;
        for (int j = 0; j < 3; ++j) {
            if (j && !(node->Opcode & (j == 1 ? 0x100 : 0x200))) continue;
            const int index = node->Data->Group.MatrixIDs[j];
            if (index < 0 || index >= model->obj->numMatrices) continue;
            Mtxf *matrix = &matrices[index].pos;
            float relative[3], shifted[3];
            for (int k = 0; k < 3; ++k) relative[k] = matrix->m[3][k] - pivot[k];
            coverRotate(axis, relative, sine[arm], cosine[arm], shifted);
            for (int k = 0; k < 3; ++k)
                matrix->m[3][k] = pivot[k] + shifted[k] + reach[arm] * shift[k] + lower[arm] * up[k];
            for (int j = 0; j < 3; ++j) {
                coverRotate(axis, matrix->m[j], sine[arm], cosine[arm], shifted);
                memcpy(matrix->m[j], shifted, sizeof(shifted));
            }
        }
    }
}

int gevrGuardCoverApply(ChrRecord *chr)
{
    if (!chr || !chr->model || !chr->model->render_pos || !chr->prop
        || chr->prop->type != PROP_TYPE_CHR || chr->actiontype == ACT_DEAD || chr->actiontype == ACT_DIE) return FALSE;
    if (chr->tacticalCover.kind < GEVR_GUARD_COVER_LEAN_LEFT && guardCoverProgress(chr) >= 1.f) return FALSE;
    if (chr->tacticalCover.kind < GEVR_GUARD_COVER_LEAN_LEFT
        && chr->tacticalCover.from[0] == 0.f && chr->tacticalCover.from[1] == 0.f) return FALSE;
    float offset[2], blind;
    guardCoverEvaluate(chr, offset, &blind, guardCoverProgress(chr));
    const float distance = hypotf(offset[0], offset[1]);
    if (distance < .1f) return FALSE;
    Model *model = chr->model;
    ModelNode *nodes[16];
    const Mtxf *view = camGetWorldToScreenMtxf();
    if (!view || !findRig(model, nodes)) return FALSE;
    float shift[3], up[3], axis[3];
    for (int k = 0; k < 3; ++k) {
        shift[k] = offset[0] * view->m[0][k] + offset[1] * view->m[2][k];
        up[k] = view->m[1][k];
    }
    cross(up, shift, axis);
    if (!unit(axis) || !unit(up)) return FALSE;
    coverBendMatrices(model, model->render_pos, nodes, shift, up, axis, distance, blind);
    RenderPosView *collision = gevrAvatarCollisionPose(model);
    if (collision && collision != model->render_pos)
        coverBendMatrices(model, collision, nodes, shift, up, axis, distance, blind);
    return TRUE;
}

void gevrGuardCoverCaptureMuzzles(ChrRecord *chr)
{
    if (g_gevrExtraPass || chr->tacticalCover.kind < GEVR_GUARD_COVER_LEAN_LEFT
        || chr->actiontype == ACT_DEAD || chr->actiontype == ACT_DIE || guardCoverProgress(chr) < 1.f) return;
    Mtxf *view = currentPlayerGetViewToWorldMtxf();
    if (!view) return;
    chr->tacticalCover.muzzleMask = 0;
    for (int hand = GUNRIGHT; hand <= GUNLEFT; ++hand) {
        const PropRecord *prop = chrGetEquippedWeaponProp(chr, hand);
        if (!prop || !prop->weapon || !(prop->flags & PROPFLAG_ONSCREEN)) continue;
        Mtxf muzzle;
        const GevrGearAssembly *assembly = NULL;
        if (!gevrTacticalMeshMuzzleFrame(prop->weapon->model, prop->weapon->weaponnum, muzzle.m, &assembly)) continue;
        coord3d point = {{muzzle.m[3][0], muzzle.m[3][1], muzzle.m[3][2]}};
        mtx4TransformVecInPlace(view, &point);
        if (!isfinite(point.x) || !isfinite(point.y) || !isfinite(point.z)
            || SQR(point.x - chr->prop->pos.x) + SQR(point.z - chr->prop->pos.z) > SQR(235.f)) continue;
        chr->tacticalCover.muzzle[hand] = point;
        chr->tacticalCover.muzzleMask |= 1u << hand;
    }
    chr->tacticalCover.muzzleTick = g_GlobalTimer;
}

static Gfx *drawTacticalGear(Gfx *gdl, ModelRenderData *body, Gfx *list, const Mtxf *pose)
{
    if (!list) return gdl;
    Mtx *fixed = dynAllocateMatrix();
    if (!fixed) return gdl;
    guMtxF2L(pose->m, fixed);
    body->gdl = gdl;
    modelApplyRenderModeType3(body, TRUE);
    gdl = body->gdl;
    gSPMatrix(gdl++, osVirtualToPhysical(fixed), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPDisplayList(gdl++, list);
    return gdl;
}

static Mtxf tacticalBodyFit(const float axes[3][3], const Mtxf *joint, const float offset[3])
{
    Mtxf pose;
    matrix_4x4_set_identity(&pose);
    const float scale = .085f * D_800364CC;  /* gear in mm, body in world cm */
    for (int i = 0; i < 3; ++i) {
        pose.m[3][i] = joint->m[3][i] + D_800364CC
            * (offset[0] * axes[0][i] + offset[1] * axes[1][i] + offset[2] * axes[2][i]);
        for (int axis = 0; axis < 3; ++axis) pose.m[axis][i] = axes[axis][i] * scale;
    }
    return pose;
}

Gfx *gevrAvatarTacticalDraw(ChrRecord *chr, ModelRenderData *body, Gfx *gdl)
{
    if (g_gevrExtraPass || !chr || chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD) return gdl;
    const GevrTacticalVisual *look = gevrTacticalRemoteBody(chr);
    if (!look) return gdl;

    Model *model = chr->model;
    ModelNode *nodes[16];
    if (!model || !model->render_pos || !findRig(model, nodes)) return gdl;
    const Mtxf *chest = modelFindNodeMtx(model, nodes[2], 0);
    const Mtxf *head = modelFindNodeMtx(model, nodes[3], 0);
    const Mtxf *left = modelFindNodeMtx(model, nodes[4], 0);
    const Mtxf *right = modelFindNodeMtx(model, nodes[5], 0);
    float axes[3][3];
    for (int i = 0; i < 3; ++i) {
        axes[0][i] = right->m[3][i] - left->m[3][i];
        axes[1][i] = head->m[3][i] - chest->m[3][i];
    }
    if (!unit(axes[0]) || !unit(axes[1])) return gdl;
    cross(axes[1], axes[0], axes[2]); /* front of the chest */
    if (!unit(axes[2])) return gdl;
    cross(axes[0], axes[2], axes[1]);

    const int leftHanded = (look->body & GEVR_VIS_LEFT_HANDED) != 0;
    const float mirror = leftHanded ? -1.f : 1.f;
    for (int i = GEVR_VIS_SHOULDER; i < GEVR_VIS_GUNS; ++i) {
        const GevrVisualWeapon *gun = &look->weapons[i];
        const int kind = gevrTacticalItem(gun->item);
        if (kind < 0) continue;
        const int shoulder = i == GEVR_VIS_SHOULDER;
        const float at[3] = {shoulder ? 16.f * mirror : (i == GEVR_VIS_HIP_LEFT ? -22.f : 22.f),
            shoulder ? -12.f : -38.f, shoulder ? -15.f : -2.f};
        Mtxf pose = tacticalBodyFit(axes, chest, at);
        for (int col = 0; col < 3; ++col) {
            const float up = pose.m[1][col], front = pose.m[2][col];
            pose.m[1][col] = front;
            pose.m[2][col] = -up;
            pose.m[0][col] = -pose.m[0][col];
        }
        const TacticalMagazine round = {.id = (gun->flags & GEVR_VIS_CHAMBER) != 0};
        Gfx *list = gevrTacticalMountedGearList(kind, gun->build, (gun->flags & GEVR_VIS_SUPPRESSED) != 0,
            kind == TACTICAL_GUN_RPG ? &round : &gun->magazine);
        gdl = drawTacticalGear(gdl, body, list, &pose);
    }
    for (int katana = 0; katana < 2; ++katana) {
        if (!(look->body & (katana ? GEVR_VIS_KATANA : GEVR_VIS_KNIFE))) continue;
        const float at[3] = {(katana ? -17.f : 24.f) * mirror, katana ? -18.f : -51.f, katana ? -16.f : 0.f};
        Mtxf pose = tacticalBodyFit(axes, chest, at);
        for (int col = 0; col < 3; ++col) pose.m[2][col] = -pose.m[2][col];
        Gfx *list = katana ? gevrTacticalKatanaScabbard() : gevrTacticalKnifeSheathList();
        gdl = drawTacticalGear(gdl, body, list, &pose);
    }
    static const float sides[GEVR_VIS_RIG_MAGS] = {-10.f, 0.f, 10.f, -24.f, 24.f};
    for (int i = 0; i < GEVR_VIS_MAGS; ++i) {
        const TacticalMagazine *mag = &look->magazines[i];
        const int kind = tacticalMagazineGun(mag);
        if (kind < 0) continue;
        Gfx *list = gevrTacticalGearLooseMagList(mag->gear_part, kind);
        if (!list) list = gevrTacticalHeldMagList(kind);
        if (!list) continue;
        Mtxf pose;
        if (i < GEVR_VIS_RIG_MAGS) {
            const float at[3] = {sides[i] * mirror, -26.f, 15.f};
            pose = tacticalBodyFit(axes, chest, at);
            for (int col = 0; col < 3; ++col) {
                pose.m[0][col] = -pose.m[2][col];
                pose.m[2][col] = axes[0][col] * (.085f * D_800364CC);
            }
        } else {
            const int leftHand = (i == GEVR_VIS_MAG_LEFT) ^ leftHanded;
            const Mtxf *wrist = modelFindNodeMtx(model, nodes[leftHand ? 8 : 9], 0);
            float grip[3][3];
            basis(wrist, grip);
            const float palm[3] = {-4.f, 0.f, 2.f};
            pose = tacticalBodyFit(grip, wrist, palm);
        }
        gdl = drawTacticalGear(gdl, body, list, &pose);
        gdl = gevrTacticalDrawMagazineRounds(gdl, kind, mag->rounds, 0, 0, mag->gear_part);
    }
    if (sysArgCheck("--tactical-coop-test")) {
        static uint16_t shown;
        const int slot = getPlayerPointerIndex(chr->prop);
        if (slot >= 0 && slot < GEVR_MAX_PLAYERS && !(shown & (1u << slot))) {
            shown |= 1u << slot;
            sysLogPrintf(LOG_NOTE, "tactical co-op: drawing teammate body gear for slot %d", slot);
        }
    }
    return gdl;
}

RenderPosView *gevrAvatarCollisionPose(Model *model) {
    for(int i=0;i<GEVR_MAX_PLAYERS;i++)if(s_collision[i].model==model && s_collision[i].posed==model->render_pos)return s_collision[i].base;
    for(int i=0;i<256;i++)if(s_guardCollision[i].model==model && s_guardCollision[i].posed==model->render_pos)return s_guardCollision[i].base;
    return NULL;
}
ModelNode *gevrAvatarWeaponHand(ChrRecord *chr,int hand,int *physicalLeft) {
    if(!chr || !chr->prop || chr->prop->type!=PROP_TYPE_VIEWER || g_gevrExtraPass || hand<0 || hand>1)return NULL;
    int slot=getPlayerPointerIndex(chr->prop);GevrAvatarPose pose;float weight[3];
    if(!netGetAvatarPose(slot,&pose,weight))return NULL;
    /* An untracked hand keeps the native attachment, not an animated far-side wrist. */
    if(weight[hand+1]<=0)return NULL;
    ModelNode *nodes[16];if(!findRig(chr->model,nodes))return NULL;
    *physicalLeft=(hand==GUNLEFT)^((pose.flags&GEVR_POSE_LEFT_HANDED)?1:0);
    return nodes[*physicalLeft?8:9];
}

/*
 * gevr_forcepull.c online: where another player's hand is in the world, for
 * a gun flying to it (docs/force-pull.md "Online"). hand is a weapon role
 * (GUNRIGHT, GUNLEFT). The tracked palm as gevrAvatarApply reaches for it;
 * a flat player, a bot or an untracked hand: the chest below the eye.
 */
int gevrNetRemoteHandWorld(int slot,int hand,float out[3]) {
    if(slot<0 || slot>=GEVR_MAX_PLAYERS || hand<0 || hand>1 || !g_playerPointers[slot] || !g_playerPointers[slot]->prop)return 0;
    struct player *p=g_playerPointers[slot];
    GevrAvatarPose pose;float weight[3];
    if(netGetAvatarPose(slot,&pose,weight) && weight[hand+1]>0) {
        float grip[3][3];gevrPoseQuatMatrix(pose.hand[hand].q,grip);
        for(int k=0;k<3;k++)out[k]=p->field_488.collision_position.f[k]+pose.head.pos[k]+pose.hand[hand].pos[k]+grip[1][k]*4;
        return 1;
    }
    for(int k=0;k<3;k++)out[k]=p->field_488.collision_position.f[k];
    out[1]-=40;
    return 1;
}
