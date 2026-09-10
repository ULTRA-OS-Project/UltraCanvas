// ui/UltraCanvasSmartHomeDialogs.cpp
// The device detail dialog and the pairing wizard — the two widgets the
// dashboard's ShowDeviceDetails() and ShowAddDeviceWizard() hooks call into.
//
// The dialog picks the right per-device control for whatever it is showing, so
// opening a light gets a light control and opening a lock gets a lock control
// without the caller choosing. The wizard walks the steps the module README
// describes: select protocol, search, configure, complete.
//
// Author: UltraCanvas Framework

#include "UltraCanvasSmartHomeDeviceControl.h"

#include <algorithm>

namespace UltraCanvas {
namespace SmartHome {

namespace {

constexpr double kPad = 16.0;

const Color kText    (0x21, 0x21, 0x21);
const Color kTextDim (0x75, 0x75, 0x75);
const Color kTrack   (0xE0, 0xE0, 0xE0);
const Color kAccent  (33, 150, 243);
const Color kDanger  (0xF4, 0x43, 0x36);
const Color kOk      (76, 175, 80);
const Color kSheet   (0xFF, 0xFF, 0xFF);
const Color kScrim   (0, 0, 0, 140);

void Button(IRenderContext* ctx, const Rect2Dd& r, const std::string& label,
            const Color& fill, const Color& textColor) {
    ctx->SetFillPaint(fill);
    ctx->FillRoundedRectangle(r, r.height / 2.0);
    ctx->SetTextPaint(textColor);
    ctx->SetFontSize(11.0);
    ctx->DrawTextInRect(label, Rect2Dd(r.x + 10.0, r.y + r.height / 2.0 - 7.0,
                                       r.width - 20.0, 14.0));
}

bool Hit(const Rect2Dd& r, int x, int y) {
    return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}

}  // namespace

// ============================================================================
// Device detail dialog
// ============================================================================

SmartHomeDeviceDialog::SmartHomeDeviceDialog(const std::string& identifier,
                                             float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {
    SetVisible(false);   // a dialog starts closed
}

SmartHomeDeviceDialog::~SmartHomeDeviceDialog() = default;

void SmartHomeDeviceDialog::LoadDeviceInfo() {
    deviceInfo = SMARTHOME_API.GetDevice(deviceId);
}

std::shared_ptr<UltraCanvasUIElement> SmartHomeDeviceDialog::CreateControlWidget() {
    // The dialog picks the control, so a caller opening a device does not have
    // to know what kind of device it is.
    switch (deviceInfo.Category) {
        case SmartHomeDeviceCategory::Light: {
            auto w = std::make_shared<SmartHomeLightControl>(
                GetIdentifier() + ".light", 0, 0, 100, 100);
            w->SetDevice(deviceId);
            return w;
        }
        case SmartHomeDeviceCategory::Thermostat: {
            auto w = std::make_shared<SmartHomeThermostatControl>(
                GetIdentifier() + ".thermostat", 0, 0, 100, 100);
            w->SetDevice(deviceId);
            return w;
        }
        case SmartHomeDeviceCategory::Lock: {
            auto w = std::make_shared<SmartHomeLockControl>(
                GetIdentifier() + ".lock", 0, 0, 100, 100);
            w->SetDevice(deviceId);
            return w;
        }
        case SmartHomeDeviceCategory::Blind: {
            auto w = std::make_shared<SmartHomeBlindControl>(
                GetIdentifier() + ".blind", 0, 0, 100, 100);
            w->SetDevice(deviceId);
            return w;
        }
        case SmartHomeDeviceCategory::Sensor: {
            auto w = std::make_shared<SmartHomeSensorDisplay>(
                GetIdentifier() + ".sensor", 0, 0, 100, 100);
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
    controlWidget = CreateControlWidget();
    SetVisible(true);
    RequestRedraw();
}

void SmartHomeDeviceDialog::Hide() {
    SetVisible(false);
    // Drop the control with the dialog: it holds a device id that may be gone
    // by the time the dialog is opened again.
    controlWidget.reset();
    if (onClose) onClose();
    RequestRedraw();
}

void SmartHomeDeviceDialog::RenderHeader(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    ctx->SetTextPaint(kText);
    ctx->SetFontSize(15.0);
    ctx->DrawTextInRect(deviceInfo.Name.empty() ? deviceId : deviceInfo.Name,
                        Rect2Dd(kPad, kPad, b.width - 2 * kPad - 30.0, 20.0));

    // Close affordance, top right.
    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(14.0);
    ctx->DrawTextInRect("×", Rect2Dd(b.width - kPad - 20.0, kPad, 20.0, 20.0));
}

void SmartHomeDeviceDialog::RenderDeviceInfo(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    double y = kPad + 28.0;
    ctx->SetFontSize(10.0);

    const std::pair<const char*, std::string> rows[] = {
        {"Category",     DeviceCategoryToString(deviceInfo.Category)},
        {"Protocol",     ProtocolTypeToString(deviceInfo.Protocol)},
        {"State",        DeviceStateToString(deviceInfo.State)},
        {"Manufacturer", deviceInfo.Manufacturer},
        {"Model",        deviceInfo.Model},
        {"Firmware",     deviceInfo.FirmwareVersion},
    };
    for (const auto& [label, value] : rows) {
        if (value.empty()) continue;   // an empty row tells the reader nothing
        ctx->SetTextPaint(kTextDim);
        ctx->DrawTextInRect(label, Rect2Dd(kPad, y, 90.0, 14.0));
        ctx->SetTextPaint(kText);
        ctx->DrawTextInRect(value, Rect2Dd(kPad + 94.0, y, b.width - kPad * 2 - 94.0, 14.0));
        y += 16.0;
    }
}

void SmartHomeDeviceDialog::RenderControls(IRenderContext* ctx) {
    if (!controlWidget) return;
    const Rect2Df b = GetLocalBounds();
    const double top = kPad + 140.0;
    const double height = b.height - top - 56.0;
    if (height <= 0.0) return;

    controlWidget->SetBounds(Rect2Df(static_cast<float>(kPad), static_cast<float>(top),
                                     static_cast<float>(b.width - 2 * kPad),
                                     static_cast<float>(height)));
    controlWidget->Render(ctx, b);
}

void SmartHomeDeviceDialog::RenderActions(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const double y = b.height - kPad - 26.0;
    Button(ctx, Rect2Dd(kPad, y, 90.0, 26.0), "Rename", kTrack, kText);
    Button(ctx, Rect2Dd(b.width - kPad - 90.0, y, 90.0, 26.0), "Remove", kDanger, Colors::White);
}

void SmartHomeDeviceDialog::Render(IRenderContext* ctx, const Rect2Df&) {
    if (!ctx || !IsVisible()) return;

    const Rect2Df b = GetLocalBounds();
    ctx->SetFillPaint(kSheet);
    ctx->FillRoundedRectangle(Rect2Dd(b), 10.0);
    ctx->SetStrokePaint(kTrack);
    ctx->SetStrokeWidth(1.0);
    ctx->DrawRoundedRectangle(Rect2Dd(b), 10.0);

    RenderHeader(ctx);
    RenderDeviceInfo(ctx);
    RenderControls(ctx);
    RenderActions(ctx);
}

bool SmartHomeDeviceDialog::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;

    const Rect2Df b = GetLocalBounds();
    const int x = event.pointer.x;
    const int y = event.pointer.y;

    if (event.type == UCEventType::KeyDown && event.virtualKey == UCKeys::Escape) {
        Hide();
        return true;
    }

    if (event.type != UCEventType::MouseDown && event.type != UCEventType::TouchStart) {
        // Hand anything else to the inline control, which owns the drags.
        if (controlWidget) {
            const Rect2Df cb = controlWidget->GetBounds();
            UCEvent local = event;
            local.pointer.x = x - static_cast<int>(cb.x);
            local.pointer.y = y - static_cast<int>(cb.y);
            return controlWidget->OnEvent(local);
        }
        return false;
    }

    const Rect2Dd close(b.width - kPad - 20.0, kPad, 20.0, 20.0);
    if (Hit(close, x, y)) { Hide(); return true; }

    const double ay = b.height - kPad - 26.0;
    if (Hit(Rect2Dd(kPad, ay, 90.0, 26.0), x, y)) {
        if (onRename) onRename(deviceInfo.Name);
        return true;
    }
    if (Hit(Rect2Dd(b.width - kPad - 90.0, ay, 90.0, 26.0), x, y)) {
        SMARTHOME_API.RemoveDevice(deviceId);
        if (onRemove) onRemove();
        Hide();
        return true;
    }

    if (controlWidget) {
        const Rect2Df cb = controlWidget->GetBounds();
        UCEvent local = event;
        local.pointer.x = x - static_cast<int>(cb.x);
        local.pointer.y = y - static_cast<int>(cb.y);
        if (controlWidget->OnEvent(local)) return true;
    }

    // A modal sheet swallows clicks that land on it, so they cannot reach
    // whatever is behind.
    return Contains(Point2Df(static_cast<float>(x), static_cast<float>(y)));
}

// ============================================================================
// Pairing wizard
// ============================================================================

SmartHomePairingWizard::SmartHomePairingWizard(const std::string& identifier,
                                               float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {}

SmartHomePairingWizard::~SmartHomePairingWizard() = default;

void SmartHomePairingWizard::SetSupportedProtocols(
        const std::vector<SmartHomeProtocolType>& protocols) {
    supportedProtocols = protocols;
    RequestRedraw();
}

void SmartHomePairingWizard::Start() {
    currentStep = WizardStep::SelectProtocol;
    selectedProtocol = SmartHomeProtocolType::Unknown;
    selectedDeviceId.clear();
    discoveredDevices.clear();
    pairedDevice = SmartHomeDeviceInfo{};
    errorMessage.clear();
    searchProgress = 0;

    // Offer what the module can actually pair over, unless a host has already
    // narrowed the list itself.
    if (supportedProtocols.empty()) {
        supportedProtocols = SMARTHOME_API.GetAvailableProtocols();
    }
    RequestRedraw();
}

void SmartHomePairingWizard::Cancel() {
    StopSearch();
    currentStep = WizardStep::SelectProtocol;
    if (onCancel) onCancel();
    RequestRedraw();
}

void SmartHomePairingWizard::SelectProtocol(SmartHomeProtocolType protocol) {
    selectedProtocol = protocol;
    StartSearch();
}

void SmartHomePairingWizard::StartSearch() {
    if (selectedProtocol == SmartHomeProtocolType::Unknown) {
        errorMessage = "No protocol selected";
        currentStep = WizardStep::Error;
        RequestRedraw();
        return;
    }
    if (!SMARTHOME_API.StartPairing(selectedProtocol)) {
        // Naming the protocol matters: "pairing failed" with five backends
        // installed tells nobody which one to look at.
        errorMessage = "Could not start pairing over " +
                       ProtocolTypeToString(selectedProtocol);
        currentStep = WizardStep::Error;
        RequestRedraw();
        return;
    }
    currentStep = WizardStep::Searching;
    searchProgress = 0;
    RequestRedraw();
}

void SmartHomePairingWizard::StopSearch() {
    if (SMARTHOME_API.IsPairing()) SMARTHOME_API.StopPairing();
}

void SmartHomePairingWizard::ConfigureDevice() {
    if (selectedDeviceId.empty()) {
        errorMessage = "No device selected";
        currentStep = WizardStep::Error;
        RequestRedraw();
        return;
    }
    auto it = std::find_if(discoveredDevices.begin(), discoveredDevices.end(),
                           [this](const SmartHomeDeviceInfo& d) {
                               return d.DeviceId == selectedDeviceId;
                           });
    if (it == discoveredDevices.end()) {
        errorMessage = "That device is no longer being advertised";
        currentStep = WizardStep::Error;
        RequestRedraw();
        return;
    }
    pairedDevice = *it;
    currentStep = WizardStep::Configuring;
    RequestRedraw();
}

void SmartHomePairingWizard::FinishPairing() {
    StopSearch();
    currentStep = WizardStep::Complete;
    if (onComplete) onComplete(pairedDevice);
    RequestRedraw();
}

void SmartHomePairingWizard::RenderProtocolSelection(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    ctx->SetTextPaint(kText);
    ctx->SetFontSize(13.0);
    ctx->DrawTextInRect("How is the device connected?",
                        Rect2Dd(kPad, kPad, b.width - 2 * kPad, 18.0));

    if (supportedProtocols.empty()) {
        ctx->SetTextPaint(kTextDim);
        ctx->SetFontSize(11.0);
        ctx->DrawTextInRect("No protocol backends are available in this build.",
                            Rect2Dd(kPad, kPad + 30.0, b.width - 2 * kPad, 16.0));
        return;
    }

    double y = kPad + 30.0;
    for (const auto& protocol : supportedProtocols) {
        Button(ctx, Rect2Dd(kPad, y, b.width - 2 * kPad, 28.0),
               ProtocolTypeToString(protocol), kTrack, kText);
        y += 34.0;
    }
}

void SmartHomePairingWizard::RenderSearching(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    ctx->SetTextPaint(kText);
    ctx->SetFontSize(13.0);
    ctx->DrawTextInRect("Searching over " + ProtocolTypeToString(selectedProtocol) + "…",
                        Rect2Dd(kPad, kPad, b.width - 2 * kPad, 18.0));

    const double barW = b.width - 2 * kPad;
    ctx->SetFillPaint(kTrack);
    ctx->FillRoundedRectangle(Rect2Dd(kPad, kPad + 28.0, barW, 6.0), 3.0);
    ctx->SetFillPaint(kAccent);
    ctx->FillRoundedRectangle(
        Rect2Dd(kPad, kPad + 28.0, barW * (std::clamp(searchProgress, 0, 100) / 100.0), 6.0), 3.0);

    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(11.0);
    ctx->DrawTextInRect(discoveredDevices.empty()
                            ? "Put the device into pairing mode."
                            : std::to_string(discoveredDevices.size()) + " found",
                        Rect2Dd(kPad, kPad + 44.0, barW, 16.0));

    Button(ctx, Rect2Dd(kPad, b.height - kPad - 26.0, 90.0, 26.0), "Cancel", kTrack, kText);
}

void SmartHomePairingWizard::RenderDeviceFound(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    ctx->SetTextPaint(kText);
    ctx->SetFontSize(13.0);
    ctx->DrawTextInRect("Found these devices", Rect2Dd(kPad, kPad, b.width - 2 * kPad, 18.0));

    double y = kPad + 30.0;
    ctx->SetFontSize(11.0);
    for (const auto& device : discoveredDevices) {
        const bool chosen = device.DeviceId == selectedDeviceId;
        ctx->SetFillPaint(chosen ? kAccent : kTrack);
        ctx->FillRoundedRectangle(Rect2Dd(kPad, y, b.width - 2 * kPad, 26.0), 6.0);
        ctx->SetTextPaint(chosen ? Colors::White : kText);
        ctx->DrawTextInRect(device.Name.empty() ? device.DeviceId : device.Name,
                            Rect2Dd(kPad + 8.0, y + 6.0, b.width - 2 * kPad - 16.0, 14.0));
        y += 30.0;
    }

    Button(ctx, Rect2Dd(b.width - kPad - 90.0, b.height - kPad - 26.0, 90.0, 26.0),
           "Continue", selectedDeviceId.empty() ? kTrack : kAccent,
           selectedDeviceId.empty() ? kTextDim : Colors::White);
}

void SmartHomePairingWizard::RenderConfiguring(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    ctx->SetTextPaint(kText);
    ctx->SetFontSize(13.0);
    ctx->DrawTextInRect("Adding " + (pairedDevice.Name.empty() ? pairedDevice.DeviceId
                                                              : pairedDevice.Name),
                        Rect2Dd(kPad, kPad, b.width - 2 * kPad, 18.0));
    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(11.0);
    ctx->DrawTextInRect("Reading what the device can do…",
                        Rect2Dd(kPad, kPad + 26.0, b.width - 2 * kPad, 16.0));

    Button(ctx, Rect2Dd(b.width - kPad - 90.0, b.height - kPad - 26.0, 90.0, 26.0),
           "Finish", kAccent, Colors::White);
}

void SmartHomePairingWizard::RenderComplete(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    ctx->SetTextPaint(kOk);
    ctx->SetFontSize(13.0);
    ctx->DrawTextInRect("Added " + (pairedDevice.Name.empty() ? pairedDevice.DeviceId
                                                             : pairedDevice.Name),
                        Rect2Dd(kPad, kPad, b.width - 2 * kPad, 18.0));
    Button(ctx, Rect2Dd(b.width - kPad - 90.0, b.height - kPad - 26.0, 90.0, 26.0),
           "Done", kTrack, kText);
}

void SmartHomePairingWizard::RenderError(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    ctx->SetTextPaint(kDanger);
    ctx->SetFontSize(13.0);
    ctx->DrawTextInRect("Pairing failed", Rect2Dd(kPad, kPad, b.width - 2 * kPad, 18.0));
    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(11.0);
    ctx->DrawTextInRect(errorMessage, Rect2Dd(kPad, kPad + 26.0, b.width - 2 * kPad, 32.0));

    Button(ctx, Rect2Dd(kPad, b.height - kPad - 26.0, 90.0, 26.0), "Back", kTrack, kText);
}

void SmartHomePairingWizard::Render(IRenderContext* ctx, const Rect2Df&) {
    if (!ctx || !IsVisible()) return;

    const Rect2Df b = GetLocalBounds();
    ctx->SetFillPaint(kSheet);
    ctx->FillRoundedRectangle(Rect2Dd(b), 10.0);
    ctx->SetStrokePaint(kTrack);
    ctx->SetStrokeWidth(1.0);
    ctx->DrawRoundedRectangle(Rect2Dd(b), 10.0);

    switch (currentStep) {
        case WizardStep::SelectProtocol: RenderProtocolSelection(ctx); break;
        case WizardStep::Searching:      RenderSearching(ctx);         break;
        case WizardStep::DeviceFound:    RenderDeviceFound(ctx);       break;
        case WizardStep::Configuring:    RenderConfiguring(ctx);       break;
        case WizardStep::Complete:       RenderComplete(ctx);          break;
        case WizardStep::Error:          RenderError(ctx);             break;
    }
}

bool SmartHomePairingWizard::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;

    const Rect2Df b = GetLocalBounds();
    const int x = event.pointer.x;
    const int y = event.pointer.y;

    if (event.type == UCEventType::KeyDown && event.virtualKey == UCKeys::Escape) {
        Cancel();
        return true;
    }
    if (event.type != UCEventType::MouseDown && event.type != UCEventType::TouchStart) {
        return false;
    }
    if (!Contains(Point2Df(static_cast<float>(x), static_cast<float>(y)))) return false;

    switch (currentStep) {
        case WizardStep::SelectProtocol: {
            const int index = static_cast<int>((y - (kPad + 30.0)) / 34.0);
            if (index >= 0 && index < static_cast<int>(supportedProtocols.size())) {
                SelectProtocol(supportedProtocols[static_cast<size_t>(index)]);
            }
            return true;
        }

        case WizardStep::Searching:
            if (Hit(Rect2Dd(kPad, b.height - kPad - 26.0, 90.0, 26.0), x, y)) Cancel();
            return true;

        case WizardStep::DeviceFound: {
            if (Hit(Rect2Dd(b.width - kPad - 90.0, b.height - kPad - 26.0, 90.0, 26.0), x, y)) {
                ConfigureDevice();
                return true;
            }
            const int index = static_cast<int>((y - (kPad + 30.0)) / 30.0);
            if (index >= 0 && index < static_cast<int>(discoveredDevices.size())) {
                selectedDeviceId = discoveredDevices[static_cast<size_t>(index)].DeviceId;
                RequestRedraw();
            }
            return true;
        }

        case WizardStep::Configuring:
            if (Hit(Rect2Dd(b.width - kPad - 90.0, b.height - kPad - 26.0, 90.0, 26.0), x, y)) {
                FinishPairing();
            }
            return true;

        case WizardStep::Complete:
            if (Hit(Rect2Dd(b.width - kPad - 90.0, b.height - kPad - 26.0, 90.0, 26.0), x, y)) {
                currentStep = WizardStep::SelectProtocol;
                RequestRedraw();
            }
            return true;

        case WizardStep::Error:
            if (Hit(Rect2Dd(kPad, b.height - kPad - 26.0, 90.0, 26.0), x, y)) {
                errorMessage.clear();
                currentStep = WizardStep::SelectProtocol;
                RequestRedraw();
            }
            return true;
    }
    return true;
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
