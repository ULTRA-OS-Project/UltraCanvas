// Apps/UltraWeb/ui/UltraWebWindow.cpp
// The browser window (UltraWebWindow.h).
// Version: 0.3.0 - web pages in an UltraCanvasWebView; back / forward
// Version: 0.2.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebWindow.h"

#include "../host/UltraWebFetch.h"
#include "../host/UltraWebPages.h"
#include "../host/UltraWebStorage.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasUtils.h"
#include "WasmHost/UltraCanvasWasmHost.h"

#include <algorithm>
#include <cstdio>

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

void PostToUi(std::function<void()> task) {
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->PostToUIThread(std::move(task));
}

void AppLog(const std::string& line) { std::fprintf(stderr, "[UltraWeb app] %s\n", line.c_str()); }

// The services an app reaches beside its elements (GuestServices), real
// ones: the application's timers, UltraNet, a store on disk per origin,
// the system clipboard.
GuestServices RealServices(const std::string& address) {
    GuestServices services;
    services.startTimer = [](uint32_t ms, bool repeat, std::function<void()> fire) -> uint32_t {
        auto* app = UltraCanvasApplicationBase::GetCurrent();
        if (!app) return 0;
        return app->StartTimer(ms, repeat, [fire](TimerId) { fire(); });
    };
    services.stopTimer = [](uint32_t id) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(id);
    };
    services.fetch = MakeNetworkFetch(PostToUi);
    std::string problem;
    services.storage = UltraWebStorage::Open(UltraWebStorage::DefaultDirectory(),
                                             UltraWebStorage::PartitionFor(address), problem);
    if (!problem.empty()) AppLog("UltraWeb: storage kept in memory only: " + problem);
    services.writeClipboard = [](const std::string& text) { return SetClipboardText(text); };
    return services;
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

UltraWebWindow::UltraWebWindow() : loader_(PostToUi), alive_(std::make_shared<bool>(true)) {}

// The guest first: it removes its elements from the app area, which must
// still exist.
UltraWebWindow::~UltraWebWindow() { guest_.reset(); }

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

    auto toolbar = MakeBar("uwToolbar");
    backButton_ = CreateButton("uwBack", 0, 0, 34, 28, "\xE2\x86\x90");        // ←
    backButton_->SetTooltip("Back");
    backButton_->SetOnClick([this]() { if (showingPage_) webView_->GoBack(); });
    toolbar->AddChild(backButton_);
    forwardButton_ = CreateButton("uwForward", 0, 0, 34, 28, "\xE2\x86\x92");  // →
    forwardButton_->SetTooltip("Forward");
    forwardButton_->SetOnClick([this]() { if (showingPage_) webView_->GoForward(); });
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
    reloadButton_->SetOnClick([this]() {
        if (showingPage_) webView_->Reload();
        else if (!currentAddress_.empty()) Navigate(currentAddress_);
    });
    toolbar->AddChild(reloadButton_);
    page_->AddChild(toolbar);

    // UC_ROOT_HANDLE: a flex column the app fills; how its children sit
    // across it is the app's to say (UC_PROP_ALIGN), start until then.
    appArea_ = CreateContainer("uwAppArea", 0, 0, 0, 0);
    appArea_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Start);
    appArea_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    appArea_->layoutItem.SetFlexGrow(1);
    appArea_->layoutItem.SetFlexShrink(1);
    appArea_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    page_->AddChild(appArea_);

    errorTitle_ = CreateLabel("uwErrorTitle", "");
    errorTitle_->SetFontSize(16);
    errorTitle_->SetTextColor(Color(176, 32, 32));
    errorDetail_ = CreateLabel("uwErrorDetail", "");
    errorDetail_->SetWrap(TextWrap::WrapWord);

    auto statusBar = MakeBar("uwStatusBar");
    status_ = CreateLabel("uwStatus", "");
    status_->layoutItem.SetFlexGrow(1);
    status_->layoutItem.SetFlexShrink(1);
    statusBar->AddChild(status_);
    page_->AddChild(statusBar);

    // Web pages: the HTML reader on UltraNet. The view lives here and joins
    // the app area while a page is shown.
    webView_ = std::make_shared<UltraCanvasWebView>("uwWebView");
    webView_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    webView_->layoutItem.SetFlexGrow(1);
    webView_->layoutItem.SetFlexShrink(1);
    webView_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    webView_->SetServices(MakePageServices(PostToUi));
    webView_->SetScheduler(PostToUi);
    HTML::BuildOptions pageOptions;
    pageOptions.style.baseFontSizePx = 16.f;
    pageOptions.linkTooltips = false;          // the status bar shows the address
    webView_->SetBuildOptions(pageOptions);
    webView_->onUrlChanged = [this](const std::string& url) {
        currentAddress_ = url;
        address_->SetText(url);
        UpdateNavigationButtons();
    };
    webView_->onTitleChanged = [this](const std::string& title) {
        if (window_) window_->SetWindowTitle(title + " - UltraWeb " + ULTRAWEB_VERSION);
    };
    webView_->onLoadingChanged = [this](bool loading) {
        SetStatus(loading ? "Loading " + currentAddress_ + " ..." : currentAddress_);
        UpdateNavigationButtons();
    };
    webView_->onLinkHovered = [this](const std::string& url) {
        SetStatus(url.empty() ? currentAddress_ : url);
    };
    webView_->onExternalLink = [this](const std::string& url) {
        SetStatus("Opening " + url + " with the system ...");
        OpenURL(url);
    };
    // A file input's button: the system's (or the framework's) file dialog,
    // filtered by the input's accept="" list where it names extensions.
    webView_->onChooseFiles = [](bool multiple, const std::string& accept,
                                 std::function<void(std::vector<std::string>)> done) {
        FileDialogOptions options;
        options.title = multiple ? "Choose files to upload" : "Choose a file to upload";
        std::vector<std::string> extensions = AcceptExtensions(accept);
        if (!extensions.empty()) options.filters.emplace_back("Accepted files", extensions);
        options.filters.emplace_back("All files", std::vector<std::string>{ "*" });
        if (multiple) {
            UltraCanvasFileLoader::OpenMultipleFilesDialog(options,
                [done](DialogResult result, const std::vector<std::string>& paths) {
                    done(result == DialogResult::OK ? paths : std::vector<std::string>{});
                });
        } else {
            UltraCanvasFileLoader::OpenFileDialog(options, [done](DialogResult result, const std::string& path) {
                done(result == DialogResult::OK && !path.empty() ? std::vector<std::string>{ path }
                                                                 : std::vector<std::string>{});
            });
        }
    };
    webView_->onNonHtml = [this](const HTML::LoadedPage& page) {
        // An app served without .wasm in its address, say; anything else is
        // not something UltraWeb shows (downloads come later).
        if (page.contentType.find("application/wasm") != std::string::npos) RunApp(page.url);
        else SetStatus(page.url + " is not a page (" +
                       (page.contentType.empty() ? std::string("unknown type") : page.contentType) + ")");
    };

    window_->AddChild(page_);
    UpdateNavigationButtons();
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
    const std::string target = UltraWebLoader::Normalise(address);
    if (IsAppAddress(target)) RunApp(target);
    else OpenPage(ToPageUrl(target));
}

void UltraWebWindow::OpenPage(const std::string& url) {
    loader_.Cancel();
    guest_.reset();
    if (!showingPage_) {
        ClearAppArea();
        appArea_->AddChild(webView_);
        showingPage_ = true;
    }
    webView_->Navigate(url);
    appArea_->InvalidateLayout();
}

void UltraWebWindow::UpdateNavigationButtons() {
    backButton_->SetDisabled(!showingPage_ || !webView_->CanGoBack());
    forwardButton_->SetDisabled(!showingPage_ || !webView_->CanGoForward());
}

void UltraWebWindow::RunApp(const std::string& target) {
    if (!UltraCanvasWasm_IsAvailable()) {
        if (showingPage_) { webView_->Stop(); showingPage_ = false; }
        ClearAppArea();
        UpdateNavigationButtons();
        ShowError("This UltraWeb cannot run apps", UltraCanvasWasm_EngineDescription());
        return;
    }
    address_->SetText(target);
    SetStatus("Loading " + target + " ...");
    loader_.Load(target, [this](LoadedApp app) { ShowApp(app); });
}

void UltraWebWindow::ShowApp(const LoadedApp& app) {
    guest_.reset();
    if (showingPage_) {
        webView_->Stop();
        showingPage_ = false;
    }
    ClearAppArea();
    UpdateNavigationButtons();
    currentAddress_ = app.address;
    address_->SetText(app.address);
    SetTitleFor(app.address);
    if (!app.ok) {
        ShowError("Cannot open " + app.address, app.error);
        SetStatus("Not loaded");
        return;
    }

    GuestOptions options;
    options.onLog = AppLog;
    options.address = app.address;
    options.services = RealServices(app.address);
    std::weak_ptr<bool> alive = alive_;
    options.onFailure = [this, alive](const std::string& message) {
        if (alive.expired()) return;
        guest_.reset();
        ClearAppArea();
        ShowError("The app stopped", message);
        SetStatus("Stopped: " + currentAddress_);
    };
    options.defer = PostToUi;

    std::string error;
    guest_ = UltraWebGuest::Start(app.module, appArea_, std::move(options), error);
    if (!guest_) {
        ClearAppArea();
        ShowError("The app did not start", error);
        SetStatus("Not running: " + app.address);
        return;
    }
    SetStatus("Running " + app.address + "  ·  " + std::to_string(guest_->ElementCount()) + " elements  ·  "
              + UltraCanvasWasm_EngineDescription());
}

void UltraWebWindow::ShowError(const std::string& title, const std::string& detail) {
    errorTitle_->SetText(title);
    errorDetail_->SetText(detail);
    appArea_->AddChild(errorTitle_);
    appArea_->AddChild(errorDetail_);
    appArea_->InvalidateLayout();
    appArea_->RequestRedraw();
}

void UltraWebWindow::ClearAppArea() {
    appArea_->ClearChildren();
    appArea_->InvalidateLayout();
    appArea_->RequestRedraw();
}

void UltraWebWindow::SetStatus(const std::string& text) {
    status_->SetText(text);
    status_->RequestRedraw();
}

void UltraWebWindow::SetTitleFor(const std::string& address) {
    if (!window_) return;
    window_->SetWindowTitle(address + " - UltraWeb " + ULTRAWEB_VERSION);
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
