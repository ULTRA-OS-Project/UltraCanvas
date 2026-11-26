# Modules

UltraCanvas is the UI framework; around it sit **sibling modules** with no UI
of their own. Each is the *single* place its job is done in the stack - no
application rolls its own HTTP client, crypto or archive reader - and each is
wrapped behind an UltraCanvas-owned API so its backing library can be
swapped. They can be used from a console program as well as from an
UltraCanvas application.

The authoritative registry - purpose and full public function surface of
every module - is [`Masterfile_modules.md`](../../Masterfile_modules.md).
Read it before adding cross-module code.

| Module | What it does | Docs |
|---|---|---|
| **UltraCanvas** | the UI framework: elements, layout, events, rendering, services | this wiki |
| **FileLoader** | universal load / save / convert facade for every file a user touches | [README](../Modules/FileLoader/README.md) |
| **File-type plug-ins** | charts, diagrams, vector, documents, video, models, LaTeX, codes (`UltraCanvas/Plugins/`) | [Element plug-ins](../UltraCanvas/UltraCanvasElementPlugins.md) |
| **UltraNet** | networking: HTTP(S), WebSocket, FTP, TCP / UDP, TLS (verification on by default), DNS; blocking calls return `UltraNetResult` | [README](../Modules/UltraNet/README.md) |
| **UltraAI** | provider-agnostic AI: LLM chat, embeddings, speech-to-text, text-to-speech, vision, image / video / music generation, translation, code assist | [README](../Modules/UltraAI/README.md) |
| **UltraDatabase** | database access through named connections; bundled SQLite, other engines by configuration; parameter binding only | [README](../Modules/UltraDatabase/README.md) |
| **UltraCrypt** | hashing, MACs, authenticated encryption, signatures, key derivation, secure random | [README](../Modules/UltraCrypt/README.md) |
| **UltraVault** | the system's home for API keys, tokens and passphrases | [README](../Modules/UltraVault/README.md) |
| **VirtualFS** | archives, format detection, decompression, entry caching, passwords, RAM discs | [README](../Modules/VirtualFS/README.md) |
| **UltraCloud** | cloud accounts and "upload this and give me a share link", providers as plug-ins | [README](../Modules/UltraCloud/README.md) |
| **VideoFX** | video probing, frames, timelines of trimmed / filtered / joined segments, encoding (on FFmpeg) | [README](../Modules/VideoFX/README.md) |
| **AudioFX** | audio processing | [README](../Modules/AudioFX/README.md) |
| **PixelFX** | bitmap processing and analysis | [README](../Modules/PixelFX/README.md) |
| **OCR** | text recognition in images and document pages | [README](../Modules/OCR/README.md) |
| **Vectorizer** | raster images to vector drawings | [README](../Modules/Vectorizer/README.md) |
| **QRCode** | QR code generation and reading | [README](../Modules/QRCode/README.md) |
| **PDF** | PDF reading and rendering | [README](../Modules/PDF/README.md) |
| **UltraMessage** | messages between applications and from the platform; notifications | [README](../Modules/UltraMessage/README.md) |
| **UltraIM** | instant messaging across networks (providers as plug-ins), calls | [README](../Modules/UltraIM/README.md) |
| **UltraRTC** | WebRTC peer connections, data channels, audio and video calls | [API status](../Modules/UltraRTC/ApiStatus.md) |
| **UltraLocalisation** | `Tr()` and the translation catalogues | [README](../Modules/UltraLocalisation/README.md) |
| **UltraSite** | static websites generated from routes | [README](../Modules/UltraSite/README.md) |
| **UltraScript** | the scripting language for automating UltraCanvas applications | [`Masterfile_modules.md`](../../Masterfile_modules.md) |
| **NetworkMonitor** | the system's socket table with the owning process | [docs](../Modules/NetworkMonitor/) |
| **IODeviceManager** | printers, cameras, scanners | [Architecture](../Modules/IODeviceManager/Architecture.md) |
| **WasmHost** | running WebAssembly modules (WASI) inside an application | [`Masterfile_modules.md`](../../Masterfile_modules.md) |
| **UltraWin**, **UltraAndroid** | Windows and Android applications as native windows on Linux / ULTRA OS | [UltraWin](../Modules/UltraWin/) |
| **Smart Home** | device integration and its widgets | [README](../Modules/Smarthome/README.md), [widgets](Groups/Module-widgets-Smart-Home.md) |

## Rules every module follows

- **Wrapped engines**: no third-party type in a public header; applications
  never call vendored libraries directly. A new dependency is recorded in
  [`Docs/Dependencies.md`](../Dependencies.md), `master_dependencies.yaml` and
  `THIRD_PARTY_LICENSES.md`.
- **Results, not exceptions**: blocking operations return a result type
  (`UltraNetResult`, `UltraDbResult` ...), handles are opaque
  (`UltraNetHandle`, `UltraDbHandle` ...).
- **Secure defaults**: TLS verification on, SQL through parameter binding,
  secrets only in UltraVault.
- **UI-free**: a module's UI, where it has one, is a separate library built
  from UltraCanvas elements.
