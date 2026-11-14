// Apps/UltraNotifications/ui/UltraNotificationsWindow.h
// UltraNotifications' main window: every notification, chat message and mail
// that reached the UltraMessage feed, in one list to read, search, open and
// dismiss - the framework's UltraCanvasMessageCenter filling the window.
//
// The desktop draws the notifications as they come (its toasts); this is
// where they stay afterwards. While the program runs it publishes the unread
// count as a desktop notice ("UltraNotifications"), which UltraDesktop shows
// on its bell. "Open" on a mail opens UltraMail, on a chat ChatCube, on a
// notification its first action.
//
// Started in the background (SetKeepRunning), closing the window hides it
// and the program stays connected and counting; Ctrl+Q or Quit ends it.
// Version: 0.1.1 - ApplyLook: the window, the footer and the message centre
//                  from the theme (a dark look, and a change while it runs)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"

#include <memory>
#include <string>

namespace UltraCanvas {
    class UltraCanvasButton;
    class UltraCanvasContainer;
    class UltraCanvasMessageCenter;
    struct MessageCenterEntry;
    struct UCEvent;
}

namespace UltraNotifications {

class UltraNotificationsWindow {
public:
    explicit UltraNotificationsWindow(UltraCanvas::UltraCanvasApplication& app);
    ~UltraNotificationsWindow();

    // Builds the window (not shown) and connects to the bus.
    bool Create();
    void Show();
    // Another start handed this over: shown when it was hidden, brought
    // forward.
    void Present();
    // Started in the background: the window hides on close, and Quit shows.
    void SetKeepRunning(bool keep);

private:
    void Open(const UltraCanvas::MessageCenterEntry& entry);
    void UnreadChanged(int unread);
    bool OnKey(const UltraCanvas::UCEvent& event);
    void Quit();
    // The colours set here by hand - the window, the footer, the message
    // centre's style - from the current UltraCanvasTheme.
    void ApplyLook();

    UltraCanvas::UltraCanvasApplication& app_;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasMessageCenter> center_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> footer_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> quit_;
    bool keepRunning_ = false;
    bool shown_ = false;
    int unread_ = -1;
    int themeHandler_ = 0;   // UltraCanvasTheme::AddChangeHandler, removed in the destructor
};

} // namespace UltraNotifications
