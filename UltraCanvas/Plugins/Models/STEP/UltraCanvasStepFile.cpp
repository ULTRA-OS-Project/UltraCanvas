// Plugins/Models/STEP/UltraCanvasStepFile.cpp
// The ISO 10303-21 syntax layer. Declared in UltraCanvasStepFile.h.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/STEP/UltraCanvasStepFile.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace UltraCanvas {
namespace StepFile {

// ===== VALUE =====

bool Value::AsBoolean(bool fallback) const {
    if (Type != ValueType::Enumeration) return fallback;
    return Text == "T" || Text == "TRUE";
}

std::vector<double> Value::AsNumbers() const {
    std::vector<double> numbers;
    if (!IsList()) return numbers;
    numbers.reserve(Items.size());
    for (const Value& item : Items)
        if (item.IsNumber()) numbers.push_back(item.Number);
    return numbers;
}

std::vector<int> Value::AsReferences() const {
    std::vector<int> references;
    if (!IsList()) return references;
    references.reserve(Items.size());
    for (const Value& item : Items)
        if (item.IsReference()) references.push_back(item.Reference);
    return references;
}

// ===== ENTITY =====

namespace {
const std::string kEmptyString;
const std::vector<Value> kEmptyParams;
const Value kUnsetValue;
} // namespace

const std::string& Entity::Keyword() const {
    return Records.size() == 1 ? Records[0].Keyword : kEmptyString;
}

bool Entity::Is(const std::string& keyword) const {
    for (const Record& record : Records)
        if (record.Keyword == keyword) return true;
    return false;
}

const std::vector<Value>* Entity::Params(const std::string& keyword) const {
    for (const Record& record : Records)
        if (record.Keyword == keyword) return &record.Params;
    return nullptr;
}

const std::vector<Value>& Entity::SimpleParams() const {
    return Records.size() == 1 ? Records[0].Params : kEmptyParams;
}

const Value& Entity::Param(const std::string& keyword, size_t index) const {
    const std::vector<Value>* params = Params(keyword);
    if (!params || index >= params->size()) return kUnsetValue;
    return (*params)[index];
}

const Value& Entity::Param(size_t index) const {
    const std::vector<Value>& params = SimpleParams();
    if (index >= params.size()) return kUnsetValue;
    return params[index];
}

// ===== PARSER =====

namespace {

// A cursor over the whole file. Part 21 is small-token, deeply nested and
// entirely ASCII, so a hand-written scanner beats anything general here — and
// it is the only way to keep the string rules (doubled quotes, \X2\ escapes)
// from leaking into a tokeniser that would otherwise mangle them.
class Parser {
public:
    Parser(const std::string& text, const std::function<void(const std::string&)>& warn)
            : text_(text), warn_(warn) {}

    bool Run(Model& model);

private:
    const std::string& text_;
    const std::function<void(const std::string&)>& warn_;
    size_t at_ = 0;

    void Warn(const std::string& message) const { if (warn_) warn_(message); }

    bool AtEnd() const { return at_ >= text_.size(); }
    char Peek() const { return at_ < text_.size() ? text_[at_] : '\0'; }

    // Whitespace and /* comments */. Part 21 comments do not nest and cannot
    // appear inside a string, which is why this is only ever called between
    // tokens.
    void SkipTrivia() {
        while (at_ < text_.size()) {
            const char c = text_[at_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
                ++at_;
            } else if (c == '/' && at_ + 1 < text_.size() && text_[at_ + 1] == '*') {
                const size_t end = text_.find("*/", at_ + 2);
                if (end == std::string::npos) { at_ = text_.size(); return; }
                at_ = end + 2;
            } else {
                return;
            }
        }
    }

    bool Consume(char expected) {
        SkipTrivia();
        if (Peek() != expected) return false;
        ++at_;
        return true;
    }

    // A keyword: letters, digits and underscore, optionally introduced by '!'
    // for a user-defined entity.
    std::string ReadKeyword() {
        SkipTrivia();
        const size_t start = at_;
        if (Peek() == '!') ++at_;
        while (at_ < text_.size()) {
            const char c = text_[at_];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ++at_;
            else break;
        }
        std::string keyword = text_.substr(start, at_ - start);
        for (char& c : keyword) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return keyword;
    }

    // Part 21 strings are single-quoted; a literal quote is written twice.
    // The \X\, \X2\ and \S\ escapes encode non-ASCII; the common ones are
    // decoded so a part named with an accent does not arrive as mojibake.
    std::string ReadString() {
        std::string out;
        ++at_;   // the opening quote
        while (at_ < text_.size()) {
            const char c = text_[at_];
            if (c == '\'') {
                if (at_ + 1 < text_.size() && text_[at_ + 1] == '\'') { out += '\''; at_ += 2; continue; }
                ++at_;
                return DecodeEscapes(out);
            }
            out += c;
            ++at_;
        }
        Warn("STEP: a string literal is not closed before the end of the file");
        return DecodeEscapes(out);
    }

    static void AppendUtf8(std::string& out, unsigned int code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    static std::string DecodeEscapes(const std::string& raw) {
        if (raw.find('\\') == std::string::npos) return raw;
        std::string out;
        out.reserve(raw.size());
        for (size_t i = 0; i < raw.size();) {
            if (raw[i] != '\\') { out += raw[i++]; continue; }
            // \X2\<4-hex per character>\X0\  — UTF-16 code units.
            if (raw.compare(i, 4, "\\X2\\") == 0) {
                size_t j = i + 4;
                while (j + 4 <= raw.size() && raw.compare(j, 4, "\\X0\\") != 0) {
                    const unsigned int code = static_cast<unsigned int>(
                            std::strtoul(raw.substr(j, 4).c_str(), nullptr, 16));
                    AppendUtf8(out, code);
                    j += 4;
                }
                i = (j + 4 <= raw.size() && raw.compare(j, 4, "\\X0\\") == 0) ? j + 4 : raw.size();
                continue;
            }
            // \X\<2 hex> — one 8-bit character.
            if (raw.compare(i, 3, "\\X\\") == 0 && i + 5 <= raw.size()) {
                const unsigned int code = static_cast<unsigned int>(
                        std::strtoul(raw.substr(i + 3, 2).c_str(), nullptr, 16));
                AppendUtf8(out, code);
                i += 5;
                continue;
            }
            out += raw[i++];
        }
        return out;
    }

    Value ReadValue() {
        SkipTrivia();
        Value value;
        if (AtEnd()) return value;

        const char c = Peek();
        if (c == '$') { ++at_; value.Type = ValueType::Unset; return value; }
        if (c == '*') { ++at_; value.Type = ValueType::Derived; return value; }

        if (c == '#') {
            ++at_;
            const size_t start = at_;
            while (at_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[at_]))) ++at_;
            value.Type = ValueType::Reference;
            value.Reference = std::atoi(text_.substr(start, at_ - start).c_str());
            return value;
        }

        if (c == '\'') {
            value.Type = ValueType::String;
            value.Text = ReadString();
            return value;
        }

        if (c == '"') {
            ++at_;
            const size_t start = at_;
            while (at_ < text_.size() && text_[at_] != '"') ++at_;
            value.Type = ValueType::Binary;
            value.Text = text_.substr(start, at_ - start);
            if (at_ < text_.size()) ++at_;
            return value;
        }

        if (c == '.') {
            ++at_;
            const size_t start = at_;
            while (at_ < text_.size() && text_[at_] != '.') ++at_;
            value.Type = ValueType::Enumeration;
            value.Text = text_.substr(start, at_ - start);
            if (at_ < text_.size()) ++at_;
            return value;
        }

        if (c == '(') {
            value.Type = ValueType::List;
            value.Items = ReadParameterList();
            return value;
        }

        if (c == '-' || c == '+' || std::isdigit(static_cast<unsigned char>(c))) {
            const size_t start = at_;
            bool real = false;
            if (c == '-' || c == '+') ++at_;
            while (at_ < text_.size()) {
                const char d = text_[at_];
                if (std::isdigit(static_cast<unsigned char>(d))) { ++at_; continue; }
                if (d == '.') { real = true; ++at_; continue; }
                if (d == 'E' || d == 'e') {
                    real = true;
                    ++at_;
                    if (at_ < text_.size() && (text_[at_] == '-' || text_[at_] == '+')) ++at_;
                    continue;
                }
                break;
            }
            const std::string number = text_.substr(start, at_ - start);
            value.Number = std::atof(number.c_str());
            if (real) {
                value.Type = ValueType::Real;
            } else {
                value.Type = ValueType::Integer;
                value.Whole = std::atoll(number.c_str());
            }
            return value;
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '!') {
            // A typed parameter: LENGTH_MEASURE(25.4), or a select type's
            // wrapper. Its own parameters follow in parentheses.
            value.Type = ValueType::Typed;
            value.Text = ReadKeyword();
            SkipTrivia();
            if (Peek() == '(') value.Items = ReadParameterList();
            return value;
        }

        // Nothing recognisable: step over it so the scan cannot stall.
        ++at_;
        return value;
    }

    std::vector<Value> ReadParameterList() {
        std::vector<Value> params;
        if (!Consume('(')) return params;
        SkipTrivia();
        if (Peek() == ')') { ++at_; return params; }
        while (!AtEnd()) {
            params.push_back(ReadValue());
            SkipTrivia();
            const char c = Peek();
            if (c == ',') { ++at_; continue; }
            if (c == ')') { ++at_; break; }
            if (AtEnd()) break;
            // A stray character inside a list: skip it rather than abandoning
            // the instance.
            ++at_;
        }
        return params;
    }

    // The records of one instance: either KEYWORD(...) or ( KEYWORD(...) ... ).
    std::vector<Record> ReadRecords() {
        std::vector<Record> records;
        SkipTrivia();
        if (Peek() == '(') {
            ++at_;
            while (!AtEnd()) {
                SkipTrivia();
                if (Peek() == ')') { ++at_; break; }
                Record record;
                record.Keyword = ReadKeyword();
                if (record.Keyword.empty()) { ++at_; continue; }
                record.Params = ReadParameterList();
                records.push_back(std::move(record));
                SkipTrivia();
                if (Peek() == ',') ++at_;   // tolerated, though the standard has no comma here
            }
            return records;
        }
        Record record;
        record.Keyword = ReadKeyword();
        if (record.Keyword.empty()) return records;
        record.Params = ReadParameterList();
        records.push_back(std::move(record));
        return records;
    }

    // Everything to the next semicolon, used to resynchronise after trouble.
    void SkipToSemicolon() {
        while (at_ < text_.size()) {
            const char c = text_[at_];
            if (c == '\'') { ReadString(); continue; }
            ++at_;
            if (c == ';') return;
        }
    }
};

bool Parser::Run(Model& model) {
    SkipTrivia();
    // The magic line. A file without it is not Part 21, and guessing would
    // mean handing the converter a table of nonsense.
    if (text_.compare(at_, 13, "ISO-10303-21;") != 0) {
        Warn("STEP: the file does not begin with ISO-10303-21");
        return false;
    }
    at_ += 13;

    bool inHeader = false;
    bool inData = false;
    size_t duplicates = 0;
    size_t unparsed = 0;

    while (!AtEnd()) {
        SkipTrivia();
        if (AtEnd()) break;

        if (Peek() == '#') {
            ++at_;
            const size_t start = at_;
            while (at_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[at_]))) ++at_;
            const int id = std::atoi(text_.substr(start, at_ - start).c_str());
            SkipTrivia();
            if (Peek() != '=') { SkipToSemicolon(); ++unparsed; continue; }
            ++at_;

            Entity entity;
            entity.Id = id;
            entity.Records = ReadRecords();
            SkipTrivia();
            if (Peek() == ';') ++at_;

            if (entity.Records.empty()) { ++unparsed; continue; }
            if (!inData) {
                // Numbered instances outside DATA are not legal, but some
                // exporters emit them; keeping them costs nothing.
            }
            auto existing = model.Entities.find(id);
            if (existing != model.Entities.end()) { ++duplicates; continue; }
            model.Entities.emplace(id, std::move(entity));
            continue;
        }

        const std::string keyword = ReadKeyword();
        if (keyword.empty()) { ++at_; continue; }

        if (keyword == "HEADER") { inHeader = true; inData = false; Consume(';'); continue; }
        if (keyword == "DATA") {
            inData = true;
            inHeader = false;
            // DATA may carry a parameter list (the schema population); ignore it.
            SkipTrivia();
            if (Peek() == '(') ReadParameterList();
            Consume(';');
            continue;
        }
        if (keyword == "ENDSEC") { inHeader = false; inData = false; Consume(';'); continue; }
        if (keyword == "END") {
            // END-ISO-10303-21;
            SkipToSemicolon();
            break;
        }

        Record record;
        record.Keyword = keyword;
        record.Params = ReadParameterList();
        SkipTrivia();
        if (Peek() == ';') ++at_;
        if (inHeader) model.Header.push_back(std::move(record));
    }

    if (duplicates)
        Warn("STEP: " + std::to_string(duplicates) +
             " instance id(s) were defined twice; the first definition was kept");
    if (unparsed)
        Warn("STEP: " + std::to_string(unparsed) + " instance(s) could not be parsed and were skipped");
    return !model.Entities.empty();
}

} // namespace

bool Model::Parse(const std::string& text,
                  const std::function<void(const std::string&)>& warn) {
    Entities.clear();
    Header.clear();
    Parser parser(text, warn);
    return parser.Run(*this);
}

bool Model::Parse(std::istream& stream, const std::function<void(const std::string&)>& warn) {
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    std::string text = buffer.str();
    // A UTF-8 byte-order mark before ISO-10303-21 is common enough from
    // Windows exporters to be worth stepping over rather than rejecting.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);
    return Parse(text, warn);
}

const Entity* Model::Find(int id) const {
    auto found = Entities.find(id);
    return found == Entities.end() ? nullptr : &found->second;
}

std::vector<const Entity*> Model::OfType(const std::string& keyword) const {
    std::vector<const Entity*> found;
    for (const auto& entry : Entities)
        if (entry.second.Is(keyword)) found.push_back(&entry.second);
    return found;
}

const Record* Model::HeaderRecord(const std::string& keyword) const {
    for (const Record& record : Header)
        if (record.Keyword == keyword) return &record;
    return nullptr;
}

std::vector<std::string> Model::Schemas() const {
    std::vector<std::string> schemas;
    const Record* record = HeaderRecord("FILE_SCHEMA");
    if (!record || record->Params.empty() || !record->Params[0].IsList()) return schemas;
    for (const Value& item : record->Params[0].Items) {
        if (item.Type != ValueType::String) continue;
        std::string name = item.Text;
        for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        schemas.push_back(name);
    }
    return schemas;
}

std::string Model::SourceName() const {
    const Record* record = HeaderRecord("FILE_NAME");
    if (!record || record->Params.empty()) return {};
    return record->Params[0].Type == ValueType::String ? record->Params[0].Text : std::string();
}

std::string Model::OriginatingSystem() const {
    const Record* record = HeaderRecord("FILE_NAME");
    // FILE_NAME(name, time_stamp, author, organization, preprocessor, originating_system, authorisation)
    if (!record || record->Params.size() < 6) return {};
    return record->Params[5].Type == ValueType::String ? record->Params[5].Text : std::string();
}

bool LooksLikeStepFile(const std::vector<uint8_t>& data) {
    size_t at = 0;
    if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) at = 3;
    while (at < data.size() && (data[at] == ' ' || data[at] == '\t' ||
                                data[at] == '\r' || data[at] == '\n')) ++at;
    static const char kMagic[] = "ISO-10303-21";
    const size_t length = sizeof(kMagic) - 1;
    if (at + length > data.size()) return false;
    for (size_t i = 0; i < length; ++i)
        if (static_cast<char>(data[at + i]) != kMagic[i]) return false;
    return true;
}

} // namespace StepFile
} // namespace UltraCanvas
