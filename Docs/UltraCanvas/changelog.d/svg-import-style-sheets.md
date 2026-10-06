- **SVG import applies `<style>` sheets.** The Vector plugin's SVG reader
  skipped every `<style>` element, so a drawing that colours its shapes by
  class came in with all of them black (SVG's default fill), its rounded
  corners square and its centred titles left-aligned - an architecture
  diagram whose page background is `.container { fill: #f8f9fa; rx: 8 }`
  imported as a black page in ArtCreator, while UltraFiler's thumbnail
  (librsvg) drew it correctly. The reader now gathers every `<style>`,
  inside `<defs>` or not and CDATA or not, skipping one whose `media` does
  not match. It parses them with the HTMLReader's CSS subset
  (`HTML::StyleSheet`) and matches the selectors against the SVG tree: type,
  class, id, attribute and structural pseudo-classes, and descendant chains.
  Each property cascades as SVG 2 specifies: presentation attribute, then
  the sheets by specificity and order, then `style=""`, with `!important`
  turning the last two round (`style=""` now honours `!important` too, which
  it used to read as part of the value). The geometry properties `x`, `y`,
  `width`, `height`, `rx`, `ry`, `cx`, `cy` and `r` can come from a sheet on
  rects, circles and ellipses. The reader note "`<style>` is not supported"
  is gone. `Tests/SVGConverterTest.cpp` covers the cascade.
