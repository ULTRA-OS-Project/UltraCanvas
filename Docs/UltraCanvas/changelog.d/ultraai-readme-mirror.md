- **The module READMEs in `Docs/Modules/` are generated from the modules'
  own.** `Docs/Modules/UltraAI/README.md`, `Docs/Modules/UltraNet/README.md`
  and `Docs/Modules/VirtualFS/README.md` were hand-kept copies of
  `UltraAI/README.md`, `UltraNet/README.md` and `VirtualFS/README.md` and
  had drifted apart: the UltraAI pair disagreed on the adapters and the test
  count, the UltraNet module copy was two weeks behind the docs copy, and
  the VirtualFS copies each had a section the other lacked. The pairs are
  merged, `scripts/generate_llms_txt.py` writes the docs-tree copy from the
  module's README (relative links adjusted), and the llms.txt workflow fails
  when a mirror is stale, as for `llms.txt` itself. Edit `<Name>/README.md`,
  then run the script.
