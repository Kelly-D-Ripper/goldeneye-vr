/* Production avatar adapter + production joint builders, synthetic guard rig. */
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gevr_avatar.h"
#include "gevr_guard_cover.h"
#include "gevr_tactical_gear.h"
#include "net/net_core.h"
#include "model.h"
#include "player.h"
#include "matrixmath.h"
#include "objecthandler.h"
#include "quaternion.h"
#include "math_unk_05A9E0.h"
#include "system.h"

static Mtxf view,world;
static ModelNode nodes[10];static union ModelRoData ro[10];
static ModelRwData_HeaderRecord rootRw;
static GevrAvatarPose received;
static float weights[3]={1,1,1};
static int active=1,ready=1,tracked[2]={1,1};
static int focused=1,aiming;
static int tacticalTestOn;

int gevrTacticalGuardSupportContact(const Model *model, int item, float out[3])
{
    (void)model;
    if (item != ITEM_AK47) return 0;
    out[0] = -137.f;
    out[1] = -8.f;
    out[2] = 38.f;
    return 1;
}
static uint64_t now=1000000;
static coord3d eye;
static struct player remote;
struct player *g_playerPointers[MAX_PLAYER_COUNT];
int g_gevrStereo=1,VrLeftHandedMode;
s32 g_gevrExtraPass, g_GlobalTimer;
PropRecord *chrGetEquippedWeaponProp(ChrRecord *chr, GUNHAND hand) { return chr->weapons_held[hand]; }
int gevrTacticalMeshMuzzleFrame(const Model *model, int item, float out[4][4], const GevrGearAssembly **gear)
{
    (void)item;
    *gear = NULL;
    if (!model || !model->render_pos) return 0;
    memcpy(out, model->render_pos[0].pos.m, sizeof(Mtxf));
    return 1;
}
int gevrVrReady(void) {return ready;}
int gevrVrSessionFocused(void) {return focused;}
s32 gevrStereoShotWorld(s32 role,coord3d *origin,coord3d *dir) {(void)role;memset(origin,0,sizeof(*origin));dir->x=1;dir->y=dir->z=0;return aiming;}
int gevrVrGripTracked(int ctrl) {return tracked[ctrl^(VrLeftHandedMode!=0)];}
int gevrVrGripUsable(int ctrl) {return gevrVrGripTracked(ctrl);}
int gevrVrGripPoseCamera(int ctrl,float pos[3],float q[4]) {
    int physical=ctrl^(VrLeftHandedMode!=0);
    pos[0]=physical?.2f:-.2f;pos[1]=-.1f;pos[2]=-.3f;
    q[0]=q[1]=q[2]=0;q[3]=1;return 1;
}
coord3d *gevrEyePosition(void) {return &eye;}
u64 sysGetMicroseconds(void) {return now;}
void sysLogPrintf(s32 level,const char *fmt,...) {(void)level;(void)fmt;}
s32 sysArgCheck(const char *arg) {return tacticalTestOn && (!strcmp(arg,"--tactical-enemy-rigs") || !strcmp(arg,"--tactical-mesh"));}
bool netIsActive(void) {return false;}
Mtxf *camGetWorldToScreenMtxf(void) {return &view;}
Mtxf *currentPlayerGetViewToWorldMtxf(void) {return &world;}
s32 getPlayerPointerIndex(PropRecord *p) {(void)p;return 1;}
int netGetAvatarPose(int slot,GevrAvatarPose *p,float w[3]) {(void)slot;*p=received;memcpy(w,weights,sizeof(weights));return active;}
static void *allocations[50];static int allocated;
void *dynAllocate(s32 size) {assert(allocated<50);return allocations[allocated++]=calloc(1,size);}
static void clearFrame(void) {while(allocated)free(allocations[--allocated]);}
Mtxf *modelFindNodeMtx(Model *m,ModelNode *n,s32 which) {
    (void)which;return &m->render_pos[(n==nodes)?0:n->Data->Group.MatrixID0].pos;
}
union ModelRwData *modelGetNodeRwData(Model *m,ModelNode *n) {(void)m;(void)n;return (union ModelRwData *)&rootRw;}
void (*g_ModelJointPositionedFunc)(s32,Mtxf *);
coord3d D_80036094,D_800360A0,D_800360AC,D_800360B8;
void sub_GAME_7F06DEC0(s32 j,s32 flip,ModelSkeleton *s,ModelAnimation *a,uintptr_t bits,coord3d *rot)
{(void)j;(void)flip;(void)s;(void)a;(void)bits;memset(rot,0,sizeof(*rot));}
void sub_GAME_7F06D160(coord3d *a,coord3d *b,f32 t) {for(int k=0;k<3;k++)a->f[k]+=(b->f[k]-a->f[k])*t;}
bool gevrCampaignGuardIsIdleGroupMember(const ChrRecord *guard) {(void)guard;return false;}
float gevrGuardCigarettePose(ChrRecord *guard, float foregrip[3]) {(void)guard;(void)foregrip;return 0.f;}
void gevrGuardCigaretteGrip(const ChrRecord *guard, const float target[3], const float wrist[3])
{ (void)guard; (void)target; (void)wrist; }

/* PRODUCTION_BUILDERS */

void modelUpdateMatrices(ModelRenderData *data,Model *m) {
    process_01_group_heading(data,m,nodes);
    /* Guard parents precede children. The production decoder is used for all
     * GROUP nodes, including its avatar override and auxiliary bend matrices. */
    for(int j=1;j<=9;j++)process_02_position(data,m,&nodes[j]);
}
static void nearAt(float a,float b,int line) {if(!(isfinite(a)&&isfinite(b)&&fabsf(a-b)<.003f)) {fprintf(stderr,"rig line %d: %.6f != %.6f\n",line,a,b);abort();}}
#define near(a,b) nearAt(a,b,__LINE__)
static float length(const float *a,const float *b) {float d=0;for(int k=0;k<3;k++)d+=(a[k]-b[k])*(a[k]-b[k]);return sqrtf(d);}
static void fixture(Model *m,ModelFileHeader *header,ChrRecord *chr,PropRecord *prop,ModelRenderData *data,float scale) {
    memset(nodes,0,sizeof(nodes));memset(ro,0,sizeof(ro));memset(&rootRw,0,sizeof(rootRw));
    matrix_4x4_set_identity(&world);matrix_4x4_set_identity(&view);
    for(int i=0;i<3;i++) {view.m[i][i]=scale;world.m[i][i]=1/scale;}
    header->RootNode=nodes;header->numMatrices=30;m->obj=header;m->scale=1;m->anim=(ModelAnimation *)1;
    m->render_pos=dynAllocate(30*sizeof(*m->render_pos));
    nodes[0].Data=&ro[0];nodes[0].Opcode=MODELNODE_OPCODE_HEADER;
    ro[0].Header.MatrixIndex=0;
    int parent[10]={0,0,1,2,2,2,4,5,6,7};
    const float position[10][3]={{0},{0},{0,70,0},{0,25,0},{-20,15,0},{20,15,0},{-30,0,0},{30,0,0},{-25,0,0},{25,0,0}};
    for(int j=1;j<=9;j++) {
        nodes[j].Data=&ro[j];nodes[j].Opcode=MODELNODE_OPCODE_GROUP|0x300;nodes[j].Parent=&nodes[parent[j]];
        ro[j].Group.JointID=j;ro[j].Group.MatrixID0=j*3;ro[j].Group.MatrixID1=j*3+1;ro[j].Group.MatrixID2=j*3+2;
        memcpy(ro[j].Group.Origin.f,position[j],sizeof(position[j]));
        if(!nodes[parent[j]].Child)nodes[parent[j]].Child=&nodes[j];
        else {ModelNode *n=nodes[parent[j]].Child;while(n->Next)n=n->Next;n->Next=&nodes[j];}
    }
    prop->type=PROP_TYPE_VIEWER;prop->chr=chr;prop->pos.y=100;chr->prop=prop;chr->model=m;
    remote.field_488.collision_position=prop->pos;g_playerPointers[1]=&remote;
    data->basemtx=&view;modelUpdateMatrices(data,m);
    memset(&received,0,sizeof(received));received.flags=7;received.head.q[1]=1; /* neutral camera faces +Z */
    for(int role=0;role<2;role++) {received.hand[role].q[3]=1;received.hand[role].pos[0]=role?-35:35;received.hand[role].pos[1]=-10;received.hand[role].pos[2]=30;}
}
static void rig(float scale,int lefty,float yaw,float height) {
    Model m={0};ModelFileHeader header={0};ChrRecord chr={0};PropRecord prop={0};ModelRenderData data={0};
    fixture(&m,&header,&chr,&prop,&data,scale);
    for(int role=0;role<2;role++)received.hand[role].pos[1]=height;
    prop.pos.y-=70; /* native animated pelvis, below the movement eye */
    if(lefty) {
        received.flags|=GEVR_POSE_LEFT_HANDED;
        GevrTrackedPose tmp=received.hand[0];received.hand[0]=received.hand[1];received.hand[1]=tmp;
    }
    if(yaw!=0) {
        Mtxf turn;matrix_4x4_set_rotation_around_y(-yaw*3.14159265358979323846f/180,&turn);
        for(int j=0;j<3;j++) {
            GevrTrackedPose *p=j?&received.hand[j-1]:&received.head;
            float rot[3][3],turned[3][3],pos[3];gevrPoseQuatMatrix(p->q,rot);
            for(int i=0;i<3;i++)for(int k=0;k<3;k++)turned[i][k]=rot[i][0]*turn.m[0][k]+rot[i][1]*turn.m[1][k]+rot[i][2]*turn.m[2][k];
            for(int k=0;k<3;k++)pos[k]=p->pos[0]*turn.m[0][k]+p->pos[1]*turn.m[1][k]+p->pos[2]*turn.m[2][k];
            memcpy(p->pos,pos,sizeof(pos));gevrPoseMatrixQuat(turned,p->q);
        }
        received.body_yaw=yaw;
    }
    /* Pitch and roll the HMD independently of the torso and gun wrists. */
    float headCamera[3][3],tilted[3][3];
    Mtxf tilt;coord3d tiltAngles={.x=.2f,.y=0,.z=.4f};
    matrix_4x4_set_rotation_around_xyz(&tiltAngles,&tilt);
    gevrPoseQuatMatrix(received.head.q,headCamera);
    for(int i=0;i<3;i++)for(int k=0;k<3;k++)tilted[i][k]=headCamera[i][0]*tilt.m[0][k]+headCamera[i][1]*tilt.m[1][k]+headCamera[i][2]*tilt.m[2][k];
    gevrPoseMatrixQuat(tilted,received.head.q);
    received.head.pos[0]=5;
    ModelRwData_HeaderRecord saved=rootRw;
    RenderPosView *base=m.render_pos;
    gevrAvatarApply(&chr,&data);
    assert(m.render_pos!=base && gevrAvatarCollisionPose(&m)==base);
    assert(!memcmp(&rootRw,&saved,sizeof(rootRw)));
    Mtxf headWorld;matrix_4x4_multiply(&world,modelFindNodeMtx(&m,&nodes[3],0),&headWorld);
    for(int i=0;i<3;i++)for(int k=0;k<3;k++)near(headWorld.m[i][k],tilted[i][k]*(i==1?1:-1));
    for(int side=0;side<2;side++) {
        int role=(side==0?GUNLEFT:GUNRIGHT)^(lefty?1:0);
        Mtxf sh,el,wr;
        matrix_4x4_multiply(&world,modelFindNodeMtx(&m,&nodes[4+side],0),&sh);
        matrix_4x4_multiply(&world,modelFindNodeMtx(&m,&nodes[6+side],0),&el);
        matrix_4x4_multiply(&world,modelFindNodeMtx(&m,&nodes[8+side],0),&wr);
        near(length(sh.m[3],el.m[3]),30);near(length(el.m[3],wr.m[3]),25);
        float grip[3][3];gevrPoseQuatMatrix(received.hand[role].q,grip);
        for(int k=0;k<3;k++) {
            near(wr.m[3][k],remote.field_488.collision_position.f[k]+received.head.pos[k]+received.hand[role].pos[k]+grip[1][k]*4);
            near(wr.m[0][k],grip[1][k]*(side==0?-1:1));near(wr.m[2][k],-grip[2][k]);
        }
        int physicalLeft=-1;assert(gevrAvatarWeaponHand(&chr,role,&physicalLeft)==&nodes[8+side]);assert(physicalLeft==(side==0));
        /* Primary and both half-bend matrices were generated; no invalid axes. */
        for(int k=0;k<16;k++)assert(isfinite(((float *)&m.render_pos[(6+side)*3+2].pos)[k]));
    }
    /* A second view builds from its own base, never the prior camera's pose. */
    m.render_pos=base;modelUpdateMatrices(&data,&m);gevrAvatarApply(&chr,&data);
    assert(gevrAvatarCollisionPose(&m)==base);
    active=0;m.render_pos=base;gevrAvatarApply(&chr,&data);assert(m.render_pos==base && !gevrAvatarCollisionPose(&m));active=1;
    chr.actiontype=ACT_DEAD;gevrAvatarApply(&chr,&data);assert(m.render_pos==base);chr.actiontype=ACT_INIT;
    nodes[6].Parent=&nodes[2];gevrAvatarApply(&chr,&data);assert(m.render_pos==base); /* unsupported rig */
    clearFrame();
}
static void capture(void) {
    Model m={0};ModelFileHeader header={0};ChrRecord chr={0};PropRecord prop={0};ModelRenderData data={0};
    fixture(&m,&header,&chr,&prop,&data,.2f);
    struct player pl={0};pl.prop=&prop;eye=prop.pos;eye.y+=10;
    GevrAvatarPose p;gevrAvatarCapture(&p,&pl);assert(p.flags==7);near(p.hand[0].pos[0],20);near(p.hand[1].pos[0],-20);near(p.head.pos[1],10);
    VrLeftHandedMode=1;gevrAvatarCapture(&p,&pl);assert(p.flags==15);near(p.hand[0].pos[0],-20);near(p.hand[1].pos[0],20);
    tracked[0]=0;gevrAvatarCapture(&p,&pl);assert(p.flags==(GEVR_POSE_HEAD|GEVR_POSE_LEFT|GEVR_POSE_LEFT_HANDED));
    ready=0;gevrAvatarCapture(&p,&pl);assert(!p.flags);
    ready=1;tracked[0]=1;VrLeftHandedMode=0;
    aiming=1;pl.hands[0].weaponnum=ITEM_WPPK;gevrAvatarCapture(&p,&pl);
    float grip[3][3];gevrPoseQuatMatrix(p.hand[0].q,grip);near(-grip[1][0],1);near(-grip[1][1],0);near(-grip[1][2],0);
    aiming=0;focused=0;gevrAvatarCapture(&p,&pl);assert(!p.flags);focused=1;clearFrame();
}
static void guardSupport(void) {
    Model m={0},gun={0}; ModelFileHeader header={0}; ChrRecord chr={0}; PropRecord prop={0},held={0};
    WeaponObjRecord weapon={0}; ModelRenderData data={0};
    fixture(&m,&header,&chr,&prop,&data,1);
    prop.type=PROP_TYPE_CHR; held.weapon=&weapon; held.flags|=PROPFLAG_ONSCREEN; weapon.model=&gun; weapon.weaponnum=ITEM_AK47;
    chr.weapons_held[GUNRIGHT]=&held; gun.attachedto=&m; gun.render_pos=dynAllocate(sizeof(*gun.render_pos));
    matrix_4x4_set_identity(&gun.render_pos[0].pos);
    for(int k=0;k<3;k++)gun.render_pos[0].pos.m[k][k]=.1f;
    gun.render_pos[0].pos.m[3][0]=-6.3f;gun.render_pos[0].pos.m[3][1]=75.8f;gun.render_pos[0].pos.m[3][2]=26.2f;
    float target[3]={-20,75,30};
    RenderPosView *base=m.render_pos;
    float before=length(modelFindNodeMtx(&m,&nodes[8],0)->m[3],target);
    tacticalTestOn=1;gevrAvatarGuardForget(&chr);gevrAvatarGuardApply(&chr,&data);
    assert(m.render_pos!=base && gevrAvatarCollisionPose(&m)==base);
    float after=length(modelFindNodeMtx(&m,&nodes[8],0)->m[3],target);
    assert(after<before && after<1.0f);
    gevrAvatarGuardForget(&chr);assert(!gevrAvatarCollisionPose(&m));
    tacticalTestOn=0;clearFrame();
}
static void guardedCover(void)
{
    Model model={0},gun={0}; ModelFileHeader header={0};
    ChrRecord chr={0}; PropRecord prop={0},held={0};
    WeaponObjRecord weapon={0}; ModelRenderData data={0};
    fixture(&model,&header,&chr,&prop,&data,1);
    prop.type=PROP_TYPE_CHR;
    float head=model.render_pos[9].pos.m[3][0], wrist=model.render_pos[27].pos.m[3][0];
    const Mtxf pelvis=model.render_pos[3].pos;
    float offset[2],blend;
    for(int side=-1;side<=1;side+=2) {
        memset(&chr.tacticalCover,0,sizeof(chr.tacticalCover));
        g_GlobalTimer=10;
        int kind=side<0?GEVR_GUARD_COVER_LEAN_LEFT:GEVR_GUARD_COVER_LEAN_RIGHT;
        gevrGuardCoverPoseSet(&chr,kind,side*56,0);
        assert(gevrGuardCoverPoseGet(&chr,offset,&blend)==kind);
        near(offset[0],0);near(blend,0);
        g_GlobalTimer+=4; gevrGuardCoverPoseGet(&chr,offset,&blend);
        near(offset[0],side*28);near(blend,.5f);
        g_GlobalTimer+=4; gevrGuardCoverPoseGet(&chr,offset,&blend);
        near(offset[0],side*56);near(blend,1);
        modelUpdateMatrices(&data,&model); assert(gevrGuardCoverApply(&chr));
        assert(!memcmp(&model.render_pos[3].pos,&pelvis,sizeof(pelvis)));
        assert(side*(model.render_pos[9].pos.m[3][0]-head)>38.f);
        assert(side*(model.render_pos[27].pos.m[3][0]-wrist)>32.f);
    }
    gevrGuardCoverPoseSet(&chr,GEVR_GUARD_COVER_BLIND_RIGHT,56,0); g_GlobalTimer+=8;
    modelUpdateMatrices(&data,&model); assert(gevrGuardCoverApply(&chr));
    assert(model.render_pos[9].pos.m[3][0]-head<18.f); /* head remains tucked */
    assert(model.render_pos[27].pos.m[3][0]-wrist>32.f);
    held.weapon=&weapon; held.flags=PROPFLAG_ONSCREEN; weapon.model=&gun; weapon.weaponnum=ITEM_AK47;
    chr.weapons_held[GUNRIGHT]=&held; gun.render_pos=&model.render_pos[27];
    coord3d muzzle;
    assert(!gevrGuardCoverMuzzle(&chr,GUNRIGHT,&muzzle));
    gevrGuardCoverCaptureMuzzles(&chr);
    assert(gevrGuardCoverMuzzle(&chr,GUNRIGHT,&muzzle));
    near(muzzle.x,model.render_pos[27].pos.m[3][0]);
    assert(!gevrGuardCoverMuzzle(&chr,GUNLEFT,&muzzle));
    g_GlobalTimer+=6;
    assert(!gevrGuardCoverMuzzle(&chr,GUNRIGHT,&muzzle)); /* no stale render geometry */
    gevrGuardCoverPoseSet(&chr,GEVR_GUARD_COVER_DUCK,0,0); g_GlobalTimer+=8;
    modelUpdateMatrices(&data,&model); assert(!gevrGuardCoverApply(&chr));
    near(model.render_pos[9].pos.m[3][0],head);
    chr.actiontype=ACT_DIE;
    assert(gevrGuardCoverPoseGet(&chr,offset,&blend)==GEVR_GUARD_COVER_NONE);
    clearFrame();
}
int main(void) {rig(1,0,0,-10);rig(.2f,0,0,-40);rig(1,1,0,20);rig(1,0,90,-10);rig(.2f,1,350,20);capture();guardSupport();guardedCover();puts("avatar: production rig builders, tracked wrists, guard support reach and collision pose passed");return 0;}
