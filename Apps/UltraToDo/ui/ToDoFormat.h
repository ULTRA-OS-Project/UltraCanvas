// Apps/UltraToDo/ui/ToDoFormat.h
// The texts UltraToDo makes of a task's times: a duration ("2 h 30 min"), a
// moment ("Today, 14:00"; "Tue, 17 Nov 2026"), what a task's urgency says
// ("Overdue by 3 h", "Start in 2 d 4 h") and the line a reminder or an alarm
// is announced with. All through Tr (UltraLocalisation).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "core/ToDoTask.h"

#include <cstdint>
#include <string>

namespace UltraToDo {

// "45 min", "2 h", "2 h 30 min", "3 d", "3 d 4 h"; "" for nothing.
std::string FormatDuration(int64_t minutes);
// A moment as a person says it: today and tomorrow by name, another day by
// its date; the time of day when `withTime`.
std::string FormatMoment(int64_t when, bool withTime, int64_t now);
// The mark on a task's panel: "Overdue by 3 h", "Start now", "Start in 2 h",
// "No date", "Done".
std::string UrgencyText(const ToDoTask& task, int64_t now);
// The details under a task's title: its planned end, the time it needs and
// when it has to be started. "" for a task with neither date nor estimate.
std::string ScheduleText(const ToDoTask& task, int64_t now);
// The marker's text: the time left ("3 h 20 min"), how late ("2 h late"),
// "No date".
std::string CountdownText(const ToDoTask& task, int64_t now);
// The line beside the marker in a panel's buttons: "Due today, 17:00",
// "Was due today, 03:45", "No due date"; a started task adds "In progress
// since 10:30".
std::string TimeLeftText(const ToDoTask& task, int64_t now);
// What a reminder or an alarm says about its task.
std::string NoticeText(const ToDoTask& task, ToDoNoticeKind kind, int64_t now);

// The current time, in seconds since the epoch.
int64_t NowSeconds();

} // namespace UltraToDo
