// Tests/Fuzz/HTMLBuilderFuzz.cpp
// Fuzz target: HTML through the whole reader - parser, cascade, element
// builder - and the CSSLayout engine laying the result out at a phone and a
// desktop width, as UltraMail shows a message. This is where hostile CSS
// meets real work: flex and grid tracks, spans and line numbers, tables,
// floats and percentages become elements and layout passes, so a value that
// slips past the resolver's limits shows up here as a hang or a crash.
//
// Links the full UltraCanvas library (text is measured on an offscreen
// context), so it runs as a deterministic smoke test (FuzzSmokeMain.cpp)
// from the main build rather than as a sanitizer-instrumented libFuzzer
// target. See Tests/Fuzz/README.md.
// Version: 1.0.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLElementBuilder.h"
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasRenderContext.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

using namespace UltraCanvas;

namespace {

// A container that owns an offscreen render context, so the labels below it
// can measure their text without a window.
struct Host : UltraCanvasContainer {
    Host() : UltraCanvasContainer("host") {}
    void Adopt(std::unique_ptr<IRenderContext> c) { renderContext = std::move(c); }
};

void LayOut(const std::string& html, float width) {
    auto host = std::make_shared<Host>();
    host->Adopt(CreateRenderContext(Size2Di(static_cast<int>(width), 600), nullptr));
    HTML::BuildOptions opts;
    opts.viewportWidth = width;
    // Every picture is missing: the builder's fallbacks run, and no decoder.
    opts.resourceLoader = [](const std::string&) { return std::vector<uint8_t>{}; };
    opts.onLinkActivated = [](const std::string&) {};
    HTML::ElementBuilder builder;
    HTML::BuildResult built = builder.Build(html, opts);
    if (!built.root) return;
    built.root->size.width = CSSLayout::Dimension::Px(width);
    host->AddChild(built.root);
    CSSLayout::LayoutContext ctx;
    ctx.viewportWidth = width;
    ctx.viewportHeight = 600;
    CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, width },
                                      { CSSLayout::ConstraintMode::Unbounded, INFINITY } };
    built.root->Measure(mc, ctx);
    const float height = built.root->measured.measuredHeight;
    built.root->Arrange(Rect2Df{ 0, 0, width, std::isfinite(height) ? height : 0.f }, ctx);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static const bool initialized = [] {
        UCImage::InitializeImageSubsysterm("HTMLBuilderFuzz");
        return true;
    }();
    (void)initialized;
    if (size > 64 * 1024) return 0;
    const std::string html(reinterpret_cast<const char*>(data), size);
    LayOut(html, 360.f);
    LayOut(html, 1024.f);
    return 0;
}
