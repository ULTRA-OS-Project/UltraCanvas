# UltraCanvasHTMLReader — HTML, CSS and entities, once

<!-- doc-check: void Log(const std::string& line); void Remember(const std::string& href); std::vector<uint8_t> LoadBytes(const std::string& href); void Open(const std::string& href); bool LoadCidPart(const std::string& src, std::string& mimeType, std::vector<uint8_t>& data); struct MyElement; struct Message { std::string htmlBody; }; Message message; -->

The **HTMLReader** module (`UltraCanvas/{include,core}/HTMLReader/`) is the
framework's one implementation of reading HTML and CSS: a tolerant parser, a
DOM, a CSS parser with selector matching and a cascade, a builder that turns
the styled DOM into native UltraCanvas elements laid out by the CSSLayout
engine, and an importer that turns it into an editable `UCRichDocument`. It
also owns the two small helpers every other place used to re-implement:
decoding entities and reducing markup to plain text.

It is part of the core library on every platform, with no build option
(`UltraCanvas/CMakeLists.txt`, the *HTMLReader* block), so any core file,
plugin or application may depend on it. The parse, CSS and resolve layers use
only the standard library, which is why `Tests/HTMLReaderTest.cpp` builds
them without linking UltraCanvas.

Who uses it today: the eBook viewer and the EPUB, FB2 and MOBI engines
(`HTML::ElementBuilder`), UltraMail's message pane (`HTML::ElementBuilder`),
UltraMail's composer, replies and signatures (`ImportHTMLToRichDocument`),
a rich paste into `UltraCanvasRichTextEdit` (`UCRichDocument::FromHTML`, on
the same importer), UltraMail's header decoding (`HTML::DecodeEntities`), its
scam check (the parsed page's links and pictures, `HTML::ExtractPlainText`)
and plain-text view (`PlainTextLayout::Lines`), EmailCleaner's matching, the
Filer's `.html` preview and UltraCloud's WebDAV client, and, from its pull
request 696 on, the Vector plugin's SVG reader (`HTML::StyleSheet` for
`<style>` sheets; its own copy of the selector matcher is to become a Traits
type for the shared one, below), and UltraWeb's web pages (`HTML::PageLoader`
and `UltraCanvasWebView`, below).

**The rule** (AGENTS.md, *Core conventions*): HTML, CSS and HTML entities are
read through this module. Do not write another tag stripper, entity table,
`style=""` splitter or selector matcher; add what is missing here, so the
next caller finds it. `scripts/check_html_reuse.py` flags new ones.

```cpp
#include "HTMLReader/HTMLParser.h"          // Parser, Document, Node
#include "HTMLReader/CSSStyleSheet.h"       // StyleSheet, Selector, matching
#include "HTMLReader/HTMLStyleResolver.h"   // StyleResolver, ComputedStyle
#include "HTMLReader/HTMLElementBuilder.h"  // ElementBuilder (needs the UI library)
#include "HTMLReader/HTMLRichDocumentImporter.h"
#include "HTMLReader/HTMLPageLoader.h"      // PageLoader: a URL to a page
using namespace UltraCanvas;
```

Everything but the importer lives in `namespace UltraCanvas::HTML`.

## Pick the entry point

| You have | You want | Call |
|---|---|---|
| HTML text | something to show on screen | `HTML::ElementBuilder::Build` → a container tree, hosted in a scroll view |
| A URL | the page, shown and browsable | `UltraCanvasWebView` (an element), or `HTML::PageLoader` to load it yourself |
| HTML text | text to edit in `UltraCanvasRichTextEdit` | `ImportHTMLToRichDocument` / `AppendHTMLToRichDocument` |
| HTML text | its words, for a list or a search index (one line), or to read or quote (`PlainTextLayout::Lines`) | `HTML::ExtractPlainText` |
| A string with `&amp;`, `&#233;`, `&nbsp;` | the characters | `HTML::DecodeEntities` |
| HTML text | the links, images, headings or any other elements | `HTML::Parser::Parse`, then walk the `Document` or use `HTML::Matches` with a selector |
| CSS text and your own tree (SVG, XML) | which rules apply to an element | `HTML::StyleSheet::ParseAppend`, then `HTML::MatchingRules<YourTraits>` |
| The content of a `style=""` attribute | its declarations | `HTML::StyleSheet::ParseDeclarationList` |
| A `media=""` attribute or `@media` query | whether it applies at a width | `HTML::StyleSheet::MediaMatches` |

## The parser and the DOM

`HTML::Parser::Parse` reads HTML or XHTML the way documents are actually
written: unclosed `<p>` and `<li>`, void elements, self-closing XHTML
syntax, comments, CDATA, a doctype, entities, and the raw-text bodies of
`<style>` and `<script>`. As HTML's tree builder does, it drops the one
newline right after a `<pre>`, `<listing>` or `<textarea>` start tag, so
content that starts on the next source line has no empty first line; a
second newline is a blank line. It never fails; `Errors()` lists what it had to
repair. The result is an `HTML::Document` whose `root` is always an `<html>`
with a body, even for a fragment, so `Body()` is always there.

```cpp
HTML::Parser parser;
HTML::Document doc = parser.Parse(html);
for (const std::string& problem : parser.Errors()) Log(problem);

doc.title;             // <title>
doc.doctype;           // text after DOCTYPE, "" when none
doc.quirksMode;        // no standards doctype: laid out as browsers lay out such pages
doc.styleSheets;       // the <style> blocks' text, in order
doc.styleSheetLinks;   // the hrefs of <link rel="stylesheet">, for the caller to load
doc.meta;              // <meta name="..." content="...">

HTML::Node* body = doc.Body();
HTML::Node* heading = body->FindFirst("h1");
std::string text = heading ? heading->TextContent() : "";
body->ForEachElement([](HTML::Node& n) {
    if (n.IsElement("a") && n.HasAttribute("href")) Remember(n.GetAttribute("href"));
    return true;   // false stops the walk
});
```

`HTML::Node` is one struct for every node; `type` says whether `tag` and
`attributes` or `text` are meaningful. **Tag and attribute names are stored
lower-case for HTML elements**; attribute values, text, class names and ids
keep their case. Inside an inline `<svg>` or `<math>` (foreign content, HTML
standard 13.2.6.5) names carry the case their vocabulary defines -
`linearGradient`, `viewBox`, `definitionURL` - whatever case the source
used, and HTML resumes inside `foreignObject`, `desc`, `title` and the MathML
text elements. Attribute lookup is exact first and then ASCII
case-insensitive, so `GetAttribute("viewbox")` finds `viewBox`.
`ClassList()`, `HasClass()`, `GetId()`, `GetElementById()` do what their
names say. `parent` is a raw back pointer, children are `shared_ptr`.

**Depth.** Elements nest at most `HTML::kMaxTreeDepth` (128) deep. An element
that would open deeper is kept as a leaf, and what follows it lands beside it,
as browsers flatten past their own limit, and `Errors()` says so. The resolver,
the builder, the layout and the DOM's own destructor all recurse per level, so
without the cap a few kilobytes of nested `<div>`s in a mail overflowed the
stack. 128 is far deeper than real mail or books nest, and leaves the builder
and the layout a wide margin on a 1 MB stack.

Two helpers need no DOM of the caller's:

```cpp
std::string plain = HTML::ExtractPlainText(html);   // tags gone, <script>/<style> bodies dropped,
                                                     // entities decoded, whitespace collapsed
std::string text = HTML::ExtractPlainText(html, HTML::PlainTextLayout::Lines);   // line by line
std::string s = HTML::DecodeEntities("Tom &amp; Jerry &#8212; &eacute;");   // every HTML 4 entity, numeric too
std::string safe = HTML::EscapeText("a < b & \"c\"");   // the inverse: text to put in HTML
```

`ExtractPlainText` with the default `PlainTextLayout::SingleLine` is what a
message list's preview line, a search index or a keyword filter want: block
boundaries, `<br>` and pictures become spaces, everything is one line. An
inline element (`<b>`, `<span>`, `<a>` ...) leaves no space, as on screen -
`wor<b>ld</b>` is "world", and a spam word split by formatting
(`<b>via</b>gra`) is still the word. A no-break space counts as a space.

`ExtractPlainText(const Node&)` gives the same single line for an element
already parsed - what a link, a cell or a heading says, read from the DOM.
UltraMail's threat scan reads every link's text that way while it walks the
parsed page for `a[href]`, `area[href]` and `form[action]`.

`PlainTextLayout::Lines` is the text a person reads or quotes - an HTML mail
shown as plain text, the quote in a reply. It parses the page and writes it
the way a browser's `innerText` does, simplified:

- a block element (`<div>`, `<li>`, `<tr>`, `<table>` ...) on lines of its
  own, a blank line around `<p>`, headings, `<blockquote>`, `<pre>` and
  `<hr>` - never more than one blank line in a row;
- `<br>` a line break, two of them a blank line;
- the cells of a table row a tab apart;
- list items as `- item`, or `1. item` in an `<ol>` (from its `start`);
- `<pre>` as written; elsewhere whitespace collapses to single spaces, and a
  no-break space is a space;
- what a browser does not show is left out: `<head>`, `<script>`, `<style>`,
  `<template>`, `<select>`, `<svg>`, the `hidden` attribute, and an inline
  `display: none`, `visibility: hidden` or `mso-hide: all` (a mail's hidden
  preheader). A class that hides through a style sheet is not looked up.

## CSS: the style sheet model

`HTML::StyleSheet::ParseAppend(css)` reads a sheet and appends its `rules`,
keeping cascade order across calls. It handles comments, `<!-- -->` around a
sheet, `@media` blocks (applied when their query holds at `SetMediaWidth`),
and skips other at-rules whole. A selector it cannot read drops out of its
rule alone; the rule's other selectors still apply.

`@import` is skipped by `ParseAppend` and listed by
`StyleSheet::ParseImports(css)`: the rules at the head of the sheet, each an
`href` as written and its `media` (`layer()` and `supports()` dropped). Only
those before the first other rule count, as in CSS. Whoever loads sheets
fetches them: the element builder puts each imported sheet in front of the
importing sheet's rules, in an `@media` block when the import names media,
reading its address relative to the importing sheet (an EPUB's
`css/book.css` importing `fonts.css` reads `css/fonts.css`), 8 deep and 64
sheets at most, each sheet once. `HTML::PageLoader` fetches them before a
page is ready.

Selectors: type, `*`, `.class`, `#id`, attribute selectors (`[a]`, `[a=v]`,
`~=` `^=` `$=` `*=` `|=`, the `i` flag), `:link` / `:any-link`, the
structural pseudo-classes (`:first-child`, `:last-child`, `:only-child`,
`:nth-child(an+b)`, the `-last-` and `-of-type` forms, `:root`, `:empty`),
and descendant chains. The child combinator `>` is read as a descendant
combinator; sibling combinators and dynamic pseudo-classes (`:hover`) drop
the selector, since nothing is hovered in a document.

**Case.** The parser lower-cases type selectors, attribute names, property
names and pseudo-class names. Class names and ids are kept as written, as CSS
requires: `.Hot` and `.hot` are different classes. A tree with camelCase
element names (SVG's `linearGradient`) therefore compares type selectors
case-insensitively in its traits (below); the HTML DOM's traits do the same
for a foreign element inside a page, and compare its lower-case HTML names
directly.

Values: `HTML::CssColor::Parse` (`#rgb`, `#rrggbb`, `#rrggbbaa`, `rgb()`,
`rgba()`, named colours), `HTML::CssLength::Parse` with `ToPx(em, rem,
percentBase)`. Numbers are read with `ParseFloatClassic`: dot-decimal
whatever the process locale, as *Core conventions* in AGENTS.md requires.

```cpp
HTML::StyleSheet sheet;
sheet.SetMediaWidth(480);                     // before ParseAppend: @media is answered for it
sheet.ParseAppend(doc.styleSheets[0]);
for (const HTML::Rule& rule : sheet.rules)
    for (const HTML::Declaration& d : rule.declarations)
        ;   // d.property (lower-case), d.value (trimmed, "!important" removed), d.important

auto declarations = HTML::StyleSheet::ParseDeclarationList("fill: red; stroke-width: 2 !important");
bool print = HTML::StyleSheet::MediaMatches("print, screen and (min-width: 600px)", 480);   // false

for (const HTML::StyleImport& import : HTML::StyleSheet::ParseImports("@import url(a.css) print; p{}"))
    ;   // import.href == "a.css", import.media == "print"
```

## Selector matching, on any tree

Which selector matches which element is decided in one place,
`CSSStyleSheet.h`, for every tree the framework styles. The matcher is
written against a *Traits* type rather than the HTML DOM, so a reader with
its own element type - the Vector plugin's SVG reader holds tinyxml2
elements - matches through the same code as the HTML style resolver, and a
selector feature added once (a new pseudo-class, an operator) reaches both.

```cpp
namespace UltraCanvas { namespace HTML {
template <class Traits> bool CompoundMatches(const SimpleSelector&, const typename Traits::Element&);
template <class Traits> bool SelectorMatches(const Selector&, const typename Traits::Element&);
template <class Traits> std::vector<const Rule*> MatchingRules(const StyleSheet&, const typename Traits::Element&);
bool AttributeValueMatches(const AttributeSelector&, const std::string& value);
bool NthPositionMatches(const PseudoClass&, int index, int count);
} }
```

`MatchingRules` returns the rules that match, weakest first: by the
specificity of the rule's best matching selector, then source order. Apply
their declarations in that order, the normal ones first and the `!important`
ones after, and the author cascade is done; a `style=""` attribute's normal
declarations go after the normal rules and its `!important` ones last of all
(CSS Cascading 4, §6.1 - `HTMLStyleResolver.cpp` does exactly this).

A tree joins by supplying a Traits type with these static members:

```cpp
struct MyTraits {
    using Element = MyElement;
    static bool TagIs(const Element&, const std::string& lowerTag);   // name without namespace prefix
    static bool IdIs(const Element&, const std::string& id);          // exact
    static bool HasClass(const Element&, const std::string& name);    // exact, one of the class words
    static bool GetAttribute(const Element&, const std::string& lowerName, std::string& value);  // false when absent
    static bool IsLink(const Element&);        // <a href>, for :link
    static bool IsRoot(const Element&);        // the document element, for :root
    static bool IsEmpty(const Element&);       // no child elements, no text, for :empty
    static bool SiblingPosition(const Element&, bool ofType, int& index, int& count);  // 1-based, among element siblings
    static const Element* Parent(const Element&);   // nullptr at the top
};
```

The HTML DOM's traits are `HTML::NodeSelectorTraits` in
`HTMLStyleResolver.h`, and `HTML::Matches(selector, node)` is the one-line
test on a node - what a `querySelector` is made of:

```cpp
HTML::StyleSheet probe;
probe.ParseAppend("a[href^='mailto:'] { x: y }");          // a selector, parsed once
const HTML::Selector& mailLinks = probe.rules[0].selectors[0];
doc.Body()->ForEachElement([&](HTML::Node& n) {
    if (HTML::Matches(mailLinks, n)) Remember(n.GetAttribute("href"));
    return true;
});
```

`Tests/HTMLReaderTest.cpp` (`TestSelectorMatchingOnForeignTree`) runs the
matcher over a small SVG-shaped tree that is not the DOM, so the contract is
checked, not described.

## The style resolver

`HTML::StyleResolver` runs the cascade for a whole document: user-agent
defaults per tag, the presentational attributes mail still uses (`align`,
`bgcolor`, `width`, `cellpadding`, …), the author sheets added with
`AddStyleSheet`, and each element's `style=""`, with inheritance of the text
properties and resolution of lengths to px. One `ComputedStyle` per element:
display mode, font, colour, alignment, line height, margins, padding,
borders per side, backgrounds (several layers, position, size, repeat),
widths and heights in px or percent, floats, `box-sizing`, `overflow`,
`object-fit`, flex and grid (below), links.

```cpp
HTML::StyleResolver resolver;
resolver.SetMediaWidth(paneWidth);
for (const std::string& css : doc.styleSheets) resolver.AddStyleSheet(css);
HTML::ResolverOptions options;
options.baseFontSizePx = 14;
options.overrideAuthorColors = true;   // a reading mode recolours everything
resolver.Resolve(doc, options);
const HTML::ComputedStyle& s = resolver.StyleOf(heading);
```

### Flex and grid

`display: flex` / `grid` keep `display` a block (and `inline-flex` /
`inline-grid` an inline block): how the box sits among its siblings does not
change. What changes is `layoutMode` (`BoxLayoutMode::Flex` or `Grid`), which
says how it lays out its children. The resolver reads:

| Properties | Into |
|---|---|
| `flex-direction`, `flex-wrap`, `flex-flow` | `flexDirection`, `flexWrap` |
| `justify-content`, `align-items`, `align-content`, `justify-items`, `place-items`, `place-content` | `BoxAlignMode` fields; `Unset` is CSS's `normal` (`safe` / `unsafe` dropped) |
| `gap`, `row-gap`, `column-gap` (and the `grid-` names) | `rowGapPx`, `columnGapPx` (a percentage is 0) |
| `flex`, `flex-grow`, `flex-shrink`, `flex-basis`, `order`, `align-self`, `justify-self`, `place-self` | the item fields; `flex: 1` is `1 1 0`, `none`, `auto` as in CSS |
| `grid-template-columns`, `grid-template-rows`, `grid-template`, `grid` (rows / columns) | `GridTemplate`: px, %, `fr`, `auto`, `min-content`, `max-content`, `fit-content()`, `minmax()`, `repeat(N, ...)`, one `repeat(auto-fill / auto-fit, ...)`; line names dropped |
| `grid-template-areas` | `gridAreas`, one row of names per string, `.` for none |
| `grid-column`, `grid-row`, `grid-area` and the `-start` / `-end` longhands | `GridLineSpec`: auto, a line (negative from the end), `span N`, an area name |
| `grid-auto-flow` | `gridAutoFlowColumn` |

An invalid value (a `minmax()` with an `fr` minimum, two auto repeats,
`subgrid`, rows of areas of unequal length) is ignored, as in CSS. Track
counts, line numbers, spans and `order` are capped at `HTML::kMaxGridLines`
(1000), so `grid-column: 99999` or `repeat(99999999, 1px)` cannot make the
layout build a grid that size.

`Docs/UltraWeb/UltraWebProposal.md` §5.2 lists what is still not mapped: no
`position`, `float` only through the existing float path, no viewport units
or `calc()` in the resolver.

## The element builder

`HTML::ElementBuilder` turns HTML into a native element tree - containers for
blocks with CSSLayout box properties, `UltraCanvasLabel` runs with Pango
markup for inline text, `UltraCanvasImageElement` for pictures, the
CSSLayout table engine for tables - so there is no separate HTML layout
engine and every element behaves like the rest of the framework. This is the
UI half of the module; it needs the UltraCanvas library.

```cpp
HTML::BuildOptions opts;
opts.style.baseFontSizePx = 12;
opts.viewportWidth = pane->GetWidth();        // @media (min-width) is answered for the pane
opts.userCss = "body { margin: 0 }";          // the reader's own sheet, after the document's
opts.enableImages = true;
opts.resourceLoader = [&](const std::string& href) { return LoadBytes(href); };   // <img src>, <link rel=stylesheet>
opts.onLinkActivated = [&](const std::string& href) { Open(href); };
opts.onLinkHovered = [&](const std::string& href) { status->SetText(href); };
opts.linkTooltips = false;                    // the status line shows the address instead
opts.selectableText = true;                   // the text selects and copies, as in a browser

HTML::ElementBuilder builder;
HTML::BuildResult r = builder.Build(html, opts);     // or BuildDocument(doc, opts) for a parsed one
if (r.root) {
    r.root->size.width = CSSLayout::Dimension::Pct(100.0f);
    scrollView->AddChild(r.root);          // a plain container with autoShowScrollbars on
    auto target = r.anchors.find("chapter-3");   // id / name → element, for #fragment links
}
for (const std::string& w : r.warnings) Log(w);
```

With `selectableText` every label of the tree joins one
`UltraCanvasTextSelection` (`r.textSelection`): a drag runs on from
paragraph to paragraph, a double-click takes a word, Ctrl+C / Ctrl+A work
once the text has been clicked, and a text link opens when it is released
without a drag. Keep `r.textSelection` to offer Copy and Select All in a
menu (its `onContextMenu` is asked on a right-click); see
`UltraCanvasLabelExamples.md`, *Selectable text*.

Pictures are sized as a browser sizes a replaced element: `width` alone or
`height` alone gives the other in the picture's shape, both give a box of
their own, and min / max sizes clamp keeping the shape where CSS does. The
size becomes a width the height follows, so a narrower line shrinks the
picture in proportion - in its own shape, or in the box's where the author
gave one (`UltraCanvasImageElement::SetBoxAspectRatio`). `object-fit: fill`
(the default) stretches the picture only into a box the author shaped
differently; any other box is the picture's shape, and the picture is fitted
keeping its proportions. A percentage height inside a table cell is a share
of the cell's set height, and auto when the cell sets none.

Table cells ignore `min-height` and `max-height`, as browsers do (CSS 2.1
leaves them undefined there): the spacer cell many mailers use -
`<td height="32" style="font-size:1px; line-height:1px; max-height:1px">` -
is as tall as its `height` says. A cell made `display: block` is a block,
and keeps them. A line is as tall as the tallest `line-height` of the inline
boxes on it, and at least the block's own: a `<span style="line-height:40px">`
in a cell that sets none is a 40px line. Text where nothing sets a
`line-height` keeps the font's own line spacing.

An inline element's horizontal margin, border and padding are room in its
line: `<nav><a style="margin-right:18px">About</a><a>Index</a></nav>` keeps
the two links 18px apart. The room is a picture-less placeholder in the
label's run, joined to the element's first or last letter by a word joiner,
so a line does not break between a word and its margin. The margin lies
outside the element's link and the padding inside it, and white space on
either side of the room collapses to one space. Vertical margins do not move
the line, as in CSS.

Its background and border are drawn by the label around its text and
padding (`LabelTextBox`), at any depth in a run (`<b><span
style="border:...">`): a piece per line when it breaks across lines, the
left border on the first piece and the right on the last, its vertical
padding painted above and below the text without moving the lines. An
inline element that has vertical padding as well as a background or a border
- the mail button, `<a style="background:#07f;padding:12px 24px;color:#fff">Buy</a>`
- is a box on the line instead, as an `inline-block` is, when it is a child of
the block: the line grows to hold its padding, and it does not break. A space
between a box and the text beside it stays: "Order now: [Buy] today".

**Flex and grid containers** are laid out by the CSSLayout flex and grid
engines. Their children are items, as in CSS. Each child element is a box of
its own at its own size (a `<span>` or `<a>` item is "blockified"), with real
margins that do not collapse. Each run of text between the elements is an
anonymous item formatted as the container, and whitespace between items
renders nothing. Flex items take `flex`, `order` and `align-self`, and
`align-items: normal` stretches them, as in CSS. Grid items take their lines,
spans and named areas: negative lines are counted from the end of the
explicit grid, and `span N / <line>` counts back from the line. A
`repeat(auto-fill, ...)` is written out as many times as fit the grid's width, estimated before layout from the viewport down
through its ancestors' widths, margins, padding and borders
(`EstimateContentWidth`); `auto-fit` keeps no more repetitions than there are
items. A grid without columns gets one column as wide as the grid, as CSS's
stretched implicit column is. `Tests/HTMLFlexGridLayoutTest.cpp` lays all of
this out and checks the positions.

Where the engines differ from CSS: `minmax(<min>, <n>fr)` is `<n>fr` (the
minimum still counts for auto-fill); grid items' margins are not applied; no
baseline alignment (`baseline` aligns to the start); `order` does not reorder
grid items.

The built tree has its own scrollbars disabled on purpose: host it in a
container that scrolls (see `UltraCanvasEBookViewer.cpp` and UltraMail's
`MessagePreview::RenderBody` for the two hosts that exist). For a page that
comes from a URL, `UltraCanvasWebView` (below) is that host.

## Pages from a URL

`HTML::PageLoader` turns a URL into a page the builder can show. It does
not reach the network itself: the caller passes in the fetch, URL
resolution and charset conversion (`HTML::PageLoaderServices`; UltraWeb
passes UltraNet's), so it is framework-independent and runs offline in tests.

- **Charset:** the BOM, the `Content-Type` charset, a `<meta charset>` in the
  first 1024 bytes, else UTF-8 when the bytes are valid UTF-8 and
  Windows-1252 when they are not.
- **Base:** `<base href>`, else the page's final URL after redirects. A
  `#fragment` is never fetched and stays with the address.
- **Style sheets:** linked sheets (up to 32) are fetched before the page is
  ready, so the first build has them. Each sheet's `url()`s are rewritten
  against the sheet's own URL (`RewriteCssUrls`), so they still mean the same
  inside the page. The sheets a linked sheet or a `<style>` imports
  (`@import`, up to 4 deep, within the 32) are fetched before the page is
  ready too, and their addresses rewritten to absolute ones.
- **Other resources** load on demand. `GetResource(href)` is the builder's
  `resourceLoader`: it answers from a cache (64 MB, oldest out first),
  queues what it does not have, and `onResourcesArrived` says when to build
  again.
- **Kinds:** HTML and XHTML. Plain text becomes a `<pre>` page, a picture a
  page of its own. Anything else (`PageKind::Other`, say `application/wasm`)
  goes back to the caller undecided, and a failed fetch is `PageKind::Error`.
- **Rules:** an http(s) page loads http(s) and `data:` resources, an https
  page no http ones, and only a `file:` page loads `file:` resources.
- **Limits:** 16 MB per document, 32 style sheets, 512 resources a page.
- **Threading:** the loader is used from one thread, and `fetch` calls its
  `done` back on it. A newer `Load` or `Cancel` drops the answers of the
  navigation before.

```cpp
HTML::PageLoaderServices services;
services.fetch = [](const std::string& url, std::function<void(HTML::FetchResult)> done) {
    HTML::FetchResult result;   // fetch url; fill ok, status, finalUrl, contentType, body
    done(result);               // on the loader's thread
};
services.resolve = [](const std::string& base, const std::string& reference, std::string& absolute) {
    absolute = base + reference;   // UltraWeb: UltraNet_ResolveUrl (RFC 3986)
    return true;
};
HTML::PageLoader loader(services);
loader.Load("https://example.org/", [&](HTML::LoadedPage page) {
    if (page.kind != HTML::PageKind::Html) return;
    HTML::BuildOptions options;
    options.resourceLoader = [&](const std::string& href) { return loader.GetResource(href); };
    HTML::ElementBuilder builder;
    HTML::BuildResult built = builder.BuildDocument(*page.document, options);
});
```

`UltraCanvasWebView` (`UltraCanvasWebView.h`) puts this together as an element: give
it the services and a URL, and it builds the page into a scrolling pane,
follows links, keeps the history (back / forward with each page's scroll
position), scrolls to `#fragments`, builds again when its width changes or
pictures arrive (on a later turn of the UI loop, never inside layout), and
shows an error page when a page cannot be loaded. `mailto:` and other
schemes go to `onExternalLink`, and a non-HTML answer to `onNonHtml`.
`<script>` is not run, and `<noscript>` and its content are shown.
`Tests/HTMLPageLoaderTest.cpp` and `Tests/WebViewTest.cpp` cover both
against a fake web. UltraWeb is the browser around it
(`Docs/UltraWeb/UltraWeb.md`).

```cpp
auto view = std::make_shared<UltraCanvasWebView>("web");
view->SetServices(services);
view->onTitleChanged = [](const std::string& title) { /* the window title */ };
view->onExternalLink = [](const std::string& url) { /* the system's mail client */ };
view->Navigate("https://example.org/");
```

### Forms

With `BuildOptions::interactiveForms` the builder makes a form's controls
the framework's widgets, so the reader can fill it in: `<input>` text-like
types and `password` are `UltraCanvasTextInput`s, `<textarea>` an
`UltraCanvasTextArea`, checkboxes and radios `UltraCanvasCheckbox` /
`UltraCanvasRadio` (radios with one name in one form are one
`UltraCanvasRadioGroup`), `<select>` an `UltraCanvasDropdown` (`<optgroup>`s
flattened), and buttons `UltraCanvasButton`s. They are `inline-block`, as in
browsers: on the line with their labels, sized by CSS `width` / `height`,
else by `size`, `cols` / `rows` and the font. A control can be a flex or
grid item (a search bar). `disabled`, a disabled `<fieldset>`, `readonly`,
`maxlength`, `placeholder` and `form="id"` are honoured. Colour and range
inputs are drawn for display only and not sent.

**File inputs** are a "Choose file…" button and the names chosen. The
button asks the host through `BuildOptions::onChooseFiles` (several files
with `multiple`, the input's `accept` list as written), and stays disabled
without it. The files are read when the form is sent: in
`FormSubmission::files`, each with its file name, a type from its
extension and its bytes, sent as multipart file parts; with nothing chosen,
one empty part, as browsers send it. Outside multipart a file is its name.
Files over `FormController::kMaxUploadBytes` (64 MB together), or that
cannot be read, go empty and are named in the submission's `warnings`.

**Labels.** A click on a `<label>`'s text acts on its control - the one
its `for="id"` names, else the first control inside it: a checkbox toggles,
a radio is chosen (its group follows), a button is pressed, a file input
opens its chooser, any other control takes the focus
(`FormController::ActivateControl`). The text is a *quiet* link
(`LabelTextLink::quiet`): clickable, with no tooltip and no hover report.

Each `<form>` gets an `HTML::FormController` (`HTMLReader/HTMLForms.h`) in
`BuildResult::forms`; keep them, since they hold the radio groups. A submit
button, or Enter in a text field, calls `BuildOptions::onFormSubmit` with
an `HTML::FormSubmission`: the form's action, method and enctype, and the
entry list in tree order, as HTML builds it (named, enabled controls; a
checkbox or radio only when checked, `on` without a value; the selected
option's value; only the button that was clicked). `Encoded()`,
`Multipart(boundary)` and `PlainText()` are its three encodings, and `Body`
/ `ContentType` pick the one the form asks for. A reset button puts back
what the page wrote. `Save()` / `Restore()` carry what was typed into a
rebuilt page.

```cpp
HTML::BuildOptions opts;
opts.interactiveForms = true;
opts.onFormSubmit = [](const HTML::FormSubmission& form) {
    if (form.method == "get") { /* open form.action + "?" + form.Encoded() */ }
    else { /* POST form.Body(boundary) as form.ContentType(boundary) */ }
};
HTML::ElementBuilder builder;
HTML::BuildResult r = builder.Build("<form action='/find'><input name='q'> <button>Go</button></form>", opts);
for (const auto& form : r.forms) {
    HTML::FormSubmission now = form->Collect();   // what it would send now
    (void)now;
}
```

`UltraCanvasWebView` turns forms on (`SetInteractiveForms(false)` turns them
off) and sends them itself (`SubmitForm`): GET as a navigation to the
action with the fields as its query, POST through
`PageLoader::LoadPost` - the `post` service the host passes in - and back
from the answer re-asks its address by GET. A rebuild of the page (a new
width, pictures arriving) keeps what was typed and the focus. Mail keeps
`interactiveForms` off: its controls are drawn for display only, the value
as text on a line of its own. The view passes its own `onChooseFiles` on to
file inputs; UltraWeb answers it with the file dialog.

## HTML into an editable document

`ImportHTMLToRichDocument(html, options)` and `AppendHTMLToRichDocument`
read HTML through the same parser and cascade and produce a `UCRichDocument`
for `UltraCanvasRichTextEdit`: paragraphs, headings, lists, rules, the
inline formats, links, colours, fonts and sizes, alignment and indents,
`<blockquote>` as quote level, right-to-left paragraphs (`dir="rtl"` on the
element or one around it), Word's list paragraphs as list items, pictures (`data:` URIs decoded, others through
`options.resolveImage`, else their alt text in brackets; a 1-2 px tracking
pixel left out), and tables, with the one-column scaffolding tables of mail
layouts unwrapped into the flow. See `UltraCanvasRichTextEdit.md` for the
document model and `Tests/HTMLRichImportTest.cpp` for what is covered.

Word writes a list as paragraphs, not `<ul>` / `<ol>` - on the clipboard and
in every mail Outlook sends: `<p class=MsoListParagraph style="mso-list:l0
level1 lfo1">`, with the label it shows typed out in front, inside a
`<span style="mso-list:Ignore">`. Such a paragraph becomes a list item at
that level, and its label its marker rather than text: a number, letter or
Roman numeral (`1.`, `a)`, `(iv)`) makes a numbered item in that format, and
gives it its own number only where the model's count would differ - a list
starting at 4, or one Word carries on past a paragraph between; a bullet in
Word's symbol fonts becomes the model's (`o` in Courier New a circle, `§` in
Wingdings a square). A numbered heading stays a heading, its number text.

Two options are for a paste rather than a mail, and are off by default:
`preAsCodeBlock` makes a `<pre>` a code block (a mail's `<pre>` is mostly a
quoted plain-text message, so there it stays monospaced lines of a
paragraph), and `skipWordListLabels` leaves out a label Word types out in a
`mso-list:Ignore` span where no list item takes it - the "1." of a numbered
heading (a browser shows it, so a mail keeps it). `UCRichDocument::FromHTML` - the editor's paste from
another application - is the importer with both on, and a no-break space
turned into a space.

```cpp
HTMLRichImportOptions options;
options.quoteLevel = 1;             // a reply quotes the message it answers
options.resolveImage = [&](const std::string& src, HTMLRichImportImage& out) {
    return LoadCidPart(src, out.mimeType, out.data);
};
UCRichDocument reply = ImportHTMLToRichDocument(message.htmlBody, options);
```

`UCRichDocument::ToHTML` is the writer for the other direction.

## What is still implemented elsewhere

Nothing: `scripts/html_reuse_baseline.txt` lists no site. The last ones -
`UCRichDocument::FromHTML`'s tokenizer, entity table and CSS reader, and the
Filer's tag-level line splitter for its `.html` preview - read through the
importer and `PlainTextLayout::Lines` now. `scripts/check_html_reuse.py`
blocks new ones.

## Limits

- Not the HTML5 tree-construction algorithm: adoption-agency cases and
  misnested formatting elements are repaired heuristically.
- Foreign content is parsed and named correctly, not rendered: the element
  builder shows an inline `<svg>`'s first raster `<image>` (EPUB cover pages)
  and skips its vector content. The SVG file readers do not go through
  `HTML::Parser`.
- `>` is a descendant combinator; sibling combinators and `:hover`-style
  pseudo-classes are not matched.
- The resolver knows no `position`, viewport units or `calc()`. Flex and
  grid are mapped, with the differences listed under the element builder.
- The builder is one-shot: it builds a tree, it does not patch one. A live
  DOM is UltraWeb's Phase 4 (`UltraWebProposal.md` §7.3).

## Files

| File | Contents |
|---|---|
| `include/HTMLReader/HTMLDocument.h` | `Node`, `Document`, `DecodeEntities`, `ExtractPlainText`, `IsQuirksDoctype` |
| `include/HTMLReader/HTMLParser.h` | `Parser`, `ParseOptions` |
| `include/HTMLReader/CSSStyleSheet.h` | `CssColor`, `CssLength`, `Declaration`, `Selector`, `Rule`, `StyleSheet`, the matcher templates |
| `include/HTMLReader/HTMLStyleResolver.h` | `ComputedStyle`, `ResolverOptions`, `StyleResolver`, `NodeSelectorTraits`, `Matches` |
| `include/HTMLReader/HTMLElementBuilder.h` | `BuildOptions`, `BuildResult`, `ElementBuilder`, `BuildElementsFromHTML` |
| `include/HTMLReader/HTMLRichDocumentImporter.h` | `ImportHTMLToRichDocument`, `AppendHTMLToRichDocument`, `HTMLRichImportOptions` |
| `include/HTMLReader/HTMLPageLoader.h` | `PageLoader`, `PageLoaderServices`, `PageLoaderLimits`, `FetchResult`, `LoadedPage`, `PageKind` |
| `include/HTMLReader/HTMLForms.h` | `FormSubmission`, `FormController` (forms with real widgets) |
| `include/UltraCanvasWebView.h` | `UltraCanvasWebView`, the element that shows pages from URLs |
| `Tests/HTMLReaderTest.cpp` | parser, CSS, matcher and resolver, without the UI library |
| `Tests/HTMLRichImportTest.cpp`, `HTMLTableLayoutTest.cpp`, `HTMLImageAlignTest.cpp`, `HTMLFlexGridLayoutTest.cpp`, `HTMLInlineBoxTest.cpp`, `HTMLFormsTest.cpp`, `EBookViewerTest.cpp` | importer, tables, images, flex and grid, inline boxes, forms, the viewer |
| `Tests/Fuzz/` | fuzz targets for the parser, the CSS, and the builder with the layout ([README](../../Tests/Fuzz/README.md)) |

## Fuzzing

Everything this module reads comes from someone else: mail, books, web
pages. `Tests/Fuzz` has a libFuzzer target for the parser and one for the
CSS, plus a builder-and-layout target. In every `BUILD_TESTS` build they run
as deterministic smoke tests in ctest (`HTMLParserFuzzSmoke`, `CSSFuzzSmoke`,
`HTMLBuilderFuzzSmoke`). The `html-fuzz.yml` workflow fuzzes the first two
under AddressSanitizer and UndefinedBehaviorSanitizer whenever this module
changes. A change to the parser or the resolver keeps the smoke tests
passing. A crash the fuzzer finds is fixed, and its input is added to
`Tests/Fuzz/corpus`. The first run found the nesting overflow that
`kMaxTreeDepth` now prevents.
