// Tests/DiagramJsonLocaleTest.cpp
// The node and compositor diagrams write their JSON - the saved diagram, and
// the compositor's clipboard copy - dot-decimal whatever locale the host runs
// in. A stream follows the C++ global locale, so a host that sets it
// (std::locale::global) turned "zoom": 1.5 into "zoom": 1,5 - which no JSON
// reader takes - until the writers pinned their streams to the classic
// locale. Each diagram's JSON is written under "C" and under a comma-decimal
// locale and must be the same text. Skipped where no such locale is installed.
// Headless: the elements are built and serialised, never shown.
// Version: 1.0.0
// Last Modified: 2026-10-15
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

int main() {
    TestNodeDiagram();
    TestCompositorDiagram();
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
