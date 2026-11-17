// Apps/UltraToDo/ui/ToDoFormat.cpp
// See ToDoFormat.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "ToDoFormat.h"

#include "UltraLocalisation/UltraLocalisation.h"

#include <cstdio>
#include <ctime>

using UltraCanvas::Tr;
using UltraCanvas::TrF;

namespace UltraToDo {

namespace {
std::tm LocalTm(int64_t seconds) {
    const std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

std::string Strftime(const std::tm& tm, const char* format) {
    char buffer[96];
    const size_t n = std::strftime(buffer, sizeof(buffer), format, &tm);
    return std::string(buffer, n);
}

// Days from the day of `now` to the day of `when`, by the calendar.
long DaysApart(int64_t when, int64_t now) {
    std::tm a = LocalTm(when), b = LocalTm(now);
    a.tm_hour = b.tm_hour = 12;
    a.tm_min = b.tm_min = a.tm_sec = b.tm_sec = 0;
    a.tm_isdst = b.tm_isdst = -1;
    const double seconds = std::difftime(std::mktime(&a), std::mktime(&b));
    return static_cast<long>((seconds + (seconds >= 0 ? 43200.0 : -43200.0)) / 86400.0);
}
} // namespace

int64_t NowSeconds() {
    return static_cast<int64_t>(std::time(nullptr));
}

std::string FormatDuration(int64_t minutes) {
    if (minutes <= 0) return {};
    const int64_t days = minutes / (24 * 60);
    const int64_t hours = (minutes % (24 * 60)) / 60;
    const int64_t rest = minutes % 60;
    if (days > 0) {
        if (hours > 0) return TrF("{0} d {1} h", days, hours);
        return TrF("{0} d", days);
    }
    if (hours > 0) {
        if (rest > 0) return TrF("{0} h {1} min", hours, rest);
        return TrF("{0} h", hours);
    }
    return TrF("{0} min", rest);
}

std::string FormatMoment(int64_t when, bool withTime, int64_t now) {
    if (when <= 0) return {};
    const std::tm tm = LocalTm(when);
    const long days = DaysApart(when, now);
    std::string day;
    if (days == 0) day = Tr("Today");
    else if (days == 1) day = Tr("Tomorrow");
    else if (days == -1) day = Tr("Yesterday");
    else day = Strftime(tm, "%a, %d %b %Y");
    if (!withTime) return day;
    return TrF("{0}, {1}", day, Strftime(tm, "%H:%M"));
}

std::string UrgencyText(const ToDoTask& task, int64_t now) {
    switch (UrgencyOf(task, now)) {
        case ToDoUrgency::Finished:
            return Tr("Done");
        case ToDoUrgency::Unscheduled:
            return Tr("No date");
        case ToDoUrgency::Overdue: {
            const int64_t late = (now - task.due) / 60;
            return late > 0 ? TrF("Overdue by {0}", FormatDuration(late)) : Tr("Overdue");
        }
        case ToDoUrgency::StartNow:
            return task.started > 0 ? Tr("In progress") : Tr("Start now");
        case ToDoUrgency::Soon:
        case ToDoUrgency::Later: {
            if (task.started > 0) return Tr("In progress");
            const int64_t left = (LatestStartOf(task) - now + 59) / 60;
            if (task.estimateMinutes > 0) return TrF("Start in {0}", FormatDuration(left));
            return TrF("Due in {0}", FormatDuration(left));
        }
    }
    return {};
}

std::string ScheduleText(const ToDoTask& task, int64_t now) {
    std::string text;
    auto add = [&text](const std::string& part) {
        if (part.empty()) return;
        if (!text.empty()) text += "   ·   ";
        text += part;
    };
    if (task.HasDue()) add(TrF("Planned end: {0}", FormatMoment(task.due, task.dueHasTime, now)));
    if (task.estimateMinutes > 0) add(TrF("Needs {0}", FormatDuration(task.estimateMinutes)));
    if (task.HasDue() && task.estimateMinutes > 0 && !task.done)
        add(TrF("Latest start: {0}", FormatMoment(LatestStartOf(task), true, now)));
    if (task.done && task.completed > 0)
        add(TrF("Finished {0}", FormatMoment(task.completed, true, now)));
    return text;
}

std::string CountdownText(const ToDoTask& task, int64_t now) {
    if (!task.HasDue()) return Tr("No date");
    if (now >= task.due) {
        const int64_t late = (now - task.due) / 60;
        return late > 0 ? TrF("{0} late", FormatDuration(late)) : Tr("Due now");
    }
    return FormatDuration((task.due - now + 59) / 60);
}

std::string TimeLeftText(const ToDoTask& task, int64_t now) {
    std::string text;
    if (!task.HasDue()) text = Tr("No due date");
    else if (now >= task.due) text = TrF("Was due {0}", FormatMoment(task.due, task.dueHasTime, now));
    else text = TrF("Due {0}", FormatMoment(task.due, task.dueHasTime, now));
    if (task.started > 0 && !task.done)
        text += "\n" + TrF("In progress since {0}", FormatMoment(task.started, true, now));
    return text;
}

std::string NoticeText(const ToDoTask& task, ToDoNoticeKind kind, int64_t now) {
    if (kind == ToDoNoticeKind::Alarm) {
        return TrF("\"{0}\" has reached its planned end ({1}).", task.title,
                   FormatMoment(task.due, task.dueHasTime, now));
    }
    return TrF("Time to start \"{0}\". Latest start: {1}. Planned end: {2}.", task.title,
               FormatMoment(LatestStartOf(task), true, now),
               FormatMoment(task.due, task.dueHasTime, now));
}

} // namespace UltraToDo
