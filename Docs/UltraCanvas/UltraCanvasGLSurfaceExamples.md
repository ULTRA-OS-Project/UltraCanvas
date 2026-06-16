# UltraCanvasGLSurface Documentation

## Overview

**UltraCanvasGLSurface** is a UI element that hosts a real OpenGL rendering context inside the UltraCanvas widget tree. It owns an EGL/OpenGL context, renders to an offscreen framebuffer (FBO), and composites the result into the surrounding Cairo surface, letting hardware-accelerated 3D content live side-by-side with regular UltraCanvas widgets.

**Version:** 1.0.0
**Header:** `include/UltraCanvasGLSurface.h`
**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasUIElement`

> The header is gated by `ULTRACANVAS_ENABLE_GL`. Build with `-DULTRACANVAS_ENABLE_GL=ON` and link the OpenGL/EGL libraries (`libgl-dev`, `libegl-dev` on Linux; the OpenGL framework on macOS).

## Features

- **OpenGL 3.3 Core profile** by default; configurable major/minor version, profile, depth/stencil, color bits, MSAA, and sRGB framebuffer
- **Three render modes** — `OnDemand`, `Continuous`, and `TimedUpdate`
- **Callback-based rendering** via `Init`, `Render`, `Resize`, and `Cleanup` callbacks (or override the virtual hooks)
- **FBO-backed offscreen rendering** with automatic resize on bounds changes
- **CPU-readback compositing** into the Cairo surface (zero-copy GPU compositing planned)
- **Per-frame timing** delivered through `RenderSurfaceInfo` (delta time, frame number, device pixel ratio)

## Header Include

```cpp
#ifdef ULTRACANVAS_ENABLE_GL
#include "UltraCanvasGLSurface.h"
#endif

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif
```

## Class Reference

### Constructors

```cpp
UltraCanvasGLSurface(const std::string& identifier = "GLSurface",
                     int x = 0, int y = 0, int width = 300, int height = 300);

UltraCanvasGLSurface(const GLSurfaceConfig& config,
                     const std::string& identifier = "GLSurface",
                     int x = 0, int y = 0, int width = 300, int height = 300);
```

The class is non-copyable but movable.

### GLSurfaceConfig

```cpp
struct GLSurfaceConfig {
    int glVersionMajor = 3;     // OpenGL major version (default 3.3 Core)
    int glVersionMinor = 3;
    bool coreProfile = true;    // Core (vs Compatibility) profile
    int depthBits   = 24;       // Depth buffer bits (0 to disable)
    int stencilBits = 8;        // Stencil buffer bits (0 to disable)
    int samples     = 0;        // MSAA samples (0 = disabled, 2/4/8/16)
    int redBits = 8, greenBits = 8, blueBits = 8, alphaBits = 8;
    bool sRGB         = false;  // Request sRGB framebuffer
    bool debugContext = false;  // Enable GL debug output

    static GLSurfaceConfig Default();
    static GLSurfaceConfig WithMSAA(int sampleCount);
    static GLSurfaceConfig NoDepthStencil();
};
```

`SetConfig()` must be called before the first render; once the GL context is created the config is locked.

### Render modes

```cpp
enum class RenderMode {
    OnDemand,    // Render only when RequestRender() is called (default)
    Continuous,  // Render every frame (for animations)
    TimedUpdate  // Render at a fixed interval
};

void SetRenderMode(RenderMode mode, float interval = 1.0f / 60.0f);
RenderMode GetRenderMode() const;
float GetUpdateInterval() const;
```

### Callbacks

```cpp
using RenderCallback  = std::function<void(const RenderSurfaceInfo&)>;
using InitCallback    = std::function<void()>;
using ResizeCallback  = std::function<void(int width, int height)>;
using CleanupCallback = std::function<void()>;

void SetRenderCallback(RenderCallback callback);
void SetInitCallback(InitCallback callback);
void SetResizeCallback(ResizeCallback callback);
void SetCleanupCallback(CleanupCallback callback);
```

Alternatively, subclass and override the virtual hooks:

```cpp
virtual void OnGLInit();
virtual void OnGLRender(const RenderSurfaceInfo& info);
virtual void OnGLResize(int width, int height);
virtual void OnGLCleanup();
```

### RenderSurfaceInfo

Passed to each render callback / `OnGLRender` invocation:

```cpp
struct RenderSurfaceInfo {
    int width;                // Surface width in pixels
    int height;               // Surface height in pixels
    float devicePixelRatio;   // For HiDPI/Retina displays (1.0 = standard)
    double deltaTime;         // Time since last frame in seconds
    uint64_t frameNumber;     // Monotonically increasing frame counter
};
```

### Rendering control

```cpp
void RequestRender();          // Schedule a render (for OnDemand mode)
bool IsInitialized() const;
bool IsContextValid() const;
```

### Context access

```cpp
bool MakeCurrent();
void ReleaseCurrent();
void* GetNativeGLContext() const;
```

### Surface dimensions

```cpp
int GetSurfaceWidth() const;   // May differ from element bounds due to DPI scaling
int GetSurfaceHeight() const;
```

### Factory functions

```cpp
std::shared_ptr<UltraCanvasGLSurface> CreateGLSurface(
    const std::string& identifier,
    int x, int y, int width, int height,
    const GLSurfaceConfig& config = GLSurfaceConfig());

std::shared_ptr<UltraCanvasGLSurface> CreateGLSurface(
    int x, int y, int width, int height,
    const GLSurfaceConfig& config = GLSurfaceConfig());
```

## Events / Callbacks

| Callback           | Fired When                                                                 |
|--------------------|----------------------------------------------------------------------------|
| `InitCallback`     | GL context has been created and made current; create shaders/VBOs/textures |
| `RenderCallback`   | Each frame, with up-to-date `RenderSurfaceInfo`                            |
| `ResizeCallback`   | The element bounds (and thus the FBO size) have changed                    |
| `CleanupCallback`  | Just before the context is torn down; delete GL resources here             |

## The 3D Showcase Demo

The **OpenGL 3D Showcase** entry in the demo application (`Graphics3D → OpenGL 3D Showcase`)
hosts three independent `UltraCanvasGLSurface` instances inside a tabbed container, each
showing a different facet of the GL surface:

| Tab | Source | What it demonstrates |
|-----|--------|----------------------|
| **3D Models** | `UltraCanvasGLModelsTab.cpp` | Loads complex Wavefront `.obj` meshes from `media/models/` (torus knot, spring coil, ico-sphere) into the surface and renders them with a two-light Phong + rim shader. A `UltraCanvasGLSurface` subclass turns mouse drag into an orbit camera and the wheel into zoom; 4× MSAA smooths the silhouettes. |
| **Shaders** | `UltraCanvasGLShaderTab.cpp` | A shader playground: one full-screen triangle (emitted from `gl_VertexID`, no vertex buffer) driven entirely by animated fragment shaders — plasma, an SDF ray-marched scene with soft shadows, an animated Julia set, a polar tunnel, a parallax warp starfield, a Twigl/つぶやきGLSL "geek-mode" "Borg Sphere" lattice one-liner, a numerically-integrated Rössler strange attractor (`dx/dt=−y−z`, `dy/dt=x+ay`, `dz/dt=b+z(x−c)`, `a=b=0.2`, `c=5.7`; the integrator is clamped to a bounding box so divergent parameter sweeps stay on-screen), a fragment-shader port of the "Ball Surface" p5.js sketch (four noise-warped rings of `F`-weighted white splats), a second Twigl one-liner, "Pulse" (a per-pixel ray-fold accumulating tinted glow), and "Mandala" — the 12-wave interference intensity `I(r) ∝ |Σ¹²A·e^{i(k·r+φ)}|²` with `|k| ∈ [24,32]` (a 12-fold quasicrystal). The base inputs are `uTime` and `uResolution`; selecting the Rössler, Ball Surface, Pulse or Mandala effect also reveals live parameter sliders — Rössler `a`/`b`/`c` (sweep `c` ≈ 4→9 to watch the attractor period-double into chaos), Ball Surface brightness / splat radius, Pulse step scale / fold phase, and Mandala wave number `|k|` / fold symmetry / phase spread — pushed in as extra uniforms. Three more Twigl one-liners are also included: "Fragments" (a marched tube with quantised sine turbulence, with live fold-count / tube-radius sliders), "Mountains" (a log-polar fractal terrain with HSV-tinted glow), "Horizon" (a turbulent landscape under a glowing sky), and "Protostar2" (a glowing volumetric core folded about a twisting axis). "Circles" — an openFrameworks (C++) generative sketch — is also included; it is not a fragment shader but a 2D node-link network (180 Perlin-noise-placed nodes, links under 50px, rings sized by neighbour count), drawn with native 2D GL geometry through a custom render path in the same surface. A "⛶" icon button overlaid on the canvas — or double-clicking the canvas — maximizes it over the whole tab (hiding the controls and source viewer); pressing Esc or a single click restores the layout. A read-only `UltraCanvasTextArea` under the canvas shows each effect's generating source (the original Twigl/p5.js snippet or closed-form formula as a header, followed by the live GLSL) with true GLSL syntax highlighting — a dedicated GLSL grammar (`vec*`/`mat*`/`sampler*` types and built-ins like `normalize`/`mix`/`texture`) was added to `UltraCanvasSyntaxTokenizer`. |
| **Zarch** | `UltraCanvasGLZarchTab.cpp` | A miniature real-time simulation inspired by *Zarch* (Acorn Archimedes, 1987): a checkerboarded biome terrain (water/sand/grass/rock/snow) is rebuilt on the CPU each frame and scrolls toward the camera while a flat-shaded hover-ship bobs and banks above it, dropping a soft ground shadow, surrounded by cone "trees" and an additive particle exhaust trail. Drag to steer; release to auto-pilot. |

Shared math (vectors, matrices), the OBJ loader, and GL shader helpers live in the
header-only `Apps/DemoApp/UltraCanvasGLDemoSupport.h`. The model meshes are generated by
`media/models/generate_models.py` and committed as `.obj` files.

## Usage Examples

The snippets below come from the original single-surface spinning-cube example and remain
the simplest end-to-end illustration of the `UltraCanvasGLSurface` callback lifecycle.

### Configuring and creating the surface

```cpp
GLSurfaceConfig config;
config.glVersionMajor = 3;
config.glVersionMinor = 3;
config.coreProfile = true;
config.depthBits   = 24;
config.stencilBits = 0;
config.samples     = 1;   // No MSAA for simplicity

auto glSurface = std::make_shared<UltraCanvasGLSurface>(
    config, "SpinningCube", 20, 70, 600, 500);
glSurface->SetRenderMode(RenderMode::Continuous);  // Animate continuously
```

### Init callback — create GL resources

```cpp
auto glResources = std::make_shared<GLResources>();  // shader, VAO, VBO, uniform locations

glSurface->SetInitCallback([glResources]() {
    glResources->program = CreateShaderProgram();
    if (!glResources->program) {
        std::cerr << "[GLSurfaceDemo] Failed to create shader program" << std::endl;
        return;
    }

    glResources->modelViewLoc  = glGetUniformLocation(glResources->program, "uModelView");
    glResources->projectionLoc = glGetUniformLocation(glResources->program, "uProjection");
    glResources->colorLoc      = glGetUniformLocation(glResources->program, "uColor");

    glGenVertexArrays(1, &glResources->vao);
    glGenBuffers(1, &glResources->vbo);

    glBindVertexArray(glResources->vao);
    glBindBuffer(GL_ARRAY_BUFFER, glResources->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glResources->initialized = true;
});
```

### Render callback — draw each frame

`info.deltaTime` provides framerate-independent animation:

```cpp
glSurface->SetRenderCallback([glResources, cubeState](const RenderSurfaceInfo& info) {
    if (!glResources->initialized) return;

    glClearColor(cubeState->clearColor.r / 255.0f,
                 cubeState->clearColor.g / 255.0f,
                 cubeState->clearColor.b / 255.0f,
                 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (cubeState->spinning) {
        cubeState->rotationY += static_cast<float>(info.deltaTime) * cubeState->rotationSpeed;
        cubeState->rotationX += static_cast<float>(info.deltaTime) * cubeState->rotationSpeed * 0.7f;
    }

    float modelView[16], projection[16];
    float aspect = static_cast<float>(info.width) / static_cast<float>(info.height);
    MatrixPerspective(projection, 45.0f * 3.14159f / 180.0f, aspect, 0.1f, 100.0f);

    MatrixIdentity(modelView);
    MatrixTranslate(modelView, 0.0f, 0.0f, -3.0f);
    MatrixRotateX(modelView, cubeState->rotationX);
    MatrixRotateY(modelView, cubeState->rotationY);

    glUseProgram(glResources->program);
    glUniformMatrix4fv(glResources->modelViewLoc,  1, GL_FALSE, modelView);
    glUniformMatrix4fv(glResources->projectionLoc, 1, GL_FALSE, projection);

    glBindVertexArray(glResources->vao);
    for (int face = 0; face < 6; face++) {
        glUniform4fv(glResources->colorLoc, 1, &faceColors[face * 4]);
        glDrawArrays(GL_TRIANGLE_FAN, face * 4, 4);
    }
    glBindVertexArray(0);
    glUseProgram(0);
});
```

### Cleanup callback — release GL resources

```cpp
glSurface->SetCleanupCallback([glResources]() {
    if (glResources->vbo)     { glDeleteBuffers(1, &glResources->vbo);          glResources->vbo = 0; }
    if (glResources->vao)     { glDeleteVertexArrays(1, &glResources->vao);     glResources->vao = 0; }
    if (glResources->program) { glDeleteProgram(glResources->program);          glResources->program = 0; }
    glResources->initialized = false;
});
```

### Driving rotation from UltraCanvas widgets

Regular UltraCanvas widgets coexist with the GL surface — here, a button toggles spinning and a slider sets rotation speed:

```cpp
auto spinButton = std::make_shared<UltraCanvasButton>("SpinButton", 10, 45, 150, 30);
spinButton->SetText("Pause Rotation");
spinButton->onClick = [spinButton, cubeState]() {
    cubeState->spinning = !cubeState->spinning;
    spinButton->SetText(cubeState->spinning ? "Pause Rotation" : "Resume Rotation");
};

auto speedSlider = std::make_shared<UltraCanvasSlider>("SpeedSlider", 10, 115, 300, 25);
speedSlider->SetRange(0.1f, 5.0f);
speedSlider->SetValue(1.0f);
speedSlider->onValueChanged = [cubeState](float value) {
    cubeState->rotationSpeed = value;
};
```

### OnDemand rendering pattern

When the scene only changes in response to user input, prefer `OnDemand` to avoid burning CPU/GPU:

```cpp
auto surface = std::make_shared<UltraCanvasGLSurface>(config, "Viewer", x, y, w, h);
surface->SetRenderMode(RenderMode::OnDemand);

// ... after mutating scene state:
surface->RequestRender();
```

## Compositing and Performance Notes

- Each frame the GL context renders into an offscreen FBO, then the pixel buffer is read back via `glReadPixels` and blitted into the Cairo surface.
- CPU readback is the simplest correct compositing path on every platform but adds bandwidth cost; the framework plans GPU-native compositing (EGL Image / DMA-BUF zero-copy) in a future revision.
- `Continuous` mode invokes the render callback every paint pass. Use `OnDemand` whenever practical.
- The FBO is recreated lazily on resize; expensive per-frame allocations should be cached in the init callback.

## Best Practices

1. **Allocate all GL resources in the Init callback** and free them in the Cleanup callback — do not assume the context is alive at constructor/destructor time.
2. **Always check `glResources->initialized` at the top of your render callback** — initialization failure is silent (it just logs to `stderr`).
3. **Use `info.deltaTime` for animation**, not wall-clock time — it matches how often the surface is actually rendering.
4. **Prefer `OnDemand`** for static or input-driven scenes; reserve `Continuous` for true animation.
5. **Unbind state at the end of each render** (`glBindVertexArray(0)`, `glUseProgram(0)`) — the framework does not assume GL state is owned by the surface.

## Platform Notes

- **Linux:** uses EGL on top of the X11/Wayland surface
- **macOS:** uses the OpenGL framework via `#include <OpenGL/gl3.h>`
- **Windows:** uses WGL/EGL depending on the build configuration

## See Also

- [UltraCanvasUIElement](UltraCanvasUIElement.md) — Base class documentation
- [UltraCanvasRenderContext](UltraCanvasRenderContext.md) — 2D rendering pipeline
- [UltraCanvasContainer](UltraCanvasContainer.md) — Hosting GL surfaces inside layouts
