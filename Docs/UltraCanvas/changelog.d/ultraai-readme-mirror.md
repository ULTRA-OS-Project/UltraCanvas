- **`Docs/Modules/UltraAI/README.md` is generated from `UltraAI/README.md`.**
  The two were edited by hand and had drifted apart - one listed the ElevenLabs
  adapter and a different test count. `scripts/generate_llms_txt.py` now
  writes the docs-tree copy from the module's README (relative links
  adjusted), and the llms.txt workflow fails when it is stale, as for
  `llms.txt` itself. Edit `UltraAI/README.md`, then run the script.
