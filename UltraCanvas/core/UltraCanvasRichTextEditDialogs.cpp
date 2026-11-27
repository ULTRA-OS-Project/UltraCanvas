// core/UltraCanvasRichTextEditDialogs.cpp
// The document dialogs of UltraCanvasRichTextEdit (UltraCanvasRichTextEditDialogs.h):
// what each reads from the document, what its OK applies - through the
// element's calls, so each change is one undo step where the editor keeps one
// - and the form that shows them, built of the framework's own elements
// (UltraCanvasFormLayout in an UltraCanvasModalDialog).
// Version: 1.1.1 - the Picture dialog shows a picture of its own size at that size (GetImageDisplaySize), not 0 x 0
// Version: 1.1.0 - the Styles dialog's font from every installed font (UltraCanvasFontPicker), not nine
// Version: 1.0.0
// Last Modified: 2026-11-27
// Author: UltraCanvas Framework

#include "UltraCanvasRichTextEditDialogs.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFontPicker.h"
#include "UltraCanvasFormLayout.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasSpinner.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ============================================================================
// WHAT THE DIALOGS READ AND APPLY
// ============================================================================

namespace {

constexpr float kPointsPerCm = 72.0f / 2.54f;

bool Near(float a, float b) { return std::fabs(a - b) < 1.0f; }

} // namespace

const std::vector<RichPaperSize>& RichPaperSizes() {
    static const std::vector<RichPaperSize> sizes = {
        {"A3", 841.89f, 1190.55f},
        {"A4", 595.28f, 841.89f},
        {"A5", 419.53f, 595.28f},
        {"B5", 498.90f, 708.66f},
        {"Letter", 612.0f, 792.0f},
        {"Legal", 612.0f, 1008.0f},
        {"Tabloid", 792.0f, 1224.0f}};
    return sizes;
}

// ---- Page setup ----

RichPageSetupDialogValues ReadPageSetupDialogValues(const UltraCanvasRichTextEdit& edit) {
    const RichPageSetup page = edit.GetEffectivePageSetup();
    RichPageSetupDialogValues values;
    values.widthPt = page.widthPt;
    values.heightPt = page.heightPt;
    values.landscape = page.widthPt > page.heightPt;
    const float shortSide = std::min(page.widthPt, page.heightPt);
    const float longSide = std::max(page.widthPt, page.heightPt);
    const auto& sizes = RichPaperSizes();
    for (size_t i = 0; i < sizes.size(); ++i) {
        if (Near(sizes[i].widthPt, shortSide) && Near(sizes[i].heightPt, longSide)) {
            values.paper = static_cast<int>(i);
            break;
        }
    }
    values.marginTopPt = page.marginTopPt;
    values.marginBottomPt = page.marginBottomPt;
    values.marginLeftPt = page.marginLeftPt;
    values.marginRightPt = page.marginRightPt;
    values.headerTopPt = page.headerTopPt;
    values.footerBottomPt = page.footerBottomPt;
    return values;
}

RichPageSetup PageSetupFromDialogValues(const RichPageSetupDialogValues& values) {
    RichPageSetup page;
    const auto& sizes = RichPaperSizes();
    if (values.paper >= 0 && values.paper < static_cast<int>(sizes.size())) {
        const RichPaperSize& paper = sizes[static_cast<size_t>(values.paper)];
        page.widthPt = values.landscape ? paper.heightPt : paper.widthPt;
        page.heightPt = values.landscape ? paper.widthPt : paper.heightPt;
    } else {
        page.widthPt = values.widthPt;
        page.heightPt = values.heightPt;
    }
    page.marginTopPt = std::max(0.0f, values.marginTopPt);
    page.marginBottomPt = std::max(0.0f, values.marginBottomPt);
    page.marginLeftPt = std::max(0.0f, values.marginLeftPt);
    page.marginRightPt = std::max(0.0f, values.marginRightPt);
    page.headerTopPt = std::max(0.0f, values.headerTopPt);
    page.footerBottomPt = std::max(0.0f, values.footerBottomPt);
    return page;
}

bool ApplyPageSetupDialogValues(UltraCanvasRichTextEdit& edit, const RichPageSetupDialogValues& values) {
    const RichPageSetup page = PageSetupFromDialogValues(values);
    // The text column needs some room between the margins.
    if (page.marginLeftPt + page.marginRightPt > page.widthPt - 36.0f) return false;
    if (page.marginTopPt + page.marginBottomPt > page.heightPt - 36.0f) return false;
    return edit.SetPageSetup(page);
}

// ---- Paragraph ----

RichParagraphFormat ReadParagraphDialogValues(const UltraCanvasRichTextEdit& edit) {
    return edit.GetParagraphFormat();
}

bool ApplyParagraphDialogValues(UltraCanvasRichTextEdit& edit, const RichParagraphFormat& values) {
    return edit.SetParagraphFormat(values);
}

// ---- Styles ----

RichStylesDialogValues StylesDialogValuesFor(const UltraCanvasRichTextEdit& edit, const std::string& styleId) {
    RichStylesDialogValues values;
    values.styleId = styleId;
    for (const RichStyle& style : edit.GetStyles()) {
        if (style.id != styleId) continue;
        values.name = style.name.empty() ? style.id : style.name;
        values.character = style.character;
        values.paragraph = style.paragraph;
        break;
    }
    return values;
}

RichStylesDialogValues ReadStylesDialogValues(const UltraCanvasRichTextEdit& edit) {
    return StylesDialogValuesFor(edit, edit.GetCurrentParagraphStyle());
}

bool ApplyStylesDialogValues(UltraCanvasRichTextEdit& edit, const RichStylesDialogValues& values) {
    switch (values.action) {
        case RichStylesDialogValues::Action::Apply: {
            for (const RichStyle& style : edit.GetStyles()) {
                if (style.id != values.styleId) continue;
                return style.kind == RichStyle::Kind::Paragraph ? edit.ApplyParagraphStyle(style.id)
                                                                : edit.ApplyCharacterStyle(style.id);
            }
            return false;
        }
        case RichStylesDialogValues::Action::Change: {
            for (const RichStyle& style : edit.GetStyles()) {
                if (style.id != values.styleId) continue;
                RichStyle changed = style;
                if (!values.name.empty()) changed.name = values.name;
                // What the dialog shows replaces what the style said; what it
                // does not show is kept.
                changed.character.Overlay(values.character);
                if (changed.kind == RichStyle::Kind::Paragraph) changed.paragraph.Overlay(values.paragraph);
                return edit.UpdateStyle(changed);
            }
            return false;
        }
        case RichStylesDialogValues::Action::NewFromSelection:
            return !values.name.empty() && edit.NewStyleFromCaret(values.name);
    }
    return false;
}

// ---- Table ----

RichTableProperties ReadTableDialogValues(const UltraCanvasRichTextEdit& edit) {
    RichTableProperties values;
    edit.GetTableProperties(values);
    return values;
}

bool ApplyTableDialogValues(UltraCanvasRichTextEdit& edit, const RichTableProperties& values) {
    return edit.SetTableProperties(values);
}

// ---- Picture ----

RichPictureDialogValues ReadPictureDialogValues(const UltraCanvasRichTextEdit& edit) {
    RichPictureDialogValues values;
    if (!edit.HasSelectedImage()) return values;
    values.altText = edit.GetSelectedImageAltText();
    RichTextAlign align = RichTextAlign::Left;
    edit.GetSelectedImageWrap(values.wrap, align);
    values.align = align == RichTextAlign::Default ? RichTextAlign::Left : align;
    // The size it is laid out at: a picture of its own size (0 in the
    // document) shows that size, not 0 x 0.
    if (!edit.GetImageDisplaySize(edit.GetSelectedImage(), values.widthPt, values.heightPt)) {
        std::string alt;
        int media = -1;
        edit.GetEditor().GetImageInfo(edit.GetSelectedImage(), values.widthPt, values.heightPt, alt, media);
    }
    return values;
}

bool ApplyPictureDialogValues(UltraCanvasRichTextEdit& edit, const RichPictureDialogValues& values) {
    if (!edit.HasSelectedImage()) return false;
    const RichPictureDialogValues now = ReadPictureDialogValues(edit);
    bool changed = false;
    if (values.altText != now.altText) changed = edit.SetSelectedImageAltText(values.altText) || changed;
    if (values.widthPt > 0.0f && values.heightPt > 0.0f
        && (!Near(values.widthPt, now.widthPt) || !Near(values.heightPt, now.heightPt))) {
        changed = edit.SetSelectedImageSize(values.widthPt, values.heightPt) || changed;
    }
    if (values.wrap != now.wrap || (values.wrap != RichTextRun::ImageWrap::Inline && values.align != now.align)) {
        changed = edit.SetSelectedImageWrap(values.wrap, values.align) || changed;
    }
    return changed;
}

// ---- Contents, caption, cross-reference, note ----

bool HasTableOfContents(const UCRichDocument& document) {
    return std::any_of(document.blocks.begin(), document.blocks.end(),
                       [](const RichDocBlock& block) { return block.tocLevel > 0; });
}

RichContentsDialogValues ReadContentsDialogValues(const UltraCanvasRichTextEdit& edit) {
    RichContentsDialogValues values;
    values.replaceExisting = HasTableOfContents(*edit.GetDocument());
    if (values.replaceExisting) {
        int deepest = 0;
        for (const RichDocBlock& block : edit.GetDocument()->blocks) deepest = std::max(deepest, block.tocLevel);
        values.maxLevel = std::clamp(deepest, 1, 9);
    }
    return values;
}

bool ApplyContentsDialogValues(UltraCanvasRichTextEdit& edit, const RichContentsDialogValues& values) {
    const int levels = std::clamp(values.maxLevel, 1, 9);
    if (values.replaceExisting && HasTableOfContents(*edit.GetDocument())) {
        return edit.UpdateTableOfContents(levels);
    }
    return edit.InsertTableOfContents(levels);
}

RichCaptionDialogValues ReadCaptionDialogValues(const UltraCanvasRichTextEdit& edit) {
    RichCaptionDialogValues values;
    const UCRichDocumentEditor& editor = edit.GetEditor();
    const RichDocPosition caret = editor.GetCaret();
    const bool onTable = caret.InCell() || (caret.blockIndex >= 0 && caret.blockIndex < editor.GetBlockCount()
                                            && editor.GetBlock(caret.blockIndex).type == RichBlockType::Table);
    values.label = onTable ? "Table" : "Figure";
    return values;
}

bool ApplyCaptionDialogValues(UltraCanvasRichTextEdit& edit, const RichCaptionDialogValues& values) {
    if (values.label.empty()) return false;
    return !edit.InsertCaption(values.label, values.text).empty();
}

std::vector<RichCrossReferenceTarget> RichCrossReferenceTargets(const UCRichDocument& document) {
    std::vector<RichCrossReferenceTarget> targets;
    for (const UCRichDocument::BookmarkInfo& bookmark : document.Bookmarks()) {
        if (bookmark.name.rfind("_Toc", 0) == 0) continue;           // the contents' own marks
        RichCrossReferenceTarget target;
        target.bookmark = bookmark.name;
        target.shown = bookmark.name;
        if (bookmark.name.rfind("_Ref", 0) == 0 && bookmark.blockIndex >= 0
            && bookmark.blockIndex < static_cast<int>(document.blocks.size())) {
            // A caption's mark: its text says what it is ("Figure 2 Sales").
            target.shown = UCRichDocument::ConcatenateRunText(
                document.blocks[static_cast<size_t>(bookmark.blockIndex)].runs);
            if (target.shown.size() > 60) target.shown = target.shown.substr(0, 60) + "...";
            if (target.shown.empty()) target.shown = bookmark.name;
        }
        targets.push_back(std::move(target));
    }
    return targets;
}

RichCrossReferenceDialogValues ReadCrossReferenceDialogValues(const UltraCanvasRichTextEdit& edit) {
    RichCrossReferenceDialogValues values;
    const std::vector<RichCrossReferenceTarget> targets = RichCrossReferenceTargets(*edit.GetDocument());
    if (!targets.empty()) values.bookmark = targets.front().bookmark;
    return values;
}

bool ApplyCrossReferenceDialogValues(UltraCanvasRichTextEdit& edit, const RichCrossReferenceDialogValues& values) {
    if (values.bookmark.empty()) return false;
    return edit.InsertCrossReference(values.bookmark, values.pageNumber);
}

RichNoteDialogValues ReadNoteDialogValues(const UltraCanvasRichTextEdit& edit) {
    (void)edit;
    return RichNoteDialogValues();
}

bool ApplyNoteDialogValues(UltraCanvasRichTextEdit& edit, const RichNoteDialogValues& values) {
    return edit.InsertNote(values.kind, values.text) >= 0;
}

// ---- Comments ----

RichCommentDialogValues ReadCommentDialogValues(const UltraCanvasRichTextEdit& edit, int index) {
    RichCommentDialogValues values;
    const auto& comments = edit.GetDocument()->comments;
    if (index >= 0 && index < static_cast<int>(comments.size())) {
        values.index = index;
        values.text = comments[static_cast<size_t>(index)].text;
        values.resolved = comments[static_cast<size_t>(index)].resolved;
    }
    return values;
}

bool ApplyCommentDialogValues(UltraCanvasRichTextEdit& edit, const RichCommentDialogValues& values) {
    if (values.index < 0) return !values.text.empty() && edit.AddComment(values.text) >= 0;
    const auto& comments = edit.GetDocument()->comments;
    if (values.index >= static_cast<int>(comments.size())) return false;
    const RichComment now = comments[static_cast<size_t>(values.index)];
    bool changed = false;
    if (values.text != now.text) changed = edit.SetCommentText(values.index, values.text) || changed;
    if (values.resolved != now.resolved) changed = edit.SetCommentResolved(values.index, values.resolved) || changed;
    return changed;
}

std::string DescribeRichComment(const RichComment& comment) {
    std::string text = comment.text;
    std::replace(text.begin(), text.end(), '\n', ' ');
    if (text.size() > 60) {
        size_t cut = 60;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;   // a whole character
        text = text.substr(0, cut) + "...";
    }
    std::string line = comment.author.empty() ? text : comment.author + ": " + text;
    if (comment.resolved) line += " (" + Tr("Resolved") + ")";
    return line;
}

bool ApplyCommentsDialogValues(UltraCanvasRichTextEdit& edit, const RichCommentsDialogValues& values) {
    const auto& comments = edit.GetDocument()->comments;
    if (values.index < 0 || values.index >= static_cast<int>(comments.size())) return false;
    switch (values.action) {
        case RichCommentsDialogValues::Action::GoTo:
            return edit.GoToComment(values.index);
        case RichCommentsDialogValues::Action::ToggleResolved:
            return edit.SetCommentResolved(values.index, !comments[static_cast<size_t>(values.index)].resolved);
        case RichCommentsDialogValues::Action::Delete:
            return edit.RemoveComment(values.index);
    }
    return false;
}

// ============================================================================
// THE FORMS
// ============================================================================

namespace {

constexpr int kRowHeight = 28;
constexpr float kRowGap = 8.0f;
constexpr float kColumnGap = 12.0f;
constexpr float kNumberWidth = 150.0f;

// A dialog holding a form, its buttons and no message. Its height follows
// the rows (FitForm); its width is the form's.
struct DialogForm {
    std::shared_ptr<UltraCanvasModalDialog> dialog;
    std::shared_ptr<UltraCanvasContainer> form;
};

float FormHeight(float rows, float extra) {
    return rows * kRowHeight + kRowGap * std::max(0.0f, rows - 1.0f) + extra;
}

DialogForm MakeDialogForm(const std::string& id, const std::string& title, float rows, int width = 520,
                          float extra = 0.0f, DialogButtons buttons = DialogButtons::OKCancel) {
    DialogConfig config;
    config.title = title;
    config.dialogType = DialogType::Information;
    config.buttons = buttons;
    config.defaultButton = DialogButton::OK;
    config.showIcon = false;
    config.width = width;
    config.height = static_cast<int>(FormHeight(rows, extra)) + 110;
    DialogForm out;
    out.dialog = UltraCanvasDialogManager::CreateDialog(config);
    if (!out.dialog) return out;
    out.dialog->SetMessageVisible(false);
    out.form = CreateFormGrid(id, kRowGap, kColumnGap);
    out.form->size.height = CSSLayout::Dimension::Px(FormHeight(rows, extra));
    return out;
}

// The form as tall as the rows it shows, so a dialog whose rows follow a
// choice has no empty band under them; once the dialog is on screen its
// window grows or shrinks by as much as the form did.
void FitForm(UltraCanvasModalDialog* dialog, UltraCanvasContainer* form, float extra = 0.0f) {
    if (!dialog || !form) return;
    int rows = 0;
    for (const auto& child : form->GetChildren()) {
        if (!child || !child->IsVisible()) continue;
        if (dynamic_cast<UltraCanvasLabel*>(child.get())) ++rows;
        else if (const auto* cell = std::get_if<CSSLayout::GridItem>(&child->layoutItem.data)) {
            if (cell->columnEnd.type == CSSLayout::GridLineKind::Span && cell->columnEnd.index > 1) ++rows;
        }
    }
    const float height = FormHeight(static_cast<float>(std::max(4, rows)), extra);
    const float before = form->GetHeight();
    form->size.height = CSSLayout::Dimension::Px(height);
    form->InvalidateLayout();
    if (dialog->IsWindowVisible() && before > 0.0f && std::abs(height - before) >= 1.0f) {
        dialog->SetWindowSize(static_cast<int>(dialog->GetWidth()),
                              static_cast<int>(std::lround(dialog->GetHeight() + height - before)));
    }
}

std::shared_ptr<UltraCanvasDropdown> MakeChoice(const std::string& id, const std::vector<std::string>& items,
                                                int selected) {
    auto list = std::make_shared<UltraCanvasDropdown>(id, 0, 0, 0, kRowHeight);
    for (const std::string& item : items) list->AddItem(item);
    if (!items.empty()) list->SetSelectedIndex(std::clamp(selected, 0, static_cast<int>(items.size()) - 1), false);
    return list;
}

std::shared_ptr<UltraCanvasTextInput> MakeField(const std::string& id, const std::string& text) {
    auto field = std::make_shared<UltraCanvasTextInput>(id, 0, 0, 0, kRowHeight);
    field->SetText(text);
    return field;
}

std::shared_ptr<UltraCanvasCheckbox> MakeCheck(const std::string& id, const std::string& text, bool checked) {
    auto box = UltraCanvasCheckbox::CreateCheckbox(id, 0, 0, -1, kRowHeight, text, checked);
    box->layoutItem.SetJustifySelf(CSSLayout::JustifySelf::Start);
    return box;
}

// A length in centimetres, kept in points.
std::shared_ptr<UltraCanvasSpinner> MakeCentimetres(const std::string& id, float valuePt, double minCm,
                                                    double maxCm) {
    auto spin = CreateDecimalSpinner(id, 0, 0, kNumberWidth, kRowHeight, minCm, maxCm,
                                     static_cast<double>(valuePt / kPointsPerCm), 0.1, 2);
    // A document's 0.5 inch stays 1.27 cm: the arrows step by 0.1 without
    // moving a value onto their grid.
    spin->SetSnapToStep(false);
    spin->SetValue(static_cast<double>(valuePt / kPointsPerCm));
    spin->SetSuffix(" " + Tr("cm"));
    spin->layoutItem.SetJustifySelf(CSSLayout::JustifySelf::Start);
    return spin;
}

// "12 pt", or Auto for a value that is not stated (below 0).
std::shared_ptr<UltraCanvasSpinner> MakePoints(const std::string& id, float valuePt, double maxPt, bool withAuto,
                                               double step = 1.0) {
    auto spin = CreateDecimalSpinner(id, 0, 0, kNumberWidth, kRowHeight, withAuto ? -1.0 : 0.0, maxPt,
                                     withAuto && valuePt < 0.0f ? -1.0 : static_cast<double>(valuePt), step, 1);
    const std::string automatic = Tr("Auto");
    const std::string unit = Tr("pt");
    spin->SetFormatter([automatic, unit](double v) {
        if (v < 0.0) return automatic;
        char text[32];
        std::snprintf(text, sizeof text, "%.1f %s", v, unit.c_str());   // locale-ok: a value a person reads
        return std::string(text);
    });
    spin->SetSnapToStep(false);
    spin->SetValue(withAuto && valuePt < 0.0f ? -1.0 : static_cast<double>(valuePt));
    spin->layoutItem.SetJustifySelf(CSSLayout::JustifySelf::Start);
    return spin;
}

float PointsOfCentimetres(double cm) { return static_cast<float>(cm) * kPointsPerCm; }

// A form row that can be shown or hidden: its caption and its control.
struct FormRow {
    UltraCanvasLabel* caption = nullptr;
    UltraCanvasUIElement* control = nullptr;
    void Show(bool visible) const {
        if (caption) caption->SetVisible(visible);
        if (control) control->SetVisible(visible);
    }
};

FormRow AddRow(const std::shared_ptr<UltraCanvasContainer>& form, const std::string& id, const std::string& caption,
               const std::shared_ptr<UltraCanvasUIElement>& control) {
    FormRow row;
    row.caption = AddFormRow(form, id, caption, control).get();
    row.control = control.get();
    return row;
}

// The caption of a control several lines tall (a text box) stands level with
// its first line rather than with its middle.
FormRow AddTallRow(const std::shared_ptr<UltraCanvasContainer>& form, const std::string& id,
                   const std::string& caption, const std::shared_ptr<UltraCanvasUIElement>& control) {
    FormRow row = AddRow(form, id, caption, control);
    if (row.caption) {
        row.caption->layoutItem.SetJustifySelf(CSSLayout::JustifySelf::Stretch);
        row.caption->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Start);
        row.caption->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
        row.caption->SetPadding(4, 0, 0, 0);
    }
    return row;
}

// The element the dialog was opened for, held weakly: closing its tab while
// the dialog is open does not keep it alive, and the dialog then does nothing.
std::weak_ptr<UltraCanvasRichTextEdit> WeakOf(UltraCanvasRichTextEdit& edit) {
    return std::static_pointer_cast<UltraCanvasRichTextEdit>(edit.shared_from_this());
}

// Shows the dialog; on OK runs `apply` on the element. onClosed hears
// whether the document changed either way.
void ShowForm(const DialogForm& built, UltraCanvasRichTextEdit& edit,
              std::function<bool(UltraCanvasRichTextEdit&)> apply, RichDialogClosed onClosed,
              DialogResult applyOn = DialogResult::OK) {
    std::weak_ptr<UltraCanvasRichTextEdit> weak = WeakOf(edit);
    UltraCanvasDialogManager::ShowDialog(built.dialog, [weak, apply, onClosed, applyOn](DialogResult result) {
        bool applied = false;
        if (auto target = weak.lock()) {
            if (result == applyOn && apply) applied = apply(*target);
            target->SetFocus(true);
        }
        if (onClosed) onClosed(applied);
    }, edit.GetWindow());
}

// A dialog that has nothing to work on says why instead.
void Explain(UltraCanvasRichTextEdit& edit, const std::string& message, const std::string& title,
             const RichDialogClosed& onClosed) {
    UltraCanvasDialogManager::ShowMessage(message, title, DialogType::Information, DialogButtons::OK,
        [onClosed](DialogResult) { if (onClosed) onClosed(false); }, edit.GetWindow());
}

std::vector<std::string> AlignmentNames() {
    return {Tr("Left"), Tr("Centre"), Tr("Right"), Tr("Justify")};
}

int AlignmentIndex(RichTextAlign align) {
    switch (align) {
        case RichTextAlign::Center: return 1;
        case RichTextAlign::Right: return 2;
        case RichTextAlign::Justify: return 3;
        default: return 0;
    }
}

RichTextAlign AlignmentOf(int index) {
    switch (index) {
        case 1: return RichTextAlign::Center;
        case 2: return RichTextAlign::Right;
        case 3: return RichTextAlign::Justify;
        default: return RichTextAlign::Left;
    }
}

} // namespace

// ---- Page setup ----

void ShowPageSetupDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed) {
    auto state = std::make_shared<RichPageSetupDialogValues>(ReadPageSetupDialogValues(edit));
    DialogForm built = MakeDialogForm("PageSetupForm", Tr("Page setup"), 10);
    if (!built.dialog) return;
    auto& form = built.form;

    std::vector<std::string> papers;
    for (const RichPaperSize& paper : RichPaperSizes()) {
        // The ISO sizes are names in every language; the American ones words.
        if (paper.name == "Letter") papers.push_back(Tr("Letter"));
        else if (paper.name == "Legal") papers.push_back(Tr("Legal"));
        else if (paper.name == "Tabloid") papers.push_back(Tr("Tabloid"));
        else papers.push_back(paper.name);
    }
    papers.push_back(Tr("Custom"));
    const int customIndex = static_cast<int>(papers.size()) - 1;
    auto paper = MakeChoice("PageSetupPaper", papers, state->paper < 0 ? customIndex : state->paper);
    auto orientation = MakeChoice("PageSetupOrientation", {Tr("Portrait"), Tr("Landscape")}, state->landscape ? 1 : 0);
    auto width = MakeCentimetres("PageSetupWidth", state->widthPt, 5.0, 150.0);
    auto height = MakeCentimetres("PageSetupHeight", state->heightPt, 5.0, 150.0);
    auto top = MakeCentimetres("PageSetupTop", state->marginTopPt, 0.0, 20.0);
    auto bottom = MakeCentimetres("PageSetupBottom", state->marginBottomPt, 0.0, 20.0);
    auto left = MakeCentimetres("PageSetupLeft", state->marginLeftPt, 0.0, 20.0);
    auto right = MakeCentimetres("PageSetupRight", state->marginRightPt, 0.0, 20.0);
    auto header = MakeCentimetres("PageSetupHeader", state->headerTopPt, 0.0, 20.0);
    auto footer = MakeCentimetres("PageSetupFooter", state->footerBottomPt, 0.0, 20.0);

    AddRow(form, "PageSetupPaper", Tr("Paper") + ":", paper);
    AddRow(form, "PageSetupOrientation", Tr("Orientation") + ":", orientation);
    AddRow(form, "PageSetupWidth", Tr("Width") + ":", width);
    AddRow(form, "PageSetupHeight", Tr("Height") + ":", height);
    AddRow(form, "PageSetupTop", Tr("Top margin") + ":", top);
    AddRow(form, "PageSetupBottom", Tr("Bottom margin") + ":", bottom);
    AddRow(form, "PageSetupLeft", Tr("Left margin") + ":", left);
    AddRow(form, "PageSetupRight", Tr("Right margin") + ":", right);
    AddRow(form, "PageSetupHeader", Tr("Header from the top edge") + ":", header);
    AddRow(form, "PageSetupFooter", Tr("Footer from the bottom edge") + ":", footer);
    built.dialog->AddDialogElement(form);
    HoldFormCaptionColumn(form);

    // Raw: the dialog owns them all.
    UltraCanvasDropdown* paperRaw = paper.get();
    UltraCanvasDropdown* orientationRaw = orientation.get();
    UltraCanvasSpinner* widthRaw = width.get();
    UltraCanvasSpinner* heightRaw = height.get();
    // The size the paper and orientation give, shown in the two fields.
    auto showSize = [state, widthRaw, heightRaw]() {
        const RichPageSetup page = PageSetupFromDialogValues(*state);
        state->widthPt = page.widthPt;
        state->heightPt = page.heightPt;
        widthRaw->SetValue(page.widthPt / kPointsPerCm);
        heightRaw->SetValue(page.heightPt / kPointsPerCm);
    };
    paper->onSelectionChanged = [state, showSize, customIndex](int index, const DropdownItem&) {
        state->paper = index >= customIndex ? -1 : index;
        showSize();
    };
    orientation->onSelectionChanged = [state, showSize](int index, const DropdownItem&) {
        const bool landscape = index == 1;
        if (landscape == state->landscape) return;
        state->landscape = landscape;
        if (state->paper < 0) std::swap(state->widthPt, state->heightPt);   // a custom paper turned
        showSize();
    };
    // A size typed in is a custom paper, unless it is a listed one.
    auto sizeTyped = [state, paperRaw, orientationRaw, customIndex]() {
        state->paper = -1;
        const float shortSide = std::min(state->widthPt, state->heightPt);
        const float longSide = std::max(state->widthPt, state->heightPt);
        const auto& sizes = RichPaperSizes();
        for (size_t i = 0; i < sizes.size(); ++i) {
            if (Near(sizes[i].widthPt, shortSide) && Near(sizes[i].heightPt, longSide)) state->paper = static_cast<int>(i);
        }
        state->landscape = state->widthPt > state->heightPt;
        paperRaw->SetSelectedIndex(state->paper < 0 ? customIndex : state->paper, false);
        orientationRaw->SetSelectedIndex(state->landscape ? 1 : 0, false);
    };
    width->onValueChanged = [state, sizeTyped](double v) {
        const float pt = PointsOfCentimetres(v);
        if (Near(pt, state->widthPt)) return;
        state->widthPt = pt;
        sizeTyped();
    };
    height->onValueChanged = [state, sizeTyped](double v) {
        const float pt = PointsOfCentimetres(v);
        if (Near(pt, state->heightPt)) return;
        state->heightPt = pt;
        sizeTyped();
    };
    top->onValueChanged = [state](double v) { state->marginTopPt = PointsOfCentimetres(v); };
    bottom->onValueChanged = [state](double v) { state->marginBottomPt = PointsOfCentimetres(v); };
    left->onValueChanged = [state](double v) { state->marginLeftPt = PointsOfCentimetres(v); };
    right->onValueChanged = [state](double v) { state->marginRightPt = PointsOfCentimetres(v); };
    header->onValueChanged = [state](double v) { state->headerTopPt = PointsOfCentimetres(v); };
    footer->onValueChanged = [state](double v) { state->footerBottomPt = PointsOfCentimetres(v); };

    ShowForm(built, edit, [state](UltraCanvasRichTextEdit& target) {
        return ApplyPageSetupDialogValues(target, *state);
    }, std::move(onClosed));
}

// ---- Paragraph ----

namespace {

// The line spacing list, in its order.
enum class LineChoice { Single, OneFifteen, OneAndAHalf, Double, Multiple, AtLeast, Exactly };

LineChoice LineChoiceOf(const RichParagraphFormat& format) {
    if (format.lineHeightPt > 0.0f) return format.lineHeightAtLeast ? LineChoice::AtLeast : LineChoice::Exactly;
    if (format.lineSpacing <= 0.0f || Near(format.lineSpacing * 100.0f, 100.0f)) return LineChoice::Single;
    if (Near(format.lineSpacing * 100.0f, 115.0f)) return LineChoice::OneFifteen;
    if (Near(format.lineSpacing * 100.0f, 150.0f)) return LineChoice::OneAndAHalf;
    if (Near(format.lineSpacing * 100.0f, 200.0f)) return LineChoice::Double;
    return LineChoice::Multiple;
}

struct ParagraphDialogState {
    RichParagraphFormat format;
    int lineChoice = 0;
    double lineValue = 1.0;            // the multiple, or the height in points
    int firstLine = 0;                 // none, first line, hanging
    double firstLineBy = 0.0;          // centimetres
    double tabPosition = 1.25;         // centimetres
    int tabKind = 0;
    int pickedTab = 0;
};

std::string DescribeTabStop(const RichTabStop& stop) {
    const std::vector<std::string> kinds = {Tr("Left"), Tr("Centre"), Tr("Right"), Tr("Decimal")};
    char number[32];
    std::snprintf(number, sizeof number, "%.2f", stop.positionPt / kPointsPerCm);   // locale-ok: a value a person reads
    return std::string(number) + " " + Tr("cm") + "  " + kinds[static_cast<size_t>(stop.kind)];
}

// The format the state's choices make.
RichParagraphFormat FormatOfState(const ParagraphDialogState& state) {
    RichParagraphFormat format = state.format;
    switch (static_cast<LineChoice>(state.lineChoice)) {
        case LineChoice::Single: format.lineSpacing = 0.0f; format.lineHeightPt = 0.0f; break;
        case LineChoice::OneFifteen: format.lineSpacing = 1.15f; format.lineHeightPt = 0.0f; break;
        case LineChoice::OneAndAHalf: format.lineSpacing = 1.5f; format.lineHeightPt = 0.0f; break;
        case LineChoice::Double: format.lineSpacing = 2.0f; format.lineHeightPt = 0.0f; break;
        case LineChoice::Multiple:
            format.lineSpacing = static_cast<float>(state.lineValue);
            format.lineHeightPt = 0.0f;
            break;
        case LineChoice::AtLeast:
        case LineChoice::Exactly:
            format.lineSpacing = 0.0f;
            format.lineHeightPt = static_cast<float>(state.lineValue);
            format.lineHeightAtLeast = static_cast<LineChoice>(state.lineChoice) == LineChoice::AtLeast;
            break;
    }
    if (format.lineHeightPt <= 0.0f) format.lineHeightAtLeast = false;
    const float by = PointsOfCentimetres(state.firstLineBy);
    format.firstLineIndentPt = state.firstLine == 0 ? 0.0f : (state.firstLine == 1 ? by : -by);
    return format;
}

} // namespace

void ShowParagraphDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed) {
    auto state = std::make_shared<ParagraphDialogState>();
    state->format = ReadParagraphDialogValues(edit);
    const LineChoice line = LineChoiceOf(state->format);
    state->lineChoice = static_cast<int>(line);
    state->lineValue = line == LineChoice::AtLeast || line == LineChoice::Exactly ? state->format.lineHeightPt
                     : (state->format.lineSpacing > 0.0f ? state->format.lineSpacing : 1.0f);
    state->firstLine = state->format.firstLineIndentPt > 0.0f ? 1 : (state->format.firstLineIndentPt < 0.0f ? 2 : 0);
    state->firstLineBy = std::fabs(state->format.firstLineIndentPt) / kPointsPerCm;
    const bool inCell = edit.GetEditor().GetCaret().InCell();

    DialogForm built = MakeDialogForm("ParagraphForm", Tr("Paragraph"), 9, 600);
    if (!built.dialog) return;
    auto& form = built.form;

    auto align = MakeChoice("ParagraphAlign", AlignmentNames(), AlignmentIndex(state->format.align));
    auto leftIndent = MakeCentimetres("ParagraphLeft", state->format.leftIndentPt, -10.0, 30.0);
    auto rightIndent = MakeCentimetres("ParagraphRight", state->format.rightIndentPt, -10.0, 30.0);
    auto firstRow = CreateFormCellRow("ParagraphFirstLine", 8.0f, kRowHeight);
    auto firstKind = MakeChoice("ParagraphFirstLineKind", {Tr("None"), Tr("First line"), Tr("Hanging")}, state->firstLine);
    firstKind->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    auto firstBy = MakeCentimetres("ParagraphFirstLineBy", static_cast<float>(state->firstLineBy * kPointsPerCm), 0.0, 20.0);
    firstBy->layoutItem.SetFlexShrink(0);
    firstRow->AddChild(firstKind);
    firstRow->AddChild(firstBy);
    auto before = MakePoints("ParagraphBefore", state->format.spaceBeforePt, 999.0, true);
    auto after = MakePoints("ParagraphAfter", state->format.spaceAfterPt, 999.0, true);
    auto lineRow = CreateFormCellRow("ParagraphLineRow", 8.0f, kRowHeight);
    auto lineKind = MakeChoice("ParagraphLineSpacing", {Tr("Single"), "1.15", Tr("1.5 lines"), Tr("Double"),
                                                         Tr("Multiple"), Tr("At least"), Tr("Exactly")},
                               state->lineChoice);
    lineKind->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    auto lineValue = CreateDecimalSpinner("ParagraphLineValue", 0, 0, kNumberWidth, kRowHeight, 0.5, 999.0,
                                          state->lineValue, 0.5, 2);
    lineValue->layoutItem.SetFlexShrink(0);
    lineRow->AddChild(lineKind);
    lineRow->AddChild(lineValue);

    auto tabList = MakeChoice("ParagraphTabs", {}, 0);
    auto tabRow = CreateFormCellRow("ParagraphTabEdit", 8.0f, kRowHeight);
    auto tabAt = CreateDecimalSpinner("ParagraphTabAt", 0, 0, kNumberWidth - 20.0f, kRowHeight, 0.0, 50.0,
                                      state->tabPosition, 0.25, 2);
    tabAt->SetSuffix(" " + Tr("cm"));
    tabAt->layoutItem.SetFlexShrink(0);
    auto tabKind = MakeChoice("ParagraphTabKind", {Tr("Left"), Tr("Centre"), Tr("Right"), Tr("Decimal")}, 0);
    tabKind->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    auto tabSet = CreateButton("ParagraphTabSet", 0, 0, 0, kRowHeight, Tr("Set"));
    tabSet->size.width = CSSLayout::Dimension::Auto();
    tabSet->layoutItem.SetFlexShrink(0);
    auto tabClear = CreateButton("ParagraphTabClear", 0, 0, 0, kRowHeight, Tr("Clear"));
    tabClear->size.width = CSSLayout::Dimension::Auto();
    tabClear->layoutItem.SetFlexShrink(0);
    tabRow->AddChild(tabAt);
    tabRow->AddChild(tabKind);
    tabRow->AddChild(tabSet);
    tabRow->AddChild(tabClear);

    AddRow(form, "ParagraphAlign", Tr("Alignment") + ":", align);
    FormRow rowLeft = AddRow(form, "ParagraphLeft", Tr("Indent before text") + ":", leftIndent);
    FormRow rowRight = AddRow(form, "ParagraphRight", Tr("Indent after text") + ":", rightIndent);
    FormRow rowFirst = AddRow(form, "ParagraphFirstLine", Tr("First line") + ":", firstRow);
    FormRow rowBefore = AddRow(form, "ParagraphBefore", Tr("Space before") + ":", before);
    FormRow rowAfter = AddRow(form, "ParagraphAfter", Tr("Space after") + ":", after);
    AddRow(form, "ParagraphLineRow", Tr("Line spacing") + ":", lineRow);
    FormRow rowTabs = AddRow(form, "ParagraphTabs", Tr("Tab stops") + ":", tabList);
    FormRow rowTabEdit = AddRow(form, "ParagraphTabEdit", Tr("Tab stop at") + ":", tabRow);
    built.dialog->AddDialogElement(form);
    HoldFormCaptionColumn(form);
    // In a table cell only the alignment and the line spacing are the cell's.
    for (const FormRow& row : {rowLeft, rowRight, rowFirst, rowBefore, rowAfter, rowTabs, rowTabEdit}) row.Show(!inCell);
    FitForm(built.dialog.get(), form.get());

    UltraCanvasDropdown* tabListRaw = tabList.get();
    UltraCanvasSpinner* lineValueRaw = lineValue.get();
    UltraCanvasSpinner* firstByRaw = firstBy.get();
    auto refreshTabs = [state, tabListRaw]() {
        tabListRaw->ClearItems();
        if (state->format.tabStops.empty()) tabListRaw->AddItem(Tr("None"));
        for (const RichTabStop& stop : state->format.tabStops) tabListRaw->AddItem(DescribeTabStop(stop));
        state->pickedTab = std::clamp(state->pickedTab, 0, std::max(0, static_cast<int>(state->format.tabStops.size()) - 1));
        tabListRaw->SetSelectedIndex(state->pickedTab, false);
        tabListRaw->RequestRedraw();          // the list shown changed under its selection
    };
    refreshTabs();
    auto refreshLine = [state, lineValueRaw, firstByRaw]() {
        const LineChoice choice = static_cast<LineChoice>(state->lineChoice);
        const bool value = choice == LineChoice::Multiple || choice == LineChoice::AtLeast || choice == LineChoice::Exactly;
        lineValueRaw->SetDisabled(!value);
        lineValueRaw->SetSuffix(choice == LineChoice::AtLeast || choice == LineChoice::Exactly ? " " + Tr("pt") : "");
        firstByRaw->SetDisabled(state->firstLine == 0);
    };
    refreshLine();

    align->onSelectionChanged = [state](int index, const DropdownItem&) { state->format.align = AlignmentOf(index); };
    leftIndent->onValueChanged = [state](double v) { state->format.leftIndentPt = PointsOfCentimetres(v); };
    rightIndent->onValueChanged = [state](double v) { state->format.rightIndentPt = PointsOfCentimetres(v); };
    firstKind->onSelectionChanged = [state, refreshLine](int index, const DropdownItem&) {
        state->firstLine = index;
        refreshLine();
    };
    firstBy->onValueChanged = [state](double v) { state->firstLineBy = v; };
    before->onValueChanged = [state](double v) { state->format.spaceBeforePt = v < 0.0 ? -1.0f : static_cast<float>(v); };
    after->onValueChanged = [state](double v) { state->format.spaceAfterPt = v < 0.0 ? -1.0f : static_cast<float>(v); };
    lineKind->onSelectionChanged = [state, refreshLine, lineValueRaw](int index, const DropdownItem&) {
        const LineChoice was = static_cast<LineChoice>(state->lineChoice);
        state->lineChoice = index;
        const LineChoice now = static_cast<LineChoice>(index);
        // A multiple and a height in points are different numbers.
        const bool wasPoints = was == LineChoice::AtLeast || was == LineChoice::Exactly;
        const bool nowPoints = now == LineChoice::AtLeast || now == LineChoice::Exactly;
        if (wasPoints != nowPoints) {
            state->lineValue = nowPoints ? 12.0 : 1.0;
            lineValueRaw->SetValue(state->lineValue);
        }
        refreshLine();
    };
    lineValue->onValueChanged = [state](double v) { state->lineValue = v; };
    tabList->onSelectionChanged = [state](int index, const DropdownItem&) { state->pickedTab = index; };
    tabAt->onValueChanged = [state](double v) { state->tabPosition = v; };
    tabKind->onSelectionChanged = [state](int index, const DropdownItem&) { state->tabKind = index; };
    tabSet->onClick = [state, refreshTabs]() {
        RichTabStop stop;
        stop.positionPt = PointsOfCentimetres(state->tabPosition);
        stop.kind = static_cast<RichTabKind>(std::clamp(state->tabKind, 0, 3));
        auto& stops = state->format.tabStops;
        // One stop per place: setting it again changes its kind.
        stops.erase(std::remove_if(stops.begin(), stops.end(),
                                   [&stop](const RichTabStop& s) { return Near(s.positionPt, stop.positionPt); }),
                    stops.end());
        stops.push_back(stop);
        std::sort(stops.begin(), stops.end(), [](const RichTabStop& a, const RichTabStop& b) { return a.positionPt < b.positionPt; });
        for (size_t i = 0; i < stops.size(); ++i) {
            if (Near(stops[i].positionPt, stop.positionPt)) state->pickedTab = static_cast<int>(i);
        }
        refreshTabs();
    };
    tabClear->onClick = [state, refreshTabs]() {
        auto& stops = state->format.tabStops;
        if (state->pickedTab < 0 || state->pickedTab >= static_cast<int>(stops.size())) return;
        stops.erase(stops.begin() + state->pickedTab);
        refreshTabs();
    };

    ShowForm(built, edit, [state](UltraCanvasRichTextEdit& target) {
        return ApplyParagraphDialogValues(target, FormatOfState(*state));
    }, std::move(onClosed));
}

// ---- Styles ----

namespace {

struct StylesDialogState {
    RichStylesDialogValues values;
    std::vector<std::string> ids;           // the list's styles, in its order
    std::vector<bool> paragraphStyle;       // ...and whether each is a paragraph style
    std::string newName;
};

} // namespace

void ShowStylesDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed) {
    auto state = std::make_shared<StylesDialogState>();
    state->values = ReadStylesDialogValues(edit);
    std::vector<std::string> names;
    int current = 0;
    for (const RichStyle& style : edit.GetStyles()) {
        state->ids.push_back(style.id);
        state->paragraphStyle.push_back(style.kind == RichStyle::Kind::Paragraph);
        std::string name = style.name.empty() ? style.id : style.name;
        if (style.kind == RichStyle::Kind::Character) name = TrF("{0} (characters)", name);
        if (style.id == state->values.styleId) current = static_cast<int>(names.size());
        names.push_back(name);
    }
    if (state->ids.empty()) {
        Explain(edit, Tr("This document has no named styles."), Tr("Styles"), onClosed);
        return;
    }
    state->values = StylesDialogValuesFor(edit, state->ids[static_cast<size_t>(current)]);

    DialogForm built = MakeDialogForm("StylesForm", Tr("Styles"), 11, 560);
    if (!built.dialog) return;
    auto& form = built.form;

    auto action = MakeChoice("StylesAction", {Tr("Apply the style to the selection"), Tr("Change the style"),
                                              Tr("New paragraph style from the selection")}, 0);
    auto style = MakeChoice("StylesStyle", names, current);
    auto name = MakeField("StylesName", "");
    // Every installed font, each in itself; "Default" leaves the style's
    // font unset.
    auto font = std::make_shared<UltraCanvasFontPicker>("StylesFont", 0, 0, 0, kRowHeight);
    font->SetDefaultFontEntry(Tr("Default"));
    font->SetListWidth(300);
    auto size = MakePoints("StylesSize", -1.0f, 400.0, true, 0.5);
    auto bold = MakeCheck("StylesBold", Tr("Bold"), false);
    auto italic = MakeCheck("StylesItalic", Tr("Italic"), false);
    auto underline = MakeCheck("StylesUnderline", Tr("Underline"), false);
    auto align = MakeChoice("StylesAlign", AlignmentNames(), 0);
    auto before = MakePoints("StylesBefore", -1.0f, 999.0, true);
    auto after = MakePoints("StylesAfter", -1.0f, 999.0, true);
    auto newName = MakeField("StylesNewName", "");

    AddRow(form, "StylesAction", Tr("Do") + ":", action);
    FormRow rowStyle = AddRow(form, "StylesStyle", Tr("Style") + ":", style);
    FormRow rowName = AddRow(form, "StylesName", Tr("Name") + ":", name);
    FormRow rowFont = AddRow(form, "StylesFont", Tr("Font") + ":", font);
    FormRow rowSize = AddRow(form, "StylesSize", Tr("Size") + ":", size);
    auto marks = CreateFormCellRow("StylesMarks", 16.0f, kRowHeight);
    for (const auto& box : {bold, italic, underline}) {
        box->layoutItem.SetFlexShrink(0);
        marks->AddChild(box);
    }
    FormRow rowMarks = AddRow(form, "StylesMarks", Tr("Text") + ":", marks);
    FormRow rowAlign = AddRow(form, "StylesAlign", Tr("Alignment") + ":", align);
    FormRow rowBefore = AddRow(form, "StylesBefore", Tr("Space before") + ":", before);
    FormRow rowAfter = AddRow(form, "StylesAfter", Tr("Space after") + ":", after);
    FormRow rowNew = AddRow(form, "StylesNewName", Tr("New style") + ":", newName);
    built.dialog->AddDialogElement(form);
    HoldFormCaptionColumn(form);

    // Raw: the dialog owns them all.
    UltraCanvasTextInput* nameRaw = name.get();
    UltraCanvasFontPicker* fontRaw = font.get();
    UltraCanvasSpinner* sizeRaw = size.get();
    UltraCanvasCheckbox* boldRaw = bold.get();
    UltraCanvasCheckbox* italicRaw = italic.get();
    UltraCanvasCheckbox* underlineRaw = underline.get();
    UltraCanvasDropdown* alignRaw = align.get();
    UltraCanvasSpinner* beforeRaw = before.get();
    UltraCanvasSpinner* afterRaw = after.get();
    UltraCanvasModalDialog* dialogRaw = built.dialog.get();
    UltraCanvasContainer* formRaw = form.get();
    std::weak_ptr<UltraCanvasRichTextEdit> weak = WeakOf(edit);

    // Which rows the action needs; a character style has no paragraph rows.
    auto refresh = [state, rowStyle, rowName, rowFont, rowSize, rowMarks, rowAlign, rowBefore, rowAfter, rowNew,
                    dialogRaw, formRaw]() {
        const auto doing = state->values.action;
        const bool change = doing == RichStylesDialogValues::Action::Change;
        bool paragraph = true;
        for (size_t i = 0; i < state->ids.size(); ++i) {
            if (state->ids[i] == state->values.styleId) paragraph = state->paragraphStyle[i];
        }
        rowStyle.Show(doing != RichStylesDialogValues::Action::NewFromSelection);
        for (const FormRow& row : {rowName, rowFont, rowSize, rowMarks}) row.Show(change);
        for (const FormRow& row : {rowAlign, rowBefore, rowAfter}) row.Show(change && paragraph);
        rowNew.Show(doing == RichStylesDialogValues::Action::NewFromSelection);
        FitForm(dialogRaw, formRaw);
    };
    // The style's properties into the fields. What it leaves unset shows
    // empty; only what is changed here is set on it.
    auto load = [state, nameRaw, fontRaw, sizeRaw, boldRaw, italicRaw, underlineRaw, alignRaw, beforeRaw, afterRaw,
                 weak](std::string id) {   // a copy: it may name state->values.styleId, reset below
        auto target = weak.lock();
        if (!target) return;
        const RichStylesDialogValues::Action doing = state->values.action;
        RichStylesDialogValues shown = StylesDialogValuesFor(*target, id);
        state->values = RichStylesDialogValues();
        state->values.action = doing;
        state->values.styleId = id;
        state->values.name = shown.name;
        nameRaw->SetText(shown.name);
        fontRaw->SetCurrentFont(shown.character.fontFamily ? *shown.character.fontFamily : std::string());
        sizeRaw->SetValue(shown.character.fontSizePt ? *shown.character.fontSizePt : -1.0f);
        boldRaw->SetChecked(shown.character.bold.value_or(false));
        italicRaw->SetChecked(shown.character.italic.value_or(false));
        underlineRaw->SetChecked(shown.character.underline.value_or(false));
        alignRaw->SetSelectedIndex(AlignmentIndex(shown.paragraph.align.value_or(RichTextAlign::Left)), false);
        beforeRaw->SetValue(shown.paragraph.spaceBeforePt ? *shown.paragraph.spaceBeforePt : -1.0);
        afterRaw->SetValue(shown.paragraph.spaceAfterPt ? *shown.paragraph.spaceAfterPt : -1.0);
    };
    load(state->values.styleId);
    refresh();

    action->onSelectionChanged = [state, refresh](int index, const DropdownItem&) {
        state->values.action = static_cast<RichStylesDialogValues::Action>(std::clamp(index, 0, 2));
        refresh();
    };
    style->onSelectionChanged = [state, load, refresh](int index, const DropdownItem&) {
        if (index < 0 || index >= static_cast<int>(state->ids.size())) return;
        load(state->ids[static_cast<size_t>(index)]);
        refresh();
    };
    name->onTextChanged = [state](const std::string& text) { state->values.name = text; };
    font->onFontChosen = [state, boldRaw, italicRaw](const FontPickerChoice& choice) {
        if (choice.family.empty()) {
            state->values.character.fontFamily.reset();
            return;
        }
        state->values.character.fontFamily = choice.family;
        // A style from its list says bold and italic too, as the toolbar's
        // does (the boxes follow, and set the style's values as they change).
        if (choice.fromStyles) {
            boldRaw->SetChecked(choice.weight >= 600);
            italicRaw->SetChecked(choice.slant != FontSlant::Normal);
            state->values.character.bold = choice.weight >= 600;
            state->values.character.italic = choice.slant != FontSlant::Normal;
        }
    };
    size->onValueChanged = [state](double v) {
        if (v > 0.0) state->values.character.fontSizePt = static_cast<float>(v);
        else state->values.character.fontSizePt.reset();
    };
    bold->onStateChanged = [state](CheckedState, CheckedState now) { state->values.character.bold = now == CheckedState::Checked; };
    italic->onStateChanged = [state](CheckedState, CheckedState now) { state->values.character.italic = now == CheckedState::Checked; };
    underline->onStateChanged = [state](CheckedState, CheckedState now) {
        state->values.character.underline = now == CheckedState::Checked;
    };
    align->onSelectionChanged = [state](int index, const DropdownItem&) { state->values.paragraph.align = AlignmentOf(index); };
    before->onValueChanged = [state](double v) {
        if (v >= 0.0) state->values.paragraph.spaceBeforePt = static_cast<float>(v);
        else state->values.paragraph.spaceBeforePt.reset();
    };
    after->onValueChanged = [state](double v) {
        if (v >= 0.0) state->values.paragraph.spaceAfterPt = static_cast<float>(v);
        else state->values.paragraph.spaceAfterPt.reset();
    };
    newName->onTextChanged = [state](const std::string& text) { state->newName = text; };

    ShowForm(built, edit, [state](UltraCanvasRichTextEdit& target) {
        RichStylesDialogValues values = state->values;
        if (values.action == RichStylesDialogValues::Action::NewFromSelection) values.name = state->newName;
        return ApplyStylesDialogValues(target, values);
    }, std::move(onClosed));
}

// ---- Table properties ----

namespace {

// The line colours offered, and their names in the same order.
const std::vector<std::string>& BorderColours() {
    static const std::vector<std::string> colours = {"#000000", "#808080", "#1F4E79", "#C00000", "#38761D"};
    return colours;
}
std::vector<std::string> BorderColourNames() {
    return {Tr("Black"), Tr("Grey"), Tr("Blue"), Tr("Red"), Tr("Green")};
}

struct TableDialogState {
    RichTableProperties values;
    int widthKind = 0;          // the whole column, a share of it, a width
    double share = 100.0;
    double widthCm = 10.0;
};

} // namespace

void ShowTablePropertiesDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed) {
    RichTableProperties now;
    if (!edit.GetTableProperties(now)) {
        Explain(edit, Tr("Put the cursor in a table first."), Tr("Table properties"), onClosed);
        return;
    }
    auto state = std::make_shared<TableDialogState>();
    state->values = now;
    state->widthKind = now.widthPercent > 0.0f ? 1 : (now.widthPt > 0.0f ? 2 : 0);
    if (now.widthPercent > 0.0f) state->share = now.widthPercent;
    if (now.widthPt > 0.0f) state->widthCm = now.widthPt / kPointsPerCm;

    DialogForm built = MakeDialogForm("TableForm", Tr("Table properties"), 10, 560);
    if (!built.dialog) return;
    auto& form = built.form;

    auto widthRow = CreateFormCellRow("TableWidthRow", 8.0f, kRowHeight);
    auto widthKind = MakeChoice("TableWidthKind", {Tr("The whole column"), Tr("A share of the column"), Tr("A width")},
                                state->widthKind);
    widthKind->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    auto share = CreateDecimalSpinner("TableShare", 0, 0, kNumberWidth, kRowHeight, 5.0, 100.0, state->share, 5.0, 0);
    share->SetSuffix(" %");
    share->layoutItem.SetFlexShrink(0);
    auto widthCm = CreateDecimalSpinner("TableWidthCm", 0, 0, kNumberWidth, kRowHeight, 1.0, 100.0, state->widthCm, 0.5, 2);
    widthCm->SetSuffix(" " + Tr("cm"));
    widthCm->layoutItem.SetFlexShrink(0);
    widthRow->AddChild(widthKind);
    widthRow->AddChild(share);
    widthRow->AddChild(widthCm);
    auto align = MakeChoice("TableAlign", {Tr("Left"), Tr("Centre"), Tr("Right")}, std::min(2, AlignmentIndex(now.align)));
    auto indent = MakeCentimetres("TableIndent", now.indentPt, -10.0, 30.0);
    auto header = MakeCheck("TableHeader", Tr("The first row is a header row"), now.headerRow);
    auto autoFit = MakeCheck("TableAutoFit", Tr("Widen columns to fit their contents"), now.autoFit);
    const int borderKind = now.borderWidthPt < 0.0f ? 0 : (now.borderWidthPt == 0.0f ? 1 : 2);
    auto borders = MakeChoice("TableBorders", {Tr("The editor's grid"), Tr("No lines"), Tr("Lines round every cell")},
                              borderKind);
    auto lineWidth = MakePoints("TableLineWidth", now.borderWidthPt > 0.0f ? now.borderWidthPt : 0.5f, 12.0, false, 0.25);
    int colourIndex = 0;
    for (size_t i = 0; i < BorderColours().size(); ++i) {
        if (now.borderColor == BorderColours()[i]) colourIndex = static_cast<int>(i);
    }
    auto colour = MakeChoice("TableLineColour", BorderColourNames(), colourIndex);
    auto padding = MakePoints("TablePadding", now.cellPaddingPt, 72.0, true, 0.5);
    auto vertical = MakeChoice("TableVertical", {Tr("Top"), Tr("Middle"), Tr("Bottom")}, static_cast<int>(now.verticalAlign));

    AddRow(form, "TableWidthRow", Tr("Width") + ":", widthRow);
    AddRow(form, "TableAlign", Tr("Alignment") + ":", align);
    FormRow rowIndent = AddRow(form, "TableIndent", Tr("Indent from the left") + ":", indent);
    AddFormWideRow(form, header);
    AddFormWideRow(form, autoFit);
    AddRow(form, "TableBorders", Tr("Borders") + ":", borders);
    FormRow rowLineWidth = AddRow(form, "TableLineWidth", Tr("Line width") + ":", lineWidth);
    FormRow rowColour = AddRow(form, "TableLineColour", Tr("Line colour") + ":", colour);
    AddRow(form, "TablePadding", Tr("Cell padding") + ":", padding);
    AddRow(form, "TableVertical", Tr("Text in the cells") + ":", vertical);
    built.dialog->AddDialogElement(form);
    HoldFormCaptionColumn(form);

    UltraCanvasSpinner* shareRaw = share.get();
    UltraCanvasSpinner* widthCmRaw = widthCm.get();
    UltraCanvasSpinner* lineWidthRaw = lineWidth.get();
    UltraCanvasDropdown* colourRaw = colour.get();
    UltraCanvasModalDialog* dialogRaw = built.dialog.get();
    UltraCanvasContainer* formRaw = form.get();
    auto bordersShown = std::make_shared<int>(borderKind);
    auto refresh = [state, shareRaw, widthCmRaw, rowIndent, rowLineWidth, rowColour, bordersShown, dialogRaw, formRaw]() {
        shareRaw->SetVisible(state->widthKind == 1);
        widthCmRaw->SetVisible(state->widthKind == 2);
        rowIndent.Show(state->values.align == RichTextAlign::Left || state->values.align == RichTextAlign::Default);
        rowLineWidth.Show(*bordersShown == 2);
        rowColour.Show(*bordersShown == 2);
        FitForm(dialogRaw, formRaw);
    };
    // The width the choice and its number give.
    auto setWidth = [state]() {
        state->values.widthPercent = state->widthKind == 1 ? static_cast<float>(state->share) : 0.0f;
        state->values.widthPt = state->widthKind == 2 ? PointsOfCentimetres(state->widthCm) : 0.0f;
    };
    // The lines the choice, width and colour give.
    auto setBorders = [state, bordersShown, lineWidthRaw, colourRaw]() {
        if (*bordersShown == 0) state->values.borderWidthPt = -1.0f;
        else if (*bordersShown == 1) state->values.borderWidthPt = 0.0f;
        else state->values.borderWidthPt = static_cast<float>(std::max(0.25, lineWidthRaw->GetValue()));
        const int c = colourRaw->GetSelectedIndex();
        state->values.borderColor = *bordersShown == 2 && c >= 0 && c < static_cast<int>(BorderColours().size())
                                        ? BorderColours()[static_cast<size_t>(c)] : std::string();
    };
    refresh();

    widthKind->onSelectionChanged = [state, refresh, setWidth](int index, const DropdownItem&) {
        state->widthKind = index;
        setWidth();
        refresh();
    };
    share->onValueChanged = [state, setWidth](double v) { state->share = v; setWidth(); };
    widthCm->onValueChanged = [state, setWidth](double v) { state->widthCm = v; setWidth(); };
    align->onSelectionChanged = [state, refresh](int index, const DropdownItem&) {
        state->values.align = AlignmentOf(index);
        refresh();
    };
    indent->onValueChanged = [state](double v) { state->values.indentPt = PointsOfCentimetres(v); };
    header->onStateChanged = [state](CheckedState, CheckedState now) { state->values.headerRow = now == CheckedState::Checked; };
    autoFit->onStateChanged = [state](CheckedState, CheckedState now) { state->values.autoFit = now == CheckedState::Checked; };
    borders->onSelectionChanged = [bordersShown, setBorders, refresh](int index, const DropdownItem&) {
        *bordersShown = index;
        setBorders();
        refresh();
    };
    lineWidth->onValueChanged = [setBorders](double) { setBorders(); };
    colour->onSelectionChanged = [setBorders](int, const DropdownItem&) { setBorders(); };
    padding->onValueChanged = [state](double v) { state->values.cellPaddingPt = v < 0.0 ? -1.0f : static_cast<float>(v); };
    vertical->onSelectionChanged = [state](int index, const DropdownItem&) {
        state->values.verticalAlign = static_cast<RichVerticalAlign>(std::clamp(index, 0, 2));
    };

    ShowForm(built, edit, [state](UltraCanvasRichTextEdit& target) {
        return ApplyTableDialogValues(target, state->values);
    }, std::move(onClosed));
}

// ---- Picture ----

namespace {

struct PictureDialogState {
    RichPictureDialogValues values;
    double aspect = 1.0;            // width / height, while the proportions are kept
    bool keepProportions = true;
};

} // namespace

void ShowPictureDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed) {
    if (!edit.HasSelectedImage()) {
        Explain(edit, Tr("Click a picture to select it first."), Tr("Picture"), onClosed);
        return;
    }
    auto state = std::make_shared<PictureDialogState>();
    state->values = ReadPictureDialogValues(edit);
    if (state->values.widthPt > 0.0f && state->values.heightPt > 0.0f) {
        state->aspect = state->values.widthPt / state->values.heightPt;
    }
    constexpr float kAltHeight = 76.0f;
    DialogForm built = MakeDialogForm("PictureForm", Tr("Picture"), 6, 560, kAltHeight - kRowHeight);
    if (!built.dialog) return;
    auto& form = built.form;

    auto alt = std::make_shared<UltraCanvasTextArea>("PictureAlt", 0, 0, 0, kAltHeight);
    alt->SetText(state->values.altText, false);
    auto wrap = MakeChoice("PictureWrap", {Tr("In the line of text"), Tr("Text flows beside it"),
                                           Tr("Text above and below it"), Tr("Behind the text"), Tr("In front of the text")},
                           static_cast<int>(state->values.wrap));
    auto position = MakeChoice("PicturePosition", {Tr("Left"), Tr("Centre"), Tr("Right")},
                               std::min(2, AlignmentIndex(state->values.align)));
    auto width = MakeCentimetres("PictureWidth", state->values.widthPt, 0.1, 100.0);
    auto height = MakeCentimetres("PictureHeight", state->values.heightPt, 0.1, 100.0);
    auto keep = MakeCheck("PictureKeep", Tr("Keep the proportions"), true);

    AddTallRow(form, "PictureAlt", Tr("Alt text") + ":", alt);
    AddRow(form, "PictureWrap", Tr("Wrapping") + ":", wrap);
    FormRow rowPosition = AddRow(form, "PicturePosition", Tr("Position") + ":", position);
    AddRow(form, "PictureWidth", Tr("Width") + ":", width);
    AddRow(form, "PictureHeight", Tr("Height") + ":", height);
    AddFormWideRow(form, keep);
    built.dialog->AddDialogElement(form);
    HoldFormCaptionColumn(form);

    UltraCanvasSpinner* widthRaw = width.get();
    UltraCanvasSpinner* heightRaw = height.get();
    UltraCanvasModalDialog* dialogRaw = built.dialog.get();
    UltraCanvasContainer* formRaw = form.get();
    auto refresh = [state, rowPosition, dialogRaw, formRaw]() {
        rowPosition.Show(state->values.wrap != RichTextRun::ImageWrap::Inline);
        FitForm(dialogRaw, formRaw, kAltHeight - kRowHeight);
    };
    refresh();

    alt->SetOnTextChanged([state](const std::string& text) { state->values.altText = text; });
    wrap->onSelectionChanged = [state, refresh](int index, const DropdownItem&) {
        state->values.wrap = static_cast<RichTextRun::ImageWrap>(std::clamp(index, 0, 4));
        refresh();
    };
    position->onSelectionChanged = [state](int index, const DropdownItem&) { state->values.align = AlignmentOf(index); };
    width->onValueChanged = [state, heightRaw](double v) {
        const float pt = PointsOfCentimetres(v);
        if (Near(pt, state->values.widthPt)) return;
        state->values.widthPt = pt;
        if (state->keepProportions && state->aspect > 0.0) {
            state->values.heightPt = static_cast<float>(pt / state->aspect);
            heightRaw->SetValue(state->values.heightPt / kPointsPerCm);
        }
    };
    height->onValueChanged = [state, widthRaw](double v) {
        const float pt = PointsOfCentimetres(v);
        if (Near(pt, state->values.heightPt)) return;
        state->values.heightPt = pt;
        if (state->keepProportions && state->aspect > 0.0) {
            state->values.widthPt = static_cast<float>(pt * state->aspect);
            widthRaw->SetValue(state->values.widthPt / kPointsPerCm);
        }
    };
    keep->onStateChanged = [state](CheckedState, CheckedState now) {
        state->keepProportions = now == CheckedState::Checked;
        if (state->keepProportions && state->values.heightPt > 0.0f) {
            state->aspect = state->values.widthPt / state->values.heightPt;
        }
    };

    ShowForm(built, edit, [state](UltraCanvasRichTextEdit& target) {
        return ApplyPictureDialogValues(target, state->values);
    }, std::move(onClosed));
}

// ---- Table of contents ----

void ShowContentsDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed) {
    auto state = std::make_shared<RichContentsDialogValues>(ReadContentsDialogValues(edit));
    const bool exists = state->replaceExisting;
    DialogForm built = MakeDialogForm("ContentsForm", Tr("Table of contents"), exists ? 2 : 1, 480);
    if (!built.dialog) return;
    auto& form = built.form;
    auto levels = CreateIntSpinner("ContentsLevels", 0, 0, kNumberWidth, kRowHeight, 1, 9, state->maxLevel, 1);
    levels->layoutItem.SetJustifySelf(CSSLayout::JustifySelf::Start);
    AddRow(form, "ContentsLevels", Tr("Heading levels") + ":", levels);
    levels->onValueChanged = [state](double v) { state->maxLevel = static_cast<int>(std::lround(v)); };
    if (exists) {
        auto replace = MakeCheck("ContentsReplace", Tr("Rebuild the existing contents"), true);
        replace->onStateChanged = [state](CheckedState, CheckedState now) { state->replaceExisting = now == CheckedState::Checked; };
        AddFormWideRow(form, replace);
    }
    built.dialog->AddDialogElement(form);
    HoldFormCaptionColumn(form);
    ShowForm(built, edit, [state](UltraCanvasRichTextEdit& target) {
        return ApplyContentsDialogValues(target, *state);
    }, std::move(onClosed));
}

// ---- Caption ----

void ShowCaptionDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed) {
    auto state = std::make_shared<RichCaptionDialogValues>(ReadCaptionDialogValues(edit));
    // The label is what the numbering counts and what a cross-reference
    // shows, so it is the document's word: it is offered in the reader's
    // language and kept as chosen.
    const std::vector<std::string> labels = {Tr("Figure"), Tr("Table"), Tr("Equation")};
    state->label = state->label == "Table" ? labels[1] : labels[0];
    DialogForm built = MakeDialogForm("CaptionForm", Tr("Caption"), 2, 520);
    if (!built.dialog) return;
    auto& form = built.form;
    auto label = MakeChoice("CaptionLabel", labels, state->label == labels[1] ? 1 : 0);
    auto text = MakeField("CaptionText", "");
    AddRow(form, "CaptionLabel", Tr("Label") + ":", label);
    AddRow(form, "CaptionText", Tr("Caption") + ":", text);
    built.dialog->AddDialogElement(form);
    HoldFormCaptionColumn(form);
    label->onSelectionChanged = [state](int, const DropdownItem& item) { state->label = item.text; };
    text->onTextChanged = [state](const std::string& now) { state->text = now; };
    ShowForm(built, edit, [state](UltraCanvasRichTextEdit& target) {
        return ApplyCaptionDialogValues(target, *state);
    }, std::move(onClosed));
}

// ---- Cross-reference ----

void ShowCrossReferenceDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed) {
    const std::vector<RichCrossReferenceTarget> targets = RichCrossReferenceTargets(*edit.GetDocument());
    if (targets.empty()) {
        Explain(edit, Tr("There is nothing to refer to yet: add a bookmark or a caption first."),
                Tr("Cross-reference"), onClosed);
        return;
    }
    auto state = std::make_shared<RichCrossReferenceDialogValues>(ReadCrossReferenceDialogValues(edit));
    std::vector<std::string> shown;
    for (const RichCrossReferenceTarget& target : targets) shown.push_back(target.shown);
    DialogForm built = MakeDialogForm("CrossReferenceForm", Tr("Cross-reference"), 2, 560);
    if (!built.dialog) return;
    auto& form = built.form;
    auto target = MakeChoice("CrossReferenceTarget", shown, 0);
    auto kind = MakeChoice("CrossReferenceKind", {Tr("Its text"), Tr("Its page number")}, 0);
    AddRow(form, "CrossReferenceTarget", Tr("Refer to") + ":", target);
    AddRow(form, "CrossReferenceKind", Tr("Insert") + ":", kind);
    built.dialog->AddDialogElement(form);
    HoldFormCaptionColumn(form);
    target->onSelectionChanged = [state, targets](int index, const DropdownItem&) {
        if (index >= 0 && index < static_cast<int>(targets.size())) state->bookmark = targets[static_cast<size_t>(index)].bookmark;
    };
    kind->onSelectionChanged = [state](int index, const DropdownItem&) { state->pageNumber = index == 1; };
    ShowForm(built, edit, [state](UltraCanvasRichTextEdit& editTarget) {
        return ApplyCrossReferenceDialogValues(editTarget, *state);
    }, std::move(onClosed));
}

// ---- Footnote ----

void ShowNoteDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed) {
    auto state = std::make_shared<RichNoteDialogValues>(ReadNoteDialogValues(edit));
    DialogForm built = MakeDialogForm("NoteForm", Tr("Footnote"), 2, 520);
    if (!built.dialog) return;
    auto& form = built.form;
    auto kind = MakeChoice("NoteKind", {Tr("Footnote, at the foot of the page"), Tr("Endnote, at the end")}, 0);
    auto text = MakeField("NoteText", "");
    text->SetPlaceholder(Tr("Empty: type it in the note"));
    AddRow(form, "NoteKind", Tr("Kind") + ":", kind);
    AddRow(form, "NoteText", Tr("Note") + ":", text);
    built.dialog->AddDialogElement(form);
    HoldFormCaptionColumn(form);
    kind->onSelectionChanged = [state](int index, const DropdownItem&) {
        state->kind = index == 1 ? RichNote::Kind::Endnote : RichNote::Kind::Footnote;
    };
    text->onTextChanged = [state](const std::string& now) { state->text = now; };
    ShowForm(built, edit, [state](UltraCanvasRichTextEdit& target) {
        return ApplyNoteDialogValues(target, *state);
    }, std::move(onClosed));
}

// ---- Comments ----

void ShowCommentDialog(UltraCanvasRichTextEdit& edit, int index, RichDialogClosed onClosed) {
    auto state = std::make_shared<RichCommentDialogValues>(ReadCommentDialogValues(edit, index));
    const bool existing = state->index >= 0;
    constexpr float kTextHeight = 96.0f;
    DialogForm built = MakeDialogForm("CommentForm", Tr("Comment"),
                                      existing ? 2 : 1, 520, kTextHeight - kRowHeight);
    if (!built.dialog) return;
    auto& form = built.form;
    auto text = std::make_shared<UltraCanvasTextArea>("CommentText", 0, 0, 0, kTextHeight);
    text->SetText(state->text, false);
    AddTallRow(form, "CommentText", Tr("Comment") + ":", text);
    text->SetOnTextChanged([state](const std::string& now) { state->text = now; });
    if (existing) {
        auto resolved = MakeCheck("CommentResolved", Tr("Resolved"), state->resolved);
        resolved->onStateChanged = [state](CheckedState, CheckedState now) { state->resolved = now == CheckedState::Checked; };
        AddFormWideRow(form, resolved);
    }
    built.dialog->AddDialogElement(form);
    HoldFormCaptionColumn(form);
    ShowForm(built, edit, [state](UltraCanvasRichTextEdit& target) {
        return ApplyCommentDialogValues(target, *state);
    }, std::move(onClosed));
}

void ShowCommentsDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed) {
    const std::vector<int> active = edit.GetDocument()->ActiveComments();
    if (active.empty()) {
        Explain(edit, Tr("This document has no comments."), Tr("Comments"), onClosed);
        return;
    }
    auto state = std::make_shared<RichCommentsDialogValues>();
    // The comment at the caret is picked to start with.
    const std::vector<int> here = edit.GetCommentsAtCaret();
    int picked = 0;
    std::vector<std::string> lines;
    for (size_t i = 0; i < active.size(); ++i) {
        lines.push_back(DescribeRichComment(edit.GetDocument()->comments[static_cast<size_t>(active[i])]));
        if (!here.empty() && active[i] == here.back()) picked = static_cast<int>(i);
    }
    state->index = active[static_cast<size_t>(picked)];
    constexpr float kTextHeight = 96.0f;
    DialogForm built = MakeDialogForm("CommentsForm", Tr("Comments"),
                                      2, 600, kTextHeight - kRowHeight, DialogButtons::NoButtons);
    if (!built.dialog) return;
    auto& form = built.form;
    auto list = MakeChoice("CommentsList", lines, picked);
    auto detail = std::make_shared<UltraCanvasTextArea>("CommentsText", 0, 0, 0, kTextHeight);
    detail->SetReadOnly(true);
    AddRow(form, "CommentsList", Tr("Comment") + ":", list);
    AddTallRow(form, "CommentsText", Tr("Text") + ":", detail);
    built.dialog->AddDialogElement(form);
    HoldFormCaptionColumn(form);

    UltraCanvasTextArea* detailRaw = detail.get();
    std::weak_ptr<UltraCanvasRichTextEdit> weak = WeakOf(edit);
    auto show = [state, detailRaw, weak]() {
        auto target = weak.lock();
        if (!target || state->index < 0 || state->index >= static_cast<int>(target->GetDocument()->comments.size())) return;
        const RichComment& comment = target->GetDocument()->comments[static_cast<size_t>(state->index)];
        std::string text = comment.text;
        // Who wrote it and when, under it: "Ann Lee  2026-11-25".
        std::string signature = comment.author;
        if (!comment.date.empty()) signature += (signature.empty() ? "" : "  ") + comment.date.substr(0, 10);
        if (!signature.empty()) text += "\n\n" + signature;
        detailRaw->SetText(text, false);
    };
    show();
    list->onSelectionChanged = [state, active, show](int index, const DropdownItem&) {
        if (index >= 0 && index < static_cast<int>(active.size())) state->index = active[static_cast<size_t>(index)];
        show();
    };

    built.dialog->AddCustomButton(Tr("Go to"), DialogResult::OK, DialogButtonRole::Default);
    built.dialog->AddCustomButton(Tr("Edit"), DialogResult::Apply);
    built.dialog->AddCustomButton(Tr("Resolve or reopen"), DialogResult::Yes);
    built.dialog->AddCustomButton(Tr("Delete"), DialogResult::No, DialogButtonRole::Destructive);
    built.dialog->AddCustomButton(Tr("Close"), DialogResult::Cancel, DialogButtonRole::Cancel);
    UltraCanvasDialogManager::ShowDialog(built.dialog, [weak, state, onClosed](DialogResult result) {
        auto target = weak.lock();
        bool applied = false;
        if (target) {
            if (result == DialogResult::Apply) {
                // Edit: the comment's own dialog, which reports when it closes.
                target->GoToComment(state->index);
                ShowCommentDialog(*target, state->index, onClosed);
                return;
            }
            RichCommentsDialogValues values = *state;
            if (result == DialogResult::OK) values.action = RichCommentsDialogValues::Action::GoTo;
            else if (result == DialogResult::Yes) values.action = RichCommentsDialogValues::Action::ToggleResolved;
            else if (result == DialogResult::No) values.action = RichCommentsDialogValues::Action::Delete;
            else values.index = -1;
            applied = ApplyCommentsDialogValues(*target, values) && values.action != RichCommentsDialogValues::Action::GoTo;
            target->SetFocus(true);
        }
        if (onClosed) onClosed(applied);
    }, edit.GetWindow());
}

} // namespace UltraCanvas
