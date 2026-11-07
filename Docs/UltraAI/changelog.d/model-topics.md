- **Model topics: which model is used for what, and what happens to new
  models.** Settings > Services > *Model topics* lists the topics - general
  chat, programming, translation, summaries, pictures and documents, search -
  with the local model each uses on this computer. A topic follows the model
  server's choice (and downloads it when the server changes it, unless that
  is switched off) or keeps a model the user picks. New models the server
  publishes are reported, downloaded when they become a followed topic's
  choice, or ignored, as the user says. The page also takes the model
  server's address and refreshes from it.
- The local runtime reads `topics.json` beside the gallery index (a built-in
  copy otherwise): `LocalRuntime::ListTopics`, `ModelForTopic`,
  `GetTopicPreferences` / `SetTopicPreferences` / `SetTopicModel`,
  `NewModels`, `MarkModelsSeen` and `ApplyTopicUpdates`; the preferences are
  kept in `topic-preferences.json`. The `local` provider takes
  `providerOptions["topic"]` and uses the model the user set for it.
- **The UltraAI model server** (`Server/UltraAIModelServer`): a FastAPI
  service on SQLite that publishes the gallery index, the catalogue and the
  topics every UltraAI client reads, serves the uploaded model files (range
  requests for a resumed download), and has the admin API UltraAI
  ServerAdmin (`Docs/UltraAIServerAdmin/CHANGELOG.md`) works through: models
  from draft to published to withdrawn, files uploaded or by address, topics,
  engines, super and editor accounts, an audit log. A new server starts with
  the framework's built-in gallery, catalogue and topics.
