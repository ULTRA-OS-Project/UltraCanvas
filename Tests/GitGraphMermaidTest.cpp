// Tests/GitGraphMermaidTest.cpp
// Unit tests for Mermaid `gitGraph` import/export: commit/branch/checkout/
// merge/cherry-pick statements, id/tag/type/order attributes, the init header,
// direction suffixes, error reporting, and an import -> export -> import
// round trip.
//
// Headless - no UI stack, no link dependencies beyond the mermaid source.
//
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasGitGraphMermaid.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }               \
    } while (0)

static const GitGraphCommit* Find(const GitGraphMermaidDocument& document,
                                  const std::string& sha) {
    for (const GitGraphCommit& commit : document.commits) {
        if (commit.sha == sha) return &commit;
    }
    return nullptr;
}

static const GitGraphRef* FindRef(const GitGraphMermaidDocument& document,
                                  const std::string& name) {
    for (const GitGraphRef& ref : document.refs) {
        if (ref.name == name) return &ref;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------

static void TestBasicCommits() {
    std::printf("Basic commits (ids, parents, tags)\n");

    const GitGraphMermaidDocument document = ParseMermaidGitGraph(R"(
gitGraph
    commit id: "one"
    commit id: "two" tag: "v1.0"
    commit id: "three"
)");

    CHECK(document.ok, "parses");
    CHECK(document.commits.size() == 3, "three commits");
    CHECK(Find(document, "one") && Find(document, "one")->parents.empty(),
          "the first commit is a root");
    CHECK(Find(document, "two") && Find(document, "two")->parents.size() == 1 &&
          Find(document, "two")->parents[0] == "one",
          "each commit takes the branch tip as its parent");
    const GitGraphRef* tag = FindRef(document, "v1.0");
    CHECK(tag && tag->type == GitGraphRefType::Tag && tag->sha == "two",
          "the tag attaches to its commit");
    CHECK(document.mainBranchName == "main", "the default main branch is 'main'");
}

static void TestBranchMerge() {
    std::printf("Branch, checkout and merge\n");

    const GitGraphMermaidDocument document = ParseMermaidGitGraph(R"(
gitGraph
    commit id: "base"
    branch develop
    commit id: "d1"
    checkout main
    commit id: "m1"
    merge develop id: "merged"
)");

    CHECK(document.ok, "parses");
    CHECK(document.commits.size() == 4, "four commits");

    const GitGraphCommit* branchCommit = Find(document, "d1");
    CHECK(branchCommit && branchCommit->branch == "develop",
          "a branch commit records its branch");
    CHECK(branchCommit && branchCommit->parents.size() == 1 &&
          branchCommit->parents[0] == "base",
          "the branch forks from the tip at branch time");

    const GitGraphCommit* merge = Find(document, "merged");
    CHECK(merge && merge->parents.size() == 2, "the merge has two parents");
    CHECK(merge && merge->parents[0] == "m1" && merge->parents[1] == "d1",
          "first parent is the current branch, second is the merged branch");
    CHECK(merge && merge->branch == "main", "the merge belongs to the target branch");

    const GitGraphRef* develop = FindRef(document, "develop");
    CHECK(develop && develop->sha == "d1", "the branch ref points at its tip");
    CHECK(document.branchOrder.size() == 2 && document.branchOrder[0] == "main",
          "the branch order starts with main");
}

static void TestCommitTypesAndSwitch() {
    std::printf("Commit types and the `switch` alias\n");

    const GitGraphMermaidDocument document = ParseMermaidGitGraph(R"(
gitGraph
    commit id: "a" type: HIGHLIGHT
    branch topic
    commit id: "b" type: REVERSE
    switch main
    commit id: "c"
)");

    CHECK(document.ok, "parses");
    CHECK(Find(document, "a") && Find(document, "a")->type == GitGraphCommitType::Highlight,
          "HIGHLIGHT is read");
    CHECK(Find(document, "b") && Find(document, "b")->type == GitGraphCommitType::Reverse,
          "REVERSE is read");
    CHECK(Find(document, "c") && Find(document, "c")->branch == "main",
          "`switch` behaves like `checkout`");
}

static void TestCherryPick() {
    std::printf("Cherry-pick\n");

    const GitGraphMermaidDocument document = ParseMermaidGitGraph(R"(
gitGraph
    commit id: "base"
    branch topic
    commit id: "fix"
    checkout main
    cherry-pick id: "fix"
)");

    CHECK(document.ok, "parses");
    CHECK(document.commits.size() == 3, "the cherry-pick adds one commit");

    const GitGraphCommit* picked = &document.commits.back();
    CHECK(picked->cherryPickSource == "fix", "the source commit is recorded");
    CHECK(picked->branch == "main", "the picked commit lands on the current branch");
    CHECK(picked->parents.size() == 1 && picked->parents[0] == "base",
          "its parent is the current tip, not the source");
}

static void TestDirectivesAndConfig() {
    std::printf("Direction suffix and the init header\n");

    const GitGraphMermaidDocument lr = ParseMermaidGitGraph("gitGraph LR:\n    commit\n");
    CHECK(lr.ok && lr.orientation == GitGraphOrientation::LeftToRight, "LR maps to LeftToRight");

    const GitGraphMermaidDocument tb = ParseMermaidGitGraph("gitGraph TB:\n    commit\n");
    CHECK(tb.ok && tb.orientation == GitGraphOrientation::TopToBottom, "TB maps to TopToBottom");

    const GitGraphMermaidDocument bt = ParseMermaidGitGraph("gitGraph BT:\n    commit\n");
    CHECK(bt.ok && bt.orientation == GitGraphOrientation::BottomToTop, "BT maps to BottomToTop");

    const GitGraphMermaidDocument init = ParseMermaidGitGraph(R"(
%%{init: { 'gitGraph': { 'mainBranchName': 'trunk', 'showCommitLabel': false }} }%%
gitGraph
    commit id: "x"
)");
    CHECK(init.ok, "the init header parses");
    CHECK(init.mainBranchName == "trunk", "mainBranchName is honoured");
    CHECK(!init.showCommitLabel, "showCommitLabel is honoured");
    CHECK(Find(init, "x") && Find(init, "x")->branch == "trunk",
          "commits land on the renamed main branch");
}

static void TestBranchOrder() {
    std::printf("Branch order attribute\n");

    const GitGraphMermaidDocument document = ParseMermaidGitGraph(R"(
gitGraph
    commit
    branch feature order: 3
    commit
    checkout main
    branch hotfix order: 1
    commit
)");

    CHECK(document.ok, "parses");
    CHECK(document.branchOrder.size() == 3, "three branches declared");
    const auto hotfix = std::find(document.branchOrder.begin(), document.branchOrder.end(),
                                  "hotfix");
    const auto feature = std::find(document.branchOrder.begin(), document.branchOrder.end(),
                                   "feature");
    CHECK(hotfix < feature, "a lower order: value sorts first");
}

static void TestComments() {
    std::printf("Comments are ignored\n");

    const GitGraphMermaidDocument document = ParseMermaidGitGraph(R"(
gitGraph
    %% a whole-line comment
    commit id: "a"
    commit id: "b"  %% a trailing comment
)");

    CHECK(document.ok, "parses");
    CHECK(document.commits.size() == 2, "comments do not become commits");
    CHECK(Find(document, "b") != nullptr, "a trailing comment is stripped, not the statement");
}

static void TestErrors() {
    std::printf("Error reporting\n");

    const GitGraphMermaidDocument noDirective = ParseMermaidGitGraph("flowchart TD\n  A --> B\n");
    CHECK(!noDirective.ok, "a non-gitGraph diagram is rejected");

    const GitGraphMermaidDocument unknownBranch =
        ParseMermaidGitGraph("gitGraph\n    commit\n    checkout nope\n");
    CHECK(!unknownBranch.ok, "checking out an unknown branch fails");
    CHECK(unknownBranch.errorLine == 3, "the failing line is reported");

    const GitGraphMermaidDocument badStatement =
        ParseMermaidGitGraph("gitGraph\n    commit\n    rebase main\n");
    CHECK(!badStatement.ok, "an unsupported statement fails");

    const GitGraphMermaidDocument duplicate =
        ParseMermaidGitGraph("gitGraph\n    commit id: \"a\"\n    commit id: \"a\"\n");
    CHECK(!duplicate.ok, "a duplicate commit id fails");

    const GitGraphMermaidDocument danglingPick =
        ParseMermaidGitGraph("gitGraph\n    commit\n    cherry-pick id: \"missing\"\n");
    CHECK(!danglingPick.ok, "cherry-picking an unknown commit fails");
}

static void TestRoundTrip() {
    std::printf("Round trip (import -> export -> import)\n");

    const std::string source = R"(
gitGraph
    commit id: "base"
    branch develop
    commit id: "d1"
    branch feature
    commit id: "f1"
    checkout develop
    merge feature id: "md"
    checkout main
    merge develop id: "mm" tag: "v1.0"
)";

    const GitGraphMermaidDocument first = ParseMermaidGitGraph(source);
    CHECK(first.ok, "the original parses");

    const std::string exported = ExportMermaidGitGraph(first.commits, first.refs, "main");
    const GitGraphMermaidDocument second = ParseMermaidGitGraph(exported);

    CHECK(second.ok, "the exported text parses back");
    if (!second.ok) {
        std::printf("       error: %s (line %d)\n%s\n", second.error.c_str(),
                    second.errorLine, exported.c_str());
        return;
    }

    CHECK(second.commits.size() == first.commits.size(), "commit count survives the round trip");

    bool parentsMatch = true;
    bool branchesMatch = true;
    for (const GitGraphCommit& original : first.commits) {
        const GitGraphCommit* copy = Find(second, original.sha);
        if (!copy) { parentsMatch = false; branchesMatch = false; break; }
        if (copy->parents != original.parents) parentsMatch = false;
        if (copy->branch != original.branch) branchesMatch = false;
    }
    CHECK(parentsMatch, "every commit keeps its parents");
    CHECK(branchesMatch, "every commit keeps its branch");
    CHECK(FindRef(second, "v1.0") != nullptr, "tags survive the round trip");
}

static void TestExportFromRepositoryOrder() {
    std::printf("Export from git-log order (newest first, branch from refs)\n");

    // No `branch` field set - membership has to come from the refs.
    std::vector<GitGraphCommit> commits;
    auto add = [&commits](const std::string& sha, std::vector<std::string> parents, int64_t date) {
        GitGraphCommit commit;
        commit.sha = sha;
        commit.parents = std::move(parents);
        commit.commitDate = date;
        commit.subject = sha;
        commits.push_back(commit);
    };
    add("m2", {"m1", "t1"}, 400);       // newest first
    add("t1", {"m1"},       300);
    add("m1", {},           100);

    std::vector<GitGraphRef> refs;
    GitGraphRef main;  main.name = "main";  main.sha = "m2";
    GitGraphRef topic; topic.name = "topic"; topic.sha = "t1";
    refs.push_back(main);
    refs.push_back(topic);

    const std::string exported = ExportMermaidGitGraph(commits, refs, "main");
    const GitGraphMermaidDocument reparsed = ParseMermaidGitGraph(exported);

    CHECK(reparsed.ok, "the export parses");
    if (!reparsed.ok) {
        std::printf("       error: %s (line %d)\n%s\n", reparsed.error.c_str(),
                    reparsed.errorLine, exported.c_str());
        return;
    }
    CHECK(reparsed.commits.size() == 3, "all three commits are emitted");
    CHECK(Find(reparsed, "m2") && Find(reparsed, "m2")->parents.size() == 2,
          "the merge is emitted as a merge, not a plain commit");
    CHECK(Find(reparsed, "t1") && Find(reparsed, "t1")->branch == "topic",
          "branch membership is recovered from the refs");
}

// ---------------------------------------------------------------------------

int main() {
    std::printf("=== GitGraph mermaid tests ===\n\n");

    TestBasicCommits();
    TestBranchMerge();
    TestCommitTypesAndSwitch();
    TestCherryPick();
    TestDirectivesAndConfig();
    TestBranchOrder();
    TestComments();
    TestErrors();
    TestRoundTrip();
    TestExportFromRepositoryOrder();

    std::printf("\n%s (%d failure%s)\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
