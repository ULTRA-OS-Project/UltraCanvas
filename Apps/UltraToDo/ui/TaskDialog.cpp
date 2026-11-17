// Apps/UltraToDo/ui/TaskDialog.cpp
// See TaskDialog.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "TaskDialog.h"

#include "ToDoFormat.h"
#include "ToDoTheme.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDatePicker.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSpinner.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTimePicker.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <memory>

using namespace UltraCanvas;

namespace UltraToDo {

namespace {
constexpr float kLabelWidth = 130.0f;

// What the dialog collects while it is open: the files added, the ones taken
// off, and what the list of attachments shows.
struct AttachmentEdits {
    std::vector<ToDoAttachment> kept;     // already on the task, still on it
    std::vector<std::string> added;       // paths chosen in this dialog
    std::vector<std::string> removed;     // ToDoAttachment::file of the ones taken off
};

std::shared_ptr<UltraCanvasContainer> MakeRow(const std::string& id, float height) {
    auto row = CreateContainer(id, 0, 0, 0, height);
    row->layout.SetFlexRow().SetFlexGap(Theme::kInnerGap).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexShrink(0);
    return row;
}

std::shared_ptr<UltraCanvasLabel> MakeCaption(const std::string& id, const std::string& text) {
    auto caption = Theme::MakeLine(id, text, Theme::kControlHeight, Theme::SizeBody(), Theme::InkMuted());
    caption->SetElementSize(Size2Df(kLabelWidth, Theme::kControlHeight));
    caption->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Start);
    return caption;
}

std::string FileNameOf(const std::string& path) {
    return PathToUtf8(PathFromUtf8(path).filename());
}

// The list of attachments in the dialog: one row per file with a button that
// takes it off again.
void FillAttachmentList(UltraCanvasContainer* list, const std::shared_ptr<AttachmentEdits>& edits) {
    list->ClearChildren();
    auto addRow = [list, &edits](const std::string& id, const std::string& name, std::function<void()> remove) {
        auto row = MakeRow(id, Theme::kControlHeight);
        auto label = Theme::MakeLine(id + ".name", name, Theme::kControlHeight, Theme::SizeBody(), Theme::Ink());
        label->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        row->AddChild(label);
        auto button = Theme::MakePill(id + ".remove", Tr("Remove"), 72);
        // The list - with this button in it - is rebuilt after the click,
        // not inside it: the button is gone once the list is cleared.
        button->onClick = [list, edits, remove]() {
            remove();
            if (auto* app = UltraCanvasApplicationBase::GetCurrent())
                app->PostToUIThread([list, edits]() { FillAttachmentList(list, edits); });
        };
        row->AddChild(button);
        list->AddChild(row);
    };
    for (size_t i = 0; i < edits->kept.size(); ++i) {
        const std::string file = edits->kept[i].file;
        addRow("tdAttached" + std::to_string(i), edits->kept[i].name, [edits, file]() {
            auto& kept = edits->kept;
            kept.erase(std::remove_if(kept.begin(), kept.end(),
                                      [&file](const ToDoAttachment& a) { return a.file == file; }),
                       kept.end());
            edits->removed.push_back(file);
        });
    }
    for (size_t i = 0; i < edits->added.size(); ++i) {
        const std::string path = edits->added[i];
        addRow("tdAdded" + std::to_string(i), FileNameOf(path), [edits, path]() {
            auto& added = edits->added;
            added.erase(std::remove(added.begin(), added.end(), path), added.end());
        });
    }
    if (edits->kept.empty() && edits->added.empty()) {
        list->AddChild(Theme::MakeLine("tdNoAttachments", Tr("No files attached"), Theme::kControlHeight,
                                       Theme::SizeBody(), Theme::InkFaint()));
    }
}
} // namespace

const std::vector<int>& ReminderChoices() {
    static const std::vector<int> choices = {kNoReminder, 0, 5, 15, 30, 60, 120, 24 * 60, 2 * 24 * 60};
    return choices;
}

std::string ReminderChoiceText(int minutes) {
    if (minutes < 0) return Tr("No reminder");
    if (minutes == 0) return Tr("At the latest start");
    return TrF("{0} before the latest start", FormatDuration(minutes));
}

void TaskDialog::Show(UltraCanvasWindowBase* parent, const ToDoTask& task,
                      std::function<void(TaskDialogResult&)> onSave) {
    const bool editing = !task.id.empty();
    const int64_t now = NowSeconds();

    DialogConfig config;
    config.title      = editing ? Tr("Edit task") : Tr("New task");
    config.width      = 600;
    config.height     = 580;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;
    config.showIcon   = false;

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    auto* dlg = dialog.get();
    dialog->layout.SetFlexColumn().SetFlexGap(Theme::kGap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(20);
    dialog->SetBackgroundColor(Theme::Pastel(Theme::Lavender()));

    auto form = CreateContainer("tdForm", 0, 0, 0, 0);
    form->layout.SetFlexColumn().SetFlexGap(Theme::kInnerGap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    form->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    form->SetPadding(2);

    // ----- title, description -----
    auto title = CreateTextInput("tdTitle", 0, 0, 0, Theme::kControlHeight);
    title->SetPlaceholder(Tr("What is to be done?"));
    title->SetText(task.title);
    {
        auto row = MakeRow("tdTitleRow", Theme::kControlHeight);
        row->AddChild(MakeCaption("tdTitleLabel", Tr("Task")));
        title->layoutItem.SetFlexGrow(1);
        row->AddChild(title);
        form->AddChild(row);
    }

    auto description = std::make_shared<UltraCanvasTextArea>("tdDescription", 0, 0, 0, 110);
    description->SetWordWrap(true);
    description->SetShowLineNumbers(false);
    description->SetHighlightSyntax(false);
    description->SetText(task.description, false);
    description->SetPlaceholder(Tr("Details, steps, links ..."));
    {
        auto row = MakeRow("tdDescriptionRow", 110);
        row->AddChild(MakeCaption("tdDescriptionLabel", Tr("Description")));
        description->layoutItem.SetFlexGrow(1);
        row->AddChild(description);
        form->AddChild(row);
    }

    // ----- planned end: a day, and a time of day if it has one -----
    UCDate dueDate = UCDate::Today().AddDays(1);
    UCTime dueTime(17, 0);
    if (task.HasDue()) {
        const LocalTimeParts parts = SplitLocalTime(task.due);
        dueDate = UCDate(parts.year, parts.month, parts.day);
        if (task.dueHasTime) dueTime = UCTime(parts.hour, parts.minute);
    }
    // A checkbox does not size itself to its text: each takes the rest of
    // its row, which holds a longer translation too.
    auto hasDue = UltraCanvasCheckbox::CreateCheckbox("tdHasDue", 0, 0, 0, Theme::kControlHeight,
                                                      Tr("The task has a planned end"), task.HasDue());
    hasDue->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    auto date = CreateDatePicker("tdDueDate", 0, 0, 150, Theme::kControlHeight);
    date->SetSelectedDate(dueDate);
    date->layoutItem.SetFlexShrink(0);
    auto time = CreateTimePicker24h("tdDueTime", 0, 0, 100, Theme::kControlHeight);
    time->SetTime(dueTime);
    time->layoutItem.SetFlexShrink(0);
    auto hasTime = UltraCanvasCheckbox::CreateCheckbox("tdHasTime", 0, 0, 0, Theme::kControlHeight,
                                                       Tr("At a time of day"), task.dueHasTime);
    hasTime->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    {
        auto row = MakeRow("tdDueRow", Theme::kControlHeight);
        row->AddChild(MakeCaption("tdDueLabel", Tr("Planned end")));
        row->AddChild(hasDue);
        form->AddChild(row);
        auto when = MakeRow("tdWhenRow", Theme::kControlHeight);
        when->AddChild(MakeCaption("tdWhenLabel", ""));
        when->AddChild(date);
        when->AddChild(time);
        when->AddChild(hasTime);
        form->AddChild(when);
    }

    // ----- estimated time -----
    const int estimate = std::max(0, task.estimateMinutes);
    auto days = CreateIntSpinner("tdEstimateDays", 0, 0, 70, Theme::kControlHeight, 0, 365, estimate / (24 * 60));
    auto hours = CreateIntSpinner("tdEstimateHours", 0, 0, 70, Theme::kControlHeight, 0, 23, (estimate / 60) % 24);
    auto minutes = CreateIntSpinner("tdEstimateMinutes", 0, 0, 70, Theme::kControlHeight, 0, 59, estimate % 60, 5);
    {
        auto row = MakeRow("tdEstimateRow", Theme::kControlHeight);
        row->AddChild(MakeCaption("tdEstimateLabel", Tr("Estimated time")));
        row->AddChild(days);
        row->AddChild(Theme::MakeLine("tdDaysUnit", Tr("d"), Theme::kControlHeight, Theme::SizeBody(), Theme::InkMuted()));
        row->AddChild(hours);
        row->AddChild(Theme::MakeLine("tdHoursUnit", Tr("h"), Theme::kControlHeight, Theme::SizeBody(), Theme::InkMuted()));
        row->AddChild(minutes);
        row->AddChild(Theme::MakeLine("tdMinutesUnit", Tr("min"), Theme::kControlHeight, Theme::SizeBody(), Theme::InkMuted()));
        form->AddChild(row);
    }

    // ----- reminder, alarm -----
    auto reminder = CreateDropdown("tdReminder", 0, 0, 0, Theme::kControlHeight);
    int reminderIndex = 0;
    const auto& choices = ReminderChoices();
    for (size_t i = 0; i < choices.size(); ++i) {
        reminder->AddItem(ReminderChoiceText(choices[i]));
        if (choices[i] == task.reminderMinutes) reminderIndex = static_cast<int>(i);
    }
    reminder->SetSelectedIndex(reminderIndex, false);
    {
        auto row = MakeRow("tdReminderRow", Theme::kControlHeight);
        row->AddChild(MakeCaption("tdReminderLabel", Tr("Reminder")));
        reminder->layoutItem.SetFlexGrow(1);
        row->AddChild(reminder);
        form->AddChild(row);
    }
    auto alarm = UltraCanvasCheckbox::CreateCheckbox("tdAlarm", 0, 0, 0, Theme::kControlHeight,
                                                     Tr("Alarm at the planned end"), task.alarm);
    alarm->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    {
        auto row = MakeRow("tdAlarmRow", Theme::kControlHeight);
        row->AddChild(MakeCaption("tdAlarmLabel", Tr("Alarm")));
        row->AddChild(alarm);
        form->AddChild(row);
    }

    // What the schedule comes to, as it is being set.
    auto summary = Theme::MakeWrapped("tdSummary", "", Theme::SizeSmall(), Theme::InkMuted());
    form->AddChild(summary);

    // ----- attachments -----
    auto edits = std::make_shared<AttachmentEdits>();
    edits->kept = task.attachments;
    auto attachments = CreateContainer("tdAttachments", 0, 0, 0, 0);
    attachments->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    attachments->size.height = CSSLayout::Dimension::Auto();
    attachments->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    FillAttachmentList(attachments.get(), edits);
    {
        auto row = CreateContainer("tdAttachRow", 0, 0, 0, 0);
        row->layout.SetFlexRow().SetFlexGap(Theme::kInnerGap).SetFlexAlignItems(CSSLayout::AlignItems::Start);
        row->size.height = CSSLayout::Dimension::Auto();
        row->layoutItem.SetFlexShrink(0);
        row->AddChild(MakeCaption("tdAttachLabel", Tr("Attachments")));
        row->AddChild(attachments);
        auto attach = Theme::MakePill("tdAttach", Tr("Attach files"), 96);
        attach->SetTooltip(Tr("Documents, pictures or any other file: a copy is kept with the task"));
        attach->onClick = [list = attachments.get(), edits, dlg]() {
            FileDialogOptions options;
            options.title = Tr("Attach files");
            options.parentWindow = dlg;
            UltraCanvasFileLoader::OpenMultipleFilesDialog(
                options, [list, edits](DialogResult result, const std::vector<std::string>& paths) {
                    if (result != DialogResult::OK) return;
                    for (const std::string& path : paths)
                        if (std::find(edits->added.begin(), edits->added.end(), path) == edits->added.end())
                            edits->added.push_back(path);
                    FillAttachmentList(list, edits);
                });
        };
        row->AddChild(attach);
        form->AddChild(row);
    }

    dialog->AddChild(form);

    auto error = Theme::MakeLine("tdError", "", 18, Theme::SizeBody(), Theme::Danger());
    error->layoutItem.SetFlexShrink(0);
    dialog->AddChild(error);

    // ----- Cancel / Save -----
    auto buttons = MakeRow("tdButtons", Theme::kControlHeight + 4);
    buttons->AddStretchSpacer(1);
    auto cancel = Theme::MakePill("tdCancel", Tr("Cancel"), 90);
    cancel->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttons->AddChild(cancel);
    auto save = Theme::MakePill("tdSave", editing ? Tr("Save") : Tr("Add task"), 100, /*main=*/true);
    buttons->AddChild(save);
    dialog->AddChild(buttons);

    // The task as the fields say it is.
    auto collect = [base = task, title = title.get(), description = description.get(), hasDue = hasDue.get(),
                    date = date.get(), hasTime = hasTime.get(), time = time.get(), days = days.get(),
                    hours = hours.get(), minutes = minutes.get(), reminder = reminder.get(),
                    alarm = alarm.get()]() {
        ToDoTask out = base;
        out.title = title->GetText();
        out.description = description->GetText();
        out.due = 0;
        out.dueHasTime = false;
        const UCDate day = date->GetSelectedDate();
        if (hasDue->IsChecked() && day.IsValid()) {
            const UCTime at = time->GetTime();
            if (hasTime->IsChecked() && at.IsValid()) {
                out.due = MakeLocalTime(day.year, day.month, day.day, at.hour, at.minute, 0);
                out.dueHasTime = true;
            } else {
                out.due = EndOfLocalDay(day.year, day.month, day.day);
            }
        }
        out.estimateMinutes = static_cast<int>(days->GetValue()) * 24 * 60 +
                              static_cast<int>(hours->GetValue()) * 60 + static_cast<int>(minutes->GetValue());
        const int index = reminder->GetSelectedIndex();
        const auto& all = ReminderChoices();
        out.reminderMinutes = index >= 0 && static_cast<size_t>(index) < all.size() ? all[static_cast<size_t>(index)]
                                                                                  : kNoReminder;
        out.alarm = alarm->IsChecked();
        // A changed schedule is reminded of again.
        if (ReminderTimeOf(out) != ReminderTimeOf(base)) out.remindedFor = 0;
        if (AlarmTimeOf(out) != AlarmTimeOf(base)) out.alarmedFor = 0;
        return out;
    };

    // The parts that only mean something with a planned end follow its box.
    auto refresh = [collect, now, summary = summary.get(), hasDue = hasDue.get(), date = date.get(),
                    hasTime = hasTime.get(), time = time.get(), reminder = reminder.get(), alarm = alarm.get()]() {
        const bool due = hasDue->IsChecked();
        date->SetDisabled(!due);
        hasTime->SetDisabled(!due);
        time->SetDisabled(!due || !hasTime->IsChecked());
        reminder->SetDisabled(!due);
        alarm->SetDisabled(!due);
        const ToDoTask preview = collect();
        std::string text = ScheduleText(preview, now);
        if (preview.HasDue() && preview.reminderMinutes != kNoReminder)
            text += (text.empty() ? "" : "   ·   ") + TrF("Reminder: {0}", FormatMoment(ReminderTimeOf(preview), true, now));
        if (!preview.HasDue()) text = Tr("Without a planned end the task is listed after the planned ones.");
        summary->SetText(text);
    };
    refresh();
    hasDue->onStateChanged = [refresh](CheckedState, CheckedState) { refresh(); };
    hasTime->onStateChanged = [refresh](CheckedState, CheckedState) { refresh(); };
    alarm->onStateChanged = [refresh](CheckedState, CheckedState) { refresh(); };
    date->onDateChanged = [refresh](const UCDate&) { refresh(); };
    time->onTimeChanged = [refresh](const UCTime&) { refresh(); };
    reminder->onSelectionChanged = [refresh](int, const DropdownItem&) { refresh(); };
    days->onValueChanged = [refresh](double) { refresh(); };
    hours->onValueChanged = [refresh](double) { refresh(); };
    minutes->onValueChanged = [refresh](double) { refresh(); };

    auto result = std::make_shared<TaskDialogResult>();
    save->onClick = [dlg, collect, edits, result, error = error.get()]() {
        ToDoTask out = collect();
        if (out.title.find_first_not_of(" \t\r\n") == std::string::npos) {
            error->SetText(Tr("Give the task a title."));
            return;
        }
        result->task = std::move(out);
        result->attach = edits->added;
        result->detach = edits->removed;
        dlg->CloseDialog(DialogResult::OK);
    };

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [result, onSave](DialogResult r) {
            if (r == DialogResult::OK && onSave) onSave(*result);
        },
        parent);
    dlg->SetFocusedElement(title.get());
}

} // namespace UltraToDo
