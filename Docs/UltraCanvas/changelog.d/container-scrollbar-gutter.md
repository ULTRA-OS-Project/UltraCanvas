- **UltraCanvasContainer: children are laid out clear of the vertical
  scrollbar.** A scrolling container laid its children out across its full
  width and then clipped them a scrollbar's width short, so the right edge
  of every row sat under the bar - a right-aligned value lost its unit, a
  slider its end, a card its border (SmartHome Center's Home cards, the
  Smart Home light control in its dialog) - and with both bars allowed the
  bar's width made up a horizontal overflow. Once the vertical bar shows,
  the container lays its children out again a track narrower; narrower
  content is only taller, so the bar stays and the second pass is the last.
  `ScrollContainerGutterTest` covers it. The Smart Home light control's own
  workaround is gone.
