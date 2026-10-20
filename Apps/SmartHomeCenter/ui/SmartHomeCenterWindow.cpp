// Apps/SmartHomeCenter/ui/SmartHomeCenterWindow.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "SmartHomeCenterWindow.h"

#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasListModel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasUtils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

// SMARTHOMECENTER_VERSION comes from the build alone: CMake reads the first
// line of Docs/SmartHomeCenter/CHANGELOG.md (cmake/UltraCanvasVersion.cmake)
// and passes it as a compile definition. No fallback here, so a build that
// lost it fails instead of reporting a wrong number.
#ifndef SMARTHOMECENTER_VERSION
#error "SMARTHOMECENTER_VERSION is not defined: build through CMake, which reads it from Docs/SmartHomeCenter/CHANGELOG.md"
#endif

using namespace UltraCanvas;
using namespace UltraCanvas::SmartHome;

namespace SmartHomeCenter {

namespace {

// ===== LOOK =====
// One palette for the whole window: a cool grey canvas, white cards, one
// accent, and a warm colour per kind of "active" so a glance at the Home page
// says what is on: amber for light and power, coral for heat, green for an
// open lock or blind, blue for a sensor reporting, red for an alarm.
const Color kCanvas(243, 245, 250);
const Color kSurface(255, 255, 255);
const Color kBorder(228, 232, 240);
const Color kInk(27, 33, 50);
const Color kMuted(107, 115, 133);
const Color kFaint(160, 167, 182);
const Color kAccent(69, 96, 230);
const Color kAccentSoft(232, 236, 253);
const Color kHover(240, 242, 247);

struct Tone { Color fg, soft, border; };
const Tone kIdle    {Color(120, 128, 146), Color(241, 243, 247), kBorder};
const Tone kLit     {Color(232, 150, 20),  Color(255, 246, 229), Color(249, 213, 140)};
const Tone kHeat    {Color(240, 100, 60),  Color(255, 240, 234), Color(250, 196, 176)};
const Tone kOpen    {Color(34, 160, 107),  Color(229, 246, 238), Color(160, 220, 190)};
const Tone kSense   {Color(46, 124, 246),  Color(232, 241, 253), Color(170, 200, 248)};
const Tone kAlarm   {Color(229, 72, 77),   Color(253, 236, 236), Color(246, 180, 182)};
const Tone kClimate {Color(46, 124, 246),  Color(236, 244, 255), kBorder};

constexpr float kSidebarWidth = 236.0f;
constexpr float kInspectorWidth = 330.0f;
constexpr float kTileWidth = 206.0f;
constexpr float kTileHeight = 118.0f;
constexpr unsigned kUiTimerMs = 250;
constexpr size_t kActivityLimit = 500;

const char* const kSceneMorning = "center.morning";
const char* const kSceneEvening = "center.evening";
const char* const kSceneAway = "center.away";
const char* const kSceneNight = "center.night";

Tone ToneOf(const DeviceView& v) {
    if (v.alert) return kAlarm;
    switch (v.kind) {
        case DeviceKind::Light:
        case DeviceKind::Switch: return v.active ? kLit : kIdle;
        case DeviceKind::Thermostat: return v.active ? kHeat : kClimate;
        case DeviceKind::Lock:
        case DeviceKind::Blind: return v.active ? kOpen : kIdle;
        case DeviceKind::ClimateSensor: return kClimate;
        case DeviceKind::BinarySensor: return v.active ? kSense : kIdle;
        default: return kIdle;
    }
}

int64_t NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string ClockText(int64_t seconds) {
    std::time_t t = static_cast<std::time_t>(seconds);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char text[16];
    std::strftime(text, sizeof text, "%H:%M:%S", &local);
    return text;
}

std::string TodayText() {
    std::time_t t = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char text[64];
    std::strftime(text, sizeof text, "%A, %d %B", &local);
    return text;
}

std::string Greeting() {
    std::time_t t = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    if (local.tm_hour < 5) return "Good night";
    if (local.tm_hour < 12) return "Good morning";
    if (local.tm_hour < 18) return "Good afternoon";
    return "Good evening";
}

std::string AgoText(uint64_t lastSeenMs) {
    if (lastSeenMs == 0) return "never";
    const int64_t seconds = NowSeconds() - static_cast<int64_t>(lastSeenMs / 1000);
    if (seconds < 10) return "just now";
    if (seconds < 60) return std::to_string(seconds) + " s ago";
    if (seconds < 3600) return std::to_string(seconds / 60) + " min ago";
    return std::to_string(seconds / 3600) + " h ago";
}

std::string SecurityText(SmartHomeSecurityLevel level) {
    switch (level) {
        case SmartHomeSecurityLevel::Certified: return "Certified (attested)";
        case SmartHomeSecurityLevel::Encrypted: return "Encrypted";
        case SmartHomeSecurityLevel::Basic: return "Basic";
        default: return "Unencrypted";
    }
}

// ===== BUILDING BLOCKS =====

void NoScrollbars(const std::shared_ptr<UltraCanvasContainer>& c) {
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    c->SetContainerStyle(plain);
}

std::shared_ptr<UltraCanvasContainer> Column(const std::string& id, float gap) {
    auto c = CreateContainer(id);
    c->layout.SetFlexColumn().SetFlexGap(gap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    NoScrollbars(c);
    return c;
}

std::shared_ptr<UltraCanvasContainer> Row(const std::string& id, float gap,
                                          CSSLayout::AlignItems align = CSSLayout::AlignItems::Center) {
    auto c = CreateContainer(id);
    c->layout.SetFlexRow().SetFlexGap(gap).SetFlexAlignItems(align);
    NoScrollbars(c);
    return c;
}

void Fixed(const std::shared_ptr<UltraCanvasUIElement>& e, float w, float h) {
    e->SetElementSize(w < 0 ? CSSLayout::Dimension::Auto() : CSSLayout::Dimension::Px(w),
                      h < 0 ? CSSLayout::Dimension::Auto() : CSSLayout::Dimension::Px(h));
    e->layoutItem.SetFlexGrow(0);
    e->layoutItem.SetFlexShrink(0);
}

void Grow(const std::shared_ptr<UltraCanvasUIElement>& e) {
    e->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    e->layoutItem.SetFlexGrow(1);
    e->layoutItem.SetFlexShrink(1);
    e->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

// A flexible gap that pushes what follows it to the far end of a row.
std::shared_ptr<UltraCanvasContainer> Spacer(const std::string& id) {
    auto s = CreateContainer(id);
    NoScrollbars(s);
    s->layoutItem.SetFlexGrow(1);
    s->layoutItem.SetFlexShrink(1);
    s->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    s->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(1));
    return s;
}

void Card(const std::shared_ptr<UltraCanvasContainer>& c, float padding = 16.0f) {
    c->SetBackgroundColor(kSurface);
    c->SetBorders(1.0f, kBorder, 14.0f);
    c->SetPadding(padding);
}

std::shared_ptr<UltraCanvasLabel> Text(const std::string& id, const std::string& text, float size,
                                       const Color& color, bool bold = false, float height = -1) {
    auto label = CreateLabel(id, 0, 0, 0, height > 0 ? height : size + 10, text);
    label->SetFontSize(size);
    label->SetTextColor(color);
    if (bold) label->SetFontWeight(FontWeight::Bold);
    label->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(height > 0 ? height : size + 10));
    label->layoutItem.SetFlexShrink(0);
    return label;
}

} // namespace

// ===== TABLE MODELS =====

class DevicesTableModel : public IListModel {
public:
    void Set(std::vector<DeviceView> views) { rows = std::move(views); NotifyDataChanged(); }
    const DeviceView* At(int row) const { return row >= 0 && row < static_cast<int>(rows.size()) ? &rows[row] : nullptr; }

    int GetRowCount() const override { return static_cast<int>(rows.size()); }
    int GetColumnCount() const override { return 8; }
    ListColumnDef GetColumnDef(int column) const override {
        static const ListColumnDef columns[] = {
            {"Name", 190}, {"Room", 120}, {"Type", 160}, {"State", 210},
            {"Battery", 74, TextAlignment::Right}, {"Power", 80, TextAlignment::Right},
            {"Protocol", 96}, {"Last seen", 100}};
        return columns[std::clamp(column, 0, 7)];
    }
    ListDataValue GetData(const ListIndex& index, ListDataRole role) const override {
        const DeviceView* v = At(index.row);
        if (!v) return {};
        if (role == ListDataRole::SortRole) {
            if (index.column == 4) return v->battery;
            if (index.column == 5) return v->power;
            if (index.column == 7) return static_cast<int>(v->info.LastSeen / 1000);
            return {};
        }
        if (role != ListDataRole::DisplayRole) return {};
        switch (index.column) {
            case 0: return v->info.Name;
            case 1: return v->info.Room.empty() ? std::string(kNoRoom) : v->info.Room;
            case 2: return v->typeName;
            case 3: return v->value.empty() ? v->state : v->value + " · " + v->state;
            case 4: return v->battery >= 0 ? FormatPercent(v->battery) : std::string("mains");
            case 5: return v->metered ? FormatPower(v->power) : std::string("-");
            case 6: return ProtocolTypeToString(v->info.Protocol);
            case 7: return AgoText(v->info.LastSeen);
            default: return {};
        }
    }
    bool SetData(const ListIndex&, ListDataRole, const ListDataValue&) override { return false; }

private:
    std::vector<DeviceView> rows;
};

class ActivityTableModel : public IListModel {
public:
    explicit ActivityTableModel(const std::deque<ActivityEntry>& source) : entries(source) {}
    void Changed() { NotifyDataChanged(); }

    int GetRowCount() const override { return static_cast<int>(entries.size()); }
    int GetColumnCount() const override { return 4; }
    ListColumnDef GetColumnDef(int column) const override {
        static const ListColumnDef columns[] = {{"Time", 104}, {"Device", 220}, {"Room", 140}, {"What happened", 560}};
        return columns[std::clamp(column, 0, 3)];
    }
    ListDataValue GetData(const ListIndex& index, ListDataRole role) const override {
        if (index.row < 0 || index.row >= static_cast<int>(entries.size())) return {};
        const ActivityEntry& e = entries[static_cast<size_t>(index.row)];
        if (role != ListDataRole::DisplayRole) return {};
        switch (index.column) {
            case 0: return ClockText(e.time);
            case 1: return e.deviceName;
            case 2: return e.room;
            case 3: return e.alert ? "⚠ " + e.text : e.text;
            default: return {};
        }
    }
    bool SetData(const ListIndex&, ListDataRole, const ListDataValue&) override { return false; }

private:
    const std::deque<ActivityEntry>& entries;
};

// ===== LIFECYCLE =====

SmartHomeCenterWindow::~SmartHomeCenterWindow() {
    // The API outlives the window: stop it calling into us first.
    auto& api = SmartHomeAPI::Instance();
    api.SetOnDeviceUpdate(nullptr);
    api.SetOnDeviceStateChange(nullptr);
    api.SetOnDeviceDiscover(nullptr);
    api.SetOnPairingComplete(nullptr);
    api.SetOnSmartHomeError(nullptr);
    api.SetOnEvent(nullptr);
    if (uiTimer_ != 0) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(uiTimer_);
    }
}

bool SmartHomeCenterWindow::Initialize(bool demoHome) {
    demoHome_ = demoHome;
    iconsDir_ = NormalizePath(GetResourcesDir() + "media/icons/smarthome/");

    WindowConfig config;
    config.title = std::string("SmartHome Center ") + SMARTHOMECENTER_VERSION;
    config.width = 1360;
    config.height = 860;
    config.minWidth = 1000;
    config.minHeight = 640;
    config.backgroundColor = kCanvas;
    window_ = CreateWindow(config);
    if (!window_) return false;

    root_ = Row("shcRoot", 0, CSSLayout::AlignItems::Stretch);
    root_->SetBackgroundColor(kCanvas);

    auto sidebar = BuildSidebar();
    root_->AddChild(sidebar);
    Fixed(sidebar, kSidebarWidth, -1);
    sidebar->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    content_ = Column("shcContent", 18);
    content_->SetPadding(24, 28, 24, 28);
    root_->AddChild(content_);
    Grow(content_);

    auto header = BuildHeader();
    content_->AddChild(header);
    Fixed(header, -1, 52);
    header->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    pages_[Page::Home] = BuildHomePage();
    pages_[Page::Devices] = BuildDevicesPage();
    pages_[Page::Energy] = BuildEnergyPage();
    pages_[Page::Activity] = BuildActivityPage();
    for (auto& [page, container] : pages_) {
        content_->AddChild(container);
        Grow(container);
    }

    window_->AddChild(root_);
    LayoutForSize(static_cast<float>(config.width), static_cast<float>(config.height));
    window_->onWindowResize = [this](int width, int height) {
        LayoutForSize(static_cast<float>(width), static_cast<float>(height));
    };

    if (demoHome_) {
        CreateDemoScenes();
        CreateDemoAutomations();
    }
    ConnectApi();
    ShowPage(Page::Home);
    ReloadAll();
    Log("SmartHome Center", "", demoHome_ ? "Opened the demo home - every device here is simulated"
                                          : "Opened the home", false);

    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
        uiTimer_ = app->StartTimer(kUiTimerMs, /*periodic=*/true, [this](TimerId) { OnTimer(); });
    }
    return true;
}

void SmartHomeCenterWindow::Show() {
    if (window_) window_->Show();
}

void SmartHomeCenterWindow::LayoutForSize(float width, float height) {
    if (!root_) return;
    // SetElementSize, not SetBounds: the layout engine sizes from the CSS
    // dimensions, so a bounds-only change is overwritten on the next pass.
    root_->SetElementAbsolutePosition(Point2Df(0, 0));
    root_->SetElementSize(Size2Df(std::max(800.0f, width), std::max(500.0f, height)));
    root_->InvalidateLayout();
    if (window_) window_->AddDirtyRectangle(Rect2Di(0, 0, static_cast<int>(width), static_cast<int>(height)));
}

// ===== SIDEBAR =====

std::shared_ptr<UltraCanvasContainer> SmartHomeCenterWindow::BuildSidebar() {
    auto side = Column("shcSidebar", 4);
    side->SetBackgroundColor(kSurface);
    side->SetBorderRight(1.0f, kBorder);
    side->SetPadding(22, 16, 18, 16);

    // Brand
    auto brand = Row("shcBrand", 12);
    Fixed(brand, -1, 44);
    auto logo = CreateImageElement("shcLogo", 36, 36);
    logo->LoadFromFile(NormalizePath(GetResourcesDir() + "media/appicon/SmartHomeCenter.png"));
    logo->SetFitMode(ImageFitMode::Contain);
    Fixed(logo, 36, 36);
    brand->AddChild(logo);
    auto names = Column("shcBrandNames", 0);
    names->AddChild(Text("shcBrandName", "SmartHome", 16, kInk, true, 20));
    names->AddChild(Text("shcBrandSub", "Center", 12, kMuted, false, 16));
    Fixed(names, 150, 38);
    brand->AddChild(names);
    side->AddChild(brand);

    auto gap = CreateContainer("shcBrandGap");
    Fixed(gap, -1, 18);
    side->AddChild(gap);
    side->AddChild(Text("shcNavCaption", "MENU", 11, kFaint, true, 22));

    const struct { Page page; const char* text; const char* icon; } items[] = {
        {Page::Home, "Home", "home"},
        {Page::Devices, "Devices", "devices"},
        {Page::Energy, "Energy", "energy"},
        {Page::Activity, "Activity", "activity"},
    };
    for (const auto& item : items) {
        auto button = CreateButton(std::string("shcNav") + item.text, 0, 0, 200, 42, item.text);
        button->SetIcon(iconsDir_ + item.icon + ".svg");
        button->SetIconSize(20, 20);
        button->SetIconPosition(ButtonIconPosition::Left);
        button->SetIconSpacing(12);
        button->SetTextAlign(TextAlignment::Left);
        button->SetFontSize(14);
        button->SetCornerRadius(10);
        button->SetBorder(0, Colors::Transparent);
        button->SetPadding(0, 14);
        Fixed(button, -1, 42);
        button->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        const Page page = item.page;
        button->SetOnClick([this, page]() { ShowPage(page); });
        navButtons_[page] = button;
        side->AddChild(button);
    }

    side->AddChild(Spacer("shcSideSpacer"));

    if (demoHome_) {
        auto demo = BuildDemoPanel();
        side->AddChild(demo);
        Fixed(demo, -1, -1);
        demo->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    }

    auto about = CreateButton("shcAbout", 0, 0, 200, 34, "About SmartHome Center");
    about->SetFontSize(12);
    about->SetTextColors(kMuted, kInk);
    about->SetColors(Colors::Transparent, kHover);
    about->SetBorder(0, Colors::Transparent);
    about->SetCornerRadius(8);
    about->SetOnClick([this]() { ShowAbout(); });
    Fixed(about, -1, 34);
    about->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    side->AddChild(about);
    return side;
}

std::shared_ptr<UltraCanvasContainer> SmartHomeCenterWindow::BuildDemoPanel() {
    // The demo home says so, everywhere a person looks for the radio: here,
    // in each device's details and in the log. Its buttons stage the outside
    // world through ordinary SmartHomeAPI commands.
    auto panel = Column("shcDemo", 8);
    panel->SetBackgroundColor(Color(255, 248, 235));
    panel->SetBorders(1.0f, Color(247, 214, 150), 12.0f);
    panel->SetPadding(12);
    panel->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(238));

    auto badge = CreateBadge("shcDemoBadge", 0, 0, "DEMO HOME", BadgeVariant::Warning);
    Fixed(badge, 108, 20);
    panel->AddChild(badge);
    auto note = Text("shcDemoNote", "Simulated devices - nothing here controls real hardware.", 12, Color(122, 86, 20), false, 52);
    note->SetWrap(TextWrap::WrapWord);
    note->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
    panel->AddChild(note);

    auto stage = [this](const std::string& name, const std::string& command, const std::string& what) {
        const std::string id = FindDeviceByName(name);
        if (id.empty()) return;
        SmartHomeAPI::Instance().SendCommand({id, command, {}});
        Log(name, "", what, command == "simulateTrigger" && name.find("leak") != std::string::npos);
        SelectDevice(id);
    };
    const struct { const char* id; const char* text; const char* device; const char* command; const char* what; } actions[] = {
        {"shcDemoMotion", "Motion in the hallway", "Hallway motion", "simulateTrigger", "Simulated: someone walks through"},
        {"shcDemoDoor", "Open the patio door", "Patio door", "simulateTrigger", "Simulated: the patio door opens"},
        {"shcDemoLeak", "Water under the sink", "Under-sink leak sensor", "simulateTrigger", "Simulated: water under the sink"},
    };
    for (const auto& a : actions) {
        auto button = CreateButton(a.id, 0, 0, 180, 32, a.text);
        button->SetFontSize(12);
        button->SetCornerRadius(8);
        button->SetColors(kSurface, Color(255, 241, 214));
        button->SetTextColors(Color(122, 86, 20));
        button->SetBorder(1, Color(247, 214, 150));
        Fixed(button, -1, 32);
        button->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        const std::string device = a.device, command = a.command, what = a.what;
        button->SetOnClick([stage, device, command, what]() { stage(device, command, what); });
        panel->AddChild(button);
    }
    return panel;
}

// ===== HEADER =====

std::shared_ptr<UltraCanvasContainer> SmartHomeCenterWindow::BuildHeader() {
    auto header = Row("shcHeader", 12);
    auto titles = Column("shcTitles", 2);
    pageTitle_ = Text("shcPageTitle", "Home", 24, kInk, true, 30);
    pageSubtitle_ = Text("shcPageSubtitle", "", 13, kMuted, false, 18);
    titles->AddChild(pageTitle_);
    titles->AddChild(pageSubtitle_);
    Grow(titles);
    header->AddChild(titles);

    addButton_ = CreateButton("shcAdd", 0, 0, 150, 40, "Add device");
    addButton_->SetIcon(iconsDir_ + "plus.svg");
    addButton_->SetIconSize(18, 18);
    addButton_->SetIconMaskColor(Colors::White);
    addButton_->SetIconSpacing(8);
    addButton_->SetColors(kAccent, Color(56, 82, 214), Color(46, 68, 190), Color(170, 180, 220));
    addButton_->SetTextColors(Colors::White);
    addButton_->SetFontSize(14);
    addButton_->SetCornerRadius(10);
    addButton_->SetBorder(0, Colors::Transparent);
    addButton_->SetOnClick([this]() { AddDevice(); });
    Fixed(addButton_, 150, 40);
    header->AddChild(addButton_);
    return header;
}

// ===== HOME =====

SmartHomeCenterWindow::Stat SmartHomeCenterWindow::MakeStat(const std::string& id, const std::string& caption,
                                                            const std::string& icon) {
    // The icon and caption share the top line, so the figure below gets the
    // card's whole width: four of these share a row beside the inspector.
    Stat stat;
    stat.card = Column(id, 4);
    Card(stat.card, 14);
    stat.card->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(112));
    Grow(stat.card);
    stat.card->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Auto);

    auto top = Row(id + "Top", 8);
    Fixed(top, -1, 28);
    top->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    stat.badge = Row(id + "Badge", 0);
    stat.badge->layout.SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
    stat.badge->SetBorderRadius(8);
    Fixed(stat.badge, 28, 28);
    stat.icon = CreateImageElement(id + "Icon", 16, 16);
    stat.icon->LoadFromFile(iconsDir_ + icon + ".svg");
    stat.icon->SetFitMode(ImageFitMode::Contain);
    Fixed(stat.icon, 16, 16);
    stat.badge->AddChild(stat.icon);
    top->AddChild(stat.badge);
    auto captionLabel = Text(id + "Caption", caption, 12, kMuted, false, 18);
    Grow(captionLabel);
    captionLabel->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
    top->AddChild(captionLabel);
    stat.card->AddChild(top);

    stat.value = Text(id + "Value", "-", 20, kInk, true, 28);
    stat.card->AddChild(stat.value);
    stat.detail = Text(id + "Detail", "", 12, kMuted, false, 16);
    stat.card->AddChild(stat.detail);
    return stat;
}

std::shared_ptr<UltraCanvasContainer> SmartHomeCenterWindow::BuildScenesRow() {
    auto row = Row("shcScenes", 12);
    const struct { const char* id; const char* name; const char* icon; const char* hint; } scenes[] = {
        {kSceneMorning, "Good morning", "sunrise", "Blinds up, kitchen on, heating to 21 °C"},
        {kSceneEvening, "Evening", "sunset", "Warm living room light, blinds down"},
        {kSceneAway, "Away", "away", "Everything off, doors locked"},
        {kSceneNight, "Good night", "moon", "Lights off, doors locked, heating to 18 °C"},
    };
    for (const auto& s : scenes) {
        auto button = CreateButton(std::string("shcScene_") + s.id, 0, 0, 180, 52, s.name);
        button->SetIcon(iconsDir_ + s.icon + ".svg");
        button->SetIconSize(22, 22);
        button->SetIconMaskColor(kAccent);
        button->SetIconSpacing(10);
        button->SetTextAlign(TextAlignment::Left);
        button->SetFontSize(14);
        button->SetColors(kSurface, kAccentSoft, Color(220, 226, 252), kSurface);
        button->SetTextColors(kInk);
        button->SetBorder(1, kBorder);
        button->SetCornerRadius(12);
        button->SetPadding(0, 16);
        button->SetTooltip(s.hint);
        button->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(52));
        Grow(button);
        button->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Auto);
        const std::string id = s.id, name = s.name;
        button->SetOnClick([this, id, name]() { ActivateScene(id, name); });
        row->AddChild(button);
    }
    return row;
}

std::shared_ptr<UltraCanvasContainer> SmartHomeCenterWindow::BuildHomePage() {
    auto page = Row("shcHomePage", 20, CSSLayout::AlignItems::Stretch);

    // Left: everything that scrolls.
    roomsColumn_ = CreateScrollableContainer("shcRoomsScroll", 0, 0, 0, 0, false, false);
    ContainerStyle scroll = roomsColumn_->GetContainerStyle();
    scroll.autoShowHorizontalScrollbar = false;   // the tiles wrap; only height overflows
    roomsColumn_->SetContainerStyle(scroll);
    roomsColumn_->layout.SetFlexColumn().SetFlexGap(18).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    // Room on the right for the scrollbar, so it never sits on a card.
    roomsColumn_->SetPadding(0, 16, 0, 0);
    Grow(roomsColumn_);
    page->AddChild(roomsColumn_);

    alertBanner_ = Row("shcAlert", 12);
    alertBanner_->SetBackgroundColor(kAlarm.soft);
    alertBanner_->SetBorders(1.0f, kAlarm.border, 12.0f);
    alertBanner_->SetPadding(10, 16);
    Fixed(alertBanner_, -1, 48);
    alertBanner_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto alertIcon = CreateImageElement("shcAlertIcon", 22, 22);
    alertIcon->LoadFromFile(iconsDir_ + "water.svg");
    alertIcon->SetTintColor(kAlarm.fg);
    Fixed(alertIcon, 22, 22);
    alertBanner_->AddChild(alertIcon);
    alertText_ = Text("shcAlertText", "", 14, Color(160, 30, 36), true, 22);
    Grow(alertText_);
    alertText_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
    alertBanner_->AddChild(alertText_);
    alertBanner_->SetVisible(false);
    roomsColumn_->AddChild(alertBanner_);

    auto stats = Row("shcStats", 14, CSSLayout::AlignItems::Stretch);
    Fixed(stats, -1, 112);
    stats->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    climateStat_ = MakeStat("shcStatClimate", "Inside", "thermostat");
    lightsStat_ = MakeStat("shcStatLights", "Lights", "light");
    securityStat_ = MakeStat("shcStatSecurity", "Doors", "lock");
    powerStat_ = MakeStat("shcStatPower", "Power now", "energy");
    for (Stat* s : {&climateStat_, &lightsStat_, &securityStat_, &powerStat_}) stats->AddChild(s->card);
    roomsColumn_->AddChild(stats);

    roomsColumn_->AddChild(Text("shcScenesTitle", "Scenes", 15, kInk, true, 22));
    auto scenes = BuildScenesRow();
    Fixed(scenes, -1, 52);
    scenes->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    roomsColumn_->AddChild(scenes);

    roomsHolder_ = Column("shcRooms", 18);
    roomsHolder_->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    roomsHolder_->layoutItem.SetFlexShrink(0);
    roomsColumn_->AddChild(roomsHolder_);

    // Right: the selected device.
    inspector_ = BuildInspector();
    Fixed(inspector_, kInspectorWidth, -1);
    inspector_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    page->AddChild(inspector_);
    return page;
}

SmartHomeCenterWindow::Tile SmartHomeCenterWindow::MakeTile(const DeviceView& view) {
    const std::string id = view.info.DeviceId;
    Tile tile;
    tile.card = Column("shcTile_" + id, 6);
    Card(tile.card, 14);
    Fixed(tile.card, kTileWidth, kTileHeight);
    tile.card->SetMouseCursor(UCMouseCursor::Hand);

    auto top = Row("shcTileTop_" + id, 8);
    Fixed(top, -1, 40);
    top->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    tile.badge = Row("shcTileBadge_" + id, 0);
    tile.badge->layout.SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
    tile.badge->SetBorderRadius(11);
    Fixed(tile.badge, 40, 40);
    tile.icon = CreateImageElement("shcTileIcon_" + id, 22, 22);
    tile.icon->SetFitMode(ImageFitMode::Contain);
    Fixed(tile.icon, 22, 22);
    tile.badge->AddChild(tile.icon);
    top->AddChild(tile.badge);
    top->AddChild(Spacer("shcTileSpace_" + id));

    if (view.canToggle || (!view.online && (view.kind == DeviceKind::Light || view.kind == DeviceKind::Switch))) {
        tile.toggle = UltraCanvasSwitch::Create("shcTileToggle_" + id, 0, 0, "", view.on);
        tile.toggle->SetTrackSize(42, 24);
        SwitchVisualStyle style = tile.toggle->GetVisualStyle();
        style.trackOnColor = kAccent;
        style.trackOffColor = Color(214, 219, 230);
        style.trackBorderColor = Colors::Transparent;
        style.thumbBorderColor = Colors::Transparent;
        tile.toggle->SetVisualStyle(style);
        Fixed(tile.toggle, 44, 26);
        // The switch is owned by the tile, which the window owns: `this` is
        // the only capture, and the id is a copy.
        tile.toggle->onStateChanged = [this, id](CheckedState, CheckedState now) {
            if (updating_) return;
            ToggleDevice(id, now == CheckedState::Checked);
        };
        top->AddChild(tile.toggle);
    } else if (!view.value.empty()) {
        tile.value = Text("shcTileValue_" + id, view.value, 18, kInk, true, 26);
        tile.value->SetAlignment(TextAlignment::Right);
        Fixed(tile.value, 100, 26);
        top->AddChild(tile.value);
    }
    tile.card->AddChild(top);

    tile.name = Text("shcTileName_" + id, view.info.Name, 14, kInk, true, 20);
    tile.card->AddChild(tile.name);
    tile.state = Text("shcTileState_" + id, view.state, 12, kMuted, false, 18);
    tile.card->AddChild(tile.state);

    // A press anywhere on the card that its switch did not take selects the
    // device: presses travel up from the element under the pointer.
    tile.card->SetEventCallback([this, id](const UCEvent& event) {
        if (event.type != UCEventType::MouseDown) return false;
        SelectDevice(id);
        return true;
    });
    UpdateTile(tile, view);
    return tile;
}

void SmartHomeCenterWindow::UpdateTile(Tile& tile, const DeviceView& view) {
    const Tone tone = ToneOf(view);
    const bool selected = view.info.DeviceId == selectedId_;
    tile.card->SetBackgroundColor(view.active || view.alert ? tone.soft : kSurface);
    tile.card->SetBorders(selected ? 2.0f : 1.0f, selected ? kAccent : (view.active || view.alert ? tone.border : kBorder), 14.0f);
    tile.badge->SetBackgroundColor(view.active || view.alert ? kSurface : tone.soft);
    if (tile.iconName != view.icon) {
        tile.icon->LoadFromFile(iconsDir_ + view.icon + ".svg");
        tile.iconName = view.icon;
    }
    tile.icon->SetTintColor(view.online ? tone.fg : kFaint);
    tile.name->SetText(view.info.Name);
    tile.name->SetTextColor(view.online ? kInk : kMuted);
    tile.state->SetText(view.state);
    tile.state->SetTextColor(view.alert ? kAlarm.fg : kMuted);
    if (tile.value) tile.value->SetText(view.value);
    if (tile.toggle) {
        updating_ = true;
        tile.toggle->SetChecked(view.on);
        updating_ = false;
        tile.toggle->SetDisabled(!view.online);
    }
    tile.card->RequestRedraw();
}

void SmartHomeCenterWindow::RebuildRooms() {
    roomsHolder_->ClearChildren();
    tiles_.clear();
    roomCounts_.clear();
    int roomIndex = 0;
    for (const std::string& room : RoomsOf(views_)) {
        const std::string key = std::to_string(roomIndex++);
        auto section = Column("shcRoom_" + key, 10);
        section->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
        section->layoutItem.SetFlexShrink(0);

        std::vector<const DeviceView*> members;
        for (const DeviceView& v : views_) {
            if ((v.info.Room.empty() ? std::string(kNoRoom) : v.info.Room) == room) members.push_back(&v);
        }

        auto heading = Row("shcRoomHead_" + key, 10);
        Fixed(heading, -1, 24);
        heading->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        // The count sits right after the name: the far edge of the column is
        // where the scrollbar goes.
        auto title = Text("shcRoomTitle_" + key, room, 16, kInk, true, 24);
        heading->AddChild(title);
        auto countLabel = Text("shcRoomCount_" + key, "", 12, kMuted, false, 20);
        Fixed(countLabel, 200, 20);
        countLabel->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        heading->AddChild(countLabel);
        roomCounts_.emplace_back(room, countLabel);
        section->AddChild(heading);

        auto grid = CreateContainer("shcRoomGrid_" + key);
        grid->layout.SetFlex(CSSLayout::FlexDirection::Row, CSSLayout::FlexWrap::Wrap).SetFlexGap(12);
        NoScrollbars(grid);
        grid->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
        grid->layoutItem.SetFlexShrink(0);
        for (const DeviceView* v : members) {
            Tile tile = MakeTile(*v);
            grid->AddChild(tile.card);
            tiles_[v->info.DeviceId] = tile;
        }
        section->AddChild(grid);
        roomsHolder_->AddChild(section);
    }
    UpdateRoomCounts();
    roomsHolder_->InvalidateLayout();
    roomsColumn_->InvalidateLayout();
}

void SmartHomeCenterWindow::UpdateRoomCounts() {
    for (auto& [room, label] : roomCounts_) {
        int count = 0, active = 0;
        for (const DeviceView& v : views_) {
            if ((v.info.Room.empty() ? std::string(kNoRoom) : v.info.Room) != room) continue;
            ++count;
            if (v.active) ++active;
        }
        std::string text = std::to_string(count) + (count == 1 ? " device" : " devices");
        if (active > 0) text += " · " + std::to_string(active) + " active";
        label->SetText(text);
    }
}

// ===== INSPECTOR =====

std::shared_ptr<UltraCanvasContainer> SmartHomeCenterWindow::BuildInspector() {
    auto panel = Column("shcInspector", 14);
    Card(panel, 20);

    auto head = Row("shcInspHead", 14);
    Fixed(head, -1, 56);
    head->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    inspectorBadge_ = Row("shcInspBadge", 0);
    inspectorBadge_->layout.SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
    inspectorBadge_->SetBorderRadius(14);
    Fixed(inspectorBadge_, 54, 54);
    inspectorIcon_ = CreateImageElement("shcInspIcon", 28, 28);
    inspectorIcon_->SetFitMode(ImageFitMode::Contain);
    Fixed(inspectorIcon_, 28, 28);
    inspectorBadge_->AddChild(inspectorIcon_);
    head->AddChild(inspectorBadge_);
    auto names = Column("shcInspNames", 2);
    inspectorName_ = Text("shcInspName", "", 17, kInk, true, 24);
    inspectorType_ = Text("shcInspType", "", 12, kMuted, false, 18);
    names->AddChild(inspectorName_);
    names->AddChild(inspectorType_);
    Grow(names);
    head->AddChild(names);
    panel->AddChild(head);

    inspectorStatus_ = CreateBadge("shcInspStatus", 0, 0, "", BadgeVariant::Neutral);
    Fixed(inspectorStatus_, -1, 20);
    inspectorStatus_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Start);
    panel->AddChild(inspectorStatus_);

    inspectorBody_ = Column("shcInspBody", 12);
    inspectorBody_->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    inspectorBody_->layoutItem.SetFlexShrink(0);
    panel->AddChild(inspectorBody_);
    return panel;
}

const DeviceView* SmartHomeCenterWindow::Selected() const {
    for (const DeviceView& v : views_) {
        if (v.info.DeviceId == selectedId_) return &v;
    }
    return nullptr;
}

void SmartHomeCenterWindow::RebuildInspector() {
    inspectorBody_->ClearChildren();
    inspectorToggle_.reset();
    inspectorLevel_.reset();
    inspectorWarmth_.reset();
    inspectorMode_.reset();
    inspectorTarget_.reset();
    inspectorAction_.reset();
    inspectorValue_.reset();
    inspectorFacts_.clear();

    const DeviceView* v = Selected();
    if (!v) {
        inspectorBuiltFor_.clear();
        inspectorName_->SetText("Nothing selected");
        inspectorType_->SetText("Choose a device to see its controls");
        inspectorStatus_->SetVisible(false);
        inspectorBadge_->SetBackgroundColor(kIdle.soft);
        inspectorIcon_->LoadFromFile(iconsDir_ + "devices.svg");
        inspectorIcon_->SetTintColor(kIdle.fg);
        return;
    }
    inspectorStatus_->SetVisible(true);
    inspectorBuiltFor_ = v->info.DeviceId + "/" + std::to_string(static_cast<int>(v->kind)) + "/" + (v->online ? "1" : "0");
    const std::string id = v->info.DeviceId;
    auto& api = SmartHomeAPI::Instance();

    auto section = [&](const std::string& key, const std::string& caption) {
        inspectorBody_->AddChild(Text("shcInspCap_" + key, caption, 11, kFaint, true, 18));
    };
    auto controlRow = [&](const std::string& key, const std::string& caption) {
        auto row = Row("shcInspRow_" + key, 10);
        Fixed(row, -1, 32);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        auto label = Text("shcInspRowLabel_" + key, caption, 14, kInk, false, 22);
        Grow(label);
        label->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        row->AddChild(label);
        inspectorBody_->AddChild(row);
        return row;
    };
    auto bigValue = [&](const std::string& text) {
        inspectorValue_ = Text("shcInspValue", text, 34, kInk, true, 46);
        inspectorBody_->AddChild(inspectorValue_);
    };
    auto slider = [&](const std::string& key, float min, float max) {
        auto s = CreateHorizontalSlider("shcInspSlider_" + key, 0, 0, 280, 28, min, max);
        s->SetColors(Color(226, 230, 239), kAccent, kSurface);
        s->SetTrackHeight(6);
        s->SetHandleSize(20);
        s->SetValueDisplay(SliderValueDisplay::NoDisplay);
        Fixed(s, -1, 28);
        s->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        inspectorBody_->AddChild(s);
        return s;
    };
    auto actionButton = [&](const std::string& key, const std::string& text, bool primary) {
        auto b = CreateButton("shcInspAction_" + key, 0, 0, 120, 40, text);
        b->SetFontSize(14);
        b->SetCornerRadius(10);
        if (primary) {
            b->SetColors(kAccent, Color(56, 82, 214), Color(46, 68, 190), Color(170, 180, 220));
            b->SetTextColors(Colors::White);
            b->SetBorder(0, Colors::Transparent);
        } else {
            b->SetColors(kSurface, kHover, Color(228, 232, 240), kSurface);
            b->SetTextColors(kInk);
            b->SetBorder(1, kBorder);
        }
        b->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(40));
        return b;
    };

    section("controls", "CONTROLS");
    if (!v->online) {
        auto note = Text("shcInspOffline", "This device is not answering. Check its power, or that it is in range.", 13, kAlarm.fg, false, 40);
        note->SetWrap(TextWrap::WrapWord);
        inspectorBody_->AddChild(note);
    } else {
        switch (v->kind) {
            case DeviceKind::Light: {
                auto row = controlRow("power", "Power");
                inspectorToggle_ = UltraCanvasSwitch::Create("shcInspToggle", 0, 0, "", v->on);
                inspectorToggle_->SetTrackSize(46, 26);
                SwitchVisualStyle style = inspectorToggle_->GetVisualStyle();
                style.trackOnColor = kAccent;
                style.trackOffColor = Color(214, 219, 230);
                style.trackBorderColor = Colors::Transparent;
                style.thumbBorderColor = Colors::Transparent;
                inspectorToggle_->SetVisualStyle(style);
                Fixed(inspectorToggle_, 48, 28);
                inspectorToggle_->onStateChanged = [this, id](CheckedState, CheckedState now) {
                    if (!updating_) ToggleDevice(id, now == CheckedState::Checked);
                };
                row->AddChild(inspectorToggle_);
                const SmartHomeLightState light = api.GetLightState(id);
                if (light.Capabilities & static_cast<uint32_t>(SmartHomeLightCapability::Brightness)) {
                    controlRow("brightness", "Brightness");
                    inspectorLevel_ = slider("brightness", 1, 100);
                    inspectorLevel_->onValueChanged = [this, id](float value) {
                        if (updating_) return;
                        SmartHomeLightState state;
                        state.On = true;
                        state.Brightness = static_cast<uint8_t>(std::lround(value * 255.0f / 100.0f));
                        SmartHomeAPI::Instance().SetLightState(id, state);
                    };
                }
                if (light.Capabilities & static_cast<uint32_t>(SmartHomeLightCapability::ColorTemp)) {
                    controlRow("warmth", "Colour temperature");
                    inspectorWarmth_ = slider("warmth", 2200, 6500);
                    inspectorWarmth_->SetTrackGradient({GradientStop(0.0, Color(255, 176, 80)),
                                                        GradientStop(0.5, Color(255, 236, 210)),
                                                        GradientStop(1.0, Color(200, 222, 255))});
                    inspectorWarmth_->onValueChanged = [this, id](float value) {
                        if (updating_) return;
                        SmartHomeLightState state = SmartHomeAPI::Instance().GetLightState(id);
                        state.On = true;
                        state.ColorTemp = static_cast<uint16_t>(std::lround(value));
                        state.Red = state.Green = state.Blue = 0;   // "unchanged"
                        SmartHomeAPI::Instance().SetLightState(id, state);
                    };
                }
                break;
            }
            case DeviceKind::Switch: {
                if (v->metered) bigValue(FormatPower(v->power));
                auto row = controlRow("power", "Power");
                inspectorToggle_ = UltraCanvasSwitch::Create("shcInspToggle", 0, 0, "", v->on);
                inspectorToggle_->SetTrackSize(46, 26);
                SwitchVisualStyle style = inspectorToggle_->GetVisualStyle();
                style.trackOnColor = kAccent;
                style.trackOffColor = Color(214, 219, 230);
                style.trackBorderColor = Colors::Transparent;
                style.thumbBorderColor = Colors::Transparent;
                inspectorToggle_->SetVisualStyle(style);
                Fixed(inspectorToggle_, 48, 28);
                inspectorToggle_->onStateChanged = [this, id](CheckedState, CheckedState now) {
                    if (!updating_) ToggleDevice(id, now == CheckedState::Checked);
                };
                row->AddChild(inspectorToggle_);
                break;
            }
            case DeviceKind::Thermostat: {
                const SmartHomeThermostatState t = api.GetThermostatState(id);
                bigValue(FormatTemperature(t.CurrentTemperature));
                pendingTarget_ = t.TargetTemperature;
                auto row = controlRow("target", "Target");
                auto minus = actionButton("minus", "−", false);
                Fixed(minus, 40, 36);
                inspectorTarget_ = Text("shcInspTarget", FormatTemperature(t.TargetTemperature), 16, kInk, true, 24);
                inspectorTarget_->SetAlignment(TextAlignment::Center);
                Fixed(inspectorTarget_, 86, 24);
                auto plus = actionButton("plus", "+", false);
                Fixed(plus, 40, 36);
                auto step = [this, id](float delta) {
                    pendingTarget_ = std::clamp(pendingTarget_ + delta, 10.0f, 30.0f);
                    if (inspectorTarget_) inspectorTarget_->SetText(FormatTemperature(pendingTarget_));
                    SmartHomeAPI::Instance().SetThermostatTarget(id, pendingTarget_);
                };
                minus->SetOnClick([step]() { step(-0.5f); });
                plus->SetOnClick([step]() { step(0.5f); });
                row->AddChild(minus);
                row->AddChild(inspectorTarget_);
                row->AddChild(plus);

                inspectorMode_ = CreateSegmentedControl("shcInspMode", 0, 0, 280, 34);
                for (const char* m : {"Heat", "Auto", "Off"}) inspectorMode_->AddSegment(m);
                Fixed(inspectorMode_, -1, 34);
                inspectorMode_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
                inspectorMode_->onSegmentSelected = [this, id](int index) {
                    if (updating_) return;
                    static const char* modes[] = {"heat", "auto", "off"};
                    if (index >= 0 && index < 3) SmartHomeAPI::Instance().SetThermostatMode(id, modes[index]);
                };
                inspectorBody_->AddChild(inspectorMode_);
                break;
            }
            case DeviceKind::Lock: {
                const bool locked = !v->active;
                inspectorAction_ = actionButton("lock", locked ? "Unlock" : "Lock", true);
                inspectorAction_->SetIcon(iconsDir_ + (locked ? "unlock.svg" : "lock.svg"));
                inspectorAction_->SetIconSize(18, 18);
                inspectorAction_->SetIconMaskColor(Colors::White);
                inspectorAction_->SetIconSpacing(8);
                inspectorAction_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
                inspectorAction_->SetOnClick([this, id]() {
                    const bool nowLocked = SmartHomeAPI::Instance().GetLockState(id).Locked;
                    SmartHomeAPI::Instance().SetLockState(id, !nowLocked);
                });
                inspectorBody_->AddChild(inspectorAction_);
                break;
            }
            case DeviceKind::Blind: {
                bigValue(v->value);
                controlRow("position", "Position");
                inspectorLevel_ = slider("position", 0, 100);
                inspectorLevel_->onValueChanged = [this, id](float value) {
                    if (!updating_) SmartHomeAPI::Instance().SetBlindPosition(id, static_cast<uint8_t>(std::lround(value)));
                };
                auto buttons = Row("shcInspBlindButtons", 10, CSSLayout::AlignItems::Stretch);
                Fixed(buttons, -1, 40);
                buttons->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
                auto open = actionButton("open", "Open", false);
                auto close = actionButton("close", "Close", false);
                Grow(open);
                Grow(close);
                open->SetOnClick([id]() { SmartHomeAPI::Instance().SetBlindPosition(id, 100); });
                close->SetOnClick([id]() { SmartHomeAPI::Instance().SetBlindPosition(id, 0); });
                buttons->AddChild(open);
                buttons->AddChild(close);
                inspectorBody_->AddChild(buttons);
                break;
            }
            case DeviceKind::ClimateSensor:
                bigValue(v->value);
                break;
            case DeviceKind::BinarySensor: {
                bigValue(v->state);
                inspectorValue_->SetFontSize(24);
                if (demoHome_) {
                    auto buttons = Row("shcInspSimButtons", 10, CSSLayout::AlignItems::Stretch);
                    Fixed(buttons, -1, 40);
                    buttons->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
                    auto trigger = actionButton("trigger", "Simulate event", false);
                    auto clear = actionButton("clear", "Clear", false);
                    Grow(trigger);
                    Grow(clear);
                    trigger->SetOnClick([id]() { SmartHomeAPI::Instance().SendCommand({id, "simulateTrigger", {}}); });
                    clear->SetOnClick([id]() { SmartHomeAPI::Instance().SendCommand({id, "simulateClear", {}}); });
                    buttons->AddChild(trigger);
                    buttons->AddChild(clear);
                    inspectorBody_->AddChild(buttons);
                }
                break;
            }
            default:
                break;
        }
    }

    section("details", "DETAILS");
    for (const char* fact : {"Room", "Type", "Model", "Made by", "Protocol", "Security", "Battery", "Last seen"}) {
        auto row = Row(std::string("shcInspFact_") + fact, 8);
        Fixed(row, -1, 22);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        auto key = Text(std::string("shcInspFactKey_") + fact, fact, 13, kMuted, false, 20);
        Fixed(key, 96, 20);
        row->AddChild(key);
        auto value = Text(std::string("shcInspFactValue_") + fact, "", 13, kInk, false, 20);
        Grow(value);
        row->AddChild(value);
        inspectorBody_->AddChild(row);
        inspectorFacts_.emplace_back(fact, value);
    }

    if (demoHome_) {
        auto offline = actionButton("offline", v->online ? "Simulate: take offline" : "Simulate: bring back online", false);
        offline->SetFontSize(12);
        offline->SetTextColors(kMuted, kInk);
        offline->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(32));
        offline->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        const bool online = v->online;
        offline->SetOnClick([id, online]() {
            SmartHomeAPI::Instance().SendCommand({id, online ? "simulateOffline" : "simulateOnline", {}});
        });
        inspectorBody_->AddChild(offline);
    }
    inspectorBody_->InvalidateLayout();
    UpdateInspector();
}

void SmartHomeCenterWindow::UpdateInspector() {
    const DeviceView* v = Selected();
    if (!v) return;
    const std::string builtFor = v->info.DeviceId + "/" + std::to_string(static_cast<int>(v->kind)) + "/" + (v->online ? "1" : "0");
    if (builtFor != inspectorBuiltFor_) {
        RebuildInspector();   // a device that went offline gets a different body
        return;
    }
    const Tone tone = ToneOf(*v);
    inspectorBadge_->SetBackgroundColor(tone.soft);
    inspectorIcon_->LoadFromFile(iconsDir_ + v->icon + ".svg");
    inspectorIcon_->SetTintColor(v->online ? tone.fg : kFaint);
    inspectorName_->SetText(v->info.Name);
    inspectorType_->SetText(v->typeName + " · " + (v->info.Room.empty() ? std::string(kNoRoom) : v->info.Room));
    inspectorStatus_->SetText(v->state);
    // The pill takes the tile's colour: amber for light, heat and power,
    // green for open, blue for a sensor reporting, red for an alarm.
    BadgeVariant variant = BadgeVariant::Neutral;
    if (v->alert) variant = BadgeVariant::Danger;
    else if (v->active && (v->kind == DeviceKind::Light || v->kind == DeviceKind::Switch ||
                           v->kind == DeviceKind::Thermostat)) variant = BadgeVariant::Warning;
    else if (v->active && v->kind == DeviceKind::BinarySensor) variant = BadgeVariant::Info;
    else if (v->active) variant = BadgeVariant::Successful;
    inspectorStatus_->SetVariant(variant);

    auto& api = SmartHomeAPI::Instance();
    const std::string& id = v->info.DeviceId;
    updating_ = true;
    if (inspectorToggle_) inspectorToggle_->SetChecked(v->on);
    if (v->kind == DeviceKind::Light) {
        const SmartHomeLightState light = api.GetLightState(id);
        if (inspectorLevel_ && !inspectorLevel_->IsDragging())
            inspectorLevel_->SetValue(std::max(1.0f, std::round(light.Brightness * 100.0f / 255.0f)));
        if (inspectorWarmth_ && !inspectorWarmth_->IsDragging() && light.ColorTemp > 0)
            inspectorWarmth_->SetValue(static_cast<float>(light.ColorTemp));
    }
    if (v->kind == DeviceKind::Switch && inspectorValue_) inspectorValue_->SetText(FormatPower(v->power));
    if (v->kind == DeviceKind::Thermostat) {
        const SmartHomeThermostatState t = api.GetThermostatState(id);
        if (inspectorValue_) inspectorValue_->SetText(FormatTemperature(t.CurrentTemperature));
        if (inspectorTarget_) inspectorTarget_->SetText(FormatTemperature(t.TargetTemperature));
        pendingTarget_ = t.TargetTemperature;
        if (inspectorMode_) inspectorMode_->SetSelectedIndex(t.Mode == "auto" ? 1 : t.Mode == "off" ? 2 : 0);
    }
    if (v->kind == DeviceKind::Lock && inspectorAction_) {
        const bool locked = !v->active;
        inspectorAction_->SetText(locked ? "Unlock" : "Lock");
        inspectorAction_->SetIcon(iconsDir_ + (locked ? "unlock.svg" : "lock.svg"));
    }
    if (v->kind == DeviceKind::Blind) {
        if (inspectorValue_) inspectorValue_->SetText(v->value);
        if (inspectorLevel_ && !inspectorLevel_->IsDragging())
            inspectorLevel_->SetValue(static_cast<float>(api.GetBlindPosition(id)));
    }
    if ((v->kind == DeviceKind::ClimateSensor) && inspectorValue_) inspectorValue_->SetText(v->value);
    if (v->kind == DeviceKind::BinarySensor && inspectorValue_) {
        inspectorValue_->SetText(v->state);
        inspectorValue_->SetTextColor(v->alert ? kAlarm.fg : v->active ? kSense.fg : kInk);
    }
    updating_ = false;

    for (auto& [fact, label] : inspectorFacts_) {
        std::string text;
        if (fact == "Room") text = v->info.Room.empty() ? std::string(kNoRoom) : v->info.Room;
        else if (fact == "Type") text = v->typeName;
        else if (fact == "Model") text = v->info.Model;
        else if (fact == "Made by") text = v->info.Manufacturer;
        else if (fact == "Protocol") text = v->info.Protocol == SmartHomeProtocolType::Simulated
                                          ? "Simulated (demo home)" : ProtocolTypeToString(v->info.Protocol);
        else if (fact == "Security") text = SecurityText(v->info.Security);
        else if (fact == "Battery") text = v->battery >= 0 ? FormatPercent(v->battery) : "Mains powered";
        else if (fact == "Last seen") text = AgoText(v->info.LastSeen);
        label->SetText(text);
        label->SetTextColor(fact == "Battery" && v->battery >= 0 && v->battery < 15 ? kAlarm.fg : kInk);
    }
    inspector_->RequestRedraw();
}

void SmartHomeCenterWindow::SelectDevice(const std::string& deviceId) {
    const std::string previous = selectedId_;
    selectedId_ = deviceId;
    for (const std::string& id : {previous, deviceId}) {
        auto tile = tiles_.find(id);
        if (tile == tiles_.end()) continue;
        for (const DeviceView& v : views_) {
            if (v.info.DeviceId == id) UpdateTile(tile->second, v);
        }
    }
    RebuildInspector();
}

std::string SmartHomeCenterWindow::FindDeviceByName(const std::string& name) const {
    for (const DeviceView& v : views_) {
        if (v.info.Name == name) return v.info.DeviceId;
    }
    return "";
}

// ===== DEVICES =====

namespace {
// Both tables: the window's ink and type sizes, a quiet header, hover rows.
void StyleTable(const std::shared_ptr<UltraCanvasListView>& list) {
    ListViewStyle style;
    style.headerBackgroundColor = Color(247, 248, 251);
    style.headerTextColor = kMuted;
    style.headerFontSize = 11;
    style.headerHeight = 34;
    style.gridLineColor = Color(238, 240, 245);
    style.showGridLines = false;
    style.alternateRowColors = true;
    style.alternateRowColor = Color(250, 251, 253);
    style.selectionBackgroundColor = kAccentSoft;
    style.hoverBackgroundColor = Color(244, 246, 250);
    style.showHeader = true;
    list->SetStyle(style);
    list->SetBorders(0.0f, Colors::Transparent);   // the card around it is the frame
    if (auto* delegate = dynamic_cast<UltraCanvasDefaultListDelegate*>(list->GetDelegate())) {
        delegate->SetTextColor(kInk);
        delegate->SetSelectedTextColor(kAccent);
        delegate->SetFontSize(13);
        delegate->SetTextPadding(12);
    }
}
} // namespace

std::shared_ptr<UltraCanvasContainer> SmartHomeCenterWindow::BuildDevicesPage() {
    auto page = Column("shcDevicesPage", 12);
    Card(page, 8);
    devicesModel_ = std::make_shared<DevicesTableModel>();
    devicesList_ = std::make_shared<UltraCanvasListView>("shcDevicesList", 0, 0, 800, 500);
    devicesList_->SetModel(devicesModel_);
    StyleTable(devicesList_);
    devicesList_->SetShowHeader(true);
    devicesList_->SetRowHeight(34);
    devicesList_->onItemActivated = [this](int row) {
        if (const DeviceView* v = devicesModel_->At(row)) {
            ShowPage(Page::Home);
            SelectDevice(v->info.DeviceId);
        }
    };
    Grow(devicesList_);
    page->AddChild(devicesList_);
    return page;
}

void SmartHomeCenterWindow::UpdateDevicesTable() {
    if (devicesModel_) devicesModel_->Set(views_);
}

// ===== ENERGY =====

namespace {
constexpr const char* kEnergyNote =
    "Metered plugs, live, in watts, biggest first. Lights and heating are not metered in this home.";
}

std::shared_ptr<UltraCanvasContainer> SmartHomeCenterWindow::BuildEnergyPage() {
    auto page = Column("shcEnergyPage", 18);
    auto stats = Row("shcEnergyStats", 14, CSSLayout::AlignItems::Stretch);
    Fixed(stats, -1, 112);
    stats->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    energyNowStat_ = MakeStat("shcEnergyNow", "Drawing now", "energy");
    energyTotalStat_ = MakeStat("shcEnergyTotal", "Metered so far", "power");
    energyTopStat_ = MakeStat("shcEnergyTop", "Biggest consumer", "plug");
    for (Stat* s : {&energyNowStat_, &energyTotalStat_, &energyTopStat_}) stats->AddChild(s->card);
    page->AddChild(stats);

    auto card = Column("shcEnergyCard", 8);
    Card(card, 16);
    Grow(card);
    card->AddChild(Text("shcEnergyCardTitle", "Power by device", 15, kInk, true, 22));
    energyNoteText_ = Text("shcEnergyCardNote", kEnergyNote, 12, kMuted, false, 18);
    card->AddChild(energyNoteText_);

    // The framework's bar chart, run sideways: one row per plug drawing
    // power, biggest at the top, its name on the left, watts along the
    // bottom at whole steps.
    energyData_ = std::make_shared<ChartDataVector>();
    energyChart_ = CreateBarChartElement("shcEnergyChart", 0, 0, 700, 360);
    energyChart_->SetOrientation(BarChartOrientation::Horizontal);
    energyChart_->SetXAxisLabelMode(XAxisLabelMode::DataLabel);
    energyChart_->SetBarColor(Color(245, 165, 36));
    energyChart_->SetBarBorderWidth(0);
    energyChart_->SetBarSpacing(0.35);
    energyChart_->SetShowValueLabels(true);
    energyChart_->SetBackgroundColor(kSurface);
    energyChart_->SetPlotAreaColor(kSurface);
    energyChart_->SetGridColor(Color(236, 239, 245));
    energyChart_->SetSeriesName("Power");
    energyChart_->SetEmptyMessageColor(kMuted);
    energyChart_->SetCustomTooltipGenerator([](const ChartDataPoint& point, size_t) {
        return point.label + ": " + FormatPower(static_cast<float>(point.y)) +
               ", " + FormatEnergy(static_cast<float>(point.value)) + " metered";
    });
    energyChart_->SetDataSource(energyData_);
    Grow(energyChart_);
    card->AddChild(energyChart_);

    page->AddChild(card);
    return page;
}

void SmartHomeCenterWindow::UpdateEnergy() {
    float now = 0.0f, total = 0.0f, topWatts = -1.0f;
    std::string topName, key;
    size_t metered = 0;
    // The plugs drawing something, biggest first; an idle plug (under half
    // a watt) has no bar.
    struct Bar { double watts; float energy; const SmartHomeDeviceInfo* info; };
    std::vector<Bar> bars;
    for (const DeviceView& v : views_) {
        if (!v.metered) continue;
        ++metered;
        now += v.power;
        total += v.energy;
        if (v.power > topWatts) { topWatts = v.power; topName = v.info.Name; }
        // Whole watts: the chart is redrawn only when a bar would move.
        const double watts = std::round(v.power);
        if (watts >= 1.0) bars.push_back({watts, v.energy, &v.info});
    }
    std::stable_sort(bars.begin(), bars.end(), [](const Bar& a, const Bar& b) { return a.watts > b.watts; });
    std::vector<ChartDataPoint> points;
    for (const Bar& b : bars) {
        key += b.info->DeviceId + "=" + std::to_string(static_cast<long>(b.watts)) + ";";
        points.emplace_back(static_cast<double>(points.size()), b.watts, 0.0, b.info->Name, b.energy);
    }
    if (key != energyChartKey_) {
        energyChartKey_ = key;
        energyData_->LoadFromArray(points);
        energyChart_->SetDataSource(energyData_);
    }
    const size_t idle = metered - bars.size();
    // With no bars the chart says why, in place of bare axes.
    const bool allIdle = metered > 0 && bars.empty();
    if (metered == 0) {
        energyChart_->SetEmptyMessage("No metered plugs", "Plugs that measure power show up here.");
    } else {
        energyChart_->SetEmptyMessage("Nothing is drawing power",
                                      metered == 1 ? std::string("The metered plug is idle.")
                                                   : "All " + std::to_string(metered) + " metered plugs are idle.");
    }
    energyNoteText_->SetText(idle == 0 || allIdle ? std::string(kEnergyNote)
                             : std::string(kEnergyNote) + " " + std::to_string(idle) +
                                   (idle == 1 ? " idle plug is" : " idle plugs are") + " not shown.");
    energyNowStat_.value->SetText(FormatPower(now));
    energyNowStat_.detail->SetText(std::to_string(metered) + " metered plugs");
    energyTotalStat_.value->SetText(FormatEnergy(total));
    energyTotalStat_.detail->SetText("since each plug started metering");
    energyTopStat_.value->SetText(topName.empty() || allIdle ? "-" : topName);
    energyTopStat_.detail->SetText(!allIdle && topWatts > 0 ? FormatPower(topWatts) + " now" : "");
    for (Stat* s : {&energyNowStat_, &energyTotalStat_, &energyTopStat_}) {
        s->badge->SetBackgroundColor(kLit.soft);
        s->icon->SetTintColor(kLit.fg);
    }
}

// ===== ACTIVITY =====

std::shared_ptr<UltraCanvasContainer> SmartHomeCenterWindow::BuildActivityPage() {
    auto page = Column("shcActivityPage", 12);
    Card(page, 8);
    activityModel_ = std::make_shared<ActivityTableModel>(activity_);
    activityList_ = std::make_shared<UltraCanvasListView>("shcActivityList", 0, 0, 800, 500);
    activityList_->SetModel(activityModel_);
    StyleTable(activityList_);
    activityList_->SetShowHeader(true);
    activityList_->SetRowHeight(32);
    Grow(activityList_);
    page->AddChild(activityList_);
    return page;
}

void SmartHomeCenterWindow::Log(const std::string& device, const std::string& room, const std::string& text, bool alert) {
    activity_.push_front({NowSeconds(), device, room, text, alert});
    if (activity_.size() > kActivityLimit) activity_.pop_back();
    if (activityModel_) activityModel_->Changed();
}

void SmartHomeCenterWindow::NoteEvent(const DeviceView& view, bool initial) {
    auto& last = eventKeys_[view.info.DeviceId];
    if (last == view.eventKey) return;
    const bool first = last.empty();
    last = view.eventKey;
    if (initial || first) return;   // the state it was in when we arrived is not news
    Log(view.info.Name, view.info.Room, view.state, view.alert);
}

// ===== DATA FLOW =====

void SmartHomeCenterWindow::ConnectApi() {
    // These run on the API's threads. They only note what changed; OnTimer
    // reads it back on the UI thread.
    auto& api = SmartHomeAPI::Instance();
    api.SetOnDeviceUpdate([this](const std::string& id) {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingIds_.insert(id);
    });
    api.SetOnDeviceStateChange([this](const std::string& id, SmartHomeDeviceState) {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingIds_.insert(id);
    });
    api.SetOnDeviceDiscover([this](const SmartHomeDeviceInfo&) {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingAll_ = true;
    });
    api.SetOnPairingComplete([this](bool success, const SmartHomeDeviceInfo& device) {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingAll_ = true;
        pendingNotes_.push_back(success ? "Added " + device.Name + " - give it a room in its details"
                                        : "Pairing found nothing");
    });
    api.SetOnSmartHomeError([this](int, const std::string& message) {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingNotes_.push_back("Error: " + message);
    });
    // The engine's own log: automations that ran and why, their notices,
    // scenes, problems. Device changes the window logs itself.
    api.SetOnEvent([this](const SmartHomeEvent& event) {
        if (event.Kind == "device") return;
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingEvents_.push_back(event);
    });
}

void SmartHomeCenterWindow::OnTimer() {
    std::set<std::string> ids;
    std::vector<std::string> notes;
    std::vector<SmartHomeEvent> events;
    bool all = false;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        ids.swap(pendingIds_);
        notes.swap(pendingNotes_);
        events.swap(pendingEvents_);
        all = pendingAll_;
        pendingAll_ = false;
    }
    for (const std::string& note : notes) Log("SmartHome Center", "", note, note.rfind("Error", 0) == 0);
    for (const SmartHomeEvent& event : events) {
        const std::string who = event.Kind == "scene" ? "Scene" : event.Kind == "error" ? "SmartHome" : "Automation";
        Log(who, "", event.Text, event.Kind == "error" || event.Text.rfind("Notice:", 0) == 0);
    }
    if (all) {
        ReloadAll();
    } else if (!ids.empty()) {
        ApplyChanged(ids);
    }
    // Once a second, the things that change with time alone: "last seen",
    // the subtitle, the pairing button.
    if (++ticks_ % 4 == 0) {
        if (addButton_) {
            const bool pairing = SmartHomeAPI::Instance().IsPairing();
            addButton_->SetText(pairing ? "Searching…" : "Add device");
            addButton_->SetDisabled(pairing);
        }
        if (page_ == Page::Devices) UpdateDevicesTable();
        if (page_ == Page::Home) UpdateInspector();
    }
}

void SmartHomeCenterWindow::ReloadAll() {
    views_ = DescribeHome();
    std::string signature;
    for (const DeviceView& v : views_) signature += v.info.DeviceId + "@" + v.info.Room + ";";
    if (signature != roomsSignature_) {
        roomsSignature_ = signature;
        RebuildRooms();
    } else {
        for (const DeviceView& v : views_) {
            auto tile = tiles_.find(v.info.DeviceId);
            if (tile != tiles_.end()) UpdateTile(tile->second, v);
        }
    }
    const bool initial = eventKeys_.empty();
    for (const DeviceView& v : views_) NoteEvent(v, initial);
    if (selectedId_.empty() && !views_.empty()) selectedId_ = views_.front().info.DeviceId;
    RebuildInspector();
    UpdateSummary();
    UpdateDevicesTable();
    UpdateEnergy();
    window_->RequestRedraw();
}

void SmartHomeCenterWindow::ApplyChanged(const std::set<std::string>& ids) {
    bool selectedChanged = false;
    for (DeviceView& v : views_) {
        if (!ids.count(v.info.DeviceId)) continue;
        v = DescribeDevice(SmartHomeAPI::Instance().GetDevice(v.info.DeviceId));
        auto tile = tiles_.find(v.info.DeviceId);
        if (tile != tiles_.end()) {
            // A switch that appeared or went (a device going offline) needs
            // the tile rebuilt; everything else updates in place.
            if (static_cast<bool>(tile->second.toggle) != (v.canToggle || (!v.online && (v.kind == DeviceKind::Light || v.kind == DeviceKind::Switch)))) {
                roomsSignature_.clear();
            } else {
                UpdateTile(tile->second, v);
            }
        }
        NoteEvent(v, false);
        if (v.info.DeviceId == selectedId_) selectedChanged = true;
    }
    if (roomsSignature_.empty()) {
        ReloadAll();
        return;
    }
    if (selectedChanged) UpdateInspector();
    UpdateRoomCounts();
    UpdateSummary();
    if (page_ == Page::Devices) UpdateDevicesTable();
    UpdateEnergy();
}

void SmartHomeCenterWindow::UpdateSummary() {
    const HomeSummary s = SummarizeHome(views_);

    climateStat_.value->SetText(s.hasInsideTemperature ? FormatTemperature(s.insideTemperature) : "-");
    climateStat_.detail->SetText(s.heating > 0 ? std::to_string(s.heating) + (s.heating == 1 ? " room heating" : " rooms heating")
                                               : "Heating idle");
    const Tone climate = s.heating > 0 ? kHeat : kClimate;
    climateStat_.badge->SetBackgroundColor(climate.soft);
    climateStat_.icon->SetTintColor(climate.fg);

    lightsStat_.value->SetText(std::to_string(s.lightsOn) + " on");
    lightsStat_.detail->SetText("of " + std::to_string(s.lights) + " lights");
    const Tone lights = s.lightsOn > 0 ? kLit : kIdle;
    lightsStat_.badge->SetBackgroundColor(lights.soft);
    lightsStat_.icon->SetTintColor(lights.fg);

    securityStat_.value->SetText(s.unlocked == 0 ? "All locked" : std::to_string(s.unlocked) + " unlocked");
    securityStat_.detail->SetText(s.unlocked == 0 ? std::to_string(s.locks) + " locks" : s.firstUnlocked);
    const Tone security = s.unlocked == 0 ? kOpen : kHeat;
    securityStat_.badge->SetBackgroundColor(security.soft);
    securityStat_.icon->SetTintColor(security.fg);
    securityStat_.icon->LoadFromFile(iconsDir_ + (s.unlocked == 0 ? "lock.svg" : "unlock.svg"));

    powerStat_.value->SetText(FormatPower(s.power));
    powerStat_.detail->SetText("metered plugs");
    powerStat_.badge->SetBackgroundColor(kLit.soft);
    powerStat_.icon->SetTintColor(kLit.fg);

    const bool alarming = s.alerts > 0;
    if (alarming) alertText_->SetText(s.alerts == 1 ? s.firstAlert : s.firstAlert + "  (and " + std::to_string(s.alerts - 1) + " more)");
    if (alertBanner_->IsVisible() != alarming) {
        alertBanner_->SetVisible(alarming);
        roomsColumn_->InvalidateLayout();
    }

    std::string subtitle = TodayText() + "  ·  " + std::to_string(s.devices) + " devices";
    if (s.online < s.devices) subtitle += ", " + std::to_string(s.devices - s.online) + " offline";
    subtitle += alarming ? "  ·  needs attention" : "  ·  all quiet";
    if (page_ == Page::Home) {
        pageTitle_->SetText(Greeting());
        pageSubtitle_->SetText(subtitle);
    }
}

// ===== PAGES =====

void SmartHomeCenterWindow::ShowPage(Page page) {
    page_ = page;
    for (auto& [p, container] : pages_) container->SetVisible(p == page);
    for (auto& [p, button] : navButtons_) {
        const bool current = p == page;
        button->SetColors(current ? kAccentSoft : Colors::Transparent, current ? kAccentSoft : kHover);
        button->SetTextColors(current ? kAccent : Color(58, 66, 86));
        button->SetIconMaskColor(current ? kAccent : kMuted);
        button->SetFont("", 14, current ? FontWeight::Bold : FontWeight::Normal);
    }
    switch (page) {
        case Page::Home:
            UpdateSummary();
            break;
        case Page::Devices:
            pageTitle_->SetText("Devices");
            pageSubtitle_->SetText(std::to_string(views_.size()) + " devices in " + std::to_string(RoomsOf(views_).size()) +
                                   " rooms  ·  double-click one to open it");
            UpdateDevicesTable();
            break;
        case Page::Energy:
            pageTitle_->SetText("Energy");
            pageSubtitle_->SetText("What the metered plugs draw, live");
            UpdateEnergy();
            break;
        case Page::Activity:
            pageTitle_->SetText("Activity");
            pageSubtitle_->SetText("Everything that happened since SmartHome Center opened, newest first");
            if (activityModel_) activityModel_->Changed();
            break;
    }
    content_->InvalidateLayout();
    if (window_) window_->RequestRedraw();
}

// ===== ACTIONS =====

void SmartHomeCenterWindow::ToggleDevice(const std::string& id, bool on) {
    SmartHomeAPI::Instance().SetSwitchState(id, on);
}

void SmartHomeCenterWindow::AddDevice() {
    auto& api = SmartHomeAPI::Instance();
    const auto protocols = api.GetEnabledProtocols();
    if (protocols.empty()) {
        UltraCanvasDialogManager::ShowMessage("No radio is enabled, so there is nothing to search with.",
                                              "Add device", DialogType::Information, DialogButtons::OK, nullptr,
                                              window_.get());
        return;
    }
    if (api.StartPairing(protocols.front(), 60)) {
        Log("SmartHome Center", "", "Looking for new devices - put yours in pairing mode", false);
        addButton_->SetText("Searching…");
        addButton_->SetDisabled(true);
    }
}

void SmartHomeCenterWindow::ActivateScene(const std::string& sceneId, const std::string& name) {
    // The engine logs the activation; the Activity page shows it from there.
    (void)name;
    SmartHomeAPI::Instance().ActivateScene(sceneId);
}

void SmartHomeCenterWindow::CreateDemoAutomations() {
    // What a home would set up first, in the demo home's devices: they run
    // in the module's automation engine and show in Activity when they do.
    // Created once; after that the store has them, with whatever the person
    // changed.
    auto& api = SmartHomeAPI::Instance();
    if (!api.GetAutomations().empty()) return;
    // From the API: the window's own list is not filled yet.
    const auto devices = api.GetDevices();
    auto idOf = [&](const char* name) {
        for (const auto& d : devices) if (d.Name == name) return d.DeviceId;
        return std::string();
    };
    const std::string leak = idOf("Under-sink leak sensor");
    const std::string smoke = idOf("Kitchen smoke alarm");
    const std::string motion = idOf("Hallway motion");
    const std::string patio = idOf("Patio door");
    const std::string hall = idOf("Hall light");
    const std::string kettle = idOf("Kettle");
    const std::string rail = idOf("Towel rail");
    auto device = [](const std::string& id, const char* attribute, const char* value, int forMinutes = 0) {
        std::string config = std::string(R"({"type":"device","deviceId":")") + id + R"(","attribute":")" +
                             attribute + R"(","value":")" + value + "\"";
        if (forMinutes > 0) config += ",\"forMinutes\":" + std::to_string(forMinutes);
        return config + "}";
    };
    auto notify = [](const std::string& message) { return SmartHomeCommand{"", "notify", {{"message", message}}}; };

    api.CreateAutomation({"center.leak", "Water leak", true, "device", device(leak, "triggered", "true"),
                          {notify("Water under the kitchen sink - the kettle and towel rail are switched off"),
                           {kettle, "turnOff", {}}, {rail, "turnOff", {}}}, {}});
    api.CreateAutomation({"center.smoke", "Smoke alarm", true, "device", device(smoke, "triggered", "true"),
                          {notify("Smoke in the kitchen"), {hall, "turnOn", {}}}, {}});
    api.CreateAutomation({"center.hall", "Hall light on motion", true, "device", device(motion, "triggered", "true"),
                          {{hall, "turnOn", {}}, {"", "delay", {{"seconds", "120"}}}, {hall, "turnOff", {}}}, {}});
    api.CreateAutomation({"center.patio", "Patio door left open", true, "device", device(patio, "open", "true", 10),
                          {notify("The patio door has been open for 10 minutes")}, {}});
    api.CreateAutomation({"center.sunset", "Evening at sunset", true, "sunset", R"({"type":"sunset","offsetMinutes":-15})",
                          {{"", "activateScene", {{"sceneId", kSceneEvening}}}}, {}});
    // Sunset needs a place; the demo home stands in Berlin.
    double latitude = 0, longitude = 0;
    if (!api.GetHomeLocation(latitude, longitude)) api.SetHomeLocation(52.52, 13.405);
}

void SmartHomeCenterWindow::CreateDemoScenes() {
    // The demo home's scenes, by device name. A real home builds its own in
    // the scene editor (proposal phase 2); these show what one does.
    auto& api = SmartHomeAPI::Instance();
    const auto devices = api.GetDevices();
    auto idOf = [&](const char* name) {
        for (const auto& d : devices) if (d.Name == name) return d.DeviceId;
        return std::string();
    };
    auto cmd = [&](const char* name, const char* command, std::map<std::string, std::string> params = {}) {
        return SmartHomeCommand{idOf(name), command, std::move(params)};
    };
    api.CreateScene({kSceneMorning, "Good morning", "sunrise", {
        cmd("Patio blinds", "setPosition", {{"position", "100"}}),
        cmd("Bedroom blinds", "setPosition", {{"position", "100"}}),
        cmd("Kitchen spots", "turnOn"), cmd("Kettle", "turnOn"),
        cmd("Living room heating", "setTargetTemp", {{"temperature", "21"}}),
        cmd("Office heating", "setTargetTemp", {{"temperature", "21"}})}, false});
    api.CreateScene({kSceneEvening, "Evening", "sunset", {
        cmd("Ceiling light", "setLight", {{"on", "true"}, {"brightness", "120"}, {"colorTemp", "2600"}}),
        cmd("Floor lamp", "setLight", {{"on", "true"}, {"brightness", "150"}, {"colorTemp", "2300"}}),
        cmd("Patio blinds", "setPosition", {{"position", "0"}}),
        cmd("Path lights", "turnOn"), cmd("Desk lamp", "turnOff"), cmd("Workstation", "turnOff")}, false});
    api.CreateScene({kSceneAway, "Away", "away", {
        cmd("Ceiling light", "turnOff"), cmd("Floor lamp", "turnOff"), cmd("Kitchen spots", "turnOff"),
        cmd("Desk lamp", "turnOff"), cmd("Bedside lamp", "turnOff"), cmd("Mirror light", "turnOff"),
        cmd("Hall light", "turnOff"), cmd("TV", "turnOff"), cmd("Kettle", "turnOff"), cmd("Towel rail", "turnOff"),
        cmd("Front door", "lock"), cmd("Garden gate", "lock")}, false});
    api.CreateScene({kSceneNight, "Good night", "moon", {
        cmd("Ceiling light", "turnOff"), cmd("Floor lamp", "turnOff"), cmd("Kitchen spots", "turnOff"),
        cmd("TV", "turnOff"), cmd("Path lights", "turnOff"), cmd("Bedroom blinds", "setPosition", {{"position", "0"}}),
        cmd("Front door", "lock"), cmd("Garden gate", "lock"),
        cmd("Bedroom heating", "setTargetTemp", {{"temperature", "18"}})}, false});
}

void SmartHomeCenterWindow::ShowAbout() {
    std::string text = std::string("SmartHome Center ") + SMARTHOMECENTER_VERSION +
        "\nUltraCanvas Framework " + UltraCanvas::versionString +
        "\n\nControl the whole home from one window: every device, room, scene and"
        " meter, over Matter, Thread, Zigbee, Z-Wave and KNX - locally, with no cloud.";
    if (demoHome_) text += "\n\nThis is the demo home: simulated devices, no radio.";
    UltraCanvasDialogManager::ShowMessage(text, "About SmartHome Center", DialogType::Information,
                                          DialogButtons::OK, nullptr, window_.get());
}

} // namespace SmartHomeCenter
