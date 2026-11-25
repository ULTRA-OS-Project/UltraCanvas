- **A presentation is played as a slide show, with its transitions and
  timings.** New, in `UltraCanvasSlideShowPlayer.h`:
  - `UltraCanvasSlideShowPlayer` (`CreateSlideShowPlayer`): a deck's slides
    letterboxed on black, hidden ones left out, a black end screen after
    the last. Going on plays the slide's transition over its duration -
    Fade, Push, Wipe, Split, Cover, Zoom - from pixmaps drawn once at the
    size shown, at about 60 frames a second; going back shows the slide at
    once, and a key or click finishes a transition. A slide's
    `advanceAfterSec` moves on by itself, and one with `advanceOnClick` false
    ignores a click. PowerPoint's keys: on, back, Home, End, a number and
    Enter, B / W for a black or white screen, Escape. `SetClock` and `Tick`
    let a test run it without real time. It replaces the simple show mode
    of `UltraCanvasSlideEdit`.
  - `UltraCanvasPresenterView` (`CreatePresenterView`): the presenter's
    screen - the slide shown and the next one, the speaker notes, the time
    with Pause / Resume and Reset, Previous / Next slide, "Slide N of M" -
    following a player and driving it.
  - `StartSlideShow(deck, fromSlide)`: the show full screen on the second
    monitor and the presenter view on the first when
    `UltraCanvasDesktopShell::ListScreens` lists two, else the show alone.
  Documented in `Docs/UltraCanvas/UltraCanvasSlideShowPlayer.md`.
- **The photo slideshow's transitions are shared.** `UltraCanvasSlideTransition.h`
  holds the frames' geometry (`SlideTransitionFrame`: cross-fade, fade out
  and in, push left or up, wipe, split, cover, zoom, zoom-fade);
  `UltraCanvasSlideshow` draws from it, looking as before, and so does the
  player.
- The presentation words the player and presenter view show ("Next slide",
  "Previous slide", "Speaker notes", "Presenter view", "End of slideshow",
  "Slide show", "Slide {0} of {1}") moved into the standard set, with their
  translations, from the Presentation base set and UltraOffice's catalogue.

  Covered by `SlideShowPlayerTest`.
- `UltraCanvasSlideEdit`'s own show mode (`StartShow`, `StopShow`,
  `onShowEnd`) is gone: the player plays the deck. UltraOffice presents
  through `StartSlideShow`, and picks each slide's transition in its Slide
  show tab.
