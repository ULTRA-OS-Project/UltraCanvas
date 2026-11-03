// Tests/MediaViewerDetailsPanelTest.cpp
// The media viewer's Details panel, and the Markdown table it shows.
//
// The panel is a group box: its caption is the title of what is shown
// ("Image information") - not a heading at the top of the Markdown text - and
// the "x" at the caption's right end closes it. For a GIF it lists the size
// of the colour table and shows the table itself as swatches under the text,
// in the file's order; a file without one shows no swatches.
//
// A Markdown table is drawn as one grid: the header's fill meets the rule
// under it (the rule used to sit in the middle of the separator row, with a
// band of background between them), and every rule - the column dividers too,
// which were drawn in the text colour - is one pixel wide in the border
// colour.
//
// Headless: the viewer and the text area sit under a container that owns an
// offscreen render context, which is what lays out and draws them.
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasGroupBox.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasColorSwatchBar.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasPathUtf8.h"

#include <cairo/cairo.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

// A container that owns an offscreen render context, so the elements under it
// lay out and draw without a window.
struct Host : UltraCanvasContainer {
    Host() : UltraCanvasContainer("host") {}
    void Adopt(std::unique_ptr<IRenderContext> c) { renderContext = std::move(c); }
};

void ArrangeAt(UltraCanvasUIElement& e, float w, float h) {
    CSSLayout::LayoutContext lc;
    lc.viewportWidth = w;
    lc.viewportHeight = h;
    CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, w },
                                      { CSSLayout::ConstraintMode::Exact, h } };
    e.Measure(mc, lc);
    e.Arrange(Rect2Df(0, 0, w, h), lc);
}

// The GIF's colour table straight from its bytes: the global table that
// follows the 13-byte header.
std::vector<Color> GlobalTableOf(const fs::path& gif) {
    std::ifstream in(gif, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), {});
    std::vector<Color> out;
    if (bytes.size() < 13 || !(bytes[10] & 0x80)) return out;
    const size_t n = size_t(1) << ((bytes[10] & 7) + 1);
    if (bytes.size() < 13 + 3 * n) return out;
    for (size_t i = 0; i < n; ++i)
        out.emplace_back(bytes[13 + 3 * i], bytes[14 + 3 * i], bytes[15 + 3 * i], 255);
    return out;
}

void TestDetailsPanel(const fs::path& media) {
    std::cout << "\nDetails panel\n";
    const fs::path gif = media / "images" / "dice.gif";
    const fs::path jpeg = media / "images" / "tn_dice.jpg";

    auto host = std::make_shared<Host>();
    host->Adopt(CreateRenderContext(Size2Di(900, 600), nullptr));
    auto viewer = CreateMediaViewer("Viewer", 0, 0, 900, 600);
    host->AddChild(viewer);

    viewer->SetFiles({ PathToUtf8(gif) });
    viewer->SetDetailsVisible(true);
    ArrangeAt(*host, 900, 600);

    auto* panel = dynamic_cast<UltraCanvasGroupBox*>(viewer->FindChildById("MV_Details_Panel"));
    auto* text = dynamic_cast<UltraCanvasTextArea*>(viewer->FindChildById("MV_Details_Text"));
    auto* swatches = dynamic_cast<UltraCanvasColorSwatchBar*>(viewer->FindChildById("MV_Details_Palette"));
    auto* caption = dynamic_cast<UltraCanvasLabel*>(viewer->FindChildById("MV_Details_PaletteCaption"));
    Check(panel && text && swatches && caption,
          "the panel is a group box holding the text, a caption and the swatches");
    if (!panel || !text || !swatches || !caption) return;

    Check(viewer->IsDetailsVisible() && panel->IsVisible(), "Details opens the panel");
    Check(panel->GetTitle() == "Image information", "its caption is the title of what is shown");
    Check(panel->HasCloseButton(), "it has a close button");
    const std::string md = text->GetText();
    Check(md.find("Image information") == std::string::npos,
          "the title is not repeated as a heading in the text");
    Check(md.find("| **Palette** | 256 colours (global colour table) |") != std::string::npos,
          "the table lists the GIF's colour table");

    const std::vector<Color> expected = GlobalTableOf(gif);
    const std::vector<Color>& shown = swatches->GetColors();
    bool same = expected.size() == 256 && shown.size() == expected.size();
    for (size_t i = 0; same && i < shown.size(); ++i)
        same = shown[i].r == expected[i].r && shown[i].g == expected[i].g && shown[i].b == expected[i].b;
    Check(same, "the swatches are the file's colour table, in its order");
    Check(swatches->IsVisible() && caption->IsVisible() &&
          caption->GetText() == "Palette - 256 colours",
          "the swatches and their caption are shown");
    Check(swatches->GetHeight() > 20.0f && text->GetHeight() > swatches->GetHeight(),
          "the swatches get their height under the text, which keeps the most");

    // The caption names the entry under the pointer, and goes back after.
    if (swatches->onColorHovered) {
        swatches->onColorHovered(1);
        char want[64];
        std::snprintf(want, sizeof(want), "Entry 1:  #%02X%02X%02X", expected[1].r, expected[1].g, expected[1].b);
        Check(caption->GetText().rfind(want, 0) == 0, "hovering a swatch names it in the caption");
        swatches->onColorHovered(-1);
        Check(caption->GetText() == "Palette - 256 colours", "leaving the swatches restores the caption");
    }

    // UC_DETAILS_PNG=<file> writes the viewer with the panel open, to look at.
    if (const char* out = std::getenv("UC_DETAILS_PNG")) {
        IRenderContext* ctx = host->GetRenderContext();
        auto* cr = static_cast<cairo_t*>(ctx->GetNativeContext());
        host->Render(ctx, Rect2Df(0, 0, 900, 600));
        cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
        cairo_paint(cr);
        host->Render(ctx, Rect2Df(0, 0, 900, 600));
        cairo_surface_write_to_png(cairo_get_target(cr), out);
    }

    // A file without a colour table: no Palette row, no swatches.
    viewer->SetFiles({ PathToUtf8(jpeg) });
    ArrangeAt(*host, 900, 600);
    Check(text->GetText().find("**Palette**") == std::string::npos, "a JPEG lists no palette");
    Check(!swatches->IsVisible() && !caption->IsVisible(), "and shows no swatches");
    Check(panel->GetTitle() == "Image information", "the caption follows the file");

    // The close button: a click at the right end of the caption.
    UCEvent click;
    click.type = UCEventType::MouseDown;
    click.button = UCMouseButton::Left;
    click.pointer.x = static_cast<int>(panel->GetWidth()) - 17;
    click.pointer.y = 13;
    panel->OnEvent(click);
    Check(!viewer->IsDetailsVisible(), "the close button closes the panel");
}

// The colour of the surface at (x, y), opaque.
Color PixelAt(cairo_t* cr, int x, int y) {
    cairo_surface_t* s = cairo_get_target(cr);
    cairo_surface_flush(s);
    const uint32_t px = reinterpret_cast<const uint32_t*>(
        cairo_image_surface_get_data(s) + y * cairo_image_surface_get_stride(s))[x];
    return Color((px >> 16) & 0xFF, (px >> 8) & 0xFF, px & 0xFF, 255);
}

bool Same(const Color& a, const Color& b, int tolerance = 3) {
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance &&
           std::abs(a.b - b.b) <= tolerance;
}

void TestTableGrid() {
    std::cout << "\nMarkdown table grid\n";
    const Color background(22, 22, 28);
    const Color headerFill(44, 44, 54);
    const Color border(78, 78, 90);

    auto host = std::make_shared<Host>();
    auto owned = CreateRenderContext(Size2Di(400, 200), nullptr);
    IRenderContext* ctx = owned.get();
    host->Adopt(std::move(owned));
    Check(ctx != nullptr, "offscreen render context");
    if (!ctx) return;
    auto* cr = static_cast<cairo_t*>(ctx->GetNativeContext());

    auto ta = std::make_shared<UltraCanvasTextArea>("Table", 0, 0, 400, 200);
    host->AddChild(ta);
    ta->ApplyDarkTheme();
    ta->SetBackgroundColor(background);
    ta->SetBorders(0.0f, Colors::Transparent);
    ta->SetFontSize(11);
    ta->SetShowLineNumbers(false);
    ta->SetHighlightSyntax(false);
    ta->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
    ta->SetDisplayOnly(true);
    MarkdownHybridStyle md = ta->GetMarkdownStyle();
    md.tableHeaderBackground = headerFill;
    md.tableBorderColor = border;
    ta->SetMarkdownStyle(md);
    ta->SetText("| Property | Value |\n| --- | --- |\n"
                "| **Colour space** | Uncalibrated |\n| **Width** | 754 |\n");
    ArrangeAt(*host, 400, 200);
    ta->Render(ctx, Rect2Df(0, 0, 400, 200));   // lays the lines out
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    ta->Render(ctx, Rect2Df(0, 0, 400, 200));
    if (const char* out = std::getenv("UC_TABLE_PNG")) cairo_surface_write_to_png(cairo_get_target(cr), out);

    // Down a column inside the first cell: background, the header's fill, then
    // straight away the rule under it - one pixel - and background again.
    int fillTop = -1, fillLeft = -1;
    for (int y = 0; y < 100 && fillTop < 0; ++y)
        for (int x = 0; x < 60; ++x)
            if (Same(PixelAt(cr, x, y), headerFill, 0)) { fillTop = y; fillLeft = x; break; }
    Check(fillTop >= 0, "the header row is filled");
    if (fillTop < 0) return;
    const int x = fillLeft + 1;
    int y = fillTop;
    while (y < 199 && Same(PixelAt(cr, x, y), headerFill)) ++y;
    Check(Same(PixelAt(cr, x, y), border), "the rule under the header meets its fill");
    Check(Same(PixelAt(cr, x, y + 1), background), "and is one pixel high");

    // Across the header row: one divider between the columns, one pixel wide,
    // in the border colour (not the text colour).
    const int midY = fillTop + 2;
    int dividers = 0, widest = 0, run = 0;
    for (int px = fillLeft; px < 400; ++px) {
        const Color c = PixelAt(cr, px, midY);
        if (Same(c, border)) { ++run; widest = std::max(widest, run); }
        else { if (run) ++dividers; run = 0; }
    }
    if (run) ++dividers;
    Check(dividers == 1 && widest == 1, "the column divider is one pixel in the border colour");
}

} // namespace

int main() {
    std::cout << "MediaViewerDetailsPanelTest\n";
    UCImage::InitializeImageSubsysterm("MediaViewerDetailsPanelTest");
    const fs::path media = PathFromUtf8(UC_MEDIA_DIR);
    TestDetailsPanel(media);
    TestTableGrid();
    std::cout << (g_failures ? "\nFAILED: " + std::to_string(g_failures) + "\n" : "\nAll passed\n");
    return g_failures ? 1 : 0;
}
