#ifndef GEVR_TACTICAL_ASSET_H
#define GEVR_TACTICAL_ASSET_H

#include <stdio.h>

#ifdef GEVR_TACTICAL_QUEST
FILE *gevrTacticalOpenAsset(const char *path, const char *mode);
#else
static inline FILE *gevrTacticalOpenAsset(const char *path, const char *mode)
{
    return fopen(path, mode);
}
#endif

#endif
