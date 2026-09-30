// Apps/UltraMail/ui/UltraMailSignatureDialog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSignatureDialog.h"

#include "UltraMailTheme.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasColorSwatchBar.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextUtils.h"   // Trim

#include "UltraCanvasRichDocument.h"
#include "UltraMailSignature.h"

#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace UltraMail {

namespace {

constexpr float kLabelWidth  = 70.0f;
constexpr float kToolButton  = 30.0f;

// Segment order of the kind switch.
constexpr int kSegmentNone = 0;
constexpr int kSegmentText = 1;
constexpr int kSegmentHtml = 2;

int SegmentFor(SignatureKind kind) {
    switch (kind) {
        case SignatureKind::Text: return kSegmentText;
        case SignatureKind::Html: return kSegmentHtml;
        case SignatureKind::Off: break;
    }
    return kSegmentNone;
}

SignatureKind KindAt(int segment) {
    return segment == kSegmentText ? SignatureKind::Text
         : segment == kSegmentHtml ? SignatureKind::Html
                                   : SignatureKind::Off;
}

// Fonts every mail program can show: a signature in a font the reader lacks
// falls back to theirs anyway.
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

// The controls the callbacks work on. Raw pointers: the dialog owns every
// element, and a callback stored on an element must not own it back (or a
// container above it) - see AGENTS.md. Every callback runs while the dialog,
// and so every element, is alive.
struct EditorState {
    UltraCanvasSegmentedControl* kind      = nullptr;
    UltraCanvasLabel*            noneNote  = nullptr;
    UltraCanvasTextArea*         text      = nullptr;
    UltraCanvasContainer*        htmlPane  = nullptr;
    UltraCanvasContainer*        formatRows = nullptr;
    UltraCanvasRichTextEdit*     rich      = nullptr;
    UltraCanvasTextArea*         source    = nullptr;
    UltraCanvasButton*           sourceBtn = nullptr;
    UltraCanvasCheckbox*         onReplies = nullptr;
    UltraCanvasLabel*            hint      = nullptr;
    bool                         sourceMode = false;
};

void ShowKind(EditorState& s, int segment) {
    s.noneNote->SetVisible(segment == kSegmentNone);
    s.text->SetVisible(segment == kSegmentText);
    s.htmlPane->SetVisible(segment == kSegmentHtml);
    s.onReplies->SetVisible(segment != kSegmentNone);
    s.hint->SetText(segment == kSegmentHtml
        ? "An HTML signature makes the messages it is added to formatted (HTML) "
          "messages, with a plain-text version for programs that show no HTML."
        : segment == kSegmentText
        ? "The signature is put below a \"-- \" line, which mail programs "
          "recognise as the start of a signature."
        : "");
    // Switching to HTML with nothing designed yet starts from the plain text.
    if (segment == kSegmentHtml && !s.sourceMode
        && SignatureHtmlFrom(*s.rich->GetDocument()).empty()) {
        const std::string plain = s.text->GetText();
        if (plain.find_first_not_of(" \t\r\n") != std::string::npos) {
            auto doc = PlainBodyToRichDocument(plain);
            // PlainBodyToRichDocument keeps the first line on its own; a
            // signature has no line to write on, so none is left empty.
            if (!doc->blocks.empty() && doc->blocks.front().runs.empty())
                doc->blocks.erase(doc->blocks.begin());
            if (doc->blocks.empty()) doc->blocks.push_back(RichDocBlock{});
            s.rich->SetDocument(doc);
        }
    }
    if (segment == kSegmentText) s.text->SetFocus(true);
    if (segment == kSegmentHtml) (s.sourceMode ? static_cast<UltraCanvasUIElement*>(s.source)
                                               : s.rich)->SetFocus(true);
}

// WYSIWYG <-> HTML source. The source is what the editor would store; going
// back reads it into the editor (what the model cannot hold is dropped).
void SetSourceMode(EditorState& s, bool on) {
    if (on == s.sourceMode) return;
    if (on) {
        s.source->SetText(SignatureHtmlFrom(*s.rich->GetDocument()));
    } else {
        s.rich->SetDocument(SignatureDocument(s.source->GetText()));
    }
    s.sourceMode = on;
    s.rich->SetVisible(!on);
    s.source->SetVisible(on);
    s.formatRows->SetVisible(!on);
    s.sourceBtn->SetText(on ? "Design" : "HTML source");
    s.sourceBtn->SetTooltip(on ? "Back to the editor"
                               : "Write or paste the signature as HTML");
    (on ? static_cast<UltraCanvasUIElement*>(s.source) : s.rich)->SetFocus(true);
}

Signature Collect(const EditorState& s) {
    Signature out;
    out.kind = KindAt(s.kind->GetSelectedIndex());
    out.text = s.text->GetText();
    out.html = s.sourceMode ? SignatureHtmlFrom(*SignatureDocument(s.source->GetText()))
                            : SignatureHtmlFrom(*s.rich->GetDocument());
    out.onReplies = s.onReplies->IsChecked();
    return out;
}

std::shared_ptr<UltraCanvasContainer> MakeRow(const std::string& id, float gap = 4.0f) {
    auto row = CreateContainer(id, 0, 0, 0, Theme::kControlHeight);
    row->layout.SetFlexRow()
               .SetFlexGap(gap)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    ContainerStyle style;
    style.autoShowScrollbars = false;
    row->SetContainerStyle(style);
    return row;
}

} // namespace

void SignatureDialog::Show(UltraCanvasWindowBase* parent, const std::string& email,
                           const Signature& current,
                           std::function<void(const Signature&)> onSave) {
    DialogConfig config;
    config.title      = "Signature for " + email;
    config.width      = 720;
    config.height     = 600;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    UltraCanvasModalDialog* dlg = dialog.get();   // no shared_ptr in element callbacks
    dialog->layout.SetFlexColumn()
                  .SetFlexGap(Theme::kGap)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(20);
    dialog->SetBackgroundColor(Theme::kCardBackground);

    auto state = std::make_shared<EditorState>();

    auto intro = Theme::MakeLine("sigIntro",
        "The signature is added below what you write in new messages from " + email + ".",
        Theme::kControlHeight, Theme::kSizeBody, Theme::kTextSecondary);
    intro->SetWrap(TextWrap::WrapWord);
    dialog->AddChild(intro);

    // ----- Kind: None | Plain text | HTML -----
    auto kindRow = MakeRow("sigKindRow", Theme::kInnerGap);
    auto kindLabel = Theme::MakeLine("sigKindLbl", "Signature", Theme::kControlHeight,
                                     Theme::kSizeBody, Theme::kTextSecondary);
    kindLabel->SetElementSize(Size2Df(kLabelWidth, Theme::kControlHeight));
    kindRow->AddChild(kindLabel);
    auto kind = CreateSegmentedControl("sigKind", 0, 0, 270, Theme::kControlHeight);
    {
        SegmentedControlStyle style = kind->GetStyle();
        style.fontSize = Theme::kSizeBody;
        style.selectedColor = Theme::kAccent;
        style.hoverColor = Theme::kAccentSoft;
        style.normalColor = Theme::kCardBackground;
        style.borderColor = Theme::kCardBorder;
        style.separatorColor = Theme::kCardBorder;
        style.cornerRadius = Theme::kControlRadius;
        style.paddingVertical = 3;
        kind->SetStyle(style);
    }
    kind->AddSegment("None");
    kind->AddSegment("Plain text");
    kind->AddSegment("HTML");
    kindRow->AddChild(kind);
    dialog->AddChild(kindRow);
    state->kind = kind.get();

    // ----- The editors: one visible at a time, taking the spare height -----
    auto noneNote = Theme::MakeLine("sigNone",
        "No signature is added to messages from this account.",
        Theme::kControlHeight, Theme::kSizeBody, Theme::kTextMuted);
    dialog->AddChild(noneNote);
    state->noneNote = noneNote.get();

    auto text = std::make_shared<UltraCanvasTextArea>("sigText", 0, 0, 0, 0);
    text->SetEditingMode(TextAreaEditingMode::PlainText);
    text->SetWordWrap(true);
    Theme::StyleTextArea(text);
    text->SetBorders(1.0f, Theme::kCardBorder);   // framed like the HTML editor
    text->SetPlaceholder("Erika Example\nSales, ACME Ltd.\n+49 30 1234567");
    text->SetText(current.text);
    dialog->AddChild(text);
    text->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    state->text = text.get();

    auto htmlPane = CreateContainer("sigHtmlPane", 0, 0, 0, 0);
    htmlPane->layout.SetFlexColumn()
                    .SetFlexGap(Theme::kInnerGap)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    {
        ContainerStyle style;
        style.autoShowScrollbars = false;
        htmlPane->SetContainerStyle(style);
    }
    state->htmlPane = htmlPane.get();

    auto formatRows = CreateContainer("sigFormat", 0, 0, 0, 2 * Theme::kControlHeight + 4.0f);
    formatRows->layout.SetFlexColumn()
                      .SetFlexGap(4.0f)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    {
        ContainerStyle style;
        style.autoShowScrollbars = false;
        formatRows->SetContainerStyle(style);
    }
    state->formatRows = formatRows.get();

    // A formatting button: runs `action` on the editor, then gives it the
    // keyboard back so typing continues where it was.
    auto addTool = [state](const std::shared_ptr<UltraCanvasContainer>& row,
                           const std::string& id, const std::string& label,
                           const std::string& tooltip, float width,
                           std::function<void(UltraCanvasRichTextEdit&)> action) {
        auto button = CreateButton(id, 0, 0, width, Theme::kControlHeight, label);
        Theme::FitToLabel(button, width);
        Theme::StyleSecondary(button);
        button->SetTooltip(tooltip);
        button->onClick = [state, action]() {
            action(*state->rich);
            state->rich->SetFocus(true);
        };
        row->AddChild(button);
        return button;
    };

    // Row 1: character formatting.
    auto charRow = MakeRow("sigCharRow");
    auto bold = addTool(charRow, "sigBold", "B", "Bold (Ctrl+B)", kToolButton,
                        [](UltraCanvasRichTextEdit& e) { e.ToggleBold(); });
    bold->SetFont("", Theme::kSizeBody, FontWeight::Bold);
    addTool(charRow, "sigItalic", "I", "Italic (Ctrl+I)", kToolButton,
            [](UltraCanvasRichTextEdit& e) { e.ToggleItalic(); });
    addTool(charRow, "sigUnderline", "U", "Underline (Ctrl+U)", kToolButton,
            [](UltraCanvasRichTextEdit& e) { e.ToggleUnderline(); });
    addTool(charRow, "sigStrike", "S", "Strikethrough", kToolButton,
            [](UltraCanvasRichTextEdit& e) { e.ToggleStrikethrough(); });

    auto font = CreateDropdown("sigFont", 0, 0, 130, Theme::kControlHeight);
    font->AddItem("Font", "");
    for (const char* name : kFonts) font->AddItem(name, name);
    Theme::StyleDropdown(font);
    font->SetSelectedIndex(0, /*runNotifications=*/false);
    font->SetTooltip("Font of the selected text");
    font->onSelectionChanged = [state](int, const DropdownItem& item) {
        state->rich->SetFontFamily(item.value);   // "" = the reader's default
        state->rich->SetFocus(true);
    };
    charRow->AddChild(font);

    auto size = CreateDropdown("sigSize", 0, 0, 64, Theme::kControlHeight);
    size->AddItem("Size", "");
    for (int pt : kSizes) size->AddItem(std::to_string(pt), std::to_string(pt));
    Theme::StyleDropdown(size);
    size->SetSelectedIndex(0, /*runNotifications=*/false);
    size->SetTooltip("Size of the selected text, in points");
    size->onSelectionChanged = [state](int index, const DropdownItem&) {
        // The items after "Size" are kSizes in order; "Size" itself = default.
        state->rich->SetFontSize(index > 0 ? static_cast<float>(kSizes[index - 1]) : 0.0f);
        state->rich->SetFocus(true);
    };
    charRow->AddChild(size);

    auto colourLabel = Theme::MakeLine("sigColourLbl", "Colour", Theme::kControlHeight,
                                       Theme::kSizeBody, Theme::kTextSecondary);
    colourLabel->SetElementSize(Size2Df(44, Theme::kControlHeight));
    colourLabel->SetAlignment(TextAlignment::Right);
    charRow->AddChild(colourLabel);
    auto colours = CreateColorSwatchBar("sigColour", 0, 0, 0, Theme::kControlHeight - 4.0f,
                                        TextPalette());
    colours->SetElementSize(Size2Df(colours->GetPreferredWidth(), Theme::kControlHeight - 4.0f));
    colours->SetTooltip("Colour of the selected text");
    colours->onColorSelected = [state](const Color& c) {
        state->rich->SetTextColor(c.ToHexString());
        state->rich->SetFocus(true);
    };
    charRow->AddChild(colours);
    charRow->AddStretchSpacer(1);
    formatRows->AddChild(charRow);

    // Row 2: paragraphs, links, pictures - and the switch to the HTML source.
    auto paraRow = MakeRow("sigParaRow");
    addTool(paraRow, "sigLeft", "Left", "Align left", 40,
            [](UltraCanvasRichTextEdit& e) { e.SetAlignment(RichTextAlign::Left); });
    addTool(paraRow, "sigCenter", "Centre", "Centre", 50,
            [](UltraCanvasRichTextEdit& e) { e.SetAlignment(RichTextAlign::Center); });
    addTool(paraRow, "sigRight", "Right", "Align right", 44,
            [](UltraCanvasRichTextEdit& e) { e.SetAlignment(RichTextAlign::Right); });
    addTool(paraRow, "sigBullets", "\xE2\x80\xA2 List", "Bulleted list", 50,
            [](UltraCanvasRichTextEdit& e) { e.ToggleBulletList(); });
    addTool(paraRow, "sigNumbers", "1. List", "Numbered list", 50,
            [](UltraCanvasRichTextEdit& e) { e.ToggleNumberedList(); });
    addTool(paraRow, "sigRule", "Line", "A horizontal line", 40,
            [](UltraCanvasRichTextEdit& e) { e.InsertHorizontalRule(); });

    // Link: the address for the selected text (or for the address itself,
    // typed at the caret, when nothing is selected).
    std::weak_ptr<UltraCanvasModalDialog> weak = dialog;
    auto link = CreateButton("sigLink", 0, 0, 50, Theme::kControlHeight, "Link\xE2\x80\xA6");
    Theme::FitToLabel(link, 50);
    Theme::StyleSecondary(link);
    link->SetTooltip("Link the selected text to a web page or an e-mail address");
    link->onClick = [state, weak, dlg]() {
        UltraCanvasDialogManager::ShowInputDialog(
            "Web page or e-mail address", "Link", "https://", InputType::Text,
            [state, weak](DialogResult result, const std::string& value) {
                if (result != DialogResult::OK || !weak.lock()) return;
                std::string target = UltraCanvas::Trim(value);
                if (target.empty() || target == "https://" || target == "http://") return;
                // A bare address is an e-mail link.
                if (target.find(':') == std::string::npos && target.find('@') != std::string::npos)
                    target = "mailto:" + target;
                UltraCanvasRichTextEdit& e = *state->rich;
                if (!e.HasSelection()) {
                    UCRichDocumentEditor& editor = e.GetEditor();
                    const RichDocPosition start = editor.GetCaret();
                    e.InsertText(target.rfind("mailto:", 0) == 0 ? target.substr(7) : target);
                    editor.SetSelection(start, editor.GetCaret());
                }
                e.SetLink(target);
                e.InvalidateDocument();
                e.SetFocus(true);
            },
            dlg);
    };
    paraRow->AddChild(link);

    // Picture: a logo or a photo, inside the line at the caret. It travels
    // with the signature (stored inline) and is sent as part of the message.
    auto picture = CreateButton("sigPicture", 0, 0, 64, Theme::kControlHeight,
                                "Picture\xE2\x80\xA6");
    Theme::FitToLabel(picture, 64);
    Theme::StyleSecondary(picture);
    picture->SetTooltip("Put a picture (a logo, a photo) at the cursor");
    picture->onClick = [state, weak, dlg]() {
        FileDialogOptions options;
        options.title = "Picture for the signature";
        options.parentWindow = dlg;
        options.AddFilter("Pictures", std::vector<std::string>{"png", "jpg", "jpeg", "gif"});
        UltraCanvasFileLoader::OpenFileDialog(
            options, [state, weak, dlg](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty() || !weak.lock()) return;
                if (!state->rich->InsertInlineImageFromFile(path)) {
                    UltraCanvasDialogManager::ShowError("Could not read the picture " + path,
                                                        "Signature", nullptr, dlg);
                    return;
                }
                state->rich->SetFocus(true);
            });
    };
    paraRow->AddChild(picture);

    paraRow->AddStretchSpacer(1);
    formatRows->AddChild(paraRow);
    htmlPane->AddChild(formatRows);

    auto rich = CreateRichTextEdit("sigRich", 0, 0, 0, 0);
    {
        RichTextEditStyle style = rich->GetStyle();
        style.baseFont.fontSize = Theme::kSizeBody + 1.0f;   // as the composer
        rich->SetStyle(style);
    }
    rich->SetDocument(SignatureDocument(current.html));
    htmlPane->AddChild(rich);
    rich->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    state->rich = rich.get();

    auto source = std::make_shared<UltraCanvasTextArea>("sigSource", 0, 0, 0, 0);
    source->SetEditingMode(TextAreaEditingMode::PlainText);
    source->SetWordWrap(true);
    Theme::StyleTextArea(source);
    source->SetBorders(1.0f, Theme::kCardBorder);
    // Coloured as HTML, as the mail source viewer is.
    source->SetHighlightSyntax(true);
    source->SetProgrammingLanguage("HTML");
    source->SetPlaceholder("<p><b>Erika Example</b><br>Sales, ACME Ltd.</p>");
    source->SetVisible(false);
    htmlPane->AddChild(source);
    source->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    state->source = source.get();

    dialog->AddChild(htmlPane);
    htmlPane->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // ----- Where it goes -----
    auto onReplies = std::make_shared<UltraCanvasCheckbox>(
        "sigOnReplies", 0, 0, 400, Theme::kControlHeight,
        "Add the signature to replies and forwards too");
    onReplies->SetFontSize(Theme::kSizeBody);
    onReplies->SetChecked(current.onReplies);
    dialog->AddChild(onReplies);
    state->onReplies = onReplies.get();

    auto hint = Theme::MakeLine("sigHint", "", 2 * Theme::kControlHeight,
                                Theme::kSizeBody, Theme::kTextSecondary);
    hint->SetWrap(TextWrap::WrapWord);
    dialog->AddChild(hint);
    state->hint = hint.get();

    // ----- Buttons: the source switch on the left, Cancel / Save right -----
    auto buttonRow = MakeRow("sigButtons", Theme::kInnerGap);
    buttonRow->SetElementSize(Size2Df(0, Theme::kToolbarHeight));
    auto sourceBtn = CreateButton("sigSourceBtn", 0, 0, 110, Theme::kControlHeight, "HTML source");
    Theme::FitToLabel(sourceBtn, 110);
    Theme::StyleSecondary(sourceBtn);
    sourceBtn->SetTooltip("Write or paste the signature as HTML");
    sourceBtn->onClick = [state]() { SetSourceMode(*state, !state->sourceMode); };
    buttonRow->AddChild(sourceBtn);
    state->sourceBtn = sourceBtn.get();

    buttonRow->AddStretchSpacer(1);
    auto cancelBtn = CreateButton("sigCancel", 0, 0, 90, Theme::kControlHeight, "Cancel");
    Theme::FitToLabel(cancelBtn, 90);
    Theme::StyleSecondary(cancelBtn);
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);

    auto result = std::make_shared<Signature>(current);
    auto saveBtn = CreateButton("sigSave", 0, 0, 90, Theme::kControlHeight, "Save");
    Theme::FitToLabel(saveBtn, 90);
    Theme::StylePrimary(saveBtn);
    saveBtn->onClick = [dlg, state, result]() {
        *result = Collect(*state);
        dlg->CloseDialog(DialogResult::OK);
    };
    buttonRow->AddChild(saveBtn);
    dialog->AddChild(buttonRow);

    // The source switch belongs to the HTML editor only.
    kind->onSegmentSelected = [state](int segment) {
        state->sourceBtn->SetVisible(segment == kSegmentHtml);
        ShowKind(*state, segment);
    };
    const int initial = SegmentFor(current.kind);
    kind->SetSelectedIndex(initial);
    state->sourceBtn->SetVisible(initial == kSegmentHtml);
    ShowKind(*state, initial);

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [result, onSave](DialogResult res) {
            if (res == DialogResult::OK && onSave) onSave(*result);
        },
        parent);
}

} // namespace UltraMail
