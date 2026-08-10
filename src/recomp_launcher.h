/* Mega Man X wrapper around recomp-ui's public launcher ABI.
 *
 * recomp-ui already supports a game-supplied renderer vocabulary through
 * RecompLauncherCGameInfo::renderer_labels. Use that generic capability here
 * instead of forking or teaching the shared launcher about SNES-specific host
 * renderer enums. */
#ifndef MMX_RECOMP_LAUNCHER_SHIM_H
#define MMX_RECOMP_LAUNCHER_SHIM_H

#include "../recomp-ui/src/recomp_launcher.h"
#include "config.h"
#include <stdio.h>

static inline int MmxLauncherRendererIndex(MmxHostRendererBackend backend) {
#ifdef _WIN32
  return (int)backend;
#else
  switch (backend) {
    case kMmxHostRenderer_Software: return 1;
    case kMmxHostRenderer_OpenGL:   return 2;
    case kMmxHostRenderer_Vulkan:   return 3;
    case kMmxHostRenderer_Auto:
    default:                         return 0;
  }
#endif
}

static inline MmxHostRendererBackend MmxLauncherRendererBackend(int index) {
#ifdef _WIN32
  if ((unsigned)index <= (unsigned)kMmxHostRenderer_Vulkan)
    return (MmxHostRendererBackend)index;
  return kMmxHostRenderer_Auto;
#else
  switch (index) {
    case 1: return kMmxHostRenderer_Software;
    case 2: return kMmxHostRenderer_OpenGL;
    case 3: return kMmxHostRenderer_Vulkan;
    case 0:
    default: return kMmxHostRenderer_Auto;
  }
#endif
}

static inline int MmxRecompLauncherRunWindow(
    const char *window_title,
    RecompLauncherCSettings *io,
    const RecompLauncherCGameInfo *game,
    const char *assets_dir,
    const char *initial_rom,
    char *out_rom_path,
    size_t out_rom_path_len) {
#ifdef _WIN32
  static const char *const kRendererLabels[] = {
      "SDL (Auto)", "SDL Software", "OpenGL", "Direct3D 9",
      "Direct3D 11", "DirectDraw", "Vulkan",
  };
#else
  static const char *const kRendererLabels[] = {
      "SDL (Auto)", "SDL Software", "OpenGL", "Vulkan",
  };
#endif

  RecompLauncherCGameInfo game_copy;
  const RecompLauncherCGameInfo *launcher_game = game;
  if (game) {
    game_copy = *game;
    game_copy.has_renderer = 1;
    game_copy.renderer_labels = kRendererLabels;
    game_copy.num_renderers =
        (int)(sizeof(kRendererLabels) / sizeof(kRendererLabels[0]));
    launcher_game = &game_copy;
  }

  if (io) {
    io->renderer = MmxLauncherRendererIndex(MmxHostRenderer_GetBackend());
    fprintf(stderr,
            "Launcher renderer seed: backend=%s index=%d legacy_output=%d config=%s\n",
            MmxHostRenderer_GetName(), io->renderer, io->output_method,
            (game && game->config_path) ? game->config_path : "(default)");
  }

  /* This name still denotes recomp-ui's real function here. The macro redirect
   * is intentionally declared only after this wrapper definition. */
  int result = recomp_launcher_run_window(
      window_title, io, launcher_game, assets_dir, initial_rom,
      out_rom_path, out_rom_path_len);

  if (io) {
    fprintf(stderr,
            "Launcher renderer return: result=%d index=%d legacy_output=%d\n",
            result, io->renderer, io->output_method);
  }

  if (io && (result == RECOMP_LAUNCHER_RESULT_LAUNCH ||
             result == RECOMP_LAUNCHER_RESULT_RELAUNCH)) {
    MmxHostRenderer_SetBackend(MmxLauncherRendererBackend(io->renderer));
    /* main.c copies io->output_method back into g_config immediately after the
     * launcher returns. Keep that legacy field synchronized with our richer
     * selection so DirectDraw still takes the explicit-renderer factory route
     * and D3D/Vulkan still take the SDL route. */
    io->output_method = g_config.output_method;
    fprintf(stderr,
            "Launcher renderer applied: backend=%s index=%d route=%d\n",
            MmxHostRenderer_GetName(), io->renderer, io->output_method);
    MmxHostRenderer_PersistConfig(game ? game->config_path : NULL);
  }

  return result;
}

#define recomp_launcher_run_window MmxRecompLauncherRunWindow

#endif
