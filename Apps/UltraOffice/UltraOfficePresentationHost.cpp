// Apps/UltraOffice/UltraOfficePresentationHost.cpp
// A presentation tab: the framework's slide editor (UltraCanvasSlideEdit) over
// the deck model both readers and both writers share - the slide strip, the
// slide with its shapes to select, move, resize, turn, order, duplicate and
// delete, new slides from the deck's layouts, text boxes, drawn shapes and
// pictures, and every change undone and redone. A shape's text is edited in
// place, as plain lines, in a text area laid over the shape at the slide's
// zoom (the rich in-place editor comes with Phase 4). The show runs full
// screen in a window of its own; PDF and print draw the same slides.
//
// Version: 0.2.0 - editing: UltraCanvasSlideEdit, text in place, the show, PDF and print
// Version: 0.1.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraOfficeHost.h"
#include "UltraOfficeToolbar.h"

#include "IODeviceManager/UltraCanvasIODevicePrintDialog.h"   // PrintDocumentWithDialog
#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPresentationOdp.h"
#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasPresentationPrint.h"
#include "UltraCanvasSlideEdit.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>

namespace UltraOffice {

using namespace UltraCanvas;

namespace {

constexpr int64_t kInch = 914400;

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
        edit_ = CreateSlideEdit("office-presentation", 0, 0, 800, 600);
        edit_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        content_->AddChild(edit_);
        text_ = std::make_shared<UltraCanvasTextArea>("office-presentation-text", 10, 10);
        text_->SetShowLineNumbers(false);
        text_->SetWordWrap(true);
        text_->SetVisible(false);
        content_->AddChild(text_);

        UltraCanvasSlideEdit* edit = edit_.get();
        edit_->onChange = [this]() { Sync(); if (onChanged) onChanged(); };
        edit_->onSelectionChange = [this]() { CloseText(true); Sync(); if (onChanged) onChanged(); };
        edit_->onEditText = [this](int shape) { OpenText(shape); };
        edit_->onContextMenu = [this, edit](bool inStrip, int, int shape, int, int) {
            (void)edit; (void)inStrip; (void)shape;   // the toolbars carry the commands for now
        };
        UltraCanvasTextArea* text = text_.get();
        text_->onFocusLost = [this]() { CloseText(true); };
        text_->onBeforeKeyDown = [this, text](const UCEvent& event) {
            (void)text;
            if (event.virtualKey == UCKeys::Escape) { CloseText(false); return true; }
            if (event.virtualKey == UCKeys::Return && event.ctrl) { CloseText(true); return true; }
            return false;
        };
        BuildHome();
        BuildInsert();
        BuildArrange();
        BuildShow();
    }

    ~PresentationHost() override {
        if (showWindow_) showWindow_->Close();
    }

    OfficeKind GetKind() const override { return OfficeKind::Presentation; }
    std::shared_ptr<UltraCanvasUIElement> GetContent() override { return content_; }
    const std::vector<CommandTab>& GetCommandTabs() override { return tabs_; }

    void NewBlank() override {
        auto deck = std::make_shared<PptxPresentation>();
        deck->slideWidth = 12192000;   // 16:9
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
        Sync();
    }

    bool Load(const std::string& path, const std::string& password, std::string& error) override {
        auto deck = std::make_shared<PptxPresentation>();
        // With the pictures: they are drawn, and written back on save.
        const bool read = IsOdpExtension(path) ? ReadOdpPresentation(path, *deck, error, password, true)
                                               : ReadPptxPresentation(path, *deck, error, password, true);
        if (!read) return false;
        edit_->SetDeck(deck);
        Sync();
        return true;
    }

    bool Write(const std::string& path, const std::string& password, std::string& error) override {
        CloseText(true);
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
    void Undo() override { CloseText(true); edit_->Editor().Undo(); }
    void Redo() override { CloseText(true); edit_->Editor().Redo(); }
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
    std::shared_ptr<UltraCanvasSlideEdit> edit_;
    std::shared_ptr<UltraCanvasTextArea> text_;
    int textShape_ = -1;      // the shape whose text is open; -1: none
    int textSlide_ = -1;
    std::vector<CommandTab> tabs_;
    UltraCanvasButton* hidden_ = nullptr;
    std::shared_ptr<UltraCanvasDropdown> layouts_;
    std::shared_ptr<UltraCanvasWindow> showWindow_;

    UCSlideDeckEditor& Editor() { return edit_->Editor(); }

    std::function<void()> Act(std::function<void(UCSlideDeckEditor&)> action) {
        return [this, action]() {
            CloseText(true);
            action(Editor());
            edit_->SetFocus(true);
        };
    }

    // ===== TEXT IN PLACE =====

    void OpenText(int shape) {
        CloseText(true);
        const PptxSlide* slide = Editor().CurrentSlide();
        if (!slide || shape < 0 || shape >= static_cast<int>(slide->shapes.size())) return;
        const PptxShape& target = slide->shapes[static_cast<size_t>(shape)];
        const Rect2Dd at = edit_->SlideRect();
        const double emu = edit_->EmuPerPixel();
        const float x = static_cast<float>(at.x + static_cast<double>(target.x) / emu);
        const float y = static_cast<float>(at.y + static_cast<double>(target.y) / emu);
        const float w = std::max(80.0f, static_cast<float>(static_cast<double>(target.width) / emu));
        const float h = std::max(40.0f, static_cast<float>(static_cast<double>(target.height) / emu));
        // The text at the size it has on the slide at this zoom.
        float sizePt = 18;
        if (!target.paragraphs.empty() && !target.paragraphs.front().runs.empty() && target.paragraphs.front().runs.front().sizePt > 0)
            sizePt = target.paragraphs.front().runs.front().sizePt;
        const double pixelsPerPoint = 12700.0 / emu;
        text_->SetFontSize(std::clamp(static_cast<float>(sizePt * pixelsPerPoint * 0.75), 8.0f, 96.0f));
        text_->SetElementAbsolutePosition(Point2Df(x, y));
        text_->SetElementSize(CSSLayout::Dimension::Px(w), CSSLayout::Dimension::Px(h));
        text_->SetText(UCSlideDeckEditor::ShapeText(target), false);
        textShape_ = shape;
        textSlide_ = Editor().GetCurrentSlide();
        text_->SetVisible(true);
        text_->SetFocus(true);
    }

    // `keep`: the text goes into the shape (one undo step); else it is dropped.
    void CloseText(bool keep) {
        if (textShape_ < 0) return;
        const int shape = textShape_, slide = textSlide_;
        textShape_ = -1;
        text_->SetVisible(false);
        if (keep && slide == Editor().GetCurrentSlide()) Editor().SetShapeText(shape, text_->GetText());
        edit_->SetFocus(true);
    }

    // ===== TOOLBARS =====

    void BuildHome() {
        auto bar = MakeToolbar("office-slides-home");
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

    void BuildShow() {
        auto bar = MakeToolbar("office-slides-show");
        AddTool(*bar, "slides-show-first", Tr("From the beginning"), "play.svg", Tr("Show the slides full screen, from the first"),
                [this]() { Present(0); });
        AddTool(*bar, "slides-show-here", Tr("From this slide"), "", Tr("Show the slides full screen, from this one"),
                [this]() { Present(std::max(0, Editor().GetCurrentSlide())); });
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

    // The slides full screen, in a window of their own, the deck shared.
    void Present(int fromSlide) {
        CloseText(true);
        if (showWindow_) return;
        auto window = std::make_shared<UltraCanvasWindow>();
        WindowConfig config;
        config.title = Tr("Slide show");
        config.width = 1280;
        config.height = 720;
        config.deleteOnClose = false;
        if (!window->Create(config)) return;
        auto player = CreateSlideEdit("office-slide-show", 0, 0, 1280, 720);
        player->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Pct(100));
        player->SetShowSlideStrip(false);
        player->SetDeck(Editor().GetDeck());
        window->AddChild(player);
        UltraCanvasWindow* raw = window.get();
        player->onShowEnd = [this, raw]() {
            // Closed after the key or click that ended it is handled.
            if (auto* app = UltraCanvasApplication::GetInstance())
                app->StartTimer(1, false, [this, raw](TimerId) {
                    if (showWindow_.get() == raw) {
                        showWindow_->Close();
                        showWindow_.reset();
                    }
                    edit_->SetFocus(true);
                });
        };
        window->onWindowClosed = [this, raw]() {
            if (showWindow_.get() == raw)
                if (auto* app = UltraCanvasApplication::GetInstance())
                    app->StartTimer(1, false, [this, raw](TimerId) { if (showWindow_.get() == raw) showWindow_.reset(); });
        };
        showWindow_ = window;
        window->Show();
        window->SetFullscreen(true);
        player->StartShow(fromSlide);
    }

    void Sync() {
        const auto& editor = Editor();
        if (hidden_) {
            const PptxSlide* slide = editor.CurrentSlide();
            hidden_->SetPressed(slide && slide->hidden);
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
