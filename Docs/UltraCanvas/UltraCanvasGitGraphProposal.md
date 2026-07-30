# UltraCanvasGitGraph — Research & Feature Proposal

Status: **Proposal only — nothing implemented yet.** This document is the
research write-up and the roadmap for a comprehensive Git commit-graph element
for UltraCanvas. No header, source file, demo tab or CMake entry exists for it
at the time of writing.

Author: UltraCanvas Framework
Last Modified: 2026-07-30

---

## 1. What a Git graph is

A **Git graph** (commit graph, revision graph, history graph) visualises the
commit history of a repository. The underlying data is a **directed acyclic
graph**: every commit points at *zero or more parents*, and nothing ever points
forward in time. Everything else — branches, tags, `HEAD` — is a *ref*: a
movable label attached to one commit.

The five structural cases any complete implementation has to draw correctly:

| Case | Parents | What it looks like |
|---|---|---|
| **Root commit** | 0 | Start of a lane; a repo can have several (unrelated histories, grafts) |
| **Normal commit** | 1 | A node on a continuing lane |
| **Branch point** | 1 (shared by ≥2 children) | One lane splits into two — the *fork* |
| **Merge commit** | 2 | Two lanes converge into one node |
| **Octopus merge** | ≥3 | Three or more lanes converge — rare, but it must not corrupt the layout |

Beyond the DAG itself, a usable element must render the *decorations* — local
and remote branch chips, lightweight and annotated tags, the `HEAD` marker,
detached `HEAD`, stash entries — and it must survive the awkward real-world
shapes: criss-cross merges, orphan branches, thousands of concurrent lanes in a
busy monorepo, and histories of 10⁵–10⁶ commits.

The hard part is **not** the drawing, it is the **layout**. As the survey of
existing implementations puts it: "getting the swim lanes right, the curves
between branches, the spacing, the commit messages aligned properly — what looks
like a simple visual is actually a gnarly layout algorithm problem underneath."
Different tools resolve it differently, and the differences are visible at a
glance:

| Tool | Layout character |
|---|---|
| `git log --graph` | Text, line-by-line, compact, commits reordered (`--topo-order`) rather than strictly by date |
| gitk / qgit / tig | Lane-based, very compact, reordered — the classic "leftmost free lane" assignment |
| GitKraken | Branches drawn as **totally straight** lines — lanes are never recycled while a branch is alive |
| Git Extensions / SmartGit | Very **curved** branch transitions, similar algorithms to each other |
| VS Code *Git Graph* / GitLens | Orthogonal routing with rounded corners, graph column paired with a commit table |
| Mermaid `gitGraph` / gitgraph.js | Authored (not repository-derived) diagrams for documentation and teaching |

Those two poles — **compact-and-recycled** vs **straight-and-stable** — are a
user preference, not a right answer, so the element should offer both.

---

## 2. What the reference image demands

The uploaded image ("GIT Branch and its Operations") is not a repository dump —
it is the **didactic swimlane form** of a Git graph, and it defines a distinct
set of requirements from the tool-style layouts above:

* A single **horizontal trunk** labelled `Master`, drawn as a continuous
  straight axis line with an arrowhead, time flowing **left → right**.
* Two feature branches, `Feature-2` **above** the trunk and `Feature-1`
  **below** it — i.e. lanes are distributed on *both sides* of the trunk, not
  stacked monotonically to one side.
* Branches leave the trunk on a **smooth curve**, run flat while they are
  alive, and **curve back** into a merge node on the trunk.
* Commit nodes are **filled circles coloured per branch** (green trunk, blue
  `Feature-2`, magenta `Feature-1`); the merge nodes sit on the trunk and take
  the trunk colour.
* Every commit carries a **stacked column of file labels** (`File 1.0.1`,
  `File 2.0.1`, `File 3.0.1` …) hanging off the node — a per-commit changed-file
  list rendered as boxes, above the node on the upper branch and below it on the
  lower one.
* Branch names are drawn as **lane-end labels** (`Master` at the right end,
  `Feature-1` / `Feature-2` next to their lanes), not as chips on a tip commit.

> Requires: left-to-right orientation; a **swimlane layout mode** where lane
> index maps to a signed offset either side of a nominated trunk; curved
> fork/merge edges (arc or Bezier); per-lane colour applied to nodes *and*
> edges; a per-commit file-label column with leader lines and side-aware
> placement; lane-end branch labels; an arrowheaded trunk baseline.

This matters for scope: the element must serve **two audiences at once** — the
repository browser (thousands of commits, compact lanes, a paired commit table)
and the documentation/teaching diagram (a dozen commits, curved swimlanes,
authored programmatically). They share a data model, a lane model and a
renderer; they differ in layout mode and density. Both are in scope; the
reference image is the second.

---

## 3. How this fits the existing UltraCanvas code

The framework already has almost all of the supporting machinery. The Git graph
element should **reuse, not duplicate**:

| Existing piece | Reuse for |
|---|---|
| `include/Plugins/Diagrams/UltraCanvasGourceTree.h` | The closest structural precedent: `UltraCanvasUIElement` subclass, node map + link list, `GourceStyle` struct with `GetStyle()`/`SetStyle()`, theme enum, zoom/pan/`ZoomToFit`/`CenterOnNode`, `SaveToSVG`, and the `onNodeClick`/`onNodeHover`/… callback block. Copy this shape. |
| `include/Plugins/Diagrams/UltraCanvasNodeDiagram.h` | The 2.0-era API conventions: simple + verbose `AddNode` overloads, selection API (`SelectNode(id, addToSelection)`, `GetSelectedNodeIds`), viewport block (`ZoomIn`/`ZoomOut`/`FitView`/`CenterOn`, min/max zoom), minimap and controls-panel config structs |
| `include/Plugins/Charts/UltraCanvasConnectionRenderer.h` | Edge drawing — `ConnectionDrawStyle`, `BezierControlPoints`, `SampleBezier`, `SampleArc`, `BuildOrganicCurve`, `DrawDashed`, `BuildArrowHead`. The reference image's fork/merge curves and the dashed cherry-pick links are already covered here |
| `include/Plugins/Charts/UltraCanvasLabelPlacement.h` | Collision-aware placement of commit labels, ref chips and the per-commit file boxes (`LabelShape`, `LabelSide`, `LabelPlacementOptions`, `PlacedShapeLabel::fitted`) |
| `include/Plugins/Diagrams/UltraCanvasDendrogram*.h` | The precedent for splitting a diagram into a **headless layout unit** (`UltraCanvasDendrogramLayout.h/.cpp`) plus a thin UI element — exactly the split proposed in §4 |
| `include/UltraCanvasListView.h` | Row virtualisation done right: `rowHeight`, variable-height rows, and the cached `rowTops` prefix-sum. The commit rows need the same treatment for 10⁵-commit histories |
| `include/UltraCanvasTableView.h` | The paired commit table (subject / author / date / sha) rather than reimplementing columns inside the graph element |
| `include/UltraCanvasSplitPane.h` | Graph pane ∥ table pane ∥ diff pane composition |
| `IRenderContext` (`UltraCanvasRenderContext.h`) | Everything the renderer needs already exists: `DrawBezierCurve`, `QuadraticCurveTo`/`BezierCurveTo`, `DrawArc`, `DrawCircle`, `DrawLinePath`/`FillLinePath`, `ClipPath`, `SetLineDash`, `Rotate`/`SetTransform` (rotated commit labels), `MeasureText` |
| `include/Plugins/Charts/UltraCanvasColormap.h` | Lane palettes and the colour-blind-safe default (Viridis/Tableau-style ramps already vendored) |
| `include/UltraCanvasSyntaxTokenizer.h` | Syntax-highlighted diff pane, if the optional diff view is built |
| `third_party/miniz` (already vendored, already in `UltraCanvas/CMakeLists.txt`) | zlib inflate — makes a native `.git` reader (loose objects + packfiles) feasible **without adding libgit2** |
| `Tests/ContourGeometryTest.cpp` | The precedent for a dependency-free geometry unit test; the lane assignment must be testable the same way |

**Build wiring** follows the existing pattern exactly: sources listed in
`UltraCanvas/CMakeLists.txt` next to the other `Plugins/Diagrams/*.cpp` entries,
demo file added to `DEMO_SOURCES` in the root `CMakeLists.txt`, and a
`CreateGitGraphExamples()` declaration in `Apps/DemoApp/UltraCanvasDemo.h`.

---

## 4. Proposed architecture

Four units, mirroring the Dendrogram precedent (a pure, unit-testable layout
core plus a thin UI element):

```
include/Plugins/Diagrams/UltraCanvasGitGraphTypes.h    # commit/ref/lane model     (no UI deps)
include/Plugins/Diagrams/UltraCanvasGitGraphLayout.h   # ordering + lane assignment (no UI deps)
include/Plugins/Diagrams/UltraCanvasGitGraph.h         # the element (UltraCanvasUIElement)
Plugins/Diagrams/UltraCanvasGitGraphLayout.cpp
Plugins/Diagrams/UltraCanvasGitGraph.cpp
Apps/DemoApp/UltraCanvasGitGraphExamples.cpp
Tests/GitGraphLayoutTest.cpp
Docs/UltraCanvas/UltraCanvasGitGraphExamples.md
```

Optional, and deliberately **outside** the element (see open question Q3):

```
include/UltraCanvasGitRepository.h                     # .git reader: refs, loose objects, packfiles
```

### 4.1 The layout core

`UltraCanvasGitGraphLayout` takes a commit list plus options and returns plain
geometry — no render context, no colours, no fonts:

```cpp
struct GitGraphPlacedCommit {
    std::string sha;
    int  row  = 0;          // position along the time axis
    int  lane = 0;          // signed in Swimlane mode, ≥0 otherwise
    bool isMerge = false;
    bool isRoot  = false;
};

struct GitGraphPlacedEdge {
    std::string childSha, parentSha;
    int  fromLane = 0, toLane = 0;
    int  fromRow  = 0, toRow  = 0;
    GitGraphEdgeKind kind = GitGraphEdgeKind::Parent;  // Parent | Merge | CherryPick | Skipped
};

struct GitGraphLayoutResult {
    std::vector<GitGraphPlacedCommit> commits;
    std::vector<GitGraphPlacedEdge>   edges;
    std::vector<std::vector<int>>     passThroughLanes;   // per row, lanes crossing it
    int laneCount = 0, rowCount = 0;
};
```

That signature is what makes the whole thing testable: a known DAG in, an
expected `(row, lane)` assignment out, with no window and no GL.

### 4.2 Lane assignment

The standard active-lane sweep, in commit order:

```
activeLanes : vector<optional<sha>>   // lane -> the sha that lane is waiting for

for each commit C at row r:
    matching = { lanes L : activeLanes[L] == C.sha }

    if matching is empty:
        lane(C) = AllocateLane()            // a tip / new head
    else:
        lane(C) = leftmost(matching)        // occupy the leftmost claimant
        for each other L in matching:       // the rest converge here…
            emit edge (L -> lane(C), kind = Merge)
            FreeLane(L)                     // …and die

    activeLanes[lane(C)] = C.parents[0]     // first parent continues this lane

    for each additional parent P in C.parents[1..]:
        L = lane already waiting for P, or AllocateLane()
        activeLanes[L] = P
        emit edge (lane(C) -> L, kind = Merge)

    record passThroughLanes[r] = { L : activeLanes[L] is set, L != lane(C) }
```

`AllocateLane` / `FreeLane` are where the two visual poles live:

* **`GitGraphLaneStrategy::Compact`** — `AllocateLane` returns the leftmost free
  slot and `FreeLane` marks it reusable immediately. Narrow graphs, branches
  wander sideways. This is gitk / `git log --graph`.
* **`GitGraphLaneStrategy::Stable`** — a lane is reserved for the life of the
  branch and never recycled while any descendant is pending, so a branch is a
  straight line end to end. Wider graphs, far easier to follow. This is
  GitKraken.

Complexity is `O(n · activeLanes)` with `activeLanes` bounded by the number of
concurrently open branches — linear in practice, and it runs incrementally as
rows arrive, which is what makes streaming a 200k-commit history viable.

### 4.3 Swimlane mode (the reference image)

`GitGraphLayoutMode::Swimlane` replaces the lane sweep with a branch-band
assignment: one band per branch, the nominated trunk at offset 0, other branches
alternating ±1, ±2 … either side (or all on one side, configurable). Row
position comes from commit order or from the timestamp (feature **L9**), and
fork/merge edges become curves between bands rather than lane steps. Everything
downstream — nodes, edges, labels, file boxes — is shared with the repository
modes.

---

## 5. Proposed feature list

Grouped, with a suggested delivery phase. **P1** = core, ship first; **P2** =
completes the reference image and the repository-browser use case; **P3** =
polish / advanced.

### 5.1 Data model & ingest

| # | Feature | Phase |
|---|---|---|
| D1 | Programmatic commit API — `AddCommit(sha, parents, author, email, timestamp, subject, body)` | P1 |
| D2 | Refs: local branches, remote branches, lightweight + annotated tags, `HEAD`, detached `HEAD` | P1 |
| D3 | Merge commits (2 parents) and octopus merges (≥3) without layout corruption | P1 |
| D4 | Root commits, and **multiple** roots / unrelated histories in one graph | P1 |
| D5 | Ingest from `git log` output with a fixed `--pretty=format:` template (parsed in-element; the element never spawns a process) | P1 |
| D6 | Authoring API for teaching diagrams — `BranchFrom`, `Commit`, `Merge`, `CherryPick`, `Tag`, `Checkout` | P1 |
| D7 | Per-commit changed-file payload (path + added/modified/deleted) — drives the image's file boxes | P1 |
| D8 | `IGitGraphDataSource` pull interface for lazy history (`FetchCommits(offset, count)`) | P2 |
| D9 | Cherry-pick and revert relations as non-parent edges | P2 |
| D10 | Working-tree / index pseudo-node above `HEAD` ("uncommitted changes") | P2 |
| D11 | Stash entries as side nodes | P2 |
| D12 | Incremental `AppendCommits()` / `PrependCommits()` that extends the layout instead of recomputing it | P2 |
| D13 | Mermaid `gitGraph` DSL import (`commit`/`branch`/`checkout`/`merge`/`cherry-pick`) | P2 |
| D14 | Native `.git` reader — `packed-refs`, loose objects via the vendored miniz inflate, `.idx`/`.pack` packfiles, reflog | P3 |
| D15 | Per-commit signature (GPG) state and a CI/status badge slot | P3 |

### 5.2 Ordering & layout

| # | Feature | Phase |
|---|---|---|
| L1 | Ordering modes: `AsGiven`, `CommitDate`, `AuthorDate`, `Topological` | P1 |
| L2 | Active-lane sweep with leftmost-claimant occupation (§4.2) | P1 |
| L3 | Lane strategies: `Compact` (gitk) and `Stable` (GitKraken straight branches) | P1 |
| L4 | Trunk pinning — a nominated branch always holds lane 0 (mermaid's `mainBranchOrder`) | P1 |
| L5 | `GitGraphLayoutMode::Swimlane` — signed bands either side of the trunk (reference image) | P1 |
| L6 | Uniform **and** content-sized row heights (multi-line labels, file-box stacks) | P1 |
| L7 | Layout is headless and deterministic; cached and invalidated only by data/ordering/strategy changes | P1 |
| L8 | Explicit lane ordering / priority by branch name | P2 |
| L9 | Time-proportional axis — position from timestamp rather than row index, with a date ruler and gap compression | P2 |
| L10 | Crossing reduction — barycentre reordering of newly allocated lanes | P2 |
| L11 | Parallel-commit rows: same-time commits share a row (mermaid `parallelCommits`) | P2 |
| L12 | Collapse a linear run into a single "*n* commits" ellipsis node, expandable | P2 |
| L13 | Filtering by branch / path / author / date range, with edges through hidden commits drawn as dashed `Skipped` edges | P2 |
| L14 | Incremental relayout on append — `O(new rows)`, not `O(n)` | P2 |
| L15 | Optional background-thread layout with an `onLayoutComplete` callback | P3 |

### 5.3 Rendering

| # | Feature | Phase |
|---|---|---|
| R1 | Orientations: `LeftToRight` (image), `TopToBottom` (gitk/SourceTree), `BottomToTop`, `RightToLeft` | P1 |
| R2 | Node shapes: filled circle, ring, square, diamond — with per-commit override | P1 |
| R3 | Distinct merge-node and root-node markers | P1 |
| R4 | Edge styles: `Straight`, `Orthogonal` (rounded corners), `Bezier`, `Arc` — all via `UltraCanvasConnectionRenderer` | P1 |
| R5 | Edge colour policy: from the child lane, the parent lane, or a fixed colour | P1 |
| R6 | Correct z-order: edges below nodes, with a background-coloured halo behind each node so crossing edges read cleanly | P1 |
| R7 | Trunk baseline — a continuous straight axis line for the main branch with an arrowhead (image) | P1 |
| R8 | Per-commit file-box column: stacked labelled boxes with leader lines, placed on the side away from the trunk (image) | P1 |
| R9 | Virtualised rendering — only rows intersecting the viewport (plus overscan) are drawn | P1 |
| R10 | Row striping and hover-row highlight | P1 |
| R11 | Commit types `Normal` / `Reverse` (crossed circle) / `Highlight` (filled rectangle), matching mermaid | P2 |
| R12 | Cherry-pick glyph plus a dashed link back to the source commit | P2 |
| R13 | Dashed edge and hollow node for the uncommitted-changes pseudo-node | P2 |
| R14 | Density presets: `Comfortable` / `Compact` / `Dense` | P2 |
| R15 | Minimap / overview strip of the whole history with a viewport rectangle | P3 |
| R16 | Animated transitions when commits are appended or a filter changes | P3 |

### 5.4 Refs, labels & decorations

| # | Feature | Phase |
|---|---|---|
| B1 | Branch chips at the tip commit, coloured by lane | P1 |
| B2 | Remote-branch chips with distinct styling and the remote prefix (`origin/main`) | P1 |
| B3 | Tag chips with a tag glyph; annotated vs lightweight distinguished | P1 |
| B4 | `HEAD` marker and current-branch emphasis | P1 |
| B5 | Lane-end branch labels for swimlane mode (`Master`, `Feature-1`, `Feature-2` — image) | P1 |
| B6 | Commit label template `{short_sha} {subject}` plus a `std::function` formatter override | P1 |
| B7 | Show/hide toggles: branches, commit labels, tags, dates, authors, file boxes | P1 |
| B8 | Chip overflow collapse (`+3`) with a tooltip listing the rest | P2 |
| B9 | Rotated commit labels (mermaid's 45° `rotateCommitLabel`) | P2 |
| B10 | Collision-aware label placement via `UltraCanvasLabelPlacement` | P2 |
| B11 | Text halo behind labels for legibility over edges and fills | P2 |

### 5.5 Paired commit table

| # | Feature | Phase |
|---|---|---|
| M1 | Row-alignment API (`GetRowTop`, `GetRowHeight`, `GetRowCount`, `onScroll`) so an `UltraCanvasTableView` can sit beside the graph | P2 |
| M2 | Built-in optional table pane: subject, author, relative date, short sha | P2 |
| M3 | Two-way synchronised scrolling and selection between graph and table | P2 |
| M4 | Column show/hide, ordering, widths; date-format callback | P2 |
| M5 | Author avatar column (image supplied by the app; initials fallback) | P3 |
| M6 | Syntax-highlighted diff pane for the selected commit, via `UltraCanvasSyntaxTokenizer` | P3 |

### 5.6 Interaction

| # | Feature | Phase |
|---|---|---|
| I1 | `onCommitClick` / `onCommitDoubleClick` / `onCommitRightClick` / `onCommitHover` / `onRefClick` callbacks | P1 |
| I2 | Tooltip: full sha, author + committer, both dates, subject/body, refs, changed files | P1 |
| I3 | Zoom (wheel), pan (drag), `ZoomIn`/`ZoomOut`/`ZoomToFit`/`ResetView` | P1 |
| I4 | Keyboard navigation: row up/down, lane left/right, `Home`/`End`, `PgUp`/`PgDn`, `Enter` to activate | P1 |
| I5 | `CenterOnCommit(sha)` / `ScrollToRef(name)` / `SelectCommit(sha)` | P1 |
| I6 | Hover highlights the commit's ancestry (or descendants) path through the graph | P2 |
| I7 | Multi-select (ctrl/shift) and two-commit range selection for a diff | P2 |
| I8 | Search box with match highlighting and next/previous jump (sha, subject, author) | P2 |
| I9 | Context-menu **hooks** — checkout, branch, tag, revert, cherry-pick, reset. The element raises callbacks; **it never executes git** | P2 |
| I10 | Follow-`HEAD` auto-scroll when commits are appended | P2 |
| I11 | Copy sha / subject to clipboard via `UltraCanvasClipboard` | P2 |
| I12 | Drag-to-author branches and merges in synthetic mode (teaching diagrams) | P3 |

### 5.7 Style & theme

| # | Feature | Phase |
|---|---|---|
| S1 | `GitGraphStyle` struct with `GetStyle()`/`SetStyle()` — the `GourceStyle` pattern | P1 |
| S2 | Themes: `Default`, `Dark`, `Light`, `Colorful`, `Monochrome`, `Custom` | P1 |
| S3 | Cyclic lane palette (mermaid's `git0`…`git7` equivalent), user-replaceable | P1 |
| S4 | Geometry knobs: lane spacing, row height, node radius, line width, corner radius, chip padding | P1 |
| S5 | Per-commit and per-branch colour overrides | P1 |
| S6 | Background, plot area, grid, stripe, hover and selection colours | P1 |
| S7 | Fonts: family and size for commit labels, chips and file boxes | P1 |
| S8 | Deterministic per-branch colour from a name hash, so a branch keeps its colour across sessions | P2 |
| S9 | Dashed/dotted styles for cherry-pick, revert and skipped edges | P2 |
| S10 | Colour-blind-safe default palette, reinforced by node **shape** coding | P2 |

### 5.8 Performance & scale

| # | Feature | Phase |
|---|---|---|
| P1 | 100k+ commits: layout `O(n · lanes)`, render `O(visible rows)` | P1 |
| P2 | Cached text measurement and chip geometry | P1 |
| P3 | Cached `rowTops` prefix-sum for variable-height rows (the `UltraCanvasListView` approach) | P1 |
| P4 | Chunked lazy loading from `IGitGraphDataSource` as the user scrolls | P2 |
| P5 | Dirty-rect-aware repaint via `UltraCanvasDirtyRectManager` | P2 |
| P6 | Bounded memory mode — keep only a window of commits resident | P3 |

### 5.9 Export & integration

| # | Feature | Phase |
|---|---|---|
| X1 | `SaveToSVG` (matching `UltraCanvasGourceTree` / `UltraCanvasSankey`) | P1 |
| X2 | JSON export of the laid-out geometry (commits, edges, lanes) for tooling and golden tests | P2 |
| X3 | PNG export via the framework's surface capture | P2 |
| X4 | Mermaid `gitGraph` text export | P3 |
| X5 | Copy the graph region to the clipboard | P3 |

### 5.10 Testing

| # | Feature | Phase |
|---|---|---|
| T1 | `Tests/GitGraphLayoutTest.cpp` — headless, no window, in the style of `ContourGeometryTest.cpp` | P1 |
| T2 | Golden cases: octopus merge, criss-cross merge, multiple roots, orphan branch, 1000-lane fan-out | P1 |
| T3 | Invariants under a randomised DAG generator: no two commits share `(row, lane)`; every edge is monotone in row order; no lane is leaked; every commit is placed exactly once | P2 |
| T4 | Swimlane-mode geometry test reproducing the reference image's structure | P2 |

---

## 6. Proposed API sketch

Naming and shape follow `UltraCanvasGourceTree` and `UltraCanvasNodeDiagram`:

```cpp
enum class GitGraphOrientation  { LeftToRight, TopToBottom, BottomToTop, RightToLeft };
enum class GitGraphLayoutMode   { Lanes, Swimlane, TimeProportional };
enum class GitGraphLaneStrategy { Compact, Stable };
enum class GitGraphOrderMode    { AsGiven, CommitDate, AuthorDate, Topological };
enum class GitGraphEdgeStyle    { Straight, Orthogonal, Bezier, Arc };
enum class GitGraphNodeShape    { Circle, Ring, Square, Diamond };
enum class GitGraphCommitType   { Normal, Reverse, Highlight };
enum class GitGraphRefType      { LocalBranch, RemoteBranch, Tag, Head, Stash };
enum class GitGraphTheme        { Default, Dark, Light, Colorful, Monochrome, Custom };

struct GitGraphFileChange { std::string path; int added = 0, removed = 0; char status = 'M'; };

struct GitGraphCommit {
    std::string sha, shortSha, subject, body;
    std::string authorName, authorEmail, committerName;
    int64_t     authorDate = 0, commitDate = 0;      // Unix seconds
    std::vector<std::string> parents;
    std::vector<GitGraphFileChange> files;
    GitGraphCommitType type = GitGraphCommitType::Normal;
    bool  hasColorOverride = false;
    Color colorOverride;
};

struct GitGraphRef { std::string name, sha, remote; GitGraphRefType type = GitGraphRefType::LocalBranch; };

class UltraCanvasGitGraph : public UltraCanvasUIElement {
public:
    UltraCanvasGitGraph(const std::string& id, long x, long y, long w, long h);
    bool AcceptsFocus() const override { return true; }

    // Data
    void AddCommit(const GitGraphCommit& commit);
    void AddCommits(const std::vector<GitGraphCommit>& commits);
    void AppendCommits(const std::vector<GitGraphCommit>& commits);   // incremental
    void AddRef(const GitGraphRef& ref);
    void SetHead(const std::string& refNameOrSha);
    void Clear();
    bool LoadFromGitLog(const std::string& text);                     // D5
    void SetDataSource(std::shared_ptr<IGitGraphDataSource> source);  // D8

    // Authoring (D6)
    std::string Commit(const std::string& subject, const std::string& tag = "");
    void Branch(const std::string& name);
    void Checkout(const std::string& name);
    std::string Merge(const std::string& fromBranch, const std::string& subject = "");
    std::string CherryPick(const std::string& sha);
    void Tag(const std::string& name, const std::string& sha = "");

    // Layout
    void SetOrientation(GitGraphOrientation o);
    void SetLayoutMode(GitGraphLayoutMode m);
    void SetLaneStrategy(GitGraphLaneStrategy s);
    void SetOrderMode(GitGraphOrderMode m);
    void SetTrunkBranch(const std::string& name);      // L4 / L5
    void PerformLayout();

    // Query
    const GitGraphCommit* GetCommit(const std::string& sha) const;
    int  GetRowCount() const;  int GetRowTop(int row) const;  int GetRowHeight(int row) const;   // M1
    std::vector<std::string> GetSelectedCommits() const;

    // Style / view — mirrors GourceTree
    void SetTheme(GitGraphTheme theme);
    GitGraphStyle& GetStyle();   void SetStyle(const GitGraphStyle& s);
    void ZoomIn(); void ZoomOut(); void ZoomToFit(); void ResetView();
    void CenterOnCommit(const std::string& sha);
    void ScrollToRef(const std::string& refName);
    bool SaveToSVG(const std::string& filePath);

    // Callbacks
    std::function<void(const std::string&)> onCommitClick, onCommitDoubleClick,
                                            onCommitRightClick, onCommitHover, onCommitSelect;
    std::function<void(const GitGraphRef&)> onRefClick;
    std::function<void()>                   onLayoutComplete;
};

std::shared_ptr<UltraCanvasGitGraph> CreateGitGraph(const std::string& id,
                                                    long x, long y, long w, long h);
```

---

## 7. Suggested delivery phases

**Phase 1 — core element.** Data model, active-lane sweep with both strategies,
`Lanes` + `Swimlane` layout modes, all four orientations, node/edge/chip
rendering, file-box column, virtualised rows, style + themes, zoom/pan,
selection, tooltips, keyboard nav, `SaveToSVG`, `git log` text ingest, authoring
API, headless layout tests, demo tab and docs. **This delivers the reference
image and a usable repository browser.**

**Phase 2 — repository-browser completeness.** `IGitGraphDataSource` with lazy
chunked loading, incremental append, filtering and collapsing, time-proportional
mode, crossing reduction, cherry-pick/revert/uncommitted nodes, paired commit
table with synchronised scrolling, search, context-menu hooks, mermaid import,
JSON/PNG export, colour-blind palette.

**Phase 3 — advanced.** Native `.git` reader (miniz-backed), minimap, diff pane,
avatars, animated transitions, background-thread layout, mermaid export,
drag-to-author.

---

## 8. Open questions (with recommendations)

**Q1 — Base class: `UltraCanvasUIElement` or `UltraCanvasChartElementBase`?**
*Recommendation: `UltraCanvasUIElement`*, as `UltraCanvasGourceTree` and
`UltraCanvasNodeDiagram` do. The chart base is built around numeric axes, data
bounds and a plot-area transform that a commit graph does not have (the
`TimeProportional` mode is the only partial exception, and it needs one axis,
not two).

**Q2 — Which plugin directory?**
*Recommendation: `Plugins/Diagrams`.* That is where the other DAG-shaped
diagrams live, it already has the CMake wiring, and the palette/label helpers it
needs are adjacent. `Plugins/Graphs` currently holds only the legacy
`UltraCanvasTimeline` (not a `UltraCanvasUIElement` subclass) and is not wired
into the library target.

**Q3 — Does the framework read `.git` itself?**
*Recommendation: not in v1.* Ship the programmatic API plus the `git log` text
parser, so the element stays render-only and adds **no new third-party
dependency** (no libgit2 — which would also mean updating
`Docs/Dependencies.md`, `master_dependencies.yaml` and
`THIRD_PARTY_LICENSES.md`). If a native reader is wanted later, the vendored
miniz already provides inflate for loose objects and packfiles; it belongs in a
separate `UltraCanvasGitRepository` unit under `core/`, not inside the widget.

**Q4 — Commit table: inside the element or a paired `UltraCanvasTableView`?**
*Recommendation: paired, with the row-alignment API (M1) exposed.* Reusing the
existing table avoids reimplementing columns, sorting and cell rendering; an
optional built-in pane can be layered on top in Phase 2 for convenience.

**Q5 — Can the element mutate a repository?**
*Recommendation: never.* The element is read-only and raises callbacks
(`onCommitRightClick`, context-menu hooks). Executing checkout/reset/rebase is
the application's responsibility — this keeps the widget free of process
spawning and of any destructive behaviour.

**Q6 — Default lane strategy?**
*Recommendation: `Stable`* (straight branches, GitKraken-like) as the default,
because it is the more legible of the two for people who are not already fluent
in `git log --graph`, with `Compact` one call away for dense histories.

---

## 9. Sources consulted

- [pvigier — Commit Graph Drawing Algorithms](https://pvigier.github.io/2019/05/06/commit-graph-drawing-algorithms.html)
  and its [Hacker News discussion](https://news.ycombinator.com/item?id=21079643)
  — comparison of GitKraken (straight lines), Git Extensions / SmartGit (curved),
  gitk and `git log --graph` (compact, reordered).
- [indigane/git-graph-drawing](https://github.com/indigane/git-graph-drawing)
  — catalogue of implementations (serie, tig, qgit, gitk, VS Code *Git Graph*,
  IntelliJ, GitKraken, Tower, gitgraph.js, learnGitBranching, Jujutsu's
  `renderdag`) and their line-by-line vs lane-based approaches.
- [Mermaid — GitGraph Diagrams syntax](https://github.com/mermaid-js/mermaid/blob/develop/docs/syntax/gitgraph.md)
  and [mermaid.js.org/syntax/gitgraph](https://mermaid.js.org/syntax/gitgraph.html)
  — commit types (`NORMAL`/`REVERSE`/`HIGHLIGHT`), tags, `branch`/`checkout`/
  `merge`/`cherry-pick`, `LR`/`TB`/`BT` orientation, `showBranches`,
  `showCommitLabel`, `rotateCommitLabel`, `parallelCommits`, `mainBranchName`,
  `mainBranchOrder`, and the `git0`…`git7` branch-colour theme variables.
- [Mermaid Studio — Git Graph](https://mermaidstudio.dev/docs/diagram-types/gitgraph/)
  and [Mermaid gitGraph cherry-pick semantics](https://github.com/mermaid-js/mermaid/issues/3496)
  — cherry-pick of merge commits requires an explicit immediate parent.
- [erikbrinkman/d3-dag](https://github.com/erikbrinkman/d3-dag) — layering and
  coordinate-assignment strategies for DAGs, including crossing minimisation
  (useful reference for L10; full ILP is out of scope here).
- [The Git Graph: A Directed Acyclic Graph](https://medium.com/@a.kago1988/why-the-git-graph-is-a-directed-acyclic-graph-dag-f9052b95f97f)
  and [Visualising changes to the Git DAG in real time](https://www.eseth.org/2023/git-graph-dag.html)
  — DAG semantics and refs-as-labels model.
- Uploaded reference image — "GIT Branch and its Operations" swimlane diagram
  (§2).
