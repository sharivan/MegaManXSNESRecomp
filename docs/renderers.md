# Host rendering backends

MegaManXSNESRecomp renders the SNES PPU into a 32-bit CPU framebuffer and then presents that framebuffer through a selectable host graphics API.

Set `OutputMethod` in the `[Graphics]` section of `config.ini`:

```ini
[Graphics]
OutputMethod = Direct3D11
```

Supported values are:

| Value | Presentation path | Platform notes |
| --- | --- | --- |
| `SDL` | SDL chooses the accelerated renderer | Cross-platform default |
| `SDL-Software` | SDL software renderer | Cross-platform |
| `OpenGL` | Existing OpenGL 3.3 presenter | Cross-platform where OpenGL 3.3 is available |
| `Direct3D9` | SDL `direct3d` render driver | Windows |
| `Direct3D11` | SDL `direct3d11` render driver | Windows |
| `DirectDraw` | Native DirectDraw primary/offscreen surfaces | Windows |
| `Vulkan` | SDL `vulkan` render driver | Requires an SDL build that includes the Vulkan renderer and a working Vulkan driver/ICD |

`D3D9`/`Direct3D` and `D3D11` are accepted aliases. `Software`, `Auto`, `SDL-Auto`, and `DDraw` are also accepted.

## Behavior shared by all backends

The PPU renderer itself is unchanged. All host backends consume the same ARGB8888 framebuffer, so the old/new PPU renderer toggle, no-sprite-limits mode, experimental widescreen rendering, display-aspect selection, fullscreen/window resizing, and save-state behavior are independent of the host graphics API.

The SDL-backed Direct3D/Vulkan paths use the same streaming texture path as the existing SDL renderer, including nearest/linear filtering and VSync. Benchmark mode disables SDL VSync as before.

The DirectDraw path locks a 32-bit system-memory offscreen surface and lets the PPU write into it directly. It then clears the client area, computes the same aspect-correct viewport used by OpenGL/SDL, waits for vertical blank outside benchmark mode, and performs a scaled `Blt` to the primary surface. DirectDraw scaling/filtering quality is driver-defined; GLSL shaders remain OpenGL-only.

## SDL2 fallback

The project defaults to SDL3. The SDL2 compatibility build can explicitly select any renderer name present in that SDL2 build. Direct3D9 and Direct3D11 are commonly available on Windows SDL2 builds; Vulkan presentation should be considered an SDL3 path unless the particular SDL build actually exposes a `vulkan` 2D render driver.
