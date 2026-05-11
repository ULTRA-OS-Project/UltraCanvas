// Apps/Demo/Items/UltraCanvasNodeDiagramExamples.cpp
// Comprehensive examples for UltraCanvasNodeDiagram v2.0.5
// Last Modified: 2026-05-09
//
// Demonstrates all features of NodeDiagram 2.0.x:
//  - Simple API (graph visualization)
//  - Verbose API with custom handles (workflow editor)
//  - Drag-to-connect interactive editing
//  - All four link styles (Straight, Bezier, SmoothStep, Step)
//  - JSON save/load
//  - Multi-select with Shift+click and selection box
//  - Themes
//  - Layouts (force-directed, circular, grid, hierarchical)
//
// 2.0.5: Editor polish:
//        - Color swatches now render as real color squares (UltraCanvasButton
//          would not paint its background reliably for empty-text buttons; we
//          use a thin ColorSwatch class that draws the color directly).
//        - Cancel reverts node creation (was leaving the half-created node).
//        - Friends layout no longer overlaps thanks to component fixes.
// 2.0.4: Demo polished from real-screenshot feedback:
//        - Custom nodes no longer have handles (cleaner look)
//        - New nodes auto-size to fit their label (uses SuggestNodeSizeForLabel)
//        - Connect button is now a real "click A, click B" link mode
//        - Custom tab gets dedicated Delete (selection) and Clear All buttons
//        - Reset in Custom only re-fits viewport (preserves user's nodes)
//        - Per-tab button visibility (Add Node/Delete/Clear All only on Custom)
//        - Friends Reset uses the upgraded force-directed layout (no overlaps)
// 2.0.3: "Custom" tab is now a real editor with Add Node, right-click-to-add,
//        and a label/color edit popup that opens for new nodes and on
//        double-click of existing nodes.
// 2.0.2: Main demo refactored to a container layout (title + tabs Friends/Custom
//        + button bar + status bar) following the Block Diagram demo pattern.
//        Per-component minimap/controls overlays removed in favor of an
//        external button bar with explicit Select/Connect/Reset actions.

#include "Plugins/Diagrams/UltraCanvasNodeDiagram.h"
#include "UltraCanvasDemo.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUIElement.h"
#include <memory>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>

namespace UltraCanvas {

// =============================================================================
// 1. SIMPLE API - GRAPH VISUALIZATION (compatible with 1.x)
// =============================================================================
//
// Quick way to visualize a graph: AddNode with simple parameters,
// AddLink between ids, run a layout, done.
//
// 2.0.2: This now returns the bare diagram (no overlays). The button bar and
// status are provided by the wrapping container (see CreateNodeDiagramExamples).

std::shared_ptr<UltraCanvasNodeDiagram> CreateFriendsNetworkDiagram(
        int x, int y, int w, int h) {
    auto diagram = CreateNodeDiagram("nd_friends", x, y, w, h);
    
    diagram->SetTheme(NodeDiagramTheme::Colorful);
    diagram->SetGridVisible(true, 25.0f);
    
    // Just node id, label, position - cannot get simpler
    diagram->AddNode("alice",   "Alice",   200, 200);
    diagram->AddNode("bob",     "Bob",     400, 150);
    diagram->AddNode("charlie", "Charlie", 600, 250);
    diagram->AddNode("diana",   "Diana",   300, 400);
    diagram->AddNode("eve",     "Eve",     500, 450);
    
    diagram->AddLink("l1", "alice", "bob");
    diagram->AddLink("l2", "alice", "diana");
    diagram->AddLink("l3", "bob", "charlie");
    diagram->AddLink("l4", "charlie", "eve");
    diagram->AddLink("l5", "diana", "eve");
    diagram->AddLink("l6", "bob", "diana");
    
    diagram->SetLayout(NodeDiagramLayout::ForceDirected);
    diagram->RunLayout();  // Auto-fits at the end (2.0.1)
    
    return diagram;
}

// Custom tab: empty canvas where the user builds their own graph
// Custom tab: a real editor. Empty canvas + Add Node + right-click create +
// double-click edit. Returns a container with the diagram inside, plus an
// initially-hidden popup overlay for label/color editing. The caller can fish
// the diagram out via GetCustomDiagram() since we wire its callbacks here.
//
// To keep state between callbacks (next node id, popup mode, edited node id),
// we attach a small state object via shared_ptr captured by the lambdas.

// Modes for the Custom editor (mutually exclusive)
enum class CustomMode {
    Select,    // Default: click selects, Shift+click toggles
    Connect    // Click node A, then click node B to create a link
};

struct CustomEditorState {
    int nodeCounter = 0;                         // For auto-generated ids
    int linkCounter = 0;                         // For auto-generated link ids
    std::string editingNodeId;                   // Node currently being edited (empty = creating new)
    std::shared_ptr<UltraCanvasNodeDiagram> diagram;
    std::shared_ptr<UltraCanvasContainer> popup;  // The edit popup overlay
    std::shared_ptr<UltraCanvasTextInput> labelInput;
    Color selectedColor = Color(120, 160, 220, 255);  // Currently picked swatch
    std::vector<std::shared_ptr<UltraCanvasUIElement>> swatchButtons;
    
    // 2.0.4: Connect mode state
    CustomMode mode = CustomMode::Select;
    std::string connectFirstNodeId;              // Node A in two-click connect
    std::shared_ptr<UltraCanvasLabel> statusRef; // For mode change feedback
    
    // 2.0.5: Cancel-revert. When the popup opens for a JUST-CREATED node (Add
    // Node button or right-click), this remembers its id so Cancel can remove
    // it. For double-click edits of existing nodes, this stays empty.
    std::string pendingNewNodeId;
};

// Predefined color palette for the swatches (matches Colorful theme tones)
static const std::vector<Color> kCustomSwatches = {
    Color(120, 160, 220, 255),  // Blue
    Color(120, 200, 120, 255),  // Green
    Color(220, 120, 120, 255),  // Red
    Color(255, 200, 100, 255),  // Yellow/Amber
    Color(180, 130, 220, 255),  // Purple
    Color(100, 200, 200, 255)   // Teal
};

// Helper: derive a darker border color from a fill color
static Color DeriveBorderColor(const Color& fill) {
    return Color(
        std::max(0, fill.r - 60),
        std::max(0, fill.g - 60),
        std::max(0, fill.b - 60),
        255
    );
}

// 2.0.5: Color swatch widget. UltraCanvasButton's SetBackgroundColor turned
// out to not paint the swatch in render in some configurations (the button
// only paints its background through its style/state machine which empty-text
// buttons skip). This thin element draws a colored rounded square directly
// and reports clicks via the public onClick callback - same shape as Button.
class ColorSwatch : public UltraCanvasUIElement {
public:
    ColorSwatch(const std::string& id, int x, int y, int size,
                const Color& fill)
        : UltraCanvasUIElement(id, x, y, size, size), color(fill) {}
    
    void SetSelected(bool sel) {
        if (selected != sel) {
            selected = sel;
            RequestRedraw();
        }
    }
    bool IsSelected() const { return selected; }
    Color GetColor() const { return color; }
    
    void Render(IRenderContext* ctx, const Rect2Di& dirtyRect) override {
        if (!ctx || !IsVisible()) return;
        // Outer "selected" ring
        auto localBounds = GetLocalBounds();
        if (selected) {
            ctx->SetStrokePaint(Color(255, 100, 0, 255));
            ctx->SetStrokeWidth(2.5f);
            ctx->DrawRoundedRectangle(localBounds, 1.0f);
        }
        localBounds.x += 1;
        localBounds.y += 1;
        localBounds.width -= 2;
        localBounds.height -= 2;
        // Filled color square
        ctx->SetFillPaint(color);
        ctx->FillRoundedRectangle(localBounds, 3.0f);
        // Subtle border for separation against light backgrounds
        ctx->SetStrokePaint(Color(120, 120, 130, 255));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRoundedRectangle(localBounds, 3.0f);
    }
    
    bool OnEvent(const UCEvent& event) override {
        if (!IsVisible() || IsDisabled()) return false;
        if (event.type == UCEventType::MouseDown &&
            event.button == UCMouseButton::Left &&
            Contains(event.pointer)) {
            if (onClick) onClick();
            return true;
        }
        return false;
    }
    
    std::function<void()> onClick;
    
private:
    Color color;
    bool selected = false;
};

// Forward declaration: opens the popup positioned over the diagram
static void OpenCustomEditorPopup(const std::shared_ptr<CustomEditorState>& st,
                                   const std::string& nodeIdToEdit,
                                   const std::string& initialLabel,
                                   const Color& initialColor,
                                   bool isNewNode);

// Returns the root container that holds the diagram + the (initially hidden)
// edit popup. Diagram is exposed via st->diagram.
std::shared_ptr<UltraCanvasContainer> CreateCustomEditor(
        int x, int y, int w, int h,
        std::shared_ptr<CustomEditorState> st,
        std::shared_ptr<UltraCanvasLabel> statusLabel) {
    
    auto root = std::make_shared<UltraCanvasContainer>(
        "ndCustomRoot", x, y, w, h);
    root->SetBackgroundColor(Color(255, 255, 255, 255));
    
    // ---- Diagram fills the whole tab ----
    auto diagram = CreateNodeDiagram("nd_custom", 0, 0, w, h);
    diagram->SetTheme(NodeDiagramTheme::Professional);
    diagram->SetGridVisible(true, 25.0f);
    diagram->SetSnapToGrid(true);
    diagram->SetSnapGrid(25.0f, 25.0f);
    diagram->SetDefaultLinkStyle(LinkStyle::Bezier);
    st->diagram = diagram;
    st->statusRef = statusLabel;  // 2.0.4: store for mode change feedback
    root->AddChild(diagram);
    
    // 2.0.4: auto-size helper using the new SuggestNodeSizeForLabel API
    auto sizeForLabel = [&diagram](const std::string& label) -> std::pair<float, float> {
        double w = 60.0, h = 60.0;
        diagram->SuggestNodeSizeForLabel(label, /*fontSize=*/11.0f,
                                          /*minSize=*/60.0f, w, h);
        // For circle nodes use square (max of the two) so geometry stays clean
        float s = static_cast<float>(std::max(w, h));
        return { s, s };
    };
    
    // ---- Right-click on canvas: create node at click position + open popup ----
    diagram->onCanvasRightClick = [st, statusLabel, sizeForLabel](float worldX, float worldY) {
        if (!st->diagram) return;
        std::ostringstream idStream;
        idStream << "node_" << (++st->nodeCounter);
        std::string newId = idStream.str();
        
        std::string defaultLabel = "Node " + std::to_string(st->nodeCounter);
        auto [w, h] = sizeForLabel(defaultLabel);
        
        NodeDiagramNode n(newId, defaultLabel);
        n.shape = NodeShape::Circle;
        n.x = worldX - w * 0.5f;
        n.y = worldY - h * 0.5f;
        n.width = w;
        n.height = h;
        n.fillColor = st->selectedColor;
        n.borderColor = DeriveBorderColor(st->selectedColor);
        st->diagram->AddNode(n);
        // 2.0.4: NO AddDefaultHandles - cleaner look in Custom (per Stefan's
        // "no handles - links use node centers" decision). Drag-to-connect is
        // replaced by the Connect button mode (click A then click B).
        
        OpenCustomEditorPopup(st, newId, n.label, n.fillColor, /*isNewNode=*/true);
        if (statusLabel) {
            statusLabel->SetText("Node created at right-click position - "
                                  "Edit label and color in the popup, click OK; "
                                  "Cancel will remove it.");
        }
    };
    
    // ---- Double-click on existing node: open popup with current values ----
    diagram->onNodeDoubleClick = [st, statusLabel](const std::string& nodeId) {
        if (!st->diagram) return;
        auto* node = st->diagram->GetNode(nodeId);
        if (!node) return;
        OpenCustomEditorPopup(st, nodeId, node->label, node->fillColor, /*isNewNode=*/false);
        if (statusLabel) {
            statusLabel->SetText("Editing node '" + nodeId + "' - "
                                  "Change label/color and click OK.");
        }
    };
    
    // ---- 2.0.4: Click on node - in Connect mode, this is the two-step
    //      A->B link creation. In Select mode, this is just the normal
    //      selection (which the component handles itself).
    diagram->onNodeClick = [st, statusLabel](const std::string& nodeId) {
        if (!st->diagram || st->mode != CustomMode::Connect) return;
        
        if (st->connectFirstNodeId.empty()) {
            // First click: remember the source
            st->connectFirstNodeId = nodeId;
            if (statusLabel) {
                statusLabel->SetText("Connect: source = '" + nodeId +
                                      "'. Click target node to create link, "
                                      "or Esc / Select to cancel.");
            }
        } else {
            // Second click: create link from A to B (skip if same node)
            if (nodeId == st->connectFirstNodeId) {
                if (statusLabel) {
                    statusLabel->SetText("Connect: same node clicked twice - "
                                          "cancelled. Click two different nodes.");
                }
                st->connectFirstNodeId.clear();
                return;
            }
            std::ostringstream idStream;
            idStream << "link_" << (++st->linkCounter);
            std::string linkId = idStream.str();
            st->diagram->AddLink(linkId, st->connectFirstNodeId, nodeId);
            
            if (statusLabel) {
                statusLabel->SetText("Connect: link created '" + st->connectFirstNodeId +
                                      "' -> '" + nodeId + "'. Click another source "
                                      "node to continue, or Select to exit Connect.");
            }
            // Stay in Connect mode so the user can chain links
            st->connectFirstNodeId.clear();
        }
    };
    
    // ---- Edit popup ----
    // Centered, ~340x220, hidden until OpenCustomEditorPopup() is called.
    const int POPUP_W = 340;
    const int POPUP_H = 220;
    int popupX = (w - POPUP_W) / 2;
    int popupY = (h - POPUP_H) / 2;
    
    auto popup = std::make_shared<UltraCanvasContainer>(
        "ndCustomPopup", popupX, popupY, POPUP_W, POPUP_H);
    popup->SetBackgroundColor(Color(252, 252, 254, 255));
    popup->SetVisible(false);
    st->popup = popup;
    
    // Popup title
    auto popupTitle = std::make_shared<UltraCanvasLabel>(
        "popupTitle", 12, 10, POPUP_W - 24, 22);
    popupTitle->SetText("Edit Node");
    popupTitle->SetFontSize(14);
    popupTitle->SetFontWeight(FontWeight::Bold);
    popupTitle->SetTextColor(Color(40, 50, 110, 255));
    popup->AddChild(popupTitle);
    
    // "Label:" caption
    auto lblCap = std::make_shared<UltraCanvasLabel>(
        "popupLblCap", 12, 40, 60, 22);
    lblCap->SetText("Label:");
    lblCap->SetFontSize(11);
    popup->AddChild(lblCap);
    
    // Label TextInput
    auto labelInput = std::make_shared<UltraCanvasTextInput>(
        "popupLabelInput", 70, 40, POPUP_W - 82, 24);
    labelInput->SetPlaceholder("Type a label...");
    st->labelInput = labelInput;
    popup->AddChild(labelInput);
    
    // "Color:" caption
    auto colCap = std::make_shared<UltraCanvasLabel>(
        "popupColCap", 12, 80, 60, 22);
    colCap->SetText("Color:");
    colCap->SetFontSize(11);
    popup->AddChild(colCap);
    
    // 6 color swatches - custom widget that paints a colored rounded square
    const int SW_SIZE = 28;
    const int SW_GAP = 6;
    int swX = 70;
    int swY = 80;
    for (size_t i = 0; i < kCustomSwatches.size(); ++i) {
        const Color& c = kCustomSwatches[i];
        std::ostringstream idStream;
        idStream << "popupSwatch" << i;
        auto sw = std::make_shared<ColorSwatch>(
            idStream.str(),
            swX + (int)i * (SW_SIZE + SW_GAP), swY, SW_SIZE, c);
        // Capture st as weak so the swatch can update sibling swatches' selected
        // state (so only one looks active at a time).
        sw->onClick = [st, c]() {
            st->selectedColor = c;
            // Mark which swatch is selected for visual feedback
            for (auto& other : st->swatchButtons) {
                auto* asSwatch = dynamic_cast<ColorSwatch*>(other.get());
                if (asSwatch) {
                    asSwatch->SetSelected(asSwatch->GetColor() == c);
                }
            }
        };
        popup->AddChild(sw);
        st->swatchButtons.push_back(sw);
    }
    
    // OK / Cancel / Delete buttons
    const int BTN_W = 80;
    const int BTN_H = 28;
    int bY = POPUP_H - 12 - BTN_H;
    
    auto btnDelete = std::make_shared<UltraCanvasButton>(
        "popupBtnDelete", 12, bY, BTN_W, BTN_H);
    btnDelete->SetText("Delete");
    btnDelete->SetBackgroundColor(Color(220, 90, 90, 255));
    btnDelete->onClick = [st, statusLabel]() {
        if (!st->diagram || st->editingNodeId.empty()) {
            if (st->popup) st->popup->SetVisible(false);
            return;
        }
        std::string id = st->editingNodeId;
        st->diagram->RemoveNode(id);
        st->editingNodeId.clear();
        st->pendingNewNodeId.clear();   // 2.0.5
        if (st->popup) st->popup->SetVisible(false);
        if (statusLabel) {
            statusLabel->SetText("Node '" + id + "' deleted.");
        }
    };
    popup->AddChild(btnDelete);
    
    auto btnCancel = std::make_shared<UltraCanvasButton>(
        "popupBtnCancel", POPUP_W - 12 - BTN_W * 2 - 6, bY, BTN_W, BTN_H);
    btnCancel->SetText("Cancel");
    btnCancel->onClick = [st, statusLabel]() {
        // 2.0.5: If this popup was opened for a JUST-CREATED node (right-click
        // or Add Node button), Cancel reverts the creation by deleting the
        // node. For double-click edits of existing nodes, pendingNewNodeId is
        // empty so we just close the popup leaving the node alone.
        bool revert = !st->pendingNewNodeId.empty();
        std::string revertedId = st->pendingNewNodeId;
        if (revert && st->diagram) {
            st->diagram->RemoveNode(revertedId);
        }
        st->editingNodeId.clear();
        st->pendingNewNodeId.clear();
        if (st->popup) st->popup->SetVisible(false);
        if (statusLabel) {
            if (revert) {
                statusLabel->SetText("Cancelled - node '" + revertedId + "' removed.");
            } else {
                statusLabel->SetText("Edit cancelled.");
            }
        }
    };
    popup->AddChild(btnCancel);
    
    auto btnOk = std::make_shared<UltraCanvasButton>(
        "popupBtnOk", POPUP_W - 12 - BTN_W, bY, BTN_W, BTN_H);
    btnOk->SetText("OK");
    btnOk->SetBackgroundColor(Color(80, 140, 220, 255));
    btnOk->onClick = [st, statusLabel]() {
        if (!st->diagram || st->editingNodeId.empty()) {
            if (st->popup) st->popup->SetVisible(false);
            return;
        }
        auto* node = st->diagram->GetNode(st->editingNodeId);
        if (!node) {
            if (st->popup) st->popup->SetVisible(false);
            return;
        }
        // Apply label
        std::string newLabel = st->labelInput ? st->labelInput->GetText() : "";
        if (!newLabel.empty()) {
            st->diagram->UpdateNodeLabel(st->editingNodeId, newLabel);
            
            // 2.0.4: Auto-resize the node so the new label fits cleanly.
            // We re-center on the node's current center to avoid jumping.
            float oldCx = node->x + node->width  * 0.5f;
            float oldCy = node->y + node->height * 0.5f;
            double newW = 60.0, newH = 60.0;
            st->diagram->SuggestNodeSizeForLabel(newLabel, 11.0f, 60.0f, newW, newH);
            float side = static_cast<float>(std::max(newW, newH));
            node->width  = side;
            node->height = side;
            node->x = oldCx - side * 0.5f;
            node->y = oldCy - side * 0.5f;
        }
        // Apply color
        st->diagram->SetNodeColor(st->editingNodeId,
                                   st->selectedColor,
                                   DeriveBorderColor(st->selectedColor));
        std::string id = st->editingNodeId;
        st->editingNodeId.clear();
        st->pendingNewNodeId.clear();  // 2.0.5: confirm the creation
        if (st->popup) st->popup->SetVisible(false);
        if (statusLabel) {
            statusLabel->SetText("Node '" + id + "' updated.");
        }
    };
    popup->AddChild(btnOk);
    
    // The popup is added LAST so it draws on top of the diagram
    root->AddChild(popup);
    
    return root;
}

// Opens the popup. Sets the editing target and pre-fills label/color.
// 2.0.5: isNewNode=true means "this node was JUST created and Cancel should
// remove it"; isNewNode=false (the default) is the double-click-to-edit case
// where Cancel just closes the popup leaving the existing node alone.
static void OpenCustomEditorPopup(const std::shared_ptr<CustomEditorState>& st,
                                   const std::string& nodeIdToEdit,
                                   const std::string& initialLabel,
                                   const Color& initialColor,
                                   bool isNewNode) {
    if (!st || !st->popup) return;
    st->editingNodeId = nodeIdToEdit;
    st->pendingNewNodeId = isNewNode ? nodeIdToEdit : std::string();
    if (st->labelInput) st->labelInput->SetText(initialLabel);
    st->selectedColor = initialColor;
    
    // 2.0.5: refresh swatch visual selection so the active color is highlighted
    for (auto& sw : st->swatchButtons) {
        auto* asSwatch = dynamic_cast<ColorSwatch*>(sw.get());
        if (asSwatch) {
            asSwatch->SetSelected(asSwatch->GetColor() == initialColor);
        }
    }
    
    st->popup->SetVisible(true);
}

// Legacy helper kept for compatibility with old callers
std::shared_ptr<UltraCanvasNodeDiagram> CreateCustomBlankDiagram(
        int x, int y, int w, int h) {
    auto diagram = CreateNodeDiagram("nd_custom", x, y, w, h);
    diagram->SetTheme(NodeDiagramTheme::Professional);
    diagram->SetGridVisible(true, 25.0f);
    return diagram;
}

// Legacy entry point retained for callers/registries that referenced it.
std::shared_ptr<UltraCanvasUIElement> CreateNodeDiagramSocialNetworkExample() {
    return CreateFriendsNetworkDiagram(0, 0, 800, 600);
}

// =============================================================================
// 2. VERBOSE API - WORKFLOW EDITOR WITH HANDLES
// =============================================================================
//
// For interactive editing with explicit input/output ports, use the verbose
// AddNode(NodeDiagramNode&) and AddDefaultHandles().

std::shared_ptr<UltraCanvasUIElement> CreateNodeDiagramWorkflowEditorExample() {
    auto diagram = CreateNodeDiagram("nd_workflow", 0, 0, 800, 600);
    
    diagram->SetTheme(NodeDiagramTheme::Professional);
    diagram->SetGridVisible(true, 25.0f);
    diagram->SetSnapToGrid(true);
    diagram->SetSnapGrid(25.0f, 25.0f);
    diagram->SetDefaultLinkStyle(LinkStyle::Bezier);
    
    // Build a "Start -> Process -> Decision -> End" workflow with verbose API
    NodeDiagramNode startNode("start", "Start");
    startNode.shape = NodeShape::RoundedSquare;
    startNode.x = 100; startNode.y = 250;
    startNode.width = 80; startNode.height = 50;
    startNode.fillColor = Color(120, 200, 120, 255);
    startNode.borderColor = Color(60, 120, 60, 255);
    diagram->AddNode(startNode);
    diagram->AddDefaultHandles("start");  // left=target, right=source
    
    NodeDiagramNode processNode("process", "Process Data");
    processNode.shape = NodeShape::Rectangle;
    processNode.x = 275; processNode.y = 250;
    processNode.width = 120; processNode.height = 60;
    processNode.fillColor = Color(120, 160, 220, 255);
    processNode.borderColor = Color(60, 80, 140, 255);
    diagram->AddNode(processNode);
    diagram->AddDefaultHandles("process");
    
    NodeDiagramNode decisionNode("decision", "Valid?");
    decisionNode.shape = NodeShape::Diamond;
    decisionNode.x = 475; decisionNode.y = 240;
    decisionNode.width = 80; decisionNode.height = 80;
    decisionNode.fillColor = Color(255, 200, 100, 255);
    decisionNode.borderColor = Color(180, 120, 40, 255);
    diagram->AddNode(decisionNode);
    diagram->AddDefaultHandles("decision");
    
    NodeDiagramNode endNode("end", "End");
    endNode.shape = NodeShape::RoundedSquare;
    endNode.x = 650; endNode.y = 175;
    endNode.width = 80; endNode.height = 50;
    endNode.fillColor = Color(220, 120, 120, 255);
    endNode.borderColor = Color(140, 60, 60, 255);
    diagram->AddNode(endNode);
    diagram->AddDefaultHandles("end");
    
    NodeDiagramNode retryNode("retry", "Retry");
    retryNode.shape = NodeShape::RoundedSquare;
    retryNode.x = 650; retryNode.y = 325;
    retryNode.width = 80; retryNode.height = 50;
    retryNode.fillColor = Color(220, 180, 120, 255);
    retryNode.borderColor = Color(140, 100, 40, 255);
    diagram->AddNode(retryNode);
    diagram->AddDefaultHandles("retry");
    
    // Verbose link with explicit handles
    NodeDiagramLink l1("l_start_proc", "start", "process");
    l1.sourceHandleId = "source";
    l1.targetHandleId = "target";
    l1.style = LinkStyle::Bezier;
    diagram->AddLink(l1);
    
    NodeDiagramLink l2("l_proc_dec", "process", "decision");
    l2.sourceHandleId = "source";
    l2.targetHandleId = "target";
    l2.style = LinkStyle::Bezier;
    diagram->AddLink(l2);
    
    NodeDiagramLink l3("l_dec_end", "decision", "end");
    l3.sourceHandleId = "source";
    l3.targetHandleId = "target";
    l3.style = LinkStyle::Bezier;
    l3.label = "yes";
    l3.lineColor = Color(60, 140, 60, 255);
    diagram->AddLink(l3);
    
    NodeDiagramLink l4("l_dec_retry", "decision", "retry");
    l4.sourceHandleId = "source";
    l4.targetHandleId = "target";
    l4.style = LinkStyle::Bezier;
    l4.label = "no";
    l4.lineColor = Color(200, 80, 60, 255);
    diagram->AddLink(l4);
    
    NodeDiagramLink l5("l_retry_proc", "retry", "process");
    l5.sourceHandleId = "source";
    l5.targetHandleId = "target";
    l5.style = LinkStyle::SmoothStep;
    l5.lineColor = Color(140, 100, 60, 255);
    diagram->AddLink(l5);
    
    // Enable interactive editing
    diagram->SetControlsVisible(true);
    diagram->SetMinimapVisible(true);
    diagram->SetMinimapPosition(NodeDiagramPanelPosition::BottomRight);
    
    // Hook a callback so the user gets feedback when drag-to-connect creates a link
    diagram->onLinkCreated = [](const NodeDiagramLink& link) {
        std::cout << "[Workflow] New connection: " << link.sourceNodeId
                  << " -> " << link.targetNodeId << std::endl;
    };
    
    return diagram;
}

// =============================================================================
// 3. LINK STYLES SHOWCASE
// =============================================================================
//
// Demonstrates the four routing styles side by side.

std::shared_ptr<UltraCanvasUIElement> CreateNodeDiagramLinkStylesExample() {
    auto diagram = CreateNodeDiagram("nd_styles", 0, 0, 800, 600);
    
    diagram->SetTheme(NodeDiagramTheme::Professional);
    diagram->SetGridVisible(true);
    
    // Four pairs of nodes, each connected with a different style
    struct StylePair {
        std::string label;
        LinkStyle style;
        float yOffset;
        Color color;
    };
    
    std::vector<StylePair> pairs = {
        { "Straight",   LinkStyle::Straight,   100.0f, Color(80, 80, 200, 255) },
        { "Bezier",     LinkStyle::Bezier,     220.0f, Color(80, 200, 80, 255) },
        { "SmoothStep", LinkStyle::SmoothStep, 340.0f, Color(200, 120, 60, 255) },
        { "Step",       LinkStyle::Step,       460.0f, Color(200, 60, 200, 255) }
    };
    
    int idx = 0;
    for (const auto& pair : pairs) {
        std::string srcId = "src_" + std::to_string(idx);
        std::string tgtId = "tgt_" + std::to_string(idx);
        std::string linkId = "link_" + std::to_string(idx);
        
        NodeDiagramNode src(srcId, pair.label + " src");
        src.shape = NodeShape::RoundedSquare;
        src.x = 100; src.y = pair.yOffset;
        src.width = 100; src.height = 50;
        src.fillColor = pair.color;
        diagram->AddNode(src);
        diagram->AddDefaultHandles(srcId);
        
        NodeDiagramNode tgt(tgtId, pair.label + " tgt");
        tgt.shape = NodeShape::RoundedSquare;
        tgt.x = 550; tgt.y = pair.yOffset;
        tgt.width = 100; tgt.height = 50;
        tgt.fillColor = pair.color;
        diagram->AddNode(tgt);
        diagram->AddDefaultHandles(tgtId);
        
        NodeDiagramLink link(linkId, srcId, tgtId);
        link.sourceHandleId = "source";
        link.targetHandleId = "target";
        link.style = pair.style;
        link.lineColor = pair.color;
        link.lineWidth = 2.5f;
        diagram->AddLink(link);
        
        idx++;
    }
    
    diagram->SetControlsVisible(true);
    return diagram;
}

// =============================================================================
// 4. MULTI-SELECT + KEYBOARD
// =============================================================================
//
// A grid of nodes you can multi-select with Shift+click or selection box,
// then delete with Delete key, or Ctrl+A to select all.

std::shared_ptr<UltraCanvasUIElement> CreateNodeDiagramMultiSelectExample() {
    auto diagram = CreateNodeDiagram("nd_multi", 0, 0, 800, 600);
    
    diagram->SetTheme(NodeDiagramTheme::Default);
    diagram->SetGridVisible(true);
    
    // 5x4 grid
    int idx = 0;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 5; ++col) {
            std::string id = "n" + std::to_string(idx++);
            std::string label = "N" + std::to_string(idx);
            float x = 100.0f + col * 130.0f;
            float y = 80.0f + row * 110.0f;
            diagram->AddNode(id, label, x, y, 60.0f);
        }
    }
    
    // Connect a few in zig-zag
    diagram->AddLink("la", "n0", "n6");
    diagram->AddLink("lb", "n6", "n12");
    diagram->AddLink("lc", "n12", "n18");
    diagram->AddLink("ld", "n2", "n8");
    diagram->AddLink("le", "n8", "n14");
    
    // Selection feedback
    diagram->onSelectionChange = [](const std::vector<std::string>& nodeIds,
                                    const std::vector<std::string>& linkIds) {
        std::cout << "[MultiSelect] " << nodeIds.size() << " nodes, "
                  << linkIds.size() << " links selected" << std::endl;
    };
    
    diagram->SetControlsVisible(true);
    return diagram;
}

// =============================================================================
// 5. JSON SAVE/LOAD
// =============================================================================
//
// Serialize the diagram to JSON, then reconstruct it from that JSON.

std::shared_ptr<UltraCanvasUIElement> CreateNodeDiagramJsonRoundTripExample() {
    auto diagram = CreateNodeDiagram("nd_json", 0, 0, 800, 600);
    
    diagram->SetTheme(NodeDiagramTheme::Colorful);
    diagram->SetGridVisible(true);
    
    // Build a small graph
    diagram->AddNode("a", "Node A", 150, 150);
    diagram->AddNode("b", "Node B", 350, 150);
    diagram->AddNode("c", "Node C", 550, 250);
    diagram->AddNode("d", "Node D", 250, 350);
    diagram->SetNodeShape("b", NodeShape::Diamond);
    diagram->SetNodeShape("c", NodeShape::Hexagon);
    diagram->SetNodeShape("d", NodeShape::Star);
    
    diagram->AddLink("ab", "a", "b");
    diagram->AddLink("bc", "b", "c");
    diagram->AddLink("ad", "a", "d");
    diagram->AddLink("dc", "d", "c");
    diagram->SetLinkLabel("ab", "1");
    diagram->SetLinkLabel("bc", "2");
    diagram->SetLinkLabel("ad", "3");
    
    // Round trip: ToJson -> FromJson -> verify
    std::string json = diagram->ToJson();
    std::cout << "[JSON] Serialized size: " << json.size() << " bytes" << std::endl;
    
    // Save to disk for inspection (optional)
    std::ofstream out("/tmp/nodediagram_export.json");
    if (out) {
        out << json;
        out.close();
        std::cout << "[JSON] Saved to /tmp/nodediagram_export.json" << std::endl;
    }
    
    // Reload (clears + rebuilds)
    bool ok = diagram->FromJson(json);
    std::cout << "[JSON] Round trip: " << (ok ? "OK" : "FAILED") << std::endl;
    
    diagram->SetControlsVisible(true);
    return diagram;
}

// =============================================================================
// 6. MINIMAP + CONTROLS DASHBOARD
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> CreateNodeDiagramDashboardExample() {
    auto diagram = CreateNodeDiagram("nd_dashboard", 0, 0, 800, 600);
    
    diagram->SetTheme(NodeDiagramTheme::Professional);
    diagram->SetGridVisible(true, 30.0f);
    
    // A big graph to make the minimap meaningful
    int N = 18;
    for (int i = 0; i < N; ++i) {
        std::string id = "node_" + std::to_string(i);
        std::string label = "Node " + std::to_string(i);
        float x = 80.0f + (i % 6) * 130.0f;
        float y = 80.0f + (i / 6) * 130.0f;
        diagram->AddNode(id, label, x, y, 70.0f);
    }
    for (int i = 0; i < N - 1; ++i) {
        diagram->AddLink("link_" + std::to_string(i),
                         "node_" + std::to_string(i),
                         "node_" + std::to_string(i + 1));
    }
    // Some cross-links
    diagram->AddLink("c1", "node_0", "node_7");
    diagram->AddLink("c2", "node_3", "node_10");
    diagram->AddLink("c3", "node_5", "node_14");
    
    // Configure minimap (bottom-right) and controls (bottom-left)
    diagram->SetMinimapVisible(true);
    diagram->SetMinimapPosition(NodeDiagramPanelPosition::BottomRight);
    
    NodeDiagramMinimapConfig miniCfg = diagram->GetMinimapConfig();
    miniCfg.width = 200.0f;
    miniCfg.height = 140.0f;
    miniCfg.viewportFill = Color(0, 150, 200, 50);
    diagram->SetMinimapConfig(miniCfg);
    
    diagram->SetControlsVisible(true);
    diagram->SetControlsPosition(NodeDiagramPanelPosition::BottomLeft);
    
    return diagram;
}

// =============================================================================
// 7. CIRCULAR LAYOUT SHOWCASE
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> CreateNodeDiagramCircularLayoutExample() {
    auto diagram = CreateNodeDiagram("nd_circular", 0, 0, 800, 600);
    
    diagram->SetTheme(NodeDiagramTheme::Colorful);
    
    // 12 nodes around a circle, fully connected (mesh)
    int N = 12;
    for (int i = 0; i < N; ++i) {
        diagram->AddNode("p" + std::to_string(i), "P" + std::to_string(i + 1),
                          0, 0, 50.0f);
    }
    int linkIdx = 0;
    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            std::string lid = "link_" + std::to_string(linkIdx++);
            diagram->AddLink(lid, "p" + std::to_string(i), "p" + std::to_string(j));
        }
    }
    
    diagram->SetLayout(NodeDiagramLayout::Circular);
    diagram->RunLayout();
    diagram->SetControlsVisible(true);
    return diagram;
}

// =============================================================================
// 8. HIERARCHICAL TREE
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> CreateNodeDiagramHierarchicalExample() {
    auto diagram = CreateNodeDiagram("nd_hier", 0, 0, 800, 600);
    
    diagram->SetTheme(NodeDiagramTheme::Professional);
    diagram->SetGridVisible(true);
    diagram->SetDefaultLinkStyle(LinkStyle::SmoothStep);
    
    // Org chart
    diagram->AddNode("ceo",     "CEO",          0, 0, 100, 50);
    diagram->AddNode("cto",     "CTO",          0, 0, 100, 50);
    diagram->AddNode("cfo",     "CFO",          0, 0, 100, 50);
    diagram->AddNode("eng_mgr", "Eng Manager",  0, 0, 100, 50);
    diagram->AddNode("design",  "Design Lead",  0, 0, 100, 50);
    diagram->AddNode("dev1",    "Developer 1",  0, 0, 100, 50);
    diagram->AddNode("dev2",    "Developer 2",  0, 0, 100, 50);
    diagram->AddNode("dev3",    "Developer 3",  0, 0, 100, 50);
    diagram->AddNode("acc",     "Accountant",   0, 0, 100, 50);
    
    diagram->AddLink("e1", "ceo", "cto");
    diagram->AddLink("e2", "ceo", "cfo");
    diagram->AddLink("e3", "cto", "eng_mgr");
    diagram->AddLink("e4", "cto", "design");
    diagram->AddLink("e5", "eng_mgr", "dev1");
    diagram->AddLink("e6", "eng_mgr", "dev2");
    diagram->AddLink("e7", "eng_mgr", "dev3");
    diagram->AddLink("e8", "cfo", "acc");
    
    diagram->ApplyHierarchicalLayout("ceo");
    diagram->SetControlsVisible(true);
    return diagram;
}

// =============================================================================
// 9. THEMES SHOWCASE - DARK
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> CreateNodeDiagramDarkThemeExample() {
    auto diagram = CreateNodeDiagram("nd_dark", 0, 0, 800, 600);
    
    diagram->SetTheme(NodeDiagramTheme::Dark);
    diagram->SetBackgroundColor(Color(30, 32, 40, 255));
    
    diagram->AddNode("server",   "Server",     400, 100, 90.0f);
    diagram->AddNode("api",      "API",        250, 250, 80.0f);
    diagram->AddNode("auth",     "Auth",       400, 250, 80.0f);
    diagram->AddNode("db",       "Database",   550, 250, 80.0f);
    diagram->AddNode("client1",  "Client 1",   250, 400, 70.0f);
    diagram->AddNode("client2",  "Client 2",   400, 400, 70.0f);
    diagram->AddNode("client3",  "Client 3",   550, 400, 70.0f);
    
    diagram->SetNodeShape("server", NodeShape::Hexagon);
    diagram->SetNodeShape("db",     NodeShape::RoundedSquare);
    
    diagram->AddLink("a1", "server", "api");
    diagram->AddLink("a2", "server", "auth");
    diagram->AddLink("a3", "server", "db");
    diagram->AddLink("a4", "api", "client1");
    diagram->AddLink("a5", "api", "client2");
    diagram->AddLink("a6", "api", "client3");
    
    diagram->SetControlsVisible(true);
    return diagram;
}

// =============================================================================
// FACTORY REGISTRATION
// =============================================================================
//
// Following the demo app pattern: factory functions returning UIElement*,
// to be registered in RegisterAllDemoItems().

void RegisterNodeDiagramExamples(/* DemoRegistry& registry */) {
    // registry.Register("Node Diagram - Social Network",     CreateNodeDiagramSocialNetworkExample);
    // registry.Register("Node Diagram - Workflow Editor",    CreateNodeDiagramWorkflowEditorExample);
    // registry.Register("Node Diagram - Link Styles",        CreateNodeDiagramLinkStylesExample);
    // registry.Register("Node Diagram - Multi-Select",       CreateNodeDiagramMultiSelectExample);
    // registry.Register("Node Diagram - JSON Round-Trip",    CreateNodeDiagramJsonRoundTripExample);
    // registry.Register("Node Diagram - Dashboard",          CreateNodeDiagramDashboardExample);
    // registry.Register("Node Diagram - Circular Layout",    CreateNodeDiagramCircularLayoutExample);
    // registry.Register("Node Diagram - Hierarchical",       CreateNodeDiagramHierarchicalExample);
    // registry.Register("Node Diagram - Dark Theme",         CreateNodeDiagramDarkThemeExample);
}

// =============================================================================
// MAIN ENTRY POINT - CREATE NODE DIAGRAM EXAMPLES
// =============================================================================
//
// Block-Diagram-demo-style layout: title (with subtitle) + 2 tabs (Friends /
// Custom) + button bar (Select / Connect / Reset) + status bar with hint about
// mouse wheel zoom. The component itself has no overlays - all controls are
// external widgets, like the Block Diagram demo (image 3) Stefan asked to
// emulate.

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateNodeDiagramExamples() {
    // ---- Layout constants ----
    const int CONTAINER_W = 960;
    const int CONTAINER_H = 720;
    const int PAD = 20;
    const int TITLE_H = 32;
    const int SUBTITLE_H = 44;   // 2.0.6: 22->44 to fit 2 lines (text was truncated)
    const int TABS_H = 470;     // The TabbedContainer panel
    const int BTNBAR_H = 36;
    const int STATUS_H = 26;
    
    // ---- Root container (light grey background like Gource demo) ----
    auto root = std::make_shared<UltraCanvasContainer>(
        "ndDemoRoot", 0, 0, CONTAINER_W, CONTAINER_H);
    root->SetBackgroundColor(Color(248, 248, 250, 255));
    
    int yCursor = PAD;
    
    // ---- Title ----
    auto titleLabel = std::make_shared<UltraCanvasLabel>(
        "ndTitle", PAD, yCursor, CONTAINER_W - 2 * PAD, TITLE_H);
    titleLabel->SetText("Node Diagram");
    titleLabel->SetFontSize(20);
    titleLabel->SetFontWeight(FontWeight::Bold);
    titleLabel->SetTextColor(Color(50, 60, 130, 255));
    root->AddChild(titleLabel);
    yCursor += TITLE_H + 4;
    
    // ---- Subtitle ----
    auto subtitleLabel = std::make_shared<UltraCanvasLabel>(
        "ndSubtitle", PAD, yCursor, CONTAINER_W - 2 * PAD, SUBTITLE_H);
    subtitleLabel->SetText(
        "Interactive graph editor - Friends shows a force-directed network. "
        "In Custom: click 'Add Node' or right-click on canvas to create nodes; "
        "double-click any node to edit its label and color.");
    subtitleLabel->SetFontSize(11);
    subtitleLabel->SetTextColor(Color(110, 110, 120, 255));
    subtitleLabel->SetWrap(TextWrap::WrapWord);   // 2.0.6: was being truncated to "do..."
    root->AddChild(subtitleLabel);
    yCursor += SUBTITLE_H + 8;
    
    // ---- Tabbed container with the two diagrams ----
    auto tabs = std::make_shared<UltraCanvasTabbedContainer>(
        "ndTabs", PAD, yCursor, CONTAINER_W - 2 * PAD, TABS_H);
    tabs->SetTabStyle(TabStyle::Rounded);
    
    // Inner area available to each tab content (TabbedContainer reserves ~32px
    // for the tab bar; the rest is content area).
    const int INNER_W = CONTAINER_W - 2 * PAD - 8;
    const int INNER_H = TABS_H - 40;
    
    // Pre-compute the final Y for the status label so we can construct it
    // once at the right place, without later mutation. yCursor here is the
    // top of the tabs panel; status sits below tabs + button bar.
    int statusY = yCursor + TABS_H + 8 + BTNBAR_H + 4;
    
    auto statusLabel = std::make_shared<UltraCanvasLabel>(
        "ndStatus", PAD, statusY, CONTAINER_W - 2 * PAD, STATUS_H);
    statusLabel->SetText("Ready - Click 'Add Node' or right-click on the canvas "
                         "(Custom tab) to start building. Wheel to zoom, drag to pan.");
    statusLabel->SetFontSize(10);
    statusLabel->SetTextColor(Color(90, 90, 100, 255));
    
    // ---- Friends tab (read-only example graph) ----
    auto friendsDiag = CreateFriendsNetworkDiagram(4, 4, INNER_W, INNER_H);
    auto friendsTab = std::make_shared<UltraCanvasContainer>(
        "ndTabFriends", 0, 0, INNER_W, INNER_H);
    friendsTab->SetBackgroundColor(Color(255, 255, 255, 255));
    friendsTab->AddChild(friendsDiag);
    
    // ---- Custom tab (real editor: blank canvas + popup + right-click create) ----
    auto customState = std::make_shared<CustomEditorState>();
    auto customTab = CreateCustomEditor(0, 0, INNER_W, INNER_H,
                                          customState, statusLabel);
    auto customDiag = customState->diagram;  // For activeDiagram routing
    
    tabs->AddTab("Friends", friendsTab);
    tabs->AddTab("Custom", customTab);
    tabs->SetActiveTab(0);
    
    root->AddChild(tabs);
    yCursor += TABS_H + 8;
    
    // ---- Active-diagram tracker (for button bar callbacks) ----
    auto activeDiagram = std::make_shared<std::shared_ptr<UltraCanvasNodeDiagram>>(friendsDiag);
    auto isCustomTab   = std::make_shared<bool>(false);
    
    tabs->onTabChange = [activeDiagram, isCustomTab, friendsDiag, customDiag, statusLabel]
                        (int /*oldIdx*/, int newIdx) {
        bool custom = (newIdx == 1);
        *activeDiagram = custom ? customDiag : friendsDiag;
        *isCustomTab = custom;
        if (statusLabel) {
            if (custom) {
                statusLabel->SetText("Custom - Click 'Add Node' or right-click on the "
                                      "canvas to create a node. Double-click any node "
                                      "to edit its label/color.");
            } else {
                statusLabel->SetText("Friends - Force-directed graph. "
                                      "Use mouse wheel to zoom, drag to pan.");
            }
        }
    };
    
    // ---- Button bar (Select / Connect / Reset / [Add Node | Delete | Clear All]) ----
    // 2.0.4: Add Node, Delete, Clear All are visible only on the Custom tab.
    // We update visibility in onTabChange below.
    const int BTN_W = 90;
    const int BTN_W_ADD = 100;   // "Add Node" needs a hair more
    const int BTN_W_CLEAR = 90;
    const int BTN_GAP = 8;
    int btnX = PAD;
    
    auto btnSelect = std::make_shared<UltraCanvasButton>(
        "ndBtnSelect", btnX, yCursor, BTN_W, BTNBAR_H - 4);
    btnSelect->SetText("Select");
    btnX += BTN_W + BTN_GAP;
    
    auto btnConnect = std::make_shared<UltraCanvasButton>(
        "ndBtnConnect", btnX, yCursor, BTN_W, BTNBAR_H - 4);
    btnConnect->SetText("Connect");
    btnX += BTN_W + BTN_GAP;
    
    auto btnReset = std::make_shared<UltraCanvasButton>(
        "ndBtnReset", btnX, yCursor, BTN_W, BTNBAR_H - 4);
    btnReset->SetText("Reset");
    btnX += BTN_W + BTN_GAP;
    
    auto btnAddNode = std::make_shared<UltraCanvasButton>(
        "ndBtnAddNode", btnX, yCursor, BTN_W_ADD, BTNBAR_H - 4);
    btnAddNode->SetText("Add Node");
    btnAddNode->SetBackgroundColor(Color(80, 160, 80, 255));
    btnX += BTN_W_ADD + BTN_GAP;
    
    auto btnDelete = std::make_shared<UltraCanvasButton>(
        "ndBtnDelete", btnX, yCursor, BTN_W, BTNBAR_H - 4);
    btnDelete->SetText("Delete");
    btnDelete->SetBackgroundColor(Color(220, 90, 90, 255));
    btnX += BTN_W + BTN_GAP;
    
    auto btnClearAll = std::make_shared<UltraCanvasButton>(
        "ndBtnClearAll", btnX, yCursor, BTN_W_CLEAR, BTNBAR_H - 4);
    btnClearAll->SetText("Clear All");
    btnClearAll->SetBackgroundColor(Color(160, 90, 130, 255));
    
    // Custom-only buttons start hidden (Friends is the initial tab)
    btnAddNode->SetVisible(false);
    btnDelete->SetVisible(false);
    btnClearAll->SetVisible(false);
    
    // ---- Wire up button callbacks ----
    
    // Select: deselects everything and sets Custom mode back to Select.
    btnSelect->onClick = [activeDiagram, customState, statusLabel]() {
        if (auto d = *activeDiagram) {
            d->DeselectAll();
        }
        // Reset Custom-mode state too (so Connect doesn't linger)
        if (customState) {
            customState->mode = CustomMode::Select;
            customState->connectFirstNodeId.clear();
        }
        if (statusLabel) {
            statusLabel->SetText("Select mode - Click nodes/links to select. "
                                  "Shift+click toggles. Delete key removes.");
        }
    };
    
    // Connect: enters Connect mode in Custom. In Friends it's just a hint
    // because Friends nodes have handles for drag-to-connect.
    btnConnect->onClick = [activeDiagram, customState, isCustomTab, statusLabel]() {
        if (auto d = *activeDiagram) {
            d->DeselectAll();
        }
        if (*isCustomTab && customState) {
            customState->mode = CustomMode::Connect;
            customState->connectFirstNodeId.clear();
            if (statusLabel) {
                statusLabel->SetText("Connect mode (Custom) - Click first node, then "
                                      "click second node to create a link. Esc / Select "
                                      "to exit.");
            }
        } else if (statusLabel) {
            statusLabel->SetText("Connect (Friends) - Drag from a handle (the small "
                                  "circles on a node) to another to create a link. "
                                  "Switch to Custom for click-A-then-B mode.");
        }
    };
    
    btnReset->onClick = [activeDiagram, isCustomTab, statusLabel]() {
        if (auto d = *activeDiagram) {
            d->DeselectAll();
            d->SetZoomLevel(1.0f);
            d->SetPanOffset(0.0f, 0.0f);
            // Friends: re-run layout (the upgraded 2.0.4 force-directed gives
            // clean, non-overlapping results every call).
            // Custom: only re-fit; preserves user's nodes (Clear All wipes).
            if (!*isCustomTab) {
                d->RunLayout();
                statusLabel->SetText("Reset - Layout re-run and view fitted.");
            } else {
                d->FitView();
                statusLabel->SetText("Reset - View re-fitted to content.");
            }
        }
    };
    
    btnAddNode->onClick = [customState, isCustomTab, tabs, statusLabel]() {
        // If somehow on Friends (button shouldn't be visible there) switch first
        if (!*isCustomTab) {
            tabs->SetActiveTab(1);
            *isCustomTab = true;
        }
        if (!customState || !customState->diagram) return;
        
        // Place the new node at the center of the current viewport.
        // ScreenToWorld is private, so we replicate its math with public API:
        //   world = (screen - bounds - pan) / zoom
        auto d = customState->diagram;
        float zoom = d->GetZoomLevel();
        Point2Df pan = d->GetPanOffset();
        float screenCx = static_cast<float>(d->GetX() + d->GetWidth()  / 2);
        float screenCy = static_cast<float>(d->GetY() + d->GetHeight() / 2);
        float worldCx = (screenCx - d->GetX() - pan.x) / zoom;
        float worldCy = (screenCy - d->GetY() - pan.y) / zoom;
        
        std::ostringstream idStream;
        idStream << "node_" << (++customState->nodeCounter);
        std::string newId = idStream.str();
        std::string defaultLabel = "Node " + std::to_string(customState->nodeCounter);
        
        // 2.0.4: auto-size to fit label
        double w = 60.0, h = 60.0;
        d->SuggestNodeSizeForLabel(defaultLabel, 11.0f, 60.0f, w, h);
        float side = static_cast<float>(std::max(w, h));
        
        NodeDiagramNode n(newId, defaultLabel);
        n.shape = NodeShape::Circle;
        n.x = worldCx - side * 0.5f;
        n.y = worldCy - side * 0.5f;
        n.width = side;
        n.height = side;
        n.fillColor = customState->selectedColor;
        n.borderColor = DeriveBorderColor(customState->selectedColor);
        d->AddNode(n);
        // 2.0.4: NO AddDefaultHandles - cleaner look in Custom
        
        OpenCustomEditorPopup(customState, newId, n.label, n.fillColor, /*isNewNode=*/true);
        if (statusLabel) {
            statusLabel->SetText("Node added at viewport center - "
                                  "Edit label/color and click OK; "
                                  "Cancel will remove it.");
        }
    };
    
    // Delete: removes whatever is selected (nodes and/or links). Active only
    // on Custom (the Custom diagram). Friends is read-only.
    btnDelete->onClick = [customState, statusLabel]() {
        if (!customState || !customState->diagram) return;
        auto d = customState->diagram;
        auto selectedNodeIds = d->GetSelectedNodeIds();
        auto selectedLinkIds = d->GetSelectedLinkIds();
        if (selectedNodeIds.empty() && selectedLinkIds.empty()) {
            if (statusLabel) {
                statusLabel->SetText("Delete - Nothing selected. Click a node or link "
                                      "first, then Delete.");
            }
            return;
        }
        d->DeleteSelected();
        if (statusLabel) {
            std::ostringstream s;
            s << "Deleted " << selectedNodeIds.size() << " node(s) and "
              << selectedLinkIds.size() << " link(s).";
            statusLabel->SetText(s.str());
        }
    };
    
    // Clear All: wipes the Custom canvas. Asks via status (no confirm dialog
    // because we don't want to introduce another widget for this demo).
    btnClearAll->onClick = [customState, statusLabel]() {
        if (!customState || !customState->diagram) return;
        customState->diagram->Clear();
        customState->nodeCounter = 0;
        customState->linkCounter = 0;
        customState->editingNodeId.clear();
        customState->connectFirstNodeId.clear();
        if (customState->popup) customState->popup->SetVisible(false);
        if (statusLabel) {
            statusLabel->SetText("Cleared - Canvas is empty. Use Add Node or "
                                  "right-click to start a new diagram.");
        }
    };
    
    root->AddChild(btnSelect);
    root->AddChild(btnConnect);
    root->AddChild(btnReset);
    root->AddChild(btnAddNode);
    root->AddChild(btnDelete);
    root->AddChild(btnClearAll);
    yCursor += BTNBAR_H;
    
    // 2.0.4: Toggle button visibility per tab. Override the earlier
    // onTabChange that only updated activeDiagram and status.
    auto baseOnTabChange = tabs->onTabChange;
    tabs->onTabChange = [baseOnTabChange, btnAddNode, btnDelete, btnClearAll]
                        (int oldIdx, int newIdx) {
        if (baseOnTabChange) baseOnTabChange(oldIdx, newIdx);
        bool custom = (newIdx == 1);
        if (btnAddNode)  btnAddNode->SetVisible(custom);
        if (btnDelete)   btnDelete->SetVisible(custom);
        if (btnClearAll) btnClearAll->SetVisible(custom);
    };
    
    // ---- Status bar ----
    root->AddChild(statusLabel);
    
    return root;
}

} // namespace UltraCanvas