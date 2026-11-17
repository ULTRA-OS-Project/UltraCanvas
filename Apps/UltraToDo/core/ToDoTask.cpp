// Apps/UltraToDo/core/ToDoTask.cpp
// See ToDoTask.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "ToDoTask.h"

#include <algorithm>
#include <ctime>

namespace UltraToDo {

namespace {
constexpr int64_t kDay = 24 * 60 * 60;

// The group a task is listed in: 0 planned and open, 1 open without a
// planned end, 2 finished.
int GroupOf(const ToDoTask& task) {
    if (task.done) return 2;
    return task.HasDue() ? 0 : 1;
}
} // namespace

int64_t LatestStartOf(const ToDoTask& task) {
    if (!task.HasDue()) return 0;
    const int64_t work = static_cast<int64_t>(std::max(0, task.estimateMinutes)) * 60;
    return std::max<int64_t>(1, task.due - work);
}

int64_t ReminderTimeOf(const ToDoTask& task) {
    if (!task.HasDue() || task.reminderMinutes < 0) return 0;
    return std::max<int64_t>(1, LatestStartOf(task) - static_cast<int64_t>(task.reminderMinutes) * 60);
}

int64_t AlarmTimeOf(const ToDoTask& task) {
    return task.HasDue() && task.alarm ? task.due : 0;
}

ToDoUrgency UrgencyOf(const ToDoTask& task, int64_t now) {
    if (task.done) return ToDoUrgency::Finished;
    if (!task.HasDue()) return ToDoUrgency::Unscheduled;
    if (now >= task.due) return ToDoUrgency::Overdue;
    const int64_t start = LatestStartOf(task);
    if (now >= start) return ToDoUrgency::StartNow;
    if (start - now <= kDay) return ToDoUrgency::Soon;
    return ToDoUrgency::Later;
}

bool MoreUrgent(const ToDoTask& a, const ToDoTask& b) {
    const int groupA = GroupOf(a), groupB = GroupOf(b);
    if (groupA != groupB) return groupA < groupB;
    if (groupA == 0) {
        const int64_t startA = LatestStartOf(a), startB = LatestStartOf(b);
        if (startA != startB) return startA < startB;
        if (a.due != b.due) return a.due < b.due;
    } else if (groupA == 2) {
        if (a.completed != b.completed) return a.completed > b.completed;
    }
    if (a.created != b.created) return a.created < b.created;
    return a.id < b.id;
}

void SortByUrgency(std::vector<ToDoTask>& tasks) {
    std::stable_sort(tasks.begin(), tasks.end(), MoreUrgent);
}

std::vector<ToDoNotice> DueNotices(const std::vector<ToDoTask>& tasks, int64_t now) {
    std::vector<ToDoNotice> due;
    for (const ToDoTask& task : tasks) {
        if (task.done) continue;
        const int64_t alarmAt = AlarmTimeOf(task);
        if (alarmAt > 0 && now >= alarmAt && task.alarmedFor != alarmAt) {
            due.push_back({task.id, ToDoNoticeKind::Alarm, alarmAt});
            continue;
        }
        const int64_t remindAt = ReminderTimeOf(task);
        // Once the planned end has passed, a reminder to start says nothing
        // the alarm - or the overdue mark - does not.
        if (remindAt > 0 && now >= remindAt && now < task.due && task.remindedFor != remindAt) {
            due.push_back({task.id, ToDoNoticeKind::Reminder, remindAt});
        }
    }
    std::stable_sort(due.begin(), due.end(),
                     [](const ToDoNotice& a, const ToDoNotice& b) { return a.when < b.when; });
    return due;
}

void MarkNoticeGiven(ToDoTask& task, const ToDoNotice& notice) {
    if (notice.kind == ToDoNoticeKind::Alarm) {
        task.alarmedFor = notice.when;
        // The alarm stands for the reminder it overtook.
        const int64_t remindAt = ReminderTimeOf(task);
        if (remindAt > 0) task.remindedFor = remindAt;
    } else {
        task.remindedFor = notice.when;
    }
}

int64_t MakeLocalTime(int year, int month, int day, int hour, int minute, int second) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = -1;   // the zone's rule for that date
    const std::time_t t = std::mktime(&tm);
    return t == static_cast<std::time_t>(-1) ? 0 : static_cast<int64_t>(t);
}

LocalTimeParts SplitLocalTime(int64_t seconds) {
    const std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    LocalTimeParts parts;
    parts.year = tm.tm_year + 1900;
    parts.month = tm.tm_mon + 1;
    parts.day = tm.tm_mday;
    parts.hour = tm.tm_hour;
    parts.minute = tm.tm_min;
    parts.second = tm.tm_sec;
    return parts;
}

int64_t EndOfLocalDay(int year, int month, int day) {
    return MakeLocalTime(year, month, day, 23, 59, 59);
}

} // namespace UltraToDo
