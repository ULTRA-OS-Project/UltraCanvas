- **The llama.cpp adapter is built and tested in CI, on a generated model.**
  The adapter is opt-in (it vendors the engine), so the build rows never
  compiled it and its test skipped its inference checks without a model file
  in `ULTRAAI_TEST_LLAMA_MODEL`; a change to it was compile-checked by hand
  at best. Now `UltraAI/tests/make_tiny_llama_gguf.py` writes the fixture at
  build time with the gguf-py of the llama.cpp checkout the adapter is built
  from: a random one-layer llama with a byte-fallback vocabulary and Gemma's
  chat template, 115 KB, with its output rows arranged so greedy decoding
  never picks end-of-sequence. `ctest` points the test at it (when Python
  with numpy is available; otherwise the test still skips and says so), and
  the new `ultraai-llamacpp.yml` workflow builds the adapter on every change
  under `UltraAI/` and fails unless the inference checks ran. The test also
  checks the same-role turn merge end to end: a history with two user turns
  prompts with the same token count and the same greedy reply as the merged
  turn.
