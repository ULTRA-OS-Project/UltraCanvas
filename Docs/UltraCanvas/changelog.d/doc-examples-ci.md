- **CI compiles every doc's C++ against the headers.** `scripts/check_doc_examples.py`
  ran only when someone edited a doc and remembered to, so
  `UltraCanvasFileDialog.md` shipped with two examples that did not compile
  and nobody noticed until the next edit. A new workflow
  (`.github/workflows/doc-examples.yml`) runs it over every doc under
  `Docs/UltraCanvas/` on every change to a doc or a public header, so a
  header change that breaks an example is caught too; the precompiled
  header it builds is cached between runs.
  - The checker gains a per-doc baseline (`scripts/doc_examples_baseline.txt`,
    one `<doc>::<findings>` per line; `--all`, `--strict`, `--no-baseline`,
    `--update-baseline`). 83 of the 215 docs had findings when the check
    was introduced, 682 in all - proposals, the changelog and guides never
    written to compile among them - and are listed, so only a new doc with
    findings, or a listed doc gaining some, fails. A listed doc with fewer
    findings than recorded is reported so its line can be lowered; one with
    none is stale and fails a strict run until it is dropped.
