#include "gevr_tactical_optics.h"
#include "gevr_tactical_gear.h"
#include "gevr_tactical_asset.h"
#include "system.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_OPTIC_MODES = 12, MAX_RETICLE_SIZE = 256 };

typedef struct OpticProfile {
    GevrOpticMode *modes;
    int status, count, selected, flipMode;
    float zoom;
} OpticProfile;

static OpticProfile s_optics[GEVR_GEAR_PARTS];
enum { FLICK_READY, FLICK_ARMED, FLICK_USED };
static struct {
    int part, state;
    unsigned started;
    float startCm;
} s_flick = {.part = -1};

/* Lift the emissive marks once when a profile loads. A light one-texel
 * shoulder keeps small marks legible without changing their outline. */
static void improveCollimatorMark(GevrOpticMode *mode, size_t length)
{
    unsigned char *source = malloc(length);
    if (!source) return;
    memcpy(source, mode->reticle, length);
    const unsigned width = mode->width, height = mode->height;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const size_t index = (size_t)y * width + x;
            const unsigned char *mark = source + index * 4;
            unsigned alpha = mark[3] * 4u;
            const size_t adjacent[] = {
                index - (x > 0), index + (x + 1 < width),
                index - (y > 0 ? width : 0), index + (y + 1 < height ? width : 0),
            };
            for (unsigned i = 0; i < 4; ++i) {
                const unsigned char *sample = source + adjacent[i] * 4;
                if (sample[3] <= alpha) continue;
                alpha = sample[3];
                mark = sample;
            }
            alpha = (alpha + 2u) / 4u;
            if (!alpha) continue;
            unsigned char *pixel = mode->reticle + index * 4;
            const float peak = fmaxf(mark[0], fmaxf(mark[1], mark[2]));
            const float lift = peak > 0.f ? .24f + .76f * 255.f / peak : 1.f;
            for (unsigned i = 0; i < 3; ++i) pixel[i] = (unsigned char)lroundf(mark[i] * lift);
            pixel[3] = alpha + (alpha * (255u - alpha) + 127u) / 255u;
        }
    }
    free(source);
}

/* Keep all fitted optics on the same nominal shot, in camera-space game
 * units. Spread and recoil remain part of the normal shot calculation. */
void gevrTacticalOpticZero(GevrOpticSurface *surface, const float origin[3], const float shot[3], float distance)
{
    float toTarget[3];
    float length2 = 0.f, forwards = 0.f;
    for (int i = 0; i < 3; ++i) {
        toTarget[i] = origin[i] + distance * shot[i] - surface->centre[i];
        length2 += toTarget[i] * toTarget[i];
        forwards += toTarget[i] * surface->forward[i];
    }
    const float length = sqrtf(length2);
    if (isfinite(length) && length > 1e-4f && forwards > .65f * length) {
        for (int i = 0; i < 3; ++i) {
            surface->zero[i] = toTarget[i];
            surface->aim[i] = toTarget[i] / length;
        }
    } else {
        for (int i = 0; i < 3; ++i) {
            surface->zero[i] = surface->forward[i] * distance;
            surface->aim[i] = surface->forward[i];
        }
    }
}

static int readMode(FILE *file, GevrOpticMode *mode)
{
    uint32_t info[4];
    float fields[19];
    if (fread(info, sizeof(*info), 4, file) != 4 || fread(fields, sizeof(*fields), 19, file) != 19)
        return 0;
    const unsigned kind = info[0] & 255u;
    if (kind < GEVR_OPTIC_COLLIMATOR || kind > GEVR_OPTIC_THERMAL
        || info[1] < 3 || info[1] > GEVR_OPTIC_POINTS
        || info[2] < 1 || info[2] > MAX_RETICLE_SIZE || info[3] < 1 || info[3] > MAX_RETICLE_SIZE)
        return 0;
    for (int i = 0; i < 19; ++i)
        if (!isfinite(fields[i])) return 0;
    if (fields[12] <= 0.f || fields[13] <= 0.f || fields[17] < 1.f || fields[18] < fields[17])
        return 0;
    mode->kind = kind;
    mode->folded = (info[0] >> 8) & 1u;
    mode->palette = (info[0] >> 9) & 3u;
    mode->firstFocalPlane = (info[0] >> 11) & 1u;
    mode->points = info[1];
    mode->width = info[2];
    mode->height = info[3];
    memcpy(mode->centre, fields, 3 * sizeof(float));
    memcpy(mode->right, fields + 3, 3 * sizeof(float));
    memcpy(mode->up, fields + 6, 3 * sizeof(float));
    memcpy(mode->forward, fields + 9, 3 * sizeof(float));
    memcpy(mode->half, fields + 12, 2 * sizeof(float));
    memcpy(mode->reticleAxis, fields + 14, 2 * sizeof(float));
    mode->angular = fields[16];
    mode->lowZoom = fields[17];
    mode->highZoom = fields[18];
    if (fread(mode->boundary, sizeof(mode->boundary[0]), mode->points, file) != mode->points)
        return 0;
    for (unsigned i = 0; i < mode->points; ++i)
        if (!isfinite(mode->boundary[i][0]) || !isfinite(mode->boundary[i][1])
            || fabsf(mode->boundary[i][0]) > 1.01f || fabsf(mode->boundary[i][1]) > 1.01f)
            return 0;
    const size_t length = 4u * mode->width * mode->height;
    mode->reticle = malloc(length);
    if (!mode->reticle || fread(mode->reticle, 1, length, file) != length) return 0;
    if (kind == GEVR_OPTIC_COLLIMATOR) improveCollimatorMark(mode, length);
    return 1;
}

static OpticProfile *getProfile(int part)
{
    if (part < 0 || part >= GEVR_GEAR_PARTS) return NULL;
    OpticProfile *profile = &s_optics[part];
    if (profile->status) return profile->status > 0 ? profile : NULL;
    const GevrGearPart *info = gevrTacticalGearPart(part);
    if (!info) return NULL;

    profile->status = -1;
    char path[96];
    snprintf(path, sizeof(path), "mods/tactical/gear/optics/%s.opt", info->id);
    FILE *file = gevrTacticalOpenAsset(path, "rb");
    if (!file) return NULL;
    char signature[8];
    uint32_t count = 0;
    int good = fread(signature, 1, sizeof(signature), file) == sizeof(signature)
        && !memcmp(signature, "GEVROPT4", sizeof(signature))
        && fread(&count, sizeof(count), 1, file) == 1 && count > 0 && count <= MAX_OPTIC_MODES;
    if (good) {
        profile->modes = calloc(count, sizeof(*profile->modes));
        good = profile->modes != NULL;
        for (unsigned i = 0; good && i < count; ++i)
            good = readMode(file, &profile->modes[i]);
        good = good && fgetc(file) == EOF;
    }
    fclose(file);
    if (!good) {
        if (profile->modes)
            for (unsigned i = 0; i < count; ++i) free(profile->modes[i].reticle);
        free(profile->modes);
        profile->modes = NULL;
        sysLogPrintf(LOG_WARNING, "tactical optics: invalid %s profile", info->name);
        return NULL;
    }
    profile->count = (int)count;
    profile->flipMode = -1;
    for (int i = 0; i < profile->count; ++i)
        if (profile->modes[i].folded) profile->flipMode = i;
    profile->zoom = profile->modes[0].lowZoom;
    profile->status = 1;
    sysLogPrintf(LOG_NOTE, "tactical optics: loaded %s (%u modes)", info->name, count);
    return profile;
}

const GevrOpticMode *gevrTacticalOpticMode(int part, float *zoom)
{
    OpticProfile *profile = getProfile(part);
    if (!profile) return NULL;
    if (zoom) *zoom = profile->zoom;
    return &profile->modes[profile->selected];
}

int gevrTacticalOpticMagnification(int part, float *low, float *high)
{
    const OpticProfile *profile = getProfile(part);
    if (!profile) return 0;
    *low = profile->modes[0].lowZoom;
    *high = profile->modes[0].highZoom;
    for (int i = 1; i < profile->count; ++i) {
        *low = fminf(*low, profile->modes[i].lowZoom);
        *high = fmaxf(*high, profile->modes[i].highZoom);
    }
    return 1;
}

int gevrTacticalOpticCycle(int part)
{
    OpticProfile *profile = getProfile(part);
    if (!profile || profile->count < 2) return 0;
    profile->selected = (profile->selected + 1) % profile->count;
    profile->zoom = profile->modes[profile->selected].lowZoom;
    return 1;
}

int gevrTacticalOpticAdjustZoom(int part, float amount)
{
    OpticProfile *profile = getProfile(part);
    if (!profile) return 0;
    const GevrOpticMode *mode = &profile->modes[profile->selected];
    if (mode->highZoom <= mode->lowZoom) return 0;
    profile->zoom = fminf(mode->highZoom, fmaxf(mode->lowZoom, profile->zoom * expf(amount)));
    return 1;
}

int gevrTacticalOpticCanAdjust(int part)
{
    OpticProfile *profile = getProfile(part);
    if (!profile) return 0;
    const GevrOpticMode *mode = &profile->modes[profile->selected];
    return profile->count > 1 || mode->highZoom > mode->lowZoom;
}

int gevrTacticalOpticCanFlip(int part)
{
    const OpticProfile *profile = getProfile(part);
    return profile && profile->flipMode > 0;
}

void gevrTacticalOpticFlickReset(void)
{
    s_flick.part = -1;
    s_flick.state = FLICK_READY;
}

int gevrTacticalOpticFlick(int part, unsigned tick, float alongCm, float acrossSquaredCm)
{
    OpticProfile *profile = getProfile(part);
    if (!profile || profile->flipMode <= 0) return 0;
    if (s_flick.part != part) {
        gevrTacticalOpticFlickReset();
        s_flick.part = part;
    }
    const float distanceSquared = alongCm * alongCm + acrossSquaredCm;
    if (s_flick.state == FLICK_USED) {
        if (distanceSquared > 6.5f * 6.5f) s_flick.state = FLICK_READY;
        return 0;
    }
    if (acrossSquaredCm > 4.5f * 4.5f || distanceSquared > 7.5f * 7.5f) {
        s_flick.state = FLICK_READY;
        return 0;
    }
    /* The fingertips must pass through the magnifier and travel along the
     * hinge's swing. Let the same hand leave the mount before it can rearm. */
    if (s_flick.state == FLICK_ARMED && tick - s_flick.started <= 18) {
        if (alongCm < 1.7f || alongCm - s_flick.startCm < 2.6f) return 0;
        profile->selected = profile->modes[profile->selected].folded ? 0 : profile->flipMode;
        profile->zoom = profile->modes[profile->selected].lowZoom;
        s_flick.state = FLICK_USED;
        return 1;
    }
    s_flick.state = FLICK_READY;
    if (distanceSquared <= 4.25f * 4.25f && alongCm <= 1.25f) {
        s_flick.state = FLICK_ARMED;
        s_flick.started = tick;
        s_flick.startCm = alongCm;
    }
    return 0;
}

void gevrTacticalOpticsReset(void)
{
    gevrTacticalOpticFlickReset();
    for (int i = 0; i < GEVR_GEAR_PARTS; ++i) {
        OpticProfile *profile = &s_optics[i];
        if (profile->status <= 0) continue;
        profile->selected = 0;
        profile->zoom = profile->modes[0].lowZoom;
    }
}
