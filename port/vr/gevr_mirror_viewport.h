#ifndef GEVR_MIRROR_VIEWPORT_H
#define GEVR_MIRROR_VIEWPORT_H

/* Centre-crop an eye into the monitor without changing its aspect ratio. */
typedef struct GevrMirrorViewport { int x, y, w, h; } GevrMirrorViewport;
static inline GevrMirrorViewport gevrMirrorViewport(int sourceW, int sourceH, int destW, int destH, int fill)
{
    GevrMirrorViewport v = {0, 0, destW, destH};
    if (!fill || sourceW <= 0 || sourceH <= 0 || destW <= 0 || destH <= 0) return v;
    if ((double)destW * sourceH > (double)destH * sourceW)
        v.h = (int)((double)destW * sourceH / sourceW + .5);
    else
        v.w = (int)((double)destH * sourceW / sourceH + .5);
    v.x = (destW - v.w) / 2;
    v.y = (destH - v.h) / 2;
    return v;
}
#endif
