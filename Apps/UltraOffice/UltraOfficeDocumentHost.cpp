// Apps/UltraOffice/UltraOfficeDocumentHost.cpp
// A word-processing tab: the framework's WYSIWYG element
// (UltraCanvasRichTextEdit) over a UCRichDocument, read and written through
// UltraCanvasFileLoader (.odt, .docx, .doc read-only, .md, .tex), with four
// command tabs - Home, Insert, Layout, Review - whose toolbars follow the
// caret (GetFormatState), the framework's document dialogs
// (UltraCanvasRichTextEditDialogs.h), and PDF export and printing of the pages.
// At the toolbar's right end the view group: the navigator, pages on or off,
// the three fits (page width, whole page, a share of the width with its list)
// and the zoom. At the document's right, in a resizable side panel, the navigator
// (UltraCanvasDocumentNavigator.h): its tool or F5 shows and hides it, and
// the window's settings remember the choice and its width, the fit last
// chosen and the share. A new document starts with the Settings window's
// font, size and page.
//
// Version: 0.7.0 - the tools are one table (UltraOfficeTools.h) the command tabs and the view group are
//                  built from, with no change to what they show; the panels' own tools in it too:
//                  superscript, subscript, text colour, highlight, line spacing, space before and after,
//                  the indents, and the table's rows, columns, header row and borders
// Version: 0.6.0 - the font list is the framework's UltraCanvasFontPicker: every installed font, each
//                  in itself, the document's fonts and the recent ones on top, a family's styles beside
//                  it (a style sets bold and italic, a light or condensed one its face's family name)
// Version: 0.5.0 - Home fits beside the view group at 1280 px: narrower style, font and size lists,
//                  the lists and the alignment each one slot (a tool group), Paragraph and Styles as
//                  icons; Insert's table, picture and link and Review's comment tools as icons
// Version: 0.5.0 - the navigator at the document's right
// Version: 0.4.0 - the view group (navigator, pages, page width, whole page, width share with its list,
//                  zoom out, the zoom level, zoom in) replaces the View tab; the fit and the share are
//                  remembered; a new document's font, size and page from the settings; the author name
// Version: 0.3.0 - the navigator: headings, tables, pictures, charts, comments, bookmarks, notes and changes in a
//                  side panel (View > Navigator, F5), remembered in the settings
// Version: 0.2.0 - the document dialogs: paragraph and styles (Home), contents, caption,
//                  cross-reference and footnote (Insert), page setup, table and picture
//                  (Layout, a new tab), comments and tracked changes (Review, a new tab)
// Version: 0.1.0
// Last Modified: 2026-11-27
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraOfficeHost.h"
#include "UltraOfficeSettings.h"
#include "UltraOfficeToolbar.h"
#include "UltraOfficeTools.h"

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
#include <optional>

namespace UltraOffice {

using namespace UltraCanvas;

namespace {

// The sizes a new tab's list starts with; a size a document uses that is
// not here joins its tab's list when the caret first reaches it. (The font
// list is every installed font; the empty family is "Default font": text
// with no font of its own.)
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
constexpr size_t kNavigatorPane = 1;   // the split's panes: the document, then the navigator

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
        // The document fills a split whose right pane, when shown, is the
        // navigator over the same element.
        split_ = std::make_shared<UltraCanvasSplitPane>("office-document-split", SplitOrientation::Horizontal);
        split_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        FillPane(split_->AddPane(1.0), editor_);
        split_->onSplitterDragEnd = [this](size_t) { RememberNavigatorWidth(); };
        navigator_ = std::make_shared<UltraCanvasDocumentNavigator>("office-navigator", 0, 0, kNavigatorWidth, 600);
        navigator_->SetEdit(editor_);
        BuildTools();
        bars_.Build(tools_, window_);
        SyncView();
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

    std::shared_ptr<UltraCanvasToolbar> GetViewToolbar() override { return bars_.ViewBar(); }
    void ZoomIn() override { editor_->SetZoom(std::min(5.0f, editor_->GetZoom() * 1.1f)); SyncView(); }
    void ZoomOut() override { editor_->SetZoom(std::max(0.25f, editor_->GetZoom() / 1.1f)); SyncView(); }
    void ZoomToActualSize() override { editor_->SetZoom(1.0f); SyncView(); }

    // F5 shows and hides the navigator, as in LibreOffice Writer.
    bool HandleShortcut(const UCEvent& event) override {
        if (event.virtualKey != UCKeys::F5 || event.ctrl || event.alt || event.shift) return false;
        ToggleNavigator();
        return true;
    }
    const ToolTable& GetTools() const override { return tools_; }
    const std::vector<CommandTab>& GetCommandTabs() override { return bars_.Tabs(); }

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
    std::shared_ptr<UltraCanvasContainer> navigatorPane_;     // the split's right pane while it is shown
    UltraCanvasAppSettings* settings_ = nullptr;              // the window's
    bool navigatorShown_ = false;
    // The tools, and the command tabs' bars and the view group built from them.
    ToolTable tools_;
    ToolBars bars_;
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
    std::vector<float> sizes_ = kSizes;   // the size list's: kSizes and the sizes the document uses

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

    // ===== THE TOOLS =====
    // Every tool of a document, once (UltraOfficeTools.h): the command tabs'
    // bars and the view group are built from it, and the tool panels will be.
    void BuildTools() {
        using P = ToolPanel;
        tools_.kind = OfficeKind::Document;
        UltraCanvasRichTextEdit* edit = editor_.get();
        tools_.inContext = [edit](ToolContext context) {
            switch (context) {
                case ToolContext::InTable: return edit->IsCaretInTable();
                case ToolContext::PictureSelected: return edit->HasSelectedImage();
                default: return false;
            }
        };
        auto state = [edit]() { return edit->GetFormatState(); };
        ToolWriter w(tools_);

        // Home: the clipboard | style, font and size | B I U S | lists and
        // alignment | the paragraph and styles dialogs.
        w.Tab("home", Tr("Home"), "office-document-home");
        w.Button("doc-cut", "menu/cut.svg", Tr("Cut"), Tr("Cut (Ctrl+X)"), P::Clipboard, Act([](auto& e) { e.Cut(); }));
        w.Button("doc-copy", "menu/copy.svg", Tr("Copy"), Tr("Copy (Ctrl+C)"), P::Clipboard, Act([](auto& e) { e.Copy(); }));
        w.Button("doc-paste", "menu/paste.svg", Tr("Paste"), Tr("Paste (Ctrl+V)"), P::Clipboard, Act([](auto& e) { e.Paste(); }));
        w.NextGroup();
        {
            ToolEntry& style = w.List("doc-style", Tr("Paragraph style"), Tr("Paragraph style"), P::Style);
            style.barWidth = 120;
            style.list.items = [] {
                return std::vector<std::string>{Tr("Body text"), Tr("Heading 1"), Tr("Heading 2"), Tr("Heading 3"), Tr("Heading 4")};
            };
            style.list.choose = [this, edit](int index) {
                edit->SetHeadingLevel(std::max(0, index));
                Sync();
                edit->SetFocus(true);
            };
            style.list.selected = [edit]() {
                const int level = edit->GetCurrentHeadingLevel();
                return level >= 0 && level <= 4 ? level : 0;
            };
        }
        {
            // Every installed font, each in itself; on top the fonts this
            // document uses and the ones chosen last (the window's settings).
            ToolEntry& font = w.List("doc-font", Tr("Font"), Tr("Font"), P::Font, ToolListStyle::Font);
            font.list.settings = [this]() { return settings_; };
            font.list.documentFonts = [edit]() { return edit->GetFontReport().namedFamilies; };
            font.list.defaultFontEntry = Tr("Default font");
            font.list.chooseFont = [this, edit](const FontPickerChoice& choice) {
                ApplyFontChoice(*edit, choice);
                Sync();
                edit->SetFocus(true);
            };
            // Several fonts show none; text with none of its own shows the
            // editor's base font.
            font.list.currentFont = [edit]() -> std::optional<std::string> {
                const RichCharFormatState now = edit->GetFormatState();
                if (now.fontFamilyMixed) return std::nullopt;
                return now.fontFamily.empty() ? edit->GetStyle().baseFont.fontFamily : now.fontFamily;
            };
        }
        {
            ToolEntry& size = w.List("doc-size", Tr("Font size"), Tr("Font size"), P::Font);
            size.barWidth = 56;
            size.list.items = [this]() {
                std::vector<std::string> names;
                for (float s : sizes_) names.push_back(SizeText(s));
                return names;
            };
            size.list.choose = [this, edit](int index) {
                if (index < static_cast<int>(sizes_.size())) edit->SetFontSize(sizes_[static_cast<size_t>(index)]);
                Sync();
                edit->SetFocus(true);
            };
            // A size the list lacks joins it; several sizes show none. Text
            // with none of its own shows the base size in body text (a
            // heading scales it).
            size.list.selected = [this, edit]() {
                const RichCharFormatState now = edit->GetFormatState();
                float sizePt = now.fontSizePt;
                if (sizePt <= 0.0f && !now.fontSizeMixed && edit->GetCurrentHeadingLevel() == 0)
                    sizePt = static_cast<float>(edit->GetStyle().baseFont.fontSize);
                if (now.fontSizeMixed || sizePt <= 0.0f) return -1;
                auto it = std::find(sizes_.begin(), sizes_.end(), sizePt);
                if (it == sizes_.end()) it = sizes_.insert(std::upper_bound(sizes_.begin(), sizes_.end(), sizePt), sizePt);
                return static_cast<int>(it - sizes_.begin());
            };
        }
        w.NextGroup();
        w.Toggle("doc-bold", "textformat/bold.svg", Tr("Bold"), Tr("Bold (Ctrl+B)"), P::Font, Act([](auto& e) { e.ToggleBold(); }),
                 [state]() { return RichCharFormatState::IsOn(state().bold); });
        w.Toggle("doc-italic", "textformat/italic.svg", Tr("Italic"), Tr("Italic (Ctrl+I)"), P::Font,
                 Act([](auto& e) { e.ToggleItalic(); }), [state]() { return RichCharFormatState::IsOn(state().italic); });
        w.Toggle("doc-underline", "textformat/underline.svg", Tr("Underline"), Tr("Underline (Ctrl+U)"), P::Font,
                 Act([](auto& e) { e.ToggleUnderline(); }), [state]() { return RichCharFormatState::IsOn(state().underline); });
        w.Toggle("doc-strike", "textformat/strike.svg", Tr("Strikethrough"), Tr("Strikethrough"), P::Font,
                 Act([](auto& e) { e.ToggleStrikethrough(); }), [state]() { return RichCharFormatState::IsOn(state().strikethrough); });
        w.NextGroup();
        // The lists and the alignment: one slot each, doing the tool picked
        // last; resting the pointer on it shows the others.
        w.ActionGroup("doc-lists", Tr("Lists"), P::Lists, {
            {"doc-bullets", "ultramail/list-bullets.svg", Tr("Bulleted list"), Tr("Bulleted list"), Act([](auto& e) { e.ToggleBulletList(); })},
            {"doc-numbers", "ultramail/list-numbers.svg", Tr("Numbered list"), Tr("Numbered list"), Act([](auto& e) { e.ToggleNumberedList(); })},
            {"doc-outdent", "office/outdent.svg", Tr("Decrease indent"), Tr("Decrease indent (Shift+Tab)"), Act([](auto& e) { e.OutdentList(); })},
            {"doc-indent", "office/indent.svg", Tr("Increase indent"), Tr("Increase indent (Tab)"), Act([](auto& e) { e.IndentList(); })}});
        w.ActionGroup("doc-align", Tr("Alignment"), P::Paragraph, {
            {"doc-left", "textformat/align-left.svg", Tr("Align left"), Tr("Align left"), Act([](auto& e) { e.SetAlignment(RichTextAlign::Left); })},
            {"doc-centre", "textformat/align-centre.svg", Tr("Centre"), Tr("Centre"), Act([](auto& e) { e.SetAlignment(RichTextAlign::Center); })},
            {"doc-right", "textformat/align-right.svg", Tr("Align right"), Tr("Align right"), Act([](auto& e) { e.SetAlignment(RichTextAlign::Right); })},
            {"doc-justify", "office/justify.svg", Tr("Justify"), Tr("Justify"), Act([](auto& e) { e.SetAlignment(RichTextAlign::Justify); })}});
        w.NextGroup();
        w.Button("doc-paragraph", "textformat/line-spacing.svg", Tr("Paragraph"),
                 Tr("Paragraph: indents, spacing, line spacing, alignment and tab stops"), P::Paragraph,
                 Dialog([](auto& e, RichDialogClosed done) { ShowParagraphDialog(e, std::move(done)); }));
        w.Button("doc-styles", "menu/palette.svg", Tr("Styles"), Tr("Styles: apply, change or make a named style"), P::Style,
                 Dialog([](auto& e, RichDialogClosed done) { ShowStylesDialog(e, std::move(done)); }));

        // Insert: table, picture, link | page break | notes, contents,
        // captions and cross-references.
        w.Tab("insert", Tr("Insert"), "office-document-insert");
        w.Button("doc-table", "table.svg", Tr("Table"), Tr("Table: insert a table of three rows and three columns"), P::Insert,
                 Act([](auto& e) { e.InsertTable(3, 3, true); }));
        w.Button("doc-picture", "image.svg", Tr("Picture"), Tr("Picture: insert a picture from a file"), P::Insert,
                 [this]() { InsertPicture(); });
        w.Button("doc-link", "link.svg", Tr("Link"), Tr("Link: make the selection a link"), P::Insert, [this]() { InsertLink(); });
        w.NextGroup();
        w.Button("doc-pagebreak", "", Tr("Page break"), Tr("Start a new page here"), P::Insert,
                 Act([](auto& e) { e.InsertPageBreak(); })).labelOnBar = true;
        w.NextGroup();
        w.Button("doc-footnote", "superscript.svg", Tr("Footnote"), Tr("Insert a footnote or an endnote at the caret"), P::Insert,
                 Dialog([](auto& e, RichDialogClosed done) { ShowNoteDialog(e, std::move(done)); })).labelOnBar = true;
        w.Button("doc-toc", "list-tree.svg", Tr("Contents"), Tr("Insert or rebuild a table of contents of the headings"), P::Insert,
                 Dialog([](auto& e, RichDialogClosed done) { ShowContentsDialog(e, std::move(done)); })).labelOnBar = true;
        w.Button("doc-caption", "add_text.svg", Tr("Caption"), Tr("A numbered caption under the picture or table"), P::Insert,
                 Dialog([](auto& e, RichDialogClosed done) { ShowCaptionDialog(e, std::move(done)); })).labelOnBar = true;
        w.Button("doc-crossref", "menu/book.svg", Tr("Cross-reference"), Tr("Refer to a caption or a bookmark by its text or its page"),
                 P::Insert, Dialog([](auto& e, RichDialogClosed done) { ShowCrossReferenceDialog(e, std::move(done)); })).labelOnBar = true;

        // Layout: page setup | the table's and the picture's properties.
        w.Tab("layout", Tr("Layout"), "office-document-layout");
        w.Button("doc-pagesetup", "document.svg", Tr("Page setup"), Tr("Paper, orientation, margins, header and footer"), P::Page,
                 Dialog([](auto& e, RichDialogClosed done) { ShowPageSetupDialog(e, std::move(done)); })).labelOnBar = true;
        w.NextGroup();
        {
            ToolEntry& table = w.Button("doc-tableprops", "table.svg", Tr("Table"),
                                        Tr("The width, alignment, header row and borders of the table"), P::Table,
                                        Dialog([](auto& e, RichDialogClosed done) { ShowTablePropertiesDialog(e, std::move(done)); }));
            table.labelOnBar = true;
            table.context = ToolContext::InTable;
        }
        {
            ToolEntry& picture = w.Button("doc-pictureprops", "image.svg", Tr("Picture"),
                                          Tr("The alt text, wrapping and size of the selected picture"), P::Picture,
                                          Dialog([](auto& e, RichDialogClosed done) { ShowPictureDialog(e, std::move(done)); }));
            picture.labelOnBar = true;
            picture.context = ToolContext::PictureSelected;
        }

        // Review: comments | tracked changes.
        w.Tab("review", Tr("Review"), "office-document-review");
        w.Button("doc-comment", "clipboard/edit.svg", Tr("Comment"), Tr("Comment: comment on the selection"), P::Review,
                 Dialog([](auto& e, RichDialogClosed done) { ShowCommentDialog(e, -1, std::move(done)); }));
        w.Button("doc-prevcomment", "texter/angle-left.svg", Tr("Previous comment"), Tr("Previous comment"), P::Review,
                 Act([](auto& e) { e.GoToPreviousComment(); }));
        w.Button("doc-nextcomment", "texter/angle-right.svg", Tr("Next comment"), Tr("Next comment"), P::Review,
                 Act([](auto& e) { e.GoToNextComment(); }));
        w.Button("doc-comments", "list.svg", Tr("All comments"),
                 Tr("All comments: the comments of the document: go to, edit, resolve or delete one"), P::Review,
                 Dialog([](auto& e, RichDialogClosed done) { ShowCommentsDialog(e, std::move(done)); }));
        w.Toggle("doc-showcomments", "", Tr("Show comments"), Tr("Show the comments and their pane"), P::Review,
                 Act([](auto& e) { e.SetShowComments(!e.IsShowingComments()); }),
                 [edit]() { return edit->IsShowingComments(); }).labelOnBar = true;
        w.NextGroup();
        w.Toggle("doc-track", "edit.svg", Tr("Track changes"), Tr("Mark what is typed and deleted"), P::Review,
                 Act([](auto& e) { e.SetTrackChanges(!e.IsTrackingChanges()); }),
                 [edit]() { return edit->IsTrackingChanges(); }).labelOnBar = true;
        w.Button("doc-prevchange", "arrow-left.svg", Tr("Previous change"), Tr("Previous change"), P::Review,
                 Act([](auto& e) { e.GoToPreviousChange(); }));
        w.Button("doc-nextchange", "arrow-right.svg", Tr("Next change"), Tr("Next change"), P::Review,
                 Act([](auto& e) { e.GoToNextChange(); }));
        w.Button("doc-accept", "ultramail/check.svg", Tr("Accept"), Tr("Accept: accept the change at the caret, or those selected"),
                 P::Review, Act([](auto& e) { e.AcceptChangeAtCaret(); }));
        w.Button("doc-reject", "delete.svg", Tr("Reject"), Tr("Reject: reject the change at the caret, or those selected"),
                 P::Review, Act([](auto& e) { e.RejectChangeAtCaret(); }));
        w.Button("doc-acceptall", "", Tr("Accept all"), Tr("Accept every change"), P::Review,
                 Act([](auto& e) { e.AcceptAllChanges(); })).labelOnBar = true;
        w.Button("doc-rejectall", "", Tr("Reject all"), Tr("Reject every change"), P::Review,
                 Act([](auto& e) { e.RejectAllChanges(); })).labelOnBar = true;

        // The view group: navigator, pages | page width, whole page, text
        // width [share] | zoom out, [zoom], zoom in.
        using Fit = UltraCanvasRichTextEdit::ZoomFit;
        w.ViewGroup("office-document-view");
        w.Toggle("doc-navigator", "menu/sidebar.svg", Tr("Navigator"),
                 Tr("Navigator: show the headings, tables, pictures and comments beside the document (F5)"), P::ViewGroup,
                 [this]() { ToggleNavigator(); }, [this]() { return navigatorShown_; });
        w.Toggle("doc-pageview", "office/view-pages.svg", Tr("Pages"), Tr("Pages: show the document as pages"), P::ViewGroup,
                 Act([](auto& e) { e.SetPageView(!e.IsPageView()); }), [edit]() { return edit->IsPageView(); });
        w.NextGroup();
        w.Toggle("doc-fitwidth", "office/fit-page-width.svg", Tr("Page width"), Tr("Page width: zoom to the width of the page"),
                 P::ViewGroup, [this]() { ChooseFit(DocumentFit::PageWidth); },
                 [edit]() { return edit->GetZoomFit() == Fit::PageWidth; });
        w.Toggle("doc-fitpage", "office/fit-whole-page.svg", Tr("Whole page"), Tr("Whole page: zoom to show the whole page"),
                 P::ViewGroup, [this]() { ChooseFit(DocumentFit::WholePage); },
                 [edit]() { return edit->GetZoomFit() == Fit::WholePage; });
        w.Toggle("doc-fitshare", "office/fit-width-share.svg", Tr("Text width"),
                 Tr("Text width: the page fills the share of the width chosen beside it"), P::ViewGroup,
                 [this]() { ChooseFit(DocumentFit::WidthShare); }, [edit]() { return edit->GetZoomFit() == Fit::WidthShare; });
        {
            ToolEntry& share = w.List("doc-share", Tr("Text width"), Tr("Share of the width for Text width"), P::ViewGroup,
                                      ToolListStyle::Percent);
            for (float choice : OfficeSettings::WidthShareChoices()) share.list.percents.push_back(static_cast<int>(std::lround(choice * 100.0f)));
            share.list.minimum = 10;
            share.list.maximum = 100;
            share.list.askTitle = Tr("Text width");
            share.list.choosePercent = [this](int percent) { ChooseShare(static_cast<float>(percent) / 100.0f); };
            share.list.currentPercent = [this]() { return static_cast<int>(std::lround(share_ * 100.0f)); };
        }
        w.NextGroup();
        w.Button("doc-zoomout", "menu/zoom-out.svg", Tr("Zoom out"), Tr("Zoom out (Ctrl+-)"), P::ViewGroup,
                 [this]() { ZoomOut(); Refocus(); });
        {
            ToolEntry& zoom = w.List("doc-zoom", Tr("Zoom"), Tr("Zoom level (Ctrl+0: 100 %)"), P::ViewGroup, ToolListStyle::Percent);
            zoom.list.percents = ZoomLevelChoices();
            zoom.list.minimum = 25;
            zoom.list.maximum = 500;
            zoom.list.askTitle = Tr("Zoom");
            zoom.list.choosePercent = [this](int percent) {
                editor_->SetZoom(static_cast<float>(percent) / 100.0f);
                SyncView();
                Refocus();
            };
            zoom.list.currentPercent = [edit]() { return static_cast<int>(std::lround(edit->GetZoom() * 100.0f)); };
        }
        w.Button("doc-zoomin", "menu/zoom-in.svg", Tr("Zoom in"), Tr("Zoom in (Ctrl++)"), P::ViewGroup,
                 [this]() { ZoomIn(); Refocus(); });

        // The panels' own: what the bars leave to the dialogs, a value each.
        w.PanelsOnly();
        w.Toggle("doc-superscript", "superscript.svg", Tr("Superscript"), Tr("Superscript"), P::Font,
                 Act([](auto& e) { e.ToggleSuperscript(); }), [state]() { return RichCharFormatState::IsOn(state().superscript); });
        w.Toggle("doc-subscript", "subscript.svg", Tr("Subscript"), Tr("Subscript"), P::Font,
                 Act([](auto& e) { e.ToggleSubscript(); }), [state]() { return RichCharFormatState::IsOn(state().subscript); });
        w.Colour("doc-textcolour", Tr("Text colour"), P::Font, [state]() { return TextColour(state().color, state().colorMixed); },
                 [this, edit](const Color& colour) { edit->SetTextColor(colour.ToHexString()); Sync(); });
        w.Colour("doc-highlight", Tr("Highlight"), P::Font,
                 [state]() { return TextColour(state().highlightColor, state().highlightMixed); },
                 [this, edit](const Color& colour) { edit->SetHighlightColor(colour.ToHexString()); Sync(); });
        // The caret paragraph's spacing and indents (the selection's
        // paragraphs take what is set), in points; line spacing as a
        // multiple of single.
        auto spacing = [this, edit](float RichParagraphFormat::*field, bool unsetBelowZero) {
            ToolValue value;
            value.get = [edit, field, unsetBelowZero]() -> std::optional<double> {
                const float now = edit->GetParagraphFormat().*field;
                if (unsetBelowZero && now < 0.0f) return std::nullopt;   // the view's own spacing
                return now;
            };
            value.set = [this, edit, field](double to) {
                RichParagraphFormat format = edit->GetParagraphFormat();
                format.*field = static_cast<float>(to);
                edit->SetParagraphFormat(format);
                Sync();
            };
            return value;
        };
        w.Number("doc-linespacing", Tr("Line spacing"), Tr("lines"), 0.5, 5.0, 0.05, 2, P::Paragraph,
                 [edit]() -> std::optional<double> {
                     const RichParagraphFormat format = edit->GetParagraphFormat();
                     if (format.lineHeightPt > 0.0f) return std::nullopt;   // a height in points, not a multiple
                     return format.lineSpacing > 0.0f ? format.lineSpacing : 1.0f;
                 },
                 [this, edit](double to) {
                     RichParagraphFormat format = edit->GetParagraphFormat();
                     format.lineSpacing = static_cast<float>(to);
                     format.lineHeightPt = 0.0f;
                     format.lineHeightAtLeast = false;
                     edit->SetParagraphFormat(format);
                     Sync();
                 });
        struct Spacing { const char* id; std::string label; float RichParagraphFormat::*field; double minimum; bool unset; };
        const Spacing spacings[] = {
            {"doc-spacebefore", Tr("Space before"), &RichParagraphFormat::spaceBeforePt, 0.0, true},
            {"doc-spaceafter", Tr("Space after"), &RichParagraphFormat::spaceAfterPt, 0.0, true},
            {"doc-indentleft", Tr("Indent left"), &RichParagraphFormat::leftIndentPt, 0.0, false},
            {"doc-indentright", Tr("Indent right"), &RichParagraphFormat::rightIndentPt, 0.0, false},
            {"doc-indentfirst", Tr("First line"), &RichParagraphFormat::firstLineIndentPt, -288.0, false}};
        for (const Spacing& s : spacings) {
            const ToolValue value = spacing(s.field, s.unset);
            w.Number(s.id, s.label, Tr("pt"), s.minimum, 288.0, 1.0, 1, P::Paragraph, value.get, value.set);
        }
        // The table the caret is in.
        auto inTable = [](ToolEntry& entry) { entry.context = ToolContext::InTable; };
        inTable(w.Button("doc-row-above", "", Tr("Row above"), Tr("A row above this cell"), P::Table,
                         Act([](auto& e) { e.InsertRowAbove(); })));
        inTable(w.Button("doc-row-below", "", Tr("Row below"), Tr("A row below this cell"), P::Table,
                         Act([](auto& e) { e.InsertRowBelow(); })));
        inTable(w.Button("doc-col-left", "", Tr("Column left"), Tr("A column left of this cell"), P::Table,
                         Act([](auto& e) { e.InsertColumnLeft(); })));
        inTable(w.Button("doc-col-right", "", Tr("Column right"), Tr("A column right of this cell"), P::Table,
                         Act([](auto& e) { e.InsertColumnRight(); })));
        inTable(w.Button("doc-row-delete", "", Tr("Delete row"), Tr("Delete this cell's row"), P::Table,
                         Act([](auto& e) { e.DeleteCurrentRow(); })));
        inTable(w.Button("doc-col-delete", "", Tr("Delete column"), Tr("Delete this cell's column"), P::Table,
                         Act([](auto& e) { e.DeleteCurrentColumn(); })));
        inTable(w.Toggle("doc-table-header", "", Tr("Header row"), Tr("The first row is a header row"), P::Table,
                         Act([](auto& e) {
                             RichTableProperties table;
                             if (!e.GetTableProperties(table)) return;
                             table.headerRow = !table.headerRow;
                             e.SetTableProperties(table);
                         }),
                         [edit]() {
                             RichTableProperties table;
                             return edit->GetTableProperties(table) && table.headerRow;
                         }));
        inTable(w.Number("doc-table-border", Tr("Borders"), Tr("pt"), 0.0, 12.0, 0.25, 2, P::Table,
                         [edit]() -> std::optional<double> {
                             RichTableProperties table;
                             if (!edit->GetTableProperties(table) || table.borderWidthPt < 0.0f) return std::nullopt;
                             return table.borderWidthPt;
                         },
                         [this, edit](double to) {
                             RichTableProperties table;
                             if (!edit->GetTableProperties(table)) return;
                             table.borderWidthPt = static_cast<float>(to);
                             edit->SetTableProperties(table);
                             Sync();
                         }));
    }

    // A colour of the format state ("#RRGGBB"): none when there is none, or
    // the selection has several.
    static std::optional<Color> TextColour(const std::string& hex, bool mixed) {
        if (mixed || hex.empty()) return std::nullopt;
        return ParseColor(hex);
    }

    // ===== THE NAVIGATOR =====
    void ShowNavigator(bool show, bool remember) {
        if (show != navigatorShown_) {
            navigatorShown_ = show;
            if (show) {
                const int width = settings_ ? settings_->GetInt(kNavigatorWidthKey, kNavigatorWidth) : kNavigatorWidth;
                navigatorPane_ = split_->AddPane(0.25);   // at the right of the document
                FillPane(navigatorPane_, navigator_);
                split_->SetPaneMinSize(kNavigatorPane, kNavigatorMinWidth);
                split_->SetPaneFixedSize(kNavigatorPane, std::max(kNavigatorMinWidth, width));
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
        bars_.SyncTool("doc-navigator");
    }

    void ToggleNavigator() {
        ShowNavigator(!navigatorShown_, true);
        editor_->SetFocus(true);
    }

    void RememberNavigatorWidth() {
        if (!settings_ || !navigatorShown_) return;
        const int width = split_->GetPaneFixedSize(kNavigatorPane);
        if (width <= 0) return;
        settings_->Set(kNavigatorWidthKey, width);
        settings_->Save();
    }

    // ===== THE VIEW GROUP =====
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
    void SyncView() { bars_.SyncTab(kViewGroupTab); }

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
    void Sync() { bars_.Sync(); }
};

} // namespace

std::unique_ptr<OfficeHost> CreateDocumentHost(UltraCanvasWindowBase* window) {
    return std::make_unique<DocumentHost>(window);
}

} // namespace UltraOffice
