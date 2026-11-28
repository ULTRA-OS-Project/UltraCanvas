- **Telegram (UltraIM): a failed sign-in step sets the connection state before
  it answers.** The result went to the caller first and the state followed, so
  a caller woken by a failure - a sign-in refused for its proxy - could still
  read the old state. `TelegramProviderTests` caught it once on Windows CI.
