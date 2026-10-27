// Apps/UltraStoreAdmin/ui/AdminWindow.cpp
// The shell, the store's pages (Overview, Settings, Keys, Publish) and the
// worker thread; the component form is in AdminComponentPage.cpp.
// Version: 0.2.0 - the upload to the host on the Publish page
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "AdminWindow.h"

#include "AdminTheme.h"
#include "CatalogPublisher.h"
#include "KeyVault.h"
#include "Uploader.h"
#include "Translation.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
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
constexpr const char* kSettings = "settings";
constexpr const char* kComponent = "component";
constexpr const char* kItemPrefix = "c:";

const char* kSettingKeys[][2] = {
    {"catalog.base_url", "Catalog base URL"},
    {"packages.base_url", "Packages base URL"},
    {"texts.languages", "Languages to translate into"},
    {"ai.provider", "UltraAI provider"},
    {"ai.base_url", "UltraAI server"},
    {"ai.model", "UltraAI model"},
};

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
    config.title = "UltraStoreAdmin " ULTRASTOREADMIN_VERSION;
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
    if (!storeError_.empty()) SetStatus("The admin store could not be opened: " + storeError_, true);
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
    const int store = sidebar_->AddSection("Store");
    sidebar_->AddItem(store, kOverview, "Overview", Theme::Icon("home.svg"));
    sidebar_->AddItem(store, kPublish, "Publish", Theme::Icon("folder-download.svg"));
    sidebar_->AddItem(store, kKeys, "Keys", Theme::Icon("list-check.svg"));
    sidebar_->AddItem(store, kSettings, "Settings", Theme::Icon("settings.svg"));
    appsSection_ = sidebar_->AddSection("Apps");
    componentsSection_ = sidebar_->AddSection("Runtimes and libraries");
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
    BuildOverviewPage();
    BuildPublishPage();
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
        SetStatus("A job is running: close the window once it has finished.", true);
        return false;
    }
    if (draftDirty_) {
        UltraCanvasDialogManager::ShowConfirmation("The form has unsaved changes. Close and lose them?", "Unsaved changes",
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
        if (c.releases.empty()) item.badgeText = "no release";
        sidebar_->AddItem(section, item);
    }
    sidebar_->RequestRedraw();
}

// ===========================================================================
// Overview
// ===========================================================================
void AdminWindow::BuildOverviewPage() {
    std::shared_ptr<Container> header, body;
    BuildPage(kOverview, "Overview", header, body);
    auto surface = Theme::Surface("uaO.state", Theme::kInnerGap);
    surface->AddChild(Theme::Text("uaO.state.title", "This store", 14.0f, FontWeight::Bold));
    overviewText_ = Theme::Text("uaO.text", "", 12.0f);
    surface->AddChild(overviewText_);
    body->AddChild(surface);

    auto add = Theme::Surface("uaO.new", Theme::kGap);
    add->AddChild(Theme::Text("uaO.new.title", "Add an app or a component", 14.0f, FontWeight::Bold));
    newId_ = Theme::Pin(CreateTextInput("uaO.id", 0, 0, 320, Theme::kControlHeight));
    newId_->SetPlaceholder("org.ultraos.myapp");
    add->AddChild(Theme::Field("uaO.idRow", "Id (reverse DNS)", newId_));
    newName_ = Theme::Pin(CreateTextInput("uaO.name", 0, 0, 320, Theme::kControlHeight));
    newName_->SetPlaceholder("My App");
    add->AddChild(Theme::Field("uaO.nameRow", "Name", newName_));
    newKind_ = Theme::Pin(CreateDropdown("uaO.kind", 0, 0, 200, Theme::kControlHeight));
    newKind_->AddItem("App", "app");
    newKind_->AddItem("Runtime", "runtime");
    newKind_->AddItem("Library", "library");
    newKind_->SetSelectedIndex(0, false);
    add->AddChild(Theme::Field("uaO.kindRow", "Kind", newKind_));
    auto create = Theme::Button("uaO.create", "Create", 100, Theme::ButtonKind::Primary);
    create->onClick = [this]() {
        const int k = newKind_->GetSelectedIndex();
        NewComponent(k == 1 ? UltraStore::ComponentKind::Runtime : k == 2 ? UltraStore::ComponentKind::Library : UltraStore::ComponentKind::App);
    };
    auto buttons = Theme::Row("uaO.buttons", Theme::kGap);
    buttons->AddChild(create);
    add->AddChild(buttons);
    add->AddChild(Theme::Caption("uaO.hint", "The command line imports a whole component from JSON: `ultrastoreadmin app import <component.json>`."));
    body->AddChild(add);
}

void AdminWindow::RefreshOverview() {
    if (!overviewText_) return;
    size_t apps = 0, comps = 0, withoutRelease = 0;
    for (const UltraStore::Component& c : components_) {
        (c.kind == UltraStore::ComponentKind::App ? apps : comps)++;
        if (c.releases.empty()) ++withoutRelease;
    }
    std::string text = "Store folder: " + store_.dir;
    text += "\nDatabase: " + store_.DatabasePath() + (storeError_.empty() ? "" : "  (could not be opened: " + storeError_ + ")");
    text += "\nApps: " + std::to_string(apps) + ", runtimes and libraries: " + std::to_string(comps) +
            (withoutRelease ? ", without a release yet: " + std::to_string(withoutRelease) : "");
    text += "\nPublishers: " + std::to_string(publishers_.size());
    text += "\nLast published: sequence " + std::to_string(db_.Sequence()) +
            (db_.Setting("catalog.published_at").empty() ? " (never)" : " at " + db_.Setting("catalog.published_at"));
    text += "\nCatalog base URL: " + (db_.Setting("catalog.base_url").empty() ? std::string("not set (Settings)") : db_.Setting("catalog.base_url"));
    text += "\nKeys: " + std::to_string(keys_.size());
    overviewText_->SetText(text);
}

// ===========================================================================
// Settings
// ===========================================================================
void AdminWindow::BuildSettingsPage() {
    std::shared_ptr<Container> header, body;
    BuildPage(kSettings, "Settings", header, body);
    auto surface = Theme::Surface("uaS.form", Theme::kGap);
    surface->AddChild(Theme::Text("uaS.title", "The store's settings", 14.0f, FontWeight::Bold));
    int n = 0;
    for (const auto& row : kSettingKeys) {
        auto input = Theme::Pin(CreateTextInput("uaS.in" + std::to_string(n++), 0, 0, 520, Theme::kControlHeight));
        settingInputs_[row[0]] = input;
        surface->AddChild(Theme::Field("uaS.row" + std::to_string(n), row[1], input));
    }
    surface->AddChild(Theme::Caption("uaS.hint",
        "The catalog base URL is where catalog.json is served; the packages base URL where packages/ and restricted/ are, "
        "the catalog's when empty. Languages: codes separated by spaces. The UltraAI provider translates (qwen, llama-cpp, "
        "a configured endpoint; empty follows the routing policy, local first)."));
    auto buttons = Theme::Row("uaS.buttons", Theme::kGap);
    auto save = Theme::Button("uaS.save", "Save", 100, Theme::ButtonKind::Primary);
    save->onClick = [this]() { SaveSettingsPage(); };
    buttons->AddChild(save);
    auto revert = Theme::Button("uaS.revert", "Revert", 100);
    revert->onClick = [this]() { LoadSettingsPage(); };
    buttons->AddChild(revert);
    surface->AddChild(buttons);
    body->AddChild(surface);

    auto publishers = Theme::Surface("uaS.publishers", Theme::kGap);
    publishers->AddChild(Theme::Text("uaS.publishers.title", "Publishers", 14.0f, FontWeight::Bold));
    settingsInfo_ = Theme::Caption("uaS.publishers.list", "");
    publishers->AddChild(settingsInfo_);
    auto pubId = Theme::Pin(CreateTextInput("uaS.pubId", 0, 0, 200, Theme::kControlHeight));
    pubId->SetPlaceholder("ultra-os-project");
    auto pubName = Theme::Pin(CreateTextInput("uaS.pubName", 0, 0, 220, Theme::kControlHeight));
    pubName->SetPlaceholder("ULTRA OS Project");
    auto pubUrl = Theme::Pin(CreateTextInput("uaS.pubUrl", 0, 0, 260, Theme::kControlHeight));
    pubUrl->SetPlaceholder("https://ultraos.eu");
    auto addPub = Theme::Button("uaS.pubAdd", "Add publisher", 120);
    addPub->onClick = [this, id = pubId.get(), name = pubName.get(), url = pubUrl.get()]() {
        UltraStore::Publisher p{id->GetText(), name->GetText(), url->GetText()};
        if (p.id.empty()) { SetStatus("A publisher needs an id", true); return; }
        if (const auto r = db_.PutPublisher(p); !r) { SetStatus(r.error.message, true); return; }
        id->SetText("");
        name->SetText("");
        url->SetText("");
        ReloadComponents();
        LoadSettingsPage();
        SetStatus("Publisher " + p.id + " saved");
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
    if (settingsInfo_) settingsInfo_->SetText(list.empty() ? "No publisher yet: every app names one." : list);
    if (fPublisher_) {
        fPublisher_->ClearItems();
        fPublisher_->AddItem("(none)", "");
        for (const UltraStore::Publisher& p : publishers_) fPublisher_->AddItem(p.name.empty() ? p.id : p.name, p.id);
    }
}

void AdminWindow::SaveSettingsPage() {
    for (auto& [key, input] : settingInputs_) {
        if (const auto r = db_.SetSetting(key, input->GetText()); !r) { SetStatus(r.error.message, true); return; }
    }
    RefreshOverview();
    SetStatus("Settings saved");
}

// ===========================================================================
// Keys
// ===========================================================================
void AdminWindow::BuildKeysPage() {
    std::shared_ptr<Container> header, body;
    BuildPage(kKeys, "Signing keys", header, body);
    keysList_ = Theme::Column("uaK.list", Theme::kGap);
    body->AddChild(keysList_);

    auto add = Theme::Surface("uaK.new", Theme::kGap);
    add->AddChild(Theme::Text("uaK.new.title", "New key", 14.0f, FontWeight::Bold));
    add->AddChild(Theme::Caption("uaK.new.hint",
        "An Ed25519 pair generated here; the seed is sealed with the passphrase (Argon2id, XChaCha20-Poly1305) and kept in the "
        "database. The passphrase is asked for at every publish and is not recoverable. Export the public half for Apps/UltraStore/keys/."));
    keyName_ = Theme::Pin(CreateTextInput("uaK.name", 0, 0, 260, Theme::kControlHeight));
    keyName_->SetPlaceholder("production");
    add->AddChild(Theme::Field("uaK.nameRow", "Name", keyName_));
    keyPass_ = Theme::Pin(CreatePasswordInput("uaK.pass", 0, 0, 260, Theme::kControlHeight));
    add->AddChild(Theme::Field("uaK.passRow", "Passphrase", keyPass_));
    keyPassAgain_ = Theme::Pin(CreatePasswordInput("uaK.pass2", 0, 0, 260, Theme::kControlHeight));
    add->AddChild(Theme::Field("uaK.pass2Row", "Passphrase again", keyPassAgain_));
    auto create = Theme::Button("uaK.create", "Create key", 110, Theme::ButtonKind::Primary);
    create->onClick = [this]() {
        const std::string name = keyName_->GetText(), pass = keyPass_->GetText();
        if (pass.empty()) { SetStatus("The key needs a passphrase", true); return; }
        if (pass != keyPassAgain_->GetText()) { SetStatus("The passphrases differ", true); return; }
        RunJob("Creating the key (Argon2id)", [this, name, pass]() {
            JobOutcome out;
            const auto key = NewSigningKey(pass);
            if (!key) { out.ok = false; out.message = key.error.message; return out; }
            const auto added = db_.AddKey(name, key.Value().publicKeyHex, key.Value().sealed.Bytes());
            if (!added) { out.ok = false; out.message = added.error.message; return out; }
            out.message = "Key " + std::to_string(added.Value().id) + " \"" + added.Value().name + "\" created: " + added.Value().publicKeyHex;
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
        if (keys_.empty()) keysList_->AddChild(Theme::Caption("uaK.empty", "No key yet. The first publish needs one."));
        for (const KeyRecord& k : keys_) {
            const std::string id = "uaK." + std::to_string(k.id);
            auto surface = Theme::Surface(id, 4.0f);
            surface->AddChild(Theme::Text(id + ".name", std::to_string(k.id) + "  " + k.name + (k.Active() ? "" : "  (retired " + k.retiredAt + ")"), 13.0f, FontWeight::Bold));
            surface->AddChild(Theme::Caption(id + ".pk", "public " + k.publicKeyHex + "\ncreated " + k.createdAt));
            auto row = Theme::Row(id + ".row", Theme::kInnerGap, true);
            auto exportButton = Theme::Button(id + ".export", "Export .pub…", 110);
            const std::string hex = k.publicKeyHex, name = k.name;
            exportButton->onClick = [this, hex, name]() {
                FileDialogOptions opts;
                opts.SetTitle("Export the public key").SetDefaultFileName(name + ".pub").AddFilter("Public key", "pub").SetParentWindow(window_.get());
                UltraCanvasFileLoader::SaveFileDialog(opts, [this, hex](DialogResult result, const std::string& path) {
                    if (result != DialogResult::OK || path.empty()) return;
                    if (!WriteTextFile(path, hex + "\n")) { SetStatus("Cannot write " + path, true); return; }
                    SetStatus("Wrote " + path + "; commit it under Apps/UltraStore/keys/ so the store trusts it");
                });
            };
            row->AddChild(exportButton);
            if (k.Active()) {
                auto retire = Theme::Button(id + ".retire", "Retire", 80, Theme::ButtonKind::Danger);
                const int64_t keyId = k.id;
                retire->onClick = [this, keyId, name]() {
                    UltraCanvasDialogManager::ShowConfirmation("Retire the key \"" + name + "\"? It will not sign again; the store stops trusting it with its next release.",
                        "Retire key", [this, keyId](bool confirmed) {
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
    BuildPage(kPublish, "Publish", header, body);
    auto surface = Theme::Surface("uaP.form", Theme::kGap);
    surface->AddChild(Theme::Text("uaP.title", "The catalog, the packages and the media into the publish folder", 14.0f, FontWeight::Bold));
    surface->AddChild(Theme::Caption("uaP.hint",
        "The folder is what the web server serves: catalog.json and its signature, media/, packages/ (free apps) and restricted/ "
        "(paid apps, behind the buyers' signed URLs). The catalog is built with the next sequence, signed with the key, read back "
        "and verified the way the store verifies it, and only then is the sequence recorded."));
    publishDir_ = Theme::Pin(CreateTextInput("uaP.dir", 0, 0, 480, Theme::kControlHeight));
    publishDir_->SetPlaceholder("/srv/store/v1");
    auto dirRow = Theme::Row("uaP.dirRow", Theme::kInnerGap);
    dirRow->AddChild(publishDir_);
    auto pick = Theme::Button("uaP.pick", "Choose…", 90);
    pick->onClick = [this]() {
        FileDialogOptions opts;
        opts.SetTitle("The publish folder").SetParentWindow(window_.get());
        UltraCanvasFileLoader::SelectFolderDialog(opts, [this](DialogResult result, const std::string& path) {
            if (result == DialogResult::OK && !path.empty()) publishDir_->SetText(path);
        });
    };
    dirRow->AddChild(pick);
    surface->AddChild(Theme::Field("uaP.dirField", "Publish folder", dirRow, true));
    publishKey_ = Theme::Pin(CreateDropdown("uaP.key", 0, 0, 260, Theme::kControlHeight));
    surface->AddChild(Theme::Field("uaP.keyRow", "Key", publishKey_));
    publishPass_ = Theme::Pin(CreatePasswordInput("uaP.pass", 0, 0, 260, Theme::kControlHeight));
    surface->AddChild(Theme::Field("uaP.passRow", "Passphrase", publishPass_));
    publishCopy_ = Theme::Pin(UltraCanvasCheckbox::CreateCheckbox("uaP.copy", 0, 0, 420, Theme::kControlHeight, "Copy the packages and the media into the folder", true));
    surface->AddChild(publishCopy_);
    auto buttons = Theme::Row("uaP.buttons", Theme::kGap);
    auto check = Theme::Button("uaP.check", "Check", 100);
    check->SetTooltip("Build the catalog without writing anything and list what publish would warn about");
    check->onClick = [this]() { CheckCatalog(); };
    buttons->AddChild(check);
    auto publish = Theme::Button("uaP.publish", "Publish", 100, Theme::ButtonKind::Primary);
    publish->onClick = [this]() { PublishNow(); };
    buttons->AddChild(publish);
    surface->AddChild(buttons);
    body->AddChild(surface);

    // The publish folder onto the host.
    auto upload = Theme::Surface("uaU.form", Theme::kGap);
    upload->AddChild(Theme::Text("uaU.title", "Upload the publish folder to the server", 14.0f, FontWeight::Bold));
    upload->AddChild(Theme::Caption("uaU.hint",
        "What changed since the last upload to this server goes up - the packages and the media first, the signature, and catalog.json "
        "last, so a store fetching in between sees the old catalog whole or the new one whole. SFTP (sftp://user@host/path, a password "
        "or a key file), FTPS, FTP, or HTTPS with one PUT per file (a WebDAV folder). The password is never kept."));
    uploadUrl_ = Theme::Pin(CreateTextInput("uaU.url", 0, 0, 480, Theme::kControlHeight));
    uploadUrl_->SetPlaceholder("sftp://deploy@store.example/srv/store/v1");
    upload->AddChild(Theme::Field("uaU.urlRow", "Server", uploadUrl_));
    uploadUser_ = Theme::Pin(CreateTextInput("uaU.user", 0, 0, 260, Theme::kControlHeight));
    uploadUser_->SetPlaceholder("when the URL names none");
    upload->AddChild(Theme::Field("uaU.userRow", "User", uploadUser_));
    uploadPass_ = Theme::Pin(CreatePasswordInput("uaU.pass", 0, 0, 260, Theme::kControlHeight));
    upload->AddChild(Theme::Field("uaU.passRow", "Password", uploadPass_));
    uploadKeyFile_ = Theme::Pin(CreateTextInput("uaU.keyFile", 0, 0, 480, Theme::kControlHeight));
    uploadKeyFile_->SetPlaceholder("~/.ssh/id_ed25519 (SFTP with a key; the password is its passphrase then)");
    auto keyRow = Theme::Row("uaU.keyRow", Theme::kInnerGap);
    keyRow->AddChild(uploadKeyFile_);
    auto pickKey = Theme::Button("uaU.pickKey", "Choose…", 90);
    pickKey->onClick = [this]() {
        FileDialogOptions opts;
        opts.SetTitle("The SSH private key").SetParentWindow(window_.get());
        UltraCanvasFileLoader::OpenFileDialog(opts, [this](DialogResult result, const std::string& path) {
            if (result == DialogResult::OK && !path.empty()) uploadKeyFile_->SetText(path);
        });
    };
    keyRow->AddChild(pickKey);
    upload->AddChild(Theme::Field("uaU.keyField", "Key file", keyRow, true));
    uploadAll_ = Theme::Pin(UltraCanvasCheckbox::CreateCheckbox("uaU.all", 0, 0, 420, Theme::kControlHeight, "Send every file, not only what changed", false));
    upload->AddChild(uploadAll_);
    auto uploadButtons = Theme::Row("uaU.buttons", Theme::kGap);
    auto send = Theme::Button("uaU.upload", "Upload", 100, Theme::ButtonKind::Primary);
    send->onClick = [this]() { UploadNow(); };
    uploadButtons->AddChild(send);
    upload->AddChild(uploadButtons);
    body->AddChild(upload);

    auto report = Theme::Surface("uaP.report", Theme::kInnerGap);
    report->AddChild(Theme::Text("uaP.report.title", "Report", 13.0f, FontWeight::Bold));
    publishReport_ = Theme::Caption("uaP.report.text", "Nothing yet.");
    report->AddChild(publishReport_);
    body->AddChild(report);
}

void AdminWindow::CheckCatalog() {
    PublishOptions options;
    std::vector<std::string> warnings;
    const auto built = BuildCatalog(db_, options, &warnings);
    if (!built) { publishReport_->SetText("The catalog cannot be built: " + built.error.message + (built.error.detail.empty() ? "" : " (" + built.error.detail + ")")); return; }
    std::string text = "Sequence " + std::to_string(built.Value().sequence) + " would list " + std::to_string(built.Value().apps.size()) +
                       " app(s) and " + std::to_string(built.Value().components.size()) + " component(s).";
    if (warnings.empty()) text += "\nNo warnings.";
    else text += "\nWarnings:\n" + Joined(warnings);
    publishReport_->SetText(text);
    SetStatus("Checked");
}

void AdminWindow::PublishNow() {
    PublishOptions options;
    options.outDirUtf8 = publishDir_->GetText();
    options.mediaRootUtf8 = store_.dir;
    options.copyPackages = publishCopy_->IsChecked();
    const std::string pass = publishPass_->GetText();
    if (options.outDirUtf8.empty()) { SetStatus("Choose the publish folder", true); return; }
    if (publishKey_->GetSelectedIndex() < 0) { SetStatus("Create a key first (Keys)", true); return; }
    int64_t keyId = 0;
    for (const KeyRecord& k : keys_) {
        int active = 0;
        for (const KeyRecord& o : keys_) { if (!o.Active()) continue; if (o.id == k.id) break; ++active; }
        if (k.Active() && active == publishKey_->GetSelectedIndex()) keyId = k.id;
    }
    if (pass.empty()) { SetStatus("Enter the key's passphrase", true); return; }
    RunJob("Publishing", [this, keyId, pass, options]() {
        JobOutcome out;
        const auto report = Publish(db_, keyId, pass, options);
        if (!report) { out.ok = false; out.message = report.error.message + (report.error.detail.empty() ? "" : " (" + report.error.detail + ")"); return out; }
        const PublishReport& r = report.Value();
        out.message = "Published sequence " + std::to_string(r.sequence) + ": " + std::to_string(r.apps) + " app(s), " +
                      std::to_string(r.components) + " component(s), " + std::to_string(r.packagesCopied) + " package file(s), " +
                      std::to_string(r.mediaCopied) + " picture(s)";
        out.lines.push_back(r.catalogPath);
        out.lines.push_back(r.signaturePath);
        out.lines.push_back("key " + r.publicKeyHex);
        for (const std::string& w : r.warnings) out.lines.push_back("warning: " + w);
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
    if (target.url.empty()) { SetStatus("Enter the server to upload to", true); return; }
    UploadOptions options;
    options.publishDirUtf8 = publishDir_->GetText();
    if (options.publishDirUtf8.empty()) { SetStatus("Choose the publish folder", true); return; }
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
            : "Uploading " + std::to_string(p.filesDone + 1) + "/" + std::to_string(p.filesTotal) + ": " + p.currentFile;
        if (text.empty()) return;
        app_.PostToUIThread([this, token, text]() { if (token.lock()) SetStatus(text); });
    };
    const std::string url = target.url;
    RunJob("Uploading", [shared, url, options]() {
        JobOutcome out;
        const auto report = UploadPublishFolder(*shared, url, options);
        if (!report) { out.ok = false; out.message = report.error.message + (report.error.detail.empty() ? "" : " (" + report.error.detail + ")"); return out; }
        const UploadReport& r = report.Value();
        out.message = "Uploaded " + std::to_string(r.filesSent) + " file(s), " + std::to_string(r.bytesSent) + " bytes, to " + url +
                      " (" + SchemeName(SchemeOf(url)) + "); " + std::to_string(r.filesSkipped) + " unchanged";
        for (const std::string& w : r.warnings) out.lines.push_back("warning: " + w);
        return out;
    }, [this](const JobOutcome& out) {
        publishReport_->SetText(out.message + (out.lines.empty() ? "" : "\n" + Joined(out.lines)));
    });
}

// ===========================================================================
// Jobs
// ===========================================================================
void AdminWindow::RunJob(const std::string& title, std::function<JobOutcome()> work, std::function<void(const JobOutcome&)> done) {
    if (busy_) { SetStatus("A job is already running", true); return; }
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
