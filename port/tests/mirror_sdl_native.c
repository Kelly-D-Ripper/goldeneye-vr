#include <SDL.h>
#include <stdio.h>
int main(void) {
    SDL_Window *w;
    SDL_GLContext ctx;
    SDL_DisplayMode mode;
    int width, height;
    if (SDL_Init(SDL_INIT_VIDEO) || SDL_GetCurrentDisplayMode(0, &mode)) return 1;
    w = SDL_CreateWindow("GoldenEye mirror test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        916, 960, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!w || !(ctx = SDL_GL_CreateContext(w))) { fprintf(stderr, "%s\n",SDL_GetError()); return 2; }
    /* SDL does not apply fullscreen dimensions until a window is shown. Keep
     * this test transparent to avoid covering the user's existing work. */
    if (SDL_SetWindowOpacity(w, 0.0f)) { fprintf(stderr, "could not make test window transparent: %s\n", SDL_GetError()); return 7; }
    SDL_ShowWindow(w);
    if (SDL_SetWindowFullscreen(w, SDL_WINDOW_FULLSCREEN_DESKTOP)) { fprintf(stderr, "%s\n",SDL_GetError()); return 3; }
    SDL_GetWindowSize(w, &width, &height);
    printf("transparent SDL fullscreen: %dx%d, display %dx%d, fullscreen flag %u\n", width,height,mode.w,mode.h,
        !!(SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN));
    if (width != mode.w || height != mode.h || !(SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN)) return 4;
    if (SDL_SetWindowFullscreen(w,0)) return 5;
    SDL_SetWindowSize(w, 916, 960);
    SDL_GetWindowSize(w, &width, &height);
    if (width != 916 || height != 960 || (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN)) return 6;
    SDL_GL_DeleteContext(ctx); SDL_DestroyWindow(w); SDL_Quit();
    puts("hidden SDL mirror window restored successfully");
    return 0;
}
