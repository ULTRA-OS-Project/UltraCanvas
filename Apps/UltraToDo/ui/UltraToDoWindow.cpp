// Apps/UltraToDo/ui/UltraToDoWindow.cpp
// See UltraToDoWindow.h.
// Version: 0.2.0 - the panels' pastel is their border, their fill a faint tint of it
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraToDoWindow.h"

#include "TaskDialog.h"
#include "ToDoFormat.h"
#include "ToDoTheme.h"

#include "UltraCanvasBadge.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasFileAssociations.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasFilerWidget.h"        // RenderFileThumbnail
#include "UltraCanvasHostFileIcons.h"      // LoadHostFileIconPixmap
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasNativeFileIcons.h"    // NativeFileIconThreadScope
#include "UltraCanvasNotificationActivation.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTheme.h"
#include "UltraCanvasUtils.h"
#include "UltraLocalisation/UltraLocalisation.h"

#ifdef ULTRATODO_HAVE_ULTRAMESSAGE
#include "UltraMessage/UltraMessage.h"
#include "UltraMessage/UltraMessageEndpoint.h"
#endif

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>

#ifndef ULTRATODO_VERSION
#error "ULTRATODO_VERSION must come from Docs/UltraToDo/CHANGELOG.md via CMake"
#endif

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace UltraToDo {

namespace {
const char* const kAppId = "org.ultraos.ultratodo";
constexpr unsigned kTickMs = 15000;      // how often reminders and alarms are looked for
constexpr int kTicksPerRefresh = 4;      // the "start in ..." texts follow the clock once a minute

std::string WindowTitle() {
    return std::string("UltraToDo ") + ULTRATODO_VERSION;
}

// A file's picture, made the way the Filer's tiles make theirs - a photo or
// a drawing itself, a PDF's first page, a document's first lines - or the
// type's icon. On a worker thread: it decodes and renders.
std::shared_ptr<UCPixmap> MakeThumbnail(const std::string& path, int width, int height) {
    std::error_code ec;
    if (fs::exists(PathFromUtf8(path), ec)) {
        if (auto picture = RenderFileThumbnail(path, width, height, 2.0f)) return picture;
        NativeFileIconThreadScope scope;
        if (auto icon = LoadHostFileIconPixmap(path, false, height)) return icon;
    }
    if (auto image = UCImage::GetFresh(NormalizePath(GetResourcesDir() + "media/icons/document.svg")))
        return image->GetPixmap(height, height, ImageFitMode::Contain, 2.0f);
    return nullptr;
}

std::shared_ptr<UltraCanvasContainer> MakeRow(const std::string& id, float height) {
    auto row = CreateContainer(id, 0, 0, 0, height);
    row->layout.SetFlexRow().SetFlexGap(Theme::kInnerGap).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexShrink(0);
    return row;
}
} // namespace

UltraToDoWindow::UltraToDoWindow(UltraCanvasApplication& app)
    : app_(app), store_(ToDoStore::DefaultFolder()) {}

UltraToDoWindow::~UltraToDoWindow() {
    if (timer_) app_.StopTimer(timer_);
    if (thumbTimer_) app_.StopTimer(thumbTimer_);
    if (themeHandler_) UltraCanvasTheme::RemoveChangeHandler(themeHandler_);
    for (auto& job : jobs_) job.second.wait();
    DisconnectBus();
}

bool UltraToDoWindow::Create() {
    if (!store_.Load(loadError_))
        std::cerr << "UltraToDo: " << loadError_ << "\n";

    WindowConfig config;
    config.title = WindowTitle();
    config.width = 760;
    config.height = 820;
    config.minWidth = 480;
    config.minHeight = 420;
    config.backgroundColor = Theme::PageBackground();
    window_ = CreateWindow(config);
    if (!window_) return false;
    UltraCanvasWindow* window = window_.get();
    window->layout.SetFlexColumn().SetFlexGap(Theme::kGap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window->SetPadding(Theme::kPagePadding);

    topBar_ = BuildTopBar();
    window->AddChild(topBar_);

    notice_ = Theme::MakeWrapped("todo.notice", "", Theme::SizeBody(), Theme::Ink());
    notice_->SetPadding(8, 14, 8, 14);
    notice_->layoutItem.SetFlexShrink(0);
    notice_->SetVisible(false);
    window->AddChild(notice_);

    list_ = CreateScrollableContainer("todo.list", 0, 0, 0, 0);
    list_->layout.SetFlexColumn().SetFlexGap(Theme::kGap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    list_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));
    list_->SetShowHorizontalScrollbar(false);
    // Room for the scrollbar on the right and for the panels' borders.
    list_->SetPadding(2, 16, 2, 2);
    window->AddChild(list_);

    ApplyLook();
    themeHandler_ = UltraCanvasTheme::AddChangeHandler([this](const UltraCanvasTheme&) { ApplyLook(); });

    window->InstallEventFilter("ultratodo-keys", [this](const UCEvent& event) { return OnKey(event); },
                               {UCEventType::KeyDown});
    window->onWindowClosing = [this]() {
        if (keepRunning_) {
            // In the background: the window goes, the reminders stay.
            window_->Hide();
            shown_ = false;
            return false;
        }
        app_.RequestExit();
        return true;
    };

    // A click on a notification after the broker's host has gone (Windows,
    // the Action Center) comes here; elsewhere this does nothing.
    RegisterNotificationActivator(kAppId, [this](const NotificationActivation& click) {
        const auto fields = DecodeNotificationQuery(click.launch);
        const auto task = fields.find("task");
        PresentTask(task == fields.end() ? std::string() : task->second);
    });

    timer_ = app_.StartTimer(kTickMs, true, [this](TimerId) { Tick(); });
    Rebuild();
    CheckNotices(NowSeconds());
    return true;
}

void UltraToDoWindow::Show() {
    if (!window_) return;
    window_->Show();
    shown_ = true;
}

void UltraToDoWindow::Present() {
    if (!window_) return;
    if (!shown_) {
        Rebuild();
        Show();
    }
    if (window_->IsMinimized()) window_->Restore();
    window_->RaiseAndFocus();
}

void UltraToDoWindow::SetKeepRunning(bool keep) {
    keepRunning_ = keep;
    if (quit_) quit_->SetVisible(keep);
}

void UltraToDoWindow::PresentTask(const std::string& taskId) {
    markedTask_ = store_.Find(taskId) ? taskId : std::string();
    Rebuild();
    Present();
}

// ===========================================================================
// The window's parts
// ===========================================================================
std::shared_ptr<UltraCanvasContainer> UltraToDoWindow::BuildTopBar() {
    auto bar = MakeRow("todo.top", Theme::kAddButtonSize);
    bar->layout.SetFlexGap(14);

    // The round button that writes a new task, at the top left.
    addButton_ = Theme::MakeAddButton("todo.add");
    addButton_->layoutItem.SetFlexShrink(0);
    addButton_->SetTooltip(Tr("New task (Ctrl+N)"));
    addButton_->onClick = [this]() { NewTask(); };
    bar->AddChild(addButton_);

    auto titles = CreateContainer("todo.titles", 0, 0, 0, Theme::kAddButtonSize);
    titles->layout.SetFlexColumn().SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
    titles->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    heading_ = Theme::MakeLine("todo.heading", "UltraToDo", 26, Theme::SizeTitle(), Theme::Ink(), FontWeight::Bold);
    titles->AddChild(heading_);
    summary_ = Theme::MakeLine("todo.summary", "", 18, Theme::SizeSmall(), Theme::InkMuted());
    titles->AddChild(summary_);
    bar->AddChild(titles);

    showFinished_ = UltraCanvasCheckbox::CreateCheckbox("todo.showFinished", 0, 0, 0, Theme::kControlHeight,
                                                        Tr("Show finished"), true);
    showFinished_->size.width = CSSLayout::Dimension::Auto();
    showFinished_->layoutItem.SetFlexShrink(0);
    showFinished_->onStateChanged = [this](CheckedState, CheckedState) { Rebuild(); };
    bar->AddChild(showFinished_);

    quit_ = Theme::MakePill("todo.quit", Tr("Quit"), 64);
    quit_->SetTooltip(Tr("Stop UltraToDo running in the background, and its reminders with it (Ctrl+Q)"));
    quit_->onClick = [this]() { app_.RequestExit(); };
    quit_->SetVisible(keepRunning_);
    bar->AddChild(quit_);
    return bar;
}

std::shared_ptr<UltraCanvasContainer> UltraToDoWindow::BuildPanel(const ToDoTask& task, int64_t now) {
    const std::string id = "todo.task." + task.id;
    const ToDoUrgency urgency = UrgencyOf(task, now);
    const Color pastel = Theme::UrgencyPastel(urgency);

    auto panel = CreateContainer(id, 0, 0, 0, 0);
    Theme::ApplyPanel(panel, pastel);
    // The task a notification was clicked for: its border in the strong
    // lavender of a mark instead of its pastel.
    if (task.id == markedTask_)
        panel->SetBorders(Theme::kPanelBorder, Theme::PastelMark(Theme::Lavender()), Theme::kPanelRadius);
    panel->SetPadding(14, 16, 12, 16);
    panel->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    panel->size.height = CSSLayout::Dimension::Auto();
    panel->layoutItem.SetFlexShrink(0);

    // ----- done box, title, urgency -----
    auto head = MakeRow(id + ".head", 28);
    auto done = UltraCanvasCheckbox::CreateCheckbox(id + ".done", 0, 0, 24, 24, "", task.done);
    done->layoutItem.SetFlexShrink(0);
    done->SetTooltip(task.done ? Tr("Open the task again") : Tr("Mark the task done"));
    const std::string taskId = task.id;
    done->onStateChanged = [this, taskId](CheckedState, CheckedState now) {
        const bool checked = now == CheckedState::Checked;
        // The list - with this box in it - is rebuilt after the click.
        app_.PostToUIThread([this, taskId, checked]() { SetDone(taskId, checked); });
    };
    head->AddChild(done);
    auto title = Theme::MakeLine(id + ".title", task.title, 28, Theme::SizeHeading(),
                                 task.done ? Theme::InkMuted() : Theme::Ink(), FontWeight::Bold);
    title->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    head->AddChild(title);
    auto mark = CreateBadge(id + ".urgency", 0, 0, UrgencyText(task, now));
    mark->SetColor(Theme::PastelMark(pastel));
    mark->layoutItem.SetFlexShrink(0);
    head->AddChild(mark);
    panel->AddChild(head);

    // ----- planned end, estimate, latest start; reminder and alarm -----
    std::string schedule = ScheduleText(task, now);
    if (!task.done && task.HasDue()) {
        std::string signals;
        if (task.reminderMinutes != kNoReminder)
            signals = TrF("Reminder {0}", FormatMoment(ReminderTimeOf(task), true, now));
        if (task.alarm) signals += (signals.empty() ? "" : "   ·   ") + Tr("Alarm at the planned end");
        if (!signals.empty()) schedule += (schedule.empty() ? "" : "\n") + signals;
    }
    if (!schedule.empty())
        panel->AddChild(Theme::MakeWrapped(id + ".schedule", schedule, Theme::SizeSmall(), Theme::InkMuted()));

    // ----- description -----
    if (!task.description.empty())
        panel->AddChild(Theme::MakeWrapped(id + ".description", task.description, Theme::SizeBody(),
                                           task.done ? Theme::InkMuted() : Theme::Ink()));

    // ----- attachments -----
    if (!task.attachments.empty()) panel->AddChild(BuildAttachments(task));

    // ----- Attach, Edit, Delete -----
    auto foot = MakeRow(id + ".foot", Theme::kControlHeight);
    foot->AddStretchSpacer(1);
    auto attach = Theme::MakePill(id + ".attach", Tr("Attach"), 72);
    attach->SetTooltip(Tr("Attach documents or pictures to the task"));
    attach->onClick = [this, taskId]() { AttachTo(taskId); };
    foot->AddChild(attach);
    auto edit = Theme::MakePill(id + ".edit", Tr("Edit"), 64);
    edit->onClick = [this, taskId]() { EditTask(taskId); };
    foot->AddChild(edit);
    auto remove = Theme::MakePill(id + ".delete", Tr("Delete"), 64);
    remove->onClick = [this, taskId]() { ConfirmDelete(taskId); };
    foot->AddChild(remove);
    panel->AddChild(foot);
    return panel;
}

// The attached files as thumbnails with their names under them, as a mail
// shows its attachments; a click opens the file.
std::shared_ptr<UltraCanvasContainer> UltraToDoWindow::BuildAttachments(const ToDoTask& task) {
    const std::string id = "todo.task." + task.id + ".files";
    auto strip = CreateContainer(id, 0, 0, 0, 0);
    strip->layout.SetFlexRow().SetFlexWrap(CSSLayout::FlexWrap::Wrap).SetFlexGap(Theme::kInnerGap);
    strip->size.height = CSSLayout::Dimension::Auto();
    strip->layoutItem.SetFlexShrink(0);
    for (size_t i = 0; i < task.attachments.size(); ++i) {
        const ToDoAttachment& attachment = task.attachments[i];
        const std::string fileId = id + "." + std::to_string(i);
        auto tile = CreateContainer(fileId, 0, 0, Theme::kThumbWidth + 12, 0);
        tile->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        tile->size.height = CSSLayout::Dimension::Auto();
        tile->SetPadding(6);
        tile->SetBackgroundColor(Theme::Dark() ? Theme::Roles().raised : Color(255, 255, 255, 200));
        tile->SetBorders(1.0f, Theme::Roles().line, 10.0f);

        auto image = CreateImageElement(fileId + ".thumb", Theme::kThumbWidth, Theme::kThumbHeight);
        image->SetFitMode(ImageFitMode::Contain);
        image->SetMouseCursor(UCMouseCursor::Hand);
        image->SetTooltip(TrF("Open {0}", attachment.name));
        const std::string taskId = task.id, file = attachment.file;
        image->onClick = [this, taskId, file]() { OpenAttachment(taskId, file); };
        RequestThumbnail(store_.AttachmentPath(task.id, attachment.file), image.get());
        tile->AddChild(image);

        auto name = Theme::MakeLine(fileId + ".name", attachment.name, 16, Theme::SizeSmall(), Theme::InkMuted());
        name->SetElementSize(Size2Df(Theme::kThumbWidth, 16));
        name->SetAlignment(TextAlignment::Center);
        tile->AddChild(name);
        strip->AddChild(tile);
    }
    return strip;
}

void UltraToDoWindow::Rebuild() {
    if (!list_) return;
    const int64_t now = NowSeconds();
    const int scroll = list_->GetVerticalScrollPosition();
    waiting_.clear();
    list_->ClearChildren();

    const bool showFinished = !showFinished_ || showFinished_->IsChecked();
    int shown = 0;
    for (const ToDoTask& task : store_.Sorted(now)) {
        if (task.done && !showFinished) continue;
        list_->AddChild(BuildPanel(task, now));
        ++shown;
    }
    if (shown == 0) {
        auto empty = CreateContainer("todo.empty", 0, 0, 0, 0);
        Theme::ApplyPanel(empty, Theme::Lavender());
        empty->SetPadding(24);
        empty->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        empty->size.height = CSSLayout::Dimension::Auto();
        empty->AddChild(Theme::MakeLine("todo.empty.title", store_.Tasks().empty() ? Tr("Nothing to do yet")
                                                                                   : Tr("Everything is done"),
                                        28, Theme::SizeHeading(), Theme::Ink(), FontWeight::Bold));
        empty->AddChild(Theme::MakeWrapped("todo.empty.text",
                                           Tr("Press the round + at the top left to write a task: what is to be done, "
                                              "by when, how long it takes - and a reminder before it has to be started."),
                                           Theme::SizeBody(), Theme::InkMuted()));
        if (!loadError_.empty())
            empty->AddChild(Theme::MakeWrapped("todo.empty.error", loadError_, Theme::SizeBody(), Theme::Danger()));
        list_->AddChild(empty);
    }
    list_->ScrollToVertical(scroll);
    UpdateSummary(now);
}

void UltraToDoWindow::UpdateSummary(int64_t now) {
    int open = 0, overdue = 0, startNow = 0;
    for (const ToDoTask& task : store_.Tasks()) {
        if (task.done) continue;
        ++open;
        const ToDoUrgency urgency = UrgencyOf(task, now);
        if (urgency == ToDoUrgency::Overdue) ++overdue;
        else if (urgency == ToDoUrgency::StartNow) ++startNow;
    }
    std::string text = TrN("{n} open task", "{n} open tasks", open);
    if (overdue > 0) text += "   ·   " + TrN("{n} overdue", "{n} overdue", overdue);
    if (startNow > 0) text += "   ·   " + TrN("{n} to start now", "{n} to start now", startNow);
    if (summary_) summary_->SetText(text);
    if (window_) window_->SetWindowTitle(overdue > 0 ? "(" + std::to_string(overdue) + ") " + WindowTitle()
                                                     : WindowTitle());
}

void UltraToDoWindow::ApplyLook() {
    if (window_) window_->SetBackgroundColor(Theme::PageBackground());
    if (heading_) heading_->SetTextColor(Theme::Ink());
    if (summary_) summary_->SetTextColor(Theme::InkMuted());
    if (notice_) {
        Theme::ApplyPanel(notice_, Theme::Peach());
        notice_->SetTextColor(Theme::Ink());
    }
    Rebuild();
    if (window_) window_->RequestRedraw();
}

bool UltraToDoWindow::OnKey(const UCEvent& event) {
    if (event.type != UCEventType::KeyDown || !event.ctrl) return false;
    auto is = [&event](char lower, UCKeys key) {
        return event.character == lower || event.character == lower - 32 || event.virtualKey == key;
    };
    if (is('n', UCKeys::N)) {
        NewTask();
        return true;
    }
    if (is('q', UCKeys::Q)) {
        app_.RequestExit();
        return true;
    }
    return false;
}

// ===========================================================================
// Changing the tasks
// ===========================================================================
void UltraToDoWindow::NewTask() {
    ToDoTask fresh;
    fresh.alarm = false;
    fresh.reminderMinutes = 15;
    TaskDialog::Show(window_.get(), fresh, [this](TaskDialogResult& result) {
        const std::string id = store_.Put(result.task, NowSeconds());
        std::string error;
        for (const std::string& path : result.attach)
            if (!store_.AddAttachment(id, path, error)) std::cerr << "UltraToDo: " << error << "\n";
        markedTask_.clear();
        Save();
        Rebuild();
    });
}

void UltraToDoWindow::EditTask(const std::string& id) {
    const ToDoTask* task = store_.Find(id);
    if (!task) return;
    TaskDialog::Show(window_.get(), *task, [this](TaskDialogResult& result) {
        const std::string id = result.task.id;
        // The attachment list is the store's: what the dialog took off goes
        // first, what it added is copied in after.
        if (const ToDoTask* current = store_.Find(id)) result.task.attachments = current->attachments;
        store_.Put(result.task, NowSeconds());
        for (const std::string& file : result.detach) store_.RemoveAttachment(id, file);
        std::string error;
        for (const std::string& path : result.attach)
            if (!store_.AddAttachment(id, path, error)) std::cerr << "UltraToDo: " << error << "\n";
        Save();
        Rebuild();
    });
}

void UltraToDoWindow::ConfirmDelete(const std::string& id) {
    const ToDoTask* task = store_.Find(id);
    if (!task) return;
    UltraCanvasDialogManager::ShowConfirmation(
        TrF("Delete \"{0}\" and the files attached to it?", task->title), Tr("Delete task"),
        [this, id](bool confirmed) {
            if (!confirmed) return;
            store_.Remove(id);
            if (markedTask_ == id) markedTask_.clear();
            Save();
            Rebuild();
        },
        window_.get());
}

void UltraToDoWindow::SetDone(const std::string& id, bool done) {
    if (!store_.SetDone(id, done, NowSeconds())) return;
    Save();
    Rebuild();
}

void UltraToDoWindow::AttachTo(const std::string& id) {
    FileDialogOptions options;
    options.title = Tr("Attach files");
    options.parentWindow = window_.get();
    UltraCanvasFileLoader::OpenMultipleFilesDialog(
        options, [this, id](DialogResult result, const std::vector<std::string>& paths) {
            if (result != DialogResult::OK || paths.empty()) return;
            std::string error, errors;
            for (const std::string& path : paths)
                if (!store_.AddAttachment(id, path, error)) errors += error + "\n";
            Save();
            Rebuild();
            if (!errors.empty())
                UltraCanvasDialogManager::ShowError(errors, Tr("Attach files"), nullptr, window_.get());
        });
}

void UltraToDoWindow::OpenAttachment(const std::string& taskId, const std::string& file) {
    std::string error;
    if (!FileAssociations::OpenWithDefaultApplication({store_.AttachmentPath(taskId, file)}, error))
        UltraCanvasDialogManager::ShowError(TrF("The file could not be opened: {0}", error), Tr("Open attachment"),
                                            nullptr, window_.get());
}

bool UltraToDoWindow::Save() {
    std::string error;
    if (store_.Save(error)) return true;
    UltraCanvasDialogManager::ShowError(TrF("The tasks could not be saved: {0}", error), Tr("Save"), nullptr,
                                        window_.get());
    return false;
}

// ===========================================================================
// Reminders and alarms
// ===========================================================================
void UltraToDoWindow::Tick() {
    const int64_t now = NowSeconds();
    CheckNotices(now);
    if (++ticks_ % kTicksPerRefresh == 0 && shown_) Rebuild();
}

void UltraToDoWindow::CheckNotices(int64_t now) {
    const std::vector<ToDoNotice> due = DueNotices(store_.Tasks(), now);
    if (due.empty()) return;
    std::vector<std::pair<ToDoTask, ToDoNotice>> given;
    for (const ToDoNotice& notice : due) {
        const ToDoTask* task = store_.Find(notice.taskId);
        if (!task) continue;
        given.emplace_back(*task, notice);
        store_.MarkGiven(notice);
    }
    Save();
    Announce(given, now);
    Rebuild();
}

void UltraToDoWindow::Announce(const std::vector<std::pair<ToDoTask, ToDoNotice>>& notices, int64_t now) {
    if (notices.empty()) return;
    std::string text;
    bool alarm = false;
    for (const auto& [task, notice] : notices) {
        PostDesktopNotification(task, notice, now);
        if (!text.empty()) text += "\n";
        text += NoticeText(task, notice.kind, now);
        alarm = alarm || notice.kind == ToDoNoticeKind::Alarm;
    }
    // In the window as well: a line above the list that stays until the
    // next one, and - for an alarm - a dialog that waits to be answered.
    if (notice_) {
        notice_->SetText(text);
        const Color pastel = alarm ? Theme::Rose() : Theme::Peach();
        Theme::ApplyPanel(notice_, pastel);
        notice_->SetVisible(true);
    }
    markedTask_ = notices.front().second.taskId;
    if (alarm && shown_) UltraCanvasDialogManager::ShowWarning(text, Tr("Task alarm"), nullptr, window_.get());
}

bool UltraToDoWindow::PostDesktopNotification(const ToDoTask& task, const ToDoNotice& notice, int64_t now) {
#ifdef ULTRATODO_HAVE_ULTRAMESSAGE
    if (busFailed_) return false;
    if (endpoint_ == 0 || !UltraMsg_IsConnected(static_cast<UltraMsgHandle>(endpoint_))) {
        endpoint_ = 0;
        UltraMsgConnectOptions options;
        options.appId = kAppId;
        options.displayName = "UltraToDo";
        options.deliverOnUIThread = false;   // its one subscription runs on the bus thread
        UltraMsgResult error;
        const UltraMsgHandle handle = UltraMsg_Connect(options, &error);
        if (handle == UltraMsgInvalidHandle) {
            // No bus in this session: the window's own line has to do.
            busFailed_ = true;
            std::cerr << "UltraToDo: no notifications on the desktop: " << error.message << "\n";
            return false;
        }
        endpoint_ = static_cast<uint64_t>(handle);
        UltraMsgSubscribeOptions subscribe;
        subscribe.onWorkerThread = true;
        UltraMsg_Subscribe(handle, UltraMsgTopics::SystemNotificationAction,
                           [this](const UltraMsgMessage& m) {
                               const JSONValue* id = m.body.Find("notificationId");
                               if (!id || !id->IsString()) return;
                               std::string taskId;
                               {
                                   std::lock_guard<std::mutex> lock(postedMutex_);
                                   const auto it = posted_.find(id->GetString());
                                   if (it == posted_.end()) return;
                                   taskId = it->second;
                               }
                               app_.PostToUIThread([this, taskId]() { PresentTask(taskId); });
                           },
                           subscribe);
    }
    UltraMessage::SystemNotification n;
    n.appId = kAppId;
    n.appName = "UltraToDo";
    n.category = "x-ultraos.todo";
    n.summary = notice.kind == ToDoNoticeKind::Alarm ? TrF("Alarm: {0}", task.title)
                                                     : TrF("Reminder: {0}", task.title);
    n.body = NoticeText(task, notice.kind, now);
    n.icon = NormalizePath(GetResourcesDir() + "media/appicon/UltraToDo.png");
    n.urgency = notice.kind == ToDoNoticeKind::Alarm ? "critical" : "normal";
    n.actions = {{"default", Tr("Open")}};
    n.launch = EncodeNotificationQuery({{"task", task.id}});
    JSONValue body = UltraMessage::MakeSystemNotification(n);
    body.Set("desktopEntry", "UltraToDo");
    std::string messageId;
    const UltraMsgResult r = UltraMsg_Post(static_cast<UltraMsgHandle>(endpoint_),
                                           UltraMsgTopics::SystemNotification, body, {}, &messageId);
    if (!r) {
        DisconnectBus();
        return false;
    }
    std::lock_guard<std::mutex> lock(postedMutex_);
    posted_[messageId] = task.id;
    return true;
#else
    (void)task;
    (void)notice;
    (void)now;
    return false;
#endif
}

void UltraToDoWindow::DisconnectBus() {
#ifdef ULTRATODO_HAVE_ULTRAMESSAGE
    if (endpoint_ != 0) UltraMsg_Disconnect(static_cast<UltraMsgHandle>(endpoint_));
#endif
    endpoint_ = 0;
}

// ===========================================================================
// Thumbnails
// ===========================================================================
void UltraToDoWindow::RequestThumbnail(const std::string& path, UltraCanvasImageElement* image) {
    const auto made = thumbnails_.find(path);
    if (made != thumbnails_.end()) {
        image->LoadFromPixmap(made->second);
        return;
    }
    const bool running = waiting_.count(path) > 0 ||
                         std::any_of(jobs_.begin(), jobs_.end(), [&path](const auto& job) { return job.first == path; });
    waiting_[path].push_back(image);
    if (running) return;
    const int width = static_cast<int>(Theme::kThumbWidth), height = static_cast<int>(Theme::kThumbHeight);
    jobs_.emplace_back(path, std::async(std::launch::async, [path, width, height]() {
                           return MakeThumbnail(path, width, height);
                       }));
    if (!thumbTimer_) thumbTimer_ = app_.StartTimer(150, true, [this](TimerId) { PollThumbnails(); });
}

void UltraToDoWindow::PollThumbnails() {
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        std::shared_ptr<UCPixmap> picture = it->second.get();
        thumbnails_[it->first] = picture;
        const auto waiting = waiting_.find(it->first);
        if (waiting != waiting_.end()) {
            for (UltraCanvasImageElement* image : waiting->second) image->LoadFromPixmap(picture);
            waiting_.erase(waiting);
        }
        it = jobs_.erase(it);
    }
    if (jobs_.empty() && thumbTimer_) {
        app_.StopTimer(thumbTimer_);
        thumbTimer_ = 0;
    }
    if (window_) window_->RequestRedraw();
}

} // namespace UltraToDo
