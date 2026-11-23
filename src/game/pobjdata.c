#include <ultra64.h>
#include <bondgame.h>
#include "chrobjdata.h"

//D:800375D0
#include <assets/obseg/prop/propModelFileHeaders.inc.c>

//D:8003A050
#include <assets/embedded/skeletons/props.inc.c>
#ifdef GEVR
/* Dedicated header keeps the AR-33's shared carrier model independent. */
MODELFILEHEADER(tacticalak74n, 0, &SKELETON(prop_weapon), 0, 0x3, 0x2, 964.01367, 0, 6)
MODELFILEHEADER(tacticalkatana, 0, &SKELETON(prop_weapon), 0, 0x3, 0x1, 950.f, 0, 2)
/* Native shells retain the ROM's exact switch and texture table layout. */
#define TACTICAL_SHELL_chruzi 7
#define TACTICAL_SHELL_chrwppk 5
#define TACTICAL_SHELL_chrkalash 8
#define TACTICAL_SHELL_chrm16 6
#define TACTICAL_MATRICES_chruzi 1
#define TACTICAL_MATRICES_chrwppk 1
#define TACTICAL_MATRICES_chrkalash 1
#define TACTICAL_MATRICES_chrm16 2
#define TACTICAL_EXPANDED(id, group, native, gun, prop, capacity, cadence, name) \
    MODELFILEHEADER(tactical_##id, 0, &SKELETON(prop_weapon), 0, 0x3, TACTICAL_MATRICES_##prop, 400.f, 0, TACTICAL_SHELL_##prop)
#include "gevr_tactical_expanded.inc.h"
#undef TACTICAL_EXPANDED
#undef TACTICAL_MATRICES_chrm16
#undef TACTICAL_MATRICES_chrkalash
#undef TACTICAL_MATRICES_chrwppk
#undef TACTICAL_MATRICES_chruzi
#undef TACTICAL_SHELL_chrm16
#undef TACTICAL_SHELL_chrkalash
#undef TACTICAL_SHELL_chrwppk
#undef TACTICAL_SHELL_chruzi
MODELFILEHEADER(tacticalaltyn, 0, &SKELETON(hat), 0, 0, 1, 220.f, 0, 1)
MODELFILEHEADER(tacticalfasttrooper, 0, &SKELETON(hat), 0, 0, 1, 220.f, 0, 1)
#endif



//D:8003A228                     .globl PitemZ_entries
/* A hack campaign may replace both tables with its own, longer ones (gevr_romcampaign.c). */
#define PitemZ_entries gevrRetailPropRecords
#include <assets/obseg/prop/propItemModelFileRecord.inc.c>
#undef PitemZ_entries
ItemModelFileRecord *PitemZ_entries = gevrRetailPropRecords;
/* rows before the terminator */
s32 g_GevrPropCount = ARRAYCOUNT(gevrRetailPropRecords) - 1;

#if defined(VERSION_EU)
#include <assets/obseg/prop/propExplosionDetailsRecords-eu.inc.c>
#else
//D:8003b224
#define object_explosion_details gevrRetailExplosions
#include <assets/obseg/prop/propExplosionDetailsRecords.inc.c>
#undef object_explosion_details
ExplosionDetailsRecord *object_explosion_details = gevrRetailExplosions;
#endif
