// Tests/TextLayoutCacheTest.cpp
// The text layout an element draws with is built once and kept: ten draws
// of a label, a button shortened in the middle, a text input, a closed
// dropdown or a menu bar make one layout per text on the render context
// (IRenderContext::TextLayoutsCreated); a changed text, font or width makes
// one more, a changed colour none. Drawn into an offscreen surface; needs
// no display.
// Version: 1.2.0 - the context's key respects the font; the dropdown's reordered rows
// Version: 1.1.0 - the text input, the dropdown and the menu
// Version: 1.0.0
// Author: UltraCanvas Framework
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTextLayoutCache.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace UltraCanvas;

static int g_checks = 0;
static int g_failures = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

// The element laid out at `width`, as the layout engine would place it:
// a button computes its text rectangle in Arrange, and nothing else runs
// it here without a window.
static void Place(UltraCanvasUIElement& element, float width) {
    CSSLayout::LayoutContext lctx;
    element.Arrange(Rect2Df(0, 0, width, 34), lctx);
}

// The layouts the context makes for nine repaints after a first draw: the
// first draw may also cost the context its own probes of a font (a cap
// height, a line box), kept per font; a repaint must cost nothing.
static unsigned long Repaints(IRenderContext* ctx, UltraCanvasUIElement& element);

static void Draw(IRenderContext* ctx, UltraCanvasUIElement& element, int times) {
    for (int i = 0; i < times; ++i) {
        ctx->PushState();
        element.Render(ctx, Rect2Df(0, 0, 400, 40));
        ctx->PopState();
    }
}

static unsigned long Repaints(IRenderContext* ctx, UltraCanvasUIElement& element) {
    Draw(ctx, element, 1);
    const unsigned long after = ctx->TextLayoutsCreated();
    Draw(ctx, element, 9);
    return ctx->TextLayoutsCreated() - after;
}

int main() {
    auto ctx = CreateRenderContext(Size2Di(400, 40), nullptr);
    if (!ctx) {
        std::printf("no offscreen render context - skipped\n");
        return 0;
    }
    ctx->Clear(Color(255, 255, 255, 255));

    std::printf("The cache itself\n");
    {
        UltraCanvasTextLayoutCache cache;
        TextLayoutKey key;
        key.text = "Open a site folder";
        key.width = 200;
        Check(cache.Get(nullptr, key) == nullptr && !cache, "no context, no layout");
        const unsigned long before = ctx->TextLayoutsCreated();
        bool rebuilt = false;
        ITextLayout* first = cache.Get(ctx.get(), key, &rebuilt);
        Check(first != nullptr && rebuilt && cache.Builds() == 1 && ctx->TextLayoutsCreated() == before + 1,
              "the first Get builds the layout");
        Check(cache.Get(ctx.get(), key, &rebuilt) == first && !rebuilt && cache.Builds() == 1, "the same key gets the same layout");
        key.width = 150;
        Check(cache.Get(ctx.get(), key, &rebuilt) != nullptr && rebuilt && cache.Builds() == 2, "a changed width rebuilds it");
        key.ellipsize = EllipsizeMode::EllipsizeMiddle;
        Check(cache.Get(ctx.get(), key) != nullptr && cache.Builds() == 3 && cache->GetEllipsize() == EllipsizeMode::EllipsizeMiddle,
              "a changed mode rebuilds it with the mode applied");
        cache.Reset();
        Check(!cache && cache.Get(ctx.get(), key) != nullptr && cache.Builds() == 4, "Reset drops it, the next Get builds again");
    }

    std::printf("A label\n");
    {
        auto label = CreateLabel("l", 0, 0, 400, 40, "A label drawn ten times");
        const unsigned long before = ctx->TextLayoutsCreated();
        Draw(ctx.get(), *label, 10);
        Check(ctx->TextLayoutsCreated() == before + 1, "ten draws, one layout: " + std::to_string(ctx->TextLayoutsCreated() - before));
        label->SetTextColor(Color(200, 0, 0, 255));
        Draw(ctx.get(), *label, 3);
        Check(ctx->TextLayoutsCreated() == before + 1, "a colour change makes none");
        label->SetText("Another text");
        Draw(ctx.get(), *label, 3);
        Check(ctx->TextLayoutsCreated() == before + 2, "a new text makes one");
        label->SetFontSize(18);
        Draw(ctx.get(), *label, 3);
        Check(ctx->TextLayoutsCreated() == before + 3, "a new font size makes one");
        label->SetWrap(TextWrap::WrapWord);
        Draw(ctx.get(), *label, 3);
        Check(ctx->TextLayoutsCreated() == before + 4, "a new wrap makes one");
        LabelStyle style = label->GetStyle();
        label->SetStyle(style);
        Draw(ctx.get(), *label, 3);
        Check(ctx->TextLayoutsCreated() == before + 4, "the same style set again makes none");
    }

    std::printf("A button shortened in the middle\n");
    {
        auto button = CreateButton("b", 0, 0, 300, 34, "/home/erika/sites/a-folder-with-a-long-name/shop");
        button->SetEllipsize(EllipsizeMode::EllipsizeMiddle);
        Place(*button, 300);
        const unsigned long before = ctx->TextLayoutsCreated();
        Draw(ctx.get(), *button, 10);
        Check(ctx->TextLayoutsCreated() == before + 1, "ten draws, one layout: " + std::to_string(ctx->TextLayoutsCreated() - before));
        button->SetText("/home/erika/sites/another-folder-with-a-long-name/blog");
        Draw(ctx.get(), *button, 3);
        Check(ctx->TextLayoutsCreated() == before + 2, "a new text makes one");
        Place(*button, 250);
        Draw(ctx.get(), *button, 3);
        Check(ctx->TextLayoutsCreated() == before + 3, "a new width makes one");
        button->SetFontSize(16);
        Draw(ctx.get(), *button, 3);
        Check(ctx->TextLayoutsCreated() == before + 4, "a new font size makes one");
        button->SetEllipsize(EllipsizeMode::EllipsizeEnd);
        const unsigned long end = ctx->TextLayoutsCreated();
        Draw(ctx.get(), *button, 3);
        Check(ctx->TextLayoutsCreated() == end, "shortened at the end again, the button makes no layout of its own");
    }

    std::printf("The context's own cache\n");
    {
        // DrawText and the measures go through the context's string-keyed
        // cache; its key carries the font, so the same word in two sizes is
        // two layouts, not one measured at the first size.
        FontStyle small;
        small.fontSize = 12;
        FontStyle large;
        large.fontSize = 24;
        ctx->SetFontStyle(small);
        const Size2Di atSmall = ctx->GetTextLineDimensions("Hello");
        ctx->SetFontStyle(large);
        const Size2Di atLarge = ctx->GetTextLineDimensions("Hello");
        ctx->SetFontStyle(small);
        const Size2Di atSmallAgain = ctx->GetTextLineDimensions("Hello");
        Check(atLarge.width > atSmall.width && atLarge.height > atSmall.height && atSmallAgain == atSmall,
              "the same word measures larger in the larger font and the same again in the first");
        FontStyle bold = small;
        bold.fontWeight = FontWeight::Bold;
        ctx->SetFontStyle(bold);
        Check(ctx->GetTextLineDimensions("Hello").width != atSmall.width, "and differently in bold");
        ctx->SetFontStyle(small);
    }

    std::printf("A text input\n");
    {
        auto input = CreateTextInput("i", 0, 0, 300, 34);
        input->SetPlaceholder("Type here");
        Place(*input, 300);
        Check(Repaints(ctx.get(), *input) == 0, "nine repaints of the placeholder make no layout");
        input->SetText("Erika");
        Check(Repaints(ctx.get(), *input) == 0, "nine repaints of a text make none");
        unsigned long before = ctx->TextLayoutsCreated();
        input->SetText("Erika Muster");
        Draw(ctx.get(), *input, 1);
        Check(ctx->TextLayoutsCreated() == before + 1, "a changed text makes one: " + std::to_string(ctx->TextLayoutsCreated() - before));
        before = ctx->TextLayoutsCreated();
        input->SetText("");
        Draw(ctx.get(), *input, 3);
        Check(ctx->TextLayoutsCreated() == before, "back to the placeholder, which is still kept");
    }

    std::printf("A dropdown\n");
    {
        auto dropdown = CreateDropdown("d", 0, 0, 260, 28);
        dropdown->AddItem("SKR03 - Prozessgliederung", "SKR03");
        dropdown->AddItem("SKR04 - Abschlussgliederung", "SKR04");
        dropdown->SetSelectedIndex(0, false);
        Place(*dropdown, 260);
        Check(Repaints(ctx.get(), *dropdown) == 0, "nine repaints of the closed dropdown make no layout");
        const unsigned long before = ctx->TextLayoutsCreated();
        dropdown->SetSelectedIndex(1, false);
        Draw(ctx.get(), *dropdown, 1);
        Check(ctx->TextLayoutsCreated() == before + 1, "another item chosen makes one: " + std::to_string(ctx->TextLayoutsCreated() - before));
    }

    std::printf("The dropdown's rows\n");
    {
        // The list's rows are drawn by the delegate, one kept layout per row
        // index: a reordered model rebuilds every row whose text moved, once.
        std::vector<DropdownItem> rows;
        for (const char* text : {"Alpha", "Beta", "Gamma"}) {
            DropdownItem item;
            item.text = text;
            rows.push_back(item);
        }
        DropdownListModel model;
        model.SetItems(&rows);
        DropdownStyle style;
        DropdownItemDelegate delegate;
        delegate.SetStyle(&style);
        auto drawRows = [&]() {
            for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
                ListItemStyleOption option;
                option.rect = Rect2Di(0, row * 24, 260, 24);
                option.row = row;
                delegate.RenderItem(ctx.get(), &model, row, 0, option);
            }
        };
        unsigned long before = ctx->TextLayoutsCreated();
        drawRows();
        const unsigned long first = ctx->TextLayoutsCreated() - before;
        before = ctx->TextLayoutsCreated();
        drawRows();
        drawRows();
        Check(first >= 3 && ctx->TextLayoutsCreated() == before, "three rows shaped once; two repaints make no layout");
        std::swap(rows[0], rows[1]);
        before = ctx->TextLayoutsCreated();
        drawRows();
        Check(ctx->TextLayoutsCreated() == before + 2, "two rows swapped: the two rebuild, the third is kept: " + std::to_string(ctx->TextLayoutsCreated() - before));
        rows.erase(rows.begin());
        before = ctx->TextLayoutsCreated();
        drawRows();
        Check(ctx->TextLayoutsCreated() == before + 2, "the first row removed: the two that moved up rebuild: " + std::to_string(ctx->TextLayoutsCreated() - before));
        before = ctx->TextLayoutsCreated();
        drawRows();
        Check(ctx->TextLayoutsCreated() == before, "and are kept again");
    }

    std::printf("A menu bar\n");
    {
        auto menu = CreateMenuBar("m", 0, 0, 400);
        menu->AddItem(MenuItemData("File"));
        menu->AddItem(MenuItemData("Edit"));
        menu->AddItem(MenuItemData("Help"));
        Place(*menu, 400);
        const unsigned long before = ctx->TextLayoutsCreated();
        Check(Repaints(ctx.get(), *menu) == 0 && ctx->TextLayoutsCreated() >= before + 3,
              "three entries shaped once, nine repaints make no layout: " + std::to_string(ctx->TextLayoutsCreated() - before));
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
