# UltraCanvas Slide Show: SlideShowPlayer, PresenterView, StartSlideShow

<!-- doc-check: std::shared_ptr<PptxPresentation> deck; UltraCanvasWindow* window; -->

## Contents

## Overview

The **slide show player** plays a presentation - the deck model the
PowerPoint and OpenDocument readers share
([`UltraCanvasPresentationPptx.h`](../../UltraCanvas/include/UltraCanvasPresentationPptx.h)) -
the way PowerPoint's *Slide Show* does: each slide as large as the element on
black, letterboxed, the hidden ones left out, each slide coming on with its
transition and moving on by its timing, and a black end screen after the
last. The **presenter view** is the presenter's screen beside it.
`StartSlideShow` opens both, each full screen on a monitor of its own when
there are two. The design is
[`Docs/Research/UltraOfficeDesignProposal.md`](../Research/UltraOfficeDesignProposal.md),
§5.3.4.

The player replaces the simple show mode of `UltraCanvasSlideEdit`
(`StartShow`), which plays neither transitions nor timings: a host that
shows a deck puts a player in a window of its own (or calls
`StartSlideShow`) and gives it the deck it edits.

| Class / function | What it is | Files |
|---|---|---|
| `UltraCanvasSlideShowPlayer` | The show: the slides, the transitions, the timings, the keys and clicks | `include/UltraCanvasSlideShowPlayer.h`, `core/UltraCanvasSlideShowPlayer.cpp` |
| `UltraCanvasPresenterView` | The presenter's screen: the slide shown and the next, the notes, the time, "Slide N of M" | same |
| `StartSlideShow` | Opens the show and the presenter view, on two monitors when there are two | same |
| `SlideTransitionFrame` | The geometry of a transition's frame, shared with the photo slideshow | `include/UltraCanvasSlideTransition.h` |

## The player

```cpp
#include "UltraCanvasSlideShowPlayer.h"

auto player = CreateSlideShowPlayer("show", 0, 0, 1280, 720);
player->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Pct(100));
player->SetDeck(deck);                     // read only: the player never changes it
player->onSlideChanged = [](int slide) { /* -1: the end screen */ };
player->onShowEnd = []() { /* close the show's window */ };
window->AddChild(player);
player->Start(0);                          // the first slide shown at or after slide 0
```

| Call | What it does |
|---|---|
| `Start(fromSlide)` | Starts at the first slide shown at or after `fromSlide` (0-based), without a transition, and takes the keyboard |
| `Stop()` | Ends the show: `onShowEnd` |
| `Next()` | The next slide shown, with its transition; after the last, the end screen; from the end screen, the show ends |
| `Previous()` | The slide shown before, without a transition; from the end screen, the last slide |
| `GoToSlide(index)` | That slide (a hidden one too), at once |
| `IsPlaying()`, `GetCurrentSlide()` | Whether it plays; the slide shown (during a transition the one coming on), -1 on the end screen |
| `IsTransitioning()`, `GetTransitionProgress()`, `GetTransitionFromSlide()` | A transition in progress, how far (0..1), the slide going off |
| `SetBlank(SlideShowBlank)`, `GetBlank()` | A black or white screen over the slides |
| `SetClock(fn)`, `Tick()` | The time source (seconds; the default reads `steady_clock`) and bringing the show to it |
| `onSlideChanged(int)`, `onShowEnd`, `AddObserver(fn)` / `RemoveObserver(id)` | The slide shown changed; the show ended; any change, for a view that follows the player |

The keys and clicks are PowerPoint's:

| Input | What |
|---|---|
| Click, Space, Enter, Right, Down, Page Down, N, wheel down | On |
| Right-click, Backspace, Left, Up, Page Up, P, wheel up | Back |
| Home, End | The first, the last slide shown |
| A number, then Enter | That slide (Backspace takes a digit back); the digits show small in the corner as they are typed |
| B or `.`, W or `,` | A black, a white screen; the same key again, or any other, brings the slide back |
| Escape | Ends the show (`onShowEnd`) |

### Transitions

Going **on** to a slide plays its `PptxSlide::transition` over its
`durationSec`; going back, Home, End and a slide by number show it at once.
Each slide is drawn once at the size shown (`RenderPptxSlide`), kept for the
transitions, and drawn again when the size changes; a frame draws the two
cached pictures, so a transition costs no slide painting while it runs. The
application's timer drives it at about 60 frames a second and stops when
nothing is due.

| `PptxTransitionKind` | What it looks like |
|---|---|
| `Fade` | The old slide fades out as the new one fades in |
| `Push` | The new slide comes in from the right and pushes the old one out to the left |
| `Wipe` | The new slide is uncovered from left to right over the old one |
| `Split` | The new slide opens from a line down the centre, outwards to the left and right |
| `Cover` | The new slide slides in from the right over the old one, which stays |
| `Zoom` | The new slide grows from the centre over the old one |
| `NoTransition` | The new slide at once |

The movement eases in and out (`SlideTransitionEase`, a smoothstep). A key or
a click during a transition finishes it at once, and does nothing else.

### Timings

A slide whose `advanceAfterSec` is 0 or more moves on by itself that long
after it is fully shown (after its transition). One whose `advanceOnClick`
is false ignores a click - the keys still move on. A black or white screen
holds the timing; it starts again when the slide is shown again.

### Testing without time

`SetClock` replaces the time source, and `Tick()` brings the show to the
clock: it ends a transition that is over and moves on from a slide whose time
is up. Without an application (a test) no timer runs, and a test drives the
player by `OnEvent` and its clock:

```cpp
double now = 0;
auto player = CreateSlideShowPlayer("show", 0, 0, 320, 180);
player->SetClock([&now]() { return now; });
player->SetDeck(deck);
player->Start(0);
player->Next();                // the next slide's transition starts
now += 0.5;
player->Tick();                // half a second later
```

## The presenter view

```cpp
#include "UltraCanvasSlideShowPlayer.h"

auto presenter = CreatePresenterView("presenter", 0, 0, 1280, 720);
presenter->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Pct(100));
window->AddChild(presenter);
presenter->SetPlayer(player);              // follows it, and drives it
```

It shows the slide shown, large; the next slide shown, small (or *End of
slideshow*); the slide's speaker notes as plain text in a read-only,
scrolling `UltraCanvasTextArea`; the time since the show began, with *Pause*
/ *Resume* and *Reset*; *Previous slide* and *Next slide* buttons; and
"Slide N of M". It follows the player through `AddObserver`, and the keys
typed into it go to the player, so the show can be worked from either
screen. The time stops when the show ends.

| Call | What it does |
|---|---|
| `SetPlayer(player)` | The player followed and driven (kept alive while set); null: none |
| `GetShownSlide()`, `GetPositionText()`, `GetNotesText()`, `GetElapsedText()` | What it shows |
| `GetElapsedSeconds()`, `SetTimerPaused(bool)`, `IsTimerPaused()`, `ResetTimer()` | The time |
| `SetClock(fn)`, `RefreshElapsed()` | The time source; the time shown brought up to date (a timer does it every second) |

## Two screens: StartSlideShow

```cpp
std::shared_ptr<SlideShowWindows> show = StartSlideShow(deck, 0);
show->onEnd = []() { /* the windows are closed: drop `show`, focus the editor */ };
```

`StartSlideShow(deck, fromSlide, presenterView)` lists the monitors with
`UltraCanvasDesktopShell::ListScreens()` (the primary first). With two or
more it opens the show full screen on the second and, with `presenterView`,
the presenter view full screen on the first; with one, the show alone, full
screen. Each window is moved onto its monitor (`SetWindowPosition`, in the
screen pixels `ListScreens` gives) before it goes full screen, so full screen
fills that monitor. The windows close when the show ends - Escape, on from
the end screen, or one of them closed - and then `onEnd` runs. Keep the
returned `SlideShowWindows` until then.

What the platforms give:

| Platform | Monitors | Placement |
|---|---|---|
| Windows | `EnumDisplayMonitors` | Each window on its monitor |
| Linux, X11 | RandR's monitors (no primary chosen: the one at the origin) | Each window on its monitor |
| Linux, Wayland | The compositor's outputs | The compositor places windows itself: both open where it puts them |
| macOS | None listed yet (`ListScreens` is empty there) | The show alone, full screen |

A host that wants its own arrangement builds the same from the parts: a
window with a player for the show, a window with a presenter view given that
player, `ListScreens` to choose the monitors, `SetWindowPosition` and
`SetFullscreen(true)`.

## Not yet

- Animations inside a slide (entrances, emphasis, exits) are not played; the
  reader does not keep them.
- Transitions' directions and variants (Push from the top, Split
  horizontally) are not read: each kind plays its one look.
- A pen or a laser pointer over the slide.

## Tests

- `Tests/SlideShowPlayerTest.cpp` - by events and a test clock: hidden
  slides skipped, the end screen and on from it, going back without a
  transition, a transition half way (the slide coming on is the current
  one; a key or click finishes it), a mid-fade frame drawn on an off-screen
  page mixing both slides' colours, a slide's timing, a click ignored where
  only the timing moves on, a slide by number, Home and End, black and white
  screens, Escape; the presenter view following the player, its notes, keys
  and time; the transition frames' geometry.
