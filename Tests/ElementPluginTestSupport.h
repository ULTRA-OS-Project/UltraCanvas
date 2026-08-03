// Tests/ElementPluginTestSupport.h
// Shared support for the element plugin tests: a minimal stand-in for
// UltraCanvasUIElement plus a configurable test widget.
//
// The registry only ever forward-declares UltraCanvasUIElement, so these tests
// can run without the UI stack by supplying their own definition. This is safe
// ONLY because the test binaries never link the real UltraCanvas library -
// do not include this header anywhere that does.
//
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasElementProperties.h"
#include <memory>
#include <string>

namespace UltraCanvas {

// Test stand-in for the real element base (never linked together with it).
class UltraCanvasUIElement {
public:
    UltraCanvasUIElement(const std::string& elementId, int x, int y, int w, int h)
        : id(elementId), posX(x), posY(y), width(w), height(h) {}
    virtual ~UltraCanvasUIElement() = default;

    virtual std::string TypeTag() const { return "base"; }

    std::string id;
    int posX, posY, width, height;
};

// A widget the tests (and the test plugin DSO) instantiate, with the named
// property surface the registry's typed-configuration gap is about.
class TestWidget : public UltraCanvasUIElement, public IConfigurableElement {
public:
    TestWidget(const std::string& elementId, int x, int y, int w, int h)
        : UltraCanvasUIElement(elementId, x, y, w, h) {
        properties.Define("columns", static_cast<int64_t>(3));
        properties.Define("title", std::string("untitled"));
        properties.Define("compact", false);
    }

    std::string TypeTag() const override { return tag; }

    bool SetProperty(const std::string& key, const UCPropertyValue& value) override {
        return properties.Set(key, value);
    }
    UCPropertyValue GetProperty(const std::string& key) const override {
        return properties.Get(key);
    }
    std::vector<std::string> ListProperties() const override {
        return properties.Keys();
    }

    std::string tag = "test-widget";
    std::string textPayload;      // filled by createFromText in the tests

private:
    UCPropertyBag properties;
};

} // namespace UltraCanvas
