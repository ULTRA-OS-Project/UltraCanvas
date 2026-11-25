// Apps/UltraOffice/UltraOfficePresentationHost.cpp
// A presentation tab, until the slide editor exists
// (Docs/Research/UltraOfficeDesignProposal.md, §5.3): the framework's media
// viewer shows the deck's slides - drawn by the slide renderer, with the
// thumbnail strip, zoom, search and the speaker notes - and runs the
// full-screen slide show. The deck is not changed here, so Save always asks
// for a name: it writes the deck again as PowerPoint or OpenDocument,
// through the deck model both readers and both writers share.
//
// Version: 0.1.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraOfficeHost.h"
#include "UltraOfficeToolbar.h"

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasPresentationOdp.h"
#include "UltraCanvasPresentationPptx.h"
#include "UltraLocalisation/UltraLocalisation.h"

namespace UltraOffice {

using namespace UltraCanvas;

namespace {

class PresentationHost final : public OfficeHost {
public:
    explicit PresentationHost(UltraCanvasWindowBase* window) : window_(window) {
        (void)window_;
        viewer_ = CreateMediaViewer("office-presentation", 0, 0, 800, 600);
        viewer_->SetGrabFocusOnAttach(false);
        // The viewer's folder path and photo-slideshow bars belong to a file
        // browser; here the tab and its toolbar say what is open.
        viewer_->SetTopBarsVisible(false);
        viewer_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        BuildHome();
    }

    OfficeKind GetKind() const override { return OfficeKind::Presentation; }
    std::shared_ptr<UltraCanvasUIElement> GetContent() override { return viewer_; }
    const std::vector<CommandTab>& GetCommandTabs() override { return tabs_; }

    void NewBlank() override {}

    bool Load(const std::string& path, const std::string& password, std::string& error) override {
        // Read once here so an unreadable deck is refused with a reason
        // rather than shown as an empty page.
        PptxPresentation deck;
        const bool read = IsOdpExtension(path) ? ReadOdpPresentation(path, deck, error, password)
                                               : ReadPptxPresentation(path, deck, error, password);
        if (!read) return false;
        path_ = path;
        password_ = password;
        slides_ = deck.slides.size();
        viewer_->SetFiles({path});
        if (viewer_->IsLocked() && !password.empty()) {
            std::string unlockError;
            viewer_->UnlockWithPassword(password, unlockError);
        }
        return true;
    }

    bool Write(const std::string& path, const std::string& password, std::string& error) override {
        if (path_.empty()) { error = Tr("There is no presentation to save."); return false; }
        PptxPresentation deck;
        const bool read = IsOdpExtension(path_) ? ReadOdpPresentation(path_, deck, error, password_, true)
                                                : ReadPptxPresentation(path_, deck, error, password_, true);
        if (!read) return false;
        return UltraCanvasFileLoader::SavePresentation(deck, path, error, password);
    }

    void AddSaveTypes(FileDialogOptions& opts) const override { UltraCanvasFileLoader::AddPresentationSaveTypes(opts); }
    std::string DefaultExtension(SaveFormat format) const override {
        return format == SaveFormat::OpenDocument ? "odp" : "pptx";
    }
    bool CanSaveInPlace() const override { return false; }

    bool IsModified() const override { return false; }
    void SetModified(bool) override {}
    void Undo() override {}
    void Redo() override {}
    void Focus() override { viewer_->SetFocus(true); }

    std::string GetStatusText() const override {
        return TrF("{0} slides", slides_) + "    " + Tr("Viewing: slides are edited in a later version. F5 presents.");
    }

private:
    UltraCanvasWindowBase* window_;
    std::shared_ptr<UltraCanvasMediaViewer> viewer_;
    std::vector<CommandTab> tabs_;
    std::string path_;
    std::string password_;
    size_t slides_ = 0;
    UltraCanvasButton* outline_ = nullptr;

    void BuildHome() {
        auto bar = MakeToolbar("office-slides-home");
        UltraCanvasMediaViewer* viewer = viewer_.get();
        AddTool(*bar, "slides-present", Tr("Present"), "play.svg", Tr("Show the slides full screen, from the first (F5)"),
                [viewer]() { viewer->StartPresenting(false); });
        AddTool(*bar, "slides-present-here", Tr("From this slide"), "", Tr("Show the slides full screen, from this one (Shift+F5)"),
                [viewer]() { viewer->StartPresenting(true); });
        bar->AddSeparator();
        outline_ = AddToggleTool(*bar, "slides-outline", Tr("Outline"), "", Tr("Show the slides' text as an outline"),
                                 [this, viewer]() {
                                     viewer->SetPresentationOutline(!viewer->GetPresentationOutline());
                                     if (outline_) outline_->SetPressed(viewer->GetPresentationOutline());
                                 }).get();
        tabs_.push_back({Tr("Home"), bar});
    }
};

} // namespace

std::unique_ptr<OfficeHost> CreatePresentationHost(UltraCanvasWindowBase* window) {
    return std::make_unique<PresentationHost>(window);
}

} // namespace UltraOffice
