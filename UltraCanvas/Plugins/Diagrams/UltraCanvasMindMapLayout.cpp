// Plugins/Diagrams/UltraCanvasMindMapLayout.cpp
// Mind map topic model and layout engine
// Version: 1.0.0
// Last Modified: 2026-07-30
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
// LAYOUT - HORIZONTAL (Balanced / LogicRight / LogicLeft)
// =============================================================================

void UltraCanvasMindMapLayout::LayoutHorizontal(MindMapModel& model,
                                                const MindMapLayoutOptions& options,
                                                const MindMapMeasureFn& measure,
                                                const MindMapStyleFn& styleOf,
                                                MindMapLayoutResult& out) {
    MindMapTopic* root = model.GetTopic(model.GetRootId());
    if (!root) return;

    // ---- Pass 1: measure every visible topic ----
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

    // ---- Pass 2: bottom-up subtree extents (vertical space each subtree needs) ----
    std::unordered_map<std::string, double> extents;
    std::function<double(const std::string&)> computeExtent =
        [&](const std::string& id) -> double {
            MindMapTopic* topic = model.GetTopic(id);
            if (!topic) return 0.0;
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
                double childCenterX = direction *
                    (rootSize.width * 0.5 + options.rootGap + sizes[childId].width * 0.5);
                place(childId, childCenterX, childCenterY, direction);
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
