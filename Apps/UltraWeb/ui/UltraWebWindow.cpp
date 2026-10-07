// Apps/UltraWeb/ui/UltraWebWindow.cpp
// The browser window (UltraWebWindow.h).
// Version: 0.2.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebWindow.h"

#include "../host/UltraWebFetch.h"
#include "../host/UltraWebStorage.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
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
    address_ = CreateTextInput("uwAddress", 0, 0, 0, 28);
    address_->SetPlaceholder("about:demo, a .wasm file, or an https:// address");
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
    reloadButton_->SetOnClick([this]() { if (!currentAddress_.empty()) Navigate(currentAddress_); });
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

    window_->AddChild(page_);
    LayoutForSize(kWindowWidth, kWindowHeight);
    window_->onWindowResize = [this](int width, int height) {
        LayoutForSize(static_cast<float>(width), static_cast<float>(height));
    };
    if (!UltraCanvasWasm_IsAvailable()) {
        ShowError("This UltraWeb cannot run apps", UltraCanvasWasm_EngineDescription());
    }
    return true;
}

void UltraWebWindow::Show() {
    if (window_) window_->Show();
}

void UltraWebWindow::Navigate(const std::string& address) {
    const std::string target = UltraWebLoader::Normalise(address);
    address_->SetText(target);
    SetStatus("Loading " + target + " ...");
    loader_.Load(target, [this](LoadedApp app) { ShowApp(app); });
}

void UltraWebWindow::ShowApp(const LoadedApp& app) {
    guest_.reset();
    ClearAppArea();
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
