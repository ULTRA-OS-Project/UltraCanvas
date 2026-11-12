// Apps/UltraStore/ui/StoreWindow.cpp
// The shell, the pages and the app page after the shop design; the jobs are
// in StoreJobs.cpp, the settings page in StoreSettingsPage.cpp.
// Version: 0.6.1 - Report a problem on the app page, the outcome of a report
// Version: 0.6.0 - the categories by their names in the person's language, from the catalog's fixed list
// Version: 0.5.2 - the header in less room: Refresh, the wordmark and Settings as their icons, as far as it needs
// Version: 0.5.1 - every text through Tr: the header, the filters, the buttons, the facts; dates in the language's order
// Version: 0.5.0 - the shop design: the header, the Discover sections, the Categories filters, the app page
// Version: 0.4.0 - the open order resumed, the update terms
// Version: 0.3.0 - Buy through the payment service; the licences
// Version: 0.2.0 - codes and ultrastore:// links
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StoreWindow.h"

#include "StoreTheme.h"

#include "CatalogClient.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasDataFolder.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextUtils.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasUtilsUtf8.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
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

constexpr size_t kNewAndUpdatedCount = 8;   // the numbered list of Discover, in two columns
constexpr size_t kFreeEssentialsCount = 8;
constexpr size_t kMoreFromPublisherCount = 4;
constexpr size_t kVersionHistoryCount = 5;

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

// A month's short name in the person's language (the standard set's).
std::string ShortMonth(int month) {
    switch (month) {
        case 1: return TrC("month-short", "Jan");
        case 2: return TrC("month-short", "Feb");
        case 3: return TrC("month-short", "Mar");
        case 4: return TrC("month-short", "Apr");
        case 5: return TrC("month-short", "May");
        case 6: return TrC("month-short", "Jun");
        case 7: return TrC("month-short", "Jul");
        case 8: return TrC("month-short", "Aug");
        case 9: return TrC("month-short", "Sep");
        case 10: return TrC("month-short", "Oct");
        case 11: return TrC("month-short", "Nov");
        default: return TrC("month-short", "Dec");
    }
}

// "2026-10-22" -> "22 Oct 2026", in the language's order ("22. Okt. 2026",
// "2026年10月22日"); anything else as it is.
std::string PrettyDate(const std::string& iso) {
    if (iso.size() < 10 || iso[4] != '-' || iso[7] != '-') return iso;
    const int month = std::atoi(iso.substr(5, 2).c_str());
    if (month < 1 || month > 12) return iso;
    std::string day = iso.substr(8, 2);
    if (day[0] == '0') day.erase(0, 1);
    return LocalisationDetail::Substitute(TrC("day, month and year", "{0} {1} {2}"), {day, ShortMonth(month), iso.substr(0, 4)});
}

// The first `bytes` of a text and "…", never cutting a character in two.
std::string Shorten(const std::string& text, size_t bytes) {
    if (text.size() <= bytes + 2) return text;
    return text.substr(0, utf8_align_boundary(text, bytes)) + "…";
}

// What a component is, as a person reads it.
std::string KindLabel(ComponentKind kind) {
    switch (kind) {
        case ComponentKind::Runtime: return Tr("Runtime");
        case ComponentKind::Library: return Tr("Library");
        case ComponentKind::App: break;
    }
    return Tr("App");
}

// A language code as a person reads it, for the information column.
std::string LanguageName(const std::string& code) {
    static const std::map<std::string, std::string> kNames = {
        {"en", "English"}, {"de", "Deutsch"}, {"fr", "Français"}, {"es", "Español"}, {"it", "Italiano"},
        {"nl", "Nederlands"}, {"pt", "Português"}, {"pt-BR", "Português (Brasil)"}, {"pl", "Polski"},
        {"cs", "Čeština"}, {"da", "Dansk"}, {"sv", "Svenska"}, {"nb", "Norsk"}, {"fi", "Suomi"},
        {"el", "Ελληνικά"}, {"hu", "Magyar"}, {"ro", "Română"}, {"bg", "Български"}, {"hr", "Hrvatski"},
        {"sk", "Slovenčina"}, {"sl", "Slovenščina"}, {"et", "Eesti"}, {"lv", "Latviešu"}, {"lt", "Lietuvių"},
        {"ga", "Gaeilge"}, {"mt", "Malti"}, {"uk", "Українська"}, {"tr", "Türkçe"}, {"ru", "Русский"},
        {"ar", "العربية"}, {"he", "עברית"}, {"hi", "हिन्दी"}, {"id", "Bahasa Indonesia"}, {"ja", "日本語"},
        {"ko", "한국어"}, {"th", "ไทย"}, {"vi", "Tiếng Việt"}, {"zh", "中文"}, {"zh-Hans", "中文 (简体)"},
        {"zh-Hant", "中文 (繁體)"}, {"is", "Íslenska"}, {"sr", "Српски"},
    };
    const auto it = kNames.find(code);
    return it == kNames.end() ? code : it->second;
}

std::shared_ptr<UltraCanvasBadge> Badge(const std::string& id, const std::string& text, BadgeVariant variant) {
    return Theme::Pin(CreateBadge(id, 0, 0, text, variant));
}

// A fact as a small grey pill: "30-day free trial", "PEGI 3".
std::shared_ptr<UltraCanvasContainer> FactPill(const std::string& id, const std::string& text) {
    auto pill = Theme::Row(id, 0);
    pill->SetBackgroundColor(Theme::kSurfaceMuted);
    pill->SetBorderRadius(14.0f);
    pill->SetPadding(5.0f, 12.0f);
    auto label = Theme::Text(id + ".text", text, 13.0f, FontWeight::Bold, Theme::kTextBody);
    label->SetWrap(TextWrap::WrapNone);
    pill->AddChild(label);
    return pill;
}

// A label and its value, one line of a facts list.
std::shared_ptr<UltraCanvasContainer> FactLine(const std::string& id, const std::string& label, const std::string& value) {
    auto row = Theme::Row(id, Theme::kGap);
    row->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);
    auto key = Theme::Caption(id + ".key", label);
    key->SetElementSize(CSSLayout::Dimension::Px(96), CSSLayout::Dimension::Auto());
    row->AddChild(key);
    auto text = Theme::Text(id + ".value", value, 14.0f, FontWeight::Normal, Theme::kTextPrimary);
    text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    row->AddChild(text);
    return row;
}

std::shared_ptr<UltraCanvasContainer> MakeScrollColumn(const std::string& id) {
    auto list = CreateScrollableContainer(id, 0, 0, 0, 0);
    // The bar only when the content does not fit, not always.
    ContainerStyle style = list->GetContainerStyle();
    style.forceShowVerticalScrollbar = false;
    style.autoShowScrollbars = true;
    style.autoShowHorizontalScrollbar = false;
    list->SetContainerStyle(style);
    list->layout.SetFlexColumn().SetFlexGap(Theme::kSectionGap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    // Room on the right for the vertical scrollbar, so nothing runs under
    // it and the pane never needs a horizontal one.
    list->SetPadding(Theme::kPagePadding, Theme::kPagePadding + 14.0f, Theme::kPagePadding, Theme::kPagePadding);
    list->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    return list;
}

// A wrapping grid of cards.
std::shared_ptr<UltraCanvasContainer> MakeGrid(const std::string& id) {
    auto grid = Theme::Row(id, 18.0f, true);
    grid->layout.SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    return grid;
}

// The design's card: 20 px corners, a 60 px icon, Manrope for the title.
CardStyle CardStyleFor(float radius = Theme::kCardRadius, float padding = Theme::kCardPadding, float iconSize = 60.0f) {
    CardStyle style = CardStyle::Default();
    style.backgroundColor = Theme::kSurface;
    style.hoverBackgroundColor = Theme::kSurfaceSoft;
    style.selectedBackgroundColor = Theme::kAccentFaint;
    style.borderColor = Theme::kBorder;
    style.highlightBorderColor = Theme::kAccent;
    style.cornerRadius = radius;
    style.padding = padding;
    style.gap = 12.0f;
    style.iconSize = iconSize;
    style.iconGap = 14.0f;
    style.titleFont.fontFamily = Theme::kDisplayFont;
    style.titleFont.fontSize = 17.0;
    style.titleFont.fontWeight = FontWeight::Bold;
    style.subtitleFont.fontFamily = Theme::kBodyFont;
    style.subtitleFont.fontSize = 13.0;
    style.titleColor = Theme::kTextPrimary;
    style.subtitleColor = Theme::kTextSecondary;
    return style;
}

std::string OsLabel(OS os) {
    switch (os) {
        case OS::Linux: return "Linux";
        case OS::Windows: return "Windows";
        case OS::MacOS: return "macOS";
        case OS::UltraOS: return Tr("ULTRA OS");
        case OS::IOS: return "iOS";
        case OS::Android: return "Android";
        case OS::Wasm: return Tr("Browser");
        default: return OSName(os);
    }
}

// "min-os=11.0" among a target's variants, or "".
std::string MinOsOf(const Target& target) {
    for (const std::string& v : target.variants) if (v.rfind("min-os=", 0) == 0) return v.substr(7);
    return {};
}

// The catalog's price is dot-decimal whatever the desktop's locale.
double PriceNumber(const Pricing& pricing) {
    if (pricing.model == PricingModel::Free || pricing.price.empty()) return 0.0;
    double value = 0.0;
    UltraCanvas::TryParseFloat(pricing.price, value);
    return value;
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
    config.title = TrF("UltraStore {0}", ULTRASTORE_VERSION);
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
    // The header's level is measured after each frame that laid it out.
    window_->onFrameRendered = [this](const WindowFrameTiming& timing) {
        if (timing.laidOut) FitHeader();
    };

    RebuildAllPages();
    LoadSettingsIntoPage();
    ShowPage(kDiscover);

    if (settings_.refreshOnStart && !localCatalog_ && ServerNote().empty()) RefreshCatalog();
    return true;
}

void StoreWindow::Show() {
    if (window_) window_->Show();
}

std::shared_ptr<UltraCanvasButton> StoreWindow::MakeNavButton(const std::string& id, const std::string& text, float width) {
    auto b = Theme::Button("usNav." + id, text, width, Theme::ButtonKind::Ghost);
    b->onClick = [this, id]() {
        if (currentPage_ == id) return;
        detailsAppId_.clear();
        ShowPage(id);
    };
    navButtons_[id] = b;
    return b;
}

void StoreWindow::BuildHeader() {
    header_ = Theme::Row("usHeader", Theme::kGap);
    header_->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(Theme::kHeaderHeight));
    header_->SetPadding(0, Theme::kPagePadding);
    header_->SetBackgroundColor(Theme::kSurface);

    // The wordmark: the app icon and "UltraStore" in Manrope; it is the way
    // home from anywhere.
    logoButton_ = Theme::Button("usLogo", "UltraStore", 164, Theme::ButtonKind::Ghost, 48);
    {
        ButtonStyle s = Theme::ButtonStyleFor(Theme::ButtonKind::Ghost, 48);
        s.fontFamily = Theme::kDisplayFont;
        s.fontSize = 21.0f;
        s.fontWeight = FontWeight::Bold;
        s.normalTextColor = s.hoverTextColor = s.pressedTextColor = Theme::kTextPrimary;
        logoButton_->SetStyle(s);
    }
    logoButton_->SetIcon(Theme::AppIcon("UltraStore.png"));
    logoButton_->SetIconSize(36, 36);
    logoButton_->SetTooltip(TrF("UltraStore {0} - the ULTRA OS app store", ULTRASTORE_VERSION));
    logoButton_->onClick = [this]() {
        detailsAppId_.clear();
        if (searchInput_) searchInput_->SetText("");
        search_.clear();
        RebuildDiscover();
        ShowPage(kDiscover);
    };
    header_->AddChild(logoButton_);

    // The sections as pills.
    auto nav = Theme::Row("usNav", 4.0f);
    nav->AddChild(MakeNavButton(kDiscover, Tr("Discover"), 96));
    nav->AddChild(MakeNavButton(kCategories, Tr("Categories"), 108));
    nav->AddChild(MakeNavButton(kInstalled, Tr("Installed"), 96));
    nav->AddChild(MakeNavButton(kUpdates, Tr("Updates"), 92));
    updatesBadge_ = Badge("usNav.updates.badge", "0", BadgeVariant::Neutral);
    updatesBadge_->SetColor(Theme::kTextPrimary);
    updatesBadge_->SetVisible(false);
    nav->AddChild(updatesBadge_);
    header_->AddChild(nav);

    // The search pill: it fills the middle, and turns Discover into the
    // results while it holds text.
    searchInput_ = Theme::Pin(CreateTextInput("usSearch", 0, 0, 240, Theme::kControlHeight + 4));
    searchInput_->SetStyle(Theme::SearchStyle(searchInput_->GetStyle()));
    searchInput_->SetFontSize(15.0f);
    searchInput_->SetPlaceholder(Tr("Search apps, publishers, tags"));
    searchInput_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    // The pill gives way first when the labels around it are long (a
    // translation): it may shrink to this, the sections and buttons never.
    searchInput_->boxConstraints = CSSLayout::BoxConstraints{};
    searchInput_->boxConstraints->minWidth = CSSLayout::Dimension::Px(kSearchMinWidth);
    searchInput_->onTextChanged = [this](const std::string& text) {
        search_ = text;
        if (!search_.empty() && currentPage_ != kDiscover) {
            detailsAppId_.clear();
            ShowPage(kDiscover);
        }
        RebuildDiscover();
    };
    header_->AddChild(searchInput_);

    auto refresh = Theme::Button("usRefresh", Tr("Refresh"), 96, Theme::ButtonKind::Ghost);
    refresh->SetIcon(Theme::Icon("rotate-right.svg"));
    refresh->SetTooltip(Tr("Fetch the catalog from the store's servers"));
    refresh->onClick = [this]() { RefreshCatalog(); };
    header_->AddChild(refresh);
    refreshButton_ = refresh;

    // Where the design puts "Sign in": the store has no accounts, so the
    // settings live here instead.
    auto settings = Theme::Button("usSettings", Tr("Settings"), 104, Theme::ButtonKind::Secondary);
    settings->SetTooltip(Tr("Settings"));
    settings->onClick = [this]() {
        if (currentPage_ == kSettings) return;
        detailsAppId_.clear();
        ShowPage(kSettings);
    };
    header_->AddChild(settings);
    settingsButton_ = settings;
}

float StoreWindow::HeaderNeed() const {
    // What the header takes at the level it is at: each part as wide as it
    // is (a button as wide as its text), the search field at its least.
    float need = header_->GetPaddingLeft() + header_->GetPaddingRight();
    int parts = 0;
    for (const auto& child : header_->GetChildren()) {
        auto* part = dynamic_cast<UltraCanvasUIElement*>(child.get());
        if (!part || !part->IsVisible()) continue;
        need += part == searchInput_.get() ? kSearchMinWidth : part->GetWidth();
        ++parts;
    }
    return need + Theme::kGap * static_cast<float>(std::max(0, parts - 1));
}

void StoreWindow::FitHeader() {
    if (!header_ || headerLevelPosted_) return;
    const float room = header_->GetWidth();
    if (room <= 0.0f) return;
    const float need = HeaderNeed();
    headerNeed_[headerLevel_] = need;
    int level = headerLevel_;
    if (need > room + 0.5f && level < kHeaderLevels - 1) ++level;
    else if (level > 0 && headerNeed_[level - 1] > 0.0f && headerNeed_[level - 1] <= room) --level;
    if (level == headerLevel_) return;
    // Not from inside the frame: the next turn of the loop, which this wakes.
    headerLevelPosted_ = true;
    std::weak_ptr<int> token = alive_;
    app_.PostToUIThread([this, token, level]() {
        if (token.expired()) return;
        headerLevelPosted_ = false;
        ApplyHeaderLevel(level);
    });
}

void StoreWindow::ApplyHeaderLevel(int level) {
    headerLevel_ = std::clamp(level, 0, kHeaderLevels - 1);
    const float height = Theme::kControlHeight;
    if (refreshButton_) {
        const bool iconOnly = headerLevel_ >= 1;
        refreshButton_->SetText(iconOnly ? std::string() : Tr("Refresh"));
        refreshButton_->SetElementSize(CSSLayout::Dimension::Px(iconOnly ? height : 96.0f), CSSLayout::Dimension::Px(height));
    }
    if (logoButton_) {
        const bool iconOnly = headerLevel_ >= 2;
        logoButton_->SetText(iconOnly ? std::string() : std::string("UltraStore"));
        logoButton_->SetElementSize(CSSLayout::Dimension::Px(iconOnly ? 48.0f : 164.0f), CSSLayout::Dimension::Px(48.0f));
    }
    if (settingsButton_) {
        const bool iconOnly = headerLevel_ >= 3;
        settingsButton_->SetText(iconOnly ? std::string() : Tr("Settings"));
        if (iconOnly) settingsButton_->SetIcon(Theme::Icon("settings.svg"));
        else settingsButton_->SetIcon(std::shared_ptr<UCImage>());
        settingsButton_->SetElementSize(CSSLayout::Dimension::Px(iconOnly ? height : 104.0f), CSSLayout::Dimension::Px(height));
    }
    header_->InvalidateLayout();
    header_->RequestRedraw();
}

void StoreWindow::BuildShell() {
    root_ = Theme::Column("usRoot", 0);
    root_->SetBackgroundColor(Theme::kPageBackground);

    BuildHeader();

    pageHost_ = Theme::Column("usPages", 0);
    pageHost_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);

    // Discover: built section by section in RebuildDiscover.
    BuildScrollPage(kDiscover, discoverBody_);

    // Categories: the filters on the left, the grid on the right.
    {
        std::shared_ptr<UltraCanvasContainer> body;
        BuildScrollPage(kCategories, body);
        auto head = Theme::Column("usC.head", 4.0f);
        head->AddChild(Theme::Title("usC.title", Tr("Browse apps")));
        head->AddChild(Theme::Caption("usC.subtitle", Tr("Every app in the signed catalog, by category. Pick a category and a price to narrow the list.")));
        body->AddChild(head);

        auto split = Theme::Row("usC.split", 32.0f);
        split->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);

        auto aside = Theme::Column("usC.aside", Theme::kSectionGap);
        aside->SetElementSize(CSSLayout::Dimension::Px(Theme::kAsideWidth), CSSLayout::Dimension::Auto());
        {
            auto group = Theme::Column("usC.cat", Theme::kInnerGap);
            group->AddChild(Theme::Eyebrow("usC.cat.title", Tr("Category")));
            categoryChips_ = Theme::Pin(CreateChipGroup("usCategoryChips", 0, 0, 0, 0));
            categoryChips_->SetChipStyle(Theme::ChipStyleFor());
            categoryChips_->SetSpacing(6.0f);
            categoryChips_->SetSelectionMode(ChipGroupSelectionMode::Single);
            categoryChips_->SetAllowNoSelection(false);
            categoryChips_->onSelectionChanged = [this](int index) {
                category_ = index <= 0 || index > static_cast<int>(categories_.size()) ? std::string() : categories_[index - 1];
                RebuildCategories();
            };
            group->AddChild(categoryChips_);
            aside->AddChild(group);
        }
        {
            auto group = Theme::Column("usC.price", Theme::kInnerGap);
            group->AddChild(Theme::Eyebrow("usC.price.title", Tr("Price")));
            auto segment = Theme::Row("usC.price.seg", 2.0f);
            segment->SetBackgroundColor(Theme::kSurfaceMuted);
            segment->SetBorderRadius(Theme::kControlHeight / 2.0f);
            segment->SetPadding(4.0f);
            for (const auto& [key, label] : std::vector<std::pair<std::string, std::string>>{{"all", Tr("All")}, {"free", Tr("Free")}, {"paid", Tr("Paid")}}) {
                auto b = Theme::Button("usC.price." + key, label, 72, Theme::ButtonKind::Ghost, 32);
                b->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
                const std::string value = key;
                b->onClick = [this, value]() {
                    if (priceFilter_ == value) return;
                    priceFilter_ = value;
                    RebuildCategories();
                };
                priceButtons_[key] = b;
                segment->AddChild(b);
            }
            group->AddChild(segment);
            aside->AddChild(group);
        }
        {
            auto group = Theme::Column("usC.more", Theme::kInnerGap);
            group->AddChild(Theme::Eyebrow("usC.more.title", Tr("More")));
            openSourceBox_ = Theme::Pin(UltraCanvasCheckbox::CreateCheckbox("usC.oss", 0, 0, 200, 32, Tr("Open source only")));
            openSourceBox_->SetFont(Theme::kBodyFont, 14.0f);
            openSourceBox_->onStateChanged = [this](CheckedState, CheckedState) { RebuildCategories(); };
            group->AddChild(openSourceBox_);
            hideInstalledBox_ = Theme::Pin(UltraCanvasCheckbox::CreateCheckbox("usC.hide", 0, 0, 200, 32, Tr("Hide installed")));
            hideInstalledBox_->SetFont(Theme::kBodyFont, 14.0f);
            hideInstalledBox_->onStateChanged = [this](CheckedState, CheckedState) { RebuildCategories(); };
            group->AddChild(hideInstalledBox_);
            aside->AddChild(group);
        }
        split->AddChild(aside);

        auto main = Theme::Column("usC.main", Theme::kGap);
        main->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        auto bar = Theme::Row("usC.bar", Theme::kGap);
        categoryHeading_ = Theme::Heading("usC.heading", Tr("All apps"));
        categoryHeading_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        bar->AddChild(categoryHeading_);
        bar->AddChild(Theme::Caption("usC.sortLabel", Tr("Sort by")));
        sortDropdown_ = Theme::Pin(CreateDropdown("usC.sort", 0, 0, 170, Theme::kControlHeight));
        sortDropdown_->AddItem(Tr("Newest release"));
        sortDropdown_->AddItem(Tr("Name A to Z"));
        sortDropdown_->AddItem(Tr("Price, low to high"));
        sortDropdown_->SetSelectedIndex(0, false);
        sortDropdown_->onSelectionChanged = [this](int index, const DropdownItem&) {
            sortMode_ = index;
            RebuildCategories();
        };
        bar->AddChild(sortDropdown_);
        main->AddChild(bar);
        categoryGrid_ = MakeGrid("usC.grid");
        main->AddChild(categoryGrid_);
        split->AddChild(main);
        body->AddChild(split);
    }
    // Installed: the apps, then the components, and the clean-up.
    {
        BuildScrollPage(kInstalled, installedBody_);
    }
    // Updates.
    {
        BuildScrollPage(kUpdates, updatesBody_);
    }
    // Settings.
    {
        auto page = BuildSettingsPage();
        pages_[kSettings] = page;
        pageHost_->AddChild(page);
        page->SetVisible(false);
    }
    // Details: one app, with the breadcrumb as the way back.
    {
        detailsPage_ = Theme::Column(kDetails, 0);
        detailsPage_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        detailsBody_ = MakeScrollColumn("usDetailsBody");
        detailsPage_->AddChild(detailsBody_);
        pages_[kDetails] = detailsPage_;
        pageHost_->AddChild(detailsPage_);
        detailsPage_->SetVisible(false);
    }

    // The download queue along the bottom.
    downloads_ = Theme::Pin(CreateCollapsiblePanel("usDownloads", Tr("Downloads")));
    downloads_->SetExpandedHeight(static_cast<float>(std::max(100, settings_.downloadsHeight)));
    downloads_->SetExpanded(settings_.downloadsExpanded);
    downloads_->SetDetail(Tr("Nothing queued"));
    downloads_->AddHeaderButton("usClearFinished", Tr("Clear finished"), [this]() { DismissFinished(); }, 112);
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
    queueList_ = CreateScrollableContainer("usQueue", 0, 0, 0, 0);
    {
        ContainerStyle style = queueList_->GetContainerStyle();
        style.forceShowVerticalScrollbar = false;
        style.autoShowScrollbars = true;
        style.autoShowHorizontalScrollbar = false;
        queueList_->SetContainerStyle(style);
        queueList_->layout.SetFlexColumn().SetFlexGap(Theme::kInnerGap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        queueList_->SetPadding(2, 16, 2, 2);
        queueList_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    }
    panelBody->AddChild(queueList_);

    // The status line.
    auto statusRow = Theme::Row("usStatusRow", 0);
    statusRow->layoutItem.SetFlexShrink(0);
    statusRow->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(Theme::kStatusHeight));
    statusRow->SetPadding(0, Theme::kPagePadding);
    statusRow->SetBackgroundColor(Theme::kSurfaceSoft);
    statusLabel_ = Theme::Caption("usStatus", "");
    statusLabel_->SetWrap(TextWrap::WrapNone);
    statusLabel_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    statusRow->AddChild(statusLabel_);

    root_->AddChild(header_);
    root_->AddChild(Theme::Rule("usHeaderRule"));
    root_->AddChild(pageHost_);
    root_->AddChild(Theme::Rule("usFooterRule"));
    root_->AddChild(downloads_);
    root_->AddChild(statusRow);
    window_->AddChild(root_);
}

std::shared_ptr<UltraCanvasContainer> StoreWindow::BuildScrollPage(const std::string& id,
                                                                   std::shared_ptr<UltraCanvasContainer>& body) {
    auto page = Theme::Column(id, 0);
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    body = MakeScrollColumn(id + ".body");
    page->AddChild(body);
    pages_[id] = page;
    pageHost_->AddChild(page);
    page->SetVisible(false);
    return page;
}

void StoreWindow::ShowPage(const std::string& id) {
    currentPage_ = id;
    for (auto& [key, page] : pages_) page->SetVisible(key == id);
    // The pill of the section shown is the soft blue one; an app page keeps
    // the pill of the section it was opened from.
    const std::string lit = id == kDetails ? pageBeforeDetails_ : id;
    for (auto& [key, button] : navButtons_) {
        Theme::Style(button, key == lit ? Theme::ButtonKind::Soft : Theme::ButtonKind::Ghost);
    }
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
        TrF("\"{0}\" is still running. Cancel it and close UltraStore?\nA download continues where it stopped the next time; nothing is left half installed.", running_->title),
        Tr("Work in progress"),
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
        if (const auto bought = engine_->Registry().Licences()) licences_ = bought.Value();
    }
    assets_ = std::make_unique<AssetCache>(PathToUtf8(PathFromUtf8(engine_->CacheDir()) / "assets"));
    ApplySettings();

    if (!options_.catalogFile.empty()) {
        localCatalog_ = true;
        std::string text;
        if (!ReadWholeFile(options_.catalogFile, text)) {
            catalogNote_ = TrF("Cannot read {0}", options_.catalogFile);
        } else if (const Result<Catalog> parsed = ParseCatalog(text); !parsed) {
            catalogNote_ = options_.catalogFile + ": " + parsed.error.message;
        } else {
            SetCatalog(std::make_shared<const Catalog>(parsed.Value()), TrF("Local catalog {0} (sequence {1}), not verified", options_.catalogFile, parsed.Value().sequence));
        }
        return;
    }
    if (const Result<Catalog> cached = client_->LoadCached()) {
        catalogState_ = client_->State();
        SetCatalog(std::make_shared<const Catalog>(cached.Value()),
                   catalogState_.fetchedAt.empty() ? TrF("Cached catalog, sequence {0}", cached.Value().sequence)
                                                   : TrF("Cached catalog, sequence {0}, fetched {1} from {2}", cached.Value().sequence, catalogState_.fetchedAt, catalogState_.fetchedFrom));
    } else {
        catalogNote_ = ServerNote().empty() ? Tr("No catalog yet: press Refresh.") : ServerNote();
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
        return Tr("No store server is configured yet: enter one under Settings, or start with --catalog <file>.");
    if (settings_.TrustedKeys().empty())
        return Tr("No signing key is trusted yet: add the store's public key under Settings.");
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

std::vector<AppListing> StoreWindow::Listings(const ListingFilter& filter) const {
    if (!catalog_) return {};
    return ListApps(*catalog_, platform_, installed_, filter);
}

void StoreWindow::SortListings(std::vector<AppListing>& apps, int mode) {
    auto lowerName = [](const AppListing& l) {
        std::string s = l.app->name;
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    auto newestDate = [](const AppListing& l) -> std::string {
        if (l.resolution.release) return l.resolution.release->date;
        return l.app->releases.empty() ? std::string() : l.app->releases.front().date;
    };
    switch (mode) {
        case 1:
            std::stable_sort(apps.begin(), apps.end(), [&](const AppListing& a, const AppListing& b) { return lowerName(a) < lowerName(b); });
            break;
        case 2:
            std::stable_sort(apps.begin(), apps.end(), [&](const AppListing& a, const AppListing& b) {
                const double pa = PriceNumber(a.app->pricing), pb = PriceNumber(b.app->pricing);
                return pa != pb ? pa < pb : lowerName(a) < lowerName(b);
            });
            break;
        default:
            std::stable_sort(apps.begin(), apps.end(), [&](const AppListing& a, const AppListing& b) {
                const std::string da = newestDate(a), db = newestDate(b);
                return da != db ? da > db : lowerName(a) < lowerName(b);
            });
            break;
    }
}

std::vector<AppListing> StoreWindow::EditorsPicks() const {
    // The admin's picks (the catalog's `featured`), newest release first.
    std::vector<AppListing> picks;
    for (const AppListing& l : Listings(BaseFilter())) if (l.app->featured && l.app->lifecycle == Lifecycle::Active) picks.push_back(l);
    SortListings(picks, 0);
    return picks;
}

std::optional<AppListing> StoreWindow::FeaturedApp() const {
    // The hero: the admin's newest pick this machine can install; without
    // a pick, the newest release the store can show - the apps with a
    // package for this machine first, the latest date among them.
    for (const AppListing& l : EditorsPicks()) if (l.Available()) return l;
    std::vector<AppListing> apps = Listings(BaseFilter());
    if (apps.empty()) return std::nullopt;
    SortListings(apps, 0);
    for (const AppListing& l : apps) if (l.Available() && l.app->lifecycle == Lifecycle::Active) return l;
    return apps.front();
}

std::optional<InstallPlan> StoreWindow::PlanFor(const AppListing& l) const {
    if (!catalog_ || !l.resolution.release || !l.Available()) return std::nullopt;
    InstalledVersions have;
    for (const InstalledRecord& r : installed_) have.entries.emplace_back(r.id, r.version);
    const auto plan = PackageResolver::Plan(*catalog_, *l.app, *l.resolution.release, platform_, have);
    if (!plan) return std::nullopt;
    return plan.Value();
}

const Publisher* StoreWindow::PublisherOf(const Component& app) const {
    if (!catalog_ || app.publisher.empty()) return nullptr;
    for (const Publisher& p : catalog_->publishers) if (p.id == app.publisher || p.name == app.publisher) return &p;
    return nullptr;
}

std::string StoreWindow::PublisherName(const Component& app) const {
    const Publisher* p = PublisherOf(app);
    return p && !p->name.empty() ? p->name : app.publisher;
}

std::string StoreWindow::CategoryOf(const Component& app) const {
    return PrimaryCategory(app);
}

std::string StoreWindow::LanguagesOf(const Component& app) const {
    std::set<std::string> codes;
    for (const auto& [code, text] : app.summary.byLanguage) codes.insert(code);
    for (const auto& [code, text] : app.description.byLanguage) codes.insert(code);
    if (codes.empty()) return {};
    // The language shown first, then the source language, then the count.
    std::vector<std::string> ordered;
    if (codes.count(lang_)) ordered.push_back(lang_);
    if (!app.sourceLanguage.empty() && app.sourceLanguage != lang_ && codes.count(app.sourceLanguage)) ordered.push_back(app.sourceLanguage);
    for (const std::string& c : codes) if (ordered.size() < 2 && std::find(ordered.begin(), ordered.end(), c) == ordered.end()) ordered.push_back(c);
    std::string line;
    for (const std::string& c : ordered) line += (line.empty() ? "" : ", ") + LanguageName(c);
    if (codes.size() > ordered.size()) line += ", +" + std::to_string(codes.size() - ordered.size());
    return line;
}

std::string StoreWindow::PriceText(const Component& app) const {
    const Pricing& pricing = app.pricing;
    if (pricing.model == PricingModel::Free) return Tr("Free");
    return PeriodPrice(pricing, DiscountedPrice(pricing, CodeFor(app)));
}

void StoreWindow::ApplyCode(const std::string& text) {
    if (NormalizeCode(text).empty()) {
        if (settings_.code.empty()) { SetStatus(Tr("Enter a code")); return; }
        settings_.code.clear();
        settings_.codeEnteredAt.clear();
        settings_.Save();
        if (codeInput_) codeInput_->SetText("");
        RebuildAllPages();
        SetStatus(Tr("The code is forgotten"));
        return;
    }
    if (!catalog_) { SetStatus(Tr("No catalog yet to check the code against: press Refresh"), true); return; }
    const CodeCheck check = CheckCode(*catalog_, text, Today());
    if (!check.Ok()) { SetStatus(TrF("Code {0}: {1}", NormalizeCode(text), check.message), true); return; }
    settings_.code = check.code->code;
    settings_.codeEnteredAt = Today();
    settings_.Save();
    if (codeInput_) codeInput_->SetText(check.code->code);
    RebuildAllPages();
    SetStatus(TrF("Code {0}: {1} - in force for {2} days", check.code->code, check.message, StoreSettings::kCodeDays));
}

void StoreWindow::OpenLink(const std::string& url) {
    // ultrastore://code/<CODE>, ultrastore://app/<id>[?code=<CODE>]
    const std::string prefix = "ultrastore://";
    if (url.rfind(prefix, 0) != 0) { SetStatus(TrF("Not a store link: {0}", url), true); return; }
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
        if (!catalog_ || !catalog_->FindApp(value)) SetStatus(TrF("The catalog does not list {0}", value), true);
        else { searchInput_->SetText(""); search_.clear(); ShowDetails(value); }
    } else { SetStatus(TrF("Unknown store link: {0}", url), true); return; }
    if (!code.empty()) ApplyCode(code);
}

const LicenceRecord* StoreWindow::Bought(const std::string& appId) const {
    for (const LicenceRecord& l : licences_) if (l.app == appId) return &l;
    return nullptr;
}

Licence StoreWindow::PurchaseOf(const std::string& appId) const {
    const LicenceRecord* bought = Bought(appId);
    if (!bought) return {};
    const auto terms = ParseLicence(bought->token);
    return terms ? terms.Value() : Licence{};
}

void StoreWindow::SetCatalog(std::shared_ptr<const Catalog> catalog, const std::string& note) {
    catalog_ = std::move(catalog);
    catalogNote_ = note;
    // A paid app's package is fetched through the URL the payment service
    // signs for the buyer: the signer runs on the job thread with the
    // registry's licence of the app.
    if (engine_) engine_->SetUrlSigner(LicensedUrlSigner(engine_->Registry(), catalog_ ? catalog_->paymentUrl : std::string()));
    // A purchase the store was closed in the middle of: picked up once,
    // with the first catalog that names the service.
    if (!resumedOrder_ && engine_ && catalog_ && !catalog_->paymentUrl.empty() && storeError_.empty()) {
        resumedOrder_ = true;
        const StoreSettings::OpenOrder open = settings_.OrderToResume(Today());
        if (!open.Empty() && !Bought(open.app)) {
            Job job;
            job.kind = JobKind::Buy;
            job.appId = open.app;
            job.resume = true;
            const Component* app = catalog_->FindApp(open.app);
            job.title = TrF("Resume buying {0}", app ? app->name : open.app);
            Enqueue(std::move(job));
        } else if (!settings_.openOrder.Empty()) {
            settings_.openOrder = {};   // too old, or bought meanwhile
            settings_.Save();
        }
    }
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

std::shared_ptr<UltraCanvasImageElement> StoreWindow::MakePicture(const std::string& id, const std::string& url, float w, float h) {
    auto image = Theme::Pin(CreateImageElement(id, w, h));
    image->SetElementSize(CSSLayout::Dimension::Px(w), CSSLayout::Dimension::Px(h));
    image->SetFitMode(ImageFitMode::Contain);
    if (!url.empty()) {
        const auto known = picturePaths_.find(url);
        if (known != picturePaths_.end()) { if (!known->second.empty()) image->LoadFromFile(known->second); }
        else RequestPicture(url, image);
    }
    return image;
}

std::shared_ptr<UltraCanvasContainer> StoreWindow::MakeHero(const AppListing& l) {   // a card, so it opens the app
    const Component& app = *l.app;
    const std::string base = catalogState_.fetchedFrom;
    auto card = Theme::Pin(CreateCard("usH"));
    CardStyle style = CardStyleFor(Theme::kCardRadius, 32.0f);
    style.backgroundColor = Theme::kAccentFaint;
    style.hoverBackgroundColor = Theme::kAccentSoft;
    card->SetStyle(style);
    auto hero = Theme::Row("usH.row", 32.0f);
    hero->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);

    auto text = Theme::Column("usH.text", Theme::kGap);
    text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    text->AddChild(Theme::Eyebrow("usH.eyebrow", Tr("Featured app"), Theme::kAccentSoftText));

    auto nameRow = Theme::Row("usH.nameRow", Theme::kGap);
    nameRow->AddChild(MakePicture("usH.icon", ResolveCatalogUrl(base, app.icon), 72, 72));
    auto names = Theme::Column("usH.names", 2.0f);
    names->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    auto name = Theme::Display("usH.name", app.name, 36.0f);
    names->AddChild(name);
    std::string line = PublisherName(app);
    if (l.resolution.release) line += (line.empty() ? "" : " · ") + TrF("Version {0}", l.resolution.release->version);
    names->AddChild(Theme::Caption("usH.version", line));
    nameRow->AddChild(names);
    text->AddChild(nameRow);

    const std::string summary = app.summary.Text(lang_);
    if (!summary.empty()) text->AddChild(Theme::Text("usH.summary", summary, 16.0f, FontWeight::Normal, Theme::kTextBody));

    auto actions = Theme::Row("usH.actions", Theme::kInnerGap, true);
    for (auto& button : PrimaryActions(l, "usH")) actions->AddChild(button);
    if (app.updates.period == UpdatePeriod::Until) actions->AddChild(Theme::Caption("usH.updates", TrF("Free updates until {0}", PrettyDate(app.updates.until))));
    text->AddChild(actions);

    auto facts = Theme::Row("usH.facts", Theme::kInnerGap, true);
    int n = 0;
    for (const std::string& fact : FactsOf(l)) facts->AddChild(FactPill("usH.fact" + std::to_string(n++), fact));
    text->AddChild(facts);
    hero->AddChild(text);

    // The first screenshot on the right, where the design has the app's
    // picture; nothing when the app has none.
    if (!app.screenshots.empty()) {
        const std::string url = ResolveCatalogUrl(base, app.screenshots.front());
        if (!url.empty()) {
            auto shot = MakePicture("usH.shot", url, 440, 275);
            shot->SetBackgroundColor(Theme::kSurface);
            shot->SetBorders(1.0f, Theme::kBorder, Theme::kRowRadius);
            shot->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
            hero->AddChild(shot);
        }
    }
    card->AddContent(hero);
    card->SetClickable(true);
    const std::string appId = app.id;
    card->onClick = [this, appId]() { ShowDetails(appId); };
    return card;
}

std::shared_ptr<UltraCanvasContainer> StoreWindow::MakeCategoryPills() {
    auto row = Theme::Row("usCats", Theme::kInnerGap, true);
    int n = 0;
    for (const std::string& c : categories_) {
        auto pill = Theme::Button("usCats." + std::to_string(n++), CategoryName(c), 40, Theme::ButtonKind::Secondary);
        const std::string category = c;
        pill->onClick = [this, category]() {
            category_ = category;
            detailsAppId_.clear();
            RebuildCategories();
            ShowPage(kCategories);
        };
        row->AddChild(pill);
    }
    return row;
}

std::shared_ptr<UltraCanvasContainer> StoreWindow::MakeWhyStrip() {
    auto section = Theme::Section("usWhy", Tr("Why UltraStore"));
    auto grid = Theme::Row("usWhy.grid", 18.0f, true);
    grid->layout.SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    struct Reason { std::string title, text; };
    const Reason reasons[] = {
        {Tr("Signed catalog"), Tr("Every catalog is signed by the store's key and checked on this machine before it is shown; a stale or tampered copy is refused.")},
        {Tr("Small downloads"), Tr("Apps share the UltraCanvas runtime and the libraries. Only the layers this machine lacks are downloaded.")},
        {Tr("Try before you buy"), Tr("A paid app with a trial installs as the full app for the trial days; the purchase keeps what you have.")},
        {Tr("Linux, macOS, Windows"), Tr("One catalog, one store: each app comes as the build for your system and processor.")},
    };
    int n = 0;
    for (const Reason& r : reasons) {
        auto tile = Theme::Surface("usWhy." + std::to_string(n++), 6.0f, Theme::kSurfaceSoft);
        tile->SetElementSize(CSSLayout::Dimension::Px(Theme::kCardWidth - 40.0f), CSSLayout::Dimension::Auto());
        tile->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        tile->AddChild(Theme::Text("usWhy." + std::to_string(n) + ".title", r.title, 16.0f, FontWeight::Bold, Theme::kTextPrimary, Theme::kDisplayFont));
        tile->AddChild(Theme::Text("usWhy." + std::to_string(n) + ".text", r.text, 13.0f, FontWeight::Normal, Theme::kTextBody));
        grid->AddChild(tile);
    }
    section->AddChild(grid);
    return section;
}

std::shared_ptr<UltraCanvasContainer> StoreWindow::MakeCodeRow() {
    auto surface = Theme::Surface("usCode", Theme::kInnerGap, Theme::kSurfaceSoft);
    surface->AddChild(Theme::Text("usCode.title", Tr("Have a partner or discount code?"), 16.0f, FontWeight::Bold, Theme::kTextPrimary, Theme::kDisplayFont));
    surface->AddChild(Theme::Caption("usCode.hint", TrF("A code from a partner's QR code or page, or a discount code: the prices show what it takes off, for {0} days, and the partner is credited when you buy.", StoreSettings::kCodeDays)));
    auto row = Theme::Row("usCode.row", Theme::kInnerGap, true);
    codeInput_ = Theme::Pin(CreateTextInput("usCode.input", 0, 0, 220, Theme::kControlHeight));
    codeInput_->SetStyle(Theme::SearchStyle(codeInput_->GetStyle()));
    codeInput_->SetPlaceholder(TrC("discount code", "Code"));
    codeInput_->SetText(settings_.code);
    row->AddChild(codeInput_);
    auto apply = Theme::Button("usCode.apply", Tr("Apply"), 88, Theme::ButtonKind::Primary);
    apply->onClick = [this]() { ApplyCode(codeInput_->GetText()); };
    row->AddChild(apply);
    if (const PromoCode* code = CodeInForce()) {
        const std::string what = DiscountLabel(*code);
        row->AddChild(Badge("usCode.inForce", TrF("In force: {0}", code->code) + (what.empty() ? "" : ", " + what), BadgeVariant::Successful));
        auto forget = Theme::Button("usCode.forget", Tr("Forget"), 80, Theme::ButtonKind::Ghost);
        forget->onClick = [this]() { ApplyCode(""); };
        row->AddChild(forget);
    }
    surface->AddChild(row);
    return surface;
}

void StoreWindow::RebuildDiscover() {
    if (!discoverBody_) return;
    discoverBody_->ClearChildren();
    std::string note = storeError_.empty() ? std::string() : TrF("The store folder could not be opened: {0}", storeError_);
    if (note.empty() && !catalog_) note = catalogNote_;
    else if (note.empty() && !ServerNote().empty() && !localCatalog_) note = ServerNote();
    if (!note.empty()) {
        auto surface = Theme::Surface("usD.note", 4.0f, Theme::kSurfaceSoft);
        surface->AddChild(Theme::Text("usD.note.text", note, 14.0f, FontWeight::Normal, Theme::kTextBody));
        discoverBody_->AddChild(surface);
    }
    if (catalog_ && !search_.empty()) {
        // The results take the page while the search field holds text.
        ListingFilter f = BaseFilter();
        f.search = search_;
        std::vector<AppListing> apps = Listings(f);
        auto section = Theme::Section("usD.results", TrF("Results for \"{0}\"", search_), TrN("{n} app", "{n} apps", static_cast<long long>(apps.size())));
        auto grid = MakeGrid("usD.results.grid");
        FillGrid(grid, apps, "usDR", TrF("No app matches \"{0}\".", search_));
        section->AddChild(grid);
        discoverBody_->AddChild(section);
        discoverBody_->ScrollToVertical(0);
        discoverBody_->InvalidateLayout();
        discoverBody_->RequestRedraw();
        return;
    }
    if (catalog_) {
        if (const std::optional<AppListing> featured = FeaturedApp()) discoverBody_->AddChild(MakeHero(*featured));
        if (!categories_.empty()) {
            auto section = Theme::Column("usD.cats", Theme::kInnerGap);
            section->AddChild(Theme::Eyebrow("usD.cats.title", Tr("Browse by category")));
            section->AddChild(MakeCategoryPills());
            discoverBody_->AddChild(section);
        }
        // Editor's picks: the admin's featured apps, the hero's among them.
        {
            std::vector<AppListing> picks = EditorsPicks();
            if (picks.size() > kFreeEssentialsCount) picks.resize(kFreeEssentialsCount);
            if (!picks.empty()) {
                auto section = Theme::Section("usD.picks", Tr("Editor's picks"), Tr("Apps the ULTRA OS team uses every day."));
                auto grid = MakeGrid("usD.picks.grid");
                FillGrid(grid, picks, "usDP", "");
                section->AddChild(grid);
                discoverBody_->AddChild(section);
            }
        }
        // New and updated: the newest releases, numbered, in two columns.
        {
            std::vector<AppListing> apps = Listings(BaseFilter());
            SortListings(apps, 0);
            if (apps.size() > kNewAndUpdatedCount) apps.resize(kNewAndUpdatedCount);
            if (!apps.empty()) {
                auto section = Theme::Section("usD.new", Tr("New and updated"), Tr("Fresh releases from the signed catalog, newest first."));
                auto columns = Theme::Row("usD.new.cols", 18.0f, true);
                columns->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);
                const size_t half = (apps.size() + 1) / 2;
                for (size_t c = 0; c < 2; ++c) {
                    auto column = Theme::Column("usD.new.col" + std::to_string(c), Theme::kInnerGap);
                    column->SetElementSize(CSSLayout::Dimension::Px(Theme::kCardWidth + 100.0f), CSSLayout::Dimension::Auto());
                    column->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
                    for (size_t i = c * half; i < std::min(apps.size(), (c + 1) * half); ++i) {
                        column->AddChild(MakeAppRow(apps[i], "usDN" + std::to_string(i), static_cast<int>(i) + 1));
                    }
                    if (column->GetChildren().empty()) continue;
                    columns->AddChild(column);
                }
                section->AddChild(columns);
                discoverBody_->AddChild(section);
            }
        }
        // Free essentials.
        {
            std::vector<AppListing> apps;
            for (const AppListing& l : Listings(BaseFilter())) if (l.app->pricing.model == PricingModel::Free && l.Available()) apps.push_back(l);
            if (apps.size() > kFreeEssentialsCount) apps.resize(kFreeEssentialsCount);
            if (!apps.empty()) {
                auto section = Theme::Section("usD.free", Tr("Free essentials"), Tr("Free apps every ULTRA OS machine can use."));
                auto grid = MakeGrid("usD.free.grid");
                FillGrid(grid, apps, "usDF", "");
                section->AddChild(grid);
                discoverBody_->AddChild(section);
            }
        }
        discoverBody_->AddChild(MakeWhyStrip());
    }
    discoverBody_->AddChild(MakeCodeRow());
    discoverBody_->ScrollToVertical(0);
    discoverBody_->InvalidateLayout();
    discoverBody_->RequestRedraw();
}

void StoreWindow::RebuildCategories() {
    if (!categoryGrid_) return;
    // The chips: "All apps" first, then the catalog's categories, each with
    // its count under the price filter; rebuilt only when the categories
    // changed, so a click does not rebuild the row under the pointer.
    std::vector<std::string> wanted = {Tr("All apps")};
    for (const std::string& id : categories_) wanted.push_back(CategoryName(id));
    bool same = categoryChips_->GetChipCount() == static_cast<int>(wanted.size());
    for (int i = 0; same && i < categoryChips_->GetChipCount(); ++i) same = categoryChips_->IndexOf(wanted[i]) == i;
    if (!same) {
        categoryChips_->ClearChips();
        for (const std::string& c : wanted) categoryChips_->AddChip(c);
        const auto it = std::find(categories_.begin(), categories_.end(), category_);
        categoryChips_->SetSelectedIndex(it == categories_.end() ? 0 : static_cast<int>(it - categories_.begin()) + 1);
    }
    for (auto& [key, button] : priceButtons_) {
        Theme::Style(button, key == priceFilter_ ? Theme::ButtonKind::Secondary : Theme::ButtonKind::Ghost, 32);
    }
    std::vector<AppListing> apps;
    if (catalog_) {
        ListingFilter f = BaseFilter();
        f.category = category_;
        const bool openSourceOnly = openSourceBox_ && openSourceBox_->IsChecked();
        const bool hideInstalled = hideInstalledBox_ && hideInstalledBox_->IsChecked();
        for (const AppListing& l : Listings(f)) {
            const bool paid = l.app->pricing.model != PricingModel::Free;
            if (priceFilter_ == "free" && paid) continue;
            if (priceFilter_ == "paid" && !paid) continue;
            if (openSourceOnly && !l.app->openSource) continue;
            if (hideInstalled && l.Installed()) continue;
            apps.push_back(l);
        }
        SortListings(apps, sortMode_);
    }
    categoryHeading_->SetText((category_.empty() ? Tr("All apps") : CategoryName(category_)) + " · " + TrN("{n} app", "{n} apps", static_cast<long long>(apps.size())));
    std::string empty;
    if (!catalog_) empty = catalogNote_;
    else if (category_.empty() && priceFilter_ == "all") empty = Tr("The catalog lists no app.");
    else empty = Tr("No apps match these filters yet.");
    FillGrid(categoryGrid_, apps, "usC", empty);
}

void StoreWindow::RebuildInstalled() {
    if (!installedBody_) return;
    installedBody_->ClearChildren();
    const std::vector<InstalledRecord> apps = CurrentApps(installed_);
    auto head = Theme::Column("usI.head", 4.0f);
    head->AddChild(Theme::Title("usI.title", Tr("Installed")));
    head->AddChild(Theme::Caption("usI.subtitle", TrN("{n} app on this machine, with the runtimes and libraries it shares.", "{n} apps on this machine, with the runtimes and libraries they share.", static_cast<long long>(apps.size()))));
    installedBody_->AddChild(head);
    auto bar = Theme::Row("usI.bar", Theme::kInnerGap, true);
    auto reload = Theme::Button("usReloadInstalled", Tr("Reload"), 88, Theme::ButtonKind::Secondary);
    reload->SetTooltip(Tr("Read the registry of installed apps again"));
    reload->onClick = [this]() { ReloadInstalled(); };
    bar->AddChild(reload);
    auto collect = Theme::Button("usCollect", Tr("Clean up"), 96, Theme::ButtonKind::Secondary);
    collect->SetTooltip(Tr("Remove the runtimes and libraries no installed app needs any more"));
    collect->onClick = [this]() { Collect(); };
    bar->AddChild(collect);
    installedBody_->AddChild(bar);

    if (!storeError_.empty()) {
        installedBody_->AddChild(Theme::Caption("usI.note", TrF("The store folder could not be opened: {0}", storeError_)));
    } else if (apps.empty()) {
        installedBody_->AddChild(Theme::Caption("usI.empty", Tr("No app is installed yet. Discover lists what the store offers.")));
    }
    auto grid = MakeGrid("usI.grid");
    int n = 0;
    for (const InstalledRecord& rec : apps) {
        const std::string id = "usI" + std::to_string(n++);
        const Component* app = catalog_ ? catalog_->FindApp(rec.id) : nullptr;
        if (app) {
            grid->AddChild(MakeAppCard(ListingFor(*catalog_, *app, platform_, installed_, channel_), id));
            continue;
        }
        // Installed, but the catalog does not know it (another channel, a
        // local catalog, or withdrawn and gone): still shown, still removable.
        auto card = Theme::Pin(CreateCard(id, rec.id, TrF("Installed {0} · not in the current catalog", rec.version)));
        card->SetStyle(CardStyleFor());
        card->SetElementSize(CSSLayout::Dimension::Px(Theme::kCardWidth), CSSLayout::Dimension::Auto());
        card->AddContent(Theme::Caption(id + ".folder", rec.folder));
        if (!rec.entry.empty()) {
            auto open = Theme::Button(id + ".open", Tr("Open"), 80, Theme::ButtonKind::Secondary);
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
        auto remove = Theme::Button(id + ".remove", Tr("Remove"), 88, Theme::ButtonKind::Danger);
        const std::string appId = rec.id;
        remove->onClick = [this, appId]() { RemoveApp(appId); };
        remove->SetDisabled(Busy());
        card->AddAction(remove);
        grid->AddChild(card);
    }
    if (!grid->GetChildren().empty()) installedBody_->AddChild(grid);
    const std::vector<InstalledRecord> components = InstalledComponents(installed_);
    if (!components.empty()) {
        auto section = Theme::Section("usI.components", Tr("Runtimes and libraries"),
                                      Tr("Shared by the apps above; Clean up removes the versions no installed app needs."));
        auto surface = Theme::Surface("usI.components.list", 6.0f);
        int c = 0;
        for (const InstalledRecord& rec : components) {
            const Component* comp = catalog_ ? catalog_->FindComponent(rec.id) : nullptr;
            const std::string name = comp && !comp->name.empty() ? comp->name : rec.id;
            auto row = Theme::Row("usI.comp" + std::to_string(c++), Theme::kGap);
            auto title = Theme::Text("usI.comp" + std::to_string(c) + ".name", name + " " + rec.version, 14.0f, FontWeight::Bold);
            title->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            row->AddChild(title);
            row->AddChild(Theme::Caption("usI.comp" + std::to_string(c) + ".kind", KindLabel(rec.kind) + " · " + rec.id));
            surface->AddChild(row);
        }
        section->AddChild(surface);
        installedBody_->AddChild(section);
    }
    installedBody_->ScrollToVertical(0);
    installedBody_->InvalidateLayout();
    installedBody_->RequestRedraw();
}

void StoreWindow::RebuildUpdates() {
    if (!updatesBody_) return;
    std::vector<AppListing> apps;
    if (catalog_) {
        ListingFilter f = BaseFilter();
        f.onlyUpdates = true;
        apps = Listings(f);
    }
    if (updatesBadge_) {
        updatesBadge_->SetText(std::to_string(apps.size()));
        updatesBadge_->SetVisible(!apps.empty());
    }
    updatesBody_->ClearChildren();
    auto head = Theme::Column("usU.head", 4.0f);
    head->AddChild(Theme::Title("usU.title", Tr("Updates")));
    head->AddChild(Theme::Caption("usU.subtitle", apps.empty() ? Tr("Everything is up to date.") : TrN("{n} app has a newer release for this machine.", "{n} apps have a newer release for this machine.", static_cast<long long>(apps.size()))));
    updatesBody_->AddChild(head);
    updateAllButton_ = Theme::Button("usUpdateAll", Tr("Update all"), 112, Theme::ButtonKind::Primary);
    updateAllButton_->onClick = [this]() { UpdateAll(); };
    updateAllButton_->SetDisabled(apps.empty() || Busy());
    auto bar = Theme::Row("usU.bar", Theme::kInnerGap);
    bar->AddChild(updateAllButton_);
    updatesBody_->AddChild(bar);
    auto grid = MakeGrid("usU.grid");
    FillGrid(grid, apps, "usU", catalog_ ? std::string() : catalogNote_);
    updatesBody_->AddChild(grid);
    updatesBody_->ScrollToVertical(0);
    updatesBody_->InvalidateLayout();
    updatesBody_->RequestRedraw();
}

void StoreWindow::FillGrid(const std::shared_ptr<UltraCanvasContainer>& grid, const std::vector<AppListing>& apps,
                           const std::string& idPrefix, const std::string& emptyText) {
    grid->ClearChildren();
    if (apps.empty()) {
        if (!emptyText.empty()) {
            auto empty = Theme::Surface(idPrefix + ".empty", 0, Theme::kSurfaceSoft);
            empty->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            auto label = Theme::Text(idPrefix + ".empty.text", emptyText, 14.0f, FontWeight::Normal, Theme::kTextSecondary);
            label->SetAlignment(TextAlignment::Center, VerticalAlignment::Top);
            empty->AddChild(label);
            grid->AddChild(empty);
        }
    }
    int n = 0;
    for (const AppListing& l : apps) grid->AddChild(MakeAppCard(l, idPrefix + std::to_string(n++)));
    grid->InvalidateLayout();
    grid->RequestRedraw();
}

std::string StoreWindow::StateLine(const AppListing& l) const {
    std::string line;
    if (l.app->lifecycle == Lifecycle::Deprecated) {
        line = Tr("Deprecated") + (l.app->successor.empty() ? std::string() : TrF(", use {0}", l.app->successor)) + " · ";
    } else if (l.app->lifecycle == Lifecycle::Withdrawn) {
        line = Tr("Withdrawn, no new installs") + " · ";
    }
    if (l.Installed()) {
        line += TrF("Installed {0}", l.installedVersion);
        if (l.updateAvailable) {
            line += " · " + TrF("{0} available", l.resolution.release->version);
            if (l.resolution.package->size) line += " (" + FormatBytes(l.resolution.package->size) + ")";
        }
        return line;
    }
    if (l.Available()) {
        line += l.resolution.release->version;
        if (!l.resolution.release->date.empty()) line += " · " + PrettyDate(l.resolution.release->date);
        if (l.resolution.package->size) line += " · " + FormatBytes(l.resolution.package->size);
        if (l.resolution.kind == MatchKind::Emulated) line += " · " + Tr("runs under Rosetta");
        return line;
    }
    if (l.resolution.release) {
        return line + TrF("Not available for your system; available for {0}", PackageResolver::SupportedSystems(*l.resolution.release));
    }
    return line + TrF("No release on the {0} channel", channel_);
}

std::vector<std::string> StoreWindow::FactsOf(const AppListing& l) const {
    const Component& app = *l.app;
    std::vector<std::string> facts;
    if (app.pricing.model != PricingModel::Free && app.pricing.trialDays > 0 && !Bought(app.id))
        facts.push_back(TrN("{n}-day free trial", "{n}-day free trial", app.pricing.trialDays));
    if (app.openSource) facts.push_back(Tr("Open source") + (app.license.empty() ? std::string() : " · " + app.license));
    else if (!app.license.empty()) facts.push_back(app.license);
    if (l.resolution.release) {
        std::string systems;
        std::set<std::string> seen;
        for (const Package& p : l.resolution.release->packages) {
            const std::string os = OsLabel(p.target.os);
            if (!seen.insert(os).second) continue;
            systems += (systems.empty() ? "" : " · ") + os;
        }
        if (!systems.empty()) facts.push_back(systems);
    }
    if (!app.ageRating.empty()) facts.push_back(TrF("PEGI {0}", app.ageRating));
    facts.push_back(app.requiresAccount ? Tr("Needs an account") : Tr("No account needed"));
    if (app.updates.period == UpdatePeriod::Until) facts.push_back(TrF("Updates free until {0}", PrettyDate(app.updates.until)));
    else if (app.updates.period == UpdatePeriod::Never) facts.push_back(Tr("The bought version only"));
    return facts;
}

std::vector<std::shared_ptr<UltraCanvasButton>> StoreWindow::PrimaryActions(const AppListing& l, const std::string& id) {
    std::vector<std::shared_ptr<UltraCanvasButton>> out;
    const std::string appId = l.app->id;
    const bool busy = Busy() || HasJobFor(appId);
    if (l.Installed()) {
        if (!l.installedEntry.empty()) {
            auto open = Theme::Button(id + ".open", Tr("Open"), 80, Theme::ButtonKind::Secondary);
            open->onClick = [this, appId]() {
                if (!catalog_) return;
                if (const Component* app = catalog_->FindApp(appId)) LaunchApp(ListingFor(*catalog_, *app, platform_, installed_, channel_));
            };
            out.push_back(open);
        }
        if (l.updateAvailable && Bought(appId) && !PurchaseOf(appId).Covers(l.resolution.release->date)) {
            // The newer release is outside the purchase: a new one is needed.
            auto buy = Theme::Button(id + ".buy", Tr("Buy update"), 112, Theme::ButtonKind::Primary);
            buy->SetTooltip(TrF("{0} is not in your purchase ({1})", l.resolution.release->version, PurchaseOf(appId).UpdateTerms()));
            buy->onClick = [this, appId]() { BuyApp(appId); };
            buy->SetDisabled(busy || storeError_.size() > 0);
            out.push_back(buy);
        } else if (l.updateAvailable) {
            auto update = Theme::Button(id + ".update", Tr("Update"), 88, Theme::ButtonKind::Primary);
            update->onClick = [this, appId]() { UpdateApp(appId); };
            update->SetDisabled(busy);
            out.push_back(update);
        }
        auto remove = Theme::Button(id + ".remove", Tr("Remove"), 88, Theme::ButtonKind::Danger);
        remove->onClick = [this, appId]() { RemoveApp(appId); };
        remove->SetDisabled(busy);
        out.push_back(remove);
        return out;
    }
    if (!l.Available() || l.app->lifecycle == Lifecycle::Withdrawn) return out;
    const Pricing& pricing = l.app->pricing;
    const bool paid = pricing.model != PricingModel::Free;
    if ((!paid || pricing.trialDays > 0) && !Bought(appId)) {
        auto install = Theme::Button(id + ".install", paid ? Tr("Try free") : Tr("Install"), paid ? 96 : 88, paid ? Theme::ButtonKind::Primary : Theme::ButtonKind::Soft);
        if (paid) install->SetTooltip(TrN("Installs the full app for {n} day", "Installs the full app for {n} days", pricing.trialDays));
        install->onClick = [this, appId]() { InstallApp(appId); };
        install->SetDisabled(busy || storeError_.size() > 0);
        out.push_back(install);
    }
    if (paid && Bought(appId)) {
        // Bought here: the restricted package comes through the payment service's signed URL.
        auto install = Theme::Button(id + ".install", Tr("Install"), 88, Theme::ButtonKind::Primary);
        install->SetTooltip(TrF("Bought: receipt no. {0}", Bought(appId)->receiptNo));
        install->onClick = [this, appId]() { InstallApp(appId); };
        install->SetDisabled(busy || storeError_.size() > 0);
        out.push_back(install);
        return out;
    }
    if (paid) {
        // Through the catalog's payment service; without one, the publisher's page.
        const std::string price = PriceText(*l.app);
        auto buy = Theme::Button(id + ".buy", TrF("Buy · {0}", price), static_cast<float>(72 + price.size() * 8),
                                 pricing.trialDays == 0 ? Theme::ButtonKind::Primary : Theme::ButtonKind::Secondary);
        buy->SetTooltip(PriceLabel(pricing, CodeFor(*l.app)) + " - " + (catalog_ && !catalog_->paymentUrl.empty() ? Tr("through the store's payment service") : Tr("opens the publisher's page")) +
                        (CodeFor(*l.app) ? ", " + TrF("the code {0} goes with the purchase", CodeFor(*l.app)->code) : std::string()));
        buy->onClick = [this, appId]() { BuyApp(appId); };
        buy->SetDisabled(busy || storeError_.size() > 0);
        out.push_back(buy);
    }
    return out;
}

std::shared_ptr<UltraCanvasCard> StoreWindow::MakeAppCard(const AppListing& l, const std::string& id) {
    const Component& app = *l.app;
    std::string meta = PublisherName(app);
    if (const std::string category = CategoryOf(app); !category.empty()) meta += (meta.empty() ? "" : " · ") + CategoryName(category);
    auto card = Theme::Pin(CreateCard(id, app.name, meta));
    card->SetStyle(CardStyleFor());
    card->SetElementSize(CSSLayout::Dimension::Px(Theme::kCardWidth), CSSLayout::Dimension::Auto());

    const std::string iconUrl = ResolveCatalogUrl(catalogState_.fetchedFrom, app.icon);
    if (!iconUrl.empty()) {
        const auto known = picturePaths_.find(iconUrl);
        if (known != picturePaths_.end()) { if (!known->second.empty()) card->SetIcon(known->second); }
        else RequestPicture(iconUrl, card);
    }

    // A state worth a badge at the card's edge; the price sits along the
    // bottom, where the design puts it, so the title keeps its room.
    std::shared_ptr<UltraCanvasBadge> badge;
    if (l.updateAvailable && Bought(app.id) && !PurchaseOf(app.id).Covers(l.resolution.release->date)) badge = Badge(id + ".badge", Tr("New purchase"), BadgeVariant::Warning);
    else if (l.updateAvailable) badge = Badge(id + ".badge", Tr("Update"), BadgeVariant::Warning);
    else if (Bought(app.id) && !l.Installed()) { badge = Badge(id + ".badge", Tr("Bought"), BadgeVariant::Successful); badge->SetColor(Theme::kSuccess); }
    else if (!l.Available()) badge = Badge(id + ".badge", Tr("Not for this system"), BadgeVariant::Neutral);
    if (badge) card->SetTrailing(badge);

    const std::string summary = app.summary.Text(lang_);
    if (!summary.empty()) card->AddContent(Theme::Text(id + ".summary", summary, 14.0f, FontWeight::Normal, Theme::kTextBody));
    std::string state = StateLine(l);
    if (!l.Installed() && app.pricing.model != PricingModel::Free && app.pricing.trialDays > 0 && !Bought(app.id))
        state += " · " + TrN("{n}-day free trial", "{n}-day free trial", app.pricing.trialDays);
    card->AddContent(Theme::Caption(id + ".state", state));

    // Along the bottom: "Free", the price or "Installed" on the left, and
    // one action on the right; the rest of the actions are on the app page.
    const bool free = app.pricing.model == PricingModel::Free;
    auto price = Theme::Text(id + ".price", l.Installed() ? Tr("Installed") : PriceText(app), 15.0f, FontWeight::Bold,
                             l.Installed() || free ? Theme::kSuccess : Theme::kTextPrimary);
    price->SetWrap(TextWrap::WrapNone);
    price->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    card->AddAction(price);
    const auto actions = PrimaryActions(l, id);
    if (!actions.empty()) card->AddAction(actions.front());
    else {
        auto details = Theme::Button(id + ".details", Tr("Details"), 88, Theme::ButtonKind::Secondary);
        const std::string appId = app.id;
        details->onClick = [this, appId]() { ShowDetails(appId); };
        card->AddAction(details);
    }

    card->SetClickable(true);
    const std::string appId = app.id;
    card->onClick = [this, appId]() { ShowDetails(appId); };
    return card;
}

std::shared_ptr<UltraCanvasContainer> StoreWindow::MakeAppRow(const AppListing& l, const std::string& id, int number) {
    const Component& app = *l.app;
    std::string line;
    if (l.resolution.release) line = l.resolution.release->version;
    else if (!app.releases.empty()) line = app.releases.front().version;
    if (const std::string category = CategoryOf(app); !category.empty()) line += (line.empty() ? "" : " · ") + CategoryName(category);
    const std::string date = l.resolution.release ? l.resolution.release->date : (app.releases.empty() ? std::string() : app.releases.front().date);
    if (!date.empty()) line += (line.empty() ? "" : " · ") + PrettyDate(date);
    if (app.pricing.model != PricingModel::Free) line += (line.empty() ? "" : " · ") + PriceText(app);
    auto card = Theme::Pin(CreateCard(id, std::to_string(number) + "   " + app.name, line));
    CardStyle style = CardStyleFor(Theme::kRowRadius, 12.0f, 44.0f);
    style.titleFont.fontSize = 16.0;
    style.gap = 0.0f;
    card->SetStyle(style);
    const std::string iconUrl = ResolveCatalogUrl(catalogState_.fetchedFrom, app.icon);
    if (!iconUrl.empty()) {
        const auto known = picturePaths_.find(iconUrl);
        if (known != picturePaths_.end()) { if (!known->second.empty()) card->SetIcon(known->second); }
        else RequestPicture(iconUrl, card);
    }
    const auto actions = PrimaryActions(l, id);
    if (!actions.empty()) card->SetTrailing(actions.front());
    else {
        auto details = Theme::Button(id + ".details", Tr("Details"), 88, Theme::ButtonKind::Secondary);
        const std::string appId = app.id;
        details->onClick = [this, appId]() { ShowDetails(appId); };
        card->SetTrailing(details);
    }
    card->SetClickable(true);
    const std::string appId = app.id;
    card->onClick = [this, appId]() { ShowDetails(appId); };
    return card;
}

std::shared_ptr<UltraCanvasContainer> StoreWindow::MakeAppLink(const AppListing& l, const std::string& id) {
    const Component& app = *l.app;
    std::string line = app.summary.Text(lang_);
    line = Shorten(line, 38);
    line += (line.empty() ? "" : " · ") + PriceText(app);
    auto card = Theme::Pin(CreateCard(id, app.name, line));
    CardStyle style = CardStyleFor(Theme::kRowRadius, 8.0f, 40.0f);
    style.titleFont.fontSize = 14.0;
    style.borderWidth = 0.0f;
    style.gap = 0.0f;
    card->SetStyle(style);
    const std::string iconUrl = ResolveCatalogUrl(catalogState_.fetchedFrom, app.icon);
    if (!iconUrl.empty()) {
        const auto known = picturePaths_.find(iconUrl);
        if (known != picturePaths_.end()) { if (!known->second.empty()) card->SetIcon(known->second); }
        else RequestPicture(iconUrl, card);
    }
    card->SetClickable(true);
    const std::string appId = app.id;
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
        detailsBody_->AddChild(Theme::Caption("usX.gone", Tr("This app is no longer in the catalog.")));
        return;
    }
    const AppListing l = ListingFor(*catalog_, *app, platform_, installed_, channel_);
    const std::string base = catalogState_.fetchedFrom;
    const std::string category = CategoryOf(*app);

    // The breadcrumb: the way back.
    {
        auto crumbs = Theme::Row("usX.crumbs", 2.0f, true);
        auto home = Theme::Button("usX.crumb.home", pageBeforeDetails_ == kInstalled ? Tr("Installed") : pageBeforeDetails_ == kUpdates ? Tr("Updates") : Tr("Discover"), 92, Theme::ButtonKind::Ghost, 32);
        home->onClick = [this]() { HideDetails(); };
        crumbs->AddChild(home);
        if (!category.empty()) {
            crumbs->AddChild(Theme::Caption("usX.crumb.sep1", "  /  "));
            auto cat = Theme::Button("usX.crumb.cat", CategoryName(category), 40, Theme::ButtonKind::Ghost, 32);
            cat->onClick = [this, category]() {
                category_ = category;
                detailsAppId_.clear();
                RebuildCategories();
                ShowPage(kCategories);
            };
            crumbs->AddChild(cat);
        }
        crumbs->AddChild(Theme::Caption("usX.crumb.sep2", "  /  "));
        crumbs->AddChild(Theme::Text("usX.crumb.name", app->name, 13.0f, FontWeight::Bold, Theme::kTextPrimary));
        detailsBody_->AddChild(crumbs);
    }

    // The head: the icon, the name and the summary, the facts, the actions,
    // what this machine downloads; the release facts on the right.
    {
        auto head = Theme::Row("usX.head", 28.0f);
        head->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);
        head->AddChild(MakePicture("usX.icon", ResolveCatalogUrl(base, app->icon), 120, 120));

        auto middle = Theme::Column("usX.middle", Theme::kGap);
        middle->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        auto names = Theme::Column("usX.names", 4.0f);
        names->AddChild(Theme::Title("usX.name", app->name));
        const std::string summary = app->summary.Text(lang_);
        if (!summary.empty()) names->AddChild(Theme::Text("usX.summary", summary, 16.0f, FontWeight::Normal, Theme::kTextBody));
        if (!app->publisher.empty()) {
            const Publisher* publisher = PublisherOf(*app);
            const std::string name = PublisherName(*app);
            if (publisher && !publisher->url.empty()) {
                auto link = Theme::Button("usX.publisher", name, static_cast<float>(24 + name.size() * 8), Theme::ButtonKind::Ghost, 32);
                const std::string url = publisher->url;
                link->SetTooltip(url);
                link->onClick = [url]() { OpenURL(url); };
                auto row = Theme::Row("usX.publisher.row", 0);
                row->AddChild(link);
                names->AddChild(row);
            } else {
                names->AddChild(Theme::Text("usX.publisher", name, 14.0f, FontWeight::Bold, Theme::kAccentSoftText));
            }
        }
        middle->AddChild(names);

        auto facts = Theme::Row("usX.facts", Theme::kInnerGap, true);
        int n = 0;
        for (const std::string& fact : FactsOf(l)) facts->AddChild(FactPill("usX.fact" + std::to_string(n++), fact));
        if (const LicenceRecord* bought = Bought(app->id)) {
            facts->AddChild(FactPill("usX.bought", TrF("Bought: receipt no. {0}, {1}, {2}", bought->receiptNo, PrettyDate(bought->issuedAt.substr(0, 10)), PurchaseOf(app->id).UpdateTerms())));
        }
        if (const PromoCode* code = CodeFor(*app)) {
            const std::string what = DiscountLabel(*code);
            facts->AddChild(FactPill("usX.code", TrF("Code {0}", code->code) + (what.empty() ? "" : ": " + what) + (code->validUntil.empty() ? std::string() : ", " + TrF("until {0}", PrettyDate(code->validUntil)))));
        }
        if (app->lifecycle == Lifecycle::Deprecated) facts->AddChild(FactPill("usX.life", Tr("Deprecated") + (app->successor.empty() ? std::string() : TrF(", use {0}", app->successor))));
        if (app->lifecycle == Lifecycle::Withdrawn) facts->AddChild(FactPill("usX.life", Tr("Withdrawn, no new installs")));
        if (std::find(app->machineTranslated.begin(), app->machineTranslated.end(), lang_) != app->machineTranslated.end())
            facts->AddChild(FactPill("usX.mt", TrF("Translated automatically from {0}", LanguageName(app->sourceLanguage))));
        middle->AddChild(facts);

        auto actions = Theme::Row("usX.actions", Theme::kInnerGap, true);
        for (auto& button : PrimaryActions(l, "usX")) actions->AddChild(button);
        if (const LicenceRecord* bought = Bought(app->id); bought && catalog_ && !catalog_->paymentUrl.empty()) {
            auto receipt = Theme::Button("usX.receipt", Tr("Receipt"), 88, Theme::ButtonKind::Secondary);
            const std::string url = catalog_->paymentUrl + "/v1/orders/" + bought->orderId + "/receipt?secret=" + bought->orderSecret;
            receipt->onClick = [url]() { OpenURL(url); };
            actions->AddChild(receipt);
        }
        if (catalog_ && !catalog_->marketUrl.empty()) {
            auto report = Theme::Button("usX.report", Tr("Report a problem"), 130, Theme::ButtonKind::Ghost);
            report->onClick = [this]() { reportOpen_ = !reportOpen_; RebuildDetails(); };
            actions->AddChild(report);
        }
        middle->AddChild(actions);

        // What became of the problems reported from here, and the form.
        if (catalog_ && !catalog_->marketUrl.empty()) {
            int n = 0;
            for (const StoreSettings::SentReport& r : settings_.reports) {
                if (r.app != app->id) continue;
                std::string line = TrF("Your report of {0} ({1}): {2}", PrettyDate(r.sentAt), r.kind, r.state.empty() ? Tr("sent") : r.state);
                if (!r.outcome.empty()) line += " - " + r.outcome;
                middle->AddChild(Theme::Caption("usX.rep" + std::to_string(n++), line));
            }
            if (reportOpen_) {
                reportForm_ = Theme::Surface("usX.reportForm", Theme::kInnerGap);
                reportForm_->AddChild(Theme::Text("usX.reportForm.title", Tr("Report a problem with this app"), 13.0f, FontWeight::Bold, Theme::kTextPrimary));
                reportForm_->AddChild(Theme::Caption("usX.reportForm.hint", Tr("The store's operator reads it; the merchant sees the report, never who sent it. You learn the outcome here.")));
                reportKind_ = Theme::Pin(CreateDropdown("usX.reportKind", 0, 0, 220, Theme::kControlHeight));
                reportKind_->AddItem(Tr("It crashes"), "crash");
                reportKind_->AddItem(Tr("It does not work as described"), "not-working");
                reportKind_->AddItem(Tr("Malware or a security problem"), "malware");
                reportKind_->AddItem(Tr("Inappropriate content"), "content");
                reportKind_->AddItem(Tr("A copyright problem"), "copyright");
                reportKind_->AddItem(Tr("A privacy problem"), "privacy");
                reportKind_->AddItem(Tr("Something else"), "other");
                reportKind_->SetSelectedIndex(0);
                reportForm_->AddChild(reportKind_);
                reportText_ = Theme::Pin(std::make_shared<UltraCanvasTextArea>("usX.reportText", 0.0f, 96.0f));
                reportText_->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(96.0f));
                reportText_->SetWordWrap(true);
                reportText_->SetShowLineNumbers(false);
                reportText_->SetHighlightSyntax(false);
                reportText_->SetFontSize(12.0f);
                reportText_->SetBackgroundColor(Theme::kPageBackground);
                reportText_->SetBorders(1.0f, Theme::kBorder, 4.0f);
                reportForm_->AddChild(reportText_);
                auto buttons = Theme::Row("usX.reportButtons", Theme::kInnerGap, true);
                auto send = Theme::Button("usX.reportSend", Tr("Send"), 90, Theme::ButtonKind::Primary);
                send->onClick = [this, appId = app->id]() { SendReportNow(appId); };
                buttons->AddChild(send);
                auto cancel = Theme::Button("usX.reportCancel", Tr("Cancel"), 90, Theme::ButtonKind::Ghost);
                cancel->onClick = [this]() { reportOpen_ = false; RebuildDetails(); };
                buttons->AddChild(cancel);
                reportForm_->AddChild(buttons);
                middle->AddChild(reportForm_);
            }
        }

        // What this machine downloads: the plan, in one line.
        if (const std::optional<InstallPlan> plan = PlanFor(l)) {
            std::string line = TrF("Download for this machine: {0}.", FormatBytes(plan->bytesToDownload));
            std::string have, need;
            for (const InstallStep& step : plan->steps) {
                if (!step.component || step.component == app) continue;
                const std::string name = (step.component->name.empty() ? step.component->id : step.component->name) + (step.release ? " " + step.release->version : "");
                if (step.alreadyInstalled) have += (have.empty() ? "" : ", ") + name;
                else need += (need.empty() ? "" : ", ") + name + (step.package && step.package->size ? " (" + FormatBytes(step.package->size) + ")" : "");
            }
            if (!have.empty()) line += " " + TrF("Already installed: {0}.", have);
            if (!need.empty()) line += " " + TrF("To be added: {0}.", need);
            middle->AddChild(Theme::Caption("usX.download", line));
        } else if (!l.Available() && l.resolution.release) {
            middle->AddChild(Theme::Caption("usX.download", TrF("Not available for your system; available for {0}.", PackageResolver::SupportedSystems(*l.resolution.release))));
        }
        head->AddChild(middle);

        // The release facts.
        const Release* release = l.resolution.release ? l.resolution.release : app->Newest(channel_);
        auto facts2 = Theme::Surface("usX.release.facts", 10.0f, Theme::kSurfaceSoft);
        facts2->SetElementSize(CSSLayout::Dimension::Px(Theme::kInfoWidth), CSSLayout::Dimension::Auto());
        if (release) {
            facts2->AddChild(FactLine("usX.f.version", Tr("Version"), release->version));
            if (!release->date.empty()) facts2->AddChild(FactLine("usX.f.date", Tr("Released"), PrettyDate(release->date)));
            facts2->AddChild(FactLine("usX.f.channel", Tr("Channel"), release->channel == "beta" ? Tr("Beta") : Tr("Stable")));
        }
        if (!category.empty()) facts2->AddChild(FactLine("usX.f.category", Tr("Category"), CategoryName(category)));
        if (release) {
            std::string runs;
            std::set<std::string> seen;
            for (const Package& p : release->packages) {
                std::string os = OsLabel(p.target.os);
                if (const std::string minOs = MinOsOf(p.target); !minOs.empty()) os += " " + minOs + "+";
                if (!seen.insert(os).second) continue;
                runs += (runs.empty() ? "" : " · ") + os;
            }
            if (!runs.empty()) facts2->AddChild(FactLine("usX.f.runs", Tr("Runs on"), runs));
        }
        facts2->AddChild(FactLine("usX.f.id", Tr("App id"), app->id));
        head->AddChild(facts2);
        detailsBody_->AddChild(head);
    }

    // Screenshots, side by side, scrolled horizontally.
    if (!app->screenshots.empty()) {
        auto section = Theme::Section("usX.shots", Tr("Screenshots"));
        auto strip = Theme::Pin(CreateScrollableContainer("usX.shots.strip", 0, 0, 0, 0, false, true));
        strip->layout.SetFlexRow().SetFlexGap(Theme::kGap).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        strip->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(292));
        int n = 0;
        for (const std::string& shot : app->screenshots) {
            const std::string url = ResolveCatalogUrl(base, shot);
            if (url.empty()) continue;
            auto image = MakePicture("usX.shot" + std::to_string(n++), url, 440, 275);
            image->SetBackgroundColor(Theme::kSurfaceSoft);
            image->SetBorders(1.0f, Theme::kBorder, Theme::kRowRadius);
            strip->AddChild(image);
        }
        section->AddChild(strip);
        detailsBody_->AddChild(section);
    }

    // The two columns: about, what is new, what it installs; the
    // information and more from the publisher.
    {
        auto split = Theme::Row("usX.split", 32.0f);
        split->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);
        auto main = Theme::Column("usX.main", Theme::kSectionGap);
        main->layoutItem.SetFlexGrow(1).SetFlexShrink(1);

        const std::string description = app->description.Text(lang_);
        if (!description.empty()) {
            auto section = Theme::Section("usX.about", Tr("About this app"));
            section->AddChild(Theme::Text("usX.about.text", description, 15.0f, FontWeight::Normal, Theme::kTextBody));
            main->AddChild(section);
        }

        const Release* release = l.resolution.release ? l.resolution.release : app->Newest(channel_);
        if (release) {
            auto section = Theme::Section("usX.new", TrF("What's new in {0}", release->version));
            const std::string notes = release->notes.Text(lang_);
            section->AddChild(Theme::Text("usX.new.notes", notes.empty() ? Tr("The publisher gave no release notes.") : notes, 15.0f, FontWeight::Normal,
                                          notes.empty() ? Theme::kTextSecondary : Theme::kTextBody));
            // The version history: the releases before this one.
            std::vector<const Release*> earlier;
            for (const Release& r : app->releases) {
                if (&r == release || CompareVersions(r.version, release->version) >= 0) continue;
                if (r.channel != "stable" && channel_ != "beta") continue;
                earlier.push_back(&r);
            }
            if (!earlier.empty()) {
                auto history = Theme::Surface("usX.history", 6.0f, Theme::kSurfaceSoft);
                history->AddChild(Theme::Eyebrow("usX.history.title", Tr("Version history")));
                int n = 0;
                for (const Release* r : earlier) {
                    if (static_cast<size_t>(n) >= kVersionHistoryCount) break;
                    std::string line = r->version;
                    if (!r->date.empty()) line += " · " + PrettyDate(r->date);
                    if (r->channel == "beta") line += " · " + Tr("beta");
                    const std::string rnotes = r->notes.Text(lang_);
                    if (!rnotes.empty()) line += " - " + Shorten(rnotes, 118);
                    history->AddChild(Theme::Text("usX.history." + std::to_string(n++), line, 13.0f, FontWeight::Normal, Theme::kTextBody));
                }
                if (earlier.size() > kVersionHistoryCount) history->AddChild(Theme::Caption("usX.history.more", TrN("and {n} earlier release", "and {n} earlier releases", static_cast<long long>(earlier.size() - kVersionHistoryCount))));
                section->AddChild(history);
            }
            main->AddChild(section);
        }

        if (const std::optional<InstallPlan> plan = PlanFor(l)) {
            auto section = Theme::Section("usX.installs", Tr("What it installs"), Tr("Apps share layers. Only the layers missing on this machine are downloaded."));
            auto list = Theme::Surface("usX.installs.list", 10.0f);
            int n = 0;
            for (const InstallStep& step : plan->steps) {
                if (!step.component) continue;
                auto row = Theme::Row("usX.step" + std::to_string(n++), Theme::kGap);
                auto texts = Theme::Column("usX.step" + std::to_string(n) + ".texts", 1.0f);
                texts->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
                texts->AddChild(Theme::Text("usX.step" + std::to_string(n) + ".name",
                    (step.component->name.empty() ? step.component->id : step.component->name) + (step.release ? " " + step.release->version : ""), 14.0f, FontWeight::Bold));
                std::string what = KindLabel(step.component->kind);
                if (step.alreadyInstalled) what += " · " + Tr("already installed");
                else {
                    what += " · " + Tr("will be downloaded");
                    if (step.package) what += " · " + step.package->target.Describe() + " · " + step.package->format + " · " + Tr("SHA-256 verified");
                }
                texts->AddChild(Theme::Caption("usX.step" + std::to_string(n) + ".what", what));
                row->AddChild(texts);
                if (step.package && step.package->size) {
                    auto size = Theme::Text("usX.step" + std::to_string(n) + ".size", FormatBytes(step.package->size), 14.0f, FontWeight::Bold,
                                            step.alreadyInstalled ? Theme::kTextMuted : Theme::kTextPrimary);
                    size->SetWrap(TextWrap::WrapNone);
                    row->AddChild(size);
                }
                list->AddChild(row);
            }
            section->AddChild(list);
            main->AddChild(section);
        }
        if (!app->tags.empty()) {
            std::string line;
            for (const std::string& t : app->tags) line += (line.empty() ? "" : ", ") + t;
            main->AddChild(Theme::Caption("usX.tags", TrF("Tags: {0}", line)));
        }
        split->AddChild(main);

        auto aside = Theme::Column("usX.aside", Theme::kSectionGap);
        aside->SetElementSize(CSSLayout::Dimension::Px(Theme::kInfoWidth), CSSLayout::Dimension::Auto());
        {
            auto info = Theme::Surface("usX.info", 10.0f);
            info->AddChild(Theme::Heading("usX.info.title", Tr("Information")));
            if (!app->publisher.empty()) info->AddChild(FactLine("usX.i.publisher", Tr("Publisher"), PublisherName(*app)));
            if (!app->license.empty()) info->AddChild(FactLine("usX.i.licence", Tr("Licence"), app->license));
            info->AddChild(FactLine("usX.i.price", Tr("Price"), app->pricing.model == PricingModel::Free ? Tr("Free") : TrF("{0}, one-time", PriceText(*app))));
            if (app->pricing.model != PricingModel::Free && app->pricing.trialDays > 0)
                info->AddChild(FactLine("usX.i.trial", Tr("Trial"), TrN("{n} day, full app", "{n} days, full app", app->pricing.trialDays)));
            if (app->pricing.model != PricingModel::Free) {
                const std::string updates = app->updates.period == UpdatePeriod::Until ? TrF("Free until {0}", PrettyDate(app->updates.until))
                                          : app->updates.period == UpdatePeriod::Never ? Tr("The bought version only") : Tr("Free, always");
                info->AddChild(FactLine("usX.i.updates", Tr("Updates"), updates));
            }
            if (const std::string languages = LanguagesOf(*app); !languages.empty()) info->AddChild(FactLine("usX.i.languages", Tr("Languages"), languages));
            if (!app->ageRating.empty()) info->AddChild(FactLine("usX.i.age", Tr("Age rating"), TrF("PEGI {0}", app->ageRating)));
            auto links = Theme::Column("usX.links", 2.0f);
            auto link = [&](const char* id, const std::string& text, const std::string& url) {
                if (url.empty()) return;
                auto b = Theme::Button(id, text, 0, Theme::ButtonKind::Ghost, 32);
                b->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(32));
                b->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
                b->SetIcon(Theme::Icon("link.svg"));
                b->SetTextAlign(TextAlignment::Left);
                b->SetTooltip(url);
                b->onClick = [url]() { OpenURL(url); };
                links->AddChild(b);
            };
            link("usX.home", Tr("Homepage"), app->homepage);
            link("usX.support", Tr("Support"), app->supportUrl);
            if (app->openSource) link("usX.source", Tr("Source code"), app->sourceUrl);
            link("usX.privacy", Tr("Privacy policy"), app->privacyUrl);
            if (!links->GetChildren().empty()) info->AddChild(links);
            aside->AddChild(info);
        }
        // More from the publisher.
        if (!app->publisher.empty()) {
            std::vector<AppListing> others;
            for (const AppListing& o : Listings(BaseFilter())) {
                if (o.app->id == app->id || o.app->publisher != app->publisher) continue;
                others.push_back(o);
                if (others.size() >= kMoreFromPublisherCount) break;
            }
            if (!others.empty()) {
                auto more = Theme::Surface("usX.more", 4.0f);
                more->AddChild(Theme::Heading("usX.more.title", TrF("More from {0}", PublisherName(*app))));
                int n = 0;
                for (const AppListing& o : others) more->AddChild(MakeAppLink(o, "usX.more." + std::to_string(n++)));
                aside->AddChild(more);
            }
        }
        split->AddChild(aside);
        detailsBody_->AddChild(split);
    }
    detailsBody_->ScrollToVertical(0);
    detailsBody_->InvalidateLayout();
    detailsBody_->RequestRedraw();
}

// ===========================================================================
// Actions
// ===========================================================================
void StoreWindow::InstallApp(const std::string& appId) {
    if (!storeError_.empty()) { Alert(TrF("The store folder could not be opened: {0}", storeError_)); return; }
    const Component* app = catalog_ ? catalog_->FindApp(appId) : nullptr;
    if (!app) return;
    Job job;
    job.kind = JobKind::Install;
    job.appId = appId;
    job.title = TrF("Install {0}", app->name);
    Enqueue(std::move(job));
}

void StoreWindow::UpdateApp(const std::string& appId) {
    const Component* app = catalog_ ? catalog_->FindApp(appId) : nullptr;
    if (!app) return;
    Job job;
    job.kind = JobKind::Update;
    job.appId = appId;
    job.title = TrF("Update {0}", app->name);
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
        TrF("Remove {0} from this machine? Its settings and documents stay where they are; runtimes and libraries no other app needs are removed with it.", name),
        TrF("Remove {0}", name),
        [this, appId, name](bool confirmed) {
            if (!confirmed) return;
            Job job;
            job.kind = JobKind::Remove;
            job.appId = appId;
            job.title = TrF("Remove {0}", name);
            Enqueue(std::move(job));
        },
        window_.get());
}

void StoreWindow::LaunchApp(const AppListing& l) {
    InstalledRecord rec;
    rec.folder = l.installedFolder;
    rec.entry = l.installedEntry;
    const std::string path = LaunchPath(rec);
    if (path.empty()) { SetStatus(Tr("This app has nothing to start."), true); return; }
    std::vector<std::string> argv;
#if defined(__APPLE__)
    if (EndsWith(path, ".app")) argv = {"open", path};
    else argv = {path};
#else
    argv = {path};
#endif
    std::string error;
    if (!LaunchDetachedProcess(argv, l.installedFolder, error)) {
        Alert(TrF("Could not start {0}: {1}", path, error));
        return;
    }
    SetStatus(TrF("Started {0}", path));
}

void StoreWindow::Collect() {
    if (!storeError_.empty()) { Alert(TrF("The store folder could not be opened: {0}", storeError_)); return; }
    Job job;
    job.kind = JobKind::Collect;
    job.title = Tr("Clean up unused components");
    Enqueue(std::move(job));
}

void StoreWindow::RefreshCatalog() {
    if (localCatalog_) { SetStatus(Tr("A local catalog is shown (--catalog); there is nothing to refresh.")); return; }
    if (const std::string note = ServerNote(); !note.empty()) { SetStatus(note, true); return; }
    for (const Job& j : pending_) if (j.kind == JobKind::Refresh) return;
    if (running_ && running_->kind == JobKind::Refresh) return;
    Job job;
    job.kind = JobKind::Refresh;
    job.title = Tr("Refresh the catalog");
    job.showInQueue = false;
    SetStatus(Tr("Fetching the catalog…"));
    Enqueue(std::move(job));
}

void StoreWindow::BuyApp(const std::string& appId) {
    if (!catalog_) return;
    const Component* app = catalog_->FindApp(appId);
    if (!app) return;
    if (catalog_->paymentUrl.empty()) {
        if (app->homepage.empty()) SetStatus(Tr("The store has no payment service yet, and the publisher gave no page to buy from."), true);
        else OpenURL(app->homepage);
        return;
    }
    if (storeError_.size() > 0) { SetStatus(TrF("The store folder could not be opened: {0}", storeError_), true); return; }
    Job job;
    job.kind = JobKind::Buy;
    job.appId = appId;
    job.title = TrF("Buy {0}", app->name);
    Enqueue(std::move(job));
}

void StoreWindow::ReloadInstalled() {
    if (!storeError_.empty()) return;
    Job job;
    job.kind = JobKind::Reload;
    job.title = Tr("Read the installed apps");
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

void StoreWindow::SendReportNow(const std::string& appId) {
    if (!catalog_ || catalog_->marketUrl.empty() || !reportText_ || !reportKind_) return;
    const std::string text = reportText_->GetText();
    if (text.empty()) { SetStatus(Tr("Say what is wrong before sending the report"), true); return; }
    const int at = reportKind_->GetSelectedIndex();
    const std::string kind = at >= 0 ? reportKind_->GetItem(at)->value : std::string("other");
    const std::string reporter = Bought(appId) ? Bought(appId)->licenceId : std::string();
    const std::string marketUrl = catalog_->marketUrl;
    SetStatus(Tr("Sending the report"));
    std::weak_ptr<int> token = alive_;
    std::thread([this, token, marketUrl, appId, kind, text, reporter]() {
        const auto sent = SendReport(marketUrl, "app", appId, kind, text, reporter, UltraNetHttpCall());
        app_.PostToUIThread([this, token, sent, appId, kind]() {
            if (!token.lock()) return;
            if (!sent) { SetStatus(sent.error.message, true); return; }
            settings_.RememberReport({sent.Value().id, sent.Value().secret, appId, kind, Today(), "open", ""});
            settings_.Save();
            reportOpen_ = false;
            SetStatus(Tr("Report sent. The outcome shows on this page once the operator has answered."));
            if (detailsAppId_ == appId) RebuildDetails();
        });
    }).detach();
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
