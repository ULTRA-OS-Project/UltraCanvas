// Apps/UltraClaude/engine/ChatStore.cpp
// See ChatStore.h.
// Version: 0.1.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework / ULTRA OS

#include "ChatStore.h"
#include "ClaudeStreamParser.h"   // FirstLineShortened

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSettingsFolder.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <system_error>

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace UltraClaude {

namespace {
    constexpr int64_t kFormatVersion = 1;
    constexpr const char* kIndexFile = "chats.json";
    constexpr const char* kTranscriptFolder = "transcripts";

    int64_t NowSeconds() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // "c" + the time in milliseconds + 8 random hex digits: unique enough for
    // one person's chats, sortable, and safe as a file name everywhere.
    std::string NewChatId() {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        std::random_device device;
        std::ostringstream id;
        id << 'c' << std::hex << ms << '-' << (static_cast<uint32_t>(device()) & 0xFFFFFFFFu);
        return id.str();
    }

    // Only ids this store could have made name a file: chats.json is a file
    // a person can edit, and "../x" must not reach outside the folder.
    bool IsSafeId(const std::string& id) {
        if (id.empty() || id.size() > 64) return false;
        return std::all_of(id.begin(), id.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '-';
        });
    }

    bool ReadFile(const fs::path& path, std::string& out) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return true;
    }

    // Writes to "<name>.tmp" and renames it over the target, so the old file
    // stays whole until the new one is.
    bool WriteFileAtomically(const fs::path& path, const std::string& content,
                             std::string& outError) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            outError = "Cannot create " + PathToUtf8(path.parent_path()) + ": " + ec.message();
            return false;
        }
        fs::path temporary = path;
        temporary += ".tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            if (!out) { outError = "Cannot write " + PathToUtf8(temporary); return false; }
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!out) { outError = "Cannot write " + PathToUtf8(temporary); return false; }
        }
        fs::rename(temporary, path, ec);
        if (ec) {
            outError = "Cannot replace " + PathToUtf8(path) + ": " + ec.message();
            fs::remove(temporary, ec);
            return false;
        }
        return true;
    }
} // namespace

ChatStore::ChatStore(fs::path folder) : folder_(std::move(folder)) {}

fs::path ChatStore::DefaultFolder() {
    const fs::path base = UltraCanvasSettingsFolder();
    return base.empty() ? fs::path() : base / "UltraClaude";
}

bool ChatStore::Load(std::string& outError) {
    chats_.clear();
    std::string text;
    if (!ReadFile(folder_ / kIndexFile, text)) return true;   // no chats yet

    JSONParseResult parsed;
    const JSONValue doc = JSON::Parse(text, &parsed);
    if (!parsed.success || !doc.IsObject()) {
        outError = "The chat list " + PathToUtf8(folder_ / kIndexFile) + " is damaged (" +
                   (parsed.success ? std::string("not an object") : parsed.errorMessage) + ").";
        return false;
    }
    const JSONValue& list = doc["chats"];
    for (size_t i = 0; i < list.GetSize(); ++i) {
        const JSONValue& c = list[i];
        ChatRecord r;
        r.id = c["id"].GetString();
        if (!IsSafeId(r.id)) continue;   // not one of ours
        r.sessionId = c["sessionId"].GetString();
        r.title = c["title"].GetString("Chat");
        r.folder = c["folder"].GetString();
        r.model = c["model"].GetString();
        r.permissionMode = c["permissionMode"].GetString();
        r.createdAt = c["createdAt"].GetInteger(0);
        r.updatedAt = c["updatedAt"].GetInteger(r.createdAt);
        chats_.push_back(std::move(r));
    }
    SortNewestFirst();
    return true;
}

bool ChatStore::Save(std::string& outError) const {
    JSONValue list = JSONValue::MakeArray();
    for (const ChatRecord& r : chats_) {
        JSONValue c = JSONValue::MakeObject();
        c.Set("id", r.id);
        c.Set("sessionId", r.sessionId);
        c.Set("title", r.title);
        c.Set("folder", r.folder);
        c.Set("model", r.model);
        c.Set("permissionMode", r.permissionMode);
        c.Set("createdAt", r.createdAt);
        c.Set("updatedAt", r.updatedAt);
        list.Append(c);
    }
    JSONValue doc = JSONValue::MakeObject();
    doc.Set("version", kFormatVersion);
    doc.Set("chats", list);
    JSONSerializeOptions pretty;
    pretty.pretty = true;
    pretty.indentWidth = 2;
    return WriteFileAtomically(folder_ / kIndexFile, JSON::Serialize(doc, pretty) + "\n", outError);
}

ChatRecord* ChatStore::Find(const std::string& id) {
    for (ChatRecord& r : chats_) if (r.id == id) return &r;
    return nullptr;
}

ChatRecord& ChatStore::Create(const std::string& title, const std::string& folder,
                              const std::string& model, const std::string& permissionMode) {
    ChatRecord r;
    r.id = NewChatId();
    r.title = title.empty() ? std::string("New chat") : title;
    r.folder = folder;
    r.model = model;
    r.permissionMode = permissionMode;
    r.createdAt = r.updatedAt = NowSeconds();
    chats_.insert(chats_.begin(), std::move(r));
    return chats_.front();
}

void ChatStore::Touch(const std::string& id) {
    if (ChatRecord* r = Find(id)) {
        r->updatedAt = NowSeconds();
        SortNewestFirst();
    }
}

void ChatStore::Remove(const std::string& id) {
    chats_.erase(std::remove_if(chats_.begin(), chats_.end(),
                                [&](const ChatRecord& r) { return r.id == id; }),
                 chats_.end());
    if (IsSafeId(id)) {
        std::error_code ec;
        fs::remove(TranscriptPath(id), ec);
    }
}

std::string ChatStore::LoadTranscript(const std::string& id) const {
    std::string text;
    if (IsSafeId(id)) ReadFile(TranscriptPath(id), text);
    return text;
}

bool ChatStore::SaveTranscript(const std::string& id, const std::string& markdown,
                               std::string& outError) const {
    if (!IsSafeId(id)) { outError = "Not a chat id: " + id; return false; }
    return WriteFileAtomically(TranscriptPath(id), markdown, outError);
}

std::string ChatStore::TitleFromPrompt(const std::string& prompt, size_t maxBytes) {
    const std::string title = FirstLineShortened(prompt, maxBytes);
    return title.empty() ? std::string("New chat") : title;
}

fs::path ChatStore::TranscriptPath(const std::string& id) const {
    return folder_ / kTranscriptFolder / (id + ".md");   // path-string-ok: id is ASCII, checked by IsSafeId
}

void ChatStore::SortNewestFirst() {
    std::stable_sort(chats_.begin(), chats_.end(), [](const ChatRecord& a, const ChatRecord& b) {
        return a.updatedAt > b.updatedAt;
    });
}

} // namespace UltraClaude
