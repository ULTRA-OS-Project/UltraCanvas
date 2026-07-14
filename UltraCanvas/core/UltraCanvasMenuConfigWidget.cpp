// core/UltraCanvasMenuConfigWidget.cpp
// Implementation of the menu customisation widget.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasMenuConfigWidget.h"
#include "UltraCanvasUtils.h"   // Split
#include "CSSLayout/CSSLayout.h"

namespace UltraCanvas {

    UltraCanvasMenuConfigWidget::UltraCanvasMenuConfigWidget(
            const std::string& id, float x, float y, float w, float h)
            : UltraCanvasContainer(id, x, y, w, h) {
        registry = &UltraCanvasMenuRegistry::Default();
        BuildUi(w, h);
    }

    void UltraCanvasMenuConfigWidget::SetRegistry(UltraCanvasMenuRegistry* reg) {
        registry = reg ? reg : &UltraCanvasMenuRegistry::Default();
        RebuildRightTree();
        RebuildLeftTree();
    }

    void UltraCanvasMenuConfigWidget::SetLayoutSet(const MenuLayoutSet& set) {
        workingSet = set;
        baselineSet = set;
        selectedLeftId.clear();
        RebuildLeftTree();
    }

// ===== UI construction =====
    void UltraCanvasMenuConfigWidget::BuildUi(float w, float h) {
        this->layout.SetFlexColumn().SetFlexGap(6)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        SetPadding(8.0f);

        // ----- three-column split pane -----
        splitPane = std::make_shared<UltraCanvasSplitPane>(
                GetIdentifier() + "_Split", SplitOrientation::Horizontal);
        splitPane->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        auto leftPane = splitPane->AddPane(1.0);
        auto midPane  = splitPane->AddPane(0.18);
        auto rightPane = splitPane->AddPane(1.0);
        splitPane->SetPaneMinSize(1, 128);

        // Left pane: title + menu-structure tree.
        leftPane->layout.SetFlexColumn().SetFlexGap(4)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        auto leftTitle = std::make_shared<UltraCanvasLabel>(
                GetIdentifier() + "_LeftTitle", "Menu structure");
        leftTitle->SetFontWeight(FontWeight::Bold);
        leftPane->AddChild(leftTitle);

        leftTree = std::make_shared<UltraCanvasTreeView>(GetIdentifier() + "_LeftTree");
        leftTree->SetSelectionMode(TreeSelectionMode::Single);
        leftTree->SetRowHeight(22);
        leftTree->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        leftTree->onNodeSelected = [this](TreeNode* n) {
            selectedLeftId = n ? n->data.nodeId : std::string();
        };
        leftPane->AddChild(leftTree);

        // Middle pane: action buttons.
        midPane->layout.SetFlexColumn().SetFlexGap(6)
                .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
                .SetFlexAlignItems(CSSLayout::AlignItems::Center);

        auto makeMidButton = [&](const std::string& suffix, const std::string& text,
                                 std::function<void()> cb) {
            auto btn = std::make_shared<UltraCanvasButton>(
                    GetIdentifier() + "_" + suffix, 0.0f, 0.0f, 118.0f, 28.0f, text);
            btn->SetOnClick(std::move(cb));
            midPane->AddChild(btn);
            return btn;
        };
        makeMidButton("Add",    "Add \xE2\x86\x92",     [this]() { OnAddCommand(); });
        makeMidButton("Remove", "\xE2\x86\x90 Remove",  [this]() { OnRemove(); });
        makeMidButton("Up",     "Move Up",              [this]() { OnMove(-1); });
        makeMidButton("Down",   "Move Down",            [this]() { OnMove(+1); });
        makeMidButton("Sub",    "New Submenu",          [this]() { OnAddSubmenu(); });
        makeMidButton("Sep",    "Separator",            [this]() { OnAddSeparator(); });

        // Right pane: title + registry catalog tree.
        rightPane->layout.SetFlexColumn().SetFlexGap(4)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        auto rightTitle = std::make_shared<UltraCanvasLabel>(
                GetIdentifier() + "_RightTitle", "Available commands");
        rightTitle->SetFontWeight(FontWeight::Bold);
        rightPane->AddChild(rightTitle);

        rightTree = std::make_shared<UltraCanvasTreeView>(GetIdentifier() + "_RightTree");
        rightTree->SetSelectionMode(TreeSelectionMode::Single);
        rightTree->SetRowHeight(22);
        rightTree->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        rightTree->onNodeSelected = [this](TreeNode* n) {
            selectedRightId = n ? n->data.nodeId : std::string();
        };
        rightTree->onNodeDoubleClicked = [this](TreeNode* n) {
            if (n) { selectedRightId = n->data.nodeId; OnAddCommand(); }
        };
        rightPane->AddChild(rightTree);

        AddChild(splitPane);

        // ----- footer button row -----
        auto footer = std::make_shared<UltraCanvasContainer>(GetIdentifier() + "_Footer");
        footer->size.height = CSSLayout::Dimension::Px(40);
        footer->layout.SetFlexRow().SetFlexGap(8)
                .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        footer->layoutItem.SetFlexShrink(0);

        auto makeFooterButton = [&](const std::string& suffix, const std::string& text,
                                    std::function<void()> cb) {
            auto btn = std::make_shared<UltraCanvasButton>(
                    GetIdentifier() + "_" + suffix, 0.0f, 0.0f, 90.0f, 28.0f, text);
            btn->SetOnClick(std::move(cb));
            footer->AddChild(btn);
            return btn;
        };

        // Reset on the left, primary actions on the right.
        makeFooterButton("Reset", "Reset", [this]() {
            workingSet = baselineSet;
            selectedLeftId.clear();
            RebuildLeftTree();
            if (onReset) onReset();
        });
        footer->AddStretchSpacer(1);
        makeFooterButton("Cancel", "Cancel", [this]() { if (onCancel) onCancel(); });
        makeFooterButton("Apply",  "Apply",  [this]() { if (onApply) onApply(workingSet); });
        makeFooterButton("Save",   "Save",   [this]() { if (onSave)  onSave(workingSet); });

        AddChild(footer);

        RebuildRightTree();
        RebuildLeftTree();
    }

// ===== left tree =====
    std::string UltraCanvasMenuConfigWidget::LeftNodeText(const MenuLayoutNode& node) const {
        switch (node.kind) {
            case MenuLayoutNode::Kind::Separator:
                return "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"; // ────
            case MenuLayoutNode::Kind::Group:
                return node.label.empty() ? "(submenu)" : node.label;
            case MenuLayoutNode::Kind::Command:
            default: {
                if (!node.label.empty()) return node.label;
                const MenuCommand* c = registry ? registry->Find(node.commandId) : nullptr;
                if (c && !c->label.empty()) return c->label;
                return "<" + node.commandId + ">";
            }
        }
    }

    std::string UltraCanvasMenuConfigWidget::LeftNodeIcon(const MenuLayoutNode& node) const {
        if (node.kind == MenuLayoutNode::Kind::Group) return node.iconPath;
        if (node.kind == MenuLayoutNode::Kind::Command && registry) {
            if (const MenuCommand* c = registry->Find(node.commandId)) return c->iconPath;
        }
        return std::string();
    }

    void UltraCanvasMenuConfigWidget::BuildLeftNode(const std::string& parentId,
                                                    const MenuLayoutNode& node,
                                                    const std::string& nodeId) {
        TreeNodeData data(nodeId, LeftNodeText(node));
        std::string icon = LeftNodeIcon(node);
        if (!icon.empty()) data.leftIcon = TreeNodeIcon(icon, 16, 16);
        leftTree->AddNode(parentId, data);

        for (size_t k = 0; k < node.children.size(); ++k) {
            BuildLeftNode(nodeId, node.children[k], nodeId + "." + std::to_string(k));
        }
    }

    void UltraCanvasMenuConfigWidget::RebuildLeftTree() {
        if (!leftTree) return;
        leftTree->ClearSelection();

        TreeNodeData rootData("root", "Program");
        leftTree->SetRootNode(rootData);

        for (size_t i = 0; i < workingSet.menus.size(); ++i) {
            std::string mid = "m" + std::to_string(i);
            const MenuLayout& menu = workingSet.menus[i];
            TreeNodeData md(mid, menu.name.empty() ? "(menu)" : menu.name);
            leftTree->AddNode("root", md);
            for (size_t j = 0; j < menu.topLevel.size(); ++j) {
                BuildLeftNode(mid, menu.topLevel[j], mid + "." + std::to_string(j));
            }
        }

        if (TreeNode* root = leftTree->GetRootNode()) root->Expand();
        leftTree->ExpandAll();
    }

// ===== right tree =====
    void UltraCanvasMenuConfigWidget::RebuildRightTree() {
        if (!rightTree) return;
        rightTree->ClearSelection();

        TreeNodeData rootData("catroot", "Commands");
        rightTree->SetRootNode(rootData);

        if (registry) {
            for (const auto& cat : registry->Categories()) {
                std::string cid = "cat:" + cat;
                rightTree->AddNode("catroot", TreeNodeData(cid, cat));
                for (const MenuCommand* cmd : registry->InCategory(cat)) {
                    TreeNodeData d("cmd:" + cmd->id, cmd->label.empty() ? cmd->id : cmd->label);
                    if (!cmd->iconPath.empty()) d.leftIcon = TreeNodeIcon(cmd->iconPath, 16, 16);
                    rightTree->AddNode(cid, d);
                }
            }
        }

        if (TreeNode* root = rightTree->GetRootNode()) root->Expand();
        rightTree->ExpandAll();
    }

// ===== node addressing =====
    UltraCanvasMenuConfigWidget::LeftRef
    UltraCanvasMenuConfigWidget::ParseLeftId(const std::string& nodeId) const {
        LeftRef ref;
        if (nodeId.empty() || nodeId == "root") {
            ref.isRoot = true;
            return ref;
        }
        if (nodeId[0] != 'm') return ref; // not a left-layout id
        std::vector<std::string> parts = Split(nodeId.substr(1), '.');
        if (parts.empty()) return ref;
        try {
            ref.menuIndex = std::stoi(parts[0]);
        } catch (...) {
            ref.menuIndex = -1;
            return ref;
        }
        for (size_t k = 1; k < parts.size(); ++k) {
            try { ref.path.push_back(std::stoi(parts[k])); }
            catch (...) { ref.path.clear(); break; }
        }
        return ref;
    }

    std::string UltraCanvasMenuConfigWidget::MakeLeftId(const LeftRef& ref) const {
        if (ref.isRoot || ref.menuIndex < 0) return "root";
        std::string id = "m" + std::to_string(ref.menuIndex);
        for (int idx : ref.path) id += "." + std::to_string(idx);
        return id;
    }

    MenuLayoutNode* UltraCanvasMenuConfigWidget::ResolveNode(const LeftRef& ref) {
        if (ref.menuIndex < 0 || ref.menuIndex >= static_cast<int>(workingSet.menus.size())) {
            return nullptr;
        }
        if (ref.path.empty()) return nullptr; // the menu itself, not a node
        std::vector<MenuLayoutNode>* list = &workingSet.menus[ref.menuIndex].topLevel;
        MenuLayoutNode* node = nullptr;
        for (int idx : ref.path) {
            if (idx < 0 || idx >= static_cast<int>(list->size())) return nullptr;
            node = &(*list)[idx];
            list = &node->children;
        }
        return node;
    }

    std::vector<MenuLayoutNode>*
    UltraCanvasMenuConfigWidget::ResolveParentList(const LeftRef& ref, int& indexInParent) {
        indexInParent = -1;
        if (ref.menuIndex < 0 || ref.menuIndex >= static_cast<int>(workingSet.menus.size())) {
            return nullptr;
        }
        if (ref.path.empty()) return nullptr;
        std::vector<MenuLayoutNode>* list = &workingSet.menus[ref.menuIndex].topLevel;
        for (size_t k = 0; k + 1 < ref.path.size(); ++k) {
            int idx = ref.path[k];
            if (idx < 0 || idx >= static_cast<int>(list->size())) return nullptr;
            list = &(*list)[idx].children;
        }
        indexInParent = ref.path.back();
        if (indexInParent < 0 || indexInParent >= static_cast<int>(list->size())) return nullptr;
        return list;
    }

    void UltraCanvasMenuConfigWidget::ComputeInsertTarget(
            std::vector<MenuLayoutNode>*& outList, int& outIndex) {
        outList = nullptr;
        outIndex = 0;

        LeftRef ref = ParseLeftId(selectedLeftId);

        // Nothing / Program root selected -> append to first menu.
        if (ref.isRoot || ref.menuIndex < 0) {
            if (!workingSet.menus.empty()) {
                outList = &workingSet.menus[0].topLevel;
                outIndex = static_cast<int>(outList->size());
            }
            return;
        }

        // A whole menu selected -> append to its top level.
        if (ref.path.empty()) {
            outList = &workingSet.menus[ref.menuIndex].topLevel;
            outIndex = static_cast<int>(outList->size());
            return;
        }

        // A group selected -> append inside it.
        if (MenuLayoutNode* node = ResolveNode(ref); node && node->IsGroup()) {
            outList = &node->children;
            outIndex = static_cast<int>(node->children.size());
            return;
        }

        // Otherwise insert right after the selected node in its parent list.
        int idx = -1;
        if (std::vector<MenuLayoutNode>* parent = ResolveParentList(ref, idx)) {
            outList = parent;
            outIndex = idx + 1;
            return;
        }

        // Fallback: append to the menu's top level.
        outList = &workingSet.menus[ref.menuIndex].topLevel;
        outIndex = static_cast<int>(outList->size());
    }

// ===== actions =====
    void UltraCanvasMenuConfigWidget::InsertAtTarget(const MenuLayoutNode& node) {
        std::vector<MenuLayoutNode>* list = nullptr;
        int index = 0;
        ComputeInsertTarget(list, index);
        if (!list) return;
        if (index < 0) index = 0;
        if (index > static_cast<int>(list->size())) index = static_cast<int>(list->size());
        list->insert(list->begin() + index, node);
        RebuildLeftTree();
    }

    void UltraCanvasMenuConfigWidget::OnAddCommand() {
        // Need a command selected in the right tree ("cmd:<id>").
        const std::string prefix = "cmd:";
        if (selectedRightId.rfind(prefix, 0) != 0) return;
        std::string id = selectedRightId.substr(prefix.size());
        if (id.empty()) return;
        InsertAtTarget(MenuLayoutNode::Command(id));
    }

    void UltraCanvasMenuConfigWidget::OnAddSeparator() {
        InsertAtTarget(MenuLayoutNode::Separator());
    }

    void UltraCanvasMenuConfigWidget::OnAddSubmenu() {
        InsertAtTarget(MenuLayoutNode::Group("New Submenu"));
    }

    void UltraCanvasMenuConfigWidget::OnRemove() {
        LeftRef ref = ParseLeftId(selectedLeftId);
        // Program root and whole menus are structural — not removable via the arrow.
        if (ref.isRoot || ref.menuIndex < 0 || ref.path.empty()) return;
        int idx = -1;
        std::vector<MenuLayoutNode>* parent = ResolveParentList(ref, idx);
        if (!parent) return;
        parent->erase(parent->begin() + idx);
        selectedLeftId.clear();
        RebuildLeftTree();
    }

    void UltraCanvasMenuConfigWidget::OnMove(int direction) {
        LeftRef ref = ParseLeftId(selectedLeftId);
        if (ref.menuIndex < 0 || ref.path.empty()) return;
        int idx = -1;
        std::vector<MenuLayoutNode>* parent = ResolveParentList(ref, idx);
        if (!parent) return;
        int target = idx + direction;
        if (target < 0 || target >= static_cast<int>(parent->size())) return;
        std::swap((*parent)[idx], (*parent)[target]);

        ref.path.back() = target;
        std::string newId = MakeLeftId(ref);
        selectedLeftId = newId;
        RebuildLeftTree();
        ReselectLeft(newId);
    }

    void UltraCanvasMenuConfigWidget::ReselectLeft(const std::string& nodeId) {
        if (!leftTree) return;
        if (TreeNode* n = leftTree->FindNode(nodeId)) {
            leftTree->SelectNode(n);
            selectedLeftId = nodeId;
        }
    }

} // namespace UltraCanvas
