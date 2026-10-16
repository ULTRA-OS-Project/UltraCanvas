- **Every demo-app document opens with an inventory.** The 125 markdown files
  the demo's "Documentation" button opens each carry a `## Contents` list of
  their `##` / `###` sections before the first section, every entry a link
  that jumps to it. `scripts/generate_doc_toc.py` writes and refreshes the
  lists (the documents are found from the `Docs/....md` paths in
  `Apps/DemoApp/*.cpp`), and `llms-txt.yml` fails a pull request whose list
  is stale (`--check`).
- **Markdown section links jump in the text area without `{#id}`.** Only a
  heading carrying an explicit `{#id}` could be the target of a
  `[text](#id)` link, so the GitHub-style links the docs already used (54 in
  the Filer widget's page alone) did nothing in the demo's documentation
  window. Every heading now also answers to its GitHub anchor - lower-cased,
  spaces to `-`, punctuation dropped, `-1`, `-2` for repeats
  (`UltraCanvasTextArea::MarkdownHeadingSlug`); an explicit `{#id}` still
  wins, and `#` lines inside code fences are no longer taken for headings.
- PixelFX's README title was not a heading (`###PixelFX`).
