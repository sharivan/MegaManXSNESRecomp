/* Mega Man X host-side extension of snesrecomp's SDL2/SDL3 compatibility
 * layer. Keep the framework header authoritative for every compatibility shim;
 * only renderer creation is overridden so Graphics/OutputMethod can request a
 * concrete SDL render driver. */
#ifndef MMX_DESKTOP_SDL_COMPAT_H
#define MMX_DESKTOP_SDL_COMPAT_H

#include "../../snesrecomp/runner/src/desktop/sdl_compat.h"
#include "../config.h"

#include <string.h>

static inline SDL_Renderer *mmx_sdl_create_renderer(
    SDL_Window *window, bool software, bool vsync) {
  const char *driver = software ? "software" : MmxHostRenderer_GetSdlDriverName();

#if SNESRECOMP_SDL3
  SDL_Renderer *renderer = SDL_CreateRenderer(window, driver);
  if (renderer && !software)
    SDL_SetRenderVSync(renderer, vsync ? 1 : 0);
  return renderer;
#else
  if (software) {
    return SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  }

  int driver_index = -1;
  if (driver) {
    int count = SDL_GetNumRenderDrivers();
    for (int i = 0; i < count; i++) {
      SDL_RendererInfo info;
      if (SDL_GetRenderDriverInfo(i, &info) == 0 && info.name &&
          strcmp(info.name, driver) == 0) {
        driver_index = i;
        break;
      }
    }
    if (driver_index < 0) {
      SDL_SetError("Requested renderer '%s' is not available in this SDL2 build",
                   driver);
      return NULL;
    }
  }

  return SDL_CreateRenderer(
      window, driver_index,
      SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
#endif
}

/* The framework implementation is a static inline, so replace subsequent call
 * sites in game-owned translation units with the extended version. */
#undef snesrecomp_sdl_create_renderer
#define snesrecomp_sdl_create_renderer mmx_sdl_create_renderer

#endif
