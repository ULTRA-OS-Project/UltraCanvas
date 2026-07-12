// Apps/DemoApp/UltraCanvasGLSurfaceExamples.cpp
// OpenGL Surface showcase: a tabbed demo combining three GL surfaces —
//   1. Complex 3D models loaded from .obj files (Phong shaded, mouse-orbit)
//   2. A shader playground of animated full-screen fragment effects
//   3. "Zarch" — a low-poly hover-ship simulation over scrolling biome terrain
// The per-tab content is built in the UltraCanvasGL*Tab.cpp files.
// Version: 2.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"

#ifdef ULTRACANVAS_ENABLE_GL

#include "UltraCanvasGLDemoSupport.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"

namespace UltraCanvas {

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateGLSurfaceExamples() {
    auto mainContainer = std::make_shared<UltraCanvasContainer>("GLSurfaceExamples", 0, 0, 1024, 800);

    auto title = std::make_shared<UltraCanvasLabel>("GLSurfaceTitle", 16, 8, 900, 28);
    title->SetText("OpenGL 3D Showcase — Hardware-Accelerated Rendering");
    title->SetFontSize(16);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(50, 50, 150, 255));
    mainContainer->AddChild(title);

    auto subtitle = std::make_shared<UltraCanvasLabel>("GLSurfaceSubtitle", 16, 36, 900, 20);
    subtitle->SetText("Real OpenGL 3.3 Core rendering composited into UltraCanvas — models, shaders, and a Zarch-style simulation");
    subtitle->SetFontSize(11);
    subtitle->SetTextColor(Color(100, 100, 100, 255));
    mainContainer->AddChild(subtitle);

    // Sized so the per-tab content area (width × height-tabHeight) comfortably
    // contains every tab's children: the control panel reaches x≈986 and the
    // Shaders tab's source viewer reaches y≈680, so the content area must be at
    // least ~986 × ~680. 1004 × (726-34=692) clears both with margin and still
    // fits inside the demo's display column, so no tab ever shows scrollbars.
    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("GLShowcaseTabs", 12, 62, 1004, 726);
    tabs->SetTabHeight(34);
    tabs->SetTabMinWidth(150);
    tabs->AddTab("3D Models", CreateGLModelsTab());
    tabs->AddTab("Shaders", CreateGLShaderTab());
    tabs->AddTab("Zarch", CreateGLZarchTab());
    tabs->SetActiveTab(0);
    mainContainer->AddChild(tabs);

    return mainContainer;
}

} // namespace UltraCanvas

#else // !ULTRACANVAS_ENABLE_GL

#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateGLSurfaceExamples() {
    auto container = std::make_shared<UltraCanvasContainer>("GLSurfaceExamples", 0, 0, 1000, 600);

    auto title = std::make_shared<UltraCanvasLabel>("GLSurfaceTitle", 10, 10, 300, 30);
    title->SetText("OpenGL Surface Examples");
    title->SetFontSize(16);
    title->SetFontWeight(FontWeight::Bold);
    container->AddChild(title);

    auto placeholder = std::make_shared<UltraCanvasLabel>("GLSurfacePlaceholder", 20, 50, 800, 400);
    placeholder->SetText(
        "OpenGL Surface Support - NOT AVAILABLE\n\n"
        "This build was compiled without OpenGL support.\n\n"
        "To enable OpenGL surface support:\n"
        "• Rebuild with -DULTRACANVAS_ENABLE_GL=ON\n"
        "• Ensure OpenGL development libraries are installed\n"
        "• On Linux: libgl-dev, libegl-dev\n"
        "• On macOS: OpenGL framework (included with Xcode)\n"
    );
    placeholder->SetAlignment(TextAlignment::Left);
    placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
    placeholder->SetBorders(2.0f);
    placeholder->SetPadding(20.0f);
    container->AddChild(placeholder);

    return container;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_ENABLE_GL
