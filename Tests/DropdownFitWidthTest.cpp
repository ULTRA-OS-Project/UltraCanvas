// Tests/DropdownFitWidthTest.cpp
// A dropdown with SetFitWidthToItems on is as wide as its widest item needs.
// UltraFiler's toolbar dropdowns had fixed widths sized for the English
// entries, and German "Automatisch abspielen" read "Automatisch al…". The
// width given stays the least the button takes: short entries keep it, a
// long one widens the button, adding or removing items moves it, and
// turning the switch off returns to the width given.
//
// Usage: DropdownFitWidthTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework

#include "UltraCanvasDropdown.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasRenderContext.h"
#include "CSSLayout/CSSLayout.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace UltraCanvas;
using namespace UltraCanvas::CSSLayout;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

// A dropdown that measures its items against its own offscreen context, as
// one on a window measures against the window's.
class MeasuredDropdown : public UltraCanvasDropdown {
public:
    MeasuredDropdown(const std::string& id, float w, float h)
        : UltraCanvasDropdown(id, 0, 0, w, h) {
        renderContext = CreateRenderContext(Size2Di(16, 16), nullptr);
    }
    // The width one text needs on the button, as RenderButton lays it out.
    float NeededFor(const std::string& text) {
        IRenderContext* rc = GetRenderContext();
        rc->PushState();
        rc->SetFontFace(GetStyle().fontFamily, FontWeight::Normal, FontSlant::Normal);
        rc->SetFontSize(GetStyle().fontSize);
        const float w = static_cast<float>(rc->GetTextLineDimensions(text).width);
        rc->PopState();
        return GetStyle().paddingLeft + w + GetStyle().arrowSize * 2 + 2;
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

// A toolbar-like row: the dropdown, then a fixed box after it.
struct Row {
    std::shared_ptr<Element> row = std::make_shared<Element>();
    std::shared_ptr<MeasuredDropdown> dropdown;
    std::shared_ptr<Element> after = std::make_shared<Element>();

    explicit Row(float declaredWidth) {
        row->box.boxSizing = BoxSizing::BorderBox;
        row->layout.SetFlexDirection(FlexDirection::Row);
        dropdown = std::make_shared<MeasuredDropdown>("dd", declaredWidth, 26);
        dropdown->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        after->size.width = Dimension::Px(30);
        after->size.height = Dimension::Px(26);
        after->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        row->AddChild(dropdown);
        row->AddChild(after);
    }
    void LayOut() { ::LayOut(row, 1000, 40); }
};

const char* kLong = "Automatisch abspielen mit Ton";

} // namespace

int main() {
    UCImage::InitializeImageSubsysterm("DropdownFitWidthTest");
    std::printf("=== Dropdown width from its items ===\n");

    std::printf("\n-- off (the default): the width given --\n");
    {
        Row r(104);
        r.dropdown->AddItem(kLong);
        r.LayOut();
        Check(!r.dropdown->GetFitWidthToItems(), "off by default");
        Check(r.dropdown->GetWidth() == 104.f, "a long item does not widen it");
    }

    std::printf("\n-- on: a long item widens the button --\n");
    {
        Row r(104);
        r.dropdown->AddItem("Autoplay");
        r.dropdown->AddItem(kLong);
        r.dropdown->SetSelectedIndex(0, false);
        r.dropdown->SetFitWidthToItems(true);
        r.LayOut();
        const float need = r.dropdown->NeededFor(kLong);
        std::printf("  (the long item needs %.1f px, the button given 104)\n", need);
        Check(need > 104.f, "the long item needs more than the 104 px given");
        Check(r.dropdown->GetWidth() >= need - 0.5f, "the button is as wide as the long item needs");
        Check(r.dropdown->GetWidth() <= need + 4.f, "and not much wider");
        Check(r.after->finalBounds.x >= r.dropdown->GetX() + r.dropdown->GetWidth() - 0.5f,
              "the next control moves along rather than being overlapped");

        r.dropdown->RemoveItem(1);
        r.LayOut();
        Check(r.dropdown->GetWidth() == 104.f, "removing it returns to the 104 px given");

        r.dropdown->AddItem(kLong);
        r.LayOut();
        Check(r.dropdown->GetWidth() >= need - 0.5f, "adding it again widens it again");

        r.dropdown->SetFitWidthToItems(false);
        r.LayOut();
        Check(r.dropdown->GetWidth() == 104.f, "turning it off returns to the width given");
    }

    std::printf("\n-- on, with short items: the width given is the least --\n");
    {
        Row r(150);
        r.dropdown->AddItem("A");
        r.dropdown->AddItem("B");
        r.dropdown->SetFitWidthToItems(true);
        r.LayOut();
        Check(r.dropdown->GetWidth() == 150.f, "150 px for one-letter items stays 150 px");
    }

    std::printf("\n-- on: a larger font widens it --\n");
    {
        Row r(60);
        r.dropdown->AddItem("Date modified");
        r.dropdown->SetFitWidthToItems(true);
        r.LayOut();
        const float small = r.dropdown->GetWidth();
        DropdownStyle s = r.dropdown->GetStyle();
        s.fontSize *= 2;
        r.dropdown->SetStyle(s);
        r.LayOut();
        std::printf("  (%.1f px at the first font, %.1f px at twice its size)\n", small, r.dropdown->GetWidth());
        Check(r.dropdown->GetWidth() > small + 10.f, "twice the font size widens the button");
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
