// Apps/ChatCube/ui/ChatCubeSettings.cpp
// ChatCube's preferences in <settings folder>/ChatCube/settings.json.
// Version: 0.3.1 - ChatMarks under "chats.pinned" and "chats.unread"
// Version: 0.3.0 - the relay server (networks.relay)
// Version: 0.2.0 - the look is system, light or dark
// Version: 0.1.0
// Last Modified: 2026-11-12
// Author: UltraCanvas Framework / ULTRA OS

#include "ChatCubeSettings.h"

#include "UltraCanvasAppSettings.h"

namespace ChatCube {

namespace {

constexpr const char* kApp = "ChatCube";

const char* DateStyleKey(DateStyle style) {
    switch (style) {
        case DateStyle::YearMonthDay: return "ymd";
        case DateStyle::MonthDayYear: return "mdy";
        case DateStyle::DayMonthYear: break;
    }
    return "dmy";
}

DateStyle DateStyleFromKey(const std::string& key) {
    if (key == "ymd") return DateStyle::YearMonthDay;
    if (key == "mdy") return DateStyle::MonthDayYear;
    return DateStyle::DayMonthYear;
}

} // namespace

void Settings::Load() {
    UltraCanvas::UltraCanvasAppSettings file(kApp);
    file.Load();                // a missing file: the defaults below
    const Settings defaults;
    look = file.GetString("appearance.look", defaults.look);
    if (look != "light" && look != "dark") look = "system";
    clock24 = file.GetString("appearance.clock", defaults.clock24 ? "24h" : "12h") != "12h";
    dateStyle = DateStyleFromKey(file.GetString("appearance.date", DateStyleKey(defaults.dateStyle)));
    ctrlEnterSends = file.GetString("messages.send-key", defaults.ctrlEnterSends ? "ctrl-enter" : "enter")
                     == "ctrl-enter";
    sendTyping = file.GetBool("messages.send-typing", defaults.sendTyping);
    showTyping = file.GetBool("messages.show-typing", defaults.showTyping);
    showNearby = file.GetBool("networks.show-nearby", defaults.showNearby);
    relayServer = file.GetString("networks.relay", defaults.relayServer);
}

bool Settings::Save() const {
    UltraCanvas::UltraCanvasAppSettings file(kApp);
    file.Load();                // keeps whatever else the file holds
    file.Set("appearance.look", look);
    file.Set("appearance.clock", clock24 ? "24h" : "12h");
    file.Set("appearance.date", DateStyleKey(dateStyle));
    file.Set("messages.send-key", ctrlEnterSends ? "ctrl-enter" : "enter");
    file.Set("messages.send-typing", sendTyping);
    file.Set("messages.show-typing", showTyping);
    file.Set("networks.show-nearby", showNearby);
    file.Set("networks.relay", relayServer);
    return file.Save();
}

void ChatMarks::Load() {
    UltraCanvas::UltraCanvasAppSettings file(kApp);
    file.Load();
    const std::vector<std::string> p = file.GetList("chats.pinned");
    const std::vector<std::string> u = file.GetList("chats.unread");
    pinned = std::set<std::string>(p.begin(), p.end());
    unread = std::set<std::string>(u.begin(), u.end());
}

bool ChatMarks::Save() const {
    UltraCanvas::UltraCanvasAppSettings file(kApp);
    file.Load();                // keeps the Settings window's keys
    file.Set("chats.pinned", std::vector<std::string>(pinned.begin(), pinned.end()));
    file.Set("chats.unread", std::vector<std::string>(unread.begin(), unread.end()));
    return file.Save();
}

std::string Settings::File() {
    return UltraCanvas::UltraCanvasAppSettings(kApp).File();
}

std::string DateStyleExample(DateStyle style) {
    switch (style) {
        case DateStyle::YearMonthDay: return "2026-10-31  (year, month, day)";
        case DateStyle::MonthDayYear: return "10/31/2026  (month, day, year)";
        case DateStyle::DayMonthYear: break;
    }
    return "31.10.2026  (day, month, year)";
}

} // namespace ChatCube
