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
- **Done and the history.** Every task has a *Done* button that moves it
  to the history (*To do* / *History* at the top right, with their counts,
  Ctrl+H); there it is listed, the latest finished first, with *Reopen* to
  bring it back and *Delete*.
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
