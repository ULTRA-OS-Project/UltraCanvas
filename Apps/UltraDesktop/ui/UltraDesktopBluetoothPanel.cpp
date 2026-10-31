// Apps/UltraDesktop/ui/UltraDesktopBluetoothPanel.cpp
// The Bluetooth panel. See the header.
// Version: 0.2.0 - the pairing prompt
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopBluetoothPanel.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include <chrono>
#include <future>
#include <mutex>
#include <sstream>

using namespace UltraCanvas;

namespace UltraDesktop {

namespace {
constexpr int kPanelWidth = 360;
constexpr int kPanelHeight = 470;
constexpr int kInnerWidth = kPanelWidth - 28;
constexpr unsigned kRefreshMs = 5000;
constexpr unsigned kDiscoveringRefreshMs = 2000;
const Color kText(242, 242, 242);
const Color kMuted(160, 160, 160);
const Color kError(255, 140, 120);
const Color kRow(44, 44, 44);
const Color kRowConnected(30, 64, 110);

std::string Detail(const WirelessBluetoothDevice& device) {
    std::string text = device.kind.empty() ? "Device" : device.kind;
    // Connected or not is the row's colour and its button; the line keeps
    // its room for the battery.
    if (device.batteryPercent) text += " · battery " + std::to_string(*device.batteryPercent) + "%";
    if (!device.paired && device.rssiDbm) text += " · " + std::to_string(*device.rssiDbm) + " dBm";
    return text;
}

std::shared_ptr<UltraCanvasButton> SmallButton(const std::string& id, const std::string& text, int width) {
    auto button = CreateButton(id, 0, 0, width, 26, text);
    button->layoutItem.SetFlexShrink(0.0f);
    return button;
}
} // namespace

// What the pairing handler's thread and the panel share: the answer the
// thread waits for, and whether the panel is still there to give it.
struct UltraDesktopBluetoothPanel::PairingPrompt {
    std::mutex mutex;
    std::shared_ptr<std::promise<WirelessPairingAnswer>> pending;
    bool alive = true;

    // The waiting request, taken out so it is answered exactly once.
    std::shared_ptr<std::promise<WirelessPairingAnswer>> Take() {
        std::lock_guard<std::mutex> lock(mutex);
        auto taken = std::move(pending);
        pending.reset();
        return taken;
    }
};

namespace {
bool Waits(WirelessPairingStep step) {
    return step == WirelessPairingStep::ConfirmCode || step == WirelessPairingStep::EnterCode ||
           step == WirelessPairingStep::EnterPin || step == WirelessPairingStep::Authorize;
}
} // namespace

UltraDesktopBluetoothPanel::UltraDesktopBluetoothPanel(std::function<void()> onChanged)
    : UltraDesktopPopupPanel("Bluetooth", kPanelWidth, kPanelHeight),
      onChanged_(std::move(onChanged)),
      pairing_(std::make_shared<PairingPrompt>()) {
    UltraCanvasWireless::SetPairingHandler([prompt = pairing_, panel = this](const WirelessPairingRequest& request) {
        return HandlePairing(prompt, panel, request);
    });
}

UltraDesktopBluetoothPanel::~UltraDesktopBluetoothPanel() {
    UltraCanvasWireless::SetPairingHandler(nullptr);
    {
        std::lock_guard<std::mutex> lock(pairing_->mutex);
        pairing_->alive = false;
    }
    if (auto pending = pairing_->Take()) pending->set_value({});
    if (timer_) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(timer_);
    }
}

// On the backend's thread. `panel` is touched only on the UI thread, and
// only while the shared state says it is alive - the destructor, also on
// the UI thread, says otherwise first.
WirelessPairingAnswer UltraDesktopBluetoothPanel::HandlePairing(const std::shared_ptr<PairingPrompt>& prompt,
                                                                UltraDesktopBluetoothPanel* panel,
                                                                const WirelessPairingRequest& request) {
    auto* app = UltraCanvasApplicationBase::GetCurrent();
    if (!app) return {};
    std::shared_ptr<std::promise<WirelessPairingAnswer>> promise;
    std::future<WirelessPairingAnswer> answer;
    {
        std::lock_guard<std::mutex> lock(prompt->mutex);
        if (!prompt->alive) return {};
        if (Waits(request.step)) {
            if (prompt->pending) prompt->pending->set_value({});   // a request BlueZ gave up on
            promise = std::make_shared<std::promise<WirelessPairingAnswer>>();
            answer = promise->get_future();
            prompt->pending = promise;
        }
    }
    app->PostToUIThread([panel, prompt, request]() {
        bool alive = false;
        {
            std::lock_guard<std::mutex> lock(prompt->mutex);
            alive = prompt->alive;
        }
        if (alive) panel->ShowPrompt(request);
    });
    if (!promise) return {};
    if (answer.wait_for(std::chrono::seconds(90)) != std::future_status::ready) {
        std::lock_guard<std::mutex> lock(prompt->mutex);
        if (prompt->pending == promise) prompt->pending.reset();
        return {};
    }
    return answer.get();
}

void UltraDesktopBluetoothPanel::ShowPrompt(const WirelessPairingRequest& request) {
    if (request.step == WirelessPairingStep::Finished) {
        HidePrompt();
        return;
    }
    if (!promptRow_ || !IsOpen()) {
        // Pairing starts from the panel; closed, there is no one to ask.
        AnswerPrompt(false);
        return;
    }
    promptStep_ = request.step;
    const std::string name = !request.name.empty() ? request.name : !request.address.empty() ? request.address
                                                                                                : "the device";
    std::string text;
    bool input = false;
    std::string yes = "OK", no = "Cancel";
    switch (request.step) {
        case WirelessPairingStep::ShowCode:
            text = "Type this code on " + name + ", then press Enter:";
            break;
        case WirelessPairingStep::ConfirmCode:
            text = "Does " + name + " show this code?";
            yes = "Yes, pair";
            no = "No";
            break;
        case WirelessPairingStep::Authorize:
            text = "Pair with " + name + "?";
            yes = "Pair";
            break;
        case WirelessPairingStep::EnterCode:
            text = "Type the six-digit code " + name + " shows:";
            input = true;
            break;
        case WirelessPairingStep::EnterPin:
            text = "The PIN of " + name + " (often 0000 or 1234 - see its manual):";
            input = true;
            break;
        case WirelessPairingStep::Finished:
            break;
    }
    promptText_->SetText(text);
    promptCode_->SetText(request.code);
    promptCode_->SetVisible(!request.code.empty());
    promptInput_->SetText("");
    promptInput_->SetVisible(input);
    const bool answers = Waits(request.step);
    promptYes_->SetText(yes);
    promptNo_->SetText(no);
    promptYes_->SetVisible(answers);
    promptNo_->SetVisible(answers);
    promptRow_->SetVisible(true);
    if (auto* window = Window()) window->InvalidateLayout();
    if (input) promptInput_->SetFocus(true);
    if (Window()) Window()->RaiseAndFocus();
}

void UltraDesktopBluetoothPanel::HidePrompt() {
    promptStep_ = WirelessPairingStep::Finished;
    if (promptRow_) promptRow_->SetVisible(false);
    if (auto* window = Window()) window->InvalidateLayout();
}

void UltraDesktopBluetoothPanel::AnswerPrompt(bool accept) {
    WirelessPairingAnswer answer;
    answer.accept = accept;
    if (accept && promptInput_ && promptInput_->IsVisible()) {
        answer.code = promptInput_->GetText();
        if (answer.code.empty()) {
            SetMessage("Type the code first.", true);
            return;
        }
    }
    if (auto pending = pairing_->Take()) pending->set_value(answer);
    // A shown code stays until the pairing ends; an answered question goes.
    if (promptStep_ != WirelessPairingStep::ShowCode) HidePrompt();
}

void UltraDesktopBluetoothPanel::Build(UltraCanvasWindow& window) {
    window.layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window.SetPadding(12, 14, 12, 14);

    auto header = std::make_shared<UltraCanvasContainer>("bt.header");
    header->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    header->layoutItem.SetFlexShrink(0.0f);
    auto title = CreateLabel("bt.title", 0, 0, 200, 22, "Bluetooth");
    title->SetFontSize(11.5f);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(kText);
    title->layoutItem.SetFlexGrow(1.0f);
    header->AddChild(title);
    power_ = std::make_shared<UltraCanvasSwitch>("bt.power", 0, 0, 70, 26, "On");
    power_->layoutItem.SetFlexShrink(0.0f);
    power_->onStateChanged = [this](CheckedState, CheckedState now) {
        if (loading_) return;
        const bool on = now == CheckedState::Checked;
        Act(on ? "Switching Bluetooth on..." : "Switching Bluetooth off...", [on]() {
            std::string error;
            UltraCanvasWireless::SetBluetoothPowered(on, &error);
            return error;
        });
    };
    if (!UltraCanvasWireless::CanSetBluetoothPower()) {
        power_->SetTooltip("This system switches Bluetooth in its own settings");
    }
    header->AddChild(power_);
    window.AddChild(header);

    state_ = CreateLabel("bt.state", 0, 0, kInnerWidth, 18, "Reading...");
    state_->SetFontSize(9.5f);
    state_->SetTextColor(kMuted);
    state_->layoutItem.SetFlexShrink(0.0f);
    window.AddChild(state_);

    list_ = CreateScrollableContainer("bt.list", 0, 0, kInnerWidth, 200, true, false);
    list_->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    list_->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
    window.AddChild(list_);

    promptRow_ = std::make_shared<UltraCanvasContainer>("bt.prompt");
    promptRow_->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    promptRow_->layoutItem.SetFlexShrink(0.0f);
    promptRow_->SetBackgroundColor(kRow);
    promptRow_->SetBorders(0, Colors::Transparent, 6);
    promptRow_->SetPadding(8, 10);
    promptText_ = CreateLabel("bt.promptText", 0, 0, kInnerWidth - 20, 32, "");
    promptText_->SetTextColor(kText);
    promptText_->SetFontSize(9.5f);
    promptText_->SetWrap(TextWrap::WrapWord);
    promptRow_->AddChild(promptText_);
    promptCode_ = CreateLabel("bt.promptCode", 0, 0, kInnerWidth - 20, 34, "");
    promptCode_->SetTextColor(kText);
    promptCode_->SetFontSize(20.0f);
    promptCode_->SetFontWeight(FontWeight::Bold);
    promptCode_->SetAlignment(TextAlignment::Center);
    promptRow_->AddChild(promptCode_);
    auto answers = std::make_shared<UltraCanvasContainer>("bt.promptAnswers");
    answers->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    promptInput_ = CreateTextInput("bt.promptInput", 0, 0, 120, 28);
    promptInput_->SetPlaceholder("Code");
    promptInput_->layoutItem.SetFlexGrow(1.0f);
    promptInput_->onEnterPressed = [this](const std::string&) {
        Later([this]() { AnswerPrompt(true); });
        return true;
    };
    answers->AddChild(promptInput_);
    promptYes_ = SmallButton("bt.promptYes", "OK", 76);
    promptYes_->onClick = [this]() { Later([this]() { AnswerPrompt(true); }); };
    answers->AddChild(promptYes_);
    promptNo_ = SmallButton("bt.promptNo", "Cancel", 64);
    promptNo_->onClick = [this]() { Later([this]() { AnswerPrompt(false); }); };
    answers->AddChild(promptNo_);
    promptRow_->AddChild(answers);
    promptRow_->SetVisible(false);
    window.AddChild(promptRow_);

    message_ = CreateLabel("bt.message", 0, 0, kInnerWidth, 34, "");
    message_->SetFontSize(9.0f);
    message_->SetTextColor(kMuted);
    message_->SetWrap(TextWrap::WrapWord);
    message_->layoutItem.SetFlexShrink(0.0f);
    window.AddChild(message_);

    auto footer = std::make_shared<UltraCanvasContainer>("bt.footer");
    footer->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    footer->layoutItem.SetFlexShrink(0.0f);
    discover_ = SmallButton("bt.discover", "Add device", 96);
    discover_->SetTooltip("Look for devices in pairing mode");
    discover_->onClick = [this]() {
        const bool stop = status_.discovering;
        Later([this, stop]() {
            Act(stop ? "Stopping the search..." : "Looking for devices in pairing mode...", [stop]() {
                std::string error;
                if (stop) UltraCanvasWireless::StopBluetoothDiscovery(&error);
                else UltraCanvasWireless::StartBluetoothDiscovery(&error);
                return error;
            });
        });
    };
    footer->AddChild(discover_);
    if (UltraCanvasWireless::HasSystemSettings(WirelessRadio::Bluetooth)) {
        auto settings = SmallButton("bt.settings", "System settings", 120);
        settings->onClick = [this]() {
            std::string error;
            if (!UltraCanvasWireless::OpenSystemSettings(WirelessRadio::Bluetooth, &error)) SetMessage(error, true);
            else Later([this]() { Close(); });
        };
        footer->AddChild(settings);
    }
    window.AddChild(footer);

    listSignature_.clear();
    if (haveStatus_) Apply(status_);
    Refresh();
    RestartTimer(kRefreshMs);
}

void UltraDesktopBluetoothPanel::Unbuild() {
    if (timer_) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(timer_);
        timer_ = 0;
        timerInterval_ = 0;
    }
    // Nobody is watching the search any more.
    if (status_.discovering) {
        work_.Run<bool>([]() { return UltraCanvasWireless::StopBluetoothDiscovery(); }, [](bool) {});
    }
    // A question still open has no one to answer it now.
    if (auto pending = pairing_->Take()) pending->set_value({});
    promptStep_ = WirelessPairingStep::Finished;
    power_.reset();
    state_.reset();
    list_.reset();
    message_.reset();
    discover_.reset();
    promptRow_.reset();
    promptText_.reset();
    promptCode_.reset();
    promptInput_.reset();
    promptYes_.reset();
    promptNo_.reset();
}

void UltraDesktopBluetoothPanel::RestartTimer(unsigned intervalMs) {
    if (timer_ && timerInterval_ == intervalMs) return;
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    if (timer_) app->StopTimer(timer_);
    timerInterval_ = intervalMs;
    timer_ = app->StartTimer(intervalMs, true, [this](TimerId) {
        if (IsOpen() && !busy_) Refresh();
    });
}

void UltraDesktopBluetoothPanel::Later(std::function<void()> task) {
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->PostToUIThread(std::move(task));
}

void UltraDesktopBluetoothPanel::SetMessage(const std::string& text, bool error) {
    if (!message_) return;
    message_->SetText(text);
    message_->SetTextColor(error ? kError : kMuted);
}

void UltraDesktopBluetoothPanel::Refresh() {
    if (reading_) return;
    reading_ = true;
    work_.Run<WirelessBluetoothStatus>([]() { return UltraCanvasWireless::ReadBluetooth(); },
                                       [this](WirelessBluetoothStatus status) {
                                           reading_ = false;
                                           status_ = std::move(status);
                                           haveStatus_ = true;
                                           if (IsOpen()) Apply(status_);
                                       });
}

void UltraDesktopBluetoothPanel::Apply(const WirelessBluetoothStatus& status) {
    if (!state_ || !power_) return;
    loading_ = true;
    power_->SetChecked(status.powered);
    power_->SetDisabled(busy_ || !status.adapterPresent || !UltraCanvasWireless::CanSetBluetoothPower());
    loading_ = false;

    std::string state;
    if (!status.available || !status.adapterPresent) {
        // The reason can be long; the state line stays one line and the
        // message line, which wraps, says why.
        state = status.available ? "No Bluetooth adapter." : "Bluetooth cannot be reached.";
        if (!busy_ && !status.error.empty()) SetMessage(status.error, !status.available);
    } else if (!status.powered) {
        state = "Bluetooth is off.";
    } else if (status.discovering) {
        state = "Looking for devices in pairing mode...";
    } else {
        int connected = 0;
        for (const WirelessBluetoothDevice& d : status.devices) connected += d.connected ? 1 : 0;
        state = connected == 0 ? "On, nothing connected"
                               : "On, " + std::to_string(connected) + (connected == 1 ? " device" : " devices") +
                                     " connected";
    }
    if (!status.adapterName.empty() && status.adapterPresent) state += "  (" + status.adapterName + ")";
    state_->SetText(state);
    if (discover_) {
        discover_->SetText(status.discovering ? "Stop search" : "Add device");
        discover_->SetDisabled(busy_ || !status.powered);
    }
    RestartTimer(status.discovering ? kDiscoveringRefreshMs : kRefreshMs);
    RebuildList();
}

void UltraDesktopBluetoothPanel::AddSectionLabel(const std::string& id, const std::string& text) {
    auto label = CreateLabel(id, 0, 0, kInnerWidth - 20, 18, text);
    label->SetTextColor(kMuted);
    label->SetFontSize(9.0f);
    label->SetFontWeight(FontWeight::Bold);
    label->layoutItem.SetFlexShrink(0.0f);
    list_->AddChild(label);
}

void UltraDesktopBluetoothPanel::AddDeviceRow(const WirelessBluetoothDevice& device, int index) {
    const std::string id = "bt.dev" + std::to_string(index);
    auto row = CreateContainer(id, 0, 0, 0, 46);
    row->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(46));
    row->layoutItem.SetFlexShrink(0.0f);
    row->SetBackgroundColor(device.connected ? kRowConnected : kRow);
    row->SetBorders(0, Colors::Transparent, 6);
    row->SetPadding(4, 8);

    auto texts = std::make_shared<UltraCanvasContainer>(id + ".texts");
    texts->layout.SetFlexColumn().SetFlexGap(1).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    texts->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
    auto name = CreateLabel(id + ".name", 0, 0, 150, 18, device.name);
    name->SetTextColor(kText);
    name->SetFontSize(10.0f);
    if (device.connected) name->SetFontWeight(FontWeight::Bold);
    name->SetTooltip(device.address);
    texts->AddChild(name);
    auto detail = CreateLabel(id + ".detail", 0, 0, 150, 16, Detail(device));
    detail->SetTextColor(kMuted);
    detail->SetFontSize(8.5f);
    texts->AddChild(detail);
    row->AddChild(texts);

    const std::string address = device.address;
    const std::string label = device.name;
    if (!device.paired) {
        auto pair = SmallButton(id + ".pair", "Pair", 56);
        pair->onClick = [this, address, label]() {
            Later([this, address, label]() {
                Act("Pairing with " + label + "... Keep the device in pairing mode.", [address]() {
                    std::string error;
                    UltraCanvasWireless::PairBluetoothDevice(address, &error);
                    return error;
                });
            });
        };
        pair->SetDisabled(busy_ || !status_.powered);
        row->AddChild(pair);
    } else {
        const bool connected = device.connected;
        auto toggle = SmallButton(id + ".link", connected ? "Disconnect" : "Connect", connected ? 84 : 72);
        toggle->onClick = [this, address, label, connected]() {
            Later([this, address, label, connected]() {
                Act((connected ? "Disconnecting " : "Connecting ") + label + "...", [address, connected]() {
                    std::string error;
                    if (connected) UltraCanvasWireless::DisconnectBluetoothDevice(address, &error);
                    else UltraCanvasWireless::ConnectBluetoothDevice(address, &error);
                    return error;
                });
            });
        };
        toggle->SetDisabled(busy_ || !status_.powered);
        row->AddChild(toggle);
        auto remove = SmallButton(id + ".remove", "Remove", 64);
        remove->SetTooltip("Unpair: the device has to be paired again to be used");
        remove->onClick = [this, address, label]() {
            Later([this, address, label]() {
                Act("Removing " + label + "...", [address]() {
                    std::string error;
                    UltraCanvasWireless::RemoveBluetoothDevice(address, &error);
                    return error;
                });
            });
        };
        remove->SetDisabled(busy_);
        row->AddChild(remove);
    }
    list_->AddChild(row);
}

void UltraDesktopBluetoothPanel::RebuildList() {
    if (!list_) return;
    std::ostringstream signature;
    signature << busy_ << '|' << status_.powered << '|' << status_.discovering << '\n';
    for (const WirelessBluetoothDevice& d : status_.devices) {
        signature << d.address << '|' << d.name << '|' << d.paired << '|' << d.connected << '|'
                  << d.batteryPercent.value_or(-1) << '\n';
    }
    if (signature.str() == listSignature_) return;
    listSignature_ = signature.str();

    list_->ClearChildren();
    int index = 0;
    bool anyPaired = false, anyFound = false;
    for (const WirelessBluetoothDevice& device : status_.devices) {
        if (!device.paired) continue;
        if (!anyPaired) AddSectionLabel("bt.paired", "My devices");
        anyPaired = true;
        AddDeviceRow(device, index++);
    }
    for (const WirelessBluetoothDevice& device : status_.devices) {
        if (device.paired) continue;
        if (!anyFound) AddSectionLabel("bt.found", "Found nearby - put a device in pairing mode, then Pair");
        anyFound = true;
        AddDeviceRow(device, index++);
    }
    if (!anyPaired && !anyFound && status_.adapterPresent) {
        auto none = CreateLabel("bt.none", 0, 0, kInnerWidth - 20, 40,
                                !status_.powered ? "" : "No devices paired yet. Add device looks for new ones.");
        none->SetTextColor(kMuted);
        none->SetFontSize(9.5f);
        none->SetWrap(TextWrap::WrapWord);
        list_->AddChild(none);
    } else if (status_.discovering && !anyFound) {
        AddSectionLabel("bt.searching", "Looking for devices in pairing mode...");
    }
    list_->InvalidateLayout();
    list_->RequestRedraw();
}

void UltraDesktopBluetoothPanel::Act(const std::string& busyText, std::function<std::string()> action) {
    if (busy_) return;
    busy_ = true;
    SetMessage(busyText, false);
    Apply(status_);
    work_.Run<std::pair<std::string, WirelessBluetoothStatus>>(
        [action = std::move(action)]() {
            std::string error = action();
            return std::make_pair(error, UltraCanvasWireless::ReadBluetooth());
        },
        [this](std::pair<std::string, WirelessBluetoothStatus> result) {
            busy_ = false;
            status_ = std::move(result.second);
            haveStatus_ = true;
            if (!result.first.empty()) debugOutput << "UltraDesktop: Bluetooth: " << result.first << std::endl;
            if (IsOpen()) {
                SetMessage(result.first, !result.first.empty());
                listSignature_.clear();
                Apply(status_);
            }
            if (onChanged_) onChanged_();
        });
}

} // namespace UltraDesktop
