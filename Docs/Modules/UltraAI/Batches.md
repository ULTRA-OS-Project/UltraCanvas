# UltraAI Batches — many requests as one job, about half the price

**Version:** 0.1.0
**Last Modified:** 2026-11-09

## Overview

Anthropic's Message Batches and OpenAI's Batch API take a list of chat
requests and answer them within 24 hours (usually much sooner). They charge
about half of what the same requests cost one at a time. That suits work
nobody is waiting on: translating a whole catalogue, summarising an archive,
classifying a mailbox.

`ITextBatch` is that job, written once for both. It also works for every
other text-LLM provider: where a provider has no batch endpoint (`local`,
`qwen`, `llama-cpp`, `mock`, an OpenAI-compatible server), the same
interface sends the requests one after another on a thread of its own. An
application uses one API and gets the discount where there is one.

Header: `UltraAIBatch.h` (included by `UltraAI.h`), part of `UltraAI_Core`.
Tests: `UltraAI/tests/test_batch.cpp`.

```cpp
#include <UltraAI.h>
#include <iostream>
using namespace UltraAI;

void TranslateAll(const std::vector<std::string>& texts) {
    TextBatchConfig cfg;
    cfg.providerId = "anthropic";
    cfg.apiKeyVaultRef = "ai.anthropic.api_key";

    Error err;
    std::unique_ptr<ITextBatch> batch = CreateTextBatch(cfg, &err);
    if (!batch) { std::cerr << err.message << '\n'; return; }

    std::vector<BatchRequestItem> items;
    for (size_t i = 0; i < texts.size(); ++i) {
        BatchRequestItem item;
        item.customId = "text-" + std::to_string(i);
        Message m;
        m.role = Role::User;
        m.text = "Translate into German, reply with the translation only:\n" + texts[i];
        item.request.messages.push_back(m);
        items.push_back(item);
    }

    BatchRunResult run = RunBatch(*batch, items);     // submit, poll, collect; blocks
    if (!run.error.IsOk()) { std::cerr << run.error.message << '\n'; return; }
    for (const BatchItemResult& r : run.results) {
        if (r.status == BatchItemStatus::Succeeded) std::cout << r.customId << ": " << r.response.text << '\n';
        else std::cout << r.customId << " failed: " << r.response.error.message << '\n';
    }
}
```

## Contents

- [Overview](#overview)
- [Which providers](#which-providers)
- [Items](#items)
- [The job](#the-job)
- [RunBatch: submit, wait, collect](#runbatch-submit-wait-collect)
- [Step by step](#step-by-step)
- [Results](#results)
- [Limits of this version](#limits-of-this-version)

## Which providers

`CreateTextBatch(config)` gives the provider's own batch when it has one,
and the one-at-a-time fallback otherwise. `IsNative()` says which one you
got.

| Provider | `IsNative()` | How |
|---|---|---|
| `anthropic` | yes | `POST /v1/messages/batches`; results as JSONL from `.../results` |
| `openai` (hosted API) | yes | the requests uploaded as a JSONL file (`/v1/files`), a batch made from it (`/v1/batches`), results read from its output and error files |
| `openai` with a custom `baseUrl` | no | Ollama, vLLM and llama.cpp server have no batch endpoint |
| `local`, `qwen`, `llama-cpp`, `mock` | no | one request at a time through the text LLM |
| empty `providerId` | depends | the text-LLM route chooses; native if it lands on one of the above |

The fallback lives in the object that ran it: its batch ids mean nothing to
another object or after a restart. A native batch id stays valid at the
provider (results are kept for 29 days at Anthropic and 30 days at OpenAI),
so a program can store it and come back for the answers later.
`CreateSequentialTextBatch(llm, concurrency)` builds the fallback over an LLM
you already hold, with several requests in flight at once if the server
allows it. `ListTextBatchProviders()` lists the native ones and
`RegisterTextBatchProvider` adds one.

## Items

A `BatchRequestItem` is a `customId` and an ordinary `ChatRequest`: model,
system and user messages, sampling, tools and structured output, all as for
`ITextLLM::Chat`. Results come back in any order, so the `customId` is what
matches an answer to its request. It must be unique within the batch and
1–64 characters of letters, digits, `-` and `_`. `ValidateBatchItems` checks
the list (and `Submit` runs the same check first).

## The job

`BatchJob` is the batch as the provider last described it:

| Field | Meaning |
|---|---|
| `id` | the provider's batch id |
| `state` | `Validating`, `InProgress`, `Finalizing`, `Cancelling`, then `Completed`, `Cancelled`, `Expired` or `Failed` |
| `counts` | `total`, `processing`, `succeeded`, `failed`, `cancelled`, `expired` |
| `createdAt`, `endedAt`, `expiresAt` | RFC 3339 UTC times, when the provider gives them |
| `error` | the call failed, or the batch was refused (`state == Failed`) |

`IsFinished(state)` is true for the last four states: nothing will change
any more, and the results can be read. `Completed` does not mean every
request succeeded; look at each result.

## RunBatch: submit, wait, collect

`RunBatch(batch, items, options)` submits the items, polls until the job is
finished and fetches the results. It blocks, so run it off the UI thread.
`BatchRunOptions`:

| Field | Default | Meaning |
|---|---|---|
| `pollInterval` | 10 s | the first wait between polls (at least 200 ms) |
| `maxPollInterval` | 2 min | the longest wait between polls |
| `backoff` | 1.5 | each wait is this much longer than the last |
| `timeout` | 0 (none) | stop waiting after this long; the batch keeps running at the provider (`ErrorCode::Timeout`) |
| `onProgress` | unset | sees the job after every poll |
| `cancel` | null | a flag; when it turns true the batch is cancelled at the provider and what was answered is returned (`ErrorCode::Cancelled`) |

The fallback is polled every 200 ms whatever the options say. A poll that
fails (network, rate limit) is tried again, and five failures in a row end
the wait.

```cpp
#include <UltraAI.h>
#include <atomic>
using namespace UltraAI;

BatchRunResult Watched(ITextBatch& batch, const std::vector<BatchRequestItem>& items,
                       std::atomic<bool>& stopFlag) {
    BatchRunOptions options;
    options.pollInterval = std::chrono::seconds(30);
    options.onProgress = [](const BatchJob& job) {
        // e.g. post "412 of 1000 done" to the UI thread
        (void)job.counts.succeeded;
    };
    options.cancel = &stopFlag;
    return RunBatch(batch, items, options);
}
```

## Step by step

For a batch that outlives the program, use the four calls directly:

```cpp
#include <UltraAI.h>
using namespace UltraAI;

std::string Start(ITextBatch& batch, const std::vector<BatchRequestItem>& items) {
    BatchJob job = batch.Submit(items);
    return job.error.IsOk() ? job.id : std::string{};     // store the id
}

void Later(ITextBatch& batch, const std::string& id) {
    BatchJob job = batch.Get(id);
    if (!IsFinished(job.state)) return;                   // ask again later
    Error err;
    std::vector<BatchItemResult> results = batch.Results(id, &err);
    (void)results;
}

void GiveUp(ITextBatch& batch, const std::string& id) {
    batch.Cancel(id);     // Cancelling, then Cancelled; running requests still finish
}
```

## Results

`Results(id)` gives one `BatchItemResult` per request of a finished job:
its `customId`, a `status` and the `response`, which is the same
`ChatResponse` that `Chat` returns (text, tool calls, usage, finish reason):

| `BatchItemStatus` | `response` |
|---|---|
| `Succeeded` | the answer |
| `Failed` | `error` with the provider's code and message (bad parameters, unknown model, ...) |
| `Cancelled` | the batch was cancelled before this request ran |
| `Expired` | the 24-hour window ran out before this request ran |

## Limits of this version

- Text chat requests only (the providers' batch endpoints also take
  embeddings and other calls; `ITextBatch` does not yet).
- The input is sent in one upload. OpenAI accepts up to 50 000 requests or
  200 MB per batch, Anthropic 100 000 requests or 256 MB; split larger work
  into several batches.
- The dashboard has no batch page yet.
