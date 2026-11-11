// Apps/ChatCube/ui/ChatCubeSettingsWindow.h
// ChatCube's settings window, opened by the gear in the sidebar - built like
// UltraFiler's: a tree of sections on the left (each with its pages), the
// chosen page on the right, a bar at the foot with the page's "Restore
// defaults" on the left and Close on the right.
//
//   | Appearance     | Time & date                                        |
//   |   Look         | How times and dates are written in the lists:      |
//   |   Time & date  |   (o) 14:05   ( ) 2:05 PM                          |
//   | Messages       |   (o) 31.10.2026 ...                               |
//   |   Sending      |                                                    |
//   |   Typing       | | notes: what the choice is about                  |
//   | Networks       |                                                    |
//   |   Accounts     |                                                    |
//   |   Local network|                                                    |
//   | Extras         |                                                    |
//   |   Data folder  |                                                    |
//   |   About        |                                                    |
//   | [Restore defaults]                                         [Close]  |
//
// Every page is the same skeleton: a bold title, a line that says what is
// chosen, the controls, and the notes in a tinted block at the foot. A
// change applies at once (the look from the next start) and is saved
// straight away; there is nothing to confirm. In ChatCube's own colours, the
// light or the dark look.
//
// Version: 0.1.0
// Last Modified: 2026-11-01
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "ChatCubeSettings.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
class UltraCanvasWindowBase;
}

namespace ChatCube {

class ChatCubeSettingsWindow {
public:
    // One network on the Accounts page.
    struct Account {
        std::string providerId;
        std::string name;               // "ChatCube Direct", "Matrix", "Telegram"
        std::string state;              // "Signed in as @me:matrix.org", "Not signed in", ...
        bool        canSignOut = false;
    };
    // What the window needs from the main one.
    struct Hooks {
        std::function<void()> applied;                              // after every change
        std::function<std::vector<Account>()> accounts;             // the networks, now
        std::function<std::string()> myId;                          // the ChatCube ID ("" signed out)
        std::function<void()> copyMyId;
        std::function<void(const std::string& providerId)> signOut;
    };

    ChatCubeSettingsWindow(Settings& settings, std::string version, std::string dataFolder, Hooks hooks);
    ~ChatCubeSettingsWindow();
    ChatCubeSettingsWindow(const ChatCubeSettingsWindow&) = delete;
    ChatCubeSettingsWindow& operator=(const ChatCubeSettingsWindow&) = delete;

    // Opens the window, or raises it when it is open.
    void Show(UltraCanvas::UltraCanvasWindowBase* parent);
    // Closes it when open.
    void Close();
    // The Accounts page read again - a network signed in or out. Nothing
    // while the window is closed.
    void RefreshAccounts();

    struct State;

private:
    Settings&              settings_;
    std::string            version_;
    std::string            dataFolder_;
    Hooks                  hooks_;
    std::shared_ptr<State> state_;      // the open window and its controls
};

} // namespace ChatCube
