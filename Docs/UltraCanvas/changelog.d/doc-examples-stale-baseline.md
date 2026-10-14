- **The doc-examples check keeps its baseline honest and reports an undeclared
  name once.** `scripts/check_doc_examples.py --strict` now fails on a baseline
  entry it no longer finds (and, with `--all`, on one of a doc that is gone), so
  `scripts/doc_examples_baseline.txt` is rewritten in the change that implements
  a proposal's API instead of outliving it. The errors it reports come from a
  pass with clang's spell-checking off (`-fno-spell-checking`): an undeclared
  name is one finding, not the chain of follow-on errors in the terms of
  clang's guess at what was meant, and the finding's wording no longer changes
  when a header gains a type of a similar name. A design document is held to
  its proposed types: a type it names and does not declare is a finding, where
  a component doc's is taken for the reader's own.
