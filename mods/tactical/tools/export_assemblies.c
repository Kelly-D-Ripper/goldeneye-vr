/*
 * Dump the native Tactical build's gun assemblies, exactly as the native
 * game builds them, for tools/generate_assemblies.py.
 *
 * Built against the native source (branch tactical/native-0.18.4): it links
 * port/src/gevr_tactical_gear.c unchanged, so mount trees, socket fits,
 * presets and conflict rules come from the same code the native build runs.
 *
 *   export_assemblies <pack-root>      (the folder holding mods/tactical/gear)
 *
 * Output, one line each:
 *   BUILD <key> <preset or "-">
 *   AXES <16 floats>          the native gun axes (gearGunAxes, first person)
 *   STATS <modes> <rpm> <semiRpm> <burst> <mass> <ergo> <recoil> <spread> <damage> <flash> <suppressed>
 *                             gevrWeaponStats for the assembly (gevr_tactical_stats.c)
 *   ACTION <shift> <duration>  the action's travel fully open, in carrier units
 *                             (gevrTacticalActionShift of the shot's stroke), and
 *                             the shot's base return in ticks (gevrTacticalShotFired)
 *   MOUNT <path or "-"> <part id> <16 floats: frame rows>
 *   MOTION <0 none, 1 with the action, 2 with the rack only>, for the mount above
 *   END
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define chdir _chdir
#else
#include <unistd.h>
#endif

#include "gevr_tactical_gear.h"
#include "gevr_tactical_asset.h"
#include "gevr_tactical_fit.h"
#include "gevr_tactical_stats.h"
#include "gevr_tactical_session.h"

/* gevr_tactical_mesh.c, verbatim: the view-forward offsets and gearGunAxes
 * (first person). The native build runs with --tactical-physical. */
enum { TACT_VIEW_KNIFE = 5 };
static const float s_viewForwardOffset[] = {
    [1] = -70.f, [2] = 16.f, [3] = -140.f, [4] = 100.f, [TACT_VIEW_KNIFE] = 0.f
};
typedef struct { float m[4][4]; } Mtxf;
static Mtxf gearGunAxes(int kind)
{
    const GevrTacticalExpandedFit *fit = gevrTacticalExpandedFit(kind);
    if (fit) {
        const float s = fit->scale;
        return (Mtxf){.m = {{fit->lateral*s,0,0,0}, {0,0,s,0}, {0,-s,0,0}, {0,fit->lift,fit->forward,1}}};
    }
    if (kind == TACTICAL_GUN_SVD || kind == TACTICAL_GUN_SKORPION || kind == TACTICAL_GUN_USP) {
        const int pistol = kind == TACTICAL_GUN_USP;
        return (Mtxf){.m = {{-1,0,0,0}, {0,0,1,0}, {0,-1,0,0}, {0,pistol ? 7 : 20,pistol ? -70 : 0,1}}};
    }
    const int mp5 = kind == TACTICAL_GUN_MP5, mp5k = kind == TACTICAL_GUN_MP5K, pm = kind == TACTICAL_GUN_MAKAROV;
    const float scale = mp5 ? 2.018f : mp5k ? 2.0532f : pm ? 1.660014f
        : kind == TACTICAL_GUN_AK74N ? 1.7318101f : 1.7450793f;
    float s = scale, x = 0, y = pm ? 7.f : 20.f, z = s_viewForwardOffset[mp5 ? 3 : mp5k ? 4 : pm ? 1 : 2];
    if (kind == TACTICAL_GUN_AK) {   /* sysArgCheck("--tactical-physical") */
        s *= 1.2f;
        gevrTacticalScaleAkPoint(&x, &y, &z);
    }
    return (Mtxf){.m = {{s,0,0,0}, {0,0,s,0}, {0,-s,0,0}, {x,y,z,1}}};
}

static int kindOfKey(int key)
{
    if (key == GEVR_GEAR_PPK_SUPPRESSED) return TACTICAL_GUN_USP;
    return key > GEVR_GEAR_PPK_SUPPRESSED ? key - 1 : key;
}

/* the native game's surroundings */
void sysLogPrintf(int level, const char *fmt, ...)
{
    va_list ap;
    (void)level;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
int sysArgCheck(const char *arg)
{
    /* every content option the 0.18.4 launcher turns on */
    return strncmp(arg, "--tactical-", 11) == 0 && strstr(arg, "test") == NULL && strstr(arg, "preview") == NULL
        && strcmp(arg, "--tactical-classic-ai") != 0;
}
int gevrTacticalCampaignRules(void) { return 1; }

/* gevr_tactical_mesh.c buildGearGun, verbatim: which parts move, and with what */
static int gearUnderSocket(const char *path, const char *socket)
{
    const size_t size = strlen(socket);
    return !strncmp(path, socket, size) && (!path[size] || path[size] == '/');
}

static int mountMotion(int kind, const GevrGearMount *mount)
{
    const int expanded = tacticalGunIsExpanded(kind);
    const char *slot = mount->socket ? mount->socket->key : "";
    if (!strcmp(slot, "mod_charge") || !strncmp(slot, "mod_charge_", sizeof("mod_charge_") - 1))
        /* gevrTacticalChargeTravel(kind, action, manual): the action's, or the rack's alone */
        return gevrTacticalChargeTravel(kind, 1.f, 0.f) > 0.f ? 1 : 2;
    if (!strcmp(slot, "mod_bolt")) return 1;
    if (tacticalGunIsPistol(kind) && (kind == TACTICAL_GUN_MAKAROV || expanded)
        && (gearUnderSocket(mount->path, "mod_reciever")
            || gearUnderSocket(mount->path, "mod_slide")
            || gearUnderSocket(mount->path, "mod_charge")))
        return 1;
    return 0;
}

static void dump(int key, const char *preset, const GevrGearAssembly *a)
{
    if (!a) return;
    printf("BUILD %d %s\n", key, preset && preset[0] ? preset : "-");
    {
        const Mtxf axes = gearGunAxes(kindOfKey(key));
        printf("AXES");
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) printf(" %.9g", axes.m[r][c]);
        printf("\n");
    }
    {
        GevrWeaponStats st;
        if (gevrWeaponStats(key, a, &st))
            printf("STATS %d %d %d %d %.9g %.9g %.9g %.9g %.9g %.9g %d\n", st.modes, st.rpm, st.semiRpm, st.burst,
                   st.mass, st.ergo, st.recoil, st.spread, st.damage, st.flash, gevrTacticalGearAssemblySuppressed(a));
    }
    {
        /* gevrTacticalShotFired's base return, and the stroke fully open on the
         * shot's own tick (age 0), as gevrTacticalActionShift moves a part (physical) */
        const int kind = kindOfKey(key);
        const int duration = tacticalGunIsPistol(kind) ? 7 : 6;
        printf("ACTION %.9g %d\n", gevrTacticalActionShift(kind, gevrTacticalShotTravel(kind, 0, duration), 1), duration);
    }
    for (int i = 0; i < a->count; i++) {
        const GevrGearMount *m = &a->mounts[i];
        const GevrGearPart *p = gevrTacticalGearPart(m->part);
        if (!p) continue;
        printf("MOUNT %s %s", m->path[0] ? m->path : "-", p->id);
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) printf(" %.9g", m->frame[r][c]);
        printf("\n");
        printf("MOTION %d\n", mountMotion(kindOfKey(key), m));
    }
    printf("END\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: export_assemblies <pack-root>\n"); return 2; }
    if (chdir(argv[1]) != 0) { fprintf(stderr, "no folder %s\n", argv[1]); return 2; }
    if (!gevrTacticalGearEnabled()) { fprintf(stderr, "catalog not loaded\n"); return 1; }
    for (int key = 0; key < GEVR_GEAR_BUILD_COUNT; key++) {
        if (!gevrTacticalGearBuildAvailable(key)) continue;
        dump(key, NULL, gevrTacticalGearStockAssembly(key));
        const int n = gevrTacticalGearPresetCount(key);
        for (int i = 0; i < n; i++) {
            char name[GEVR_GEAR_PRESET_NAME + 1];
            const char *pn = gevrTacticalGearPresetName(key, i);
            if (!pn) continue;
            snprintf(name, sizeof(name), "%s", pn);
            if (!gevrTacticalGearLoadPreset(key, i)) continue;
            dump(key, name, gevrTacticalGearAssembly(key));
        }
        gevrTacticalGearReset(key);
    }
    return 0;
}
