// UltraAI/runtime/src/LocalRuntimeSources.cpp
// Where the wider list of models comes from: the catalogue sources (a
// LocalAI model gallery, or one of our catalogue.json documents at any
// address) and Hugging Face's search. Text in, structs out; nothing here
// touches the network or the disk.
//
// A LocalAI gallery index is YAML. It is written by a program, so it uses
// a small and regular part of the language, and that part is what
// YamlToJson reads: no dependency on a YAML library for one document. What
// it does not read (anchors, aliases, tags, merge keys) it refuses, so a
// gallery that starts using them fails its refresh instead of being
// misread.
//
// A source is somebody else's list. What it may set is what a model entry
// needs - names, addresses, hashes, a context size - and nothing that
// reaches further: its files must come over https and land under plain
// names inside the model's own folder, and the arguments it hands the
// engine are cut down to a few that name no file.
// Version: 0.1.0
// Last Modified: 2026-11-15
// Author: UltraAI Module

#include "LocalRuntimeInternal.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>

namespace UltraAI {
namespace local_detail {

using nlohmann::json;

namespace {

// ===== Small string helpers =====

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool EndsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool StartsWith(const std::string& s, const std::string& prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

std::string TrimLeft(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

std::string TrimRight(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    return s;
}

std::string Trim(const std::string& s) { return TrimRight(TrimLeft(s)); }

// The last component of a path with either separator.
std::string BaseName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool IsSha256(const std::string& s) {
    return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}

std::string JsonString(const json& j, const char* key) {
    if (!j.is_object()) return {};
    auto it = j.find(key);
    if (it == j.end()) return {};
    if (it->is_string()) return it->get<std::string>();
    return {};
}

std::vector<std::string> JsonStrings(const json& j, const char* key) {
    std::vector<std::string> out;
    if (!j.is_object()) return out;
    auto it = j.find(key);
    if (it == j.end() || !it->is_array()) return out;
    for (const auto& e : *it) {
        if (e.is_string()) out.push_back(e.get<std::string>());
    }
    return out;
}

bool JsonTrue(const json& j, const char* key) {
    if (!j.is_object()) return false;
    auto it = j.find(key);
    return it != j.end() && it->is_boolean() && it->get<bool>();
}

int64_t JsonInt(const json& j, const char* key) {
    if (!j.is_object()) return 0;
    auto it = j.find(key);
    if (it == j.end()) return 0;
    if (it->is_number_integer()) return it->get<int64_t>();
    if (it->is_number()) return static_cast<int64_t>(it->get<double>());
    return 0;
}

bool Has(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

// ===== YAML =====

struct YamlError {
    size_t line;            // 0-based
    std::string message;
};

class YamlReader {
public:
    explicit YamlReader(const std::string& text) {
        size_t start = 0;
        // A byte-order mark is not content.
        if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) start = 3;
        while (start <= text.size()) {
            const size_t end = text.find('\n', start);
            std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines_.push_back(std::move(line));
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }

    json Read() {
        int indent = 0;
        std::string content;
        if (!Peek(indent, content)) return json();
        json value = ParseNode(-1, 0);
        if (Peek(indent, content)) Fail("content after the end of the document");
        return value;
    }

private:
    static constexpr int kMaxDepth = 64;

    std::vector<std::string> lines_;
    size_t pos_ = 0;
    // A sequence item that opens a mapping ("- key: value") is read as a
    // mapping whose first line is the rest of that line, further in.
    bool hasOverride_ = false;
    int overrideIndent_ = 0;
    std::string overrideContent_;

    [[noreturn]] void Fail(const std::string& message) const { throw YamlError{pos_, message}; }

    static size_t LeadingSpaces(const std::string& line) {
        size_t i = 0;
        while (i < line.size() && line[i] == ' ') ++i;
        return i;
    }

    static bool IsBlank(const std::string& line) {
        return line.find_first_not_of(" \t") == std::string::npos;
    }

    // The current line with content: its indent and what follows it. Blank
    // lines, comment lines and document markers are passed over.
    bool Peek(int& indent, std::string& content) {
        if (hasOverride_) {
            indent  = overrideIndent_;
            content = overrideContent_;
            return true;
        }
        while (pos_ < lines_.size()) {
            const std::string& line = lines_[pos_];
            const size_t i = LeadingSpaces(line);
            if (IsBlank(line) || line[i] == '#') { ++pos_; continue; }
            if (line[i] == '\t') Fail("a tab in the indentation");
            if (i == 0 && line.compare(0, 3, "---") == 0 && (line.size() == 3 || line[3] == ' ')) {
                if (TrimRight(line).size() > 3 && Trim(line.substr(3))[0] != '#') {
                    Fail("content on a document marker line");
                }
                ++pos_;
                continue;
            }
            if (i == 0 && TrimRight(line) == "...") { pos_ = lines_.size(); return false; }
            indent  = static_cast<int>(i);
            content = TrimRight(line.substr(i));
            return true;
        }
        return false;
    }

    void Advance() {
        hasOverride_ = false;
        ++pos_;
    }

    static bool IsSequenceItem(const std::string& content) {
        return content == "-" || content.compare(0, 2, "- ") == 0;
    }

    // Where a quoted scalar that starts at `from` ends on this line
    // (the index of its closing quote); npos when it does not close here.
    static size_t ClosingQuote(const std::string& s, size_t from) {
        const char quote = s[from];
        for (size_t i = from + 1; i < s.size(); ++i) {
            if (quote == '"' && s[i] == '\\') { ++i; continue; }
            if (s[i] != quote) continue;
            if (quote == '\'' && i + 1 < s.size() && s[i + 1] == '\'') { ++i; continue; }
            return i;
        }
        return std::string::npos;
    }

    // "key: rest" - the key and what follows the colon. False when the line
    // is not a mapping entry.
    bool SplitEntry(const std::string& content, std::string& key, std::string& rest) {
        if (content.empty()) return false;
        size_t colon = std::string::npos;
        if (content[0] == '"' || content[0] == '\'') {
            const size_t close = ClosingQuote(content, 0);
            if (close == std::string::npos) return false;
            size_t i = close + 1;
            while (i < content.size() && content[i] == ' ') ++i;
            if (i >= content.size() || content[i] != ':') return false;
            if (i + 1 < content.size() && content[i + 1] != ' ') return false;
            key = content[0] == '"' ? UnescapeDouble(content.substr(1, close - 1))
                                    : UnescapeSingle(content.substr(1, close - 1));
            colon = i;
        } else {
            if (std::string("[{|>&*!%@`").find(content[0]) != std::string::npos) return false;
            for (size_t i = 0; i < content.size(); ++i) {
                if (content[i] == '#' && i > 0 && content[i - 1] == ' ') return false;
                if (content[i] == ':' && (i + 1 == content.size() || content[i + 1] == ' ')) {
                    colon = i;
                    break;
                }
            }
            if (colon == std::string::npos) return false;
            key = TrimRight(content.substr(0, colon));
        }
        rest = TrimLeft(content.substr(colon + 1));
        return true;
    }

    bool IsMapEntry(const std::string& content) {
        std::string key, rest;
        return SplitEntry(content, key, rest);
    }

    static bool IsCommentOrEmpty(const std::string& rest) { return rest.empty() || rest[0] == '#'; }

    json ParseNode(int parentIndent, int depth) {
        if (depth > kMaxDepth) Fail("nested too deeply");
        int indent = 0;
        std::string content;
        if (!Peek(indent, content)) return json();
        if (IsSequenceItem(content)) return ParseSequence(indent, depth);
        if (IsMapEntry(content)) return ParseMapping(indent, depth);
        return ParseValue(content, parentIndent, depth);
    }

    json ParseSequence(int indent, int depth) {
        if (depth > kMaxDepth) Fail("nested too deeply");
        json list = json::array();
        int at = 0;
        std::string content;
        while (Peek(at, content) && at == indent && IsSequenceItem(content)) {
            size_t offset = 1;
            while (offset < content.size() && content[offset] == ' ') ++offset;
            const std::string rest = content.substr(offset);
            if (IsCommentOrEmpty(rest)) {
                Advance();
                int next = 0;
                std::string nextContent;
                if (Peek(next, nextContent) && next > indent) list.push_back(ParseNode(indent, depth + 1));
                else list.push_back(json());
                continue;
            }
            const int inner = indent + static_cast<int>(offset);
            if (IsSequenceItem(rest)) {
                hasOverride_ = true; overrideIndent_ = inner; overrideContent_ = rest;
                list.push_back(ParseSequence(inner, depth + 1));
            } else if (IsMapEntry(rest)) {
                hasOverride_ = true; overrideIndent_ = inner; overrideContent_ = rest;
                list.push_back(ParseMapping(inner, depth + 1));
            } else {
                list.push_back(ParseValue(rest, indent, depth + 1));
            }
        }
        return list;
    }

    json ParseMapping(int indent, int depth) {
        if (depth > kMaxDepth) Fail("nested too deeply");
        json object = json::object();
        int at = 0;
        std::string content;
        while (Peek(at, content) && at == indent && !IsSequenceItem(content)) {
            std::string key, rest;
            if (!SplitEntry(content, key, rest)) Fail("expected \"key: value\"");
            if (key == "<<") Fail("YAML merge keys are not read");
            if (IsCommentOrEmpty(rest)) {
                Advance();
                int next = 0;
                std::string nextContent;
                if (Peek(next, nextContent) && next > indent) {
                    object[key] = ParseNode(indent, depth + 1);
                } else if (Peek(next, nextContent) && next == indent && IsSequenceItem(nextContent)) {
                    // A sequence may sit at its key's own indent.
                    object[key] = ParseSequence(indent, depth + 1);
                } else {
                    object[key] = json();
                }
                continue;
            }
            object[key] = ParseValue(rest, indent, depth + 1);
        }
        return object;
    }

    // A value that starts on the current line at `text`; consumes that line
    // and whatever it continues onto.
    json ParseValue(const std::string& text, int parentIndent, int depth) {
        const char c = text[0];
        if (c == '|' || c == '>') return ParseBlockScalar(text, parentIndent);
        if (c == '"' || c == '\'') return ParseQuoted(text);
        if (c == '[' || c == '{') return ParseFlow(text, depth);
        if (c == '&' || c == '*' || c == '!') Fail("YAML anchors, aliases and tags are not read");
        if (c == '%' || c == '@' || c == '`') Fail("a reserved character starts a value");

        std::string value = StripPlainComment(text);
        Advance();
        bool multiLine = false;
        // A plain scalar goes on over the lines indented further than its
        // parent; a blank line in it is a line break.
        while (pos_ < lines_.size()) {
            size_t look = pos_;
            int blanks = 0;
            while (look < lines_.size() && IsBlank(lines_[look])) { ++look; ++blanks; }
            if (look >= lines_.size()) break;
            const std::string& line = lines_[look];
            const size_t i = LeadingSpaces(line);
            if (static_cast<int>(i) <= parentIndent || line[i] == '#') break;
            const std::string more = StripPlainComment(TrimRight(line.substr(i)));
            value += blanks ? std::string(static_cast<size_t>(blanks), '\n') : std::string(" ");
            value += more;
            multiLine = true;
            pos_ = look + 1;
        }
        return multiLine ? json(value) : TypedScalar(value);
    }

    static std::string StripPlainComment(const std::string& text) {
        for (size_t i = 1; i < text.size(); ++i) {
            if (text[i] == '#' && (text[i - 1] == ' ' || text[i - 1] == '\t')) {
                return TrimRight(text.substr(0, i));
            }
        }
        return TrimRight(text);
    }

    static json TypedScalar(const std::string& value) {
        if (value.empty() || value == "~" || value == "null" || value == "Null" || value == "NULL") return json();
        if (value == "true" || value == "True" || value == "TRUE") return json(true);
        if (value == "false" || value == "False" || value == "FALSE") return json(false);
        // A whole number; anything else (a float, a date, a version) stays text.
        size_t i = (value[0] == '-' || value[0] == '+') ? 1 : 0;
        const size_t digits = value.size() - i;
        if (digits >= 1 && digits <= 18 && value.find_first_not_of("0123456789", i) == std::string::npos &&
            (digits == 1 || value[i] != '0')) {
            return json(static_cast<int64_t>(std::strtoll(value.c_str(), nullptr, 10)));
        }
        return json(value);
    }

    json ParseBlockScalar(const std::string& header, int parentIndent) {
        const bool folded = header[0] == '>';
        char chomp = 'c';           // clip
        int explicitIndent = 0;
        size_t i = 1;
        for (; i < header.size(); ++i) {
            const char c = header[i];
            if (c == '-' || c == '+') chomp = c;
            else if (c >= '1' && c <= '9') explicitIndent = c - '0';
            else break;
        }
        const std::string tail = TrimLeft(header.substr(i));
        if (!IsCommentOrEmpty(tail)) Fail("text after a block scalar's indicator");
        Advance();

        int blockIndent = explicitIndent ? std::max(parentIndent, 0) + explicitIndent : -1;
        std::vector<std::string> body;
        size_t at = pos_;
        for (; at < lines_.size(); ++at) {
            const std::string& line = lines_[at];
            if (IsBlank(line)) {
                body.push_back(blockIndent >= 0 && line.size() > static_cast<size_t>(blockIndent)
                                   ? line.substr(static_cast<size_t>(blockIndent)) : std::string());
                continue;
            }
            const int indent = static_cast<int>(LeadingSpaces(line));
            if (blockIndent < 0) {
                if (indent <= parentIndent) break;
                blockIndent = indent;
            }
            if (indent < blockIndent) break;
            body.push_back(line.substr(static_cast<size_t>(blockIndent)));
        }
        pos_ = at;

        size_t trailing = 0;
        while (!body.empty() && IsBlank(body.back())) { body.pop_back(); ++trailing; }
        std::string out;
        if (!folded) {
            for (size_t n = 0; n < body.size(); ++n) {
                if (n) out += '\n';
                out += body[n];
            }
        } else {
            // Folded: a single break between two lines of text is a space,
            // a blank line a break; a line indented further keeps its breaks.
            size_t breaks = 0;
            bool previousIndented = false, any = false;
            for (const std::string& line : body) {
                if (line.empty()) { ++breaks; continue; }
                const bool indented = line[0] == ' ' || line[0] == '\t';
                if (any) {
                    const size_t total = breaks + 1;
                    if (indented || previousIndented) out += std::string(total, '\n');
                    else if (total == 1) out += ' ';
                    else out += std::string(total - 1, '\n');
                } else {
                    out += std::string(breaks, '\n');
                }
                out += line;
                breaks = 0;
                previousIndented = indented;
                any = true;
            }
        }
        if (chomp == 'c' && !body.empty()) out += '\n';
        if (chomp == '+') out += std::string(body.empty() ? trailing : trailing + 1, '\n');
        return json(out);
    }

    static void AppendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    static std::string UnescapeSingle(const std::string& body) {
        std::string out;
        for (size_t i = 0; i < body.size(); ++i) {
            out += body[i];
            if (body[i] == '\'' && i + 1 < body.size() && body[i + 1] == '\'') ++i;
        }
        return out;
    }

    static std::string UnescapeDouble(const std::string& body) {
        std::string out;
        for (size_t i = 0; i < body.size(); ++i) {
            if (body[i] != '\\' || i + 1 >= body.size()) { out += body[i]; continue; }
            const char e = body[++i];
            auto hex = [&](int count) {
                uint32_t cp = 0;
                for (int n = 0; n < count && i + 1 < body.size(); ++n) {
                    const char h = body[i + 1];
                    int v;
                    if (h >= '0' && h <= '9') v = h - '0';
                    else if (h >= 'a' && h <= 'f') v = h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F') v = h - 'A' + 10;
                    else break;
                    cp = cp * 16 + static_cast<uint32_t>(v);
                    ++i;
                }
                return cp;
            };
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '0': out += '\0'; break;
                case 'a': out += '\a'; break;
                case 'b': out += '\b'; break;
                case 'e': out += '\x1B'; break;
                case 'f': out += '\f'; break;
                case 'v': out += '\v'; break;
                case 'N': AppendUtf8(out, 0x85); break;
                case '_': AppendUtf8(out, 0xA0); break;
                case 'L': AppendUtf8(out, 0x2028); break;
                case 'P': AppendUtf8(out, 0x2029); break;
                case 'x': AppendUtf8(out, hex(2)); break;
                case 'u': {
                    uint32_t cp = hex(4);
                    // A surrogate pair written as two \u escapes.
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 2 < body.size() && body[i + 1] == '\\' && body[i + 2] == 'u') {
                        i += 2;
                        const uint32_t low = hex(4);
                        if (low >= 0xDC00 && low < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                case 'U': AppendUtf8(out, hex(8)); break;
                default:  out += e; break;      // \" \\ \/ and the escaped space
            }
        }
        return out;
    }

    // A quoted scalar starting at text[0]; it may run over several lines,
    // whose breaks fold into a space (a blank line into a break).
    json ParseQuoted(const std::string& text) {
        const char quote = text[0];
        std::string raw;            // between the quotes, still escaped
        std::string current = text;
        size_t from = 0;
        size_t line = pos_;
        while (true) {
            const size_t close = from == 0 ? ClosingQuote(current, 0) : FindClose(current, quote);
            if (close != std::string::npos) {
                raw += current.substr(from == 0 ? 1 : 0, close - (from == 0 ? 1 : 0));
                const std::string tail = TrimLeft(current.substr(close + 1));
                if (!IsCommentOrEmpty(tail)) {
                    pos_ = line;
                    Fail("text after a quoted value");
                }
                break;
            }
            std::string piece = current.substr(from == 0 ? 1 : 0);
            // A backslash at the end of a line joins the next one to it.
            const bool joined = quote == '"' && !piece.empty() && piece.back() == '\\' &&
                                (piece.size() < 2 || piece[piece.size() - 2] != '\\');
            if (joined) piece.pop_back();
            else piece = TrimRight(piece);
            raw += piece;
            int blanks = 0;
            ++line;
            while (line < lines_.size() && IsBlank(lines_[line])) { ++line; ++blanks; }
            if (line >= lines_.size()) {
                pos_ = lines_.size() ? lines_.size() - 1 : 0;
                Fail("a quoted value is not closed");
            }
            if (!joined) raw += blanks ? std::string(static_cast<size_t>(blanks), '\n') : std::string(" ");
            current = TrimLeft(lines_[line]);
            from = 1;
        }
        hasOverride_ = false;
        pos_ = line + 1;
        return json(quote == '"' ? UnescapeDouble(raw) : UnescapeSingle(raw));
    }

    // The closing quote of a scalar whose opening quote is on an earlier line.
    static size_t FindClose(const std::string& s, char quote) {
        for (size_t i = 0; i < s.size(); ++i) {
            if (quote == '"' && s[i] == '\\') { ++i; continue; }
            if (s[i] != quote) continue;
            if (quote == '\'' && i + 1 < s.size() && s[i + 1] == '\'') { ++i; continue; }
            return i;
        }
        return std::string::npos;
    }

    // ----- flow collections: [a, b] and {k: v} -----

    json ParseFlow(const std::string& text, int depth) {
        // Gather the lines until the brackets balance.
        std::string all = text;
        size_t line = pos_;
        while (!FlowBalanced(all)) {
            ++line;
            if (line >= lines_.size()) Fail("a [ or { is not closed");
            all += ' ';
            all += Trim(lines_[line]);
        }
        size_t i = 0;
        json value = FlowValue(all, i, depth);
        const std::string tail = TrimLeft(all.substr(i));
        if (!IsCommentOrEmpty(tail)) Fail("text after a ] or }");
        hasOverride_ = false;
        pos_ = line + 1;
        return value;
    }

    static bool FlowBalanced(const std::string& s) {
        int open = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            const char c = s[i];
            if (c == '"' || c == '\'') {
                const size_t close = ClosingQuote(s, i);
                if (close == std::string::npos) return false;
                i = close;
            } else if (c == '[' || c == '{') {
                ++open;
            } else if (c == ']' || c == '}') {
                if (--open == 0) return true;
            } else if (c == '#' && i > 0 && s[i - 1] == ' ' && open == 0) {
                break;
            }
        }
        return open <= 0;
    }

    static void SkipSpaces(const std::string& s, size_t& i) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    }

    json FlowValue(const std::string& s, size_t& i, int depth) {
        if (depth > kMaxDepth) Fail("nested too deeply");
        SkipSpaces(s, i);
        if (i >= s.size()) Fail("a value is missing");
        if (s[i] == '[') {
            ++i;
            json list = json::array();
            while (true) {
                SkipSpaces(s, i);
                if (i >= s.size()) Fail("a [ is not closed");
                if (s[i] == ']') { ++i; break; }
                list.push_back(FlowValue(s, i, depth + 1));
                SkipSpaces(s, i);
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                Fail("expected , or ]");
            }
            return list;
        }
        if (s[i] == '{') {
            ++i;
            json object = json::object();
            while (true) {
                SkipSpaces(s, i);
                if (i >= s.size()) Fail("a { is not closed");
                if (s[i] == '}') { ++i; break; }
                const json key = FlowScalar(s, i, true);
                SkipSpaces(s, i);
                json value;
                if (i < s.size() && s[i] == ':') {
                    ++i;
                    SkipSpaces(s, i);
                    if (i < s.size() && s[i] != ',' && s[i] != '}') value = FlowValue(s, i, depth + 1);
                }
                object[key.is_string() ? key.get<std::string>() : key.dump()] = value;
                SkipSpaces(s, i);
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                Fail("expected , or }");
            }
            return object;
        }
        return FlowScalar(s, i, false);
    }

    json FlowScalar(const std::string& s, size_t& i, bool asKey) {
        SkipSpaces(s, i);
        if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
            const size_t close = ClosingQuote(s, i);
            if (close == std::string::npos) Fail("a quoted value is not closed");
            const std::string body = s.substr(i + 1, close - i - 1);
            const char quote = s[i];
            i = close + 1;
            return json(quote == '"' ? UnescapeDouble(body) : UnescapeSingle(body));
        }
        if (i < s.size() && (s[i] == '&' || s[i] == '*' || s[i] == '!')) {
            Fail("YAML anchors, aliases and tags are not read");
        }
        const size_t start = i;
        while (i < s.size()) {
            const char c = s[i];
            if (c == ',' || c == ']' || c == '}') break;
            if (asKey && c == ':' && (i + 1 >= s.size() || s[i + 1] == ' ' || s[i + 1] == ',' || s[i + 1] == '}')) break;
            ++i;
        }
        const std::string text = Trim(s.substr(start, i - start));
        return asKey ? json(text) : TypedScalar(text);
    }
};

// ===== LocalAI gallery entries =====

// Tags and config templates that mark a GGUF as something other than a
// language model: pictures, speech, sound, 3D, ranking.
const char* const kOtherModelTags[] = {
    "audio-classification", "sound-event-detection", "text-to-image", "image-generation",
    "image-to-image", "image-to-3d", "3d-generation", "text-to-video", "image-to-video",
    "video-generation", "tts", "text-to-speech", "stable-diffusion", "reranker", "cross-encoder",
    "asr", "speech-recognition", "vad", "object-detection", "depth-estimation",
};
const char* const kOtherModelTemplates[] = {
    "ced", "trellis", "sd-", "flux", "ltx", "piper", "sherpa", "kokoro", "vllm", "openvino",
    "kimodo", "stablediffusion", "diffusers", "bark", "parler", "rerank",
};
const char* const kLanguageUsecases[] = {"chat", "completion", "vision", "embeddings"};

// "3.03GB" / "449MB" as bytes; 0 when it is not such a figure.
uint64_t ParseSizeText(const std::string& text) {
    const std::string t = Lower(Trim(text));
    size_t i = 0;
    double value = 0.0, scale = 0.1;
    bool digits = false, fraction = false;
    for (; i < t.size(); ++i) {
        const char c = t[i];
        if (c >= '0' && c <= '9') {
            digits = true;
            if (!fraction) value = value * 10.0 + (c - '0');
            else { value += (c - '0') * scale; scale *= 0.1; }
        } else if (c == '.' && !fraction) {
            fraction = true;
        } else {
            break;
        }
    }
    if (!digits) return 0;
    const std::string unit = Trim(t.substr(i));
    double factor = 0.0;
    if (unit == "gb" || unit == "gib" || unit == "g") factor = 1024.0 * 1024.0 * 1024.0;
    else if (unit == "mb" || unit == "mib" || unit == "m") factor = 1024.0 * 1024.0;
    else if (unit == "kb" || unit == "kib" || unit == "k") factor = 1024.0;
    else return 0;
    return static_cast<uint64_t>(value * factor);
}

// The name of the config template an entry points at: "qwen3" from
// "github:mudler/LocalAI/gallery/qwen3.yaml@master".
std::string TemplateName(const std::string& url) {
    std::string name = BaseName(url);
    const size_t at = name.find('@');
    if (at != std::string::npos) name.resize(at);
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos) name.resize(dot);
    return Lower(name);
}

std::string Shorten(const std::string& text, size_t limit) {
    std::string t = Trim(text);
    while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
    if (t.size() <= limit) return t;
    // Cut at a character boundary.
    size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(t[cut]) & 0xC0) == 0x80) --cut;
    return t.substr(0, cut) + "...";
}

bool SplitPart(const std::string& name, int& part, int& count, std::string& firstName);

// One LocalAI gallery entry as a model of ours; false when the local
// engines cannot run it.
bool MapLocalAiEntry(const json& entry, const CatalogueSource& source, LocalModelInfo& out) {
    if (!entry.is_object()) return false;
    // The library shape keeps url / files / overrides under "model".
    auto nested = entry.find("model");
    const json& model = nested != entry.end() && nested->is_object() ? *nested : entry;

    std::string name = JsonString(entry, "name");
    if (name.empty()) name = JsonString(entry, "id");
    if (name.empty() || name.size() > 200) return false;

    auto filesIt = model.find("files");
    if (filesIt == model.end() || !filesIt->is_array() || filesIt->empty()) return false;
    auto overridesIt = model.find("overrides");
    const json overrides = overridesIt != model.end() && overridesIt->is_object() ? *overridesIt : json::object();

    std::string backend = JsonString(overrides, "backend");
    if (backend.empty()) backend = JsonString(model, "backend");
    if (backend.empty()) backend = JsonString(entry, "backend");
    backend = Lower(backend);
    const std::string templateName = TemplateName(JsonString(model, "url"));
    std::vector<std::string> usecases = JsonStrings(overrides, "known_usecases");
    for (auto& u : usecases) u = Lower(u);
    const std::vector<std::string> options = JsonStrings(overrides, "options");
    std::vector<std::string> tags = JsonStrings(entry, "tags");

    std::string modelFile, projectorFile;
    if (overrides.contains("parameters")) modelFile = BaseName(JsonString(overrides["parameters"], "model"));
    projectorFile = BaseName(JsonString(overrides, "mmproj"));

    std::vector<LocalModelFile> files;
    std::set<std::string> names;
    bool allGguf = true;
    for (const auto& f : *filesIt) {
        if (!f.is_object()) return false;
        LocalModelFile file;
        file.name = BaseName(JsonString(f, "filename"));
        file.url  = ResolveSourceUrl(JsonString(f, "uri"));
        if (!IsSafeModelFileName(file.name) || file.url.empty()) return false;
        if (!names.insert(file.name).second) return false;
        const std::string sha = Lower(JsonString(f, "sha256"));
        if (IsSha256(sha)) file.sha256 = sha;
        const std::string lower = Lower(file.name);
        if (!EndsWith(lower, ".gguf")) allGguf = false;
        file.projector = (!projectorFile.empty() && file.name == projectorFile) || StartsWith(lower, "mmproj");
        files.push_back(std::move(file));
    }

    const bool whisper = (backend == "whisper" || (backend.empty() && StartsWith(templateName, "whisper"))) &&
                         files.size() == 1 && EndsWith(Lower(files.front().name), ".bin") &&
                         !Has(options, "vad_only") && !Has(usecases, "vad");
    bool llama = !whisper && allGguf && (backend == "llama-cpp" || backend.empty());
    bool embeddings = false;
    if (llama) {
        if (JsonTrue(overrides, "reranking")) return false;
        bool languageUse = usecases.empty();
        for (const char* u : kLanguageUsecases) languageUse = languageUse || Has(usecases, u);
        if (!languageUse) return false;
        for (const std::string& tag : tags) {
            const std::string t = Lower(tag);
            for (const char* other : kOtherModelTags) {
                if (t == other) return false;
            }
        }
        if (backend.empty()) {
            for (const char* other : kOtherModelTemplates) {
                if (StartsWith(templateName, other)) return false;
            }
        }
        embeddings = JsonTrue(overrides, "embeddings") || Has(usecases, "embeddings");
        if (!embeddings && usecases.empty()) {
            for (const std::string& tag : tags) {
                const std::string t = Lower(tag);
                if (t == "embedding" || t == "embeddings") embeddings = true;
            }
        }
        // A projector is no model: something else has to be there.
        if (std::all_of(files.begin(), files.end(), [](const LocalModelFile& f) { return f.projector; })) return false;
    }
    if (!whisper && !llama) return false;

    // The file the engine is handed comes first.
    std::stable_sort(files.begin(), files.end(), [&](const LocalModelFile& a, const LocalModelFile& b) {
        const int ra = a.projector ? 2 : (a.name == modelFile ? 0 : 1);
        const int rb = b.projector ? 2 : (b.name == modelFile ? 0 : 1);
        return ra < rb;
    });
    if (llama) {
        // What the engine is started with, and no more: the model (every
        // part of a split one) and one projector. A draft model or another
        // extra the entry lists would be a download nothing uses.
        const std::string primary = files.front().name;
        int part = 0, count = 0;
        std::string firstName;
        const bool split = SplitPart(primary, part, count, firstName);
        bool haveProjector = false;
        files.erase(std::remove_if(files.begin(), files.end(), [&](const LocalModelFile& f) {
            if (f.name == primary) return false;
            if (f.projector) {
                if (haveProjector) return true;
                haveProjector = true;
                return false;
            }
            int otherPart = 0, otherCount = 0;
            std::string otherFirst;
            return !(split && SplitPart(f.name, otherPart, otherCount, otherFirst) && otherFirst == firstName);
        }), files.end());
    }

    out = LocalModelInfo{};
    out.id          = source.id + ":" + name;
    out.displayName = name;
    out.description = Shorten(JsonString(entry, "description"), 1200);
    out.capability  = whisper ? "speechtotext" : embeddings ? "embeddings" : "textllm";
    out.engineId    = whisper ? "whisper-server" : "llama-server";
    out.license     = JsonString(entry, "license");
    out.source      = LocalModelSource::Gallery;
    out.gallery     = source.id;
    for (const std::string& url : JsonStrings(entry, "urls")) {
        if (StartsWith(url, "https://")) { out.homepage = url; break; }
    }
    // The few entries that state a size state the download's: the one
    // file's, when there is one.
    if (files.size() == 1) files.front().sizeBytes = ParseSizeText(JsonString(entry, "size"));
    out.files = std::move(files);
    for (const auto& f : out.files) out.sizeBytes += f.sizeBytes;

    if (llama) {
        const int64_t context = JsonInt(overrides, "context_size");
        if (context > 0 && context <= 4 * 1024 * 1024) out.contextWindow = static_cast<int32_t>(context);
        // What the engine is started with stays small: a long window costs
        // memory before the first word, and the curated models use 8192 too.
        const int64_t window = context > 0 ? std::min<int64_t>(context, 8192) : 8192;
        if (!embeddings || context > 0) out.engineArgs = {"--ctx-size", std::to_string(window)};
        for (const std::string& option : options) {
            if (!StartsWith(option, "pooling:")) continue;
            const std::string pooling = Lower(option.substr(8));
            if (pooling == "none" || pooling == "mean" || pooling == "cls" || pooling == "last" || pooling == "rank") {
                out.engineArgs.push_back("--pooling");
                out.engineArgs.push_back(pooling);
            }
        }
        out.supportsVision = Has(usecases, "vision") ||
                             std::any_of(out.files.begin(), out.files.end(),
                                         [](const LocalModelFile& f) { return f.projector; });
        for (const std::string& tag : tags) {
            const std::string t = Lower(tag);
            if (t == "function-calling" || t == "tools" || t == "tool-calling") out.supportsTools = true;
        }
    }
    if (tags.size() > 24) tags.resize(24);
    out.tags = std::move(tags);
    return true;
}

// A model of one of our catalogue.json documents, published by somebody
// else: kept when its engine, its files and its names are what a model
// entry may carry.
bool AdoptSourceModel(LocalModelInfo& model, const CatalogueSource& source) {
    if (model.id.empty() || model.id.size() > 200) return false;
    for (unsigned char c : model.id) {
        if (c < 0x20 || c == 0x7F) return false;
    }
    const bool speech = model.capability == "speechtotext";
    if (!speech && model.capability != "textllm" && model.capability != "embeddings") return false;
    if (model.engineId != (speech ? "whisper-server" : "llama-server")) return false;
    if (model.files.empty()) return false;
    std::set<std::string> names;
    for (LocalModelFile& f : model.files) {
        if (!IsSafeModelFileName(f.name) || !names.insert(f.name).second) return false;
        f.url = ResolveSourceUrl(f.url);
        if (f.url.empty()) return false;
        if (!f.sha256.empty() && !IsSha256(f.sha256)) return false;
    }
    model.id         = source.id + ":" + model.id;
    model.source     = LocalModelSource::Gallery;
    model.gallery    = source.id;
    model.engineArgs = SafeEngineArgs(model.engineArgs);
    model.installedPath.clear();
    model.primaryFile.clear();
    if (!model.homepage.empty() && !StartsWith(model.homepage, "https://")) model.homepage.clear();
    return true;
}

std::string UrlEncode(const std::string& text) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0xF];
        }
    }
    return out;
}

// A repository id as a URL path: its parts encoded, the slash kept.
std::string EncodeRepoPath(const std::string& path) {
    std::string out;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t slash = path.find('/', start);
        const std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!out.empty()) out += '/';
        out += UrlEncode(part);
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return out;
}

bool ReadRepo(const json& j, HuggingFaceRepo& out) {
    if (!j.is_object()) return false;
    out = HuggingFaceRepo{};
    out.id = JsonString(j, "id");
    if (out.id.empty()) out.id = JsonString(j, "modelId");
    if (out.id.empty()) return false;
    const int64_t downloads = JsonInt(j, "downloads"), likes = JsonInt(j, "likes");
    out.downloads   = downloads > 0 ? static_cast<uint64_t>(downloads) : 0;
    out.likes       = likes > 0 ? static_cast<uint64_t>(likes) : 0;
    out.pipelineTag = JsonString(j, "pipeline_tag");
    out.tags        = JsonStrings(j, "tags");
    std::string date = JsonString(j, "lastModified");
    if (date.empty()) date = JsonString(j, "createdAt");
    out.lastModified = date.substr(0, std::min<size_t>(date.size(), 10));
    // "gated" is false, or the way the agreement is given ("auto", "manual").
    auto gated = j.find("gated");
    if (gated != j.end()) {
        out.gated = (gated->is_boolean() && gated->get<bool>()) ||
                    (gated->is_string() && gated->get<std::string>() != "false");
    }
    for (const std::string& tag : out.tags) {
        if (StartsWith(tag, "license:")) { out.license = tag.substr(8); break; }
    }
    if (out.license.empty()) {
        auto card = j.find("cardData");
        if (card != j.end()) out.license = JsonString(*card, "license");
    }
    return true;
}

// "name-00002-of-00003.gguf": the part number and the count, and the name
// with the part number set to 1. False for a file that is not a part.
bool SplitPart(const std::string& name, int& part, int& count, std::string& firstName) {
    const std::string lower = Lower(name);
    if (!EndsWith(lower, ".gguf") || name.size() < 5 + 15) return false;
    const size_t at = name.size() - 5 - 15;     // "-00001-of-00003"
    const std::string tag = lower.substr(at, 15);
    if (tag[0] != '-' || tag.compare(6, 4, "-of-") != 0) return false;
    for (size_t i : {1u, 2u, 3u, 4u, 5u, 10u, 11u, 12u, 13u, 14u}) {
        if (tag[i] < '0' || tag[i] > '9') return false;
    }
    part  = std::atoi(tag.substr(1, 5).c_str());
    count = std::atoi(tag.substr(10, 5).c_str());
    firstName = name.substr(0, at) + "-00001" + name.substr(at + 6);
    return part >= 1 && count >= 1;
}

} // namespace

// ===== Public parts =====

bool IsProjectorFile(const LocalModelFile& file) {
    return file.projector || file.name.rfind("mmproj", 0) == 0;
}

bool IsSafeModelFileName(const std::string& name) {
    if (name.empty() || name == "." || name == ".." || name.size() > 200) return false;
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7F || c == '/' || c == '\\' || c == ':') return false;
    }
    return true;
}

std::string NormalizeSourceId(const std::string& id) {
    std::string out;
    for (unsigned char c : id) {
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
        else if (c == '-') out += '-';
    }
    if (out.size() > 40) out.resize(40);
    if (out == "file" || out == "hf") return {};
    return out;
}

std::string ResolveSourceUrl(const std::string& url) {
    if (StartsWith(url, "https://")) {
        // Nothing an address has no use for.
        for (unsigned char c : url) {
            if (c <= 0x20 || c == 0x7F) return {};
        }
        return url;
    }
    // "<scheme>owner/repository/path[@revision]".
    auto split = [](const std::string& rest, std::string& repo, std::string& path, std::string& revision) {
        const size_t first = rest.find('/');
        if (first == std::string::npos) return false;
        const size_t second = rest.find('/', first + 1);
        if (second == std::string::npos || second + 1 >= rest.size()) return false;
        repo = rest.substr(0, second);
        path = rest.substr(second + 1);
        const size_t at = path.rfind('@');
        if (at != std::string::npos) {
            revision = path.substr(at + 1);
            path.resize(at);
        }
        for (const std::string* part : {&repo, &path, &revision}) {
            for (unsigned char c : *part) {
                if (c <= 0x20 || c == 0x7F) return false;
            }
        }
        return !path.empty() && path.find("..") == std::string::npos && repo[0] != '/' && first > 0;
    };
    std::string repo, path, revision;
    for (const char* scheme : {"huggingface://", "hf://"}) {
        if (!StartsWith(url, scheme)) continue;
        if (!split(url.substr(std::string(scheme).size()), repo, path, revision)) return {};
        return HuggingFaceFileUrl(repo, revision, path);
    }
    for (const char* scheme : {"github://", "github:"}) {
        if (!StartsWith(url, scheme)) continue;
        if (!split(url.substr(std::string(scheme).size()), repo, path, revision)) return {};
        return "https://raw.githubusercontent.com/" + repo + "/" + (revision.empty() ? std::string("main") : revision) +
               "/" + path;
    }
    return {};
}

std::vector<std::string> SafeEngineArgs(const std::vector<std::string>& args) {
    auto isNumber = [](const std::string& s) {
        return !s.empty() && s.size() <= 9 && s.find_first_not_of("0123456789") == std::string::npos;
    };
    auto isWord = [](const std::string& s) {
        if (s.empty() || s.size() > 32) return false;
        for (unsigned char c : s) {
            if (!std::isalnum(c) && c != '-' && c != '_') return false;
        }
        return true;
    };
    std::vector<std::string> out;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--embeddings" || a == "--jinja") {
            out.push_back(a);
        } else if ((a == "--ctx-size" || a == "--parallel" || a == "--batch-size" || a == "--ubatch-size") &&
                   i + 1 < args.size() && isNumber(args[i + 1])) {
            out.push_back(a);
            out.push_back(args[++i]);
        } else if ((a == "--pooling" || a == "--reasoning-format") && i + 1 < args.size() && isWord(args[i + 1])) {
            out.push_back(a);
            out.push_back(args[++i]);
        }
    }
    return out;
}

bool YamlToJson(const std::string& yaml, std::string& outJson, std::string& outError) {
    if (yaml.size() > 64u * 1024 * 1024) {
        outError = "the document is larger than 64 MB";
        return false;
    }
    try {
        YamlReader reader(yaml);
        const json value = reader.Read();
        outJson = value.dump(-1, ' ', false, json::error_handler_t::replace);
        return true;
    } catch (const YamlError& e) {
        outError = "line " + std::to_string(e.line + 1) + ": " + e.message;
        return false;
    }
}

bool ParseCatalogueSourceDocument(const std::string& text, const CatalogueSource& source,
                                  std::vector<LocalModelInfo>& out, int& outSkipped,
                                  std::string& outError) {
    out.clear();
    outSkipped = 0;
    size_t first = 0;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) first = 3;
    first = text.find_first_not_of(" \t\r\n", first);
    if (first == std::string::npos) {
        outError = "the document is empty";
        return false;
    }

    json doc;
    std::string asJson;         // the document as JSON text, when it came as YAML
    if (text[first] == '{' || text[first] == '[') {
        doc = json::parse(text.begin() + static_cast<std::ptrdiff_t>(first), text.end(), nullptr, false);
        if (doc.is_discarded()) {
            outError = "the document is not valid JSON";
            return false;
        }
    } else {
        if (!YamlToJson(text, asJson, outError)) return false;
        doc = json::parse(asJson, nullptr, false);
    }

    // One of ours: {"formatVersion": 1, "models": [...]}.
    if (doc.is_object()) {
        Catalogue catalogue;
        if (!ParseCatalogue(asJson.empty() ? text.substr(first) : asJson, catalogue, outError)) return false;
        std::set<std::string> ids;
        for (LocalModelInfo& model : catalogue.models) {
            if (AdoptSourceModel(model, source) && ids.insert(model.id).second) out.push_back(std::move(model));
            else ++outSkipped;
        }
        return true;
    }
    // A LocalAI model gallery: a list of entries.
    if (!doc.is_array()) {
        outError = "the document is neither a model catalogue nor a model gallery";
        return false;
    }
    std::set<std::string> ids;
    bool anyEntry = false;
    for (const auto& entry : doc) {
        if (!entry.is_object()) continue;
        anyEntry = true;
        LocalModelInfo model;
        if (MapLocalAiEntry(entry, source, model) && ids.insert(model.id).second) out.push_back(std::move(model));
        else ++outSkipped;
    }
    if (!anyEntry) {
        outError = "the document lists no models";
        return false;
    }
    return true;
}

// ===== Hugging Face =====

std::string HuggingFaceSearchUrl(const std::string& query, bool ggufOnly, int limit) {
    std::string url = "https://huggingface.co/api/models?sort=downloads&direction=-1&limit=" +
                      std::to_string(std::clamp(limit, 1, 100));
    if (ggufOnly) url += "&filter=gguf";
    const std::string q = Trim(query);
    if (!q.empty()) url += "&search=" + UrlEncode(q);
    return url;
}

std::string HuggingFaceRepoUrl(const std::string& repo) {
    return "https://huggingface.co/api/models/" + EncodeRepoPath(repo);
}

bool ParseHuggingFaceSearch(const std::string& text, std::vector<HuggingFaceRepo>& out) {
    out.clear();
    const json doc = json::parse(text, nullptr, false);
    if (!doc.is_array()) return false;
    for (const auto& entry : doc) {
        HuggingFaceRepo repo;
        if (!ReadRepo(entry, repo)) continue;
        auto priv = entry.find("private");
        if (priv != entry.end() && priv->is_boolean() && priv->get<bool>()) continue;
        out.push_back(std::move(repo));
    }
    return true;
}

bool ParseHuggingFaceRepo(const std::string& text, HuggingFaceRepo& out) {
    const json doc = json::parse(text, nullptr, false);
    return ReadRepo(doc, out);
}

std::string QuantizationOf(const std::string& fileName) {
    // The last part of the name that reads as a quantisation: Q4_K_M, Q8_0,
    // IQ2_XS, F16, BF16, FP16.
    std::string name = BaseName(fileName);
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos) name.resize(dot);
    std::string best;
    size_t start = 0;
    while (start <= name.size()) {
        size_t end = start;
        while (end < name.size() && name[end] != '-' && name[end] != '.') ++end;
        std::string part = name.substr(start, end - start);
        std::transform(part.begin(), part.end(), part.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        size_t i = 0;
        if (StartsWith(part, "IQ")) i = 2;
        else if (StartsWith(part, "BF") || StartsWith(part, "FP")) i = 2;
        else if (StartsWith(part, "Q") || StartsWith(part, "F")) i = 1;
        if (i && i < part.size() && std::isdigit(static_cast<unsigned char>(part[i]))) {
            bool shaped = true;
            for (size_t n = i; n < part.size(); ++n) {
                const unsigned char c = static_cast<unsigned char>(part[n]);
                if (!std::isalnum(c) && c != '_') shaped = false;
            }
            if (shaped && part.size() <= 10) best = part;
        }
        if (end >= name.size()) break;
        start = end + 1;
    }
    return best;
}

std::string GuessCapability(const HuggingFaceRepo& repo, const std::string& fileName) {
    const std::string name = Lower(BaseName(fileName));
    if (EndsWith(name, ".bin")) return "speechtotext";
    const std::string pipeline = Lower(repo.pipelineTag);
    if (pipeline == "feature-extraction" || pipeline == "sentence-similarity") return "embeddings";
    for (const std::string& tag : repo.tags) {
        const std::string t = Lower(tag);
        if (t == "sentence-transformers" || t == "embeddings" || t == "embedding" || t == "feature-extraction") {
            return "embeddings";
        }
    }
    if (name.find("embed") != std::string::npos) return "embeddings";
    return "textllm";
}

bool ParseHuggingFaceFiles(const std::string& treeJson, const HuggingFaceRepo& repo,
                           std::vector<HuggingFaceFile>& out) {
    out.clear();
    const json doc = json::parse(treeJson, nullptr, false);
    if (!doc.is_array()) return false;
    std::map<std::string, size_t> firstParts;       // a split model's first part -> its place in `out`
    struct Later { std::string firstName, name; uint64_t size; };
    std::vector<Later> laterParts;
    for (const auto& entry : doc) {
        if (!entry.is_object() || JsonString(entry, "type") != "file") continue;
        HuggingFaceFile file;
        file.name = JsonString(entry, "path");
        const std::string base = Lower(BaseName(file.name));
        const bool gguf = EndsWith(base, ".gguf");
        const bool whisper = EndsWith(base, ".bin") && StartsWith(base, "ggml-");
        if (file.name.empty() || (!gguf && !whisper) || file.name.find("..") != std::string::npos) continue;
        const int64_t size = JsonInt(entry, "size");
        file.sizeBytes = size > 0 ? static_cast<uint64_t>(size) : 0;
        auto lfs = entry.find("lfs");
        if (lfs != entry.end() && lfs->is_object()) {
            const std::string oid = Lower(JsonString(*lfs, "oid"));
            if (IsSha256(oid)) file.sha256 = oid;
            const int64_t lfsSize = JsonInt(*lfs, "size");
            if (lfsSize > 0) file.sizeBytes = static_cast<uint64_t>(lfsSize);
        }
        int part = 0, count = 0;
        std::string firstName;
        if (gguf && SplitPart(file.name, part, count, firstName) && part > 1) {
            laterParts.push_back({firstName, file.name, file.sizeBytes});
            continue;
        }
        file.quantization = gguf ? QuantizationOf(file.name) : std::string();
        file.projector    = gguf && base.find("mmproj") != std::string::npos;
        file.capability   = GuessCapability(repo, file.name);
        if (gguf && part == 1) firstParts[file.name] = out.size();
        out.push_back(std::move(file));
    }
    // The further parts join their first: one model, one size.
    std::sort(laterParts.begin(), laterParts.end(),
              [](const Later& a, const Later& b) { return a.name < b.name; });
    for (const Later& later : laterParts) {
        auto it = firstParts.find(later.firstName);
        if (it == firstParts.end()) continue;
        out[it->second].parts.push_back(later.name);
        out[it->second].sizeBytes += later.size;
    }
    // Models before projectors, each in the repository's order.
    std::stable_sort(out.begin(), out.end(), [](const HuggingFaceFile& a, const HuggingFaceFile& b) {
        return !a.projector && b.projector;
    });
    return true;
}

} // namespace local_detail

std::vector<CatalogueSource> DefaultCatalogueSources() {
    CatalogueSource localAi;
    localAi.id      = "localai";
    localAi.name    = "LocalAI model gallery";
    localAi.url     = "github:mudler/LocalAI/gallery/index.yaml@master";
    localAi.enabled = true;
    return {localAi};
}

} // namespace UltraAI
