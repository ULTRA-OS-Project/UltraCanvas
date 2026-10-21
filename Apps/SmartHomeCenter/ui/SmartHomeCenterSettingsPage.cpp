// Apps/SmartHomeCenter/ui/SmartHomeCenterSettingsPage.cpp
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "SmartHomeCenterPages.h"
#include "SmartHomeCenterPaths.h"
#include "SmartHomeCenterStyle.h"

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"    // PathFromUtf8
#include "UltraCanvasTextUtils.h"   // TryParseFloat / FormatFloatClassic

#include <algorithm>
#include <filesystem>
#include <system_error>

using namespace UltraCanvas;
using namespace UltraCanvas::SmartHome;

namespace SmartHomeCenter {

void FillRadioList(const std::shared_ptr<UltraCanvasContainer>& list, const std::string& idPrefix) {
    auto& api = SmartHomeAPI::Instance();
    list->ClearChildren();
    const struct { SmartHomeProtocolType type; const char* what; } radios[] = {
        {SmartHomeProtocolType::Matter, "Matter - devices on the home network"},
        {SmartHomeProtocolType::Thread, "Thread - low-power mesh, through a border router"},
        {SmartHomeProtocolType::Zigbee, "Zigbee - through a USB stick"},
        {SmartHomeProtocolType::ZWave, "Z-Wave - through a USB stick"},
        {SmartHomeProtocolType::KNX, "KNX - through an IP gateway"},
        {SmartHomeProtocolType::Simulated, "Demo home - simulated devices"},
    };
    int index = 0;
    for (const auto& radio : radios) {
        const std::string key = idPrefix + std::to_string(index++);
        const bool on = api.IsProtocolEnabled(radio.type);
        const bool built = on || api.HasProtocolBackend(radio.type);
        auto row = Row(key, 10);
        Fixed(row, -1, 26);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        auto name = Text(key + "Name", radio.what, 13, built ? kInk : kFaint, false, 20);
        Grow(name);
        row->AddChild(name);
        auto state = CreateBadge(key + "State", 0, 0,
                                 on ? "On" : built ? "No adapter found" : "Not in this build",
                                 on ? BadgeVariant::Successful : built ? BadgeVariant::Warning : BadgeVariant::Neutral);
        Fixed(state, -1, 20);
        row->AddChild(state);
        list->AddChild(row);
    }
    list->InvalidateLayout();
}

SettingsPage::SettingsPage(PageHost& h) : host(h) {}

std::shared_ptr<UltraCanvasContainer> SettingsPage::Build() {
    auto page = ScrollColumn("shcSettingsPage", 16);
    page->SetPadding(0, 16, 0, 0);

    auto card = [&](const std::string& id, const std::string& caption, float height) {
        auto c = Column(id, 10);
        Card(c, 18);
        Fixed(c, -1, height);
        c->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        c->AddChild(Caption(id + "Cap", caption));
        page->AddChild(c);
        return c;
    };

    // Where the home is.
    auto where = card("shcSetWhere", "WHERE THE HOME IS", 168);
    where->AddChild(Paragraph("shcSetWhereNote",
        "Used for sunrise and sunset in automations, worked out on this computer - the location goes nowhere else. "
        "Degrees, north and east positive (Berlin is 52.52, 13.405).", 13, kMuted, 2));
    auto coords = Row("shcSetCoords", 10);
    Fixed(coords, -1, 38);
    coords->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto latLabel = Text("shcSetLatLabel", "Latitude", 13, kMuted, false, 20);
    Fixed(latLabel, 76, 20);
    coords->AddChild(latLabel);
    latitude = MakeInput("shcSetLat", "", "52.52");
    Fixed(latitude, 120, 36);
    coords->AddChild(latitude);
    auto lonLabel = Text("shcSetLonLabel", "Longitude", 13, kMuted, false, 20);
    Fixed(lonLabel, 86, 20);
    coords->AddChild(lonLabel);
    longitude = MakeInput("shcSetLon", "", "13.405");
    Fixed(longitude, 120, 36);
    coords->AddChild(longitude);
    auto save = MakeButton("shcSetLocationSave", "Save", ButtonKind::Primary, 36);
    save->SetOnClick([this]() { SaveLocation(); });
    coords->AddChild(save);
    sunText = Text("shcSetSun", "", 13, kInk, false, 20);
    Grow(sunText);
    sunText->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
    coords->AddChild(sunText);
    where->AddChild(coords);

    // The home's data.
    auto data = card("shcSetData", "THE HOME'S DATA", 176);
    storeText = Paragraph("shcSetStore", "", 13, kInk, 2);
    data->AddChild(storeText);
    data->AddChild(Paragraph("shcSetKept",
        "Kept: rooms, names, scenes, automations; sensor history for 90 days; the last 20 000 events of the log.",
        13, kMuted, 1));
    auto backup = MakeButton("shcSetBackup", "Export a backup…", ButtonKind::Secondary, 36);
    backup->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Start);
    backup->SetOnClick([this]() { ExportBackup(); });
    data->AddChild(backup);

    // Radios.
    auto radioCard = card("shcSetRadios", "RADIOS", 290);
    radioCard->AddChild(Paragraph("shcSetRadiosNote",
        "Which ways of talking to devices this build carries. Adapters are looked for when SmartHome Center starts.",
        13, kMuted, 1));
    radios = Column("shcSetRadioList", 6);
    radios->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    radios->layoutItem.SetFlexShrink(0);
    radioCard->AddChild(radios);
    return page;
}

void SettingsPage::Refresh() {
    auto& api = SmartHomeAPI::Instance();
    double lat = 0, lon = 0;
    if (api.GetHomeLocation(lat, lon)) {
        latitude->SetText(FormatFloatClassic(lat, 8));
        longitude->SetText(FormatFloatClassic(lon, 8));
    }
    int64_t sunrise = 0, sunset = 0;
    sunText->SetText(api.GetSunTimes(sunrise, sunset)
                         ? "Today: sunrise " + ClockText(sunrise).substr(0, 5) + ", sunset " + ClockText(sunset).substr(0, 5)
                         : std::string(api.GetHomeLocation(lat, lon) ? "No sunrise or sunset there today" : "No location yet"));

    const std::string path = DefaultStorePath(host.IsDemoHome());
    storeText->SetText(api.IsStoreOpen() ? "Kept in " + path + (host.IsDemoHome() ? "  (the demo home's own file)" : "")
                                         : "Not kept: the data file is not open (" + api.GetStoreError() +
                                               "). Changes last until SmartHome Center closes.");
    FillRadioList(radios, "shcSetRadio_");
}

void SettingsPage::SaveLocation() {
    std::string lat = latitude->GetText(), lon = longitude->GetText();
    std::replace(lat.begin(), lat.end(), ',', '.');
    std::replace(lon.begin(), lon.end(), ',', '.');
    double a = 0, b = 0;
    if (!TryParseFloat(lat, a) || !TryParseFloat(lon, b) || !SmartHomeAPI::Instance().SetHomeLocation(a, b)) {
        UltraCanvasDialogManager::ShowInformation(
            "Latitude goes from -90 to 90 and longitude from -180 to 180, in degrees with a dot: 52.52 and 13.405.",
            "Where the home is", nullptr, host.DialogParent());
        return;
    }
    host.Note("Set the home's location");
    Refresh();
}

void SettingsPage::ExportBackup() {
    auto& api = SmartHomeAPI::Instance();
    if (!api.IsStoreOpen()) {
        UltraCanvasDialogManager::ShowInformation("There is no data file to back up: " + api.GetStoreError(),
                                                  "Export a backup", nullptr, host.DialogParent());
        return;
    }
    FileDialogOptions options;
    options.title = "Export a backup of the home";
    options.defaultFileName = "smarthome-backup-" + DateTimeText(NowSeconds()).substr(0, 6) + ".db";
    std::replace(options.defaultFileName.begin(), options.defaultFileName.end(), ' ', '-');
    options.filters.push_back(FileFilter("SmartHome Center backup", "db"));
    options.defaultExtension = "db";
    options.saveSettingsButton = false;   // SQLite writes it, not a FileLoader writer
    options.parentWindow = host.DialogParent();
    UltraCanvasFileLoader::SaveFileDialog(options, [this](DialogResult result, const std::string& path) {
        if (result != DialogResult::OK || path.empty()) return;
        // The dialog asked before replacing; the export refuses a file that
        // exists, so the old one goes first.
        std::error_code ec;
        std::filesystem::remove(PathFromUtf8(path), ec);
        auto& home = SmartHomeAPI::Instance();
        if (home.ExportStore(path)) {
            host.Note("Exported a backup to " + path);
        } else {
            UltraCanvasDialogManager::ShowWarning("The backup could not be written: " + home.GetStoreError(),
                                                  "Export a backup", nullptr, host.DialogParent());
        }
    });
}

} // namespace SmartHomeCenter
