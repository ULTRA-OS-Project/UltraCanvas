// MatterNodeStore.cpp
// The Matter nodes this controller commissioned, kept between runs.
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "MatterNodeStore.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace UltraCanvas {
namespace SmartHome {

namespace {

constexpr const char* kHeader = "# UltraCanvas SmartHome - Matter nodes commissioned by this controller";

std::string Hex(uint64_t value) {
    char buffer[24];
    std::snprintf(buffer, sizeof buffer, "0x%llx", static_cast<unsigned long long>(value));
    return buffer;
}

// "0x1f" -> 31. The whole token must be a hex number that fits.
template <typename T>
bool ParseHex(std::string_view token, T& out) {
    if (token.size() < 3 || token[0] != '0' || (token[1] != 'x' && token[1] != 'X')) return false;
    const char* first = token.data() + 2;
    const char* last = token.data() + token.size();
    T value{};
    const auto [end, ec] = std::from_chars(first, last, value, 16);
    if (ec != std::errc() || end != last) return false;
    out = value;
    return true;
}

std::vector<std::string_view> Split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(separator, start);
        const size_t stop = end == std::string_view::npos ? text.size() : end;
        if (stop > start) parts.push_back(text.substr(start, stop - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return parts;
}

} // namespace

std::string MatterNodeStorePath(const std::string& storageDirectory) {
    // The same directory connectedhomeip's PersistentStorage picks for its
    // ini, so the fabric and the list of its nodes stay together.
    std::string directory = storageDirectory;
    if (directory.empty()) directory = GetEnvUtf8("TMPDIR");
    if (directory.empty()) directory = "/tmp";
    return PathToUtf8(PathFromUtf8(directory) / "ultracanvas-matter-nodes.txt");
}

std::string FormatMatterNodeStore(const MatterNodeStoreData& data) {
    std::string text = std::string(kHeader) + "\n";
    if (data.NextNodeId != 0) text += "next-node-id " + Hex(data.NextNodeId) + "\n";
    for (const MatterStoredNode& node : data.Nodes) {
        text += "node " + Hex(node.NodeId);
        for (const auto& [endpoint, types] : node.DeviceTypes) {
            text += " " + std::to_string(endpoint) + "=";
            for (size_t i = 0; i < types.size(); ++i) {
                if (i) text += ",";
                text += Hex(types[i]);
            }
        }
        text += "\n";
    }
    return text;
}

bool ParseMatterNodeStore(const std::string& text, MatterNodeStoreData& data, std::string& error) {
    MatterNodeStoreData parsed;
    std::istringstream lines(text);
    std::string line;
    int number = 0;
    auto fail = [&](const std::string& why) {
        error = "line " + std::to_string(number) + ": " + why;
        return false;
    };
    while (std::getline(lines, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string_view> words = Split(line, ' ');
        if (words.empty()) continue;
        if (words[0] == "next-node-id") {
            if (words.size() != 2 || !ParseHex(words[1], parsed.NextNodeId)) return fail("bad next-node-id");
        } else if (words[0] == "node") {
            MatterStoredNode node;
            if (words.size() < 2 || !ParseHex(words[1], node.NodeId) || node.NodeId == 0) return fail("bad node id");
            for (size_t i = 2; i < words.size(); ++i) {
                const size_t equals = words[i].find('=');
                if (equals == std::string_view::npos) return fail("endpoint without '='");
                const std::string_view endpointText = words[i].substr(0, equals);
                uint16_t endpoint = 0;
                const auto [end, ec] = std::from_chars(endpointText.data(), endpointText.data() + endpointText.size(), endpoint);
                if (ec != std::errc() || end != endpointText.data() + endpointText.size()) return fail("bad endpoint");
                std::vector<uint32_t>& types = node.DeviceTypes[endpoint];
                for (std::string_view type : Split(words[i].substr(equals + 1), ',')) {
                    uint32_t id = 0;
                    if (!ParseHex(type, id)) return fail("bad device type");
                    types.push_back(id);
                }
            }
            const bool duplicate = std::any_of(parsed.Nodes.begin(), parsed.Nodes.end(),
                                               [&](const MatterStoredNode& n) { return n.NodeId == node.NodeId; });
            if (duplicate) return fail("node listed twice");
            parsed.Nodes.push_back(std::move(node));
        } else {
            return fail("unknown entry");
        }
    }
    data = std::move(parsed);
    return true;
}

bool LoadMatterNodeStore(const std::string& path, MatterNodeStoreData& data, std::string& error) {
    std::error_code ec;
    if (!std::filesystem::exists(PathFromUtf8(path), ec)) {
        data = MatterNodeStoreData{};
        return true;
    }
    std::ifstream in(PathFromUtf8(path), std::ios::binary);
    if (!in) { error = "cannot open " + path; return false; }
    std::ostringstream text;
    text << in.rdbuf();
    if (!ParseMatterNodeStore(text.str(), data, error)) {
        error = path + ", " + error;
        return false;
    }
    return true;
}

bool SaveMatterNodeStore(const std::string& path, const MatterNodeStoreData& data, std::string& error) {
    const std::filesystem::path target = PathFromUtf8(path);
    std::filesystem::path temporary = target;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) { error = "cannot write " + PathToUtf8(temporary); return false; }
        out << FormatMatterNodeStore(data);
        out.flush();
        if (!out) { error = "cannot write " + PathToUtf8(temporary); return false; }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        error = "cannot replace " + path + ": " + ec.message();
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

uint64_t NextFreeMatterNodeId(const MatterNodeStoreData& data) {
    uint64_t next = std::max(data.NextNodeId, kFirstMatterNodeId);
    for (const MatterStoredNode& node : data.Nodes) next = std::max(next, node.NodeId + 1);
    return next;
}

} // namespace SmartHome
} // namespace UltraCanvas
