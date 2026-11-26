#### 2026-11-26 *0.1.3*
- **Every text in 29 languages.** The application had no translation, so
  it showed English whatever the language; its whole catalogue is now
  translated into all 29 (machine drafts, reviewed by -; Brazilian
  Portuguese uses the Portuguese).

#### 2026-11-25 *0.1.2*
- **Started again from a terminal, UltraToDo answers in your language.**
  "UltraToDo is already running: brought it forward." - and the plain
  "... is already running." of a `--background` start - were English whatever
  the language. They come from the framework's standard set now
  (`UltraCanvasSingleInstance::HandOffMessage`).

#### 2026-11-21 *0.1.1*
- **Two displays, switched in the top bar** (beside the gear, or Ctrl+L):
  *Full*, every task as its whole panel as before, or *Compact*, two lines a
  task - the title, its urgency mark and the buttons on the first; the
  marker, the time left and the attached files as mini thumbnails 12 px high
  on the second (a click opens one; the title's tooltip is the
  description). The choice is kept in `settings.json` (`"display"`).
- **The main buttons' text is white and bold** - *Done*, *Reopen*, *Save*,
  *Add task* and the round **+** - on the theme's accent, darkened where the
  accent is too light for white text to read 4.5:1.

#### 2026-11-17 *0.1.0*
- **UltraToDo, the task list of ULTRA OS.** A round **+** at the top left
  writes a task: its title and description, an optional planned end (a day,
  or a day and a time), the time it is estimated to take, a reminder and an
  alarm, and files attached to it as to a mail.
- **Columns in a wide window**: one below about 900 px, then two, three,
  up to four side by side, filled one after the other - the most urgent
  tasks down the first column, then on at the top of the next; the window
  changes them as it is resized.
- **Icon buttons**: Attach, Edit and Delete are round icon buttons (paper
  clip, pencil, bin) with a tooltip, in the theme's colours; *Done* keeps
  its word.
- **The urgency marker and the time left** in each task's button line: a
  small ring gauge that fills and changes colour as the task grows urgent -
  a green sliver with time to spare, half and orange at the latest start,
  full and red at the planned end - and beside it the time left and when
  the task is due ("1 h 30 min left · Due Today, 11:46"). A task to start
  now has an orange ring blinking round its marker; an overdue one a red
  border blinking round its panel (steady where the desktop asks for
  reduced motion).
- **Start / Pause**: tells UltraToDo work on a task has begun - it shows
  *In progress since ...*, its orange blink and its reminder to start stop -
  or has stopped.
- **Settings page** (the gear, Ctrl+,) with the **level of management**:
  *Simple* (the default) - dates, the marker, reminders and alarms - or
  *With task supervision*, which adds *Start* / *Pause* and the blinking
  warnings for a task to start now and an overdue one.
- **Done and the history.** Every task has a *Done* button that moves it
  to the history (*To do* / *History* at the top right, with their counts,
  Ctrl+H); there it is listed, the latest finished first, with *Reopen* to
  bring it back and *Delete*.
- **Its icon**: a rounded peach attention sign with a black "!" on a pale
  mint rounded tile.
- **The most urgent first.** The list is ordered by the latest moment a task
  can be started and still be finished by its planned end (planned end minus
  estimated time), not by the planned end alone; tasks without a date
  follow.
- **Panels with round corners**, one per task, coloured by urgency: rose
  overdue, peach "start now", butter within a day, mint later, sky without a
  date, mist done. The pastel is the panel's border; its fill is a faint tint
  of the same pastel over the theme's panel colour, so the text sits on an
  almost plain ground. Under a dark look the border is mixed into the panel
  colour too.
- **Attachments with thumbnails**: documents, pictures and any other file are
  copied into the task's folder and shown as thumbnails (a photo itself, a
  PDF's first page, a document's first lines, else the type's icon); a click
  opens the file.
- **Reminders and alarms**: a reminder from 2 days before the latest start
  to the latest start itself, an alarm at the planned end. Both go to the
  desktop as notifications (UltraMessage) and are shown in the window; an
  alarm also asks to be acknowledged. `--background` keeps them running
  without the window.
- **Follows the theme, dialogs included.** The task dialog takes the
  theme's surface, its buttons are coloured by role (*Save* / *Add task* the
  primary one, round in every look), its captions use the theme's muted and
  faint inks, and it changes with the look while it is open. The round **+**
  and the panels' buttons follow the same roles.
