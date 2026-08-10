#define MMX_CONFIG_IMPLEMENTATION
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static MmxHostRendererBackend g_mmx_host_renderer = kMmxHostRenderer_Auto;
static bool g_mmx_host_renderer_initialized;
/* True once the new HostRenderer key (or the launcher) selected a concrete
 * host backend.  While true, a legacy OutputMethod in config.local.ini must
 * not silently downgrade that richer selection.  A HostRenderer in the local
 * override may still intentionally replace it. */
static bool g_mmx_host_renderer_explicit;

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

typedef struct MmxRendererFileSelection {
  bool found_host_renderer;
  bool found_output_method;
  MmxHostRendererBackend host_renderer;
  MmxHostRendererBackend output_renderer;
} MmxRendererFileSelection;

static MmxRendererFileSelection ParseRendererSelectionFromFile(
    const char *filename) {
  MmxRendererFileSelection result;
  memset(&result, 0, sizeof(result));
  result.host_renderer = kMmxHostRenderer_Auto;
  result.output_renderer = kMmxHostRenderer_Auto;

  const char *path = filename ? filename : "config.ini";
  char *data = (char *)ReadWholeFile(path, NULL);
  if (!data)
    return result;

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
    if (!value)
      continue;

    if (StringEqualsNoCase(line, "HostRenderer") ||
        StringEqualsNoCase(line, "RendererBackend")) {
      if (!ParseHostRendererName(value, &result.host_renderer)) {
        fprintf(stderr,
                "Warning: unknown Graphics/HostRenderer '%s'; using SDL auto\n",
                value);
        result.host_renderer = kMmxHostRenderer_Auto;
      }
      result.found_host_renderer = true;
    } else if (StringEqualsNoCase(line, "OutputMethod")) {
      if (!ParseHostRendererName(value, &result.output_renderer)) {
        result.output_renderer = kMmxHostRenderer_Auto;
      }
      result.found_output_method = true;
    }
  }

  free(data);
  return result;
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
       * creates any GL context. */
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

static void SetBackendInternal(MmxHostRendererBackend backend,
                               bool explicit_host_renderer) {
  if ((unsigned)backend > (unsigned)kMmxHostRenderer_Vulkan)
    backend = kMmxHostRenderer_Auto;
  g_mmx_host_renderer = backend;
  g_mmx_host_renderer_initialized = true;
  g_mmx_host_renderer_explicit = explicit_host_renderer;
  ApplyLegacyRoute(backend);
}

void MmxParseConfigFile(const char *filename) {
  /* Let the framework parse every legacy setting first.  For new renderers it
   * will intentionally collapse OutputMethod to SDL/OpenGL; HostRenderer below
   * restores the richer host-side choice afterwards. */
  ParseConfigFile(filename);

  MmxRendererFileSelection file = ParseRendererSelectionFromFile(filename);
  const char *source = "inherited";

  if (file.found_host_renderer) {
    SetBackendInternal(file.host_renderer, true);
    source = "HostRenderer";
  } else if (!g_mmx_host_renderer_explicit && file.found_output_method) {
    /* Backward compatibility for configs created before HostRenderer existed.
     * Once an explicit HostRenderer has been seen (normally in config.ini), a
     * later config.local.ini containing only legacy OutputMethod is not allowed
     * to erase it. */
    SetBackendInternal(file.output_renderer, false);
    source = "OutputMethod";
  } else if (!g_mmx_host_renderer_initialized) {
    SetBackendInternal(BackendFromLegacyOutputMethod(), false);
    source = "legacy-state";
  } else {
    ApplyLegacyRoute(g_mmx_host_renderer);
  }

  fprintf(stderr,
          "Host renderer config: path=%s source=%s backend=%s route=%u explicit=%d\n",
          filename ? filename : "config.ini", source,
          MmxHostRenderer_GetName(), (unsigned)g_config.output_method,
          g_mmx_host_renderer_explicit ? 1 : 0);
}

MmxHostRendererBackend MmxHostRenderer_GetBackend(void) {
  return g_mmx_host_renderer;
}

void MmxHostRenderer_SetBackend(MmxHostRendererBackend backend) {
  /* A UI choice is authoritative and should survive legacy OutputMethod writes. */
  SetBackendInternal(backend, true);
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

const char *MmxHostRenderer_GetConfigValue(void) {
  switch (g_mmx_host_renderer) {
    case kMmxHostRenderer_Software:   return "SDL-Software";
    case kMmxHostRenderer_OpenGL:     return "OpenGL";
    case kMmxHostRenderer_Direct3D9:  return "Direct3D9";
    case kMmxHostRenderer_Direct3D11: return "Direct3D11";
    case kMmxHostRenderer_DirectDraw: return "DirectDraw";
    case kMmxHostRenderer_Vulkan:     return "Vulkan";
    case kMmxHostRenderer_Auto:
    default:                           return "SDL";
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

static char *TrimForConfigParse(char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  char *hash = strchr(s, '#');
  if (hash)
    *hash = '\0';
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
               s[n - 1] == '\r'))
    s[--n] = '\0';
  return s;
}

void MmxHostRenderer_PersistConfig(const char *filename) {
  const char *path = filename ? filename : "config.ini";
  size_t data_size = 0;
  char *data = (char *)ReadWholeFile(path, &data_size);
  if (!data) {
    fprintf(stderr, "Warning: unable to read renderer config %s\n", path);
    return;
  }

  FILE *out = fopen(path, "wb");
  if (!out) {
    fprintf(stderr, "Warning: unable to persist renderer to %s\n", path);
    free(data);
    return;
  }

  bool in_graphics = false;
  bool saw_graphics = false;
  bool wrote_renderer = false;
  const char *value = MmxHostRenderer_GetConfigValue();
  const char *p = data;
  const char *end = data + data_size;

  while (p < end) {
    const char *newline = memchr(p, '\n', (size_t)(end - p));
    const char *line_end = newline ? newline : end;
    size_t raw_len = (size_t)(line_end - p);

    char parse[2048];
    size_t copy_len = raw_len < sizeof(parse) - 1 ? raw_len : sizeof(parse) - 1;
    memcpy(parse, p, copy_len);
    parse[copy_len] = '\0';
    char *trimmed = TrimForConfigParse(parse);
    bool is_section = trimmed[0] == '[';

    if (is_section) {
      if (in_graphics && !wrote_renderer) {
        fprintf(out, "HostRenderer = %s\n", value);
        wrote_renderer = true;
      }
      in_graphics = StringEqualsNoCase(trimmed, "[Graphics]");
      if (in_graphics)
        saw_graphics = true;
    }

    bool replace = false;
    if (in_graphics && !is_section && *trimmed) {
      char *config_value = SplitKeyValue(trimmed);
      if (config_value &&
          (StringEqualsNoCase(trimmed, "HostRenderer") ||
           StringEqualsNoCase(trimmed, "RendererBackend")))
        replace = true;
    }

    if (replace) {
      fprintf(out, "HostRenderer = %s", value);
      if (newline)
        fputc('\n', out);
      wrote_renderer = true;
    } else {
      fwrite(p, 1, raw_len, out);
      if (newline)
        fputc('\n', out);
    }

    p = newline ? newline + 1 : end;
  }

  if (in_graphics && !wrote_renderer) {
    if (data_size && data[data_size - 1] != '\n')
      fputc('\n', out);
    fprintf(out, "HostRenderer = %s\n", value);
    wrote_renderer = true;
  }
  if (!saw_graphics) {
    if (data_size && data[data_size - 1] != '\n')
      fputc('\n', out);
    fprintf(out, "\n[Graphics]\nHostRenderer = %s\n", value);
  }

  fclose(out);
  free(data);

  fprintf(stderr, "Host renderer persisted: path=%s HostRenderer=%s\n",
          path, value);
}
