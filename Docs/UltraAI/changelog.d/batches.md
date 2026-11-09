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
