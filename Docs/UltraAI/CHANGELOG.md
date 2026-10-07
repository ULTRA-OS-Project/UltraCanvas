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
