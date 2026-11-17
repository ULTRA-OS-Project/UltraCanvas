# UltraToDo

The task list of ULTRA OS. A round **+** at the top left writes a task; below
it every task is a panel with round corners and a pastel border, the most urgent first.
A task can have a planned end, an estimate of the time it takes, a
description, files attached to it as to a mail (shown as thumbnails), a
reminder before it has to be started and an alarm at its planned end.

## Contents

- [Using it](#using-it)
- [How urgent a task is](#how-urgent-a-task-is)
- [Reminders and alarms](#reminders-and-alarms)
- [Attachments](#attachments)
- [Where the tasks are kept](#where-the-tasks-are-kept)
- [Command line](#command-line)
- [Building](#building)

## Using it

| Part | What it does |
|---|---|
| The round **+** (top left, Ctrl+N) | writes a new task |
| *To do* / *History* (top right, Ctrl+H) | the tasks to do, or the history of the finished ones, with their counts |
| *Done* | marks the task done and moves it to the history |
| *Reopen* (in the history) | moves the task back to the list of tasks to do |
| A panel's mark (top right) | how urgent it is: *Overdue by 2 h*, *Start now*, *Start in 30 min*, *No date*; *Done* in the history |
| *Attach* | adds documents, pictures or any other file to the task (open tasks) |
| *Edit* | everything about the task, in the task dialog (open tasks) |
| *Delete* | deletes the task and the files attached to it, after asking |

The task dialog has the title, the description, the planned end (a day, and
a time of day if it has one; a day alone means the end of that day), the
estimated time (days, hours, minutes), the reminder, the alarm and the
attachments. The line under the fields says what they come to: the planned
end, the time needed, the latest start and when the reminder comes.

## How urgent a task is

The order is set by the **latest start** - the latest moment a task can be
started and still be finished by its planned end:

```
latest start = planned end - estimated time
```

A report due on Friday that takes two days is more urgent on Wednesday than
a call due on Thursday that takes five minutes, so it is listed first. The
list, from the top:

| Panel | Tasks |
|---|---|
| rose | overdue: the planned end has passed |
| peach | the latest start has passed: every further minute is late |
| butter | to be started within a day |
| mint | planned, with more than a day to spare |
| sky | without a planned end, in the order they were written |

Within each group the earliest latest start comes first. Finished tasks are
not in the list: *Done* moves them to the history, where they are listed in
grey, the latest finished first, with the time each was finished. The pastel is the
panel's border; the fill is a faint tint of it over the theme's panel
colour, so the text stays readable in either look.

## Reminders and alarms

- **Reminder**: from two days before the latest start to the latest start
  itself (*No reminder*, *At the latest start*, 5 / 15 / 30 min, 1 / 2 h,
  1 / 2 days before).
- **Alarm**: at the planned end, while the task is still open.

Every 15 seconds UltraToDo looks for one that is due. It is put on the
desktop as a notification (a `system.notification` on UltraMessage - the
desktop's notification service shows it), and in the window as a line above
the list; an alarm also opens a dialog that waits to be answered. A click on
the notification brings the window forward with the task marked. Each one
is given once: a reminder and an alarm that were both missed while the
program was not running come as the alarm alone. A task whose planned end,
estimate or reminder changes is reminded of again.

The reminders run while UltraToDo runs. Start it with `--background` (for
example with the desktop) to have them without the window: closing the
window then hides it, and *Quit* ends the program.

## Attachments

A file attached to a task is **copied** into the task's folder, as a mail
keeps what was attached to it, so the task still has it after the original
moved or was deleted. The panel shows each one as a thumbnail with its name
under it - a photo or a drawing itself, a PDF's first page, a document's
first lines (the pictures UltraFiler makes), else the type's icon - and a
click opens it in the program the system opens it with. Thumbnails are made
on worker threads, so a large file never holds the window up.

## Where the tasks are kept

```
<data>/UltraToDo/tasks.json                    every task
<data>/UltraToDo/attachments/<task id>/<file>  the files attached to one
```

`<data>` is the user's application data folder: `~/.local/share` (or
`$XDG_DATA_HOME`) on Linux, `%APPDATA%` on Windows,
`~/Library/Application Support` on macOS. The task file is written beside
itself and renamed into place, so a write that fails leaves the list as it
was.

## Command line

```
UltraToDo                the window
UltraToDo --background   no window: the reminders and alarms run
UltraToDo --version
```

One copy runs per user: a second start brings the running one forward.

## Building

Built with the framework (`BUILD_ULTRATODO`, on by default); the
notifications need the UltraMessage module, without which reminders and
alarms are shown in the window alone. The task model and the store
(`Apps/UltraToDo/core`, library `UltraToDoCore`) have no UI;
`Tests/UltraToDoTests.cpp` (with `BUILD_TESTS`) checks the order, the
reminders and the store.
