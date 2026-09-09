// core/UltraCanvasHardwareInfoPanel.cpp
// The "System information" view: turns an UltraCanvasHardwareInfo report into
// Name/Value rows on a columns tree. All rendering is the tree's.
// Version: 0.1.0
// Last Modified: 2026-08-29
// Author: UltraCanvas Framework

#include "UltraCanvasHardwareInfoPanel.h"

#include <algorithm>
#include <set>

namespace UltraCanvas {

namespace {
const char* kRootNodeId  = "hardware.root";
const char* kValueColumn = "value";
} // namespace

// ===== CONSTRUCTION =====

UltraCanvasHardwareInfoPanel::UltraCanvasHardwareInfoPanel(const std::string& identifier,
                                                           float x, float y,
                                                           float width, float height)
    : UltraCanvasColumnsTreeView(identifier, x, y, width, height) {
    InstallColumns();
    Refresh();
}

UltraCanvasHardwareInfoPanel::UltraCanvasHardwareInfoPanel(const std::string& identifier,
                                                           float width, float height)
    : UltraCanvasColumnsTreeView(identifier, width, height) {
    InstallColumns();
    Refresh();
}

UltraCanvasHardwareInfoPanel::UltraCanvasHardwareInfoPanel(const std::string& identifier)
    : UltraCanvasColumnsTreeView(identifier) {
    InstallColumns();
    Refresh();
}

void UltraCanvasHardwareInfoPanel::InstallColumns() {
    TreeViewColumn name;
    name.id = "name";
    name.title = "Component";
    name.isTreeColumn = true;
    name.flexWeight = 1.0f;
    name.minWidth = 140;

    TreeViewColumn value;
    value.id = kValueColumn;
    value.title = "Value";
    value.flexWeight = 1.4f;
    value.minWidth = 120;

    SetColumns({ name, value });
    SetShowColumnHeader(true);
    SetRootVisible(false);
    SetSelectionMode(TreeSelectionMode::Single);
}

// ===== CONTENT =====

void UltraCanvasHardwareInfoPanel::SetQuery(HardwareQuery what) {
    query = what;
    // Not a forced refresh: a narrowed query is usually served straight from the
    // snapshot the panel already captured.
    Refresh(false);
}

void UltraCanvasHardwareInfoPanel::SetSectionsExpanded(bool expanded) {
    sectionsExpanded = expanded;
    Rebuild();
}

void UltraCanvasHardwareInfoPanel::SetSnapshot(const HardwareSnapshot& newSnapshot) {
    snapshot = newSnapshot;
    Rebuild();
    if (onSnapshotChanged) onSnapshotChanged(snapshot);
}

void UltraCanvasHardwareInfoPanel::Refresh(bool forceRefresh) {
    snapshot = UltraCanvasHardwareInfo::Capture(query, forceRefresh);
    Rebuild();
    if (onSnapshotChanged) onSnapshotChanged(snapshot);
}

void UltraCanvasHardwareInfoPanel::RefreshSensors() {
    UltraCanvasHardwareInfo::RefreshSensors(snapshot);
    // Writing into the existing rows keeps expansion, selection and scroll
    // position - which is the whole point of a panel that updates on a timer.
    if (!UpdateValuesInPlace()) Rebuild();
    RequestRedraw();
    if (onSnapshotChanged) onSnapshotChanged(snapshot);
}

// ===== TREE BUILDING =====

std::string UltraCanvasHardwareInfoPanel::PropertyNodeId(const std::string& groupId,
                                                          const std::string& propertyName) {
    return groupId + "#" + propertyName;
}

void UltraCanvasHardwareInfoPanel::AddGroupNodes(const HardwarePropertyGroup& group,
                                                  const std::string& parentNodeId, int depth) {
    TreeNodeData groupData(group.id, group.title);
    // Top-level categories read as section headings; anything deeper is an
    // ordinary row so the hierarchy stays legible.
    groupData.isGroupHeader = (depth == 0);
    groupData.showFirstChildOnExpand = false;
    if (!AddNode(parentNodeId, groupData)) return;

    for (const auto& property : group.properties) {
        TreeNodeData row(PropertyNodeId(group.id, property.name), property.name);
        row.SetCell(kValueColumn, property.value);
        row.tooltip = property.tooltip.empty() ? property.value : property.tooltip;
        row.showFirstChildOnExpand = false;
        AddNode(group.id, row);
    }
    for (const auto& child : group.subGroups) AddGroupNodes(child, group.id, depth + 1);
}

void UltraCanvasHardwareInfoPanel::Rebuild() {
    // Remember what the user had open so a refresh does not fold the tree shut
    // under them.
    std::set<std::string> expandedIds;
    if (TreeNode* root = GetRootNode()) {
        std::vector<TreeNode*> pending{ root };
        while (!pending.empty()) {
            TreeNode* node = pending.back();
            pending.pop_back();
            if (node->IsExpanded()) expandedIds.insert(node->data.nodeId);
            for (const auto& child : node->children) pending.push_back(child.get());
        }
    }
    const bool hadTree = !expandedIds.empty();

    SetRootNode(TreeNodeData(kRootNodeId, "Hardware"));
    for (const auto& group : UltraCanvasHardwareInfo::BuildReport(snapshot))
        AddGroupNodes(group, kRootNodeId, 0);

    if (TreeNode* root = GetRootNode()) {
        for (const auto& child : root->children) {
            const bool wasExpanded = expandedIds.count(child->data.nodeId) != 0;
            if (wasExpanded || (!hadTree && sectionsExpanded)) ExpandNode(child.get());
        }
        // Restore deeper levels too, so an opened drive or adapter stays opened.
        if (hadTree) {
            std::vector<TreeNode*> pending;
            for (const auto& child : root->children) pending.push_back(child.get());
            while (!pending.empty()) {
                TreeNode* node = pending.back();
                pending.pop_back();
                for (const auto& child : node->children) {
                    if (expandedIds.count(child->data.nodeId)) ExpandNode(child.get());
                    pending.push_back(child.get());
                }
            }
        }
    }
    RequestRedraw();
}

bool UltraCanvasHardwareInfoPanel::UpdateValuesInPlace() {
    for (const auto& group : UltraCanvasHardwareInfo::BuildReport(snapshot)) {
        std::vector<const HardwarePropertyGroup*> pending{ &group };
        while (!pending.empty()) {
            const HardwarePropertyGroup* current = pending.back();
            pending.pop_back();
            for (const auto& property : current->properties) {
                TreeNode* node = FindNode(PropertyNodeId(current->id, property.name));
                // A row that was not there before (a temperature that has just
                // become readable, a drive that appeared) changes the shape of
                // the tree, so the caller has to rebuild instead.
                if (!node) return false;
                node->data.SetCell(kValueColumn, property.value);
                node->data.tooltip = property.tooltip.empty() ? property.value : property.tooltip;
            }
            for (const auto& child : current->subGroups) pending.push_back(&child);
        }
    }
    return true;
}

// ===== FACTORY =====

std::shared_ptr<UltraCanvasHardwareInfoPanel> CreateHardwareInfoPanel(
    const std::string& identifier, float x, float y, float width, float height,
    HardwareQuery what) {
    auto panel = std::make_shared<UltraCanvasHardwareInfoPanel>(identifier, x, y, width, height);
    if (what != HardwareQuery::All) panel->SetQuery(what);
    return panel;
}

} // namespace UltraCanvas
