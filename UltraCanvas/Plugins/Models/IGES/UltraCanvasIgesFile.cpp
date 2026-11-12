// Plugins/Models/IGES/UltraCanvasIgesFile.cpp
// The IGES fixed-format syntax layer. Declared in UltraCanvasIgesFile.h.
//
// Version: 1.0.0
// Last Modified: 2026-11-12
// Author: UltraCanvas Framework

#include "Models/IGES/UltraCanvasIgesFile.h"
#include "UltraCanvasTextUtils.h"   // TryParseFloat / FormatFloatClassic - IGES numbers are dot-decimal

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <sstream>

namespace UltraCanvas {
namespace IgesFile {

namespace {

const Parameter kEmptyParameter{};

std::string TrimRight(const std::string& text) {
    size_t end = text.size();
    while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
    return text.substr(0, end);
}

std::string Trim(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    return TrimRight(text.substr(begin));
}

// A fixed-width integer field: blank is zero, and so is anything unreadable.
int FieldInteger(const std::string& field) {
    const std::string text = Trim(field);
    if (text.empty()) return 0;
    long long value = 0;
    bool negative = false;
    size_t at = 0;
    if (text[0] == '-' || text[0] == '+') { negative = text[0] == '-'; at = 1; }
    for (; at < text.size(); ++at) {
        if (!std::isdigit(static_cast<unsigned char>(text[at]))) break;
        value = value * 10 + (text[at] - '0');
        if (value > 100000000LL) break;
    }
    return static_cast<int>(negative ? -value : value);
}

// Field k (0-based) of a directory line: columns 8k+1 .. 8k+8.
std::string Field(const std::string& line, int index) {
    const size_t from = static_cast<size_t>(index) * 8;
    if (from >= line.size()) return std::string();
    return line.substr(from, 8);
}

// "nH..." at `at`: the count of characters that follow the H, or -1 when the
// text there is not the start of a Hollerith string.
int HollerithLength(const std::string& text, size_t at, size_t& afterH) {
    size_t i = at;
    int count = 0;
    bool digits = false;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        count = count * 10 + (text[i] - '0');
        if (count > 1000000) return -1;
        digits = true;
        ++i;
    }
    if (!digits || i >= text.size() || (text[i] != 'H' && text[i] != 'h')) return -1;
    afterH = i + 1;
    return count;
}

// Reads free-format parameters from `text` starting at `at`, until the
// record delimiter or the end of the text.
void ParseParameters(const std::string& text, size_t at, char parameterDelimiter,
                     char recordDelimiter, std::vector<Parameter>& out) {
    const size_t n = text.size();
    auto skipBlanks = [&]() {
        while (at < n && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' ||
                          text[at] == '\n'))
            ++at;
    };

    while (true) {
        skipBlanks();
        if (at >= n) break;

        Parameter parameter;
        if (text[at] == parameterDelimiter || text[at] == recordDelimiter) {
            // An empty field: the entity's default.
        } else {
            size_t afterH = 0;
            const int length = HollerithLength(text, at, afterH);
            if (length >= 0) {
                parameter.Type = Parameter::Kind::String;
                const size_t take = std::min(static_cast<size_t>(length), n - afterH);
                parameter.Text = text.substr(afterH, take);
                at = afterH + take;
            } else {
                size_t end = at;
                while (end < n && text[end] != parameterDelimiter && text[end] != recordDelimiter)
                    ++end;
                std::string token = Trim(text.substr(at, end - at));
                at = end;
                // Fortran's double-precision exponent.
                for (char& c : token)
                    if (c == 'D' || c == 'd') c = 'E';
                double value = 0.0;
                if (!token.empty() && TryParseFloat(token, value)) {
                    parameter.Type = Parameter::Kind::Number;
                    parameter.Number = value;
                }
            }
        }
        out.push_back(std::move(parameter));

        skipBlanks();
        if (at >= n) break;
        if (text[at] == recordDelimiter) break;
        if (text[at] == parameterDelimiter) { ++at; continue; }
        // Garbage between a value and its delimiter: skip to the next one.
        while (at < n && text[at] != parameterDelimiter && text[at] != recordDelimiter) ++at;
        if (at < n && text[at] == parameterDelimiter) ++at;
        else break;
    }
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    // A deck written without line ends at all - 80-column cards back to back
    // - still turns up from older systems.
    if (text.find('\n') == std::string::npos && text.size() >= 160 && text.size() % 80 == 0) {
        for (size_t at = 0; at < text.size(); at += 80) lines.push_back(text.substr(at, 80));
        return lines;
    }
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\x1a')) line.pop_back();
        if (!line.empty()) lines.push_back(std::move(line));
        if (end >= text.size()) break;
        start = end + 1;
    }
    return lines;
}

std::string RightJustify(const std::string& text, size_t width) {
    if (text.size() >= width) return text.substr(text.size() - width);
    return std::string(width - text.size(), ' ') + text;
}

std::string SequenceTag(char section, int number) {
    return std::string(1, section) + RightJustify(std::to_string(number), 7);
}

} // namespace

// ===== PARAMETER / ENTITY =====

int Parameter::AsInteger(int fallback) const {
    if (Type != Kind::Number || !std::isfinite(Number)) return fallback;
    if (Number > 2.0e9 || Number < -2.0e9) return fallback;
    return static_cast<int>(std::lround(Number));
}

int Parameter::AsPointer() const {
    const int value = AsInteger(0);
    return value < 0 ? -value : value;
}

const Parameter& Entity::Param(size_t index) const {
    return index < Params.size() ? Params[index] : kEmptyParameter;
}

int Entity::StatusField(int index) const {
    if (index < 0 || index > 3) return 0;
    const size_t from = static_cast<size_t>(index) * 2;
    if (from + 2 > StatusNumber.size()) return 0;
    return FieldInteger(StatusNumber.substr(from, 2));
}

// ===== MODEL =====

const Entity* Model::At(int directory) const {
    auto found = Entities.find(directory);
    return found == Entities.end() ? nullptr : &found->second;
}

std::string Model::GlobalText(int number) const {
    if (number < 1 || static_cast<size_t>(number) > Global.size()) return std::string();
    return Global[static_cast<size_t>(number - 1)].AsText();
}

double Model::GlobalReal(int number, double fallback) const {
    if (number < 1 || static_cast<size_t>(number) > Global.size()) return fallback;
    return Global[static_cast<size_t>(number - 1)].AsReal(fallback);
}

int Model::GlobalInteger(int number, int fallback) const {
    if (number < 1 || static_cast<size_t>(number) > Global.size()) return fallback;
    return Global[static_cast<size_t>(number - 1)].AsInteger(fallback);
}

bool Model::Parse(std::istream& stream, const WarningSink& warn) {
    const std::string text((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());
    return ParseText(text, warn);
}

bool Model::ParseText(const std::string& text, const WarningSink& warn) {
    auto say = [&warn](const std::string& message) {
        if (warn) warn(message);
    };

    Start.clear();
    Global.clear();
    Entities.clear();
    ParameterDelimiter = ',';
    RecordDelimiter = ';';

    const std::vector<std::string> lines = SplitLines(text);
    if (lines.empty()) {
        say("IGES: the file is empty");
        return false;
    }

    std::string globalText;
    std::vector<std::string> directoryLines;
    std::vector<int> directoryNumbers;
    std::map<int, std::string> parameterText;   // owner directory -> its columns 1-64
    size_t skipped = 0;
    bool sawStart = false;

    for (const std::string& raw : lines) {
        if (raw.size() < 73) { ++skipped; continue; }
        const std::string line = raw.size() < 80 ? raw + std::string(80 - raw.size(), ' ') : raw;
        const char section = line[72];
        switch (section) {
            case 'S':
                sawStart = true;
                if (!Start.empty()) Start += '\n';
                Start += TrimRight(line.substr(0, 72));
                break;
            case 'G':
                globalText += line.substr(0, 72);
                break;
            case 'D':
                directoryLines.push_back(line.substr(0, 72));
                directoryNumbers.push_back(FieldInteger(line.substr(73, 7)));
                break;
            case 'P': {
                const int owner = FieldInteger(line.substr(64, 8));
                parameterText[owner] += line.substr(0, 64);
                break;
            }
            case 'T':
                break;
            case 'C':
                say("IGES: this is the compressed ASCII form, which is not read; "
                    "export the file again as fixed-format IGES");
                return false;
            default:
                ++skipped;
                break;
        }
    }

    if (!sawStart && globalText.empty()) {
        say("IGES: no start or global section; this is not fixed-format IGES");
        return false;
    }
    if (skipped > 0)
        say("IGES: " + std::to_string(skipped) + " line(s) were not 80-column IGES records "
            "and were ignored");

    // --- the global section names its own delimiters ---
    size_t at = 0;
    auto skipBlanks = [&]() {
        while (at < globalText.size() && globalText[at] == ' ') ++at;
    };
    skipBlanks();
    size_t afterH = 0;
    if (HollerithLength(globalText, at, afterH) == 1 && afterH < globalText.size()) {
        ParameterDelimiter = globalText[afterH];
        at = afterH + 1;
    }
    // The first field ends with the delimiter it defines; a comma there is
    // taken too, which some writers put in regardless.
    auto skipDelimiter = [&]() {
        skipBlanks();
        if (at < globalText.size() &&
            (globalText[at] == ParameterDelimiter || globalText[at] == ','))
            ++at;
        skipBlanks();
    };
    skipDelimiter();
    if (HollerithLength(globalText, at, afterH) == 1 && afterH < globalText.size()) {
        RecordDelimiter = globalText[afterH];
        at = afterH + 1;
        skipDelimiter();
    } else if (at < globalText.size() && globalText[at] == ParameterDelimiter) {
        ++at;   // an empty second field: the default ';'
    }
    {
        Parameter first;
        first.Type = Parameter::Kind::String;
        first.Text = std::string(1, ParameterDelimiter);
        Parameter second;
        second.Type = Parameter::Kind::String;
        second.Text = std::string(1, RecordDelimiter);
        Global.push_back(first);
        Global.push_back(second);
    }
    if (at < globalText.size())
        ParseParameters(globalText, at, ParameterDelimiter, RecordDelimiter, Global);

    // --- the directory, two lines per entity ---
    if (directoryLines.size() % 2 != 0)
        say("IGES: the directory section has an odd number of lines; the last is ignored");
    size_t bad = 0;
    for (size_t i = 0; i + 1 < directoryLines.size(); i += 2) {
        const std::string& first = directoryLines[i];
        const std::string& second = directoryLines[i + 1];
        Entity entity;
        entity.Directory = directoryNumbers[i] > 0 ? directoryNumbers[i]
                                                   : static_cast<int>(i) + 1;
        entity.Type = FieldInteger(Field(first, 0));
        entity.ParameterStart = FieldInteger(Field(first, 1));
        entity.Structure = FieldInteger(Field(first, 2));
        entity.LineFont = FieldInteger(Field(first, 3));
        entity.Level = FieldInteger(Field(first, 4));
        entity.View = FieldInteger(Field(first, 5));
        entity.Transform = std::abs(FieldInteger(Field(first, 6)));
        entity.LabelDisplay = FieldInteger(Field(first, 7));
        std::string status = Field(first, 8);
        status.resize(8, ' ');
        for (char& c : status)
            if (c == ' ') c = '0';
        entity.StatusNumber = status;
        entity.LineWeight = FieldInteger(Field(second, 1));
        entity.Color = FieldInteger(Field(second, 2));
        entity.ParameterLineCount = FieldInteger(Field(second, 3));
        entity.Form = FieldInteger(Field(second, 4));
        entity.Label = Trim(Field(second, 7));
        entity.Subscript = FieldInteger(Field(second, 8));

        auto found = parameterText.find(entity.Directory);
        if (found != parameterText.end()) {
            std::vector<Parameter> params;
            ParseParameters(found->second, 0, ParameterDelimiter, RecordDelimiter, params);
            if (!params.empty() && params[0].AsInteger(-1) == entity.Type) {
                params.erase(params.begin());
                entity.Params = std::move(params);
            } else {
                ++bad;
            }
        } else if (entity.Type != 0) {
            ++bad;
        }
        Entities[entity.Directory] = std::move(entity);
    }
    if (bad > 0)
        say("IGES: " + std::to_string(bad) + " entit" + (bad == 1 ? "y has" : "ies have") +
            " no parameter record that matches its directory entry");

    if (Entities.empty()) {
        say("IGES: the file holds no entities");
        return false;
    }
    return true;
}

bool LooksLikeIgesFile(const std::vector<uint8_t>& data) {
    // Line 1 is a start record: 'S' in column 73 and sequence number 1.
    if (data.size() < 80) return false;
    for (size_t i = 0; i < 72; ++i)
        if (data[i] == '\n' || data[i] == '\r') return false;   // a first line too short to be a card
    if (static_cast<char>(data[72]) != 'S') return false;
    int sequence = 0;
    for (size_t i = 73; i < 80; ++i) {
        const char c = static_cast<char>(data[i]);
        if (c == ' ') continue;
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        sequence = sequence * 10 + (c - '0');
    }
    return sequence == 1;
}

// ===== WRITING =====

std::string FormatInteger(long long value) {
    return std::to_string(value);
}

std::string FormatReal(double value, int significantDigits) {
    if (!std::isfinite(value)) value = 0.0;
    if (value == 0.0) return "0.";
    std::string text = FormatFloatClassic(value, significantDigits);
    for (char& c : text)
        if (c == 'e') c = 'E';
    if (text.find('.') == std::string::npos) {
        const size_t exponent = text.find('E');
        if (exponent == std::string::npos) text += '.';
        else text.insert(exponent, ".");
    }
    return text;
}

std::string FormatString(const std::string& text) {
    return std::to_string(text.size()) + "H" + text;
}

int FileWriter::Add(EntitySpec spec) {
    entities_.push_back(std::move(spec));
    return static_cast<int>(entities_.size()) * 2 - 1;
}

FileWriter::EntitySpec& FileWriter::Get(int directory) {
    return entities_[static_cast<size_t>((directory - 1) / 2)];
}

namespace {

// Lays tokens out into lines of `width` columns, each token followed by its
// delimiter. A token wider than a line - only a long string can be - is split
// across lines, which a Hollerith string is allowed to be.
std::vector<std::string> PackTokens(const std::vector<std::string>& tokens, size_t width,
                                    char parameterDelimiter, char recordDelimiter) {
    std::vector<std::string> lines;
    std::string current;
    for (size_t i = 0; i < tokens.size(); ++i) {
        std::string piece = tokens[i];
        piece += (i + 1 == tokens.size()) ? recordDelimiter : parameterDelimiter;
        if (current.size() + piece.size() <= width) {
            current += piece;
            continue;
        }
        if (piece.size() <= width) {
            if (!current.empty()) lines.push_back(current);
            current = piece;
            continue;
        }
        // Too wide for any line: fill this one and carry on into the next.
        size_t at = 0;
        while (at < piece.size()) {
            const size_t room = width - current.size();
            if (room == 0) { lines.push_back(current); current.clear(); continue; }
            const size_t take = std::min(room, piece.size() - at);
            current += piece.substr(at, take);
            at += take;
        }
    }
    if (!current.empty() || lines.empty()) lines.push_back(current);
    return lines;
}

} // namespace

void FileWriter::Write(std::ostream& out, const std::string& start,
                       const std::vector<std::string>& global) const {
    // Built as strings and written in one go: nothing here goes through the
    // stream's own number formatting, so its locale cannot reach the file.
    std::string text;

    // --- S ---
    int startCount = 0;
    {
        std::string remaining = start.empty() ? std::string(" ") : start;
        std::vector<std::string> startLines;
        std::istringstream split(remaining);
        std::string paragraph;
        while (std::getline(split, paragraph)) {
            if (paragraph.empty()) paragraph = " ";
            for (size_t at = 0; at < paragraph.size(); at += 72)
                startLines.push_back(paragraph.substr(at, 72));
        }
        if (startLines.empty()) startLines.push_back(" ");
        for (const std::string& line : startLines) {
            std::string padded = line;
            padded.resize(72, ' ');
            text += padded + SequenceTag('S', ++startCount) + "\n";
        }
    }

    // --- G ---
    int globalCount = 0;
    {
        std::vector<std::string> tokens = {"1H,", "1H;"};
        tokens.insert(tokens.end(), global.begin(), global.end());
        for (std::string line : PackTokens(tokens, 72, ',', ';')) {
            line.resize(72, ' ');
            text += line + SequenceTag('G', ++globalCount) + "\n";
        }
    }

    // --- P, laid out first so the directory can point into it ---
    std::vector<std::string> parameterLines;
    std::vector<int> firstLine(entities_.size(), 0);
    std::vector<int> lineCount(entities_.size(), 0);
    for (size_t i = 0; i < entities_.size(); ++i) {
        const EntitySpec& entity = entities_[i];
        std::vector<std::string> tokens;
        tokens.reserve(entity.Params.size() + 1);
        tokens.push_back(FormatInteger(entity.Type));
        tokens.insert(tokens.end(), entity.Params.begin(), entity.Params.end());
        const int directory = static_cast<int>(i) * 2 + 1;
        firstLine[i] = static_cast<int>(parameterLines.size()) + 1;
        for (std::string line : PackTokens(tokens, 64, ',', ';')) {
            line.resize(64, ' ');
            line += ' ';
            line += RightJustify(std::to_string(directory), 7);
            parameterLines.push_back(line);
            ++lineCount[i];
        }
    }

    // --- D ---
    auto field = [](const std::string& value) { return RightJustify(value, 8); };
    auto number = [&field](long long value) { return field(std::to_string(value)); };
    for (size_t i = 0; i < entities_.size(); ++i) {
        const EntitySpec& entity = entities_[i];
        std::string status = entity.StatusNumber;
        status.resize(8, '0');
        std::string label = entity.Label.substr(0, 8);
        const int directory = static_cast<int>(i) * 2 + 1;

        std::string first = number(entity.Type) + number(firstLine[i]) + number(0) + number(0) +
                            number(0) + number(0) + number(entity.Transform) + number(0) + status;
        std::string second = number(entity.Type) + number(0) + number(entity.Color) +
                             number(lineCount[i]) + number(entity.Form) + field("") + field("") +
                             field(label) + number(entity.Subscript);
        text += first + SequenceTag('D', directory) + "\n";
        text += second + SequenceTag('D', directory + 1) + "\n";
    }

    for (size_t i = 0; i < parameterLines.size(); ++i)
        text += parameterLines[i] + SequenceTag('P', static_cast<int>(i) + 1) + "\n";

    // --- T ---
    std::string terminate = SequenceTag('S', startCount) + SequenceTag('G', globalCount) +
                            SequenceTag('D', static_cast<int>(entities_.size()) * 2) +
                            SequenceTag('P', static_cast<int>(parameterLines.size()));
    terminate.resize(72, ' ');
    text += terminate + SequenceTag('T', 1) + "\n";

    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

} // namespace IgesFile
} // namespace UltraCanvas
