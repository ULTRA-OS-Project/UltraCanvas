// include/UltraCanvasMenuConfigWidget.h
// Interactive widget for customising menus.
//
// Layout (left -> right):
//   * left pane : the menu structure being edited, as a tree. A single "Program"
//                 root holds every configured menu (menubar + any context menus),
//                 so they can all be edited together.
//   * middle    : Add -> / <- Remove buttons, plus reorder and grouping helpers.
//   * right pane: the catalog of available commands from the registry, grouped by
//                 category.
//   * footer    : Save, Cancel, Apply, Reset.
//
// The widget edits a working copy of a MenuLayoutSet and never touches live menus
// itself. It reports intent through callbacks so the host application decides how
// to apply/persist:
//   * onApply(set) : rebuild the live menu(s) now, temporarily (until app exit).
//   * onSave(set)  : apply and also persist to disk.
//   * onReset()    : host reloads defaults and re-seeds the widget.
//   * onCancel()   : discard and close.
//
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMenuRegistry.h"
#include "UltraCanvasMenuLayout.h"
#include <memory>
#include <functional>
#include <string>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasMenuConfigWidget : public UltraCanvasContainer {
    public:
        UltraCanvasMenuConfigWidget(const std::string& id, float x, float y, float w, float h);
        explicit UltraCanvasMenuConfigWidget(const std::string& id)
                : UltraCanvasMenuConfigWidget(id, -1, -1, -1, -1) {}

        // ===== CONFIGURATION =====
        // Registry supplying the catalog of available commands. Defaults to the
        // process-wide UltraCanvasMenuRegistry::Default().
        void SetRegistry(UltraCanvasMenuRegistry* registry);
        UltraCanvasMenuRegistry* GetRegistry() const { return registry; }

        // Seed the widget with the menus to edit. Keeps a working copy; the passed
        // set is also remembered as the Reset baseline.
        void SetLayoutSet(const MenuLayoutSet& set);
        const MenuLayoutSet& GetLayoutSet() const { return workingSet; }

        // ===== RESULT CALLBACKS =====
        std::function<void(const MenuLayoutSet&)> onApply;  // temporary apply
        std::function<void(const MenuLayoutSet&)> onSave;   // apply + persist
        std::function<void()> onReset;                      // host reloads defaults
        std::function<void()> onCancel;                     // discard/close

    private:
        // ===== BUILD =====
        void BuildUi(float w, float h);
        void RebuildLeftTree();
        void RebuildRightTree();
        void BuildLeftNode(const std::string& parentId, const MenuLayoutNode& node,
                           const std::string& nodeId);

        // ===== NODE-ID <-> LAYOUT ADDRESSING =====
        // Left-tree node ids encode a path: "root" (Program), "m<i>" (menu i),
        // "m<i>.<j>.<k>" (node at topLevel[j].children[k]...). Right-tree command
        // ids are "cmd:<id>"; category ids are "cat:<name>".
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

        std::string LeftNodeText(const MenuLayoutNode& node) const;
        std::string LeftNodeIcon(const MenuLayoutNode& node) const;

        // ===== ACTIONS =====
        void InsertAtTarget(const MenuLayoutNode& node);
        void OnAddCommand();
        void OnRemove();
        void OnMove(int direction); // -1 up, +1 down
        void OnAddSeparator();
        void OnAddSubmenu();
        void ReselectLeft(const std::string& nodeId);

        // ===== STATE =====
        UltraCanvasMenuRegistry* registry = nullptr;
        MenuLayoutSet workingSet;   // edited copy
        MenuLayoutSet baselineSet;  // Reset target (last seeded)

        std::string selectedLeftId;
        std::string selectedRightId;

        // ===== UI =====
        std::shared_ptr<UltraCanvasSplitPane> splitPane;
        std::shared_ptr<UltraCanvasTreeView> leftTree;
        std::shared_ptr<UltraCanvasTreeView> rightTree;
    };

// ===== FACTORY =====
    inline std::shared_ptr<UltraCanvasMenuConfigWidget> CreateMenuConfigWidget(
            const std::string& id, float x, float y, float w, float h) {
        return std::make_shared<UltraCanvasMenuConfigWidget>(id, x, y, w, h);
    }

} // namespace UltraCanvas
