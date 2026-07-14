# AnchorPoint

**Peer-to-peer file sharing — computer to computer, with no server file storage.**

AnchorPoint sends files *directly* between two machines. Bytes never land on a
server's disk. On a LAN it needs no infrastructure at all; across the internet
it uses a tiny **signaling** step to help two peers find each other and punch
through NAT, after which the file flows directly and end-to-end encrypted.

> Part of ULTRA OS. Built on **UltraCanvas** (UI) and designed to sit on the
> forthcoming **UltraNet** module (transport). See "Networking strategy" below.

---

## Status

| Milestone | Scope | State |
|---|---|---|
| **M1 — Core + CLI** | Transfer protocol (chunked, SHA-256 verified, resumable) + raw-socket TCP transport + headless `anchorpoint` CLI | ✅ Done, tested |
| **M2 — GUI** | UltraCanvas window: choose a file, enter a peer address, send/receive with live progress | ✅ Done |
| **M3 — Internet** | Signaling client + STUN/UDP hole-punching + relay fallback + E2E encryption + pairing codes | ⏳ Planned |

---

## Networking strategy

AnchorPoint's networking is written against a thin interface
([`net/Transport.h`](net/Transport.h)) that mirrors the shape of the planned
**UltraNet** API (`UltraNet/UltraNetSocket.h`, `UltraNetWebSocket.h`,
`UltraNetTls.h`).

* **Today:** backed by raw POSIX / Winsock sockets
  ([`net/RawSocketTransport.cpp`](net/RawSocketTransport.cpp)) so the core
  builds and runs with **zero external dependencies**.
* **When UltraNet ships:** add an `UltraNetTransport` backend implementing the
  same `IConnection` / `IListener` interface. The protocol, CLI, and GUI are
  unchanged.

UltraNet provides almost everything AnchorPoint needs — TCP/UDP, WebSocket
(for signaling), TLS, and mDNS (LAN discovery). The **one** piece it does not
provide is NAT traversal (STUN/ICE/TURN); AnchorPoint supplies a small,
self-contained STUN + UDP hole-punching module for that in M3, on top of
UltraNet's UDP sockets.

---

## The wire protocol (M1)

Length-prefixed frames: `[1B type][4B big-endian length][payload]`.

```
Hello    → protocol version + sender display name
Offer    → file name, size, chunk size, whole-file SHA-256
Accept   → resume offset (receiver tells sender where to start)
Chunk    → [8B file offset][bytes]   (256 KiB default)
Done     → whole-file SHA-256 (sender's assertion)
Verified → receiver re-hashes the written file and confirms (or errors)
```

Integrity is verified end-to-end with SHA-256; partial files resume from the
byte count already on disk.

---

## Build

```bash
cd Apps/AnchorPoint

# Core + headless CLI only (no framework deps — std lib + platform sockets):
cmake -B build -DANCHORPOINT_BUILD_GUI=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
# → build/bin/anchorpoint

# Core + CLI + UltraCanvas GUI (needs the framework toolchain):
cmake -B build -DANCHORPOINT_BUILD_GUI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
# → build/bin/AnchorPoint  and  build/bin/anchorpoint
```

The core + CLI have **no** dependencies beyond the standard library and the
platform socket API (`ws2_32` on Windows, pthreads on Linux). The GUI links the
UltraCanvas framework the same way `Apps/Texter` does.

---

## CLI usage (M1)

The *connection role* (who listens, who connects) is independent of the *data
role* (who sends, who receives) — the same decoupling NAT-traversed sessions
will use later.

```bash
# Receiver waits; sender connects and pushes:
anchorpoint recv --out ./downloads --listen --port 5040     # machine A
anchorpoint send ./photo.jpg --to 192.168.1.20:5040         # machine B

# Or sender waits; receiver connects and pulls:
anchorpoint send ./photo.jpg --listen --port 5040           # machine A
anchorpoint recv --out ./downloads --from 192.168.1.10:5040 # machine B
```

Transfers show live progress, resume from interrupted partials, and refuse to
report success unless the received bytes hash-match the source.

---

## GUI usage (M2)

Launch `AnchorPoint`. The window has two halves:

* **Send a file** — click *Choose File…*, type the peer's address
  (`host` or `host:port`, default port 5040), and click *Send*.
* **Receive** — set a listen port and a save folder, then click *Receive* to
  wait for an incoming file. *Stop* cancels a pending listen.

A Unicode progress bar and status line show live transfer state. All networking
runs on worker threads; widgets are only ever touched from the UI thread via a
periodic timer that reads the mutex-guarded transfer state — the threading
contract UltraNet's off-thread callbacks will also require.

> **Build note:** a full GUI *link* currently depends on the UltraCanvas core
> library building, which in this tree is blocked by the framework's
> half-scaffolded `Plugins/UltraNet/*` sources (`#include <UltraNet/UltraNetCore.h>`,
> not yet present). AnchorPoint's own sources compile cleanly against the real
> framework headers. The M2 GUI does **not** use `UltraCanvasProgressBar.h`,
> which is an uncompiled/latent-broken header in the current tree.
