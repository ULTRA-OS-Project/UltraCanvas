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
- `## Delivery` is not optional when code was written. Run
  `git status --short` before writing it and report what it says:
  1. **anything uncommitted?** — this container is thrown away when the
     session ends, so an uncommitted edit is lost, not pending. Commit it, or
     say in the block that it is uncommitted and will be lost.
  2. **how much, and pushed?** — files and `+/-` lines, branch and SHA, or
     that the commits are still local.
  3. **a pull request?** — its number and state, or the words "no pull
     request". Pushed is not in review, and the reader cannot tell the
     difference unless it is said.
