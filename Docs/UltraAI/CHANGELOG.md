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
- With no provider named, text-to-speech now resolves to `elevenlabs`
  rather than `minimax` when both are built: the default route picks the
  first registered cloud provider by name. Set a default with
  `SetDefaultProvider("texttospeech", ...)` or
  `ULTRAAI_DEFAULT_TEXTTOSPEECH` to choose.

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
