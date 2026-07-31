// include/Plugins/Diagrams/UltraCanvasGitGraphMermaid.h
// Mermaid `gitGraph` import and export for UltraCanvasGitGraph.
//
// Headless: converts between the Mermaid DSL and the plain commit/ref model,
// with no rendering or windowing dependencies, so it is unit-testable on its
// own (see Tests/GitGraphMermaidTest.cpp).
//
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#pragma once

#include "Plugins/Diagrams/UltraCanvasGitGraphTypes.h"

#include <string>
#include <vector>

namespace UltraCanvas {

// Everything a `gitGraph` block can express, plus the diagnostics needed to
// report a bad line back to the caller.
struct GitGraphMermaidDocument {
    bool        ok = false;
    std::string error;              // Empty when ok
    int         errorLine = 0;      // 1-based

    std::vector<GitGraphCommit> commits;    // Oldest first, as authored
    std::vector<GitGraphRef>    refs;

    // Directives and config
    GitGraphOrientation orientation    = GitGraphOrientation::LeftToRight;
    std::string         mainBranchName = "main";
    bool                showBranches     = true;
    bool                showCommitLabel  = true;
    bool                rotateCommitLabel = true;
    bool                parallelCommits  = false;

    // Branch declaration order, honouring any `order:` attributes. Feeds
    // GitGraphLayoutOptions::swimlaneOrder.
    std::vector<std::string> branchOrder;
};

// Parse a Mermaid `gitGraph` block. Supports commit / branch / checkout /
// switch / merge / cherry-pick, the id, tag, type, msg, order and parent
// attributes, `%%` comments, an `%%{init: {...}}%%` header, and the LR / TB /
// BT direction suffixes.
GitGraphMermaidDocument ParseMermaidGitGraph(const std::string& text);

// Render commits and refs back into the DSL. The DAG is walked oldest-first
// and turned into branch / checkout / commit / merge / cherry-pick commands.
// Histories whose branch membership cannot be recovered (no `branch` field and
// no refs) collapse onto the main branch, which is lossy but still valid.
std::string ExportMermaidGitGraph(const std::vector<GitGraphCommit>& commits,
                                  const std::vector<GitGraphRef>& refs,
                                  const std::string& mainBranchName = "main");

} // namespace UltraCanvas
