#ifndef GEVR_TACTICAL_ENEMY_OUTFITS_H
#define GEVR_TACTICAL_ENEMY_OUTFITS_H

typedef struct GevrEnemyOutfit {
    GevrTacticalVest vests[2];
    u8 webbing[3];
    float rigWidth;
} GevrEnemyOutfit;

enum {
    OUTFIT_GREEN, OUTFIT_KHAKI, OUTFIT_URBAN, OUTFIT_COAT_BROWN, OUTFIT_COAT_BLACK,
    OUTFIT_WINTER, OUTFIT_ARCTIC, OUTFIT_WOODLAND, OUTFIT_MARINE, OUTFIT_JANUS,
    OUTFIT_AZTEC, OUTFIT_OFFICER, OUTFIT_BLACK_RIG, OUTFIT_XENIA, OUTFIT_JAWS, OUTFIT_SAMEDI,
};

static const GevrEnemyOutfit s_enemyOutfits[] = {
    [OUTFIT_GREEN]      = {{GEVR_VEST_FLORA_6B2, GEVR_VEST_REDUT_M},      {78,82,54},    1.f},
    [OUTFIT_KHAKI]      = {{GEVR_VEST_GZHEL_KHAKI, GEVR_VEST_GZHEL_KHAKI},{111,101,71}, 1.f},
    [OUTFIT_URBAN]      = {{GEVR_VEST_GZHEL_GREY, GEVR_VEST_GZHEL_GREY}, {69,82,93},   1.f},
    [OUTFIT_COAT_BROWN] = {{GEVR_VEST_BROWN_6B2, GEVR_VEST_BROWN_6B2},   {83,68,57},   1.f},
    [OUTFIT_COAT_BLACK] = {{GEVR_VEST_GZHEL_GREY, GEVR_VEST_GZHEL_GREY}, {53,58,62}, 1.f},
    [OUTFIT_WINTER]     = {{GEVR_VEST_WINTER_6B2, GEVR_VEST_WINTER_6B2}, {144,152,157}, 1.f},
    [OUTFIT_ARCTIC]     = {{GEVR_VEST_ARCTIC_6B2, GEVR_VEST_ARCTIC_6B2}, {88,112,136}, 1.f},
    [OUTFIT_WOODLAND]   = {{GEVR_VEST_STRANDHOGG, GEVR_VEST_REDUT_M},     {74,82,49},   1.f},
    [OUTFIT_MARINE]     = {{GEVR_VEST_GZHEL_GREY, GEVR_VEST_GZHEL_GREY},{63,68,73},    1.f},
    [OUTFIT_JANUS]      = {{GEVR_VEST_SLICK, GEVR_VEST_KORUND_BLACK},    {39,41,44},    1.f},
    [OUTFIT_AZTEC]      = {{GEVR_VEST_GZHEL_OCHRE, GEVR_VEST_GZHEL_OCHRE},{118,111,64}, 1.f},
    [OUTFIT_OFFICER]    = {{GEVR_VEST_NONE, GEVR_VEST_NONE},             {100,87,58},   .86f},
    [OUTFIT_BLACK_RIG]  = {{GEVR_VEST_NONE, GEVR_VEST_NONE},             {47,49,51},    .88f},
    [OUTFIT_XENIA]      = {{GEVR_VEST_NONE, GEVR_VEST_NONE},             {38,37,42},    .80f},
    [OUTFIT_JAWS]       = {{GEVR_VEST_NONE, GEVR_VEST_NONE},             {94,71,50},    .94f},
    [OUTFIT_SAMEDI]     = {{GEVR_VEST_NONE, GEVR_VEST_NONE},             {116,106,100}, .86f},
};

/* Scripted allies can share a guard body. The civilian flag and actual
 * Mishkin head keep those actors out of the hostile equipment pass. */
static const GevrEnemyOutfit *gevrEnemyOutfit(const ChrRecord *chr)
{
    if (chr->headnum == HEAD_Male_Mishkin || (chr->chrflags & CHRFLAG_COUNT_DEATH_AS_CIVILIAN))
        return NULL;
    switch (chr->bodynum) {
    case BODY_Russian_Soldier: return &s_enemyOutfits[OUTFIT_GREEN];
    case BODY_Russian_Infantry: return &s_enemyOutfits[OUTFIT_KHAKI];
    case BODY_St_Petersburg_Guard: return &s_enemyOutfits[OUTFIT_URBAN];
    case BODY_Siberian_Guard_2: return &s_enemyOutfits[OUTFIT_COAT_BROWN];
    case BODY_Siberian_Guard_1_Mishkin: return &s_enemyOutfits[OUTFIT_COAT_BLACK];
    case BODY_Siberian_Special_Forces: case BODY_Parka: return &s_enemyOutfits[OUTFIT_WINTER];
    case BODY_Arctic_Commando: return &s_enemyOutfits[OUTFIT_ARCTIC];
    case BODY_Jungle_Commando: case BODY_Jungle_Fatigues: return &s_enemyOutfits[OUTFIT_WOODLAND];
    case BODY_Janus_Marine: return &s_enemyOutfits[OUTFIT_MARINE];
    case BODY_Janus_Special_Forces: case BODY_Special_Operations_Uniform: return &s_enemyOutfits[OUTFIT_JANUS];
    case BODY_Moonraker_Elite_1_Male: case BODY_Moonraker_Elite_2_Female: return &s_enemyOutfits[OUTFIT_AZTEC];
    case BODY_Russian_Commandant: case BODY_Ourumov: return &s_enemyOutfits[OUTFIT_OFFICER];
    case BODY_Trevelyan_Janus: return &s_enemyOutfits[OUTFIT_BLACK_RIG];
    case BODY_Xenia: return &s_enemyOutfits[OUTFIT_XENIA];
    case BODY_Jaws: return &s_enemyOutfits[OUTFIT_JAWS];
    case BODY_Baron_Samedi: return &s_enemyOutfits[OUTFIT_SAMEDI];
    default: return NULL;
    }
}

static GevrTacticalVest gevrEnemyOutfitVest(const ChrRecord *chr, const GevrEnemyOutfit *outfit)
{
    const unsigned variant = (unsigned)(chr->chrnum + bossGetStageNum());
    if (chr->bodynum == BODY_Russian_Soldier) {
        /* The moving pads were fitted to this specific body and shoulder rig. */
        switch (variant % 16) {
        case 3: return GEVR_VEST_ZABRALO;
        case 8: return GEVR_VEST_IOTV;
        case 13: return GEVR_VEST_REDUT_T5;
        default: break;
        }
    }
    return outfit->vests[variant & 1u];
}
#endif
