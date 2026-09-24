# CLAUDE.md

All assistant guidance for this repository lives in [AGENTS.md](AGENTS.md) —
read that file first.

Quick pointers:

- Module registry: `Masterfile_modules.md`
- Component docs (consult before using any widget): `Docs/UltraCanvas/`
- LLM-ready docs index: `llms.txt` (full corpus: `llms-full.txt`)
- After editing docs, run `python3 scripts/generate_llms_txt.py`
- End every reply that reports work with a `## Delivery` block, a
  `## Next Task` block and an `## Other recommendations` block — see
  *Reporting back* in `AGENTS.md`
- `## Delivery` is not optional when code was written: how much (files and
  `+/-` lines), whether it is committed and pushed, and **whether there is a
  pull request** — its number and state, or the words "no pull request".
  Pushed is not the same as in review, and the reader cannot see the
  difference unless it is said.
