// Plugins/Diagrams/UltraCanvasMindMapLayout.cpp
// Mind map topic model and layout engine
// Version: 1.1.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasMindMapLayout.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace UltraCanvas {

// =============================================================================
// MODEL - CONSTRUCTION
// =============================================================================

std::string MindMapModel::GenerateId(const std::string& prefix) {
    std::string candidate;
    do {
        candidate = prefix + "_" + std::to_string(nextId++);
    } while (topics.find(candidate) != topics.end());
    return candidate;
}

std::string MindMapModel::SetCentralTopic(const std::string& text) {
    if (!rootId.empty()) {
        auto it = topics.find(rootId);
        if (it != topics.end()) {
            it->second.text = text;
            return rootId;
        }
    }
    MindMapTopic root(GenerateId("root"), text);
    rootId = root.id;
    topics[root.id] = root;
    return root.id;
}

std::string MindMapModel::AddTopic(const std::string& parentId, const std::string& text) {
    if (topics.find(parentId) == topics.end()) return std::string();
    MindMapTopic topic(GenerateId(), text);
    topic.parentId = parentId;
    topics[topic.id] = topic;
    topics[parentId].childIds.push_back(topic.id);
    return topic.id;
}

bool MindMapModel::AddTopic(const MindMapTopic& topic) {
    if (topic.id.empty()) return false;
    if (topics.find(topic.id) != topics.end()) return false;

    if (topic.floating) {
        topics[topic.id] = topic;
        topics[topic.id].parentId.clear();
        floatingIds.push_back(topic.id);
        return true;
    }

    if (topic.parentId.empty()) {
        // No parent given: becomes the central topic if there isn't one yet.
        if (!rootId.empty()) return false;
        topics[topic.id] = topic;
        rootId = topic.id;
        return true;
    }

    auto parentIt = topics.find(topic.parentId);
    if (parentIt == topics.end()) return false;
    topics[topic.id] = topic;
    parentIt->second.childIds.push_back(topic.id);
    return true;
}

std::string MindMapModel::AddFloatingTopic(const std::string& text, double x, double y) {
    MindMapTopic topic(GenerateId("floating"), text);
    topic.floating = true;
    topic.floatX = x;
    topic.floatY = y;
    topics[topic.id] = topic;
    floatingIds.push_back(topic.id);
    return topic.id;
}

void MindMapModel::RemoveFromParent(const std::string& id) {
    auto it = topics.find(id);
    if (it == topics.end()) return;
    const std::string& parentId = it->second.parentId;
    if (parentId.empty()) return;
    auto parentIt = topics.find(parentId);
    if (parentIt == topics.end()) return;
    auto& siblings = parentIt->second.childIds;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
}

void MindMapModel::CollectSubtree(const std::string& id, std::vector<std::string>& out) const {
    auto it = topics.find(id);
    if (it == topics.end()) return;
    out.push_back(id);
    for (const auto& childId : it->second.childIds) {
        CollectSubtree(childId, out);
    }
}

std::vector<std::string> MindMapModel::RemoveTopic(const std::string& id) {
    std::vector<std::string> removed;
    if (topics.find(id) == topics.end()) return removed;

    CollectSubtree(id, removed);
    RemoveFromParent(id);

    for (const auto& removedId : removed) {
        topics.erase(removedId);
        floatingIds.erase(std::remove(floatingIds.begin(), floatingIds.end(), removedId),
                          floatingIds.end());
    }
    if (std::find(removed.begin(), removed.end(), rootId) != removed.end()) {
        rootId.clear();
    }

    // Relationships touching a removed topic go with it.
    relationships.erase(
        std::remove_if(relationships.begin(), relationships.end(),
            [&](const MindMapRelationship& r) {
                return std::find(removed.begin(), removed.end(), r.fromTopicId) != removed.end() ||
                       std::find(removed.begin(), removed.end(), r.toTopicId) != removed.end();
            }),
        relationships.end());

    return removed;
}

bool MindMapModel::MoveTopic(const std::string& id, const std::string& newParentId, int index) {
    if (id == newParentId) return false;
    auto it = topics.find(id);
    if (it == topics.end()) return false;
    if (id == rootId) return false;                 // The central topic cannot be reparented
    auto parentIt = topics.find(newParentId);
    if (parentIt == topics.end()) return false;
    if (IsDescendantOf(newParentId, id)) return false;  // Would create a cycle

    RemoveFromParent(id);
    it->second.parentId = newParentId;
    it->second.floating = false;
    floatingIds.erase(std::remove(floatingIds.begin(), floatingIds.end(), id), floatingIds.end());

    auto& siblings = parentIt->second.childIds;
    if (index < 0 || index > static_cast<int>(siblings.size())) {
        siblings.push_back(id);
    } else {
        siblings.insert(siblings.begin() + index, id);
    }
    return true;
}

bool MindMapModel::MoveTopicToIndex(const std::string& id, int index) {
    auto it = topics.find(id);
    if (it == topics.end() || it->second.parentId.empty()) return false;
    auto parentIt = topics.find(it->second.parentId);
    if (parentIt == topics.end()) return false;

    auto& siblings = parentIt->second.childIds;
    auto pos = std::find(siblings.begin(), siblings.end(), id);
    if (pos == siblings.end()) return false;

    int clamped = std::clamp(index, 0, static_cast<int>(siblings.size()) - 1);
    if (clamped == static_cast<int>(std::distance(siblings.begin(), pos))) return false;

    siblings.erase(pos);
    siblings.insert(siblings.begin() + clamped, id);
    return true;
}

void MindMapModel::Clear() {
    topics.clear();
    floatingIds.clear();
    relationships.clear();
    rootId.clear();
    nextId = 1;
}

void MindMapModel::RestoreStructure(const std::string& declaredRootId) {
    floatingIds.clear();
    rootId.clear();

    // Drop dangling parent references so the tree is always well formed.
    for (auto& [id, topic] : topics) {
        if (!topic.parentId.empty() && topics.find(topic.parentId) == topics.end()) {
            topic.parentId.clear();
        }
        topic.childIds.erase(
            std::remove_if(topic.childIds.begin(), topic.childIds.end(),
                           [&](const std::string& childId) {
                               return topics.find(childId) == topics.end();
                           }),
            topic.childIds.end());
    }

    if (topics.find(declaredRootId) != topics.end()) {
        rootId = declaredRootId;
        topics[rootId].parentId.clear();
        topics[rootId].floating = false;
    }

    // Anything else without a parent is a floating topic.
    for (auto& [id, topic] : topics) {
        if (id == rootId) continue;
        if (topic.parentId.empty()) {
            topic.floating = true;
            floatingIds.push_back(id);
        } else {
            topic.floating = false;
        }
    }
    std::sort(floatingIds.begin(), floatingIds.end());   // Deterministic order

    // Keep generated ids from colliding with restored ones.
    nextId = static_cast<int>(topics.size()) + 1;
    while (topics.find("topic_" + std::to_string(nextId)) != topics.end()) ++nextId;
}

// =============================================================================
// MODEL - ACCESS & QUERIES
// =============================================================================

MindMapTopic* MindMapModel::GetTopic(const std::string& id) {
    auto it = topics.find(id);
    return it == topics.end() ? nullptr : &it->second;
}

const MindMapTopic* MindMapModel::GetTopic(const std::string& id) const {
    auto it = topics.find(id);
    return it == topics.end() ? nullptr : &it->second;
}

std::vector<std::string> MindMapModel::GetPath(const std::string& id) const {
    std::vector<std::string> path;
    const MindMapTopic* topic = GetTopic(id);
    if (!topic) return path;

    // Walk up, guarding against a malformed cyclic parent chain.
    size_t guard = topics.size() + 1;
    while (topic && guard-- > 0) {
        path.push_back(topic->id);
        if (topic->parentId.empty()) break;
        topic = GetTopic(topic->parentId);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<std::string> MindMapModel::GetSubtreeIds(const std::string& id) const {
    std::vector<std::string> ids;
    CollectSubtree(id, ids);
    return ids;
}

bool MindMapModel::IsDescendantOf(const std::string& id, const std::string& ancestorId) const {
    if (id == ancestorId) return false;
    const MindMapTopic* topic = GetTopic(id);
    size_t guard = topics.size() + 1;
    while (topic && !topic->parentId.empty() && guard-- > 0) {
        if (topic->parentId == ancestorId) return true;
        topic = GetTopic(topic->parentId);
    }
    return false;
}

int MindMapModel::GetDepth(const std::string& id) const {
    std::vector<std::string> path = GetPath(id);
    return path.empty() ? -1 : static_cast<int>(path.size()) - 1;
}

int MindMapModel::CountDescendants(const std::string& id) const {
    const MindMapTopic* topic = GetTopic(id);
    if (!topic) return 0;
    int count = 0;
    for (const auto& childId : topic->childIds) {
        count += 1 + CountDescendants(childId);
    }
    return count;
}

bool MindMapModel::IsHiddenByCollapse(const std::string& id) const {
    const MindMapTopic* topic = GetTopic(id);
    if (!topic) return false;
    size_t guard = topics.size() + 1;
    const MindMapTopic* cursor = topic;
    while (cursor && !cursor->parentId.empty() && guard-- > 0) {
        const MindMapTopic* parent = GetTopic(cursor->parentId);
        if (!parent) break;
        if (parent->collapsed) return true;
        cursor = parent;
    }
    return false;
}

void MindMapModel::ForEachTopic(const std::function<void(MindMapTopic&)>& fn, bool skipCollapsed) {
    std::function<void(const std::string&)> walk = [&](const std::string& id) {
        auto it = topics.find(id);
        if (it == topics.end()) return;
        fn(it->second);
        if (skipCollapsed && it->second.collapsed) return;
        // Copy: fn may mutate the child list.
        std::vector<std::string> children = it->second.childIds;
        for (const auto& childId : children) walk(childId);
    };
    if (!rootId.empty()) walk(rootId);
    std::vector<std::string> floats = floatingIds;
    for (const auto& id : floats) walk(id);
}

void MindMapModel::ForEachTopic(const std::function<void(const MindMapTopic&)>& fn,
                                bool skipCollapsed) const {
    std::function<void(const std::string&)> walk = [&](const std::string& id) {
        auto it = topics.find(id);
        if (it == topics.end()) return;
        fn(it->second);
        if (skipCollapsed && it->second.collapsed) return;
        for (const auto& childId : it->second.childIds) walk(childId);
    };
    if (!rootId.empty()) walk(rootId);
    for (const auto& id : floatingIds) walk(id);
}

// =============================================================================
// MODEL - RELATIONSHIPS
// =============================================================================

bool MindMapModel::AddRelationship(const MindMapRelationship& relationship) {
    if (relationship.id.empty()) return false;
    if (topics.find(relationship.fromTopicId) == topics.end()) return false;
    if (topics.find(relationship.toTopicId) == topics.end()) return false;
    for (const auto& existing : relationships) {
        if (existing.id == relationship.id) return false;
    }
    relationships.push_back(relationship);
    return true;
}

bool MindMapModel::RemoveRelationship(const std::string& id) {
    auto before = relationships.size();
    relationships.erase(
        std::remove_if(relationships.begin(), relationships.end(),
                       [&](const MindMapRelationship& r) { return r.id == id; }),
        relationships.end());
    return relationships.size() != before;
}

// =============================================================================
// MODEL - BULK BUILD
// =============================================================================

void MindMapModel::BuildFromOutline(const std::vector<std::pair<int, std::string>>& outline) {
    if (outline.empty()) return;

    // idsByLevel[n] is the most recent topic seen at indent level n.
    std::vector<std::string> idsByLevel;

    for (const auto& [rawLevel, text] : outline) {
        int level = std::max(0, rawLevel);
        // Never jump more than one level deeper than the previous line.
        level = std::min(level, static_cast<int>(idsByLevel.size()));

        std::string id;
        if (level == 0) {
            if (rootId.empty()) {
                id = SetCentralTopic(text);
            } else {
                // A second level-0 line becomes a child of the central topic
                // rather than a competing root.
                id = AddTopic(rootId, text);
            }
        } else {
            id = AddTopic(idsByLevel[level - 1], text);
        }
        if (id.empty()) continue;

        idsByLevel.resize(level + 1);
        idsByLevel[level] = id;
    }
}

void MindMapModel::AssignOrdinals() {
    std::function<void(const std::string&, const std::string&)> walk =
        [&](const std::string& id, const std::string& prefix) {
            auto it = topics.find(id);
            if (it == topics.end()) return;
            it->second.ordinal = prefix;
            int index = 1;
            for (const auto& childId : it->second.childIds) {
                std::string childOrdinal =
                    prefix.empty() ? std::to_string(index) : prefix + "." + std::to_string(index);
                walk(childId, childOrdinal);
                ++index;
            }
        };
    if (!rootId.empty()) walk(rootId, std::string());
    for (const auto& id : floatingIds) walk(id, std::string());
}

// =============================================================================
// LAYOUT - MEASUREMENT
// =============================================================================

Size2Dd UltraCanvasMindMapLayout::EstimateTextSize(const std::string& text, double fontSize,
                                                   bool bold, double maxWidth) {
    if (text.empty()) return Size2Dd(0.0, fontSize * 1.2);

    // Average glyph width as a fraction of the font size. Bold runs wider; the
    // 0.70 factor matches the correction made in UltraCanvasNodeDiagram 2.0.5
    // after narrower estimates let labels overflow.
    const double glyphFactor = bold ? 0.70 : 0.58;
    const double lineHeight = fontSize * 1.35;

    double singleLineWidth = static_cast<double>(text.size()) * fontSize * glyphFactor;
    if (maxWidth <= 0.0 || singleLineWidth <= maxWidth) {
        return Size2Dd(singleLineWidth, lineHeight);
    }

    // Wrapped: estimate the line count from the total ink width.
    int lines = static_cast<int>(std::ceil(singleLineWidth / maxWidth));
    return Size2Dd(maxWidth, lineHeight * lines);
}

Size2Dd UltraCanvasMindMapLayout::MeasureTopic(const MindMapTopic& topic,
                                               const MindMapTopicStyle& style,
                                               const MindMapLayoutOptions& options,
                                               const MindMapMeasureFn& measure) {
    Size2Dd ink = measure
        ? measure(topic.text, style.fontSize, style.bold, style.maxWidth)
        : EstimateTextSize(topic.text, style.fontSize, style.bold, style.maxWidth);

    double width  = ink.width  + style.paddingX * 2.0;
    double height = ink.height + style.paddingY * 2.0;

    if (!topic.iconPath.empty()) {
        width += style.fontSize * 1.6;   // Icon box plus its gap
    }

    if (options.sizeFromWeight && topic.weight > 1.0) {
        double extra = (topic.weight - 1.0) * options.weightSizeScale;
        width  += extra;
        height += extra;
    }

    // Circles and diamonds need to contain the text, not just bound it.
    if (style.shape == MindMapNodeShape::Circle) {
        double diameter = std::sqrt(width * width + height * height);
        width = height = diameter;
    } else if (style.shape == MindMapNodeShape::Diamond) {
        width *= 1.45;
        height *= 1.45;
    } else if (style.shape == MindMapNodeShape::Triangle) {
        // The label sits centred 0.62 of the way down; the triangle is just
        // wide enough at the label's top edge, and 0.8 as high as wide (or
        // taller for a label of several lines).
        const double w = width, h = height;
        double triangleWidth = (w + 0.65 * h) / 0.6;
        double triangleHeight = std::max(triangleWidth * 0.8, h * 1.4);
        triangleWidth = std::max(triangleWidth, w / (0.62 - h / (2.0 * triangleHeight)));
        width = triangleWidth;
        height = std::max(triangleHeight, triangleWidth * 0.8);
    }

    width  = std::max(width, style.minWidth);
    height = std::max(height, style.minHeight);
    return Size2Dd(width, height);
}

// =============================================================================
// LAYOUT - SIDE ASSIGNMENT
// =============================================================================

double UltraCanvasMindMapLayout::LeafWeight(const MindMapModel& model, const std::string& id) {
    const MindMapTopic* topic = model.GetTopic(id);
    if (!topic) return 0.0;
    if (topic->childIds.empty() || topic->collapsed) return 1.0;
    double total = 0.0;
    for (const auto& childId : topic->childIds) total += LeafWeight(model, childId);
    return total;
}

void UltraCanvasMindMapLayout::AssignSides(MindMapModel& model,
                                           const MindMapLayoutOptions& options) {
    const std::string& rootId = model.GetRootId();
    MindMapTopic* root = model.GetTopic(rootId);
    if (!root) return;

    root->resolvedSide = MindMapTopicSide::Right;

    // Depth-1 topics get a side; everything below inherits it.
    if (options.structure == MindMapStructure::LogicRight) {
        for (const auto& id : root->childIds) {
            if (auto* t = model.GetTopic(id)) t->resolvedSide = MindMapTopicSide::Right;
        }
    } else if (options.structure == MindMapStructure::LogicLeft) {
        for (const auto& id : root->childIds) {
            if (auto* t = model.GetTopic(id)) t->resolvedSide = MindMapTopicSide::Left;
        }
    } else {
        double leftLoad = 0.0, rightLoad = 0.0;
        int autoIndex = 0;
        const int childCount = static_cast<int>(root->childIds.size());

        for (int i = 0; i < childCount; ++i) {
            MindMapTopic* child = model.GetTopic(root->childIds[i]);
            if (!child) continue;

            // An explicit side always wins over the balancing policy.
            if (child->side == MindMapTopicSide::Left ||
                child->side == MindMapTopicSide::Right) {
                child->resolvedSide = child->side;
                (child->resolvedSide == MindMapTopicSide::Left ? leftLoad : rightLoad)
                    += LeafWeight(model, child->id);
                continue;
            }

            switch (options.balancePolicy) {
                case MindMapBalancePolicy::AlternateByIndex:
                    child->resolvedSide = (autoIndex % 2 == 0) ? MindMapTopicSide::Right
                                                               : MindMapTopicSide::Left;
                    break;
                case MindMapBalancePolicy::SplitInHalf:
                    child->resolvedSide = (autoIndex < (childCount + 1) / 2)
                        ? MindMapTopicSide::Right : MindMapTopicSide::Left;
                    break;
                case MindMapBalancePolicy::MinimiseSubtreeWeight: {
                    double load = LeafWeight(model, child->id);
                    child->resolvedSide = (rightLoad <= leftLoad) ? MindMapTopicSide::Right
                                                                  : MindMapTopicSide::Left;
                    (child->resolvedSide == MindMapTopicSide::Left ? leftLoad : rightLoad) += load;
                    break;
                }
            }
            ++autoIndex;
        }
    }

    // Cascade the depth-1 side down every subtree, and record the branch index.
    for (size_t branch = 0; branch < root->childIds.size(); ++branch) {
        const std::string& branchId = root->childIds[branch];
        MindMapTopic* branchTopic = model.GetTopic(branchId);
        if (!branchTopic) continue;
        MindMapTopicSide side = branchTopic->resolvedSide;

        for (const auto& id : model.GetSubtreeIds(branchId)) {
            if (auto* t = model.GetTopic(id)) {
                t->resolvedSide = side;
                t->branchIndex = static_cast<int>(branch);
            }
        }
    }
    root->branchIndex = -1;
}

// =============================================================================
// LAYOUT - PER-BRANCH STRUCTURE OVERRIDE (L8)
// =============================================================================

namespace {

// A branch laid out under its own structure, positioned later by the parent.
struct BranchOverrideLayout {
    MindMapLayoutResult result;   // Positions in the branch's own local space
    Rect2Dd boundingBox;          // Extent of `result`
    Point2Dd rootCentre;          // Where the branch node sits inside that box
};

} // namespace

// Lays out `branchId`'s subtree under `structure`, in its own local space.
//
// The subtree is copied into a scratch model whose root is the branch, so the
// existing layout paths can be reused verbatim rather than reimplemented. The
// copy's own structureOverride is cleared, which is what stops this recursing
// back into itself.
static bool LayoutBranchOverride(const MindMapModel& model, const std::string& branchId,
                                 MindMapStructure structure,
                                 const MindMapLayoutOptions& options,
                                 const MindMapMeasureFn& measure,
                                 const MindMapStyleFn& styleOf,
                                 BranchOverrideLayout& out) {
    MindMapModel scratch;
    for (const auto& id : model.GetSubtreeIds(branchId)) {
        const MindMapTopic* topic = model.GetTopic(id);
        if (!topic) continue;
        scratch.Topics()[id] = *topic;
    }
    auto rootIt = scratch.Topics().find(branchId);
    if (rootIt == scratch.Topics().end()) return false;
    rootIt->second.parentId.clear();
    rootIt->second.structureOverride.reset();
    scratch.RestoreStructure(branchId);

    MindMapLayoutOptions subOptions = options;
    subOptions.structure = structure;
    // Alignment and manual placement belong to the whole map, not to a branch.
    subOptions.alignLevelColumns = false;
    subOptions.alignSideRows = false;

    UltraCanvasMindMapLayout::ComputeLayout(scratch, subOptions, measure, styleOf, out.result);
    if (!out.result.Has(branchId)) return false;

    const DiagramContentBounds& bounds = out.result.contentBounds;
    if (!bounds.valid) return false;
    out.boundingBox = Rect2Dd(bounds.minX, bounds.minY,
                              bounds.GetWidth(), bounds.GetHeight());

    Rect2Dd rootBox = out.result.Get(branchId);
    out.rootCentre = Point2Dd(rootBox.x + rootBox.width * 0.5,
                              rootBox.y + rootBox.height * 0.5);
    return true;
}

// Copies a branch's local layout into the map, translated so its bounding box
// is centred on the slot the parent allocated.
static void PlaceBranchOverride(MindMapModel& model, const std::string& branchId,
                                const BranchOverrideLayout& branch,
                                double slotCentreX, double slotCentreY,
                                MindMapLayoutResult& out) {
    double boxCentreX = branch.boundingBox.x + branch.boundingBox.width * 0.5;
    double boxCentreY = branch.boundingBox.y + branch.boundingBox.height * 0.5;
    double dx = slotCentreX - boxCentreX;
    double dy = slotCentreY - boxCentreY;

    for (const auto& [id, box] : branch.result.bounds) {
        Rect2Dd placed(box.x + dx, box.y + dy, box.width, box.height);
        out.bounds[id] = placed;
        out.contentBounds.Include(placed);
        // Depth, resolvedSide and branchIndex stay as the parent structure set
        // them; only the geometry comes from the branch's own layout.
        if (MindMapTopic* topic = model.GetTopic(id)) topic->bounds = placed;
    }
    (void)branchId;
}

// =============================================================================
// LAYOUT - HORIZONTAL (Balanced / LogicRight / LogicLeft)
// =============================================================================

void UltraCanvasMindMapLayout::LayoutHorizontal(MindMapModel& model,
                                                const MindMapLayoutOptions& options,
                                                const MindMapMeasureFn& measure,
                                                const MindMapStyleFn& styleOf,
                                                MindMapLayoutResult& out) {
    MindMapTopic* root = model.GetTopic(model.GetRootId());
    if (!root) return;

    // ---- Pass 0: branches that declare their own structure (L8) ----
    // Each is laid out locally first, so the main pass can reserve exactly the
    // space it needs and the two never collide.
    std::unordered_map<std::string, BranchOverrideLayout> overrides;
    for (const auto& branchId : root->childIds) {
        const MindMapTopic* branch = model.GetTopic(branchId);
        if (!branch || !branch->structureOverride.has_value()) continue;
        if (branch->collapsed) continue;
        BranchOverrideLayout layout;
        if (LayoutBranchOverride(model, branchId, branch->structureOverride.value(),
                                 options, measure, styleOf, layout)) {
            overrides[branchId] = std::move(layout);
        }
    }

    // ---- Pass 1: measure every visible topic ----
    std::unordered_map<std::string, Size2Dd> sizes;
    std::function<void(const std::string&, int)> measureWalk =
        [&](const std::string& id, int depth) {
            MindMapTopic* topic = model.GetTopic(id);
            if (!topic) return;
            topic->depth = depth;
            sizes[id] = MeasureTopic(*topic, styleOf(*topic), options, measure);
            if (topic->collapsed) return;
            // An overridden branch owns its descendants' depths already.
            if (overrides.find(id) != overrides.end()) {
                for (const auto& childId : topic->childIds) measureWalk(childId, depth + 1);
                return;
            }
            for (const auto& childId : topic->childIds) measureWalk(childId, depth + 1);
        };
    measureWalk(root->id, 0);

    // ---- Pass 2: bottom-up subtree extents (vertical space each subtree needs) ----
    std::unordered_map<std::string, double> extents;
    std::function<double(const std::string&)> computeExtent =
        [&](const std::string& id) -> double {
            MindMapTopic* topic = model.GetTopic(id);
            if (!topic) return 0.0;
            // An overridden branch reserves the height of its own laid-out box.
            auto overrideIt = overrides.find(id);
            if (overrideIt != overrides.end()) {
                extents[id] = overrideIt->second.boundingBox.height;
                return extents[id];
            }
            double own = sizes[id].height;
            if (topic->collapsed || topic->childIds.empty()) {
                extents[id] = own;
                return own;
            }
            double total = 0.0;
            for (size_t i = 0; i < topic->childIds.size(); ++i) {
                total += computeExtent(topic->childIds[i]);
                if (i + 1 < topic->childIds.size()) total += options.siblingGap;
            }
            // A parent taller than all its children still needs its own room.
            extents[id] = std::max(total, own);
            return extents[id];
        };
    computeExtent(root->id);

    // ---- Pass 3: place ----
    // Children are stacked along Y and centred against their parent; X advances
    // by the parent's half-width + gap + the child's half-width, so variable
    // widths never cause an overlap between levels.
    std::function<void(const std::string&, double, double, int)> place =
        [&](const std::string& id, double centerX, double centerY, int direction) {
            MindMapTopic* topic = model.GetTopic(id);
            if (!topic) return;

            Size2Dd size = sizes[id];
            Rect2Dd box(centerX - size.width * 0.5, centerY - size.height * 0.5,
                        size.width, size.height);
            topic->bounds = box;
            out.bounds[id] = box;
            out.contentBounds.Include(box);

            if (topic->collapsed || topic->childIds.empty()) return;

            double childrenExtent = 0.0;
            for (size_t i = 0; i < topic->childIds.size(); ++i) {
                childrenExtent += extents[topic->childIds[i]];
                if (i + 1 < topic->childIds.size()) childrenExtent += options.siblingGap;
            }

            double cursorY = centerY - childrenExtent * 0.5;
            for (const auto& childId : topic->childIds) {
                double childExtent = extents[childId];
                double childCenterY = cursorY + childExtent * 0.5;

                double gap = (topic->depth == 0) ? options.rootGap : options.levelGap;
                double childCenterX = centerX
                    + direction * (size.width * 0.5 + gap + sizes[childId].width * 0.5);

                place(childId, childCenterX, childCenterY, direction);
                cursorY += childExtent + options.siblingGap;
            }
        };

    // The root sits at the origin; each depth-1 subtree grows outward from it.
    Size2Dd rootSize = sizes[root->id];
    Rect2Dd rootBox(-rootSize.width * 0.5, -rootSize.height * 0.5,
                    rootSize.width, rootSize.height);
    root->bounds = rootBox;
    root->depth = 0;
    out.bounds[root->id] = rootBox;
    out.contentBounds.Include(rootBox);

    if (!root->collapsed) {
        // Each side is stacked independently so the two halves stay balanced.
        for (int pass = 0; pass < 2; ++pass) {
            MindMapTopicSide side = (pass == 0) ? MindMapTopicSide::Right : MindMapTopicSide::Left;
            int direction = (side == MindMapTopicSide::Right) ? 1 : -1;

            std::vector<std::string> sideChildren;
            for (const auto& id : root->childIds) {
                const MindMapTopic* child = model.GetTopic(id);
                if (child && child->resolvedSide == side) sideChildren.push_back(id);
            }
            if (sideChildren.empty()) continue;

            double sideExtent = 0.0;
            for (size_t i = 0; i < sideChildren.size(); ++i) {
                sideExtent += extents[sideChildren[i]];
                if (i + 1 < sideChildren.size()) sideExtent += options.siblingGap;
            }

            double cursorY = -sideExtent * 0.5;
            for (const auto& childId : sideChildren) {
                double childExtent = extents[childId];
                double childCenterY = cursorY + childExtent * 0.5;

                auto overrideIt = overrides.find(childId);
                if (overrideIt != overrides.end()) {
                    // Reserve the slot for the branch's own box, offset from the
                    // centre by half the root node plus the root gap.
                    const Rect2Dd& box = overrideIt->second.boundingBox;
                    double slotCentreX = direction *
                        (rootSize.width * 0.5 + options.rootGap + box.width * 0.5);
                    PlaceBranchOverride(model, childId, overrideIt->second,
                                        slotCentreX, childCenterY, out);
                } else {
                    double childCenterX = direction *
                        (rootSize.width * 0.5 + options.rootGap + sizes[childId].width * 0.5);
                    place(childId, childCenterX, childCenterY, direction);
                }
                cursorY += childExtent + options.siblingGap;
            }
        }
    }
}

// =============================================================================
// LAYOUT - ALIGNMENT POST-PASSES (L12)
// =============================================================================

namespace {

// Moves a topic and its whole subtree by (dx, dy).
void ShiftSubtree(MindMapModel& model, const std::string& id,
                  double dx, double dy, MindMapLayoutResult& out) {
    for (const auto& subtreeId : model.GetSubtreeIds(id)) {
        auto it = out.bounds.find(subtreeId);
        if (it == out.bounds.end()) continue;   // Hidden by a collapsed ancestor
        it->second.x += dx;
        it->second.y += dy;
        if (MindMapTopic* topic = model.GetTopic(subtreeId)) topic->bounds = it->second;
    }
}

} // namespace

// Every topic at the same depth is pushed out to the same distance from the
// centre, so both sides read as aligned columns instead of ragged fans.
static void AlignLevelColumns(MindMapModel& model, MindMapLayoutResult& out) {
    // Furthest inner edge per (depth, side) decides the shared column.
    std::map<std::pair<int, int>, double> columnEdge;   // (depth, side) -> x
    for (const auto& [id, box] : out.bounds) {
        const MindMapTopic* topic = model.GetTopic(id);
        if (!topic || topic->depth < 1) continue;
        int side = (topic->resolvedSide == MindMapTopicSide::Left) ? -1 : 1;
        auto key = std::make_pair(topic->depth, side);

        // Right side: the largest left edge. Left side: the smallest right edge.
        double edge = (side > 0) ? box.x : (box.x + box.width);
        auto it = columnEdge.find(key);
        if (it == columnEdge.end()) {
            columnEdge[key] = edge;
        } else if (side > 0) {
            it->second = std::max(it->second, edge);
        } else {
            it->second = std::min(it->second, edge);
        }
    }

    // Shift each topic (not its subtree - deeper levels get their own column).
    for (auto& [id, box] : out.bounds) {
        MindMapTopic* topic = model.GetTopic(id);
        if (!topic || topic->depth < 1) continue;
        int side = (topic->resolvedSide == MindMapTopicSide::Left) ? -1 : 1;
        auto it = columnEdge.find(std::make_pair(topic->depth, side));
        if (it == columnEdge.end()) continue;

        double target = (side > 0) ? it->second : it->second - box.width;
        box.x = target;
        topic->bounds = box;
    }
}

// The Nth main topic on the left is lined up with the Nth on the right.
static void AlignSideRows(MindMapModel& model, MindMapLayoutResult& out) {
    MindMapTopic* root = model.GetTopic(model.GetRootId());
    if (!root) return;

    auto centreY = [&](const std::string& id) {
        Rect2Dd box = out.Get(id);
        return box.y + box.height * 0.5;
    };

    std::vector<std::string> left, right;
    for (const auto& id : root->childIds) {
        const MindMapTopic* child = model.GetTopic(id);
        if (!child || !out.Has(id)) continue;
        (child->resolvedSide == MindMapTopicSide::Left ? left : right).push_back(id);
    }
    // Both sides are already stacked top-to-bottom by the main pass.
    std::sort(left.begin(),  left.end(),  [&](const std::string& a, const std::string& b) {
        return centreY(a) < centreY(b);
    });
    std::sort(right.begin(), right.end(), [&](const std::string& a, const std::string& b) {
        return centreY(a) < centreY(b);
    });

    size_t rows = std::min(left.size(), right.size());
    for (size_t row = 0; row < rows; ++row) {
        double shared = (centreY(left[row]) + centreY(right[row])) * 0.5;
        ShiftSubtree(model, left[row],  0.0, shared - centreY(left[row]),  out);
        ShiftSubtree(model, right[row], 0.0, shared - centreY(right[row]), out);
    }
}

// Authored positions win; anything without one keeps what the base pass gave it.
static void ApplyManualPositions(MindMapModel& model, MindMapLayoutResult& out) {
    for (auto& [id, box] : out.bounds) {
        MindMapTopic* topic = model.GetTopic(id);
        if (!topic || !topic->hasManualPosition) continue;
        box.x = topic->manualX;
        box.y = topic->manualY;
        topic->bounds = box;
    }
}

// =============================================================================
// LAYOUT - VERTICAL (OrgChartDown / OrgChartUp)
// =============================================================================

void UltraCanvasMindMapLayout::LayoutVertical(MindMapModel& model,
                                              const MindMapLayoutOptions& options,
                                              const MindMapMeasureFn& measure,
                                              const MindMapStyleFn& styleOf,
                                              MindMapLayoutResult& out) {
    MindMapTopic* root = model.GetTopic(model.GetRootId());
    if (!root) return;

    const int direction = (options.structure == MindMapStructure::OrgChartUp) ? -1 : 1;

    std::unordered_map<std::string, Size2Dd> sizes;
    std::function<void(const std::string&, int)> measureWalk =
        [&](const std::string& id, int depth) {
            MindMapTopic* topic = model.GetTopic(id);
            if (!topic) return;
            topic->depth = depth;
            topic->resolvedSide = MindMapTopicSide::Right;
            sizes[id] = MeasureTopic(*topic, styleOf(*topic), options, measure);
            if (topic->collapsed) return;
            for (const auto& childId : topic->childIds) measureWalk(childId, depth + 1);
        };
    measureWalk(root->id, 0);

    // Extents run along X here (siblings are stacked horizontally).
    std::unordered_map<std::string, double> extents;
    std::function<double(const std::string&)> computeExtent =
        [&](const std::string& id) -> double {
            MindMapTopic* topic = model.GetTopic(id);
            if (!topic) return 0.0;
            double own = sizes[id].width;
            if (topic->collapsed || topic->childIds.empty()) {
                extents[id] = own;
                return own;
            }
            double total = 0.0;
            for (size_t i = 0; i < topic->childIds.size(); ++i) {
                total += computeExtent(topic->childIds[i]);
                if (i + 1 < topic->childIds.size()) total += options.siblingGap;
            }
            extents[id] = std::max(total, own);
            return extents[id];
        };
    computeExtent(root->id);

    std::function<void(const std::string&, double, double)> place =
        [&](const std::string& id, double centerX, double centerY) {
            MindMapTopic* topic = model.GetTopic(id);
            if (!topic) return;

            Size2Dd size = sizes[id];
            Rect2Dd box(centerX - size.width * 0.5, centerY - size.height * 0.5,
                        size.width, size.height);
            topic->bounds = box;
            out.bounds[id] = box;
            out.contentBounds.Include(box);

            if (topic->collapsed || topic->childIds.empty()) return;

            double childrenExtent = 0.0;
            for (size_t i = 0; i < topic->childIds.size(); ++i) {
                childrenExtent += extents[topic->childIds[i]];
                if (i + 1 < topic->childIds.size()) childrenExtent += options.siblingGap;
            }

            double cursorX = centerX - childrenExtent * 0.5;
            for (const auto& childId : topic->childIds) {
                double childExtent = extents[childId];
                double childCenterX = cursorX + childExtent * 0.5;
                double gap = (topic->depth == 0) ? options.rootGap : options.levelGap;
                double childCenterY = centerY
                    + direction * (size.height * 0.5 + gap + sizes[childId].height * 0.5);
                place(childId, childCenterX, childCenterY);
                cursorX += childExtent + options.siblingGap;
            }
        };

    place(root->id, 0.0, 0.0);

    for (size_t branch = 0; branch < root->childIds.size(); ++branch) {
        for (const auto& id : model.GetSubtreeIds(root->childIds[branch])) {
            if (auto* t = model.GetTopic(id)) t->branchIndex = static_cast<int>(branch);
        }
    }
    root->branchIndex = -1;
}

// =============================================================================
// LAYOUT - RADIAL
// =============================================================================

void UltraCanvasMindMapLayout::LayoutRadial(MindMapModel& model,
                                            const MindMapLayoutOptions& options,
                                            const MindMapMeasureFn& measure,
                                            const MindMapStyleFn& styleOf,
                                            MindMapLayoutResult& out) {
    MindMapTopic* root = model.GetTopic(model.GetRootId());
    if (!root) return;

    std::unordered_map<std::string, Size2Dd> sizes;
    std::function<void(const std::string&, int)> measureWalk =
        [&](const std::string& id, int depth) {
            MindMapTopic* topic = model.GetTopic(id);
            if (!topic) return;
            topic->depth = depth;
            sizes[id] = MeasureTopic(*topic, styleOf(*topic), options, measure);
            if (topic->collapsed) return;
            for (const auto& childId : topic->childIds) measureWalk(childId, depth + 1);
        };
    measureWalk(root->id, 0);

    Size2Dd rootSize = sizes[root->id];
    Rect2Dd rootBox(-rootSize.width * 0.5, -rootSize.height * 0.5,
                    rootSize.width, rootSize.height);
    root->bounds = rootBox;
    root->depth = 0;
    root->branchIndex = -1;
    out.bounds[root->id] = rootBox;
    out.contentBounds.Include(rootBox);

    if (root->collapsed || root->childIds.empty()) return;

    // Each subtree gets an angular wedge proportional to its leaf count, so
    // dense branches are not crushed against sparse ones.
    std::function<void(const std::string&, double, double, int)> place =
        [&](const std::string& id, double angleStart, double angleSpan, int depth) {
            MindMapTopic* topic = model.GetTopic(id);
            if (!topic) return;

            double angle = angleStart + angleSpan * 0.5;
            double radius = options.radialRingGap * depth;
            double cx = std::cos(angle) * radius;
            double cy = std::sin(angle) * radius;

            Size2Dd size = sizes[id];
            Rect2Dd box(cx - size.width * 0.5, cy - size.height * 0.5, size.width, size.height);
            topic->bounds = box;
            // Side drives which way labels and connectors lean.
            topic->resolvedSide = (cx < 0.0) ? MindMapTopicSide::Left : MindMapTopicSide::Right;
            out.bounds[id] = box;
            out.contentBounds.Include(box);

            if (topic->collapsed || topic->childIds.empty()) return;

            double totalWeight = 0.0;
            for (const auto& childId : topic->childIds) totalWeight += LeafWeight(model, childId);
            if (totalWeight <= 0.0) return;

            double cursor = angleStart;
            for (const auto& childId : topic->childIds) {
                double share = LeafWeight(model, childId) / totalWeight;
                double childSpan = angleSpan * share;
                place(childId, cursor, childSpan, depth + 1);
                cursor += childSpan;
            }
        };

    double totalWeight = 0.0;
    for (const auto& id : root->childIds) totalWeight += LeafWeight(model, id);
    if (totalWeight <= 0.0) return;

    double cursor = options.radialStartAngle;
    for (size_t branch = 0; branch < root->childIds.size(); ++branch) {
        const std::string& childId = root->childIds[branch];
        double span = options.radialSweep * (LeafWeight(model, childId) / totalWeight);
        place(childId, cursor, span, 1);
        for (const auto& id : model.GetSubtreeIds(childId)) {
            if (auto* t = model.GetTopic(id)) t->branchIndex = static_cast<int>(branch);
        }
        cursor += span;
    }
}

// =============================================================================
// LAYOUT - BALLOON (radial cluster map)
// =============================================================================
//
// Bottom-up, every subtree is kept as the circles of its nodes in its topic's
// own frame (the topic at the origin, the direction away from its parent
// along +x). A topic's children sit on one ring around it: the ring radius is
// the smallest at which no child's circles come near the topic and the
// wedges the children's subtrees cover, seen from the topic, fit in the
// ring's sweep - found by bisection. Wedges that do not overlap mean
// subtrees that do not overlap, so siblings never collide at any depth.
// Top-down, each child takes the middle of its share of the sweep, centred
// on the direction away from its parent (Lin and Yen's balloon drawing with
// subtree wedges instead of enclosing discs, which keeps chains of hubs from
// doubling in size at every level). A subtree with a structureOverride is
// laid out in that structure and treated as one disc around its topic; a
// one-sided structure (Logic, OrgChart) is mirrored to grow away from the
// centre.

namespace {

constexpr double kBalloonPi = 3.14159265358979323846;

// The radius of the circle a node needs: a circle's own radius, otherwise
// half the box's diagonal.
double NodeRadius(const Size2Dd& size, MindMapNodeShape shape) {
    if (shape == MindMapNodeShape::Circle) return std::max(size.width, size.height) * 0.5;
    return 0.5 * std::sqrt(size.width * size.width + size.height * size.height);
}

// A node's circle in a subtree's own frame (see LayoutBalloon).
struct BalloonCircle {
    double x, y, r;
};

// The smallest ring radius at which no circle of a child's subtree comes
// within `gap` of the hub (radius `own`), and every circle's centre is in
// front of the hub, so the subtree's wedge is well defined.
double BalloonMinimumRing(const std::vector<BalloonCircle>& shape, double own, double gap) {
    double minimum = 0.0;
    for (const BalloonCircle& c : shape) {
        double clear = own + c.r + gap;
        double need = -c.x + 1e-6;
        if (std::abs(c.y) < clear) need = std::max(need, -c.x + std::sqrt(clear * clear - c.y * c.y));
        minimum = std::max(minimum, need);
    }
    return minimum;
}

// The angles a child's subtree covers either side of the child's direction,
// seen from the hub with the child at `ring`, each circle widened by half
// the gap.
std::pair<double, double> BalloonWedge(const std::vector<BalloonCircle>& shape, double ring, double gap) {
    double before = 0.0, after = 0.0;
    for (const BalloonCircle& c : shape) {
        double x = ring + c.x;
        double d = std::hypot(x, c.y);
        double centre = std::atan2(c.y, x);
        double half = (c.r + gap * 0.5 >= d) ? kBalloonPi * 0.5 : std::asin((c.r + gap * 0.5) / d);
        before = std::max(before, half - centre);
        after = std::max(after, centre + half);
    }
    return {before, after};
}

// The smallest ring radius, at least `minimum`, at which the children's
// wedges fit in `sweep` radians. The wedges narrow as the ring grows.
double BalloonRingRadius(const std::vector<const std::vector<BalloonCircle>*>& children,
                         double gap, double sweep, double minimum) {
    auto needed = [&](double ring) {
        double total = 0.0;
        for (const auto* shape : children) {
            auto wedge = BalloonWedge(*shape, ring, gap);
            total += wedge.first + wedge.second;
        }
        return total;
    };
    if (needed(minimum) <= sweep) return minimum;
    double lo = minimum;
    double hi = std::max(minimum * 2.0, 1.0);
    while (needed(hi) > sweep && hi < 1e9) {
        lo = hi;
        hi *= 2.0;
    }
    for (int i = 0; i < 60; ++i) {
        double mid = 0.5 * (lo + hi);
        if (needed(mid) > sweep) lo = mid;
        else hi = mid;
    }
    return hi;
}

} // namespace

void UltraCanvasMindMapLayout::LayoutBalloon(MindMapModel& model,
                                             const MindMapLayoutOptions& options,
                                             const MindMapMeasureFn& measure,
                                             const MindMapStyleFn& styleOf,
                                             MindMapLayoutResult& out) {
    MindMapTopic* root = model.GetTopic(model.GetRootId());
    if (!root) return;

    const double gap = std::max(0.0, options.balloonGap);
    const double fanSweep = std::clamp(options.balloonFanSweep, 30.0, 360.0) * kBalloonPi / 180.0;
    const bool fan = options.balloonRing == MindMapBalloonRing::Fan;
    double rootSweep = std::abs(options.radialSweep);
    if (!(rootSweep > 0.0)) rootSweep = 2.0 * kBalloonPi;
    rootSweep = std::min(rootSweep, 2.0 * kBalloonPi);

    // ---- Pass 1: depth, branch, size ----
    // Depth and branch first, so a style that depends on them (level styles,
    // sizes by depth, branch hues) sees the right values when measured.
    std::function<void(const std::string&, int, int)> tag =
        [&](const std::string& id, int depth, int branch) {
            MindMapTopic* topic = model.GetTopic(id);
            if (!topic) return;
            topic->depth = depth;
            topic->branchIndex = branch;
            for (size_t i = 0; i < topic->childIds.size(); ++i) {
                tag(topic->childIds[i], depth + 1, depth == 0 ? static_cast<int>(i) : branch);
            }
        };
    tag(root->id, 0, -1);

    // A subtree with its own structure is laid out first, as one block. Its
    // scratch copy sees depths from 0, so styles are resolved from the real
    // topic instead.
    MindMapStyleFn realStyle = [&model, &styleOf](const MindMapTopic& t) {
        const MindMapTopic* real = model.GetTopic(t.id);
        return styleOf(real ? *real : t);
    };
    struct OverrideBlock {
        MindMapLayoutResult result;
        Point2Dd centre;   // The topic's centre in the block's own space
    };
    std::unordered_map<std::string, OverrideBlock> overrides;

    std::unordered_map<std::string, Size2Dd> sizes;
    std::unordered_map<std::string, double> nodeRadius;
    std::function<void(const std::string&)> measureWalk = [&](const std::string& id) {
        MindMapTopic* topic = model.GetTopic(id);
        if (!topic) return;
        MindMapTopicStyle topicStyle = styleOf(*topic);
        sizes[id] = MeasureTopic(*topic, topicStyle, options, measure);
        nodeRadius[id] = NodeRadius(sizes[id], topicStyle.shape);
        if (topic->collapsed) return;
        if (topic->depth > 0 && topic->structureOverride.has_value() &&
            topic->structureOverride.value() != MindMapStructure::Balloon &&
            !topic->childIds.empty()) {
            // Laid out through a scratch model whose root is this topic.
            MindMapModel scratch;
            for (const auto& subId : model.GetSubtreeIds(id)) {
                if (const MindMapTopic* t = model.GetTopic(subId)) scratch.Topics()[subId] = *t;
            }
            MindMapTopic& scratchRoot = scratch.Topics()[id];
            scratchRoot.parentId.clear();
            scratchRoot.structureOverride.reset();
            scratch.RestoreStructure(id);
            MindMapLayoutOptions subOptions = options;
            subOptions.structure = topic->structureOverride.value();
            subOptions.alignLevelColumns = false;
            subOptions.alignSideRows = false;
            OverrideBlock block;
            ComputeLayout(scratch, subOptions, measure, realStyle, block.result);
            if (block.result.Has(id)) {
                Rect2Dd box = block.result.Get(id);
                block.centre = Point2Dd(box.x + box.width * 0.5, box.y + box.height * 0.5);
                overrides[id] = std::move(block);
                return;
            }
        }
        for (const auto& childId : topic->childIds) measureWalk(childId);
    };
    measureWalk(root->id);

    // ---- Pass 2: bottom-up shapes ----
    // Each subtree is kept as the circles of its nodes in the topic's own
    // frame: the topic at the origin, the direction away from its parent
    // along +x. A child's place is (ring radius, angle from +x).
    std::unordered_map<std::string, std::vector<BalloonCircle>> shapes;
    std::unordered_map<std::string, double> ring;      // Ring radius of a topic's children
    std::unordered_map<std::string, double> angleOf;   // A child's angle in its parent's frame

    std::function<void(const std::string&)> computeShape = [&](const std::string& id) {
        MindMapTopic* topic = model.GetTopic(id);
        if (!topic) return;
        const double own = nodeRadius[id];
        std::vector<BalloonCircle>& shape = shapes[id];
        shape.clear();
        auto overrideIt = overrides.find(id);
        if (overrideIt != overrides.end()) {
            // A block in another structure keeps its own orientation, so it
            // is one disc around the topic whatever way the topic faces.
            double farthest = own;
            const Point2Dd c = overrideIt->second.centre;
            for (const auto& [subId, box] : overrideIt->second.result.bounds) {
                for (double x : {box.x, box.x + box.width}) {
                    for (double y : {box.y, box.y + box.height}) {
                        farthest = std::max(farthest, std::hypot(x - c.x, y - c.y));
                    }
                }
            }
            shape.push_back({0.0, 0.0, farthest});
            return;
        }
        shape.push_back({0.0, 0.0, own});
        if (topic->collapsed || topic->childIds.empty()) return;

        std::vector<const std::vector<BalloonCircle>*> children;
        for (const auto& childId : topic->childIds) {
            computeShape(childId);
            children.push_back(&shapes[childId]);
        }
        const bool isRoot = topic->depth == 0;
        const double sweep = isRoot ? rootSweep : (fan ? fanSweep : 2.0 * kBalloonPi);
        double minimum = 0.0;
        for (const auto* child : children) {
            minimum = std::max(minimum, BalloonMinimumRing(*child, own, gap));
        }
        const double r = BalloonRingRadius(children, gap, sweep, minimum);
        ring[id] = r;

        // Each child's wedge, in order from -sweep / 2 (from the start angle
        // for the centre), the slack shared evenly.
        std::vector<std::pair<double, double>> wedges;
        double total = 0.0;
        for (const auto* child : children) {
            wedges.push_back(BalloonWedge(*child, r, gap));
            total += wedges.back().first + wedges.back().second;
        }
        const double slack = std::max(0.0, sweep - total) / static_cast<double>(children.size());
        double cursor = isRoot ? 0.0 : -sweep * 0.5;
        for (size_t i = 0; i < children.size(); ++i) {
            const double angle = cursor + slack * 0.5 + wedges[i].first;
            angleOf[topic->childIds[i]] = angle;
            cursor += wedges[i].first + wedges[i].second + slack;
            if (isRoot) continue;   // The centre's shape is never asked for
            const double ca = std::cos(angle), sa = std::sin(angle);
            for (const BalloonCircle& c : *children[i]) {
                shape.push_back({r * ca + c.x * ca - c.y * sa, r * sa + c.x * sa + c.y * ca, c.r});
            }
        }
    };
    computeShape(root->id);

    // ---- Pass 3: top-down placement ----
    auto placeBox = [&](MindMapTopic& topic, double cx, double cy) {
        Size2Dd size = sizes[topic.id];
        Rect2Dd box(cx - size.width * 0.5, cy - size.height * 0.5, size.width, size.height);
        topic.bounds = box;
        out.bounds[topic.id] = box;
        out.contentBounds.Include(box);
    };

    std::function<void(const std::string&, double, double, double)> place =
        [&](const std::string& id, double cx, double cy, double away) {
            MindMapTopic* topic = model.GetTopic(id);
            if (!topic) return;
            topic->resolvedSide = (std::cos(away) < -1e-9) ? MindMapTopicSide::Left
                                                           : MindMapTopicSide::Right;
            auto overrideIt = overrides.find(id);
            if (overrideIt != overrides.end()) {
                const OverrideBlock& block = overrideIt->second;
                // A one-sided block grows away from the centre: a list facing
                // back would run over the trunk coming in. The block's disc is
                // symmetric, so mirroring it changes nothing around it.
                const MindMapStructure kind = topic->structureOverride.value();
                const bool flipX = (kind == MindMapStructure::LogicRight && std::cos(away) < -1e-9) ||
                                   (kind == MindMapStructure::LogicLeft && std::cos(away) > 1e-9);
                const bool flipY = (kind == MindMapStructure::OrgChartDown && std::sin(away) < -1e-9) ||
                                   (kind == MindMapStructure::OrgChartUp && std::sin(away) > 1e-9);
                for (const auto& [subId, local] : block.result.bounds) {
                    Rect2Dd box = local;
                    if (flipX) box.x = 2.0 * block.centre.x - box.x - box.width;
                    if (flipY) box.y = 2.0 * block.centre.y - box.y - box.height;
                    Rect2Dd moved(box.x + cx - block.centre.x, box.y + cy - block.centre.y,
                                  box.width, box.height);
                    out.bounds[subId] = moved;
                    out.contentBounds.Include(moved);
                    if (MindMapTopic* sub = model.GetTopic(subId)) {
                        sub->bounds = moved;
                        if (sub->id != id) sub->resolvedSide = topic->resolvedSide;
                    }
                }
                return;
            }
            placeBox(*topic, cx, cy);
            if (topic->collapsed || topic->childIds.empty()) return;
            const double r = ring[id];
            for (const auto& childId : topic->childIds) {
                const double angle = away + angleOf[childId];
                place(childId, cx + std::cos(angle) * r, cy + std::sin(angle) * r, angle);
            }
        };

    root->resolvedSide = MindMapTopicSide::Right;
    placeBox(*root, 0.0, 0.0);
    if (root->collapsed || root->childIds.empty()) return;

    // The main topics: a ring around the centre from the start angle, pushed
    // out by the trunk length. Pushing out only narrows each wedge, so the
    // angles found at the inner radius stay clear of one another.
    const double rootRing = ring[root->id] + std::max(0.0, options.balloonTrunkLength);
    for (const auto& childId : root->childIds) {
        const double angle = options.radialStartAngle + angleOf[childId];
        place(childId, std::cos(angle) * rootRing, std::sin(angle) * rootRing, angle);
    }
}

// =============================================================================
// LAYOUT - ENTRY POINT
// =============================================================================

void UltraCanvasMindMapLayout::ComputeLayout(MindMapModel& model,
                                             const MindMapLayoutOptions& options,
                                             const MindMapMeasureFn& measure,
                                             const MindMapStyleFn& styleOf,
                                             MindMapLayoutResult& outResult) {
    outResult.bounds.clear();
    outResult.contentBounds = DiagramContentBounds();
    if (model.IsEmpty()) return;

    MindMapStyleFn resolveStyle = styleOf
        ? styleOf
        : [](const MindMapTopic& topic) {
              return topic.styleOverride.value_or(MindMapTopicStyle());
          };

    if (!model.GetRootId().empty()) {
        AssignSides(model, options);

        switch (options.structure) {
            case MindMapStructure::Manual:
            case MindMapStructure::OrgChartDown:
            case MindMapStructure::OrgChartUp:
                LayoutVertical(model, options, measure, resolveStyle, outResult);
                break;
            case MindMapStructure::Radial:
                LayoutRadial(model, options, measure, resolveStyle, outResult);
                break;
            case MindMapStructure::Balloon:
                LayoutBalloon(model, options, measure, resolveStyle, outResult);
                break;
            case MindMapStructure::Balanced:
            case MindMapStructure::LogicRight:
            case MindMapStructure::LogicLeft:
            default:
                LayoutHorizontal(model, options, measure, resolveStyle, outResult);
                break;
        }

        // Post-passes run in a fixed order: columns, then rows, then authored
        // overrides, so a manual position always has the final say.
        const bool horizontal = (options.structure == MindMapStructure::Balanced ||
                                 options.structure == MindMapStructure::LogicRight ||
                                 options.structure == MindMapStructure::LogicLeft ||
                                 options.structure == MindMapStructure::Manual);
        if (options.alignLevelColumns && horizontal) {
            AlignLevelColumns(model, outResult);
        }
        if (options.alignSideRows && (options.structure == MindMapStructure::Balanced ||
                                      options.structure == MindMapStructure::Manual)) {
            AlignSideRows(model, outResult);
        }
        if (options.structure == MindMapStructure::Manual) {
            ApplyManualPositions(model, outResult);
        }

        // Any post-pass may have moved boxes, so the extent is recomputed
        // rather than trusted from the base pass.
        if (options.alignLevelColumns || options.alignSideRows ||
            options.structure == MindMapStructure::Manual) {
            outResult.contentBounds = DiagramContentBounds();
            for (const auto& [id, box] : outResult.bounds) outResult.contentBounds.Include(box);
        }
    }

    // Floating topics keep the position the app gave them; they are still
    // measured so hit-testing and content bounds include them.
    for (const auto& id : model.FloatingTopicIds()) {
        MindMapTopic* topic = model.GetTopic(id);
        if (!topic) continue;
        Size2Dd size = MeasureTopic(*topic, resolveStyle(*topic), options, measure);
        Rect2Dd box(topic->floatX, topic->floatY, size.width, size.height);
        topic->bounds = box;
        topic->depth = 0;
        outResult.bounds[id] = box;
        outResult.contentBounds.Include(box);

        // Children of a floating topic are laid out relative to it, growing right.
        if (topic->collapsed) continue;
        std::function<void(const std::string&, double, double, int)> placeChild =
            [&](const std::string& childId, double x, double y, int depth) {
                MindMapTopic* child = model.GetTopic(childId);
                if (!child) return;
                Size2Dd childSize = MeasureTopic(*child, resolveStyle(*child), options, measure);
                Rect2Dd childBox(x, y, childSize.width, childSize.height);
                child->bounds = childBox;
                child->depth = depth;
                child->resolvedSide = MindMapTopicSide::Right;
                outResult.bounds[childId] = childBox;
                outResult.contentBounds.Include(childBox);
                if (child->collapsed) return;
                double cursorY = y;
                for (const auto& grandChildId : child->childIds) {
                    placeChild(grandChildId, x + childSize.width + options.levelGap,
                               cursorY, depth + 1);
                    auto it = outResult.bounds.find(grandChildId);
                    cursorY += (it != outResult.bounds.end() ? it->second.height : 0.0)
                             + options.siblingGap;
                }
            };
        double cursorY = topic->floatY;
        for (const auto& childId : topic->childIds) {
            placeChild(childId, topic->floatX + size.width + options.levelGap, cursorY, 1);
            auto it = outResult.bounds.find(childId);
            cursorY += (it != outResult.bounds.end() ? it->second.height : 0.0)
                     + options.siblingGap;
        }
    }
}

} // namespace UltraCanvas
