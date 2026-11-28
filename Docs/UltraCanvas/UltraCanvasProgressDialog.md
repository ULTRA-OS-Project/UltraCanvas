# UltraCanvasProgressDialog

A modal window for an operation that takes long enough to need reporting: a
circular progress ring with the percentage in its centre; beside it a headline,
where the work goes from and to (two chips), and what is being worked on right
now; for a transfer, a strip of figures under them - speed, time left, items,
size; and Cancel, with Pause / Resume where the work can wait.

```cpp
#include "UltraCanvasProgressDialog.h"
```

The dialog **does not run the work and does not block**. The caller keeps the
operation on its own thread (or spreads it across timer ticks) and pushes the
numbers in from the UI thread as they arrive — which is what keeps the ring
animating and the Cancel button responsive.

```cpp
auto dlg = UltraCanvasProgressDialog::Show(window.get(), "Compressing",
                                           "Creating \"photos.zip\"",
                                           [&]() { job.cancel = true; });
// ... on the UI thread, as the worker reports:
dlg->SetProgress(0.42);          // 42 %
dlg->SetDetail("DSC_0042.jpg");
// ... when the work ends:
dlg->Close();
```

## API

| Call | Meaning |
|---|---|
| `Show(parent, title, caption, onCancel, showIcon = false)` | Opens the window over `parent`. Returns `nullptr` when no dialog could be created (dialogs disabled / headless) — handle that and run without a window. `showIcon = true` adds the blue Information badge on the left of the caption; leave it off (the default) to keep the ring centred. |
| `SetProgress(double fraction)` | `0..1`, clamped. A **negative** value means the total is not known: the ring shows a busy sweep and the centre reads `...` instead of inventing a percentage. |
| `SetCaption(text)` | The line above the ring. |
| `SetDetail(text)` | What is being worked on now. Long text is ellipsized from the left, so the window keeps the height it opened with. |
| `Show(parent, title, caption, onCancel, ProgressDialogOptions)` | The same with the transfer layout (below): `from` / `to` for the route chips, `figures` for the strip with an Items cell (`items`) and a Size cell (`bytes`), `onPause` for a Pause / Resume button (`startPaused` when the work is paused already), `onBackground` for a "Run in background" button (below). With a route the window's title is the headline. |
| `SetFigures(ProgressFigures)` | What is done (`doneItems`, `doneBytes`) and, where the work knows it, the total (`totalItems`, `totalBytes`; 0 = not known, and the cell shows what is done alone). Cheap on every tick. |
| `IsCancelled()` | True once the user asked to cancel. |
| `IsPaused()` | True while Pause is pressed. |
| `IsInBackground()` | True once "Run in background" closed the window. |
| `CombinedProgress(figures, ringFraction)`, `EstimateSecondsLeft(seconds, fromProgress, progress)` | How far the work is (items and bytes half each), and the time left at the run's average so far - what the window shows as *Time left*. |
| `FormatDuration(seconds)`, `FormatBytes(bytes)` | "9 s", "2 min", "1 h 5 min"; "742 MB", "1.2 GB" - as the window shows them. |
| `Close()` | Takes the window down. Safe to call twice, and safe after the user closed it. |

`onCancel` fires on the UI thread for every way out of the window — the button,
Escape, the title bar. The dialog only *reports* the cancel; stopping the work
is the caller's job, and so is deciding what a cancelled operation leaves
behind.

## Run in background

With `ProgressDialogOptions::onBackground` set, a **Run in background** button
sits beside Pause. It closes the window without cancelling: `onCancel` does not
fire, `IsInBackground()` turns true, and `onBackground` is called once the
window is gone. The work goes on; showing its progress elsewhere (a status
bar) is the caller's job, and so is opening a new window when the user wants
it back - with `startPaused = true` if the work was paused meanwhile, so the
button reads Resume.

```cpp
ProgressDialogOptions options;
options.onPause = [&](bool paused) { job.paused = paused; };
options.onBackground = [&]() { job.inBackground = true; };   // the status bar takes over
auto dlg = UltraCanvasProgressDialog::Show(window.get(), "Copying", "", onCancel, options);
```

## The transfer layout

```cpp
ProgressDialogOptions options;
options.from = "ULTRA OS";             // the chips: "ULTRA OS" -> "Projekte"
options.to = "Projekte";
options.figures = true;                // the strip: Speed, Time left ...
options.items = true;                  // ... Items
options.bytes = true;                  // ... Size; speed in bytes per second
options.onPause = [&](bool paused) { job.paused = paused; };
auto dlg = UltraCanvasProgressDialog::Show(window.get(), "Copying", "", onCancel, options);
// each tick: the figures first, then the reading that takes the sample
dlg->SetFigures({job.doneItems, job.totalItems, job.doneBytes, job.totalBytes});
dlg->SetProgress(job.fraction);
```

Everything the window shows is fixed when it opens, so its height never changes
while it is up. **Time left** is worked out from the whole run so far: how far
the work is - the share of the items done and the share of the bytes, half each
where both totals are known (`CombinedProgress`), else the one known, else the
ring - and the time that took, paused time left out (`EstimateSecondsLeft`). It
appears after two seconds of work. It is not taken from the last seconds' speed:
over many small files that speed swings with each file's size, and the estimate
jumped between seconds and hours. **Speed** is bytes (or, without `bytes`,
items) per second over the last three seconds, shown after the first second
("—" until then); a pause starts it over.

It is built to cost the work nothing measurable. The ring follows every
`SetProgress`; the texts change at most four times a second and are plain
labels, not a laid-out Markdown message. Nothing is counted for the window: the
caller hands in what it counts anyway, and leaves a total at 0 where knowing it
would mean walking a tree first. In the UltraFiler, a copy of 100,000 small
files spent 1.5 - 1.9 % of one core on the window's thread with this layout,
against 1.1 - 1.7 % with the ring and file name alone; the copy itself runs on
another core.

## Worked example: a background job

The shape the UltraFiler uses for packing and unpacking archives. The worker
touches only atomics; everything else happens on the UI thread when the poll
timer sees the numbers move.

```cpp
struct Job {
    std::atomic<bool>     finished{false}, cancelled{false};
    std::atomic<uint64_t> done{0}, total{0};
    std::mutex            fileMutex;
    std::string           currentFile;
};
auto job = std::make_shared<Job>();

auto dlg = UltraCanvasProgressDialog::Show(window.get(), "Extracting",
        "Unpacking \"backup.tar.gz\"",
        [job]() { job->cancelled.store(true); });

std::thread worker([job]() {
    Unpack(archive, dest, [job](uint64_t done, uint64_t total,
                                const std::string& file) {
        job->done.store(done);
        job->total.store(total);
        { std::lock_guard<std::mutex> lk(job->fileMutex); job->currentFile = file; }
        return !job->cancelled.load();      // false stops the backend
    });
    job->finished.store(true);
});

app->StartTimer(100, true, [=](TimerId id) {
    const uint64_t total = job->total.load();
    dlg->SetProgress(total ? double(job->done.load()) / double(total) : -1.0);
    { std::lock_guard<std::mutex> lk(job->fileMutex); dlg->SetDetail(job->currentFile); }
    if (!job->finished.load()) return;
    app->StopTimer(id);
    dlg->Close();
    // join the worker, refresh, report the result — on the UI thread
});
```

Two details worth copying:

- **Poll, don't post.** The worker never touches the dialog. A UI timer reads
  the atomics every 100 ms, which is smooth to the eye and costs nothing.
- **Own the leftovers.** A cancelled *write* usually has to be undone — the
  Filer deletes the half-written archive — while a cancelled *extraction*
  keeps what it already wrote, because those are real files the user may want.

## Opening the window late

Packing an archive is never quick, so that window opens with the job. An
operation that is *usually* over in a blink and only sometimes is not — a copy,
a move, a delete — should not flash a window at the user for every text file it
copies. Give it the poll timer straight away but hold the `Show()` call until
the job has actually been running a while (the Filer waits two seconds, timed
from the start of the whole operation rather than of the current step), and
skip it entirely for a job that has already finished by then. Remember that the
window was due even when `Show()` answers `nullptr` — otherwise the next tick
asks again, and every tick after it.

## No severity badge: the ring is centred

A message dialog puts a coloured severity badge (the blue `i`) in its own column
on the left, and the whole content column starts to the right of it. In a
progress window that column holds the ring, so the badge pushes the ring off the
centre of the window — the ring is this dialog's graphic, and a second one adds
nothing.

So the window opens with `DialogConfig::showIcon = false` and the ring is
horizontally centred in the full width of the dialog. Pass `showIcon = true` to
`Show()` if a caller really wants the classic badge back. The same switch is
available on any alert or modal dialog — see
[`UltraCanvasAlert`](UltraCanvasAlert.md) (`AlertOptions::showIcon`,
`UltraCanvasAlert::Plain`) and `UltraCanvasModalDialog::SetIconVisible()`.

## Where the ring comes from

The ring is an `UltraCanvasCircularProgressChart` in `SingleRing` style, so it
carries the framework's chart styling rather than a hand-drawn arc. Nothing in
this dialog is painted directly.

Its colours are the current theme's: the arc is the accent, the track the
hairline colour (`line`), the percentage the ink - so the reading is white on
a dark dialog and dark on a light one. The chart keeps the colours it is
given rather than following the theme itself, so the dialog sets them again
when the theme changes while it is open.
