- **DemoApp's pages go through `Tr`.** Every text a person reads on the
  ~160 element pages - page titles and descriptions, card texts and notes,
  buttons, labels, dropdown and menu items, tabs, tooltips, chart titles,
  axis and series names, generic sample labels, status lines and read-outs -
  now goes through `Tr` / `TrF` / `TrN` / `TrC`, and sentences once glued from
  English pieces with `+`, `ostringstream` or `snprintf` are one translatable
  sentence each. `media/locale/en/UltraCanvasDemo.json` grows to 7,357 texts.
  Left as they are on purpose: element ids, code samples and parser input
  (Mermaid, PlantUML, ReqIF, JSON), proper names in sample data, format
  strings, units, and every string the code compares or looks up (where a
  label doubled as a key, the key is now separate). Static text tables are
  built at runtime so `Tr` sees the language in use. The translations follow.
