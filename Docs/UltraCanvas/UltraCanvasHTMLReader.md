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
UltraMail's header decoding (`HTML::DecodeEntities`), and the Vector
plugin's SVG reader (`HTML::StyleSheet` for `<style>` sheets, matched
through the shared selector matcher with a Traits type over its tinyxml2
elements, below). UltraWeb's page reader
(`Docs/UltraWeb/UltraWebProposal.md`, §5) is planned on it.

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
using namespace UltraCanvas;
```

Everything but the importer lives in `namespace UltraCanvas::HTML`.

## Pick the entry point

| You have | You want | Call |
|---|---|---|
| HTML text | something to show on screen | `HTML::ElementBuilder::Build` → a container tree, hosted in a scroll view |
| HTML text | text to edit in `UltraCanvasRichTextEdit` | `ImportHTMLToRichDocument` / `AppendHTMLToRichDocument` |
| HTML text | its words, for a list, a search index or a reply quote | `HTML::ExtractPlainText` |
| A string with `&amp;`, `&#233;`, `&nbsp;` | the characters | `HTML::DecodeEntities` |
| HTML text | the links, images, headings or any other elements | `HTML::Parser::Parse`, then walk the `Document` or use `HTML::Matches` with a selector |
| CSS text and your own tree (SVG, XML) | which rules apply to an element | `HTML::StyleSheet::ParseAppend`, then `HTML::MatchingRules<YourTraits>` |
| The content of a `style=""` attribute | its declarations | `HTML::StyleSheet::ParseDeclarationList` |
| A `media=""` attribute or `@media` query | whether it applies at a width | `HTML::StyleSheet::MediaMatches` |

## The parser and the DOM

`HTML::Parser::Parse` reads HTML or XHTML the way documents are actually
written: unclosed `<p>` and `<li>`, void elements, self-closing XHTML
syntax, comments, CDATA, a doctype, entities, and the raw-text bodies of
`<style>` and `<script>`. It never fails; `Errors()` lists what it had to
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

Two helpers need no DOM:

```cpp
std::string plain = HTML::ExtractPlainText(html);   // tags gone, <script>/<style> bodies dropped,
                                                     // entities decoded, whitespace collapsed
std::string s = HTML::DecodeEntities("Tom &amp; Jerry &#8212; &eacute;");   // every HTML 4 entity, numeric too
```

`ExtractPlainText` is what a message list's preview line, a search index or
a reply's quoted text want. It is not a layout: block boundaries become
spaces, not newlines.

## CSS: the style sheet model

`HTML::StyleSheet::ParseAppend(css)` reads a sheet and appends its `rules`,
keeping cascade order across calls. It handles comments, `<!-- -->` around a
sheet, `@media` blocks (applied when their query holds at `SetMediaWidth`),
and skips other at-rules whole. A selector it cannot read drops out of its
rule alone; the rule's other selectors still apply.

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
`object-fit`, links.

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

`Docs/UltraWeb/UltraWebProposal.md` §6 lists what the resolver does not map
yet (flex and grid come out as blocks, no `position`, no viewport units).

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

HTML::ElementBuilder builder;
HTML::BuildResult r = builder.Build(html, opts);     // or BuildDocument(doc, opts) for a parsed one
if (r.root) {
    r.root->size.width = CSSLayout::Dimension::Pct(100.0f);
    scrollView->AddChild(r.root);          // a plain container with autoShowScrollbars on
    auto target = r.anchors.find("chapter-3");   // id / name → element, for #fragment links
}
for (const std::string& w : r.warnings) Log(w);
```

The built tree has its own scrollbars disabled on purpose: host it in a
container that scrolls (see `UltraCanvasEBookViewer.cpp` and UltraMail's
`MessagePreview::RenderBody` for the two hosts that exist). A reusable
`UltraCanvasHTMLView` element wrapping that host is the planned next step,
so that a third reader does not build it a third time.

## HTML into an editable document

`ImportHTMLToRichDocument(html, options)` and `AppendHTMLToRichDocument`
read HTML through the same parser and cascade and produce a `UCRichDocument`
for `UltraCanvasRichTextEdit`: paragraphs, headings, lists, rules, the
inline formats, links, colours, fonts and sizes, alignment and indents,
`<blockquote>` as quote level, pictures (`data:` URIs decoded, others
through `options.resolveImage`), and tables, with the one-column scaffolding
tables of mail layouts unwrapped into the flow. See
`UltraCanvasRichTextEdit.md` for the document model and
`Tests/HTMLRichImportTest.cpp` for what is covered.

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

These sites predate the rule and are listed in
`scripts/html_reuse_baseline.txt`; each is a replacement waiting to happen,
and the check blocks new ones:

| Site | Has its own | Replace with |
|---|---|---|
| `UltraCanvas/core/UltraCanvasRichDocument.cpp` (`UCRichDocument::FromHTML`) | tokenizer, entity table, `ApplyCss` | `ImportHTMLToRichDocument` |
| `UltraCanvas/core/UltraCanvasFilerWidget.cpp` (the file preview) | entity decoder, tag stripper | `HTML::ExtractPlainText` |
| `Apps/UltraMail/ui/UltraMailMessagePreview.cpp` (`HtmlToText`) | tag stripper, four entities | `HTML::ExtractPlainText` |
| `Apps/UltraMail/engine/UltraMailThreatScan.cpp` | `DecodeEntities`, `StripTags`, an `<a href>` scanner | `HTML::Parser` + a walk over `a[href]`, `area[href]`, `form[action]` |
| `Apps/EmailCleaner/engine/EmailCleanerText.cpp` (`StripHtml`) | entity table, tag stripper | `HTML::ExtractPlainText` |
| `UltraCloud/providers/UltraCloudWebDav.cpp` (`DecodeEntities`) | the five XML entities | `HTML::DecodeEntities` decodes those too |

## Limits

- Not the HTML5 tree-construction algorithm: adoption-agency cases and
  misnested formatting elements are repaired heuristically.
- Foreign content is parsed and named correctly, not rendered: the element
  builder shows an inline `<svg>`'s first raster `<image>` (EPUB cover pages)
  and skips its vector content. The SVG file readers do not go through
  `HTML::Parser`.
- `>` is a descendant combinator; sibling combinators and `:hover`-style
  pseudo-classes are not matched.
- The resolver lays out flex and grid as blocks and knows no `position`.
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
| `Tests/HTMLReaderTest.cpp` | parser, CSS, matcher and resolver, without the UI library |
| `Tests/HTMLRichImportTest.cpp`, `HTMLTableLayoutTest.cpp`, `HTMLImageAlignTest.cpp`, `EBookViewerTest.cpp` | importer, tables, images, the viewer |
