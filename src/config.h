#pragma once

/*
 * Game-local extension of snesrecomp's desktop configuration contract.
 *
 * The framework intentionally keeps the historical three-way OutputMethod
 * enum (SDL / SDL-Software / OpenGL). Mega Man X additionally exposes named
 * host presentation backends while preserving that ABI: the wrapper parser
 * records the requested backend, then maps it onto the legacy route that the
 * existing main loop already knows how to initialize.
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
const char *MmxHostRenderer_GetName(void);
const char *MmxHostRenderer_GetSdlDriverName(void);
bool MmxHostRenderer_IsDirectDraw(void);

/* main.c intentionally stays on the shared framework API. Redirect its parser
 * call to the game-local wrapper unless this translation unit is implementing
 * that wrapper and needs the original framework symbol. */
#ifndef MMX_CONFIG_IMPLEMENTATION
#define ParseConfigFile MmxParseConfigFile
#endif
