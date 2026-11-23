#include "gevr_tactical_stats.h"
#include "gevr_tactical_asset.h"
#include <math.h>

typedef struct PartStats {
    float mass, ergo, recoil, accuracy, damage;
} PartStats;

static GevrWeaponStats s_weapons[GEVR_GEAR_BUILD_COUNT];
static PartStats s_parts[GEVR_GEAR_PARTS];

static float clamp(float value, float low, float high)
{
    return fminf(high, fmaxf(low, value));
}

static void loadStats(void)
{
    static int loaded;
    if (loaded) return;
    loaded = 1;
    FILE *file = gevrTacticalOpenAsset("mods/tactical/gear/stats.txt", "r");
    if (!file) return;
    char line[256];
    if (fgets(line, sizeof(line), file) && !strncmp(line, "GEVRSTATS1", 10)) {
        while (fgets(line, sizeof(line), file)) {
            int index;
            if (line[0] == 'W') {
                GevrWeaponStats w;
                if (sscanf(line, "W %d %d %d %d %d %f %f %f %f %f %f",
                    &index, &w.modes, &w.rpm, &w.semiRpm, &w.burst,
                    &w.mass, &w.ergo, &w.recoil, &w.spread, &w.damage, &w.flash) != 11) continue;
                if (index < 0 || index >= GEVR_GEAR_BUILD_COUNT || w.modes < 1 || w.modes > 7
                    || w.semiRpm < 1 || w.semiRpm > 1500 || w.rpm < 0 || w.rpm > 1500
                    || ((w.modes & (GEVR_FIRE_AUTO | GEVR_FIRE_BURST)) && !w.rpm)
                    || w.burst < 1 || w.burst > 8 || !(w.mass > 0 && w.mass < 25)
                    || !(w.ergo >= 0 && w.ergo <= 100 && w.recoil > 0 && w.recoil < 10
                        && w.spread > 0 && w.spread < 20 && w.damage > 0 && w.damage < 100
                        && w.flash >= 0 && w.flash < 5)) continue;
                s_weapons[index] = w;
            } else if (line[0] == 'P') {
                PartStats p;
                char id[25];
                if (sscanf(line, "P %d %24s %f %f %f %f %f",
                    &index, id, &p.mass, &p.ergo, &p.recoil, &p.accuracy, &p.damage) != 7) continue;
                if (index < 0 || index >= GEVR_GEAR_PARTS) continue;
                const GevrGearPart *part = gevrTacticalGearPart(index);
                if (part && !strcmp(id, part->id)
                    && isfinite(p.mass) && isfinite(p.ergo) && isfinite(p.recoil)
                    && isfinite(p.accuracy) && isfinite(p.damage)) s_parts[index] = p;
            }
        }
    }
    fclose(file);
}

float gevrWeaponBaseFlash(int key)
{
    if (key < 0 || key >= GEVR_GEAR_BUILD_COUNT) return 1.f;
    loadStats();
    return s_weapons[key].modes ? s_weapons[key].flash : 1.f;
}

static void sumFitted(PartStats *total, const GevrGearAssembly *assembly, float sign)
{
    for (int i = 1; assembly && i < assembly->count; ++i) {
        const GevrGearMount *mount = &assembly->mounts[i];
        if (mount->part < 0 || mount->part >= GEVR_GEAR_PARTS || gevrTacticalGearIsRootChoice(mount->path))
            continue;
        const PartStats *part = &s_parts[mount->part];
        total->mass += sign * part->mass;
        total->ergo += sign * part->ergo;
        total->recoil += sign * part->recoil;
        total->accuracy += sign * part->accuracy;
        total->damage += sign * part->damage;
    }
}

int gevrWeaponStats(int key, const GevrGearAssembly *fitted, GevrWeaponStats *out)
{
    if (!out || key < 0 || key >= GEVR_GEAR_BUILD_COUNT) return 0;
    const GevrGearAssembly *stock = gevrTacticalGearStockAssembly(key);
    loadStats();
    if (!s_weapons[key].modes) return 0;
    *out = s_weapons[key];
    if (fitted) {
        PartStats delta = {0};
        sumFitted(&delta, fitted, 1.f);
        sumFitted(&delta, stock, -1.f);
        out->mass = clamp(out->mass + delta.mass, .4f, 18.f);
        out->ergo = clamp(out->ergo + delta.ergo, 0.f, 100.f);
        out->recoil *= clamp(1.f + .01f * delta.recoil, .55f, 1.5f);
        out->spread *= clamp(1.f - .01f * delta.accuracy, .55f, 1.6f);
        out->damage *= clamp(1.f + .01f * delta.damage, .88f, 1.12f);
    }
    if (fitted ? gevrTacticalGearAssemblySuppressed(fitted) : key == GEVR_GEAR_PPK_SUPPRESSED)
        out->flash *= .17f;
    return 1;
}

int gevrFireMode(const GevrWeaponStats *stats, int selected)
{
    if ((selected == GEVR_FIRE_SEMI || selected == GEVR_FIRE_BURST || selected == GEVR_FIRE_AUTO)
        && (stats->modes & selected)) return selected;
    if (stats->modes & GEVR_FIRE_AUTO) return GEVR_FIRE_AUTO;
    if (stats->modes & GEVR_FIRE_SEMI) return GEVR_FIRE_SEMI;
    return GEVR_FIRE_BURST;
}

int gevrNextFireMode(const GevrWeaponStats *stats, int selected)
{
    int mode = gevrFireMode(stats, selected);
    do {
        mode = mode == GEVR_FIRE_AUTO ? GEVR_FIRE_SEMI : mode << 1;
    } while (!(stats->modes & mode));
    return mode;
}

int gevrFireStep(GevrFireClock *clock, const GevrWeaponStats *stats, int mode,
                 double now, int trigger, int ready, int loaded)
{
    const int press = trigger && !clock->trigger;
    clock->trigger = trigger;
    if (!ready || !loaded) {
        clock->burstLeft = 0;
        return ready && press ? GEVR_FIRE_DRY : GEVR_FIRE_WAIT;
    }
    const int continuing = mode == GEVR_FIRE_AUTO ? trigger && !press
        : mode == GEVR_FIRE_BURST && clock->burstLeft > 0;
    if (!(continuing || press) || now + .000001 < clock->next) return GEVR_FIRE_WAIT;
    const double period = 60.0 / (mode == GEVR_FIRE_SEMI ? stats->semiRpm : stats->rpm);
    /* Carry the fractional remainder across live shots. A hitch skips missed
     * slots so it cannot create a backlog of shots using a single hand pose. */
    clock->next = continuing && clock->next > 0 && now - clock->next < period
        ? clock->next + period : now + period;
    if (mode == GEVR_FIRE_BURST) {
        if (!clock->burstLeft) clock->burstLeft = stats->burst;
        --clock->burstLeft;
    }
    return GEVR_FIRE_SHOT;
}

const char *gevrFireModeName(int mode)
{
    switch (mode) {
    case GEVR_FIRE_SEMI: return "Semi";
    case GEVR_FIRE_BURST: return "Burst";
    case GEVR_FIRE_AUTO: return "Auto";
    default: return "";
    }
}

const char *gevrFireModeList(int modes)
{
    static const char *const names[] = {
        "", "Semi", "Burst", "Semi / Burst", "Auto",
        "Semi / Auto", "Burst / Auto", "Semi / Burst / Auto"
    };
    return modes >= 0 && modes < 8 ? names[modes] : "";
}
