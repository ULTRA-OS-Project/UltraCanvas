// Apps/UltraStore/ui/StoreSettingsPage.cpp
// The Settings page: the channel, the texts' language, the servers, the keys
// the user trusts beside the compiled-in ones, and what the store knows
// about this machine, its folder and the catalog it has.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StoreWindow.h"

#include "StoreTheme.h"

#include "UltraCanvasAppSettings.h"

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
    header->AddChild(Theme::Heading("usS.title", "Settings"));
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
    catalog->AddChild(Theme::Text("usS.catalog.title", "Catalog", 14.0f, FontWeight::Bold));

    channelDropdown_ = Theme::Pin(CreateDropdown("usS.channel", 0, 0, 160, Theme::kControlHeight));
    channelDropdown_->AddItem("Stable", "stable");
    channelDropdown_->AddItem("Beta (and stable)", "beta");
    catalog->AddChild(FieldRow("usS.channelRow", "Channel", channelDropdown_));
    catalog->AddChild(Theme::Caption("usS.channelHint",
        "Beta shows pre-releases beside the stable ones; an app installed from beta keeps following beta."));

    languageInput_ = Theme::Pin(CreateTextInput("usS.language", 0, 0, 80, Theme::kControlHeight));
    languageInput_->SetPlaceholder(SystemLanguage());
    catalog->AddChild(FieldRow("usS.languageRow", "Language of the texts", languageInput_));
    catalog->AddChild(Theme::Caption("usS.languageHint",
        "Two letters (de, fr, th); empty follows the system (" + SystemLanguage() + "). English is the fallback for a text that has no translation."));

    catalog->AddChild(Theme::Text("usS.serversLabel", "Servers", 12.0f));
    serversArea_ = ListArea("usS.servers", 72.0f);
    serversArea_->SetPlaceholder("https://store.ultraos.eu/v1");
    catalog->AddChild(serversArea_);
    catalog->AddChild(Theme::Caption("usS.serversHint",
        "One base URL per line, tried in order; the store reads <base>/catalog.json and its signature. "
        "Empty uses the servers compiled into this build."));

    catalog->AddChild(Theme::Text("usS.keysLabel", "Additional trusted keys", 12.0f));
    keysArea_ = ListArea("usS.keys", 72.0f);
    keysArea_->SetPlaceholder("64 hex digits per line");
    catalog->AddChild(keysArea_);
    builtinInfo_ = Theme::Caption("usS.builtin", "");
    catalog->AddChild(builtinInfo_);

    refreshOnStartBox_ = Theme::Pin(UltraCanvasCheckbox::CreateCheckbox("usS.refreshOnStart", 0, 0, 300, Theme::kControlHeight,
                                                                        "Fetch the catalog when the store starts", true));
    catalog->AddChild(refreshOnStartBox_);

    auto buttons = Theme::Row("usS.buttons", Theme::kGap, true);
    auto save = Theme::Button("usS.save", "Save", 90, Theme::ButtonKind::Primary);
    save->onClick = [this]() { SaveSettingsFromPage(); };
    buttons->AddChild(save);
    auto revert = Theme::Button("usS.revert", "Revert", 90);
    revert->onClick = [this]() { LoadSettingsIntoPage(); SetStatus("Settings read again"); };
    buttons->AddChild(revert);
    auto refresh = Theme::Button("usS.refresh", "Refresh catalog now", 150);
    refresh->onClick = [this]() { RefreshCatalog(); };
    buttons->AddChild(refresh);
    catalog->AddChild(buttons);
    scroll->AddChild(catalog);

    // ----- this machine and the store -----
    auto info = Theme::Surface("usS.info", Theme::kGap);
    info->AddChild(Theme::Text("usS.info.title", "This machine and the store", 14.0f, FontWeight::Bold));
    settingsInfo_ = Theme::Text("usS.info.text", "", 12.0f);
    info->AddChild(settingsInfo_);
    auto maintenance = Theme::Row("usS.maintenance", Theme::kGap, true);
    auto collect = Theme::Button("usS.collect", "Clean up unused components", 200);
    collect->onClick = [this]() { Collect(); };
    maintenance->AddChild(collect);
    auto reload = Theme::Button("usS.reload", "Reload installed apps", 160);
    reload->onClick = [this]() { ReloadInstalled(); };
    maintenance->AddChild(reload);
    info->AddChild(maintenance);
    scroll->AddChild(info);

    // ----- about -----
    auto about = Theme::Surface("usS.about", Theme::kInnerGap);
    about->AddChild(Theme::Text("usS.about.title", "UltraStore " ULTRASTORE_VERSION, 14.0f, FontWeight::Bold));
    about->AddChild(Theme::Caption("usS.about.text",
        "The ULTRA OS app store: a signed catalog, per-user installs with the shared runtimes the apps need, "
        "updates and removal. The same program answers `ultrastore --help` on the command line. "
        "Design and decisions: Docs/UltraStore/README.md."));
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
        ? "No key is compiled into this build; the catalog's signature is checked against the keys above only."
        : std::to_string(builtin.size()) + (builtin.size() == 1 ? " key is" : " keys are") + " compiled into this build (";
    for (size_t i = 0; i < builtin.size(); ++i) text += (i ? ", " : "") + builtin[i].substr(0, 12) + "…";
    if (!builtin.empty()) text += "). One 64-digit hex key per line above adds to them.";
    else text += " One 64-digit hex key per line.";
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
        Alert("The language is two or three letters, such as de or en, or empty for the system's.");
        return;
    }
    s.language = language;
    s.servers = Lines(serversArea_->GetText());
    for (const std::string& url : s.servers) {
        if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) {
            Alert("A server is a URL starting with https://, not \"" + url + "\".");
            return;
        }
    }
    s.extraKeys = Lines(keysArea_->GetText());
    for (const std::string& key : s.extraKeys) {
        if (!IsPublicKeyHex(key)) {
            Alert("A trusted key is 64 hex digits (`ultrastore pubkey <seed>` prints one), not \"" + key + "\".");
            return;
        }
    }
    s.refreshOnStart = refreshOnStartBox_->IsChecked();
    settings_ = s;
    if (!settings_.Save()) {
        Alert("The settings could not be written to " + UltraCanvasAppSettings("UltraStore").File());
    }
    lang_ = settings_.Language();
    if (options_.channel.empty()) channel_ = settings_.channel;
    ApplySettings();
    LoadSettingsIntoPage();
    RebuildAllPages();
    SetStatus("Settings saved");
}

void StoreWindow::RefreshSettingsInfo() {
    if (!settingsInfo_) return;
    std::string text = "Machine: " + platform_.Describe();
    text += "\nChannel in use: " + channel_ + (options_.channel.empty() ? "" : " (from the command line)");
    text += "\nTexts in: " + lang_;
    text += "\nStore folder: " + (engine_ ? engine_->Root() : std::string("-"));
    if (!storeError_.empty()) text += "\n  could not be opened: " + storeError_;
    text += "\nSettings file: " + UltraCanvasAppSettings("UltraStore").File();
    text += "\nCatalog: " + (catalogNote_.empty() ? std::string("none") : catalogNote_);
    if (catalog_) text += "\n  " + std::to_string(catalog_->apps.size()) + " apps, " +
                          std::to_string(catalog_->components.size()) + " components, schema " + std::to_string(catalog_->schema);
    const std::vector<InstalledRecord> apps = CurrentApps(installed_);
    const std::vector<InstalledRecord> comps = InstalledComponents(installed_);
    text += "\nInstalled: " + std::to_string(apps.size()) + (apps.size() == 1 ? " app, " : " apps, ") +
            std::to_string(comps.size()) + (comps.size() == 1 ? " component" : " components");
    settingsInfo_->SetText(text);
}

} // namespace UltraStore::Ui
