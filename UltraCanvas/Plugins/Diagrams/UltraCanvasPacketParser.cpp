// Plugins/Diagrams/UltraCanvasPacketParser.cpp
// Mermaid-compatible packet spec parser and emitters.
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasPacketParser.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace UltraCanvas {

namespace {

    std::string TrimCopy(const std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        return s.substr(a, b - a);
    }

    bool StartsWith(const std::string& s, const std::string& prefix) {
        return s.size() >= prefix.size() &&
               s.compare(0, prefix.size(), prefix) == 0;
    }

    // Pull "quoted text" or a bare token from the front of s.
    std::string TakeLabel(const std::string& s) {
        const std::string t = TrimCopy(s);
        if (t.size() >= 2 && (t[0] == '"' || t[0] == '\'')) {
            const char quote = t[0];
            const size_t close = t.find(quote, 1);
            if (close != std::string::npos) return t.substr(1, close - 1);
        }
        // Bare token up to the first @modifier.
        const size_t at = t.find(" @");
        return TrimCopy(at == std::string::npos ? t : t.substr(0, at));
    }

    // Everything from the first " @" onwards.
    std::string TakeModifiers(const std::string& s) {
        // Skip over a quoted label first so an @ inside quotes is not a modifier.
        size_t searchFrom = 0;
        const std::string t = TrimCopy(s);
        if (!t.empty() && (t[0] == '"' || t[0] == '\'')) {
            const size_t close = t.find(t[0], 1);
            if (close != std::string::npos) searchFrom = close + 1;
        }
        const size_t at = t.find('@', searchFrom);
        return at == std::string::npos ? std::string() : t.substr(at);
    }

    Color ParseHexColor(const std::string& hex, bool& ok) {
        ok = false;
        std::string h = hex;
        if (!h.empty() && h[0] == '#') h = h.substr(1);
        if (h.size() != 6) return Color();
        char* end = nullptr;
        const unsigned long v = std::strtoul(h.c_str(), &end, 16);
        if (end == nullptr || *end != '\0') return Color();
        ok = true;
        return Color(static_cast<uint8_t>((v >> 16) & 0xFF),
                     static_cast<uint8_t>((v >> 8) & 0xFF),
                     static_cast<uint8_t>(v & 0xFF), 255);
    }

    // Apply "@payload @const=6 (TCP) @color=#AABBCC" to a field.
    void ApplyModifiers(PacketField& field, const std::string& mods) {
        size_t pos = 0;
        while ((pos = mods.find('@', pos)) != std::string::npos) {
            ++pos;
            const size_t next = mods.find('@', pos);
            const std::string chunk =
                    TrimCopy(mods.substr(pos, next == std::string::npos
                                              ? std::string::npos : next - pos));
            pos = (next == std::string::npos) ? mods.size() : next;
            if (chunk.empty()) continue;

            const size_t eq = chunk.find('=');
            const std::string key = TrimCopy(eq == std::string::npos ? chunk
                                                                     : chunk.substr(0, eq));
            const std::string value = (eq == std::string::npos)
                                      ? std::string() : TrimCopy(chunk.substr(eq + 1));

            if      (key == "payload")  field.kind = PacketFieldKind::Payload;
            else if (key == "reserved") field.kind = PacketFieldKind::Reserved;
            else if (key == "padding")  field.kind = PacketFieldKind::Padding;
            else if (key == "flag")     field.kind = PacketFieldKind::Flag;
            else if (key == "checksum") field.kind = PacketFieldKind::Checksum;
            else if (key == "marker")   field.kind = PacketFieldKind::Marker;
            else if (key == "variable") field.kind = PacketFieldKind::Variable;
            else if (key == "ellipsis") field.kind = PacketFieldKind::Ellipsis;
            else if (key == "length")   field.kind = PacketFieldKind::LengthPrefix;
            else if (key == "const")    field.constantText = value;
            else if (key == "attr")     field.attrText = value;
            else if (key == "desc")     field.description = value;
            else if (key == "id")       field.id = value;
            else if (key == "color") {
                bool ok = false;
                const Color c = ParseHexColor(value, ok);
                if (ok) { field.color = c; field.hasColorOverride = true; }
            }
        }
    }

} // anonymous namespace

// =============================================================================
// PARSER
// =============================================================================

    PacketParseResult ParsePacketText(const std::string& text) {
        PacketParseResult result;
        PacketModel& model = result.model;

        std::istringstream stream(text);
        std::string rawLine;
        size_t lineNo = 0;
        PacketLayer* current = nullptr;
        uint64_t cursor = 0;

        auto EnsureLayer = [&]() -> PacketLayer& {
            if (current == nullptr) {
                model.AddLayer("packet", "");
                current = &model.layers.back();
            }
            return *current;
        };

        auto Fail = [&](const std::string& msg, const std::string& line) {
            PacketParseError e;
            e.line = lineNo;
            e.message = msg;
            e.text = line;
            result.errors.push_back(e);
        };

        while (std::getline(stream, rawLine)) {
            ++lineNo;
            std::string line = TrimCopy(rawLine);

            if (line.empty()) continue;
            if (StartsWith(line, "%%")) continue;           // Mermaid comment
            if (line == "packet" || line == "packet-beta") continue;

            if (StartsWith(line, "title ")) {
                model.title = TrimCopy(line.substr(6));
                continue;
            }
            if (StartsWith(line, "caption ")) {
                model.caption = TrimCopy(line.substr(8));
                continue;
            }
            if (StartsWith(line, "layer ")) {
                std::string rest = TrimCopy(line.substr(6));
                const size_t sp = rest.find_first_of(" \t");
                const std::string id = (sp == std::string::npos) ? rest : rest.substr(0, sp);
                const std::string remainder =
                        (sp == std::string::npos) ? std::string() : rest.substr(sp);
                model.AddLayer(id, TakeLabel(remainder));
                current = &model.layers.back();
                cursor = 0;
                continue;
            }

            // Field statements all contain a colon.
            const size_t colon = line.find(':');
            if (colon == std::string::npos) {
                Fail("Unrecognised statement", rawLine);
                continue;
            }

            const std::string spec = TrimCopy(line.substr(0, colon));
            const std::string rest = line.substr(colon + 1);
            const std::string label = TakeLabel(rest);
            const std::string mods = TakeModifiers(rest);

            uint64_t start = 0, length = 0;

            if (!spec.empty() && spec[0] == '+') {
                // +<count>: auto-advance from the current cursor.
                const std::string countText = TrimCopy(spec.substr(1));
                if (countText.empty()) { Fail("Missing bit count after '+'", rawLine); continue; }
                const long long count = std::atoll(countText.c_str());
                if (count <= 0) { Fail("Bit count must be positive", rawLine); continue; }
                start = cursor;
                length = static_cast<uint64_t>(count);
            } else {
                const size_t dash = spec.find('-');
                if (dash == std::string::npos) {
                    // Single bit.
                    const std::string bitText = TrimCopy(spec);
                    if (bitText.empty()) { Fail("Missing bit index", rawLine); continue; }
                    start = static_cast<uint64_t>(std::atoll(bitText.c_str()));
                    length = 1;
                } else {
                    const long long a = std::atoll(TrimCopy(spec.substr(0, dash)).c_str());
                    const long long b = std::atoll(TrimCopy(spec.substr(dash + 1)).c_str());
                    if (b < a) { Fail("Range end is before its start", rawLine); continue; }
                    start = static_cast<uint64_t>(a);
                    length = static_cast<uint64_t>(b - a + 1);
                }
            }

            PacketLayer& layer = EnsureLayer();
            PacketField field(label, length);
            field.bitOffset = start;
            ApplyModifiers(field, mods);

            // @payload with an explicit range keeps that range; @payload with
            // no range is open-ended.
            layer.fields.push_back(field);
            cursor = start + length;
        }

        if (model.layers.empty()) {
            Fail("No fields found", "");
        }

        return result;
    }

// =============================================================================
// EMITTERS
// =============================================================================

    static const char* KindModifier(PacketFieldKind kind) {
        switch (kind) {
            case PacketFieldKind::Payload:      return " @payload";
            case PacketFieldKind::Reserved:     return " @reserved";
            case PacketFieldKind::Padding:      return " @padding";
            case PacketFieldKind::Flag:         return " @flag";
            case PacketFieldKind::Checksum:     return " @checksum";
            case PacketFieldKind::Marker:       return " @marker";
            case PacketFieldKind::Variable:     return " @variable";
            case PacketFieldKind::Ellipsis:     return " @ellipsis";
            case PacketFieldKind::LengthPrefix: return " @length";
            case PacketFieldKind::Fixed:
            default:                            return "";
        }
    }

    std::string EmitPacketText(const PacketModel& model, bool includeExtensions) {
        std::ostringstream out;
        out << "packet\n";
        if (!model.title.empty()) out << "title " << model.title << "\n";
        if (includeExtensions && !model.caption.empty()) {
            out << "caption " << model.caption << "\n";
        }

        uint64_t base = 0;
        for (size_t li = 0; li < model.layers.size(); ++li) {
            const PacketLayer& layer = model.layers[li];

            if (includeExtensions && (!layer.id.empty() || !layer.name.empty())) {
                out << "layer " << (layer.id.empty() ? "layer" : layer.id)
                    << " \"" << layer.name << "\"\n";
            }

            for (const auto& f : layer.fields) {
                const uint64_t start = base + f.bitOffset;
                if (f.bitLength == 0) {
                    // Open-ended: Mermaid has no syntax for it, so emit a
                    // single-bit placeholder plus the extension modifier.
                    out << start << ": \"" << f.name << "\"";
                } else if (f.bitLength == 1) {
                    out << start << ": \"" << f.name << "\"";
                } else {
                    out << start << "-" << (start + f.bitLength - 1)
                        << ": \"" << f.name << "\"";
                }

                if (includeExtensions) {
                    out << KindModifier(f.kind);
                    if (!f.constantText.empty()) out << " @const=" << f.constantText;
                    if (!f.attrText.empty())     out << " @attr=" << f.attrText;
                }
                out << "\n";
            }
            base += layer.TotalBits();
        }
        return out.str();
    }

    std::string EmitPacketAscii(const PacketModel& model, bool showBitRuler) {
        std::ostringstream out;
        const uint32_t bpr = model.bitsPerRow > 0 ? model.bitsPerRow : 32;

        // Each bit is 2 characters wide plus a closing '+', matching RFC style.
        auto Separator = [&]() {
            out << "+";
            for (uint32_t i = 0; i < bpr; ++i) out << "-+";
            out << "\n";
        };

        if (!model.title.empty()) out << model.title << "\n\n";

        if (showBitRuler) {
            // Tens row then units row, the RFC convention.
            out << " ";
            for (uint32_t i = 0; i < bpr; ++i) {
                out << ((i >= 10) ? std::to_string((i / 10) % 10) : " ") << " ";
            }
            out << "\n ";
            for (uint32_t i = 0; i < bpr; ++i) out << (i % 10) << " ";
            out << "\n";
        }

        Separator();

        // Flatten every field into absolute bit ranges.
        struct Flat { uint64_t start, length; std::string name; };
        std::vector<Flat> flat;
        uint64_t base = 0;
        for (const auto& layer : model.layers) {
            for (const auto& f : layer.fields) {
                const uint64_t len = (f.bitLength == 0) ? bpr : f.bitLength;
                flat.push_back({base + f.bitOffset, len, f.name});
            }
            base += layer.TotalBits();
        }

        uint64_t cursor = 0;
        while (true) {
            // Collect the fragments that fall on this row.
            const uint64_t rowStart = cursor;
            const uint64_t rowEnd = rowStart + bpr;
            bool any = false;
            std::string line = "|";

            uint64_t bit = rowStart;
            while (bit < rowEnd) {
                const Flat* found = nullptr;
                for (const auto& f : flat) {
                    if (bit >= f.start && bit < f.start + f.length) { found = &f; break; }
                }
                if (!found) { bit = rowEnd; break; }
                any = true;

                const uint64_t fragEnd = std::min(rowEnd, found->start + found->length);
                const uint64_t width = fragEnd - bit;
                // Cell interior is 2*width - 1 characters.
                const size_t interior = static_cast<size_t>(width * 2 - 1);
                std::string label = found->name;
                if (label.size() > interior) label = label.substr(0, interior);
                const size_t pad = interior - label.size();
                const size_t left = pad / 2;
                line += std::string(left, ' ') + label +
                        std::string(pad - left, ' ') + "|";
                bit = fragEnd;
            }

            if (!any) break;
            out << line << "\n";
            Separator();
            cursor = rowEnd;
            if (cursor > 1u << 20) break;   // runaway guard
        }

        if (!model.caption.empty()) out << "\n" << model.caption << "\n";
        return out.str();
    }

} // namespace UltraCanvas
