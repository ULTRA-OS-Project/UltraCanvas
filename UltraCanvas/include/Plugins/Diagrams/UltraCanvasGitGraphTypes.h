// include/Plugins/Diagrams/UltraCanvasGitGraphTypes.h
// Data model for the Git commit-graph element: commits, refs, annotations and
// the style/theme description. Deliberately free of rendering and windowing
// dependencies so the layout core (UltraCanvasGitGraphLayout) can be built and
// unit-tested without a UI stack.
//
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== ENUMERATIONS =====

// Direction time flows in. LeftToRight and TopToBottom put the oldest commit
// at the start of the axis (the teaching-diagram convention); BottomToTop and
// RightToLeft put the newest there, which is what every repository browser
// does - gitk, SourceTree and `git log` all show HEAD at the top.
enum class GitGraphOrientation {
    LeftToRight,
    TopToBottom,
    BottomToTop,
    RightToLeft
};

// Lanes    - one lane per concurrently open line of development (gitk, GitKraken).
// Swimlane - one band per branch, distributed either side of a trunk (git-flow
//            diagrams; the branch of each commit comes from GitGraphCommit::branch
//            or is derived by a first-parent walk from the refs).
enum class GitGraphLayoutMode {
    Lanes,
    Swimlane
};

// Compact - a freed lane is reused immediately (narrow graph, branches wander
//           sideways). This is gitk / `git log --graph`.
// Stable  - a lane is only reused when no active lane lies to its right, so a
//           branch stays on one lane for its whole life and never crosses to
//           claim a slot. This is the GitKraken "straight branches" look.
enum class GitGraphLaneStrategy {
    Compact,
    Stable
};

enum class GitGraphOrderMode {
    AsGiven,        // Trust the order commits were added in (git log order)
    CommitDate,     // Newest committer date first
    AuthorDate,     // Newest author date first
    Topological     // Parents after children, branch chunks kept contiguous
};

enum class GitGraphEdgeStyle {
    Straight,       // Direct line child -> parent
    Orthogonal,     // Axis-aligned with rounded corners (VS Code Git Graph)
    Bezier,         // Cubic curve (Git Extensions / SmartGit)
    Arc             // Quarter-circle corner (the git-flow teaching diagram)
};

enum class GitGraphNodeShape {
    Circle,
    Ring,
    Square,
    Diamond
};

enum class GitGraphCommitType {
    Normal,
    Reverse,        // Crossed circle - a revert
    Highlight       // Filled rectangle - emphasised commit
};

enum class GitGraphRefType {
    LocalBranch,
    RemoteBranch,
    Tag,
    Head,
    Stash
};

enum class GitGraphEdgeKind {
    Parent,         // First-parent link - the lane continues
    Merge,          // Second and later parents of a merge commit
    CherryPick,     // Non-parent relation, drawn dashed
    Skipped         // Link across commits hidden by a filter
};

enum class GitGraphTheme {
    Default,
    Dark,
    Light,
    Colorful,
    Monochrome,
    Custom
};

// ===== DATA STRUCTURES =====

struct GitGraphFileChange {
    std::string path;
    int  added   = 0;
    int  removed = 0;
    char status  = 'M';         // A added, M modified, D deleted, R renamed

    GitGraphFileChange() = default;
    GitGraphFileChange(const std::string& p, char st = 'M', int add = 0, int del = 0)
        : path(p), added(add), removed(del), status(st) {}
};

struct GitGraphCommit {
    // Identity
    std::string sha;
    std::vector<std::string> parents;

    // Message
    std::string subject;
    std::string body;

    // People and time (Unix seconds)
    std::string authorName;
    std::string authorEmail;
    std::string committerName;
    int64_t     authorDate = 0;
    int64_t     commitDate = 0;

    // Branch this commit belongs to. Optional: only Swimlane mode needs it, and
    // the layout derives it from the refs when it is empty.
    std::string branch;

    // Payload
    std::vector<GitGraphFileChange> files;

    // Appearance
    GitGraphCommitType type = GitGraphCommitType::Normal;
    bool  hasColorOverride  = false;
    Color colorOverride     = Color(0, 0, 0);

    // Set when this commit was cherry-picked from another; drawn as a dashed
    // non-parent edge back to the source.
    std::string cherryPickSource;

    bool IsMerge() const { return parents.size() > 1; }
    bool IsRoot()  const { return parents.empty(); }

    std::string ShortSha(size_t length = 7) const {
        return sha.size() > length ? sha.substr(0, length) : sha;
    }
};

struct GitGraphRef {
    std::string     name;                                   // "main", "v1.0", "feature/x"
    std::string     sha;
    std::string     remote;                                 // "origin" for remote branches
    GitGraphRefType type      = GitGraphRefType::LocalBranch;
    bool            isCurrent = false;                      // HEAD points here
    bool            annotated = false;                      // Annotated (not lightweight) tag

    std::string DisplayName() const {
        return (type == GitGraphRefType::RemoteBranch && !remote.empty())
                   ? remote + "/" + name
                   : name;
    }
};

// Free-standing callout attached to a commit - "release branch for 1.0",
// "only bugfixes", "major feature for the next release" in the git-flow diagram.
struct GitGraphAnnotation {
    std::string sha;
    std::string text;
    bool        hasColor = false;
    Color       color    = Color(0, 0, 0);
    double      offsetAlongAxis = 0.0;      // Nudge the callout off the node
    double      offsetAcrossAxis = -46.0;
};

// ===== STYLE =====

struct GitGraphStyle {
    // Surface
    Color backgroundColor = Color(255, 255, 255);
    Color plotAreaColor   = Color(255, 255, 255);
    bool  showRowStripes  = false;
    Color rowStripeColor  = Color(246, 248, 250);

    // Geometry
    double laneSpacing    = 26.0;       // Distance between adjacent lanes
    double rowSpacing     = 30.0;       // Distance between adjacent commits
    double marginAlongAxis  = 40.0;
    double marginAcrossAxis = 40.0;
    double nodeRadius     = 6.0;
    double mergeNodeRadius = 4.5;
    double rootNodeRadius = 7.0;
    double edgeWidth      = 2.0;
    double cornerRadius   = 12.0;       // Orthogonal/Arc corner rounding

    // Nodes
    GitGraphNodeShape nodeShape      = GitGraphNodeShape::Circle;
    GitGraphNodeShape mergeNodeShape = GitGraphNodeShape::Ring;
    bool   drawNodeHalo   = true;       // Background-coloured ring so crossing
    double nodeHaloWidth  = 2.0;        // edges read cleanly behind a node
    Color  nodeBorderColor = Color(255, 255, 255);

    // Edges
    GitGraphEdgeStyle edgeStyle       = GitGraphEdgeStyle::Orthogonal;
    bool              colorEdgeByChild = false;   // false = parent's lane colour
    bool              showEdgeArrows  = false;
    UCDashPattern     cherryPickDash  = UCDashPattern({4.0, 4.0});

    // Trunk baseline (swimlane mode)
    bool   showTrunkBaseline   = true;
    bool   showTrunkArrow      = true;
    double trunkBaselineWidth  = 2.0;

    // Swimlane bands
    bool   showSwimlaneLabels  = true;
    bool   showSwimlaneBands   = true;
    Color  swimlaneBandColor   = Color(248, 249, 250);
    double swimlaneLabelWidth  = 118.0;

    // Labels
    bool        showCommitLabels = true;
    bool        rotateCommitLabels = false;
    double      commitLabelAngle = -45.0;
    std::string fontFamily     = "Arial";
    double      fontSize       = 11.0;
    double      chipFontSize   = 10.0;
    Color       textColor      = Color(32, 34, 38);
    Color       mutedTextColor = Color(110, 116, 124);
    double      labelGap       = 12.0;      // Node edge -> label

    // Ref chips
    bool   showBranchChips = true;
    bool   showTagChips    = true;
    bool   showHeadMarker  = true;
    double chipPadding     = 5.0;
    double chipRadius      = 4.0;
    double chipGap         = 4.0;
    Color  tagChipColor    = Color(255, 213, 79);
    Color  tagChipTextColor = Color(60, 44, 0);
    Color  headChipColor   = Color(46, 160, 67);
    Color  headChipTextColor = Color(255, 255, 255);
    size_t maxChipsPerCommit = 3;           // Beyond this a "+n" chip is drawn

    // File boxes (the per-commit changed-file column)
    bool   showFileBoxes   = false;
    double fileBoxWidth    = 78.0;
    double fileBoxHeight   = 17.0;
    double fileBoxGap      = 3.0;
    double fileBoxDistance = 20.0;          // Node edge -> first box
    double fileBoxFontSize = 9.0;
    Color  fileBoxColor    = Color(255, 255, 255);
    Color  fileBoxBorderColor = Color(190, 196, 204);
    Color  fileBoxTextColor = Color(60, 64, 70);
    size_t maxFileBoxes    = 6;

    // Annotations
    double annotationFontSize = 11.0;
    Color  annotationColor    = Color(120, 126, 134);

    // Interaction feedback
    Color  hoverColor      = Color(255, 193, 7);
    Color  selectionColor  = Color(0, 122, 204);
    double selectionWidth  = 2.0;
    bool   showTooltips    = true;

    // Lane palette - cycled by lane index (mermaid's git0..git7 equivalent)
    std::vector<Color> lanePalette = {
        Color(33, 150, 243),   // blue
        Color(76, 175, 80),    // green
        Color(233, 30, 99),    // magenta
        Color(255, 152, 0),    // orange
        Color(156, 39, 176),   // purple
        Color(0, 188, 212),    // cyan
        Color(244, 67, 54),    // red
        Color(121, 85, 72)     // brown
    };
};

} // namespace UltraCanvas
