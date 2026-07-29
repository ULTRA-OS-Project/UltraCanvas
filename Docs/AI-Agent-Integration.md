# Presenting UltraCanvas to AI Chats and Agents

Investigation into how the UltraCanvas framework and its sibling modules
(UltraAI, UltraNet, UltraDatabase, FileLoader, VirtualFS, plugins) can be
made usable by AI assistants — following the pattern popularized by
UI-framework vendors such as bit BlazorUI, and comparing it with the other
mechanisms currently in use across the industry.

Status: investigation / proposal. Nothing in this document is implemented yet.

---

## 1. Problem statement

AI coding assistants (Claude, Copilot, Cursor, Windsurf, ChatGPT, …) write
code from training data. For a young framework like UltraCanvas that means
they either know nothing about it or hallucinate plausible-looking APIs
(`canvas->AddButton(...)` style inventions). To make an AI assistant a
productive UltraCanvas developer, the framework has to *feed its own
documentation and API contracts into the assistant's context* at the moment
the user asks a question.

There are two distinct integration surfaces, and it is worth keeping them
separate because they have different customers:

- **A. Development-time integration** — an AI coding assistant helping a
  human write UltraCanvas applications. This is what the bit BlazorUI
  "AI (MCP)" getting-started section addresses, and it is the near-term,
  high-value target.
- **B. Runtime integration** — AI agents interacting with *running*
  UltraCanvas applications (reading the widget tree, driving controls,
  automating apps). No mainstream UI framework ships this today; for the
  ULTRA OS project it is a potential differentiator, but it is a separate,
  larger effort.

## 2. How other UI frameworks do it (survey)

The pattern has converged remarkably quickly across the Blazor/web
component-library space:

| Framework | Mechanism | Distribution | Tool surface |
|---|---|---|---|
| bit BlazorUI | MCP server over component docs | `dnx` (NuGet), or `dotnet run` from source | `list_components`, `get_component_detail` (docs, code examples, API reference) |
| Telerik UI for Blazor | MCP server ("Agentic UI Generator") | `npx @progress/telerik-blazor-mcp` / NuGet; license-gated | docs lookup + guided UI/page generation |
| HAVIT Blazor Bootstrap | MCP server for AI coding agents | hosted/getting-started page | component docs + API |
| BootstrapBlazor | MCP server over docs *and* source code | GitHub project | docs + source search |
| Fluent UI Blazor (community) | MCP server | NuGet | component metadata |
| Storybook | MCP addon exposing the live component catalog | npm | query components/props/stories at runtime |
| Material UI / Taiga UI and many others | `llms.txt` + `llms-full.txt` docs export, some with MCP on top | static files on the docs site | n/a (context files, not tools) |

Observations:

1. **MCP is the emerging standard for the *interactive* path.** A small,
   read-only tool surface (list → detail → search) over the existing
   markdown documentation is the whole product. Nobody exposes dozens of
   tools; 3–6 well-described tools outperform a large surface because the
   model must choose among them.
2. **`llms.txt` / `llms-full.txt` is the zero-infrastructure complement.**
   An index file plus a concatenated full-docs file on the project website
   lets any agent (including ones without MCP support) pull accurate docs.
   It also feeds indexers such as Context7 that many developers already
   have connected.
3. **Repo-level agent instructions (`AGENTS.md`, `CLAUDE.md`,
   `.cursorrules`) are table stakes.** They cost an afternoon and improve
   every AI interaction *inside* the repository and in projects created
   from templates.
4. Distribution matters more than implementation language: `npx`/`dnx`
   one-liners in an `mcp.json` snippet are what make these servers actually
   get used. Users copy a 6-line JSON block into VS Code/Cursor/Claude
   Code and are done.

## 3. What UltraCanvas already has

The good news: the expensive part — the content — largely exists.

- **`Docs/UltraCanvas/` contains ~100 markdown files**, one per component
  or subsystem (`UltraCanvasButtonExamples.md`, `UltraCanvasLineChartElement.md`,
  `UltraCanvasJSON.md`, coordinate-system guide, layout examples, …), most
  with buildable C++ examples. This is exactly the corpus every MCP docs
  server in the survey is built on.
- **`Masterfile_modules.md`** is an authoritative, agent-friendly module
  registry: purpose + public function surface for UltraNet, UltraDatabase,
  FileLoader, UltraAI, etc. It reads like it was written for an LLM already.
- **Module READMEs** under `Docs/Modules/<Name>/` cover the sibling modules.
- **UltraAI** gives the framework an in-process AI capability layer
  (`ITextLLM` with tool-call support) — relevant for surface B and for MCP
  *client* support later.

Gaps:

- No machine-readable component index (name → header → doc file → category).
  File naming is consistent enough that one can be generated.
- No public docs website yet, so `llms.txt` would have to be served from
  raw.githubusercontent.com or a future site.
- Docs freshness varies; whatever pipeline is chosen must treat
  `Docs/` in-repo as the single source of truth so the MCP server can never
  drift from the framework.

## 4. Recommended approach

Do all three of the following, in this order. They are complementary, not
alternatives — the survey shows mature projects ship every layer.

### Phase 0 — repo/agent hygiene (days, no infrastructure)

1. Add **`AGENTS.md`** (and a thin `CLAUDE.md` pointing at it) at the repo
   root: what UltraCanvas is, C++20 conventions, how to build, where docs
   live, the factory/`shared_ptr` widget-creation pattern, "consult
   `Docs/UltraCanvas/<Component>*.md` before using a component".
2. Generate **`llms.txt`** (index: one line per doc file with a
   description) and **`llms-full.txt`** (concatenation of
   `Masterfile_modules.md` + `Docs/UltraCanvas/*.md` + module READMEs) via
   a small script wired into CI, committed or published as a release asset.
3. **Submit the repo to Context7** (free) so the large installed base of
   Context7 users resolves `ultracanvas` to real docs immediately.

### Phase 1 — `ultracanvas-mcp` documentation server (the bit BlazorUI equivalent)

A standalone, read-only MCP server over the docs corpus.

- **Tools** (mirroring the proven surface):
  - `list_modules()` → from `Masterfile_modules.md`
  - `list_components(category?)` → generated component index
  - `get_component_doc(name)` → full markdown for one component (accepts
    `Button` and `UltraCanvasButton` case-insensitively)
  - `search_docs(query)` → keyword/BM25 search across the corpus
  - `get_examples(name)` → just the fenced C++ blocks from a component doc
  - `get_setup_guide(platform)` → build prerequisites + CMake snippet per OS
- **Implementation language: TypeScript on the official MCP SDK,
  distributed as `npx @ultra-os/ultracanvas-mcp`**, with the markdown
  bundled into the package at publish time from this repo. Rationale: the
  `npx` one-liner is the de-facto installation UX across every AI client;
  a C++ server would be dogfooding-pure but would make installation the
  hardest part of the product. (A C++ implementation can come later as an
  ULTRA OS-native binary; the tool contract stays identical.)
- **Client config snippet** to publish in README/docs, e.g. for Claude
  Code / Cursor / VS Code:

  ```json
  {
    "mcpServers": {
      "ultracanvas": {
        "command": "npx",
        "args": ["-y", "@ultra-os/ultracanvas-mcp@latest"]
      }
    }
  }
  ```

- Keep it **stdio-only and read-only** first; a hosted Streamable-HTTP
  variant (for web chats that support remote MCP servers) can reuse the
  same tool implementations later.

### Phase 2 (optional, differentiating) — runtime MCP for ULTRA OS

Two directions, both natural extensions of existing modules:

- **MCP client in UltraAI** — `ITextLLM` already models tool calls; adding
  an MCP client (on UltraNet HTTP/WebSocket + `UltraCanvasJSON`) lets any
  UltraCanvas app hand external MCP servers' tools to its embedded AI
  features. This makes UltraCanvas apps *consumers* of the MCP ecosystem.
- **MCP server embedded in UltraCanvas apps** (an `UltraMCP` module):
  expose the running app's window/widget tree, values and actions
  (`list_windows`, `describe_widget_tree`, `click`, `set_text`,
  `read_chart_data`, screenshot via the rendering engine) so desktop
  agents can drive ULTRA OS applications through a structured interface
  instead of pixel-level automation. No mainstream C++ UI framework offers
  this today; it doubles as an agent-driven UI-testing story. Requires a
  deliberate security model (opt-in per app, local-only by default,
  user-visible indicator).

## 5. Options considered and not recommended as the primary path

- **`llms.txt` only, no MCP** — cheapest, but passive: agents must be told
  to fetch it, context windows fill with the full corpus, and there is no
  targeted lookup. Keep it as a complement (Phase 0), not the main answer.
- **Fine-tuning / custom GPTs** — high maintenance, provider-specific,
  instantly stale versus docs-driven retrieval.
- **Relying on indexers only (Context7, DeepWiki)** — zero effort but no
  control over freshness/quality and does not cover every client. Good as
  a free additional channel.
- **A large "UI generator" MCP (Telerik-style agentic page generation)** —
  attractive later, but it presupposes the docs-lookup layer; start with
  retrieval tools and add generation prompts/resources afterwards.

## 6. Suggested roadmap summary

| Phase | Deliverable | Effort | Reach |
|---|---|---|---|
| 0 | `AGENTS.md`/`CLAUDE.md`, `llms.txt` + `llms-full.txt` generator, Context7 listing | days | every AI client, passively |
| 1 | `@ultra-os/ultracanvas-mcp` stdio server (6 read-only tools) + docs page with config snippets | 1–2 weeks | all MCP clients (Claude, Cursor, VS Code, Windsurf, …) |
| 1b | Hosted Streamable-HTTP endpoint reusing the same tools | small | web-based chats |
| 2 | UltraAI MCP client; `UltraMCP` runtime server for apps | larger, design-first | ULTRA OS differentiation |
