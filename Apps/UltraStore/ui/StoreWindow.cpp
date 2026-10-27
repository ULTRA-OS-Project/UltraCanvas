// Apps/UltraStore/ui/StoreWindow.cpp
// The shell, the pages and the details view; the jobs are in StoreJobs.cpp,
// the settings page in StoreSettingsPage.cpp.
// Version: 0.2.0 - codes and ultrastore:// links
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StoreWindow.h"

#include "StoreTheme.h"

#include "CatalogClient.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasBadge.h"
#include "UltraCanvasDataFolder.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasUtils.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#ifndef ULTRASTORE_VERSION
#error "ULTRASTORE_VERSION must be passed by CMake (from Docs/UltraStore/CHANGELOG.md)"
#endif

using namespace UltraCanvas;

namespace UltraStore::Ui {

namespace {

constexpr const char* kDiscover   = "discover";
constexpr const char* kCategories = "categories";
constexpr const char* kInstalled  = "installed";
constexpr const char* kUpdates    = "updates";
constexpr const char* kSettings   = "settings";
constexpr const char* kDetails    = "details";

std::string Plural(size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

bool EndsWith(const std::string& s, const char* suffix) {
    const std::string suf(suffix);
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

bool ReadWholeFile(const std::string& path, std::string& out) {
    std::ifstream in(PathFromUtf8(path), std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

std::shared_ptr<UltraCanvasBadge> Badge(const std::string& id, const std::string& text, BadgeVariant variant) {
    return Theme::Pin(CreateBadge(id, 0, 0, text, variant));
}

std::shared_ptr<UltraCanvasContainer> MakeScrollList(const std::string& id) {
    auto list = CreateScrollableContainer(id, 0, 0, 0, 0);
    // The bar only when the cards do not fit, not always.
    ContainerStyle style = list->GetContainerStyle();
    style.forceShowVerticalScrollbar = false;
    style.autoShowScrollbars = true;
    style.autoShowHorizontalScrollbar = false;
    list->SetContainerStyle(style);
    list->layout.SetFlexColumn().SetFlexGap(Theme::kGap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    list->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    // Room on the right for the vertical scrollbar, so a card never runs
    // under it and the pane never needs a horizontal one.
    list->SetPadding(2, 16, 2, 2);
    list->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    return list;
}

} // namespace

StoreWindow::StoreWindow(UltraCanvasApplication& app, WindowOptions options)
    : app_(app), options_(std::move(options)) {}

StoreWindow::~StoreWindow() {
    alive_.reset();          // a task posted from a thread finds nobody home and returns
    cancelRunning_ = true;
    if (engine_) engine_->Downloads().Cancel();
    StopJobThread();
    StopPictureThread();
}

// ===========================================================================
// Shell
// ===========================================================================
bool StoreWindow::Create() {
    settings_ = StoreSettings::Load();
    lang_ = settings_.Language();
    channel_ = options_.channel.empty() ? settings_.channel : options_.channel;
    if (channel_ != "beta") channel_ = "stable";
    platform_ = PlatformInfo::Detect();

    WindowConfig config;
    config.title = "UltraStore " ULTRASTORE_VERSION;
    config.width = std::max(kMinWidth, settings_.windowWidth);
    config.height = std::max(kMinHeight, settings_.windowHeight);
    config.minWidth = kMinWidth;
    config.minHeight = kMinHeight;
    config.backgroundColor = Theme::kPageBackground;
    window_ = CreateWindow(config);
    if (!window_) return false;

    BuildShell();
    OpenStore();

    LayoutForSize(static_cast<float>(config.width), static_cast<float>(config.height));
    window_->onWindowResize = [this](int w, int h) {
        settings_.windowWidth = w;
        settings_.windowHeight = h;
        LayoutForSize(static_cast<float>(w), static_cast<float>(h));
    };
    window_->onWindowClosing = [this]() { return OnClosing(); };

    RebuildAllPages();
    LoadSettingsIntoPage();
    ShowPage(kDiscover);

    if (settings_.refreshOnStart && !localCatalog_ && ServerNote().empty()) RefreshCatalog();
    return true;
}

void StoreWindow::Show() {
    if (window_) window_->Show();
}

void StoreWindow::BuildShell() {
    root_ = Theme::Column("usRoot", 0);
    root_->SetBackgroundColor(Theme::kPageBackground);

    auto body = Theme::Row("usBody", 0);
    body->layout.SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    body->layoutItem.SetFlexGrow(1).SetFlexShrink(1);

    sidebar_ = Theme::Pin(CreateSidebarNavigation("usSidebar", 0, 0, 220, 0));
    sidebar_->SetElementSize(CSSLayout::Dimension::Px(220), CSSLayout::Dimension::Auto());
    const int store = sidebar_->AddSection("Store");
    sidebar_->AddItem(store, kDiscover, "Discover", Theme::Icon("home.svg"));
    sidebar_->AddItem(store, kCategories, "Categories", Theme::Icon("list-check.svg"));
    const int library = sidebar_->AddSection("Library");
    sidebar_->AddItem(library, kInstalled, "Installed", Theme::Icon("clipboard-list.svg"));
    sidebar_->AddItem(library, kUpdates, "Updates", Theme::Icon("bell-icon.svg"));
    const int more = sidebar_->AddSection("");
    sidebar_->AddItem(more, kSettings, "Settings", Theme::Icon("settings.svg"));
    sidebar_->onSelectionChanged = [this](const std::string& id) {
        if (currentPage_ == id) return;
        detailsAppId_.clear();
        ShowPage(id);
    };

    pageHost_ = Theme::Column("usPages", 0);
    pageHost_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);

    // Discover: a search field and the catalog.
    {
        std::shared_ptr<UltraCanvasContainer> header, list;
        auto page = BuildListPage(kDiscover, "Discover", header, list);
        searchInput_ = Theme::Pin(CreateTextInput("usSearch", 0, 0, 260, Theme::kControlHeight));
        searchInput_->SetPlaceholder("Search apps");
        searchInput_->onTextChanged = [this](const std::string& text) {
            search_ = text;
            RebuildDiscover();
        };
        header->AddChild(searchInput_);
        codeInput_ = Theme::Pin(CreateTextInput("usCode", 0, 0, 150, Theme::kControlHeight));
        codeInput_->SetPlaceholder("Discount or partner code");
        codeInput_->SetTooltip("A code from a partner's QR code or page, or a discount code: the prices show what it takes off, for 30 days");
        header->AddChild(codeInput_);
        auto apply = Theme::Button("usCodeApply", "Apply", 60);
        apply->onClick = [this]() { ApplyCode(codeInput_->GetText()); };
        header->AddChild(apply);
        auto refresh = Theme::Button("usRefresh", "Refresh", 80);
        refresh->SetTooltip("Fetch the catalog from the store's servers");
        refresh->onClick = [this]() { RefreshCatalog(); };
        header->AddChild(refresh);
        discoverNote_ = Theme::Caption("usDiscoverNote", "");
        discoverNote_->layoutItem.SetFlexShrink(0);
        discoverNote_->SetVisible(false);
        // The note sits between the header and the list.
        page->ClearChildren();
        page->AddChild(header);
        page->AddChild(discoverNote_);
        page->AddChild(list);
        discoverList_ = list;
    }
    // Categories: one chip per category over the same cards.
    {
        std::shared_ptr<UltraCanvasContainer> header, list;
        auto page = BuildListPage(kCategories, "Categories", header, list);
        categoryChips_ = Theme::Pin(CreateChipGroup("usCategoryChips", 0, 0, 0, 0));
        categoryChips_->SetSelectionMode(ChipGroupSelectionMode::Single);
        categoryChips_->SetAllowNoSelection(false);
        categoryChips_->onSelectionChanged = [this](int index) {
            category_ = index <= 0 || index > static_cast<int>(categories_.size()) ? std::string() : categories_[index - 1];
            RebuildCategories();
        };
        page->ClearChildren();
        page->AddChild(header);
        page->AddChild(categoryChips_);
        page->AddChild(list);
        categoryList_ = list;
    }
    // Installed: the apps, then the components, and the clean-up.
    {
        std::shared_ptr<UltraCanvasContainer> header, list;
        BuildListPage(kInstalled, "Installed", header, list);
        auto reload = Theme::Button("usReloadInstalled", "Reload", 70);
        reload->SetTooltip("Read the registry of installed apps again");
        reload->onClick = [this]() { ReloadInstalled(); };
        header->AddChild(reload);
        auto collect = Theme::Button("usCollect", "Clean up", 80);
        collect->SetTooltip("Remove the runtimes and libraries no installed app needs any more");
        collect->onClick = [this]() { Collect(); };
        header->AddChild(collect);
        installedList_ = list;
    }
    // Updates.
    {
        std::shared_ptr<UltraCanvasContainer> header, list;
        BuildListPage(kUpdates, "Updates", header, list);
        updateAllButton_ = Theme::Button("usUpdateAll", "Update all", 90, Theme::ButtonKind::Primary);
        updateAllButton_->onClick = [this]() { UpdateAll(); };
        header->AddChild(updateAllButton_);
        updatesList_ = list;
    }
    // Settings.
    {
        auto page = BuildSettingsPage();
        pages_[kSettings] = page;
        pageHost_->AddChild(page);
        page->SetVisible(false);
    }
    // Details: one app, with a way back.
    {
        detailsPage_ = Theme::Column(kDetails, Theme::kGap);
        detailsPage_->SetPadding(Theme::kPagePadding);
        detailsPage_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        auto header = Theme::Row("usDetailsHeader", Theme::kGap);
        header->layoutItem.SetFlexShrink(0);
        auto back = Theme::Button("usBack", "Back", 70);
        back->SetIcon(Theme::Icon("arrow-left.svg"));
        back->onClick = [this]() { HideDetails(); };
        header->AddChild(back);
        detailsPage_->AddChild(header);
        detailsBody_ = MakeScrollList("usDetailsBody");
        detailsPage_->AddChild(detailsBody_);
        pages_[kDetails] = detailsPage_;
        pageHost_->AddChild(detailsPage_);
        detailsPage_->SetVisible(false);
    }

    body->AddChild(sidebar_);
    body->AddChild(pageHost_);

    // The download queue along the bottom.
    downloads_ = Theme::Pin(CreateCollapsiblePanel("usDownloads", "Downloads"));
    downloads_->SetExpandedHeight(static_cast<float>(std::max(100, settings_.downloadsHeight)));
    downloads_->SetExpanded(settings_.downloadsExpanded);
    downloads_->SetDetail("Nothing queued");
    downloads_->AddHeaderButton("usClearFinished", "Clear finished", [this]() { DismissFinished(); }, 112);
    downloads_->onExpandedChanged = [this](bool expanded) {
        if (autoExpanding_) return;
        settings_.downloadsExpanded = expanded;
        settings_.Save();
    };
    downloads_->onExpandedHeightChanged = [this](float height) {
        settings_.downloadsHeight = static_cast<int>(height);
        settings_.Save();
    };
    auto panelBody = downloads_->GetBody();
    panelBody->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    panelBody->SetPadding(8.0f);
    queueList_ = MakeScrollList("usQueue");
    panelBody->AddChild(queueList_);

    // The status line.
    auto statusRow = Theme::Row("usStatusRow", 0);
    statusRow->layoutItem.SetFlexShrink(0);
    statusRow->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(Theme::kStatusHeight));
    statusRow->SetPadding(0, Theme::kPagePadding);
    statusRow->SetBackgroundColor(Theme::kSurface);
    statusRow->SetBorders(0.0f, Theme::kBorder, 0.0f);
    statusLabel_ = Theme::Caption("usStatus", "");
    statusLabel_->SetWrap(TextWrap::WrapNone);
    statusLabel_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    statusRow->AddChild(statusLabel_);

    root_->AddChild(body);
    root_->AddChild(downloads_);
    root_->AddChild(statusRow);
    window_->AddChild(root_);
}

std::shared_ptr<UltraCanvasContainer> StoreWindow::BuildListPage(const std::string& id, const std::string& title,
                                                                 std::shared_ptr<UltraCanvasContainer>& header,
                                                                 std::shared_ptr<UltraCanvasContainer>& list) {
    auto page = Theme::Column(id, Theme::kGap);
    page->SetPadding(Theme::kPagePadding);
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    header = Theme::Row(id + ".header", Theme::kGap);
    header->layoutItem.SetFlexShrink(0);
    auto heading = Theme::Heading(id + ".title", title);
    heading->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    header->AddChild(heading);
    list = MakeScrollList(id + ".list");
    page->AddChild(header);
    page->AddChild(list);
    pages_[id] = page;
    pageHost_->AddChild(page);
    page->SetVisible(false);
    return page;
}

void StoreWindow::ShowPage(const std::string& id) {
    currentPage_ = id;
    for (auto& [key, page] : pages_) page->SetVisible(key == id);
    if (id != kDetails && sidebar_->GetSelectedId() != id) sidebar_->Select(id, false);
    if (id == kSettings) RefreshSettingsInfo();
    pageHost_->InvalidateLayout();
    pageHost_->RequestRedraw();
}

void StoreWindow::LayoutForSize(float width, float height) {
    if (!root_) return;
    root_->SetElementAbsolutePosition(Point2Df(0.0f, 0.0f));
    root_->SetElementSize(Size2Df(std::max(static_cast<float>(kMinWidth), width), std::max(static_cast<float>(kMinHeight), height)));
    root_->InvalidateLayout();
    if (window_) window_->AddDirtyRectangle(Rect2Di(0, 0, static_cast<int>(width), static_cast<int>(height)));
}

void StoreWindow::SetStatus(const std::string& text, bool isError) {
    if (!statusLabel_) return;
    statusLabel_->SetText(text);
    statusLabel_->SetTextColor(isError ? Theme::kDanger : Theme::kTextSecondary);
    statusLabel_->RequestRedraw();
}

void StoreWindow::Alert(const std::string& message, bool isError) {
    if (isError) UltraCanvasAlert::Error(message, "UltraStore", nullptr, window_.get());
    else UltraCanvasAlert::Info(message, "UltraStore", nullptr, window_.get());
}

bool StoreWindow::OnClosing() {
    settings_.Save();
    if (!Busy()) {
        app_.RequestExit();
        return true;
    }
    UltraCanvasDialogManager::ShowConfirmation(
        "\"" + running_->title + "\" is still running. Cancel it and close UltraStore?\n"
        "A download continues where it stopped the next time; nothing is left half installed.",
        "Work in progress",
        [this](bool confirmed) {
            if (!confirmed) return;
            cancelRunning_ = true;
            if (engine_) engine_->Downloads().Cancel();
            pending_.clear();
            StopJobThread();
            app_.RequestExit();
            window_->Close();
        },
        window_.get());
    return false;
}

// ===========================================================================
// Data
// ===========================================================================
void StoreWindow::OpenStore() {
    const std::string root = options_.root.empty()
        ? PathToUtf8(UltraCanvasLocalDataFolderRoot() / "UltraStore") : options_.root;
    engine_ = std::make_unique<StoreEngine>(root);
    if (const auto opened = engine_->Open(); !opened) {
        storeError_ = opened.error.message + (opened.error.detail.empty() ? "" : " (" + opened.error.detail + ")");
    } else if (const auto all = engine_->Installed()) {
        installed_ = all.Value();
    }
    assets_ = std::make_unique<AssetCache>(PathToUtf8(PathFromUtf8(engine_->CacheDir()) / "assets"));
    ApplySettings();

    if (!options_.catalogFile.empty()) {
        localCatalog_ = true;
        std::string text;
        if (!ReadWholeFile(options_.catalogFile, text)) {
            catalogNote_ = "Cannot read " + options_.catalogFile;
        } else if (const Result<Catalog> parsed = ParseCatalog(text); !parsed) {
            catalogNote_ = options_.catalogFile + ": " + parsed.error.message;
        } else {
            SetCatalog(std::make_shared<const Catalog>(parsed.Value()), "Local catalog " + options_.catalogFile +
                       " (sequence " + std::to_string(parsed.Value().sequence) + "), not verified");
        }
        return;
    }
    if (const Result<Catalog> cached = client_->LoadCached()) {
        catalogState_ = client_->State();
        SetCatalog(std::make_shared<const Catalog>(cached.Value()),
                   "Cached catalog, sequence " + std::to_string(cached.Value().sequence) +
                   (catalogState_.fetchedAt.empty() ? "" : ", fetched " + catalogState_.fetchedAt + " from " + catalogState_.fetchedFrom));
    } else {
        catalogNote_ = ServerNote().empty() ? "No catalog yet: press Refresh." : ServerNote();
    }
}

void StoreWindow::ApplySettings() {
    if (!engine_) return;
    CatalogClientConfig config;
    config.baseUrls = settings_.Servers();
    config.trustedKeys = settings_.TrustedKeys();
    config.cacheDir = engine_->CacheDir();
    client_ = std::make_unique<CatalogClient>(config);
}

std::string StoreWindow::ServerNote() const {
    if (localCatalog_) return {};
    if (settings_.Servers().empty())
        return "No store server is configured yet: enter one under Settings, or start with --catalog <file>.";
    if (settings_.TrustedKeys().empty())
        return "No signing key is trusted yet: add the store's public key under Settings.";
    return {};
}

std::string StoreWindow::Today() const {
    return NowIso8601Utc().substr(0, 10);
}

const PromoCode* StoreWindow::CodeInForce() const {
    if (!catalog_) return nullptr;
    const std::string code = settings_.CodeInForce(Today());
    if (code.empty()) return nullptr;
    const PromoCode* found = catalog_->FindCode(code);
    return found && found->ValidOn(Today()) ? found : nullptr;
}

const PromoCode* StoreWindow::CodeFor(const Component& app) const {
    const PromoCode* code = CodeInForce();
    return code && code->AppliesTo(app.id) ? code : nullptr;
}

void StoreWindow::ApplyCode(const std::string& text) {
    if (NormalizeCode(text).empty()) {
        if (settings_.code.empty()) { SetStatus("Enter a code"); return; }
        settings_.code.clear();
        settings_.codeEnteredAt.clear();
        settings_.Save();
        if (codeInput_) codeInput_->SetText("");
        RebuildAllPages();
        SetStatus("The code is forgotten");
        return;
    }
    if (!catalog_) { SetStatus("No catalog yet to check the code against: press Refresh", true); return; }
    const CodeCheck check = CheckCode(*catalog_, text, Today());
    if (!check.Ok()) { SetStatus("Code " + NormalizeCode(text) + ": " + check.message, true); return; }
    settings_.code = check.code->code;
    settings_.codeEnteredAt = Today();
    settings_.Save();
    if (codeInput_) codeInput_->SetText(check.code->code);
    RebuildAllPages();
    SetStatus("Code " + check.code->code + ": " + check.message + " - in force for " + std::to_string(StoreSettings::kCodeDays) + " days");
}

void StoreWindow::OpenLink(const std::string& url) {
    // ultrastore://code/<CODE>, ultrastore://app/<id>[?code=<CODE>]
    const std::string prefix = "ultrastore://";
    if (url.rfind(prefix, 0) != 0) { SetStatus("Not a store link: " + url, true); return; }
    std::string rest = url.substr(prefix.size());
    std::string query;
    if (const size_t q = rest.find('?'); q != std::string::npos) { query = rest.substr(q + 1); rest = rest.substr(0, q); }
    while (!rest.empty() && rest.back() == '/') rest.pop_back();
    const size_t slash = rest.find('/');
    const std::string what = rest.substr(0, slash);
    const std::string value = slash == std::string::npos ? std::string() : rest.substr(slash + 1);
    std::string code;
    for (size_t at = 0; at < query.size();) {
        size_t amp = query.find('&', at);
        if (amp == std::string::npos) amp = query.size();
        const std::string pair = query.substr(at, amp - at);
        if (pair.rfind("code=", 0) == 0) code = pair.substr(5);
        at = amp + 1;
    }
    if (what == "code") code = value;
    else if (what == "app") {
        if (!catalog_ || !catalog_->FindApp(value)) SetStatus("The catalog does not list " + value, true);
        else { searchInput_->SetText(""); search_.clear(); ShowDetails(value); }
    } else { SetStatus("Unknown store link: " + url, true); return; }
    if (!code.empty()) ApplyCode(code);
}

void StoreWindow::SetCatalog(std::shared_ptr<const Catalog> catalog, const std::string& note) {
    catalog_ = std::move(catalog);
    catalogNote_ = note;
    categories_ = catalog_ ? CatalogCategories(*catalog_) : std::vector<std::string>{};
    if (std::find(categories_.begin(), categories_.end(), category_) == categories_.end()) category_.clear();
}

void StoreWindow::SetInstalled(std::vector<InstalledRecord> records) {
    installed_ = std::move(records);
}

ListingFilter StoreWindow::BaseFilter() const {
    ListingFilter f;
    f.channel = channel_;
    f.language = lang_;
    return f;
}

// ===========================================================================
// Pages
// ===========================================================================
void StoreWindow::RebuildAllPages() {
    RebuildDiscover();
    RebuildCategories();
    RebuildInstalled();
    RebuildUpdates();
    if (!detailsAppId_.empty()) RebuildDetails();
    RefreshSettingsInfo();
}

void StoreWindow::RebuildDiscover() {
    if (!discoverList_) return;
    std::string note = storeError_.empty() ? std::string() : "The store folder could not be opened: " + storeError_;
    if (note.empty() && !catalog_) note = catalogNote_;
    else if (note.empty() && !ServerNote().empty() && !localCatalog_) note = ServerNote();
    discoverNote_->SetText(note);
    discoverNote_->SetVisible(!note.empty());
    std::vector<AppListing> apps;
    if (catalog_) {
        ListingFilter f = BaseFilter();
        f.search = search_;
        apps = ListApps(*catalog_, platform_, installed_, f);
    }
    FillList(discoverList_, apps, "usD", catalog_ ? (search_.empty() ? "The catalog lists no app." : "No app matches \"" + search_ + "\".") : "");
}

void StoreWindow::RebuildCategories() {
    if (!categoryList_) return;
    // The chips: "All" first, then the catalog's categories; rebuilt only when
    // the categories changed, so a click does not rebuild the row under the pointer.
    std::vector<std::string> wanted = {"All"};
    wanted.insert(wanted.end(), categories_.begin(), categories_.end());
    bool same = categoryChips_->GetChipCount() == static_cast<int>(wanted.size());
    for (int i = 0; same && i < categoryChips_->GetChipCount(); ++i) same = categoryChips_->IndexOf(wanted[i]) == i;
    if (!same) {
        categoryChips_->ClearChips();
        for (const std::string& c : wanted) categoryChips_->AddChip(c);
        const auto it = std::find(categories_.begin(), categories_.end(), category_);
        categoryChips_->SetSelectedIndex(it == categories_.end() ? 0 : static_cast<int>(it - categories_.begin()) + 1);
    }
    std::vector<AppListing> apps;
    if (catalog_) {
        ListingFilter f = BaseFilter();
        f.category = category_;
        apps = ListApps(*catalog_, platform_, installed_, f);
    }
    FillList(categoryList_, apps, "usC", catalog_ ? (category_.empty() ? "The catalog lists no app." : "No app in \"" + category_ + "\".") : catalogNote_);
}

void StoreWindow::RebuildInstalled() {
    if (!installedList_) return;
    installedList_->ClearChildren();
    const std::vector<InstalledRecord> apps = CurrentApps(installed_);
    if (!storeError_.empty()) {
        installedList_->AddChild(Theme::Caption("usI.note", "The store folder could not be opened: " + storeError_));
    } else if (apps.empty()) {
        installedList_->AddChild(Theme::Caption("usI.empty", "No app is installed yet. Discover lists what the store offers."));
    }
    int n = 0;
    for (const InstalledRecord& rec : apps) {
        const std::string id = "usI" + std::to_string(n++);
        const Component* app = catalog_ ? catalog_->FindApp(rec.id) : nullptr;
        if (app) {
            installedList_->AddChild(MakeAppCard(ListingFor(*catalog_, *app, platform_, installed_, channel_), id));
            continue;
        }
        // Installed, but the catalog does not know it (another channel, a
        // local catalog, or withdrawn and gone): still shown, still removable.
        auto card = Theme::Pin(CreateCard(id, rec.id, "Installed " + rec.version + " · not in the current catalog"));
        card->AddContent(Theme::Caption(id + ".folder", rec.folder));
        if (!rec.entry.empty()) {
            auto open = Theme::Button(id + ".open", "Open", 72, Theme::ButtonKind::Primary);
            const std::string appId = rec.id;
            open->onClick = [this, appId]() {
                for (const InstalledRecord& r : installed_) {
                    if (r.id != appId || !r.current) continue;
                    AppListing l;
                    l.installedVersion = r.version;
                    l.installedFolder = r.folder;
                    l.installedEntry = r.entry;
                    LaunchApp(l);
                    return;
                }
            };
            card->AddAction(open);
        }
        auto remove = Theme::Button(id + ".remove", "Remove", 72, Theme::ButtonKind::Danger);
        const std::string appId = rec.id;
        remove->onClick = [this, appId]() { RemoveApp(appId); };
        remove->SetDisabled(Busy());
        card->AddAction(remove);
        installedList_->AddChild(card);
    }
    const std::vector<InstalledRecord> components = InstalledComponents(installed_);
    if (!components.empty()) {
        auto surface = Theme::Surface("usI.components", 4.0f);
        surface->AddChild(Theme::Text("usI.components.title", "Runtimes and libraries", 13.0f, FontWeight::Bold));
        surface->AddChild(Theme::Caption("usI.components.hint",
            "Shared by the apps above; Clean up removes the versions no installed app needs."));
        int c = 0;
        for (const InstalledRecord& rec : components) {
            const Component* comp = catalog_ ? catalog_->FindComponent(rec.id) : nullptr;
            const std::string name = comp && !comp->name.empty() ? comp->name : rec.id;
            surface->AddChild(Theme::Text("usI.comp" + std::to_string(c++),
                name + "  " + rec.version + "  (" + ComponentKindName(rec.kind) + ", " + rec.id + ")", 12.0f));
        }
        installedList_->AddChild(surface);
    }
    installedList_->ScrollToVertical(0);
    installedList_->InvalidateLayout();
    installedList_->RequestRedraw();
}

void StoreWindow::RebuildUpdates() {
    if (!updatesList_) return;
    std::vector<AppListing> apps;
    if (catalog_) {
        ListingFilter f = BaseFilter();
        f.onlyUpdates = true;
        apps = ListApps(*catalog_, platform_, installed_, f);
    }
    sidebar_->SetBadge(kUpdates, static_cast<int>(apps.size()));
    updateAllButton_->SetDisabled(apps.empty() || Busy());
    FillList(updatesList_, apps, "usU", catalog_ ? "Everything is up to date." : catalogNote_);
}

void StoreWindow::FillList(const std::shared_ptr<UltraCanvasContainer>& list, const std::vector<AppListing>& apps,
                           const std::string& idPrefix, const std::string& emptyText) {
    list->ClearChildren();
    if (apps.empty()) {
        if (!emptyText.empty()) list->AddChild(Theme::Caption(idPrefix + ".empty", emptyText));
    }
    int n = 0;
    for (const AppListing& l : apps) list->AddChild(MakeAppCard(l, idPrefix + std::to_string(n++)));
    list->ScrollToVertical(0);
    list->InvalidateLayout();
    list->RequestRedraw();
}

std::string StoreWindow::StateLine(const AppListing& l) const {
    std::string line;
    if (l.app->lifecycle == Lifecycle::Deprecated) {
        line = "Deprecated" + (l.app->successor.empty() ? std::string() : ", use " + l.app->successor) + " · ";
    } else if (l.app->lifecycle == Lifecycle::Withdrawn) {
        line = "Withdrawn, no new installs · ";
    }
    if (l.Installed()) {
        line += "Installed " + l.installedVersion;
        if (l.updateAvailable) {
            line += " · " + l.resolution.release->version + " available";
            if (l.resolution.package->size) line += " (" + FormatBytes(l.resolution.package->size) + ")";
        }
        return line;
    }
    if (l.Available()) {
        line += l.resolution.release->version;
        if (!l.resolution.release->date.empty()) line += " · " + l.resolution.release->date;
        if (l.resolution.package->size) line += " · " + FormatBytes(l.resolution.package->size);
        line += " · " + l.resolution.package->target.Describe();
        if (l.resolution.kind == MatchKind::Emulated) line += " · runs under Rosetta";
        return line;
    }
    if (l.resolution.release) {
        return line + "Not available for your system; available for " + PackageResolver::SupportedSystems(*l.resolution.release);
    }
    return line + "No release on the " + channel_ + " channel";
}

std::vector<std::shared_ptr<UltraCanvasButton>> StoreWindow::PrimaryActions(const AppListing& l, const std::string& id) {
    std::vector<std::shared_ptr<UltraCanvasButton>> out;
    const std::string appId = l.app->id;
    const bool busy = Busy() || HasJobFor(appId);
    if (l.Installed()) {
        if (!l.installedEntry.empty()) {
            auto open = Theme::Button(id + ".open", "Open", 72, Theme::ButtonKind::Primary);
            open->onClick = [this, appId]() {
                if (!catalog_) return;
                if (const Component* app = catalog_->FindApp(appId)) LaunchApp(ListingFor(*catalog_, *app, platform_, installed_, channel_));
            };
            out.push_back(open);
        }
        if (l.updateAvailable) {
            auto update = Theme::Button(id + ".update", "Update", 72, Theme::ButtonKind::Primary);
            update->onClick = [this, appId]() { UpdateApp(appId); };
            update->SetDisabled(busy);
            out.push_back(update);
        }
        auto remove = Theme::Button(id + ".remove", "Remove", 72, Theme::ButtonKind::Danger);
        remove->onClick = [this, appId]() { RemoveApp(appId); };
        remove->SetDisabled(busy);
        out.push_back(remove);
        return out;
    }
    if (!l.Available() || l.app->lifecycle == Lifecycle::Withdrawn) return out;
    const Pricing& pricing = l.app->pricing;
    const bool paid = pricing.model != PricingModel::Free;
    if (!paid || pricing.trialDays > 0) {
        auto install = Theme::Button(id + ".install", paid ? "Try" : "Install", 72, Theme::ButtonKind::Primary);
        if (paid) install->SetTooltip("Installs the " + std::to_string(pricing.trialDays) + "-day trial");
        install->onClick = [this, appId]() { InstallApp(appId); };
        install->SetDisabled(busy || storeError_.size() > 0);
        out.push_back(install);
    }
    if (paid) {
        // Payment is a later milestone: Buy goes to the publisher's page.
        auto buy = Theme::Button(id + ".buy", "Buy…", 72, paid && pricing.trialDays == 0 ? Theme::ButtonKind::Primary : Theme::ButtonKind::Secondary);
        buy->SetTooltip(PriceLabel(pricing, CodeFor(*l.app)) + " - opens the publisher's page" +
                        (CodeFor(*l.app) ? ", the code " + CodeFor(*l.app)->code + " goes with the purchase" : ""));
        const std::string url = l.app->homepage;
        buy->onClick = [this, url]() {
            if (url.empty()) SetStatus("The publisher gave no page to buy from.", true);
            else OpenURL(url);
        };
        out.push_back(buy);
    }
    return out;
}

std::shared_ptr<UltraCanvasCard> StoreWindow::MakeAppCard(const AppListing& l, const std::string& id) {
    const Component& app = *l.app;
    std::string subtitle = app.publisher;
    const std::string summary = app.summary.Text(lang_);
    if (!summary.empty()) subtitle += (subtitle.empty() ? "" : " · ") + summary;
    auto card = Theme::Pin(CreateCard(id, app.name, subtitle));

    const std::string iconUrl = ResolveCatalogUrl(catalogState_.fetchedFrom, app.icon);
    if (!iconUrl.empty()) {
        const auto known = picturePaths_.find(iconUrl);
        if (known != picturePaths_.end()) { if (!known->second.empty()) card->SetIcon(known->second); }
        else RequestPicture(iconUrl, card);
    }

    if (l.updateAvailable) card->SetTrailing(Badge(id + ".badge", "Update", BadgeVariant::Warning));
    else if (l.Installed()) card->SetTrailing(Badge(id + ".badge", "Installed", BadgeVariant::Successful));
    else if (!l.Available()) card->SetTrailing(Badge(id + ".badge", "Not for this system", BadgeVariant::Neutral));
    else card->SetTrailing(Badge(id + ".badge", PriceLabel(app.pricing, CodeFor(app)),
                                 app.pricing.model == PricingModel::Free ? BadgeVariant::Info : BadgeVariant::Primary));

    card->AddContent(Theme::Caption(id + ".state", StateLine(l)));

    auto details = Theme::Button(id + ".details", "Details", 72);
    const std::string appId = app.id;
    details->onClick = [this, appId]() { ShowDetails(appId); };
    card->AddAction(details);
    for (auto& button : PrimaryActions(l, id)) card->AddAction(button);

    card->SetClickable(true);
    card->onClick = [this, appId]() { ShowDetails(appId); };
    return card;
}

// ===========================================================================
// Details
// ===========================================================================
void StoreWindow::ShowDetails(const std::string& appId) {
    if (!catalog_ || !catalog_->FindApp(appId)) return;
    if (currentPage_ != kDetails) pageBeforeDetails_ = currentPage_;
    detailsAppId_ = appId;
    RebuildDetails();
    ShowPage(kDetails);
}

void StoreWindow::HideDetails() {
    detailsAppId_.clear();
    ShowPage(pageBeforeDetails_.empty() || pageBeforeDetails_ == kDetails ? std::string(kDiscover) : pageBeforeDetails_);
}

void StoreWindow::RebuildDetails() {
    if (!detailsBody_) return;
    detailsBody_->ClearChildren();
    const Component* app = catalog_ ? catalog_->FindApp(detailsAppId_) : nullptr;
    if (!app) {
        detailsBody_->AddChild(Theme::Caption("usX.gone", "This app is no longer in the catalog."));
        return;
    }
    const AppListing l = ListingFor(*catalog_, *app, platform_, installed_, channel_);
    const std::string base = catalogState_.fetchedFrom;

    // Icon, name, publisher, state.
    auto head = Theme::Row("usX.head", Theme::kGap);
    head->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);
    head->layoutItem.SetFlexShrink(0);
    auto icon = Theme::Pin(CreateImageElement("usX.icon", 96, 96));
    icon->SetElementSize(CSSLayout::Dimension::Px(96), CSSLayout::Dimension::Px(96));
    icon->SetFitMode(ImageFitMode::Contain);
    const std::string iconUrl = ResolveCatalogUrl(base, app->icon);
    if (!iconUrl.empty()) {
        const auto known = picturePaths_.find(iconUrl);
        if (known != picturePaths_.end()) { if (!known->second.empty()) icon->LoadFromFile(known->second); }
        else RequestPicture(iconUrl, icon);
    }
    head->AddChild(icon);
    auto titles = Theme::Column("usX.titles", 4.0f);
    titles->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    titles->AddChild(Theme::Text("usX.name", app->name, 20.0f, FontWeight::Bold));
    std::string by = app->publisher;
    if (!app->license.empty()) by += (by.empty() ? "" : " · ") + app->license;
    by += (by.empty() ? "" : " · ") + app->id;
    titles->AddChild(Theme::Caption("usX.by", by));
    titles->AddChild(Theme::Caption("usX.state", StateLine(l)));
    // Badges.
    auto badges = Theme::Row("usX.badges", Theme::kInnerGap, true);
    badges->AddChild(Badge("usX.price", PriceLabel(app->pricing, CodeFor(*app)), app->pricing.model == PricingModel::Free ? BadgeVariant::Info : BadgeVariant::Primary));
    if (const PromoCode* code = CodeFor(*app)) {
        const std::string what = DiscountLabel(*code);
        badges->AddChild(Badge("usX.code", "Code " + code->code + (what.empty() ? "" : ": " + what) +
                               (code->validUntil.empty() ? "" : ", until " + code->validUntil), BadgeVariant::Successful));
    }
    if (!app->ageRating.empty()) badges->AddChild(Badge("usX.age", "Age " + app->ageRating + "+", BadgeVariant::Neutral));
    if (app->openSource) badges->AddChild(Badge("usX.oss", "Open source", BadgeVariant::Successful));
    if (app->requiresAccount) badges->AddChild(Badge("usX.account", "Needs an account", BadgeVariant::Neutral));
    if (app->updates.period == UpdatePeriod::Never) badges->AddChild(Badge("usX.upd", "Updates: the bought version only", BadgeVariant::Neutral));
    else if (app->updates.period == UpdatePeriod::Until) badges->AddChild(Badge("usX.upd", "Free updates until " + app->updates.until, BadgeVariant::Neutral));
    if (app->lifecycle == Lifecycle::Deprecated) badges->AddChild(Badge("usX.life", "Deprecated", BadgeVariant::Warning));
    if (app->lifecycle == Lifecycle::Withdrawn) badges->AddChild(Badge("usX.life", "Withdrawn", BadgeVariant::Danger));
    if (std::find(app->machineTranslated.begin(), app->machineTranslated.end(), lang_) != app->machineTranslated.end())
        badges->AddChild(Badge("usX.mt", "Translated automatically from " + app->sourceLanguage, BadgeVariant::Neutral));
    titles->AddChild(badges);
    head->AddChild(titles);
    detailsBody_->AddChild(head);

    // Actions and links.
    auto actions = Theme::Row("usX.actions", Theme::kInnerGap, true);
    actions->layoutItem.SetFlexShrink(0);
    for (auto& button : PrimaryActions(l, "usX")) actions->AddChild(button);
    auto link = [&](const char* id, const char* text, const std::string& url) {
        if (url.empty()) return;
        auto b = Theme::Button(id, text, 90);
        b->SetTooltip(url);
        b->onClick = [url]() { OpenURL(url); };
        actions->AddChild(b);
    };
    link("usX.home", "Homepage", app->homepage);
    link("usX.support", "Support", app->supportUrl);
    link("usX.privacy", "Privacy", app->privacyUrl);
    if (app->openSource) link("usX.source", "Source code", app->sourceUrl);
    detailsBody_->AddChild(actions);

    // Description.
    const std::string description = app->description.Text(lang_);
    if (!description.empty()) {
        auto surface = Theme::Surface("usX.description", 6.0f);
        surface->AddChild(Theme::Text("usX.description.text", description, 13.0f));
        detailsBody_->AddChild(surface);
    }

    // Screenshots, side by side, scrolled horizontally.
    if (!app->screenshots.empty()) {
        auto surface = Theme::Surface("usX.shots", 6.0f);
        surface->AddChild(Theme::Text("usX.shots.title", "Screenshots", 13.0f, FontWeight::Bold));
        auto strip = Theme::Pin(CreateScrollableContainer("usX.shots.strip", 0, 0, 0, 0, false, true));
        strip->layout.SetFlexRow().SetFlexGap(Theme::kGap).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        strip->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(232));
        int n = 0;
        for (const std::string& shot : app->screenshots) {
            const std::string url = ResolveCatalogUrl(base, shot);
            if (url.empty()) continue;
            auto image = Theme::Pin(CreateImageElement("usX.shot" + std::to_string(n++), 340, 212));
            image->SetElementSize(CSSLayout::Dimension::Px(340), CSSLayout::Dimension::Px(212));
            image->SetFitMode(ImageFitMode::Contain);
            image->SetBackgroundColor(Theme::kPageBackground);
            const auto known = picturePaths_.find(url);
            if (known != picturePaths_.end()) { if (!known->second.empty()) image->LoadFromFile(known->second); }
            else RequestPicture(url, image);
            strip->AddChild(image);
        }
        surface->AddChild(strip);
        detailsBody_->AddChild(surface);
    }

    // The release: notes and what it needs.
    const Release* release = l.resolution.release ? l.resolution.release : app->Newest(channel_);
    if (release) {
        auto surface = Theme::Surface("usX.release", 6.0f);
        std::string title = "Version " + release->version;
        if (!release->date.empty()) title += " · " + release->date;
        if (release->channel != "stable") title += " · " + release->channel;
        surface->AddChild(Theme::Text("usX.release.title", title, 13.0f, FontWeight::Bold));
        const std::string notes = release->notes.Text(lang_);
        if (!notes.empty()) surface->AddChild(Theme::Text("usX.release.notes", notes, 12.0f));
        if (!release->depends.empty()) {
            std::string needs = "Needs";
            for (const Dependency& d : release->depends) {
                const Component* comp = catalog_->FindComponent(d.id);
                needs += "\n• " + (comp && !comp->name.empty() ? comp->name : d.id) + " " + d.version;
                bool have = false;
                for (const InstalledRecord& r : installed_) if (r.id == d.id && r.version == d.version) have = true;
                needs += have ? " (installed)" : "";
            }
            surface->AddChild(Theme::Caption("usX.release.needs", needs));
        }
        if (l.resolution.package) {
            surface->AddChild(Theme::Caption("usX.release.package",
                "Package for this machine: " + l.resolution.package->target.Describe() + ", " +
                l.resolution.package->format + ", " + FormatBytes(l.resolution.package->size)));
        }
        detailsBody_->AddChild(surface);
    }
    if (!app->tags.empty() || !app->categories.empty()) {
        std::string line;
        for (const std::string& c : app->categories) line += (line.empty() ? "" : ", ") + c;
        for (const std::string& t : app->tags) line += (line.empty() ? "" : ", ") + t;
        detailsBody_->AddChild(Theme::Caption("usX.tags", line));
    }
    detailsBody_->ScrollToVertical(0);
    detailsBody_->InvalidateLayout();
    detailsBody_->RequestRedraw();
}

// ===========================================================================
// Actions
// ===========================================================================
void StoreWindow::InstallApp(const std::string& appId) {
    if (!storeError_.empty()) { Alert("The store folder could not be opened: " + storeError_); return; }
    const Component* app = catalog_ ? catalog_->FindApp(appId) : nullptr;
    if (!app) return;
    Job job;
    job.kind = JobKind::Install;
    job.appId = appId;
    job.title = "Install " + app->name;
    Enqueue(std::move(job));
}

void StoreWindow::UpdateApp(const std::string& appId) {
    const Component* app = catalog_ ? catalog_->FindApp(appId) : nullptr;
    if (!app) return;
    Job job;
    job.kind = JobKind::Update;
    job.appId = appId;
    job.title = "Update " + app->name;
    Enqueue(std::move(job));
}

void StoreWindow::UpdateAll() {
    if (!catalog_) return;
    ListingFilter f = BaseFilter();
    f.onlyUpdates = true;
    for (const AppListing& l : ListApps(*catalog_, platform_, installed_, f)) {
        if (!HasJobFor(l.app->id)) UpdateApp(l.app->id);
    }
}

void StoreWindow::RemoveApp(const std::string& appId) {
    const Component* app = catalog_ ? catalog_->FindApp(appId) : nullptr;
    const std::string name = app ? app->name : appId;
    UltraCanvasDialogManager::ShowConfirmation(
        "Remove " + name + " from this machine? Its settings and documents stay where they are; "
        "runtimes and libraries no other app needs are removed with it.",
        "Remove " + name,
        [this, appId, name](bool confirmed) {
            if (!confirmed) return;
            Job job;
            job.kind = JobKind::Remove;
            job.appId = appId;
            job.title = "Remove " + name;
            Enqueue(std::move(job));
        },
        window_.get());
}

void StoreWindow::LaunchApp(const AppListing& l) {
    InstalledRecord rec;
    rec.folder = l.installedFolder;
    rec.entry = l.installedEntry;
    const std::string path = LaunchPath(rec);
    if (path.empty()) { SetStatus("This app has nothing to start.", true); return; }
    std::vector<std::string> argv;
#if defined(__APPLE__)
    if (EndsWith(path, ".app")) argv = {"open", path};
    else argv = {path};
#else
    argv = {path};
#endif
    std::string error;
    if (!LaunchDetachedProcess(argv, l.installedFolder, error)) {
        Alert("Could not start " + path + ": " + error);
        return;
    }
    SetStatus("Started " + path);
}

void StoreWindow::Collect() {
    if (!storeError_.empty()) { Alert("The store folder could not be opened: " + storeError_); return; }
    Job job;
    job.kind = JobKind::Collect;
    job.title = "Clean up unused components";
    Enqueue(std::move(job));
}

void StoreWindow::RefreshCatalog() {
    if (localCatalog_) { SetStatus("A local catalog is shown (--catalog); there is nothing to refresh."); return; }
    if (const std::string note = ServerNote(); !note.empty()) { SetStatus(note, true); return; }
    for (const Job& j : pending_) if (j.kind == JobKind::Refresh) return;
    if (running_ && running_->kind == JobKind::Refresh) return;
    Job job;
    job.kind = JobKind::Refresh;
    job.title = "Refresh the catalog";
    job.showInQueue = false;
    SetStatus("Fetching the catalog…");
    Enqueue(std::move(job));
}

void StoreWindow::ReloadInstalled() {
    if (!storeError_.empty()) return;
    Job job;
    job.kind = JobKind::Reload;
    job.title = "Read the installed apps";
    job.showInQueue = false;
    Enqueue(std::move(job));
}

// ===========================================================================
// Pictures
// ===========================================================================
void StoreWindow::RequestPicture(const std::string& url, const std::weak_ptr<UltraCanvasUIElement>& target) {
    if (url.empty() || !assets_) return;
    if (const auto known = picturePaths_.find(url); known != picturePaths_.end()) {
        if (!known->second.empty()) PictureReady(url, known->second);
        return;
    }
    pictureWaiters_[url].push_back(target);
    if (assets_->Has(url)) {
        PictureReady(url, assets_->PathFor(url));
        return;
    }
    {
        std::lock_guard<std::mutex> lock(pictureMutex_);
        if (std::find(pictureQueue_.begin(), pictureQueue_.end(), url) == pictureQueue_.end()) pictureQueue_.push_back(url);
    }
    if (!pictureThread_.joinable()) {
        std::weak_ptr<int> token = alive_;
        pictureThread_ = std::thread([this, token]() {
            for (;;) {
                std::string url;
                {
                    std::unique_lock<std::mutex> lock(pictureMutex_);
                    pictureWake_.wait(lock, [this]() { return pictureStop_ || !pictureQueue_.empty(); });
                    if (pictureStop_) return;
                    url = pictureQueue_.front();
                    pictureQueue_.pop_front();
                }
                const Result<std::string> fetched = assets_->Fetch(url);
                const std::string path = fetched ? fetched.Value() : std::string();
                app_.PostToUIThread([this, token, url, path]() {
                    if (!token.lock()) return;
                    PictureReady(url, path);
                });
            }
        });
    }
    pictureWake_.notify_one();
}

void StoreWindow::PictureReady(const std::string& url, const std::string& path) {
    picturePaths_[url] = path;   // an empty path remembers the miss, so nobody asks again this run
    const auto it = pictureWaiters_.find(url);
    if (it == pictureWaiters_.end()) return;
    std::vector<std::weak_ptr<UltraCanvasUIElement>> waiters = std::move(it->second);
    pictureWaiters_.erase(it);
    if (path.empty()) return;
    for (const auto& weak : waiters) {
        auto element = weak.lock();
        if (!element) continue;
        if (auto card = std::dynamic_pointer_cast<UltraCanvasCard>(element)) card->SetIcon(path);
        else if (auto image = std::dynamic_pointer_cast<UltraCanvasImageElement>(element)) {
            image->LoadFromFile(path);
            image->RequestRedraw();
        }
    }
}

void StoreWindow::StopPictureThread() {
    {
        std::lock_guard<std::mutex> lock(pictureMutex_);
        pictureStop_ = true;
    }
    pictureWake_.notify_all();
    if (pictureThread_.joinable()) pictureThread_.join();
}

} // namespace UltraStore::Ui
