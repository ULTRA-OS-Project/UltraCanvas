// Apps/UltraStore/ui/StoreSettingsPage.cpp
// The Settings page: the channel, the texts' language, the servers, the keys
// the user trusts beside the compiled-in ones, and what the store knows
// about this machine, its folder and the catalog it has.
// Version: 0.3.1 - the last labels through Tr: the title, Catalog, Channel, Stable,
//                  Servers, Save, Revert, Proxy settings
// Version: 0.3.0 - Proxy settings: the shared dialog, for the proxy the catalog,
//                  the downloads and the purchases go through
// Version: 0.2.0 - the announced keys in the catalog state
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StoreWindow.h"

#include "StoreTheme.h"

#include "UltraLocalisation/UltraLocalisation.h"

#include "UltraCanvasAppSettings.h"
#include "UltraCanvasProxySettingsDialog.h"

#include <algorithm>
#include <cctype>

#ifndef ULTRASTORE_VERSION
#error "ULTRASTORE_VERSION must be passed by CMake (from Docs/UltraStore/CHANGELOG.md)"
#endif

using namespace UltraCanvas;

namespace UltraStore::Ui {

namespace {

std::string Joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& l : lines) out += l + "\n";
    return out;
}

std::shared_ptr<UltraCanvasContainer> FieldRow(const std::string& id, const std::string& label,
                                               const std::shared_ptr<UltraCanvasUIElement>& control) {
    auto row = Theme::Row(id, Theme::kGap);
    auto text = Theme::Text(id + ".label", label, 12.0f);
    text->SetElementSize(CSSLayout::Dimension::Px(180), CSSLayout::Dimension::Auto());
    row->AddChild(text);
    row->AddChild(Theme::Pin(control));
    return row;
}

std::shared_ptr<UltraCanvasTextArea> ListArea(const std::string& id, float height) {
    auto area = Theme::Pin(std::make_shared<UltraCanvasTextArea>(id, 0.0f, height));
    area->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(height));
    area->SetWordWrap(false);
    area->SetShowLineNumbers(false);
    area->SetHighlightSyntax(false);
    area->SetFontSize(12.0f);
    area->GetStyle().borderColor = Theme::kBorder;
    area->GetStyle().backgroundColor = Theme::kPageBackground;
    area->GetStyle().textPadding = 6.0f;
    // The frame is the element's own: the style's colours paint the text and
    // its gutter, not the box.
    area->SetBackgroundColor(Theme::kPageBackground);
    area->SetBorders(1.0f, Theme::kBorder, 4.0f);
    return area;
}

} // namespace

std::shared_ptr<UltraCanvasContainer> StoreWindow::BuildSettingsPage() {
    auto page = Theme::Column("settings", Theme::kGap);
    page->SetPadding(Theme::kPagePadding);
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    auto header = Theme::Row("usS.header", Theme::kGap);
    header->layoutItem.SetFlexShrink(0);
    header->AddChild(Theme::Heading("usS.title", Tr("Settings")));
    page->AddChild(header);

    auto scroll = CreateScrollableContainer("usS.scroll", 0, 0, 0, 0);
    scroll->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    ContainerStyle scrollStyle = scroll->GetContainerStyle();
    scrollStyle.forceShowVerticalScrollbar = false;
    scrollStyle.autoShowScrollbars = true;
    scrollStyle.autoShowHorizontalScrollbar = false;
    scroll->SetContainerStyle(scrollStyle);
    scroll->layout.SetFlexColumn().SetFlexGap(Theme::kGap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    scroll->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    scroll->SetPadding(2, 16, 2, 2);

    // ----- the catalog -----
    auto catalog = Theme::Surface("usS.catalog", Theme::kGap);
    catalog->AddChild(Theme::Text("usS.catalog.title", Tr("Catalog"), 14.0f, FontWeight::Bold));

    channelDropdown_ = Theme::Pin(CreateDropdown("usS.channel", 0, 0, 160, Theme::kControlHeight));
    channelDropdown_->AddItem(Tr("Stable"), "stable");
    channelDropdown_->AddItem(Tr("Beta (and stable)"), "beta");
    catalog->AddChild(FieldRow("usS.channelRow", Tr("Channel"), channelDropdown_));
    catalog->AddChild(Theme::Caption("usS.channelHint",
        Tr("Beta shows pre-releases beside the stable ones; an app installed from beta keeps following beta.")));

    languageInput_ = Theme::Pin(CreateTextInput("usS.language", 0, 0, 80, Theme::kControlHeight));
    languageInput_->SetPlaceholder(SystemLanguage());
    catalog->AddChild(FieldRow("usS.languageRow", Tr("Language of the texts"), languageInput_));
    catalog->AddChild(Theme::Caption("usS.languageHint",
        TrF("Two letters (de, fr, th); empty follows the system ({0}). English is the fallback for a text that has no translation.", SystemLanguage())));

    catalog->AddChild(Theme::Text("usS.serversLabel", Tr("Servers"), 12.0f));
    serversArea_ = ListArea("usS.servers", 72.0f);
    serversArea_->SetPlaceholder("https://store.ultraos.eu/v1");
    catalog->AddChild(serversArea_);
    catalog->AddChild(Theme::Caption("usS.serversHint",
        Tr("One base URL per line, tried in order; the store reads <base>/catalog.json and its signature. Empty uses the servers compiled into this build.")));

    catalog->AddChild(Theme::Text("usS.keysLabel", Tr("Additional trusted keys"), 12.0f));
    keysArea_ = ListArea("usS.keys", 72.0f);
    keysArea_->SetPlaceholder(Tr("64 hex digits per line"));
    catalog->AddChild(keysArea_);
    builtinInfo_ = Theme::Caption("usS.builtin", "");
    catalog->AddChild(builtinInfo_);

    refreshOnStartBox_ = Theme::Pin(UltraCanvasCheckbox::CreateCheckbox("usS.refreshOnStart", 0, 0, 300, Theme::kControlHeight,
                                                                        Tr("Fetch the catalog when the store starts"), true));
    catalog->AddChild(refreshOnStartBox_);

    auto buttons = Theme::Row("usS.buttons", Theme::kGap, true);
    auto save = Theme::Button("usS.save", Tr("Save"), 90, Theme::ButtonKind::Primary);
    save->onClick = [this]() { SaveSettingsFromPage(); };
    buttons->AddChild(save);
    auto revert = Theme::Button("usS.revert", Tr("Revert"), 90);
    revert->onClick = [this]() { LoadSettingsIntoPage(); SetStatus(Tr("Settings read again")); };
    buttons->AddChild(revert);
    auto refresh = Theme::Button("usS.refresh", Tr("Refresh catalog now"), 150);
    refresh->onClick = [this]() { RefreshCatalog(); };
    buttons->AddChild(refresh);
    // The proxy every ULTRA OS application connects through
    auto proxy = Theme::Button("usS.proxy", Tr("Proxy settings"), 130);
    proxy->onClick = [this]() { ShowProxySettingsDialog(window_.get()); };
    buttons->AddChild(proxy);
    catalog->AddChild(buttons);
    scroll->AddChild(catalog);

    // ----- this machine and the store -----
    auto info = Theme::Surface("usS.info", Theme::kGap);
    info->AddChild(Theme::Text("usS.info.title", Tr("This machine and the store"), 14.0f, FontWeight::Bold));
    settingsInfo_ = Theme::Text("usS.info.text", "", 12.0f);
    info->AddChild(settingsInfo_);
    auto maintenance = Theme::Row("usS.maintenance", Theme::kGap, true);
    auto collect = Theme::Button("usS.collect", Tr("Clean up unused components"), 200);
    collect->onClick = [this]() { Collect(); };
    maintenance->AddChild(collect);
    auto reload = Theme::Button("usS.reload", Tr("Reload installed apps"), 160);
    reload->onClick = [this]() { ReloadInstalled(); };
    maintenance->AddChild(reload);
    auto forgetCode = Theme::Button("usS.forgetCode", Tr("Forget the code"), 130);
    forgetCode->SetTooltip(Tr("The discount or partner code entered on Discover stops counting"));
    forgetCode->onClick = [this]() { ApplyCode(""); };
    maintenance->AddChild(forgetCode);
    info->AddChild(maintenance);
    scroll->AddChild(info);

    // ----- about -----
    auto about = Theme::Surface("usS.about", Theme::kInnerGap);
    about->AddChild(Theme::Text("usS.about.title", TrF("UltraStore {0}", ULTRASTORE_VERSION), 14.0f, FontWeight::Bold));
    about->AddChild(Theme::Caption("usS.about.text",
        Tr("The ULTRA OS app store: a signed catalog, per-user installs with the shared runtimes the apps need, updates and removal. The same program answers `ultrastore --help` on the command line. Design and decisions: Docs/UltraStore/README.md.")));
    scroll->AddChild(about);

    page->AddChild(scroll);
    return page;
}

void StoreWindow::LoadSettingsIntoPage() {
    if (!channelDropdown_) return;
    channelDropdown_->SetSelectedIndex(settings_.channel == "beta" ? 1 : 0, false);
    languageInput_->SetText(settings_.language);
    serversArea_->SetText(Joined(settings_.servers), false);
    keysArea_->SetText(Joined(settings_.extraKeys), false);
    refreshOnStartBox_->SetChecked(settings_.refreshOnStart);
    const std::vector<std::string> builtin = BuiltinKeysHex();
    std::string text = builtin.empty()
        ? Tr("No key is compiled into this build; the catalog's signature is checked against the keys above only.")
        : TrN("{n} key is compiled into this build (", "{n} keys are compiled into this build (", static_cast<long long>(builtin.size()));
    for (size_t i = 0; i < builtin.size(); ++i) text += (i ? ", " : "") + builtin[i].substr(0, 12) + "…";
    if (!builtin.empty()) text += "). " + Tr("One 64-digit hex key per line above adds to them.");
    else text += " " + Tr("One 64-digit hex key per line.");
    builtinInfo_->SetText(text);
    RefreshSettingsInfo();
}

void StoreWindow::SaveSettingsFromPage() {
    StoreSettings s = settings_;
    s.channel = channelDropdown_->GetSelectedIndex() == 1 ? "beta" : "stable";
    std::string language = languageInput_->GetText();
    language.erase(std::remove_if(language.begin(), language.end(), [](unsigned char c) { return std::isspace(c); }), language.end());
    for (char& c : language) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!language.empty() && (language.size() < 2 || language.size() > 3 ||
                              !std::all_of(language.begin(), language.end(), [](unsigned char c) { return std::isalpha(c); }))) {
        Alert(Tr("The language is two or three letters, such as de or en, or empty for the system's."));
        return;
    }
    s.language = language;
    s.servers = Lines(serversArea_->GetText());
    for (const std::string& url : s.servers) {
        if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) {
            Alert(TrF("A server is a URL starting with https://, not \"{0}\".", url));
            return;
        }
    }
    s.extraKeys = Lines(keysArea_->GetText());
    for (const std::string& key : s.extraKeys) {
        if (!IsPublicKeyHex(key)) {
            Alert(TrF("A trusted key is 64 hex digits (`ultrastore pubkey <seed>` prints one), not \"{0}\".", key));
            return;
        }
    }
    s.refreshOnStart = refreshOnStartBox_->IsChecked();
    settings_ = s;
    if (!settings_.Save()) {
        Alert(TrF("The settings could not be written to {0}", UltraCanvasAppSettings("UltraStore").File()));
    }
    lang_ = settings_.Language();
    if (options_.channel.empty()) channel_ = settings_.channel;
    ApplySettings();
    LoadSettingsIntoPage();
    RebuildAllPages();
    SetStatus(Tr("Settings saved"));
}

void StoreWindow::RefreshSettingsInfo() {
    if (!settingsInfo_) return;
    std::string text = TrF("Machine: {0}", platform_.Describe());
    text += "\n" + TrF("Channel in use: {0}", channel_) + (options_.channel.empty() ? std::string() : " " + Tr("(from the command line)"));
    text += "\n" + TrF("Texts in: {0}", lang_);
    text += "\n" + TrF("Store folder: {0}", engine_ ? engine_->Root() : std::string("-"));
    if (!storeError_.empty()) text += "\n  " + TrF("could not be opened: {0}", storeError_);
    text += "\n" + TrF("Settings file: {0}", UltraCanvasAppSettings("UltraStore").File());
    text += "\n" + TrF("Catalog: {0}", catalogNote_.empty() ? Tr("none") : catalogNote_);
    if (catalog_) {
        text += "\n  " + TrF("{0} apps, {1} components, schema {2}", catalog_->apps.size(), catalog_->components.size(), catalog_->schema);
        if (!catalog_->trustedKeys.empty()) {
            text += "\n  " + TrN("announces {n} key that may sign the next catalogs", "announces {n} keys that may sign the next catalogs", static_cast<long long>(catalog_->trustedKeys.size()));
        }
    }
    if (!settings_.code.empty()) {
        const std::string inForce = settings_.CodeInForce(Today());
        text += "\n" + TrF("Code: {0}, entered {1}", settings_.code, settings_.codeEnteredAt) + " " +
                (inForce.empty() ? Tr("(no longer in force)") : CodeInForce() ? Tr("(in force)") : Tr("(the catalog does not know it)"));
    }
    const std::vector<InstalledRecord> apps = CurrentApps(installed_);
    const std::vector<InstalledRecord> comps = InstalledComponents(installed_);
    text += "\n" + TrF("Installed: {0}, {1}", TrN("{n} app", "{n} apps", static_cast<long long>(apps.size())), TrN("{n} component", "{n} components", static_cast<long long>(comps.size())));
    settingsInfo_->SetText(text);
}

} // namespace UltraStore::Ui
