// Apps/SmartHomeCenter/ui/SmartHomeCenterDevicePage.cpp
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "SmartHomeCenterControls.h"
#include "SmartHomeCenterPages.h"
#include "SmartHomeCenterStyle.h"

#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"
#include "UltraCanvasModalDialog.h"

#include <cmath>
#include <ctime>

using namespace UltraCanvas;
using namespace UltraCanvas::SmartHome;

namespace SmartHomeCenter {

namespace {

constexpr int64_t kHistorySeconds = 24 * 3600;

// What a reading's number means, for the chart's title and tooltip.
std::string ReadingWords(SmartHomeSensorType type) {
    switch (type) {
        case SmartHomeSensorType::Temperature: return "Temperature";
        case SmartHomeSensorType::Humidity: return "Humidity";
        case SmartHomeSensorType::Power: return "Power";
        case SmartHomeSensorType::Light: return "Light level";
        case SmartHomeSensorType::Motion:
        case SmartHomeSensorType::Occupancy:
        case SmartHomeSensorType::Contact:
        case SmartHomeSensorType::Water:
        case SmartHomeSensorType::Smoke:
        case SmartHomeSensorType::CarbonMonoxide: return "Raised (1) or at rest (0)";
        default: return "Reading";
    }
}

} // namespace

DevicePage::DevicePage(PageHost& h) : host(h) {
    controls = std::make_unique<DeviceControls>("shcDev", host.IconsDir(), host.IsDemoHome());
}

DevicePage::~DevicePage() = default;

std::shared_ptr<UltraCanvasContainer> DevicePage::Build() {
    auto page = Column("shcDevicePage", 16);

    // Head: back, the device's badge, its name and type, its state.
    auto head = Row("shcDevHead", 14);
    Fixed(head, -1, 60);
    head->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto back = MakeButton("shcDevBack", "Back", ButtonKind::Secondary, 36);
    back->SetIcon(host.IconsDir() + "back.svg");
    back->SetIconSize(16, 16);
    back->SetIconSpacing(6);
    back->SetOnClick([this]() { host.GoBack(); });
    head->AddChild(back);
    badge = Row("shcDevBadge", 0);
    badge->layout.SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
    badge->SetBorderRadius(14);
    Fixed(badge, 54, 54);
    icon = CreateImageElement("shcDevIcon", 28, 28);
    icon->SetFitMode(ImageFitMode::Contain);
    Fixed(icon, 28, 28);
    badge->AddChild(icon);
    head->AddChild(badge);
    auto names = Column("shcDevNames", 2);
    name = Text("shcDevName", "", 20, kInk, true, 28);
    type = Text("shcDevType", "", 13, kMuted, false, 18);
    names->AddChild(name);
    names->AddChild(type);
    Grow(names);
    head->AddChild(names);
    status = CreateBadge("shcDevStatus", 0, 0, "", BadgeVariant::Neutral);
    Fixed(status, -1, 22);
    head->AddChild(status);
    page->AddChild(head);

    auto body = Row("shcDevBody", 20, CSSLayout::AlignItems::Stretch);
    Grow(body);

    // Left: controls, then every detail.
    auto left = ScrollColumn("shcDevLeft", 16);
    Fixed(left, 360, -1);
    left->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto controlsCard = Column("shcDevControlsCard", 12);
    Card(controlsCard, 18);
    controlsCard->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    controlsCard->layoutItem.SetFlexShrink(0);
    controlsCard->AddChild(Caption("shcDevControlsCap", "CONTROLS"));
    controlsCard->AddChild(controls->Panel());
    left->AddChild(controlsCard);

    auto details = Column("shcDevDetails", 6);
    Card(details, 18);
    details->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    details->layoutItem.SetFlexShrink(0);
    details->AddChild(Caption("shcDevDetailsCap", "DETAILS"));
    for (const char* fact : {"Room", "Type", "Model", "Made by", "Firmware", "Serial", "Protocol", "Security",
                             "Battery", "Last seen", "Id"}) {
        auto row = Row(std::string("shcDevFact_") + fact, 8);
        Fixed(row, -1, 22);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        auto key = Text(std::string("shcDevFactKey_") + fact, fact, 13, kMuted, false, 20);
        Fixed(key, 96, 20);
        row->AddChild(key);
        auto value = Text(std::string("shcDevFactValue_") + fact, "", 13, kInk, false, 20);
        Grow(value);
        row->AddChild(value);
        details->AddChild(row);
        facts.emplace_back(fact, value);
    }
    left->AddChild(details);
    body->AddChild(left);

    // Right: history, then name, room and removal.
    auto right = Column("shcDevRight", 16);
    Grow(right);
    auto historyCard = Column("shcDevHistory", 8);
    Card(historyCard, 16);
    Grow(historyCard);
    historyTitle = Text("shcDevHistoryTitle", "History", 15, kInk, true, 22);
    historyCard->AddChild(historyTitle);
    chartData = std::make_shared<ChartDataVector>();
    chart = CreateLineChartElement("shcDevChart", 0, 0, 600, 300);
    chart->SetXAxisLabelMode(XAxisLabelMode::DataLabel);
    chart->SetLineColor(kAccent);
    chart->SetLineWidth(2.0f);
    chart->SetShowDataPoints(false);
    chart->SetShowValueLabels(false);
    chart->SetBackgroundColor(kSurface);
    chart->SetPlotAreaColor(kSurface);
    chart->SetGridColor(Color(236, 239, 245));
    chart->SetEmptyMessageColor(kMuted);
    chart->SetEmptyIcon(host.IconsDir() + "activity.svg", kIdle.fg, kIdle.soft);
    chart->SetDataSource(chartData);
    Grow(chart);
    historyCard->AddChild(chart);
    right->AddChild(historyCard);

    auto settings = Column("shcDevSettings", 10);
    Card(settings, 16);
    Fixed(settings, -1, 176);
    settings->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    settings->AddChild(Caption("shcDevSettingsCap", "NAME AND ROOM"));
    auto nameRow = Row("shcDevNameRow", 10);
    Fixed(nameRow, -1, 38);
    nameRow->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto nameLabel = Text("shcDevNameLabel", "Name", 13, kMuted, false, 20);
    Fixed(nameLabel, 60, 20);
    nameRow->AddChild(nameLabel);
    nameInput = MakeInput("shcDevNameInput", "", "The device's name");
    Grow(nameInput);
    nameInput->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
    nameInput->onEnterPressed = [this](const std::string&) { SaveName(); return true; };
    nameRow->AddChild(nameInput);
    auto save = MakeButton("shcDevNameSave", "Rename", ButtonKind::Secondary, 36);
    save->SetOnClick([this]() { SaveName(); });
    nameRow->AddChild(save);
    settings->AddChild(nameRow);

    auto roomRow = Row("shcDevRoomRow", 10);
    Fixed(roomRow, -1, 38);
    roomRow->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto roomLabel = Text("shcDevRoomLabel", "Room", 13, kMuted, false, 20);
    Fixed(roomLabel, 60, 20);
    roomRow->AddChild(roomLabel);
    roomChoice = std::make_shared<UltraCanvasDropdown>("shcDevRoomChoice", 240, 34);
    Fixed(roomChoice, 240, 34);
    roomChoice->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (fillingRooms || index < 0 || index >= static_cast<int>(roomChoiceIds.size())) return;
        const std::string target = roomChoiceIds[static_cast<size_t>(index)];
        if (SmartHomeAPI::Instance().AssignDeviceToRoom(deviceId, target))
            host.Note("Moved " + SmartHomeAPI::Instance().GetDevice(deviceId).Name + " to " +
                      (target.empty() ? std::string("no room") : SmartHomeAPI::Instance().GetRoom(target).Name));
    };
    roomRow->AddChild(roomChoice);
    roomRow->AddChild(Spacer("shcDevRoomSpace"));
    auto remove = MakeButton("shcDevRemove", "Remove device", ButtonKind::Danger, 36);
    remove->SetOnClick([this]() { Remove(); });
    roomRow->AddChild(remove);
    settings->AddChild(roomRow);
    settings->AddChild(Paragraph("shcDevRenameNote",
        "A new name is kept by SmartHome Center; the device itself keeps the one it was given.", 12, kMuted, 1));
    right->AddChild(settings);
    body->AddChild(right);
    page->AddChild(body);
    return page;
}

const DeviceView* DevicePage::View() const {
    for (const DeviceView& v : host.Devices())
        if (v.info.DeviceId == deviceId) return &v;
    return nullptr;
}

void DevicePage::Show(const std::string& id) {
    deviceId = id;
    const DeviceView* v = View();
    if (!v) return;
    nameInput->SetText(v->info.Name);
    controls->Build(*v);
    FillRooms();
    LoadHistory();
    UpdateStates();
}

void DevicePage::FillRooms() {
    const DeviceView* v = View();
    fillingRooms = true;
    roomChoice->ClearItems();
    roomChoiceIds.clear();
    int selected = -1;
    for (const auto& room : SmartHomeAPI::Instance().GetRooms()) {
        if (v && room.RoomId == v->info.RoomId) selected = static_cast<int>(roomChoiceIds.size());
        roomChoice->AddItem(room.Name, room.RoomId);
        roomChoiceIds.push_back(room.RoomId);
    }
    if (v && v->info.RoomId.empty()) selected = static_cast<int>(roomChoiceIds.size());
    roomChoice->AddItem(kNoRoom, "");
    roomChoiceIds.push_back("");
    roomChoice->SetSelectedIndex(selected, false);
    fillingRooms = false;
}

void DevicePage::RoomsChanged() {
    if (!deviceId.empty()) FillRooms();
}

void DevicePage::UpdateStates() {
    const DeviceView* v = View();
    if (!v) return;
    if (!controls->Update(*v)) controls->Build(*v);
    const Tone tone = ToneOf(*v);
    badge->SetBackgroundColor(tone.soft);
    icon->LoadFromFile(host.IconsDir() + v->icon + ".svg");
    icon->SetTintColor(v->online ? tone.fg : kFaint);
    name->SetText(v->info.Name);
    type->SetText(v->typeName + " · " + RoomText(v->info.Room));
    status->SetText(v->state);
    status->SetVariant(StatusVariant(*v));
    for (auto& [fact, label] : facts) {
        std::string text;
        if (fact == "Room") text = RoomText(v->info.Room);
        else if (fact == "Type") text = v->typeName;
        else if (fact == "Model") text = v->info.Model;
        else if (fact == "Made by") text = v->info.Manufacturer;
        else if (fact == "Firmware") text = v->info.FirmwareVersion;
        else if (fact == "Serial") text = v->info.SerialNumber;
        else if (fact == "Protocol") text = v->info.Protocol == SmartHomeProtocolType::Simulated
                                          ? "Simulated (demo home)" : ProtocolTypeToString(v->info.Protocol);
        else if (fact == "Security") text = SecurityText(v->info.Security);
        else if (fact == "Battery") text = v->battery >= 0 ? FormatPercent(v->battery) : "Mains powered";
        else if (fact == "Last seen") text = AgoText(v->info.LastSeen);
        else if (fact == "Id") text = v->info.DeviceId;
        label->SetText(text.empty() ? "-" : text);
        label->SetTextColor(fact == "Battery" && v->battery >= 0 && v->battery < 15 ? kAlarm.fg : kInk);
    }
    // The room may have changed elsewhere.
    if (!fillingRooms) {
        for (size_t i = 0; i < roomChoiceIds.size(); ++i) {
            if (roomChoiceIds[i] == v->info.RoomId && roomChoice->GetSelectedIndex() != static_cast<int>(i)) {
                fillingRooms = true;
                roomChoice->SetSelectedIndex(static_cast<int>(i), false);
                fillingRooms = false;
            }
        }
    }
}

void DevicePage::Tick() {
    // History is recorded at most every few seconds; half a minute keeps the
    // chart current without redrawing it all the time.
    if (++ticks % 30 == 0) LoadHistory();
    UpdateStates();
}

void DevicePage::LoadHistory() {
    auto& api = SmartHomeAPI::Instance();
    const int64_t now = NowSeconds();
    const auto readings = api.GetSensorHistory(deviceId, static_cast<uint64_t>(now - kHistorySeconds) * 1000, UINT64_MAX);
    std::vector<ChartDataPoint> points;
    SmartHomeSensorType kind = SmartHomeSensorType::Unknown;
    std::string unit;
    for (const auto& r : readings) {
        kind = r.Type;
        unit = r.Unit;
        const double y = (r.Type == SmartHomeSensorType::Motion || r.Type == SmartHomeSensorType::Contact ||
                          r.Type == SmartHomeSensorType::Water || r.Type == SmartHomeSensorType::Smoke ||
                          r.Type == SmartHomeSensorType::Occupancy || r.Type == SmartHomeSensorType::CarbonMonoxide)
                         ? (r.Triggered ? 1.0 : 0.0) : static_cast<double>(r.Value);
        points.emplace_back(static_cast<double>(points.size()), y, 0.0,
                            ClockText(static_cast<int64_t>(r.Timestamp / 1000)).substr(0, 5), 0.0);
    }
    // A floor under the axis, so a steady room reads as steady rather than
    // as its sensor's last digit swinging across the whole chart.
    double span = 1.0;   // events: 0 and 1
    switch (kind) {
        case SmartHomeSensorType::Temperature: span = 2.0; break;     // °C
        case SmartHomeSensorType::Humidity: span = 10.0; break;       // %
        case SmartHomeSensorType::Power: span = 20.0; break;          // W
        case SmartHomeSensorType::Light: span = 50.0; break;          // lx
        default: break;
    }
    chart->SetMinimumValueSpan(span);
    chartData->LoadFromArray(points);
    chart->SetDataSource(chartData);
    if (points.empty()) {
        historyTitle->SetText("History");
        if (!api.IsStoreOpen()) chart->SetEmptyMessage("No history", "The home's data file is not open, so nothing is recorded.");
        else chart->SetEmptyMessage("No history yet",
                                    "Temperatures, humidity, power and sensor events are recorded as they change.");
    } else {
        historyTitle->SetText(ReadingWords(kind) + (unit.empty() || kind == SmartHomeSensorType::Motion ? "" : " (" + unit + ")") +
                              " - last 24 hours, " + std::to_string(points.size()) +
                              (points.size() == 1 ? " reading" : " readings"));
    }
}

void DevicePage::SaveName() {
    const std::string wanted = nameInput->GetText();
    const DeviceView* v = View();
    if (!v || wanted.empty() || wanted == v->info.Name) return;
    const std::string old = v->info.Name;
    if (SmartHomeAPI::Instance().RenameDevice(deviceId, wanted)) host.Note("Renamed " + old + " to " + wanted);
}

void DevicePage::Remove() {
    const DeviceView* v = View();
    if (!v) return;
    const std::string id = deviceId, deviceName = v->info.Name;
    UltraCanvasDialogManager::ShowConfirmation(
        "Remove " + deviceName + " from the home? It stops being controlled here until it is added again.",
        "Remove device", [this, id, deviceName](bool confirmed) {
            if (!confirmed) return;
            if (SmartHomeAPI::Instance().RemoveDevice(id)) host.Note("Removed " + deviceName);
            host.GoBack();
        }, host.DialogParent());
}

} // namespace SmartHomeCenter
