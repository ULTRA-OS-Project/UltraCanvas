// Plugins/Diagrams/UltraCanvasGitGraph.cpp
// Git commit-graph element implementation.
//
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasGitGraph.h"
#include "Plugins/Diagrams/UltraCanvasGitGraphMermaid.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace UltraCanvas {

namespace {

constexpr double kPi = 3.14159265358979323846;

double Sign(double v) { return (v > 0.0) ? 1.0 : ((v < 0.0) ? -1.0 : 0.0); }

// UltraCanvasUtils.h declares a Trim() that has no definition anywhere in the
// tree, so the parser carries its own.
std::string TrimField(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// Unlike UltraCanvas::Split, empty fields are kept: `git log`'s %D expands to
// nothing on an undecorated commit and the later fields must not shift up.
std::vector<std::string> SplitFields(const std::string& line, char delimiter) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : line) {
        if (c == delimiter) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

// Blend two colours; t = 0 gives a, t = 1 gives b.
Color Blend(const Color& a, const Color& b, double t) {
    const double clamped = std::clamp(t, 0.0, 1.0);
    return Color(
        static_cast<uint8_t>(a.r + (b.r - a.r) * clamped),
        static_cast<uint8_t>(a.g + (b.g - a.g) * clamped),
        static_cast<uint8_t>(a.b + (b.b - a.b) * clamped),
        static_cast<uint8_t>(a.a + (b.a - a.a) * clamped));
}

} // namespace

// =============================================================================
// CONSTRUCTION
// =============================================================================

UltraCanvasGitGraph::UltraCanvasGitGraph(const std::string& id,
                                         float x, float y, float w, float h)
    : UltraCanvasUIElement(id, x, y, w, h) {
    ApplyThemeColors(GitGraphTheme::Default);
    layoutEngine.SetOptions(layoutOptions);
}

// =============================================================================
// DATA
// =============================================================================

void UltraCanvasGitGraph::AddCommit(const GitGraphCommit& commit) {
    auto existing = commitIndex.find(commit.sha);
    if (existing != commitIndex.end()) {
        commits[existing->second] = commit;
    } else {
        commitIndex[commit.sha] = commits.size();
        commits.push_back(commit);
    }
    needsLayout = true;
}

void UltraCanvasGitGraph::AddCommits(const std::vector<GitGraphCommit>& newCommits) {
    for (const GitGraphCommit& commit : newCommits) AddCommit(commit);
}

void UltraCanvasGitGraph::AppendCommits(const std::vector<GitGraphCommit>& newCommits) {
    AddCommits(newCommits);
}

void UltraCanvasGitGraph::AddRef(const GitGraphRef& ref) {
    for (GitGraphRef& existing : refs) {
        if (existing.name == ref.name && existing.type == ref.type &&
            existing.remote == ref.remote) {
            existing = ref;
            needsLayout = true;
            return;
        }
    }
    refs.push_back(ref);
    needsLayout = true;
}

void UltraCanvasGitGraph::AddBranchRef(const std::string& name, const std::string& sha,
                                       bool isCurrent) {
    GitGraphRef ref;
    ref.name      = name;
    ref.sha       = sha;
    ref.type      = GitGraphRefType::LocalBranch;
    ref.isCurrent = isCurrent;
    AddRef(ref);
}

void UltraCanvasGitGraph::AddTagRef(const std::string& name, const std::string& sha,
                                    bool annotated) {
    GitGraphRef ref;
    ref.name      = name;
    ref.sha       = sha;
    ref.type      = GitGraphRefType::Tag;
    ref.annotated = annotated;
    AddRef(ref);
}

void UltraCanvasGitGraph::AddRemoteRef(const std::string& remote, const std::string& name,
                                       const std::string& sha) {
    GitGraphRef ref;
    ref.name   = name;
    ref.sha    = sha;
    ref.remote = remote;
    ref.type   = GitGraphRefType::RemoteBranch;
    AddRef(ref);
}

void UltraCanvasGitGraph::SetHead(const std::string& refNameOrSha) {
    for (GitGraphRef& ref : refs) {
        ref.isCurrent = (ref.name == refNameOrSha || ref.sha == refNameOrSha);
    }
    needsLayout = true;
}

void UltraCanvasGitGraph::AddAnnotation(const GitGraphAnnotation& annotation) {
    annotations.push_back(annotation);
    RequestRedraw();
}

void UltraCanvasGitGraph::AddAnnotation(const std::string& sha, const std::string& text) {
    GitGraphAnnotation annotation;
    annotation.sha  = sha;
    annotation.text = text;
    AddAnnotation(annotation);
}

void UltraCanvasGitGraph::Clear() {
    commits.clear();
    refs.clear();
    annotations.clear();
    commitIndex.clear();
    layout.Clear();
    selectedShas.clear();
    hoveredSha.clear();
    branchTips.clear();
    currentBranch = layoutOptions.trunkBranch.empty() ? "main" : layoutOptions.trunkBranch;
    syntheticShaCounter = 0;
    dataSourceExhausted = false;
    needsLayout = true;
    RequestRedraw();
}

const GitGraphCommit* UltraCanvasGitGraph::GetCommit(const std::string& sha) const {
    auto it = commitIndex.find(sha);
    return it == commitIndex.end() ? nullptr : &commits[it->second];
}

std::vector<GitGraphRef> UltraCanvasGitGraph::GetRefsForCommit(const std::string& sha) const {
    std::vector<GitGraphRef> found;
    for (const GitGraphRef& ref : refs) {
        if (ref.sha == sha) found.push_back(ref);
    }
    // HEAD first, then branches, then tags - the order every client uses.
    std::stable_sort(found.begin(), found.end(),
                     [](const GitGraphRef& a, const GitGraphRef& b) {
                         auto rank = [](const GitGraphRef& r) {
                             if (r.isCurrent) return 0;
                             switch (r.type) {
                                 case GitGraphRefType::LocalBranch:  return 1;
                                 case GitGraphRefType::RemoteBranch: return 2;
                                 case GitGraphRefType::Tag:          return 3;
                                 default:                            return 4;
                             }
                         };
                         return rank(a) < rank(b);
                     });
    return found;
}

void UltraCanvasGitGraph::RebuildCommitIndex() {
    commitIndex.clear();
    for (size_t i = 0; i < commits.size(); ++i) commitIndex[commits[i].sha] = i;
}

// =============================================================================
// GIT LOG INGEST
// =============================================================================

std::string UltraCanvasGitGraph::GitLogFormat() {
    // Field order matches the parser below. Use with:
    //   git log --all --pretty=format:<this>
    return "%H\x1f%P\x1f%an\x1f%ae\x1f%at\x1f%ct\x1f%D\x1f%s";
}

size_t UltraCanvasGitGraph::LoadFromGitLog(const std::string& text) {
    Clear();

    std::istringstream stream(text);
    std::string line;
    size_t count = 0;

    while (std::getline(stream, line)) {
        if (TrimField(line).empty()) continue;

        // Accept the unit-separator form produced by GitLogFormat() and a plain
        // '|' separated form, which is friendlier to hand-written fixtures.
        const char delimiter = (line.find('\x1f') != std::string::npos) ? '\x1f' : '|';
        std::vector<std::string> fields = SplitFields(line, delimiter);
        if (fields.size() < 8) continue;

        GitGraphCommit commit;
        commit.sha         = TrimField(fields[0]);
        commit.authorName  = TrimField(fields[2]);
        commit.authorEmail = TrimField(fields[3]);
        commit.authorDate  = std::strtoll(TrimField(fields[4]).c_str(), nullptr, 10);
        commit.commitDate  = std::strtoll(TrimField(fields[5]).c_str(), nullptr, 10);
        commit.subject     = TrimField(fields[7]);
        commit.committerName = commit.authorName;

        std::istringstream parents(fields[1]);
        std::string parent;
        while (parents >> parent) commit.parents.push_back(parent);

        if (commit.sha.empty()) continue;
        AddCommit(commit);
        ++count;

        // %D expands to "HEAD -> main, origin/main, tag: v1.0".
        const std::string decorations = TrimField(fields[6]);
        if (decorations.empty()) continue;

        for (const std::string& raw : SplitFields(decorations, ',')) {
            std::string name = TrimField(raw);
            if (name.empty()) continue;

            bool isHead = false;
            const size_t arrow = name.find("->");
            if (name.rfind("HEAD", 0) == 0) {
                isHead = true;
                name = (arrow == std::string::npos) ? std::string()
                                                    : TrimField(name.substr(arrow + 2));
                if (name.empty()) {
                    GitGraphRef head;
                    head.name      = "HEAD";
                    head.sha       = commit.sha;
                    head.type      = GitGraphRefType::Head;
                    head.isCurrent = true;
                    AddRef(head);
                    continue;
                }
            }

            if (name.rfind("tag:", 0) == 0) {
                AddTagRef(TrimField(name.substr(4)), commit.sha, true);
                continue;
            }

            const size_t slash = name.find('/');
            if (slash != std::string::npos && name.rfind("refs/", 0) != 0) {
                AddRemoteRef(name.substr(0, slash), name.substr(slash + 1), commit.sha);
                continue;
            }

            AddBranchRef(name, commit.sha, isHead);
        }
    }

    rowsAreNewestFirst = true;
    needsLayout = true;
    return count;
}

// =============================================================================
// MERMAID
// =============================================================================

size_t UltraCanvasGitGraph::LoadFromMermaid(const std::string& text) {
    const GitGraphMermaidDocument document = ParseMermaidGitGraph(text);
    if (!document.ok) {
        lastError = "mermaid line " + std::to_string(document.errorLine) + ": " + document.error;
        return 0;
    }

    Clear();
    lastError.clear();

    AddCommits(document.commits);
    for (const GitGraphRef& ref : document.refs) AddRef(ref);

    // Mermaid authors oldest-first, exactly like the authoring API.
    rowsAreNewestFirst = false;
    layoutOptions.orderMode = GitGraphOrderMode::AsGiven;
    layoutOptions.trunkBranch = document.mainBranchName;
    layoutOptions.swimlaneOrder = document.branchOrder;
    layoutEngine.SetOptions(layoutOptions);

    orientation = document.orientation;
    style.showCommitLabels   = document.showCommitLabel;
    style.showBranchChips    = document.showBranches;
    style.rotateCommitLabels = document.rotateCommitLabel;

    // Rebuild the authoring cursor so a loaded diagram can be extended in code.
    currentBranch = document.mainBranchName;
    for (const GitGraphRef& ref : document.refs) {
        if (ref.type != GitGraphRefType::LocalBranch) continue;
        branchTips[ref.name] = ref.sha;
        if (ref.isCurrent) currentBranch = ref.name;
    }

    needsLayout = true;
    RequestRedraw();
    return document.commits.size();
}

std::string UltraCanvasGitGraph::ToMermaidText() const {
    return ExportMermaidGitGraph(commits, refs, layoutOptions.trunkBranch);
}

bool UltraCanvasGitGraph::SaveToMermaid(const std::string& filePath) const {
    std::ofstream file(filePath);
    if (!file.is_open()) {
        lastError = "cannot open '" + filePath + "' for writing";
        return false;
    }
    file << ToMermaidText();
    return true;
}

// =============================================================================
// LAZY LOADING
// =============================================================================

void UltraCanvasGitGraph::SetDataSource(std::shared_ptr<IGitGraphDataSource> source,
                                        size_t newChunkSize) {
    dataSource = std::move(source);
    chunkSize = std::max<size_t>(1, newChunkSize);
    dataSourceExhausted = false;

    Clear();
    if (!dataSource) return;

    for (const GitGraphRef& ref : dataSource->FetchRefs()) AddRef(ref);
    LoadMoreCommits();
}

void UltraCanvasGitGraph::SetChunkSize(size_t commitsPerChunk) {
    chunkSize = std::max<size_t>(1, commitsPerChunk);
}

void UltraCanvasGitGraph::SetPrefetchRows(int rows) {
    prefetchRows = std::max(0, rows);
}

bool UltraCanvasGitGraph::LoadMoreCommits() {
    if (!dataSource || dataSourceExhausted) return false;

    const size_t offset = commits.size();
    const std::vector<GitGraphCommit> chunk = dataSource->FetchCommits(offset, chunkSize);

    // A short (or empty) chunk means the source has nothing left.
    if (chunk.size() < chunkSize) dataSourceExhausted = true;
    if (chunk.empty()) return false;

    AddCommits(chunk);
    needsLayout = true;

    if (onChunkLoaded) onChunkLoaded(commits.size(), dataSource->GetTotalCommitCount());
    RequestRedraw();
    return true;
}

// =============================================================================
// AUTHORING
// =============================================================================

std::string UltraCanvasGitGraph::MakeSyntheticSha() {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%08x", ++syntheticShaCounter * 0x9E3779B1u);
    return std::string(buffer);
}

void UltraCanvasGitGraph::SetBranchTip(const std::string& branch, const std::string& sha) {
    branchTips[branch] = sha;
    AddBranchRef(branch, sha, branch == currentBranch);
}

std::string UltraCanvasGitGraph::Commit(const std::string& subject, const std::string& tag) {
    GitGraphCommit commit;
    commit.sha        = MakeSyntheticSha();
    commit.subject    = subject;
    commit.branch     = currentBranch;
    commit.authorName = "UltraCanvas";
    commit.commitDate = static_cast<int64_t>(commits.size());
    commit.authorDate = commit.commitDate;

    auto tip = branchTips.find(currentBranch);
    if (tip != branchTips.end() && !tip->second.empty()) {
        commit.parents.push_back(tip->second);
    }

    AddCommit(commit);
    SetBranchTip(currentBranch, commit.sha);
    if (!tag.empty()) AddTagRef(tag, commit.sha, true);

    rowsAreNewestFirst = false;      // The authoring API appends oldest-first
    layoutOptions.orderMode = GitGraphOrderMode::AsGiven;
    layoutEngine.SetOptions(layoutOptions);
    return commit.sha;
}

void UltraCanvasGitGraph::Branch(const std::string& name) {
    auto tip = branchTips.find(currentBranch);
    branchTips[name] = (tip != branchTips.end()) ? tip->second : std::string();
    currentBranch = name;
    if (!branchTips[name].empty()) SetBranchTip(name, branchTips[name]);
    needsLayout = true;
}

void UltraCanvasGitGraph::Checkout(const std::string& name) {
    currentBranch = name;
    if (branchTips.find(name) == branchTips.end()) branchTips[name] = std::string();
    for (GitGraphRef& ref : refs) {
        if (ref.type == GitGraphRefType::LocalBranch) ref.isCurrent = (ref.name == name);
    }
    needsLayout = true;
}

std::string UltraCanvasGitGraph::Merge(const std::string& fromBranch,
                                       const std::string& subject) {
    auto source = branchTips.find(fromBranch);
    auto target = branchTips.find(currentBranch);
    if (source == branchTips.end() || source->second.empty()) return std::string();

    GitGraphCommit commit;
    commit.sha     = MakeSyntheticSha();
    commit.subject = subject.empty() ? ("Merge branch '" + fromBranch + "'") : subject;
    commit.branch  = currentBranch;
    commit.authorName = "UltraCanvas";
    commit.commitDate = static_cast<int64_t>(commits.size());
    commit.authorDate = commit.commitDate;

    if (target != branchTips.end() && !target->second.empty()) {
        commit.parents.push_back(target->second);
    }
    commit.parents.push_back(source->second);

    AddCommit(commit);
    SetBranchTip(currentBranch, commit.sha);

    rowsAreNewestFirst = false;
    layoutOptions.orderMode = GitGraphOrderMode::AsGiven;
    layoutEngine.SetOptions(layoutOptions);
    return commit.sha;
}

std::string UltraCanvasGitGraph::CherryPick(const std::string& sha,
                                            const std::string& subject) {
    const GitGraphCommit* source = GetCommit(sha);
    if (!source) return std::string();

    const std::string picked = Commit(subject.empty() ? source->subject : subject);
    auto it = commitIndex.find(picked);
    if (it != commitIndex.end()) commits[it->second].cherryPickSource = sha;
    return picked;
}

void UltraCanvasGitGraph::Tag(const std::string& name, const std::string& sha) {
    std::string target = sha;
    if (target.empty()) {
        auto tip = branchTips.find(currentBranch);
        if (tip != branchTips.end()) target = tip->second;
    }
    if (!target.empty()) AddTagRef(name, target, true);
}

// =============================================================================
// LAYOUT CONFIGURATION
// =============================================================================

void UltraCanvasGitGraph::SetOrientation(GitGraphOrientation newOrientation) {
    orientation = newOrientation;
    needsLayout = true;
    RequestRedraw();
}

void UltraCanvasGitGraph::SetLayoutMode(GitGraphLayoutMode mode) {
    layoutOptions.layoutMode = mode;
    layoutEngine.SetOptions(layoutOptions);
    needsLayout = true;
    RequestRedraw();
}

void UltraCanvasGitGraph::SetLaneStrategy(GitGraphLaneStrategy strategy) {
    layoutOptions.laneStrategy = strategy;
    layoutEngine.SetOptions(layoutOptions);
    needsLayout = true;
    RequestRedraw();
}

void UltraCanvasGitGraph::SetOrderMode(GitGraphOrderMode mode) {
    layoutOptions.orderMode = mode;
    layoutEngine.SetOptions(layoutOptions);
    if (mode != GitGraphOrderMode::AsGiven) rowsAreNewestFirst = true;
    needsLayout = true;
    RequestRedraw();
}

void UltraCanvasGitGraph::SetTrunkBranch(const std::string& name) {
    layoutOptions.trunkBranch = name;
    layoutEngine.SetOptions(layoutOptions);
    needsLayout = true;
    RequestRedraw();
}

void UltraCanvasGitGraph::SetSwimlaneOrder(const std::vector<std::string>& branchNames) {
    layoutOptions.swimlaneOrder = branchNames;
    layoutEngine.SetOptions(layoutOptions);
    needsLayout = true;
    RequestRedraw();
}

void UltraCanvasGitGraph::SetSwimlanesBothSides(bool bothSides) {
    layoutOptions.swimlanesBothSides = bothSides;
    layoutEngine.SetOptions(layoutOptions);
    needsLayout = true;
    RequestRedraw();
}

void UltraCanvasGitGraph::SetRowsAreNewestFirst(bool newestFirst) {
    rowsAreNewestFirst = newestFirst;
    RequestRedraw();
}

void UltraCanvasGitGraph::PerformLayout() {
    RebuildCommitIndex();
    if (filter.IsActive()) {
        // Commits may have arrived since the filter was set (lazy loading).
        layoutOptions.hiddenCommits.clear();
        for (const GitGraphCommit& commit : commits) {
            if (!MatchesFilter(commit)) layoutOptions.hiddenCommits.insert(commit.sha);
        }
    }
    layoutEngine.SetOptions(layoutOptions);
    layout = layoutEngine.Compute(commits, refs);
    UpdateContentExtent();
    needsLayout = false;
    if (onLayoutComplete) onLayoutComplete();
}

void UltraCanvasGitGraph::UpdateContentExtent() {
    contentAlongAxis = style.marginAlongAxis * 2.0 +
                       style.rowSpacing * std::max(0, layout.rowCount - 1);

    const int laneSpan = std::max(0, layout.maxLane - layout.minLane);
    contentAcrossAxis = style.marginAcrossAxis * 2.0 + style.laneSpacing * laneSpan;

    // Lane 0 sits at the left/top margin, except for swimlanes that hang either
    // side of the trunk - those centre the trunk in the element.
    if (layoutOptions.layoutMode == GitGraphLayoutMode::Swimlane && layout.minLane < 0) {
        const double extent = IsHorizontalAxis() ? GetHeight() : GetWidth();
        crossOrigin = extent * 0.5;
    } else {
        crossOrigin = style.marginAcrossAxis - layout.minLane * style.laneSpacing;
    }
}

// =============================================================================
// GEOMETRY
// =============================================================================

bool UltraCanvasGitGraph::IsHorizontalAxis() const {
    return orientation == GitGraphOrientation::LeftToRight ||
           orientation == GitGraphOrientation::RightToLeft;
}

bool UltraCanvasGitGraph::IsAxisReversed() const {
    // The oldest commit sits at the start of the axis for LeftToRight and
    // TopToBottom; rows are newest-first unless the authoring API filled them.
    const bool oldestAtAxisStart = (orientation == GitGraphOrientation::LeftToRight ||
                                    orientation == GitGraphOrientation::TopToBottom);
    return rowsAreNewestFirst == oldestAtAxisStart;
}

double UltraCanvasGitGraph::AxisCoord(int row) const {
    const int index = IsAxisReversed() ? (std::max(0, layout.rowCount - 1) - row) : row;
    return style.marginAlongAxis + style.rowSpacing * index;
}

double UltraCanvasGitGraph::CrossCoord(int lane) const {
    return crossOrigin + style.laneSpacing * lane;
}

Point2Dd UltraCanvasGitGraph::MakePoint(double alongAxis, double acrossAxis) const {
    return IsHorizontalAxis() ? Point2Dd(alongAxis, acrossAxis)
                              : Point2Dd(acrossAxis, alongAxis);
}

Point2Dd UltraCanvasGitGraph::NodePoint(int row, int lane) const {
    return MakePoint(AxisCoord(row), CrossCoord(lane));
}

double UltraCanvasGitGraph::AxisOf(const Point2Dd& p) const {
    return IsHorizontalAxis() ? p.x : p.y;
}

double UltraCanvasGitGraph::CrossOf(const Point2Dd& p) const {
    return IsHorizontalAxis() ? p.y : p.x;
}

double UltraCanvasGitGraph::NodeRadiusFor(const GitGraphPlacedCommit& placed) const {
    if (placed.isMerge) return style.mergeNodeRadius;
    if (placed.isRoot)  return style.rootNodeRadius;
    return style.nodeRadius;
}

int UltraCanvasGitGraph::LabelSideSign(int lane) const {
    // In swimlane mode labels and file boxes go on the side away from the
    // trunk so they never cover it.
    if (layoutOptions.layoutMode == GitGraphLayoutMode::Swimlane && lane != 0) {
        return lane > 0 ? 1 : -1;
    }
    return 1;
}

Point2Dd UltraCanvasGitGraph::ScreenToWorld(const Point2Di& screenPos) const {
    return Point2Dd((screenPos.x - panX) / zoomLevel, (screenPos.y - panY) / zoomLevel);
}

Point2Dd UltraCanvasGitGraph::WorldToScreen(const Point2Dd& worldPos) const {
    return Point2Dd(worldPos.x * zoomLevel + panX, worldPos.y * zoomLevel + panY);
}

void UltraCanvasGitGraph::ComputeVisibleRows(int& firstRow, int& lastRow) const {
    firstRow = 0;
    lastRow  = std::max(0, layout.rowCount - 1);
    if (layout.rowCount <= 0 || style.rowSpacing <= 0.0) return;

    // Map the element's visible extent back into axis coordinates, then into
    // rows, so only the rows on screen are drawn.
    const double extent = IsHorizontalAxis() ? GetWidth() : GetHeight();
    const double panAlongAxis = IsHorizontalAxis() ? panX : panY;
    const double axisMin = (0.0 - panAlongAxis) / zoomLevel;
    const double axisMax = (extent - panAlongAxis) / zoomLevel;

    const double overscan = style.rowSpacing * 2.0;
    int indexMin = static_cast<int>(std::floor((axisMin - style.marginAlongAxis - overscan) /
                                               style.rowSpacing));
    int indexMax = static_cast<int>(std::ceil((axisMax - style.marginAlongAxis + overscan) /
                                              style.rowSpacing));
    indexMin = std::clamp(indexMin, 0, layout.rowCount - 1);
    indexMax = std::clamp(indexMax, 0, layout.rowCount - 1);

    if (IsAxisReversed()) {
        firstRow = layout.rowCount - 1 - indexMax;
        lastRow  = layout.rowCount - 1 - indexMin;
    } else {
        firstRow = indexMin;
        lastRow  = indexMax;
    }
}

std::string UltraCanvasGitGraph::FindCommitAt(const Point2Dd& worldPos) const {
    for (const GitGraphPlacedCommit& placed : layout.commits) {
        const Point2Dd center = NodePoint(placed.row, placed.lane);
        const double dx = worldPos.x - center.x;
        const double dy = worldPos.y - center.y;
        const double hitRadius = NodeRadiusFor(placed) + 5.0;
        if (dx * dx + dy * dy <= hitRadius * hitRadius) return placed.sha;
    }
    return std::string();
}

// =============================================================================
// COLOURS
// =============================================================================

Color UltraCanvasGitGraph::GetLaneColor(int lane) const {
    auto override_ = laneColorOverrides.find(lane);
    if (override_ != laneColorOverrides.end()) return override_->second;
    if (style.lanePalette.empty()) return Color(120, 120, 120);

    // Signed lanes (swimlane mode) must map onto the palette without folding
    // -1 and +1 onto the same colour.
    const int count = static_cast<int>(style.lanePalette.size());
    const int index = ((lane % count) + count) % count;
    return style.lanePalette[index];
}

Color UltraCanvasGitGraph::GetBranchColor(const std::string& branchName) const {
    for (const GitGraphSwimlane& band : layout.swimlanes) {
        if (band.name == branchName) return GetLaneColor(band.lane);
    }
    for (const GitGraphPlacedCommit& placed : layout.commits) {
        if (placed.branch == branchName) return GetLaneColor(placed.lane);
    }
    return GetLaneColor(0);
}

void UltraCanvasGitGraph::SetLaneColor(int lane, const Color& color) {
    laneColorOverrides[lane] = color;
    RequestRedraw();
}

Color UltraCanvasGitGraph::CommitColor(const GitGraphPlacedCommit& placed) const {
    const GitGraphCommit* commit = GetCommit(placed.sha);
    if (commit && commit->hasColorOverride) return commit->colorOverride;
    return GetLaneColor(placed.lane);
}

Color UltraCanvasGitGraph::EdgeColor(const GitGraphPlacedEdge& edge) const {
    if (edge.kind == GitGraphEdgeKind::CherryPick) {
        return GetLaneColor(edge.colorLane).WithAlpha(190);
    }
    return GetLaneColor(style.colorEdgeByChild ? edge.fromLane : edge.colorLane);
}

// =============================================================================
// STYLE
// =============================================================================

void UltraCanvasGitGraph::SetStyle(const GitGraphStyle& newStyle) {
    style = newStyle;
    currentTheme = GitGraphTheme::Custom;
    needsLayout = true;
    RequestRedraw();
}

void UltraCanvasGitGraph::SetShowCommitLabels(bool show) {
    style.showCommitLabels = show;
    RequestRedraw();
}

void UltraCanvasGitGraph::SetShowFileBoxes(bool show) {
    style.showFileBoxes = show;
    RequestRedraw();
}

void UltraCanvasGitGraph::SetShowBranchChips(bool show) {
    style.showBranchChips = show;
    RequestRedraw();
}

void UltraCanvasGitGraph::SetEdgeStyle(GitGraphEdgeStyle edgeStyle) {
    style.edgeStyle = edgeStyle;
    RequestRedraw();
}

void UltraCanvasGitGraph::SetTheme(GitGraphTheme theme) {
    ApplyThemeColors(theme);
    RequestRedraw();
}

void UltraCanvasGitGraph::ApplyThemeColors(GitGraphTheme theme) {
    currentTheme = theme;

    switch (theme) {
        case GitGraphTheme::Dark:
            style.backgroundColor  = Color(24, 26, 31);
            style.plotAreaColor    = Color(24, 26, 31);
            style.rowStripeColor   = Color(31, 34, 40);
            style.textColor        = Color(226, 229, 233);
            style.mutedTextColor   = Color(146, 152, 161);
            style.nodeBorderColor  = Color(24, 26, 31);
            style.swimlaneBandColor = Color(30, 33, 39);
            style.fileBoxColor     = Color(38, 41, 48);
            style.fileBoxBorderColor = Color(70, 75, 84);
            style.fileBoxTextColor = Color(206, 211, 218);
            style.annotationColor  = Color(150, 156, 165);
            style.lanePalette = {
                Color(88, 166, 255), Color(63, 185, 80),  Color(219, 97, 162),
                Color(219, 149, 62), Color(163, 113, 247), Color(57, 197, 207),
                Color(248, 81, 73),  Color(191, 143, 108)
            };
            break;

        case GitGraphTheme::Light:
            style.backgroundColor  = Color(255, 255, 255);
            style.plotAreaColor    = Color(255, 255, 255);
            style.rowStripeColor   = Color(246, 248, 250);
            style.textColor        = Color(32, 34, 38);
            style.mutedTextColor   = Color(110, 116, 124);
            style.nodeBorderColor  = Color(255, 255, 255);
            style.swimlaneBandColor = Color(248, 249, 250);
            style.lanePalette = {
                Color(9, 105, 218),  Color(26, 127, 55),  Color(191, 57, 137),
                Color(191, 135, 0),  Color(130, 80, 223), Color(0, 146, 179),
                Color(207, 34, 46),  Color(133, 96, 71)
            };
            break;

        case GitGraphTheme::Colorful:
            style.backgroundColor = Color(252, 252, 254);
            style.plotAreaColor   = Color(252, 252, 254);
            style.lanePalette = {
                Color(0, 145, 234),  Color(0, 200, 83),   Color(255, 61, 129),
                Color(255, 145, 0),  Color(170, 0, 255),  Color(0, 229, 255),
                Color(255, 23, 68),  Color(255, 214, 0)
            };
            break;

        case GitGraphTheme::Monochrome: {
            style.backgroundColor = Color(255, 255, 255);
            style.plotAreaColor   = Color(255, 255, 255);
            style.textColor       = Color(20, 20, 20);
            style.lanePalette.clear();
            for (int i = 0; i < 8; ++i) {
                const uint8_t level = static_cast<uint8_t>(40 + i * 22);
                style.lanePalette.push_back(Color(level, level, level));
            }
            break;
        }

        case GitGraphTheme::Custom:
            break;

        case GitGraphTheme::Default:
        default: {
            // Colours only - geometry the caller has tuned (lane spacing, row
            // spacing, node sizes) must survive a theme switch.
            const GitGraphStyle defaults;
            style.backgroundColor    = defaults.backgroundColor;
            style.plotAreaColor      = defaults.plotAreaColor;
            style.rowStripeColor     = defaults.rowStripeColor;
            style.textColor          = defaults.textColor;
            style.mutedTextColor     = defaults.mutedTextColor;
            style.nodeBorderColor    = defaults.nodeBorderColor;
            style.swimlaneBandColor  = defaults.swimlaneBandColor;
            style.fileBoxColor       = defaults.fileBoxColor;
            style.fileBoxBorderColor = defaults.fileBoxBorderColor;
            style.fileBoxTextColor   = defaults.fileBoxTextColor;
            style.annotationColor    = defaults.annotationColor;
            style.tagChipColor       = defaults.tagChipColor;
            style.tagChipTextColor   = defaults.tagChipTextColor;
            style.headChipColor      = defaults.headChipColor;
            style.headChipTextColor  = defaults.headChipTextColor;
            style.lanePalette        = defaults.lanePalette;
            break;
        }
    }
}

// =============================================================================
// FILTERING
// =============================================================================

namespace {

bool ContainsFold(const std::string& haystack, const std::string& needle, bool caseSensitive) {
    if (needle.empty()) return true;
    if (caseSensitive) return haystack.find(needle) != std::string::npos;

    auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(::tolower(c)); });
        return text;
    };
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

} // namespace

bool UltraCanvasGitGraph::MatchesFilter(const GitGraphCommit& commit) const {
    if (!filter.IsActive()) return true;

    if (filter.mergesOnly && !commit.IsMerge()) return false;
    if (filter.hideMerges && commit.IsMerge())  return false;

    if (filter.since != 0 && commit.commitDate < filter.since) return false;
    if (filter.until != 0 && commit.commitDate > filter.until) return false;

    if (!filter.author.empty()) {
        if (!ContainsFold(commit.authorName, filter.author, filter.caseSensitive) &&
            !ContainsFold(commit.authorEmail, filter.author, filter.caseSensitive)) {
            return false;
        }
    }

    if (!filter.text.empty()) {
        const bool hit =
            ContainsFold(commit.subject, filter.text, filter.caseSensitive) ||
            ContainsFold(commit.body, filter.text, filter.caseSensitive) ||
            ContainsFold(commit.sha, filter.text, filter.caseSensitive) ||
            ContainsFold(commit.authorName, filter.text, filter.caseSensitive);
        if (!hit) return false;
    }

    if (!filter.path.empty()) {
        bool hit = false;
        for (const GitGraphFileChange& file : commit.files) {
            if (ContainsFold(file.path, filter.path, filter.caseSensitive)) { hit = true; break; }
        }
        if (!hit) return false;
    }

    if (!filter.branches.empty()) {
        // Match the recorded branch, or any ref sitting on this commit.
        bool hit = std::find(filter.branches.begin(), filter.branches.end(),
                             commit.branch) != filter.branches.end();
        if (!hit) {
            for (const GitGraphRef& ref : refs) {
                if (ref.sha != commit.sha) continue;
                if (std::find(filter.branches.begin(), filter.branches.end(),
                              ref.name) != filter.branches.end()) {
                    hit = true;
                    break;
                }
            }
        }
        if (!hit) return false;
    }

    return true;
}

void UltraCanvasGitGraph::ApplyFilter() {
    layoutOptions.hiddenCommits.clear();
    if (filter.IsActive()) {
        for (const GitGraphCommit& commit : commits) {
            if (!MatchesFilter(commit)) layoutOptions.hiddenCommits.insert(commit.sha);
        }
    }
    layoutEngine.SetOptions(layoutOptions);
    needsLayout = true;
}

void UltraCanvasGitGraph::SetFilter(const GitGraphFilter& newFilter) {
    filter = newFilter;
    ApplyFilter();
    RequestRedraw();
}

void UltraCanvasGitGraph::ClearFilter() {
    filter.Clear();
    ApplyFilter();
    RequestRedraw();
}

void UltraCanvasGitGraph::SetReduceCrossings(bool enabled) {
    layoutOptions.reduceCrossings = enabled;
    layoutEngine.SetOptions(layoutOptions);
    needsLayout = true;
    RequestRedraw();
}

// =============================================================================
// SEARCH
// =============================================================================

size_t UltraCanvasGitGraph::Search(const std::string& query, bool caseSensitive) {
    searchMatches.clear();
    searchIndex = 0;
    searchQuery = query;
    searchCaseSensitive = caseSensitive;

    if (query.empty()) {
        RequestRedraw();
        return 0;
    }
    if (needsLayout) PerformLayout();

    // Search in row order so next/previous walk the graph top to bottom.
    for (const GitGraphPlacedCommit& placed : layout.commits) {
        const GitGraphCommit* commit = GetCommit(placed.sha);
        if (!commit) continue;

        bool hit = ContainsFold(commit->sha, query, caseSensitive) ||
                   ContainsFold(commit->subject, query, caseSensitive) ||
                   ContainsFold(commit->body, query, caseSensitive) ||
                   ContainsFold(commit->authorName, query, caseSensitive) ||
                   ContainsFold(commit->authorEmail, query, caseSensitive);
        if (!hit) {
            for (const GitGraphRef& ref : GetRefsForCommit(placed.sha)) {
                if (ContainsFold(ref.DisplayName(), query, caseSensitive)) { hit = true; break; }
            }
        }
        if (hit) searchMatches.push_back(placed.sha);
    }

    if (!searchMatches.empty()) {
        SelectCommit(searchMatches.front());
        CenterOnCommit(searchMatches.front());
    }
    if (onSearchChanged) onSearchChanged(searchMatches.empty() ? 0 : 1, searchMatches.size());

    RequestRedraw();
    return searchMatches.size();
}

void UltraCanvasGitGraph::ClearSearch() {
    searchMatches.clear();
    searchQuery.clear();
    searchIndex = 0;
    if (onSearchChanged) onSearchChanged(0, 0);
    RequestRedraw();
}

bool UltraCanvasGitGraph::FindNext() {
    if (searchMatches.empty()) return false;
    searchIndex = (searchIndex + 1) % searchMatches.size();
    SelectCommit(searchMatches[searchIndex]);
    CenterOnCommit(searchMatches[searchIndex]);
    if (onSearchChanged) onSearchChanged(searchIndex + 1, searchMatches.size());
    return true;
}

bool UltraCanvasGitGraph::FindPrevious() {
    if (searchMatches.empty()) return false;
    searchIndex = (searchIndex + searchMatches.size() - 1) % searchMatches.size();
    SelectCommit(searchMatches[searchIndex]);
    CenterOnCommit(searchMatches[searchIndex]);
    if (onSearchChanged) onSearchChanged(searchIndex + 1, searchMatches.size());
    return true;
}

std::string UltraCanvasGitGraph::GetCurrentSearchMatch() const {
    if (searchMatches.empty()) return std::string();
    return searchMatches[std::min(searchIndex, searchMatches.size() - 1)];
}

bool UltraCanvasGitGraph::IsSearchMatch(const std::string& sha) const {
    return std::find(searchMatches.begin(), searchMatches.end(), sha) != searchMatches.end();
}

// =============================================================================
// TABLE PANE / ROW ALIGNMENT / MINIMAP CONFIG
// =============================================================================

void UltraCanvasGitGraph::SetShowTable(bool show) {
    style.showTable = show;
    if (show && tableColumns.empty()) {
        tableColumns = {
            GitGraphTableColumnSpec(GitGraphTableColumn::Subject, "Subject", 320.0),
            GitGraphTableColumnSpec(GitGraphTableColumn::Author,  "Author",  140.0),
            GitGraphTableColumnSpec(GitGraphTableColumn::Date,    "Date",    130.0),
            GitGraphTableColumnSpec(GitGraphTableColumn::Sha,     "Commit",   80.0)
        };
    }
    needsLayout = true;
    RequestRedraw();
}

void UltraCanvasGitGraph::SetTableColumns(const std::vector<GitGraphTableColumnSpec>& columns) {
    tableColumns = columns;
    RequestRedraw();
}

void UltraCanvasGitGraph::SetGraphPaneWidth(double width) {
    style.tableGraphWidth = std::max(40.0, width);
    needsLayout = true;
    RequestRedraw();
}

void UltraCanvasGitGraph::SetDateFormatter(std::function<std::string(int64_t)> formatter) {
    dateFormatter = std::move(formatter);
    RequestRedraw();
}

bool UltraCanvasGitGraph::IsTableActive() const {
    // A row-aligned table only makes sense when rows run down the element.
    return style.showTable && !IsHorizontalAxis() && !tableColumns.empty();
}

double UltraCanvasGitGraph::GraphPaneWidth() const {
    return IsTableActive() ? std::min<double>(style.tableGraphWidth, GetWidth())
                           : GetWidth();
}

double UltraCanvasGitGraph::GetRowScreenPosition(int row) const {
    const double axis = AxisCoord(row) * zoomLevel;
    return axis + (IsHorizontalAxis() ? panX : panY);
}

int UltraCanvasGitGraph::GetRowAtScreenPosition(double position) const {
    if (layout.rowCount <= 0 || style.rowSpacing <= 0.0) return -1;

    const double pan = IsHorizontalAxis() ? panX : panY;
    const double axis = (position - pan) / zoomLevel;
    int index = static_cast<int>(std::lround((axis - style.marginAlongAxis) / style.rowSpacing));
    index = std::clamp(index, 0, layout.rowCount - 1);

    const int row = IsAxisReversed() ? (layout.rowCount - 1 - index) : index;
    return std::clamp(row, 0, layout.rowCount - 1);
}

std::pair<int, int> UltraCanvasGitGraph::GetVisibleRowRange() const {
    int firstRow = 0, lastRow = 0;
    ComputeVisibleRows(firstRow, lastRow);
    return {firstRow, lastRow};
}

void UltraCanvasGitGraph::SetShowMinimap(bool show) {
    style.showMinimap = show;
    RequestRedraw();
}

void UltraCanvasGitGraph::SetMinimapPosition(GitGraphMinimapPosition position) {
    style.minimapPosition = position;
    RequestRedraw();
}

// =============================================================================
// SELECTION
// =============================================================================

void UltraCanvasGitGraph::SelectCommit(const std::string& sha, bool addToSelection) {
    if (sha.empty()) return;
    if (!addToSelection) selectedShas.clear();
    if (!IsCommitSelected(sha)) selectedShas.push_back(sha);
    if (onCommitSelect) onCommitSelect(sha);
    RequestRedraw();
}

void UltraCanvasGitGraph::DeselectAll() {
    selectedShas.clear();
    RequestRedraw();
}

bool UltraCanvasGitGraph::IsCommitSelected(const std::string& sha) const {
    return std::find(selectedShas.begin(), selectedShas.end(), sha) != selectedShas.end();
}

// =============================================================================
// VIEW
// =============================================================================

void UltraCanvasGitGraph::SetZoom(float zoom) {
    zoomLevel = std::clamp(zoom, minZoom, maxZoom);
    RequestRedraw();
}

void UltraCanvasGitGraph::ZoomIn()  { SetZoom(zoomLevel * 1.15f); }
void UltraCanvasGitGraph::ZoomOut() { SetZoom(zoomLevel * 0.87f); }

void UltraCanvasGitGraph::ZoomToFit(double padding) {
    if (needsLayout) PerformLayout();
    if (layout.commits.empty()) return;

    // Fit the real node bounding box: in swimlane mode the content is centred
    // on the element rather than starting at the origin, so deriving the box
    // from the content extents alone would mis-centre it.
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
    bool first = true;
    for (const GitGraphPlacedCommit& placed : layout.commits) {
        const Point2Dd point = NodePoint(placed.row, placed.lane);
        if (first) {
            minX = maxX = point.x;
            minY = maxY = point.y;
            first = false;
        } else {
            minX = std::min(minX, point.x);
            maxX = std::max(maxX, point.x);
            minY = std::min(minY, point.y);
            maxY = std::max(maxY, point.y);
        }
    }

    const double margin = style.nodeRadius + style.labelGap;
    minX -= margin; minY -= margin;
    maxX += margin; maxY += margin;

    const double contentWidth  = std::max(1.0, maxX - minX);
    const double contentHeight = std::max(1.0, maxY - minY);
    const double availableWidth  = std::max(1.0, GetWidth()  - padding * 2.0);
    const double availableHeight = std::max(1.0, GetHeight() - padding * 2.0);

    const double fit = std::min(availableWidth / contentWidth, availableHeight / contentHeight);
    zoomLevel = std::clamp(static_cast<float>(fit), minZoom, maxZoom);
    panX = (GetWidth()  - contentWidth  * zoomLevel) * 0.5 - minX * zoomLevel;
    panY = (GetHeight() - contentHeight * zoomLevel) * 0.5 - minY * zoomLevel;
    RequestRedraw();
}

void UltraCanvasGitGraph::ResetView() {
    zoomLevel = 1.0f;
    panX = 0.0;
    panY = 0.0;
    RequestRedraw();
}

void UltraCanvasGitGraph::SetPan(double x, double y) {
    panX = x;
    panY = y;
    RequestRedraw();
}

void UltraCanvasGitGraph::CenterOnCommit(const std::string& sha) {
    if (needsLayout) PerformLayout();
    const GitGraphPlacedCommit* placed = layout.Find(sha);
    if (!placed) return;

    const Point2Dd world = NodePoint(placed->row, placed->lane);
    panX = GetWidth()  * 0.5 - world.x * zoomLevel;
    panY = GetHeight() * 0.5 - world.y * zoomLevel;
    RequestRedraw();
}

void UltraCanvasGitGraph::ScrollToRef(const std::string& refName) {
    for (const GitGraphRef& ref : refs) {
        if (ref.name == refName || ref.DisplayName() == refName) {
            CenterOnCommit(ref.sha);
            return;
        }
    }
}

// =============================================================================
// RENDERING
// =============================================================================

void UltraCanvasGitGraph::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    if (!ctx || !IsVisible()) return;
    if (needsLayout) PerformLayout();

    DrawBackground(ctx);

    ctx->PushState();
    if (IsTableActive()) {
        // Keep the graph inside its pane so long edges never bleed into the
        // table columns.
        ctx->ClipRect(Rect2Dd(0, 0, GraphPaneWidth(), GetHeight()));
    }
    ctx->Translate(panX, panY);
    ctx->Scale(zoomLevel, zoomLevel);

    int firstRow = 0, lastRow = 0;
    ComputeVisibleRows(firstRow, lastRow);

    // Pull the next chunk once the viewport comes within prefetchRows of the
    // end of the loaded history, and re-lay out so this frame is consistent.
    if (dataSource && !dataSourceExhausted &&
        lastRow >= layout.rowCount - 1 - prefetchRows) {
        if (LoadMoreCommits()) {
            PerformLayout();
            ComputeVisibleRows(firstRow, lastRow);
        }
    }

    if (layoutOptions.layoutMode == GitGraphLayoutMode::Swimlane) {
        if (style.showSwimlaneBands)  DrawSwimlaneBands(ctx);
        if (style.showTrunkBaseline)  DrawTrunkBaseline(ctx);
        if (style.showSwimlaneLabels) DrawSwimlaneLabels(ctx);
    }

    DrawEdges(ctx, firstRow, lastRow);
    DrawNodes(ctx, firstRow, lastRow);
    DrawDecorations(ctx, firstRow, lastRow);
    DrawAnnotations(ctx);

    ctx->PopState();

    if (IsTableActive())  DrawTablePane(ctx, firstRow, lastRow);
    if (style.showMinimap) DrawMinimap(ctx);
    if (style.showTooltips && !hoveredSha.empty()) DrawTooltip(ctx);
}

void UltraCanvasGitGraph::DrawBackground(IRenderContext* ctx) {
    ctx->SetFillPaint(style.backgroundColor);
    ctx->ClearPath();
    ctx->Rect(0, 0, GetWidth(), GetHeight());
    ctx->FillPathPreserve();
    ctx->ClearPath();

    if (!style.showRowStripes || layout.rowCount <= 0) return;

    // Stripes follow rows, so they only make sense on the cross axis.
    ctx->SetFillPaint(style.rowStripeColor);
    for (int row = 0; row < layout.rowCount; row += 2) {
        const double axis = AxisCoord(row) * zoomLevel + (IsHorizontalAxis() ? panX : panY);
        const double thickness = style.rowSpacing * zoomLevel;
        ctx->ClearPath();
        if (IsHorizontalAxis()) {
            ctx->Rect(axis - thickness * 0.5, 0, thickness, GetHeight());
        } else {
            ctx->Rect(0, axis - thickness * 0.5, GetWidth(), thickness);
        }
        ctx->FillPathPreserve();
    }
    ctx->ClearPath();
}

void UltraCanvasGitGraph::DrawSwimlaneBands(IRenderContext* ctx) {
    const double bandThickness = style.laneSpacing;
    for (const GitGraphSwimlane& band : layout.swimlanes) {
        if (band.lane == 0) continue;               // The trunk gets its baseline instead
        const double cross = CrossCoord(band.lane);
        const double axisStart = AxisCoord(0);
        const double axisEnd   = AxisCoord(std::max(0, layout.rowCount - 1));
        const double from = std::min(axisStart, axisEnd) - style.marginAlongAxis * 0.5;
        const double to   = std::max(axisStart, axisEnd) + style.marginAlongAxis * 0.5;

        ctx->SetFillPaint(style.swimlaneBandColor);
        ctx->ClearPath();
        if (IsHorizontalAxis()) {
            ctx->Rect(from, cross - bandThickness * 0.5, to - from, bandThickness);
        } else {
            ctx->Rect(cross - bandThickness * 0.5, from, bandThickness, to - from);
        }
        ctx->FillPathPreserve();
        ctx->ClearPath();
    }
}

void UltraCanvasGitGraph::DrawSwimlaneLabels(IRenderContext* ctx) {
    ctx->SetFontFamily(style.fontFamily);
    ctx->SetFontSize(style.chipFontSize);

    for (const GitGraphSwimlane& band : layout.swimlanes) {
        const double cross = CrossCoord(band.lane);
        const double axis  = std::min(AxisCoord(0), AxisCoord(std::max(0, layout.rowCount - 1)));
        const Color color  = GetLaneColor(band.lane);

        const double textWidth = ctx->GetTextLineWidth(band.name);
        const double chipWidth = textWidth + style.chipPadding * 2.0;
        const double chipHeight = style.chipFontSize + style.chipPadding * 2.0;

        Point2Dd topLeft = IsHorizontalAxis()
            ? Point2Dd(axis - chipWidth - style.chipGap * 2.0, cross - chipHeight * 0.5)
            : Point2Dd(cross - chipWidth * 0.5, axis - chipHeight - style.chipGap * 2.0);

        double consumed = 0.0;
        DrawChip(ctx, topLeft, band.name, color.WithAlpha(38), color, color, consumed);
    }
}

void UltraCanvasGitGraph::DrawTrunkBaseline(IRenderContext* ctx) {
    if (layout.rowCount <= 0) return;

    const double cross = CrossCoord(0);
    const double first = AxisCoord(0);
    const double last  = AxisCoord(layout.rowCount - 1);
    const double from  = std::min(first, last) - style.marginAlongAxis * 0.6;
    const double to    = std::max(first, last) + style.marginAlongAxis * 0.9;

    const Color trunkColor = GetLaneColor(0);
    ctx->SetStrokePaint(trunkColor);
    ctx->SetStrokeWidth(style.trunkBaselineWidth);
    ctx->DrawLine(MakePoint(from, cross), MakePoint(to, cross));

    if (!style.showTrunkArrow) return;

    // Arrowhead at the "newest" end of the axis.
    const bool arrowAtHigh = !IsAxisReversed() ? false : true;
    const double tipAxis   = arrowAtHigh ? to : from;
    const double direction = arrowAtHigh ? 1.0 : -1.0;
    const double size = 8.0;

    std::vector<Point2Dd> head = {
        MakePoint(tipAxis, cross),
        MakePoint(tipAxis - direction * size, cross - size * 0.55),
        MakePoint(tipAxis - direction * size, cross + size * 0.55)
    };
    ctx->SetFillPaint(trunkColor);
    ctx->FillLinePath(head);
}

// ===== EDGES =====

void UltraCanvasGitGraph::BuildEdgeGeometry(const GitGraphPlacedEdge& edge,
                                            Point2Dd& start, Point2Dd& corner,
                                            Point2Dd& end) const {
    start = NodePoint(edge.fromRow, edge.fromLane);
    end   = NodePoint(edge.toRow,   edge.toLane);

    const double startAxis = AxisOf(start);
    const double endAxis   = AxisOf(end);
    const double span      = std::fabs(endAxis - startAxis);
    const double direction = Sign(endAxis - startAxis);
    // Half the span for an adjacent-row hop, so the corner stays strictly
    // between the two commits instead of collapsing onto one of them.
    const double step      = std::min(style.rowSpacing, span * 0.5);

    if (edge.turnAtChild) {
        // Leave the child sideways, then run along the parent's lane.
        corner = MakePoint(startAxis + direction * step, CrossOf(end));
    } else {
        // Run along the child's lane, then turn in at the parent.
        corner = MakePoint(endAxis - direction * step, CrossOf(start));
    }
}

void UltraCanvasGitGraph::DrawEdge(IRenderContext* ctx, const GitGraphPlacedEdge& edge) {
    Point2Dd start, corner, end;
    BuildEdgeGeometry(edge, start, corner, end);

    const Color color = EdgeColor(edge);
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(style.edgeWidth);

    const bool dashed = (edge.kind == GitGraphEdgeKind::CherryPick ||
                         edge.kind == GitGraphEdgeKind::Skipped);
    if (dashed) ctx->SetLineDash(style.cherryPickDash);

    const bool sameLane = (edge.fromLane == edge.toLane);

    if (sameLane || style.edgeStyle == GitGraphEdgeStyle::Straight) {
        ctx->DrawLine(start, end);
    } else if (style.edgeStyle == GitGraphEdgeStyle::Bezier) {
        // One quadratic through the corner gives the smooth SmartGit look.
        ctx->ClearPath();
        ctx->MoveTo(start.x, start.y);
        ctx->QuadraticCurveTo(corner.x, corner.y, end.x, end.y);
        ctx->StrokePathPreserve();
        ctx->ClearPath();
    } else {
        // Orthogonal / Arc: two straight runs joined by a rounded corner.
        const double radius = (style.edgeStyle == GitGraphEdgeStyle::Arc)
                                  ? style.cornerRadius * 1.8
                                  : style.cornerRadius;

        const double inLength  = std::hypot(corner.x - start.x, corner.y - start.y);
        const double outLength = std::hypot(end.x - corner.x, end.y - corner.y);
        const double effective = std::min({radius, inLength * 0.5, outLength * 0.5});

        Point2Dd beforeCorner = corner;
        Point2Dd afterCorner  = corner;
        if (inLength > 0.0) {
            beforeCorner.x = corner.x + (start.x - corner.x) / inLength * effective;
            beforeCorner.y = corner.y + (start.y - corner.y) / inLength * effective;
        }
        if (outLength > 0.0) {
            afterCorner.x = corner.x + (end.x - corner.x) / outLength * effective;
            afterCorner.y = corner.y + (end.y - corner.y) / outLength * effective;
        }

        ctx->ClearPath();
        ctx->MoveTo(start.x, start.y);
        ctx->LineTo(beforeCorner.x, beforeCorner.y);
        ctx->QuadraticCurveTo(corner.x, corner.y, afterCorner.x, afterCorner.y);
        ctx->LineTo(end.x, end.y);
        ctx->StrokePathPreserve();
        ctx->ClearPath();
    }

    if (dashed) ctx->SetLineDash(UCDashPattern());

    if (style.showEdgeArrows) {
        const double dx = end.x - corner.x;
        const double dy = end.y - corner.y;
        const double length = std::hypot(dx, dy);
        if (length > 1.0) {
            const double ux = dx / length, uy = dy / length;
            const double size = 7.0;
            const Point2Dd tip(end.x - ux * (style.nodeRadius + 1.0),
                               end.y - uy * (style.nodeRadius + 1.0));
            std::vector<Point2Dd> head = {
                tip,
                Point2Dd(tip.x - ux * size - uy * size * 0.45,
                         tip.y - uy * size + ux * size * 0.45),
                Point2Dd(tip.x - ux * size + uy * size * 0.45,
                         tip.y - uy * size - ux * size * 0.45)
            };
            ctx->SetFillPaint(color);
            ctx->FillLinePath(head);
        }
    }
}

void UltraCanvasGitGraph::DrawEdges(IRenderContext* ctx, int firstRow, int lastRow) {
    for (const GitGraphPlacedEdge& edge : layout.edges) {
        const int low  = std::min(edge.fromRow, edge.toRow);
        const int high = std::max(edge.fromRow, edge.toRow);
        if (high < firstRow || low > lastRow) continue;      // Entirely off screen
        DrawEdge(ctx, edge);
    }
}

// ===== NODES =====

void UltraCanvasGitGraph::DrawNodeShape(IRenderContext* ctx, const Point2Dd& center,
                                        double radius, GitGraphNodeShape shape,
                                        const Color& color, bool filled) {
    switch (shape) {
        case GitGraphNodeShape::Square: {
            const Rect2Dd rect(center.x - radius, center.y - radius, radius * 2.0, radius * 2.0);
            if (filled) {
                ctx->SetFillPaint(color);
                ctx->FillRectangle(rect);
            } else {
                ctx->SetStrokePaint(color);
                ctx->DrawRectangle(rect);
            }
            break;
        }
        case GitGraphNodeShape::Diamond: {
            std::vector<Point2Dd> points = {
                Point2Dd(center.x, center.y - radius),
                Point2Dd(center.x + radius, center.y),
                Point2Dd(center.x, center.y + radius),
                Point2Dd(center.x - radius, center.y)
            };
            if (filled) {
                ctx->SetFillPaint(color);
                ctx->FillLinePath(points);
            } else {
                ctx->SetStrokePaint(color);
                ctx->DrawLinePath(points, true);
            }
            break;
        }
        case GitGraphNodeShape::Ring: {
            ctx->SetFillPaint(style.backgroundColor);
            ctx->FillCircle(center, radius);
            ctx->SetStrokePaint(color);
            ctx->SetStrokeWidth(std::max(1.5, style.edgeWidth));
            ctx->DrawCircle(center, radius);
            break;
        }
        case GitGraphNodeShape::Circle:
        default: {
            if (filled) {
                ctx->SetFillPaint(color);
                ctx->FillCircle(center, radius);
            } else {
                ctx->SetStrokePaint(color);
                ctx->DrawCircle(center, radius);
            }
            break;
        }
    }
}

void UltraCanvasGitGraph::DrawNode(IRenderContext* ctx, const GitGraphPlacedCommit& placed) {
    const Point2Dd center = NodePoint(placed.row, placed.lane);
    const double radius   = NodeRadiusFor(placed);
    const Color  color    = CommitColor(placed);
    const GitGraphCommit* commit = GetCommit(placed.sha);

    // Halo so edges crossing behind the node do not touch it.
    if (style.drawNodeHalo) {
        ctx->SetFillPaint(style.backgroundColor);
        ctx->FillCircle(center, radius + style.nodeHaloWidth);
    }

    const bool selected = IsCommitSelected(placed.sha);
    const bool hovered  = (placed.sha == hoveredSha);

    if (commit && commit->type == GitGraphCommitType::Highlight) {
        const Rect2Dd rect(center.x - radius * 1.6, center.y - radius * 1.1,
                           radius * 3.2, radius * 2.2);
        ctx->SetFillPaint(color);
        ctx->FillRoundedRectangle(rect, 3.0);
    } else {
        const GitGraphNodeShape shape = placed.isMerge ? style.mergeNodeShape
                                                       : style.nodeShape;
        DrawNodeShape(ctx, center, radius, shape, color, true);

        if (commit && commit->type == GitGraphCommitType::Reverse) {
            // Crossed circle - the revert marker.
            ctx->SetStrokePaint(style.backgroundColor);
            ctx->SetStrokeWidth(1.6);
            const double d = radius * 0.62;
            ctx->DrawLine(Point2Dd(center.x - d, center.y - d),
                          Point2Dd(center.x + d, center.y + d));
            ctx->DrawLine(Point2Dd(center.x - d, center.y + d),
                          Point2Dd(center.x + d, center.y - d));
        }
    }

    if (placed.isRoot) {
        ctx->SetStrokePaint(Blend(color, style.backgroundColor, 0.45));
        ctx->SetStrokeWidth(1.5);
        ctx->DrawCircle(center, radius + 2.5);
    }

    if (!searchMatches.empty() && IsSearchMatch(placed.sha)) {
        const bool isCurrent = (placed.sha == GetCurrentSearchMatch());
        ctx->SetStrokePaint(isCurrent ? style.searchCurrentColor : style.searchMatchColor);
        ctx->SetStrokeWidth(isCurrent ? 3.0 : 2.0);
        ctx->DrawCircle(center, radius + 6.5);
    }

    if (selected || hovered) {
        ctx->SetStrokePaint(selected ? style.selectionColor : style.hoverColor);
        ctx->SetStrokeWidth(style.selectionWidth);
        ctx->DrawCircle(center, radius + 4.0);
    }
}

void UltraCanvasGitGraph::DrawNodes(IRenderContext* ctx, int firstRow, int lastRow) {
    for (const GitGraphPlacedCommit& placed : layout.commits) {
        if (placed.row < firstRow || placed.row > lastRow) continue;
        DrawNode(ctx, placed);
    }
}

// ===== DECORATIONS =====

void UltraCanvasGitGraph::DrawChip(IRenderContext* ctx, const Point2Dd& topLeft,
                                   const std::string& text, const Color& fill,
                                   const Color& border, const Color& textColor,
                                   double& outWidth) {
    const double textWidth  = ctx->GetTextLineWidth(text);
    const double chipHeight = style.chipFontSize + style.chipPadding * 2.0;
    const double chipWidth  = textWidth + style.chipPadding * 2.0;

    const Rect2Dd rect(topLeft.x, topLeft.y, chipWidth, chipHeight);
    ctx->SetFillPaint(fill);
    ctx->FillRoundedRectangle(rect, style.chipRadius);
    ctx->SetStrokePaint(border);
    ctx->SetStrokeWidth(1.0);
    ctx->DrawRoundedRectangle(rect, style.chipRadius);

    ctx->SetTextPaint(textColor);
    ctx->DrawText(text, Point2Dd(topLeft.x + style.chipPadding,
                                 topLeft.y + style.chipPadding));

    outWidth = chipWidth;
}

void UltraCanvasGitGraph::DrawRefChips(IRenderContext* ctx,
                                       const GitGraphPlacedCommit& placed,
                                       const std::vector<GitGraphRef>& commitRefs,
                                       double& cursorAcross) {
    const Point2Dd center = NodePoint(placed.row, placed.lane);
    const int side = LabelSideSign(placed.lane);
    const double chipHeight = style.chipFontSize + style.chipPadding * 2.0;

    ctx->SetFontSize(style.chipFontSize);

    size_t drawn = 0;
    for (const GitGraphRef& ref : commitRefs) {
        if (ref.type == GitGraphRefType::Tag && !style.showTagChips) continue;
        if (ref.type != GitGraphRefType::Tag && !style.showBranchChips) continue;

        if (drawn >= style.maxChipsPerCommit) {
            const std::string more = "+" + std::to_string(commitRefs.size() - drawn);
            Point2Dd topLeft = IsHorizontalAxis()
                ? Point2Dd(center.x - 12.0, center.y + side * cursorAcross)
                : Point2Dd(center.x + cursorAcross, center.y - chipHeight * 0.5);
            double width = 0.0;
            DrawChip(ctx, topLeft, more, style.backgroundColor,
                     style.mutedTextColor, style.mutedTextColor, width);
            cursorAcross += width + style.chipGap;
            break;
        }

        Color fill, border, textColor;
        if (ref.isCurrent || ref.type == GitGraphRefType::Head) {
            fill = style.headChipColor;
            border = style.headChipColor;
            textColor = style.headChipTextColor;
        } else if (ref.type == GitGraphRefType::Tag) {
            fill = style.tagChipColor;
            border = Blend(style.tagChipColor, Color(0, 0, 0), 0.25);
            textColor = style.tagChipTextColor;
        } else if (ref.type == GitGraphRefType::RemoteBranch) {
            fill = style.backgroundColor;
            border = GetLaneColor(placed.lane);
            textColor = GetLaneColor(placed.lane);
        } else {
            const Color laneColor = GetLaneColor(placed.lane);
            fill = laneColor.WithAlpha(38);
            border = laneColor;
            textColor = laneColor;
        }

        const std::string label = (ref.type == GitGraphRefType::Tag)
                                      ? ("\xE2\x97\x86 " + ref.DisplayName())   // small diamond
                                      : ref.DisplayName();

        Point2Dd topLeft = IsHorizontalAxis()
            ? Point2Dd(center.x - (ctx->GetTextLineWidth(label) + style.chipPadding * 2.0) * 0.5,
                       center.y + side * cursorAcross)
            : Point2Dd(center.x + cursorAcross, center.y - chipHeight * 0.5);

        if (IsHorizontalAxis() && side < 0) topLeft.y -= chipHeight;

        double width = 0.0;
        DrawChip(ctx, topLeft, label, fill, border, textColor, width);

        cursorAcross += IsHorizontalAxis() ? (chipHeight + style.chipGap)
                                           : (width + style.chipGap);
        ++drawn;
    }
}

std::string UltraCanvasGitGraph::CommitLabelText(const GitGraphCommit& commit) const {
    if (commitLabelFormatter) return commitLabelFormatter(commit);
    if (commit.subject.empty()) return commit.ShortSha();
    return commit.ShortSha() + "  " + commit.subject;
}

void UltraCanvasGitGraph::DrawCommitLabel(IRenderContext* ctx,
                                          const GitGraphPlacedCommit& placed,
                                          const GitGraphCommit& commit,
                                          double cursorAcross) {
    const Point2Dd center = NodePoint(placed.row, placed.lane);
    const std::string text = CommitLabelText(commit);
    if (text.empty()) return;

    ctx->SetFontSize(style.fontSize);
    ctx->SetTextPaint(style.textColor);

    if (IsHorizontalAxis()) {
        // Labels sit beside the node, on the side away from the trunk.
        const int side = LabelSideSign(placed.lane);
        const double offset = cursorAcross + style.labelGap;
        const double textWidth = ctx->GetTextLineWidth(text);
        const Point2Dd position(center.x - textWidth * 0.5,
                                center.y + side * offset - (side < 0 ? style.fontSize : 0.0));

        if (style.rotateCommitLabels) {
            ctx->PushState();
            ctx->Translate(position.x + textWidth * 0.5, position.y);
            ctx->Rotate(style.commitLabelAngle * kPi / 180.0);
            ctx->DrawText(text, Point2Dd(-textWidth * 0.5, 0));
            ctx->PopState();
        } else {
            ctx->DrawText(text, position);
        }
    } else {
        // Vertical axis: one label column to the right of the widest lane -
        // the repository-browser layout.
        const double columnCross = CrossCoord(layout.maxLane) + style.laneSpacing;
        const double x = std::max(center.x + cursorAcross + style.labelGap, columnCross);
        ctx->DrawText(text, Point2Dd(x, center.y - style.fontSize * 0.65));
    }
}

void UltraCanvasGitGraph::DrawFileBoxes(IRenderContext* ctx,
                                        const GitGraphPlacedCommit& placed,
                                        const GitGraphCommit& commit) {
    if (commit.files.empty()) return;

    const Point2Dd center = NodePoint(placed.row, placed.lane);
    const int side = LabelSideSign(placed.lane);
    const size_t count = std::min(commit.files.size(), style.maxFileBoxes);

    ctx->SetFontSize(style.fileBoxFontSize);

    double distance = style.fileBoxDistance;
    for (size_t i = 0; i < count; ++i) {
        const GitGraphFileChange& file = commit.files[i];

        Rect2Dd rect;
        if (IsHorizontalAxis()) {
            const double top = center.y + side * distance;
            rect = Rect2Dd(center.x - style.fileBoxWidth * 0.5,
                           side > 0 ? top : top - style.fileBoxHeight,
                           style.fileBoxWidth, style.fileBoxHeight);
        } else {
            rect = Rect2Dd(center.x + distance, center.y - style.fileBoxHeight * 0.5,
                           style.fileBoxWidth, style.fileBoxHeight);
        }

        // Leader line from the node (or the previous box) to this one.
        if (i == 0) {
            ctx->SetStrokePaint(style.fileBoxBorderColor);
            ctx->SetStrokeWidth(1.0);
            const Point2Dd from = center;
            const Point2Dd to = IsHorizontalAxis()
                ? Point2Dd(center.x, side > 0 ? rect.y : rect.y + rect.height)
                : Point2Dd(rect.x, center.y);
            ctx->DrawLine(from, to);
        }

        ctx->SetFillPaint(style.fileBoxColor);
        ctx->FillRoundedRectangle(rect, 2.0);
        ctx->SetStrokePaint(style.fileBoxBorderColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRoundedRectangle(rect, 2.0);

        ctx->SetTextPaint(style.fileBoxTextColor);
        ctx->DrawText(file.path, Point2Dd(rect.x + 4.0, rect.y + 3.0));

        distance += style.fileBoxHeight + style.fileBoxGap;
    }
}

void UltraCanvasGitGraph::DrawDecorations(IRenderContext* ctx, int firstRow, int lastRow) {
    ctx->SetFontFamily(style.fontFamily);

    for (const GitGraphPlacedCommit& placed : layout.commits) {
        if (placed.row < firstRow || placed.row > lastRow) continue;

        const GitGraphCommit* commit = GetCommit(placed.sha);
        if (!commit) continue;

        double cursorAcross = NodeRadiusFor(placed) + style.chipGap + 2.0;

        if (style.showBranchChips || style.showTagChips) {
            const std::vector<GitGraphRef> commitRefs = GetRefsForCommit(placed.sha);
            if (!commitRefs.empty()) DrawRefChips(ctx, placed, commitRefs, cursorAcross);
        }

        if (style.showFileBoxes) DrawFileBoxes(ctx, placed, *commit);
        if (style.showCommitLabels) DrawCommitLabel(ctx, placed, *commit, cursorAcross);
    }
}

void UltraCanvasGitGraph::DrawAnnotations(IRenderContext* ctx) {
    if (annotations.empty()) return;

    ctx->SetFontFamily(style.fontFamily);
    ctx->SetFontSize(style.annotationFontSize);

    for (const GitGraphAnnotation& annotation : annotations) {
        const GitGraphPlacedCommit* placed = layout.Find(annotation.sha);
        if (!placed) continue;

        const Point2Dd center = NodePoint(placed->row, placed->lane);
        const Point2Dd target = IsHorizontalAxis()
            ? Point2Dd(center.x + annotation.offsetAlongAxis,
                       center.y + annotation.offsetAcrossAxis)
            : Point2Dd(center.x + annotation.offsetAcrossAxis,
                       center.y + annotation.offsetAlongAxis);

        const Color color = annotation.hasColor ? annotation.color : style.annotationColor;

        ctx->SetStrokePaint(color.WithAlpha(140));
        ctx->SetStrokeWidth(1.0);
        ctx->DrawLine(center, target);

        const double textWidth = ctx->GetTextLineWidth(annotation.text);
        ctx->SetTextPaint(color);
        ctx->DrawText(annotation.text,
                      Point2Dd(target.x - textWidth * 0.5,
                               target.y - style.annotationFontSize));
    }
}

void UltraCanvasGitGraph::DrawTooltip(IRenderContext* ctx) {
    const GitGraphCommit* commit = GetCommit(hoveredSha);
    const GitGraphPlacedCommit* placed = layout.Find(hoveredSha);
    if (!commit || !placed) return;

    std::vector<std::string> lines;
    lines.push_back(commit->ShortSha(10) + "  " + commit->subject);
    if (!commit->authorName.empty()) {
        lines.push_back(commit->authorName + "  " + FormatTimestamp(commit->commitDate));
    }
    for (const GitGraphRef& ref : GetRefsForCommit(hoveredSha)) {
        lines.push_back((ref.type == GitGraphRefType::Tag ? "tag: " : "ref: ") +
                        ref.DisplayName());
    }
    if (!commit->files.empty()) {
        lines.push_back(std::to_string(commit->files.size()) + " file(s) changed");
    }

    ctx->SetFontFamily(style.fontFamily);
    ctx->SetFontSize(style.fontSize);

    double width = 0.0;
    for (const std::string& line : lines) {
        width = std::max(width, static_cast<double>(ctx->GetTextLineWidth(line)));
    }
    const double lineHeight = style.fontSize + 5.0;
    const double boxWidth  = width + 16.0;
    const double boxHeight = lineHeight * lines.size() + 12.0;

    const Point2Dd anchor = WorldToScreen(NodePoint(placed->row, placed->lane));
    double x = anchor.x + 14.0;
    double y = anchor.y + 14.0;
    if (x + boxWidth > GetWidth())   x = std::max(0.0, anchor.x - boxWidth - 14.0);
    if (y + boxHeight > GetHeight()) y = std::max(0.0, anchor.y - boxHeight - 14.0);

    const Rect2Dd box(x, y, boxWidth, boxHeight);
    ctx->SetFillPaint(Blend(style.backgroundColor, Color(0, 0, 0), 0.82).WithAlpha(238));
    ctx->FillRoundedRectangle(box, 5.0);
    ctx->SetStrokePaint(GetLaneColor(placed->lane));
    ctx->SetStrokeWidth(1.0);
    ctx->DrawRoundedRectangle(box, 5.0);

    ctx->SetTextPaint(Color(245, 246, 248));
    double textY = y + 6.0;
    for (const std::string& line : lines) {
        ctx->DrawText(line, Point2Dd(x + 8.0, textY));
        textY += lineHeight;
    }
}

std::string UltraCanvasGitGraph::FormatTimestamp(int64_t timestamp) const {
    if (timestamp <= 0) return std::string();
    const std::time_t value = static_cast<std::time_t>(timestamp);
    std::tm parts{};
#ifdef _WIN32
    localtime_s(&parts, &value);
#else
    localtime_r(&value, &parts);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &parts);
    return std::string(buffer);
}

// =============================================================================
// TABLE PANE
// =============================================================================

std::string UltraCanvasGitGraph::TableCellText(const GitGraphCommit& commit,
                                               GitGraphTableColumn column) const {
    switch (column) {
        case GitGraphTableColumn::Subject: return commit.subject;
        case GitGraphTableColumn::Author:  return commit.authorName;
        case GitGraphTableColumn::Sha:     return commit.ShortSha();
        case GitGraphTableColumn::Date:
            return dateFormatter ? dateFormatter(commit.commitDate)
                                 : FormatTimestamp(commit.commitDate);
        case GitGraphTableColumn::Refs: {
            std::string text;
            for (const GitGraphRef& ref : GetRefsForCommit(commit.sha)) {
                if (!text.empty()) text += ", ";
                text += ref.DisplayName();
            }
            return text;
        }
    }
    return std::string();
}

void UltraCanvasGitGraph::DrawTablePane(IRenderContext* ctx, int firstRow, int lastRow) {
    const double paneX = GraphPaneWidth();
    const double paneWidth = GetWidth() - paneX;
    if (paneWidth <= 1.0) return;

    ctx->PushState();
    ctx->ClipRect(Rect2Dd(paneX, 0, paneWidth, GetHeight()));

    ctx->SetFillPaint(style.backgroundColor);
    ctx->ClearPath();
    ctx->Rect(paneX, 0, paneWidth, GetHeight());
    ctx->FillPathPreserve();
    ctx->ClearPath();

    ctx->SetFontFamily(style.fontFamily);
    ctx->SetFontSize(style.fontSize);

    const double rowHeight = style.rowSpacing * zoomLevel;
    const double headerHeight = style.tableHeaderHeight;

    // Rows first, so the header paints over anything that scrolls under it.
    for (int row = firstRow; row <= lastRow && row < static_cast<int>(layout.commits.size() + 1);
         ++row) {
        const GitGraphPlacedCommit* placed = nullptr;
        for (const GitGraphPlacedCommit& candidate : layout.commits) {
            if (candidate.row == row) { placed = &candidate; break; }
        }
        if (!placed) continue;

        const GitGraphCommit* commit = GetCommit(placed->sha);
        if (!commit) continue;

        const double centreY = GetRowScreenPosition(row);
        const double top = centreY - rowHeight * 0.5;
        if (top + rowHeight < headerHeight || top > GetHeight()) continue;

        const bool selected = IsCommitSelected(placed->sha);
        const bool hovered  = (placed->sha == hoveredSha);
        if (selected || hovered) {
            ctx->SetFillPaint(selected ? style.tableRowSelectedColor : style.tableRowHoverColor);
            ctx->ClearPath();
            ctx->Rect(paneX, top, paneWidth, rowHeight);
            ctx->FillPathPreserve();
            ctx->ClearPath();
        }

        double cellX = paneX + style.tableCellPadding;
        for (const GitGraphTableColumnSpec& column : tableColumns) {
            if (!column.visible) continue;
            if (cellX > GetWidth()) break;

            const std::string text = TableCellText(*commit, column.column);
            if (!text.empty()) {
                ctx->PushState();
                ctx->ClipRect(Rect2Dd(cellX, top, column.width - style.tableCellPadding,
                                      rowHeight));
                ctx->SetTextPaint(column.column == GitGraphTableColumn::Subject
                                      ? style.tableTextColor
                                      : style.tableMutedColor);
                ctx->DrawText(text, Point2Dd(cellX, centreY - style.fontSize * 0.65));
                ctx->PopState();
            }
            cellX += column.width;
        }
    }

    // Header
    ctx->SetFillPaint(style.tableHeaderColor);
    ctx->ClearPath();
    ctx->Rect(paneX, 0, paneWidth, headerHeight);
    ctx->FillPathPreserve();
    ctx->ClearPath();

    ctx->SetStrokePaint(style.tableGridColor);
    ctx->SetStrokeWidth(1.0);
    ctx->DrawLine(Point2Dd(paneX, headerHeight), Point2Dd(GetWidth(), headerHeight));
    ctx->DrawLine(Point2Dd(paneX, 0), Point2Dd(paneX, GetHeight()));

    double headerX = paneX + style.tableCellPadding;
    ctx->SetTextPaint(style.tableMutedColor);
    for (const GitGraphTableColumnSpec& column : tableColumns) {
        if (!column.visible) continue;
        if (headerX > GetWidth()) break;
        ctx->DrawText(column.title, Point2Dd(headerX, (headerHeight - style.fontSize) * 0.5));
        headerX += column.width;
        ctx->DrawLine(Point2Dd(headerX - style.tableCellPadding, 0),
                      Point2Dd(headerX - style.tableCellPadding, headerHeight));
    }

    ctx->PopState();
}

// =============================================================================
// MINIMAP
// =============================================================================

Rect2Dd UltraCanvasGitGraph::ComputeMinimapBounds() const {
    const double width  = std::min(style.minimapWidth,  GetWidth()  * 0.4);
    const double height = std::min(style.minimapHeight, GetHeight() * 0.6);
    const double margin = style.minimapMargin;

    double x = GetWidth() - width - margin;
    double y = margin;
    switch (style.minimapPosition) {
        case GitGraphMinimapPosition::TopLeft:     x = margin; y = margin; break;
        case GitGraphMinimapPosition::BottomLeft:  x = margin; y = GetHeight() - height - margin;
                                                   break;
        case GitGraphMinimapPosition::BottomRight: y = GetHeight() - height - margin; break;
        case GitGraphMinimapPosition::TopRight:
        default: break;
    }
    return Rect2Dd(x, y, width, height);
}

void UltraCanvasGitGraph::DrawMinimap(IRenderContext* ctx) {
    if (layout.commits.empty()) return;

    const Rect2Dd bounds = ComputeMinimapBounds();
    minimapBounds = bounds;

    ctx->SetFillPaint(style.minimapBackgroundColor);
    ctx->FillRoundedRectangle(bounds, 4.0);
    ctx->SetStrokePaint(style.minimapBorderColor);
    ctx->SetStrokeWidth(1.0);
    ctx->DrawRoundedRectangle(bounds, 4.0);

    const double inset = 4.0;
    const double plotWidth  = bounds.width  - inset * 2.0;
    const double plotHeight = bounds.height - inset * 2.0;
    if (plotWidth <= 0.0 || plotHeight <= 0.0) return;

    const int laneSpan = std::max(1, layout.maxLane - layout.minLane);
    const int rowSpan  = std::max(1, layout.rowCount - 1);

    // One tick per commit: rows down the long side, lanes across the short one.
    const double tick = std::max(1.0, std::min(plotWidth / (laneSpan + 1),
                                               plotHeight / static_cast<double>(rowSpan + 1)));
    for (const GitGraphPlacedCommit& placed : layout.commits) {
        const double rowFraction  = static_cast<double>(placed.row) / rowSpan;
        const double laneFraction = static_cast<double>(placed.lane - layout.minLane) / laneSpan;

        const double x = bounds.x + inset + laneFraction * (plotWidth - tick);
        const double y = bounds.y + inset + rowFraction  * (plotHeight - tick);

        ctx->SetFillPaint(CommitColor(placed));
        ctx->ClearPath();
        ctx->Rect(x, y, tick, tick);
        ctx->FillPathPreserve();
        ctx->ClearPath();
    }

    // Viewport rectangle: which rows are on screen right now.
    int firstRow = 0, lastRow = 0;
    ComputeVisibleRows(firstRow, lastRow);
    const double fromFraction = static_cast<double>(firstRow) / rowSpan;
    const double toFraction   = static_cast<double>(lastRow)  / rowSpan;

    const double viewY = bounds.y + inset + fromFraction * plotHeight;
    const double viewH = std::max(3.0, (toFraction - fromFraction) * plotHeight);

    ctx->SetFillPaint(style.minimapViewportColor);
    ctx->ClearPath();
    ctx->Rect(bounds.x + inset, viewY, plotWidth, viewH);
    ctx->FillPathPreserve();
    ctx->ClearPath();

    ctx->SetStrokePaint(style.selectionColor);
    ctx->SetStrokeWidth(1.0);
    ctx->DrawRectangle(Rect2Dd(bounds.x + inset, viewY, plotWidth, viewH));
}

// Jumps the view to the row the pointer is over inside the minimap.
bool UltraCanvasGitGraph::HandleMinimapPointer(const Point2Di& position) {
    if (!style.showMinimap || layout.rowCount <= 0) return false;

    const Rect2Dd bounds = minimapBounds;
    if (bounds.width <= 0.0) return false;
    if (position.x < bounds.x || position.x > bounds.x + bounds.width) return false;
    if (position.y < bounds.y || position.y > bounds.y + bounds.height) return false;

    const double inset = 4.0;
    const double plotHeight = bounds.height - inset * 2.0;
    if (plotHeight <= 0.0) return false;

    const double fraction =
        std::clamp((position.y - bounds.y - inset) / plotHeight, 0.0, 1.0);
    const int row = std::clamp(static_cast<int>(std::lround(fraction * (layout.rowCount - 1))),
                               0, layout.rowCount - 1);

    for (const GitGraphPlacedCommit& placed : layout.commits) {
        if (placed.row != row) continue;
        CenterOnCommit(placed.sha);
        return true;
    }
    return true;
}

// =============================================================================
// EVENTS
// =============================================================================

bool UltraCanvasGitGraph::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseMove:        return HandleMouseMove(event);
        case UCEventType::MouseDown:        return HandleMouseDown(event);
        case UCEventType::MouseUp:          return HandleMouseUp(event);
        case UCEventType::MouseWheel:       return HandleMouseWheel(event);
        case UCEventType::MouseDoubleClick: return HandleDoubleClick(event);
        case UCEventType::KeyDown:          return HandleKeyDown(event);

        case UCEventType::MouseLeave:
            if (!hoveredSha.empty()) {
                hoveredSha.clear();
                RequestRedraw();
            }
            return true;

        default:
            break;
    }
    return UltraCanvasUIElement::OnEvent(event);
}

bool UltraCanvasGitGraph::HandleMouseMove(const UCEvent& event) {
    const Point2Di mousePos(event.pointer.x, event.pointer.y);

    if (isDraggingMinimap) {
        HandleMinimapPointer(mousePos);
        return true;
    }

    if (IsTableActive() && mousePos.x >= GraphPaneWidth()) {
        // Hovering a table row highlights the commit on the graph too.
        const int row = GetRowAtScreenPosition(mousePos.y);
        std::string sha;
        for (const GitGraphPlacedCommit& placed : layout.commits) {
            if (placed.row == row) { sha = placed.sha; break; }
        }
        if (sha != hoveredSha) {
            hoveredSha = sha;
            if (!hoveredSha.empty() && onCommitHover) onCommitHover(hoveredSha);
            RequestRedraw();
        }
        return true;
    }

    if (isPanning) {
        panX = panStartOffset.x + (mousePos.x - dragStartPos.x);
        panY = panStartOffset.y + (mousePos.y - dragStartPos.y);
        RequestRedraw();
        return true;
    }

    const std::string sha = FindCommitAt(ScreenToWorld(mousePos));
    if (sha != hoveredSha) {
        hoveredSha = sha;
        if (!hoveredSha.empty() && onCommitHover) onCommitHover(hoveredSha);
        RequestRedraw();
    }
    return !hoveredSha.empty();
}

bool UltraCanvasGitGraph::HandleMouseDown(const UCEvent& event) {
    const Point2Di mousePos(event.pointer.x, event.pointer.y);

    if (event.button == UCMouseButton::Left && HandleMinimapPointer(mousePos)) {
        isDraggingMinimap = true;
        return true;
    }

    if (IsTableActive() && mousePos.x >= GraphPaneWidth() &&
        mousePos.y >= style.tableHeaderHeight) {
        const int row = GetRowAtScreenPosition(mousePos.y);
        for (const GitGraphPlacedCommit& placed : layout.commits) {
            if (placed.row != row) continue;
            if (event.button == UCMouseButton::Right) {
                if (onCommitRightClick) onCommitRightClick(placed.sha);
            } else {
                SelectCommit(placed.sha, event.ctrl || event.shift);
                if (onCommitClick) onCommitClick(placed.sha);
            }
            return true;
        }
        return true;
    }

    if (event.button == UCMouseButton::Middle) {
        isPanning = true;
        dragStartPos = mousePos;
        panStartOffset = Point2Dd(panX, panY);
        return true;
    }

    const std::string sha = FindCommitAt(ScreenToWorld(mousePos));

    if (event.button == UCMouseButton::Right) {
        if (!sha.empty()) {
            if (onCommitRightClick) onCommitRightClick(sha);
            return true;
        }
        isPanning = true;
        dragStartPos = mousePos;
        panStartOffset = Point2Dd(panX, panY);
        return true;
    }

    if (event.button == UCMouseButton::Left) {
        if (!sha.empty()) {
            SelectCommit(sha, event.ctrl || event.shift);
            if (onCommitClick) onCommitClick(sha);
            return true;
        }
        DeselectAll();
        isPanning = true;
        dragStartPos = mousePos;
        panStartOffset = Point2Dd(panX, panY);
        return true;
    }

    return false;
}

bool UltraCanvasGitGraph::HandleMouseUp(const UCEvent& event) {
    if (isDraggingMinimap) {
        isDraggingMinimap = false;
        return true;
    }
    if (isPanning) {
        isPanning = false;
        return true;
    }
    return false;
}

bool UltraCanvasGitGraph::HandleMouseWheel(const UCEvent& event) {
    const Point2Di mousePos(event.pointer.x, event.pointer.y);
    const Point2Dd worldBefore = ScreenToWorld(mousePos);

    const float factor = (event.wheelDelta > 0) ? 1.12f : 0.89f;
    const float newZoom = std::clamp(zoomLevel * factor, minZoom, maxZoom);
    if (newZoom == zoomLevel) return true;

    zoomLevel = newZoom;

    // Keep the point under the cursor fixed.
    const Point2Dd worldAfter = ScreenToWorld(mousePos);
    panX += (worldAfter.x - worldBefore.x) * zoomLevel;
    panY += (worldAfter.y - worldBefore.y) * zoomLevel;

    RequestRedraw();
    return true;
}

bool UltraCanvasGitGraph::HandleDoubleClick(const UCEvent& event) {
    const std::string sha = FindCommitAt(ScreenToWorld(Point2Di(event.pointer.x,
                                                                event.pointer.y)));
    if (sha.empty()) return false;
    if (onCommitDoubleClick) onCommitDoubleClick(sha);
    return true;
}

bool UltraCanvasGitGraph::HandleKeyDown(const UCEvent& event) {
    if (layout.commits.empty()) return false;

    // Navigation works in (row, lane) space: along the history axis with
    // up/down, across lanes with left/right.
    const GitGraphPlacedCommit* current =
        selectedShas.empty() ? nullptr : layout.Find(selectedShas.back());
    const int currentRow  = current ? current->row  : 0;
    const int currentLane = current ? current->lane : 0;

    auto selectRow = [&](int row) {
        row = std::clamp(row, 0, layout.rowCount - 1);
        for (const GitGraphPlacedCommit& placed : layout.commits) {
            if (placed.row == row) {
                SelectCommit(placed.sha);
                CenterOnCommit(placed.sha);
                return true;
            }
        }
        return false;
    };

    auto selectNearestLane = [&](int direction) {
        const GitGraphPlacedCommit* best = nullptr;
        for (const GitGraphPlacedCommit& placed : layout.commits) {
            const int delta = (placed.lane - currentLane) * direction;
            if (delta <= 0) continue;
            if (!best ||
                std::abs(placed.row - currentRow) < std::abs(best->row - currentRow) ||
                (placed.row == best->row && delta < (best->lane - currentLane) * direction)) {
                best = &placed;
            }
        }
        if (!best) return false;
        SelectCommit(best->sha);
        return true;
    };

    switch (event.virtualKey) {
        case UCKeys::Up:       return selectRow(currentRow - 1);
        case UCKeys::Down:     return selectRow(currentRow + 1);
        case UCKeys::Left:     return selectNearestLane(-1);
        case UCKeys::Right:    return selectNearestLane(1);
        case UCKeys::Home:     return selectRow(0);
        case UCKeys::End:      return selectRow(layout.rowCount - 1);
        case UCKeys::PageUp:   return selectRow(currentRow - 10);
        case UCKeys::PageDown: return selectRow(currentRow + 10);

        case UCKeys::Return:
            if (!selectedShas.empty() && onCommitDoubleClick) {
                onCommitDoubleClick(selectedShas.back());
                return true;
            }
            return false;

        default:
            break;
    }
    return false;
}

// =============================================================================
// SVG EXPORT
// =============================================================================

bool UltraCanvasGitGraph::SaveToSVG(const std::string& filePath) {
    if (needsLayout) PerformLayout();

    std::ofstream file(filePath);
    if (!file.is_open()) return false;

    // Size the canvas from the real node bounding box rather than the content
    // extents: in swimlane mode lane 0 is centred on the element, so the
    // extents alone would clip the bands. The padding leaves room for labels.
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
    bool first = true;
    for (const GitGraphPlacedCommit& placed : layout.commits) {
        const Point2Dd point = NodePoint(placed.row, placed.lane);
        if (first) {
            minX = maxX = point.x;
            minY = maxY = point.y;
            first = false;
        } else {
            minX = std::min(minX, point.x);  maxX = std::max(maxX, point.x);
            minY = std::min(minY, point.y);  maxY = std::max(maxY, point.y);
        }
    }

    const double padding = style.showCommitLabels ? 110.0 : 24.0;
    const double width   = (maxX - minX) + padding * 2.0;
    const double height  = (maxY - minY) + padding * 2.0;
    const double offsetX = padding - minX;
    const double offsetY = padding - minY;

    auto hex = [](const Color& c) {
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", c.r, c.g, c.b);
        return std::string(buffer);
    };

    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
         << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << " " << height
         << "\">\n";
    file << "  <rect width=\"100%\" height=\"100%\" fill=\"" << hex(style.backgroundColor)
         << "\"/>\n";
    file << "  <g transform=\"translate(" << offsetX << " " << offsetY << ")\">\n";

    for (const GitGraphPlacedEdge& edge : layout.edges) {
        Point2Dd start, corner, end;
        BuildEdgeGeometry(edge, start, corner, end);
        const Color color = EdgeColor(edge);
        const bool dashed = (edge.kind == GitGraphEdgeKind::CherryPick ||
                             edge.kind == GitGraphEdgeKind::Skipped);

        file << "  <path d=\"M " << start.x << " " << start.y;
        if (edge.fromLane == edge.toLane || style.edgeStyle == GitGraphEdgeStyle::Straight) {
            file << " L " << end.x << " " << end.y;
        } else {
            file << " Q " << corner.x << " " << corner.y << " " << end.x << " " << end.y;
        }
        file << "\" fill=\"none\" stroke=\"" << hex(color) << "\" stroke-width=\""
             << style.edgeWidth << "\"";
        if (dashed) file << " stroke-dasharray=\"4,4\"";
        file << "/>\n";
    }

    for (const GitGraphPlacedCommit& placed : layout.commits) {
        const Point2Dd center = NodePoint(placed.row, placed.lane);
        const double radius = NodeRadiusFor(placed);
        const Color color = CommitColor(placed);
        file << "  <circle cx=\"" << center.x << "\" cy=\"" << center.y << "\" r=\""
             << radius << "\" fill=\"" << hex(color) << "\"/>\n";

        const GitGraphCommit* commit = GetCommit(placed.sha);
        if (commit && style.showCommitLabels) {
            const double labelX = IsHorizontalAxis()
                                      ? center.x
                                      : CrossCoord(layout.maxLane) + style.laneSpacing;
            const double labelY = IsHorizontalAxis()
                                      ? center.y - radius - style.labelGap
                                      : center.y + style.fontSize * 0.35;
            file << "  <text x=\"" << labelX << "\" y=\"" << labelY << "\" font-family=\""
                 << style.fontFamily << "\" font-size=\"" << style.fontSize << "\" fill=\""
                 << hex(style.textColor) << "\"";
            if (IsHorizontalAxis()) file << " text-anchor=\"middle\"";
            file << ">";
            for (char c : CommitLabelText(*commit)) {
                if (c == '<') file << "&lt;";
                else if (c == '>') file << "&gt;";
                else if (c == '&') file << "&amp;";
                else file << c;
            }
            file << "</text>\n";
        }
    }

    file << "  </g>\n</svg>\n";
    return true;
}

} // namespace UltraCanvas
