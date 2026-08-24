# UltraCanvasProgressDialog

A modal window for an operation that takes long enough to need reporting: a
circular progress ring with the percentage in its centre, a caption above it, a
line naming what is being worked on right now, and a Cancel button.

```cpp
#include "UltraCanvasProgressDialog.h"
```

The dialog **does not run the work and does not block**. The caller keeps the
operation on its own thread (or spreads it across timer ticks) and pushes the
numbers in from the UI thread as they arrive — which is what keeps the ring
animating and the Cancel button responsive.

```cpp
auto dlg = UltraCanvasProgressDialog::Show(window, "Compressing",
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
| `Show(parent, title, caption, onCancel)` | Opens the window over `parent`. Returns `nullptr` when no dialog could be created (dialogs disabled / headless) — handle that and run without a window. |
| `SetProgress(double fraction)` | `0..1`, clamped. A **negative** value means the total is not known: the ring shows a busy sweep and the centre reads `...` instead of inventing a percentage. |
| `SetCaption(text)` | The line above the ring. |
| `SetDetail(text)` | What is being worked on now. Long text is ellipsized from the left, so the window keeps the height it opened with. |
| `IsCancelled()` | True once the user asked to cancel. |
| `Close()` | Takes the window down. Safe to call twice, and safe after the user closed it. |

`onCancel` fires on the UI thread for every way out of the window — the button,
Escape, the title bar. The dialog only *reports* the cancel; stopping the work
is the caller's job, and so is deciding what a cancelled operation leaves
behind.

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

auto dlg = UltraCanvasProgressDialog::Show(window, "Extracting",
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

## Where the ring comes from

The ring is an `UltraCanvasCircularProgressChart` in `SingleRing` style, so it
carries the framework's chart styling rather than a hand-drawn arc. Nothing in
this dialog is painted directly.
