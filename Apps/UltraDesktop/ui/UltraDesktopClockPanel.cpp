// Apps/UltraDesktop/ui/UltraDesktopClockPanel.cpp
// The clock panel. See the header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopClockPanel.h"

#include "Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDatePicker.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasWindow.h"

#include <cstdio>
#include <ctime>

using namespace UltraCanvas;

namespace UltraDesktop {

namespace {

constexpr int kPanelWidth = 320;
constexpr int kPanelHeight = 600;
const Color kText(242, 242, 242);
const Color kMuted(160, 160, 160);
const Color kFace(236, 236, 240);

const char* const kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* const kMonths[] = {"January", "February", "March", "April", "May", "June", "July",
                               "August", "September", "October", "November", "December"};

std::tm Now() {
    const std::time_t now = std::time(nullptr);
    std::tm local = {};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return local;
}

} // namespace

std::string TodayInWords() {
    const std::tm t = Now();
    return std::string(kDays[t.tm_wday % 7]) + ", " + std::to_string(t.tm_mday) + " " + kMonths[t.tm_mon % 12] +
           " " + std::to_string(t.tm_year + 1900);
}

std::string TimeNow(bool withSeconds) {
    const std::tm t = Now();
    char buffer[16];
    if (withSeconds) std::snprintf(buffer, sizeof buffer, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    else std::snprintf(buffer, sizeof buffer, "%02d:%02d", t.tm_hour, t.tm_min);
    return buffer;
}

std::string TodayShort() {
    const std::tm t = Now();
    return std::to_string(t.tm_mday) + " " + std::string(kMonths[t.tm_mon % 12]).substr(0, 3);
}

UltraDesktopClockPanel::UltraDesktopClockPanel() : UltraDesktopPopupPanel("Clock", kPanelWidth, kPanelHeight) {}

void UltraDesktopClockPanel::Build(UltraCanvasWindow& window) {
    window.layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window.SetPadding(10, 12, 12, 12);

    // The face: the gauge's analog clock, ticking on its own timer.
    auto face = CreateGaugeDiagramElement("clock.face", 0, 0, kPanelWidth - 24, 250);
    face->SetMode(GaugeMode::AnalogClock);
    face->SetBackgroundColor(kFace);
    face->SetTextColor(kText);
    face->SetShowLabels(true);
    face->layoutItem.SetFlexShrink(0.0f);
    face->SetElementSize(Size2Df(static_cast<float>(kPanelWidth - 24), 250));
    window.AddChild(face);

    // The same time in figures: the gauge's digital panel in clock mode.
    auto digits = CreateGaugeDiagramElement("clock.digits", 0, 0, kPanelWidth - 24, 72);
    digits->SetMode(GaugeMode::Digital);
    digits->SetDigitalClock(true);
    digits->SetDigitalFontFamily("DSEG7 Classic,DSEG7 Modern,Digital-7,Monospace");
    digits->SetGaugeColor(Color(120, 220, 160, 255));
    digits->SetBackgroundColor(Color(20, 20, 20, 255));
    digits->SetTextColor(kText);
    digits->layoutItem.SetFlexShrink(0.0f);
    digits->SetElementSize(Size2Df(static_cast<float>(kPanelWidth - 24), 72));
    window.AddChild(digits);

    auto today = CreateLabel("clock.today", 0, 0, kPanelWidth - 24, 24, TodayInWords());
    today->SetFontSize(12.0f);
    today->SetTextColor(kText);
    today->SetAlignment(TextAlignment::Center);
    today->layoutItem.SetFlexShrink(0.0f);
    window.AddChild(today);

    // The month, today marked. Looking at other months is allowed; nothing
    // is chosen here, so a click changes nothing.
    const std::tm now = Now();
    auto month = std::make_shared<UltraCanvasCalendarView>("clock.month", 0, 0, kPanelWidth - 24, 220);
    month->SetFirstDayOfWeek(1);
    month->SetSelectedDate(UCDate(now.tm_year + 1900, now.tm_mon + 1, now.tm_mday));
    month->SetVisibleMonth(now.tm_year + 1900, now.tm_mon + 1);
    month->SetShowFooter(false);
    month->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
    month->SetElementSize(Size2Df(static_cast<float>(kPanelWidth - 24), 220));
    window.AddChild(month);

    auto hint = CreateLabel("clock.hint", 0, 0, kPanelWidth - 24, 18, "Escape closes");
    hint->SetFontSize(8.5f);
    hint->SetTextColor(kMuted);
    hint->SetAlignment(TextAlignment::Right);
    hint->layoutItem.SetFlexShrink(0.0f);
    window.AddChild(hint);
}

} // namespace UltraDesktop
