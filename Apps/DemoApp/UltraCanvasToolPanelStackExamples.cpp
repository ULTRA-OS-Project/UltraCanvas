// Apps/DemoApp/UltraCanvasToolPanelStackExamples.cpp
// Demonstration of UltraCanvasToolPanelStack: a word processor's column of
// tool panels beside its document - panels that open and close, a context
// panel that comes with the selection, the order dragged or moved with
// Ctrl+Up / Ctrl+Down and saved with the open panels, the icon rail with its
// flyouts - and the rows the panels are built from (UltraCanvasToolRows.h),
// whose values are editable and mirror a small document model.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasTextUtils.h"
#include "UltraCanvasToolPanelStack.h"
#include "UltraCanvasToolRows.h"
#include "UltraCanvasUtils.h"

namespace UltraCanvas {

    namespace {
        std::string ToolIcon(const std::string& relative) {
            return NormalizePath(GetResourcesDir() + "media/icons/" + relative);
        }

        // What the panels edit: the selection of a small document.
        struct DemoSelection {
            bool bold = false, italic = false, underline = false;
            int align = 0;
            double size = 12, spacing = 1.15, before = 0, after = 10;
            int style = 0;
            Color colour = Colors::Black;
            std::string font = "Ubuntu";
        };

        std::string Describe(const DemoSelection& s) {
            return TrF("{0} {1} pt{2}{3}{4}, spacing {5}, before {6} pt, after {7} pt", s.font,
                       static_cast<int>(s.size), s.bold ? Tr(", bold") : std::string(),
                       s.italic ? Tr(", italic") : std::string(), s.underline ? Tr(", underlined") : std::string(),
                       FormatFixedClassic(s.spacing, 2), static_cast<int>(s.before), static_cast<int>(s.after));
        }
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateToolPanelStackExamples() {
        auto page = DemoKit::CreatePage("ToolPanelStackExamples", Tr("Tool Panel Stack Examples"),
                Tr("A column of tool panels beside a document: every topic at once, values shown with their labels, "
                   "context panels with the selection, an order of the user's own, an icon rail for a narrow window."));

        auto doc = std::make_shared<DemoSelection>();

        // ===== 1. THE COLUMN =====
        auto card = DemoKit::AddCard(page, "TpsL1", Tr("A word processor's tool column"),
                Tr("Click a header to open or close a panel; drag a header to move it; right-click it for Move up and Move down. "
                   "Change a value and the document line below follows; Caret in a table brings the Table panel."), 600);

        auto stack = CreateToolPanelStack("TpsStack");
        auto status = DemoKit::CreateText("TpsStatus", Describe(*doc));
        auto refresh = [doc, status = status.get()]() { status->SetText(Describe(*doc)); };

        // Clipboard
        auto clipboard = CreateToolPanelContent("TpsClipboard");
        auto clipRow = CreateToolButtonRow("TpsClipRow");
        clipRow->AddButton("TpsCut", ToolIcon("menu/cut.svg"), Tr("Cut"), "Ctrl+X", [] {});
        clipRow->AddButton("TpsCopy", ToolIcon("menu/copy.svg"), Tr("Copy"), "Ctrl+C", [] {});
        clipRow->AddButton("TpsPaste", ToolIcon("menu/paste.svg"), Tr("Paste"), "Ctrl+V", [] {});
        clipboard->AddChild(clipRow);
        ToolPanelOptions clipOptions;
        clipOptions.iconPath = ToolIcon("menu/paste.svg");
        clipOptions.helpText = Tr("Cut, copy and paste the selection");
        stack->AddPanel("clipboard", Tr("Clipboard"), clipboard, clipOptions);

        // Style
        auto style = CreateToolPanelContent("TpsStyle");
        auto styleRows = CreateToolValueRows("TpsStyleRows");
        styleRows->AddListRow("TpsStyleList", {Tr("Body text"), Tr("Heading 1"), Tr("Heading 2"), Tr("Quote")},
                              [doc] { return doc->style; }, [doc](int i) { doc->style = i; });
        style->AddChild(styleRows);
        ToolPanelOptions styleOptions;
        styleOptions.iconPath = ToolIcon("menu/format.svg");
        styleOptions.open = false;
        stack->AddPanel("style", Tr("Style"), style, styleOptions);

        // Font
        auto font = CreateToolPanelContent("TpsFont");
        auto fontRows = CreateToolValueRows("TpsFontRows");
        fontRows->AddFontPicker("TpsFontPicker", [doc] { return doc->font; },
                                [doc, refresh](const FontPickerChoice& c) { doc->font = c.family; refresh(); });
        fontRows->AddSpinner("TpsSize", Tr("Size"), 6, 96, 1, 0, [doc] { return doc->size; },
                             [doc, refresh](double v) { doc->size = v; refresh(); }, Tr("pt"));
        auto fontTools = CreateToolButtonRow("TpsFontTools");
        fontTools->AddToggle("TpsBold", ToolIcon("textformat/bold.svg"), Tr("Bold"), "Ctrl+B",
                             [doc] { return doc->bold; }, [doc, refresh](bool on) { doc->bold = on; refresh(); });
        fontTools->AddToggle("TpsItalic", ToolIcon("textformat/italic.svg"), Tr("Italic"), "Ctrl+I",
                             [doc] { return doc->italic; }, [doc, refresh](bool on) { doc->italic = on; refresh(); });
        fontTools->AddToggle("TpsUnderline", ToolIcon("textformat/underline.svg"), Tr("Underline"), "Ctrl+U",
                             [doc] { return doc->underline; }, [doc, refresh](bool on) { doc->underline = on; refresh(); });
        fontRows->AddWideRow(fontTools);
        fontRows->AddColorSwatches("TpsColour", Tr("Colour"),
                                   {Colors::Black, Color(196, 43, 28), Color(0, 120, 215), Color(16, 124, 16), Color(255, 185, 0)},
                                   [doc] { return doc->colour; }, [doc](const Color& c) { doc->colour = c; });
        font->AddChild(fontRows);
        ToolPanelOptions fontOptions;
        fontOptions.iconPath = ToolIcon("textformat/bold.svg");
        fontOptions.dialogAction = [status = status.get()] { status->SetText(Tr("The Font dialog would open here.")); };
        fontOptions.dialogTooltip = Tr("Font");
        stack->AddPanel("font", Tr("Font"), font, fontOptions);

        // Paragraph
        auto paragraph = CreateToolPanelContent("TpsParagraph");
        auto alignTools = CreateToolButtonRow("TpsAlign");
        const char* alignIcons[] = {"textformat/align-left.svg", "textformat/align-centre.svg",
                                    "textformat/align-right.svg", "office/justify.svg"};
        const std::string alignNames[] = {Tr("Align left"), Tr("Centre"), Tr("Align right"), Tr("Justify")};
        for (int i = 0; i < 4; ++i) {
            alignTools->AddToggle("TpsAlign" + std::to_string(i), ToolIcon(alignIcons[i]), alignNames[i], "",
                                  [doc, i] { return doc->align == i; },
                                  [doc, i, rows = alignTools.get()](bool) { doc->align = i; rows->Sync(); });
        }
        alignTools->AddGap();
        alignTools->AddButton("TpsOutdent", ToolIcon("office/outdent.svg"), Tr("Decrease indent"), "", [] {});
        alignTools->AddButton("TpsIndent", ToolIcon("office/indent.svg"), Tr("Increase indent"), "", [] {});
        auto spacingRows = CreateToolValueRows("TpsSpacing");
        spacingRows->AddSpinner("TpsLineSpacing", Tr("Spacing"), 0.5, 4.0, 0.05, 2, [doc] { return doc->spacing; },
                                [doc, refresh](double v) { doc->spacing = v; refresh(); });
        spacingRows->AddSpinner("TpsBefore", Tr("Before"), 0, 200, 1, 0, [doc] { return doc->before; },
                                [doc, refresh](double v) { doc->before = v; refresh(); }, Tr("pt"));
        spacingRows->AddSpinner("TpsAfter", Tr("After"), 0, 200, 1, 0, [doc] { return doc->after; },
                                [doc, refresh](double v) { doc->after = v; refresh(); }, Tr("pt"));
        paragraph->AddChild(alignTools);
        paragraph->AddChild(spacingRows);
        ToolPanelOptions paragraphOptions;
        paragraphOptions.iconPath = ToolIcon("textformat/align-left.svg");
        paragraphOptions.helpText = Tr("More: the Paragraph dialog (Ctrl+Shift+P)");
        paragraphOptions.dialogAction = [status = status.get()] { status->SetText(Tr("The Paragraph dialog would open here.")); };
        paragraphOptions.dialogTooltip = Tr("Paragraph");
        stack->AddPanel("paragraph", Tr("Paragraph"), paragraph, paragraphOptions);

        // Lists, Insert, Review: closed at the start.
        auto addTools = [&](const std::string& id, const std::string& title, const std::string& icon,
                            const std::vector<std::pair<std::string, std::string>>& tools, const std::string& chip) {
            auto content = CreateToolPanelContent("Tps" + id);
            auto row = CreateToolButtonRow("Tps" + id + "Row");
            for (const auto& tool : tools) row->AddButton("Tps" + id + tool.second, ToolIcon(tool.second), tool.first, "", [] {});
            content->AddChild(row);
            ToolPanelOptions options;
            options.iconPath = ToolIcon(icon);
            options.open = false;
            options.chip = chip;
            stack->AddPanel(id, title, content, options);
        };
        addTools("lists", Tr("Lists"), "ultramail/list-bullets.svg",
                 {{Tr("Bullets"), "ultramail/list-bullets.svg"}, {Tr("Numbering"), "ultramail/list-numbers.svg"}}, "");
        addTools("insert", Tr("Insert"), "image.svg",
                 {{Tr("Table"), "table.svg"}, {Tr("Picture"), "image.svg"}, {Tr("Link"), "link.svg"}}, "");
        addTools("review", Tr("Review"), "edit.svg",
                 {{Tr("Comment"), "edit.svg"}, {Tr("Track changes"), "ultramail/check.svg"}}, "3");

        // Table: a context panel.
        auto table = CreateToolPanelContent("TpsTable");
        auto tableRow = CreateToolButtonRow("TpsTableRow");
        tableRow->AddButton("TpsRowAbove", ToolIcon("arrow-up.svg"), Tr("Insert row above"), "", [] {});
        tableRow->AddButton("TpsRowBelow", ToolIcon("arrow-down.svg"), Tr("Insert row below"), "", [] {});
        tableRow->AddButton("TpsColLeft", ToolIcon("arrow-left.svg"), Tr("Insert column left"), "", [] {});
        tableRow->AddButton("TpsColRight", ToolIcon("arrow-right.svg"), Tr("Insert column right"), "", [] {});
        table->AddChild(tableRow);
        ToolPanelOptions tableOptions;
        tableOptions.iconPath = ToolIcon("table.svg");
        tableOptions.context = true;
        tableOptions.chip = Tr("in a table");
        stack->AddPanel("table", Tr("Table"), table, tableOptions);

        // The column beside a document page.
        auto workspace = DemoKit::CreateRow("TpsWorkspace", 0, false);
        workspace->size.height = CSSLayout::Dimension::Px(520);
        workspace->layout.SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        workspace->SetBorders(1.0f, DemoKit::CardBorder(), 4.0f);
        workspace->AddChild(stack);
        auto desk = DemoKit::CreateColumn("TpsDesk", 10);
        desk->SetPadding(16);
        desk->SetBackgroundColor(DemoKit::Themed(Color(214, 216, 222)));
        desk->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        DemoKit::Add(desk, DemoKit::CreateText("TpsDeskText",
                Tr("The document. Its selection is what the panels show; a change in a panel changes it:")));
        DemoKit::Add(desk, status);
        workspace->AddChild(desk);
        DemoKit::AddStretched(card, workspace);

        // The controls under it.
        auto controls = DemoKit::AddRow(card, "TpsControls");
        auto inTable = std::make_shared<UltraCanvasSwitch>("TpsInTable", Tr("Caret in a table"));
        inTable->onStateChanged = [stack = stack.get()](CheckedState, CheckedState now) {
            stack->SetPanelShown("table", now == CheckedState::Checked);
        };
        DemoKit::Add(controls, inTable);
        auto railSwitch = std::make_shared<UltraCanvasSwitch>("TpsRail", Tr("Icon rail"));
        railSwitch->onStateChanged = [stack = stack.get()](CheckedState, CheckedState now) {
            stack->SetRailMode(now == CheckedState::Checked);
        };
        DemoKit::Add(controls, railSwitch);
        auto bolden = std::make_shared<UltraCanvasButton>("TpsBolden", Tr("Make the selection bold"));
        bolden->onClick = [doc, stack = stack.get(), refresh]() {
            doc->bold = true;
            stack->Sync();
            refresh();
        };
        DemoKit::Add(controls, bolden);

        auto saved = DemoKit::CreateCaption("TpsSaved", TrF("Saved state: {0}", stack->SaveState().ToString()));
        auto showState = [stack = stack.get(), saved = saved.get()]() {
            saved->SetText(TrF("Saved state: {0}", stack->SaveState().ToString()));
        };
        stack->onOrderChanged = [showState](const std::vector<std::string>&) { showState(); };
        stack->onPanelOpenChanged = [showState](const std::string&, bool) { showState(); };
        stack->onRailModeChanged = [showState](bool) { showState(); };
        DemoKit::Add(card, saved);

        DemoKit::AddNotes(page, "TpsNotes", Tr("Implementation notes"), {
            Tr("Each panel is an UltraCanvasGroupBox in the Header style: collapsible, its header focusable and draggable, a \"…\" for the topic's dialog, a chip for a count or a context."),
            Tr("Keyboard: the host's F6 calls FocusFirst; Up and Down go from header to header, Enter or Space open and close, Tab walks into an open panel, Ctrl+Up and Ctrl+Down move the panel."),
            Tr("A context panel comes in at the top, open, in the accent; the panel under the pointer keeps its place on the screen."),
            Tr("SaveState().ToString() is one line for the settings; RestoreState(ToolPanelStackState::FromString(...)) brings back the order, the open panels and the rail."),
            Tr("In rail mode a click on an icon opens the panel as a flyout on the window's popup stack: Escape closes one level, a click outside closes it."),
            Tr("The rows (UltraCanvasToolRows.h) take a getter and a setter; Sync shows the document's values without calling a setter."),
        });
        return page;
    }

} // namespace UltraCanvas
