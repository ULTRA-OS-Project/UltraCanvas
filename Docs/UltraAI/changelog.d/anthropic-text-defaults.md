- **The Anthropic text adapter moves to the current API and models.**
  Requests send `anthropic-version: 2026-10-21` (migration guide:
  https://platform.claude.com/docs/en/api/migration-2026-10-21) and default to
  `claude-opus-5-5`; the model list is Claude Opus 5.5, Sonnet 5.5 and
  Haiku 5.5. On that version:
  - `temperature`, `top_p` and `top_k` are a 400 on every model, so the
    adapter no longer sends them - from `ChatRequest::sampling` or from the
    options. Every model samples with its defaults; a caller that set a
    lower temperature (the text-LLM translator's 0.0) gets the default, which
    Claude 4.7 and later models were already limited to - on the old
    `claude-opus-5` default, the translator's 0.0 was a 400 before this.
  - A document directly in an assistant message is a 400: a `FilePart` in an
    assistant message is sent as a user message just before that turn.
  - Forced tool choice is a 400 on Claude Fable 5.1, Mythos 5.1, Opus 5.5 and
    Sonnet 5.5: `ToolChoice::Required` / `Specific` become `auto` with a
    system instruction to call the tool(s) there; older models still get
    `any` / `tool`.
  - Server-side refusal fallbacks are on by default for Claude Fable 5.1,
    Opus 5.5, Opus 5 and Sonnet 5.5 (`"fallbacks": "default"`, beta
    `server-side-fallback-2026-07-01`): a refusal is re-run on another model
    in the same call. A configured `fallbacks` replaces it; token counting
    sends none. The Anthropic vision adapter takes the default from here.
  `ultraai.anthropic_adapter` covers each rule.
