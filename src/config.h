#pragma once

/*
 * Game-local extension of snesrecomp's desktop configuration contract.
 *
 * The framework intentionally keeps the historical three-way OutputMethod
 * enum (SDL / SDL-Software / OpenGL). Mega Man X additionally exposes named
 * host presentation backends while preserving that ABI. HostRenderer is the
 * authoritative extended setting; OutputMethod remains the legacy route used
 * by the shared main loop and framework serializer.
 */
#include "../snesrecomp/runner/src/desktop/config.h"

typedef enum MmxHostRendererBackend {
  kMmxHostRenderer_Auto = 0,
  kMmxHostRenderer_Software,
  kMmxHostRenderer_OpenGL,
  kMmxHostRenderer_Direct3D9,
  kMmxHostRenderer_Direct3D11,
  kMmxHostRenderer_DirectDraw,
  kMmxHostRenderer_Vulkan,
} MmxHostRendererBackend;

void MmxParseConfigFile(const char *filename);
MmxHostRendererBackend MmxHostRenderer_GetBackend(void);
void MmxHostRenderer_SetBackend(MmxHostRendererBackend backend);
const char *MmxHostRenderer_GetName(void);
const char *MmxHostRenderer_GetConfigValue(void);
const char *MmxHostRenderer_GetSdlDriverName(void);
bool MmxHostRenderer_IsDirectDraw(void);
void MmxHostRenderer_PersistConfig(const char *filename);

/* main.c owns benchmark mode; explicit presenters use it to suppress VSync so
 * renderer benchmarks measure throughput instead of refresh cadence. */
extern int g_benchmark_frames;

/* main.c intentionally stays on the shared framework API. Redirect its parser
 * and writer calls to the game-local wrappers unless this translation unit is
 * implementing those wrappers and needs the original framework symbols.
 *
 * The framework writer persists all legacy settings, then the wrapper writes
 * the independent HostRenderer key. Because the framework does not know that
 * key, future WriteConfigFile calls preserve it rather than collapsing an
 * explicit D3D9/D3D11/DirectDraw/Vulkan selection back to SDL/OpenGL. */
#ifndef MMX_CONFIG_IMPLEMENTATION
static inline void MmxWriteConfigFile(const char *filename) {
  WriteConfigFile(filename);
  MmxHostRenderer_PersistConfig(filename);
}
#define ParseConfigFile MmxParseConfigFile
#define WriteConfigFile MmxWriteConfigFile
#endif
