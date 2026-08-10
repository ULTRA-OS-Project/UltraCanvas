# UltraNet API status report (`UltraNetApiStatus`)

A console tool that walks the **entire public UltraNet surface** and reports,
per function, what state it is in *on the machine running it*.

It answers a different question from the `UltraNetTests` suite. The test
suite answers "does the code still behave?" and prints pass/fail. This tool
answers "which parts of the API can I actually rely on in this build?" and
prints one of four statuses per entry.

| Status | Meaning |
|---|---|
| **WORKING** | The probe drove the real code path end to end and the observable result matched what the API promises. |
| **IMPLEMENTED** | The implementation exists and was reached, but this environment cannot confirm the behaviour — no live peer, an optional backend is absent, or the effect has no public accessor. |
| **NOT IMPLEMENTED** | A documented stub / no-op, a field nothing ever writes, or a backend this build of libcurl does not carry. |
| **BROKEN** | The probe ran and the result contradicted the API. |

`BROKEN` exists on purpose: a status tool that can only report good news
would quietly turn a regression into a reassuring "implemented".

---

## Building and running

```bash
cmake -S . -B build -DULTRACANVAS_BUILD_NET_TESTS=ON
cmake --build build --target UltraNetApiStatus
./build/bin/UltraNetApiStatus
```

It is also registered with CTest:

```bash
ctest --test-dir build -R UltraNetApiStatus
```

### Options

| Option | Effect |
|---|---|
| `--format=text\|markdown\|json` | Report format (default `text`). |
| `--area=<name>` | Probe one area only: `Core`, `URL`, `HTTP`, `Session`, `SSE`, `WebSocket`, `DNS`, `Socket`, `TLS`, `FTP`, `MIME`, `Plugins`. |
| `--output=<path>` | Write the report to a file instead of stdout. |
| `--network` | Also run the probes that need the public internet. |
| `--strict` | Exit non-zero unless **every** entry is `WORKING`. |
| `--serve[=<seconds>]` | Diagnostic: start only the loopback origin, print its port, hold it open. Runs no probes. |

Exit code is `0` when nothing is `BROKEN` (and, with `--strict`, when
everything is `WORKING`), `1` otherwise, `2` on a usage error.

### Environment overrides

| Variable | Purpose |
|---|---|
| `ULTRANET_PROBE_NETWORK=1` | Same as `--network`. |
| `ULTRANET_PROBE_HTTPS_URL` | HTTPS endpoint used by the TLS-info probe. |
| `ULTRANET_PROBE_DNS_DOMAIN` | Domain used by the record-type DNS probes. |
| `ULTRANET_PROBE_FTP_URL` | Writable FTP directory, e.g. `ftp://user:pass@host/probe/`. Upgrades the whole FTP area from `IMPLEMENTED` to verified. |
| `ULTRANET_PROBE_PLUGIN_DIR` | Directory of plug-in DSOs for the `UltraNet_RefreshPlugins` probe. |

---

## How it verifies things without a network

The tool starts its own peers, so a machine with no internet access — and no
Python, and no external services — still gets verified results for most of
the surface.

* **HTTP / Session / SSE / WebSocket** — an in-process HTTP/1.1 + WebSocket
  origin built on UltraNet's *own* TCP API (`UltraNet_TcpListen` /
  `TcpAccept` / `TcpSend` / `TcpReceive`). It speaks keep-alive, chunked
  request bodies, `Expect: 100-continue`, redirects, cookies, Basic auth
  challenges, `text/event-stream`, a deliberately slow route for cancellation
  probes, and a full RFC 6455 WebSocket echo endpoint (its own SHA-1 for the
  `Sec-WebSocket-Accept` derivation, so no OpenSSL dependency). Because the
  origin *is* UltraNet TCP, a broken socket layer makes the origin fail to
  start and the dependent probes say so instead of silently reporting
  "unverified".
* **TCP / UDP** — loopback peers the probes start themselves.
* **TLS** — `openssl s_server` on loopback with a throwaway self-signed
  certificate. That also makes the trust-store entries checkable *in both
  directions*: the same handshake must fail against the default trust store
  and succeed once the probe's own certificate is trusted.
* **DNS** — numeric addresses and `localhost` resolve without a network;
  record types beyond A/AAAA/PTR need `--network`.
* **URL / MIME / SSE parser / plugin registry** — pure code, always verified.
* **FTP** — the one area with no offline peer. The probes still establish
  that each entry point validates its input and that the call reaches the
  libcurl FTP backend (a closed port must report a connection error, not
  `UnsupportedScheme`). Set `ULTRANET_PROBE_FTP_URL` for real verification.

---

## Adding a probe

Probes live in `Tests/UltraNet/ApiStatus/Probe<Area>.cpp`, one per public
entry, written in header order — the report preserves declaration order
within an area.

```cpp
ULTRANET_PROBE(kArea, UltraNet_HttpGet) {
    LoopbackServer& server = Loopback();
    if (!server.Running()) return NoOrigin(server, "a live GET");

    UltraNetResponse resp;
    const UltraNetResult r = UltraNet_HttpGet(server.Url("/hello"), resp);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT(resp.statusCode == 200);
    return Working("200 body/headers/content-type/length verified");
}
```

Rules that keep the report trustworthy:

* `PROBE_EXPECT` failing means the API misbehaved → `BROKEN`, never
  "unverified".
* A missing peer, absent backend, or unreachable network is
  `Implemented("<why>")` — never `Working`.
* Use `ULTRANET_PROBE_NAMED(area, "display name", uniqueId)` for entries
  whose reported name is not an identifier (class methods, option fields,
  grouped entries).
* The `detail` string is the point of the report. For `WORKING` say what was
  checked; for anything else say exactly what is missing and how to supply
  it.
