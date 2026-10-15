// Apps/UltraWeb/ui/UltraWebWindow.cpp
// The browser window (UltraWebWindow.h).
// Version: 0.8.0 - target="_blank" links open in a new tab
// Version: 0.7.0 - tabs: a tab strip above the toolbar, one page or app per tab
// Version: 0.6.0 - a downloads list: a panel above the status bar
// Version: 0.5.0 - downloads: a link to no page is saved through the save dialog
// Version: 0.3.0 - web pages in an UltraCanvasWebView; back / forward
// Version: 0.2.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebWindow.h"

#include "../host/UltraWebDownloads.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasFileAssociations.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasUtils.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

#ifndef ULTRAWEB_VERSION
#error "ULTRAWEB_VERSION is not defined: build through CMake, which reads it from Docs/UltraWeb/CHANGELOG.md"
#endif

using namespace UltraCanvas;

namespace UltraWeb {

namespace {

constexpr float kWindowWidth = 1000;
constexpr float kWindowHeight = 720;
constexpr float kBarHeight = 34;
constexpr float kMargin = 8;
constexpr int kTabHeight = 30;

void PostToUi(std::function<void()> task) {
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->PostToUIThread(std::move(task));
}

std::shared_ptr<UltraCanvasContainer> MakeBar(const std::string& id) {
    auto bar = CreateContainer(id, 0, 0, 0, 0);
    bar->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bar->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(kBarHeight));
    bar->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    bar->layoutItem.SetFlexGrow(0);
    bar->layoutItem.SetFlexShrink(0);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    bar->SetContainerStyle(plain);
    return bar;
}

} // namespace

UltraWebWindow::UltraWebWindow() : alive_(std::make_shared<bool>(true)) {}

// The tabs first: their guests remove their elements from areas that must
// still exist.
UltraWebWindow::~UltraWebWindow() {
    alive_.reset();
    for (const auto& download : downloads_) download.second->Cancel();   // their part files go
    downloadList_.onChanged = nullptr;
    if (window_) window_->UnInstallWindowEventFilter("uw-shortcuts");
    tabs_.clear();
}

bool UltraWebWindow::Initialize() {
    WindowConfig config;
    config.title = std::string("UltraWeb ") + ULTRAWEB_VERSION;
    config.width = static_cast<int>(kWindowWidth);
    config.height = static_cast<int>(kWindowHeight);
    config.minWidth = 480;
    config.minHeight = 360;
    window_ = CreateWindow(config);
    if (!window_) return false;

    page_ = CreateContainer("uwPage", 0, 0, 0, 0);
    page_->layout.SetFlexColumn().SetFlexGap(kMargin).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page_->SetPadding(kMargin);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    page_->SetContainerStyle(plain);

    // The tab strip is the topmost bar; the pages it switches go into
    // tabPages_, below the toolbar, which acts on the active tab.
    tabStrip_ = std::make_shared<UltraCanvasTabbedContainer>("uwTabs", 0, 0, 0, kTabHeight);
    tabStrip_->SetTabHeight(kTabHeight);
    tabStrip_->SetTabMinWidth(110);
    tabStrip_->SetTabMaxWidth(220);
    tabStrip_->SetCloseMode(TabCloseMode::Closable);
    tabStrip_->SetAllowTabReordering(true);
    tabStrip_->SetNewTabButtonPosition(NewTabButtonPosition::AfterTabs);
    tabStrip_->SetShowNewTabButton(true);
    tabStrip_->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    tabPages_ = CreateContainer("uwTabPages", 0, 0, 0, 0);
    tabPages_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    tabPages_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    tabPages_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    tabPages_->SetContainerStyle(plain);
    tabStrip_->SetContentHost(tabPages_);
    tabStrip_->onNewTabRequest = [this]() { NewTab(std::string()); };
    tabStrip_->onTabClose = [this](int index) { return CloseTab(index); };
    tabStrip_->onTabChange = [this](int, int) { ShowActiveTab(); };
    tabStrip_->onTabReorder = [this](int from, int to) {
        if (from < 0 || to < 0 || from >= (int)tabs_.size() || to >= (int)tabs_.size() || from == to) return;
        auto tab = std::move(tabs_[from]);
        tabs_.erase(tabs_.begin() + from);
        tabs_.insert(tabs_.begin() + to, std::move(tab));
    };
    page_->AddChild(tabStrip_);

    auto toolbar = MakeBar("uwToolbar");
    backButton_ = CreateButton("uwBack", 0, 0, 34, 28, "\xE2\x86\x90");        // ←
    backButton_->SetTooltip("Back");
    backButton_->SetOnClick([this]() { if (auto* tab = ActiveTab()) tab->GoBack(); });
    toolbar->AddChild(backButton_);
    forwardButton_ = CreateButton("uwForward", 0, 0, 34, 28, "\xE2\x86\x92");  // →
    forwardButton_->SetTooltip("Forward");
    forwardButton_->SetOnClick([this]() { if (auto* tab = ActiveTab()) tab->GoForward(); });
    toolbar->AddChild(forwardButton_);
    address_ = CreateTextInput("uwAddress", 0, 0, 0, 28);
    address_->SetPlaceholder("a web address, a file, about:demo or a .wasm app");
    address_->layoutItem.SetFlexGrow(1);
    address_->layoutItem.SetFlexShrink(1);
    address_->onEnterPressed = [this](const std::string& text) {
        Navigate(text);
        return true;
    };
    toolbar->AddChild(address_);
    goButton_ = CreateButton("uwGo", 0, 0, 70, 28, "Open");
    goButton_->SetOnClick([this]() { Navigate(address_->GetText()); });
    toolbar->AddChild(goButton_);
    reloadButton_ = CreateButton("uwReload", 0, 0, 80, 28, "Reload");
    reloadButton_->SetOnClick([this]() { if (auto* tab = ActiveTab()) tab->Reload(); });
    toolbar->AddChild(reloadButton_);
    downloadsButton_ = CreateButton("uwDownloadsButton", 0, 0, 120, 28, "Downloads");
    downloadsButton_->SetTooltip("Show or hide the downloads");
    downloadsButton_->SetOnClick([this]() { ShowDownloads(!downloadsShown_); });
    toolbar->AddChild(downloadsButton_);
    page_->AddChild(toolbar);

    page_->AddChild(tabPages_);

    statusBar_ = MakeBar("uwStatusBar");
    status_ = CreateLabel("uwStatus", "");
    status_->layoutItem.SetFlexGrow(1);
    status_->layoutItem.SetFlexShrink(1);
    statusBar_->AddChild(status_);
    page_->AddChild(statusBar_);

    // The downloads panel: it joins the page, above the status bar, when it
    // is shown. Open opens the file with the system, Show folder its folder.
    downloadsPanel_ = std::make_unique<UltraWebDownloadsPanel>(downloadList_, PostToUi);
    downloadsPanel_->onClose = [this]() { ShowDownloads(false); };
    downloadsPanel_->onCancel = [this](int id) {
        auto it = downloads_.find(id);
        if (it != downloads_.end()) it->second->Cancel();
    };
    // Open and Show folder go to the system's default application (no
    // shell between, so any file name is safe).
    auto openWithSystem = [this](const std::string& path) {
        std::string error;
        if (FileAssociations::OpenWithDefaultApplication({ path }, error)) SetStatus("Opened " + path);
        else SetStatus("Cannot open " + path + ": " + error);
    };
    downloadsPanel_->onOpen = [openWithSystem](const DownloadEntry& entry) { openWithSystem(entry.path); };
    downloadsPanel_->onShowFolder = [openWithSystem](const DownloadEntry& entry) {
        openWithSystem(PathToUtf8(PathFromUtf8(entry.path).parent_path()));
    };
    downloadList_.onChanged = [this](int id, bool progressOnly) {
        downloadsPanel_->Update(id, progressOnly);
        if (!progressOnly) UpdateDownloadsButton();
    };

    InstallShortcuts();
    window_->AddChild(page_);
    NewTab(std::string());
    LayoutForSize(kWindowWidth, kWindowHeight);
    window_->onWindowResize = [this](int width, int height) {
        LayoutForSize(static_cast<float>(width), static_cast<float>(height));
    };
    return true;
}

void UltraWebWindow::Show() {
    if (window_) window_->Show();
}

void UltraWebWindow::Navigate(const std::string& address) {
    if (UltraWebTab* tab = ActiveTab()) tab->Navigate(address);
}

UltraWebTab& UltraWebWindow::NewTab(const std::string& address) {
    UltraWebTab::Host host;
    host.post = PostToUi;
    host.onChanged = [this](UltraWebTab& tab) { TabChanged(tab); };
    host.onDownload = [this](UltraWebTab&, const HTML::LoadedPage& page) { SaveDownload(page); };
    // target="_blank": a new tab, made the active one, as browsers do.
    host.onOpenInNewTab = [this](UltraWebTab&, const std::string& url) { NewTab(url); };
    tabs_.push_back(std::make_unique<UltraWebTab>("uwTab" + std::to_string(++tabSerial_), std::move(host)));
    UltraWebTab& tab = *tabs_.back();
    const int index = tabStrip_->AddTab(tab.Title(), tab.Area());
    tabStrip_->SetActiveTab(index);
    ShowActiveTab();
    if (!address.empty()) {
        tab.Navigate(address);
    } else if (window_) {
        // A new, empty tab: type where to go.
        address_->SetText(std::string());
        window_->SetFocusedElement(address_.get());
    }
    return tab;
}

bool UltraWebWindow::CloseTab(int index) {
    // The last tab stays: closing it would leave a window with nothing in it.
    if (tabs_.size() <= 1 || index < 0 || index >= (int)tabs_.size()) return false;
    // The tab goes on a later turn: this runs inside the strip's own click
    // (or a key press), and the page may be mid-event.
    // The strip picks the tab shown next once this returns; the toolbar
    // follows it then.
    std::shared_ptr<UltraWebTab> closing(std::move(tabs_[index]));
    tabs_.erase(tabs_.begin() + index);
    if (shownTab_ == closing.get()) shownTab_ = nullptr;
    std::weak_ptr<bool> alive = alive_;
    PostToUi([this, alive, closing]() {
        if (!alive.expired()) ShowActiveTab();
    });
    return true;
}

void UltraWebWindow::CloseActiveTab() {
    // RemoveTab asks onTabClose (CloseTab), which lets the tab go.
    tabStrip_->RemoveTab(tabStrip_->GetActiveTab());
}

void UltraWebWindow::SwitchTab(int step) {
    const int count = tabStrip_->GetTabCount();
    if (count < 2) return;
    tabStrip_->SetActiveTab(((tabStrip_->GetActiveTab() + step) % count + count) % count);
    ShowActiveTab();
}

UltraWebTab* UltraWebWindow::ActiveTab() const {
    const int index = tabStrip_ ? tabStrip_->GetActiveTab() : -1;
    return index >= 0 && index < (int)tabs_.size() ? tabs_[index].get() : nullptr;
}

int UltraWebWindow::IndexOf(const UltraWebTab& tab) const {
    for (size_t i = 0; i < tabs_.size(); ++i)
        if (tabs_[i].get() == &tab) return static_cast<int>(i);
    return -1;
}

void UltraWebWindow::TabChanged(UltraWebTab& tab) {
    const int index = IndexOf(tab);
    if (index < 0) return;
    const std::string title = tab.Title();
    if (tabStrip_->GetTabTitle(index) != title) {
        tabStrip_->SetTabTitle(index, title);
        tabStrip_->SetTabTooltip(index, tab.Address().empty() ? title : title + "\n" + tab.Address());
    }
    if (&tab == ActiveTab()) ShowActiveTab();
}

void UltraWebWindow::ShowActiveTab() {
    UltraWebTab* tab = ActiveTab();
    if (!tab) return;
    // The address field follows the tab, but never under the user's
    // fingers: only when the tab's address (or the tab) has changed.
    if (tab != shownTab_ || tab->Address() != shownAddress_) {
        address_->SetText(tab->Address());
        shownTab_ = tab;
        shownAddress_ = tab->Address();
    }
    backButton_->SetDisabled(!tab->CanGoBack());
    forwardButton_->SetDisabled(!tab->CanGoForward());
    SetStatus(tab->StatusText());
    if (window_) window_->SetWindowTitle(tab->Title() + " - UltraWeb " + ULTRAWEB_VERSION);
}

void UltraWebWindow::InstallShortcuts() {
    // Browser keys, whatever has the focus: they type nothing.
    window_->InstallEventFilter("uw-shortcuts", [this](const UCEvent& e) -> bool {
        if (e.type != UCEventType::KeyDown || !e.ctrl || e.alt) return false;
        if (window_->GetActivePopupElement()) return false;
        switch (e.virtualKey) {
            case UCKeys::T: NewTab(std::string()); return true;
            case UCKeys::W: CloseActiveTab(); return true;
            case UCKeys::Tab: SwitchTab(e.shift ? -1 : 1); return true;
            case UCKeys::L:
                window_->SetFocusedElement(address_.get());
                address_->SelectAll();
                return true;
            default: return false;
        }
    }, { UCEventType::KeyDown });
}

void UltraWebWindow::SaveDownload(const HTML::LoadedPage& page) {
    // Where to: the save dialog, in the Downloads folder, named as the
    // server or the address says.
    FileDialogOptions options;
    options.title = "Save " + page.fileName;
    options.defaultFileName = page.fileName;
    options.initialDirectory = DefaultDownloadFolder();
    options.filters.emplace_back("All files", std::vector<std::string>{ "*" });
    std::weak_ptr<bool> alive = alive_;
    const std::string url = page.url;
    const std::string name = page.fileName;
    const bool truncated = page.truncated;
    auto bytes = std::make_shared<std::vector<uint8_t>>(page.body);
    SetStatus("Save " + name + "?");
    UltraCanvasFileLoader::SaveFileDialog(options,
        [this, alive, url, name, truncated, bytes](DialogResult result, const std::string& path) {
            if (alive.expired()) return;
            if (result != DialogResult::OK || path.empty()) {
                SetStatus("Not saved: " + name);
                return;
            }
            // The name it was saved under, which the dialog may have changed.
            const std::string savedName = PathToUtf8(PathFromUtf8(path).filename());
            if (!truncated) {
                // The loader already has all of it.
                std::string error;
                const int64_t size = static_cast<int64_t>(bytes->size());
                if (SaveBytes(path, *bytes, error)) {
                    downloadList_.AddSaved(url, savedName, path, size);
                    SetStatus("Saved " + savedName + " (" + ByteSize(size) + ") to " + path);
                } else {
                    const int id = downloadList_.Add(url, savedName, path);
                    downloadList_.Finish(id, false, error);
                    SetStatus("Could not save " + savedName + ": " + error);
                }
                return;
            }
            // Over the page limit: fetched again, straight to disk, shown
            // in the downloads panel as it goes.
            const int id = downloadList_.Add(url, savedName, path);
            ShowDownloads(true);
            SetStatus("Downloading " + savedName + " ...");
            std::shared_ptr<Download> download = StartDownload(url, path, PostToUi,
                [this, alive, id](int64_t received, int64_t total) {
                    if (alive.expired()) return;
                    downloadList_.Progress(id, received, total);
                },
                [this, alive, id, savedName, path](bool ok, const std::string& error) {
                    if (alive.expired()) return;
                    downloads_.erase(id);
                    downloadList_.Finish(id, ok, error);
                    SetStatus(ok ? "Saved " + savedName + " to " + path
                                 : "Download of " + savedName + " " + (error == "cancelled" ? "cancelled" : "failed: " + error));
                });
            downloads_[id] = download;
        });
}

void UltraWebWindow::ShowDownloads(bool show) {
    if (show == downloadsShown_) return;
    downloadsShown_ = show;
    // The page's children are its column; the panel goes in before the
    // status bar, so the bar comes out and back after it.
    if (show) {
        page_->RemoveChild(statusBar_);
        page_->AddChild(downloadsPanel_->Root());
        page_->AddChild(statusBar_);
    } else {
        page_->RemoveChild(downloadsPanel_->Root());
    }
    page_->InvalidateLayout();
    page_->RequestRedraw();
}

void UltraWebWindow::UpdateDownloadsButton() {
    const int running = downloadList_.RunningCount();
    downloadsButton_->SetText(running > 0 ? "Downloads (" + std::to_string(running) + ")" : std::string("Downloads"));
    downloadsButton_->RequestRedraw();
}

void UltraWebWindow::SetStatus(const std::string& text) {
    status_->SetText(text);
    status_->RequestRedraw();
}

void UltraWebWindow::LayoutForSize(float width, float height) {
    if (!page_) return;
    // SetElementSize, not SetBounds: the layout engine sizes from the CSS
    // dimensions, so a bounds-only change is overwritten on the next pass.
    page_->SetElementAbsolutePosition(Point2Df(0, 0));
    page_->SetElementSize(Size2Df(std::max(320.0f, width), std::max(240.0f, height)));
    page_->InvalidateLayout();
    if (window_) window_->AddDirtyRectangle(Rect2Di(0, 0, static_cast<int>(width), static_cast<int>(height)));
}

} // namespace UltraWeb
