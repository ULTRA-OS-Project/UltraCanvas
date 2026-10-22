// Apps/SmartHomeCenter/ui/SmartHomeCenterFirstRun.cpp
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "SmartHomeCenterPages.h"
#include "SmartHomeCenterStyle.h"

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextUtils.h"   // TryParseFloat / FormatFloatClassic

#include <algorithm>

using namespace UltraCanvas;
using namespace UltraCanvas::SmartHome;

namespace SmartHomeCenter {

namespace {

constexpr int kSteps = 4;

// Rooms most homes have, offered to tick on the first run.
const char* const kSuggestedRooms[] = {
    "Living room", "Kitchen", "Dining room", "Bedroom", "Kids' room", "Guest room", "Bathroom",
    "Hallway", "Office", "Utility room", "Garage", "Garden",
};

bool HasRoomNamed(const std::vector<SmartHomeRoom>& rooms, const std::string& name) {
    for (const auto& r : rooms) {
        std::string a = r.Name, b = name;
        std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (a == b) return true;
    }
    return false;
}

} // namespace

FirstRunPage::FirstRunPage(PageHost& h) : host(h) {}

std::shared_ptr<UltraCanvasContainer> FirstRunPage::Build() {
    // One card in the middle of the page, the steps one after another in it.
    auto page = Row("shcFirstRunPage", 0, CSSLayout::AlignItems::Center);
    page->layout.SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
    auto card = Column("shcFirstRunCard", 14);
    Card(card, 28);
    Fixed(card, 640, 520);
    page->AddChild(card);

    stepText = Text("shcFirstRunStep", "", 12, kAccent, true, 18);
    card->AddChild(stepText);
    auto stepsHolder = Column("shcFirstRunSteps", 0);
    Grow(stepsHolder);
    card->AddChild(stepsHolder);

    auto step = [&](const std::string& key, const std::string& title) {
        auto s = Column("shcFirstRun_" + key, 12);
        Grow(s);
        s->AddChild(Text("shcFirstRunTitle_" + key, title, 22, kInk, true, 32));
        stepsHolder->AddChild(s);
        steps.push_back(s);
        return s;
    };

    // 1. Welcome.
    auto welcome = step("welcome", "Welcome to SmartHome Center");
    welcome->AddChild(Paragraph("shcFirstRunWelcome",
        "Every light, switch, thermostat, lock, blind and sensor of the home in one window - over Matter, Thread, "
        "Zigbee, Z-Wave and KNX, run on this computer with no cloud in between.", 14, kInk, 3));
    welcome->AddChild(Paragraph("shcFirstRunWelcomeHome", host.IsDemoHome()
        ? "No radio adapter was found, so this is the demo home: 30 simulated devices in 8 rooms that answer like "
          "real ones. Nothing here switches anything in your home. The next steps work the same as for a real home."
        : "A few questions, then your home: where it is, which rooms it has, and the radios to find its devices with.",
        14, kMuted, 4));

    // 2. Where.
    auto where = step("where", "Where is the home?");
    where->AddChild(Paragraph("shcFirstRunWhereNote",
        "Only for sunrise and sunset in automations (\"blinds down at sunset\"). It is worked out here and goes "
        "nowhere else. Degrees, with a dot; you can change it later in Settings.", 14, kMuted, 3));
    auto coords = Row("shcFirstRunCoords", 10);
    Fixed(coords, -1, 38);
    coords->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto latLabel = Text("shcFirstRunLatLabel", "Latitude", 13, kMuted, false, 20);
    Fixed(latLabel, 76, 20);
    coords->AddChild(latLabel);
    latitude = MakeInput("shcFirstRunLat", "", "52.52");
    Fixed(latitude, 130, 36);
    coords->AddChild(latitude);
    auto lonLabel = Text("shcFirstRunLonLabel", "Longitude", 13, kMuted, false, 20);
    Fixed(lonLabel, 86, 20);
    coords->AddChild(lonLabel);
    longitude = MakeInput("shcFirstRunLon", "", "13.405");
    Fixed(longitude, 130, 36);
    coords->AddChild(longitude);
    where->AddChild(coords);

    // 3. Rooms.
    auto rooms = step("rooms", "Which rooms does it have?");
    rooms->AddChild(Paragraph("shcFirstRunRoomsNote",
        "Tick the ones to start with; rooms can be added, renamed and removed any time on the Rooms page.",
        14, kMuted, 2));
    roomsHolder = CreateContainer("shcFirstRunRoomBoxes");
    roomsHolder->layout.SetFlex(CSSLayout::FlexDirection::Row, CSSLayout::FlexWrap::Wrap).SetFlexGap(8);
    roomsHolder->layout.SetFlexAlignContent(CSSLayout::AlignContent::Start);   // rows together at the top
    NoScrollbars(roomsHolder);
    Grow(roomsHolder);
    rooms->AddChild(roomsHolder);

    // 4. Radios.
    auto radioStep = step("radios", "Radios and devices");
    radioStep->AddChild(Paragraph("shcFirstRunRadiosNote",
        "Devices are found through these. A USB stick or a gateway that was plugged in after SmartHome Center "
        "started is found the next time it starts.", 14, kMuted, 3));
    auto list = Column("shcFirstRunRadioList", 6);
    list->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    list->layoutItem.SetFlexShrink(0);
    FillRadioList(list, "shcFirstRunRadio_");
    radioStep->AddChild(list);
    auto addDevice = MakeButton("shcFirstRunAddDevice", "Add a device now", ButtonKind::Secondary, 38);
    addDevice->SetIcon(host.IconsDir() + "plus.svg");
    addDevice->SetIconSize(16, 16);
    addDevice->SetIconSpacing(8);
    addDevice->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Start);
    addDevice->SetOnClick([this]() {
        Finish();
        host.AddDevice();
    });
    radioStep->AddChild(addDevice);

    auto buttons = Row("shcFirstRunButtons", 10);
    Fixed(buttons, -1, 40);
    buttons->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto skip = MakeButton("shcFirstRunSkip", "Skip", ButtonKind::Quiet, 40);
    skip->SetTooltip("Straight to the home; everything here is in Settings and Rooms too");
    skip->SetOnClick([this]() { host.ShowPage(Page::Home); });
    buttons->AddChild(skip);
    buttons->AddChild(Spacer("shcFirstRunSpace"));
    backButton = MakeButton("shcFirstRunBack", "Back", ButtonKind::Secondary);
    backButton->SetOnClick([this]() { Back(); });
    buttons->AddChild(backButton);
    nextButton = MakeButton("shcFirstRunNext", "Next", ButtonKind::Primary);
    nextButton->SetOnClick([this]() { Next(); });
    buttons->AddChild(nextButton);
    card->AddChild(buttons);
    return page;
}

void FirstRunPage::Start() {
    auto& api = SmartHomeAPI::Instance();
    double lat = 0, lon = 0;
    if (api.GetHomeLocation(lat, lon)) {
        latitude->SetText(FormatFloatClassic(lat, 8));
        longitude->SetText(FormatFloatClassic(lon, 8));
    }
    // The rooms the home has already (the demo home's, or ones a backend
    // named) ticked; the suggestions it lacks, not.
    roomsHolder->ClearChildren();
    roomBoxes.clear();
    const auto existing = api.GetRooms();
    std::vector<std::pair<std::string, bool>> offered;
    for (const auto& r : existing) offered.emplace_back(r.Name, true);
    for (const char* name : kSuggestedRooms)
        if (!HasRoomNamed(existing, name)) offered.emplace_back(name, false);
    int index = 0;
    for (const auto& [name, has] : offered) {
        auto box = UltraCanvasCheckbox::CreateCheckbox("shcFirstRunRoom_" + std::to_string(index++), 0, 0, 180, 28, name, has);
        box->SetFontSize(14);
        box->SetDisabled(has);   // a room the home has stays; Rooms removes it
        Fixed(box, 186, 28);
        roomsHolder->AddChild(box);
        roomBoxes.emplace_back(name, box);
    }
    ShowStep(0);
}

void FirstRunPage::ShowStep(int index) {
    step = std::clamp(index, 0, kSteps - 1);
    for (int i = 0; i < static_cast<int>(steps.size()); ++i) steps[static_cast<size_t>(i)]->SetVisible(i == step);
    stepText->SetText("STEP " + std::to_string(step + 1) + " OF " + std::to_string(kSteps));
    backButton->SetDisabled(step == 0);
    nextButton->SetText(step == kSteps - 1 ? "Finish" : "Next");
    if (auto* parent = steps.front()->GetParentContainer()) parent->InvalidateLayout();
}

void FirstRunPage::Next() {
    if (step == 1) {
        // A location is optional; one typed must make sense.
        std::string lat = latitude->GetText(), lon = longitude->GetText();
        if (!lat.empty() || !lon.empty()) {
            std::replace(lat.begin(), lat.end(), ',', '.');
            std::replace(lon.begin(), lon.end(), ',', '.');
            double a = 0, b = 0;
            if (!TryParseFloat(lat, a) || !TryParseFloat(lon, b) || !SmartHomeAPI::Instance().SetHomeLocation(a, b)) {
                UltraCanvasDialogManager::ShowInformation(
                    "Latitude goes from -90 to 90 and longitude from -180 to 180: 52.52 and 13.405, say. "
                    "Leave both empty to skip this.", "Where is the home?", nullptr, host.DialogParent());
                return;
            }
        }
    }
    if (step == 2) {
        int added = 0;
        for (const auto& [name, box] : roomBoxes) {
            if (box->IsChecked() && !box->IsDisabled() && SmartHomeAPI::Instance().CreateRoom({"", name, "", "", 0})) ++added;
        }
        if (added > 0) host.Note("Added " + std::to_string(added) + (added == 1 ? " room" : " rooms"));
    }
    if (step == kSteps - 1) {
        Finish();
        return;
    }
    ShowStep(step + 1);
}

void FirstRunPage::Back() {
    ShowStep(step - 1);
}

void FirstRunPage::Finish() {
    host.Note("Set up SmartHome Center");
    host.ShowPage(Page::Home);
}

} // namespace SmartHomeCenter
