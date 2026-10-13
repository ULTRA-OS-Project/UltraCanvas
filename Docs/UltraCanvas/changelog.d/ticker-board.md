- **New element: `UltraCanvasTickerBoard`** (`Plugins/Diagrams/`), the ticker
  board the catalogue lacked: a strip of items - a label, a value, a change
  coloured by its trend - crawling across a band without end. Two faces: `Text`
  sets the strip in a font; `DotMatrix` lights it on a fixed grid of 5x7 LED
  dots that the text steps across one column at a time. It moves by itself
  from the first frame, pauses while the pointer rests on it, shows an item's
  tooltip, reports a click, and `UpdateItem` re-prices an item in place without
  a jump. `MakeTickerQuote` formats a quote with its arrow and percentage.
  Doc: `Docs/UltraCanvas/UltraCanvasTickerBoard.md`; a *Ticker Board* page in
  DemoApp's Diagrams section shows a market ticker, an amber LED sign, live
  dot-matrix quotes and a headline crawl.
