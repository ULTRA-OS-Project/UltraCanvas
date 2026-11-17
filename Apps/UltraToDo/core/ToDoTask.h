// Apps/UltraToDo/core/ToDoTask.h
// One task of UltraToDo, and the arithmetic every view of it shares: when it
// has to be started, how urgent it is, the order the list shows, and which
// reminders and alarms are due.
//
// A task may have a planned end (`due`) and an estimate of the work it takes.
// From the two follows the moment it has to be started at the latest:
//
//   latest start = planned end - estimated time
//
// The list is ordered by that moment, not by the planned end (once overdue,
// a task goes to the top): a report due on
// Friday that takes two days is more urgent on Wednesday than a call due on
// Thursday that takes five minutes.
//
//   reminder   `reminderMinutes` before the latest start (0: at the start)
//   alarm      at the planned end, when the task is still open
//
// Times are seconds since the epoch; a planned end without a time of day is
// the end of that day (23:59:59, local time). Nothing here reads a clock or
// touches a file: `now` is always handed in, so the tests run on fixed times.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraToDo {

// A file kept with a task, as an e-mail keeps its attachments: copied into
// the task's folder under `file`, shown under the name it had (`name`).
struct ToDoAttachment {
    std::string name;   // the name shown: the original file name
    std::string file;   // the file name inside the task's attachment folder
};

constexpr int kNoReminder = -1;

struct ToDoTask {
    std::string id;
    std::string title;
    std::string description;

    int64_t due = 0;              // the planned end; 0: none
    bool dueHasTime = false;      // false: the whole day (due is 23:59:59 of it)
    int estimateMinutes = 0;      // the estimated time to solve it; 0: not estimated
    int reminderMinutes = kNoReminder;   // minutes before the latest start; kNoReminder: none
    bool alarm = false;           // an alarm at the planned end

    bool done = false;
    int64_t created = 0;
    int64_t completed = 0;

    // The moments the reminder and the alarm were last given for. A task
    // whose dates change afterwards is reminded of again.
    int64_t remindedFor = 0;
    int64_t alarmedFor = 0;

    std::vector<ToDoAttachment> attachments;

    bool HasDue() const { return due > 0; }
};

// The latest moment the task can be started and still be finished by its
// planned end; 0 for a task without one.
int64_t LatestStartOf(const ToDoTask& task);
// When the reminder is given; 0 when the task has none.
int64_t ReminderTimeOf(const ToDoTask& task);
// When the alarm is given; 0 when the task has none.
int64_t AlarmTimeOf(const ToDoTask& task);

enum class ToDoUrgency {
    Overdue,       // the planned end has passed
    StartNow,      // the latest start has passed: every further minute is late
    Soon,          // has to be started within a day
    Later,         // planned, with more than a day to spare
    Unscheduled,   // no planned end
    Finished
};
ToDoUrgency UrgencyOf(const ToDoTask& task, int64_t now);

// The list's order at `now`: overdue tasks first, then those whose latest
// start has passed, then the rest of the planned ones - within each by their
// latest start, the earliest first - then open tasks without a planned end in
// the order they were written, then finished tasks, the latest finished
// first.
bool MoreUrgent(const ToDoTask& a, const ToDoTask& b, int64_t now);
void SortByUrgency(std::vector<ToDoTask>& tasks, int64_t now);

enum class ToDoNoticeKind { Reminder, Alarm };
struct ToDoNotice {
    std::string taskId;
    ToDoNoticeKind kind = ToDoNoticeKind::Reminder;
    int64_t when = 0;   // the moment it was due
};
// The reminders and alarms due at `now` that have not been given yet, the
// earliest first. A task whose reminder and alarm are both due gets the
// alarm alone: the later news replaces the earlier.
std::vector<ToDoNotice> DueNotices(const std::vector<ToDoTask>& tasks, int64_t now);
// Records a notice as given, so it is not due again.
void MarkNoticeGiven(ToDoTask& task, const ToDoNotice& notice);

// ----- local time -----
// A local date and time as seconds since the epoch, and back.
int64_t MakeLocalTime(int year, int month, int day, int hour, int minute, int second);
struct LocalTimeParts {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
};
LocalTimeParts SplitLocalTime(int64_t seconds);
// The planned end for a day without a time of day: its last second.
int64_t EndOfLocalDay(int year, int month, int day);

} // namespace UltraToDo
