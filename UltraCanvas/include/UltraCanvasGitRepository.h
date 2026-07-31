// include/UltraCanvasGitRepository.h
// Read-only reader for an on-disk Git repository: refs, loose objects and
// packfiles (including OFS_DELTA / REF_DELTA chains), decompressed with the
// vendored miniz. No git executable is spawned and no new third-party
// dependency is introduced.
//
// The reader produces the same GitGraphCommit / GitGraphRef model the Git graph
// element consumes, and ships an IGitGraphDataSource adapter so a large history
// can be paged into the widget lazily.
//
// Namespace: UltraCanvas
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#pragma once

#include "Plugins/Diagrams/UltraCanvasGitGraphTypes.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraCanvasGitRepository {
public:
    UltraCanvasGitRepository();
    ~UltraCanvasGitRepository();

    UltraCanvasGitRepository(const UltraCanvasGitRepository&) = delete;
    UltraCanvasGitRepository& operator=(const UltraCanvasGitRepository&) = delete;

    // `path` may be a working tree, a `.git` directory, or a `.git` file
    // pointing elsewhere (worktrees and submodules). Parent directories are
    // searched when the path is inside a working tree.
    bool Open(const std::string& path);
    void Close();
    bool IsOpen() const;

    const std::string& GetGitDirectory() const;
    const std::string& GetLastError() const;

    // ===== REFS =====

    // Local branches, remote branches and tags, with annotated tags peeled to
    // the commit they point at. The current branch is flagged isCurrent.
    std::vector<GitGraphRef> ReadRefs();

    std::string ReadHeadSha();
    std::string ReadCurrentBranch();        // Empty when HEAD is detached

    // ===== COMMITS =====

    // Walk from every ref tip, newest commit first. `limit` 0 reads everything.
    std::vector<GitGraphCommit> ReadCommits(size_t limit = 0);

    // Incremental walking, for paging a large history into the element.
    void RestartWalk();
    std::vector<GitGraphCommit> WalkMore(size_t count);
    bool IsWalkComplete() const;

    // Single-object access.
    bool ReadCommit(const std::string& sha, GitGraphCommit& outCommit);

    // Files changed by `sha` against its first parent, as a recursive tree
    // diff (a root commit reports its whole tree as added). `maxFiles` 0 means
    // no limit. Paths are repository-relative.
    std::vector<GitGraphFileChange> ReadChangedFiles(const std::string& sha,
                                                     size_t maxFiles = 0);

    // Raw object access: returns false when the object is missing or the type
    // does not match `expectedType` ("commit", "tree", "blob", "tag"; empty
    // accepts any type and reports the real one in `outType`).
    bool ReadObject(const std::string& sha, const std::string& expectedType,
                    std::string& outData, std::string& outType);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Pages a repository into the element: attach with
// UltraCanvasGitGraph::SetDataSource() and commits arrive as the user scrolls.
class UltraCanvasGitRepositorySource : public IGitGraphDataSource {
public:
    explicit UltraCanvasGitRepositorySource(std::shared_ptr<UltraCanvasGitRepository> repository);

    size_t GetTotalCommitCount() override { return 0; }        // Unknown up front
    std::vector<GitGraphCommit> FetchCommits(size_t offset, size_t count) override;
    std::vector<GitGraphRef> FetchRefs() override;

private:
    std::shared_ptr<UltraCanvasGitRepository> repository;
    std::vector<GitGraphCommit> walked;        // Everything walked so far
};

} // namespace UltraCanvas
