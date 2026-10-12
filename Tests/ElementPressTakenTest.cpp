// Tests/ElementPressTakenTest.cpp
// An element takes the presses it acts on.
//
// A press an element does not take climbs to the elements around it. So an
// element that acted on a press - cleared its selection, took the focus,
// started a pan - and then reported it as not taken handed the same press to
// its parents, which could act on it a second time. Each element here sits in
// a container, gets the press it acts on through the application's own
// dispatch, and the container must not be handed it. The other side is
// checked too: a press an element has no use for (a middle press on the curve
// editor) still goes on, without the element taking the focus first.
//
// Also: the arc diagram hit-tests in its own local space (it subtracted its
// position in the parent from a pointer that was already local, so a diagram
// away from its parent's corner could not be clicked), and a movable toolbar
// is dragged by its own surface only, in window coordinates, with the mouse
// captured.
//
// Opens a real window, so it runs under Xvfb (xvfb-run -a) and skips itself
// without a DISPLAY.
// Version: 1.0.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasCurveEditor.h"
#include "UltraCanvasGradientEditor.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasWindow.h"
#include "Plugins/Charts/UltraCanvasGanttChart.h"
#include "Plugins/Charts/UltraCanvasKanbanBoard.h"
#include "Plugins/Diagrams/UltraCanvasAdjacencyDiagram.h"
#include "Plugins/Diagrams/UltraCanvasArcDiagram.h"
#include "Plugins/Diagrams/UltraCanvasBlockDiagram.h"
#include "Plugins/Diagrams/UltraCanvasGourceTree.h"
#include "Plugins/Diagrams/UltraCanvasTreeMapElement.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

#define SKIP_ALL(reason)                                                      \
    do {                                                                      \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

// The elements a press was handed to, in order (the window's event filter
// sees every dispatch).
std::vector<std::string> handedTo;

struct Harness {
    UltraCanvasApplication& app;
    std::shared_ptr<UltraCanvasWindow> window;

    // A press at `local` (element coordinates) of `element`.
    void Press(const UltraCanvasUIElement& element, Point2Di local,
               UCEventType type = UCEventType::MouseDown,
               UCMouseButton button = UCMouseButton::Left) {
        const Rect2Df b = element.GetBoundsInWindow();
        PressAt(Point2Di(static_cast<int>(b.x) + local.x, static_cast<int>(b.y) + local.y),
                type, button);
    }

    void PressAt(Point2Di inWindow, UCEventType type = UCEventType::MouseDown,
                 UCMouseButton button = UCMouseButton::Left) {
        UCEvent e;
        e.type = type;
        e.button = button;
        e.targetWindow = window;
        e.nativeWindowHandle = window->GetNativeHandle();
        e.pointerWindow = inWindow;
        e.pointer = inWindow;
        handedTo.clear();
        app.DispatchEvent(e);
    }

    void MoveTo(Point2Di inWindow) { PressAt(inWindow, UCEventType::MouseMove, UCMouseButton::NoneButton); }

    void Frame(const std::shared_ptr<UltraCanvasUIElement>& root) {
        for (int i = 0; i < 2; ++i) {
            root->RequestRedraw();
            window->UpdateAndRender();
        }
    }
};

bool HandedTo(const std::string& id) {
    return std::find(handedTo.begin(), handedTo.end(), id) != handedTo.end();
}

Point2Di Centre(const UltraCanvasUIElement& e) {
    return Point2Di(static_cast<int>(e.GetWidth() / 2), static_cast<int>(e.GetHeight() / 2));
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Element Press Taken Suite"            << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("ElementPressTakenTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "ElementPressTakenTest";
    cfg.width = 1000;
    cfg.height = 700;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();   // UpdateAndRender() is a no-op on an unmapped window
    DisplayTest::ActivateWindow(app, window);   // so focus changes show

    window->InstallEventFilter("record", [](const UCEvent& e) {
        if (e.targetElement) handedTo.push_back(e.targetElement->GetIdentifier());
        return false;   // only watching
    }, {UCEventType::MouseDown, UCEventType::MouseUp, UCEventType::MouseDoubleClick});

    Harness h{app, window};
    // Every element sits in a container of its own, which takes nothing: a
    // press that reaches it was not taken by the element.
    auto root = CreateContainer("root", 0, 0, 1000, 700);
    window->AddChild(root);
    auto Holder = [&root](const std::string& id, float x, float y, float w, float h) {
        auto box = CreateContainer(id, x, y, w, h);
        root->AddChild(box);
        return box;
    };

    // ----- Selection cleared on the background -----
    auto kanbanBox = Holder("kanbanBox", 0, 0, 240, 160);
    auto kanban = CreateKanbanBoardElement("kanban", 0, 0, 240, 160);
    kanbanBox->AddChild(kanban);
    auto ganttBox = Holder("ganttBox", 250, 0, 240, 160);
    auto gantt = CreateGanttChartElement("gantt", 0, 0, 240, 160);
    ganttBox->AddChild(gantt);
    auto blockBox = Holder("blockBox", 500, 0, 240, 160);
    auto block = CreateBlockDiagram("block", 0, 0, 240, 160);
    blockBox->AddChild(block);
    auto gourceBox = Holder("gourceBox", 750, 0, 240, 160);
    auto gource = CreateGourceTree("gource", 0, 0, 240, 160);
    gourceBox->AddChild(gource);
    auto treeBox = Holder("treeBox", 0, 170, 240, 160);
    auto tree = CreateTreeMap("tree", 0, 0, 240, 160);
    treeBox->AddChild(tree);
    h.Frame(root);

    h.Press(*kanban, Centre(*kanban));
    TEST("a press on the kanban board's background is the board's", !HandedTo("kanbanBox"));
    h.Press(*kanban, Centre(*kanban), UCEventType::MouseUp);
    h.Press(*gantt, Centre(*gantt));
    TEST("a press on the Gantt chart off its rows is the chart's", !HandedTo("ganttBox"));
    h.Press(*gantt, Centre(*gantt), UCEventType::MouseUp);
    h.Press(*gantt, Centre(*gantt), UCEventType::MouseDoubleClick);
    TEST("and so is a double-click there", !HandedTo("ganttBox"));
    h.Press(*gantt, Centre(*gantt), UCEventType::MouseUp);
    h.Press(*block, Centre(*block));
    TEST("a press on the block diagram's empty canvas is the diagram's", !HandedTo("blockBox"));
    h.Press(*block, Centre(*block), UCEventType::MouseUp);
    h.Press(*gource, Centre(*gource));
    TEST("a press on the Gource tree's empty space is the tree's", !HandedTo("gourceBox"));
    h.Press(*gource, Centre(*gource), UCEventType::MouseUp);
    h.Press(*tree, Centre(*tree), UCEventType::MouseDoubleClick);
    TEST("a double-click on the tree map's background is the map's", !HandedTo("treeBox"));
    h.Press(*tree, Centre(*tree), UCEventType::MouseUp);

    // ----- Arc diagram: hit-tested where it is drawn -----
    // Further into its parent than it is wide: subtracting that offset from
    // a local pointer put every click outside the diagram.
    auto arcBox = Holder("arcBox", 250, 170, 500, 160);
    auto arc = std::make_shared<UltraCanvasArcDiagram>("arc", 290, 10, 200, 140);
    arcBox->AddChild(arc);
    arc->AddNode("a", "A");
    arc->AddNode("b", "B");
    arc->AddNode("c", "C");
    int nodeClicks = 0;
    arc->onNodeClick = [&nodeClicks](int, const ArcNode&) { ++nodeClicks; };
    h.Frame(root);
    bool pressesStayed = true;
    for (int y = 2; y < 140 && nodeClicks == 0; y += 3) {
        for (int x = 2; x < 200 && nodeClicks == 0; x += 3) {
            h.Press(*arc, Point2Di(x, y));
            if (HandedTo("arcBox")) pressesStayed = false;
            h.Press(*arc, Point2Di(x, y), UCEventType::MouseUp);
            if (HandedTo("arcBox")) pressesStayed = false;
        }
    }
    TEST("a click where an arc-diagram node is drawn selects it", nodeClicks > 0);
    TEST("every press and release on the arc diagram is the diagram's", pressesStayed);

    // ----- Adjacency diagram: the empty area -----
    auto adjacencyBox = Holder("adjacencyBox", 760, 170, 230, 160);
    auto adjacency = std::make_shared<UltraCanvasAdjacencyDiagram>("adjacency", 0, 0, 230, 160);
    adjacencyBox->AddChild(adjacency);
    adjacency->AddRoom("r", "Room", 12.0f, 115.0f, 80.0f);
    h.Frame(root);
    h.Press(*adjacency, Point2Di(3, 3));
    const bool downStayed = !HandedTo("adjacencyBox");
    h.Press(*adjacency, Point2Di(3, 3), UCEventType::MouseUp);
    TEST("a click on the adjacency diagram's empty area is the diagram's",
         downStayed && !HandedTo("adjacencyBox"));

    // ----- Editors: the focus, and the buttons they have no use for -----
    auto gradientBox = Holder("gradientBox", 0, 340, 240, 80);
    auto gradient = CreateGradientEditor("gradient", 0, 0, 240, 80);
    gradientBox->AddChild(gradient);
    auto curveBox = Holder("curveBox", 250, 340, 200, 200);
    auto curve = std::make_shared<UltraCanvasCurveEditor>("curve", 0, 0, 200, 200);
    curveBox->AddChild(curve);
    h.Frame(root);
    h.Press(*gradient, Point2Di(2, 2));
    TEST("a left press anywhere on the gradient editor is the editor's", !HandedTo("gradientBox"));
    h.Press(*gradient, Point2Di(2, 2), UCEventType::MouseUp);
    h.Press(*curve, Centre(*curve), UCEventType::MouseDown, UCMouseButton::Middle);
    TEST("a middle press on the curve editor goes on to its container", HandedTo("curveBox"));
    TEST("without the curve editor taking the focus", !curve->IsFocused());
    h.Press(*curve, Centre(*curve), UCEventType::MouseUp, UCMouseButton::Middle);

    // ----- A movable toolbar -----
    auto bar = std::make_shared<UltraCanvasToolbar>("bar", 500, 360, 300, 40);
    root->AddChild(bar);
    bar->SetDragMode(ToolbarDragMode::Movable);
    auto barLabel = bar->AddLabel("barLabel", "Label");
    h.Frame(root);
    const Rect2Df barAt = bar->GetBounds();
    const Rect2Df labelAt = barLabel->GetBoundsInWindow();
    const Point2Di onLabel(static_cast<int>(labelAt.x + labelAt.width / 2),
                           static_cast<int>(labelAt.y + labelAt.height / 2));
    // A frame after each move, as the event loop draws one: the bar's place
    // is laid out there.
    h.PressAt(onLabel);
    h.MoveTo(Point2Di(onLabel.x + 40, onLabel.y + 30));
    h.Frame(root);
    h.PressAt(Point2Di(onLabel.x + 40, onLabel.y + 30), UCEventType::MouseUp);
    h.Frame(root);
    TEST("a press on a toolbar's label does not carry the bar off",
         bar->GetBounds().x == barAt.x && bar->GetBounds().y == barAt.y);

    // The bar's own surface: its right end, past the label.
    const Rect2Df barInWindow = bar->GetBoundsInWindow();
    const Point2Di onBar(static_cast<int>(barInWindow.x + barInWindow.width - 6),
                         static_cast<int>(barInWindow.y + barInWindow.height / 2));
    h.PressAt(onBar);
    h.MoveTo(Point2Di(onBar.x + 30, onBar.y + 20));
    h.Frame(root);
    h.MoveTo(Point2Di(onBar.x + 60, onBar.y + 120));   // well off the bar
    h.Frame(root);
    TEST("a press on the bar's own surface drags it, step for step with the pointer",
         bar->GetBounds().x == barAt.x + 60 && bar->GetBounds().y == barAt.y + 120);
    h.PressAt(Point2Di(onBar.x + 60, onBar.y + 120), UCEventType::MouseUp);
    h.MoveTo(Point2Di(onBar.x + 90, onBar.y + 150));
    h.Frame(root);
    TEST("the release ends the drag, off the bar as well",
         bar->GetBounds().x == barAt.x + 60 && bar->GetBounds().y == barAt.y + 120);

    std::cerr << std::endl << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
