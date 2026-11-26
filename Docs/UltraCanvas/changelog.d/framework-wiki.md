- **A wiki for the whole framework: `Docs/Wiki/`.** One place that starts at
  the beginning - an introduction, a feature overview, getting started, and
  guide pages on the core concepts, layout, events and callbacks, styling and
  themes, rendering (with a custom element), files / text / localisation and
  the sibling modules - and goes down to a reference page for every element:
  what it is for, its header and include, base classes and derived elements,
  factories and constructors, **every callback** with its signature and what
  it reports, every setter, query and public field, and the option structs
  and enums of its header with their defaults. Group pages follow the element
  catalogue's sections; an element index and a callback index list
  everything A-Z (202 elements, 836 callbacks).
  - The reference is generated from the headers by the new
    `scripts/generate_wiki.py` (grouping and purpose from
    `UltraCanvasUIElements.md`), so it lists exactly what the code declares;
    `--check` reports when it is stale. The guide pages are written by hand
    and their C++ compiles with `scripts/check_doc_examples.py`.
