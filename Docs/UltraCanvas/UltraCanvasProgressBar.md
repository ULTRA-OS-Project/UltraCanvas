# Progress that sits beside the work

`UltraCanvasProgressBar` is a horizontal bar: a track, a fill, nothing else.
It is a child element sized by the layout, for progress that belongs *beside*
the work rather than over it — a status line, a row in a list, a panel footer.

Its counterpart is
[`UltraCanvasProgressDialog`](UltraCanvasProgressDialog.md), which opens a
window with a ring and a Cancel button. Use the dialog when the operation is
the only thing the user can do until it finishes; use the bar when they can
carry on and just want to know something is happening.

```cpp
#include "UltraCanvasProgressBar.h"

auto bar = CreateProgressBar("transfer", 160, 6);
statusRow->AddChild(bar);

bar->SetFraction(0.42);           // 42 %
bar->SetProgress(done, total);    // the same thing in the caller's own units
bar->SetFraction(-1.0);           // working, no total known
bar->SetVisible(false);           // nothing running
```

## It has no text

The bar reports a number; the label next to it says what the number is about.
That split is deliberate — "Uploading "clip.mp4" — 3.2 MB of 8.0 MB" tells the
user far more than a percentage fitted inside a bar ever could, and a bar 6 px
tall has nowhere to put it anyway.

It also takes no input: `Contains()` answers false, so the bar never takes the
pointer from whatever it sits on.

## When nobody knows the total

A negative fraction means "something is happening, nobody knows how much of
it" — an FTP server that sends no length, a queue still being counted. The bar
draws a block sliding along the track instead of a fill.

**The block is moved by `SetFraction(-1)` being called again, not by a timer of
its own.** The element owns no clock, so a caller that stops reporting leaves
the bar where it stood rather than animating for ever over work that has died.
A caller with nothing to report but a heartbeat should call it on a timer it
owns; one that is already reporting bytes (as an FTP transfer does) gets the
movement for free.

`SetProgress(done, total)` with a total of 0 is the same case, so a caller
that may or may not know the size does not have to branch.

## Style

| Field | What it is |
|---|---|
| `trackColor`, `fillColor` | the groove and what fills it |
| `borderColor`, `borderWidth` | a border on the track; off by default |
| `cornerRadius` | clamped to half the height, so one value suits a 4 px bar and a 20 px one — the default rounds the ends fully |
| `busyBlockFraction` | how much of the track the sliding block covers |
| `busyStepFraction` | how far it moves per report |

## See also

- [UltraCanvasProgressDialog](UltraCanvasProgressDialog.md) — the modal ring
- [UltraCanvasUIElements](UltraCanvasUIElements.md) — which element to use for what
