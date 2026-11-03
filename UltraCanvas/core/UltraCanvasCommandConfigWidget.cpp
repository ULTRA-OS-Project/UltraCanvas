// core/UltraCanvasCommandConfigWidget.cpp
// One editor for a program's menus and toolbars.
// Version: 2.0.0 - menus and toolbars in one tree, toolbar groups with check boxes, New
//                  Group, Fold, the name field, icon size and labels, a catalog filter, a
//                  live toolbar preview, ShowCommandConfigDialog (was
//                  UltraCanvasMenuConfigWidget.cpp)
// Version: 1.0.2 - the action buttons' pane scrolls when a longer translation
//                 makes them wider than it
// Version: 1.0.1 - Add / Remove show chevron icons, not "→" / "←"; a separator
//                 row reads "————", not "────" (the bundled UI font has
//                 neither the arrows nor the box-drawing line)
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework

#include "UltraCanvasCommandConfigWidget.h"
#include "UltraCanvasUtils.h"   // Split, NormalizePath
#include "UltraCanvasConfig.h"  // GetResourcesDir
#include "UltraCanvasWindow.h"
#include "CSSLayout/CSSLayout.h"

#include <algorithm>
#include <iterator>

namespace UltraCanvas {

    ToolbarLayout* CommandLayoutSet::FindToolbar(const std::string& name) {
        for (auto& t : toolbars) if (t.name == name) return &t;
        return nullptr;
    }

    const ToolbarLayout* CommandLayoutSet::FindToolbar(const std::string& name) const {
        for (const auto& t : toolbars) if (t.name == name) return &t;
        return nullptr;
    }

    namespace {
        // The icon sizes offered, in the dropdown's order.
        const ToolbarIconSize kIconSizes[] = { ToolbarIconSize::Small, ToolbarIconSize::Medium,
                                               ToolbarIconSize::Large, ToolbarIconSize::ExtraLarge };
        const char* const kIconSizeNames[] = { "16 px", "24 px", "32 px", "48 px" };

        bool HasId(const std::vector<std::string>& ids, const std::string& id) {
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        }
    }

    UltraCanvasCommandConfigWidget::UltraCanvasCommandConfigWidget(
            const std::string& id, float x, float y, float w, float h)
            : UltraCanvasContainer(id, x, y, w, h) {
        registry = &UltraCanvasCommandRegistry::Default();
        BuildUi();
    }

    void UltraCanvasCommandConfigWidget::SetRegistry(UltraCanvasCommandRegistry* reg) {
        registry = reg ? reg : &UltraCanvasCommandRegistry::Default();
        RebuildRightTree();
        RebuildLeftTree();
        UpdatePreview();
    }

    void UltraCanvasCommandConfigWidget::SetLayoutSet(const MenuLayoutSet& set) {
        working.menus = set;
        baselineMenus = set;
        selectedLeftId.clear();
        RebuildLeftTree();
        UpdateControls();
    }

    void UltraCanvasCommandConfigWidget::AddToolbar(const ToolbarLayout& current, const ToolbarLayout& defaults,
                                                    const std::string& title) {
        working.toolbars.push_back(current);
        toolbarInfo.push_back({ title.empty() ? current.name : title, defaults });
        if (previewToolbar < 0) previewToolbar = 0;
        RebuildLeftTree();
        UpdatePreview();
        UpdateControls();
    }

    void UltraCanvasCommandConfigWidget::ClearToolbars() {
        working.toolbars.clear();
        toolbarInfo.clear();
        previewToolbar = -1;
        if (IsToolbarContext()) selectedLeftId.clear();
        RebuildLeftTree();
        UpdatePreview();
        UpdateControls();
    }

    void UltraCanvasCommandConfigWidget::SetCatalogueFilter(const std::string& toolbarName,
                                                            std::function<bool(const MenuCommand&)> accepts) {
        if (accepts) catalogueFilters[toolbarName] = std::move(accepts);
        else catalogueFilters.erase(toolbarName);
    }

// ===== UI construction =====
    void UltraCanvasCommandConfigWidget::BuildUi() {
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

        // Left pane: title, the menus-and-toolbars tree, the entry's name and
        // the toolbar's options.
        leftPane->layout.SetFlexColumn().SetFlexGap(4)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        auto leftTitle = std::make_shared<UltraCanvasLabel>(
                GetIdentifier() + "_LeftTitle", "Menus and toolbars");
        leftTitle->SetFontWeight(FontWeight::Bold);
        leftPane->AddChild(leftTitle);

        leftTree = std::make_shared<UltraCanvasTreeView>(GetIdentifier() + "_LeftTree");
        leftTree->SetSelectionMode(TreeSelectionMode::Single);
        leftTree->SetRowHeight(22);
        // A toolbar group's box and its commands' boxes are separate things:
        // a hidden group keeps which of its commands are shown.
        leftTree->SetShowCheckboxes(true);
        leftTree->SetCheckPropagation(false);
        leftTree->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        leftTree->onNodeSelected = [this](TreeNode* n) {
            selectedLeftId = n ? n->data.nodeId : std::string();
            int t = -1, g = -1, c = -1;
            if (ParseToolbarId(selectedLeftId, t, g, c) && t != previewToolbar) {
                previewToolbar = t;
                UpdatePreview();
            }
            UpdateControls();
        };
        leftTree->onNodeCheckChanged = [this](TreeNode* n, TreeCheckState state) {
            if (rebuilding || !n) return;
            SetLayoutNodeChecked(n->data.nodeId, state == TreeCheckState::Checked);
        };
        leftPane->AddChild(leftTree);

        auto row = [&](const std::string& suffix) {
            auto r = std::make_shared<UltraCanvasContainer>(GetIdentifier() + "_" + suffix);
            r->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
            r->layoutItem.SetFlexShrink(0);
            leftPane->AddChild(r);
            return r;
        };
        auto nameRow = row("NameRow");
        nameRow->AddChild(std::make_shared<UltraCanvasLabel>(GetIdentifier() + "_NameCaption", "Name"));
        nameField = std::make_shared<UltraCanvasTextInput>(GetIdentifier() + "_Name", 0, 0, 160, 26);
        nameField->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        nameField->onTextChanged = [this](const std::string& text) {
            if (!rebuilding) RenameSelected(text);
        };
        nameRow->AddChild(nameField);

        auto optionsRow = row("ToolbarRow");
        optionsRow->AddChild(std::make_shared<UltraCanvasLabel>(GetIdentifier() + "_IconCaption", "Icon size"));
        iconSizeList = std::make_shared<UltraCanvasDropdown>(GetIdentifier() + "_IconSize", 0, 0, 90, 26);
        for (const char* name : kIconSizeNames) iconSizeList->AddItem(name);
        iconSizeList->SetSelectedIndex(1, false);
        iconSizeList->onSelectionChanged = [this](int index, const DropdownItem&) {
            if (rebuilding || index < 0 || index >= static_cast<int>(std::size(kIconSizes))) return;
            SetSelectedToolbarIconSize(kIconSizes[index]);
        };
        optionsRow->AddChild(iconSizeList);
        labelsBox = std::make_shared<UltraCanvasCheckbox>(GetIdentifier() + "_Labels", "Labels");
        labelsBox->onStateChanged = [this](CheckedState, CheckedState now) {
            if (!rebuilding) SetSelectedToolbarShowLabels(now == CheckedState::Checked);
        };
        optionsRow->AddChild(labelsBox);

        // Middle pane: action buttons. Each is as wide as its text, which a
        // translation can make wider than the pane: the pane then scrolls
        // sideways (and can be dragged wider) rather than cutting them.
        {
            ContainerStyle midStyle = midPane->GetContainerStyle();
            midStyle.autoShowScrollbars = true;
            midPane->SetContainerStyle(midStyle);
        }
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
        // The direction is a chevron icon drawn in the text colour.
        auto setChevron = [](UltraCanvasButton& btn, const char* file, ButtonIconPosition position) {
            btn.SetIcon(NormalizePath(GetResourcesDir() + "media/icons/menu/" + file));
            btn.SetIconSize(14, 14);
            btn.SetIconPosition(position);
            btn.SetUseIconAsMask(true);
            btn.SetIconMaskColor(Colors::Transparent);   // follow the text colour
        };
        addButton = makeMidButton("Add", "Add", [this]() { AddSelectedCommand(); });
        setChevron(*addButton, "chevron-right.svg", ButtonIconPosition::Right);
        removeButton = makeMidButton("Remove", "Remove", [this]() { RemoveSelected(); });
        setChevron(*removeButton, "chevron-left.svg", ButtonIconPosition::Left);
        upButton = makeMidButton("Up", "Move Up", [this]() { MoveSelected(-1); });
        downButton = makeMidButton("Down", "Move Down", [this]() { MoveSelected(+1); });
        groupButton = makeMidButton("Group", "New Group", [this]() { AddGroup(); });
        separatorButton = makeMidButton("Sep", "Separator", [this]() { AddSeparator(); });
        foldButton = makeMidButton("Fold", "Fold", [this]() { ToggleFoldSelected(); });
        addButton->SetTooltip("Put the command selected on the right after the entry selected on the left");
        removeButton->SetTooltip("Take the selected entry out (a toolbar keeps a program's command, unchecked)");
        groupButton->SetTooltip("A submenu in a menu, a group of your own on a toolbar");
        separatorButton->SetTooltip("A line between two menu entries");
        foldButton->SetTooltip("Show the toolbar group as one button with the others folding out");

        // Right pane: title, filter and the registry's catalog.
        rightPane->layout.SetFlexColumn().SetFlexGap(4)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        auto rightTitle = std::make_shared<UltraCanvasLabel>(
                GetIdentifier() + "_RightTitle", "Available commands");
        rightTitle->SetFontWeight(FontWeight::Bold);
        rightPane->AddChild(rightTitle);
        auto filter = std::make_shared<UltraCanvasTextInput>(GetIdentifier() + "_Filter", 0, 0, 160, 26);
        filter->SetPlaceholder("Filter commands");
        filter->layoutItem.SetFlexShrink(0);
        filter->onTextChanged = [this](const std::string& text) {
            if (rightTree) rightTree->SetFilterText(text);
        };
        rightPane->AddChild(filter);

        rightTree = std::make_shared<UltraCanvasTreeView>(GetIdentifier() + "_RightTree");
        rightTree->SetSelectionMode(TreeSelectionMode::Single);
        rightTree->SetRowHeight(22);
        rightTree->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        rightTree->onNodeSelected = [this](TreeNode* n) {
            selectedRightId = n ? n->data.nodeId : std::string();
            UpdateControls();
        };
        rightTree->onNodeDoubleClicked = [this](TreeNode* n) {
            if (n) { selectedRightId = n->data.nodeId; AddSelectedCommand(); }
        };
        rightPane->AddChild(rightTree);

        AddChild(splitPane);

        // ----- the selected toolbar as the edits leave it -----
        previewCaption = std::make_shared<UltraCanvasLabel>(GetIdentifier() + "_PreviewCaption", "Preview");
        previewCaption->SetFontWeight(FontWeight::Bold);
        previewCaption->layoutItem.SetFlexShrink(0);
        AddChild(previewCaption);
        preview = std::make_shared<UltraCanvasToolbar>(GetIdentifier() + "_Preview", 0, 0, 0, 38);
        preview->SetOverflowMode(ToolbarOverflowMode::Menu);
        preview->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        AddChild(preview);

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
        auto reset = makeFooterButton("Reset", "Reset", [this]() {
            working.menus = baselineMenus;
            for (size_t i = 0; i < working.toolbars.size() && i < toolbarInfo.size(); ++i) {
                working.toolbars[i] = toolbarInfo[i].defaults;
            }
            selectedLeftId.clear();
            RebuildLeftTree();
            UpdatePreview();
            UpdateControls();
            if (onReset) onReset();
        });
        reset->SetTooltip("The menus as they were opened, the toolbars as the program has them");
        footer->AddStretchSpacer(1);
        makeFooterButton("Cancel", "Cancel", [this]() { if (onCancel) onCancel(); });
        makeFooterButton("Apply",  "Apply",  [this]() { if (onApply) onApply(working); });
        makeFooterButton("Save",   "Save",   [this]() { if (onSave)  onSave(working); });

        AddChild(footer);

        RebuildRightTree();
        RebuildLeftTree();
        UpdatePreview();
        UpdateControls();
    }

// ===== left tree =====
    std::string UltraCanvasCommandConfigWidget::MenuNodeText(const MenuLayoutNode& node) const {
        switch (node.kind) {
            case MenuLayoutNode::Kind::Separator:
                return "\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94"; // ———— (em dashes)
            case MenuLayoutNode::Kind::Group:
                return node.label.empty() ? "(submenu)" : node.label;
            case MenuLayoutNode::Kind::Command:
            default:
                if (!node.label.empty()) return node.label;
                return CommandText(node.commandId);
        }
    }

    std::string UltraCanvasCommandConfigWidget::MenuNodeIcon(const MenuLayoutNode& node) const {
        if (node.kind == MenuLayoutNode::Kind::Group) return node.iconPath;
        if (node.kind == MenuLayoutNode::Kind::Command) return CommandIcon(node.commandId);
        return std::string();
    }

    std::string UltraCanvasCommandConfigWidget::CommandText(const std::string& commandId) const {
        const MenuCommand* c = registry ? registry->Find(commandId) : nullptr;
        if (c && !c->label.empty()) return c->label;
        return "<" + commandId + ">";
    }

    std::string UltraCanvasCommandConfigWidget::CommandIcon(const std::string& commandId) const {
        const MenuCommand* c = registry ? registry->Find(commandId) : nullptr;
        return c ? registry->ResolveIcon(*c, 16) : std::string();
    }

    std::string UltraCanvasCommandConfigWidget::GroupText(const ToolbarLayoutGroup& group) const {
        std::string text = group.label.empty() ? group.id : group.label;
        if (group.folded) text += " - folded";
        return text;
    }

    void UltraCanvasCommandConfigWidget::BuildMenuNode(const std::string& parentId,
                                                       const MenuLayoutNode& node,
                                                       const std::string& nodeId) {
        TreeNodeData data(nodeId, MenuNodeText(node));
        std::string icon = MenuNodeIcon(node);
        if (!icon.empty()) data.leftIcon = TreeNodeIcon(icon, 16, 16);
        data.showCheckbox = false;
        leftTree->AddNode(parentId, data);

        for (size_t k = 0; k < node.children.size(); ++k) {
            BuildMenuNode(nodeId, node.children[k], nodeId + "." + std::to_string(k));
        }
    }

    void UltraCanvasCommandConfigWidget::BuildToolbarNodes(int t) {
        const ToolbarLayout& bar = working.toolbars[static_cast<size_t>(t)];
        TreeNodeData td(ToolbarId(t), toolbarInfo[static_cast<size_t>(t)].title);
        td.showCheckbox = false;
        leftTree->AddNode("toolbars", td);
        for (size_t g = 0; g < bar.groups.size(); ++g) {
            const ToolbarLayoutGroup& group = bar.groups[g];
            TreeNodeData gd(ToolbarId(t, static_cast<int>(g)), GroupText(group));
            gd.checkState = group.visible ? TreeCheckState::Checked : TreeCheckState::Unchecked;
            leftTree->AddNode(td.nodeId, gd);
            for (size_t k = 0; k < group.commands.size(); ++k) {
                const std::string& id = group.commands[k];
                TreeNodeData cd(ToolbarId(t, static_cast<int>(g), static_cast<int>(k)), CommandText(id));
                const std::string icon = CommandIcon(id);
                if (!icon.empty()) cd.leftIcon = TreeNodeIcon(icon, 16, 16);
                cd.checkState = group.IsCommandShown(id) ? TreeCheckState::Checked : TreeCheckState::Unchecked;
                leftTree->AddNode(gd.nodeId, cd);
            }
        }
    }

    void UltraCanvasCommandConfigWidget::RebuildLeftTree() {
        if (!leftTree) return;
        rebuilding = true;
        leftTree->ClearSelection();

        TreeNodeData rootData("root", "Program");
        rootData.showCheckbox = false;
        leftTree->SetRootNode(rootData);

        if (!working.menus.menus.empty()) {
            TreeNodeData heading("menus", "Menus");
            heading.showCheckbox = false;
            leftTree->AddNode("root", heading);
            for (size_t i = 0; i < working.menus.menus.size(); ++i) {
                std::string mid = "m" + std::to_string(i);
                const MenuLayout& menu = working.menus.menus[i];
                TreeNodeData md(mid, menu.name.empty() ? "(menu)" : menu.name);
                md.showCheckbox = false;
                leftTree->AddNode("menus", md);
                for (size_t j = 0; j < menu.topLevel.size(); ++j) {
                    BuildMenuNode(mid, menu.topLevel[j], mid + "." + std::to_string(j));
                }
            }
        }
        if (!working.toolbars.empty()) {
            TreeNodeData heading("toolbars", "Toolbars");
            heading.showCheckbox = false;
            leftTree->AddNode("root", heading);
            for (size_t t = 0; t < working.toolbars.size(); ++t) BuildToolbarNodes(static_cast<int>(t));
        }

        if (TreeNode* root = leftTree->GetRootNode()) root->Expand();
        leftTree->ExpandAll();
        rebuilding = false;
    }

// ===== right tree =====
    void UltraCanvasCommandConfigWidget::RebuildRightTree() {
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
                    const std::string icon = registry->ResolveIcon(*cmd, 16);
                    if (!icon.empty()) d.leftIcon = TreeNodeIcon(icon, 16, 16);
                    rightTree->AddNode(cid, d);
                }
            }
        }

        if (TreeNode* root = rightTree->GetRootNode()) root->Expand();
        rightTree->ExpandAll();
    }

// ===== the controls under the tree, the buttons, the preview =====
    void UltraCanvasCommandConfigWidget::UpdateControls() {
        rebuilding = true;
        int t = -1, g = -1, c = -1;
        const bool onToolbar = ParseToolbarId(selectedLeftId, t, g, c);
        const LeftRef ref = ParseLeftId(selectedLeftId);
        const bool onMenu = !onToolbar && ref.menuIndex >= 0;
        const bool commandPicked = selectedRightId.rfind("cmd:", 0) == 0;

        // The name: a menu entry or submenu, or a toolbar group of the user's.
        bool nameable = false;
        std::string name;
        if (onMenu) {
            const LeftRef r = ParseLeftId(selectedLeftId);
            if (MenuLayoutNode* node = ResolveNode(r); node && !node->IsSeparator()) {
                nameable = true;
                name = MenuNodeText(*node);
            }
        } else if (onToolbar && g >= 0 && c < 0) {
            const ToolbarLayoutGroup& group = working.toolbars[static_cast<size_t>(t)].groups[static_cast<size_t>(g)];
            nameable = group.userMade;
            name = group.label;
        }
        if (nameField) {
            if (nameField->GetText() != name) nameField->SetText(name);
            nameField->SetDisabled(!nameable);
        }

        const bool toolbarSelected = onToolbar && t >= 0;
        if (iconSizeList && labelsBox) {
            iconSizeList->SetDisabled(!toolbarSelected);
            labelsBox->SetDisabled(!toolbarSelected);
            if (toolbarSelected) {
                const ToolbarLayout& bar = working.toolbars[static_cast<size_t>(t)];
                for (size_t i = 0; i < std::size(kIconSizes); ++i) {
                    if (kIconSizes[i] == bar.iconSize) iconSizeList->SetSelectedIndex(static_cast<int>(i), false);
                }
                labelsBox->SetChecked(bar.showLabels);
            }
        }

        if (addButton) addButton->SetDisabled(!commandPicked || (!onMenu && !onToolbar && working.menus.menus.empty()));
        if (removeButton) removeButton->SetDisabled(!(onMenu && !ref.path.empty()) && !(onToolbar && g >= 0));
        const bool movable = (onMenu && !ref.path.empty()) || (onToolbar && g >= 0);
        if (upButton) upButton->SetDisabled(!movable);
        if (downButton) downButton->SetDisabled(!movable);
        if (groupButton) groupButton->SetDisabled(!onMenu && !onToolbar && working.menus.menus.empty());
        if (separatorButton) separatorButton->SetDisabled(onToolbar || (!onMenu && working.menus.menus.empty()));
        if (foldButton) {
            foldButton->SetDisabled(!(onToolbar && g >= 0 && c < 0));
            bool folded = false;
            if (onToolbar && g >= 0) folded = working.toolbars[static_cast<size_t>(t)].groups[static_cast<size_t>(g)].folded;
            foldButton->SetText(folded ? "Unfold" : "Fold");
        }
        rebuilding = false;
    }

    void UltraCanvasCommandConfigWidget::UpdatePreview() {
        if (!preview || !previewCaption) return;
        const bool show = registry && previewToolbar >= 0 &&
                          previewToolbar < static_cast<int>(working.toolbars.size());
        preview->SetVisible(show);
        previewCaption->SetVisible(show);
        if (!show) {
            preview->ClearItems();
            return;
        }
        MaterializeToolbar(*preview, working.toolbars[static_cast<size_t>(previewToolbar)], *registry);
        previewCaption->SetText("Preview: " + toolbarInfo[static_cast<size_t>(previewToolbar)].title);
        // A picture of the toolbar: its buttons run nothing from in here.
        for (const auto& item : preview->GetItems()) {
            if (auto group = std::dynamic_pointer_cast<UltraCanvasToolGroupButton>(item)) {
                group->onItemSelected = nullptr;
            } else if (auto button = std::dynamic_pointer_cast<UltraCanvasButton>(item)) {
                button->onClick = nullptr;
                button->onToggle = nullptr;
            } else if (!std::dynamic_pointer_cast<UltraCanvasSeparator>(item)) {
                item->SetDisabled(true);
            }
        }
    }

    void UltraCanvasCommandConfigWidget::Changed(const std::string& selectId) {
        RebuildLeftTree();
        if (!selectId.empty()) SelectLayoutNode(selectId);
        else selectedLeftId.clear();
        UpdatePreview();
        UpdateControls();
    }

// ===== selection =====
    bool UltraCanvasCommandConfigWidget::SelectLayoutNode(const std::string& nodeId) {
        if (!leftTree) return false;
        TreeNode* n = leftTree->FindNode(nodeId);
        if (!n) return false;
        leftTree->SelectNode(n);   // onNodeSelected records it
        selectedLeftId = nodeId;
        return true;
    }

    bool UltraCanvasCommandConfigWidget::SelectCatalogueCommand(const std::string& commandId) {
        if (!rightTree) return false;
        TreeNode* n = rightTree->FindNode("cmd:" + commandId);
        if (!n) return false;
        rightTree->SelectNode(n);
        selectedRightId = "cmd:" + commandId;
        UpdateControls();
        return true;
    }

// ===== menu addressing =====
    UltraCanvasCommandConfigWidget::LeftRef
    UltraCanvasCommandConfigWidget::ParseLeftId(const std::string& nodeId) const {
        LeftRef ref;
        if (nodeId.empty() || nodeId == "root" || nodeId == "menus") {
            ref.isRoot = true;
            return ref;
        }
        if (nodeId[0] != 'm') return ref; // not a menu id
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

    std::string UltraCanvasCommandConfigWidget::MakeLeftId(const LeftRef& ref) const {
        if (ref.isRoot || ref.menuIndex < 0) return "root";
        std::string id = "m" + std::to_string(ref.menuIndex);
        for (int idx : ref.path) id += "." + std::to_string(idx);
        return id;
    }

    MenuLayoutNode* UltraCanvasCommandConfigWidget::ResolveNode(const LeftRef& ref) {
        if (ref.menuIndex < 0 || ref.menuIndex >= static_cast<int>(working.menus.menus.size())) {
            return nullptr;
        }
        if (ref.path.empty()) return nullptr; // the menu itself, not a node
        std::vector<MenuLayoutNode>* list = &working.menus.menus[ref.menuIndex].topLevel;
        MenuLayoutNode* node = nullptr;
        for (int idx : ref.path) {
            if (idx < 0 || idx >= static_cast<int>(list->size())) return nullptr;
            node = &(*list)[idx];
            list = &node->children;
        }
        return node;
    }

    std::vector<MenuLayoutNode>*
    UltraCanvasCommandConfigWidget::ResolveParentList(const LeftRef& ref, int& indexInParent) {
        indexInParent = -1;
        if (ref.menuIndex < 0 || ref.menuIndex >= static_cast<int>(working.menus.menus.size())) {
            return nullptr;
        }
        if (ref.path.empty()) return nullptr;
        std::vector<MenuLayoutNode>* list = &working.menus.menus[ref.menuIndex].topLevel;
        for (size_t k = 0; k + 1 < ref.path.size(); ++k) {
            int idx = ref.path[k];
            if (idx < 0 || idx >= static_cast<int>(list->size())) return nullptr;
            list = &(*list)[idx].children;
        }
        indexInParent = ref.path.back();
        if (indexInParent < 0 || indexInParent >= static_cast<int>(list->size())) return nullptr;
        return list;
    }

    void UltraCanvasCommandConfigWidget::ComputeInsertTarget(
            std::vector<MenuLayoutNode>*& outList, int& outIndex) {
        outList = nullptr;
        outIndex = 0;

        LeftRef ref = ParseLeftId(selectedLeftId);

        // Nothing / Program root selected -> append to first menu.
        if (ref.isRoot || ref.menuIndex < 0) {
            if (!working.menus.menus.empty()) {
                outList = &working.menus.menus[0].topLevel;
                outIndex = static_cast<int>(outList->size());
            }
            return;
        }

        // A whole menu selected -> append to its top level.
        if (ref.path.empty()) {
            outList = &working.menus.menus[ref.menuIndex].topLevel;
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
        outList = &working.menus.menus[ref.menuIndex].topLevel;
        outIndex = static_cast<int>(outList->size());
    }

    std::string UltraCanvasCommandConfigWidget::InsertMenuNode(const MenuLayoutNode& node) {
        std::vector<MenuLayoutNode>* list = nullptr;
        int index = 0;
        ComputeInsertTarget(list, index);
        if (!list) return std::string();
        if (index < 0) index = 0;
        if (index > static_cast<int>(list->size())) index = static_cast<int>(list->size());
        list->insert(list->begin() + index, node);
        // The id of the entry just put in: the list's owner's id and the index.
        LeftRef ref = ParseLeftId(selectedLeftId);
        if (ref.isRoot || ref.menuIndex < 0) {
            ref = LeftRef();
            ref.menuIndex = 0;
            ref.path = { index };
        } else if (ref.path.empty()) {
            ref.path = { index };
        } else if (MenuLayoutNode* parent = ResolveNode(ref); parent && parent->IsGroup() &&
                   &parent->children == list) {
            ref.path.push_back(index);
        } else {
            ref.path.back() = index;
        }
        return MakeLeftId(ref);
    }

// ===== toolbar addressing =====
    bool UltraCanvasCommandConfigWidget::ParseToolbarId(const std::string& nodeId, int& toolbar,
                                                        int& group, int& command) {
        toolbar = group = command = -1;
        if (nodeId.size() < 2 || nodeId[0] != 't' || nodeId == "toolbars") return false;
        std::vector<std::string> parts = Split(nodeId.substr(1), '.');
        int* out[] = { &toolbar, &group, &command };
        if (parts.empty() || parts.size() > 3) return false;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (parts[i].empty() || !std::all_of(parts[i].begin(), parts[i].end(),
                                                 [](char ch) { return ch >= '0' && ch <= '9'; })) return false;
            *out[i] = std::stoi(parts[i]);
        }
        return true;
    }

    std::string UltraCanvasCommandConfigWidget::ToolbarId(int toolbar, int group, int command) {
        std::string id = "t" + std::to_string(toolbar);
        if (group >= 0) id += "." + std::to_string(group);
        if (group >= 0 && command >= 0) id += "." + std::to_string(command);
        return id;
    }

    bool UltraCanvasCommandConfigWidget::IsToolbarContext() const {
        int t, g, c;
        return ParseToolbarId(selectedLeftId, t, g, c) || selectedLeftId == "toolbars";
    }

    const ToolbarLayoutGroup* UltraCanvasCommandConfigWidget::DefaultGroupOf(int toolbar, const std::string& groupId) const {
        if (toolbar < 0 || toolbar >= static_cast<int>(toolbarInfo.size())) return nullptr;
        const ToolbarLayoutGroup* g = toolbarInfo[static_cast<size_t>(toolbar)].defaults.FindGroup(groupId);
        return (g && !g->userMade) ? g : nullptr;
    }

    std::string UltraCanvasCommandConfigWidget::UniqueGroupId(const ToolbarLayout& layout) const {
        for (int n = 1;; ++n) {
            std::string id = "user-" + std::to_string(n);
            if (!layout.FindGroup(id)) return id;
        }
    }

// ===== actions =====
    void UltraCanvasCommandConfigWidget::AddSelectedCommand() {
        const std::string prefix = "cmd:";
        if (selectedRightId.rfind(prefix, 0) != 0) return;
        const std::string id = selectedRightId.substr(prefix.size());
        if (id.empty()) return;

        int t = -1, g = -1, c = -1;
        if (ParseToolbarId(selectedLeftId, t, g, c) && t < static_cast<int>(working.toolbars.size())) {
            ToolbarLayout& bar = working.toolbars[static_cast<size_t>(t)];
            if (auto f = catalogueFilters.find(bar.name); f != catalogueFilters.end()) {
                const MenuCommand* cmd = registry ? registry->Find(id) : nullptr;
                if (!cmd || !f->second(*cmd)) return;   // not a command this toolbar takes
            }
            int insertAt = -1;
            if (g < 0) {
                // The toolbar itself: its last group, or a first group of the user's.
                if (bar.groups.empty()) {
                    ToolbarLayoutGroup own = ToolbarLayoutGroup::Make(UniqueGroupId(bar), "My commands", {});
                    own.userMade = true;
                    bar.groups.push_back(own);
                }
                g = static_cast<int>(bar.groups.size()) - 1;
            } else if (c >= 0) {
                insertAt = c + 1;
            }
            ToolbarLayoutGroup& group = bar.groups[static_cast<size_t>(g)];
            auto existing = std::find(group.commands.begin(), group.commands.end(), id);
            int at;
            if (existing != group.commands.end()) {
                group.SetCommandShown(id, true);   // already there: shown again, not doubled
                at = static_cast<int>(existing - group.commands.begin());
            } else {
                if (insertAt < 0 || insertAt > static_cast<int>(group.commands.size()))
                    insertAt = static_cast<int>(group.commands.size());
                group.commands.insert(group.commands.begin() + insertAt, id);
                at = insertAt;
            }
            Changed(ToolbarId(t, g, at));
            return;
        }
        if (working.menus.menus.empty()) return;
        Changed(InsertMenuNode(MenuLayoutNode::Command(id)));
    }

    void UltraCanvasCommandConfigWidget::RemoveSelected() {
        int t = -1, g = -1, c = -1;
        if (ParseToolbarId(selectedLeftId, t, g, c)) {
            if (g < 0 || t >= static_cast<int>(working.toolbars.size())) return;
            ToolbarLayout& bar = working.toolbars[static_cast<size_t>(t)];
            if (g >= static_cast<int>(bar.groups.size())) return;
            ToolbarLayoutGroup& group = bar.groups[static_cast<size_t>(g)];
            const ToolbarLayoutGroup* programGroup = DefaultGroupOf(t, group.id);
            if (c < 0) {
                // The program's group is hidden (it is the program's to drop);
                // the user's own goes.
                if (programGroup) {
                    group.visible = false;
                    Changed(ToolbarId(t, g));
                } else {
                    bar.groups.erase(bar.groups.begin() + g);
                    Changed(bar.groups.empty() ? ToolbarId(t) : ToolbarId(t, std::min(g, static_cast<int>(bar.groups.size()) - 1)));
                }
                return;
            }
            if (c >= static_cast<int>(group.commands.size())) return;
            const std::string id = group.commands[static_cast<size_t>(c)];
            if (programGroup && HasId(programGroup->commands, id)) {
                // The program's command stays in its group, unchecked, and can
                // be checked again.
                group.SetCommandShown(id, false);
                Changed(ToolbarId(t, g, c));
            } else {
                group.commands.erase(group.commands.begin() + c);
                group.hidden.erase(std::remove(group.hidden.begin(), group.hidden.end(), id), group.hidden.end());
                Changed(group.commands.empty() ? ToolbarId(t, g)
                                               : ToolbarId(t, g, std::min(c, static_cast<int>(group.commands.size()) - 1)));
            }
            return;
        }

        LeftRef ref = ParseLeftId(selectedLeftId);
        // Program root and whole menus are structural — not removable.
        if (ref.isRoot || ref.menuIndex < 0 || ref.path.empty()) return;
        int idx = -1;
        std::vector<MenuLayoutNode>* parent = ResolveParentList(ref, idx);
        if (!parent) return;
        parent->erase(parent->begin() + idx);
        Changed(std::string());
    }

    void UltraCanvasCommandConfigWidget::MoveSelected(int direction) {
        int t = -1, g = -1, c = -1;
        if (ParseToolbarId(selectedLeftId, t, g, c)) {
            if (g < 0 || t >= static_cast<int>(working.toolbars.size())) return;
            ToolbarLayout& bar = working.toolbars[static_cast<size_t>(t)];
            if (g >= static_cast<int>(bar.groups.size())) return;
            if (c < 0) {
                const int target = g + direction;
                if (target < 0 || target >= static_cast<int>(bar.groups.size())) return;
                std::swap(bar.groups[static_cast<size_t>(g)], bar.groups[static_cast<size_t>(target)]);
                Changed(ToolbarId(t, target));
            } else {
                auto& commands = bar.groups[static_cast<size_t>(g)].commands;
                const int target = c + direction;
                if (target < 0 || target >= static_cast<int>(commands.size())) return;
                std::swap(commands[static_cast<size_t>(c)], commands[static_cast<size_t>(target)]);
                Changed(ToolbarId(t, g, target));
            }
            return;
        }

        LeftRef ref = ParseLeftId(selectedLeftId);
        if (ref.menuIndex < 0 || ref.path.empty()) return;
        int idx = -1;
        std::vector<MenuLayoutNode>* parent = ResolveParentList(ref, idx);
        if (!parent) return;
        int target = idx + direction;
        if (target < 0 || target >= static_cast<int>(parent->size())) return;
        std::swap((*parent)[idx], (*parent)[target]);
        ref.path.back() = target;
        Changed(MakeLeftId(ref));
    }

    void UltraCanvasCommandConfigWidget::AddGroup() {
        int t = -1, g = -1, c = -1;
        if (ParseToolbarId(selectedLeftId, t, g, c)) {
            if (t >= static_cast<int>(working.toolbars.size())) return;
            ToolbarLayout& bar = working.toolbars[static_cast<size_t>(t)];
            ToolbarLayoutGroup own = ToolbarLayoutGroup::Make(UniqueGroupId(bar), "New group", {});
            own.userMade = true;
            const int at = (g >= 0) ? g + 1 : static_cast<int>(bar.groups.size());
            bar.groups.insert(bar.groups.begin() + at, own);
            Changed(ToolbarId(t, at));
            return;
        }
        if (working.menus.menus.empty()) return;
        Changed(InsertMenuNode(MenuLayoutNode::Group("New Submenu")));
    }

    void UltraCanvasCommandConfigWidget::AddSeparator() {
        if (IsToolbarContext() || working.menus.menus.empty()) return;
        Changed(InsertMenuNode(MenuLayoutNode::Separator()));
    }

    void UltraCanvasCommandConfigWidget::ToggleFoldSelected() {
        int t = -1, g = -1, c = -1;
        if (!ParseToolbarId(selectedLeftId, t, g, c) || g < 0 || c >= 0) return;
        if (t >= static_cast<int>(working.toolbars.size())) return;
        ToolbarLayout& bar = working.toolbars[static_cast<size_t>(t)];
        if (g >= static_cast<int>(bar.groups.size())) return;
        bar.groups[static_cast<size_t>(g)].folded = !bar.groups[static_cast<size_t>(g)].folded;
        Changed(ToolbarId(t, g));
    }

    bool UltraCanvasCommandConfigWidget::RenameSelected(const std::string& name) {
        int t = -1, g = -1, c = -1;
        if (ParseToolbarId(selectedLeftId, t, g, c)) {
            if (g < 0 || c >= 0 || t >= static_cast<int>(working.toolbars.size())) return false;
            ToolbarLayoutGroup& group = working.toolbars[static_cast<size_t>(t)].groups[static_cast<size_t>(g)];
            if (!group.userMade) return false;
            group.label = name;
            if (TreeNode* n = leftTree->FindNode(selectedLeftId)) {
                n->data.text = GroupText(group);
                leftTree->RequestRedraw();
            }
            return true;
        }
        LeftRef ref = ParseLeftId(selectedLeftId);
        MenuLayoutNode* node = ResolveNode(ref);
        if (!node || node->IsSeparator()) return false;
        node->label = name;   // a submenu's label, or a command's rename ("" = the command's own)
        if (TreeNode* n = leftTree->FindNode(selectedLeftId)) {
            n->data.text = MenuNodeText(*node);
            leftTree->RequestRedraw();
        }
        return true;
    }

    bool UltraCanvasCommandConfigWidget::SetLayoutNodeChecked(const std::string& nodeId, bool checked) {
        int t = -1, g = -1, c = -1;
        if (!ParseToolbarId(nodeId, t, g, c) || g < 0) return false;
        if (t >= static_cast<int>(working.toolbars.size())) return false;
        ToolbarLayout& bar = working.toolbars[static_cast<size_t>(t)];
        if (g >= static_cast<int>(bar.groups.size())) return false;
        ToolbarLayoutGroup& group = bar.groups[static_cast<size_t>(g)];
        if (c < 0) {
            group.visible = checked;
        } else {
            if (c >= static_cast<int>(group.commands.size())) return false;
            group.SetCommandShown(group.commands[static_cast<size_t>(c)], checked);
        }
        // The box itself follows when this came from code rather than a click.
        if (TreeNode* n = leftTree ? leftTree->FindNode(nodeId) : nullptr) {
            const TreeCheckState want = checked ? TreeCheckState::Checked : TreeCheckState::Unchecked;
            if (n->data.checkState != want) {
                rebuilding = true;
                leftTree->SetNodeCheckState(n, want);
                rebuilding = false;
            }
        }
        previewToolbar = t;
        UpdatePreview();
        return true;
    }

    void UltraCanvasCommandConfigWidget::SetSelectedToolbarIconSize(ToolbarIconSize size) {
        int t = -1, g = -1, c = -1;
        if (!ParseToolbarId(selectedLeftId, t, g, c) || t >= static_cast<int>(working.toolbars.size())) return;
        working.toolbars[static_cast<size_t>(t)].iconSize = size;
        UpdatePreview();
        UpdateControls();
    }

    void UltraCanvasCommandConfigWidget::SetSelectedToolbarShowLabels(bool show) {
        int t = -1, g = -1, c = -1;
        if (!ParseToolbarId(selectedLeftId, t, g, c) || t >= static_cast<int>(working.toolbars.size())) return;
        working.toolbars[static_cast<size_t>(t)].showLabels = show;
        UpdatePreview();
        UpdateControls();
    }

// ===== in a window =====
    std::shared_ptr<UltraCanvasWindowBase> ShowCommandConfigDialog(
            UltraCanvasWindowBase* parent, std::shared_ptr<UltraCanvasCommandConfigWidget> widget,
            const std::string& title, int width, int height) {
        if (!widget) return nullptr;
        WindowConfig wc;
        wc.title = title;
        wc.width = width;
        wc.height = height;
        wc.resizable = true;
        wc.type = WindowType::Dialog;
        wc.parentWindow = parent;
        auto window = CreateWindow(wc);
        if (!window || !window->IsCreated()) return nullptr;
        window->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        widget->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        window->AddChild(widget);

        // Cancel and Save end the editing: the host's callback, then the
        // window closes. The window is above the widget, so it is held raw.
        UltraCanvasWindow* win = window.get();
        auto hostCancel = widget->onCancel;
        widget->onCancel = [hostCancel, win]() {
            if (hostCancel) hostCancel();
            win->Close();
        };
        auto hostSave = widget->onSave;
        widget->onSave = [hostSave, win](const CommandLayoutSet& set) {
            if (hostSave) hostSave(set);
            win->Close();
        };
        window->Show();
        return window;
    }

} // namespace UltraCanvas
