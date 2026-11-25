// Apps/UltraOffice/UltraOfficeDocumentHost.cpp
// A word-processing tab: the framework's WYSIWYG element
// (UltraCanvasRichTextEdit) over a UCRichDocument, read and written through
// UltraCanvasFileLoader (.odt, .docx, .doc read-only, .md, .tex), with five
// command tabs - Home, Insert, Layout, Review, View - whose toolbars follow
// the caret (GetFormatState), the framework's document dialogs
// (UltraCanvasRichTextEditDialogs.h), and PDF export and printing of the pages.
//
// Version: 0.2.0 - the document dialogs: paragraph and styles (Home), contents, caption,
//                  cross-reference and footnote (Insert), page setup, table and picture
//                  (Layout, a new tab), comments and tracked changes (Review, a new tab)
// Version: 0.1.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraOfficeHost.h"
#include "UltraOfficeToolbar.h"

#include "IODeviceManager/UltraCanvasIODevicePrintDialog.h"   // PrintDocumentWithDialog
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasRichDocument.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasRichTextEditDialogs.h"
#include "UltraCanvasRichTextPrint.h"
#include "UltraCanvasPathUtf8.h"      // GetEnvUtf8
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"

#include <algorithm>
#include <charconv>

namespace UltraOffice {

using namespace UltraCanvas;

namespace {

// The lists a new tab starts with; a font or size a document uses that is
// not here joins its tab's list when the caret first reaches it. The empty
// family is "Default font": text with no font of its own.
const std::vector<std::string> kFonts = {"", "Ubuntu", "Arial", "Georgia", "Times New Roman", "Courier New", "Verdana"};
const std::vector<float> kSizes = {8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 72};

// "10.5", whatever the locale: the list reads the same as the size boxes
// of the documents it came from.
std::string SizeText(float size) {
    char text[32];
    const auto result = std::to_chars(text, text + sizeof text, size);
    return std::string(text, result.ptr);
}

class DocumentHost final : public OfficeHost {
public:
    explicit DocumentHost(UltraCanvasWindowBase* window) : window_(window) {
        editor_ = CreateRichTextEdit("office-document", 0, 0, 800, 600);
        editor_->SetPageView(true);
        editor_->SetZoomFit(UltraCanvasRichTextEdit::ZoomFit::PageWidth);
        editor_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        UltraCanvasRichTextEdit* edit = editor_.get();
        edit->onSelectionChanged = [this]() { Sync(); if (onChanged) onChanged(); };
        edit->onDocumentChanged = [this]() { Sync(); if (onChanged) onChanged(); };
        // Comments and tracked changes are signed with the login name (UTF-8,
        // as the document stores it); a double-click on a comment edits it.
        std::string user = GetEnvUtf8("USER");
        if (user.empty()) user = GetEnvUtf8("USERNAME");
        edit->SetCommentAuthor(user);
        edit->onCommentActivated = [this, edit](int index) {
            ShowCommentDialog(*edit, index, [this](bool) { Sync(); });
        };
        BuildHome();
        BuildInsert();
        BuildLayout();
        BuildReview();
        BuildView();
        NewBlank();
    }

    OfficeKind GetKind() const override { return OfficeKind::Document; }
    std::shared_ptr<UltraCanvasUIElement> GetContent() override { return editor_; }
    const std::vector<CommandTab>& GetCommandTabs() override { return tabs_; }

    void NewBlank() override {
        editor_->SetDocument(std::make_shared<UCRichDocument>());
        editor_->SetModified(false);
        Sync();
    }

    bool Load(const std::string& path, const std::string& password, std::string& error) override {
        auto document = UltraCanvasFileLoader::LoadTextDocument(path, error, password);
        if (!document) return false;
        editor_->SetDocument(std::move(document));
        editor_->SetModified(false);
        Sync();
        return true;
    }

    bool Write(const std::string& path, const std::string& password, std::string& error) override {
        const auto& document = editor_->GetDocument();
        if (!document) { error = Tr("There is no document to save."); return false; }
        if (DetectWordDocumentFormat(path) == WordDocumentFormat::LegacyDoc ||
            WordDocumentFormatFromExtension(path) == WordDocumentFormat::LegacyDoc) {
            error = Tr("Word 97-2003 (.doc) files are read, not written. Save as .docx or .odt instead.");
            return false;
        }
        return UltraCanvasFileLoader::SaveTextDocument(*document, path, error, password);
    }

    void AddSaveTypes(FileDialogOptions& opts) const override { UltraCanvasFileLoader::AddTextDocumentSaveTypes(opts); }
    std::string DefaultExtension(SaveFormat format) const override {
        return format == SaveFormat::OpenDocument ? "odt" : "docx";
    }

    bool CanExportPdf() const override { return true; }
    bool ExportPdf(const std::string& path, std::string& error) override { return editor_->ExportToPdf(path, error); }
    bool Print(const std::string& name, UltraCanvasWindowBase* parent, std::string& error) override {
        std::vector<uint8_t> pdf;
        if (!editor_->ExportToPdf(pdf, error)) return false;
        const IODeviceResult printed =
            PrintDocumentWithDialog(name, pdf, "application/pdf", parent, CreateRichDocumentPrintPages(*editor_));
        if (!printed.success && printed.code != IODeviceResultCode::Cancelled) {
            error = printed.message;
            return false;
        }
        return true;
    }

    bool IsModified() const override { return editor_->IsModified(); }
    void SetModified(bool modified) override { editor_->SetModified(modified); }
    void Undo() override { editor_->Undo(); Sync(); }
    void Redo() override { editor_->Redo(); Sync(); }
    void Focus() override { editor_->SetFocus(true); }

    std::string GetStatusText() const override {
        const auto& editor = editor_->GetEditor();
        const int blocks = editor.GetBlockCount();
        const int at = editor.GetCaret().blockIndex + 1;
        return TrF("Paragraph {0} of {1}", at, blocks) + "    " +
               (editor_->IsPageView() ? Tr("Page view") : Tr("Continuous view"));
    }

private:
    UltraCanvasWindowBase* window_;
    std::shared_ptr<UltraCanvasRichTextEdit> editor_;
    std::vector<CommandTab> tabs_;
    // Raw: the toolbars own them, and the host owns the toolbars.
    UltraCanvasButton* bold_ = nullptr;
    UltraCanvasButton* italic_ = nullptr;
    UltraCanvasButton* underline_ = nullptr;
    UltraCanvasButton* strike_ = nullptr;
    UltraCanvasButton* pageView_ = nullptr;
    UltraCanvasButton* trackChanges_ = nullptr;
    UltraCanvasButton* showComments_ = nullptr;
    UltraCanvasDropdown* style_ = nullptr;
    UltraCanvasDropdown* font_ = nullptr;
    UltraCanvasDropdown* size_ = nullptr;
    std::vector<std::string> fonts_ = kFonts;
    std::vector<float> sizes_ = kSizes;

    // Every tool acts on the document and then brings the toolbar into step:
    // a format armed at a collapsed caret raises no change of its own.
    std::function<void()> Act(std::function<void(UltraCanvasRichTextEdit&)> action) {
        UltraCanvasRichTextEdit* edit = editor_.get();
        return [this, edit, action]() { action(*edit); Sync(); edit->SetFocus(true); };
    }

    // A tool that opens one of the framework's document dialogs; the toolbar
    // follows what its OK changed.
    std::function<void()> Dialog(std::function<void(UltraCanvasRichTextEdit&, RichDialogClosed)> show) {
        UltraCanvasRichTextEdit* edit = editor_.get();
        return [this, edit, show]() { show(*edit, [this](bool) { Sync(); }); };
    }

    void BuildHome() {
        auto bar = MakeToolbar("office-document-home");
        AddTool(*bar, "doc-cut", "", "menu/cut.svg", Tr("Cut (Ctrl+X)"), Act([](auto& e) { e.Cut(); }));
        AddTool(*bar, "doc-copy", "", "menu/copy.svg", Tr("Copy (Ctrl+C)"), Act([](auto& e) { e.Copy(); }));
        AddTool(*bar, "doc-paste", "", "menu/paste.svg", Tr("Paste (Ctrl+V)"), Act([](auto& e) { e.Paste(); }));
        bar->AddSeparator();

        UltraCanvasRichTextEdit* edit = editor_.get();
        auto style = bar->AddDropdownButton("doc-style", "",
            {Tr("Body text"), Tr("Heading 1"), Tr("Heading 2"), Tr("Heading 3"), Tr("Heading 4")}, nullptr);
        style->SetTooltip(Tr("Paragraph style"));
        style->onSelectionChanged = [this, edit](int index, const DropdownItem&) {
            edit->SetHeadingLevel(std::max(0, index));
            Sync();
            edit->SetFocus(true);
        };
        style_ = style.get();
        std::vector<std::string> fontNames = kFonts;
        fontNames.front() = Tr("Default font");
        auto font = bar->AddDropdownButton("doc-font", "", fontNames, nullptr);
        font->SetTooltip(Tr("Font"));
        font->onSelectionChanged = [this, edit](int index, const DropdownItem&) {
            if (index >= 0 && index < static_cast<int>(fonts_.size())) edit->SetFontFamily(fonts_[static_cast<size_t>(index)]);
            Sync();
            edit->SetFocus(true);
        };
        font_ = font.get();
        std::vector<std::string> sizes;
        for (float s : kSizes) sizes.push_back(SizeText(s));
        auto size = bar->AddDropdownButton("doc-size", "", sizes, nullptr);
        size->SetTooltip(Tr("Font size"));
        size->onSelectionChanged = [this, edit](int index, const DropdownItem&) {
            if (index >= 0 && index < static_cast<int>(sizes_.size())) edit->SetFontSize(sizes_[static_cast<size_t>(index)]);
            Sync();
            edit->SetFocus(true);
        };
        size_ = size.get();
        bar->AddSeparator();

        bold_ = AddToggleTool(*bar, "doc-bold", "", "textformat/bold.svg", Tr("Bold (Ctrl+B)"), Act([](auto& e) { e.ToggleBold(); })).get();
        italic_ = AddToggleTool(*bar, "doc-italic", "", "textformat/italic.svg", Tr("Italic (Ctrl+I)"), Act([](auto& e) { e.ToggleItalic(); })).get();
        underline_ = AddToggleTool(*bar, "doc-underline", "", "textformat/underline.svg", Tr("Underline (Ctrl+U)"), Act([](auto& e) { e.ToggleUnderline(); })).get();
        strike_ = AddToggleTool(*bar, "doc-strike", "", "textformat/strike.svg", Tr("Strikethrough"), Act([](auto& e) { e.ToggleStrikethrough(); })).get();
        bar->AddSeparator();

        AddTool(*bar, "doc-bullets", "", "ultramail/list-bullets.svg", Tr("Bulleted list"), Act([](auto& e) { e.ToggleBulletList(); }));
        AddTool(*bar, "doc-numbers", "", "ultramail/list-numbers.svg", Tr("Numbered list"), Act([](auto& e) { e.ToggleNumberedList(); }));
        AddTool(*bar, "doc-outdent", "", "office/outdent.svg", Tr("Decrease indent (Shift+Tab)"), Act([](auto& e) { e.OutdentList(); }));
        AddTool(*bar, "doc-indent", "", "office/indent.svg", Tr("Increase indent (Tab)"), Act([](auto& e) { e.IndentList(); }));
        bar->AddSeparator();

        AddTool(*bar, "doc-left", "", "textformat/align-left.svg", Tr("Align left"), Act([](auto& e) { e.SetAlignment(RichTextAlign::Left); }));
        AddTool(*bar, "doc-centre", "", "textformat/align-centre.svg", Tr("Centre"), Act([](auto& e) { e.SetAlignment(RichTextAlign::Center); }));
        AddTool(*bar, "doc-right", "", "textformat/align-right.svg", Tr("Align right"), Act([](auto& e) { e.SetAlignment(RichTextAlign::Right); }));
        AddTool(*bar, "doc-justify", "", "office/justify.svg", Tr("Justify"), Act([](auto& e) { e.SetAlignment(RichTextAlign::Justify); }));
        bar->AddSeparator();
        AddTool(*bar, "doc-paragraph", Tr("Paragraph"), "textformat/line-spacing.svg",
                Tr("Indents, spacing, line spacing, alignment and tab stops"),
                Dialog([](auto& e, RichDialogClosed done) { ShowParagraphDialog(e, std::move(done)); }));
        AddTool(*bar, "doc-styles", Tr("Styles"), "menu/palette.svg",
                Tr("Apply, change or make a named style"),
                Dialog([](auto& e, RichDialogClosed done) { ShowStylesDialog(e, std::move(done)); }));
        tabs_.push_back({Tr("Home"), bar});
    }

    void BuildInsert() {
        auto bar = MakeToolbar("office-document-insert");
        AddTool(*bar, "doc-table", Tr("Table"), "table.svg", Tr("Insert a table of three rows and three columns"),
                Act([](auto& e) { e.InsertTable(3, 3, true); }));
        AddTool(*bar, "doc-picture", Tr("Picture"), "image.svg", Tr("Insert a picture from a file"), [this]() { InsertPicture(); });
        AddTool(*bar, "doc-link", Tr("Link"), "link.svg", Tr("Make the selection a link"), [this]() { InsertLink(); });
        bar->AddSeparator();
        AddTool(*bar, "doc-pagebreak", Tr("Page break"), "", Tr("Start a new page here"), Act([](auto& e) { e.InsertPageBreak(); }));
        bar->AddSeparator();
        AddTool(*bar, "doc-footnote", Tr("Footnote"), "superscript.svg", Tr("Insert a footnote or an endnote at the caret"),
                Dialog([](auto& e, RichDialogClosed done) { ShowNoteDialog(e, std::move(done)); }));
        AddTool(*bar, "doc-toc", Tr("Contents"), "list-tree.svg", Tr("Insert or rebuild a table of contents of the headings"),
                Dialog([](auto& e, RichDialogClosed done) { ShowContentsDialog(e, std::move(done)); }));
        AddTool(*bar, "doc-caption", Tr("Caption"), "add_text.svg", Tr("A numbered caption under the picture or table"),
                Dialog([](auto& e, RichDialogClosed done) { ShowCaptionDialog(e, std::move(done)); }));
        AddTool(*bar, "doc-crossref", Tr("Cross-reference"), "menu/book.svg",
                Tr("Refer to a caption or a bookmark by its text or its page"),
                Dialog([](auto& e, RichDialogClosed done) { ShowCrossReferenceDialog(e, std::move(done)); }));
        tabs_.push_back({Tr("Insert"), bar});
    }

    void BuildLayout() {
        auto bar = MakeToolbar("office-document-layout");
        AddTool(*bar, "doc-pagesetup", Tr("Page setup"), "document.svg", Tr("Paper, orientation, margins, header and footer"),
                Dialog([](auto& e, RichDialogClosed done) { ShowPageSetupDialog(e, std::move(done)); }));
        bar->AddSeparator();
        AddTool(*bar, "doc-tableprops", Tr("Table"), "table.svg", Tr("The width, alignment, header row and borders of the table"),
                Dialog([](auto& e, RichDialogClosed done) { ShowTablePropertiesDialog(e, std::move(done)); }));
        AddTool(*bar, "doc-pictureprops", Tr("Picture"), "image.svg", Tr("The alt text, wrapping and size of the selected picture"),
                Dialog([](auto& e, RichDialogClosed done) { ShowPictureDialog(e, std::move(done)); }));
        tabs_.push_back({Tr("Layout"), bar});
    }

    void BuildReview() {
        auto bar = MakeToolbar("office-document-review");
        AddTool(*bar, "doc-comment", Tr("Comment"), "clipboard/edit.svg", Tr("Comment on the selection"),
                Dialog([](auto& e, RichDialogClosed done) { ShowCommentDialog(e, -1, std::move(done)); }));
        AddTool(*bar, "doc-prevcomment", "", "texter/angle-left.svg", Tr("Previous comment"), Act([](auto& e) { e.GoToPreviousComment(); }));
        AddTool(*bar, "doc-nextcomment", "", "texter/angle-right.svg", Tr("Next comment"), Act([](auto& e) { e.GoToNextComment(); }));
        AddTool(*bar, "doc-comments", Tr("All comments"), "list.svg", Tr("The comments of the document: go to, edit, resolve or delete one"),
                Dialog([](auto& e, RichDialogClosed done) { ShowCommentsDialog(e, std::move(done)); }));
        showComments_ = AddToggleTool(*bar, "doc-showcomments", Tr("Show comments"), "", Tr("Show the comments and their pane"),
                                      Act([](auto& e) { e.SetShowComments(!e.IsShowingComments()); })).get();
        bar->AddSeparator();
        trackChanges_ = AddToggleTool(*bar, "doc-track", Tr("Track changes"), "edit.svg", Tr("Mark what is typed and deleted"),
                                      Act([](auto& e) { e.SetTrackChanges(!e.IsTrackingChanges()); })).get();
        AddTool(*bar, "doc-prevchange", "", "arrow-left.svg", Tr("Previous change"), Act([](auto& e) { e.GoToPreviousChange(); }));
        AddTool(*bar, "doc-nextchange", "", "arrow-right.svg", Tr("Next change"), Act([](auto& e) { e.GoToNextChange(); }));
        AddTool(*bar, "doc-accept", Tr("Accept"), "ultramail/check.svg", Tr("Accept the change at the caret, or those selected"),
                Act([](auto& e) { e.AcceptChangeAtCaret(); }));
        AddTool(*bar, "doc-reject", Tr("Reject"), "delete.svg", Tr("Reject the change at the caret, or those selected"),
                Act([](auto& e) { e.RejectChangeAtCaret(); }));
        AddTool(*bar, "doc-acceptall", Tr("Accept all"), "", Tr("Accept every change"), Act([](auto& e) { e.AcceptAllChanges(); }));
        AddTool(*bar, "doc-rejectall", Tr("Reject all"), "", Tr("Reject every change"), Act([](auto& e) { e.RejectAllChanges(); }));
        tabs_.push_back({Tr("Review"), bar});
    }

    void BuildView() {
        auto bar = MakeToolbar("office-document-view");
        pageView_ = AddToggleTool(*bar, "doc-pageview", Tr("Pages"), "", Tr("Show the document as pages"),
                                  Act([](auto& e) { e.SetPageView(!e.IsPageView()); })).get();
        bar->AddSeparator();
        AddTool(*bar, "doc-fitwidth", Tr("Page width"), "zoom-fit.svg", Tr("Zoom to the width of the page"),
                Act([](auto& e) { e.SetZoomFit(UltraCanvasRichTextEdit::ZoomFit::PageWidth); }));
        AddTool(*bar, "doc-fitpage", Tr("Whole page"), "", Tr("Zoom to show the whole page"),
                Act([](auto& e) { e.SetZoomFit(UltraCanvasRichTextEdit::ZoomFit::WholePage); }));
        AddTool(*bar, "doc-zoomin", "", "zoom-in.svg", Tr("Zoom in"),
                Act([](auto& e) { e.SetZoom(std::min(5.0f, e.GetZoom() * 1.1f)); }));
        AddTool(*bar, "doc-zoomout", "", "zoom-out.svg", Tr("Zoom out"),
                Act([](auto& e) { e.SetZoom(std::max(0.25f, e.GetZoom() / 1.1f)); }));
        tabs_.push_back({Tr("View"), bar});
    }

    void InsertPicture() {
        FileDialogOptions opts;
        opts.title = Tr("Insert picture");
        UltraCanvasFileLoader::AddLoadTypes(opts, Tr("Pictures"), {MediaFormatCategory::Bitmap, MediaFormatCategory::Vector});
        opts.parentWindow = window_;
        UltraCanvasRichTextEdit* edit = editor_.get();
        UltraCanvasWindowBase* window = window_;
        UltraCanvasFileLoader::OpenFileDialog(opts, [this, edit, window](DialogResult result, const std::string& path) {
            if (result != DialogResult::OK || path.empty()) return;
            if (!edit->InsertImageFromFile(path)) {
                UltraCanvasDialogManager::ShowError(Tr("The picture could not be read."), Tr("Insert picture"), nullptr, window);
            }
            Sync();
        });
    }

    void InsertLink() {
        UltraCanvasRichTextEdit* edit = editor_.get();
        if (!edit->HasSelection()) {
            UltraCanvasDialogManager::ShowMessage(Tr("Select the words the link should cover first."), Tr("Link"),
                                                  DialogType::Information, DialogButtons::OK, nullptr, window_);
            return;
        }
        UltraCanvasDialogManager::ShowInputDialog(Tr("The address the link opens"), Tr("Link"), "https://", InputType::Text,
            [this, edit](DialogResult result, const std::string& target) {
                if (result != DialogResult::OK || target.empty()) return;
                edit->SetLink(target);
                Sync();
            }, window_);
    }

    // The toolbar shows what the caret or the selection has.
    void Sync() {
        const RichCharFormatState state = editor_->GetFormatState();
        if (bold_) bold_->SetPressed(RichCharFormatState::IsOn(state.bold));
        if (italic_) italic_->SetPressed(RichCharFormatState::IsOn(state.italic));
        if (underline_) underline_->SetPressed(RichCharFormatState::IsOn(state.underline));
        if (strike_) strike_->SetPressed(RichCharFormatState::IsOn(state.strikethrough));
        if (pageView_) pageView_->SetPressed(editor_->IsPageView());
        if (trackChanges_) trackChanges_->SetPressed(editor_->IsTrackingChanges());
        if (showComments_) showComments_->SetPressed(editor_->IsShowingComments());
        if (style_) {
            const int level = editor_->GetCurrentHeadingLevel();
            style_->SetSelectedIndex(level >= 0 && level <= 4 ? level : 0, false);   // read out, not applied again
        }
        // A selection with more than one font or size shows none; text with
        // none of its own shows the editor's base font, and in body text its
        // size (a heading scales the base size).
        const FontStyle& base = editor_->GetStyle().baseFont;
        std::string family = state.fontFamily;
        if (family.empty() && !state.fontFamilyMixed) family = base.fontFamily;
        float sizePt = state.fontSizePt;
        if (sizePt <= 0.0f && !state.fontSizeMixed && editor_->GetCurrentHeadingLevel() == 0)
            sizePt = static_cast<float>(base.fontSize);
        if (font_) {
            int index = -1;
            if (!state.fontFamilyMixed) {
                auto it = std::find(fonts_.begin(), fonts_.end(), family);
                if (it == fonts_.end()) {
                    fonts_.push_back(family);
                    font_->AddItem(family);
                    it = fonts_.end() - 1;
                }
                index = static_cast<int>(it - fonts_.begin());
            }
            font_->SetSelectedIndex(index, false);
        }
        if (size_) {
            int index = -1;
            if (!state.fontSizeMixed && sizePt > 0.0f) {
                auto it = std::find(sizes_.begin(), sizes_.end(), sizePt);
                if (it == sizes_.end()) {
                    it = sizes_.insert(std::upper_bound(sizes_.begin(), sizes_.end(), sizePt), sizePt);
                    size_->ClearItems();
                    for (float s : sizes_) size_->AddItem(SizeText(s));
                }
                index = static_cast<int>(it - sizes_.begin());
            }
            size_->SetSelectedIndex(index, false);
        }
    }
};

} // namespace

std::unique_ptr<OfficeHost> CreateDocumentHost(UltraCanvasWindowBase* window) {
    return std::make_unique<DocumentHost>(window);
}

} // namespace UltraOffice
