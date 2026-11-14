// Apps/UltraStore/web/WebStore.cpp
// The web store's pages: Discover, Categories, an app's page, and the
// catalog behind them (WebStore.h).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "WebStore.h"

#include "StoreTheme.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasCard.h"
#include "UltraCanvasChip.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>

using namespace UltraCanvas;
namespace Theme = UltraStore::Ui::Theme;

namespace UltraStore::Web {

namespace {

constexpr float kColumnWidth = 1280.0f;   // the design's centred column
constexpr float kSidePadding = 32.0f;
constexpr float kCardMinWidth = 280.0f;   // a card in the grids; they wrap
constexpr float kShotWidth = 560.0f;      // the design's screenshot column
constexpr float kShotHeight = 350.0f;

const Color kWarningInk(146, 64, 14, 255), kWarningFill(254, 243, 199, 255);   // #92400E on #FEF3C7
const Color kSuccessFill(220, 252, 231, 255);                                   // #DCFCE7
const Color kBetaInk(91, 33, 182, 255), kBetaFill(237, 233, 254, 255);         // #5B21B6 on #EDE9FE

// The names of the fixed category list, the same entries the native store
// translates (StoreQuery.cpp), so one catalogue serves both.
std::string CategoryName(const std::string& id) {
    const std::string key = CategoryId(id);
    if (key == "audio") return TrC("app category", "Music and audio");
    if (key == "video") return TrC("app category", "Video");
    if (key == "graphics") return TrC("app category", "Graphics and photos");
    if (key == "office") return TrC("app category", "Office");
    if (key == "communication") return TrC("app category", "Communication");
    if (key == "internet") return TrC("app category", "Internet");
    if (key == "development") return TrC("app category", "Developer tools");
    if (key == "education") return TrC("app category", "Education");
    if (key == "science") return TrC("app category", "Science");
    if (key == "finance") return TrC("app category", "Finance");
    if (key == "security") return TrC("app category", "Security");
    if (key == "games") return TrC("app category", "Games");
    if (key == "system") return TrC("app category", "System");
    if (key == "utilities") return TrC("app category", "Utilities");
    return id;
}

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

// "2026-10-22" -> "22 Oct 2026", the way the native store writes it.
std::string PrettyDate(const std::string& iso) {
    if (iso.size() < 10 || iso[4] != '-' || iso[7] != '-') return iso;
    const int month = std::atoi(iso.substr(5, 2).c_str());   // locale-ok: digits of an ISO date
    if (month < 1 || month > 12) return iso;
    std::string day = iso.substr(8, 2);
    if (day[0] == '0') day.erase(0, 1);
    return LocalisationDetail::Substitute(TrC("day, month and year", "{0} {1} {2}"), {day, ShortMonth(month), iso.substr(0, 4)});
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

std::string FormatBytes(uint64_t bytes) {
    char buffer[32];
    if (bytes >= (uint64_t(1) << 30)) { std::snprintf(buffer, sizeof(buffer), "%.1f", bytes / double(uint64_t(1) << 30)); return TrF("{0} GB", std::string(buffer)); }   // locale-ok: a size shown in the classic form
    if (bytes >= (uint64_t(1) << 20)) { std::snprintf(buffer, sizeof(buffer), "%.1f", bytes / double(uint64_t(1) << 20)); return TrF("{0} MB", std::string(buffer)); }   // locale-ok: as above
    if (bytes >= 1024) return TrF("{0} KB", static_cast<unsigned long long>(bytes / 1024));
    return TrF("{0} bytes", static_cast<unsigned long long>(bytes));
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A raw 64-byte signature, or its 128 hex digits with whitespace around.
bool ParseSignature(const std::string& fileBytes, std::vector<uint8_t>& out) {
    if (fileBytes.size() == 64) { out.assign(fileBytes.begin(), fileBytes.end()); return true; }
    std::string hex;
    for (char c : fileBytes) if (!std::isspace(static_cast<unsigned char>(c))) hex += c;
    return FromHex(hex, out) && out.size() == 64;
}

// A clickable surface: the card element is the one container that takes a
// click no child took (the design's cards, rows and tiles open the app).
CardStyle SurfaceStyle(float radius, float padding, float gap, const Color& fill, const Color& hover, const Color& border) {
    CardStyle style = CardStyle::Default();
    style.backgroundColor = fill;
    style.hoverBackgroundColor = hover;
    style.selectedBackgroundColor = Theme::kAccentFaint;
    style.borderColor = border;
    style.highlightBorderColor = Theme::kAccent;
    style.borderWidth = 1.0f;
    style.cornerRadius = radius;
    style.padding = padding;
    style.gap = gap;
    return style;
}

std::shared_ptr<UltraCanvasCard> ClickCard(const std::string& id, float radius, float padding, float gap,
                                           const Color& fill = Theme::kSurface, const Color& hover = Theme::kSurfaceSoft,
                                           const Color& border = Theme::kBorder) {
    auto card = Theme::Pin(CreateCard(id));
    card->SetStyle(SurfaceStyle(radius, padding, gap, fill, hover, border));
    return card;
}

bool Shown(const Component& app) {
    return app.visibility == Visibility::Listed && app.lifecycle != Lifecycle::Withdrawn;
}

const Release* NewestOf(const Component& app) {
    if (const Release* r = app.Newest("stable")) return r;
    return app.releases.empty() ? nullptr : &app.releases.front();
}

} // namespace

// ===========================================================================
// Start, and the catalog
// ===========================================================================
WebStore::WebStore(UltraCanvasApplication& app, std::shared_ptr<UltraCanvasWindow> window, std::vector<std::vector<uint8_t>> builtInKeys)
    : app_(app), window_(std::move(window)), builtInKeys_(std::move(builtInKeys)) {
    const std::vector<std::string> languages = Platform::Languages();
    if (!languages.empty()) lang_ = languages.front();
    downloadUrl_ = Platform::Option("store-download");
    contactUrl_ = Platform::Option("store-contact");
}

void WebStore::Start() {
    ReadTrustState();
    BuildShell();
    page_ = Platform::Page();
    Platform::OnNavigate([this]() { ShowPage(Platform::Page()); });
    BuildMessage(Tr("Loading the catalog"), Tr("The signed catalog is being fetched and checked."), false);
    LoadCatalog();
}

void WebStore::LoadCatalog() {
    std::weak_ptr<int> token = alive_;
    Platform::FetchText("catalog.json", [this, token](bool ok, const std::string& bytes) {
        if (token.expired()) return;
        if (!ok) { Refuse(Tr("The catalog is not available"), Tr("catalog.json could not be fetched from this site.")); return; }
        Platform::FetchText("catalog.json.sig", [this, token, bytes](bool okSig, const std::string& signature) {
            if (token.expired()) return;
            if (!okSig) { Refuse(Tr("The catalog is not signed"), Tr("catalog.json.sig could not be fetched from this site, so the catalog cannot be trusted.")); return; }
            CatalogArrived(bytes, signature);
        });
    });
}

void WebStore::CatalogArrived(const std::string& bytes, const std::string& signatureText) {
    std::vector<uint8_t> signature;
    if (!ParseSignature(signatureText, signature)) { Refuse(Tr("The catalog is not signed"), Tr("The signature file is not a 64-byte Ed25519 signature.")); return; }
    const std::vector<std::vector<uint8_t>> keys = KeysTrustedNow();
    if (keys.empty()) {
        Refuse(Tr("This store has no signing key built in"),
               Tr("The web store trusts a catalog only when a key it knows signed it, and this build knows none yet: the first catalog is published once the key exists."));
        return;
    }
    std::weak_ptr<int> token = alive_;
    Platform::Verify(bytes, signature, keys, [this, token, bytes](bool ok, const std::string& error) {
        if (token.expired()) return;
        if (!ok) {
            Platform::Log("catalog refused: " + error);
            Refuse(Tr("This catalog could not be verified"), TrF("No key this store trusts signed the catalog ({0}). Nothing from it is shown.", error));
            return;
        }
        Result<Catalog> parsed = ParseCatalog(bytes);
        if (!parsed) { Refuse(Tr("The catalog does not read"), parsed.error.message); return; }
        if (parsed.Value().sequence < trust_.sequence) {
            Platform::Log("catalog refused: sequence rollback");
            Refuse(Tr("This catalog is older than the one seen before"),
                   TrF("The site serves catalog {0}, but this browser has already accepted catalog {1}. A mirror cannot roll the store back.",
                       std::to_string(parsed.Value().sequence), std::to_string(trust_.sequence)));
            return;
        }
        Accept(std::move(parsed.Value()), bytes);
    });
}

void WebStore::Accept(Catalog catalog, const std::string&) {
    catalog_ = std::move(catalog);
    loaded_ = true;
    trust_.sequence = catalog_.sequence;
    trust_.announcedKeys = catalog_.trustedKeys;
    WriteTrustState();
    Platform::Log("catalog accepted: sequence " + std::to_string(catalog_.sequence) + ", " + std::to_string(catalog_.apps.size()) + " apps");
    ShowPage(page_);
}

void WebStore::Refuse(const std::string& title, const std::string& text) {
    loaded_ = false;
    refusedTitle_ = title;
    refusedText_ = text;
    BuildMessage(title, text, true);
}

std::vector<std::vector<uint8_t>> WebStore::KeysTrustedNow() const {
    std::vector<std::vector<uint8_t>> keys = builtInKeys_;
    for (const std::string& hex : trust_.announcedKeys) {
        std::vector<uint8_t> bytes;
        if (!FromHex(hex, bytes) || bytes.size() != 32) continue;
        if (std::find(keys.begin(), keys.end(), bytes) == keys.end()) keys.push_back(std::move(bytes));
    }
    return keys;
}

void WebStore::ReadTrustState() {
    const std::string text = Platform::ReadState();
    if (text.empty()) return;
    JSONParseResult parse;
    const JSONValue v = JSON::Parse(text, &parse);
    if (!parse.success || !v.IsObject()) return;
    trust_.sequence = static_cast<uint64_t>(std::max<int64_t>(0, v.Get("sequence").GetInteger(0)));
    for (const JSONValue& k : v.Get("announcedKeys").GetElements()) {
        std::vector<uint8_t> bytes;
        if (FromHex(k.GetString(), bytes) && bytes.size() == 32) trust_.announcedKeys.push_back(ToHex(bytes));
    }
}

void WebStore::WriteTrustState() {
    JSONValue v = JSONValue::MakeObject();
    v.Set("sequence", static_cast<int64_t>(trust_.sequence));
    JSONValue announced = JSONValue::MakeArray();
    for (const std::string& k : trust_.announcedKeys) announced.Append(k);
    v.Set("announcedKeys", std::move(announced));
    Platform::WriteState(JSON::Serialize(v));
}

// ===========================================================================
// The shell: the scrolling page, the header, the footer
// ===========================================================================
void WebStore::BuildShell() {
    // The page scrolls inside the window like a browser's viewport; Block
    // layout stacks the bands and measures wrapped text at its real width.
    root_ = CreateScrollableContainer("web.page", 0, 0, 0, 0, true, false);
    root_->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Pct(100));
    {
        ContainerStyle style = root_->GetContainerStyle();
        style.autoShowHorizontalScrollbar = false;
        root_->SetContainerStyle(style);
    }
    root_->SetBackgroundColor(Theme::kPageBackground);
    root_->layout.SetDisplay(CSSLayout::DisplayType::Block);
    window_->AddChild(root_);

    BuildHeader();
    body_ = CreateContainer("web.body");
    body_->layout.SetDisplay(CSSLayout::DisplayType::Block);
    body_->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    root_->AddChild(body_);
}

WebStore::Band WebStore::AddBand(const std::string& id, const Color& background, float paddingTop, float paddingBottom) {
    Band band;
    band.outer = CreateContainer(id);
    band.outer->SetBackgroundColor(background);
    band.outer->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    band.outer->SetPadding(paddingTop, kSidePadding, paddingBottom, kSidePadding);
    band.outer->layout.SetFlexRow().SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
    band.inner = CreateContainer(id + ".inner");
    band.inner->layout.SetDisplay(CSSLayout::DisplayType::Block);
    band.inner->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    CSSLayout::BoxConstraints limits;
    limits.maxWidth = CSSLayout::Dimension::Px(kColumnWidth);
    band.inner->boxConstraints = limits;
    band.inner->layoutItem.SetFlexShrink(1);
    band.outer->AddChild(band.inner);
    body_->AddChild(band.outer);
    return band;
}

void WebStore::BuildHeader() {
    auto outer = CreateContainer("web.header");
    outer->SetBackgroundColor(Theme::kSurface);
    outer->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    outer->SetPadding(12.0f, kSidePadding);
    outer->layout.SetFlexRow().SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
    header_ = Theme::Row("web.header.row", Theme::kGap, true);
    header_->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    CSSLayout::BoxConstraints limits;
    limits.maxWidth = CSSLayout::Dimension::Px(kColumnWidth);
    header_->boxConstraints = limits;
    header_->layoutItem.SetFlexShrink(1);
    outer->AddChild(header_);

    // The wordmark is the way home from anywhere.
    auto logo = Theme::Button("web.logo", "UltraStore", 150, Theme::ButtonKind::Ghost, 44);
    {
        ButtonStyle s = Theme::ButtonStyleFor(Theme::ButtonKind::Ghost, 44);
        s.fontFamily = Theme::kDisplayFont;
        s.fontSize = 21.0f;
        s.fontWeight = FontWeight::Bold;
        s.normalTextColor = s.hoverTextColor = s.pressedTextColor = Theme::kTextPrimary;
        logo->SetStyle(s);
    }
    logo->SetTooltip(Tr("UltraStore - the ULTRA OS app store"));
    logo->onClick = [this]() {
        if (search_input_) search_input_->SetText("");
        search_.clear();
        GoTo("");
    };
    // The mark before the name, from the site: the module carries no pictures.
    header_->AddChild(Picture("web.logo.mark", "ultrastore.png", {}, 36, 36, 8));
    header_->AddChild(logo);

    auto nav = Theme::Row("web.nav", 4.0f);
    auto navButton = [this, &nav](const std::string& page, const std::string& text, float width) {
        auto b = Theme::Button("web.nav." + (page.empty() ? std::string("discover") : page), text, width, Theme::ButtonKind::Ghost);
        b->onClick = [this, page]() {
            if (search_input_) search_input_->SetText("");
            search_.clear();
            GoTo(page);
        };
        navButtons_[page] = b;
        nav->AddChild(b);
    };
    navButton("", Tr("Discover"), 100);
    navButton("categories", Tr("Categories"), 112);
    header_->AddChild(nav);

    search_input_ = Theme::Pin(CreateTextInput("web.search", 0, 0, 240, Theme::kControlHeight + 4));
    search_input_->SetStyle(Theme::SearchStyle(search_input_->GetStyle()));
    search_input_->SetFontSize(15.0f);
    search_input_->SetPlaceholder(Tr("Search apps"));
    search_input_->SetAccessibleName(Tr("Search apps"));
    search_input_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(200));
    search_input_->onTextChanged = [this](const std::string& text) {
        search_ = text;
        if (!search_.empty() && !page_.empty()) GoTo("", false);
        else Rebuild();
    };
    header_->AddChild(search_input_);

    if (!downloadUrl_.empty()) {
        auto get = Theme::Button("web.get", Tr("Get UltraStore"), 150, Theme::ButtonKind::Primary);
        get->SetTooltip(Tr("Download the UltraStore app for your computer"));
        const std::string url = downloadUrl_;
        get->onClick = [url]() { Platform::OpenExternal(url); };
        header_->AddChild(get);
    }
    root_->AddChild(outer);
    root_->AddChild(Theme::Rule("web.header.rule"));
}

std::shared_ptr<UltraCanvasContainer> WebStore::BuildFooter() {
    Band band = AddBand("web.footer", Theme::kSurfaceSoft, 28.0f, 36.0f);
    auto row = Theme::Row("web.footer.row", Theme::kGap, true);
    row->layout.SetFlexJustifyContent(CSSLayout::JustifyContent::SpaceBetween);
    auto name = Theme::Row("web.footer.name", 6.0f);
    name->AddChild(Theme::Display("web.footer.mark", "UltraStore", 18.0f));
    name->AddChild(Theme::Caption("web.footer.claim", Tr("· the ULTRA OS app store")));
    row->AddChild(name);
    auto links = Theme::Row("web.footer.links", 4.0f, true);
    if (!downloadUrl_.empty()) links->AddChild(LinkButton("web.footer.get", Tr("Get UltraStore"), downloadUrl_));
    if (!contactUrl_.empty()) links->AddChild(LinkButton("web.footer.contact", Tr("Contact"), contactUrl_));
    if (loaded_) links->AddChild(Theme::Caption("web.footer.seq", TrF("Signed catalog {0}, {1}", std::to_string(catalog_.sequence), PrettyDate(catalog_.generated.substr(0, 10)))));
    row->AddChild(links);
    band.inner->AddChild(row);
    return band.outer;
}

// ===========================================================================
// Pages
// ===========================================================================
void WebStore::GoTo(const std::string& page, bool addHistory) {
    Platform::SetPage(page, addHistory);
    // Deferred: the click that brought us here came from a button of the
    // page being replaced, which must finish its callback first.
    std::weak_ptr<int> token = alive_;
    app_.PostToUIThread([this, token, page]() {
        if (token.expired()) return;
        ShowPage(page);
    });
}

void WebStore::ShowPage(const std::string& page) {
    page_ = page;
    Rebuild();
}

void WebStore::Rebuild() {
    body_->ClearChildren();
    for (auto& [key, button] : navButtons_) {
        const bool lit = key.empty() ? (page_.empty() || page_.rfind("app/", 0) == 0) : page_.rfind(key, 0) == 0;
        Theme::Style(button, lit ? Theme::ButtonKind::Soft : Theme::ButtonKind::Ghost);
    }
    if (!loaded_) {
        BuildMessage(refusedTitle_.empty() ? Tr("Loading the catalog") : refusedTitle_,
                     refusedText_.empty() ? Tr("The signed catalog is being fetched and checked.") : refusedText_, !refusedTitle_.empty());
        return;
    }
    if (page_.rfind("app/", 0) == 0) {
        const std::string id = page_.substr(4);
        if (const Component* app = catalog_.FindApp(id); app && app->visibility != Visibility::Hidden) BuildApp(*app);
        else BuildMessage(Tr("No such app"), TrF("The catalog has no app with the id {0}.", id), false);
    } else if (page_ == "categories") {
        BuildCategories("");
    } else if (page_.rfind("category/", 0) == 0) {
        BuildCategories(CategoryId(page_.substr(9)));
    } else if (!search_.empty()) {
        BuildResults();
    } else {
        BuildDiscover();
    }
    BuildFooter();
    root_->ScrollToVertical(0);
    body_->InvalidateLayout();
    root_->InvalidateLayout();
    root_->RequestRedraw();
    Platform::Log("page " + (page_.empty() ? std::string("discover") : page_));
}

void WebStore::BuildMessage(const std::string& title, const std::string& text, bool offerStore) {
    body_->ClearChildren();
    Band band = AddBand("web.message", Theme::kPageBackground, 64.0f, 64.0f);
    auto card = Theme::Surface("web.message.card", Theme::kGap, Theme::kSurfaceSoft);
    card->AddChild(Theme::Display("web.message.title", title, 28.0f));
    card->AddChild(Theme::Text("web.message.text", text, 15.0f, FontWeight::Normal, Theme::kTextBody));
    if (offerStore && !downloadUrl_.empty()) {
        auto row = Theme::Row("web.message.actions", Theme::kGap, true);
        auto get = Theme::Button("web.message.get", Tr("Get UltraStore"), 160, Theme::ButtonKind::Primary, 44);
        const std::string url = downloadUrl_;
        get->onClick = [url]() { Platform::OpenExternal(url); };
        row->AddChild(get);
        row->AddChild(Theme::Caption("web.message.hint", Tr("The UltraStore app verifies the catalog itself and installs what you pick.")));
        card->AddChild(row);
    }
    band.inner->AddChild(card);
    if (!title.empty()) Platform::Log("message: " + title);
    body_->InvalidateLayout();
    root_->InvalidateLayout();
    root_->RequestRedraw();
}

void WebStore::BuildDiscover() {
    const std::vector<const Component*> featured = Featured();
    const std::vector<const Component*> listed = Listed();
    if (listed.empty()) {
        BuildMessage(Tr("The store is empty"), Tr("The catalog lists no app yet."), false);
        return;
    }
    // The hero: the admin's first pick, else the newest app.
    const Component* heroApp = !featured.empty() ? featured.front() : Newest(1).front();
    {
        Band band = AddBand("web.hero", Theme::kPageBackground, 40.0f, 24.0f);
        band.inner->AddChild(Hero(*heroApp));
    }
    // The categories as pills.
    {
        Band band = AddBand("web.cats", Theme::kPageBackground, 16.0f, 24.0f);
        auto row = Theme::Row("web.cats.row", 10.0f, true);
        for (const auto& [id, count] : CategoriesWithCounts()) {
            auto chip = Theme::Pin(CreateChip("web.cats." + id, 0, 0, CategoryName(id)));
            chip->SetStyle(Theme::ChipStyleFor());
            chip->SetTooltip(TrN("{n} app", "{n} apps", static_cast<int>(count)));
            const std::string page = "category/" + id;
            chip->onClick = [this, page]() { GoTo(page); };
            row->AddChild(chip);
        }
        band.inner->AddChild(row);
    }
    // Editor's picks: the featured apps.
    if (featured.size() > 1 || (featured.size() == 1 && listed.size() > 1)) {
        Band band = AddBand("web.picks", Theme::kPageBackground, 24.0f, 24.0f);
        band.inner->AddChild(SectionHead("web.picks.head", Tr("Editor's picks"), Tr("Apps the ULTRA OS team uses every day."),
                                         Tr("See all"), [this]() { GoTo("categories"); }));
        auto grid = Grid("web.picks.grid");
        int n = 0;
        for (const Component* app : featured) {
            if (n >= 4) break;
            grid->AddChild(AppCard(*app, "web.picks." + std::to_string(n++)));
        }
        band.inner->AddChild(grid);
    }
    // New and updated: the newest releases, numbered.
    {
        Band band = AddBand("web.new", Theme::kSurfaceSoft, 32.0f, 32.0f);
        band.inner->AddChild(SectionHead("web.new.head", Tr("New and updated"), Tr("Fresh releases from the signed catalog, newest first.")));
        auto grid = Grid("web.new.grid", 12.0f);
        int n = 0;
        for (const Component* app : Newest(8)) grid->AddChild(AppRow(*app, ++n, "web.new." + std::to_string(n)));
        band.inner->AddChild(grid);
    }
    // Free essentials.
    if (const std::vector<const Component*> free = Free(6); !free.empty()) {
        Band band = AddBand("web.free", Theme::kPageBackground, 32.0f, 24.0f);
        band.inner->AddChild(SectionHead("web.free.head", Tr("Free essentials"), Tr("Security, system and everyday tools at no cost."),
                                         Tr("See all free apps"), [this]() { priceFilter_ = "free"; GoTo("categories"); }));
        auto grid = Grid("web.free.grid", 12.0f);
        int n = 0;
        for (const Component* app : free) grid->AddChild(AppMini(*app, "web.free." + std::to_string(n++)));
        band.inner->AddChild(grid);
    }
    // Why UltraStore: drawn, not shipped as pictures.
    {
        Band band = AddBand("web.why", Theme::kPageBackground, 24.0f, 40.0f);
        band.inner->AddChild(SectionHead("web.why.head", Tr("Why UltraStore"), ""));
        auto grid = Grid("web.why.grid");
        // Literal, so the catalogues carry them.
        const std::pair<std::string, std::string> tiles[] = {
            {Tr("Signed catalog"), Tr("Every listing and package is Ed25519-signed and SHA-256 checked. Nothing a mirror serves can roll you back.")},
            {Tr("Small downloads"), Tr("Apps share one UltraCanvas runtime. Install a second app and only its few megabytes come down.")},
            {Tr("Try before you buy"), Tr("Paid apps install as a full trial. Buy once, and updates stay free for the period shown on the page.")},
            {Tr("Linux, macOS, Windows"), Tr("The store picks the build for your machine, Raspberry Pi and Apple silicon included, and keeps it current.")},
        };
        int n = 0;
        for (const auto& [title, text] : tiles) {
            const std::string id = "web.why." + std::to_string(n++);
            auto card = Theme::Surface(id, 8.0f, Theme::kSurfaceSoft);
            card->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(kCardMinWidth));
            card->AddChild(Theme::Display(id + ".title", title, 17.0f));
            card->AddChild(Theme::Text(id + ".text", text, 14.0f, FontWeight::Normal, Theme::kTextBody));
            grid->AddChild(card);
        }
        band.inner->AddChild(grid);
    }
}

void WebStore::BuildResults() {
    Band band = AddBand("web.results", Theme::kPageBackground, 32.0f, 40.0f);
    const std::vector<const Component*> found = Matching(search_);
    band.inner->AddChild(SectionHead("web.results.head", TrF("Results for \"{0}\"", search_), TrN("{n} app", "{n} apps", static_cast<int>(found.size()))));
    if (found.empty()) {
        band.inner->AddChild(Theme::Text("web.results.none", Tr("No app matches. Try another word, or browse the categories."), 15.0f, FontWeight::Normal, Theme::kTextBody));
        return;
    }
    auto grid = Grid("web.results.grid");
    int n = 0;
    for (const Component* app : found) grid->AddChild(AppCard(*app, "web.results." + std::to_string(n++)));
    band.inner->AddChild(grid);
}

void WebStore::BuildCategories(const std::string& selected) {
    Band band = AddBand("web.browse", Theme::kPageBackground, 32.0f, 40.0f);
    band.inner->AddChild(SectionHead("web.browse.head", Tr("Browse apps"), Tr("Every app in the signed catalog, by category. Pick a category and a price to narrow the list.")));

    // The category pills, with their counts.
    auto cats = Theme::Row("web.browse.cats", 10.0f, true);
    cats->SetPadding(12.0f, 0.0f);
    auto addCat = [this, &cats, &selected](const std::string& id, const std::string& text, size_t count) {
        auto chip = Theme::Pin(CreateFilterChip("web.browse.cat." + (id.empty() ? std::string("all") : id), 0, 0, text + "  " + std::to_string(count), id == selected));
        chip->SetStyle(Theme::ChipStyleFor());
        const std::string page = id.empty() ? std::string("categories") : "category/" + id;
        chip->onClick = [this, page]() { GoTo(page); };
        cats->AddChild(chip);
    };
    const std::vector<const Component*> listed = Listed();
    addCat("", Tr("All"), listed.size());
    for (const auto& [id, count] : CategoriesWithCounts()) addCat(id, CategoryName(id), count);
    band.inner->AddChild(cats);

    // Price and order.
    auto filters = Theme::Row("web.browse.filters", Theme::kGap, true);
    auto group = [this, &filters](const std::string& id, const std::string& label, const std::vector<std::pair<std::string, std::string>>& options,
                                  std::string& state) {
        auto row = Theme::Row("web.browse." + id, 6.0f);
        row->AddChild(Theme::Caption("web.browse." + id + ".label", label));
        for (const auto& [value, text] : options) {
            auto chip = Theme::Pin(CreateFilterChip("web.browse." + id + "." + (value.empty() ? std::string("all") : value), 0, 0, text, state == value));
            ChipStyle style = Theme::ChipStyleFor();
            style.height = 34.0f;
            style.cornerRadius = 17.0f;
            chip->SetStyle(style);
            std::string* slot = &state;
            const std::string v = value;
            chip->onClick = [this, slot, v]() { *slot = v; GoTo(page_, false); };
            row->AddChild(chip);
        }
        filters->AddChild(row);
    };
    group("price", Tr("Price"), {{"", Tr("All")}, {"free", Tr("Free")}, {"paid", Tr("Paid")}}, priceFilter_);
    group("sort", Tr("Sort by"), {{"newest", Tr("Newest release")}, {"name", Tr("Name A to Z")}, {"price", Tr("Price, low to high")}}, sort_);
    band.inner->AddChild(filters);

    std::vector<const Component*> apps = selected.empty() ? listed : InCategory(selected);
    if (priceFilter_ == "free") apps.erase(std::remove_if(apps.begin(), apps.end(), [](const Component* a) { return a->pricing.model != PricingModel::Free; }), apps.end());
    if (priceFilter_ == "paid") apps.erase(std::remove_if(apps.begin(), apps.end(), [](const Component* a) { return a->pricing.model == PricingModel::Free; }), apps.end());
    if (sort_ == "newest") {
        std::stable_sort(apps.begin(), apps.end(), [](const Component* a, const Component* b) {
            const Release* ra = NewestOf(*a);
            const Release* rb = NewestOf(*b);
            return (ra ? ra->date : std::string()) > (rb ? rb->date : std::string());
        });
    } else if (sort_ == "price") {
        std::stable_sort(apps.begin(), apps.end(), [](const Component* a, const Component* b) {
            const double pa = a->pricing.model == PricingModel::Free ? 0.0 : std::atof(a->pricing.price.c_str());   // locale-ok: the catalog's decimal point
            const double pb = b->pricing.model == PricingModel::Free ? 0.0 : std::atof(b->pricing.price.c_str());   // locale-ok: as above
            return pa < pb;
        });
    }
    auto head = Theme::Row("web.browse.count", 8.0f);
    head->SetPadding(16.0f, 0.0f, 8.0f, 0.0f);
    head->AddChild(Theme::Display("web.browse.heading", selected.empty() ? Tr("All apps") : CategoryName(selected), 22.0f));
    head->AddChild(Theme::Caption("web.browse.n", "· " + TrN("{n} app", "{n} apps", static_cast<int>(apps.size()))));
    band.inner->AddChild(head);
    if (apps.empty()) {
        band.inner->AddChild(Theme::Text("web.browse.none", Tr("No apps match these filters yet."), 15.0f, FontWeight::Normal, Theme::kTextBody));
        return;
    }
    auto grid = Grid("web.browse.grid");
    int n = 0;
    for (const Component* app : apps) grid->AddChild(AppCard(*app, "web.browse." + std::to_string(n++)));
    band.inner->AddChild(grid);
}

void WebStore::BuildApp(const Component& app) {
    const Release* release = NewestOf(app);
    const std::string category = CategoryOf(app);
    Band band = AddBand("web.app", Theme::kPageBackground, 24.0f, 40.0f);

    // Breadcrumb.
    auto crumbs = Theme::Row("web.app.crumbs", 2.0f, true);
    auto crumb = [this, &crumbs](const std::string& id, const std::string& text, const std::string& page) {
        auto b = Theme::Button(id, text, static_cast<float>(24 + text.size() * 8), Theme::ButtonKind::Ghost, 32);
        b->onClick = [this, page]() { GoTo(page); };
        crumbs->AddChild(b);
    };
    crumb("web.app.crumb.home", Tr("Discover"), "");
    if (!category.empty()) {
        crumbs->AddChild(Theme::Caption("web.app.crumb.sep1", "/"));
        crumb("web.app.crumb.cat", CategoryName(category), "category/" + category);
    }
    crumbs->AddChild(Theme::Caption("web.app.crumb.sep2", "/"));
    crumbs->AddChild(Theme::Caption("web.app.crumb.name", app.name));
    band.inner->AddChild(crumbs);

    // The head: icon, name, summary, publisher; the badges; the actions.
    auto head = Theme::Row("web.app.head", 24.0f, true);
    head->SetPadding(20.0f, 0.0f, 0.0f, 0.0f);
    head->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);
    head->AddChild(Picture("web.app.icon", app.icon, app.iconVariants, 112, 112, 112 * 0.22f));
    auto names = Theme::Column("web.app.names", 8.0f);
    names->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(320));
    names->AddChild(Theme::Display("web.app.name", app.name, 36.0f));
    if (const std::string summary = Summary(app); !summary.empty()) names->AddChild(Theme::Text("web.app.summary", summary, 18.0f, FontWeight::Normal, Theme::kTextBody));
    names->AddChild(Theme::Caption("web.app.publisher", PublisherName(app)));

    auto badges = Theme::Row("web.app.badges", 8.0f, true);
    if (app.pricing.model != PricingModel::Free && app.pricing.trialDays > 0) badges->AddChild(Badge("web.app.b.trial", TrN("{n}-day free trial", "{n}-day free trial", app.pricing.trialDays), Theme::kAccentSoft, Theme::kAccentSoftText));
    if (app.openSource) badges->AddChild(Badge("web.app.b.oss", app.license.empty() ? Tr("Open source") : TrF("Open source · {0}", app.license), kSuccessFill, Theme::kSuccess));
    else if (!app.license.empty()) badges->AddChild(Badge("web.app.b.lic", app.license, Theme::kSurfaceMuted, Theme::kTextBody));
    if (!app.ageRating.empty()) badges->AddChild(Badge("web.app.b.age", TrF("PEGI {0}", app.ageRating), Theme::kSurfaceMuted, Theme::kTextBody));
    badges->AddChild(Badge("web.app.b.account", app.requiresAccount ? Tr("Needs an account") : Tr("No account needed"), Theme::kSurfaceMuted, Theme::kTextBody));
    if (app.updates.period == UpdatePeriod::Until && !app.updates.until.empty()) badges->AddChild(Badge("web.app.b.updates", TrF("Updates free until {0}", PrettyDate(app.updates.until)), kWarningFill, kWarningInk));
    if (release && release->channel == "beta") badges->AddChild(Badge("web.app.b.beta", Tr("Beta"), kBetaFill, kBetaInk));
    if (app.lifecycle == Lifecycle::Deprecated) badges->AddChild(Badge("web.app.b.dep", Tr("Deprecated"), kWarningFill, kWarningInk));
    if (app.lifecycle == Lifecycle::Withdrawn) badges->AddChild(Badge("web.app.b.wd", Tr("Withdrawn"), kWarningFill, kWarningInk));
    names->AddChild(badges);

    auto actions = Theme::Row("web.app.actions", Theme::kGap, true);
    actions->SetPadding(8.0f, 0.0f, 0.0f, 0.0f);
    if (app.lifecycle != Lifecycle::Withdrawn) {
        actions->AddChild(ActionButton(app, "web.app.action", 52.0f));
        if (app.pricing.model != PricingModel::Free) {
            auto price = Theme::Text("web.app.price", PriceText(app), 20.0f, FontWeight::Bold);
            actions->AddChild(price);
        }
    }
    names->AddChild(actions);
    names->AddChild(Theme::Caption("web.app.hint", downloadUrl_.empty()
        ? Tr("Installing, trying and buying happen in the UltraStore app, which verifies every package.")
        : Tr("Installing, trying and buying happen in the UltraStore app, which verifies every package. Not installed yet? Get UltraStore above.")));
    head->AddChild(names);
    band.inner->AddChild(head);

    // The facts in a row of small columns.
    auto facts = Theme::Row("web.app.facts", 28.0f, true);
    facts->SetPadding(24.0f, 0.0f, 0.0f, 0.0f);
    facts->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);
    if (release) {
        facts->AddChild(InfoLine("web.app.f.version", Tr("Version"), release->version));
        if (!release->date.empty()) facts->AddChild(InfoLine("web.app.f.date", Tr("Released"), PrettyDate(release->date)));
        facts->AddChild(InfoLine("web.app.f.channel", Tr("Channel"), release->channel == "beta" ? Tr("Beta") : Tr("Stable")));
    }
    if (!category.empty()) facts->AddChild(InfoLine("web.app.f.cat", Tr("Category"), CategoryName(category)));
    if (const std::string runs = RunsOn(app); !runs.empty()) facts->AddChild(InfoLine("web.app.f.runs", Tr("Runs on"), runs));
    facts->AddChild(InfoLine("web.app.f.id", Tr("App id"), app.id));
    band.inner->AddChild(facts);

    // Screenshots, in the design's column, lazily: only this page asks for them.
    if (!app.screenshots.empty()) {
        auto shotsHead = SectionHead("web.app.shots.head", Tr("Screenshots"), "");
        shotsHead->SetPadding(16.0f, 0.0f, 16.0f, 0.0f);
        band.inner->AddChild(shotsHead);
        auto row = Theme::Row("web.app.shots", Theme::kGap, true);
        for (size_t i = 0; i < app.screenshots.size(); ++i) {
            const std::vector<MediaVariant> none;
            const std::vector<MediaVariant>& variants = i < app.screenshotVariants.size() ? app.screenshotVariants[i] : none;
            row->AddChild(Picture("web.app.shot." + std::to_string(i), app.screenshots[i], variants, kShotWidth, kShotHeight, 14.0f));
        }
        band.inner->AddChild(row);
    }

    // Two columns: the texts, and the information card.
    auto columns = Theme::Row("web.app.columns", 32.0f, true);
    columns->SetPadding(24.0f, 0.0f, 0.0f, 0.0f);
    columns->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);
    auto texts = Theme::Column("web.app.texts", 24.0f);
    texts->layoutItem.SetFlexGrow(2).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(420));
    if (const std::string about = Description(app); !about.empty()) {
        auto section = Theme::Column("web.app.about", 10.0f);
        section->AddChild(Theme::Heading("web.app.about.title", Tr("About this app")));
        // Paragraphs on blank lines.
        size_t at = 0;
        int n = 0;
        while (at <= about.size()) {
            size_t end = about.find("\n\n", at);
            if (end == std::string::npos) end = about.size();
            const std::string paragraph = about.substr(at, end - at);
            if (!paragraph.empty()) section->AddChild(Theme::Text("web.app.about.p" + std::to_string(n++), paragraph, 15.0f, FontWeight::Normal, Theme::kTextBody));
            at = end + 2;
        }
        texts->AddChild(section);
    }
    if (release && !release->notes.Empty()) {
        auto section = Theme::Column("web.app.news", 10.0f);
        section->AddChild(Theme::Heading("web.app.news.title", TrF("What's new in {0}", release->version)));
        section->AddChild(Theme::Text("web.app.news.text", release->notes.Text(lang_), 15.0f, FontWeight::Normal, Theme::kTextBody));
        texts->AddChild(section);
    }
    if (release && (!release->depends.empty() || !release->packages.empty())) {
        auto section = Theme::Column("web.app.installs", 10.0f);
        section->AddChild(Theme::Heading("web.app.installs.title", Tr("What it installs")));
        section->AddChild(Theme::Caption("web.app.installs.note", Tr("Apps share layers. Only the layers missing on a machine are downloaded.")));
        int n = 0;
        for (const Dependency& d : release->depends) {
            const Component* component = catalog_.FindComponent(d.id);
            auto line = Theme::RowSurface("web.app.dep." + std::to_string(n++), Theme::kGap);
            auto name = Theme::Text("web.app.dep.name" + std::to_string(n), (component ? component->name : d.id) + " " + d.version, 14.0f, FontWeight::Bold);
            name->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            line->AddChild(name);
            line->AddChild(Theme::Caption("web.app.dep.kind" + std::to_string(n), component && component->kind == ComponentKind::Runtime ? Tr("Runtime") : Tr("Library")));
            section->AddChild(line);
        }
        for (const Package& p : release->packages) {
            auto line = Theme::RowSurface("web.app.pkg." + std::to_string(n++), Theme::kGap);
            auto name = Theme::Text("web.app.pkg.name" + std::to_string(n), app.name + " " + release->version, 14.0f, FontWeight::Bold);
            name->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            line->AddChild(name);
            line->AddChild(Theme::Caption("web.app.pkg.target" + std::to_string(n), p.target.Describe() + " · " + p.format + " · " + Tr("SHA-256 verified")));
            if (p.size) line->AddChild(Theme::Caption("web.app.pkg.size" + std::to_string(n), FormatBytes(p.size)));
            section->AddChild(line);
        }
        texts->AddChild(section);
    }
    columns->AddChild(texts);

    auto info = Theme::Surface("web.app.info", 10.0f, Theme::kSurfaceSoft);
    info->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(300));
    info->AddChild(Theme::Heading("web.app.info.title", Tr("Information")));
    info->AddChild(InfoLine("web.app.i.pub", Tr("Publisher"), PublisherName(app)));
    if (!app.license.empty()) info->AddChild(InfoLine("web.app.i.lic", Tr("Licence"), app.license));
    info->AddChild(InfoLine("web.app.i.price", Tr("Price"), PriceText(app)));
    if (app.pricing.model != PricingModel::Free) {
        info->AddChild(InfoLine("web.app.i.trial", Tr("Trial"), app.pricing.trialDays > 0 ? TrN("{n} day, full app", "{n} days, full app", app.pricing.trialDays) : Tr("None")));
        std::string updates = Tr("Free, always");
        if (app.updates.period == UpdatePeriod::Never) updates = Tr("The bought version only");
        else if (app.updates.period == UpdatePeriod::Until) updates = TrF("Free until {0}", PrettyDate(app.updates.until));
        info->AddChild(InfoLine("web.app.i.updates", Tr("Updates"), updates));
    }
    if (!app.ageRating.empty()) info->AddChild(InfoLine("web.app.i.age", Tr("Age rating"), TrF("PEGI {0}", app.ageRating)));
    {
        std::vector<std::string> langs;
        for (const auto& [code, text] : app.summary.byLanguage) langs.push_back(code);
        if (!langs.empty()) info->AddChild(InfoLine("web.app.i.langs", Tr("Languages"), std::to_string(langs.size()) + " · " + app.sourceLanguage));
    }
    auto links = Theme::Row("web.app.links", 4.0f, true);
    if (!app.homepage.empty()) links->AddChild(LinkButton("web.app.l.home", Tr("Homepage"), app.homepage));
    if (!app.supportUrl.empty()) links->AddChild(LinkButton("web.app.l.support", Tr("Support"), app.supportUrl));
    if (!app.sourceUrl.empty()) links->AddChild(LinkButton("web.app.l.source", Tr("Source code"), app.sourceUrl));
    if (!app.privacyUrl.empty()) links->AddChild(LinkButton("web.app.l.privacy", Tr("Privacy policy"), app.privacyUrl));
    info->AddChild(links);
    columns->AddChild(info);
    band.inner->AddChild(columns);

    // More from the publisher.
    std::vector<const Component*> more;
    for (const Component* other : Listed()) if (other->id != app.id && other->publisher == app.publisher && more.size() < 4) more.push_back(other);
    if (!more.empty()) {
        auto section = Theme::Column("web.app.more", Theme::kGap);
        section->SetPadding(32.0f, 0.0f, 0.0f, 0.0f);
        section->AddChild(Theme::Heading("web.app.more.title", TrF("More from {0}", PublisherName(app))));
        auto grid = Grid("web.app.more.grid", 12.0f);
        int n = 0;
        for (const Component* other : more) grid->AddChild(AppMini(*other, "web.app.more." + std::to_string(n++)));
        section->AddChild(grid);
        band.inner->AddChild(section);
    }
}

// ===========================================================================
// Pieces
// ===========================================================================
std::shared_ptr<UltraCanvasContainer> WebStore::Grid(const std::string& id, float gap) {
    auto grid = Theme::Row(id, gap, true);
    grid->layout.SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    grid->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Auto());
    return grid;
}

std::shared_ptr<UltraCanvasContainer> WebStore::SectionHead(const std::string& id, const std::string& title, const std::string& subtitle,
                                                            const std::string& linkText, std::function<void()> onLink) {
    auto row = Theme::Row(id, Theme::kGap, true);
    row->layout.SetFlexJustifyContent(CSSLayout::JustifyContent::SpaceBetween).SetFlexAlignItems(CSSLayout::AlignItems::End);
    row->SetPadding(0.0f, 0.0f, 16.0f, 0.0f);
    auto text = Theme::Column(id + ".text", 4.0f);
    text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    text->AddChild(Theme::Display(id + ".title", title, 26.0f));
    if (!subtitle.empty()) text->AddChild(Theme::Text(id + ".sub", subtitle, 15.0f, FontWeight::Normal, Theme::kTextSecondary));
    row->AddChild(text);
    if (!linkText.empty()) {
        auto link = Theme::Button(id + ".link", linkText, static_cast<float>(32 + linkText.size() * 8), Theme::ButtonKind::Secondary);
        link->onClick = std::move(onLink);
        row->AddChild(link);
    }
    return row;
}

std::shared_ptr<UltraCanvasButton> WebStore::ActionButton(const Component& app, const std::string& id, float height) {
    std::string text = Tr("Install");
    if (app.pricing.model != PricingModel::Free) text = app.pricing.trialDays > 0 ? Tr("Try free") : TrF("Buy · {0}", PriceText(app));
    auto button = Theme::Button(id, text, static_cast<float>(40 + text.size() * 9), Theme::ButtonKind::Primary, height);
    button->SetTooltip(Tr("Opens the app in the UltraStore app on this computer"));
    const std::string appId = app.id;
    button->onClick = [this, appId]() {
        if (const Component* a = catalog_.FindApp(appId)) OpenInStore(*a);
    };
    return button;
}

void WebStore::OpenInStore(const Component& app) {
    Platform::Log("open in store: " + app.id);
    Platform::OpenExternal("ultrastore://app/" + app.id);
}

std::shared_ptr<UltraCanvasContainer> WebStore::Badge(const std::string& id, const std::string& text, const Color& fill, const Color& ink) {
    auto pill = Theme::Row(id, 0.0f);
    pill->SetBackgroundColor(fill);
    pill->SetBorderRadius(14.0f);
    pill->SetPadding(5.0f, 12.0f);
    auto label = Theme::Text(id + ".text", text, 12.0f, FontWeight::Bold, ink);
    label->SetWrap(TextWrap::WrapNone);
    pill->AddChild(label);
    return pill;
}

std::shared_ptr<UltraCanvasContainer> WebStore::InfoLine(const std::string& id, const std::string& key, const std::string& value) {
    auto column = Theme::Column(id, 2.0f);
    column->AddChild(Theme::Caption(id + ".key", key));
    column->AddChild(Theme::Text(id + ".value", value, 15.0f, FontWeight::Bold));
    return column;
}

std::shared_ptr<UltraCanvasButton> WebStore::LinkButton(const std::string& id, const std::string& text, const std::string& url) {
    auto b = Theme::Button(id, text, static_cast<float>(24 + text.size() * 8), Theme::ButtonKind::Ghost, 36);
    ButtonStyle s = Theme::ButtonStyleFor(Theme::ButtonKind::Ghost, 36);
    s.normalTextColor = s.hoverTextColor = s.pressedTextColor = Theme::kAccent;
    b->SetStyle(s);
    b->SetTooltip(url);
    b->onClick = [url]() { Platform::OpenExternal(url); };
    return b;
}

std::shared_ptr<UltraCanvasCard> WebStore::Hero(const Component& app) {
    const Release* release = NewestOf(app);
    auto card = ClickCard("web.hero.card", Theme::kCardRadius, 36.0f, 0.0f, Theme::kAccentFaint, Theme::kAccentSoft, Theme::kAccentSoft);
    auto row = Theme::Row("web.hero.row", 32.0f, true);
    row->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);

    auto text = Theme::Column("web.hero.text", Theme::kGap);
    text->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(360));
    text->AddChild(Theme::Eyebrow("web.hero.eyebrow", Tr("Featured app"), Theme::kAccentSoftText));
    auto nameRow = Theme::Row("web.hero.nameRow", Theme::kGap);
    nameRow->AddChild(Picture("web.hero.icon", app.icon, app.iconVariants, 72, 72, 72 * 0.22f));
    auto names = Theme::Column("web.hero.names", 2.0f);
    names->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    names->AddChild(Theme::Display("web.hero.name", app.name, 34.0f));
    names->AddChild(Theme::Caption("web.hero.meta", PublisherName(app) + (release ? " · " + TrF("Version {0}", release->version) : std::string())));
    nameRow->AddChild(names);
    text->AddChild(nameRow);
    if (const std::string summary = Summary(app); !summary.empty()) text->AddChild(Theme::Text("web.hero.summary", summary, 17.0f, FontWeight::Normal, Theme::kTextBody));
    auto actions = Theme::Row("web.hero.actions", Theme::kGap, true);
    actions->AddChild(ActionButton(app, "web.hero.action", 52.0f));
    auto details = Theme::Button("web.hero.details", Tr("See the app"), 130, Theme::ButtonKind::Secondary, 52.0f);
    const std::string appId = app.id;
    details->onClick = [this, appId]() { GoTo("app/" + appId); };
    actions->AddChild(details);
    if (app.pricing.model != PricingModel::Free) actions->AddChild(Theme::Text("web.hero.price", PriceText(app), 18.0f, FontWeight::Bold));
    text->AddChild(actions);
    auto badges = Theme::Row("web.hero.badges", 8.0f, true);
    if (app.updates.period == UpdatePeriod::Until && !app.updates.until.empty()) badges->AddChild(Badge("web.hero.b.updates", TrF("Free updates until {0}", PrettyDate(app.updates.until)), Theme::kSurface, Theme::kTextBody));
    if (app.openSource) badges->AddChild(Badge("web.hero.b.oss", app.license.empty() ? Tr("Open source") : TrF("Open source · {0}", app.license), Theme::kSurface, Theme::kTextBody));
    if (const std::string runs = RunsOn(app); !runs.empty()) badges->AddChild(Badge("web.hero.b.runs", runs, Theme::kSurface, Theme::kTextBody));
    if (!app.ageRating.empty()) badges->AddChild(Badge("web.hero.b.age", TrF("PEGI {0}", app.ageRating), Theme::kSurface, Theme::kTextBody));
    text->AddChild(badges);
    row->AddChild(text);

    // The first screenshot at the design's size, else the icon large.
    if (!app.screenshots.empty()) {
        const std::vector<MediaVariant> none;
        row->AddChild(Picture("web.hero.shot", app.screenshots.front(), app.screenshotVariants.empty() ? none : app.screenshotVariants.front(), kShotWidth, kShotHeight, 14.0f));
    } else {
        row->AddChild(Picture("web.hero.big", app.icon, app.iconVariants, 200, 200, 200 * 0.22f));
    }
    card->AddContent(row);
    card->SetClickable(true);
    card->onClick = [this, appId]() { GoTo("app/" + appId); };
    return card;
}

std::shared_ptr<UltraCanvasCard> WebStore::AppCard(const Component& app, const std::string& id) {
    auto card = ClickCard(id, Theme::kCardRadius, Theme::kCardPadding, 12.0f);
    card->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(kCardMinWidth));
    auto top = Theme::Row(id + ".top", 14.0f);
    top->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);
    top->AddChild(Picture(id + ".icon", app.icon, app.iconVariants, 64, 64, 64 * 0.22f));
    auto names = Theme::Column(id + ".names", 2.0f);
    names->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    names->AddChild(Theme::Display(id + ".name", app.name, 17.0f));
    std::string meta = PublisherName(app);
    if (const std::string category = CategoryOf(app); !category.empty()) meta += (meta.empty() ? "" : " · ") + CategoryName(category);
    names->AddChild(Theme::Caption(id + ".meta", meta));
    top->AddChild(names);
    card->AddContent(top);
    if (const std::string summary = Summary(app); !summary.empty()) {
        card->AddContent(Theme::Text(id + ".summary", summary, 14.0f, FontWeight::Normal, Theme::kTextBody));
    }
    const bool free = app.pricing.model == PricingModel::Free;
    auto price = Theme::Text(id + ".price", PriceText(app), 15.0f, FontWeight::Bold, free ? Theme::kSuccess : Theme::kTextPrimary);
    price->SetWrap(TextWrap::WrapNone);
    price->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    card->AddAction(price);
    const std::string appId = app.id;
    auto open = Theme::Button(id + ".open", free ? Tr("Install") : (app.pricing.trialDays > 0 ? Tr("Try") : Tr("Buy")), 88, Theme::ButtonKind::Soft);
    open->SetTooltip(Tr("Opens the app in the UltraStore app on this computer"));
    open->onClick = [this, appId]() { if (const Component* a = catalog_.FindApp(appId)) OpenInStore(*a); };
    card->AddAction(open);
    card->SetClickable(true);
    card->onClick = [this, appId]() { GoTo("app/" + appId); };
    return card;
}

std::shared_ptr<UltraCanvasCard> WebStore::AppRow(const Component& app, int number, const std::string& id) {
    const Release* release = NewestOf(app);
    auto card = ClickCard(id, Theme::kRowRadius, 12.0f, 0.0f);
    card->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(kColumnWidth / 2 - 40));
    auto row = Theme::Row(id + ".row", Theme::kGap);
    auto n = Theme::Text(id + ".n", std::to_string(number), 15.0f, FontWeight::Bold, Theme::kTextMuted);
    n->SetWrap(TextWrap::WrapNone);
    n->SetElementSize(CSSLayout::Dimension::Px(22), CSSLayout::Dimension::Auto());
    row->AddChild(n);
    row->AddChild(Picture(id + ".icon", app.icon, app.iconVariants, 44, 44, 44 * 0.22f));
    auto names = Theme::Column(id + ".names", 2.0f);
    names->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    names->AddChild(Theme::Display(id + ".name", app.name, 16.0f));
    std::string line = release ? release->version : std::string();
    if (const std::string category = CategoryOf(app); !category.empty()) line += (line.empty() ? "" : " · ") + CategoryName(category);
    if (release && !release->date.empty()) line += (line.empty() ? "" : " · ") + PrettyDate(release->date);
    if (app.pricing.model != PricingModel::Free) line += (line.empty() ? "" : " · ") + PriceText(app);
    names->AddChild(Theme::Caption(id + ".line", line));
    row->AddChild(names);
    const bool free = app.pricing.model == PricingModel::Free;
    const std::string appId = app.id;
    auto open = Theme::Button(id + ".open", free ? Tr("Install") : (app.pricing.trialDays > 0 ? Tr("Try") : Tr("Buy")), 84, Theme::ButtonKind::Soft);
    open->onClick = [this, appId]() { if (const Component* a = catalog_.FindApp(appId)) OpenInStore(*a); };
    row->AddChild(open);
    card->AddContent(row);
    card->SetClickable(true);
    card->onClick = [this, appId]() { GoTo("app/" + appId); };
    return card;
}

std::shared_ptr<UltraCanvasCard> WebStore::AppMini(const Component& app, const std::string& id) {
    auto card = ClickCard(id, Theme::kRowRadius, 12.0f, 0.0f);
    card->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(200));
    auto tile = Theme::Row(id + ".row", 12.0f);
    tile->AddChild(Picture(id + ".icon", app.icon, app.iconVariants, 48, 48, 48 * 0.22f));
    auto names = Theme::Column(id + ".names", 2.0f);
    names->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    names->AddChild(Theme::Display(id + ".name", app.name, 15.0f));
    std::string line = Summary(app);
    if (line.size() > 40) {
        size_t cut = 40;
        while (cut > 0 && (static_cast<unsigned char>(line[cut]) & 0xC0) == 0x80) --cut;
        line = line.substr(0, cut) + "…";
    }
    if (app.pricing.model != PricingModel::Free) line += (line.empty() ? "" : " · ") + PriceText(app);
    names->AddChild(Theme::Caption(id + ".line", line));
    tile->AddChild(names);
    card->AddContent(tile);
    const std::string appId = app.id;
    card->SetClickable(true);
    card->onClick = [this, appId]() { GoTo("app/" + appId); };
    return card;
}

// ===========================================================================
// Pictures
// ===========================================================================
std::string WebStore::PickUrl(const std::string& original, const std::vector<MediaVariant>& variants, float slotWidth) const {
    // The format the browser decodes, AVIF first; then the smallest width
    // at or above the slot times the pixel ratio, else the widest there is.
    const float wanted = slotWidth * static_cast<float>(Platform::PixelRatio());
    for (const char* format : {"avif", "webp", "png", "jpeg"}) {
        if (!Platform::DecodesFormat(format)) continue;
        const MediaVariant* best = nullptr;
        const MediaVariant* widest = nullptr;
        for (const MediaVariant& v : variants) {
            if (v.format != format) continue;
            if (!widest || v.width > widest->width) widest = &v;
            if (static_cast<float>(v.width) >= wanted && (!best || v.width < best->width)) best = &v;
        }
        if (best) return best->url;
        if (widest) return widest->url;
    }
    return original;
}

std::shared_ptr<UltraCanvasImageElement> WebStore::Picture(const std::string& id, const std::string& original,
                                                           const std::vector<MediaVariant>& variants, float w, float h, float radius) {
    auto image = Theme::Pin(CreateImageElement(id + "." + std::to_string(ids_++), w, h));
    image->SetElementSize(CSSLayout::Dimension::Px(w), CSSLayout::Dimension::Px(h));
    image->SetFitMode(ImageFitMode::Cover);
    image->SetBackgroundColor(Theme::kSurfaceMuted);
    image->SetBorderRadius(radius);
    const std::string url = PickUrl(original, variants, w);
    if (url.empty()) return image;
    if (const auto known = pictures_.find(url); known != pictures_.end()) {
        if (known->second) image->LoadFromImage(known->second);
        return image;
    }
    const bool first = waiting_[url].empty();
    waiting_[url].push_back(image);
    if (first) {
        std::weak_ptr<int> token = alive_;
        Platform::LoadPicture(url, [this, token, url](std::shared_ptr<UCImage> decoded) {
            if (token.expired()) return;
            Deliver(url, decoded);
        });
    }
    return image;
}

void WebStore::Deliver(const std::string& url, const std::shared_ptr<UCImage>& image) {
    pictures_[url] = image;
    auto waiting = waiting_.find(url);
    if (waiting == waiting_.end()) return;
    for (const auto& weak : waiting->second) {
        if (auto element = weak.lock(); element && image) {
            element->LoadFromImage(image);
            element->RequestRedraw();
        }
    }
    waiting_.erase(waiting);
    if (!image) Platform::Log("picture failed: " + url);
}

// ===========================================================================
// Texts and lists
// ===========================================================================
std::string WebStore::Summary(const Component& app) const {
    std::string text = app.summary.Text(lang_);
    if (text.empty() && !app.sourceLanguage.empty()) text = app.summary.Text(app.sourceLanguage);
    return text;
}

std::string WebStore::Description(const Component& app) const {
    std::string text = app.description.Text(lang_);
    if (text.empty() && !app.sourceLanguage.empty()) text = app.description.Text(app.sourceLanguage);
    return text;
}

std::string WebStore::PublisherName(const Component& app) const {
    for (const Publisher& p : catalog_.publishers) if (p.id == app.publisher) return p.name;
    return app.publisher;
}

std::string WebStore::PriceText(const Component& app) const {
    const Pricing& pricing = app.pricing;
    if (pricing.model == PricingModel::Free) return Tr("Free");
    std::string label = pricing.price;
    if (!pricing.currency.empty()) label += " " + pricing.currency;
    if (pricing.model == PricingModel::Monthly) return TrF("{0} / month", label);
    if (pricing.model == PricingModel::Yearly) return TrF("{0} / year", label);
    return label;
}

std::string WebStore::RunsOn(const Component& app) const {
    const Release* release = NewestOf(app);
    if (!release) return {};
    std::vector<std::string> names;
    for (const Package& p : release->packages) {
        const std::string name = OsLabel(p.target.os);
        if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    }
    std::string out;
    for (const std::string& n : names) out += (out.empty() ? "" : " · ") + n;
    return out;
}

std::string WebStore::CategoryOf(const Component& app) const {
    for (const std::string& c : app.categories) {
        const std::string id = CategoryId(c);
        if (IsStoreCategory(id)) return id;
    }
    return {};
}

std::vector<const Component*> WebStore::Listed() const {
    std::vector<const Component*> out;
    for (const Component& app : catalog_.apps) if (Shown(app)) out.push_back(&app);
    std::sort(out.begin(), out.end(), [](const Component* a, const Component* b) {
        const std::string la = Lower(a->name), lb = Lower(b->name);
        return la != lb ? la < lb : a->id < b->id;
    });
    return out;
}

std::vector<const Component*> WebStore::Featured() const {
    std::vector<const Component*> out;
    for (const Component* app : Newest(catalog_.apps.size())) if (app->featured && app->lifecycle == Lifecycle::Active) out.push_back(app);
    return out;
}

std::vector<const Component*> WebStore::Newest(size_t count) const {
    std::vector<const Component*> out = Listed();
    std::stable_sort(out.begin(), out.end(), [](const Component* a, const Component* b) {
        const Release* ra = NewestOf(*a);
        const Release* rb = NewestOf(*b);
        return (ra ? ra->date : std::string()) > (rb ? rb->date : std::string());
    });
    if (out.size() > count) out.resize(count);
    return out;
}

std::vector<const Component*> WebStore::Free(size_t count) const {
    std::vector<const Component*> out;
    for (const Component* app : Listed()) if (app->pricing.model == PricingModel::Free && out.size() < count) out.push_back(app);
    return out;
}

std::vector<const Component*> WebStore::InCategory(const std::string& id) const {
    std::vector<const Component*> out;
    for (const Component* app : Listed()) {
        for (const std::string& c : app->categories) if (CategoryId(c) == id) { out.push_back(app); break; }
    }
    return out;
}

std::vector<const Component*> WebStore::Matching(const std::string& search) const {
    // Every word must occur in the name, the id, the publisher, the summary,
    // a tag or a category, without case (ASCII; other scripts as typed).
    std::vector<std::string> words;
    std::string word;
    for (char c : Lower(search) + " ") {
        if (std::isspace(static_cast<unsigned char>(c))) { if (!word.empty()) words.push_back(word); word.clear(); }
        else word += c;
    }
    std::vector<const Component*> out;
    for (const Component* app : Listed()) {
        std::string hay = Lower(app->name) + " " + Lower(app->id) + " " + Lower(PublisherName(*app)) + " " + Lower(Summary(*app));
        for (const std::string& t : app->tags) hay += " " + Lower(t);
        for (const std::string& c : app->categories) hay += " " + Lower(c) + " " + Lower(CategoryName(c));
        bool all = true;
        for (const std::string& w : words) if (hay.find(w) == std::string::npos) { all = false; break; }
        if (all) out.push_back(app);
    }
    // An unlisted app is found by its exact id.
    for (const Component& app : catalog_.apps) {
        if (app.visibility == Visibility::Unlisted && Lower(app.id) == Lower(search)) out.push_back(&app);
    }
    return out;
}

std::vector<std::pair<std::string, size_t>> WebStore::CategoriesWithCounts() const {
    std::vector<std::pair<std::string, size_t>> out;
    for (const std::string& id : StoreCategories()) {
        const size_t count = InCategory(id).size();
        if (count) out.push_back({id, count});
    }
    return out;
}

} // namespace UltraStore::Web
