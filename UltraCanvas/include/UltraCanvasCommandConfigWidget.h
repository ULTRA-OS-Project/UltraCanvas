// include/UltraCanvasCommandConfigWidget.h
// One editor for a program's menus and toolbars.
//
// Both are built from the same command catalog (UltraCanvasCommandRegistry),
// so they are edited in one place:
//   * left pane  : one tree, "Program" at its root, holding the program's menus
//                  (menubar, context menus: submenus, commands, separators) and
//                  its toolbars (groups of commands). A toolbar's group and
//                  each of its commands carry a check box: shown or not. Under
//                  the tree, the selected entry's name and the selected
//                  toolbar's icon size and labels.
//   * middle     : Add, Remove, Move Up / Down, New Group (a submenu in a
//                  menu, a group of the user's on a toolbar), Separator (menus)
//                  and Fold (a toolbar group into one slot with a fold-out bar).
//   * right pane : the catalog of commands, by category, with a filter field.
//   * below      : a live preview of the selected toolbar; Reset, Cancel,
//                  Apply, Save.
//
// It edits a working copy (CommandLayoutSet) and never touches the live menus
// and toolbars itself: it says what the user did through callbacks, and the
// host applies (MaterializeInto, MaterializeToolbar) and saves
// (SaveMenuLayoutSet, SaveToolbarLayout). ShowCommandConfigDialog opens it in a
// window of its own.
//
// It was UltraCanvasMenuConfigWidget, which edited menus only; that name is an
// alias (UltraCanvasMenuConfigWidget.h).
//
// Version: 2.0.0 - one editor for menus and toolbars: toolbar groups with check boxes,
//                  New Group, Fold, the name field, icon size and labels, a catalog
//                  filter, a live toolbar preview, ShowCommandConfigDialog; the editing
//                  actions are public; onApply / onSave carry a CommandLayoutSet
// Version: 1.0.2 - the action buttons' pane scrolls when a longer translation makes
//                  them wider than it
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasCommandRegistry.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMenuLayout.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasToolbarLayout.h"
#include "UltraCanvasTreeView.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasWindowBase;

// ===== WHAT THE EDITOR EDITS =====
    struct CommandLayoutSet {
        MenuLayoutSet menus;                  // the program's menus
        std::vector<ToolbarLayout> toolbars;  // its toolbars, by ToolbarLayout::name

        ToolbarLayout* FindToolbar(const std::string& name);
        const ToolbarLayout* FindToolbar(const std::string& name) const;
    };

    class UltraCanvasCommandConfigWidget : public UltraCanvasContainer {
    public:
        UltraCanvasCommandConfigWidget(const std::string& id, float x, float y, float w, float h);
        explicit UltraCanvasCommandConfigWidget(const std::string& id)
                : UltraCanvasCommandConfigWidget(id, -1, -1, -1, -1) {}

        // ===== WHAT IS EDITED =====
        // The catalog the commands come from; the process-wide Default() until
        // set. It must outlive the widget.
        void SetRegistry(UltraCanvasCommandRegistry* registry);
        UltraCanvasCommandRegistry* GetRegistry() const { return registry; }

        // The menus to edit; also what Reset returns the menus to.
        void SetLayoutSet(const MenuLayoutSet& set);
        // A toolbar to edit, as it is now, and the program's defaults for it -
        // what Reset returns it to, and what tells a group of the program's
        // from one the user made. `title` is its name in the tree
        // (default: the layout's name).
        void AddToolbar(const ToolbarLayout& current, const ToolbarLayout& defaults,
                        const std::string& title = "");
        void ClearToolbars();
        // Which commands may go on the toolbar `toolbarName` (a palette takes
        // tools only); every command when none is set.
        void SetCatalogueFilter(const std::string& toolbarName,
                                std::function<bool(const MenuCommand&)> accepts);

        // The working copy: the menus and toolbars as edited so far.
        const CommandLayoutSet& GetLayoutSet() const { return working; }

        // ===== EDITING =====
        // What the buttons do, open to the keyboard and to tests as well. The
        // left tree's ids: "m<i>" a menu and "m<i>.<j>.<k>..." a node of it;
        // "t<i>" a toolbar, "t<i>.<j>" its group j, "t<i>.<j>.<k>" command k
        // of that group; "menus", "toolbars" and "root" the headings.
        bool SelectLayoutNode(const std::string& nodeId);
        const std::string& GetSelectedLayoutNode() const { return selectedLeftId; }
        bool SelectCatalogueCommand(const std::string& commandId);
        // Add: the catalog's selected command after the selected entry - into
        // the selected submenu or group, or a toolbar's last group. On a
        // toolbar a command already in the group is shown again, not doubled.
        void AddSelectedCommand();
        // Remove: a menu entry goes; on a toolbar a command of the program's
        // group is hidden (it stays in its group, unchecked) and one the user
        // added goes, a program's group is hidden and the user's own goes.
        void RemoveSelected();
        // -1 up, +1 down, within the entry's list (a group among the toolbar's
        // groups, a command within its group).
        void MoveSelected(int direction);
        // A submenu in a menu; a group of the user's on a toolbar, after the
        // selected group.
        void AddGroup();
        // A menu separator (toolbars separate their groups themselves).
        void AddSeparator();
        // Fold the selected toolbar group into one slot, or unfold it.
        void ToggleFoldSelected();
        // The selected submenu's or menu entry's label, or a toolbar group of
        // the user's; false where a name cannot be given.
        bool RenameSelected(const std::string& name);
        // The check box of a toolbar group (shown) or of a command in it.
        bool SetLayoutNodeChecked(const std::string& nodeId, bool checked);
        // The selected toolbar's icon size and labels.
        void SetSelectedToolbarIconSize(ToolbarIconSize size);
        void SetSelectedToolbarShowLabels(bool show);

        // The parts, for a host that wants to style them or a test.
        std::shared_ptr<UltraCanvasTreeView> GetLayoutTree() const { return leftTree; }
        std::shared_ptr<UltraCanvasTreeView> GetCatalogueTree() const { return rightTree; }
        // The selected toolbar as the edits leave it; its buttons do nothing.
        std::shared_ptr<UltraCanvasToolbar> GetPreview() const { return preview; }

        // ===== RESULT CALLBACKS =====
        std::function<void(const CommandLayoutSet&)> onApply;  // rebuild the live menus and toolbars now
        std::function<void(const CommandLayoutSet&)> onSave;   // apply, and keep them
        std::function<void()> onReset;                         // the working copy is back to the baseline
        std::function<void()> onCancel;                        // discard and close

    private:
        // ===== BUILD =====
        void BuildUi();
        void RebuildLeftTree();
        void RebuildRightTree();
        void BuildMenuNode(const std::string& parentId, const MenuLayoutNode& node,
                           const std::string& nodeId);
        void BuildToolbarNodes(int toolbarIndex);
        void UpdateControls();
        void UpdatePreview();
        void Changed(const std::string& selectId);

        // ===== MENU ADDRESSING =====
        struct LeftRef {
            bool isRoot = false;
            int menuIndex = -1;
            std::vector<int> path; // empty => the menu itself
        };
        LeftRef ParseLeftId(const std::string& nodeId) const;
        std::string MakeLeftId(const LeftRef& ref) const;
        MenuLayoutNode* ResolveNode(const LeftRef& ref);
        std::vector<MenuLayoutNode>* ResolveParentList(const LeftRef& ref, int& indexInParent);
        void ComputeInsertTarget(std::vector<MenuLayoutNode>*& outList, int& outIndex);
        std::string MenuNodeText(const MenuLayoutNode& node) const;
        std::string MenuNodeIcon(const MenuLayoutNode& node) const;
        std::string InsertMenuNode(const MenuLayoutNode& node);

        // ===== TOOLBAR ADDRESSING =====
        // "t<i>[.<j>[.<k>]]": -1 for a part not given; false for another id.
        static bool ParseToolbarId(const std::string& nodeId, int& toolbar, int& group, int& command);
        static std::string ToolbarId(int toolbar, int group = -1, int command = -1);
        bool IsToolbarContext() const;
        const ToolbarLayoutGroup* DefaultGroupOf(int toolbar, const std::string& groupId) const;
        std::string GroupText(const ToolbarLayoutGroup& group) const;
        std::string CommandText(const std::string& commandId) const;
        std::string CommandIcon(const std::string& commandId) const;
        std::string UniqueGroupId(const ToolbarLayout& layout) const;

        // ===== STATE =====
        UltraCanvasCommandRegistry* registry = nullptr;
        CommandLayoutSet working;            // edited copy
        MenuLayoutSet baselineMenus;         // Reset target for the menus
        struct ToolbarInfo {
            std::string title;
            ToolbarLayout defaults;          // Reset target, and what is the program's
        };
        std::vector<ToolbarInfo> toolbarInfo;   // parallel to working.toolbars
        std::map<std::string, std::function<bool(const MenuCommand&)>> catalogueFilters;
        std::string selectedLeftId;
        std::string selectedRightId;
        int previewToolbar = -1;
        bool rebuilding = false;             // no check callbacks while the tree is rebuilt

        // ===== UI =====
        std::shared_ptr<UltraCanvasSplitPane> splitPane;
        std::shared_ptr<UltraCanvasTreeView> leftTree;
        std::shared_ptr<UltraCanvasTreeView> rightTree;
        std::shared_ptr<UltraCanvasTextInput> nameField;
        std::shared_ptr<UltraCanvasDropdown> iconSizeList;
        std::shared_ptr<UltraCanvasCheckbox> labelsBox;
        std::shared_ptr<UltraCanvasLabel> previewCaption;
        std::shared_ptr<UltraCanvasToolbar> preview;
        std::shared_ptr<UltraCanvasButton> addButton, removeButton, upButton, downButton,
                                           groupButton, separatorButton, foldButton;
    };

// ===== IN A WINDOW =====
    // Opens `widget` in a dialog window of its own, `width` x `height`, over
    // `parent`. Cancel and Save close the window after the widget's own
    // callbacks have run; Apply leaves it open. The window keeps itself alive
    // until it is closed; it is returned for a host that wants to raise or
    // close it. Null when no window could be made.
    std::shared_ptr<UltraCanvasWindowBase> ShowCommandConfigDialog(
            UltraCanvasWindowBase* parent, std::shared_ptr<UltraCanvasCommandConfigWidget> widget,
            const std::string& title = "Customise Menus and Toolbars", int width = 980, int height = 680);

// ===== FACTORY =====
    inline std::shared_ptr<UltraCanvasCommandConfigWidget> CreateCommandConfigWidget(
            const std::string& id, float x, float y, float w, float h) {
        return std::make_shared<UltraCanvasCommandConfigWidget>(id, x, y, w, h);
    }

} // namespace UltraCanvas
