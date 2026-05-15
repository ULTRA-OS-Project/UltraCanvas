# UltraAI

**Provider-agnostic AI capability module for ULTRA OS.**
Sibling of `UltraCanvas` (UI) and `UltraNet` (networking).

UltraAI gives every ULTRA OS app a single, stable C++ surface for AI
features — chat, vision, speech, image / video / music generation,
translation, code assist, and more — backed by interchangeable
providers (cloud SaaS, local models, or in-process mocks).

---

## Why it exists

The OS owns AI features the same way it owns audio, networking, or
the filesystem: one API for apps, swappable backends underneath.
Without that, every app would re-implement key vault, retries, SSE
parsing, streaming, and provider quirks — and users couldn't switch
between cloud and local models without rebuilding apps.

---

## Capabilities

| # | Interface | What it does |
|---|---|---|
| 1 | `ITextLLM` | Chat / completion, multimodal input, tool calls, structured output, streaming |
| 2 | `IEmbeddings` | Batched text embeddings, variable dimensions, cosine similarity helper |
| 3 | `ISpeechToText` | File transcription + live mic streaming, segments, diarization, word timestamps |
| 4 | `ITextToSpeech` | Synthesis, voice listing, chunked streaming playback, voice cloning |
| 5 | `IImageGen` | txt2img / img2img / inpaint / outpaint / upscale / variation, ControlNet, job progress |
| 6 | `IVisionAnalyzer` | Caption, tags, detection, segmentation, OCR + document layout, faces, safety, VQA |
| 7 | `ITranslator` | Batched translation with auto-detect, formality, glossary; language detection |
| 8 | `IVideoGen` | txt2vid / img2vid / vid2vid / interpolation / upscale, optional audio track |
| 9 | `IMusicGen` | Instrumental / Song / SFX / continuation, lyrics, vocals, separated stems |
| 10 | `ICodeAssist` | Generate / Explain / Refactor / Translate-language / Detect-bugs / Tests / Docs / FIM |

Each interface ships with a programmable mock adapter so apps and
tests run without any network or external model.

---

## Architecture

```
App
 ├─► UltraAI::I<Capability>          (stable C++ interface)
 │     │
 │     └─► Adapter (mock | anthropic | openai | local-llama | ...)
 │           │
 │           ├─► UltraNet            (HTTP, WebSocket, TLS, DNS)   [opt-in]
 │           └─► UltraVault          (named-secret resolution)     [opt-in]
 │
 └─► UltraCanvas                     (UI widgets)
```

* **Adapters are independently opt-in** via `ULTRAAI_ADAPTER_<NAME>`
  CMake options.
* **Self-registering factories** — adapters appear in
  `Create<Capability>` / `List<Capability>Providers` / `Register<...>Provider`
  the moment they're linked.
* **Escape hatch** — every interface exposes `RawProvider()` returning
  the underlying SDK pointer, so power users keep full provider-specific
  feature access.

---

## Module layout

```
UltraAI/
├── include/                       # Public capability headers
│   ├── UltraAI.h                  # umbrella
│   ├── UltraAICommon.h            # Error, OptionsMap, MediaBlob, TokenUsage, ProviderConfig
│   ├── UltraAITextLLM.h
│   ├── UltraAIEmbeddings.h
│   ├── UltraAISpeechToText.h
│   ├── UltraAITextToSpeech.h
│   ├── UltraAIImageGen.h
│   ├── UltraAIVisionAnalyzer.h
│   ├── UltraAITranslator.h
│   ├── UltraAIVideoGen.h
│   ├── UltraAIMusicGen.h
│   └── UltraAICodeAssist.h
├── src/                           # Capability factories (registry)
├── adapters/
│   └── mock/                      # In-process mocks for every capability
├── tests/                         # cassert-based unit tests
├── Docs/
│   ├── UltraNetIntegration.md     # how network adapters use UltraNet
│   └── UltraVault.md              # credential storage architecture
└── CMakeLists.txt
```

---

## Quick example

```cpp
#include <UltraAI.h>
using namespace UltraAI;

TextLLMConfig cfg;                  // empty providerId -> default route
auto llm = CreateTextLLM(cfg);

ChatRequest req;
Message m; m.role = Role::User; m.text = "summarize a kettle";
req.messages.push_back(std::move(m));

ChatResponse resp = llm->Chat(req);
if (resp.error) { /* ... */ }
std::cout << resp.text << '\n';
```

Streaming:

```cpp
llm->ChatStream(req, [](const StreamEvent& ev) {
    if (ev.kind == StreamEventKind::TextDelta) std::cout << ev.textDelta;
    if (ev.kind == StreamEventKind::Done)       std::cout << '\n';
});
```

Pattern is identical for every other capability — `Speak`, `Generate`,
`Analyze`, `Transcribe`, `Translate`, `Run`.

---

## Build and test

```bash
cmake -S UltraAI -B build -DULTRAAI_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

Optional sibling-module integration (no-ops gracefully if the targets
aren't visible to CMake):

```bash
cmake -S UltraAI -B build \
    -DULTRAAI_USE_ULTRANET=ON \
    -DULTRAAI_USE_ULTRAVAULT=ON
```

---

## Status (v0.1)

| Component | State |
|---|---|
| 10 capability interfaces | Complete |
| Mock adapters (every capability) | Complete |
| Registry / factory plumbing | Complete |
| Unit tests | Complete (3 executables, all passing) |
| Real network adapters | Not yet — pending UltraNet integration |
| UltraVault credential lookup | Wired, awaits UltraVault module |

---

## Design documents

* [`Docs/UltraNetIntegration.md`](Docs/UltraNetIntegration.md) —
  how network adapters use UltraNet (HTTP / WebSocket / SSE),
  threading model, adapter checklist.
* [`Docs/UltraVault.md`](Docs/UltraVault.md) —
  credential-storage architecture comparison and recommendation.

---

*Part of ULTRA OS · MIT license · Cloverleaf UG*
