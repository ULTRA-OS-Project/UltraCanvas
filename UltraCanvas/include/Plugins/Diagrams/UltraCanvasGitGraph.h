// include/Plugins/Diagrams/UltraCanvasGitGraph.h
// Git commit-graph element: renders a repository history (or an authored
// teaching diagram) as a lane graph or a git-flow style swimlane diagram.
//
// Two layout families share one data model:
//   Lanes    - one lane per open line of development, newest commit first.
//              The gitk / GitKraken / SourceTree repository-browser view.
//   Swimlane - one band per branch either side of a trunk, time flowing along
//              the axis. The git-flow teaching-diagram view.
//
// Namespace: UltraCanvas
// Base class: UltraCanvasUIElement
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasUIElement.h"
#include "Plugins/Diagrams/UltraCanvasGitGraphLayout.h"
#include "Plugins/Diagrams/UltraCanvasGitGraphTypes.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace UltraCanvas {

class UltraCanvasGitGraph : public UltraCanvasUIElement {
public:
    UltraCanvasGitGraph(const std::string& id, float x, float y, float w, float h);

    bool AcceptsFocus() const override { return true; }

    // ===== DATA =====

    void AddCommit(const GitGraphCommit& commit);
    void AddCommits(const std::vector<GitGraphCommit>& newCommits);
    void AppendCommits(const std::vector<GitGraphCommit>& newCommits);   // Older history
    void AddRef(const GitGraphRef& ref);
    void AddBranchRef(const std::string& name, const std::string& sha, bool isCurrent = false);
    void AddTagRef(const std::string& name, const std::string& sha, bool annotated = false);
    void AddRemoteRef(const std::string& remote, const std::string& name, const std::string& sha);
    void SetHead(const std::string& refNameOrSha);
    void AddAnnotation(const GitGraphAnnotation& annotation);
    void AddAnnotation(const std::string& sha, const std::string& text);
    void Clear();

    // Parse `git log` output produced with GitGraphGitLogFormat(). Returns the
    // number of commits read; existing content is replaced.
    size_t LoadFromGitLog(const std::string& text);

    // The --pretty format LoadFromGitLog() expects.
    static std::string GitLogFormat();

    // ===== MERMAID =====

    // Replace the content with a Mermaid `gitGraph` block. Returns the number
    // of commits read, or 0 on a parse error (see GetLastError()).
    size_t LoadFromMermaid(const std::string& text);
    std::string ToMermaidText() const;
    bool SaveToMermaid(const std::string& filePath) const;
    const std::string& GetLastError() const { return lastError; }

    // ===== LAZY LOADING =====

    // Attach a pull source. The first chunk is fetched immediately; further
    // chunks arrive as the viewport approaches the end of the loaded history.
    void   SetDataSource(std::shared_ptr<IGitGraphDataSource> source, size_t chunkSize = 500);
    void   SetChunkSize(size_t commitsPerChunk);
    size_t GetChunkSize() const { return chunkSize; }
    bool   HasMoreCommits() const { return dataSource && !dataSourceExhausted; }
    bool   LoadMoreCommits();               // Fetch one more chunk on demand
    void   SetPrefetchRows(int rows);       // How close to the end triggers a fetch
    std::function<void(size_t, size_t)> onChunkLoaded;   // (loaded, total or 0)

    const std::vector<GitGraphCommit>& GetCommits() const { return commits; }
    const std::vector<GitGraphRef>&    GetRefs()    const { return refs; }
    const GitGraphCommit* GetCommit(const std::string& sha) const;
    size_t GetCommitCount() const { return commits.size(); }
    std::vector<GitGraphRef> GetRefsForCommit(const std::string& sha) const;

    // ===== AUTHORING (teaching diagrams) =====
    // Commits are appended oldest-first and carry their branch, so Swimlane
    // mode can band them without a first-parent walk.

    std::string Commit(const std::string& subject, const std::string& tag = "");
    void        Branch(const std::string& name);          // Create + check out
    void        Checkout(const std::string& name);
    std::string Merge(const std::string& fromBranch, const std::string& subject = "");
    std::string CherryPick(const std::string& sha, const std::string& subject = "");
    void        Tag(const std::string& name, const std::string& sha = "");
    std::string GetCurrentBranch() const { return currentBranch; }

    // ===== LAYOUT =====

    void SetOrientation(GitGraphOrientation newOrientation);
    GitGraphOrientation GetOrientation() const { return orientation; }

    void SetLayoutMode(GitGraphLayoutMode mode);
    GitGraphLayoutMode GetLayoutMode() const { return layoutOptions.layoutMode; }

    void SetLaneStrategy(GitGraphLaneStrategy strategy);
    GitGraphLaneStrategy GetLaneStrategy() const { return layoutOptions.laneStrategy; }

    void SetOrderMode(GitGraphOrderMode mode);
    GitGraphOrderMode GetOrderMode() const { return layoutOptions.orderMode; }

    void SetTrunkBranch(const std::string& name);
    const std::string& GetTrunkBranch() const { return layoutOptions.trunkBranch; }

    void SetSwimlaneOrder(const std::vector<std::string>& branchNames);
    void SetSwimlanesBothSides(bool bothSides);

    // Whether row 0 holds the newest commit (git log order, the default) or the
    // oldest (the authoring API). Controls which end of the axis time starts at.
    void SetRowsAreNewestFirst(bool newestFirst);
    bool GetRowsAreNewestFirst() const { return rowsAreNewestFirst; }

    void PerformLayout();
    const GitGraphLayoutResult& GetLayoutResult() const { return layout; }
    int  GetRowCount() const { return layout.rowCount; }

    // ===== STYLE =====

    void SetTheme(GitGraphTheme theme);
    GitGraphTheme GetTheme() const { return currentTheme; }

    GitGraphStyle&       GetStyle()       { return style; }
    const GitGraphStyle& GetStyle() const { return style; }
    void SetStyle(const GitGraphStyle& newStyle);

    void SetShowCommitLabels(bool show);
    void SetShowFileBoxes(bool show);
    void SetShowBranchChips(bool show);
    void SetEdgeStyle(GitGraphEdgeStyle edgeStyle);
    void SetLaneColor(int lane, const Color& color);
    Color GetLaneColor(int lane) const;
    Color GetBranchColor(const std::string& branchName) const;

    // ===== FILTERING =====

    // Commits that fail the filter leave the graph; edges that ran through them
    // are re-pointed at the nearest surviving ancestor and drawn dashed.
    void SetFilter(const GitGraphFilter& filter);
    const GitGraphFilter& GetFilter() const { return filter; }
    void ClearFilter();
    size_t GetFilteredOutCount() const { return layout.hiddenCount; }
    size_t GetVisibleCommitCount() const { return layout.commits.size(); }

    void SetReduceCrossings(bool enabled);
    bool GetReduceCrossings() const { return layoutOptions.reduceCrossings; }
    size_t CountEdgeCrossings() const { return layout.CountCrossings(); }

    // ===== SEARCH =====

    // Matches sha, subject, author and ref names. Returns the match count and
    // moves to the first match.
    size_t Search(const std::string& query, bool caseSensitive = false);
    void   ClearSearch();
    const std::vector<std::string>& GetSearchMatches() const { return searchMatches; }
    bool   FindNext();
    bool   FindPrevious();
    std::string GetCurrentSearchMatch() const;
    std::function<void(size_t, size_t)> onSearchChanged;   // (currentIndex, total)

    // ===== TABLE PANE =====

    // Row-aligned commit table beside the graph (vertical orientations only).
    void SetShowTable(bool show);
    bool GetShowTable() const { return style.showTable; }
    void SetTableColumns(const std::vector<GitGraphTableColumnSpec>& columns);
    const std::vector<GitGraphTableColumnSpec>& GetTableColumns() const { return tableColumns; }
    void SetGraphPaneWidth(double width);
    void SetDateFormatter(std::function<std::string(int64_t)> formatter);

    // Row alignment, so an external UltraCanvasTableView can be paired instead.
    double GetRowScreenPosition(int row) const;    // Along-axis position in element space
    double GetRowSpacing() const { return style.rowSpacing * zoomLevel; }
    int    GetRowAtScreenPosition(double position) const;
    std::pair<int, int> GetVisibleRowRange() const;

    // ===== TIME AXIS =====

    // Rows (equal slices) or TimeProportional (position follows the commit
    // date, with quiet periods compressed between the min/max row spacing).
    void SetAxisMode(GitGraphAxisMode mode);
    GitGraphAxisMode GetAxisMode() const { return style.axisMode; }
    void SetTimeRowSpacingRange(double minimum, double maximum);
    void SetShowDateRuler(bool show);

    // ===== COLLAPSING =====

    void SetCollapseLinearRuns(bool enabled, int minimumRun = 8);
    bool GetCollapseLinearRuns() const { return layoutOptions.collapseLinearRuns; }
    void ExpandRun(const std::string& sha);        // Unfold one placeholder
    void CollapseAllRuns();                        // Drop every manual expansion
    int  GetCollapsedCount(const std::string& sha) const;

    // ===== PARALLEL ROWS =====

    void SetParallelCommits(bool enabled, int64_t toleranceSeconds = 0);
    bool GetParallelCommits() const { return layoutOptions.parallelCommits; }

    // ===== MINIMAP =====

    void SetShowMinimap(bool show);
    bool GetShowMinimap() const { return style.showMinimap; }
    void SetMinimapPosition(GitGraphMinimapPosition position);

    // ===== SELECTION =====

    void SelectCommit(const std::string& sha, bool addToSelection = false);
    void DeselectAll();
    bool IsCommitSelected(const std::string& sha) const;
    std::vector<std::string> GetSelectedCommits() const { return selectedShas; }

    // ===== VIEW =====

    void  SetZoom(float zoom);
    float GetZoom() const { return zoomLevel; }
    void  ZoomIn();
    void  ZoomOut();
    void  ZoomToFit(double padding = 24.0);
    void  ResetView();
    void  SetPan(double x, double y);
    Point2Dd GetPan() const { return Point2Dd(panX, panY); }
    void  CenterOnCommit(const std::string& sha);
    void  ScrollToRef(const std::string& refName);

    // ===== EXPORT =====

    bool SaveToSVG(const std::string& filePath);

    // Laid-out geometry (commits, edges, lanes, refs) as JSON, for tooling and
    // golden tests.
    std::string ToJSON() const;
    bool SaveToJSON(const std::string& filePath) const;

    // ===== RENDERING / EVENTS =====

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

    // ===== CALLBACKS =====

    std::function<void(const std::string&)> onCommitClick;
    std::function<void(const std::string&)> onCommitDoubleClick;
    std::function<void(const std::string&)> onCommitRightClick;
    std::function<void(const std::string&)> onCommitHover;
    std::function<void(const std::string&)> onCommitSelect;
    std::function<void(const GitGraphRef&)> onRefClick;
    std::function<void()>                   onLayoutComplete;

    // Overrides the default "{shortSha} {subject}" commit label.
    std::function<std::string(const GitGraphCommit&)> commitLabelFormatter;

private:
    // ===== DATA =====
    std::shared_ptr<IGitGraphDataSource> dataSource;
    bool   dataSourceExhausted = false;
    size_t chunkSize    = 500;
    int    prefetchRows = 40;
    mutable std::string lastError;

    std::vector<GitGraphCommit>    commits;
    std::vector<GitGraphRef>       refs;
    std::vector<GitGraphAnnotation> annotations;
    std::unordered_map<std::string, size_t> commitIndex;

    // ===== LAYOUT =====
    GitGraphLayoutOptions   layoutOptions;
    GitGraphLayoutResult    layout;
    UltraCanvasGitGraphLayout layoutEngine;
    GitGraphOrientation     orientation = GitGraphOrientation::BottomToTop;
    bool rowsAreNewestFirst = true;
    bool needsLayout        = true;
    double crossOrigin      = 0.0;      // Cross-axis position of lane 0
    double contentAlongAxis = 0.0;      // Extent of the laid-out content
    double contentAcrossAxis = 0.0;

    // ===== STYLE =====
    GitGraphStyle style;
    GitGraphTheme currentTheme = GitGraphTheme::Default;
    std::unordered_map<int, Color> laneColorOverrides;

    // ===== VIEW =====
    float  zoomLevel = 1.0f;
    double panX = 0.0, panY = 0.0;
    float  minZoom = 0.15f, maxZoom = 6.0f;

    // ===== FILTER / SEARCH / TABLE / MINIMAP =====
    GitGraphFilter filter;
    std::vector<std::string> searchMatches;
    size_t      searchIndex = 0;
    std::string searchQuery;
    bool        searchCaseSensitive = false;
    std::vector<GitGraphTableColumnSpec> tableColumns;
    std::function<std::string(int64_t)>  dateFormatter;
    Rect2Dd     minimapBounds;          // Element-space, updated each render
    bool        isDraggingMinimap = false;

    // Axis positions per row index (TimeProportional mode only; empty in Rows
    // mode, where spacing is uniform).
    std::vector<double> rowAxisOffsets;

    // Label rectangles already drawn this frame, for collision avoidance.
    mutable std::vector<Rect2Dd> occupiedLabelRects;

    // ===== INTERACTION =====
    std::string hoveredSha;
    std::vector<std::string> selectedShas;
    bool     isPanning = false;
    Point2Di dragStartPos;
    Point2Dd panStartOffset;

    // ===== AUTHORING STATE =====
    std::string currentBranch = "main";
    std::unordered_map<std::string, std::string> branchTips;   // branch -> sha
    int syntheticShaCounter = 0;

    // ===== GEOMETRY HELPERS =====
    bool     IsHorizontalAxis() const;
    bool     IsAxisReversed() const;
    double   AxisCoord(int row) const;
    double   CrossCoord(int lane) const;
    Point2Dd NodePoint(int row, int lane) const;
    Point2Dd MakePoint(double alongAxis, double acrossAxis) const;
    double   AxisOf(const Point2Dd& p) const;
    double   CrossOf(const Point2Dd& p) const;
    double   NodeRadiusFor(const GitGraphPlacedCommit& placed) const;
    int      LabelSideSign(int lane) const;
    void     UpdateContentExtent();
    void     ComputeVisibleRows(int& firstRow, int& lastRow) const;
    Point2Dd ScreenToWorld(const Point2Di& screenPos) const;
    Point2Dd WorldToScreen(const Point2Dd& worldPos) const;
    std::string FindCommitAt(const Point2Dd& worldPos) const;
    std::string CommitLabelText(const GitGraphCommit& commit) const;
    Color    EdgeColor(const GitGraphPlacedEdge& edge) const;
    Color    CommitColor(const GitGraphPlacedCommit& placed) const;

    // ===== DRAWING =====
    void DrawBackground(IRenderContext* ctx);
    void DrawSwimlaneBands(IRenderContext* ctx);
    void DrawSwimlaneLabels(IRenderContext* ctx);
    void DrawTrunkBaseline(IRenderContext* ctx);
    void DrawEdges(IRenderContext* ctx, int firstRow, int lastRow);
    void DrawEdge(IRenderContext* ctx, const GitGraphPlacedEdge& edge);
    void DrawNodes(IRenderContext* ctx, int firstRow, int lastRow);
    void DrawNode(IRenderContext* ctx, const GitGraphPlacedCommit& placed);
    void DrawNodeShape(IRenderContext* ctx, const Point2Dd& center, double radius,
                       GitGraphNodeShape shape, const Color& color, bool filled);
    void DrawDecorations(IRenderContext* ctx, int firstRow, int lastRow);
    void DrawRefChips(IRenderContext* ctx, const GitGraphPlacedCommit& placed,
                      const std::vector<GitGraphRef>& commitRefs, double& cursorAcross);
    void DrawCommitLabel(IRenderContext* ctx, const GitGraphPlacedCommit& placed,
                         const GitGraphCommit& commit, double cursorAcross);
    void DrawFileBoxes(IRenderContext* ctx, const GitGraphPlacedCommit& placed,
                       const GitGraphCommit& commit);
    void DrawAnnotations(IRenderContext* ctx);
    void DrawTooltip(IRenderContext* ctx);
    void DrawTablePane(IRenderContext* ctx, int firstRow, int lastRow);
    void DrawDateRuler(IRenderContext* ctx, int firstRow, int lastRow);
    void DrawBadges(IRenderContext* ctx, const GitGraphPlacedCommit& placed,
                    const GitGraphCommit& commit, double& cursorAcross);
    void DrawCollapsedNode(IRenderContext* ctx, const GitGraphPlacedCommit& placed);
    void DrawAvatar(IRenderContext* ctx, const GitGraphCommit& commit,
                    const Rect2Dd& bounds);
    bool ReserveLabelRect(const Rect2Dd& rect) const;
    void RebuildTimeAxis();
    void DrawMinimap(IRenderContext* ctx);
    std::string TableCellText(const GitGraphCommit& commit, GitGraphTableColumn column) const;
    bool     IsTableActive() const;
    double   GraphPaneWidth() const;
    Rect2Dd  ComputeMinimapBounds() const;
    bool     HandleMinimapPointer(const Point2Di& position);
    void     ApplyFilter();
    bool     MatchesFilter(const GitGraphCommit& commit) const;
    bool     IsSearchMatch(const std::string& sha) const;
    void DrawChip(IRenderContext* ctx, const Point2Dd& topLeft, const std::string& text,
                  const Color& fill, const Color& border, const Color& textColor,
                  double& outWidth);
    void BuildEdgeGeometry(const GitGraphPlacedEdge& edge,
                           Point2Dd& start, Point2Dd& corner, Point2Dd& end) const;

    // ===== EVENTS =====
    bool HandleMouseMove(const UCEvent& event);
    bool HandleMouseDown(const UCEvent& event);
    bool HandleMouseUp(const UCEvent& event);
    bool HandleMouseWheel(const UCEvent& event);
    bool HandleDoubleClick(const UCEvent& event);
    bool HandleKeyDown(const UCEvent& event);

    // ===== HELPERS =====
    void RebuildCommitIndex();
    void ApplyThemeColors(GitGraphTheme theme);
    std::string MakeSyntheticSha();
    void SetBranchTip(const std::string& branch, const std::string& sha);
    std::string FormatTimestamp(int64_t timestamp) const;
};

// ===== FACTORY =====

inline std::shared_ptr<UltraCanvasGitGraph> CreateGitGraph(
        const std::string& id, float x, float y, float w, float h) {
    return std::make_shared<UltraCanvasGitGraph>(id, x, y, w, h);
}

} // namespace UltraCanvas
