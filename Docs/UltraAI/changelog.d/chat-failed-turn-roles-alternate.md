- **A failed chat request no longer poisons the next one.** The dashboard's
  Chat kept the user message of a request that failed before the model
  answered (a provider that was down, a wrong endpoint, a local model not yet
  installed), so the next send carried two consecutive `user` turns - and a
  server whose chat template insists on alternating roles (LiteLLM in front of
  LocalAI running Gemma) refused the whole conversation with HTTP 500
  *"Conversation roles must alternate user/assistant/user/assistant"*. Switching
  to a working endpoint and resending therefore failed too, while the same
  question sent to that endpoint first worked. An unanswered user turn is now
  dropped from the history when the stream ends in an error with no reply, and
  a send always replaces a trailing unanswered user turn instead of appending
  after it; the transcript still shows the attempt and its error, and a
  partial reply is kept as the assistant's turn.
