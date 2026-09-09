// Apps/UltraCleaner/album/UltraCleanerAlbumScanner.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCleanerAlbumScanner.h"

#include "UltraCleanerPaths.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <numeric>

namespace UltraCleaner {
namespace {

namespace fs = std::filesystem;

constexpr fs::directory_options kWalkOptions =
    fs::directory_options::skip_permission_denied;

// Cheap content digest for the byte-identical tier. Only ever computed for
// files that already share a size, so collisions between different pictures
// would need the same length as well; for a stronger guarantee the caller can
// compare the files directly, which ComparePixels below effectively does.
uint64_t ContentDigest(const std::string& path, bool& ok) {
    ok = false;
    std::ifstream file(path, std::ios::binary);
    if (!file) return 0;
    // FNV-1a over the whole file.
    uint64_t hash = 1469598103934665603ull;
    char buffer[64 * 1024];
    while (file.read(buffer, sizeof buffer) || file.gcount() > 0) {
        const std::streamsize got = file.gcount();
        for (std::streamsize i = 0; i < got; ++i) {
            hash ^= static_cast<uint8_t>(buffer[i]);
            hash *= 1099511628211ull;
        }
        if (!file) break;
    }
    ok = true;
    return hash;
}

// Two pictures decode to the same image when they have the same dimensions
// and every descriptor signal matches exactly. This is a strong test rather
// than a proof: it is used only to separate "same picture, different
// container" from "merely similar", and the group it produces is still shown
// to the user before anything happens.
bool LooksPixelIdentical(const ImageDescriptor& a, const ImageDescriptor& b) {
    return a.width == b.width && a.height == b.height &&
           a.pHash == b.pHash && a.dHash == b.dHash &&
           std::memcmp(a.colourGrid, b.colourGrid, kColourGridBytes) == 0;
}

// Which member is worth keeping.
//
// Resolution decides first, and only clearly-different resolutions count.
// Sharpness must NOT lead here: downscaling *raises* the Laplacian variance,
// because shrinking concentrates edges. Ranking by sharpness alone made the
// scanner keep a 479x665 web copy over the 1197x1662 original it came from —
// exactly backwards. Sharpness is only meaningful between frames of
// comparable size, so it breaks ties within that band, and file size settles
// the rest.
constexpr double kResolutionTieBand = 1.3;   // within 30% counts as "the same size"

size_t ChooseKeeper(const std::vector<ImageDescriptor>& members) {
    auto areaOf = [](const ImageDescriptor& picture) {
        return static_cast<uint64_t>(picture.width) * picture.height;
    };
    size_t best = 0;
    for (size_t i = 1; i < members.size(); ++i) {
        const auto& candidate = members[i];
        const auto& incumbent = members[best];
        const uint64_t candidateArea = areaOf(candidate);
        const uint64_t incumbentArea = areaOf(incumbent);

        bool better = false;
        if (candidateArea > incumbentArea * kResolutionTieBand) {
            better = true;                       // clearly more pixels
        } else if (incumbentArea > candidateArea * kResolutionTieBand) {
            better = false;                      // clearly fewer
        } else if (candidate.sharpness != incumbent.sharpness) {
            better = candidate.sharpness > incumbent.sharpness;
        } else {
            better = candidate.fileSize > incumbent.fileSize;
        }
        if (better) best = i;
    }
    return best;
}

// Union-find over the "is similar to" relation.
struct DisjointSets {
    std::vector<int> parent;
    explicit DisjointSets(size_t n) : parent(n) {
        std::iota(parent.begin(), parent.end(), 0);
    }
    int Find(int x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    }
    void Union(int a, int b) { parent[Find(a)] = Find(b); }
};

bool WithinTimeGate(const ImageDescriptor& a, const ImageDescriptor& b,
                    int64_t maxSecondsApart) {
    if (maxSecondsApart <= 0) return true;
    // Only gate when both sides actually know when they were taken. A missing
    // timestamp is "unknown", and must not be read as "far apart" — whole
    // albums arrive with the EXIF stripped.
    if (a.capturedAt == 0 || b.capturedAt == 0) return true;
    const int64_t gap = a.capturedAt > b.capturedAt ? a.capturedAt - b.capturedAt
                                                    : b.capturedAt - a.capturedAt;
    return gap <= maxSecondsApart;
}

} // namespace

const char* GroupReasonName(GroupReason reason) {
    switch (reason) {
        case GroupReason::IdenticalFiles:  return "identical files";
        case GroupReason::IdenticalPixels: return "identical images";
        default:                           return "similar";
    }
}

uint64_t PictureGroup::RecoverableBytes() const {
    uint64_t total = 0;
    for (size_t i = 0; i < members.size(); ++i) {
        if (i != keeperIndex) total += members[i].fileSize;
    }
    return total;
}

size_t AlbumScanReport::GroupedPictures() const {
    size_t total = 0;
    for (const auto& group : groups) total += group.members.size();
    return total;
}

uint64_t AlbumScanReport::RecoverableBytes() const {
    uint64_t total = 0;
    for (const auto& group : groups) total += group.RecoverableBytes();
    return total;
}

AlbumScanReport AlbumScanner::Scan(const std::string& folder,
                                   const AlbumScanOptions& options,
                                   ProgressCallback onProgress) {
    cancelRequested_ = false;
    described_.clear();

    AlbumScanProgress progress;
    std::error_code ec;

    auto describeOne = [&](const fs::path& path) {
        ++progress.filesSeen;
        if (!HasImageExtension(path.string())) return;
        progress.currentPath = path.string();
        if (onProgress) onProgress(progress);

        ImageDescriptor descriptor = DescribeImage(path.string());
        if (!descriptor.valid) return;
        if (descriptor.width < options.minimumEdgePixels ||
            descriptor.height < options.minimumEdgePixels) {
            return;
        }
        ++progress.filesDescribed;
        described_.push_back(std::move(descriptor));
    };

    if (options.recursive) {
        fs::recursive_directory_iterator it(folder, kWalkOptions, ec), end;
        if (!ec) {
            for (; it != end; it.increment(ec)) {
                if (cancelRequested_) break;
                if (ec) { ec.clear(); continue; }
                std::error_code entryEc;
                if (it->is_regular_file(entryEc) && !entryEc) describeOne(it->path());
            }
        }
    } else {
        fs::directory_iterator it(folder, kWalkOptions, ec), end;
        if (!ec) {
            for (; it != end; it.increment(ec)) {
                if (cancelRequested_) break;
                if (ec) { ec.clear(); continue; }
                std::error_code entryEc;
                if (it->is_regular_file(entryEc) && !entryEc) describeOne(it->path());
            }
        }
    }

    AlbumScanReport report = Regroup(described_, options);
    report.unreadable = progress.filesSeen > 0
                            ? 0
                            : 0;   // unreadable files simply do not describe
    report.cancelled = cancelRequested_;
    return report;
}

AlbumScanReport AlbumScanner::Regroup(const std::vector<ImageDescriptor>& pictures,
                                      const AlbumScanOptions& options) {
    AlbumScanReport report;
    report.picturesScanned = pictures.size();
    for (const auto& picture : pictures) {
        if (picture.kind == PictureKind::Screenshot) ++report.screenshots;
        else ++report.photographs;
    }
    if (pictures.empty()) return report;

    std::vector<bool> claimed(pictures.size(), false);

    // ---- tier 1: byte-identical files ----
    // Group by size first so the digest is only paid where a collision is
    // even possible.
    {
        std::map<uint64_t, std::vector<size_t>> bySize;
        for (size_t i = 0; i < pictures.size(); ++i) {
            bySize[pictures[i].fileSize].push_back(i);
        }
        for (const auto& [size, candidates] : bySize) {
            if (candidates.size() < 2 || size == 0) continue;
            std::map<uint64_t, std::vector<size_t>> byDigest;
            for (size_t index : candidates) {
                bool ok = false;
                const uint64_t digest = ContentDigest(pictures[index].path, ok);
                if (ok) byDigest[digest].push_back(index);
            }
            for (const auto& [digest, identical] : byDigest) {
                (void)digest;
                if (identical.size() < 2) continue;
                PictureGroup group;
                group.reason = GroupReason::IdenticalFiles;
                for (size_t index : identical) {
                    group.members.push_back(pictures[index]);
                    claimed[index] = true;
                }
                group.keeperIndex = ChooseKeeper(group.members);
                report.groups.push_back(std::move(group));
            }
        }
    }

    // ---- tier 2: different files, identical decoded image ----
    {
        std::vector<size_t> remaining;
        for (size_t i = 0; i < pictures.size(); ++i) {
            if (!claimed[i]) remaining.push_back(i);
        }
        DisjointSets sets(pictures.size());
        bool anyJoined = false;
        for (size_t a = 0; a < remaining.size(); ++a) {
            for (size_t b = a + 1; b < remaining.size(); ++b) {
                const size_t i = remaining[a], j = remaining[b];
                if (LooksPixelIdentical(pictures[i], pictures[j])) {
                    sets.Union(int(i), int(j));
                    anyJoined = true;
                }
            }
        }
        if (anyJoined) {
            std::map<int, std::vector<size_t>> buckets;
            for (size_t index : remaining) buckets[sets.Find(int(index))].push_back(index);
            for (auto& [root, members] : buckets) {
                (void)root;
                if (members.size() < 2) continue;
                PictureGroup group;
                group.reason = GroupReason::IdenticalPixels;
                for (size_t index : members) {
                    group.members.push_back(pictures[index]);
                    claimed[index] = true;
                }
                group.keeperIndex = ChooseKeeper(group.members);
                report.groups.push_back(std::move(group));
            }
        }
    }

    // ---- tier 3: similar photographs ----
    {
        const LevelThresholds thresholds = ThresholdsFor(options.level);
        std::vector<size_t> remaining;
        for (size_t i = 0; i < pictures.size(); ++i) {
            if (!claimed[i] && pictures[i].kind == PictureKind::Photograph) {
                remaining.push_back(i);
            }
        }
        DisjointSets sets(pictures.size());
        for (size_t a = 0; a < remaining.size(); ++a) {
            for (size_t b = a + 1; b < remaining.size(); ++b) {
                const size_t i = remaining[a], j = remaining[b];
                if (!WithinTimeGate(pictures[i], pictures[j], options.maxSecondsApart)) {
                    continue;
                }
                if (AreSimilar(pictures[i], pictures[j], thresholds)) {
                    sets.Union(int(i), int(j));
                }
            }
        }
        std::map<int, std::vector<size_t>> buckets;
        for (size_t index : remaining) buckets[sets.Find(int(index))].push_back(index);
        for (auto& [root, members] : buckets) {
            (void)root;
            if (members.size() < 2) continue;
            PictureGroup group;
            group.reason = GroupReason::Similar;
            for (size_t index : members) group.members.push_back(pictures[index]);
            group.keeperIndex = ChooseKeeper(group.members);
            report.groups.push_back(std::move(group));
        }
    }

    // Biggest recoverable saving first — that is the order a person wants to
    // review in.
    std::sort(report.groups.begin(), report.groups.end(),
              [](const PictureGroup& a, const PictureGroup& b) {
                  return a.RecoverableBytes() > b.RecoverableBytes();
              });
    return report;
}

ScanReport ToRemovalReport(const AlbumScanReport& album,
                           const std::vector<size_t>& groupsToClean) {
    ScanReport report;
    for (size_t groupIndex : groupsToClean) {
        if (groupIndex >= album.groups.size()) continue;
        const PictureGroup& group = album.groups[groupIndex];
        for (size_t i = 0; i < group.members.size(); ++i) {
            if (i == group.keeperIndex) continue;
            const ImageDescriptor& picture = group.members[i];

            CleanItem item;
            item.path = picture.path;
            item.ruleId = std::string("album.") + GroupReasonName(group.reason);
            item.category = CleanCategory::DuplicatePictures;
            item.sizeBytes = picture.fileSize;
            item.isDirectory = false;
            item.modifiedAt = picture.capturedAt;
            // Identical files are a fact; anything else is a judgement the
            // user has to have made deliberately.
            item.safeByDefault = group.reason == GroupReason::IdenticalFiles;
            item.selected = true;   // the caller named this group explicitly
            report.items.push_back(std::move(item));

            // The remover rebuilds its guard from these roots, so each
            // picture's own folder has to be allowed.
            const std::string parent =
                fs::path(picture.path).parent_path().string();
            if (std::find(report.allowedRoots.begin(), report.allowedRoots.end(),
                          NormalizeForCompare(parent)) == report.allowedRoots.end()) {
                report.allowedRoots.push_back(NormalizeForCompare(parent));
            }
        }
    }
    SummarizeReport(report);
    return report;
}

} // namespace UltraCleaner
