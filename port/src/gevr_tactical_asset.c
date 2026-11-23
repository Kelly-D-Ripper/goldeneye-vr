#ifdef GEVR_TACTICAL_QUEST
#define _BSD_SOURCE
#include "gevr_tactical_asset.h"
#include <SDL.h>

static int readAsset(void *source, char *output, int bytes)
{
    const size_t count = SDL_RWread(source, output, 1, bytes);
    return count > (size_t)bytes ? -1 : (int)count;
}

static fpos_t seekAsset(void *source, fpos_t offset, int whence)
{
    return SDL_RWseek(source, offset, whence);
}

static int closeAsset(void *source)
{
    return SDL_RWclose(source);
}

/* SDL reads Android assets with the APK's AssetManager. Keep the stdio
 * readers shared with the Windows Workshop and allow local file overrides. */
FILE *gevrTacticalOpenAsset(const char *path, const char *mode)
{
    FILE *file = fopen(path, mode);
    if (file) return file;

    SDL_RWops *source = SDL_RWFromFile(path, "rb");
    if (!source) return NULL;
    file = funopen(source, readAsset, NULL, seekAsset, closeAsset);
    if (!file) SDL_RWclose(source);
    return file;
}
#endif
