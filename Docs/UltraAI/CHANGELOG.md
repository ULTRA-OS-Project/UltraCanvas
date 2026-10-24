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
