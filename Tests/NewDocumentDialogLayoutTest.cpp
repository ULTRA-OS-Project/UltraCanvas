// Tests/NewDocumentDialogLayoutTest.cpp
// The new-document dialog draws its text where it belongs.
//
//  - A list row's name was drawn 18 px below the row's top: in a 28 px row
//    that put it at the row's bottom edge and across the next row. It is
//    centred on its row now, and so is the category beside it.
//  - "File name:" was placed at the dialog's own offset in its window
//    (finalBounds.y + 8), so wherever the dialog sat it drew somewhere over
//    the title; it sits left of its field, level with it.
//  - The "Will create: ..." preview was placed below the dialog's bottom
//    edge in window terms, outside the dialog; it is under its field.
//  - Under the CSS layout the dialog drew none of that: the container's own
//    background was painted over the title and list, and its fields were
//    stacked at its top-left corner. They are pinned at their places now.
//
// Runs headless: the dialog draws into an offscreen surface, read back
// pixel by pixel (NEWDOC_DUMP_PNG=<file> also writes it out, to look at).
// The geometry below is the dialog's default layout (padding 16, 28 px rows,
// a 520 x 500 dialog).
// Version: 1.0.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework

#include "UltraCanvasNewDocumentDialog.h"
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

namespace {

constexpr int kWidth = 520;
constexpr int kHeight = 500;
constexpr int kPadding = 16;
constexpr int kRowHeight = 28;
// Title band (30) + gap (20) + filter field (28) + gap (12).
constexpr int kListTop = kPadding + 30 + 20 + 28 + 12;
// Row x + icon inset (8) + icon column (24).
constexpr int kNameLeft = kPadding + 2 + 8 + 24;
// Above the buttons (32), a gap (12), the field (28) and 8 more.
constexpr int kFileFieldTop = kHeight - kPadding - 32 - 12 - 28 - 8;
constexpr int kFileFieldLeft = kPadding + 80;
constexpr int kButtonsTop = kHeight - kPadding - 32;

struct Ink {
    int count = 0;
    int top = -1;
    int bottom = -1;
};

// Dark pixels (text: R + G + B below `limit`) in a rectangle of the
// surface: how many, and the rows the first run of them spans.
Ink DarkInk(cairo_surface_t* surface, int x0, int y0, int x1, int y1, int limit = 250) {
    cairo_surface_flush(surface);
    const unsigned char* data = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    Ink ink;
    bool inRun = false, runDone = false;
    for (int y = y0; y < y1; ++y) {
        bool rowHasInk = false;
        for (int x = x0; x < x1; ++x) {
            const uint32_t px = *reinterpret_cast<const uint32_t*>(data + y * stride + x * 4);
            const int r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF, b = px & 0xFF;
            if (r + g + b < limit) {
                ++ink.count;
                rowHasInk = true;
            }
        }
        if (rowHasInk && !runDone) {
            if (!inRun) ink.top = y;
            inRun = true;
            ink.bottom = y;
        } else if (!rowHasInk && inRun) {
            runDone = true;
        }
    }
    return ink;
}

} // namespace

int main() {
    auto ctx = CreateRenderContext(Size2Di(kWidth, kHeight), nullptr);
    cairo_t* cr = ctx ? static_cast<cairo_t*>(ctx->GetNativeContext()) : nullptr;
    if (!cr) {
        TEST("an offscreen surface to draw the dialog into", false);
        return 1;
    }
    cairo_surface_t* surface = cairo_get_target(cr);

    // Placed away from its window's origin, as a dialog is: the label that
    // used the dialog's own offset only lands right at (0, 0).
    auto dialog = std::make_shared<UltraCanvasNewDocumentDialog>("newdoc", 30, 40, kWidth, kHeight);
    dialog->ClearDocumentTypes();
    dialog->AddDocumentType(DocumentTypeInfo("Plain Text", "txt", "Text"));
    dialog->AddDocumentType(DocumentTypeInfo("Markdown", "md", "Text"));
    dialog->AddDocumentType(DocumentTypeInfo("C++ Source", "cpp", "Code"));
    dialog->SetDefaultFileName("notes");
    dialog->Show();
    dialog->SetSelectedIndex(0);
    CSSLayout::LayoutContext lctx;
    dialog->Arrange(Rect2Df(30, 40, kWidth, kHeight), lctx);
    dialog->Render(ctx.get(), Rect2Df(0, 0, kWidth, kHeight));
    if (const char* dump = std::getenv("NEWDOC_DUMP_PNG")) cairo_surface_write_to_png(surface, dump);

    // ---- The list's first row ----
    const Ink name = DarkInk(surface, kNameLeft, kListTop, kNameLeft + 160, kListTop + 2 * kRowHeight);
    TEST("the first row's name is drawn", name.count > 30);
    TEST("its text starts inside its row, below the row's top",
         name.top > kListTop + 2);
    TEST("and ends inside its row, not across the next one (" + std::to_string(name.top - kListTop) + ".." +
                 std::to_string(name.bottom - kListTop) + " of 28)",
         name.bottom < kListTop + kRowHeight - 2);
    const int above = name.top - kListTop;
    const int below = kListTop + kRowHeight - 1 - name.bottom;
    TEST("centred: the space above and below the text differs by a few pixels at most",
         std::abs(above - below) <= 6);

    // ---- "File name:" beside its field ----
    const Ink label = DarkInk(surface, kPadding, kFileFieldTop, kFileFieldLeft - 4, kFileFieldTop + 28);
    TEST("\"File name:\" is drawn level with its field, left of it", label.count > 30);

    // ---- The preview under the field, inside the dialog ----
    // Grey text (100, 100, 100).
    const Ink preview = DarkInk(surface, kFileFieldLeft, kFileFieldTop + 28, kFileFieldLeft + 200, kButtonsTop, 400);
    TEST("the \"Will create:\" preview is under the field, above the buttons", preview.count > 30);

    // ---- The fields are where the dialog draws around them ----
    // A text field's border is grey; at the file name field's left edge,
    // half-way down, the dialog's white background would show if the field
    // had been stacked in the corner instead.
    const Ink fieldEdge = DarkInk(surface, kFileFieldLeft, kFileFieldTop + 10, kFileFieldLeft + 2, kFileFieldTop + 18, 700);
    TEST("the file name field is placed at its spot beside the label", fieldEdge.count > 0);
    const Ink corner = DarkInk(surface, 2, 70, 12, 90, 700);
    TEST("and not stacked at the dialog's top-left corner", corner.count == 0);

    std::cerr << "\nNewDocumentDialogLayoutTest: " << testCount << " checks, " << failCount << " failures" << std::endl;
    return failCount;
}
