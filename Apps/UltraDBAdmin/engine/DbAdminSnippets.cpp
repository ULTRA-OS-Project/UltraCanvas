// Apps/UltraDBAdmin/engine/DbAdminSnippets.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminSnippets.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

using UltraCanvas::JSONValue;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace UltraDBAdmin {
namespace {

const char* const kFileName   = "snippets.json";
const int         kFileFormat = 1;

std::string Lower(std::string text) {
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

bool SameName(const std::string& a, const std::string& b) { return Lower(a) == Lower(b); }

} // namespace

bool SnippetStore::Open(const std::string& directory, std::string& error) {
    snippets_.clear();
    file_.clear();
    if (directory.empty()) return true;
    file_ = PathToUtf8(PathFromUtf8(directory) / kFileName);
    std::error_code ec;
    if (!std::filesystem::exists(PathFromUtf8(file_), ec)) return true;
    UltraCanvas::JSONParseResult parsed;
    const JSONValue root = UltraCanvas::JSON::ParseFile(file_, &parsed);
    if (!parsed.errorMessage.empty()) {
        error = "cannot read " + file_ + ": " + parsed.errorMessage;
        return false;
    }
    for (const JSONValue& item : root["snippets"].GetElements()) {
        Snippet snippet{item["name"].GetString(), item["sql"].GetString()};
        if (!snippet.name.empty() && !snippet.sql.empty()) snippets_.push_back(std::move(snippet));
    }
    std::sort(snippets_.begin(), snippets_.end(),
              [](const Snippet& a, const Snippet& b) { return Lower(a.name) < Lower(b.name); });
    return true;
}

const Snippet* SnippetStore::Find(const std::string& name) const {
    for (const auto& snippet : snippets_)
        if (SameName(snippet.name, name)) return &snippet;
    return nullptr;
}

bool SnippetStore::Put(const Snippet& snippet, std::string& error) {
    if (snippet.name.find_first_not_of(" \t") == std::string::npos) { error = "A snippet needs a name."; return false; }
    if (snippet.sql.find_first_not_of(" \t\r\n;") == std::string::npos) { error = "There is no SQL to keep."; return false; }
    const std::vector<Snippet> before = snippets_;
    snippets_.erase(std::remove_if(snippets_.begin(), snippets_.end(),
                                   [&](const Snippet& s) { return SameName(s.name, snippet.name); }),
                    snippets_.end());
    snippets_.push_back(snippet);
    std::sort(snippets_.begin(), snippets_.end(),
              [](const Snippet& a, const Snippet& b) { return Lower(a.name) < Lower(b.name); });
    if (!Save(error)) { snippets_ = before; return false; }
    return true;
}

bool SnippetStore::Remove(const std::string& name, std::string& error) {
    const std::vector<Snippet> before = snippets_;
    snippets_.erase(std::remove_if(snippets_.begin(), snippets_.end(),
                                   [&](const Snippet& s) { return SameName(s.name, name); }),
                    snippets_.end());
    if (snippets_.size() == before.size()) return true;
    if (!Save(error)) { snippets_ = before; return false; }
    return true;
}

bool SnippetStore::Save(std::string& error) const {
    if (file_.empty()) return true;
    JSONValue root = JSONValue::MakeObject();
    root.Set("format", kFileFormat);
    JSONValue list = JSONValue::MakeArray();
    for (const auto& snippet : snippets_) {
        JSONValue item = JSONValue::MakeObject();
        item.Set("name", snippet.name);
        item.Set("sql", snippet.sql);
        list.Append(std::move(item));
    }
    root.Set("snippets", std::move(list));
    UltraCanvas::JSONSerializeOptions options;
    options.pretty = true;
    // Written beside and renamed over, as the connection list is.
    const std::string temp = file_ + ".tmp";
    if (!UltraCanvas::JSON::SerializeToFile(temp, root, options)) {
        error = "cannot write " + temp;
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(PathFromUtf8(temp), PathFromUtf8(file_), ec);
    if (ec) {
        error = "cannot replace " + file_ + ": " + ec.message();
        return false;
    }
    return true;
}

} // namespace UltraDBAdmin
