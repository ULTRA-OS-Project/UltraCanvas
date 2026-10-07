- **CSS selector matching is one piece of code for every tree.** The HTML
  style resolver matched selectors with a private method typed on the HTML
  DOM, so the Vector plugin's SVG reader, which holds tinyxml2 elements, had
  to carry a copy of it to apply `<style>` sheets. The matcher now lives in
  `CSSStyleSheet.h` as `SelectorMatches` / `CompoundMatches` /
  `MatchingRules`, written against a small Traits type a tree supplies
  (`NodeSelectorTraits` is the DOM's; `HTML::Matches(selector, node)` is the
  stand-alone test a querySelector is made of), with the attribute operators
  and the an+b arithmetic as plain functions. The resolver uses it; a second
  tree joins with ten one-line traits. `Tests/HTMLReaderTest.cpp` runs the
  matcher over an SVG-shaped tree that is not the DOM.
- **The HTMLReader module is documented and registered.** It had no page
  under `Docs/UltraCanvas/`, no row in `Masterfile_modules.md` and no entry
  in `llms.txt`, which is how seven tag strippers and entity tables came to
  be written around it. `Docs/UltraCanvas/UltraCanvasHTMLReader.md` is the
  page; AGENTS.md states the rule (HTML, CSS and entities are read through
  the module); `scripts/check_html_reuse.py` and `html-reuse.yml` block a new
  copy, with the existing sites in `scripts/html_reuse_baseline.txt` to be
  worked off.
