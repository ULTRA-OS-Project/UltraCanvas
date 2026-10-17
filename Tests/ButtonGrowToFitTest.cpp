// Tests/ButtonGrowToFitTest.cpp
// A button with text never comes out narrower than its text. Demo pages and
// applications give buttons a fixed width sized for the English label; a
// translation (or a larger font) then ran past the button's edges. The width
// given is now the least the button takes: it grows when its text needs more,
// returns to the given width when the text gets shorter, and in a flex row
// that is too narrow it is not shrunk below its text either.
//
// Usage: ButtonGrowToFitTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-10-16
// Author: UltraCanvas Framework

#include "UltraCanvasButton.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>
#include "CSSLayout/CSSLayout.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::CSSLayout;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

// A button that measures its text against its own offscreen context, as one
// on a window measures against the window's.
class MeasuredButton : public UltraCanvasButton {
public:
    MeasuredButton(const std::string& id, float x, float y, float w, float h, const std::string& text)
        : UltraCanvasButton(id, x, y, w, h, text) {
        renderContext = CreateRenderContext(Size2Di(16, 16), nullptr);
    }
    // The border-box width the text needs.
    float NeededWidth() {
        IRenderContext* rc = GetRenderContext();
        rc->PushState();
        Size2Df content = MeasureContentSize(rc);
        rc->PopState();
        return content.width + GetStyle().shadowOffset.x +
               GetTotalPaddingHorizontal() + GetTotalBorderHorizontal();
    }
};

void LayOut(const std::shared_ptr<Element>& root, float w, float h) {
    LayoutContext ctx;
    ctx.viewportWidth = w;
    ctx.viewportHeight = h;
    MeasureConstraints mc{ { ConstraintMode::Exact, w }, { ConstraintMode::Exact, h } };
    root->Measure(mc, ctx);
    root->Arrange(Rect2Df{ 0, 0, w, h }, ctx);
}

std::shared_ptr<Element> Page() {
    auto page = std::make_shared<Element>();
    page->box.boxSizing = BoxSizing::BorderBox;
    return page;
}

const char* kLong = "Open the spreadsheet file in another language";

// A square icon as an image decoded from a PNG made in memory.
std::shared_ptr<UCImage> Square(int size) {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t* cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    std::vector<uint8_t> png;
    cairo_surface_write_to_png_stream(surface, [](void* closure, const unsigned char* data, unsigned int length) {
        auto* out = static_cast<std::vector<uint8_t>*>(closure);
        out->insert(out->end(), data, data + length);
        return CAIRO_STATUS_SUCCESS;
    }, &png);
    cairo_surface_destroy(surface);
    return UCImage::GetFromMemory(png.data(), png.size());
}

} // namespace

int main() {
    UCImage::InitializeImageSubsysterm("ButtonGrowToFitTest");
    std::printf("=== Button grows to fit its text ===\n");

    std::printf("\n-- an absolutely placed button with a fixed width --\n");
    {
        auto page = Page();
        auto button = std::make_shared<MeasuredButton>("b", 10, 10, 60, 30, kLong);
        page->AddChild(button);
        LayOut(page, 800, 200);
        const float needed = button->NeededWidth();
        std::printf("  (text needs %.1f px, button given 60)\n", needed);
        Check(needed > 60.f, "the long label needs more than the 60 px given");
        Check(button->GetWidth() >= needed - 0.5f, "the button grows to its text");
        Check(button->GetX() == 10.f && button->GetY() == 10.f, "and keeps its position");

        button->SetText("OK");
        LayOut(page, 800, 200);
        Check(button->GetWidth() == 60.f, "a short text returns it to the 60 px given");

        button->SetText(kLong);
        button->SetGrowToFitText(false);
        LayOut(page, 800, 200);
        Check(button->GetWidth() == 60.f, "SetGrowToFitText(false) keeps the given width");
    }

    std::printf("\n-- a label that fits keeps the given width --\n");
    {
        auto page = Page();
        auto button = std::make_shared<MeasuredButton>("b", 10, 10, 200, 30, "Save");
        page->AddChild(button);
        LayOut(page, 800, 200);
        Check(button->GetWidth() == 200.f, "200 px for \"Save\" stays 200 px");
    }

    std::printf("\n-- a button without text is left alone --\n");
    {
        auto page = Page();
        auto button = std::make_shared<MeasuredButton>("b", 10, 10, 4, 30, "");
        page->AddChild(button);
        LayOut(page, 800, 200);
        Check(button->GetWidth() == 4.f, "a 4 px button without text stays 4 px");
    }

    std::printf("\n-- an icon above or below the text adds to the height, not the width --\n");
    {
        auto icon = Square(32);
        auto page = Page();
        auto above = std::make_shared<MeasuredButton>("above", 10, 10, 100, 70, "Upload");
        auto beside = std::make_shared<MeasuredButton>("beside", 10, 100, 100, 40, "Upload");
        above->SetIcon(icon);
        above->SetIconSize(32, 32);
        above->SetIconPosition(ButtonIconPosition::Top);
        beside->SetIcon(icon);
        beside->SetIconSize(32, 32);
        beside->SetIconPosition(ButtonIconPosition::Left);
        page->AddChild(above);
        page->AddChild(beside);
        LayOut(page, 800, 200);
        std::printf("  (icon above needs %.1f px, beside %.1f px)\n", above->NeededWidth(), beside->NeededWidth());
        Check(above->NeededWidth() + 30.f < beside->NeededWidth(),
              "the icon above takes no width beside the text");
        Check(above->GetWidth() == 100.f && beside->GetWidth() > 100.f,
              "so with 100 px given, the icon above fits and the icon beside grows the button");

        auto iconOnly = std::make_shared<MeasuredButton>("center", 10, 150, 40, 40, kLong);
        iconOnly->SetIcon(icon);
        iconOnly->SetIconSize(32, 32);
        iconOnly->SetIconPosition(ButtonIconPosition::Center);
        page->AddChild(iconOnly);
        LayOut(page, 800, 200);
        Check(iconOnly->GetWidth() == 40.f, "an icon placed Center hides the text, so the button stays 40 px");
    }

    std::printf("\n-- a flex row too narrow for its buttons --\n");
    {
        auto row = Page();
        row->layout.SetFlexDirection(FlexDirection::Row);
        auto a = std::make_shared<MeasuredButton>("a", 0, 0, 0, 30, kLong);
        auto b = std::make_shared<MeasuredButton>("b", 0, 0, 0, 30, kLong);
        row->AddChild(a);
        row->AddChild(b);
        LayOut(row, 120, 40);
        // The row read the buttons' minimum before they had measured their
        // text; drawing them asks for the pass that applies it, as a frame
        // on a window does.
        auto paint = CreateRenderContext(Size2Di(16, 16), nullptr);
        a->Render(paint.get(), Rect2Df(0, 0, 16, 16));
        b->Render(paint.get(), Rect2Df(0, 0, 16, 16));
        Check(!row->IsLayoutValid(), "drawing a squeezed button asks for a new layout");
        LayOut(row, 120, 40);
        Check(a->GetWidth() >= a->NeededWidth() - 0.5f, "the first button is not shrunk below its text");
        Check(b->GetWidth() >= b->NeededWidth() - 0.5f, "nor is the second");
        Check(b->GetX() >= a->GetX() + a->GetWidth() - 0.5f, "and they do not overlap");
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
