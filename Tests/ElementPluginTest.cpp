// Tests/ElementPluginTest.cpp
// Unit tests for the element plugin system: registry semantics, create-by-name,
// keyword-dispatched text creation, the named-property configuration surface,
// the ABI handshake, and a real end-to-end DSO load through the host vtable.
//
// Usage: ElementPluginTest [pluginDirectory]
// The DSO tests run only when a directory containing the two test plugins
// (TestElementPlugin, TestRefusingPlugin) is passed; the CMake registration
// builds them and passes the directory.
//
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework

#include "UltraCanvasElementPlugins.h"
#include "ElementPluginTestSupport.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }           \
    } while (0)

// =============================================================================
// Helpers
// =============================================================================

static UCElementDescriptor MakeWidgetDescriptor(const std::string& typeName,
                                                UCPluginCategory category) {
    UCElementDescriptor d;
    d.typeName = typeName;
    d.category = category;
    d.displayName = typeName;
    d.create = [typeName](const std::string& id, int x, int y, int w, int h) {
        auto widget = std::make_shared<TestWidget>(id, x, y, w, h);
        widget->tag = typeName;
        return std::static_pointer_cast<UltraCanvasUIElement>(widget);
    };
    return d;
}

// =============================================================================

static void TestRegistryBasics() {
    std::printf("Registry (register, find, create, list, unregister)\n");
    UltraCanvasElementRegistry::Clear();

    UltraCanvasElementRegistry::Register(MakeWidgetDescriptor("kanban-board",
                                                              UCPluginCategory::Diagram));
    UltraCanvasElementRegistry::Register(MakeWidgetDescriptor("parallel-coordinate-chart",
                                                              UCPluginCategory::Chart));

    CHECK(UltraCanvasElementRegistry::IsRegistered("kanban-board"),
          "a registered type is found");
    CHECK(!UltraCanvasElementRegistry::IsRegistered("missing-type"),
          "an unknown type is not");

    const UCElementDescriptor* found =
        UltraCanvasElementRegistry::Find("parallel-coordinate-chart");
    CHECK(found && found->category == UCPluginCategory::Chart,
          "Find returns the descriptor with its category");

    auto element = UltraCanvasElementRegistry::Create("kanban-board", "k1", 10, 20, 300, 200);
    CHECK(element != nullptr, "Create by name returns an instance");
    CHECK(element && element->TypeTag() == "kanban-board",
          "the instance came from the right factory");
    CHECK(element && element->id == "k1" && element->width == 300,
          "constructor arguments pass through");

    CHECK(UltraCanvasElementRegistry::Create("missing-type", "m", 0, 0, 1, 1) == nullptr,
          "an unknown type creates nothing");
    CHECK(UltraCanvas_GetLastPluginError().find("missing-type") != std::string::npos,
          "the miss is explained in GetLastPluginError");

    CHECK(UltraCanvasElementRegistry::List(UCPluginCategory::Chart).size() == 1,
          "List filters by category");
    CHECK(UltraCanvasElementRegistry::ListAll().size() == 2, "ListAll sees everything");

    // Duplicate registration is ignored, keeping the first factory.
    UCElementDescriptor duplicate = MakeWidgetDescriptor("kanban-board",
                                                         UCPluginCategory::Widget);
    UltraCanvasElementRegistry::Register(duplicate);
    const UCElementDescriptor* still = UltraCanvasElementRegistry::Find("kanban-board");
    CHECK(still && still->category == UCPluginCategory::Diagram,
          "re-registering a live typeName is ignored");

    UltraCanvasElementRegistry::Unregister("kanban-board");
    CHECK(!UltraCanvasElementRegistry::IsRegistered("kanban-board"),
          "Unregister removes the type");
    CHECK(UltraCanvasElementRegistry::ListAll().size() == 1,
          "the other registration survives");

    // Invalid descriptors are rejected, not stored.
    UCElementDescriptor broken;
    broken.typeName = "broken";      // no create factory
    UltraCanvasElementRegistry::Register(broken);
    CHECK(!UltraCanvasElementRegistry::IsRegistered("broken"),
          "a descriptor without a factory is rejected");
}

static void TestCreateFromText() {
    std::printf("CreateFromText (keyword dispatch)\n");
    UltraCanvasElementRegistry::Clear();

    UCElementDescriptor d = MakeWidgetDescriptor("kanban-board", UCPluginCategory::Diagram);
    d.textKeywords = {"kanban"};
    d.createFromText = [](const std::string& id, int x, int y, int w, int h,
                          const std::string& text, std::string*) {
        auto widget = std::make_shared<TestWidget>(id, x, y, w, h);
        widget->tag = "kanban-board";
        widget->textPayload = text;
        return std::static_pointer_cast<UltraCanvasUIElement>(widget);
    };
    UltraCanvasElementRegistry::Register(d);

    std::string error;
    auto board = UltraCanvasElementRegistry::CreateFromText(
        "  kanban\n  column: Todo\n", "b1", 0, 0, 400, 300, &error);
    CHECK(board != nullptr, "the first word of the definition selects the element");
    auto widget = std::dynamic_pointer_cast<TestWidget>(board);
    CHECK(widget && widget->textPayload.find("column: Todo") != std::string::npos,
          "the full definition reaches the text loader");

    CHECK(UltraCanvasElementRegistry::CreateFromText("KANBAN x", "b2", 0, 0, 10, 10) != nullptr,
          "keyword matching is case-insensitive");

    CHECK(UltraCanvasElementRegistry::CreateFromText("gantt x", "b3", 0, 0, 10, 10,
                                                     &error) == nullptr &&
              !error.empty(),
          "an unknown keyword fails with an explanation");
    CHECK(UltraCanvasElementRegistry::CreateFromText("   ", "b4", 0, 0, 10, 10,
                                                     &error) == nullptr,
          "an empty definition fails cleanly");
}

static void TestMissingTypeResolver() {
    std::printf("Missing-type resolver (the lazy-load hook)\n");
    UltraCanvasElementRegistry::Clear();

    int resolverCalls = 0;
    UltraCanvasElementRegistry::SetMissingTypeResolver(
        [&resolverCalls](const std::string& typeName) {
            ++resolverCalls;
            if (typeName != "late-widget") return false;
            // Simulate a DSO registering the type during the resolve.
            UltraCanvasElementRegistry::Register(
                MakeWidgetDescriptor("late-widget", UCPluginCategory::Widget));
            return true;
        });

    auto element = UltraCanvasElementRegistry::Create("late-widget", "l1", 0, 0, 50, 50);
    CHECK(resolverCalls == 1, "a miss consults the resolver once");
    CHECK(element && element->TypeTag() == "late-widget",
          "the retry after the resolver succeeds");

    auto second = UltraCanvasElementRegistry::Create("late-widget", "l2", 0, 0, 50, 50);
    CHECK(resolverCalls == 1 && second != nullptr,
          "a hit never consults the resolver");

    CHECK(UltraCanvasElementRegistry::Create("never-appears", "n", 0, 0, 1, 1) == nullptr &&
              resolverCalls == 2,
          "a failed resolve stays a miss");

    UltraCanvasElementRegistry::SetMissingTypeResolver(nullptr);
}

static void TestPropertySurface() {
    std::printf("Named properties (typed configuration across a boundary)\n");
    UltraCanvasElementRegistry::Clear();

    UCElementDescriptor d = MakeWidgetDescriptor("configurable", UCPluginCategory::Widget);
    d.propertyKeys = {"columns", "title", "compact"};
    UltraCanvasElementRegistry::Register(d);

    const UCElementDescriptor* desc = UltraCanvasElementRegistry::Find("configurable");
    CHECK(desc && desc->propertyKeys.size() == 3,
          "the descriptor advertises its property keys");

    auto element = UltraCanvasElementRegistry::Create("configurable", "c1", 0, 0, 10, 10);
    auto configurable = std::dynamic_pointer_cast<IConfigurableElement>(element);
    CHECK(configurable != nullptr,
          "the base-class handle reaches the property surface via dynamic cast");
    if (!configurable) return;

    CHECK(configurable->SetProperty("columns", static_cast<int64_t>(5)),
          "a known key accepts a value");
    double columns = 0.0;
    CHECK(UCPropertyAsDouble(configurable->GetProperty("columns"), columns) && columns == 5.0,
          "the value reads back (with numeric conversion)");

    CHECK(configurable->SetProperty("title", std::string("Sprint 12")),
          "string properties work");
    std::string title;
    CHECK(UCPropertyAsString(configurable->GetProperty("title"), title) &&
              title == "Sprint 12",
          "and read back");

    CHECK(!configurable->SetProperty("no-such-key", 1.0),
          "an unknown key is rejected, not silently dropped");
    CHECK(!UCPropertyHasValue(configurable->GetProperty("no-such-key")),
          "reading an unknown key yields no value");
    CHECK(configurable->ListProperties().size() == 3, "ListProperties matches");
}

static void TestAbiContract() {
    std::printf("ABI contract\n");

    CHECK(ULTRACANVAS_PLUGIN_HOST_ABI_VERSION == 1, "the entry contract is at version 1");
    const std::string tag = ULTRACANVAS_FRAMEWORK_ABI_TAG;
    CHECK(!tag.empty() && tag.find("cxx20-uc1") != std::string::npos,
          "the ABI tag carries the standard and descriptor version");
    CHECK(tag.find("gcc") != std::string::npos || tag.find("clang") != std::string::npos ||
              tag.find("msvc") != std::string::npos,
          "the ABI tag names the compiler family");
}

static void TestDsoEndToEnd(const std::string& pluginDirectory) {
    std::printf("DSO end-to-end (load, register through the host vtable, refuse)\n");
    UltraCanvasElementRegistry::Clear();

    CHECK(UltraCanvas_AddPluginDirectory(pluginDirectory),
          "the plugin directory exists");
    CHECK(!UltraCanvasElementRegistry::IsRegistered("plugin-widget"),
          "nothing is loaded before it is needed");

    // No explicit refresh: the registry miss triggers the on-demand load.
    auto element = UltraCanvasElementRegistry::Create("plugin-widget", "p1", 5, 5, 200, 100);
    CHECK(element != nullptr, "a registry miss loads the plugin on demand");
    CHECK(element && element->TypeTag() == "plugin-widget",
          "the instance was built inside the DSO");

    auto configurable = std::dynamic_pointer_cast<IConfigurableElement>(element);
    CHECK(configurable && configurable->SetProperty("columns", static_cast<int64_t>(7)),
          "a DSO-created element is configurable across the boundary");

    const UCElementDescriptor* desc = UltraCanvasElementRegistry::Find("plugin-widget");
    CHECK(desc && desc->version == "1.0.0" && !desc->propertyKeys.empty(),
          "the descriptor registered by the DSO is complete");

    auto fromText = UltraCanvasElementRegistry::CreateFromText(
        "pluginboard some payload", "p2", 0, 0, 50, 50);
    CHECK(fromText != nullptr, "text keywords registered by the DSO dispatch");

    // The refusing plugin was in the same directory: skipped, nothing registered.
    CHECK(UltraCanvasElementRegistry::ListAll().size() == 1,
          "the refusing plugin registered nothing");

    CHECK(UltraCanvas_RefreshElementPlugins() == 0,
          "a second refresh loads nothing new (idempotent)");
}

// =============================================================================

int main(int argc, char** argv) {
    std::printf("=== ElementPluginTest ===\n\n");

    TestRegistryBasics();
    TestCreateFromText();
    TestMissingTypeResolver();
    TestPropertySurface();
    TestAbiContract();

    if (argc > 1) {
        TestDsoEndToEnd(argv[1]);
    } else {
        std::printf("DSO end-to-end: SKIPPED (no plugin directory argument)\n");
    }

    std::printf("\n%s (%d failure%s)\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
