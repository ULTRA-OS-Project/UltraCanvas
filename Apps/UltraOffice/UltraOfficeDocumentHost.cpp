// Apps/UltraOffice/UltraOfficeDocumentHost.cpp
// A word-processing tab: the framework's WYSIWYG element
// (UltraCanvasRichTextEdit) over a UCRichDocument, read and written through
// UltraCanvasFileLoader (.odt, .docx, .doc read-only, .md, .tex), with four
// command tabs - Home, Insert, Layout, Review - whose toolbars follow the
// caret (GetFormatState), the framework's document dialogs
// (UltraCanvasRichTextEditDialogs.h), and PDF export and printing of the pages.
// At the toolbar's right end the view group: the navigator, pages on or off,
// the three fits (page width, whole page, a share of the width with its list)
// and the zoom. At its left, in a resizable side panel, the navigator
// (UltraCanvasDocumentNavigator.h): its tool or F5 shows and hides it, and
// the window's settings remember the choice and its width, the fit last
// chosen and the share. A new document starts with the Settings window's
// font, size and page.
//
// Version: 0.5.0 - Home fits beside the view group at 1280 px: narrower style, font and size lists,
//                  the lists and the alignment each one slot (a tool group), Paragraph and Styles as
//                  icons; Insert's table, picture and link and Review's comment tools as icons
// Version: 0.4.0 - the view group (navigator, pages, page width, whole page, width share with its list,
//                  zoom out, the zoom level, zoom in) replaces the View tab; the fit and the share are
//                  remembered; a new document's font, size and page from the settings; the author name
// Version: 0.3.0 - the navigator: headings, tables, pictures, charts, comments, bookmarks, notes and changes in a
//                  side panel (View > Navigator, F5), remembered in the settings
// Version: 0.2.0 - the document dialogs: paragraph and styles (Home), contents, caption,
//                  cross-reference and footnote (Insert), page setup, table and picture
//                  (Layout, a new tab), comments and tracked changes (Review, a new tab)
// Version: 0.1.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraOfficeHost.h"
#include "UltraOfficeSettings.h"
#include "UltraOfficeToolbar.h"

#include "IODeviceManager/UltraCanvasIODevicePrintDialog.h"   // PrintDocumentWithDialog
#include "UltraCanvasAppSettings.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasDocumentNavigator.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasRichDocument.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasRichTextEditDialogs.h"
#include "UltraCanvasRichTextPrint.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace UltraOffice {

using namespace UltraCanvas;

namespace {

// The lists a new tab starts with; a font or size a document uses that is
// not here joins its tab's list when the caret first reaches it. The empty
// family is "Default font": text with no font of its own.
const std::vector<std::string> kFonts = OfficeSettings::FontChoices();
const std::vector<float> kSizes = OfficeSettings::FontSizeChoices();

// "10.5", whatever the locale: the list reads the same as the size boxes
// of the documents it came from.
std::string SizeText(float size) {
    char text[32];
    const auto result = std::to_chars(text, text + sizeof text, size);
    return std::string(text, result.ptr);
}

// What the window's settings keep of the navigator's width (whether it is
// shown is OfficeSettings::GetShowNavigator).
const char* kNavigatorWidthKey = "documentNavigatorWidth";
constexpr int kNavigatorWidth = 260;
constexpr int kNavigatorMinWidth = 160;

// A pane of the split: a column its one element fills.
void FillPane(const std::shared_ptr<UltraCanvasContainer>& pane, const std::shared_ptr<UltraCanvasUIElement>& element) {
    pane->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    pane->SetContainerStyle(plain);
    pane->SetFollowTheme(true);
    pane->AddChild(element);
    element->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    element->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
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
        // The zoom box follows a fit worked out again, Ctrl+wheel, the keys -
        // once the frame that changed it is drawn: a fit is worked out while
        // the editor is laid out, and a redraw asked for then is lost.
        edit->onZoomChanged = [this, alive = std::weak_ptr<bool>(alive_)](float) {
            auto* app = UltraCanvasApplication::GetInstance();
            if (!app) { SyncView(); return; }
            if (zoomSyncPending_) return;
            zoomSyncPending_ = true;
            app->PostToUIThread([this, alive]() {
                if (alive.expired()) return;
                zoomSyncPending_ = false;
                SyncView();
            });
        };
        // Comments and tracked changes are signed with the author name of
        // the settings, the login name until one is given (UTF-8, as the
        // document stores it); a double-click on a comment edits it.
        edit->SetCommentAuthor(OfficeSettings::LoginName());
        edit->onCommentActivated = [this, edit](int index) {
            ShowCommentDialog(*edit, index, [this](bool) { Sync(); });
        };
        // The document fills a split whose left pane, when shown, is the
        // navigator over the same element.
        split_ = std::make_shared<UltraCanvasSplitPane>("office-document-split", SplitOrientation::Horizontal);
        split_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        FillPane(split_->AddPane(1.0), editor_);
        split_->onSplitterDragEnd = [this](size_t) { RememberNavigatorWidth(); };
        navigator_ = std::make_shared<UltraCanvasDocumentNavigator>("office-navigator", 0, 0, kNavigatorWidth, 600);
        navigator_->SetEdit(editor_);
        BuildHome();
        BuildInsert();
        BuildLayout();
        BuildReview();
        BuildViewGroup();
        NewBlank();
    }

    OfficeKind GetKind() const override { return OfficeKind::Document; }
    std::shared_ptr<UltraCanvasUIElement> GetContent() override { return split_; }

    void AttachSettings(UltraCanvasAppSettings* settings) override {
        settings_ = settings;
        if (!settings_) return;
        const OfficeSettings prefs(*settings_);
        ShowNavigator(prefs.GetShowNavigator(), false);
        ApplyFit(prefs.GetDocumentFit(), prefs.GetWidthShare());
        ApplySettings();
    }

    void ApplySettings() override {
        if (!settings_) return;
        const OfficeSettings prefs(*settings_);
        editor_->SetCommentAuthor(prefs.EffectiveAuthorName());
        if (prefs.GetShowNavigator() != navigatorShown_) ShowNavigator(!navigatorShown_, false);
        // A fit or share the Settings window changed (not one this tab
        // already shows: a zoom of its own stays).
        if (prefs.GetDocumentFit() != appliedFit_ || std::abs(prefs.GetWidthShare() - share_) > 0.001f)
            ApplyFit(prefs.GetDocumentFit(), prefs.GetWidthShare());
        SyncView();
    }

    std::shared_ptr<UltraCanvasToolbar> GetViewToolbar() override { return viewBar_; }
    void ZoomIn() override { editor_->SetZoom(std::min(5.0f, editor_->GetZoom() * 1.1f)); SyncView(); }
    void ZoomOut() override { editor_->SetZoom(std::max(0.25f, editor_->GetZoom() / 1.1f)); SyncView(); }
    void ZoomToActualSize() override { editor_->SetZoom(1.0f); SyncView(); }

    // F5 shows and hides the navigator, as in LibreOffice Writer.
    bool HandleShortcut(const UCEvent& event) override {
        if (event.virtualKey != UCKeys::F5 || event.ctrl || event.alt || event.shift) return false;
        ToggleNavigator();
        return true;
    }
    const std::vector<CommandTab>& GetCommandTabs() override { return tabs_; }

    // A new document: the page, the font and the size of the settings,
    // stated in the document (its page and its Normal style), so another
    // program opens it the same.
    void NewBlank() override {
        auto document = std::make_shared<UCRichDocument>();
        RichTextEditStyle style = editor_->GetStyle();
        style.baseFont.fontFamily = "";
        style.baseFont.fontSize = kEditorBaseSize;
        if (settings_) {
            const OfficeSettings prefs(*settings_);
            RichPageSetup& page = document->page;
            if (prefs.GetPageSize() == PageSizeChoice::Letter) {
                page.widthPt = 612.0f;    // 8.5 x 11 inches, one-inch margins
                page.heightPt = 792.0f;
                page.marginTopPt = page.marginBottomPt = page.marginLeftPt = page.marginRightPt = 72.0f;
                page.headerTopPt = page.footerBottomPt = 36.0f;
            } else {
                page.widthPt = 595.28f;   // A4, 2 cm margins
                page.heightPt = 841.89f;
                page.marginTopPt = page.marginBottomPt = page.marginLeftPt = page.marginRightPt = 56.69f;
                page.headerTopPt = page.footerBottomPt = 35.43f;
            }
            const std::string family = prefs.GetDocumentFont();
            const float size = prefs.GetDocumentFontSize();
            document->styles = UCRichDocument::DefaultStyles();
            for (RichStyle& named : document->styles) {
                if (named.id != "Normal") continue;
                if (!family.empty()) named.character.fontFamily = family;
                named.character.fontSizePt = size;
            }
            style.baseFont.fontFamily = family;
            style.baseFont.fontSize = size;
        }
        SetBaseStyle(style);
        editor_->SetDocument(document);
        editor_->SetModified(false);
        Sync();
    }

    bool Load(const std::string& path, const std::string& password, std::string& error) override {
        auto document = UltraCanvasFileLoader::LoadTextDocument(path, error, password);
        if (!document) return false;
        // A file's text with no font of its own keeps the editor's, not the
        // one the settings give a new document.
        RichTextEditStyle style = editor_->GetStyle();
        style.baseFont.fontFamily = "";
        style.baseFont.fontSize = kEditorBaseSize;
        SetBaseStyle(style);
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
    void Focus() override {
        // Another tab may have shown or hidden the navigator meanwhile: this
        // one follows the choice when it comes forward.
        if (settings_ && OfficeSettings(*settings_).GetShowNavigator() != navigatorShown_)
            ShowNavigator(!navigatorShown_, false);
        editor_->SetFocus(true);
    }

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
    std::shared_ptr<UltraCanvasSplitPane> split_;
    std::shared_ptr<UltraCanvasDocumentNavigator> navigator_;
    std::shared_ptr<UltraCanvasContainer> navigatorPane_;     // the split's left pane while it is shown
    UltraCanvasAppSettings* settings_ = nullptr;              // the window's
    bool navigatorShown_ = false;
    UltraCanvasButton* navigatorButton_ = nullptr;
    std::vector<CommandTab> tabs_;
    // Raw: the toolbars own them, and the host owns the toolbars.
    UltraCanvasButton* bold_ = nullptr;
    UltraCanvasButton* italic_ = nullptr;
    UltraCanvasButton* underline_ = nullptr;
    UltraCanvasButton* strike_ = nullptr;
    UltraCanvasButton* pageView_ = nullptr;
    // The view group, at the toolbar's right end.
    std::shared_ptr<UltraCanvasToolbar> viewBar_;
    UltraCanvasButton* fitWidth_ = nullptr;
    UltraCanvasButton* fitPage_ = nullptr;
    UltraCanvasButton* fitShare_ = nullptr;
    UltraCanvasDropdown* shareBox_ = nullptr;
    UltraCanvasDropdown* zoomBox_ = nullptr;
    std::vector<int> shareChoices_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);   // a task posted to the UI thread checks it
    bool zoomSyncPending_ = false;
    float share_ = 0.6f;   // the width share's, until the settings give theirs
    DocumentFit appliedFit_ = DocumentFit::PageWidth;   // the settings' fit this tab took last
    static constexpr double kEditorBaseSize = 14.0;   // the editor's own base size

    // The editor's base font, its colours still the theme's.
    void SetBaseStyle(const RichTextEditStyle& style) {
        const RichTextEditStyle& now = editor_->GetStyle();
        if (now.baseFont.fontFamily == style.baseFont.fontFamily && now.baseFont.fontSize == style.baseFont.fontSize) return;
        editor_->SetStyle(style);
        editor_->SetFollowTheme(true);
    }
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
        style->size.width = CSSLayout::Dimension::Px(120);
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
        font->size.width = CSSLayout::Dimension::Px(130);
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
        size->size.width = CSSLayout::Dimension::Px(56);
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

        // The lists and the alignment: one slot each, doing the tool picked
        // last; resting the pointer on it shows the others.
        AddActionGroup(*bar, "doc-lists", {
            {"doc-bullets", "ultramail/list-bullets.svg", Tr("Bulleted list"), Act([](auto& e) { e.ToggleBulletList(); })},
            {"doc-numbers", "ultramail/list-numbers.svg", Tr("Numbered list"), Act([](auto& e) { e.ToggleNumberedList(); })},
            {"doc-outdent", "office/outdent.svg", Tr("Decrease indent (Shift+Tab)"), Act([](auto& e) { e.OutdentList(); })},
            {"doc-indent", "office/indent.svg", Tr("Increase indent (Tab)"), Act([](auto& e) { e.IndentList(); })}});
        AddActionGroup(*bar, "doc-align", {
            {"doc-left", "textformat/align-left.svg", Tr("Align left"), Act([](auto& e) { e.SetAlignment(RichTextAlign::Left); })},
            {"doc-centre", "textformat/align-centre.svg", Tr("Centre"), Act([](auto& e) { e.SetAlignment(RichTextAlign::Center); })},
            {"doc-right", "textformat/align-right.svg", Tr("Align right"), Act([](auto& e) { e.SetAlignment(RichTextAlign::Right); })},
            {"doc-justify", "office/justify.svg", Tr("Justify"), Act([](auto& e) { e.SetAlignment(RichTextAlign::Justify); })}});
        bar->AddSeparator();
        AddTool(*bar, "doc-paragraph", "", "textformat/line-spacing.svg",
                Tr("Paragraph: indents, spacing, line spacing, alignment and tab stops"),
                Dialog([](auto& e, RichDialogClosed done) { ShowParagraphDialog(e, std::move(done)); }));
        AddTool(*bar, "doc-styles", "", "menu/palette.svg",
                Tr("Styles: apply, change or make a named style"),
                Dialog([](auto& e, RichDialogClosed done) { ShowStylesDialog(e, std::move(done)); }));
        tabs_.push_back({Tr("Home"), bar});
    }

    void BuildInsert() {
        auto bar = MakeToolbar("office-document-insert");
        AddTool(*bar, "doc-table", "", "table.svg", Tr("Table: insert a table of three rows and three columns"),
                Act([](auto& e) { e.InsertTable(3, 3, true); }));
        AddTool(*bar, "doc-picture", "", "image.svg", Tr("Picture: insert a picture from a file"), [this]() { InsertPicture(); });
        AddTool(*bar, "doc-link", "", "link.svg", Tr("Link: make the selection a link"), [this]() { InsertLink(); });
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
        AddTool(*bar, "doc-comment", "", "clipboard/edit.svg", Tr("Comment: comment on the selection"),
                Dialog([](auto& e, RichDialogClosed done) { ShowCommentDialog(e, -1, std::move(done)); }));
        AddTool(*bar, "doc-prevcomment", "", "texter/angle-left.svg", Tr("Previous comment"), Act([](auto& e) { e.GoToPreviousComment(); }));
        AddTool(*bar, "doc-nextcomment", "", "texter/angle-right.svg", Tr("Next comment"), Act([](auto& e) { e.GoToNextComment(); }));
        AddTool(*bar, "doc-comments", "", "list.svg", Tr("All comments: the comments of the document: go to, edit, resolve or delete one"),
                Dialog([](auto& e, RichDialogClosed done) { ShowCommentsDialog(e, std::move(done)); }));
        showComments_ = AddToggleTool(*bar, "doc-showcomments", Tr("Show comments"), "", Tr("Show the comments and their pane"),
                                      Act([](auto& e) { e.SetShowComments(!e.IsShowingComments()); })).get();
        bar->AddSeparator();
        trackChanges_ = AddToggleTool(*bar, "doc-track", Tr("Track changes"), "edit.svg", Tr("Mark what is typed and deleted"),
                                      Act([](auto& e) { e.SetTrackChanges(!e.IsTrackingChanges()); })).get();
        AddTool(*bar, "doc-prevchange", "", "arrow-left.svg", Tr("Previous change"), Act([](auto& e) { e.GoToPreviousChange(); }));
        AddTool(*bar, "doc-nextchange", "", "arrow-right.svg", Tr("Next change"), Act([](auto& e) { e.GoToNextChange(); }));
        AddTool(*bar, "doc-accept", "", "ultramail/check.svg", Tr("Accept: accept the change at the caret, or those selected"),
                Act([](auto& e) { e.AcceptChangeAtCaret(); }));
        AddTool(*bar, "doc-reject", "", "delete.svg", Tr("Reject: reject the change at the caret, or those selected"),
                Act([](auto& e) { e.RejectChangeAtCaret(); }));
        AddTool(*bar, "doc-acceptall", Tr("Accept all"), "", Tr("Accept every change"), Act([](auto& e) { e.AcceptAllChanges(); }));
        AddTool(*bar, "doc-rejectall", Tr("Reject all"), "", Tr("Reject every change"), Act([](auto& e) { e.RejectAllChanges(); }));
        tabs_.push_back({Tr("Review"), bar});
    }

    // ===== THE NAVIGATOR =====
    void ShowNavigator(bool show, bool remember) {
        if (show != navigatorShown_) {
            navigatorShown_ = show;
            if (show) {
                const int width = settings_ ? settings_->GetInt(kNavigatorWidthKey, kNavigatorWidth) : kNavigatorWidth;
                navigatorPane_ = split_->InsertPane(0, 0.25);
                FillPane(navigatorPane_, navigator_);
                split_->SetPaneMinSize(0, kNavigatorMinWidth);
                split_->SetPaneFixedSize(0, std::max(kNavigatorMinWidth, width));
                navigator_->Refresh();
            } else if (navigatorPane_) {
                navigatorPane_->RemoveChild(navigator_);
                split_->RemovePane(navigatorPane_.get());
                navigatorPane_.reset();
            }
            split_->InvalidateLayout();
            split_->RequestRedraw();
        }
        if (remember && settings_) {
            OfficeSettings(*settings_).SetShowNavigator(show);
            settings_->Save();
        }
        if (navigatorButton_) navigatorButton_->SetPressed(navigatorShown_);
    }

    void ToggleNavigator() {
        ShowNavigator(!navigatorShown_, true);
        editor_->SetFocus(true);
    }

    void RememberNavigatorWidth() {
        if (!settings_ || !navigatorShown_) return;
        const int width = split_->GetPaneFixedSize(0);
        if (width <= 0) return;
        settings_->Set(kNavigatorWidthKey, width);
        settings_->Save();
    }

    // ===== THE VIEW GROUP =====
    // navigator | pages | page width | whole page | text width [share] |
    // zoom out | [zoom] | zoom in
    void BuildViewGroup() {
        viewBar_ = MakeViewToolbar("office-document-view");
        viewBar_->AddSeparator();
        navigatorButton_ = AddToggleTool(*viewBar_, "doc-navigator", "", "menu/sidebar.svg",
                                         Tr("Navigator: show the headings, tables, pictures and comments beside the document (F5)"),
                                         [this]() { ToggleNavigator(); }).get();
        pageView_ = AddToggleTool(*viewBar_, "doc-pageview", "", "office/view-pages.svg",
                                  Tr("Pages: show the document as pages"),
                                  Act([](auto& e) { e.SetPageView(!e.IsPageView()); })).get();
        viewBar_->AddSeparator();
        fitWidth_ = AddToggleTool(*viewBar_, "doc-fitwidth", "", "office/fit-page-width.svg",
                                  Tr("Page width: zoom to the width of the page"),
                                  [this]() { ChooseFit(DocumentFit::PageWidth); }).get();
        fitPage_ = AddToggleTool(*viewBar_, "doc-fitpage", "", "office/fit-whole-page.svg",
                                 Tr("Whole page: zoom to show the whole page"),
                                 [this]() { ChooseFit(DocumentFit::WholePage); }).get();
        fitShare_ = AddToggleTool(*viewBar_, "doc-fitshare", "", "office/fit-width-share.svg",
                                  Tr("Text width: the page fills the share of the width chosen beside it"),
                                  [this]() { ChooseFit(DocumentFit::WidthShare); }).get();
        for (float share : OfficeSettings::WidthShareChoices()) shareChoices_.push_back(static_cast<int>(std::lround(share * 100.0f)));
        shareBox_ = AddPercentBox(*viewBar_, "doc-share", shareChoices_, 10, 100,
                                  Tr("Share of the width for Text width"), Tr("Text width"), window_,
                                  [this](int percent) { ChooseShare(static_cast<float>(percent) / 100.0f); }).get();
        viewBar_->AddSeparator();
        AddTool(*viewBar_, "doc-zoomout", "", "menu/zoom-out.svg", Tr("Zoom out (Ctrl+-)"), [this]() { ZoomOut(); Refocus(); });
        zoomBox_ = AddPercentBox(*viewBar_, "doc-zoom", ZoomLevelChoices(), 25, 500,
                                 Tr("Zoom level (Ctrl+0: 100 %)"), Tr("Zoom"), window_, [this](int percent) {
                                     editor_->SetZoom(static_cast<float>(percent) / 100.0f);
                                     SyncView();
                                     Refocus();
                                 }).get();
        AddTool(*viewBar_, "doc-zoomin", "", "menu/zoom-in.svg", Tr("Zoom in (Ctrl++)"), [this]() { ZoomIn(); Refocus(); });
        SyncView();
    }

    void Refocus() { editor_->SetFocus(true); }

    // A fit the editor keeps as the window is resized; 100 % is none.
    void ApplyFit(DocumentFit fit, float share) {
        share_ = share;
        appliedFit_ = fit;
        switch (fit) {
            case DocumentFit::Actual: editor_->SetZoom(1.0f); break;
            case DocumentFit::WholePage: editor_->SetZoomFit(UltraCanvasRichTextEdit::ZoomFit::WholePage); break;
            case DocumentFit::WidthShare: editor_->ZoomToWidthShare(share); break;
            default: editor_->SetZoomFit(UltraCanvasRichTextEdit::ZoomFit::PageWidth); break;
        }
        SyncView();
    }

    // A fit chosen in the view group: this tab's, and every tab's opened
    // after it, the next run's too.
    void ChooseFit(DocumentFit fit) {
        ApplyFit(fit, share_);
        if (settings_) {
            OfficeSettings(*settings_).SetDocumentFit(fit);
            settings_->Save();
        }
        Refocus();
    }

    void ChooseShare(float share) {
        if (settings_) {
            OfficeSettings prefs(*settings_);
            prefs.SetWidthShare(share);
            share = prefs.GetWidthShare();
        }
        share_ = share;
        ChooseFit(DocumentFit::WidthShare);
    }

    // The view group as the editor is.
    void SyncView() {
        using Fit = UltraCanvasRichTextEdit::ZoomFit;
        const Fit fit = editor_->GetZoomFit();
        if (pageView_) pageView_->SetPressed(editor_->IsPageView());
        if (navigatorButton_) navigatorButton_->SetPressed(navigatorShown_);
        if (fitWidth_) fitWidth_->SetPressed(fit == Fit::PageWidth);
        if (fitPage_) fitPage_->SetPressed(fit == Fit::WholePage);
        if (fitShare_) fitShare_->SetPressed(fit == Fit::WidthShare);
        if (shareBox_) ShowPercent(*shareBox_, shareChoices_, static_cast<int>(std::lround(share_ * 100.0f)));
        if (zoomBox_) ShowPercent(*zoomBox_, ZoomLevelChoices(), static_cast<int>(std::lround(editor_->GetZoom() * 100.0f)));
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
        SyncView();
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
