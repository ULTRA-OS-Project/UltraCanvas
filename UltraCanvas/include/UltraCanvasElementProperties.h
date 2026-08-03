// include/UltraCanvasElementProperties.h
// Named-property configuration surface for elements created by name.
//
// UltraCanvasElementRegistry::Create returns a base-class pointer, so an
// application that creates an element by type name cannot reach the element's
// typed API (SetLevelStep, SetColumns, ...). This interface is the third
// configuration path from the element plugin plan (and chart engine proposal
// §12.11): string keys and variant values, which survive any module boundary
// because no plugin-defined type crosses it.
//
// Elements implement IConfigurableElement (usually by embedding UCPropertyBag
// and forwarding), and descriptors advertise the keys they accept.
//
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace UltraCanvas {

// monostate = "no value" / "unknown key".
using UCPropertyValue = std::variant<std::monostate, bool, int64_t, double,
                                     std::string, Color>;

inline bool UCPropertyHasValue(const UCPropertyValue& v) {
    return !std::holds_alternative<std::monostate>(v);
}

// Numeric fetch that accepts whichever numeric alternative was stored.
inline bool UCPropertyAsDouble(const UCPropertyValue& v, double& out) {
    if (const double* d = std::get_if<double>(&v))  { out = *d; return true; }
    if (const int64_t* i = std::get_if<int64_t>(&v)) { out = static_cast<double>(*i); return true; }
    if (const bool* b = std::get_if<bool>(&v))       { out = *b ? 1.0 : 0.0; return true; }
    return false;
}

inline bool UCPropertyAsString(const UCPropertyValue& v, std::string& out) {
    if (const std::string* s = std::get_if<std::string>(&v)) { out = *s; return true; }
    return false;
}

// Implemented by elements that can be configured through named properties.
// SetProperty returns false for an unknown key or a value the element cannot
// use, so document-driven callers can report bad input instead of silently
// dropping it.
class IConfigurableElement {
public:
    virtual ~IConfigurableElement() = default;

    virtual bool SetProperty(const std::string& key, const UCPropertyValue& value) = 0;
    // monostate result = unknown key.
    virtual UCPropertyValue GetProperty(const std::string& key) const = 0;
    virtual std::vector<std::string> ListProperties() const { return {}; }
};

// Convenience storage an element can embed and forward to. Keys must be
// registered (with a default) before they accept values, so ListProperties
// and unknown-key rejection come for free.
class UCPropertyBag {
public:
    void Define(const std::string& key, UCPropertyValue defaultValue) {
        values[key] = std::move(defaultValue);
    }

    bool Set(const std::string& key, const UCPropertyValue& value) {
        auto it = values.find(key);
        if (it == values.end()) return false;      // undefined key: reject
        it->second = value;
        return true;
    }

    UCPropertyValue Get(const std::string& key) const {
        auto it = values.find(key);
        return (it != values.end()) ? it->second : UCPropertyValue{};
    }

    std::vector<std::string> Keys() const {
        std::vector<std::string> keys;
        keys.reserve(values.size());
        for (const auto& kv : values) keys.push_back(kv.first);
        return keys;
    }

private:
    std::map<std::string, UCPropertyValue> values;   // ordered => stable Keys()
};

} // namespace UltraCanvas
