// Apps/UltraDesktop/ui/UltraDesktopClockPanel.h
// The clock panel: what a click on the bar's clock opens - the time large,
// as an analog clock face and a digital one (both the framework's gauge
// element in its clock modes, which tick by themselves), today's date in
// words, and the month on the framework's calendar view with today marked.
// Nothing to set here: the clock is the system's.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDesktopPopupPanel.h"

namespace UltraDesktop {

class UltraDesktopClockPanel : public UltraDesktopPopupPanel {
public:
    UltraDesktopClockPanel();

protected:
    void Build(UltraCanvas::UltraCanvasWindow& window) override;
};

// Today, written out: "Thursday, 30 October 2026".
std::string TodayInWords();
// The time now: "14:05" (`withSeconds`: "14:05:09").
std::string TimeNow(bool withSeconds = false);
// The day and the month, short, for the bar: "30 Oct".
std::string TodayShort();

} // namespace UltraDesktop
