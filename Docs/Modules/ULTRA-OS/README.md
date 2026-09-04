# ULTRA OS

ULTRA OS intends to create an open source, user friendly and well structured OS
based on Linux for desktop and on Android for mobile devices. ULTRA OS will be
designed for the consumer as well as the professional.

One universal UI layer, **UltraCanvas**, renders every app across Linux, Windows,
and macOS from a single codebase. A complete suite of modules handles files,
images, audio, video, networking, devices, and the smart home. Professional-grade
engines like **PixelFX**, **AudioFX**, and **VideoFX** put serious creative power
in every developer's hands. Designed to run on affordable hardware, from ARM and
RISC-V to x86. Free, open-source, and community-funded. The **UltraAI** module is
set up to offer local AI access at low cost, as well as connections to online AI
for professional-grade performance.

Besides the technical implementation ULTRA OS focuses also on the ecosystem to
create a competition to the current mainstream OS that dominate the market. The
intention is to create new marketplaces for all.

## The modules

Every entry under this category is a module of the ULTRA OS stack. Select one
for its documentation, its architecture diagram and — where the demo ships a
live example — a working screen built on it.

| Module | What it does |
|---|---|
| **AudioFX** | Audio effects, analysis and decoding. |
| **FileLoader** | One universal load / save / convert facade for every file type. |
| **IODeviceManager** | Scanners, cameras and printers behind one device API. |
| **PixelFX** | Image effects and processing. |
| **Smart Home** | Devices, rooms and scenes for the connected home. |
| **UltraAI** | Provider-agnostic AI: chat, vision, speech, translation, image / video / music generation. |
| **UltraCloud** | Cloud accounts, uploads and share links (Nextcloud / ownCloud, WebDAV, Dropbox, OneDrive, Google Drive). |
| **UltraCrypt** | Hashing, HMAC, authenticated encryption, key derivation and secure random — the one place crypto is implemented. |
| **UltraDatabase** | Named database connections, parameterized queries, transactions and migrations. |
| **UltraNet** | Networking: HTTP, WebSocket, FTP, TCP/UDP, TLS, DNS. |
| **UltraVault** | Credential and secret storage for API keys, tokens and passphrases. |
| **UltraWin** | Windows applications on Linux / ULTRA OS, as single native windows. |
| **VideoFX** | Video effects and transcoding. |
| **VirtualFS** | Archives and compression browsed as ordinary folders. |

The full registry — the purpose and public function surface of every module —
is `Masterfile_modules.md`; the third-party library each one pulls in is listed
under **Dependencies & Third Party** in this demo.

![ULTRA OS](media/diagrams/ULTRA-OS.svg)
