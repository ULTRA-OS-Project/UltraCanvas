// Apps/UltraMail/ui/UltraMailFormatBar.cpp
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
#include "UltraCanvasTextUtils.h"   // Trim

#include <string>
#include <vector>

using namespace UltraCanvas;

namespace UltraMail {

namespace {

constexpr float kToolButton = Theme::kControlHeight + 2.0f;   // a square tool
constexpr float kRowGap     = 4.0f;
constexpr float kStripPadding = 4.0f;
constexpr float kSwatchBarWidth = 160.0f;

// The strip's height with both rows, or with the paragraph row alone (which
// holds the host's own controls).
float BarHeight(bool bothRows) {
    const float row = kToolButton;
    return 2 * kStripPadding + (bothRows ? 2 * row + kRowGap : row);
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

    // A tinted strip: the host puts it on top of the editor, inside one frame,
    // so the tools read as the editor's own toolbar.
    bar.root = CreateContainer(id + "Bar", 0, 0, 0, BarHeight(true));
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

    // Icon-only and flat: what a tool does is in its tooltip, as in any
    // word processor.
    auto addTool = [&editor](const std::shared_ptr<UltraCanvasContainer>& row,
                             const std::string& toolId, const std::string& icon,
                             const std::string& tooltip,
                             std::function<void(UltraCanvasRichTextEdit&)> action) {
        auto button = Theme::MakeToolButton(toolId, icon, tooltip, kToolButton);
        button->onClick = [editor, action]() { OnEditor(editor, action); };
        row->AddChild(button);
        return button;
    };

    // ----- Row 1: characters -----
    auto charRow = MakeRow(id + "CharRow");
    addTool(charRow, id + "Bold", "bold", "Bold (Ctrl+B)",
            [](UltraCanvasRichTextEdit& e) { e.ToggleBold(); });
    addTool(charRow, id + "Italic", "italic", "Italic (Ctrl+I)",
            [](UltraCanvasRichTextEdit& e) { e.ToggleItalic(); });
    addTool(charRow, id + "Underline", "underline", "Underline (Ctrl+U)",
            [](UltraCanvasRichTextEdit& e) { e.ToggleUnderline(); });
    addTool(charRow, id + "Strike", "strikethrough", "Strikethrough",
            [](UltraCanvasRichTextEdit& e) { e.ToggleStrikethrough(); });
    charRow->AddChild(Theme::MakeToolSeparator(id + "Sep1"));

    auto font = CreateDropdown(id + "Font", 0, 0, 130, Theme::kControlHeight);
    font->AddItem("Font", "");
    for (const char* name : kFonts) font->AddItem(name, name);
    Theme::StyleDropdown(font);
    font->SetSelectedIndex(0, /*runNotifications=*/false);
    font->SetTooltip("Font of the selected text");
    font->onSelectionChanged = [editor](int, const DropdownItem& item) {
        const std::string family = item.value;   // "" = the reader's default
        OnEditor(editor, [family](UltraCanvasRichTextEdit& e) { e.SetFontFamily(family); });
    };
    charRow->AddChild(font);

    auto size = CreateDropdown(id + "Size", 0, 0, 64, Theme::kControlHeight);
    size->AddItem("Size", "");
    for (int pt : kSizes) size->AddItem(std::to_string(pt), std::to_string(pt));
    Theme::StyleDropdown(size);
    size->SetSelectedIndex(0, /*runNotifications=*/false);
    size->SetTooltip("Size of the selected text, in points");
    size->onSelectionChanged = [editor](int index, const DropdownItem&) {
        // The items after "Size" are kSizes in order; "Size" itself = default.
        const float pt = index > 0 ? static_cast<float>(kSizes[index - 1]) : 0.0f;
        OnEditor(editor, [pt](UltraCanvasRichTextEdit& e) { e.SetFontSize(pt); });
    };
    charRow->AddChild(size);
    charRow->AddChild(Theme::MakeToolSeparator(id + "Sep2"));

    // The text-colour mark (an A over a bar) in front of the swatches, in
    // place of the word "Colour".
    auto colourMark = CreateImageElement(id + "ColourMark", Theme::kToolIcon, Theme::kToolIcon);
    colourMark->LoadFromFile(Theme::MailIcon("text-colour"));
    colourMark->SetFitMode(ImageFitMode::Contain);
    colourMark->SetTintColor(Theme::kTextSecondary);
    colourMark->SetTooltip("Colour of the selected text");
    colourMark->layoutItem.SetFlexShrink(0);
    charRow->AddChild(colourMark);
    auto colours = CreateColorSwatchBar(id + "Colour", 0, 0, 0, Theme::kControlHeight - 4.0f,
                                        TextPalette());
    // A fixed width: the swatches fit themselves into it. At its preferred
    // width (swatches at full size) the row outgrew the compose window.
    colours->SetElementSize(Size2Df(kSwatchBarWidth, Theme::kControlHeight - 4.0f));
    colours->SetTooltip("Colour of the selected text");
    colours->onColorSelected = [editor](const Color& c) {
        const std::string hex = c.ToHexString();
        OnEditor(editor, [hex](UltraCanvasRichTextEdit& e) { e.SetTextColor(hex); });
    };
    charRow->AddChild(colours);
    charRow->AddStretchSpacer(1);
    bar.root->AddChild(charRow);
    bar.characterRow_ = charRow.get();

    // ----- Row 2: paragraphs, quotes, links, pictures -----
    auto paraRow = MakeRow(id + "ParaRow");
    auto keep = [&bar](const std::shared_ptr<UltraCanvasUIElement>& tool) {
        bar.paragraphTools_.push_back(tool.get());
    };
    auto separator = [&](const std::string& sepId) {
        auto line = Theme::MakeToolSeparator(id + sepId);
        paraRow->AddChild(line);
        keep(line);
    };
    keep(addTool(paraRow, id + "Left", "align-left", "Align left",
                 [](UltraCanvasRichTextEdit& e) { e.SetAlignment(RichTextAlign::Left); }));
    keep(addTool(paraRow, id + "Center", "align-centre", "Centre",
                 [](UltraCanvasRichTextEdit& e) { e.SetAlignment(RichTextAlign::Center); }));
    keep(addTool(paraRow, id + "Right", "align-right", "Align right",
                 [](UltraCanvasRichTextEdit& e) { e.SetAlignment(RichTextAlign::Right); }));
    separator("Sep3");
    keep(addTool(paraRow, id + "Bullets", "list-bullets", "Bulleted list",
                 [](UltraCanvasRichTextEdit& e) { e.ToggleBulletList(); }));
    keep(addTool(paraRow, id + "Numbers", "list-numbers", "Numbered list",
                 [](UltraCanvasRichTextEdit& e) { e.ToggleNumberedList(); }));
    if (options.quoteTools) {
        // Where an answer goes between quoted lines, or a quoted line comes
        // out of the quote: the paragraphs at the cursor (or every selected
        // one) one quote level in or out. Each click is one undo step.
        keep(addTool(paraRow, id + "QuoteIn", "quote-in",
                     "Quote: the paragraph one quote level in",
                     [](UltraCanvasRichTextEdit& e) { e.IncreaseQuoteLevel(); }));
        keep(addTool(paraRow, id + "QuoteOut", "quote-out",
                     "Unquote: the paragraph one quote level out",
                     [](UltraCanvasRichTextEdit& e) { e.DecreaseQuoteLevel(); }));
    }
    separator("Sep4");
    keep(addTool(paraRow, id + "Rule", "rule", "A horizontal line",
                 [](UltraCanvasRichTextEdit& e) { e.InsertHorizontalRule(); }));

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
    bar.root->AddChild(paraRow);
    bar.paragraphRow = paraRow.get();
    return bar;
}

void FormatBar::SetToolsVisible(bool visible) {
    if (characterRow_) characterRow_->SetVisible(visible);
    for (UltraCanvasUIElement* tool : paragraphTools_) tool->SetVisible(visible);
    if (root) {
        // Only the height: the width stays whatever the host's layout gives.
        root->size.height = CSSLayout::Dimension::Px(BarHeight(visible));
        root->InvalidateLayout();
    }
}

} // namespace UltraMail
