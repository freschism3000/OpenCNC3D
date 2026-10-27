/* Compile/link verification only. This program is never launched. */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <gl4esinit.h>
int main(void) {
    SDL_SetMainReady();
    if (SDL_Init(0) != 0) return 1;
    /* Keep init and proc lookup in the link without calling GL with no context. */
    volatile void *init = (void *)&initialize_gl4es;
    volatile void *lookup = (void *)&gl4es_GetProcAddress;
    SDL_Quit();
    return init && lookup ? 0 : 2;
}
