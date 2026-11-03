// UltraCanvas/core/UltraCanvasAppSettings.cpp
// See include/UltraCanvasAppSettings.h.
// Version: 1.0.0
// Author: UltraCanvas Framework
#include "UltraCanvasAppSettings.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSettingsFolder.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace UltraCanvas {

namespace {

const char* kFileName = "settings.json";

// "window.width" is {"window", "width"}; an empty part is skipped, so "a..b"
// is "a.b" and a lone "." nothing.
std::vector<std::string> Parts(const std::string& key) {
    std::vector<std::string> parts;
    std::string part;
    for (const char c : key) {
        if (c == '.') {
            if (!part.empty()) parts.push_back(part);
            part.clear();
        } else {
            part.push_back(c);
        }
    }
    if (!part.empty()) parts.push_back(part);
    return parts;
}

// `node` with `value` put at parts[i..]: the objects on the way made where
// they are missing, a scalar in the way replaced by an object.
JSONValue WithValue(const JSONValue& node, const std::vector<std::string>& parts, size_t i, JSONValue value) {
    if (i == parts.size()) return value;
    JSONValue object = node.IsObject() ? node : JSONValue::MakeObject();
    object.Set(parts[i], WithValue(object.Get(parts[i]), parts, i + 1, std::move(value)));
    return object;
}

// `node` without parts[i..]; `removed` says whether there was one.
JSONValue Without(const JSONValue& node, const std::vector<std::string>& parts, size_t i, bool& removed) {
    if (!node.IsObject() || i == parts.size()) return node;
    JSONValue object = node;
    if (i + 1 == parts.size()) {
        removed = object.Remove(parts[i]);
        return object;
    }
    if (!object.Contains(parts[i])) return object;
    object.Set(parts[i], Without(object.Get(parts[i]), parts, i + 1, removed));
    return object;
}

} // namespace

UltraCanvasAppSettings::UltraCanvasAppSettings(const std::string& app) {
    const std::filesystem::path folder = UltraCanvasSettingsFolder();
    if (!folder.empty() && !app.empty()) file_ = PathToUtf8(folder / PathFromUtf8(app) / kFileName);
}

UltraCanvasAppSettings UltraCanvasAppSettings::AtFile(const std::string& file) {
    UltraCanvasAppSettings settings("");
    settings.file_ = file;
    return settings;
}

bool UltraCanvasAppSettings::Load() {
    document_ = JSONValue::MakeObject();
    if (file_.empty()) return false;
    std::ifstream in(PathFromUtf8(file_));
    if (!in) return false;
    std::stringstream text;
    text << in.rdbuf();
    JSONParseResult result;
    JSONValue json = JSON::Parse(text.str(), &result);
    if (!result.success || !json.IsObject()) return false;
    document_ = std::move(json);
    return true;
}

bool UltraCanvasAppSettings::Save() const {
    if (file_.empty()) return false;
    const std::filesystem::path path = PathFromUtf8(file_);
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    JSONSerializeOptions pretty;
    pretty.pretty = true;
    pretty.indentWidth = 2;
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << JSON::Serialize(document_, pretty) << '\n';
    return static_cast<bool>(out);
}

const JSONValue& UltraCanvasAppSettings::At(const std::string& key) const {
    const std::vector<std::string> parts = Parts(key);
    if (parts.empty()) return JSONValue::NullValue();   // "" names nothing, not the document
    const JSONValue* node = &document_;
    for (const std::string& part : parts) {
        if (!node->IsObject()) return JSONValue::NullValue();
        node = &node->Get(part);
    }
    return *node;
}

void UltraCanvasAppSettings::Put(const std::string& key, JSONValue value) {
    const std::vector<std::string> parts = Parts(key);
    if (parts.empty()) return;
    document_ = WithValue(document_, parts, 0, std::move(value));
}

bool UltraCanvasAppSettings::Has(const std::string& key) const { return !At(key).IsNull(); }

std::string UltraCanvasAppSettings::GetString(const std::string& key, const std::string& fallback) const {
    const JSONValue& value = At(key);
    return value.IsString() ? value.GetString() : fallback;
}

int UltraCanvasAppSettings::GetInt(const std::string& key, int fallback) const {
    const JSONValue& value = At(key);
    return value.IsNumber() ? static_cast<int>(value.GetInteger(fallback)) : fallback;
}

bool UltraCanvasAppSettings::GetBool(const std::string& key, bool fallback) const {
    const JSONValue& value = At(key);
    return value.IsBoolean() ? value.GetBoolean(fallback) : fallback;
}

double UltraCanvasAppSettings::GetNumber(const std::string& key, double fallback) const {
    const JSONValue& value = At(key);
    return value.IsNumber() ? value.GetNumber(fallback) : fallback;
}

std::vector<std::string> UltraCanvasAppSettings::GetList(const std::string& key) const {
    std::vector<std::string> list;
    const JSONValue& value = At(key);
    if (!value.IsArray()) return list;
    for (const JSONValue& element : value.GetElements()) {
        const std::string entry = element.GetString();
        if (!entry.empty() && std::find(list.begin(), list.end(), entry) == list.end()) list.push_back(entry);
    }
    return list;
}

void UltraCanvasAppSettings::Set(const std::string& key, const std::string& value) { Put(key, JSONValue(value)); }
void UltraCanvasAppSettings::Set(const std::string& key, const char* value) { Put(key, JSONValue(value ? value : "")); }
void UltraCanvasAppSettings::Set(const std::string& key, int value) { Put(key, JSONValue(value)); }
void UltraCanvasAppSettings::Set(const std::string& key, bool value) { Put(key, JSONValue(value)); }
void UltraCanvasAppSettings::Set(const std::string& key, double value) { Put(key, JSONValue(value)); }

void UltraCanvasAppSettings::Set(const std::string& key, const std::vector<std::string>& values) {
    JSONValue array = JSONValue::MakeArray();
    for (const std::string& value : values) array.Append(JSONValue(value));
    Put(key, std::move(array));
}

bool UltraCanvasAppSettings::Remove(const std::string& key) {
    const std::vector<std::string> parts = Parts(key);
    if (parts.empty()) return false;
    bool removed = false;
    document_ = Without(document_, parts, 0, removed);
    return removed;
}

void UltraCanvasAppSettings::Remember(const std::string& key, const std::string& entry, std::size_t keep) {
    if (entry.empty()) return;
    std::vector<std::string> list = GetList(key);
    list.erase(std::remove(list.begin(), list.end(), entry), list.end());
    list.insert(list.begin(), entry);
    if (list.size() > keep) list.resize(keep);
    Set(key, list);
}

void UltraCanvasAppSettings::Forget(const std::string& key, const std::string& entry) {
    std::vector<std::string> list = GetList(key);
    list.erase(std::remove(list.begin(), list.end(), entry), list.end());
    Set(key, list);
}

} // namespace UltraCanvas
