- **CI compiles the component docs' C++.** `scripts/check_doc_examples.py`
  checked a doc only when someone ran it, so a doc could drift from the
  header it describes and nobody would notice: two of the file dialog
  doc's snippets did, and were mended twice on two branches in one day, in
  ways that then clashed. The new `doc-examples.yml` workflow runs it over
  every component doc (`--all`: `Docs/UltraCanvas/*.md` but the changelog
  and the Proposal / Plan / Investigation design documents, whose code is
  of APIs not written yet) whenever a doc or a public header changes. It
  fails on a finding not in `scripts/doc_examples_baseline.txt`, which
  holds the 337 that predate it - none in the Examples docs - and only
  shrinks (`--update-baseline`). A baseline entry is the doc and the
  message without the line, so editing elsewhere in a doc does not disturb
  it. It runs on ubuntu-24.04 because the baseline holds clang 18's wording.
