// Apps/UltraToDo/core/ToDoSettings.h
// UltraToDo's own settings, in <data>/UltraToDo/settings.json beside the
// tasks (ToDoStore's folder): a program's internal file, read and written
// directly.
//
//   management   how closely UltraToDo follows the work on a task:
//     Simple       tasks with their dates, reminders and alarms
//     Supervised   with task supervision as well: the Start / Pause button
//                  (the user says when work on a task begins) and the
//                  blinking warnings - an orange ring round the marker of a
//                  task to be started now, a red border round an overdue
//                  task's panel
//
// A file that is missing or cannot be read gives the defaults (Simple).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>

namespace UltraToDo {

enum class ManagementLevel { Simple, Supervised };

struct ToDoSettings {
    ManagementLevel management = ManagementLevel::Simple;

    bool Supervised() const { return management == ManagementLevel::Supervised; }

    // `folder` is the store's (ToDoStore::Folder), UTF-8.
    static ToDoSettings Load(const std::string& folder);
    bool Save(const std::string& folder, std::string& error) const;
};

} // namespace UltraToDo
