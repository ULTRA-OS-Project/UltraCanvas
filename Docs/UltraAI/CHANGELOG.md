#### 2026-11-12 *0.1.27*
- **The model server's lists are signed, and the client checks them.** The
  model server signs `index.json`, `catalogue.json` and `topics.json` with its
  Ed25519 key (the signature in the `X-UltraAI-Signature` header and as
  `<document>.sig`) and publishes its keys in `keys.json`. The local runtime
  pins those keys on the first refresh from a server
  (`LocalRuntimeSettings::trustedKeys`) and from then on refuses a refresh
  whose index, catalogue or topics are not signed by a pinned key - nothing
  is replaced, and the error's `providerCode` is `"bad-signature"`. A new
  model server address drops the old pins; a gallery that publishes no keys
  (the built-in one, a plain mirror) stays unsigned. Settings > Model topics
  says whether the lists in use are signed and by which key.
- The settings window's pages - Default providers, Local models, Model
  topics, Local & cloud, Endpoints - go through `Tr`, so they show in the
  desktop's language once `media/locale/<lang>/UltraAI.json` has them.

#### 2026-11-12 *0.1.26*
- **Consecutive same-role turns are merged before a chat template sees
  them.** Two `user` messages in a row (or two `assistant` ones) now reach
  the wire as one message, texts joined with a blank line and parts, tool
  calls and names kept in order; tool results and turns from different named
  speakers stay apart. OpenAI accepts either shape, but an OpenAI-compatible
  server that hands the history to the model's chat template (LocalAI,
  llama.cpp server, vLLM with Gemma, Llama 2 or Mistral) refuses two in a
  row with *"Conversation roles must alternate user/assistant/user/assistant"*
  - the HTTP 500 the dashboard's Chat hit through LiteLLM. The merge is
  `MergeSameRoleTurns` in the shared adapter library
  (`UltraAIMessageHistory.h`): the OpenAI adapter applies it, so the Qwen and
  `local` providers that delegate to it are covered, and the in-process
  llama.cpp adapter applies it before `llama_chat_apply_template`. The
  dashboard no longer produces such a history since 0.1.20, and now no other
  UltraAI client can trip it either.

#### 2026-11-11 *0.1.25*
- **Settings > Services > Proxy.** The cloud providers are reached through
  the proxy every ULTRA OS application shares - the system's until another
  is chosen - and the page opens the Proxy settings dialog to choose one; a
  local model on this machine is never reached through it.

#### 2026-11-10 *0.1.24*
- **Speech to text for UltraCanvas applications.**
  `RegisterUltraAISpeechTranscription()` (`UltraAISpeechTranscription.h`, in
  `UltraAI_UI`) makes UltraAI `UltraCanvasFileLoader`'s speech transcriber:
  the words spoken in a sound file or a video come from the first
  speech-to-text provider on the default route (chosen, then local, a cloud
  one only when chosen or allowed, never `mock`), chosen again at every file.
  A sound file of a type the provider takes, within its size limit, goes as
  it is; every video, another type or a file over the limit has its sound
  track taken out with VideoFX first, as a mono 16 kHz MP3 at 64 kbit/s
  (about 50 minutes in 25 MB). Without a provider the transcriber installed
  before it transcribes. `FindSpeechProvider` and
  `LastSpeechTranscriptionProvider` say which. `UltraAI_UI` links VideoFX
  where it is in the build. `UltraAISpeechTranscriptionTest` covers it with a
  generated WAV, the MP3 conversion, and a video where the ffmpeg command
  line is installed.
- **OpenAI speech to text.** `CreateOpenAISpeechToText` (`UltraAIOpenAI.h`)
  transcribes over the audio API: a multipart upload to
  `/v1/audio/transcriptions` (or `/v1/audio/translations` with `whisper-1`),
  `gpt-4o-transcribe` by default, `whisper-1` with segment and word
  timestamps (`verbose_json`), the language as its primary subtag, options as
  form fields, 25 MB refused before upload, errors mapped. Without a key in
  the config it reads the UltraVault entry `ai.openai.api_key`. Against a
  custom `baseUrl` - a self-hosted OpenAI-compatible whisper server - it runs
  keyless with `whisper-1` by default. Live transcription is not supported.
  Registered as `"openai"` for `"speechtotext"`, a cloud provider used once
  chosen (`ULTRAAI_DEFAULT_SPEECHTOTEXT=openai`). `ultraai.openai_speech`
  tests it offline.

#### 2026-11-10 *0.1.23*
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
- **Claude reads text: the Anthropic vision adapter.**
  `CreateAnthropicVisionAnalyzer` (`UltraAIAnthropicVisionAnalyzer.h`, in
  `UltraAI_AdapterAnthropic`) is an `IVisionAnalyzer` for `VisionTask::Ocr`
  over the Messages API, built on the Anthropic text adapter (its
  serialization, key resolution, error mapping and transport). The picture
  goes as a base64 image block with an instruction to transcribe every word
  in reading order - lines, paragraphs, table rows by tabs, nothing
  translated or corrected - and the transcription comes back as structured
  output (`{"text": ...}`), so no commentary is mixed in. Defaults:
  `claude-opus-5-5`, effort `low`, server-side refusal fallbacks
  (`"fallbacks": "default"`, beta `server-side-fallback-2026-07-01`); a
  configured `output_config` or `fallbacks` replaces them. A refusal is
  `ContentFiltered`, a cut-off transcription an error, other tasks
  `InvalidRequest`. Without a key or a vault reference in the config it
  reads the UltraVault entry `ai.anthropic.api_key`, so the default route can
  build it. Registered as `"anthropic"` for `"visionanalyzer"`: a cloud
  provider, used only once chosen. With `RegisterUltraAITextRecognition` it
  reads pictures and scanned pages for `UltraCanvasFileLoader::ExtractText`
  (UltraClipboard's *Extract text*); pictures over 3.75 MB of PNG are halved
  there until they fit the API's image limit. `ultraai.anthropic_vision`
  tests it offline.
- **UltraAI reads the words in pictures for UltraCanvas applications.**
  `RegisterUltraAITextRecognition()` (`UltraAITextRecognition.h`, in
  `UltraAI_UI`) installs UltraAI in front of `UltraCanvasFileLoader`'s text
  recognizer: when a vision provider on the default route declares
  `VisionTask::Ocr` - the one chosen for `"visionanalyzer"`, then the ones on
  this computer, a cloud one only when chosen or allowed, never `mock` - it
  reads pictures and scanned PDF pages (a PNG, with the model that declares
  OCR; the OCR text, else the spans, else the document regions in reading
  order). Without one, or when it fails, the recognizer installed before it
  (the OCR plugin's Tesseract) reads the picture. The provider is chosen at
  every picture, so one set up while the application runs is used from the
  next. `FindOcrProvider` and `LastTextRecognitionProvider` say which.
  `UltraAI_UI` now links `UltraAI_Core`. No adapter but the mock implements
  `IVisionAnalyzer` yet, so the built-in OCR keeps reading until one does.
  `UltraAITextRecognitionTest` covers the choice with stand-in providers.

#### 2026-11-10 *0.1.22*
- **The Agents tile.** The dashboard has an *Agents* tile (and the
  `--agents` start option). Give a model a task and it works through it
  with the ticked tools: a clock, a calculator, text statistics and
  random numbers, none of which touch files or programs. The transcript
  shows its text as it arrives, every tool call with its arguments and
  each result. *Ask before a tool runs* puts each call to you first,
  *Stop* ends the task, and the conversation carries over between tasks
  until *New conversation*. The instructions (the system prompt) and the
  most model calls per task can be set, and the status line counts the
  model calls, tool calls and tokens.
- The dashboard lays its tiles out six to a row.
- **Agents: a model that uses tools until the task is done**
  (`UltraAIAgent.h`, part of `UltraAI_Core`). `UltraAI::Agent` runs the
  model ↔ tool loop over any `ITextLLM` (anthropic, openai, qwen, local,
  llama-cpp, mock). An application registers C++ functions as tools
  (`AddTool`; `ToolArguments` reads the arguments the model wrote,
  `ToolResult::Ok` / `Fail` answers) and calls `Run`. The agent calls the
  model, runs the tools it asks for and feeds the results back until it
  answers. Each tool call is offered to `AgentOptions::canUseTool` (allow,
  deny with a reason the model reads, or stop the run), and each result
  passes `afterToolCall`. Progress events (turns, streamed text with
  `stream`, tool calls and results) all arrive on the run's thread. Also:
  `maxTurns`, `Cancel()` from any thread, `RunAsync`, a conversation that
  carries over between runs (`History` / `SetHistory` / `ClearHistory`;
  a run that fails before the model answered leaves it unchanged),
  `CreateAgent` from a provider config, and subagents
  (`MakeSubagentTool`). Every tool call gets a result in the history
  (denied, unknown, throwing and cancelled ones too), so the conversation
  stays one a provider accepts. `Docs/Modules/UltraAI/Agents.md`; tests in
  `UltraAI/tests/test_agent.cpp`.
- The text-LLM translator's private JSON reader moved to
  `UltraAI/src/UltraAIMiniJson.h`, shared with the agent.
- **Batches: many chat requests as one job, at about half the price**
  (`UltraAIBatch.h`, part of `UltraAI_Core`). `ITextBatch` submits a list
  of `ChatRequest`s with a custom id each (`Submit`), reports the job
  (`Get`: state and counts), cancels it (`Cancel`) and returns one
  `ChatResponse` per request when it is finished (`Results`).
  `RunBatch` does all four: submit, poll with a growing interval, report
  progress, honour a cancel flag and a timeout, collect.
  `CreateTextBatch` gives Anthropic's Message Batches
  (`CreateAnthropicTextBatch`) and OpenAI's Batch API
  (`CreateOpenAITextBatch`: a JSONL file upload, then a batch from it).
  Every other provider, and an OpenAI-compatible server, gets a fallback
  that sends the requests one at a time on a thread of its own
  (`CreateSequentialTextBatch`, optionally several at once), so an
  application uses one API everywhere. `IsNative()` says which kind it
  got. `Docs/Modules/UltraAI/Batches.md`; tests in
  `UltraAI/tests/test_batch.cpp`.
- The `anthropic` adapter sends the results of one turn's tool calls
  together, in one user message, instead of one user message each.
- **Chat is a playground now.** A settings panel above the conversation
  (*Hide settings* / *Show settings*) chooses the model: the endpoint's
  default, a model its server lists, or any model id typed in. It also
  sets the system prompt, the temperature, top p and the most output
  tokens, and declares tools as JSON (`[{"name", "description",
  "parameters"}]`). An out-of-range number or broken tools JSON is
  reported before anything is sent. When the model calls a tool, the call
  and its arguments are shown and the next message goes back as the
  tool's result. The status line reads out the model and the tokens of
  the last reply and of the whole chat.

#### 2026-11-07 *0.1.21*
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

#### 2026-10-31 *0.1.20*
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

#### 2026-10-21 *0.1.19*
- **No enumerator is named `None` any more.** X11's `None` macro rewrote
  `ErrorCode::None`, `ToolChoice::None` and `ControlType::None` in any file
  that included a UltraCanvas UI header before a UltraAI one, so every such
  file worked around it with include order and `UltraAICommon.h` carried an
  `#undef`. They are `ErrorCode::Ok`, `ToolChoice::NoTool` and
  `ControlType::NoControl`; the `#undef` and the include-order comments are
  gone. `Error::IsOk()` is unchanged.
- **The built-in catalogue's files were checked against Hugging Face.** The
  six models' file names match their repositories; Qwen3 1.7B (Q8_0) is
  2.17 GB (the catalogue said 1.83) and Qwen3 8B (Q4_K_M) carries its exact
  size, so the fit and the download figures are right. The hashes are still
  read from the tree API at install time.

#### 2026-10-21 *0.1.18*
- **The "Get a local model" dialog is the module's, for every application.**
  It moved from the dashboard into `UltraAI/ui/` (`UltraAILocalModelDialog.h`,
  library `UltraAI_UI`, built in-tree with the UltraCanvas core), so UltraTexter's
  new Edit > Translate Selection... asks for a model the same way the
  dashboard's Chat does. `UltraAILocalModelDialog::NeedsInstall(error)` is
  the test an application makes on a chat, embedding or translation error
  before showing it.

#### 2026-10-21 *0.1.17*
- **Local models in the dashboard.** Settings > Services has a *Local models*
  page: what this computer is (memory, cores, GPU) and which acceleration
  the engine will use, with the choice to override it; the llama-server
  engine's state with Install / Remove; a table of the catalogue's models and
  the user's own with service, size, fit (GPU / CPU / too large) and state
  (installed, running, not installed), and Install, Remove, Stop and Forget
  for the selected one; the progress of the install in flight, with Cancel;
  *Add model file...* (a GGUF of your own, used in place), *Add from Hugging
  Face...* (organisation/repository:file), *Refresh catalogue* and *Open
  folder*; the download policy (ask, automatic, never) and the idle time
  after which an unused engine stops; disk use. Restore defaults puts the
  policy back to Ask, the accelerator to Auto and the idle time to 15 min.
- **A service that needs a model asks for it.** The first time Chat is sent
  to a `local` endpoint whose model is not installed, the *Get a local model*
  dialog opens: the models that fit this computer with the recommended one
  preselected, size, licence and what else is downloaded (the engine), a
  checkbox to download future models without asking, and *Download and
  continue*, which shows the download's progress and then sends the message
  that was waiting. *Not now* leaves everything as it was.
- **The endpoint editor knows the `local` provider.** With it picked, *Fetch
  models* lists the local runtime's models (installed or not) instead of
  asking a server, and the hint under the provider says so.
- The settings pages' shared parts (the type scale, the page skeleton, the
  control factories) moved into `UltraAISettingsWidgets.h` so the new page is
  built from the same ones as the rest of the window.

#### 2026-10-20 *0.1.16*
- **Models run on this computer with nothing to install.** The new local
  runtime (`UltraAILocalRuntime.h`, `UltraAI/runtime/`) downloads an inference
  engine (llama.cpp's `llama-server`, from a gallery of prebuilt bundles per
  platform and accelerator, verified by SHA-256) and a model (from a curated
  catalogue, or a GGUF the user has, or a Hugging Face file) on demand,
  starts the engine on a loopback port with a per-installation key, shares
  it between every UltraCanvas application through a record under a lock
  file, and stops it when idle. The new `local` provider serves chat and
  embeddings through it and ranks first in the local-first routing, so an
  empty provider id reaches a model on this machine whenever one is
  installed or can be. The hardware inventory decides the bundle (Metal on
  macOS, Vulkan beside a GPU on Linux and Windows, CPU otherwise) and marks
  each model *recommended*, *runs on CPU* or *too large*. Under the default
  *Ask* policy a first call answers with `ModelNotFound` / `needs-install`
  for the application to ask the user; `allow_download` or the *Automatic*
  policy lets it install and answer. Doc: `Docs/Modules/UltraAI/LocalRuntime.md`;
  design: `Docs/Research/UltraAILocalRuntimeDesignProposal.md` (phase 1 of
  its plan; the dashboard page, the framework dialog, speech and images
  follow). Tested against a fake engine over real loopback HTTP
  (`UltraAI/tests/test_local_runtime.cpp`), and the module's whole suite now
  runs in CI (`ULTRACANVAS_BUILD_ULTRAAI_TESTS`).

#### 2026-10-16 *0.1.15*
- **The dashboard draws its own icons.** The ten service buttons and the
  Settings button were labelled with emoji, which show as empty boxes where
  the system font has no emoji glyphs. Each service is now a tile with a
  128 x 128 icon over its name (and a tooltip saying what it does); Settings
  carries a gear. The set lives in `media/icons/ultraai/`: one SVG per icon
  (rounded gradient tile, white glyph), the 128 px PNG rendered from it that
  the app loads, and `generate_icons.py`, which writes the SVGs. The window
  grows to 1000 x 468 to fit the tiles.

#### 2026-10-13 *0.1.14*
- **The endpoint settings are found in a profile named in any script.**
  `endpoints.json`'s folder was built by hand from `HOME` and
  `XDG_CONFIG_HOME` read through the narrow `getenv` on macOS and Linux; it
  comes from the framework's settings root now
  (`UltraCanvasSettingsFolderRoot`, every variable read as UTF-8) and lands in
  the same folder as before on every platform (framework changelog:
  `settings-root-and-xdg-everywhere`).

#### 2026-10-12 *0.1.13*
- **One UltraAI at a time.** Starting UltraAI while it is already running brings
  the open window forward (restored if minimised; an open dialog comes forward
  with it) instead of opening a second copy. Two starts at the same moment
  cannot both stay, and a copy that does not answer is reported with
  *"UltraAI is already running"*. Command-line modes are unaffected.
  Framework: `UltraCanvasSingleInstance` (framework changelog: `instance-lock`).

#### 2026-10-09 *0.1.12*
- **New dashboard icon.** The face is drawn larger and without the light blue
  disc behind it (`media/appicon/UltraAI.svg`, the uploaded artwork).
  `media/appicon/UltraAI.png` is re-rendered from it at 256 px through
  librsvg; the drawing is taller than wide (229 x 303 units), so it is padded
  to a square on a transparent background. The window and taskbar icon, the
  Windows `.exe` icon and the `hicolor` theme icons are all made from this
  pair, so every one of them changes with it.

#### 2026-10-07 *0.1.11*
- **The Translation dialog takes the source language and the formality.**
  It sent only the texts and a target language, so the register and a known
  source language could not be tried from the dashboard. The target field now
  shares a row with a source-language field (empty: the provider detects it)
  and a Default / Formal / Informal picker, and the run happens off the UI
  thread like the other dialogs, since a chat model behind the translator
  takes seconds per batch where the mock answered at once.
- **The Translation dialog names the model.** The provider row carries a
  model field, as the image, video and speech dialogs do: the translator
  providers translate through their chat models, and until now only the
  provider's default model could be used.

#### 2026-10-07 *0.1.10*
- **Translation works through every chat model.** `ITranslator` had only the
  mock behind it; the module now serves it with any `ITextLLM`
  (`UltraAITextLLMTranslator.h`): the texts go to the model as numbered JSON
  segments with the register, domain, glossary and markup rules in the
  system prompt, and the reply is asked for as JSON in the same numbering,
  so batches come back in order. Every text-LLM provider but the mock is a
  translator provider under its own id — `anthropic`, `openai`, `qwen`,
  `llama-cpp` — so the dashboard's Translation dialog lists them, and an
  empty provider follows the routing policy, local LLMs first
  (`KnownLocalProviders("translator")`). `DetectLanguage` goes the same way.
  Options: `textllm.provider`, `textllm.batchSize`, `textllm.temperature`,
  `textllm.maxOutputTokens`, and a per-request `textllm.glossary`. Doc:
  `Docs/Modules/UltraAI/Adapters.md`; test: `test_textllm_translator`.

#### 2026-10-05 *0.1.9*
- **The dashboard keeps its settings and endpoints in a Windows profile
  named in any script.** Its configuration folder was read with the narrow
  `getenv("APPDATA")`, which answers in the ANSI code page, and handed to the
  JSON file helpers, which open it as UTF-8; for a user name outside the code
  page `endpoints.json` and `config.ini` were neither saved nor read back. The
  folder is read with the framework's `GetEnvUtf8` now and every file in it
  opened as UTF-8 (framework changelog: `check_path_string` sees what a
  header declares, and `env-narrow`).

#### 2026-10-04 *0.1.8*
- **The chat dialog sends on Enter.** Its message box only sent through the
  Send button; now Enter sends and Shift+Enter starts a new line, through the
  text area's `onBeforeKeyDown` hook (framework,
  `Docs/UltraCanvas/changelog.d/textarea-before-keydown.md`). The placeholder
  says so. While a reply is still on its way Enter does nothing, as Send.

#### 2026-09-29 *0.1.7*
- **Ctrl-C and SIGTERM exit in order.** The signal handler called
  `RequestExit()` (which logs and runs a callback) and then `std::exit`,
  running the static destructors under live threads. It now makes the one
  call a handler may, `UltraCanvasApplicationBase::RequestExitFromSignal()`,
  and the main loop turns it into the same shutdown as a closed window.

#### 2026-09-28 *0.1.6*
- **The version is in the window title** — `UltraAI 0.1.6` — so a screenshot or a
  bug report says which build it came from. The number is this changelog's
  first line, as everywhere else (`cmake/UltraCanvasVersion.cmake`).

#### 2026-09-24 *0.1.5*
- **The dashboard app reports this changelog's version.** `UltraAIApp`'s
  `ULTRAAI_APP_VERSION` was a literal `"0.1.0"` in `CMakeLists.txt`, and
  `--version` printed its own literal `0.1.0`; both now come from the first
  line of this file through `cmake/UltraCanvasVersion.cmake`, as for the
  other applications. `Apps/UltraAIApp/main.cpp` fails at compile time if
  the definition is missing instead of falling back to a number.

#### 2026-09-24 *0.1.4*
- **ElevenLabs text-to-speech.** A new `elevenlabs` adapter
  (`UltraAI/adapters/elevenlabs/`, `ULTRAAI_ADAPTER_ELEVENLABS`, on by
  default) implements `ITextToSpeech`: one-shot synthesis, streamed
  synthesis, voice listing filtered by language, and instant voice cloning.
  It serves both ways of offering the service: pointed at a hosted relay
  with the user's session token as a bearer credential
  (`providerOptions["auth_scheme"] = "bearer"`), or straight at ElevenLabs
  with the user's own key from `ai.elevenlabs.api_key`. Responses report the
  character cost ElevenLabs bills, which is what a relay meters. See
  `Docs/Modules/UltraAI/Adapters.md`.
- **The transport seam streams raw response bodies.** `ITransport::ByteStream`
  hands a chunked body over as it arrives, for bodies that are not
  Server-Sent Events (newline-delimited JSON, streamed audio).
  `UltraNetTransport` backs it with UltraNet's `onDataChunk`; every other
  transport — `ScriptedTransport`, `RecordingTransport`, cassette replay —
  gets a default that delivers the whole body as one chunk.
- **Local AI is the default.** With no provider named, a service now
  routes to a provider that runs on this machine (llama.cpp, an Ollama /
  vLLM server, ComfyUI, ...) and to the in-process test double where there
  is none — never to a cloud service. Until now the default route fell back
  to the first registered cloud provider by name, so text-to-speech would
  have gone to `elevenlabs`, a paid service, without anyone choosing it.
  A cloud provider is used when it is named: as `providerId`, with
  `SetDefaultProvider`, or with `ULTRAAI_DEFAULT_<CAPABILITY>`. The old
  fallback is still there as an opt-in — `SetCloudFallbackAllowed(true)` or
  `ULTRAAI_ALLOW_CLOUD_FALLBACK=1` — and `IsLocalProvider()` tells the two
  kinds apart (`UltraAIRouting.h`).
- **The dashboard has a settings window like UltraFiler's.** "⚙ Settings"
  now opens a window with a page tree on the left and the page on the
  right: *Services > Default providers* picks the provider each service's
  "(default route)" uses — "Automatic — local first" unless one is chosen,
  with what Automatic resolves to right now shown beside it — *Services >
  Local & cloud* turns cloud fallback on (off by default), and *Accounts >
  Endpoints* opens the endpoint editor, which is titled "UltraAI —
  Endpoints" now. Every change applies at once and is saved to
  `config.ini` beside `endpoints.json` in the UltraAI config directory; the
  app applies it at startup.

#### 2026-09-22 *0.1.3*
- **The dashboard icon is back.** `media/appicon/UltraAI.svg` had been
  overwritten by an empty Xara page — a blank A4 canvas with nothing on the
  one layer — so the scalable half of the pair drew nothing at all, while
  `media/appicon/UltraAI.png` still held the artwork from 0.1.2. Nothing in
  the window would have changed, because that reads the PNG; what breaks is
  the desktop lookup, which prefers `share/icons/hicolor/scalable/apps` where
  the install rules put the SVG, so the application menu would have shown an
  empty tile. The SVG is restored, and it renders pixel-identical to the
  shipped PNG again.

#### 2026-09-21 *0.1.2*
- **The dashboard app has an icon.** `media/appicon/UltraAI.svg` is the
  uploaded artwork; `media/appicon/UltraAI.png` is its 256 px render and
  what the window and taskbar icon (`main.cpp` hands it to
  `SetDefaultWindowIcon`) and the Windows `.exe` icon are made from, as for
  the other applications. The app now takes the build's asset copy as a
  dependency, so the icon is in its resources dir. A freedesktop entry
  (`Apps/UltraAIApp/UltraAI.desktop`) puts UltraAI in the application menu;
  the install rules place it, the binary and both icon files where the
  desktop looks for them.

#### 2026-08-31 *0.1.1*
- **UltraAI keeps its own changelog from here.** Everything up to and including
  this version shipped as part of a framework release and is recorded in
  [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) — nothing was
  rewritten or moved, so that history stays where it was published. From now on
  a change to the UltraAI module and its dashboard app (`UltraAI/`,
  `Apps/UltraAIApp`) is described here and carries this file's version, and
  UltraAI no longer moves when the framework releases.
- A framework change UltraAI needs still belongs in the framework changelog.
  Cross-reference it from here when a release depends on it; never describe one
  change in two files under two version numbers.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
