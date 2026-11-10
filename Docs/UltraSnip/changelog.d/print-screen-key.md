- **Print Screen starts a snip.** While UltraSnip runs, the Print Screen key
  starts a snip with the shape, kind and delay chosen on the bar, as Start
  does - taken from the system (`UltraCanvasGlobalShortcut::StartExclusive`),
  so on Windows it no longer opens the Snipping Tool as well. Alt+Print
  Screen and Win+Print Screen stay the system's. A press while a snip is
  under way (the screen dimmed for a choice, the delay, a recording) is
  ignored. *Settings > Behaviour > After a snip* has it, on by default.
