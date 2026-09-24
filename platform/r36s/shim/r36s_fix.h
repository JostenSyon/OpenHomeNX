#pragma once
// Fix globale X/Y e A/B per R36S su OH_LINUX: intercetta SDL_PollEvent e
// scambia le due coppie. Layout fisico R36S (stile Xbox: A=sud, B=est,
// X=ovest, Y=nord) e' ruotato rispetto a quello Switch che il resto del
// codice assume (A=est, B=sud, X=nord, Y=ovest) -- stesso scambio in
// diagonale per entrambe le coppie, non solo X/Y.
// Includiamo questo header via -include in tutti i TU, cosi' ogni PollEvent passa di qui.
#ifdef OH_LINUX
#include <SDL2/SDL.h>
static inline int r36s_SDL_PollEvent(SDL_Event* e) {
    // Chiama il vero SDL_PollEvent (non ancora ridefinito qui, quindi è l'originale)
    int r = SDL_PollEvent(e);
    if (r && (e->type == SDL_CONTROLLERBUTTONDOWN || e->type == SDL_CONTROLLERBUTTONUP)) {
        switch (e->cbutton.button) {
            case SDL_CONTROLLER_BUTTON_X: e->cbutton.button = SDL_CONTROLLER_BUTTON_Y; break;
            case SDL_CONTROLLER_BUTTON_Y: e->cbutton.button = SDL_CONTROLLER_BUTTON_X; break;
            case SDL_CONTROLLER_BUTTON_A: e->cbutton.button = SDL_CONTROLLER_BUTTON_B; break;
            case SDL_CONTROLLER_BUTTON_B: e->cbutton.button = SDL_CONTROLLER_BUTTON_A; break;
            default: break;
        }
    }
    return r;
}
#undef SDL_PollEvent
#define SDL_PollEvent r36s_SDL_PollEvent
#endif