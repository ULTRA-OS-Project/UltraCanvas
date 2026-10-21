// Tests/DiagramJsonLocaleTest.cpp
// The node and compositor diagrams write their JSON - the saved diagram, and
// the compositor's clipboard copy - dot-decimal whatever locale the host runs
// in. A stream follows the C++ global locale, so a host that sets it
// (std::locale::global) turned "zoom": 1.5 into "zoom": 1,5 - which no JSON
// reader takes - until the writers pinned their streams to the classic
// locale. Each diagram's JSON is written under "C" and under a comma-decimal
// locale and must be the same text. Skipped where no such locale is installed.
// Headless: the elements are built and serialised, never shown.
// Also: a compositor graph saved with ToJson loads back with FromJson whole
// (a child's parentId, a link's label and colours), and Ungroup leaves an
// ordinary node alone.
// Version: 1.1.0 - compositor save / load round trip; Ungroup of a non-group node
// Version: 1.0.0
// Last Modified: 2026-10-16
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasNodeDiagram.h"
#include "Plugins/Diagrams/UltraCanvasCompositorDiagram.h"

#include <clocale>
#include <cstdio>
#include <exception>
#include <functional>
#include <locale>
#include <memory>
#include <string>

using namespace UltraCanvas;

static int failures = 0;
static void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// `write` under "C", then under a comma-decimal locale - the C library's and
// the C++ global one. False when none is installed.
static bool WriteInBothLocales(const std::function<std::string()>& write,
                               std::string& inC, std::string& inComma) {
    const char* original = std::setlocale(LC_ALL, nullptr);
    const std::string saved = original ? original : "C";
    const std::locale savedGlobal;
    inC = write();
    for (const char* candidate : {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "de_DE"}) {
        if (!std::setlocale(LC_ALL, candidate)) continue;
        try {
            std::locale::global(std::locale(candidate));
        } catch (const std::exception&) {
            continue;
        }
        inComma = write();
        std::locale::global(savedGlobal);
        std::setlocale(LC_ALL, saved.c_str());
        return true;
    }
    std::setlocale(LC_ALL, saved.c_str());
    std::printf("  [SKIP] no comma-decimal locale installed\n");
    return false;
}

static void TestNodeDiagram() {
    std::printf("Node diagram\n");
    auto diagram = std::make_shared<UltraCanvasNodeDiagram>("nodes", 0, 0, 800, 600);
    diagram->SetZoomLevel(1.5);
    diagram->AddNode("a", "A", 10.25, 20.5);
    std::string inC, inComma;
    if (!WriteInBothLocales([&]() { return diagram->ToJson(); }, inC, inComma)) return;
    Check(inC.find("1.5") != std::string::npos && inC.find("10.25") != std::string::npos,
          "the JSON holds the zoom and the node's position");
    Check(inComma == inC, "the JSON is the same under a comma-decimal locale");
}

static void TestCompositorDiagram() {
    std::printf("Compositor diagram\n");
    auto diagram = std::make_shared<UltraCanvasCompositorDiagram>("compositor", 0, 0, 800, 600);
    CompositorNodeTemplate shape;
    shape.id = "box";
    shape.title = "Box";
    diagram->RegisterTemplate(shape);
    diagram->SetZoomLevel(1.5);
    CompositorNode node;
    node.id = "n1";
    node.templateId = "box";
    node.x = 10.25;
    node.y = 20.5;
    diagram->AddNode(node);
    std::string inC, inComma;
    if (!WriteInBothLocales([&]() { return diagram->ToJson(); }, inC, inComma)) return;
    Check(inC.find("1.5") != std::string::npos && inC.find("10.25") != std::string::npos,
          "the JSON holds the zoom and the node's position");
    Check(inComma == inC, "the JSON is the same under a comma-decimal locale");
}

// Before 0.4.1 ToJson wrote no parentId and no link colours or label, so a
// saved subgraph came back with its children at the top level.
static void TestCompositorRoundTrip() {
    std::printf("Compositor save / load\n");
    auto make = []() {
        auto d = std::make_shared<UltraCanvasCompositorDiagram>("compositor", 0, 0, 800, 600);
        CompositorNodeTemplate box;
        box.id = "box";
        box.title = "Box";
        CompositorSocketSpec in, out;
        in.id = "in";   in.label = "In";   in.type = SocketDataType::Scalar;
        out.id = "out"; out.label = "Out"; out.type = SocketDataType::Scalar;
        box.inputs.push_back(in);
        box.outputs.push_back(out);
        d->RegisterTemplate(box);
        d->RegisterDefaultGroupTemplate();
        return d;
    };
    auto saved = make();
    saved->AddNode("g", "group", 0, 0);
    saved->AddNode("a", "box", 10, 10);
    saved->AddNode("b", "box", 300, 10);
    saved->SetNodeParent("a", "g");
    CompositorLink link;
    link.id = "l1";
    link.sourceNodeId = "a";
    link.sourceSocketId = "out";
    link.targetNodeId = "b";
    link.targetSocketId = "in";
    link.label = "r = 0.8";
    link.lineColorOverride = Color(10, 20, 30, 255);
    link.labelBgColor = Color(1, 2, 3, 4);
    link.labelTextColor = Color(5, 6, 7, 8);
    saved->AddLink(link);

    auto loaded = make();
    Check(loaded->FromJson(saved->ToJson()), "FromJson accepts ToJson's output");
    Check(loaded->GetNodeParent("a") == "g", "a child keeps its group (parentId)");
    const CompositorLink* back = loaded->GetLink("l1");
    Check(back != nullptr, "the link is loaded");
    if (back) {
        Check(back->label == "r = 0.8", "the link keeps its label");
        Check(back->lineColorOverride == Color(10, 20, 30, 255), "the link keeps its line colour");
        Check(back->labelBgColor == Color(1, 2, 3, 4) && back->labelTextColor == Color(5, 6, 7, 8),
              "the link keeps its label colours");
    }

    // Ungroup on an ordinary childless node took the "empty group" branch
    // and deleted the node.
    loaded->Ungroup("b");
    Check(loaded->GetNode("b") != nullptr, "Ungroup leaves an ordinary node alone");
    loaded->Ungroup("g");
    Check(loaded->GetNode("g") == nullptr && loaded->GetNodeParent("a").empty(),
          "Ungroup of a group removes it and promotes its child");
}

int main() {
    TestNodeDiagram();
    TestCompositorDiagram();
    TestCompositorRoundTrip();
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
