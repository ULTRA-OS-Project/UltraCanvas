- **The OpenAI adapter merges consecutive same-role turns.** Two `user`
  messages in a row (or two `assistant` ones) now reach the wire as one
  message, texts joined with a blank line and parts, tool calls and names
  kept in order; tool results and turns from different named speakers stay
  apart. OpenAI accepts either shape, but an OpenAI-compatible server that
  hands the history to the model's chat template (LocalAI, llama.cpp server,
  vLLM with Gemma, Llama 2 or Mistral) refuses two in a row with
  *"Conversation roles must alternate user/assistant/user/assistant"* - the
  HTTP 500 the dashboard's Chat hit through LiteLLM. The dashboard no longer
  produces such a history since 0.1.20, and now no other UltraAI client can
  trip it either.
