// ui/UltraCanvasSmartHomeDialogs.cpp
// The device detail dialog and the pairing wizard - what a host opens for
// "show me this device" and "add a device".
//
// Both are containers of framework elements: labels, buttons, a progress
// bar, and the per-device control as a real child, so its sliders and
// buttons get their events from the window like any other element. The
// dialog picks that control for whatever it is showing, so opening a light
// gets a light control and opening a lock gets a lock control without the
// caller choosing.
//
// Version: 2.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasSmartHomeDeviceControl.h"
#include "SmartHomeWidgetKit.h"

#include "UltraCanvasApplication.h"

#include <algorithm>
#include <chrono>

namespace UltraCanvas {
namespace SmartHome {

using namespace Kit;

namespace {

const Color kSheet(0xFF, 0xFF, 0xFF);

// A white sheet with a rounded border: what both widgets sit on.
void AsSheet(UltraCanvasContainer& c, float gap) {
    AsColumn(c, gap, 16.0f);
    c.SetBackgroundColor(kSheet);
    c.SetBorders(1.0f, kTrack);
    c.SetBorderRadius(10.0f);
}

std::shared_ptr<UltraCanvasButton> DangerButton(const std::string& id, const std::string& text) {
    auto b = Kit::Button(id, text, false);
    b->SetColors(kBad, Color(0xE5, 0x39, 0x35), Color(0xD3, 0x2F, 0x2F), kTrack);
    b->SetTextColors(Colors::White);
    return b;
}

std::shared_ptr<UltraCanvasContainer> Spacer(const std::string& id) {
    auto s = CreateContainer(id);
    Plain(*s);
    Grow(*s);
    return s;
}

uint64_t NowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

}  // namespace

// ============================================================================
// Device detail dialog
// ============================================================================

SmartHomeDeviceDialog::SmartHomeDeviceDialog(const std::string& identifier,
                                             float x, float y, float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    AsSheet(*this, 10);

    auto header = Row(identifier + "_Header", 8, 28);
    title = Label(identifier + "_Title", "", 15, kText, true);
    Grow(*title);
    closeButton = Kit::Button(identifier + "_Close", "×", false, 26);
    closeButton->SetFontSize(14);
    closeButton->SetTooltip("Close");
    Size(*closeButton, 26, 26);
    closeButton->SetOnClick([this]() { Hide(); });
    header->AddChild(title);
    header->AddChild(closeButton);
    AddChild(header);

    facts = CreateContainer(identifier + "_Facts");
    AsColumn(*facts, 2, 0);
    facts->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    facts->layoutItem.SetFlexShrink(0);
    AddChild(facts);

    // The control goes in here; it takes the room the rest leaves.
    controlHolder = CreateContainer(identifier + "_ControlHolder");
    AsColumn(*controlHolder, 0, 0);
    Grow(*controlHolder);
    AddChild(controlHolder);

    auto actions = Row(identifier + "_Actions", 8, 28);
    renameButton = Kit::Button(identifier + "_Rename", "Rename");
    Size(*renameButton, 90, 28);
    renameButton->SetOnClick([this]() { if (onRename) onRename(deviceInfo.Name); });
    removeButton = DangerButton(identifier + "_Remove", "Remove");
    Size(*removeButton, 90, 28);
    removeButton->SetOnClick([this]() {
        SMARTHOME_API.RemoveDevice(deviceId);
        if (onRemove) onRemove();
        Hide();
    });
    actions->AddChild(renameButton);
    actions->AddChild(Spacer(identifier + "_ActionsGap"));
    actions->AddChild(removeButton);
    AddChild(actions);

    SetVisible(false);   // a dialog starts closed
}

SmartHomeDeviceDialog::~SmartHomeDeviceDialog() = default;

void SmartHomeDeviceDialog::LoadDeviceInfo() {
    deviceInfo = SMARTHOME_API.GetDevice(deviceId);
}

std::shared_ptr<UltraCanvasUIElement> SmartHomeDeviceDialog::CreateControlWidget() {
    // The dialog picks the control, so a caller opening a device does not have
    // to know what kind of device it is.
    const std::string id = GetIdentifier();
    switch (deviceInfo.Category) {
        case SmartHomeDeviceCategory::Light: {
            auto w = std::make_shared<SmartHomeLightControl>(id + "_Light");
            w->SetDevice(deviceId);
            return w;
        }
        case SmartHomeDeviceCategory::Thermostat: {
            auto w = std::make_shared<SmartHomeThermostatControl>(id + "_Thermostat");
            w->SetDevice(deviceId);
            return w;
        }
        case SmartHomeDeviceCategory::Lock: {
            auto w = std::make_shared<SmartHomeLockControl>(id + "_Lock");
            w->SetDevice(deviceId);
            return w;
        }
        case SmartHomeDeviceCategory::Blind: {
            auto w = std::make_shared<SmartHomeBlindControl>(id + "_Blind");
            w->SetDevice(deviceId);
            return w;
        }
        case SmartHomeDeviceCategory::Sensor: {
            auto w = std::make_shared<SmartHomeSensorDisplay>(id + "_Sensor");
            w->SetDevice(deviceId);
            return w;
        }
        default:
            // Switches, plugs and anything unrecognised get no inline control;
            // the dialog still shows the identity and the actions.
            return nullptr;
    }
}

void SmartHomeDeviceDialog::Show(const std::string& id) {
    deviceId = id;
    LoadDeviceInfo();
    title->SetText(deviceInfo.Name.empty() ? deviceId : deviceInfo.Name);

    facts->ClearChildren();
    const std::pair<const char*, std::string> rows[] = {
        {"Category",     DeviceCategoryToString(deviceInfo.Category)},
        {"Protocol",     ProtocolTypeToString(deviceInfo.Protocol)},
        {"State",        DeviceStateToString(deviceInfo.State)},
        {"Manufacturer", deviceInfo.Manufacturer},
        {"Model",        deviceInfo.Model},
        {"Firmware",     deviceInfo.FirmwareVersion},
    };
    int n = 0;
    for (const auto& [caption, value] : rows) {
        if (value.empty()) continue;   // an empty row tells the reader nothing
        const std::string rowId = GetIdentifier() + "_Fact" + std::to_string(n++);
        auto row = Row(rowId, 4, 16);
        auto name = Label(rowId + "Name", caption, 10, kTextDim);
        Size(*name, 90, 16);
        auto text = Label(rowId + "Value", value, 10, kText);
        Grow(*text);
        row->AddChild(name);
        row->AddChild(text);
        facts->AddChild(row);
    }

    controlHolder->ClearChildren();
    controlWidget = CreateControlWidget();
    if (controlWidget) {
        Grow(*controlWidget);
        controlHolder->AddChild(controlWidget);
    }
    SetVisible(true);
    InvalidateLayout();
    RequestRedraw();
}

void SmartHomeDeviceDialog::Hide() {
    SetVisible(false);
    // Drop the control with the dialog: it holds a device id that may be gone
    // by the time the dialog is opened again.
    controlHolder->ClearChildren();
    controlWidget.reset();
    if (onClose) onClose();
    RequestRedraw();
}

bool SmartHomeDeviceDialog::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (event.type == UCEventType::KeyDown && event.virtualKey == UCKeys::Escape) {
        Hide();
        return true;
    }
    if (UltraCanvasContainer::OnEvent(event)) return true;
    // A modal sheet swallows presses that land on it, so they cannot reach
    // whatever is behind.
    if (event.type == UCEventType::MouseDown || event.type == UCEventType::TouchStart) {
        return Contains(Point2Df(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y)));
    }
    return false;
}

// ============================================================================
// Pairing wizard
// ============================================================================

SmartHomePairingWizard::SmartHomePairingWizard(const std::string& identifier,
                                               float x, float y, float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    AsSheet(*this, 0);

    auto step = [&](const std::string& name) {
        auto c = CreateContainer(identifier + "_" + name);
        AsColumn(*c, 10, 0);
        Grow(*c);
        AddChild(c);
        return c;
    };

    // 1. Which radio.
    protocolStep = step("ProtocolStep");
    protocolStep->AddChild(Label(identifier + "_ProtocolTitle", "How is the device connected?", 13, kText, true));
    protocolList = CreateContainer(identifier + "_Protocols");
    AsColumn(*protocolList, 6, 0);
    protocolList->SetContainerStyle([]{ ContainerStyle st; st.autoShowScrollbars = true;
                                        st.autoShowHorizontalScrollbar = false; return st; }());
    Grow(*protocolList);
    protocolStep->AddChild(protocolList);
    noProtocols = Label(identifier + "_NoProtocols", "No protocol backends are available in this build.", 11, kTextDim);
    noProtocols->SetVisible(false);
    protocolStep->AddChild(noProtocols);
    auto protocolActions = Row(identifier + "_ProtocolActions", 8, 28);
    auto closeButton = Kit::Button(identifier + "_Close", "Cancel");
    Size(*closeButton, 90, 28);
    closeButton->SetOnClick([this]() { Cancel(); });
    protocolActions->AddChild(closeButton);
    protocolStep->AddChild(protocolActions);

    // 2. Searching.
    searchStep = step("SearchStep");
    searchTitle = Label(identifier + "_SearchTitle", "", 13, kText, true);
    searchStep->AddChild(searchTitle);
    searchProgress = CreateGaugeDiagramElement(identifier + "_SearchProgress", 0, 0, 300, 8);
    searchProgress->SetMode(GaugeMode::LinearBar);
    searchProgress->SetOrientation(GaugeOrientation::Horizontal);
    searchProgress->SetMinValue(0);
    searchProgress->SetMaxValue(100);
    searchProgress->SetShowLabels(false);
    searchProgress->SetGaugeColor(kAccent);
    searchProgress->SetTrackColor(kTrack);
    searchProgress->SetBackgroundColor(kSheet);
    Height(*searchProgress, 8);
    searchStep->AddChild(searchProgress);
    searchHint = Label(identifier + "_SearchHint", "Put the device into pairing mode.", 11, kTextDim);
    searchStep->AddChild(searchHint);
    searchStep->AddChild(Spacer(identifier + "_SearchGap"));
    auto searchActions = Row(identifier + "_SearchActions", 8, 28);
    cancelButton = Kit::Button(identifier + "_Cancel", "Cancel");
    Size(*cancelButton, 90, 28);
    cancelButton->SetOnClick([this]() { Cancel(); });
    searchActions->AddChild(cancelButton);
    searchStep->AddChild(searchActions);

    // 3. Added.
    doneStep = step("DoneStep");
    doneText = Label(identifier + "_DoneText", "", 13, kGood, true);
    doneStep->AddChild(doneText);
    doneStep->AddChild(Label(identifier + "_DoneHint", "Give it a room and a name in its details.", 11, kTextDim));
    doneStep->AddChild(Spacer(identifier + "_DoneGap"));
    auto doneActions = Row(identifier + "_DoneActions", 8, 28);
    doneActions->AddChild(Spacer(identifier + "_DoneActionsGap"));
    doneButton = Kit::Button(identifier + "_Done", "Done", true);
    Size(*doneButton, 90, 28);
    doneButton->SetOnClick([this]() { ShowStep(WizardStep::SelectProtocol); });
    doneActions->AddChild(doneButton);
    doneStep->AddChild(doneActions);

    // 4. Failed.
    errorStep = step("ErrorStep");
    errorStep->AddChild(Label(identifier + "_ErrorTitle", "Pairing failed", 13, kBad, true));
    errorText = Label(identifier + "_ErrorText", "", 11, kTextDim);
    errorText->SetWrap(TextWrap::WrapWord);
    Height(*errorText, 36);
    errorStep->AddChild(errorText);
    errorStep->AddChild(Spacer(identifier + "_ErrorGap"));
    auto errorActions = Row(identifier + "_ErrorActions", 8, 28);
    backButton = Kit::Button(identifier + "_Back", "Back");
    Size(*backButton, 90, 28);
    backButton->SetOnClick([this]() {
        errorMessage.clear();
        ShowStep(WizardStep::SelectProtocol);
    });
    errorActions->AddChild(backButton);
    errorStep->AddChild(errorActions);

    ShowStep(WizardStep::SelectProtocol);
}

SmartHomePairingWizard::~SmartHomePairingWizard() { StopTimer(); }

void SmartHomePairingWizard::ShowStep(WizardStep s) {
    currentStep = s;
    protocolStep->SetVisible(s == WizardStep::SelectProtocol);
    searchStep->SetVisible(s == WizardStep::Searching);
    doneStep->SetVisible(s == WizardStep::Complete);
    errorStep->SetVisible(s == WizardStep::Error);
    InvalidateLayout();
    RequestRedraw();
}

void SmartHomePairingWizard::RebuildProtocolButtons() {
    protocolList->ClearChildren();
    protocolButtons.clear();
    for (size_t i = 0; i < supportedProtocols.size(); ++i) {
        const SmartHomeProtocolType protocol = supportedProtocols[i];
        auto b = Kit::Button(GetIdentifier() + "_Protocol" + std::to_string(i), ProtocolTypeToString(protocol));
        b->SetFontSize(12);
        b->SetOnClick([this, protocol]() { SelectProtocol(protocol); });
        protocolList->AddChild(b);
        protocolButtons.push_back(b);
    }
    noProtocols->SetVisible(supportedProtocols.empty());
    InvalidateLayout();
}

void SmartHomePairingWizard::SetSupportedProtocols(
        const std::vector<SmartHomeProtocolType>& protocols) {
    supportedProtocols = protocols;
    RebuildProtocolButtons();
}

void SmartHomePairingWizard::Start() {
    StopTimer();
    selectedProtocol = SmartHomeProtocolType::Unknown;
    pairedDevice = SmartHomeDeviceInfo{};
    errorMessage.clear();

    // Offer what the module can actually pair over, unless a host has already
    // narrowed the list itself.
    if (supportedProtocols.empty()) supportedProtocols = SMARTHOME_API.GetAvailableProtocols();
    RebuildProtocolButtons();
    ShowStep(WizardStep::SelectProtocol);
}

void SmartHomePairingWizard::Cancel() {
    StopSearch();
    ShowStep(WizardStep::SelectProtocol);
    if (onCancel) onCancel();
}

void SmartHomePairingWizard::SelectProtocol(SmartHomeProtocolType protocol) {
    selectedProtocol = protocol;
    StartSearch();
}

void SmartHomePairingWizard::Fail(const std::string& message) {
    StopSearch();
    errorMessage = message;
    errorText->SetText(message);
    ShowStep(WizardStep::Error);
}

void SmartHomePairingWizard::StartSearch() {
    if (selectedProtocol == SmartHomeProtocolType::Unknown) {
        Fail("No protocol selected");
        return;
    }
    // What is in the home now, so a device that joins stands out.
    knownDevices.clear();
    for (const auto& d : SMARTHOME_API.GetDevices()) knownDevices.push_back(d.DeviceId);

    if (!SMARTHOME_API.StartPairing(selectedProtocol, searchTimeout)) {
        // Naming the protocol matters: "pairing failed" with five backends
        // installed tells nobody which one to look at.
        Fail("Could not start pairing over " + ProtocolTypeToString(selectedProtocol) +
             ". Is it enabled, and is its adapter connected?");
        return;
    }
    searchStartedMs = NowMs();
    searchTitle->SetText("Searching over " + ProtocolTypeToString(selectedProtocol) + "...");
    searchProgress->SetValue(0);
    ShowStep(WizardStep::Searching);

    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
        timerId = app->StartTimer(500, /*periodic=*/true, [this](TimerId) { Poll(); });
    }
}

void SmartHomePairingWizard::StopTimer() {
    if (timerId == 0) return;
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(timerId);
    timerId = 0;
}

void SmartHomePairingWizard::StopSearch() {
    StopTimer();
    if (SMARTHOME_API.IsPairing()) SMARTHOME_API.StopPairing();
}

void SmartHomePairingWizard::Poll() {
    if (currentStep != WizardStep::Searching) return;

    for (const auto& d : SMARTHOME_API.GetDevices()) {
        if (std::find(knownDevices.begin(), knownDevices.end(), d.DeviceId) != knownDevices.end()) continue;
        pairedDevice = d;
        StopSearch();
        doneText->SetText("Added " + (d.Name.empty() ? d.DeviceId : d.Name));
        ShowStep(WizardStep::Complete);
        if (onComplete) onComplete(pairedDevice);
        return;
    }

    const double elapsed = static_cast<double>(NowMs() - searchStartedMs) / 1000.0;
    searchProgress->SetValue(std::min(100.0, elapsed * 100.0 / searchTimeout));
    if (elapsed >= searchTimeout || !SMARTHOME_API.IsPairing()) {
        Fail("Nothing joined over " + ProtocolTypeToString(selectedProtocol) + " in " +
             std::to_string(searchTimeout) + " seconds. Put the device into pairing mode and try again.");
    }
}

bool SmartHomePairingWizard::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (event.type == UCEventType::KeyDown && event.virtualKey == UCKeys::Escape) {
        Cancel();
        return true;
    }
    return UltraCanvasContainer::OnEvent(event);
}

// ===== FACTORIES =====

std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeDeviceDialogElement() {
    return std::make_shared<SmartHomeDeviceDialog>("SmartHomeDeviceDialog", 0, 0, 340, 420);
}
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomePairingWizardElement() {
    return std::make_shared<SmartHomePairingWizard>("SmartHomePairingWizard", 0, 0, 340, 300);
}

}  // namespace SmartHome
}  // namespace UltraCanvas
