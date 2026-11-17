// Tests/UltraToDoTests.cpp
// Unit tests for UltraToDo's task model and store (Apps/UltraToDo/core):
// the latest start, the urgency, the list's order, which reminders and
// alarms are due (and that each is given once), and the task file and the
// attachments, under a Thai-and-emoji folder. Every time is handed in, so
// nothing sleeps; no UI.
//
// Author: UltraCanvas Framework / ULTRA OS
#include "core/ToDoStore.h"
#include "core/ToDoTask.h"

#include "UltraCanvasPathUtf8.h"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace UltraToDo;
namespace fs = std::filesystem;
using UltraCanvas::OpenFileUtf8;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

static int g_failures = 0;
static int g_checks = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

constexpr int64_t kNow = 1800000000;   // a fixed "now"
constexpr int64_t kHour = 3600;

static ToDoTask Task(const std::string& id, int64_t due, int estimateMinutes) {
    ToDoTask t;
    t.id = id;
    t.title = id;
    t.due = due;
    t.dueHasTime = due > 0;
    t.estimateMinutes = estimateMinutes;
    t.created = kNow - 100;
    return t;
}

static void TestLatestStartAndUrgency() {
    std::printf("Latest start is the planned end less the estimate\n");
    ToDoTask t = Task("a", kNow + 10 * kHour, 120);
    Check(LatestStartOf(t) == kNow + 8 * kHour, "10 h out, 2 h of work: start in 8 h");
    Check(UrgencyOf(t, kNow) == ToDoUrgency::Soon, "within a day: Soon");
    Check(UrgencyOf(t, kNow + 9 * kHour) == ToDoUrgency::StartNow, "past the latest start: StartNow");
    Check(UrgencyOf(t, kNow + 10 * kHour) == ToDoUrgency::Overdue, "at the planned end: Overdue");
    Check(UrgencyOf(Task("b", kNow + 30 * kHour, 0), kNow) == ToDoUrgency::Later, "more than a day: Later");
    Check(UrgencyOf(Task("c", 0, 60), kNow) == ToDoUrgency::Unscheduled, "no planned end: Unscheduled");
    Check(LatestStartOf(Task("c", 0, 60)) == 0, "no planned end: no latest start");
    t.done = true;
    Check(UrgencyOf(t, kNow) == ToDoUrgency::Finished, "done: Finished");
}

static void TestOrder() {
    std::printf("The most urgent first: by the latest start, not the planned end\n");
    // The report is due later but needs two days; the call is due sooner
    // and takes five minutes - the report has to be started first.
    std::vector<ToDoTask> tasks = {
        Task("call", kNow + 30 * kHour, 5),
        Task("report", kNow + 40 * kHour, 48 * 60),
        Task("someday", 0, 0),
        Task("finished", kNow + kHour, 0),
        Task("overdue", kNow - kHour, 0),
    };
    tasks[3].done = true;
    tasks[3].completed = kNow - 10;
    SortByUrgency(tasks, kNow);
    const std::vector<std::string> expected = {"overdue", "report", "call", "someday", "finished"};
    bool same = tasks.size() == expected.size();
    for (size_t i = 0; same && i < tasks.size(); ++i) same = tasks[i].id == expected[i];
    Check(same, "overdue, report, call, then undated, then finished");
}

static void TestNotices() {
    std::printf("Reminders and alarms are due once each\n");
    ToDoTask t = Task("t", kNow + 4 * kHour, 60);    // latest start in 3 h
    t.reminderMinutes = 30;                           // reminder in 2 h 30 min
    t.alarm = true;
    std::vector<ToDoTask> tasks = {t};
    Check(DueNotices(tasks, kNow).empty(), "nothing yet");
    auto due = DueNotices(tasks, kNow + 2 * kHour + 30 * 60);
    Check(due.size() == 1 && due[0].kind == ToDoNoticeKind::Reminder, "the reminder at 2 h 30 min");
    if (!due.empty()) MarkNoticeGiven(tasks[0], due[0]);
    Check(DueNotices(tasks, kNow + 3 * kHour).empty(), "the reminder only once");
    due = DueNotices(tasks, kNow + 4 * kHour);
    Check(due.size() == 1 && due[0].kind == ToDoNoticeKind::Alarm, "the alarm at the planned end");
    if (!due.empty()) MarkNoticeGiven(tasks[0], due[0]);
    Check(DueNotices(tasks, kNow + 5 * kHour).empty(), "the alarm only once");

    // Both missed while the program was not running: the alarm alone.
    ToDoTask late = t;
    late.id = "late";
    std::vector<ToDoTask> missed = {late};
    due = DueNotices(missed, kNow + 6 * kHour);
    Check(due.size() == 1 && due[0].kind == ToDoNoticeKind::Alarm, "a missed reminder gives way to the alarm");
    if (!due.empty()) MarkNoticeGiven(missed[0], due[0]);
    Check(DueNotices(missed, kNow + 7 * kHour).empty(), "and neither comes again");

    // A finished task is not reminded of; a moved one is again.
    tasks[0].done = true;
    Check(DueNotices(tasks, kNow + 10 * kHour).empty(), "a finished task is quiet");
    tasks[0].done = false;
    tasks[0].due += 24 * kHour;
    due = DueNotices(tasks, kNow + 24 * kHour + 2 * kHour + 31 * 60);
    Check(due.size() == 1 && due[0].kind == ToDoNoticeKind::Reminder, "a task moved a day is reminded of again");
}

static void TestUrgencyLevelAndStart() {
    std::printf("The marker's level runs from green to red; a started task is not reminded to start\n");
    ToDoTask t = Task("t", kNow + 10 * kHour, 120);              // latest start in 8 h
    Check(UrgencyLevel(Task("u", 0, 0), kNow) < 0, "no planned end: no level");
    Check(UrgencyLevel(Task("far", kNow + 100 * kHour, 0), kNow) == 0.0, "more than the horizon away: green");
    const double eightHours = UrgencyLevel(t, kNow);
    Check(eightHours > 0.0 && eightHours < 1.0, "8 h before the latest start: between green and orange");
    Check(UrgencyLevel(t, kNow + 8 * kHour) == 1.0, "at the latest start: orange");
    Check(UrgencyLevel(t, kNow + 9 * kHour) == 1.5, "half way to the planned end");
    Check(UrgencyLevel(t, kNow + 10 * kHour) == 2.0, "at the planned end: red");
    t.done = true;
    Check(UrgencyLevel(t, kNow) < 0, "finished: no level");

    ToDoTask r = Task("r", kNow + 4 * kHour, 60);
    r.reminderMinutes = 30;
    r.started = kNow;
    std::vector<ToDoTask> tasks = {r};
    Check(DueNotices(tasks, kNow + 2 * kHour + 45 * 60).empty(), "started: no reminder to start");
    tasks[0].alarm = true;
    const auto due = DueNotices(tasks, kNow + 4 * kHour);
    Check(due.size() == 1 && due[0].kind == ToDoNoticeKind::Alarm, "started: the alarm still comes");
}

static void TestDayWithoutTime() {
    std::printf("A planned end without a time of day is the end of that day\n");
    const int64_t end = EndOfLocalDay(2026, 11, 20);
    const LocalTimeParts parts = SplitLocalTime(end);
    Check(parts.year == 2026 && parts.month == 11 && parts.day == 20, "the same day");
    Check(parts.hour == 23 && parts.minute == 59 && parts.second == 59, "its last second");
}

static void WriteFile(const fs::path& path, const std::string& text) {
    std::FILE* f = OpenFileUtf8(PathToUtf8(path), "wb");
    if (!f) return;
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

static void TestStore() {
    std::printf("The store keeps tasks and attachments, in a folder of any name\n");
    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec) / PathFromUtf8("UltraToDoTest งาน 📋");
    fs::remove_all(root, ec);
    const std::string folder = PathToUtf8(root / "UltraToDo");

    ToDoStore store(folder);
    std::string error;
    Check(store.Load(error) && store.Tasks().empty(), "a folder without a file is an empty list");

    ToDoTask t = Task("", kNow + 5 * kHour, 90);
    t.title = "Steuererklärung abgeben";
    t.description = "Belege: siehe Anhang\nZweite Zeile";
    t.reminderMinutes = 60;
    t.alarm = true;
    const std::string id = store.Put(t, kNow);
    Check(!id.empty(), "a new task gets an id");

    const fs::path source = root / PathFromUtf8("ใบเสร็จ 🧾.txt");
    fs::create_directories(root, ec);
    WriteFile(source, "receipt");
    Check(store.AddAttachment(id, PathToUtf8(source), error), "a Thai-and-emoji file is attached: " + error);
    Check(store.AddAttachment(id, PathToUtf8(source), error), "the same file twice");
    Check(store.Save(error), "saved: " + error);

    ToDoStore again(folder);
    Check(again.Load(error), "read back: " + error);
    const ToDoTask* read = again.Find(id);
    Check(read != nullptr, "the task is there");
    if (read) {
        Check(read->title == t.title && read->description == t.description, "title and description");
        Check(read->due == t.due && read->estimateMinutes == 90 && read->reminderMinutes == 60 && read->alarm,
              "the schedule");
        Check(read->attachments.size() == 2, "two attachments");
        if (read->attachments.size() == 2) {
            Check(read->attachments[0].name == "ใบเสร็จ 🧾.txt", "the name as it was");
            Check(read->attachments[0].file != read->attachments[1].file, "two copies, two names");
            Check(fs::exists(PathFromUtf8(again.AttachmentPath(id, read->attachments[1].file)), ec),
                  "the copy is on disk");
        }
    }
    const std::string second = again.Put(Task("", 0, 0), kNow);
    Check(second != id, "a second task gets another id");

    if (read && !read->attachments.empty()) {
        const std::string file = read->attachments[0].file;
        const std::string copy = again.AttachmentPath(id, file);
        Check(again.RemoveAttachment(id, file) && !fs::exists(PathFromUtf8(copy), ec), "an attachment taken off");
    }
    Check(again.SetStarted(id, true, kNow + 5) && again.Find(id)->started == kNow + 5, "started");
    Check(again.Save(error), "saved with the start: " + error);
    {
        ToDoStore third(folder);
        Check(third.Load(error) && third.Find(id) && third.Find(id)->started == kNow + 5, "the start is kept");
    }
    Check(again.SetDone(id, true, kNow + 1) && again.Find(id)->done, "marked done");
    Check(again.Remove(id) && !again.Find(id), "removed");
    Check(!fs::exists(root / "UltraToDo" / "attachments" / PathFromUtf8(id), ec), "with its files");
    fs::remove_all(root, ec);
}

int main() {
    TestLatestStartAndUrgency();
    TestOrder();
    TestNotices();
    TestUrgencyLevelAndStart();
    TestDayWithoutTime();
    TestStore();
    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
