#include "third_party/gl_core/gl_core_3_1.h"
#include "desktop/sdl_compat.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "util.h"
#include "glsl_shader.h"
#include "config.h"
#include "mmx_display.h"

#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#if !SNESRECOMP_SDL3
#include <SDL_syswm.h>
#endif
#endif

#define CODE(...) #__VA_ARGS__

static SDL_Window *g_window;
static uint8 *g_screen_buffer;
static size_t g_screen_buffer_size;
static int g_draw_width, g_draw_height;
static unsigned int g_program, g_VAO;
static GlTextureWithSize g_texture;
static GlslShader *g_glsl_shader;

static void GL_APIENTRY MessageCallback(GLenum source,
                GLenum type,
                GLuint id,
                GLenum severity,
                GLsizei length,
                const GLchar *message,
                const void *userParam) {
  if (type == GL_DEBUG_TYPE_OTHER)
    return;

  fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
          (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""),
          type, severity, message);
  if (type == GL_DEBUG_TYPE_ERROR)
    Die("OpenGL error!\n");
}

static bool OpenGLRenderer_Init(SDL_Window *window) {
  g_window = window;
  SDL_GLContext context = SDL_GL_CreateContext(window);
  (void)context;

  SDL_GL_SetSwapInterval(1);
  ogl_LoadFunctions();

  if (!ogl_IsVersionGEQ(3, 3))
    Die("You need OpenGL 3.3");

  if (kDebugFlag) {
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(MessageCallback, 0);
  }

  glGenTextures(1, &g_texture.gl_texture);

  static const float kVertices[] = {
    // positions          // texture coords
    -1.0f,  1.0f, 0.0f,   0.0f, 0.0f, // top left
    -1.0f, -1.0f, 0.0f,   0.0f, 1.0f, // bottom left
     1.0f,  1.0f, 0.0f,   1.0f, 0.0f, // top right
     1.0f, -1.0f, 0.0f,   1.0f, 1.0f,  // bottom right
  };

  // create a vertex buffer object
  unsigned int vbo;
  glGenBuffers(1, &vbo);

  // vertex array object
  glGenVertexArrays(1, &g_VAO);
  // 1. bind Vertex Array Object
  glBindVertexArray(g_VAO);
  // 2. copy our vertices array in a buffer for OpenGL to use
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);
  // position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  // texture coord attribute
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // vertex shader
  const GLchar *vs_code = "#version 330 core\n" CODE(
  layout(location = 0) in vec3 aPos;
  layout(location = 1) in vec2 aTexCoord;
  out vec2 TexCoord;
  void main(void) {
    gl_Position = vec4(aPos, 1.0);
    TexCoord = vec2(aTexCoord.x, aTexCoord.y);
  }
);

  unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &vs_code, NULL);
  glCompileShader(vs);

  int success;
  char infolog[512];
  glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(vs, 512, NULL, infolog);
    printf("%s\n", infolog);
  }

  // fragment shader
  const GLchar *fs_code = "#version 330 core\n" CODE(
  out vec4 FragColor;
  in vec2 TexCoord;
  // texture samplers
  uniform sampler2D texture1;
  void main(void) {
    FragColor = texture(texture1, TexCoord);
  }
);

  unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &fs_code, NULL);
  glCompileShader(fs);

  glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(fs, 512, NULL, infolog);
    printf("%s\n", infolog);
  }

  glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(fs, 512, NULL, infolog);
    printf("%s\n", infolog);
  }

  // create program
  int program = g_program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glGetProgramiv(program, GL_LINK_STATUS, &success);

  if (!success) {
    glGetProgramInfoLog(program, 512, NULL, infolog);
    printf("%s\n", infolog);
  }

  if (g_config.shader)
    g_glsl_shader = GlslShader_CreateFromFile(g_config.shader);
  
  return true;
}

static void OpenGLRenderer_GetOutputSize(int *width, int *height) {
  snesrecomp_sdl_get_drawable_size(g_window, width, height);
}

static void OpenGLRenderer_Destroy(void) {
}

static void OpenGLRenderer_BeginDraw(int width, int height, uint8 **pixels, int *pitch) {
  int size = width * height;

  if ((size_t)size > g_screen_buffer_size) {
    g_screen_buffer_size = (size_t)size;
    free(g_screen_buffer);
    g_screen_buffer = (uint8*)malloc((size_t)size * 4);
  }

  g_draw_width = width;
  g_draw_height = height;
  *pixels = g_screen_buffer;
  *pitch = width * 4;
}

static void OpenGLRenderer_EndDraw(void) {
  int drawable_width, drawable_height;

  snesrecomp_sdl_get_drawable_size(
      g_window, &drawable_width, &drawable_height);
  
  MmxDisplayViewport viewport;
  MmxDisplay_ComputeViewport(g_draw_width, g_draw_height,
                             drawable_width, drawable_height,
                             SnesDisplayAspect_Clamp(g_config.display_aspect),
                             g_config.ignore_aspect_ratio, false,
                             &viewport);
  int viewport_width = viewport.width, viewport_height = viewport.height;
  int viewport_x = viewport.x;
  int viewport_y = viewport.y;

  glBindTexture(GL_TEXTURE_2D, g_texture.gl_texture);
  if (g_draw_width == g_texture.width && g_draw_height == g_texture.height) {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_draw_width, g_draw_height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, g_screen_buffer);
  } else {
    g_texture.width = g_draw_width;
    g_texture.height = g_draw_height;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_draw_width, g_draw_height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, g_screen_buffer);
  }

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  if (g_glsl_shader == NULL) {
    glViewport(viewport_x, viewport_y, viewport_width, viewport_height);
    glUseProgram(g_program);
    int filter = g_config.linear_filtering ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glBindVertexArray(g_VAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  } else {
    GlslShader_Render(g_glsl_shader, &g_texture, viewport_x, viewport_y, viewport_width, viewport_height);
  }

  SDL_GL_SwapWindow(g_window);
}

static const struct RendererFuncs kOpenGLRendererFuncs = {
  &OpenGLRenderer_Init,
  &OpenGLRenderer_Destroy,
  &OpenGLRenderer_GetOutputSize,
  &OpenGLRenderer_BeginDraw,
  &OpenGLRenderer_EndDraw,
};

#ifdef _WIN32
/* -------------------------------------------------------------------------
 * DirectDraw presenter
 *
 * DirectDraw is deliberately loaded from ddraw.dll at runtime. This keeps the
 * existing MSVC/CMake link lines unchanged and lets modern Windows systems
 * decide whether the compatibility implementation is available. The SNES PPU
 * writes directly into a 32-bit system-memory DirectDraw surface; presentation
 * is a single scaled Blt to the primary surface after vertical blank.
 * ------------------------------------------------------------------------- */
typedef HRESULT (WINAPI *MmxDirectDrawCreateProc)(
    GUID FAR *guid, LPDIRECTDRAW FAR *direct_draw, IUnknown FAR *outer);

static HMODULE g_ddraw_module;
static LPDIRECTDRAW g_ddraw;
static LPDIRECTDRAWSURFACE g_ddraw_primary;
static LPDIRECTDRAWSURFACE g_ddraw_frame;
static LPDIRECTDRAWCLIPPER g_ddraw_clipper;
static HWND g_ddraw_hwnd;
static int g_ddraw_width, g_ddraw_height;
static bool g_ddraw_locked;

static HWND DirectDrawRenderer_GetHwnd(SDL_Window *window) {
#if SNESRECOMP_SDL3
  SDL_PropertiesID props = SDL_GetWindowProperties(window);
  return (HWND)SDL_GetPointerProperty(
      props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
#else
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo(window, &info))
    return NULL;
  return info.info.win.window;
#endif
}

static void DirectDrawRenderer_ReleaseFrame(void) {
  if (g_ddraw_locked && g_ddraw_frame) {
    IDirectDrawSurface_Unlock(g_ddraw_frame, NULL);
    g_ddraw_locked = false;
  }
  if (g_ddraw_frame) {
    IDirectDrawSurface_Release(g_ddraw_frame);
    g_ddraw_frame = NULL;
  }
  g_ddraw_width = g_ddraw_height = 0;
}

static bool DirectDrawRenderer_CreateFrame(int width, int height) {
  DirectDrawRenderer_ReleaseFrame();

  DDSURFACEDESC desc;
  memset(&desc, 0, sizeof(desc));
  desc.dwSize = sizeof(desc);
  desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
  desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
  desc.dwWidth = (DWORD)width;
  desc.dwHeight = (DWORD)height;
  desc.ddpfPixelFormat.dwSize = sizeof(desc.ddpfPixelFormat);
  desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
  desc.ddpfPixelFormat.dwRGBBitCount = 32;
  desc.ddpfPixelFormat.dwRBitMask = 0x00ff0000;
  desc.ddpfPixelFormat.dwGBitMask = 0x0000ff00;
  desc.ddpfPixelFormat.dwBBitMask = 0x000000ff;

  HRESULT hr = IDirectDraw_CreateSurface(
      g_ddraw, &desc, &g_ddraw_frame, NULL);
  if (FAILED(hr)) {
    fprintf(stderr,
            "DirectDraw: failed to create %dx%d X8R8G8B8 frame surface "
            "(HRESULT=0x%08lx)\n",
            width, height, (unsigned long)hr);
    return false;
  }

  g_ddraw_width = width;
  g_ddraw_height = height;
  return true;
}

static bool DirectDrawRenderer_Init(SDL_Window *window) {
  g_window = window;
  g_ddraw_hwnd = DirectDrawRenderer_GetHwnd(window);
  if (!g_ddraw_hwnd) {
    fprintf(stderr, "DirectDraw: unable to obtain Win32 HWND from SDL: %s\n",
            SDL_GetError());
    return false;
  }

  g_ddraw_module = LoadLibraryA("ddraw.dll");
  if (!g_ddraw_module) {
    fprintf(stderr, "DirectDraw: ddraw.dll is unavailable\n");
    return false;
  }

  MmxDirectDrawCreateProc create_ddraw =
      (MmxDirectDrawCreateProc)GetProcAddress(g_ddraw_module, "DirectDrawCreate");
  if (!create_ddraw) {
    fprintf(stderr, "DirectDraw: DirectDrawCreate export is unavailable\n");
    return false;
  }

  HRESULT hr = create_ddraw(NULL, &g_ddraw, NULL);
  if (FAILED(hr) || !g_ddraw) {
    fprintf(stderr, "DirectDraw: DirectDrawCreate failed (HRESULT=0x%08lx)\n",
            (unsigned long)hr);
    return false;
  }

  hr = IDirectDraw_SetCooperativeLevel(g_ddraw, g_ddraw_hwnd, DDSCL_NORMAL);
  if (FAILED(hr)) {
    fprintf(stderr,
            "DirectDraw: SetCooperativeLevel failed (HRESULT=0x%08lx)\n",
            (unsigned long)hr);
    return false;
  }

  DDSURFACEDESC primary_desc;
  memset(&primary_desc, 0, sizeof(primary_desc));
  primary_desc.dwSize = sizeof(primary_desc);
  primary_desc.dwFlags = DDSD_CAPS;
  primary_desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
  hr = IDirectDraw_CreateSurface(
      g_ddraw, &primary_desc, &g_ddraw_primary, NULL);
  if (FAILED(hr)) {
    fprintf(stderr,
            "DirectDraw: primary surface creation failed (HRESULT=0x%08lx)\n",
            (unsigned long)hr);
    return false;
  }

  hr = IDirectDraw_CreateClipper(g_ddraw, 0, &g_ddraw_clipper, NULL);
  if (SUCCEEDED(hr) && g_ddraw_clipper) {
    IDirectDrawClipper_SetHWnd(g_ddraw_clipper, 0, g_ddraw_hwnd);
    IDirectDrawSurface_SetClipper(g_ddraw_primary, g_ddraw_clipper);
  }

  if (g_config.shader)
    fprintf(stderr,
            "Warning: GLSL shaders are supported only with the OpenGL backend\n");
  if (g_config.linear_filtering)
    fprintf(stderr,
            "DirectDraw: LinearFiltering is driver-defined for scaled Blt output\n");

  fprintf(stderr, "DirectDraw renderer initialized (system-memory X8R8G8B8)\n");
  return true;
}

static void DirectDrawRenderer_Destroy(void) {
  DirectDrawRenderer_ReleaseFrame();
  if (g_ddraw_clipper) {
    IDirectDrawClipper_Release(g_ddraw_clipper);
    g_ddraw_clipper = NULL;
  }
  if (g_ddraw_primary) {
    IDirectDrawSurface_Release(g_ddraw_primary);
    g_ddraw_primary = NULL;
  }
  if (g_ddraw) {
    IDirectDraw_Release(g_ddraw);
    g_ddraw = NULL;
  }
  if (g_ddraw_module) {
    FreeLibrary(g_ddraw_module);
    g_ddraw_module = NULL;
  }
  g_ddraw_hwnd = NULL;
}

static void DirectDrawRenderer_GetOutputSize(int *width, int *height) {
  RECT rect;
  if (g_ddraw_hwnd && GetClientRect(g_ddraw_hwnd, &rect)) {
    *width = rect.right - rect.left;
    *height = rect.bottom - rect.top;
  } else {
    *width = *height = 0;
  }
}

static void DirectDrawRenderer_BeginDraw(
    int width, int height, uint8 **pixels, int *pitch) {
  if (!g_ddraw_frame || width != g_ddraw_width || height != g_ddraw_height) {
    if (!DirectDrawRenderer_CreateFrame(width, height))
      Die("DirectDraw framebuffer creation failed");
  }

  DDSURFACEDESC desc;
  memset(&desc, 0, sizeof(desc));
  desc.dwSize = sizeof(desc);
  HRESULT hr = IDirectDrawSurface_Lock(
      g_ddraw_frame, NULL, &desc, DDLOCK_WAIT, NULL);
  if (hr == DDERR_SURFACELOST) {
    IDirectDrawSurface_Restore(g_ddraw_frame);
    hr = IDirectDrawSurface_Lock(
        g_ddraw_frame, NULL, &desc, DDLOCK_WAIT, NULL);
  }
  if (FAILED(hr) || !desc.lpSurface)
    Die("DirectDraw framebuffer lock failed");

  g_ddraw_locked = true;
  g_draw_width = width;
  g_draw_height = height;
  *pixels = (uint8 *)desc.lpSurface;
  *pitch = (int)desc.lPitch;
}

static void DirectDrawRenderer_EndDraw(void) {
  if (!g_ddraw_frame || !g_ddraw_primary)
    return;

  if (g_ddraw_locked) {
    IDirectDrawSurface_Unlock(g_ddraw_frame, NULL);
    g_ddraw_locked = false;
  }

  RECT client;
  if (!GetClientRect(g_ddraw_hwnd, &client))
    return;
  int output_width = client.right - client.left;
  int output_height = client.bottom - client.top;
  if (output_width <= 0 || output_height <= 0)
    return;

  POINT origin = {0, 0};
  ClientToScreen(g_ddraw_hwnd, &origin);
  RECT output_rect = {
      origin.x, origin.y, origin.x + output_width, origin.y + output_height};

  MmxDisplayViewport viewport;
  MmxDisplay_ComputeViewport(g_draw_width, g_draw_height,
                             output_width, output_height,
                             SnesDisplayAspect_Clamp(g_config.display_aspect),
                             g_config.ignore_aspect_ratio, false,
                             &viewport);
  RECT dest = {
      origin.x + viewport.x,
      origin.y + viewport.y,
      origin.x + viewport.x + viewport.width,
      origin.y + viewport.y + viewport.height};
  RECT src = {0, 0, g_draw_width, g_draw_height};

  DDBLTFX fill;
  memset(&fill, 0, sizeof(fill));
  fill.dwSize = sizeof(fill);
  fill.dwFillColor = 0;
  HRESULT hr = IDirectDrawSurface_Blt(
      g_ddraw_primary, &output_rect, NULL, NULL,
      DDBLT_COLORFILL | DDBLT_WAIT, &fill);
  if (hr == DDERR_SURFACELOST) {
    IDirectDrawSurface_Restore(g_ddraw_primary);
    IDirectDrawSurface_Restore(g_ddraw_frame);
    IDirectDrawSurface_Blt(
        g_ddraw_primary, &output_rect, NULL, NULL,
        DDBLT_COLORFILL | DDBLT_WAIT, &fill);
  }

  if (g_benchmark_frames == 0)
    IDirectDraw_WaitForVerticalBlank(g_ddraw, DDWAITVB_BLOCKBEGIN, NULL);

  hr = IDirectDrawSurface_Blt(
      g_ddraw_primary, &dest, g_ddraw_frame, &src, DDBLT_WAIT, NULL);
  if (hr == DDERR_SURFACELOST) {
    IDirectDrawSurface_Restore(g_ddraw_primary);
    IDirectDrawSurface_Restore(g_ddraw_frame);
    hr = IDirectDrawSurface_Blt(
        g_ddraw_primary, &dest, g_ddraw_frame, &src, DDBLT_WAIT, NULL);
  }
  if (FAILED(hr) && kDebugFlag) {
    fprintf(stderr, "DirectDraw: Blt failed (HRESULT=0x%08lx)\n",
            (unsigned long)hr);
  }
}

static const struct RendererFuncs kDirectDrawRendererFuncs = {
  &DirectDrawRenderer_Init,
  &DirectDrawRenderer_Destroy,
  &DirectDrawRenderer_GetOutputSize,
  &DirectDrawRenderer_BeginDraw,
  &DirectDrawRenderer_EndDraw,
};
#endif

void OpenGLRenderer_Create(struct RendererFuncs *funcs) {
  if (MmxHostRenderer_IsDirectDraw()) {
#ifdef _WIN32
    *funcs = kDirectDrawRendererFuncs;
    return;
#else
    fprintf(stderr,
            "Warning: DirectDraw is Windows-only; falling back to OpenGL\n");
#endif
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  *funcs = kOpenGLRendererFuncs;
}
