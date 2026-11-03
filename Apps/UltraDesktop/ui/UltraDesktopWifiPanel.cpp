// Apps/UltraDesktop/ui/UltraDesktopWifiPanel.cpp
// The Wi-Fi panel. See the header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopWifiPanel.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include <chrono>
#include <sstream>
#include <thread>

using namespace UltraCanvas;

namespace UltraDesktop {

namespace {
constexpr int kPanelWidth = 360;
constexpr int kPanelHeight = 470;
constexpr int kInnerWidth = kPanelWidth - 28;
constexpr unsigned kRefreshMs = 5000;
const Color kText(242, 242, 242);
const Color kMuted(160, 160, 160);
const Color kError(255, 140, 120);
const Color kRow(44, 44, 44);
const Color kRowJoined(30, 64, 110);

std::string Detail(const WirelessNetwork& network) {
    std::string text = std::to_string(network.signalPercent) + "% signal";
    text += network.secured ? " · " + network.security : " · open";
    if (network.connected) text += " · connected";
    else if (network.known) text += " · saved";
    return text;
}

std::shared_ptr<UltraCanvasButton> SmallButton(const std::string& id, const std::string& text, int width) {
    auto button = CreateButton(id, 0, 0, width, 26, text);
    button->layoutItem.SetFlexShrink(0.0f);
    return button;
}
} // namespace

UltraDesktopWifiPanel::UltraDesktopWifiPanel(std::function<void()> onChanged, std::function<void()> openMonitor)
    : UltraDesktopPopupPanel("Wi-Fi", kPanelWidth, kPanelHeight),
      onChanged_(std::move(onChanged)),
      openMonitor_(std::move(openMonitor)) {}

UltraDesktopWifiPanel::~UltraDesktopWifiPanel() {
    if (timer_) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(timer_);
    }
}

void UltraDesktopWifiPanel::Build(UltraCanvasWindow& window) {
    window.layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window.SetPadding(12, 14, 12, 14);

    auto header = std::make_shared<UltraCanvasContainer>("wifi.header");
    header->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    header->layoutItem.SetFlexShrink(0.0f);
    auto title = CreateLabel("wifi.title", 0, 0, 200, 22, "Wi-Fi");
    title->SetFontSize(11.5f);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(kText);
    title->layoutItem.SetFlexGrow(1.0f);
    header->AddChild(title);
    power_ = std::make_shared<UltraCanvasSwitch>("wifi.power", 0, 0, 70, 26, "On");
    power_->layoutItem.SetFlexShrink(0.0f);
    power_->onStateChanged = [this](CheckedState, CheckedState now) {
        if (loading_) return;
        const bool on = now == CheckedState::Checked;
        Act(on ? "Switching Wi-Fi on..." : "Switching Wi-Fi off...", [on]() {
            std::string error;
            UltraCanvasWireless::SetWifiEnabled(on, &error);
            if (on && error.empty()) {
                // The adapter needs a moment before it can scan.
                std::this_thread::sleep_for(std::chrono::seconds(2));
                UltraCanvasWireless::RequestWifiScan();
            }
            return error;
        });
    };
    header->AddChild(power_);
    window.AddChild(header);

    state_ = CreateLabel("wifi.state", 0, 0, kInnerWidth, 18, "Reading...");
    state_->SetFontSize(9.5f);
    state_->SetTextColor(kMuted);
    state_->layoutItem.SetFlexShrink(0.0f);
    window.AddChild(state_);

    list_ = CreateScrollableContainer("wifi.list", 0, 0, kInnerWidth, 200, true, false);
    list_->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    list_->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
    window.AddChild(list_);

    passwordRow_ = std::make_shared<UltraCanvasContainer>("wifi.passwordRow");
    passwordRow_->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    passwordRow_->layoutItem.SetFlexShrink(0.0f);
    passwordLabel_ = CreateLabel("wifi.passwordLabel", 0, 0, kInnerWidth, 18, "");
    passwordLabel_->SetFontSize(9.5f);
    passwordLabel_->SetTextColor(kText);
    passwordRow_->AddChild(passwordLabel_);
    auto entry = std::make_shared<UltraCanvasContainer>("wifi.passwordEntry");
    entry->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    password_ = CreatePasswordInput("wifi.password", 0, 0, 180, 28);
    password_->SetPlaceholder("Password");
    password_->layoutItem.SetFlexGrow(1.0f);
    password_->onEnterPressed = [this](const std::string&) {
        Later([this]() { SubmitPassword(); });
        return true;
    };
    entry->AddChild(password_);
    auto join = SmallButton("wifi.join", "Join", 56);
    join->onClick = [this]() { Later([this]() { SubmitPassword(); }); };
    entry->AddChild(join);
    auto cancel = SmallButton("wifi.cancel", "Cancel", 64);
    cancel->onClick = [this]() { Later([this]() { HidePassword(); }); };
    entry->AddChild(cancel);
    passwordRow_->AddChild(entry);
    passwordRow_->SetVisible(false);
    window.AddChild(passwordRow_);

    message_ = CreateLabel("wifi.message", 0, 0, kInnerWidth, 34, "");
    message_->SetFontSize(9.0f);
    message_->SetTextColor(kMuted);
    message_->SetWrap(TextWrap::WrapWord);
    message_->layoutItem.SetFlexShrink(0.0f);
    window.AddChild(message_);

    auto footer = std::make_shared<UltraCanvasContainer>("wifi.footer");
    footer->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    footer->layoutItem.SetFlexShrink(0.0f);
    scan_ = SmallButton("wifi.scan", "Scan", 64);
    scan_->onClick = [this]() { Refresh(true); };
    footer->AddChild(scan_);
    if (openMonitor_) {
        auto monitor = SmallButton("wifi.monitor", "Traffic", 70);
        monitor->SetTooltip("UltraNetMonitor: what goes over the network");
        monitor->onClick = [this]() {
            Later([this]() {
                if (openMonitor_) openMonitor_();
                Close();
            });
        };
        footer->AddChild(monitor);
    }
    if (UltraCanvasWireless::HasSystemSettings(WirelessRadio::WiFi)) {
        auto settings = SmallButton("wifi.settings", "System settings", 120);
        settings->onClick = [this]() {
            std::string error;
            if (!UltraCanvasWireless::OpenSystemSettings(WirelessRadio::WiFi, &error)) SetMessage(error, true);
            else Later([this]() { Close(); });
        };
        footer->AddChild(settings);
    }
    window.AddChild(footer);

    listSignature_.clear();
    passwordFor_.clear();
    if (haveStatus_) Apply(status_);
    Refresh(true);
    if (auto* app = UltraCanvasApplication::GetInstance()) {
        if (timer_) app->StopTimer(timer_);
        timer_ = app->StartTimer(kRefreshMs, true, [this](TimerId) {
            if (IsOpen() && !busy_) Refresh(false);
        });
    }
}

void UltraDesktopWifiPanel::Unbuild() {
    if (timer_) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(timer_);
        timer_ = 0;
    }
    power_.reset();
    state_.reset();
    list_.reset();
    passwordRow_.reset();
    passwordLabel_.reset();
    password_.reset();
    message_.reset();
    scan_.reset();
}

void UltraDesktopWifiPanel::Later(std::function<void()> task) {
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->PostToUIThread(std::move(task));
}

void UltraDesktopWifiPanel::SetMessage(const std::string& text, bool error) {
    if (!message_) return;
    message_->SetText(text);
    message_->SetTextColor(error ? kError : kMuted);
}

void UltraDesktopWifiPanel::Refresh(bool scan) {
    if (reading_) return;
    reading_ = true;
    if (scan && scan_) scan_->SetDisabled(true);
    work_.Run<WirelessWifiStatus>(
        [scan]() {
            if (scan && UltraCanvasWireless::RequestWifiScan()) {
                // The networks a scan finds show a few seconds later.
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
            return UltraCanvasWireless::ReadWifi();
        },
        [this, scan](WirelessWifiStatus status) {
            reading_ = false;
            if (scan && scan_) scan_->SetDisabled(false);
            status_ = std::move(status);
            haveStatus_ = true;
            if (IsOpen()) Apply(status_);
        });
}

void UltraDesktopWifiPanel::Apply(const WirelessWifiStatus& status) {
    if (!state_ || !power_) return;
    loading_ = true;
    power_->SetChecked(status.enabled);
    power_->SetDisabled(busy_ || !status.adapterPresent || status.hardwareBlocked);
    loading_ = false;

    std::string state;
    if (!status.available || !status.adapterPresent) {
        // The reason can be long; the state line stays one line and the
        // message line, which wraps, says why.
        state = status.available ? "No Wi-Fi adapter." : "Wi-Fi cannot be reached.";
        if (!busy_ && !status.error.empty()) SetMessage(status.error, !status.available);
    } else if (status.hardwareBlocked) {
        state = "Wi-Fi is off: the computer's wireless switch or key has it off.";
    } else if (!status.enabled) {
        state = "Wi-Fi is off.";
    } else if (!status.connectedSsid.empty()) {
        state = "Connected to " + status.connectedSsid;
    } else {
        state = "Not connected";
    }
    if (!status.adapterName.empty() && status.adapterPresent) state += "  (" + status.adapterName + ")";
    state_->SetText(state);
    if (!busy_ && status.available && !status.error.empty() && status.adapterPresent) SetMessage(status.error, true);
    if (scan_) scan_->SetDisabled(busy_ || reading_ || !status.enabled);
    RebuildList();
}

void UltraDesktopWifiPanel::RebuildList() {
    if (!list_) return;
    // Rebuilt only when something shown changed: the rows are buttons the
    // person may be about to click.
    std::ostringstream signature;
    signature << busy_ << '|' << status_.enabled << '|' << status_.connectedSsid << '\n';
    for (const WirelessNetwork& n : status_.networks) {
        signature << n.ssid << '|' << n.signalPercent / 10 << '|' << n.known << '|' << n.connected << '\n';
    }
    if (signature.str() == listSignature_) return;
    listSignature_ = signature.str();

    list_->ClearChildren();
    if (!status_.enabled || status_.networks.empty()) {
        auto none = CreateLabel("wifi.none", 0, 0, kInnerWidth - 20, 40,
                                !status_.enabled ? "" : reading_ ? "Looking for networks..." : "No networks in reach.");
        none->SetTextColor(kMuted);
        none->SetFontSize(9.5f);
        list_->AddChild(none);
    }
    int index = 0;
    if (status_.enabled) {
        for (const WirelessNetwork& network : status_.networks) {
            const std::string id = "wifi.net" + std::to_string(index++);
            auto row = CreateContainer(id, 0, 0, 0, 46);
            row->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
            row->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(46));
            row->layoutItem.SetFlexShrink(0.0f);
            row->SetBackgroundColor(network.connected ? kRowJoined : kRow);
            row->SetBorders(0, Colors::Transparent, 6);
            row->SetPadding(4, 8);

            auto texts = std::make_shared<UltraCanvasContainer>(id + ".texts");
            texts->layout.SetFlexColumn().SetFlexGap(1).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
            texts->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
            auto name = CreateLabel(id + ".name", 0, 0, 150, 18, network.ssid);
            name->SetTextColor(kText);
            name->SetFontSize(10.0f);
            if (network.connected) name->SetFontWeight(FontWeight::Bold);
            texts->AddChild(name);
            auto detail = CreateLabel(id + ".detail", 0, 0, 150, 16, Detail(network));
            detail->SetTextColor(kMuted);
            detail->SetFontSize(8.5f);
            texts->AddChild(detail);
            row->AddChild(texts);

            const WirelessNetwork copy = network;
            if (network.connected) {
                auto leave = SmallButton(id + ".leave", "Disconnect", 84);
                leave->onClick = [this]() {
                    Later([this]() {
                        Act("Leaving the network...", []() {
                            std::string error;
                            UltraCanvasWireless::DisconnectWifi(&error);
                            return error;
                        });
                    });
                };
                leave->SetDisabled(busy_);
                row->AddChild(leave);
            } else {
                auto join = SmallButton(id + ".join", "Connect", 72);
                join->onClick = [this, copy]() { Later([this, copy]() { Connect(copy); }); };
                join->SetDisabled(busy_);
                row->AddChild(join);
            }
            if (network.known) {
                auto forget = SmallButton(id + ".forget", "Forget", 60);
                forget->SetTooltip("Delete the saved password: the next Connect asks for it again");
                const std::string ssid = network.ssid;
                forget->onClick = [this, ssid]() {
                    Later([this, ssid]() {
                        Act("Forgetting " + ssid + "...", [ssid]() {
                            std::string error;
                            UltraCanvasWireless::ForgetWifi(ssid, &error);
                            return error;
                        });
                    });
                };
                forget->SetDisabled(busy_);
                row->AddChild(forget);
            }
            list_->AddChild(row);
        }
    }
    list_->InvalidateLayout();
    list_->RequestRedraw();
}

void UltraDesktopWifiPanel::Act(const std::string& busyText, std::function<std::string()> action) {
    if (busy_) return;
    busy_ = true;
    SetMessage(busyText, false);
    Apply(status_);
    work_.Run<std::pair<std::string, WirelessWifiStatus>>(
        [action = std::move(action)]() {
            std::string error = action();
            return std::make_pair(error, UltraCanvasWireless::ReadWifi());
        },
        [this](std::pair<std::string, WirelessWifiStatus> result) {
            busy_ = false;
            status_ = std::move(result.second);
            haveStatus_ = true;
            if (!result.first.empty()) debugOutput << "UltraDesktop: Wi-Fi: " << result.first << std::endl;
            if (IsOpen()) {
                SetMessage(result.first, !result.first.empty());
                listSignature_.clear();
                Apply(status_);
            }
            if (onChanged_) onChanged_();
        });
}

void UltraDesktopWifiPanel::Connect(const WirelessNetwork& network) {
    if (network.secured && !network.known) {
        AskPassword(network.ssid);
        return;
    }
    HidePassword();
    const std::string ssid = network.ssid;
    Act("Joining " + ssid + "...", [ssid]() {
        std::string error;
        UltraCanvasWireless::ConnectWifi(ssid, "", &error);
        return error;
    });
}

void UltraDesktopWifiPanel::AskPassword(const std::string& ssid) {
    if (!passwordRow_ || !password_) return;
    passwordFor_ = ssid;
    passwordLabel_->SetText("Password for " + ssid);
    password_->SetText("");
    passwordRow_->SetVisible(true);
    if (auto* window = Window()) window->InvalidateLayout();
    password_->SetFocus(true);
    SetMessage("", false);
}

void UltraDesktopWifiPanel::HidePassword() {
    passwordFor_.clear();
    if (password_) password_->SetText("");
    if (passwordRow_) passwordRow_->SetVisible(false);
    if (auto* window = Window()) window->InvalidateLayout();
}

void UltraDesktopWifiPanel::SubmitPassword() {
    if (!password_ || passwordFor_.empty()) return;
    const std::string ssid = passwordFor_;
    const std::string passphrase = password_->GetText();
    if (passphrase.empty()) {
        SetMessage(ssid + " needs its password.", true);
        return;
    }
    HidePassword();
    Act("Joining " + ssid + "...", [ssid, passphrase]() {
        std::string error;
        UltraCanvasWireless::ConnectWifi(ssid, passphrase, &error);
        return error;
    });
}

} // namespace UltraDesktop
