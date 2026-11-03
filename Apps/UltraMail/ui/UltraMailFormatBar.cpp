// Apps/UltraMail/ui/UltraMailFormatBar.cpp
// Version: 0.4.0 - Options::singleRow (every tool in one row); Font and Size
//                  show the font and size at the cursor (Sync); a Text |
//                  Background switch in front of the swatches picks what a
//                  swatch colours, each with its own palette
// Version: 0.3.0 - icon-only flat tools (media/icons/ultramail/) in groups
//                  with separators, on a tinted strip; the quote tools join the
//                  paragraph row, the colour swatches get a text-colour mark
// Version: 0.2.0 - Quote + / Quote − for the compose window, at the end of
//                  the character row
// Version: 0.1.0 - moved out of the signature editor, so the compose window has
//                  the same tools
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailFormatBar.h"

#include "UltraMailTheme.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasColorSwatchBar.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasTextUtils.h"   // Trim, TryParseFloat, FormatFloatClassic

#include <string>
#include <vector>

using namespace UltraCanvas;

namespace UltraMail {

namespace {

constexpr float kToolButton = Theme::kControlHeight + 2.0f;   // a square tool
constexpr float kRowGap     = 4.0f;
constexpr float kStripPadding = 4.0f;
constexpr float kSwatchBarWidth = 160.0f;

// The strip's height with `rows` rows of tools.
float BarHeight(int rows) {
    return 2 * kStripPadding + rows * kToolButton + (rows - 1) * kRowGap;
}

// Fonts every mail program can show: text in a font the reader lacks falls
// back to theirs anyway.
const char* const kFonts[] = {
    "Arial", "Helvetica", "Verdana", "Tahoma", "Trebuchet MS",
    "Georgia", "Times New Roman", "Courier New",
};
const int kSizes[] = { 8, 9, 10, 11, 12, 14, 16, 18, 20, 24 };

// A small palette of text colours that read on white.
std::vector<Color> TextPalette() {
    return {
        Color(0, 0, 0),       Color(68, 68, 68),    Color(128, 128, 128),
        Color(192, 57, 43),   Color(230, 126, 34),  Color(39, 174, 96),
        Color(37, 99, 235),   Color(30, 58, 138),   Color(124, 58, 237),
    };
}

// Highlighter colours: light enough that black text stays readable on them.
// The first, white, takes the background away again.
std::vector<Color> BackgroundPalette() {
    return {
        Color(255, 255, 255), Color(255, 245, 157), Color(200, 230, 201),
        Color(178, 235, 242), Color(187, 222, 251), Color(248, 187, 208),
        Color(255, 224, 178), Color(225, 225, 225), Color(225, 190, 231),
    };
}

// The segments of the colour switch.
constexpr int kColourText       = 0;
constexpr int kColourBackground = 1;

// A size as the Size list writes it: "12", "10.5".
std::string SizeLabel(float pt) {
    if (pt == static_cast<float>(static_cast<int>(pt))) return std::to_string(static_cast<int>(pt));
    return FormatFloatClassic(pt, 4);   // significant digits: 10.5, 10.25
}

using EditorLookup = std::function<UltraCanvasRichTextEdit*()>;

// Never wider than the parent. A button sized to its label measures wider than
// it is drawn, and a row of them would otherwise widen the whole compose window
// past its edge, pushing the right end of the bar (and Cancel) out of sight.
void CapToParentWidth(UltraCanvasContainer& c) {
    CSSLayout::BoxConstraints limits = c.boxConstraints.value_or(CSSLayout::BoxConstraints{});
    limits.maxWidth = CSSLayout::Dimension::Pct(100);
    c.boxConstraints = limits;
}

std::shared_ptr<UltraCanvasContainer> MakeRow(const std::string& id) {
    auto row = CreateContainer(id, 0, 0, 0, kToolButton);
    CapToParentWidth(*row);
    row->layout.SetFlexRow()
               .SetFlexGap(kRowGap)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    ContainerStyle style;
    style.autoShowScrollbars = false;
    row->SetContainerStyle(style);
    return row;
}

// Runs `action` on the editor, if there still is one, and gives it the
// keyboard back so typing continues where it was.
void OnEditor(const EditorLookup& editor, const std::function<void(UltraCanvasRichTextEdit&)>& action) {
    UltraCanvasRichTextEdit* e = editor ? editor() : nullptr;
    if (!e) return;
    action(*e);
    e->SetFocus(true);
}

} // namespace

FormatBar FormatBar::Build(const Options& options) {
    FormatBar bar;
    const std::string& id = options.idPrefix;
    const EditorLookup editor = options.editor;
    UltraCanvasWindowBase* parent = options.dialogParent;
    bar.singleRow_ = options.singleRow;

    // A tinted strip: the host puts it on top of the editor, inside one frame,
    // so the tools read as the editor's own toolbar.
    bar.root = CreateContainer(id + "Bar", 0, 0, 0, BarHeight(options.singleRow ? 1 : 2));
    bar.root->layout.SetFlexColumn()
                    .SetFlexGap(kRowGap)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    {
        ContainerStyle style;
        style.autoShowScrollbars = false;
        bar.root->SetContainerStyle(style);
    }
    bar.root->SetPadding(kStripPadding);
    bar.root->SetBackgroundColor(Theme::kSidebar);
    CapToParentWidth(*bar.root);

    // Every tool goes on the list SetToolsVisible hides; the host's own
    // controls, added to paragraphRow afterwards, are not on it.
    auto keep = [&bar](const std::shared_ptr<UltraCanvasUIElement>& tool) {
        bar.tools_.push_back(tool.get());
    };
    // Icon-only and flat: what a tool does is in its tooltip, as in any
    // word processor.
    auto addTool = [&editor, &keep](const std::shared_ptr<UltraCanvasContainer>& row,
                                    const std::string& toolId, const std::string& icon,
                                    const std::string& tooltip,
                                    std::function<void(UltraCanvasRichTextEdit&)> action) {
        auto button = Theme::MakeToolButton(toolId, icon, tooltip, kToolButton);
        button->onClick = [editor, action]() { OnEditor(editor, action); };
        row->AddChild(button);
        keep(button);
        return button;
    };
    auto separator = [&id, &keep](const std::shared_ptr<UltraCanvasContainer>& row,
                                  const std::string& sepId) {
        auto line = Theme::MakeToolSeparator(id + sepId);
        row->AddChild(line);
        keep(line);
    };

    // ----- Characters -----
    auto charRow = MakeRow(id + "CharRow");
    addTool(charRow, id + "Bold", "bold", "Bold (Ctrl+B)",
            [](UltraCanvasRichTextEdit& e) { e.ToggleBold(); });
    addTool(charRow, id + "Italic", "italic", "Italic (Ctrl+I)",
            [](UltraCanvasRichTextEdit& e) { e.ToggleItalic(); });
    addTool(charRow, id + "Underline", "underline", "Underline (Ctrl+U)",
            [](UltraCanvasRichTextEdit& e) { e.ToggleUnderline(); });
    addTool(charRow, id + "Strike", "strikethrough", "Strikethrough",
            [](UltraCanvasRichTextEdit& e) { e.ToggleStrikethrough(); });
    separator(charRow, "Sep1");

    // Font and Size show what the text at the cursor is in (Sync); "Default"
    // is no font or size of its own, which the reader's program chooses.
    auto font = CreateDropdown(id + "Font", 0, 0, 130, Theme::kControlHeight);
    font->AddItem("Default", "");
    for (const char* name : kFonts) font->AddItem(name, name);
    Theme::StyleDropdown(font);
    font->SetSelectedIndex(0, /*runNotifications=*/false);
    font->SetTooltip("Font of the selected text");
    font->onSelectionChanged = [editor](int, const DropdownItem& item) {
        const std::string family = item.value;   // "" = the reader's default
        OnEditor(editor, [family](UltraCanvasRichTextEdit& e) { e.SetFontFamily(family); });
    };
    charRow->AddChild(font);
    font->layoutItem.SetFlexShrink(0);   // cut off at the edge, never squeezed
    keep(font);
    bar.font_ = font.get();

    auto size = CreateDropdown(id + "Size", 0, 0, 72, Theme::kControlHeight);
    size->AddItem("Default", "");
    for (int pt : kSizes) size->AddItem(std::to_string(pt), std::to_string(pt));
    Theme::StyleDropdown(size);
    size->SetSelectedIndex(0, /*runNotifications=*/false);
    size->SetTooltip("Size of the selected text, in points");
    size->onSelectionChanged = [editor](int, const DropdownItem& item) {
        float pt = 0.0f;   // "Default"
        if (!item.value.empty() && !TryParseFloat(item.value, pt)) return;
        OnEditor(editor, [pt](UltraCanvasRichTextEdit& e) { e.SetFontSize(pt); });
    };
    charRow->AddChild(size);
    size->layoutItem.SetFlexShrink(0);
    keep(size);
    bar.size_ = size.get();
    separator(charRow, "Sep2");

    // What a swatch colours: the letters, or the background behind them (a
    // highlighter). Each has its own palette.
    auto colours = CreateColorSwatchBar(id + "Colour", 0, 0, 0, Theme::kControlHeight - 4.0f,
                                        TextPalette());
    auto target = CreateSegmentedControl(id + "ColourTarget", 0, 0, 56, Theme::kControlHeight);
    Theme::StyleSegmented(target);
    {
        auto style = target->GetStyle();
        style.iconAsMask = true;
        style.iconSize = Theme::kButtonIcon;
        style.paddingHorizontal = 6;
        target->SetStyle(style);
    }
    target->AddSegment("", Theme::MailIcon("text-colour"));
    target->AddSegment("", Theme::MailIcon("highlight"));
    target->SetSelectedIndex(kColourText);
    target->SetTooltip("What a colour is for: the text, or the background behind it");
    UltraCanvasColorSwatchBar* swatches = colours.get();   // a sibling in this row
    target->onSegmentSelected = [swatches](int segment) {
        swatches->SetColors(segment == kColourBackground ? BackgroundPalette() : TextPalette());
        swatches->SetTooltip(segment == kColourBackground
                                 ? "Background colour of the selected text (white: none)"
                                 : "Colour of the selected text");
    };
    target->layoutItem.SetFlexShrink(0);
    charRow->AddChild(target);
    keep(target);

    // A fixed width: the swatches fit themselves into it. At its preferred
    // width (swatches at full size) the row outgrew the compose window.
    colours->SetElementSize(Size2Df(kSwatchBarWidth, Theme::kControlHeight - 4.0f));
    colours->SetTooltip("Colour of the selected text");
    UltraCanvasSegmentedControl* targetRaw = target.get();   // a sibling in this row
    colours->onColorSelected = [editor, targetRaw](const Color& c) {
        const bool background = targetRaw->GetSelectedIndex() == kColourBackground;
        // White behind the text is no background at all.
        const std::string hex = background && c == Color(255, 255, 255) ? std::string()
                                                                          : c.ToHexString();
        OnEditor(editor, [hex, background](UltraCanvasRichTextEdit& e) {
            if (background) e.SetHighlightColor(hex);
            else            e.SetTextColor(hex);
        });
    };
    charRow->AddChild(colours);
    colours->layoutItem.SetFlexShrink(0);
    keep(colours);
    bar.root->AddChild(charRow);

    // ----- Paragraphs, quotes, links, pictures: a row of their own, or on in
    // the same row after a separator -----
    std::shared_ptr<UltraCanvasContainer> paraRow;
    if (options.singleRow) {
        paraRow = charRow;
        separator(paraRow, "Sep3a");
    } else {
        charRow->AddStretchSpacer(1);
        bar.characterRow_ = charRow.get();
        paraRow = MakeRow(id + "ParaRow");
    }
    addTool(paraRow, id + "Left", "align-left", "Align left",
            [](UltraCanvasRichTextEdit& e) { e.SetAlignment(RichTextAlign::Left); });
    addTool(paraRow, id + "Center", "align-centre", "Centre",
            [](UltraCanvasRichTextEdit& e) { e.SetAlignment(RichTextAlign::Center); });
    addTool(paraRow, id + "Right", "align-right", "Align right",
            [](UltraCanvasRichTextEdit& e) { e.SetAlignment(RichTextAlign::Right); });
    separator(paraRow, "Sep3");
    addTool(paraRow, id + "Bullets", "list-bullets", "Bulleted list",
            [](UltraCanvasRichTextEdit& e) { e.ToggleBulletList(); });
    addTool(paraRow, id + "Numbers", "list-numbers", "Numbered list",
            [](UltraCanvasRichTextEdit& e) { e.ToggleNumberedList(); });
    if (options.quoteTools) {
        // Where an answer goes between quoted lines, or a quoted line comes
        // out of the quote: the paragraphs at the cursor (or every selected
        // one) one quote level in or out. Each click is one undo step.
        addTool(paraRow, id + "QuoteIn", "quote-in", "Quote: the paragraph one quote level in",
                [](UltraCanvasRichTextEdit& e) { e.IncreaseQuoteLevel(); });
        addTool(paraRow, id + "QuoteOut", "quote-out",
                "Unquote: the paragraph one quote level out",
                [](UltraCanvasRichTextEdit& e) { e.DecreaseQuoteLevel(); });
    }
    separator(paraRow, "Sep4");
    addTool(paraRow, id + "Rule", "rule", "A horizontal line",
            [](UltraCanvasRichTextEdit& e) { e.InsertHorizontalRule(); });

    // Link: the address for the selected text (or the address itself, typed
    // at the cursor, when nothing is selected). The editor is looked up again
    // when the answer comes: the window may have closed meanwhile.
    auto link = Theme::MakeToolButton(id + "Link", "link",
                                      "Link the selected text to a web page or an e-mail address",
                                      kToolButton);
    link->onClick = [editor, parent]() {
        UltraCanvasDialogManager::ShowInputDialog(
            "Web page or e-mail address", "Link", "https://", InputType::Text,
            [editor](DialogResult result, const std::string& value) {
                if (result != DialogResult::OK) return;
                std::string target = Trim(value);
                if (target.empty() || target == "https://" || target == "http://") return;
                // A bare address is an e-mail link.
                if (target.find(':') == std::string::npos && target.find('@') != std::string::npos)
                    target = "mailto:" + target;
                OnEditor(editor, [target](UltraCanvasRichTextEdit& e) {
                    if (!e.HasSelection()) {
                        UCRichDocumentEditor& core = e.GetEditor();
                        const RichDocPosition start = core.GetCaret();
                        e.InsertText(target.rfind("mailto:", 0) == 0 ? target.substr(7) : target);
                        core.SetSelection(start, core.GetCaret());
                    }
                    e.SetLink(target);
                    e.InvalidateDocument();
                });
            },
            parent);
    };
    paraRow->AddChild(link);
    keep(link);

    // Picture: a logo or a photo, inside the line at the cursor. It is sent
    // as a part of the message (cid:), like the pictures of a reply.
    auto picture = Theme::MakeToolButton(id + "Picture", "picture",
                                         "Put a picture (a logo, a photo) at the cursor",
                                         kToolButton);
    picture->onClick = [editor, parent]() {
        FileDialogOptions dialog;
        dialog.title = "Insert picture";
        dialog.parentWindow = parent;
        dialog.AddFilter("Pictures", std::vector<std::string>{"png", "jpg", "jpeg", "gif"});
        UltraCanvasFileLoader::OpenFileDialog(
            dialog, [editor, parent](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty()) return;
                UltraCanvasRichTextEdit* e = editor ? editor() : nullptr;
                if (!e) return;
                if (!e->InsertInlineImageFromFile(path)) {
                    UltraCanvasDialogManager::ShowError("Could not read the picture " + path,
                                                        "Insert picture", nullptr, parent);
                    return;
                }
                e->SetFocus(true);
            });
    };
    paraRow->AddChild(picture);
    keep(picture);

    paraRow->AddStretchSpacer(1);
    if (!options.singleRow) bar.root->AddChild(paraRow);
    bar.paragraphRow = paraRow.get();
    return bar;
}

namespace {
// The item of `dropdown` whose value is `value`; added at the end when there
// is none (a font or size the message brought along), so it can be shown.
int ItemFor(UltraCanvasDropdown& dropdown, const std::string& value, const std::string& text) {
    for (int i = 0; i < dropdown.GetItemCount(); ++i)
        if (const DropdownItem* item = dropdown.GetItem(i); item && item->value == value) return i;
    dropdown.AddItem(text, value);
    return dropdown.GetItemCount() - 1;
}
} // namespace

void FormatBar::Sync(const RichCharFormatState& state) {
    // A selection over several fonts or sizes keeps what was shown: there is
    // no one name to give.
    if (font_ && !state.fontFamilyMixed)
        font_->SetSelectedIndex(ItemFor(*font_, state.fontFamily, state.fontFamily),
                                /*runNotifications=*/false);
    if (size_ && !state.fontSizeMixed) {
        const std::string value = state.fontSizePt > 0.0f ? SizeLabel(state.fontSizePt) : "";
        size_->SetSelectedIndex(ItemFor(*size_, value, value), /*runNotifications=*/false);
    }
}

void FormatBar::SetToolsVisible(bool visible) {
    if (characterRow_) characterRow_->SetVisible(visible);
    for (UltraCanvasUIElement* tool : tools_) tool->SetVisible(visible);
    if (root) {
        // Only the height: the width stays whatever the host's layout gives.
        // One row holds the host's own controls even with the tools hidden.
        root->size.height = CSSLayout::Dimension::Px(
            BarHeight(singleRow_ || !visible ? 1 : 2));
        root->InvalidateLayout();
    }
}

} // namespace UltraMail
