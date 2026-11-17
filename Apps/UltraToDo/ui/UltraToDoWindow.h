// Apps/UltraToDo/ui/UltraToDoWindow.h
// UltraToDo's window: a round "+" at the top left that writes a new task,
// and below it every task as a pastel panel with round corners, the most
// urgent first (SortByUrgency: by the latest moment it can be started and
// still be finished by its planned end). A panel shows the title with the
// box that marks it done, how urgent it is, the planned end, the estimated
// time and the latest start, the description, and the attached files as
// thumbnails - a click opens one in the program the system opens it with.
// A window wide enough for two panels side by side shows the list in
// columns - up to four, sharing the width - filled one after the other: the
// most urgent tasks down the first column, then on at the top of the next.
//
// Each panel's button line starts with a ring marker - its colour and fill
// running from green (time to spare) through orange (the latest start has
// come) to red (past the planned end) - and the time left. A task to start
// now that is not started has the marker's ring blink orange; an overdue
// one has its panel's border blink red (steady where the desktop asks for
// reduced motion). Start tells the program work on the task has begun: the
// orange blink and the reminder to start stop; Pause takes it back. Start
// and the blinking are task supervision's: the settings page (the gear,
// Ctrl+,) sets the level of management, Simple (the default) or With task
// supervision.
//
// Done on a panel moves the task to the history (To do | History at the
// top), where Reopen brings it back.
//
// Reminders and alarms: every 15 seconds the window looks for one that is
// due (DueNotices). It puts it on the desktop as a notification
// (system.notification on UltraMessage, where the bus is in the build) and
// says it in the window too, so it is not missed where no notification
// service draws it. A click on the notification brings the window forward
// with the task marked.
//
// Started with --background the window can be closed: the program stays,
// keeps the reminders going, and comes forward at the next start; Quit ends
// it.
// Version: 0.5.0 - the settings page: Simple, or with task supervision
// Version: 0.4.0 - the urgency marker and time left in the button line, blinking, Start / Pause
// Version: 0.3.0 - the list in columns when the window is wide enough
// Version: 0.2.0 - Done on every task moves it to the history (To do | History)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "core/ToDoSettings.h"
#include "core/ToDoStore.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasImage.h"   // UCPixmap
#include "UltraCanvasTimer.h"
#include "UltraCanvasWindow.h"

#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {
    class UltraCanvasButton;
    class UltraCanvasSegmentedControl;
    class UltraCanvasContainer;
    class UltraCanvasImageElement;
    class UltraCanvasLabel;
    struct UCEvent;
}

namespace UltraToDo {

class UltraToDoWindow {
public:
    explicit UltraToDoWindow(UltraCanvas::UltraCanvasApplication& app);
    ~UltraToDoWindow();

    bool Create();
    void Show();
    // Another start handed over (UltraCanvasSingleInstance): forward, shown
    // first when it was hidden.
    void Present();
    void SetKeepRunning(bool keep);
    // A click on one of UltraToDo's notifications: forward, the task marked.
    void PresentTask(const std::string& taskId);

private:
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildTopBar();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildPanel(const ToDoTask& task, int64_t now);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildAttachments(const ToDoTask& task);
    void Rebuild();
    // The columns (see the top of this file): how many fit in `width`, the
    // window's client width; the list is built again when that changes.
    static int ColumnsFor(int width);
    void LayoutColumns(int width);
    void UpdateSummary(int64_t now);
    void ApplyLook();
    bool OnKey(const UltraCanvas::UCEvent& event);
    // The settings page; and a task as the level of management shows it
    // (without task supervision, never started).
    void OpenSettings();
    ToDoTask Supervision(const ToDoTask& task) const;

    void NewTask();
    void EditTask(const std::string& id);
    void ConfirmDelete(const std::string& id);
    // Done moves a task to the history, Reopen brings it back to the list.
    void SetDone(const std::string& id, bool done);
    void ShowHistory(bool history);
    // Start / Pause: the user began working on the task, or stopped.
    void SetStarted(const std::string& id, bool started);
    // The blinking (see the top of this file): one step, and the timer that
    // runs while something blinks.
    void Blink();
    void UpdateBlinkTimer();
    void AttachTo(const std::string& id);
    void OpenAttachment(const std::string& taskId, const std::string& file);
    bool Save();

    // Reminders and alarms (see the top of this file).
    void Tick();
    void CheckNotices(int64_t now);
    void Announce(const std::vector<std::pair<ToDoTask, ToDoNotice>>& notices, int64_t now);
    bool PostDesktopNotification(const ToDoTask& task, const ToDoNotice& notice, int64_t now);
    void DisconnectBus();

    // Thumbnails are made on worker threads; the image elements waiting for
    // one are filled in when it arrives (PollThumbnails).
    void RequestThumbnail(const std::string& path, UltraCanvas::UltraCanvasImageElement* image);
    void PollThumbnails();

    UltraCanvas::UltraCanvasApplication& app_;
    ToDoStore store_;
    ToDoSettings settings_;
    std::string loadError_;

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> topBar_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> addButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> heading_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> summary_;
    // To do | History: the open tasks, or the finished ones.
    std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl> view_;
    bool history_ = false;
    int columns_ = 1;             // the panels side by side in the list now

    // What blinks: a panel's border red (overdue), or a marker's ring
    // orange (to start now and not started). Rebuilt with the list.
    struct BlinkItem {
        UltraCanvas::UltraCanvasContainer* element = nullptr;   // the panel (overdue), or the marker's ring
        UltraCanvas::Color off;                                  // its border between blinks
        bool ring = false;                                       // the marker's ring: orange; else the panel: red
    };
    std::vector<BlinkItem> blinking_;
    bool blinkOn_ = false;
    UltraCanvas::TimerId blinkTimer_ = 0;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> quit_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> list_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> notice_;   // the last reminder, in the window

    bool keepRunning_ = false;
    bool shown_ = false;
    int themeHandler_ = 0;
    std::string markedTask_;      // the task a notification was clicked for
    int ticks_ = 0;

    UltraCanvas::TimerId timer_ = 0;
    UltraCanvas::TimerId thumbTimer_ = 0;

    std::map<std::string, std::shared_ptr<UltraCanvas::UCPixmap>> thumbnails_;
    std::map<std::string, std::vector<UltraCanvas::UltraCanvasImageElement*>> waiting_;
    std::vector<std::pair<std::string, std::future<std::shared_ptr<UltraCanvas::UCPixmap>>>> jobs_;

    // The bus (UltraMessage), connected at the first notice.
    uint64_t endpoint_ = 0;
    bool busFailed_ = false;
    std::mutex postedMutex_;
    std::map<std::string, std::string> posted_;   // notification id -> task id
};

} // namespace UltraToDo
