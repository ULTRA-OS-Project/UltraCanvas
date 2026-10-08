- **CI compiles the component docs' C++, and every doc passes.**
  `scripts/check_doc_examples.py` checked a doc only when someone ran it,
  so docs drifted from the headers they describe: 319 findings across 60
  component docs, and two of the file dialog doc's snippets mended twice
  on two branches in one day in ways that then clashed. The new
  `doc-examples.yml` workflow runs it over every component doc (`--all`:
  `Docs/UltraCanvas/*.md` but the changelog and the Proposal / Plan /
  Investigation design documents, whose code is of APIs not written yet)
  whenever a doc or a public header changes, and fails on any finding. It
  runs on ubuntu-24.04 because clang's wording is part of a finding.
  - **The 60 docs are fixed against today's headers**, not silenced:
    renamed and moved APIs (`AddElement` -> `AddChild`, `GetInstance` ->
    `GetCurrent`, `UltraCanvasJSON::Parse` -> `JSON::Parse`, the dialogs'
    `UltraCanvasWindowBase*` parent, the financial chart without its old
    `uid`, `CreateImageFromFile` for an image from a path), listings and
    enum copies made to match their headers, `...` placeholders turned into
    code, member fragments turned into small classes, and the names a
    snippet takes from its application (a window, a path, a callback the
    reader writes) declared in a `<!-- doc-check: ... -->` comment with
    their real types. One example was also wrong at run time: a list
    view's header-click handler captured its own view by `shared_ptr`, so
    the view was never freed; it captures it raw now, as AGENTS.md asks.
  - **The checker reads more C++ the way a compiler does.** A framework
    class's member defined out of line (`void
    UltraCanvasUIElement::Render(...) {`) is compiled in the class's
    namespace; a file-scope macro line without `;`
    (`ULTRACANVAS_DEFINE_ELEMENT_PLUGIN(Init)`) is a definition; a copy of a
    type prefers the framework's (`UltraCanvas::BlendMode`, not
    `PixelFX::BlendMode`) and a listing a top-level namespace
    (`PixelFX::Colour`); prose may name a function of any framework header
    - backend, platform or dialog - not only the public ones;
    `auto x = UltraCanvas::CreateX(...)` is typed; and a doc-check comment
    may define a macro the application's build provides.
  - `--all`, `--strict`, `--baseline` and `--update-baseline` are new.
    `scripts/doc_examples_baseline.txt` holds findings that predate the
    check, and is empty.
