- **Agents: a model that uses tools until the task is done**
  (`UltraAIAgent.h`, part of `UltraAI_Core`). `UltraAI::Agent` runs the
  model ↔ tool loop over any `ITextLLM` (anthropic, openai, qwen, local,
  llama-cpp, mock). An application registers C++ functions as tools
  (`AddTool`; `ToolArguments` reads the arguments the model wrote,
  `ToolResult::Ok` / `Fail` answers) and calls `Run`. The agent calls the
  model, runs the tools it asks for and feeds the results back until it
  answers. Each tool call is offered to `AgentOptions::canUseTool` (allow,
  deny with a reason the model reads, or stop the run), and each result
  passes `afterToolCall`. Progress events (turns, streamed text with
  `stream`, tool calls and results) all arrive on the run's thread. Also:
  `maxTurns`, `Cancel()` from any thread, `RunAsync`, a conversation that
  carries over between runs (`History` / `SetHistory` / `ClearHistory`;
  a run that fails before the model answered leaves it unchanged),
  `CreateAgent` from a provider config, and subagents
  (`MakeSubagentTool`). Every tool call gets a result in the history
  (denied, unknown, throwing and cancelled ones too), so the conversation
  stays one a provider accepts. `Docs/Modules/UltraAI/Agents.md`; tests in
  `UltraAI/tests/test_agent.cpp`.
- The text-LLM translator's private JSON reader moved to
  `UltraAI/src/UltraAIMiniJson.h`, shared with the agent.
