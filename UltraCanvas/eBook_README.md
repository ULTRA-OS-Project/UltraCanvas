# UltraCanvas eBook Support

Status: **under construction — rebuilt from scratch** (the previous eBook code
never compiled and was removed; see "History" below).

## Architecture

Rendering is native: chapters are converted to real UltraCanvas elements and
laid out by the CSSLayout engine. There is no separate HTML layout engine and
no page-image rendering pipeline for reflowable formats.

```
eBook file ──► format engine (IEBookEngine) ──► chapters as XHTML + CSS + resources
                                                        │
                                     HTML::Parser ──► DOM (HTML::Document)
                                                        │
                              HTML::StyleResolver ──► computed styles (CSS cascade)
                                                        │
                             HTML::ElementBuilder ──► UltraCanvasContainer / Label /
                                                      ImageElement tree
                                                        │
                                        CSSLayout engine lays it out natively
```

### Modules

| Module | Location | Status |
|--------|----------|--------|
| HTML/XHTML parser (tolerant, entities, recovery) | `include/HTMLReader/HTMLParser.h` | ✅ implemented + unit-tested |
| CSS subset (selectors, specificity, cascade) | `include/HTMLReader/CSSStyleSheet.h` | ✅ implemented + unit-tested |
| Style resolver (UA defaults, inheritance, inline) | `include/HTMLReader/HTMLStyleResolver.h` | ✅ implemented + unit-tested |
| DOM → element builder (Pango markup for inline styling) | `include/HTMLReader/HTMLElementBuilder.h` | ✅ implemented |
| Engine interface + registry (chapter-oriented) | `Plugins/Documents/eBook/IEBookEngine.h` | ✅ implemented |
| ZIP/DEFLATE container access (miniz) | `Plugins/Documents/eBook/EBookArchive.h` | ✅ implemented + unit-tested |
| EPUB 2/3 engine (container/OPF/NCX/nav, cover, resources) | `Plugins/Documents/eBook/EPUBEngine.h` | ✅ implemented + unit-tested |
| TXT engine | `Plugins/Documents/eBook/TXTEngine.h` | ✅ implemented + unit-tested |
| FB2 engine | — | 🔄 planned |
| Viewer widget (toolbar, TOC, reading modes) | — | 🔄 planned |
| MOBI / AZW3 / legacy formats | — | 🔄 later |

### The HTMLReader module

`UltraCanvas::HTML` is framework-independent up to the builder (parser, CSS,
resolver use std C++ only) so it is unit-testable without linking the library
(`Tests/HTMLReaderTest`). Supported CSS: element/class/id selectors with
descendant chains, `!important`, box model (margins/padding/width/height in
px/em/rem/pt/%), typography (font-size/family/weight/style, text-align,
text-decoration, line-height, white-space), colors (hex/rgb()/named),
list-style-type. Unsupported selectors (pseudo-classes, attribute selectors)
are dropped per-selector; at-rules are skipped.

Reading modes (night/sepia) use `ResolverOptions::overrideAuthorColors` so
author colors never fight the theme.

Known v1 approximations (documented in code):
- `>` child combinator matched as descendant
- tables render as flex rows (equal-width cells) — Grid mapping is a TODO
- inline images inside a text run render as `[alt]` placeholders; block-level
  images are fully supported
- `text-indent` maps to padding (no first-line indent in Label yet)
- no line-height on Label yet

### Engine interface

`IEBookEngine` is deliberately small and chapter-oriented: engines open a
container and hand out `EBookChapter { title, href, content(XHTML) }`,
stylesheets, and resources; the shared base class provides file loading,
plain-text extraction, and search. Engines self-register per extension via
`RegisterEBookEngine`, and viewers obtain one via `CreateEBookEngineForFile`.

### Usage sketch

```cpp
UltraCanvas::RegisterBuiltinEBookEngines();   // once at startup

auto engine = UltraCanvas::CreateEBookEngineForFile("book.epub");
engine->LoadFromFile("book.epub");

UltraCanvas::HTML::BuildOptions options;
options.style.baseFontSizePx = 18.f;
options.userCss = engine->GetStylesheets();
options.resourceLoader = [&](const std::string& href) {
    return engine->GetResource(href);
};

UltraCanvas::HTML::ElementBuilder builder;
auto chapter = engine->GetChapter(0);
auto result = builder.Build(chapter.content, options);
scrollContainer->AddChild(result.root);   // CSSLayout lays it out
```

## History

The original eBook implementation (viewer, 9 format engines, a private HTML
style/layout engine) was validated in July 2026 and found to be
non-functional: it referenced a CSS parser and VirtualFS bridge that never
existed in the repository, used three incompatible versions of the engine
interface, and produced 700+ compile errors in total. It predated the
CSSLayout engine, which now provides everything its private layout code
attempted. It was removed rather than repaired; format-parsing knowledge
worth keeping (PalmDOC decompression, PDB/EXTH structures, FB2 mappings) will
be ported into the new engines from the git history
(`git log -- UltraCanvas/Plugins/Documents/eBook`).
