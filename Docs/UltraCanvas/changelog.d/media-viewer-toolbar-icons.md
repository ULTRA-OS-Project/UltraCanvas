- **UltraCanvasMediaViewer: the toolbars are icons, not captions.** Open, Prev, Next,
  Slideshow, Zoom -, Zoom +, Fit, Rotate L/R, Mirror H/V, Adjust, Curves, Save as
  and Info were text buttons, and two rows of words did not fit the narrow
  preview pane UltraFiler gives the viewer. Every button is now an icon from
  `media/icons/` drawn as a mask (so it takes the toolbar's foreground colour and
  greys out with the button) with the caption as its tooltip, and so are the
  info bar's *Details* button and the adjustments panel's *Auto* and *Reset*.
  The interval, transition and zoom dropdowns stay text, because a value picker
  shows its value: the first two now start on their defaults (`5 s`, `Cross fade`)
  instead of blank, and all three carry a tooltip naming the value.
  - *Auto* is a toggle now, since auto-optimise latches: its pressed look says
    whether it is on, and *Reset* un-presses it with the sliders.
  - The Slideshow toggle follows the state whichever way it changed: Space and
    `PlaySlideshow()` / `PauseSlideshow()` press and release it too.
  - New icons under `media/icons/`: `rotate-left`, `rotate-right`, `curves`,
    `slideshow`, `transition`; `mirror-h`, `mirror-v`, `zoom-fit` and `wand` are
    copies of the ArtCreator / UltraPaint ones, at the root so core can use them.
