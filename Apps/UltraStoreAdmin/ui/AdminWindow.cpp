// Apps/UltraStoreAdmin/ui/AdminWindow.cpp
// The shell, the store's pages (Overview, Settings, Keys, Publish) and the
// worker thread; the component form is in AdminComponentPage.cpp.
// Version: 0.5.0 - the Marketplace page and the notices banner
// Version: 0.4.0 - the payment settings and Sync sales
// Version: 0.3.0 - the Affiliates and codes page
// Version: 0.2.0 - the upload to the host on the Publish page
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "AdminWindow.h"

#include "AdminTheme.h"
#include "CatalogPublisher.h"
#include "KeyVault.h"
#include "Affiliates.h"
#include "StoreQuery.h"
#include "Uploader.h"
#include "Translation.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <sstream>
#include <fstream>

#ifndef ULTRASTOREADMIN_VERSION
#error "ULTRASTOREADMIN_VERSION must be passed by CMake (from Docs/UltraStoreAdmin/CHANGELOG.md)"
#endif

using namespace UltraCanvas;

namespace UltraStoreAdmin::Ui {

namespace {

constexpr const char* kOverview = "overview";
constexpr const char* kPublish = "publish";
constexpr const char* kKeys = "keys";
constexpr const char* kAffiliates = "affiliates";
constexpr const char* kSettings = "settings";
constexpr const char* kMarket = "market";
constexpr const char* kComponent = "component";
constexpr const char* kItemPrefix = "c:";

const char* kSettingKeys[] = {"catalog.base_url", "packages.base_url", "store.contact", "texts.languages", "ai.provider",
                               "ai.base_url", "ai.model", "payment.url", "payment.licence_key", "payment.token",
                               "market.url", "market.merchant_id", "market.key"};

// A setting's label, in the person's language.
std::string SettingLabel(const std::string& key) {
    if (key == "catalog.base_url")    return Tr("Catalog base URL");
    if (key == "packages.base_url")   return Tr("Packages base URL");
    if (key == "store.contact")       return Tr("Store contact (on the partners' join pages)");
    if (key == "texts.languages")     return Tr("Languages to translate into");
    if (key == "ai.provider")         return Tr("UltraAI provider");
    if (key == "ai.base_url")         return Tr("UltraAI server");
    if (key == "ai.model")            return Tr("UltraAI model");
    if (key == "payment.url")         return Tr("Payment service URL");
    if (key == "payment.licence_key") return Tr("Licence key (hex, GET /v1/keys)");
    if (key == "payment.token")       return Tr("Payment service admin token");
    if (key == "market.url")          return Tr("Marketplace service URL");
    if (key == "market.merchant_id")  return Tr("Merchant id at the marketplace (from the operator)");
    if (key == "market.key")          return Tr("Key that signs submissions (id; empty: the first active)");
    return key;
}

std::shared_ptr<UltraCanvasContainer> MakeScrollList(const std::string& id) {
    auto list = CreateScrollableContainer(id, 0, 0, 0, 0);
    ContainerStyle style = list->GetContainerStyle();
    style.forceShowVerticalScrollbar = false;
    style.autoShowScrollbars = true;
    style.autoShowHorizontalScrollbar = false;
    list->SetContainerStyle(style);
    list->layout.SetFlexColumn().SetFlexGap(Theme::kGap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    list->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    list->SetPadding(2, 16, 2, 2);
    return list;
}

std::string Joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& l : lines) out += (out.empty() ? "" : "\n") + l;
    return out;
}

bool WriteTextFile(const std::string& path, const std::string& text) {
    std::ofstream out(PathFromUtf8(path), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << text;
    return static_cast<bool>(out);
}

} // namespace

AdminWindow::AdminWindow(UltraCanvasApplication& app, std::string storeDirUtf8) : app_(app) {
    store_.dir = std::move(storeDirUtf8);
}

AdminWindow::~AdminWindow() {
    alive_.reset();
    if (worker_.joinable()) worker_.join();
}

// ===========================================================================
// Shell
// ===========================================================================
bool AdminWindow::Create() {
    if (const auto made = store_.CreateFolders(); !made) storeError_ = made.error.message;
    if (storeError_.empty()) {
        if (const auto opened = db_.Open(store_.DatabasePath()); !opened) storeError_ = opened.error.message + (opened.error.detail.empty() ? "" : " (" + opened.error.detail + ")");
        else if (db_.Setting("texts.languages").empty()) db_.SetSetting("texts.languages", [] {
            std::string s;
            for (const std::string& c : OfficialLanguageCodes()) s += (s.empty() ? "" : " ") + c;
            return s;
        }());
    }

    WindowConfig config;
    config.title = "UltraStoreAdmin " ULTRASTOREADMIN_VERSION;   // the product's name and version: not translated
    config.width = 1240;
    config.height = 800;
    config.minWidth = kMinWidth;
    config.minHeight = kMinHeight;
    config.backgroundColor = Theme::kPageBackground;
    window_ = CreateWindow(config);
    if (!window_) return false;

    BuildShell();
    LayoutForSize(static_cast<float>(config.width), static_cast<float>(config.height));
    window_->onWindowResize = [this](int w, int h) { LayoutForSize(static_cast<float>(w), static_cast<float>(h)); };
    window_->onWindowClosing = [this]() { return OnClosing(); };

    ReloadComponents();
    RefreshOverview();
    LoadSettingsPage();
    RefreshKeysPage();
    ShowPage(kOverview);
    if (!storeError_.empty()) SetStatus(TrF("The admin store could not be opened: {0}", storeError_), true);
    return true;
}

void AdminWindow::Show() {
    if (window_) window_->Show();
}

void AdminWindow::BuildShell() {
    root_ = Theme::Column("uaRoot", 0);
    root_->SetBackgroundColor(Theme::kPageBackground);
    auto body = Theme::Row("uaBody", 0);
    body->layout.SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    body->layoutItem.SetFlexGrow(1).SetFlexShrink(1);

    sidebar_ = Theme::Pin(CreateSidebarNavigation("uaSidebar", 0, 0, 240, 0));
    sidebar_->SetElementSize(CSSLayout::Dimension::Px(240), CSSLayout::Dimension::Auto());
    const int store = sidebar_->AddSection(Tr("Store"));
    sidebar_->AddItem(store, kOverview, Tr("Overview"), Theme::Icon("home.svg"));
    sidebar_->AddItem(store, kPublish, Tr("Publish"), Theme::Icon("folder-download.svg"));
    sidebar_->AddItem(store, kKeys, Tr("Keys"), Theme::Icon("list-check.svg"));
    sidebar_->AddItem(store, kAffiliates, Tr("Affiliates and codes"), Theme::Icon("user-plus.svg"));
    sidebar_->AddItem(store, kMarket, Tr("Marketplace"), Theme::Icon("mail.svg"));
    sidebar_->AddItem(store, kSettings, Tr("Settings"), Theme::Icon("settings.svg"));
    appsSection_ = sidebar_->AddSection(Tr("Apps"));
    componentsSection_ = sidebar_->AddSection(Tr("Runtimes and libraries"));
    sidebar_->onSelectionChanged = [this](const std::string& id) {
        if (id.rfind(kItemPrefix, 0) == 0) {
            const std::string componentId = id.substr(2);
            if (currentPage_ == kComponent && draft_.id == componentId) return;
            EnsureDraftSaved([this, componentId]() { ShowComponent(componentId); });
            return;
        }
        if (currentPage_ == id) return;
        EnsureDraftSaved([this, id]() { ShowPage(id); });
    };

    pageHost_ = Theme::Column("uaPages", 0);
    pageHost_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    // The notices banner: the operator's unacknowledged notices, above every page until acknowledged.
    noticesBanner_ = Theme::Row("uaBanner", Theme::kInnerGap, true);
    noticesBanner_->SetPadding(8, Theme::kPagePadding);
    noticesBanner_->SetBackgroundColor(Theme::kAccentSoft);
    noticesBanner_->SetBorders(0.0f, Theme::kBorder, 0.0f);
    bannerLabel_ = Theme::Caption("uaBanner.text", "");
    bannerLabel_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    noticesBanner_->AddChild(bannerLabel_);
    auto openNotices = Theme::Button("uaBanner.open", Tr("Open"), 70, Theme::ButtonKind::Secondary);
    openNotices->onClick = [this]() { EnsureDraftSaved([this]() { ShowPage(kMarket); }); };
    noticesBanner_->AddChild(openNotices);
    noticesBanner_->SetVisible(false);
    pageHost_->AddChild(noticesBanner_);
    BuildOverviewPage();
    BuildPublishPage();
    BuildAffiliatesPage();
    BuildMarketPage();
    BuildKeysPage();
    BuildSettingsPage();
    BuildComponentPage();

    body->AddChild(sidebar_);
    body->AddChild(pageHost_);

    auto statusRow = Theme::Row("uaStatusRow", 0);
    statusRow->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(Theme::kStatusHeight));
    statusRow->SetPadding(0, Theme::kPagePadding);
    statusRow->SetBackgroundColor(Theme::kSurface);
    statusRow->SetBorders(0.0f, Theme::kBorder, 0.0f);
    statusLabel_ = Theme::Caption("uaStatus", "");
    statusLabel_->SetWrap(TextWrap::WrapNone);
    statusLabel_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    statusRow->AddChild(statusLabel_);

    root_->AddChild(body);
    root_->AddChild(statusRow);
    window_->AddChild(root_);
}

std::shared_ptr<AdminWindow::Container> AdminWindow::BuildPage(const std::string& id, const std::string& title,
                                                               std::shared_ptr<Container>& header, std::shared_ptr<Container>& body) {
    auto page = Theme::Column(id, Theme::kGap);
    page->SetPadding(Theme::kPagePadding);
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    header = Theme::Row(id + ".header", Theme::kGap);
    auto heading = Theme::Heading(id + ".title", title);
    heading->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    header->AddChild(heading);
    body = MakeScrollList(id + ".body");
    page->AddChild(header);
    page->AddChild(body);
    pages_[id] = page;
    pageHost_->AddChild(page);
    page->SetVisible(false);
    return page;
}

void AdminWindow::ShowPage(const std::string& id) {
    currentPage_ = id;
    for (auto& [key, page] : pages_) page->SetVisible(key == id);
    if (id != kComponent && sidebar_->GetSelectedId() != id) sidebar_->Select(id, false);
    if (id == kOverview) RefreshOverview();
    if (id == kKeys) RefreshKeysPage();
    if (id == kPublish) RefreshKeysPage();
    if (id == kAffiliates) RefreshAffiliatesPage();
    if (id == kMarket) RefreshMarketPage();
    pageHost_->InvalidateLayout();
    pageHost_->RequestRedraw();
}

void AdminWindow::LayoutForSize(float width, float height) {
    if (!root_) return;
    root_->SetElementAbsolutePosition(Point2Df(0.0f, 0.0f));
    root_->SetElementSize(Size2Df(std::max(static_cast<float>(kMinWidth), width), std::max(static_cast<float>(kMinHeight), height)));
    root_->InvalidateLayout();
    if (window_) window_->AddDirtyRectangle(Rect2Di(0, 0, static_cast<int>(width), static_cast<int>(height)));
}

void AdminWindow::SetStatus(const std::string& text, bool isError) {
    if (!statusLabel_) return;
    statusLabel_->SetText(text);
    statusLabel_->SetTextColor(isError ? Theme::kDanger : Theme::kTextSecondary);
    statusLabel_->RequestRedraw();
}

void AdminWindow::Alert(const std::string& message, bool isError) {
    if (isError) UltraCanvasAlert::Error(message, "UltraStoreAdmin", nullptr, window_.get());
    else UltraCanvasAlert::Info(message, "UltraStoreAdmin", nullptr, window_.get());
}

bool AdminWindow::OnClosing() {
    if (busy_) {
        SetStatus(Tr("A job is running: close the window once it has finished."), true);
        return false;
    }
    if (draftDirty_) {
        UltraCanvasDialogManager::ShowConfirmation(Tr("The form has unsaved changes. Close and lose them?"), Tr("Unsaved changes"),
            [this](bool confirmed) {
                if (!confirmed) return;
                draftDirty_ = false;
                app_.RequestExit();
                window_->Close();
            }, window_.get());
        return false;
    }
    app_.RequestExit();
    return true;
}

void AdminWindow::ReloadComponents() {
    components_.clear();
    publishers_.clear();
    if (!db_.IsOpen()) { RebuildSidebar(); return; }
    if (const auto all = db_.Components()) components_ = all.Value();
    else SetStatus(all.error.message, true);
    if (const auto all = db_.Publishers()) publishers_ = all.Value();
    RebuildSidebar();
}

void AdminWindow::RebuildSidebar() {
    for (const std::string& id : sidebar_->GetItemIds()) {
        if (id.rfind(kItemPrefix, 0) == 0) sidebar_->RemoveItem(id);
    }
    for (const UltraStore::Component& c : components_) {
        const int section = c.kind == UltraStore::ComponentKind::App ? appsSection_ : componentsSection_;
        UltraCanvas::SidebarItem item;
        item.id = kItemPrefix + c.id;
        item.label = c.name.empty() ? c.id : c.name;
        item.tooltip = c.id;
        if (c.releases.empty()) item.badgeText = Tr("no release");
        sidebar_->AddItem(section, item);
    }
    sidebar_->RequestRedraw();
}

// ===========================================================================
// Overview
// ===========================================================================
void AdminWindow::BuildOverviewPage() {
    std::shared_ptr<Container> header, body;
    BuildPage(kOverview, Tr("Overview"), header, body);
    auto surface = Theme::Surface("uaO.state", Theme::kInnerGap);
    surface->AddChild(Theme::Text("uaO.state.title", Tr("This store"), 14.0f, FontWeight::Bold));
    overviewText_ = Theme::Text("uaO.text", "", 12.0f);
    surface->AddChild(overviewText_);
    body->AddChild(surface);

    auto add = Theme::Surface("uaO.new", Theme::kGap);
    add->AddChild(Theme::Text("uaO.new.title", Tr("Add an app or a component"), 14.0f, FontWeight::Bold));
    newId_ = Theme::Pin(CreateTextInput("uaO.id", 0, 0, 320, Theme::kControlHeight));
    newId_->SetPlaceholder("org.ultraos.myapp");
    add->AddChild(Theme::Field("uaO.idRow", Tr("Id (reverse DNS)"), newId_));
    newName_ = Theme::Pin(CreateTextInput("uaO.name", 0, 0, 320, Theme::kControlHeight));
    newName_->SetPlaceholder(Tr("My App"));
    add->AddChild(Theme::Field("uaO.nameRow", Tr("Name"), newName_));
    newKind_ = Theme::Pin(CreateDropdown("uaO.kind", 0, 0, 200, Theme::kControlHeight));
    newKind_->AddItem(Tr("App"), "app");
    newKind_->AddItem(Tr("Runtime"), "runtime");
    newKind_->AddItem(Tr("Library"), "library");
    newKind_->SetSelectedIndex(0, false);
    add->AddChild(Theme::Field("uaO.kindRow", Tr("Kind"), newKind_));
    auto create = Theme::Button("uaO.create", Tr("Create"), 100, Theme::ButtonKind::Primary);
    create->onClick = [this]() {
        const int k = newKind_->GetSelectedIndex();
        NewComponent(k == 1 ? UltraStore::ComponentKind::Runtime : k == 2 ? UltraStore::ComponentKind::Library : UltraStore::ComponentKind::App);
    };
    auto buttons = Theme::Row("uaO.buttons", Theme::kGap);
    buttons->AddChild(create);
    add->AddChild(buttons);
    add->AddChild(Theme::Caption("uaO.hint", Tr("The command line imports a whole component from JSON: `ultrastoreadmin app import <component.json>`.")));
    body->AddChild(add);
}

void AdminWindow::RefreshOverview() {
    if (!overviewText_) return;
    size_t apps = 0, comps = 0, withoutRelease = 0;
    for (const UltraStore::Component& c : components_) {
        (c.kind == UltraStore::ComponentKind::App ? apps : comps)++;
        if (c.releases.empty()) ++withoutRelease;
    }
    std::string text = TrF("Store folder: {0}", store_.dir);
    text += "\n" + (storeError_.empty() ? TrF("Database: {0}", store_.DatabasePath())
                                        : TrF("Database: {0}  (could not be opened: {1})", store_.DatabasePath(), storeError_));
    text += "\n" + (withoutRelease ? TrF("Apps: {0}, runtimes and libraries: {1}, without a release yet: {2}", apps, comps, withoutRelease)
                                   : TrF("Apps: {0}, runtimes and libraries: {1}", apps, comps));
    text += "\n" + TrF("Publishers: {0}", publishers_.size());
    text += "\n" + (db_.Setting("catalog.published_at").empty()
                        ? TrF("Last published: sequence {0} (never)", db_.Sequence())
                        : TrF("Last published: sequence {0} at {1}", db_.Sequence(), db_.Setting("catalog.published_at")));
    text += "\n" + (db_.Setting("catalog.base_url").empty() ? Tr("Catalog base URL: not set (Settings)")
                                                            : TrF("Catalog base URL: {0}", db_.Setting("catalog.base_url")));
    text += "\n" + TrF("Keys: {0}", keys_.size());
    overviewText_->SetText(text);
}

// ===========================================================================
// Settings
// ===========================================================================
void AdminWindow::BuildSettingsPage() {
    std::shared_ptr<Container> header, body;
    BuildPage(kSettings, Tr("Settings"), header, body);
    auto surface = Theme::Surface("uaS.form", Theme::kGap);
    surface->AddChild(Theme::Text("uaS.title", Tr("The store's settings"), 14.0f, FontWeight::Bold));
    int n = 0;
    for (const auto& row : kSettingKeys) {
        auto input = Theme::Pin(CreateTextInput("uaS.in" + std::to_string(n++), 0, 0, 520, Theme::kControlHeight));
        settingInputs_[row] = input;
        surface->AddChild(Theme::Field("uaS.row" + std::to_string(n), SettingLabel(row), input));
    }
    surface->AddChild(Theme::Caption("uaS.hint",
        Tr("The catalog base URL is where catalog.json is served; the packages base URL where packages/ and restricted/ are, "
        "the catalog's when empty. Languages: codes separated by spaces. The UltraAI provider translates (qwen, llama-cpp, "
        "a configured endpoint; empty follows the routing policy, local first).")));
    auto buttons = Theme::Row("uaS.buttons", Theme::kGap);
    auto save = Theme::Button("uaS.save", Tr("Save"), 100, Theme::ButtonKind::Primary);
    save->onClick = [this]() { SaveSettingsPage(); };
    buttons->AddChild(save);
    auto revert = Theme::Button("uaS.revert", Tr("Revert"), 100);
    revert->onClick = [this]() { LoadSettingsPage(); };
    buttons->AddChild(revert);
    surface->AddChild(buttons);
    body->AddChild(surface);

    auto publishers = Theme::Surface("uaS.publishers", Theme::kGap);
    publishers->AddChild(Theme::Text("uaS.publishers.title", Tr("Publishers"), 14.0f, FontWeight::Bold));
    settingsInfo_ = Theme::Caption("uaS.publishers.list", "");
    publishers->AddChild(settingsInfo_);
    auto pubId = Theme::Pin(CreateTextInput("uaS.pubId", 0, 0, 200, Theme::kControlHeight));
    pubId->SetPlaceholder(Tr("ultra-os-project"));
    auto pubName = Theme::Pin(CreateTextInput("uaS.pubName", 0, 0, 220, Theme::kControlHeight));
    pubName->SetPlaceholder(Tr("ULTRA OS Project"));
    auto pubUrl = Theme::Pin(CreateTextInput("uaS.pubUrl", 0, 0, 260, Theme::kControlHeight));
    pubUrl->SetPlaceholder("https://ultraos.eu");
    auto addPub = Theme::Button("uaS.pubAdd", Tr("Add publisher"), 120);
    addPub->onClick = [this, id = pubId.get(), name = pubName.get(), url = pubUrl.get()]() {
        UltraStore::Publisher p{id->GetText(), name->GetText(), url->GetText()};
        if (p.id.empty()) { SetStatus(Tr("A publisher needs an id"), true); return; }
        if (const auto r = db_.PutPublisher(p); !r) { SetStatus(r.error.message, true); return; }
        id->SetText("");
        name->SetText("");
        url->SetText("");
        ReloadComponents();
        LoadSettingsPage();
        SetStatus(TrF("Publisher {0} saved", p.id));
    };
    auto pubRow = Theme::Row("uaS.pubRow", Theme::kInnerGap, true);
    pubRow->AddChild(pubId);
    pubRow->AddChild(pubName);
    pubRow->AddChild(pubUrl);
    pubRow->AddChild(addPub);
    publishers->AddChild(pubRow);
    body->AddChild(publishers);
}

void AdminWindow::LoadSettingsPage() {
    for (auto& [key, input] : settingInputs_) input->SetText(db_.Setting(key));
    if (uploadUrl_ && uploadUrl_->GetText().empty()) {   // the Publish page's upload fields, kept by the last upload
        uploadUrl_->SetText(db_.Setting("upload.url"));
        uploadUser_->SetText(db_.Setting("upload.user"));
        uploadKeyFile_->SetText(db_.Setting("upload.key_file"));
    }
    std::string list;
    for (const UltraStore::Publisher& p : publishers_) list += (list.empty() ? "" : "\n") + p.id + "  " + p.name + (p.url.empty() ? "" : "  " + p.url);
    if (settingsInfo_) settingsInfo_->SetText(list.empty() ? Tr("No publisher yet: every app names one.") : list);
    if (fPublisher_) {
        fPublisher_->ClearItems();
        fPublisher_->AddItem(Tr("(none)"), "");
        for (const UltraStore::Publisher& p : publishers_) fPublisher_->AddItem(p.name.empty() ? p.id : p.name, p.id);
    }
}

void AdminWindow::SaveSettingsPage() {
    for (auto& [key, input] : settingInputs_) {
        if (const auto r = db_.SetSetting(key, input->GetText()); !r) { SetStatus(r.error.message, true); return; }
    }
    RefreshOverview();
    SetStatus(Tr("Settings saved"));
}

// ===========================================================================
// Keys
// ===========================================================================
void AdminWindow::BuildKeysPage() {
    std::shared_ptr<Container> header, body;
    BuildPage(kKeys, Tr("Signing keys"), header, body);
    keysList_ = Theme::Column("uaK.list", Theme::kGap);
    body->AddChild(keysList_);

    auto add = Theme::Surface("uaK.new", Theme::kGap);
    add->AddChild(Theme::Text("uaK.new.title", Tr("New key"), 14.0f, FontWeight::Bold));
    add->AddChild(Theme::Caption("uaK.new.hint",
        Tr("An Ed25519 pair generated here; the seed is sealed with the passphrase (Argon2id, XChaCha20-Poly1305) and kept in the "
        "database. The passphrase is asked for at every publish and is not recoverable. Export the public half for Apps/UltraStore/keys/.")));
    keyName_ = Theme::Pin(CreateTextInput("uaK.name", 0, 0, 260, Theme::kControlHeight));
    keyName_->SetPlaceholder("production");
    add->AddChild(Theme::Field("uaK.nameRow", Tr("Name"), keyName_));
    keyPass_ = Theme::Pin(CreatePasswordInput("uaK.pass", 0, 0, 260, Theme::kControlHeight));
    add->AddChild(Theme::Field("uaK.passRow", Tr("Passphrase"), keyPass_));
    keyPassAgain_ = Theme::Pin(CreatePasswordInput("uaK.pass2", 0, 0, 260, Theme::kControlHeight));
    add->AddChild(Theme::Field("uaK.pass2Row", Tr("Passphrase again"), keyPassAgain_));
    auto create = Theme::Button("uaK.create", Tr("Create key"), 110, Theme::ButtonKind::Primary);
    create->onClick = [this]() {
        const std::string name = keyName_->GetText(), pass = keyPass_->GetText();
        if (pass.empty()) { SetStatus(Tr("The key needs a passphrase"), true); return; }
        if (pass != keyPassAgain_->GetText()) { SetStatus(Tr("The passphrases differ"), true); return; }
        RunJob(Tr("Creating the key (Argon2id)"), [this, name, pass]() {
            JobOutcome out;
            const auto key = NewSigningKey(pass);
            if (!key) { out.ok = false; out.message = key.error.message; return out; }
            const auto added = db_.AddKey(name, key.Value().publicKeyHex, key.Value().sealed.Bytes());
            if (!added) { out.ok = false; out.message = added.error.message; return out; }
            out.message = TrF("Key {0} \"{1}\" created: {2}", added.Value().id, added.Value().name, added.Value().publicKeyHex);
            return out;
        }, [this](const JobOutcome& out) {
            if (out.ok) { keyName_->SetText(""); keyPass_->SetText(""); keyPassAgain_->SetText(""); }
            RefreshKeysPage();
            RefreshOverview();
        });
    };
    auto buttons = Theme::Row("uaK.buttons", Theme::kGap);
    buttons->AddChild(create);
    add->AddChild(buttons);
    body->AddChild(add);
}

void AdminWindow::RefreshKeysPage() {
    keys_.clear();
    if (db_.IsOpen()) { if (const auto all = db_.Keys()) keys_ = all.Value(); }
    if (keysList_) {
        keysList_->ClearChildren();
        if (keys_.empty()) keysList_->AddChild(Theme::Caption("uaK.empty", Tr("No key yet. The first publish needs one.")));
        for (const KeyRecord& k : keys_) {
            const std::string id = "uaK." + std::to_string(k.id);
            auto surface = Theme::Surface(id, 4.0f);
            surface->AddChild(Theme::Text(id + ".name", std::to_string(k.id) + "  " + (k.Active() ? k.name : TrF("{0}  (retired {1})", k.name, k.retiredAt)), 13.0f, FontWeight::Bold));
            surface->AddChild(Theme::Caption(id + ".pk", TrF("public {0}\ncreated {1}", k.publicKeyHex, k.createdAt)));
            auto row = Theme::Row(id + ".row", Theme::kInnerGap, true);
            auto exportButton = Theme::Button(id + ".export", Tr("Export .pub"), 110);
            const std::string hex = k.publicKeyHex, name = k.name;
            exportButton->onClick = [this, hex, name]() {
                FileDialogOptions opts;
                opts.SetTitle(Tr("Export the public key")).SetDefaultFileName(name + ".pub").AddFilter(Tr("Public key"), "pub").SetParentWindow(window_.get());
                UltraCanvasFileLoader::SaveFileDialog(opts, [this, hex](DialogResult result, const std::string& path) {
                    if (result != DialogResult::OK || path.empty()) return;
                    if (!WriteTextFile(path, hex + "\n")) { SetStatus(TrF("Cannot write {0}", path), true); return; }
                    SetStatus(TrF("Wrote {0}; commit it under Apps/UltraStore/keys/ so the store trusts it", path));
                });
            };
            row->AddChild(exportButton);
            if (k.Active()) {
                auto retire = Theme::Button(id + ".retire", Tr("Retire"), 80, Theme::ButtonKind::Danger);
                const int64_t keyId = k.id;
                retire->onClick = [this, keyId, name]() {
                    UltraCanvasDialogManager::ShowConfirmation(TrF("Retire the key \"{0}\"? It will not sign again; the store stops trusting it with its next release.", name),
                        Tr("Retire key"), [this, keyId](bool confirmed) {
                            if (!confirmed) return;
                            if (const auto r = db_.RetireKey(keyId); !r) { SetStatus(r.error.message, true); return; }
                            RefreshKeysPage();
                        }, window_.get());
                };
                row->AddChild(retire);
            }
            surface->AddChild(row);
            keysList_->AddChild(surface);
        }
        keysList_->InvalidateLayout();
        keysList_->RequestRedraw();
    }
    if (publishKey_) {
        publishKey_->ClearItems();
        for (const KeyRecord& k : keys_) if (k.Active()) publishKey_->AddItem(std::to_string(k.id) + "  " + k.name, std::to_string(k.id));
        if (publishKey_->GetSelectedIndex() < 0 && !keys_.empty()) publishKey_->SetSelectedIndex(0, false);
    }
}

// ===========================================================================
// Publish
// ===========================================================================
void AdminWindow::BuildPublishPage() {
    std::shared_ptr<Container> header, body;
    BuildPage(kPublish, Tr("Publish"), header, body);
    auto surface = Theme::Surface("uaP.form", Theme::kGap);
    surface->AddChild(Theme::Text("uaP.title", Tr("The catalog, the packages and the media into the publish folder"), 14.0f, FontWeight::Bold));
    surface->AddChild(Theme::Caption("uaP.hint",
        Tr("The folder is what the web server serves: catalog.json and its signature, media/, packages/ (free apps) and restricted/ "
        "(paid apps, behind the buyers' signed URLs). The catalog is built with the next sequence, signed with the key, read back "
        "and verified the way the store verifies it, and only then is the sequence recorded.")));
    publishDir_ = Theme::Pin(CreateTextInput("uaP.dir", 0, 0, 480, Theme::kControlHeight));
    publishDir_->SetPlaceholder("/srv/store/v1");
    auto dirRow = Theme::Row("uaP.dirRow", Theme::kInnerGap);
    dirRow->AddChild(publishDir_);
    auto pick = Theme::Button("uaP.pick", Tr("Choose"), 90);
    pick->onClick = [this]() {
        FileDialogOptions opts;
        opts.SetTitle(Tr("The publish folder")).SetParentWindow(window_.get());
        UltraCanvasFileLoader::SelectFolderDialog(opts, [this](DialogResult result, const std::string& path) {
            if (result == DialogResult::OK && !path.empty()) publishDir_->SetText(path);
        });
    };
    dirRow->AddChild(pick);
    surface->AddChild(Theme::Field("uaP.dirField", Tr("Publish folder"), dirRow, true));
    publishKey_ = Theme::Pin(CreateDropdown("uaP.key", 0, 0, 260, Theme::kControlHeight));
    surface->AddChild(Theme::Field("uaP.keyRow", Tr("Key"), publishKey_));
    publishPass_ = Theme::Pin(CreatePasswordInput("uaP.pass", 0, 0, 260, Theme::kControlHeight));
    surface->AddChild(Theme::Field("uaP.passRow", Tr("Passphrase"), publishPass_));
    publishCopy_ = Theme::Pin(UltraCanvasCheckbox::CreateCheckbox("uaP.copy", 0, 0, 420, Theme::kControlHeight, Tr("Copy the packages and the media into the folder"), true));
    surface->AddChild(publishCopy_);
    auto buttons = Theme::Row("uaP.buttons", Theme::kGap);
    auto check = Theme::Button("uaP.check", Tr("Check"), 100);
    check->SetTooltip(Tr("Build the catalog without writing anything and list what publish would warn about"));
    check->onClick = [this]() { CheckCatalog(); };
    buttons->AddChild(check);
    auto publish = Theme::Button("uaP.publish", Tr("Publish"), 100, Theme::ButtonKind::Primary);
    publish->onClick = [this]() { PublishNow(); };
    buttons->AddChild(publish);
    surface->AddChild(buttons);
    body->AddChild(surface);

    // The publish folder onto the host.
    auto upload = Theme::Surface("uaU.form", Theme::kGap);
    upload->AddChild(Theme::Text("uaU.title", Tr("Upload the publish folder to the server"), 14.0f, FontWeight::Bold));
    upload->AddChild(Theme::Caption("uaU.hint",
        Tr("What changed since the last upload to this server goes up - the packages and the media first, the signature, and catalog.json "
        "last, so a store fetching in between sees the old catalog whole or the new one whole. SFTP (sftp://user@host/path, a password "
        "or a key file), FTPS, FTP, or HTTPS with one PUT per file (a WebDAV folder). The password is never kept.")));
    uploadUrl_ = Theme::Pin(CreateTextInput("uaU.url", 0, 0, 480, Theme::kControlHeight));
    uploadUrl_->SetPlaceholder(Tr("sftp://deploy@store.example/srv/store/v1"));
    upload->AddChild(Theme::Field("uaU.urlRow", Tr("Server"), uploadUrl_));
    uploadUser_ = Theme::Pin(CreateTextInput("uaU.user", 0, 0, 260, Theme::kControlHeight));
    uploadUser_->SetPlaceholder(Tr("when the URL names none"));
    upload->AddChild(Theme::Field("uaU.userRow", Tr("User"), uploadUser_));
    uploadPass_ = Theme::Pin(CreatePasswordInput("uaU.pass", 0, 0, 260, Theme::kControlHeight));
    upload->AddChild(Theme::Field("uaU.passRow", Tr("Password"), uploadPass_));
    uploadKeyFile_ = Theme::Pin(CreateTextInput("uaU.keyFile", 0, 0, 480, Theme::kControlHeight));
    uploadKeyFile_->SetPlaceholder(Tr("~/.ssh/id_ed25519 (SFTP with a key; the password is its passphrase then)"));
    auto keyRow = Theme::Row("uaU.keyRow", Theme::kInnerGap);
    keyRow->AddChild(uploadKeyFile_);
    auto pickKey = Theme::Button("uaU.pickKey", Tr("Choose"), 90);
    pickKey->onClick = [this]() {
        FileDialogOptions opts;
        opts.SetTitle(Tr("The SSH private key")).SetParentWindow(window_.get());
        UltraCanvasFileLoader::OpenFileDialog(opts, [this](DialogResult result, const std::string& path) {
            if (result == DialogResult::OK && !path.empty()) uploadKeyFile_->SetText(path);
        });
    };
    keyRow->AddChild(pickKey);
    upload->AddChild(Theme::Field("uaU.keyField", Tr("Key file"), keyRow, true));
    uploadAll_ = Theme::Pin(UltraCanvasCheckbox::CreateCheckbox("uaU.all", 0, 0, 420, Theme::kControlHeight, Tr("Send every file, not only what changed"), false));
    upload->AddChild(uploadAll_);
    auto uploadButtons = Theme::Row("uaU.buttons", Theme::kGap);
    auto send = Theme::Button("uaU.upload", Tr("Upload"), 100, Theme::ButtonKind::Primary);
    send->onClick = [this]() { UploadNow(); };
    uploadButtons->AddChild(send);
    upload->AddChild(uploadButtons);
    body->AddChild(upload);

    auto report = Theme::Surface("uaP.report", Theme::kInnerGap);
    report->AddChild(Theme::Text("uaP.report.title", Tr("Report"), 13.0f, FontWeight::Bold));
    publishReport_ = Theme::Caption("uaP.report.text", Tr("Nothing yet."));
    report->AddChild(publishReport_);
    body->AddChild(report);
}

void AdminWindow::CheckCatalog() {
    PublishOptions options;
    std::vector<std::string> warnings;
    const auto built = BuildCatalog(db_, options, &warnings);
    if (!built) { publishReport_->SetText(TrF("The catalog cannot be built: {0}", built.error.message + (built.error.detail.empty() ? "" : " (" + built.error.detail + ")"))); return; }
    std::string text = TrF("Sequence {0} would list {1} app(s) and {2} component(s).", built.Value().sequence,
                           built.Value().apps.size(), built.Value().components.size());
    if (warnings.empty()) text += "\n" + Tr("No warnings.");
    else text += "\n" + Tr("Warnings:") + "\n" + Joined(warnings);
    publishReport_->SetText(text);
    SetStatus(Tr("Checked"));
}

void AdminWindow::PublishNow() {
    PublishOptions options;
    options.outDirUtf8 = publishDir_->GetText();
    options.mediaRootUtf8 = store_.dir;
    options.copyPackages = publishCopy_->IsChecked();
    const std::string pass = publishPass_->GetText();
    if (options.outDirUtf8.empty()) { SetStatus(Tr("Choose the publish folder"), true); return; }
    if (publishKey_->GetSelectedIndex() < 0) { SetStatus(Tr("Create a key first (Keys)"), true); return; }
    int64_t keyId = 0;
    for (const KeyRecord& k : keys_) {
        int active = 0;
        for (const KeyRecord& o : keys_) { if (!o.Active()) continue; if (o.id == k.id) break; ++active; }
        if (k.Active() && active == publishKey_->GetSelectedIndex()) keyId = k.id;
    }
    if (pass.empty()) { SetStatus(Tr("Enter the key's passphrase"), true); return; }
    RunJob(Tr("Publishing"), [this, keyId, pass, options]() {
        JobOutcome out;
        const auto report = Publish(db_, keyId, pass, options);
        if (!report) { out.ok = false; out.message = report.error.message + (report.error.detail.empty() ? "" : " (" + report.error.detail + ")"); return out; }
        const PublishReport& r = report.Value();
        out.message = TrF("Published sequence {0}: {1} app(s), {2} component(s), {3} package file(s), {4} picture(s), "
                          "{5} code page(s), {6} join page(s), {7} retired page(s)",
                          r.sequence, r.apps, r.components, r.packagesCopied, r.mediaCopied, r.codePages, r.joinPages,
                          r.retiredPages);
        out.lines.push_back(r.catalogPath);
        out.lines.push_back(r.signaturePath);
        out.lines.push_back(TrF("key {0}", r.publicKeyHex));
        for (const std::string& w : r.warnings) out.lines.push_back(TrF("warning: {0}", w));
        return out;
    }, [this](const JobOutcome& out) {
        publishReport_->SetText(out.message + (out.lines.empty() ? "" : "\n" + Joined(out.lines)));
        publishPass_->SetText("");
        RefreshOverview();
    });
}

void AdminWindow::UploadNow() {
    UploadTarget target;
    target.url = uploadUrl_->GetText();
    target.username = uploadUser_->GetText();
    target.privateKeyFileUtf8 = uploadKeyFile_->GetText();
    if (target.privateKeyFileUtf8.empty()) target.password = uploadPass_->GetText();
    else target.keyPassphrase = uploadPass_->GetText();
    if (target.url.empty()) { SetStatus(Tr("Enter the server to upload to"), true); return; }
    UploadOptions options;
    options.publishDirUtf8 = publishDir_->GetText();
    if (options.publishDirUtf8.empty()) { SetStatus(Tr("Choose the publish folder"), true); return; }
    options.stateFileUtf8 = UploadStatePath(store_.dir);
    options.everything = uploadAll_->IsChecked();
    auto transport = CreateUltraNetTransport(target);
    if (!transport) { SetStatus(transport.error.message + " (" + transport.error.detail + ")", true); return; }
    // The server, the user and the key file are kept for next time; the password is not.
    db_.SetSetting("upload.url", target.url);
    db_.SetSetting("upload.user", target.username);
    db_.SetSetting("upload.key_file", target.privateKeyFileUtf8);
    std::shared_ptr<IUploadTransport> shared = std::move(transport.Value());
    std::weak_ptr<int> token = alive_;
    options.onProgress = [this, token](const UploadProgress& p) {
        const std::string text = p.currentFile.empty()
            ? std::string()
            : TrF("Uploading {0}/{1}: {2}", p.filesDone + 1, p.filesTotal, p.currentFile);
        if (text.empty()) return;
        app_.PostToUIThread([this, token, text]() { if (token.lock()) SetStatus(text); });
    };
    const std::string url = target.url;
    RunJob(Tr("Uploading"), [shared, url, options]() {
        JobOutcome out;
        const auto report = UploadPublishFolder(*shared, url, options);
        if (!report) { out.ok = false; out.message = report.error.message + (report.error.detail.empty() ? "" : " (" + report.error.detail + ")"); return out; }
        const UploadReport& r = report.Value();
        out.message = TrF("Uploaded {0} file(s), {1} bytes, to {2} ({3}); {4} unchanged", r.filesSent, r.bytesSent, url,
                          SchemeName(SchemeOf(url)), r.filesSkipped);
        for (const std::string& w : r.warnings) out.lines.push_back(TrF("warning: {0}", w));
        return out;
    }, [this](const JobOutcome& out) {
        publishReport_->SetText(out.message + (out.lines.empty() ? "" : "\n" + Joined(out.lines)));
    });
}

// ===========================================================================
// Affiliates and codes
// ===========================================================================
void AdminWindow::BuildAffiliatesPage() {
    std::shared_ptr<Container> header, body;
    BuildPage(kAffiliates, Tr("Affiliates and codes"), header, body);
    body->AddChild(Theme::Caption("uaA.hint",
        Tr("A partner brings buyers with a code - typed into the store, or carried by a QR code and a link - and earns a share of each sale "
        "made with it. Two tiers and no more: a partner recruited by another names them, and the recruiter earns a recruit share of the "
        "recruit's sales; nobody above the recruiter earns from them. A discount code without a partner takes something off the price. "
        "The catalog carries the codes and what they take off; the shares stay here. Until the payment service reports sales, they are "
        "recorded below by hand.")));

    // ----- affiliates -----
    auto affiliates = Theme::Surface("uaA.list", Theme::kInnerGap);
    affiliates->AddChild(Theme::Text("uaA.list.title", Tr("Partners"), 14.0f, FontWeight::Bold));
    affiliatesList_ = Theme::Column("uaA.items", Theme::kInnerGap);
    affiliates->AddChild(affiliatesList_);
    body->AddChild(affiliates);
    auto addA = Theme::Surface("uaA.add", Theme::kInnerGap);
    addA->AddChild(Theme::Text("uaA.add.title", Tr("Add or change a partner"), 13.0f, FontWeight::Bold));
    aId_ = Theme::Pin(CreateTextInput("uaA.id", 0, 0, 200, Theme::kControlHeight));
    aId_->SetPlaceholder(Tr("aff-anna"));
    addA->AddChild(Theme::Field("uaA.idRow", Tr("Id"), aId_));
    aName_ = Theme::Pin(CreateTextInput("uaA.name", 0, 0, 300, Theme::kControlHeight));
    addA->AddChild(Theme::Field("uaA.nameRow", Tr("Name"), aName_));
    aContact_ = Theme::Pin(CreateTextInput("uaA.contact", 0, 0, 300, Theme::kControlHeight));
    aContact_->SetPlaceholder(Tr("anna@example.org"));
    addA->AddChild(Theme::Field("uaA.contactRow", Tr("Contact"), aContact_));
    aShare_ = Theme::Pin(CreateTextInput("uaA.share", 0, 0, 70, Theme::kControlHeight));
    aShare_->SetPlaceholder("20");
    addA->AddChild(Theme::Field("uaA.shareRow", Tr("Share of a sale (%)"), aShare_));
    aRecruitShare_ = Theme::Pin(CreateTextInput("uaA.recruitShare", 0, 0, 70, Theme::kControlHeight));
    aRecruitShare_->SetPlaceholder("5");
    addA->AddChild(Theme::Field("uaA.recruitShareRow", Tr("Share of a recruit's sale (%)"), aRecruitShare_));
    aRecruitedBy_ = Theme::Pin(CreateDropdown("uaA.recruitedBy", 0, 0, 260, Theme::kControlHeight));
    addA->AddChild(Theme::Field("uaA.recruitedByRow", Tr("Recruited by"), aRecruitedBy_));
    auto saveA = Theme::Button("uaA.save", Tr("Save partner"), 110, Theme::ButtonKind::Primary);
    saveA->onClick = [this]() {
        Affiliate a;
        if (const auto existing = db_.GetAffiliate(aId_->GetText())) a = existing.Value();
        a.id = aId_->GetText();
        a.name = aName_->GetText();
        a.contact = aContact_->GetText();
        a.sharePercent = std::atoi(aShare_->GetText().c_str());                 // locale-ok: digits only
        a.recruitSharePercent = std::atoi(aRecruitShare_->GetText().c_str());   // locale-ok: digits only
        const int by = aRecruitedBy_->GetSelectedIndex();
        a.recruitedBy = by > 0 ? aRecruitedBy_->GetItem(by)->value : std::string();
        if (const auto r = db_.PutAffiliate(a); !r) { SetStatus(r.error.message + (r.error.detail.empty() ? "" : " (" + r.error.detail + ")"), true); return; }
        SetStatus(TrF("Partner {0} saved; join link {1}", a.id, JoinLink(db_.Setting("catalog.base_url"), a.id)));
        RefreshAffiliatesPage();
    };
    auto aButtons = Theme::Row("uaA.buttons", Theme::kGap);
    aButtons->AddChild(saveA);
    addA->AddChild(aButtons);
    body->AddChild(addA);

    // ----- codes -----
    auto codes = Theme::Surface("uaC2.list", Theme::kInnerGap);
    codes->AddChild(Theme::Text("uaC2.list.title", Tr("Codes"), 14.0f, FontWeight::Bold));
    codesList_ = Theme::Column("uaC2.items", Theme::kInnerGap);
    codes->AddChild(codesList_);
    body->AddChild(codes);
    auto addC = Theme::Surface("uaC2.add", Theme::kInnerGap);
    addC->AddChild(Theme::Text("uaC2.add.title", Tr("New code"), 13.0f, FontWeight::Bold));
    cCode_ = Theme::Pin(CreateTextInput("uaC2.code", 0, 0, 200, Theme::kControlHeight));
    cCode_->SetPlaceholder(Tr("ANNA-20"));
    addC->AddChild(Theme::Field("uaC2.codeRow", Tr("Code"), cCode_));
    cAffiliate_ = Theme::Pin(CreateDropdown("uaC2.affiliate", 0, 0, 260, Theme::kControlHeight));
    addC->AddChild(Theme::Field("uaC2.affiliateRow", Tr("Partner (none: a discount code)"), cAffiliate_));
    auto off = Theme::Row("uaC2.off", Theme::kInnerGap);
    cPercent_ = Theme::Pin(CreateTextInput("uaC2.percent", 0, 0, 70, Theme::kControlHeight));
    cPercent_->SetPlaceholder("20");
    off->AddChild(cPercent_);
    off->AddChild(Theme::Caption("uaC2.offOr", "% off, or"));
    cAmount_ = Theme::Pin(CreateTextInput("uaC2.amount", 0, 0, 90, Theme::kControlHeight));
    cAmount_->SetPlaceholder("5.00");
    off->AddChild(cAmount_);
    cCurrency_ = Theme::Pin(CreateTextInput("uaC2.currency", 0, 0, 70, Theme::kControlHeight));
    cCurrency_->SetPlaceholder("EUR");
    off->AddChild(cCurrency_);
    off->AddChild(Theme::Caption("uaC2.offAmount", "off"));
    addC->AddChild(Theme::Field("uaC2.offRow", Tr("Discount"), off));
    cApps_ = Theme::Pin(CreateTextInput("uaC2.apps", 0, 0, 420, Theme::kControlHeight));
    cApps_->SetPlaceholder(Tr("every app; or ids separated by spaces"));
    addC->AddChild(Theme::Field("uaC2.appsRow", Tr("Apps"), cApps_));
    auto dates = Theme::Row("uaC2.dates", Theme::kInnerGap);
    cFrom_ = Theme::Pin(CreateTextInput("uaC2.from", 0, 0, 120, Theme::kControlHeight));
    cFrom_->SetPlaceholder(Tr("from, optional"));
    dates->AddChild(cFrom_);
    cUntil_ = Theme::Pin(CreateTextInput("uaC2.until", 0, 0, 120, Theme::kControlHeight));
    cUntil_->SetPlaceholder(Tr("until, optional"));
    dates->AddChild(cUntil_);
    addC->AddChild(Theme::Field("uaC2.datesRow", Tr("Valid (YYYY-MM-DD)"), dates));
    auto saveC = Theme::Button("uaC2.save", Tr("Create code"), 110, Theme::ButtonKind::Primary);
    saveC->onClick = [this]() {
        CodeRecord r;
        r.code.code = UltraStore::NormalizeCode(cCode_->GetText());
        const int who = cAffiliate_->GetSelectedIndex();
        r.code.affiliate = who > 0 ? cAffiliate_->GetItem(who)->value : std::string();
        r.code.kind = r.code.affiliate.empty() ? UltraStore::CodeKind::Discount : UltraStore::CodeKind::Affiliate;
        r.code.discountPercent = std::atoi(cPercent_->GetText().c_str());   // locale-ok: digits only
        r.code.discountAmount = cAmount_->GetText();
        r.code.currency = cCurrency_->GetText();
        std::istringstream apps(cApps_->GetText());
        for (std::string a; apps >> a;) r.code.apps.push_back(a);
        r.code.validFrom = cFrom_->GetText();
        r.code.validUntil = cUntil_->GetText();
        if (const auto put = db_.PutCode(r); !put) { SetStatus(put.error.message + (put.error.detail.empty() ? "" : " (" + put.error.detail + ")"), true); return; }
        cCode_->SetText("");
        SetStatus(TrF("Code {0} created; it goes into the catalog with the next publish", r.code.code));
        RefreshAffiliatesPage();
    };
    auto cButtons = Theme::Row("uaC2.buttons", Theme::kGap);
    cButtons->AddChild(saveC);
    addC->AddChild(cButtons);
    body->AddChild(addC);

    // ----- sales -----
    auto sales = Theme::Surface("uaS2", Theme::kInnerGap);
    sales->AddChild(Theme::Text("uaS2.title", Tr("Sales and what they credit"), 13.0f, FontWeight::Bold));
    salesText_ = Theme::Caption("uaS2.text", "");
    sales->AddChild(salesText_);
    auto saleRow = Theme::Row("uaS2.add", Theme::kInnerGap, true);
    sCode_ = Theme::Pin(CreateTextInput("uaS2.code", 0, 0, 120, Theme::kControlHeight));
    sCode_->SetPlaceholder(Tr("code, or none"));
    saleRow->AddChild(sCode_);
    sApp_ = Theme::Pin(CreateTextInput("uaS2.app", 0, 0, 220, Theme::kControlHeight));
    sApp_->SetPlaceholder("org.ultraos.app");
    saleRow->AddChild(sApp_);
    sAmount_ = Theme::Pin(CreateTextInput("uaS2.amount", 0, 0, 90, Theme::kControlHeight));
    sAmount_->SetPlaceholder("23.92");
    saleRow->AddChild(sAmount_);
    sCurrency_ = Theme::Pin(CreateTextInput("uaS2.currency", 0, 0, 70, Theme::kControlHeight));
    sCurrency_->SetPlaceholder("EUR");
    saleRow->AddChild(sCurrency_);
    auto sync = Theme::Button("uaS2.sync", Tr("Sync sales"), 100, Theme::ButtonKind::Primary);
    sync->SetTooltip(Tr("Fetch the paid orders from the payment service (Settings: payment.url and the admin token) into the ledger"));
    sync->onClick = [this]() {
        const std::string url = db_.Setting("payment.url"), token = db_.Setting("payment.token");
        RunJob(Tr("Syncing the sales"), [this, url, token]() {
            JobOutcome out;
            const auto report = SyncSales(db_, url, token);
            if (!report) { out.ok = false; out.message = report.error.message + (report.error.detail.empty() ? "" : " (" + report.error.detail + ")"); return out; }
            out.message = TrF("Synced: {0} sale(s) recorded, {1} already there", report.Value().recorded, report.Value().skipped);
            return out;
        }, [this](const JobOutcome&) { RefreshAffiliatesPage(); });
    };
    saleRow->AddChild(sync);
    auto record = Theme::Button("uaS2.record", Tr("Record sale"), 110);
    record->onClick = [this]() {
        const auto sale = RecordSale(db_, sCode_->GetText(), sApp_->GetText(), sAmount_->GetText(), sCurrency_->GetText());
        if (!sale) { SetStatus(sale.error.message + (sale.error.detail.empty() ? "" : " (" + sale.error.detail + ")"), true); return; }
        const SaleRecord& s = sale.Value();
        std::string text = TrF("Sale recorded: {0} {1}", s.amount, s.currency);
        if (!s.affiliate.empty()) text += "; " + TrF("{0} earns {1}", s.affiliate, s.affiliateCredit);
        if (!s.recruiter.empty()) text += "; " + TrF("{0} earns {1} for recruiting them", s.recruiter, s.recruiterCredit);
        SetStatus(text);
        RefreshAffiliatesPage();
    };
    saleRow->AddChild(record);
    sales->AddChild(saleRow);
    body->AddChild(sales);
}

void AdminWindow::RefreshAffiliatesPage() {
    if (!affiliatesList_ || !db_.IsOpen()) return;
    const std::string base = db_.Setting("catalog.base_url");
    const auto report = AffiliateReport(db_);
    affiliatesList_->ClearChildren();
    aRecruitedBy_->ClearItems();
    aRecruitedBy_->AddItem(Tr("(nobody)"), "");
    cAffiliate_->ClearItems();
    cAffiliate_->AddItem(Tr("(none: a discount code)"), "");
    if (report && report.Value().empty()) affiliatesList_->AddChild(Theme::Caption("uaA.none", Tr("No partner yet.")));
    if (report) {
        int n = 0;
        for (const AffiliateTotals& t : report.Value()) {
            const Affiliate& a = t.affiliate;
            aRecruitedBy_->AddItem(a.name + " (" + a.id + ")", a.id);
            cAffiliate_->AddItem(a.name + " (" + a.id + ")", a.id);
            const std::string id = "uaA.item" + std::to_string(n++);
            auto surface = Theme::Column(id, 2.0f);
            surface->SetBackgroundColor(Theme::kPageBackground);
            surface->SetBorders(1.0f, Theme::kBorder, 6.0f);
            surface->SetPadding(8.0f);
            auto head = Theme::Row(id + ".head", Theme::kInnerGap);
            auto title = Theme::Text(id + ".title", a.name + "  " + a.id + (a.enabled ? std::string() : "  " + Tr("(off)")), 13.0f, FontWeight::Bold);
            title->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            head->AddChild(title);
            auto edit = Theme::Button(id + ".edit", Tr("Edit"), 60);
            edit->onClick = [this, a]() {
                aId_->SetText(a.id); aName_->SetText(a.name); aContact_->SetText(a.contact);
                aShare_->SetText(std::to_string(a.sharePercent)); aRecruitShare_->SetText(std::to_string(a.recruitSharePercent));
                for (int i = 0; i < aRecruitedBy_->GetItemCount(); ++i) if (aRecruitedBy_->GetItem(i)->value == a.recruitedBy) aRecruitedBy_->SetSelectedIndex(i, false);
            };
            head->AddChild(edit);
            auto qr = Theme::Button(id + ".qr", Tr("Join QR"), 90);
            qr->SetTooltip(TrF("The QR code a future partner scans to join under this one: {0}", JoinLink(base, a.id)));
            const std::string affId = a.id;
            qr->onClick = [this, affId, base]() {
                if (base.empty()) { SetStatus(Tr("Set the catalog base URL first (Settings): the link needs it"), true); return; }
                FileDialogOptions opts;
                opts.SetTitle(Tr("Save the join QR code")).SetDefaultFileName("join-" + affId + ".svg").AddFilter("SVG", "svg").SetParentWindow(window_.get());
                UltraCanvasFileLoader::SaveFileDialog(opts, [this, affId, base](DialogResult result, const std::string& path) {
                    if (result != DialogResult::OK || path.empty()) return;
                    if (const auto r = WriteQrCodeSvg(JoinLink(base, affId), path); !r) { SetStatus(r.error.message, true); return; }
                    SetStatus(TrF("Wrote {0}: {1}", path, JoinLink(base, affId)));
                });
            };
            head->AddChild(qr);
            auto toggle = Theme::Button(id + ".toggle", a.enabled ? Tr("Switch off") : Tr("Switch on"), 90, a.enabled ? Theme::ButtonKind::Danger : Theme::ButtonKind::Secondary);
            toggle->onClick = [this, a]() {
                Affiliate changed = a;
                changed.enabled = !a.enabled;
                if (const auto r = db_.PutAffiliate(changed); !r) { SetStatus(r.error.message, true); return; }
                RefreshAffiliatesPage();
            };
            head->AddChild(toggle);
            surface->AddChild(head);
            std::string line = TrF("{0}% of a sale with their codes, {1}% of a recruit's sale", a.sharePercent, a.recruitSharePercent);
            if (!a.recruitedBy.empty()) line += " · " + TrF("recruited by {0}", a.recruitedBy);
            if (!a.contact.empty()) line += " · " + a.contact;
            surface->AddChild(Theme::Caption(id + ".line", line));
            std::string own, recruits;
            for (const auto& [cur, sum] : t.ownCredit) own += (own.empty() ? "" : ", ") + sum + " " + cur;
            for (const auto& [cur, sum] : t.recruitCredit) recruits += (recruits.empty() ? "" : ", ") + sum + " " + cur;
            surface->AddChild(Theme::Caption(id + ".totals", TrF("{0} sale(s) with their codes: {1} · {2} recruit sale(s): {3}",
                                                                 t.ownSales, own.empty() ? std::string("0.00") : own,
                                                                 t.recruitSales, recruits.empty() ? std::string("0.00") : recruits)));
            affiliatesList_->AddChild(surface);
        }
    }
    aRecruitedBy_->SetSelectedIndex(0, false);
    cAffiliate_->SetSelectedIndex(0, false);

    codesList_->ClearChildren();
    const auto codes = db_.Codes();
    if (codes && codes.Value().empty()) codesList_->AddChild(Theme::Caption("uaC2.none", Tr("No code yet.")));
    if (codes) {
        int n = 0;
        for (const CodeRecord& c : codes.Value()) {
            const std::string id = "uaC2.item" + std::to_string(n++);
            auto row = Theme::Row(id, Theme::kInnerGap, true);
            std::string what = UltraStore::DiscountLabel(c.code);
            std::string text = c.code.code + "  " + (what.empty() ? Tr("partner code") : what) + "  " +
                               (c.code.affiliate.empty() ? Tr("discount") : TrF("for {0}", c.code.affiliate)) + "  " +
                               (c.code.apps.empty() ? Tr("every app") : TrF("on {0} app(s)", c.code.apps.size())) +
                               (c.code.validUntil.empty() ? std::string() : "  " + TrF("until {0}", c.code.validUntil)) +
                               (c.enabled ? std::string() : "  " + Tr("(off)"));
            auto label = Theme::Text(id + ".text", text, 12.0f);
            label->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            row->AddChild(label);
            const std::string code = c.code.code;
            auto qr = Theme::Button(id + ".qr", "QR", 60);
            qr->SetTooltip(CodeLink(base, code) + "  -  " + StoreLink(code));
            qr->onClick = [this, code, base]() {
                if (base.empty()) { SetStatus(Tr("Set the catalog base URL first (Settings): the link needs it"), true); return; }
                FileDialogOptions opts;
                opts.SetTitle(Tr("Save the code's QR code")).SetDefaultFileName("code-" + code + ".svg").AddFilter("SVG", "svg").SetParentWindow(window_.get());
                UltraCanvasFileLoader::SaveFileDialog(opts, [this, code, base](DialogResult result, const std::string& path) {
                    if (result != DialogResult::OK || path.empty()) return;
                    if (const auto r = WriteQrCodeSvg(CodeLink(base, code), path); !r) { SetStatus(r.error.message, true); return; }
                    SetStatus(TrF("Wrote {0}: {1}", path, CodeLink(base, code)));
                });
            };
            row->AddChild(qr);
            auto toggle = Theme::Button(id + ".toggle", c.enabled ? Tr("Switch off") : Tr("Switch on"), 90);
            toggle->onClick = [this, c]() {
                CodeRecord changed = c;
                changed.enabled = !c.enabled;
                if (const auto r = db_.PutCode(changed); !r) { SetStatus(r.error.message, true); return; }
                RefreshAffiliatesPage();
            };
            row->AddChild(toggle);
            auto remove = Theme::Button(id + ".remove", Tr("Remove"), 72, Theme::ButtonKind::Danger);
            remove->onClick = [this, code]() {
                if (const auto r = db_.RemoveCode(code); !r) { SetStatus(r.error.message, true); return; }
                RefreshAffiliatesPage();
            };
            row->AddChild(remove);
            codesList_->AddChild(row);
        }
    }
    const auto sales = db_.Sales();
    if (sales) {
        std::string text = TrF("{0} sale(s) recorded.", sales.Value().size());
        size_t shown = 0;
        for (auto it = sales.Value().rbegin(); it != sales.Value().rend() && shown < 5; ++it, ++shown) {
            text += "\n" + it->at + "  " + (it->code.empty() ? "-" : it->code) + "  " + it->app + "  " + it->amount + " " + it->currency;
            if (!it->affiliate.empty()) text += "  → " + it->affiliate + " " + it->affiliateCredit;
            if (!it->recruiter.empty()) text += ", " + it->recruiter + " " + it->recruiterCredit;
        }
        salesText_->SetText(text);
    }
    affiliatesList_->InvalidateLayout();
    codesList_->InvalidateLayout();
    pageHost_->InvalidateLayout();
    pageHost_->RequestRedraw();
}

// ===========================================================================
// Marketplace
// ===========================================================================
void AdminWindow::BuildMarketPage() {
    std::shared_ptr<Container> header, body;
    BuildPage(kMarket, Tr("Marketplace"), header, body);
    body->AddChild(Theme::Caption("uaM.hint",
        Tr("The operator's marketplace service (Settings: market.url, market.merchant_id): where this backend stands with the terms, the notices "
        "the operator sent, a release submitted for review - the component as it is here, signed with this backend's key - and the reports about "
        "this merchant. Every request is signed with the key, so the passphrase is asked for once per visit.")));

    auto standing = Theme::Surface("uaM.standing", Theme::kInnerGap);
    marketStandingLabel_ = Theme::Text("uaM.standing.text", Tr("Not asked yet."), 14.0f, FontWeight::Bold);
    standing->AddChild(marketStandingLabel_);
    auto keyRow = Theme::Row("uaM.keyRow", Theme::kInnerGap, true);
    marketPassphrase_ = Theme::Pin(CreatePasswordInput("uaM.pass", 0, 0, 260, Theme::kControlHeight));
    marketPassphrase_->SetPlaceholder(Tr("the key's passphrase"));
    keyRow->AddChild(Theme::Field("uaM.passField", Tr("Passphrase"), marketPassphrase_));
    auto refresh = Theme::Button("uaM.refresh", Tr("Ask the service"), 130, Theme::ButtonKind::Primary);
    refresh->onClick = [this]() { RefreshMarketPage(); };
    keyRow->AddChild(refresh);
    standing->AddChild(keyRow);
    body->AddChild(standing);

    auto notices = Theme::Surface("uaM.notices", Theme::kInnerGap);
    notices->AddChild(Theme::Text("uaM.notices.title", Tr("Notices"), 14.0f, FontWeight::Bold));
    noticesList_ = Theme::Column("uaM.notices.list", Theme::kInnerGap);
    notices->AddChild(noticesList_);
    body->AddChild(notices);

    auto terms = Theme::Surface("uaM.terms", Theme::kInnerGap);
    terms->AddChild(Theme::Text("uaM.terms.title", Tr("Terms"), 14.0f, FontWeight::Bold));
    marketTermsLabel_ = Theme::Caption("uaM.terms.state", "");
    terms->AddChild(marketTermsLabel_);
    marketTermsText_ = Theme::Caption("uaM.terms.text", "");
    terms->AddChild(marketTermsText_);
    auto acceptRow = Theme::Row("uaM.acceptRow", Theme::kInnerGap, true);
    marketAcceptedBy_ = Theme::Pin(CreateTextInput("uaM.by", 0, 0, 220, Theme::kControlHeight));
    marketAcceptedBy_->SetPlaceholder(Tr("who accepts, by name"));
    acceptRow->AddChild(marketAcceptedBy_);
    auto accept = Theme::Button("uaM.accept", Tr("Accept the terms"), 140);
    accept->onClick = [this]() {
        if (!haveTerms_) { SetStatus(Tr("No terms are in force"), true); return; }
        const std::string by = marketAcceptedBy_->GetText();
        if (by.empty()) { SetStatus(Tr("Say who accepts the terms"), true); return; }
        const auto seed = MarketSeed();
        if (!seed) { SetStatus(seed.error.message, true); return; }
        MarketClient client(MarketSettings::From(db_), seed.Value());
        const int version = terms_.version;
        RunJob(Tr("Accepting the terms"), [client, version, by]() {
            JobOutcome out;
            const auto s = client.AcceptTerms(version, by);
            if (!s) { out.ok = false; out.message = s.error.message; return out; }
            out.message = TrF("Terms version {0} accepted; standing {1}", version, s.Value().standing);
            return out;
        }, [this](const JobOutcome& out) { if (out.ok) RefreshMarketPage(); });
    };
    acceptRow->AddChild(accept);
    terms->AddChild(acceptRow);
    body->AddChild(terms);

    auto submit = Theme::Surface("uaM.submit", Theme::kInnerGap);
    submit->AddChild(Theme::Text("uaM.submit.title", Tr("Submit a release"), 14.0f, FontWeight::Bold));
    submit->AddChild(Theme::Caption("uaM.submit.hint", Tr("The app as it is saved here goes to the review, signed; the packages are named by their hashes.")));
    submitApp_ = Theme::Pin(CreateDropdown("uaM.app", 0, 0, 320, Theme::kControlHeight));
    submit->AddChild(Theme::Field("uaM.appRow", Tr("App"), submitApp_));
    submitAnswers_ = Theme::Pin(CreateTextInput("uaM.answers", 0, 0, 200, Theme::kControlHeight));
    submitAnswers_->SetPlaceholder(Tr("s-00000012 (a resubmission)"));
    submit->AddChild(Theme::Field("uaM.answersRow", Tr("Answers"), submitAnswers_));
    auto send = Theme::Button("uaM.send", Tr("Submit"), 100, Theme::ButtonKind::Primary);
    send->onClick = [this]() {
        const int at = submitApp_->GetSelectedIndex();
        if (at < 0) { SetStatus(Tr("Pick the app to submit"), true); return; }
        const std::string appId = submitApp_->GetItem(at)->value;
        const auto component = db_.GetComponent(appId);
        if (!component) { SetStatus(component.error.message, true); return; }
        const auto seed = MarketSeed();
        if (!seed) { SetStatus(seed.error.message, true); return; }
        MarketClient client(MarketSettings::From(db_), seed.Value());
        const std::string answers = submitAnswers_->GetText();
        RunJob(TrF("Submitting {0}", appId), [client, c = component.Value(), answers]() {
            JobOutcome out;
            const auto sent = client.Submit(c, answers);
            if (!sent) { out.ok = false; out.message = sent.error.message; return out; }
            out.message = TrF("Submitted {0} {1} as {2}: {3}", sent.Value().app, sent.Value().version, sent.Value().id, sent.Value().state);
            for (const auto& [name, outcome] : sent.Value().checks) out.lines.push_back(name + ": " + outcome);
            return out;
        }, [this](const JobOutcome& out) { if (out.ok) { submitAnswers_->SetText(""); RefreshMarketPage(); } });
    };
    submit->AddChild(send);
    body->AddChild(submit);

    auto submissions = Theme::Surface("uaM.submissions", Theme::kInnerGap);
    submissions->AddChild(Theme::Text("uaM.submissions.title", Tr("Submissions"), 14.0f, FontWeight::Bold));
    submissionsList_ = Theme::Column("uaM.submissions.list", Theme::kInnerGap);
    submissions->AddChild(submissionsList_);
    body->AddChild(submissions);

    auto reports = Theme::Surface("uaM.reports", Theme::kInnerGap);
    reports->AddChild(Theme::Text("uaM.reports.title", Tr("Reports about this merchant"), 14.0f, FontWeight::Bold));
    reportsList_ = Theme::Column("uaM.reports.list", Theme::kInnerGap);
    reports->AddChild(reportsList_);
    body->AddChild(reports);
}

Result<std::vector<uint8_t>> AdminWindow::MarketSeed() {
    const MarketSettings settings = MarketSettings::From(db_);
    if (!settings.Complete()) return Result<std::vector<uint8_t>>::Failure(ErrorCode::InvalidArgument, Tr("Settings: market.url and market.merchant_id name the marketplace service"));
    if (!marketPassphrase_ || marketPassphrase_->GetText().empty()) return Result<std::vector<uint8_t>>::Failure(ErrorCode::InvalidArgument, Tr("The key's passphrase signs the request"));
    return OpenKeySeed(db_, 0, marketPassphrase_->GetText());
}

void AdminWindow::RefreshMarketPage() {
    if (submitApp_) {
        submitApp_->ClearItems();
        for (const UltraStore::Component& c : components_) if (c.kind == UltraStore::ComponentKind::App) submitApp_->AddItem(c.name.empty() ? c.id : c.name + " (" + c.id + ")", c.id);
    }
    const auto seed = MarketSeed();
    if (!seed) { marketError_ = seed.error.message; ShowMarketPage(); return; }
    MarketClient client(MarketSettings::From(db_), seed.Value());
    RunJob(Tr("Asking the marketplace service"), [client]() {
        JobOutcome out;
        out.message = Tr("Marketplace: up to date");
        return out;
    }, nullptr);
    // The job above only keeps the status line honest; the fetch itself:
    std::weak_ptr<int> token = alive_;
    std::thread([this, token, client]() {
        std::string standingText, error;
        std::vector<Notice> notices;
        bool haveTerms = false;
        Terms terms;
        std::vector<std::string> subs, reps;
        const auto standing = client.GetStanding();
        if (!standing) error = standing.error.message;
        else {
            const Standing& s = standing.Value();
            standingText = TrF("{0} ({1}): {2}, {3} strike(s), terms in force {4}, accepted {5}", s.name, s.id, s.standing,
                               s.strikes, s.termsInForce, s.termsAccepted);
        }
        if (const auto n = client.Notices()) notices = n.Value();
        if (const auto t = client.CurrentTerms()) { haveTerms = true; terms = t.Value(); }
        if (const auto list = client.Submissions()) {
            for (const UltraCanvas::JSONValue& sub : list.Value().Get("submissions").GetElements()) {
                std::string line = sub.Get("id").GetString() + "  " + sub.Get("app").GetString() + " " + sub.Get("version").GetString() + "  " + sub.Get("state").GetString();
                const UltraCanvas::JSONValue& decisions = sub.Get("decisions");
                if (decisions.GetSize() > 0) {
                    const UltraCanvas::JSONValue& last = decisions.At(decisions.GetSize() - 1);
                    line += "  (" + last.Get("action").GetString() + (last.Get("message").GetString().empty() ? "" : ": " + last.Get("message").GetString()) + ")";
                }
                subs.push_back(line);
            }
        }
        if (const auto list = client.Reports()) {
            for (const UltraCanvas::JSONValue& r : list.Value().Get("reports").GetElements()) {
                reps.push_back(r.Get("id").GetString() + "  " + TrF("{0} about {1}", r.Get("kind").GetString(), r.Get("target").GetString()) + ", " + r.Get("state").GetString() +
                               (r.Get("outcome").GetString().empty() ? "" : ": " + r.Get("outcome").GetString()) + "\n" + r.Get("text").GetString());
            }
        }
        app_.PostToUIThread([this, token, standingText, error, notices, haveTerms, terms, subs, reps]() {
            if (!token.lock()) return;
            marketStanding_ = standingText;
            marketError_ = error;
            notices_ = notices;
            haveTerms_ = haveTerms;
            terms_ = terms;
            submissionLines_ = subs;
            reportLines_ = reps;
            ShowMarketPage();
        });
    }).detach();
}

void AdminWindow::ShowMarketPage() {
    if (!marketStandingLabel_) return;
    marketStandingLabel_->SetText(!marketError_.empty() ? marketError_ : marketStanding_.empty() ? Tr("Not asked yet.") : marketStanding_);
    marketStandingLabel_->SetTextColor(marketError_.empty() ? Theme::kTextPrimary : Theme::kDanger);
    noticesList_->ClearChildren();
    if (notices_.empty()) noticesList_->AddChild(Theme::Caption("uaM.n.none", Tr("No notices.")));
    int n = 0;
    for (const Notice& notice : notices_) {
        const std::string id = "uaM.n" + std::to_string(n++);
        auto row = Theme::Row(id, Theme::kInnerGap, true);
        auto text = Theme::Caption(id + ".t", notice.kind + ": " + notice.subject + (notice.clause.empty() ? std::string() : " " + TrF("(clause {0})", notice.clause)) +
                                   (notice.deadline.empty() ? std::string() : ", " + TrF("answer by {0}", notice.deadline)) +
                                   (notice.Acknowledged() ? " - " + Tr("acknowledged") : std::string()) +
                                   (notice.text.empty() ? "" : "\n" + notice.text));
        text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        row->AddChild(text);
        if (!notice.Acknowledged()) {
            auto ack = Theme::Button(id + ".ack", Tr("Acknowledge"), 110);
            ack->onClick = [this, noticeId = notice.id]() {
                const auto seed = MarketSeed();
                if (!seed) { SetStatus(seed.error.message, true); return; }
                MarketClient client(MarketSettings::From(db_), seed.Value());
                RunJob(TrF("Acknowledging {0}", noticeId), [client, noticeId]() {
                    JobOutcome out;
                    const auto at = client.Acknowledge(noticeId);
                    if (!at) { out.ok = false; out.message = at.error.message; return out; }
                    out.message = Tr("Acknowledged ") + noticeId;
                    return out;
                }, [this](const JobOutcome& out) { if (out.ok) RefreshMarketPage(); });
            };
            row->AddChild(ack);
        }
        noticesList_->AddChild(row);
    }
    if (haveTerms_) {
        marketTermsLabel_->SetText(TrF("Version {0}, in force from {1}", terms_.version, terms_.inForceFrom) + (terms_.changes.empty() ? "" : " (" + terms_.changes + ")"));
        marketTermsText_->SetText(terms_.text);
    } else {
        marketTermsLabel_->SetText(Tr("No terms are in force."));
        marketTermsText_->SetText("");
    }
    submissionsList_->ClearChildren();
    if (submissionLines_.empty()) submissionsList_->AddChild(Theme::Caption("uaM.s.none", Tr("Nothing submitted yet.")));
    n = 0;
    for (const std::string& line : submissionLines_) submissionsList_->AddChild(Theme::Caption("uaM.s" + std::to_string(n++), line));
    reportsList_->ClearChildren();
    if (reportLines_.empty()) reportsList_->AddChild(Theme::Caption("uaM.r.none", Tr("No reports.")));
    n = 0;
    for (const std::string& line : reportLines_) reportsList_->AddChild(Theme::Caption("uaM.r" + std::to_string(n++), line));
    UpdateNoticesBanner();
    pageHost_->InvalidateLayout();
    pageHost_->RequestRedraw();
}

void AdminWindow::UpdateNoticesBanner() {
    if (!noticesBanner_) return;
    size_t open = 0;
    std::string first;
    for (const Notice& notice : notices_) if (!notice.Acknowledged()) { if (first.empty()) first = notice.kind + ": " + notice.subject; ++open; }
    noticesBanner_->SetVisible(open > 0);
    if (open > 0) bannerLabel_->SetText(TrN("{n} notice from the operator - ", "{n} notices from the operator - ", static_cast<long long>(open)) + first);
    pageHost_->InvalidateLayout();
    pageHost_->RequestRedraw();
}

// ===========================================================================
// Jobs
// ===========================================================================
void AdminWindow::RunJob(const std::string& title, std::function<JobOutcome()> work, std::function<void(const JobOutcome&)> done) {
    if (busy_) { SetStatus(Tr("A job is already running"), true); return; }
    if (worker_.joinable()) worker_.join();
    busy_ = true;
    SetStatus(title + "…");
    std::weak_ptr<int> token = alive_;
    worker_ = std::thread([this, token, work = std::move(work), done = std::move(done)]() {
        JobOutcome out = work();
        app_.PostToUIThread([this, token, out, done]() {
            if (!token.lock()) return;
            busy_ = false;
            SetStatus(out.message, !out.ok);
            if (!out.ok) Alert(out.message);
            if (done) done(out);
        });
    });
}

} // namespace UltraStoreAdmin::Ui
