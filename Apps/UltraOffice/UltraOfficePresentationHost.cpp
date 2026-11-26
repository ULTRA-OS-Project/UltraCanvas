// Apps/UltraOffice/UltraOfficePresentationHost.cpp
// A presentation tab: the framework's slide editor (UltraCanvasSlideEdit) over
// the deck model both readers and both writers share - the slide strip, the
// slide with its shapes to select, move, resize, turn, order, duplicate and
// delete, new slides from the deck's layouts, text boxes, drawn shapes and
// pictures, and every change undone and redone. A shape's text is edited in
// place, as plain lines, in a text area laid over the shape at the slide's
// zoom (the rich in-place editor comes with Phase 4). The show runs full
// screen in a window of its own; PDF and print draw the same slides. At the
// toolbar's right end the view group: normal, the slide sorter, the notes,
// the fitted slide and the zoom.
//
// Version: 0.8.0 - the view group (normal, slide sorter, notes, fit the slide, zoom out, the zoom level,
//                  zoom in) replaces the View tab; a new deck's slide size and the show's monitors from
//                  the settings
// Version: 0.7.0 - an Arrange tab with group / ungroup, align and distribute; a Format tab with crop,
//                  reset crop and the chart's data (a small spreadsheet in a dialog, also on a
//                  double-click); a View tab with the slide sorter and the notes pane under the slide
//                  (rich text, a splitter, one step on leaving it); a menu on a right-click
// Version: 0.6.0 - a deck is read through UltraCanvasFileLoader::LoadPresentation: PowerPoint 97-2003
//                  (.ppt, .pps, .pot) opens too
// Version: 0.5.0 - Cut, Copy and Paste on the Home bar: cells, charts and text from the other kinds
// Version: 0.4.0 - the show is UltraCanvasSlideShowPlayer (transitions, the presenter view); a
//                  transition picker
// Version: 0.3.0 - formatted text in place (the rich editor), a Text tab, tables (insert, cells, rows,
//                  columns)
// Version: 0.2.0 - editing: UltraCanvasSlideEdit, text in place, the show, PDF and print
// Version: 0.1.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraOfficeHost.h"
#include "UltraOfficeSettings.h"
#include "UltraOfficeToolbar.h"

#include "IODeviceManager/UltraCanvasIODevicePrintDialog.h"   // PrintDocumentWithDialog
#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPresentationOdp.h"
#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasPresentationPrint.h"
#include "UltraCanvasSlideEdit.h"
#include "UltraCanvasSlideShowPlayer.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasTextUtils.h"
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cmath>

namespace UltraOffice {

using namespace UltraCanvas;

namespace {

constexpr int64_t kInch = 914400;
constexpr int64_t kDefaultInsetX = 91440, kDefaultInsetY = 45720;   // PowerPoint's 0.1" and 0.05"

// The rich text editor laid over a shape while its text is edited: Escape
// drops the edit, Ctrl+Enter keeps it (a click elsewhere on the slide keeps
// it too).
class InPlaceRichText final : public UltraCanvasRichTextEdit {
public:
    using UltraCanvasRichTextEdit::UltraCanvasRichTextEdit;
    std::function<void(bool keep)> onFinish;
    bool OnEvent(const UCEvent& event) override {
        if (event.type == UCEventType::KeyDown && onFinish) {
            if (event.virtualKey == UCKeys::Escape) { onFinish(false); return true; }
            if (event.virtualKey == UCKeys::Return && event.ctrl) { onFinish(true); return true; }
        }
        return UltraCanvasRichTextEdit::OnEvent(event);
    }
};

// The speaker notes under the slide: what is typed goes into the slide
// (one step) when the notes are left.
class NotesRichText final : public UltraCanvasRichTextEdit {
public:
    using UltraCanvasRichTextEdit::UltraCanvasRichTextEdit;
    std::function<void()> onLeave;
    bool OnEvent(const UCEvent& event) override {
        if (event.type == UCEventType::FocusLost && onLeave) onLeave();
        return UltraCanvasRichTextEdit::OnEvent(event);
    }
};

// The shapes Insert > Shape offers: their preset outlines, and (in the same
// order) their names, each a literal Tr so the catalogue finds it.
const char* const kGeometries[] = {"rect", "roundRect", "ellipse", "triangle", "rightArrow", "star5"};
std::vector<std::string> GeometryNames() {
    return {Tr("Rectangle"), Tr("Rounded rectangle"), Tr("Ellipse"), Tr("Triangle"), Tr("Arrow"), Tr("Star")};
}

class PresentationHost final : public OfficeHost {
public:
    explicit PresentationHost(UltraCanvasWindowBase* window) : window_(window) {
        // The editor fills the tab; the text area for a shape's text lies
        // over it, placed on the shape while it is open.
        content_ = CreateContainer("office-presentation-pane");
        content_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        content_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        // The slide over the notes, a splitter between them.
        split_ = CreateVerticalSplitPane("office-presentation-split", 0, 0, 800, 600);
        split_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        content_->AddChild(split_);
        auto slidePane = split_->AddPane(1.0);
        slidePane->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        edit_ = CreateSlideEdit("office-presentation", 0, 0, 800, 600);
        edit_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        slidePane->AddChild(edit_);
        notes_ = std::make_shared<NotesRichText>("office-presentation-notes", 0.0f, 0.0f, 800.0f, 120.0f);
        notes_->SetPageView(false);
        notes_->SetShowFontNotice(false);
        notes_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        ShowNotes(true);
        text_ = std::make_shared<InPlaceRichText>("office-presentation-text", 0.0f, 0.0f, 10.0f, 10.0f);
        text_->SetPageView(false);
        text_->SetShowFontNotice(false);
        text_->SetZoomRange(0.1f, 10.0f);
        text_->SetVisible(false);
        content_->AddChild(text_);

        UltraCanvasSlideEdit* edit = edit_.get();
        edit_->onChange = [this]() {
            // Undo and redo bring the notes back: shown, unless being typed.
            if (!committingNotes_ && notes_ && !notes_->IsModified() && Editor().GetCurrentSlide() == notesSlide_) {
                const PptxSlide* slide = Editor().CurrentSlide();
                if (slide && UCSlideDeckEditor::SlideNotesText(*slide) != NotesText(notesBefore_)) LoadNotes();
            }
            Sync();
            if (onChanged) onChanged();
        };
        edit_->onSelectionChange = [this]() {
            CloseText(true);
            if (Editor().GetCurrentSlide() != notesSlide_) {   // another slide: its notes
                CommitNotes();
                LoadNotes();
            }
            Sync();
            if (onChanged) onChanged();
        };
        edit_->onModeChange = [this]() { SyncView(); Sync(); if (onChanged) onChanged(); };
        edit_->onEditText = [this](int shape) { OpenText(shape); };
        edit_->onEditTableCell = [this](int shape, int row, int col) { OpenText(shape, row, col); };
        edit_->onEditChart = [this](int shape) { EditChartData(shape); };
        edit_->onContextMenu = [this, edit](bool inStrip, int slide, int shape, int windowX, int windowY) {
            (void)edit;
            ShowContextMenu(inStrip, slide, shape, windowX, windowY);
        };
        notes_->onLeave = [this]() { CommitNotes(); };
        text_->onFinish = [this](bool keep) { CloseText(keep); };
        text_->onSelectionChanged = [this]() { SyncText(); };
        BuildHome();
        BuildInsert();
        BuildText();
        BuildTable();
        BuildArrange();
        BuildFormat();
        BuildShow();
        BuildViewGroup();
    }

    ~PresentationHost() override {
        if (show_) {
            show_->onEnd = nullptr;   // nothing may come back to this host
            if (show_->player) show_->player->Stop();
        }
    }

    OfficeKind GetKind() const override { return OfficeKind::Presentation; }
    std::shared_ptr<UltraCanvasUIElement> GetContent() override { return content_; }
    const std::vector<CommandTab>& GetCommandTabs() override { return tabs_; }
    std::shared_ptr<UltraCanvasToolbar> GetViewToolbar() override { return viewBar_; }
    void AttachSettings(UltraCanvasAppSettings* settings) override { settings_ = settings; }

    // The slide's zoom: 100 % is the slide fitting the space.
    void ZoomIn() override { SetSlideZoom(edit_->GetZoom() * 1.25); }
    void ZoomOut() override { SetSlideZoom(edit_->GetZoom() / 1.25); }
    void ZoomToActualSize() override { SetSlideZoom(1.0); }

    void NewBlank() override {
        auto deck = std::make_shared<PptxPresentation>();
        const bool standard = settings_ && OfficeSettings(*settings_).GetSlideSize() == SlideSizeChoice::Standard;
        deck->slideWidth = standard ? 9144000 : 12192000;   // 10 x 7.5 inches (4:3), or 13.33 x 7.5 (16:9)
        deck->slideHeight = 6858000;
        // A title slide: the title and, under it, the subtitle.
        PptxSlide slide = PptxSlideFromLayout(*deck, -1);
        if (slide.shapes.size() == 2) {
            PptxShape& title = slide.shapes[0];
            title.y = deck->slideHeight / 4;
            title.height = deck->slideHeight / 4;
            PptxShape& subtitle = slide.shapes[1];
            subtitle.kind = PptxShapeKind::Subtitle;
            subtitle.placeholder = "subTitle";
            subtitle.y = deck->slideHeight / 2 + kInch / 8;
            subtitle.height = deck->slideHeight / 5;
            subtitle.paragraphs[0].bullet = false;
            subtitle.paragraphs[0].align = PptxAlign::Center;
            subtitle.paragraphs[0].runs[0].sizePt = 24;
            title.paragraphs[0].align = PptxAlign::Center;
        }
        deck->slides.push_back(slide);
        deck->slides[0].number = 1;
        edit_->SetDeck(deck);
        LoadNotes();
        Sync();
    }

    bool Load(const std::string& path, const std::string& password, std::string& error) override {
        auto deck = std::make_shared<PptxPresentation>();
        // With the pictures: they are drawn, and written back on save. The
        // reader is the one the file's content asks for (.pptx, .odp, .ppt).
        if (!UltraCanvasFileLoader::LoadPresentation(path, *deck, error, password, true)) return false;
        edit_->SetDeck(deck);
        LoadNotes();
        Sync();
        return true;
    }

    bool Write(const std::string& path, const std::string& password, std::string& error) override {
        CloseText(true);
        CommitNotes();
        return UltraCanvasFileLoader::SavePresentation(*edit_->Editor().GetDeck(), path, error, password);
    }

    void AddSaveTypes(FileDialogOptions& opts) const override { UltraCanvasFileLoader::AddPresentationSaveTypes(opts); }
    std::string DefaultExtension(SaveFormat format) const override {
        return format == SaveFormat::OpenDocument ? "odp" : "pptx";
    }

    bool CanExportPdf() const override { return true; }
    bool ExportPdf(const std::string& path, std::string& error) override {
        PresentationPrintPages pages(*edit_->Editor().GetDeck());
        return pages.ExportPdf(path, error);
    }
    bool Print(const std::string& name, UltraCanvasWindowBase* parent, std::string& error) override {
        auto pages = std::make_shared<PresentationPrintPages>(*edit_->Editor().GetDeck());
        std::vector<uint8_t> pdf;
        if (!pages->ExportPdf(pdf, error)) return false;
        const IODeviceResult printed = PrintDocumentWithDialog(name, pdf, "application/pdf", parent, pages);
        if (!printed.success && printed.code != IODeviceResultCode::Cancelled) {
            error = printed.message;
            return false;
        }
        return true;
    }

    bool IsModified() const override { return edit_->Editor().IsModified(); }
    void SetModified(bool modified) override { edit_->Editor().SetModified(modified); }
    void Undo() override { CloseText(true); CommitNotes(); edit_->Editor().Undo(); }
    void Redo() override { CloseText(true); CommitNotes(); edit_->Editor().Redo(); }
    void Focus() override { edit_->SetFocus(true); }

    std::string GetStatusText() const override {
        const auto& editor = edit_->Editor();
        if (editor.GetSlideCount() == 0) return Tr("No slides");
        std::string status = TrF("Slide {0} of {1}", editor.GetCurrentSlide() + 1, editor.GetSlideCount());
        if (const PptxSlide* slide = editor.CurrentSlide(); slide && slide->hidden) status += "    " + Tr("Hidden in the show");
        if (!editor.GetSelection().empty())
            status += "    " + TrN("{n} shape selected", "{n} shapes selected", static_cast<int>(editor.GetSelection().size()));
        return status;
    }

private:
    UltraCanvasWindowBase* window_;
    std::shared_ptr<UltraCanvasContainer> content_;
    std::shared_ptr<UltraCanvasSplitPane> split_;
    std::shared_ptr<UltraCanvasSlideEdit> edit_;
    std::shared_ptr<NotesRichText> notes_;
    std::shared_ptr<UltraCanvasContainer> notesPane_;   // the split pane's lower pane, while shown
    bool notesShown_ = false;
    bool notesWanted_ = true;      // the View tab's choice (the sorter hides them meanwhile)
    int notesHeight_ = 130;
    int notesSlide_ = -1;          // the slide whose notes are shown
    std::vector<PptxParagraph> notesBefore_;
    bool committingNotes_ = false;
    std::shared_ptr<UltraCanvasMenu> menu_;
    UltraCanvasButton* normalView_ = nullptr;
    UltraCanvasButton* sorterView_ = nullptr;
    UltraCanvasButton* notesTool_ = nullptr;
    UltraCanvasButton* crop_ = nullptr;
    UltraCanvasAppSettings* settings_ = nullptr;   // the window's
    std::shared_ptr<UltraCanvasToolbar> viewBar_;
    UltraCanvasDropdown* zoomBox_ = nullptr;
    std::shared_ptr<InPlaceRichText> text_;
    int textShape_ = -1;      // the shape whose text is open; -1: none
    int textRow_ = -1, textCol_ = -1;   // a table's cell; -1: the shape's own text
    int textSlide_ = -1;
    std::vector<PptxParagraph> textBefore_;   // the paragraphs as they were, for what the editor does not hold
    std::vector<CommandTab> tabs_;
    UltraCanvasButton* hidden_ = nullptr;
    UltraCanvasButton* bold_ = nullptr;
    UltraCanvasButton* italic_ = nullptr;
    UltraCanvasButton* underline_ = nullptr;
    UltraCanvasButton* strike_ = nullptr;
    int lastCellRow_ = -1, lastCellCol_ = -1;   // the table cell edited last
    std::shared_ptr<UltraCanvasDropdown> layouts_;
    std::shared_ptr<UltraCanvasDropdown> transition_;
    bool syncing_ = false;   // the tools are being set from the deck, not by a person
    std::shared_ptr<SlideShowWindows> show_;

    UCSlideDeckEditor& Editor() { return edit_->Editor(); }

    std::function<void()> Act(std::function<void(UCSlideDeckEditor&)> action) {
        return [this, action]() {
            CloseText(true);
            CommitNotes();
            action(Editor());
            edit_->SetFocus(true);
        };
    }

    // ===== TEXT IN PLACE =====

    bool TextOpen() const { return textShape_ >= 0; }

    // The shape's text - or a table cell's (`row`, `col`) - in the rich
    // editor over it, at the slide's zoom.
    void OpenText(int shape, int row = -1, int col = -1) {
        CloseText(true);
        const PptxSlide* slide = Editor().CurrentSlide();
        if (!slide || shape < 0 || shape >= static_cast<int>(slide->shapes.size())) return;
        const PptxShape& target = slide->shapes[static_cast<size_t>(shape)];
        const bool cell = row >= 0 && col >= 0 && row < static_cast<int>(target.rows.size()) &&
                          col < static_cast<int>(target.rows[static_cast<size_t>(row)].size());
        // Where the text goes: the shape's box less its insets, or the cell's.
        int64_t bx = target.x, by = target.y, bw = target.width, bh = target.height;
        int64_t insetL = target.insetLeft >= 0 ? target.insetLeft : kDefaultInsetX;
        int64_t insetR = target.insetRight >= 0 ? target.insetRight : kDefaultInsetX;
        int64_t insetT = target.insetTop >= 0 ? target.insetTop : kDefaultInsetY;
        int64_t insetB = target.insetBottom >= 0 ? target.insetBottom : kDefaultInsetY;
        if (cell) {
            const SlideBox box = UCSlideDeckEditor::TableCellBox(target, row, col);
            bx = box.x; by = box.y; bw = box.width; bh = box.height;
            const PptxCellStyle* style = row < static_cast<int>(target.cellStyles.size()) &&
                                                 col < static_cast<int>(target.cellStyles[static_cast<size_t>(row)].size())
                                             ? &target.cellStyles[static_cast<size_t>(row)][static_cast<size_t>(col)] : nullptr;
            insetL = style ? style->insetLeft : kDefaultInsetX;
            insetR = style ? style->insetRight : kDefaultInsetX;
            insetT = style ? style->insetTop : kDefaultInsetY;
            insetB = style ? style->insetBottom : kDefaultInsetY;
        }
        textBefore_ = cell ? target.rows[static_cast<size_t>(row)][static_cast<size_t>(col)] : target.paragraphs;
        const Rect2Dd at = edit_->SlideRect();
        const double emu = edit_->EmuPerPixel();
        const float x = static_cast<float>(at.x + static_cast<double>(bx + insetL) / emu);
        const float y = static_cast<float>(at.y + static_cast<double>(by + insetT) / emu);
        const float w = std::max(60.0f, static_cast<float>(static_cast<double>(bw - insetL - insetR) / emu));
        const float h = std::max(30.0f, static_cast<float>(static_cast<double>(bh - insetT - insetB) / emu));

        // The text's own look as the editor's base, so what has none of its
        // own (a new line) looks as the shape says; drawn over the slide.
        RichTextEditStyle style = text_->GetStyle();
        const PptxRun* first = nullptr;
        for (const PptxParagraph& p : textBefore_)
            if (!p.runs.empty()) { first = &p.runs.front(); break; }
        style.baseFont.fontSize = first && first->sizePt > 0 ? first->sizePt : 18.0f;
        // Text with no typeface of its own takes the theme's: the heading
        // font in a title, else the body font.
        const auto& deck = *Editor().GetDeck();
        const std::string themeFont = target.kind == PptxShapeKind::Title ? deck.majorFont : deck.minorFont;
        style.baseFont.fontFamily = !themeFont.empty() ? themeFont : (first && !first->font.empty() ? first->font : std::string());
        const std::string ink = first && !first->color.empty() ? first->color : slide->textColor;
        style.textColor = ink.size() == 6 ? Color(static_cast<uint8_t>(std::stoi(ink.substr(0, 2), nullptr, 16)),
                                                  static_cast<uint8_t>(std::stoi(ink.substr(2, 2), nullptr, 16)),
                                                  static_cast<uint8_t>(std::stoi(ink.substr(4, 2), nullptr, 16)))
                                            : Color(0, 0, 0);
        style.backgroundColor = Color(0, 0, 0, 0);
        style.borderColor = Color(0, 0, 0, 0);
        style.padding = 0;
        text_->SetStyle(style);
        // Points drawn at the slide's size: the editor shows 96 pixels to the inch.
        text_->SetZoom(static_cast<float>((12700.0 / emu) / (96.0 / 72.0)));
        text_->SetDocument(std::make_shared<UCRichDocument>(PptxParagraphsToRichDocument(textBefore_)));
        text_->SetModified(false);
        text_->SetElementAbsolutePosition(Point2Df(x, y));
        text_->SetElementSize(CSSLayout::Dimension::Px(w), CSSLayout::Dimension::Px(h));
        textShape_ = shape;
        if (cell) { lastCellRow_ = row; lastCellCol_ = col; }
        textRow_ = cell ? row : -1;
        textCol_ = cell ? col : -1;
        textSlide_ = Editor().GetCurrentSlide();
        edit_->SetEditingShape(cell ? -1 : shape);
        edit_->SetEditingCell(cell ? shape : -1, row, col);
        text_->SetVisible(true);
        text_->SetFocus(true);
        SyncText();
    }

    // `keep`: the text goes into the shape (one undo step); else it is dropped.
    void CloseText(bool keep) {
        if (textShape_ < 0) return;
        const int shape = textShape_, slide = textSlide_, row = textRow_, col = textCol_;
        textShape_ = -1;
        text_->SetVisible(false);
        edit_->SetEditingShape(-1);
        edit_->SetEditingCell(-1, -1, -1);
        if (keep && text_->IsModified() && slide == Editor().GetCurrentSlide() && text_->GetDocument()) {
            std::vector<PptxParagraph> paragraphs = PptxParagraphsFromRichDocument(*text_->GetDocument(), textBefore_);
            Editor().ChangeSlide("Typing", [&](PptxSlide& s) {
                if (shape >= static_cast<int>(s.shapes.size())) return;
                PptxShape& target = s.shapes[static_cast<size_t>(shape)];
                if (row >= 0 && row < static_cast<int>(target.rows.size()) &&
                    col < static_cast<int>(target.rows[static_cast<size_t>(row)].size()))
                    target.rows[static_cast<size_t>(row)][static_cast<size_t>(col)] = paragraphs;
                else if (row < 0)
                    target.paragraphs = paragraphs;
            });
        }
        edit_->SetFocus(true);
        SyncText();
    }

    // ===== TOOLBARS =====

    void BuildHome() {
        auto bar = MakeToolbar("office-slides-home");
        // The shapes, and what other programs and the other kinds copied:
        // cells as a table, a chart as a chart, text as a text box.
        auto clip = [this](void (UltraCanvasSlideEdit::*action)()) {
            return [this, action]() {
                CloseText(true);
                (edit_.get()->*action)();
                edit_->SetFocus(true);
            };
        };
        AddTool(*bar, "slides-cut", "", "menu/cut.svg", Tr("Cut (Ctrl+X)"), clip(&UltraCanvasSlideEdit::Cut));
        AddTool(*bar, "slides-copy", "", "menu/copy.svg", Tr("Copy (Ctrl+C)"), clip(&UltraCanvasSlideEdit::Copy));
        AddTool(*bar, "slides-paste", "", "menu/paste.svg", Tr("Paste (Ctrl+V)"), clip(&UltraCanvasSlideEdit::Paste));
        bar->AddSeparator();
        AddTool(*bar, "slides-new", Tr("New slide"), "", Tr("A new slide after this one (Ctrl+M)"),
                Act([](UCSlideDeckEditor& e) { e.NewSlide(-1); }));
        layouts_ = bar->AddDropdownButton("slides-layout", "", {}, nullptr);
        layouts_->SetTooltip(Tr("A new slide from one of the deck's layouts"));
        layouts_->onSelectionChanged = [this](int index, const DropdownItem&) {
            if (index < 0) return;
            CloseText(true);
            Editor().NewSlide(index);
            layouts_->SetSelectedIndex(-1, false);
            edit_->SetFocus(true);
        };
        AddTool(*bar, "slides-duplicate", Tr("Duplicate slide"), "", Tr("A copy of this slide after it"),
                Act([](UCSlideDeckEditor& e) { e.DuplicateSlide(e.GetCurrentSlide()); }));
        AddTool(*bar, "slides-delete", Tr("Delete slide"), "", Tr("Delete this slide"),
                Act([](UCSlideDeckEditor& e) { e.DeleteSlide(e.GetCurrentSlide()); }));
        hidden_ = AddToggleTool(*bar, "slides-hide", Tr("Hide"), "", Tr("Leave this slide out of the show"),
                                Act([](UCSlideDeckEditor& e) {
                                    if (const PptxSlide* s = e.CurrentSlide()) e.SetSlideHidden(e.GetCurrentSlide(), !s->hidden);
                                })).get();
        bar->AddSeparator();
        AddTool(*bar, "slides-present", Tr("Present"), "play.svg", Tr("Show the slides full screen, from the first"),
                [this]() { Present(0); });
        tabs_.push_back({Tr("Home"), bar});
    }

    void BuildInsert() {
        auto bar = MakeToolbar("office-slides-insert");
        AddTool(*bar, "slides-textbox", Tr("Text box"), "", Tr("A text box in the middle of the slide"), [this]() {
            CloseText(true);
            const SlideBox box{Editor().SlideWidth() / 2 - 2 * kInch, Editor().SlideHeight() / 2 - kInch / 2, 4 * kInch, kInch};
            OpenText(Editor().InsertTextBox(box));
        });
        auto shapes = bar->AddDropdownButton("slides-shape", Tr("Shape"), GeometryNames(), nullptr);
        shapes->SetTooltip(Tr("A drawn shape in the middle of the slide"));
        shapes->SetPlaceholderText(Tr("Shape"));
        shapes->SetSelectedIndex(-1, false);
        UltraCanvasDropdown* dropdown = shapes.get();
        shapes->onSelectionChanged = [this, dropdown](int index, const DropdownItem&) {
            if (index < 0 || index >= static_cast<int>(std::size(kGeometries))) return;
            CloseText(true);
            const int64_t side = 2 * kInch;
            Editor().InsertGeometry(SlideBox{Editor().SlideWidth() / 2 - side / 2, Editor().SlideHeight() / 2 - side / 2,
                                             side, side},
                                    kGeometries[index]);
            dropdown->SetSelectedIndex(-1, false);
            edit_->SetFocus(true);
        };
        AddTool(*bar, "slides-picture", Tr("Picture"), "image.svg", Tr("Insert a picture from a file"), [this]() { InsertPicture(); });
        tabs_.push_back({Tr("Insert"), bar});
    }

    void BuildArrange() {
        auto bar = MakeToolbar("office-slides-arrange");
        AddTool(*bar, "slides-group", Tr("Group"), "artcreator/group.svg", Tr("The selected shapes as one group (Ctrl+G)"),
                Act([](UCSlideDeckEditor& e) { e.GroupSelection(); }));
        AddTool(*bar, "slides-ungroup", Tr("Ungroup"), "artcreator/ungroup.svg",
                Tr("The selected group taken apart (Ctrl+Shift+G)"), Act([](UCSlideDeckEditor& e) { e.UngroupSelection(); }));
        bar->AddSeparator();
        // Lined up with each other, or with the slide when one is selected.
        AddTool(*bar, "slides-align-left", "", "artcreator/align-left.svg", Tr("Align left edges"),
                Act([](UCSlideDeckEditor& e) { e.AlignSelection(SlideAlignment::Left); }));
        AddTool(*bar, "slides-align-centre", "", "artcreator/align-hcentre.svg", Tr("Align centres"),
                Act([](UCSlideDeckEditor& e) { e.AlignSelection(SlideAlignment::Centre); }));
        AddTool(*bar, "slides-align-right", "", "artcreator/align-right.svg", Tr("Align right edges"),
                Act([](UCSlideDeckEditor& e) { e.AlignSelection(SlideAlignment::Right); }));
        AddTool(*bar, "slides-align-top", "", "artcreator/align-top.svg", Tr("Align tops"),
                Act([](UCSlideDeckEditor& e) { e.AlignSelection(SlideAlignment::Top); }));
        AddTool(*bar, "slides-align-middle", "", "artcreator/align-vcentre.svg", Tr("Align middles"),
                Act([](UCSlideDeckEditor& e) { e.AlignSelection(SlideAlignment::Middle); }));
        AddTool(*bar, "slides-align-bottom", "", "artcreator/align-bottom.svg", Tr("Align bottoms"),
                Act([](UCSlideDeckEditor& e) { e.AlignSelection(SlideAlignment::Bottom); }));
        AddTool(*bar, "slides-distribute-h", "", "artcreator/distribute-h.svg", Tr("Distribute horizontally"),
                Act([](UCSlideDeckEditor& e) { e.DistributeSelection(true); }));
        AddTool(*bar, "slides-distribute-v", "", "artcreator/distribute-v.svg", Tr("Distribute vertically"),
                Act([](UCSlideDeckEditor& e) { e.DistributeSelection(false); }));
        bar->AddSeparator();
        AddTool(*bar, "slides-front", Tr("Bring to front"), "", Tr("In front of every other shape"),
                Act([](UCSlideDeckEditor& e) { e.BringToFront(); }));
        AddTool(*bar, "slides-forward", Tr("Bring forward"), "", Tr("One shape further forward"),
                Act([](UCSlideDeckEditor& e) { e.BringForward(); }));
        AddTool(*bar, "slides-backward", Tr("Send backward"), "", Tr("One shape further back"),
                Act([](UCSlideDeckEditor& e) { e.SendBackward(); }));
        AddTool(*bar, "slides-back", Tr("Send to back"), "", Tr("Behind every other shape"),
                Act([](UCSlideDeckEditor& e) { e.SendToBack(); }));
        bar->AddSeparator();
        AddTool(*bar, "slides-shape-duplicate", Tr("Duplicate"), "", Tr("A copy of the selected shapes (Ctrl+D)"),
                Act([](UCSlideDeckEditor& e) { e.DuplicateSelection(); }));
        AddTool(*bar, "slides-shape-delete", Tr("Delete"), "", Tr("Delete the selected shapes (Delete)"),
                Act([](UCSlideDeckEditor& e) { e.DeleteSelection(); }));
        tabs_.push_back({Tr("Arrange"), bar});
    }

    // Pictures and charts: the crop, and a chart's data.
    void BuildFormat() {
        auto bar = MakeToolbar("office-slides-format");
        crop_ = AddToggleTool(*bar, "slides-crop", Tr("Crop"), "ultrapaint/crop.svg",
                              Tr("Crop the selected picture with its handles (Enter: done)"), [this]() {
                                  CloseText(true);
                                  if (!edit_->SetCropMode(!edit_->IsCropping()))
                                      UltraCanvasDialogManager::ShowMessage(Tr("Select a picture to crop it."), Tr("Crop"),
                                                                            DialogType::Information, DialogButtons::OK,
                                                                            nullptr, window_);
                                  edit_->SetFocus(true);
                                  Sync();
                              }).get();
        AddTool(*bar, "slides-crop-reset", Tr("Reset crop"), "", Tr("The whole picture again"), Act([](UCSlideDeckEditor& e) {
            const PptxSlide* slide = e.CurrentSlide();
            for (int i : e.GetSelection())
                if (slide && slide->shapes[static_cast<size_t>(i)].kind == PptxShapeKind::Picture) e.ResetPictureCrop(i);
        }));
        bar->AddSeparator();
        AddTool(*bar, "slides-chart-data", Tr("Chart data"), "chart.svg", Tr("Edit the selected chart's data"), [this]() {
            const PptxSlide* slide = Editor().CurrentSlide();
            for (int i : Editor().GetSelection())
                if (slide && slide->shapes[static_cast<size_t>(i)].kind == PptxShapeKind::Chart) { EditChartData(i); return; }
            UltraCanvasDialogManager::ShowMessage(Tr("Select a chart to edit its data."), Tr("Chart data"),
                                                  DialogType::Information, DialogButtons::OK, nullptr, window_);
        });
        tabs_.push_back({Tr("Format"), bar});
    }

    // ===== THE VIEW GROUP =====
    // normal | slide sorter | notes | fit the slide | zoom out | [zoom] | zoom in
    void BuildViewGroup() {
        viewBar_ = MakeViewToolbar("office-slides-view");
        viewBar_->AddSeparator();
        normalView_ = AddToggleTool(*viewBar_, "slides-view-normal", "", "office/view-normal.svg",
                                    Tr("Normal: the slide being edited, the slides beside it"),
                                    [this]() { SetView(SlideEditView::Normal); }).get();
        sorterView_ = AddToggleTool(*viewBar_, "slides-view-sorter", "", "artcreator/grid.svg",
                                    Tr("Slide sorter: every slide in a grid, to order, duplicate, hide and delete them"),
                                    [this]() { SetView(SlideEditView::Sorter); }).get();
        notesTool_ = AddToggleTool(*viewBar_, "slides-view-notes", "", "office/notes.svg",
                                   Tr("Notes: the speaker notes under the slide"), [this]() {
                                       notesWanted_ = !notesWanted_;
                                       SyncView();
                                   }).get();
        viewBar_->AddSeparator();
        AddTool(*viewBar_, "slides-fit", "", "office/fit-slide.svg", Tr("Fit the slide (Ctrl+0)"),
                [this]() { SetSlideZoom(1.0); });
        AddTool(*viewBar_, "slides-zoomout", "", "menu/zoom-out.svg", Tr("Zoom out (Ctrl+-)"), [this]() { ZoomOut(); });
        zoomBox_ = AddPercentBox(*viewBar_, "slides-zoom", ZoomLevelChoices(), 25, 800,
                                 Tr("Zoom level: 100 % is the slide fitting the space"), Tr("Zoom"), window_,
                                 [this](int percent) { SetSlideZoom(percent / 100.0); }).get();
        AddTool(*viewBar_, "slides-zoomin", "", "menu/zoom-in.svg", Tr("Zoom in (Ctrl++)"), [this]() { ZoomIn(); });
        SyncView();
    }

    void SetSlideZoom(double zoom) {
        edit_->SetZoom(zoom);
        SyncView();
        edit_->SetFocus(true);
    }

    void SetView(SlideEditView view) {
        CloseText(true);
        CommitNotes();
        edit_->SetView(view);
        SyncView();
        edit_->SetFocus(true);
    }

    // The tools and the notes pane as the view is.
    void SyncView() {
        const bool sorter = edit_->GetView() == SlideEditView::Sorter;
        if (normalView_) normalView_->SetPressed(!sorter);
        if (sorterView_) sorterView_->SetPressed(sorter);
        if (notesTool_) notesTool_->SetPressed(notesWanted_);
        if (crop_) crop_->SetPressed(edit_->IsCropping());
        if (zoomBox_) ShowPercent(*zoomBox_, ZoomLevelChoices(), static_cast<int>(std::lround(edit_->GetZoom() * 100.0)));
        ShowNotes(notesWanted_ && !sorter);
    }

    // ===== NOTES =====

    void ShowNotes(bool show) {
        if (show == notesShown_ || !split_ || !notes_) return;
        if (show) {
            notesPane_ = split_->AddPane(0.0);
            notesPane_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
            notesPane_->AddChild(notes_);
            split_->SetPaneMinSize(1, 48);
            split_->SetPaneFixedSize(1, notesHeight_);
        } else {
            CommitNotes();
            if (split_->PaneCount() > 1 && split_->GetPaneFixedSize(1) > 0) notesHeight_ = split_->GetPaneFixedSize(1);
            if (notesPane_) notesPane_->RemoveChild(notes_);
            split_->RemovePane(1);
            notesPane_.reset();
        }
        notesShown_ = show;
    }

    static std::string NotesText(const std::vector<PptxParagraph>& notes) {
        PptxSlide slide;
        slide.notes = notes;
        return UCSlideDeckEditor::SlideNotesText(slide);
    }

    // The current slide's notes in the pane.
    void LoadNotes() {
        const PptxSlide* slide = Editor().CurrentSlide();
        notesSlide_ = Editor().GetCurrentSlide();
        notesBefore_ = slide ? slide->notes : std::vector<PptxParagraph>();
        notes_->SetDocument(std::make_shared<UCRichDocument>(PptxParagraphsToRichDocument(notesBefore_)));
        notes_->SetModified(false);
    }

    // What was typed into the notes, into its slide: one step.
    void CommitNotes() {
        if (!notes_ || !notes_->IsModified() || !notes_->GetDocument()) return;
        notes_->SetModified(false);
        if (notesSlide_ < 0 || notesSlide_ >= Editor().GetSlideCount()) return;
        std::vector<PptxParagraph> paragraphs = PptxParagraphsFromRichDocument(*notes_->GetDocument(), notesBefore_);
        committingNotes_ = true;
        Editor().SetSlideNotes(notesSlide_, paragraphs);
        committingNotes_ = false;
        notesBefore_ = Editor().GetDeck()->slides[static_cast<size_t>(notesSlide_)].notes;
    }

    // ===== THE CHART'S DATA =====

    // The chart's categories and series in a small spreadsheet; OK puts them
    // into the chart (one step), its kind and look kept.
    void EditChartData(int shape) {
        CloseText(true);
        CommitNotes();
        const PptxSlide* slide = Editor().CurrentSlide();
        if (!slide || shape < 0 || shape >= static_cast<int>(slide->shapes.size()) ||
            slide->shapes[static_cast<size_t>(shape)].kind != PptxShapeKind::Chart)
            return;
        const int slideIndex = Editor().GetCurrentSlide();
        const RichChart before = slide->shapes[static_cast<size_t>(shape)].chart;
        const std::vector<std::vector<std::string>> rows = UCSlideDeckEditor::ChartDataRows(before);
        DialogConfig config;
        config.title = Tr("Chart data");
        config.dialogType = DialogType::Information;
        config.buttons = DialogButtons::OKCancel;
        config.defaultButton = DialogButton::OK;
        // Enter finishes a cell, as in a sheet, and a letter typed into a cell
        // is no button's mnemonic (the dialog takes the grid for no text field).
        config.activateDefaultOnEnter = false;
        config.useButtonMnemonics = false;
        config.showIcon = false;
        config.width = 660;
        config.height = 440;
        auto dialog = UltraCanvasDialogManager::CreateDialog(config);
        if (!dialog) return;
        dialog->SetMessageVisible(false);
        auto sheet = CreateSpreadsheetElement("office-chart-data", 620.0f, 300.0f);
        sheet->size.width = CSSLayout::Dimension::Px(620);
        sheet->size.height = CSSLayout::Dimension::Px(300);
        sheet->SetShowSheetTabs(false);
        size_t columns = 0;
        for (size_t r = 0; r < rows.size(); ++r) {
            columns = std::max(columns, rows[r].size());
            for (size_t c = 0; c < rows[r].size(); ++c) {
                const std::string& text = rows[r][c];
                double number = 0;
                if (r > 0 && c > 0 && TryParseFloat(text, number)) sheet->SetCellValue(static_cast<int>(r), static_cast<int>(c), number);
                else if (!text.empty()) sheet->SetCellValue(static_cast<int>(r), static_cast<int>(c), text);
            }
        }
        sheet->SetActiveCell(1, 1);
        dialog->AddDialogElement(sheet);
        // The dialog owns the sheet for as long as it is open: raw.
        UltraCanvasSpreadsheet* raw = sheet.get();
        const size_t lastRow = rows.size() + 40, lastColumn = columns + 16;
        UltraCanvasDialogManager::ShowDialog(dialog, [this, raw, shape, slideIndex, before, lastRow, lastColumn](DialogResult result) {
            if (result != DialogResult::OK) { edit_->SetFocus(true); return; }
            raw->StopEditing(true);
            std::vector<std::vector<std::string>> grid(lastRow, std::vector<std::string>(lastColumn));
            for (size_t r = 0; r < lastRow; ++r)
                for (size_t c = 0; c < lastColumn; ++c) {
                    const SpreadsheetCell* cell = raw->GetCellIfExists(static_cast<int>(r), static_cast<int>(c));
                    if (!cell) continue;
                    grid[r][c] = cell->GetValueType() == CellValueType::Number
                                     ? FormatFloatClassic(raw->GetCellNumber(static_cast<int>(r), static_cast<int>(c)), 12)
                                     : raw->GetCellText(static_cast<int>(r), static_cast<int>(c));
                }
            if (slideIndex < 0 || slideIndex >= Editor().GetSlideCount()) return;
            if (Editor().GetCurrentSlide() != slideIndex) Editor().SetCurrentSlide(slideIndex);
            Editor().SetShapeChart(shape, UCSlideDeckEditor::ChartWithDataRows(before, grid));
            Editor().Select({shape});
            edit_->SetFocus(true);
        }, window_);
    }

    // ===== THE CONTEXT MENU =====

    void ShowContextMenu(bool inStrip, int slide, int shape, int windowX, int windowY) {
        CloseText(true);
        CommitNotes();
        if (!window_) return;
        menu_ = std::make_shared<UltraCanvasMenu>("office-slides-menu", 0, 0, 220, 0);
        menu_->SetMenuType(MenuType::PopupMenu);
        auto act = [this](std::function<void()> action) {
            return [this, action]() { action(); edit_->SetFocus(true); };
        };
        if (inStrip) {
            // The slides chosen (in the sorter, several).
            std::vector<int> slides = edit_->GetView() == SlideEditView::Sorter ? edit_->GetSorterSelection()
                                                                                : std::vector<int>{slide};
            slides.erase(std::remove(slides.begin(), slides.end(), -1), slides.end());
            bool allHidden = !slides.empty();
            for (int i : slides) allHidden = allHidden && Editor().GetDeck()->slides[static_cast<size_t>(i)].hidden;
            menu_->AddItem(MenuItemData::ActionWithShortcut(Tr("New slide"), "Ctrl+M", act([this]() { Editor().NewSlide(-1); })));
            if (!slides.empty()) {
                menu_->AddItem(MenuItemData::Action(Tr("Duplicate slide"), act([this, slides]() { Editor().DuplicateSlides(slides); })));
                menu_->AddItem(MenuItemData::Action(allHidden ? Tr("Show in the show") : Tr("Hide in the show"),
                                                    act([this, slides, allHidden]() { Editor().SetSlidesHidden(slides, !allHidden); })));
                menu_->AddItem(MenuItemData::Separator());
                menu_->AddItem(MenuItemData::Action(Tr("Delete slide"), act([this, slides]() { Editor().DeleteSlides(slides); })));
            }
        } else {
            const PptxSlide* current = Editor().CurrentSlide();
            const PptxShape* target = current && shape >= 0 && shape < static_cast<int>(current->shapes.size())
                                          ? &current->shapes[static_cast<size_t>(shape)] : nullptr;
            menu_->AddItem(MenuItemData::ActionWithShortcut(Tr("Cut"), "Ctrl+X", act([this]() { edit_->Cut(); })));
            menu_->AddItem(MenuItemData::ActionWithShortcut(Tr("Copy"), "Ctrl+C", act([this]() { edit_->Copy(); })));
            menu_->AddItem(MenuItemData::ActionWithShortcut(Tr("Paste"), "Ctrl+V", act([this]() { edit_->Paste(); })));
            if (target) {
                menu_->AddItem(MenuItemData::Separator());
                if (Editor().CanGroup())
                    menu_->AddItem(MenuItemData::ActionWithShortcut(Tr("Group"), "Ctrl+G", act([this]() { Editor().GroupSelection(); })));
                if (Editor().CanUngroup())
                    menu_->AddItem(MenuItemData::ActionWithShortcut(Tr("Ungroup"), "Ctrl+Shift+G",
                                                                    act([this]() { Editor().UngroupSelection(); })));
                menu_->AddItem(MenuItemData::Action(Tr("Bring to front"), act([this]() { Editor().BringToFront(); })));
                menu_->AddItem(MenuItemData::Action(Tr("Send to back"), act([this]() { Editor().SendToBack(); })));
                if (target->kind == PptxShapeKind::Picture)
                    menu_->AddItem(MenuItemData::Action(Tr("Crop"), act([this]() { edit_->SetCropMode(true); })));
                if (target->kind == PptxShapeKind::Chart)
                    menu_->AddItem(MenuItemData::Action(Tr("Chart data"), [this, shape]() { EditChartData(shape); }));
                menu_->AddItem(MenuItemData::Separator());
                menu_->AddItem(MenuItemData::Action(Tr("Delete"), act([this]() { Editor().DeleteSelection(); })));
            }
        }
        menu_->OpenMenu(Point2Di(windowX, windowY), *window_, PopupElementSettings());
    }

    // ===== TEXT =====
    // Each tool acts on the text being edited in place, else on all the text
    // of the selected shapes (one step).
    void FormatText(const char* description, const std::function<void(UltraCanvasRichTextEdit&)>& inPlace,
                    const std::function<void(PptxRun&)>& run, const std::function<void(PptxParagraph&)>& paragraph = nullptr) {
        if (TextOpen()) {
            inPlace(*text_);
            text_->SetFocus(true);
        } else {
            Editor().FormatSelectedText(description, run, paragraph);
            edit_->SetFocus(true);
        }
        SyncText();
    }

    // Bold, italic ... on the selected shapes: off where every run has it, else on.
    void ToggleRuns(const char* description, bool PptxRun::*flag, void (UltraCanvasRichTextEdit::*toggle)()) {
        const bool all = Editor().AllSelectedText([flag](const PptxRun& r) { return r.*flag; });
        FormatText(description, [toggle](UltraCanvasRichTextEdit& e) { (e.*toggle)(); },
                   [flag, all](PptxRun& r) { r.*flag = !all; });
    }

    void BuildText() {
        auto bar = MakeToolbar("office-slides-text");
        bold_ = AddToggleTool(*bar, "slides-bold", "", "textformat/bold.svg", Tr("Bold (Ctrl+B)"),
                              [this]() { ToggleRuns("Bold", &PptxRun::bold, &UltraCanvasRichTextEdit::ToggleBold); }).get();
        italic_ = AddToggleTool(*bar, "slides-italic", "", "textformat/italic.svg", Tr("Italic (Ctrl+I)"),
                                [this]() { ToggleRuns("Italic", &PptxRun::italic, &UltraCanvasRichTextEdit::ToggleItalic); }).get();
        underline_ = AddToggleTool(*bar, "slides-underline", "", "textformat/underline.svg", Tr("Underline (Ctrl+U)"),
                                   [this]() { ToggleRuns("Underline", &PptxRun::underline, &UltraCanvasRichTextEdit::ToggleUnderline); }).get();
        strike_ = AddToggleTool(*bar, "slides-strike", "", "textformat/strike.svg", Tr("Strikethrough"),
                                [this]() { ToggleRuns("Strikethrough", &PptxRun::strike, &UltraCanvasRichTextEdit::ToggleStrikethrough); }).get();
        bar->AddSeparator();
        const std::vector<std::string> fonts = {"Calibri", "Arial", "Helvetica", "Georgia", "Times New Roman", "Verdana", "Courier New"};
        auto font = bar->AddDropdownButton("slides-font", "", fonts, nullptr);
        font->SetTooltip(Tr("Font"));
        font->SetPlaceholderText(Tr("Font"));
        font->onSelectionChanged = [this, fonts](int index, const DropdownItem&) {
            if (index < 0 || index >= static_cast<int>(fonts.size())) return;
            const std::string family = fonts[static_cast<size_t>(index)];
            FormatText("Font", [family](UltraCanvasRichTextEdit& e) { e.SetFontFamily(family); },
                       [family](PptxRun& r) { r.font = family; });
        };
        const std::vector<float> sizes = {10, 12, 14, 16, 18, 20, 24, 28, 32, 36, 40, 44, 54, 60, 72, 96};
        std::vector<std::string> sizeNames;
        for (float size : sizes) sizeNames.push_back(std::to_string(static_cast<int>(size)));
        auto size = bar->AddDropdownButton("slides-size", "", sizeNames, nullptr);
        size->SetTooltip(Tr("Font size"));
        size->SetPlaceholderText(Tr("Size"));
        size->onSelectionChanged = [this, sizes](int index, const DropdownItem&) {
            if (index < 0 || index >= static_cast<int>(sizes.size())) return;
            const float pt = sizes[static_cast<size_t>(index)];
            FormatText("Font size", [pt](UltraCanvasRichTextEdit& e) { e.SetFontSize(pt); }, [pt](PptxRun& r) { r.sizePt = pt; });
        };
        AddTool(*bar, "slides-grow", "A+", "", Tr("Larger text"), [this]() { Grow(1.2f); });
        AddTool(*bar, "slides-shrink", "A-", "", Tr("Smaller text"), [this]() { Grow(1.0f / 1.2f); });
        const std::vector<std::pair<std::string, std::string>> colours = {
            {Tr("Black"), "000000"}, {Tr("Dark grey"), "404040"}, {Tr("White"), "FFFFFF"}, {Tr("Red"), "C00000"},
            {Tr("Orange"), "ED7D31"}, {Tr("Green"), "70AD47"}, {Tr("Blue"), "4472C4"}, {Tr("Purple"), "7030A0"}};
        std::vector<std::string> colourNames;
        for (const auto& c : colours) colourNames.push_back(c.first);
        auto colour = bar->AddDropdownButton("slides-colour", "", colourNames, nullptr);
        colour->SetTooltip(Tr("Text colour"));
        colour->SetPlaceholderText(Tr("Colour"));
        UltraCanvasDropdown* colourBox = colour.get();
        colour->onSelectionChanged = [this, colours, colourBox](int index, const DropdownItem&) {
            if (index < 0 || index >= static_cast<int>(colours.size())) return;
            const std::string hex = colours[static_cast<size_t>(index)].second;
            FormatText("Text colour", [hex](UltraCanvasRichTextEdit& e) { e.SetTextColor("#" + hex); },
                       [hex](PptxRun& r) { r.color = hex; });
            colourBox->SetSelectedIndex(-1, false);
        };
        bar->AddSeparator();
        auto align = [this](const char* id, const char* icon, const std::string& tip, RichTextAlign rich, PptxAlign pptx) {
            return [this, rich, pptx]() {
                FormatText("Alignment", [rich](UltraCanvasRichTextEdit& e) { e.SetAlignment(rich); }, nullptr,
                           [pptx](PptxParagraph& p) { p.align = pptx; });
            };
        };
        AddTool(*bar, "slides-left", "", "textformat/align-left.svg", Tr("Align left"),
                align("", "", "", RichTextAlign::Left, PptxAlign::Left));
        AddTool(*bar, "slides-centre", "", "textformat/align-centre.svg", Tr("Centre"),
                align("", "", "", RichTextAlign::Center, PptxAlign::Center));
        AddTool(*bar, "slides-right", "", "textformat/align-right.svg", Tr("Align right"),
                align("", "", "", RichTextAlign::Right, PptxAlign::Right));
        AddTool(*bar, "slides-justify", "", "office/justify.svg", Tr("Justify"),
                align("", "", "", RichTextAlign::Justify, PptxAlign::Justify));
        bar->AddSeparator();
        AddTool(*bar, "slides-bullets", "", "ultramail/list-bullets.svg", Tr("Bulleted list"), [this]() {
            const bool all = AllParagraphs([](const PptxParagraph& p) { return p.bullet && !p.numbered; });
            FormatText("Bullets", [](UltraCanvasRichTextEdit& e) { e.ToggleBulletList(); }, nullptr,
                       [all](PptxParagraph& p) { p.bullet = !all; p.numbered = false; });
        });
        AddTool(*bar, "slides-numbers", "", "ultramail/list-numbers.svg", Tr("Numbered list"), [this]() {
            const bool all = AllParagraphs([](const PptxParagraph& p) { return p.numbered; });
            FormatText("Numbering", [](UltraCanvasRichTextEdit& e) { e.ToggleNumberedList(); }, nullptr, [all](PptxParagraph& p) {
                p.numbered = !all;
                p.bullet = !all;
                if (p.numbered && p.numberScheme.empty()) p.numberScheme = "arabicPeriod";
            });
        });
        AddTool(*bar, "slides-outdent", "", "office/outdent.svg", Tr("Up a level"), [this]() {
            FormatText("Level", [](UltraCanvasRichTextEdit& e) { e.OutdentList(); }, nullptr,
                       [](PptxParagraph& p) { p.level = std::max(0, p.level - 1); });
        });
        AddTool(*bar, "slides-indent", "", "office/indent.svg", Tr("Down a level"), [this]() {
            FormatText("Level", [](UltraCanvasRichTextEdit& e) { e.IndentList(); }, nullptr,
                       [](PptxParagraph& p) { p.level = std::min(8, p.level + 1); });
        });
        tabs_.push_back({Tr("Text"), bar});
    }

    bool AllParagraphs(const std::function<bool(const PptxParagraph&)>& test) const {
        const PptxSlide* slide = edit_->Editor().CurrentSlide();
        if (!slide || edit_->Editor().GetSelection().empty()) return false;
        for (int i : edit_->Editor().GetSelection())
            for (const PptxParagraph& p : slide->shapes[static_cast<size_t>(i)].paragraphs)
                if (!test(p)) return false;
        return true;
    }

    void Grow(float factor) {
        if (TextOpen()) {
            const RichCharFormatState state = text_->GetFormatState();
            const float size = state.fontSizePt > 0 ? state.fontSizePt : text_->GetStyle().baseFont.fontSize;
            text_->SetFontSize(std::clamp(std::round(size * factor), 6.0f, 200.0f));
            text_->SetFocus(true);
            return;
        }
        Editor().FormatSelectedText(factor > 1 ? "Larger text" : "Smaller text", [factor](PptxRun& r) {
            const float size = r.sizePt > 0 ? r.sizePt : 18.0f;
            r.sizePt = std::clamp(std::round(size * factor), 6.0f, 200.0f);
        });
        edit_->SetFocus(true);
    }

    // The text tools show what the text has.
    void SyncText() {
        auto set = [](UltraCanvasButton* button, bool on) { if (button) button->SetPressed(on); };
        if (TextOpen()) {
            const RichCharFormatState state = text_->GetFormatState();
            set(bold_, RichCharFormatState::IsOn(state.bold));
            set(italic_, RichCharFormatState::IsOn(state.italic));
            set(underline_, RichCharFormatState::IsOn(state.underline));
            set(strike_, RichCharFormatState::IsOn(state.strikethrough));
            return;
        }
        const auto& editor = edit_->Editor();
        set(bold_, editor.AllSelectedText([](const PptxRun& r) { return r.bold; }));
        set(italic_, editor.AllSelectedText([](const PptxRun& r) { return r.italic; }));
        set(underline_, editor.AllSelectedText([](const PptxRun& r) { return r.underline; }));
        set(strike_, editor.AllSelectedText([](const PptxRun& r) { return r.strike; }));
    }

    // ===== TABLES =====

    // The table the commands act on and its cell: the one being edited, else
    // the selected table's last cell.
    bool TableCell(int& shape, int& row, int& col) {
        if (TextOpen() && textRow_ >= 0) {
            shape = textShape_; row = textRow_; col = textCol_;
            return true;
        }
        const auto& selection = Editor().GetSelection();
        const PptxSlide* slide = Editor().CurrentSlide();
        if (!slide || selection.size() != 1) return false;
        const PptxShape& table = slide->shapes[static_cast<size_t>(selection.front())];
        if (table.kind != PptxShapeKind::Table || table.rows.empty()) return false;
        shape = selection.front();
        row = lastCellRow_ >= 0 && lastCellRow_ < static_cast<int>(table.rows.size()) ? lastCellRow_ : static_cast<int>(table.rows.size()) - 1;
        col = lastCellCol_ >= 0 && lastCellCol_ < static_cast<int>(table.rows.front().size()) ? lastCellCol_ : static_cast<int>(table.rows.front().size()) - 1;
        return true;
    }

    std::function<void()> TableAct(std::function<void(UCSlideDeckEditor&, int, int, int)> action) {
        return [this, action]() {
            int shape = -1, row = -1, col = -1;
            if (!TableCell(shape, row, col)) return;
            CloseText(true);
            action(Editor(), shape, row, col);
            Editor().Select({shape});
            edit_->SetFocus(true);
        };
    }

    void BuildTable() {
        auto bar = MakeToolbar("office-slides-table");
        const std::vector<std::pair<int, int>> sizes = {{2, 2}, {3, 3}, {4, 4}, {3, 5}, {5, 3}, {6, 4}};
        std::vector<std::string> names;
        for (const auto& size : sizes) names.push_back(TrF("{0} rows, {1} columns", size.first, size.second));
        auto insert = bar->AddDropdownButton("slides-table", "", names, nullptr);
        insert->SetTooltip(Tr("A table in the middle of the slide"));
        insert->SetPlaceholderText(Tr("Insert table"));
        UltraCanvasDropdown* insertBox = insert.get();
        insert->onSelectionChanged = [this, sizes, insertBox](int index, const DropdownItem&) {
            if (index < 0 || index >= static_cast<int>(sizes.size())) return;
            CloseText(true);
            const int rows = sizes[static_cast<size_t>(index)].first, cols = sizes[static_cast<size_t>(index)].second;
            const int64_t width = Editor().SlideWidth() * 7 / 10, height = rows * (kInch * 2 / 5);
            Editor().InsertTable(SlideBox{(Editor().SlideWidth() - width) / 2, (Editor().SlideHeight() - height) / 2, width, height},
                                 rows, cols);
            insertBox->SetSelectedIndex(-1, false);
            edit_->SetFocus(true);
        };
        bar->AddSeparator();
        AddTool(*bar, "slides-row-above", Tr("Row above"), "", Tr("A row above this cell"),
                TableAct([](UCSlideDeckEditor& e, int s, int r, int) { e.InsertTableRow(s, r, false); }));
        AddTool(*bar, "slides-row-below", Tr("Row below"), "", Tr("A row below this cell"),
                TableAct([](UCSlideDeckEditor& e, int s, int r, int) { e.InsertTableRow(s, r, true); }));
        AddTool(*bar, "slides-col-left", Tr("Column left"), "", Tr("A column left of this cell"),
                TableAct([](UCSlideDeckEditor& e, int s, int, int c) { e.InsertTableColumn(s, c, false); }));
        AddTool(*bar, "slides-col-right", Tr("Column right"), "", Tr("A column right of this cell"),
                TableAct([](UCSlideDeckEditor& e, int s, int, int c) { e.InsertTableColumn(s, c, true); }));
        bar->AddSeparator();
        AddTool(*bar, "slides-row-delete", Tr("Delete row"), "", Tr("Delete this cell's row"),
                TableAct([](UCSlideDeckEditor& e, int s, int r, int) { e.DeleteTableRow(s, r); }));
        AddTool(*bar, "slides-col-delete", Tr("Delete column"), "", Tr("Delete this cell's column"),
                TableAct([](UCSlideDeckEditor& e, int s, int, int c) { e.DeleteTableColumn(s, c); }));
        tabs_.push_back({Tr("Table"), bar});
    }

    void BuildShow() {
        auto bar = MakeToolbar("office-slides-show");
        AddTool(*bar, "slides-show-first", Tr("From the beginning"), "play.svg", Tr("Show the slides full screen, from the first"),
                [this]() { Present(0); });
        AddTool(*bar, "slides-show-here", Tr("From this slide"), "", Tr("Show the slides full screen, from this one"),
                [this]() { Present(std::max(0, Editor().GetCurrentSlide())); });
        bar->AddSeparator();
        // How this slide comes on in the show.
        transition_ = bar->AddDropdownButton("slides-transition", "",
            {Tr("No transition"), Tr("Fade"), Tr("Push"), Tr("Wipe"), Tr("Split"), Tr("Cover"), Tr("Zoom")}, nullptr);
        transition_->SetTooltip(Tr("How this slide comes on in the show"));
        transition_->onSelectionChanged = [this](int index, const DropdownItem&) {
            if (index < 0 || syncing_) return;
            const auto kind = static_cast<PptxTransitionKind>(index);
            const PptxSlide* slide = Editor().CurrentSlide();
            if (!slide || slide->transition.kind == kind) return;
            Editor().ChangeSlide("Transition", [kind](PptxSlide& s) {
                s.transition.kind = kind;
                if (s.transition.durationSec <= 0) s.transition.durationSec = 0.7;
            });
            edit_->SetFocus(true);
        };
        AddTool(*bar, "slides-transition-all", Tr("Apply to all slides"), "", Tr("Every slide comes on as this one does"), [this]() {
            const PptxSlide* slide = Editor().CurrentSlide();
            if (!slide) return;
            const PptxTransition transition = slide->transition;
            const int current = Editor().GetCurrentSlide();
            // One step per slide changed, the last one shown again.
            for (int i = 0; i < Editor().GetSlideCount(); ++i) {
                if (i == current) continue;
                Editor().SetCurrentSlide(i);
                Editor().ChangeSlide("Transition", [transition](PptxSlide& s) { s.transition = transition; });
            }
            Editor().SetCurrentSlide(current);
            edit_->SetFocus(true);
        });
        tabs_.push_back({Tr("Slide show"), bar});
    }

    void InsertPicture() {
        CloseText(true);
        FileDialogOptions opts;
        opts.title = Tr("Insert picture");
        UltraCanvasFileLoader::AddLoadTypes(opts, Tr("Pictures"), {MediaFormatCategory::Bitmap});
        opts.parentWindow = window_;
        UltraCanvasWindowBase* window = window_;
        UltraCanvasFileLoader::OpenFileDialog(opts, [this, window](DialogResult result, const std::string& path) {
            if (result != DialogResult::OK || path.empty()) return;
            FileBytesResult file = UltraCanvasFileLoader::LoadFile(path);
            auto image = file.success ? UCImage::LoadFromMemory(file.bytes) : nullptr;
            if (!image) {
                UltraCanvasDialogManager::ShowError(Tr("The picture could not be read."), Tr("Insert picture"), nullptr, window);
                return;
            }
            const std::string ext = PathToUtf8(PathFromUtf8(path).extension());
            Editor().InsertPicture(file.bytes, ext, image->GetWidth(), image->GetHeight(),
                                   PathToUtf8(PathFromUtf8(path).stem()));
            edit_->SetFocus(true);
        });
    }

    // ===== THE SHOW =====

    // The show: the player full screen with the slides' transitions, and on
    // a second monitor the presenter view (the next slide, the notes, the
    // time) - StartSlideShow. The deck is shared, read only.
    void Present(int fromSlide) {
        CloseText(true);
        CommitNotes();
        if (show_) return;
        // With two monitors: the show on the one the settings say, the
        // presenter view on the other unless they turn it off.
        const bool presenter = !settings_ || OfficeSettings(*settings_).GetPresenterView();
        const bool second = !settings_ || OfficeSettings(*settings_).GetShowOnSecondScreen();
        show_ = StartSlideShow(Editor().GetDeck(), fromSlide, presenter, second);
        if (!show_) return;
        SlideShowWindows* raw = show_.get();
        show_->onEnd = [this, raw]() {
            // Let go once the event that ended it is over.
            if (auto* app = UltraCanvasApplication::GetInstance())
                app->StartTimer(1, false, [this, raw](TimerId) {
                    if (show_.get() == raw) show_.reset();
                    edit_->SetFocus(true);
                });
        };
    }

    void Sync() {
        SyncText();
        if (crop_) crop_->SetPressed(edit_->IsCropping());
        if (zoomBox_) ShowPercent(*zoomBox_, ZoomLevelChoices(), static_cast<int>(std::lround(edit_->GetZoom() * 100.0)));
        const auto& editor = Editor();
        if (hidden_) {
            const PptxSlide* slide = editor.CurrentSlide();
            hidden_->SetPressed(slide && slide->hidden);
        }
        if (transition_) {
            const PptxSlide* slide = editor.CurrentSlide();
            syncing_ = true;
            transition_->SetSelectedIndex(slide ? static_cast<int>(slide->transition.kind) : -1, false);
            syncing_ = false;
        }
        // The deck's layouts, by name, for a new slide.
        if (layouts_) {
            std::vector<std::string> names;
            for (const PptxLayout& layout : editor.GetDeck()->layouts) names.push_back(layout.name);
            if (names.empty()) names.push_back(Tr("Title and Content"));
            if (layouts_->GetItemCount() != static_cast<int>(names.size())) {
                layouts_->ClearItems();
                for (const auto& name : names) layouts_->AddItem(name);
                layouts_->SetPlaceholderText(Tr("Layout"));
            }
        }
    }
};

} // namespace

std::unique_ptr<OfficeHost> CreatePresentationHost(UltraCanvasWindowBase* window) {
    return std::make_unique<PresentationHost>(window);
}

} // namespace UltraOffice
