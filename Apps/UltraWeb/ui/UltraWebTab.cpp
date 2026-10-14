// Apps/UltraWeb/ui/UltraWebTab.cpp
// One browser tab (UltraWebTab.h): what used to be the whole window's page
// or app, now one per tab.
// Version: 0.1.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebTab.h"

#include "../host/UltraWebFetch.h"
#include "../host/UltraWebPages.h"
#include "../host/UltraWebStorage.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasUtils.h"
#include "WasmHost/UltraCanvasWasmHost.h"

#include <cstdio>

using namespace UltraCanvas;

namespace UltraWeb {

namespace {

void AppLog(const std::string& line) { std::fprintf(stderr, "[UltraWeb app] %s\n", line.c_str()); }

// The services an app reaches beside its elements (GuestServices), real
// ones: the application's timers, UltraNet, a store on disk per origin,
// the system clipboard.
GuestServices RealServices(const std::string& address, std::function<void(std::function<void()>)> post) {
    GuestServices services;
    services.startTimer = [](uint32_t ms, bool repeat, std::function<void()> fire) -> uint32_t {
        auto* app = UltraCanvasApplicationBase::GetCurrent();
        if (!app) return 0;
        return app->StartTimer(ms, repeat, [fire](TimerId) { fire(); });
    };
    services.stopTimer = [](uint32_t id) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(id);
    };
    services.fetch = MakeNetworkFetch(post);
    std::string problem;
    services.storage = UltraWebStorage::Open(UltraWebStorage::DefaultDirectory(),
                                             UltraWebStorage::PartitionFor(address), problem);
    if (!problem.empty()) AppLog("UltraWeb: storage kept in memory only: " + problem);
    services.writeClipboard = [](const std::string& text) { return SetClipboardText(text); };
    return services;
}

} // namespace

UltraWebTab::UltraWebTab(const std::string& id, Host host)
    : host_(std::move(host)), loader_(host_.post) {
    // UC_ROOT_HANDLE: a flex column the app fills; how its children sit
    // across it is the app's to say (UC_PROP_ALIGN), start until then.
    area_ = CreateContainer(id, 0, 0, 0, 0);
    area_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Start);
    area_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    area_->layoutItem.SetFlexGrow(1);
    area_->layoutItem.SetFlexShrink(1);
    area_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    area_->SetContainerStyle(plain);

    errorTitle_ = CreateLabel(id + "ErrorTitle", "");
    errorTitle_->SetFontSize(16);
    errorTitle_->SetTextColor(Color(176, 32, 32));
    errorDetail_ = CreateLabel(id + "ErrorDetail", "");
    errorDetail_->SetWrap(TextWrap::WrapWord);

    // Web pages: the HTML reader on UltraNet. The view joins the area while
    // a page is shown.
    webView_ = std::make_shared<UltraCanvasWebView>(id + "WebView");
    webView_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    webView_->layoutItem.SetFlexGrow(1);
    webView_->layoutItem.SetFlexShrink(1);
    webView_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    webView_->SetServices(MakePageServices(host_.post));
    webView_->SetScheduler(host_.post);
    HTML::BuildOptions pageOptions;
    pageOptions.style.baseFontSizePx = 16.f;
    pageOptions.linkTooltips = false;          // the status bar shows the address
    webView_->SetBuildOptions(pageOptions);
    webView_->onUrlChanged = [this](const std::string& url) {
        address_ = url;
        Changed();
    };
    webView_->onTitleChanged = [this](const std::string&) { Changed(); };
    webView_->onLoadingChanged = [this](bool loading) {
        SetStatus(loading ? "Loading " + address_ + " ..." : address_);
    };
    webView_->onLinkHovered = [this](const std::string& url) {
        SetStatus(url.empty() ? address_ : url);
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
        // An app served without .wasm in its address runs; anything else
        // that is no page is a file to save.
        if (!page.attachment && page.contentType.find("application/wasm") != std::string::npos) RunApp(page.url);
        else if (host_.onDownload) host_.onDownload(*this, page);
    };
}

UltraWebTab::~UltraWebTab() {
    alive_.reset();
    loader_.Cancel();
    guest_.reset();
    if (showingPage_) webView_->Stop();
    webView_->onUrlChanged = nullptr;
    webView_->onTitleChanged = nullptr;
    webView_->onLoadingChanged = nullptr;
    webView_->onLinkHovered = nullptr;
    webView_->onExternalLink = nullptr;
    webView_->onNonHtml = nullptr;
}

std::string UltraWebTab::Title() const {
    // The view's own: a download keeps the page, and its title, on show.
    if (showingPage_) {
        const std::string title = webView_->GetTitle();
        if (!title.empty()) return title;
    }
    if (!address_.empty()) return address_;
    return "New tab";
}

void UltraWebTab::Navigate(const std::string& address) {
    const std::string target = UltraWebLoader::Normalise(address);
    if (IsAppAddress(target)) RunApp(target);
    else OpenPage(ToPageUrl(target));
}

void UltraWebTab::Reload() {
    if (showingPage_) webView_->Reload();
    else if (!address_.empty()) Navigate(address_);
}

void UltraWebTab::GoBack() { if (showingPage_) webView_->GoBack(); }
void UltraWebTab::GoForward() { if (showingPage_) webView_->GoForward(); }
bool UltraWebTab::CanGoBack() const { return showingPage_ && webView_->CanGoBack(); }
bool UltraWebTab::CanGoForward() const { return showingPage_ && webView_->CanGoForward(); }

void UltraWebTab::OpenPage(const std::string& url) {
    loader_.Cancel();
    guest_.reset();
    if (!showingPage_) {
        ClearArea();
        area_->AddChild(webView_);
        showingPage_ = true;
    }
    webView_->Navigate(url);
    area_->InvalidateLayout();
    Changed();
}

void UltraWebTab::RunApp(const std::string& target) {
    if (!UltraCanvasWasm_IsAvailable()) {
        if (showingPage_) { webView_->Stop(); showingPage_ = false; }
        ClearArea();
        address_ = target;
        ShowError("This UltraWeb cannot run apps", UltraCanvasWasm_EngineDescription());
        SetStatus("Not running: " + target);
        return;
    }
    address_ = target;
    SetStatus("Loading " + target + " ...");
    loader_.Load(target, [this](LoadedApp app) { ShowApp(app); });
}

void UltraWebTab::ShowApp(const LoadedApp& app) {
    guest_.reset();
    if (showingPage_) {
        webView_->Stop();
        showingPage_ = false;
    }
    ClearArea();
    address_ = app.address;
    if (!app.ok) {
        ShowError("Cannot open " + app.address, app.error);
        SetStatus("Not loaded");
        return;
    }

    GuestOptions options;
    options.onLog = AppLog;
    options.address = app.address;
    options.services = RealServices(app.address, host_.post);
    std::weak_ptr<bool> alive = alive_;
    options.onFailure = [this, alive](const std::string& message) {
        if (alive.expired()) return;
        guest_.reset();
        ClearArea();
        ShowError("The app stopped", message);
        SetStatus("Stopped: " + address_);
    };
    options.defer = host_.post;

    std::string error;
    guest_ = UltraWebGuest::Start(app.module, area_, std::move(options), error);
    if (!guest_) {
        ClearArea();
        ShowError("The app did not start", error);
        SetStatus("Not running: " + app.address);
        return;
    }
    SetStatus("Running " + app.address + "  \xC2\xB7  " + std::to_string(guest_->ElementCount()) + " elements  \xC2\xB7  "
              + UltraCanvasWasm_EngineDescription());
}

void UltraWebTab::ShowError(const std::string& title, const std::string& detail) {
    errorTitle_->SetText(title);
    errorDetail_->SetText(detail);
    area_->AddChild(errorTitle_);
    area_->AddChild(errorDetail_);
    area_->InvalidateLayout();
    area_->RequestRedraw();
}

void UltraWebTab::ClearArea() {
    area_->ClearChildren();
    area_->InvalidateLayout();
    area_->RequestRedraw();
}

void UltraWebTab::SetStatus(const std::string& text) {
    status_ = text;
    Changed();
}

void UltraWebTab::Changed() {
    if (host_.onChanged) host_.onChanged(*this);
}

} // namespace UltraWeb
