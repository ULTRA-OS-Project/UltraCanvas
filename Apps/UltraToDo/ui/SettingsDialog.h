// Apps/UltraToDo/ui/SettingsDialog.h
// UltraToDo's settings page, a dialog. Its first entry is the level of
// management (ToDoSettings.h): Simple, or With task supervision - the
// Start / Pause button and the blinking warnings for a task to start now
// and an overdue one. Nothing changes until Save.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "core/ToDoSettings.h"

#include <functional>

namespace UltraCanvas { class UltraCanvasWindowBase; }

namespace UltraToDo {

class SettingsDialog {
public:
    // `onSave` runs once, on Save, with the settings as the dialog left them.
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent, const ToDoSettings& settings,
                     std::function<void(const ToDoSettings&)> onSave);
};

} // namespace UltraToDo
