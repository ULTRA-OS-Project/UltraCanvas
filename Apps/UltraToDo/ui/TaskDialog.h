// Apps/UltraToDo/ui/TaskDialog.h
// The dialog a task is written and changed in: its title, its description,
// an optional planned end (a day, and a time of day if it has one), the time
// it is estimated to take, a reminder before it has to be started, an alarm
// at the planned end - and its attachments, added and taken off as in a mail
// being written. Nothing is changed until Save; Cancel leaves the task as it
// was.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "core/ToDoTask.h"

#include <functional>
#include <string>
#include <vector>

namespace UltraCanvas { class UltraCanvasWindowBase; }

namespace UltraToDo {

// What Save hands back: the task as the dialog left it (its attachment list
// untouched), the files to attach (UTF-8 paths) and the attached files to
// take off (ToDoAttachment::file).
struct TaskDialogResult {
    ToDoTask task;
    std::vector<std::string> attach;
    std::vector<std::string> detach;
};

// The reminder choices, minutes before the latest start (kNoReminder: none).
const std::vector<int>& ReminderChoices();
std::string ReminderChoiceText(int minutes);

class TaskDialog {
public:
    // `task` with an empty id is a new task. `onSave` runs once, on Save.
    static void Show(UltraCanvas::UltraCanvasWindowBase* parent, const ToDoTask& task,
                     std::function<void(TaskDialogResult&)> onSave);
};

} // namespace UltraToDo
