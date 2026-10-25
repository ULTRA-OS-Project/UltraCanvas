// Tests/TextCentredOnCapsTest.cpp
// A segmented control, a spreadsheet cell and a checkbox's label centre their
// text vertically on its capitals: the middle of a capital letter sits on
// the middle of the segment, row or control, as a button's caption and a
// label's text do. They used to centre the text's line box - ascent plus
// descent - which sits wherever the font's metrics put it: FreeSans' line
// box puts its capitals 2.5 px above the middle of an 80 px segment,
// Carlito's 1.5 px below, and the Windows build drew the demo's segmented
// controls high and the spreadsheet's cells low enough to cut their
// descenders off. A layout sized by a font attribute must also measure the
// capitals of that size, which the centring relies on.
//
// A caption of capitals only ("HHHH": their ink is the cap height, nothing
// above or below it) is drawn offscreen in black on white and read back: the
// ink's middle row must be within a pixel of the middle. In the bundled
// Ubuntu font always, and in FreeSans and Carlito when they are installed.
//
// Usage: TextCentredOnCapsTest
// Exit code is the number of failed checks.
// Version: 1.1.0 - a checkbox's label; the cap height of a layout sized by a font attribute
// Version: 1.0.0
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework

#include "UltraCanvasCheckbox.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasRenderContext.h"

#include <fontconfig/fontconfig.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

// The bundled Ubuntu font, for fontconfig to find by name.
bool AddBundledUbuntu() {
    const std::string file = GetBundledFontsDir() + "Ubuntu-R.ttf";
    if (!std::filesystem::exists(PathFromUtf8(file))) return false;
    return FcConfigAppFontAddFile(FcConfigGetCurrent(),
                                  reinterpret_cast<const FcChar8*>(file.c_str())) == FcTrue;
}

// Whether fontconfig has a font of exactly this family.
bool HasFamily(const std::string& family) {
    FcPattern* pattern = FcNameParse(reinterpret_cast<const FcChar8*>(family.c_str()));
    if (!pattern) return false;
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result;
    FcPattern* match = FcFontMatch(nullptr, pattern, &result);
    bool same = false;
    FcChar8* found = nullptr;
    if (match && FcPatternGetString(match, FC_FAMILY, 0, &found) == FcResultMatch) {
        same = family == reinterpret_cast<const char*>(found);
    }
    if (match) FcPatternDestroy(match);
    FcPatternDestroy(pattern);
    return same;
}

// The first and last row of `area` holding a dark pixel.
struct InkRows {
    int top = -1;
    int bottom = -1;
};

template <typename Element>
InkRows DrawAndMeasure(Element& element, int width, int height, const Rect2Di& area) {
    auto ctx = CreateRenderContext(Size2Di(width, height), nullptr);
    ctx->Clear(Color(255, 255, 255, 255));
    element.Render(ctx.get(), Rect2Df(0, 0, width, height));
    UCPixmap pixmap;
    pixmap.Init(width, height);
    ctx->FlushToSurface(pixmap.GetSurface(), Point2Dd(0, 0));
    pixmap.MarkDirty();
    pixmap.Flush();
    InkRows rows;
    for (int y = area.y; y < area.y + area.height; ++y) {
        for (int x = area.x; x < area.x + area.width; ++x) {
            const int green = static_cast<int>((pixmap.GetPixel(x, y) >> 8) & 0xFF);
            if (green < 128) {
                if (rows.top < 0) rows.top = y;
                rows.bottom = y;
                break;
            }
        }
    }
    return rows;
}

void CheckRows(const InkRows& rows, double middle, const std::string& what) {
    if (rows.top < 0) {
        Check(false, what + ": drawn");
        return;
    }
    const double inkMiddle = (rows.top + rows.bottom + 1) / 2.0;
    char detail[96];
    std::snprintf(detail, sizeof detail, " (ink rows %d-%d, middle %.1f; box middle %.1f)",
                  rows.top, rows.bottom, inkMiddle, middle);
    Check(std::fabs(inkMiddle - middle) <= 1.0, what + detail);
}

void CheckSegment(int width, int height, float fontSize, const std::string& family) {
    auto control = std::make_shared<UltraCanvasSegmentedControl>("segments", 0, 0, width, height);
    SegmentedControlStyle style = SegmentedControlStyle::Flat();
    style.normalColor = Colors::Transparent;
    style.selectedColor = Colors::Transparent;
    style.hoverColor = Colors::Transparent;
    style.normalTextColor = Colors::Black;
    style.selectedTextColor = Colors::Black;
    style.fontFamily = family;
    style.fontSize = fontSize;
    control->SetStyle(style);
    control->AddSegment("HHHH");
    control->SetSelectedIndex(0);
    const InkRows rows = DrawAndMeasure(*control, width, height, Rect2Di(0, 0, width, height));
    char what[128];
    std::snprintf(what, sizeof what, "%s %.0f pt: a segment %d px high centres its capitals",
                  family.c_str(), fontSize, height);
    CheckRows(rows, height / 2.0, what);
}

void CheckCell(float fontSize, const std::string& family, int rowHeight = 0) {
    const int width = 400;
    const int height = 300;
    auto sheet = std::make_shared<UltraCanvasSpreadsheet>("sheet", 0, 0, width, height);
    sheet->SetCellValue(0, 0, "HHHH");
    // A1 is the active cell: the Format Cells calls style it.
    sheet->SetSelectionFontFamily(family);
    sheet->SetSelectionFontSize(fontSize);
    if (rowHeight > 0) sheet->SetRowHeight(0, rowHeight);
    const Rect2Di cell = sheet->GetCellBounds(0, 0);
    // Inside the cell's gridlines and clear of the selection frame's handle.
    const Rect2Di inside(cell.x + 3, cell.y + 3, cell.width / 2, cell.height - 6);
    const InkRows rows = DrawAndMeasure(*sheet, width, height, inside);
    char what[128];
    std::snprintf(what, sizeof what, "%s %.0f pt: a spreadsheet row %d px high centres its capitals",
                  family.c_str(), fontSize, cell.height);
    CheckRows(rows, cell.y + cell.height / 2.0, what);
}

// A checkbox's label beside its box: the label's capitals on the middle.
void CheckCheckbox(float fontSize, const std::string& family) {
    const int width = 400;
    const int height = 80;
    auto checkbox = std::make_shared<UltraCanvasCheckbox>("check", 0, 0, width, height, "HHHHHH");
    CheckboxVisualStyle style = checkbox->GetVisualStyle();
    style.base.fontFamily = family;
    style.base.fontSize = fontSize;
    style.base.textColor = Colors::Black;
    checkbox->SetVisualStyle(style);
    CSSLayout::LayoutContext layoutContext;
    checkbox->Arrange(Rect2Df(0, 0, width, height), layoutContext);
    // Right of the box: the label's ink alone.
    const InkRows rows = DrawAndMeasure(*checkbox, width, height, Rect2Di(width / 3, 0, width - width / 3, height));
    char what[128];
    std::snprintf(what, sizeof what, "%s %.0f pt: a checkbox %d px high centres its label's capitals",
                  family.c_str(), fontSize, height);
    CheckRows(rows, height / 2.0, what);
}

// A layout sized by a font attribute measures the capitals of that size, as
// one given the font as its own does - not those of its 12 pt base font.
void CheckCapHeightOfAttributes(const std::string& family) {
    auto ctx = CreateRenderContext(Size2Di(10, 10), nullptr);
    FontStyle base;
    base.fontFamily = family;
    base.fontSize = 12;
    FontStyle large = base;
    large.fontSize = 36;
    auto byAttribute = ctx->CreateTextLayout("<span>HH</span>", true);
    byAttribute->SetFontStyle(base);
    byAttribute->ChangeAttribute(TextAttributeFactory::CreateFontSize(36));
    auto byFont = ctx->CreateTextLayout("HH", false);
    byFont->SetFontStyle(large);
    const double a = byAttribute->GetCapHeight();
    const double b = byFont->GetCapHeight();
    char what[160];
    std::snprintf(what, sizeof what,
                  "%s: a layout sized to 36 pt by an attribute measures 36 pt capitals (%.1f px; its own font %.1f px)",
                  family.c_str(), a, b);
    Check(b > 0 && std::fabs(a - b) <= 0.5, what);
}

} // namespace

int main() {
    UCImage::InitializeImageSubsysterm("TextCentredOnCapsTest");

    std::vector<std::string> families;
    if (AddBundledUbuntu() && HasFamily("Ubuntu")) families.push_back("Ubuntu");
    else Check(false, "the bundled Ubuntu font loads");
    for (const char* lopsided : {"FreeSans", "Carlito"}) {
        if (HasFamily(lopsided)) families.push_back(lopsided);
        else std::printf("skip: %s is not installed\n", lopsided);
    }

    for (const auto& family : families) {
        CheckSegment(200, 35, 12.0f, family);   // the default style at the demo's height
        CheckSegment(240, 50, 20.0f, family);
        CheckSegment(320, 80, 36.0f, family);   // a large font shows a line-box offset plainly
        CheckCell(11.0f, family);
        CheckCell(14.0f, family);
        CheckCell(36.0f, family, 80);
        CheckCheckbox(36.0f, family);
        CheckCapHeightOfAttributes(family);
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
