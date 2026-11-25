// Apps/UltraOffice/UltraOfficeDocumentHost.cpp
// A word-processing tab: the framework's WYSIWYG element
// (UltraCanvasRichTextEdit) over a UCRichDocument, read and written through
// UltraCanvasFileLoader (.odt, .docx, .doc read-only, .md, .tex), with three
// command tabs - Home, Insert, View - whose toolbars follow the caret
// (GetFormatState), and PDF export and printing of the pages.
//
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
#include "UltraCanvasRichTextPrint.h"
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
        BuildHome();
        BuildInsert();
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

    bool Save(const std::string& path, const std::string& password, std::string& error) override {
        const auto& document = editor_->GetDocument();
        if (!document) { error = Tr("There is no document to save."); return false; }
        if (DetectWordDocumentFormat(path) == WordDocumentFormat::LegacyDoc ||
            WordDocumentFormatFromExtension(path) == WordDocumentFormat::LegacyDoc) {
            error = Tr("Word 97-2003 (.doc) files are read, not written. Save as .docx or .odt instead.");
            return false;
        }
        if (!UltraCanvasFileLoader::SaveTextDocument(*document, path, error, password)) return false;
        editor_->SetModified(false);
        return true;
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
        AddTool(*bar, "doc-footnote", Tr("Footnote"), "", Tr("Insert a footnote at the caret"), Act([](auto& e) { e.InsertFootnote(); }));
        AddTool(*bar, "doc-toc", Tr("Contents"), "", Tr("Insert a table of contents of the headings"),
                Act([](auto& e) { e.InsertTableOfContents(3); }));
        tabs_.push_back({Tr("Insert"), bar});
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
