#define MMX_CONFIG_IMPLEMENTATION
#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "util.h"

static MmxHostRendererBackend g_mmx_host_renderer = kMmxHostRenderer_Auto;
static bool g_mmx_host_renderer_initialized;

static MmxHostRendererBackend BackendFromLegacyOutputMethod(void) {
  switch (g_config.output_method) {
    case kOutputMethod_SDLSoftware:
      return kMmxHostRenderer_Software;
    case kOutputMethod_OpenGL:
      return kMmxHostRenderer_OpenGL;
    case kOutputMethod_SDL:
    default:
      return kMmxHostRenderer_Auto;
  }
}

static bool ParseHostRendererName(const char *value,
                                  MmxHostRendererBackend *backend) {
  if (StringEqualsNoCase(value, "SDL") ||
      StringEqualsNoCase(value, "Auto") ||
      StringEqualsNoCase(value, "SDL-Auto")) {
    *backend = kMmxHostRenderer_Auto;
  } else if (StringEqualsNoCase(value, "SDL-Software") ||
             StringEqualsNoCase(value, "Software")) {
    *backend = kMmxHostRenderer_Software;
  } else if (StringEqualsNoCase(value, "OpenGL")) {
    *backend = kMmxHostRenderer_OpenGL;
  } else if (StringEqualsNoCase(value, "Direct3D") ||
             StringEqualsNoCase(value, "Direct3D9") ||
             StringEqualsNoCase(value, "D3D9")) {
    *backend = kMmxHostRenderer_Direct3D9;
  } else if (StringEqualsNoCase(value, "Direct3D11") ||
             StringEqualsNoCase(value, "D3D11")) {
    *backend = kMmxHostRenderer_Direct3D11;
  } else if (StringEqualsNoCase(value, "DirectDraw") ||
             StringEqualsNoCase(value, "DDraw")) {
    *backend = kMmxHostRenderer_DirectDraw;
  } else if (StringEqualsNoCase(value, "Vulkan")) {
    *backend = kMmxHostRenderer_Vulkan;
  } else {
    return false;
  }
  return true;
}

static bool ParseHostRendererFromFile(const char *filename,
                                      MmxHostRendererBackend *backend) {
  const char *path = filename ? filename : "config.ini";
  char *data = (char *)ReadWholeFile(path, NULL);
  if (!data)
    return false;

  bool found = false;
  int section = -1;
  char *iter = data;
  char *line;
  while ((line = NextLineStripComments(&iter)) != NULL) {
    if (!*line)
      continue;
    if (*line == '[') {
      section = StringEqualsNoCase(line, "[Graphics]") ? 1 : 0;
      continue;
    }
    if (section != 1)
      continue;

    char *value = SplitKeyValue(line);
    if (value && StringEqualsNoCase(line, "OutputMethod")) {
      if (!ParseHostRendererName(value, backend)) {
        fprintf(stderr,
                "Warning: unknown Graphics/OutputMethod '%s'; using SDL auto\n",
                value);
        *backend = kMmxHostRenderer_Auto;
      }
      found = true;
    }
  }

  free(data);
  return found;
}

static void ApplyLegacyRoute(MmxHostRendererBackend backend) {
  switch (backend) {
    case kMmxHostRenderer_Software:
      g_config.output_method = kOutputMethod_SDLSoftware;
      break;
    case kMmxHostRenderer_OpenGL:
    case kMmxHostRenderer_DirectDraw:
      /* DirectDraw uses the existing explicit-renderer route so main.c calls
       * OpenGLRenderer_Create(); that factory dispatches DirectDraw before it
       * creates any GL context. This keeps the shared main loop unchanged. */
      g_config.output_method = kOutputMethod_OpenGL;
      break;
    case kMmxHostRenderer_Direct3D9:
    case kMmxHostRenderer_Direct3D11:
    case kMmxHostRenderer_Vulkan:
    case kMmxHostRenderer_Auto:
    default:
      g_config.output_method = kOutputMethod_SDL;
      break;
  }
}

void MmxParseConfigFile(const char *filename) {
  ParseConfigFile(filename);

  MmxHostRendererBackend backend = g_mmx_host_renderer;
  bool explicitly_selected = ParseHostRendererFromFile(filename, &backend);
  if (explicitly_selected || !g_mmx_host_renderer_initialized) {
    if (!explicitly_selected)
      backend = BackendFromLegacyOutputMethod();
    g_mmx_host_renderer = backend;
    g_mmx_host_renderer_initialized = true;
  }

  ApplyLegacyRoute(g_mmx_host_renderer);
  fprintf(stderr, "Host renderer = %s\n", MmxHostRenderer_GetName());
}

MmxHostRendererBackend MmxHostRenderer_GetBackend(void) {
  return g_mmx_host_renderer;
}

const char *MmxHostRenderer_GetName(void) {
  switch (g_mmx_host_renderer) {
    case kMmxHostRenderer_Software:   return "SDL Software";
    case kMmxHostRenderer_OpenGL:     return "OpenGL";
    case kMmxHostRenderer_Direct3D9:  return "Direct3D 9";
    case kMmxHostRenderer_Direct3D11: return "Direct3D 11";
    case kMmxHostRenderer_DirectDraw: return "DirectDraw";
    case kMmxHostRenderer_Vulkan:     return "Vulkan";
    case kMmxHostRenderer_Auto:
    default:                           return "SDL Auto";
  }
}

const char *MmxHostRenderer_GetSdlDriverName(void) {
  switch (g_mmx_host_renderer) {
    case kMmxHostRenderer_Direct3D9:  return "direct3d";
    case kMmxHostRenderer_Direct3D11: return "direct3d11";
    case kMmxHostRenderer_Vulkan:     return "vulkan";
    default:                           return NULL;
  }
}

bool MmxHostRenderer_IsDirectDraw(void) {
  return g_mmx_host_renderer == kMmxHostRenderer_DirectDraw;
}
