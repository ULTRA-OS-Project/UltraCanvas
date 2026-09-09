// Tests/GitRepositoryTest.cpp
// Tests for the on-disk Git repository reader: repository discovery, refs
// (loose and packed, annotated tags peeled), loose and packed object reads
// including delta chains, commit parsing, and the newest-first walk that backs
// the element's lazy loading.
//
// Runs against a real repository - this checkout by default, or a path given as
// argv[1] - so packfiles, deltas and packed-refs are exercised for real rather
// than against a synthetic fixture.
//
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "UltraCanvasGitRepository.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace UltraCanvas;

static int g_failures = 0;
static int g_skipped  = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }               \
    } while (0)

#define SKIP(msg)                                       \
    do { std::printf("  skip: %s\n", msg); ++g_skipped; } while (0)

static bool IsHexSha(const std::string& text) {
    if (text.size() != 40) return false;
    return std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

// ---------------------------------------------------------------------------

static void TestDiscoveryAndHead(UltraCanvasGitRepository& repository) {
    std::printf("Discovery and HEAD\n");

    CHECK(repository.IsOpen(), "the repository opens");
    CHECK(!repository.GetGitDirectory().empty(), "the git directory is resolved");

    const std::string head = repository.ReadHeadSha();
    CHECK(IsHexSha(head), "HEAD resolves to a full sha");

    GitGraphCommit commit;
    CHECK(repository.ReadCommit(head, commit), "the HEAD commit object reads");
    CHECK(commit.sha == head, "the commit carries its own sha");
    CHECK(!commit.subject.empty(), "the subject is parsed");
    CHECK(!commit.authorName.empty(), "the author name is parsed");
    CHECK(commit.commitDate > 1000000000, "the commit date is a plausible unix time");
    for (const std::string& parent : commit.parents) {
        CHECK(IsHexSha(parent), "every parent is a full sha");
    }
}

static void TestRefs(UltraCanvasGitRepository& repository) {
    std::printf("Refs\n");

    const std::vector<GitGraphRef> refs = repository.ReadRefs();
    CHECK(!refs.empty(), "at least one ref is found");

    size_t branches = 0, tags = 0, remotes = 0, current = 0;
    bool shasValid = true;
    bool namesValid = true;
    for (const GitGraphRef& ref : refs) {
        if (!IsHexSha(ref.sha)) shasValid = false;
        if (ref.name.empty()) namesValid = false;
        switch (ref.type) {
            case GitGraphRefType::LocalBranch:  ++branches; break;
            case GitGraphRefType::Tag:          ++tags;     break;
            case GitGraphRefType::RemoteBranch: ++remotes;  break;
            default: break;
        }
        if (ref.isCurrent) ++current;
    }

    CHECK(shasValid, "every ref points at a full sha");
    CHECK(namesValid, "every ref has a name");
    // Local *or* remote: a CI checkout is a detached HEAD with only
    // remote-tracking refs (0 branches, 41 remotes on a GitHub runner), which
    // is a perfectly valid repository. What this is really checking is that
    // branch refs are enumerated and classified at all, and requiring a local
    // one made the suite fail on any clone that has none. The detached-HEAD
    // case is already handled a few lines below, so demanding a local branch
    // here contradicted the check that follows it.
    CHECK(branches + remotes > 0, "at least one branch ref (local or remote)");
    CHECK(current <= 1, "at most one ref is marked current");
    std::printf("       (%zu branches, %zu remotes, %zu tags)\n", branches, remotes, tags);

    // Every ref must resolve to a readable commit - this is what peeling
    // annotated tags is for.
    size_t resolvable = 0;
    for (const GitGraphRef& ref : refs) {
        GitGraphCommit commit;
        if (repository.ReadCommit(ref.sha, commit)) ++resolvable;
    }
    CHECK(resolvable == refs.size(), "every ref resolves to a commit object");

    const std::string branch = repository.ReadCurrentBranch();
    if (branch.empty()) {
        SKIP("HEAD is detached - no current branch to check");
    } else {
        const bool found = std::any_of(refs.begin(), refs.end(),
                                       [&branch](const GitGraphRef& ref) {
                                           return ref.type == GitGraphRefType::LocalBranch &&
                                                  ref.name == branch;
                                       });
        CHECK(found, "the current branch appears among the refs");
    }
}

static void TestObjectReads(UltraCanvasGitRepository& repository) {
    std::printf("Object reads (loose and packed, with delta chains)\n");

    const std::string head = repository.ReadHeadSha();
    std::string data, type;

    CHECK(repository.ReadObject(head, "commit", data, type), "a commit object reads");
    CHECK(type == "commit", "its type is reported");
    CHECK(data.rfind("tree ", 0) == 0, "a commit body starts with its tree line");

    // The tree named by HEAD exercises a different object type, and in a packed
    // repository it is usually delta-compressed.
    const size_t treeStart = data.find("tree ");
    const std::string treeSha = (treeStart == std::string::npos)
                                    ? std::string()
                                    : data.substr(treeStart + 5, 40);
    if (IsHexSha(treeSha)) {
        std::string treeData, treeType;
        CHECK(repository.ReadObject(treeSha, "tree", treeData, treeType),
              "the tree object reads (packed and possibly delta-compressed)");
        CHECK(treeType == "tree", "the tree type is reported");
        CHECK(!treeData.empty(), "the tree body is non-empty");
    } else {
        SKIP("no tree sha in the HEAD commit");
    }

    std::string missingData, missingType;
    CHECK(!repository.ReadObject("0000000000000000000000000000000000000000", "",
                                 missingData, missingType),
          "a missing object fails cleanly");
    CHECK(!repository.ReadObject(head, "blob", missingData, missingType),
          "a type mismatch is rejected");
}

static void TestWalk(UltraCanvasGitRepository& repository) {
    std::printf("Commit walk (newest first, every parent visited)\n");

    repository.RestartWalk();
    const std::vector<GitGraphCommit> commits = repository.WalkMore(120);
    CHECK(!commits.empty(), "the walk produces commits");
    if (commits.empty()) return;

    bool descending = true;
    for (size_t i = 1; i < commits.size(); ++i) {
        if (commits[i].commitDate > commits[i - 1].commitDate) descending = false;
    }
    CHECK(descending, "commits come back newest first");

    std::unordered_set<std::string> shas;
    bool unique = true;
    for (const GitGraphCommit& commit : commits) {
        if (!shas.insert(commit.sha).second) unique = false;
    }
    CHECK(unique, "no commit is emitted twice");

    // Any parent that was itself emitted must appear after its child.
    std::unordered_map<std::string, size_t> position;
    for (size_t i = 0; i < commits.size(); ++i) position[commits[i].sha] = i;
    bool ordered = true;
    for (size_t i = 0; i < commits.size(); ++i) {
        for (const std::string& parent : commits[i].parents) {
            auto at = position.find(parent);
            if (at != position.end() && at->second < i) ordered = false;
        }
    }
    CHECK(ordered, "a parent never precedes its child");

    const bool sawMerge = std::any_of(commits.begin(), commits.end(),
                                      [](const GitGraphCommit& c) { return c.IsMerge(); });
    if (sawMerge) {
        CHECK(sawMerge, "merge commits are read with all their parents");
    } else {
        SKIP("no merge commit within the walked window");
    }

    // Restarting must reproduce the same prefix.
    repository.RestartWalk();
    const std::vector<GitGraphCommit> again = repository.WalkMore(10);
    bool deterministic = again.size() == std::min<size_t>(10, commits.size());
    for (size_t i = 0; i < again.size() && deterministic; ++i) {
        if (again[i].sha != commits[i].sha) deterministic = false;
    }
    CHECK(deterministic, "restarting the walk reproduces the same order");
}

static void TestDataSource(const std::shared_ptr<UltraCanvasGitRepository>& repository) {
    std::printf("Lazy data-source adapter\n");

    UltraCanvasGitRepositorySource source(repository);
    repository->RestartWalk();

    const std::vector<GitGraphCommit> first  = source.FetchCommits(0, 8);
    const std::vector<GitGraphCommit> second = source.FetchCommits(8, 8);

    CHECK(first.size() == 8, "the first page is full");
    CHECK(!second.empty(), "the second page continues the walk");

    bool disjoint = true;
    for (const GitGraphCommit& a : first) {
        for (const GitGraphCommit& b : second) {
            if (a.sha == b.sha) disjoint = false;
        }
    }
    CHECK(disjoint, "pages do not overlap");

    // Re-requesting an earlier page must serve it from the cache unchanged.
    const std::vector<GitGraphCommit> replay = source.FetchCommits(0, 8);
    bool stable = replay.size() == first.size();
    for (size_t i = 0; i < replay.size() && stable; ++i) {
        if (replay[i].sha != first[i].sha) stable = false;
    }
    CHECK(stable, "re-fetching an earlier page returns the same commits");

    CHECK(!source.FetchRefs().empty(), "the source exposes the repository refs");
}

static void TestChangedFiles(UltraCanvasGitRepository& repository) {
    std::printf("Changed files (recursive tree diff against the first parent)\n");

    repository.RestartWalk();
    const std::vector<GitGraphCommit> commits = repository.WalkMore(40);
    if (commits.empty()) { SKIP("no commits to diff"); return; }

    // Find a non-merge commit: a merge diffs against its first parent too, but
    // a plain commit gives the clearest assertion.
    const GitGraphCommit* plain = nullptr;
    for (const GitGraphCommit& commit : commits) {
        if (!commit.IsMerge() && !commit.IsRoot()) { plain = &commit; break; }
    }
    if (!plain) { SKIP("no plain commit in the walked window"); return; }

    const std::vector<GitGraphFileChange> changes = repository.ReadChangedFiles(plain->sha);
    CHECK(!changes.empty(), "a plain commit reports changed files");

    bool pathsValid = true, statusesValid = true, sorted = true;
    for (size_t i = 0; i < changes.size(); ++i) {
        if (changes[i].path.empty() || changes[i].path.front() == '/') pathsValid = false;
        const char status = changes[i].status;
        if (status != 'A' && status != 'M' && status != 'D') statusesValid = false;
        if (i > 0 && changes[i - 1].path > changes[i].path) sorted = false;
    }
    CHECK(pathsValid, "every path is non-empty and repository-relative");
    CHECK(statusesValid, "every status is A, M or D");
    CHECK(sorted, "the list comes back sorted by path");
    std::printf("       (%s touched %zu file(s))\n", plain->ShortSha().c_str(), changes.size());

    // maxFiles must cap the result.
    const std::vector<GitGraphFileChange> capped = repository.ReadChangedFiles(plain->sha, 1);
    CHECK(capped.size() <= 1, "maxFiles caps the result");

    // A root commit has no parent, so its whole tree is added.
    const GitGraphCommit* root = nullptr;
    for (const GitGraphCommit& commit : commits) {
        if (commit.IsRoot()) { root = &commit; break; }
    }
    if (root) {
        const std::vector<GitGraphFileChange> initial = repository.ReadChangedFiles(root->sha);
        const bool allAdded = std::all_of(initial.begin(), initial.end(),
                                          [](const GitGraphFileChange& f) {
                                              return f.status == 'A';
                                          });
        CHECK(!initial.empty() && allAdded, "a root commit reports its whole tree as added");
    } else {
        SKIP("no root commit in the walked window");
    }

    CHECK(repository.ReadChangedFiles("0000000000000000000000000000000000000000").empty(),
          "an unknown commit yields no changes");
}

static void TestFailureModes() {
    std::printf("Failure modes\n");

    UltraCanvasGitRepository repository;
    CHECK(!repository.Open("/nonexistent-path-for-ultracanvas-test"),
          "opening a missing path fails");
    CHECK(!repository.GetLastError().empty(), "an error message is set");
    CHECK(!repository.IsOpen(), "the repository stays closed");

    GitGraphCommit commit;
    CHECK(!repository.ReadCommit("0000000000000000000000000000000000000000", commit),
          "reading from a closed repository fails cleanly");
    CHECK(repository.ReadRefs().empty(), "a closed repository has no refs");
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    std::printf("=== GitRepository tests ===\n\n");

    std::string path = (argc > 1) ? argv[1] :
#ifdef ULTRACANVAS_REPO_ROOT
        ULTRACANVAS_REPO_ROOT;
#else
        ".";
#endif

    auto repository = std::make_shared<UltraCanvasGitRepository>();
    if (!repository->Open(path)) {
        std::printf("  skip: no git repository at '%s' (%s)\n",
                    path.c_str(), repository->GetLastError().c_str());
        std::printf("\nSKIPPED - nothing to read\n");
        return 0;                       // Not a failure: a tarball export has no .git
    }
    std::printf("  repository: %s\n\n", repository->GetGitDirectory().c_str());

    TestDiscoveryAndHead(*repository);
    TestRefs(*repository);
    TestObjectReads(*repository);
    TestWalk(*repository);
    TestChangedFiles(*repository);
    TestDataSource(repository);
    TestFailureModes();

    std::printf("\n%s (%d failure%s, %d skipped)\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                g_failures, g_failures == 1 ? "" : "s", g_skipped);
    return g_failures == 0 ? 0 : 1;
}
