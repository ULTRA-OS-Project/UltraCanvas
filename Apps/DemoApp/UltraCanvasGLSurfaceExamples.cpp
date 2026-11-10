// Apps/DemoApp/UltraCanvasGLSurfaceExamples.cpp
// OpenGL Surface showcase: a tabbed demo combining three GL surfaces —
//   1. Complex 3D models loaded from .obj files (Phong shaded, mouse-orbit)
//   2. A shader playground of animated full-screen fragment effects
//   3. "Zarch" — a low-poly hover-ship simulation over scrolling biome terrain
// The per-tab content is built in the UltraCanvasGL*Tab.cpp files.
// Version: 2.1.0 - the shared page look (UltraCanvasDemoPageKit): kit title and
//                  description, the tabs in a card, the no-GL notice as a card
// Version: 2.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"

#ifdef ULTRACANVAS_ENABLE_GL

#include "UltraCanvasGLDemoSupport.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"

namespace UltraCanvas {

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateGLSurfaceExamples() {
    auto page = DemoKit::CreatePage("GLSurfaceExamples",
            Tr("OpenGL 3D Showcase — Hardware-Accelerated Rendering"),
            Tr("Real OpenGL 3.3 Core rendering composited into UltraCanvas — models, shaders, and a Zarch-style simulation"));
    auto card = DemoKit::AddCard(page, "GLSurfaceCard");

    // Sized so the per-tab content area (width × height-tabHeight) comfortably
    // contains every tab's children: the control panel reaches x≈986 and the
    // Shaders tab's source viewer reaches y≈680, so the content area must be at
    // least ~986 × ~680. 1004 × (726-34=692) clears both with margin and still
    // fits inside the demo's display column, so no tab ever shows scrollbars.
    // The tabs' children are placed absolutely, so the tabs keep that size.
    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("GLShowcaseTabs", 0, 0, 1004, 726);
    tabs->SetTabHeight(34);
    tabs->SetTabMinWidth(150);
    tabs->AddTab("3D Models", CreateGLModelsTab());
    tabs->AddTab("Shaders", CreateGLShaderTab());
    tabs->AddTab("Zarch", CreateGLZarchTab());
    tabs->SetActiveTab(0);
    DemoKit::AddFixed(card, tabs);

    return page;
}

} // namespace UltraCanvas

#else // !ULTRACANVAS_ENABLE_GL

#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateGLSurfaceExamples() {
    auto page = DemoKit::CreatePage("GLSurfaceExamples", Tr("OpenGL Surface Examples"), "");

    // Tinted red: the feature is missing from this build.
    auto card = DemoKit::AddCard(page, "GLSurfacePlaceholder", Tr("OpenGL Surface Support - NOT AVAILABLE"));
    card->SetBackgroundColor(Color(255, 200, 200, 100));
    DemoKit::AddText(card, "GLSurfacePlaceholderText", Tr("This build was compiled without OpenGL support."));
    DemoKit::AddText(card, "GLSurfacePlaceholderHow", Tr("To enable OpenGL surface support:"));
    DemoKit::Add(card, DemoKit::CreateBullets("GLSurfacePlaceholderSteps", {
        Tr("Rebuild with -DULTRACANVAS_ENABLE_GL=ON"),
        Tr("Ensure OpenGL development libraries are installed"),
        Tr("On Linux: libgl-dev, libegl-dev"),
        Tr("On macOS: OpenGL framework (included with Xcode)")}));

    return page;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_ENABLE_GL
