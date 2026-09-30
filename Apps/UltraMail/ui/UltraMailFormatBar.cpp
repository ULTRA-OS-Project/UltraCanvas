// Apps/UltraMail/ui/UltraMailFormatBar.cpp
// Version: 0.1.0 - moved out of the signature editor, so the compose window has
//                  the same tools
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailFormatBar.h"

#include "UltraMailTheme.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasColorSwatchBar.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasTextUtils.h"   // Trim

#include <string>
#include <vector>

using namespace UltraCanvas;

namespace UltraMail {

namespace {

constexpr float kToolButton = 30.0f;
constexpr float kRowGap     = 4.0f;
constexpr float kSwatchBarWidth = 160.0f;

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
    auto row = CreateContainer(id, 0, 0, 0, Theme::kControlHeight);
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

    bar.root = CreateContainer(id + "Bar", 0, 0, 0, 2 * Theme::kControlHeight + kRowGap);
    bar.root->layout.SetFlexColumn()
                    .SetFlexGap(kRowGap)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    {
        ContainerStyle style;
        style.autoShowScrollbars = false;
        bar.root->SetContainerStyle(style);
    }
    CapToParentWidth(*bar.root);

    auto addTool = [&editor](const std::shared_ptr<UltraCanvasContainer>& row,
                             const std::string& toolId, const std::string& label,
                             const std::string& tooltip, float width,
                             std::function<void(UltraCanvasRichTextEdit&)> action) {
        auto button = CreateButton(toolId, 0, 0, width, Theme::kControlHeight, label);
        Theme::FitToLabel(button, width);
        Theme::StyleSecondary(button);
        button->SetTooltip(tooltip);
        button->onClick = [editor, action]() { OnEditor(editor, action); };
        row->AddChild(button);
        return button;
    };

    // ----- Row 1: characters -----
    auto charRow = MakeRow(id + "CharRow");
    auto bold = addTool(charRow, id + "Bold", "B", "Bold (Ctrl+B)", kToolButton,
                        [](UltraCanvasRichTextEdit& e) { e.ToggleBold(); });
    bold->SetFont("", Theme::kSizeBody, FontWeight::Bold);
    addTool(charRow, id + "Italic", "I", "Italic (Ctrl+I)", kToolButton,
            [](UltraCanvasRichTextEdit& e) { e.ToggleItalic(); });
    addTool(charRow, id + "Underline", "U", "Underline (Ctrl+U)", kToolButton,
            [](UltraCanvasRichTextEdit& e) { e.ToggleUnderline(); });
    addTool(charRow, id + "Strike", "S", "Strikethrough", kToolButton,
            [](UltraCanvasRichTextEdit& e) { e.ToggleStrikethrough(); });

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

    auto colourLabel = Theme::MakeLine(id + "ColourLbl", "Colour", Theme::kControlHeight,
                                       Theme::kSizeBody, Theme::kTextSecondary);
    colourLabel->SetElementSize(Size2Df(44, Theme::kControlHeight));
    colourLabel->SetAlignment(TextAlignment::Right);
    charRow->AddChild(colourLabel);
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

    // ----- Row 2: paragraphs, links, pictures -----
    auto paraRow = MakeRow(id + "ParaRow");
    auto keep = [&bar](const std::shared_ptr<UltraCanvasButton>& tool) {
        bar.paragraphTools_.push_back(tool.get());
    };
    keep(addTool(paraRow, id + "Left", "Left", "Align left", 40,
                 [](UltraCanvasRichTextEdit& e) { e.SetAlignment(RichTextAlign::Left); }));
    keep(addTool(paraRow, id + "Center", "Centre", "Centre", 50,
                 [](UltraCanvasRichTextEdit& e) { e.SetAlignment(RichTextAlign::Center); }));
    keep(addTool(paraRow, id + "Right", "Right", "Align right", 44,
                 [](UltraCanvasRichTextEdit& e) { e.SetAlignment(RichTextAlign::Right); }));
    keep(addTool(paraRow, id + "Bullets", "\xE2\x80\xA2 List", "Bulleted list", 50,
                 [](UltraCanvasRichTextEdit& e) { e.ToggleBulletList(); }));
    keep(addTool(paraRow, id + "Numbers", "1. List", "Numbered list", 50,
                 [](UltraCanvasRichTextEdit& e) { e.ToggleNumberedList(); }));
    keep(addTool(paraRow, id + "Rule", "Line", "A horizontal line", 40,
                 [](UltraCanvasRichTextEdit& e) { e.InsertHorizontalRule(); }));

    // Link: the address for the selected text (or the address itself, typed
    // at the cursor, when nothing is selected). The editor is looked up again
    // when the answer comes: the window may have closed meanwhile.
    auto link = CreateButton(id + "Link", 0, 0, 50, Theme::kControlHeight, "Link\xE2\x80\xA6");
    Theme::FitToLabel(link, 50);
    Theme::StyleSecondary(link);
    link->SetTooltip("Link the selected text to a web page or an e-mail address");
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
    auto picture = CreateButton(id + "Picture", 0, 0, 64, Theme::kControlHeight,
                                "Picture\xE2\x80\xA6");
    Theme::FitToLabel(picture, 64);
    Theme::StyleSecondary(picture);
    picture->SetTooltip("Put a picture (a logo, a photo) at the cursor");
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
        root->size.height = CSSLayout::Dimension::Px(
            visible ? 2 * Theme::kControlHeight + kRowGap : Theme::kControlHeight);
        root->InvalidateLayout();
    }
}

} // namespace UltraMail
