// UltraCanvas/Plugins/Vector/UltraCanvasAIReader.cpp
// Adobe Illustrator (.ai) reader - the import side of AIConverter (the
// writer lives in UltraCanvasAIConverter.cpp).
//
// Why this exists: "a modern .ai file is a PDF, so open it with the PDF
// engine" is only half true. Illustrator's "Create PDF Compatible File"
// option decides whether the PDF page carries the artwork at all. With it
// off - and it is off in everything CorelDRAW and several other exporters
// write - the file is still a valid PDF, but its page content stream only
// sets a transform and a graphics state, and every byte of artwork lives in
// the private streams the PieceInfo /Illustrator dictionary points at
// (/AIPrivateData1../AIPrivateDataN, or /AIPrivateData when there is one
// block). A PDF viewer then renders exactly what the page says: a blank
// page. Both files under media/vector/AI are of that kind, which is why
// they came up empty in the MuPDF viewer and in the online .ai viewers
// they were tried against.
//
// So this reader goes after the private data. It locates the AIPrivateData
// streams in the PDF container, undoes their /Filter chain (ASCIIHexDecode
// / ASCII85Decode / FlateDecode), and interprets the result: Illustrator's
// own art language, the same PostScript-flavoured operator set that legacy
// (v8 and earlier) EPS-based .ai files carry in the open, which is why a
// legacy file is handled by the same parser with no container step.
//
// Implemented from the published Adobe Illustrator file format
// specification: path construction (m, l/L, c/C, v/V, y/Y), painting
// (n/N, f/F, s/S, b/B) with closepath on the lowercase forms, clipping
// (W), compound paths (*u/*U), groups (u/U), layers (Lb/Ln/LB), the
// graphics state (w, J, j, M, d, XR) and every colour operator (g/G grey,
// k/K CMYK, Xa/XA RGB, x/X and Xx/XX spot colours through their CMYK
// equivalent; p/P pattern fills keep the flat colour in force and say so)
// plus the AI9 transparency operator Xy. Gradient fills
// (Bd..BB) and text (To..TE) are counted and reported through the warning
// callback rather than dropped silently - the paths around them still come
// through.
//
// Coordinates: the art language places objects in Illustrator's ruler
// space, whose origin is the top-left corner of the artboard with y running
// down as negative numbers, while a legacy EPS-based .ai uses PostScript's
// bottom-left origin with y running up. Both map to the document's y-down
// page; ArtIsYDown() picks between them from %AI3_Cropmarks, which states
// the convention, or from the art's own extent when the file declares none.
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework

#include "UltraCanvasMetafileConverters.h"
#include "DataFormats/UltraCanvasVectorStorage.h"
#include "UltraCanvasTextUtils.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace VectorConverter {

using namespace VectorStorage;

namespace {

// ===== DIAGNOSTICS =====

    // "The file displays wrong" triage: operators the parser does not
    // implement (with use counts) name the missing features, and the
    // warnings mark every place it approximated or recovered.
    class Diagnostics {
    public:
        explicit Diagnostics(const ConversionOptions& options) : warn(options.WarningCallback) {}

        void Warn(const std::string& message) {
            if (warn && emitted < 64) { warn(message); ++emitted; }
        }
        // One warning per distinct cause, however many times it occurs.
        void WarnOnce(const std::string& key, const std::string& message) {
            if (seen.insert(key).second) Warn(message);
        }
        void Unknown(const std::string& op) { unknown[op]++; }

        void ReportUnknown() {
            if (unknown.empty()) return;
            std::string list;
            size_t shown = 0;
            for (const auto& entry : unknown) {
                if (shown++ == 8) { list += ", ..."; break; }
                if (!list.empty()) list += ", ";
                list += entry.first + " (" + std::to_string(entry.second) + ")";
            }
            Warn("AI: unsupported operators ignored: " + list);
        }

    private:
        std::function<void(const std::string&)> warn;
        std::map<std::string, size_t> unknown;
        std::set<std::string> seen;
        size_t emitted = 0;
    };

// ===== STREAM FILTERS =====

    std::string Inflate(const std::string& input, Diagnostics& diag) {
        std::string out;
        if (input.empty()) return out;
        z_stream zs{};
        // 47 = 15 window bits plus automatic zlib/gzip header detection, so a
        // raw zlib stream and a gzip-wrapped one both decode.
        if (inflateInit2(&zs, 47) != Z_OK) {
            diag.Warn("AI: zlib could not start; private data left compressed");
            return out;
        }
        zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
        zs.avail_in = static_cast<uInt>(input.size());
        char buffer[65536];
        int rc = Z_OK;
        do {
            zs.next_out = reinterpret_cast<Bytef*>(buffer);
            zs.avail_out = static_cast<uInt>(sizeof(buffer));
            rc = inflate(&zs, Z_NO_FLUSH);
            out.append(buffer, sizeof(buffer) - zs.avail_out);
        } while (rc == Z_OK && zs.avail_in > 0);
        inflateEnd(&zs);
        if (rc != Z_OK && rc != Z_STREAM_END && out.empty()) {
            diag.Warn("AI: private data failed to decompress");
        }
        return out;
    }

    std::string HexDecode(const std::string& input) {
        std::string out;
        out.reserve(input.size() / 2);
        int high = -1;
        for (char c : input) {
            if (c == '>') break;                       // EOD marker
            int value;
            if (c >= '0' && c <= '9')      value = c - '0';
            else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') value = c - 'A' + 10;
            else continue;                             // whitespace and noise
            if (high < 0) high = value;
            else { out.push_back(static_cast<char>((high << 4) | value)); high = -1; }
        }
        if (high >= 0) out.push_back(static_cast<char>(high << 4));   // odd digit pads with 0
        return out;
    }

    std::string Ascii85Decode(const std::string& input) {
        std::string out;
        out.reserve(input.size() * 4 / 5);
        uint32_t group = 0;
        int count = 0;
        size_t i = 0;
        if (input.size() >= 2 && input[0] == '<' && input[1] == '~') i = 2;   // optional <~ prefix
        for (; i < input.size(); ++i) {
            const char c = input[i];
            if (c == '~') break;                       // ~> EOD marker
            if (c == 'z' && count == 0) { out.append(4, '\0'); continue; }
            if (c < '!' || c > 'u') continue;          // whitespace and noise
            group = group * 85 + static_cast<uint32_t>(c - '!');
            if (++count == 5) {
                for (int shift = 24; shift >= 0; shift -= 8)
                    out.push_back(static_cast<char>((group >> shift) & 0xFF));
                group = 0;
                count = 0;
            }
        }
        if (count > 1) {                               // partial final group
            for (int pad = count; pad < 5; ++pad) group = group * 85 + 84;
            for (int byte = 0; byte < count - 1; ++byte)
                out.push_back(static_cast<char>((group >> (24 - byte * 8)) & 0xFF));
        }
        return out;
    }

// ===== PDF CONTAINER =====

    // Just enough PDF to reach the Illustrator private streams: this is not a
    // PDF reader, and deliberately so - the page content of these files is
    // empty, so nothing in the PDF object graph matters except the streams
    // the /AIPrivateData keys name.
    class PdfContainer {
    public:
        PdfContainer(const std::string& data, Diagnostics& diag) : data(data), diag(diag) {}

        // The private data blocks in index order, concatenated. A block that
        // begins a fresh art program (%!PS-Adobe) replaces what came before
        // it rather than being appended: Illustrator splits one program
        // across AIPrivateData1..N, but CorelDRAW's exporter puts the whole
        // program in the last block and uses the earlier one for the
        // thumbnail, and concatenating those would parse the art twice.
        std::string ReadPrivateData() {
            std::string combined;
            for (const auto& entry : PrivateDataRefs()) {
                std::string block = ReadStream(entry.second);
                if (block.empty()) continue;
                const size_t firstNonSpace = block.find_first_not_of(" \t\r\n");
                const bool startsProgram =
                        firstNonSpace != std::string::npos &&
                        block.compare(firstNonSpace, 10, "%!PS-Adobe") == 0;
                if (startsProgram) combined = std::move(block);
                else combined += block;
            }
            return combined;
        }

    private:
        // /AIPrivateData1 10 0 R, /AIPrivateData2 8 0 R, ... - or a single
        // /AIPrivateData with no index. Keyed by index so they are read in
        // the order Illustrator wrote them, not the order they appear.
        std::map<int, int> PrivateDataRefs() {
            std::map<int, int> refs;
            size_t pos = 0;
            const std::string key = "/AIPrivateData";
            while ((pos = data.find(key, pos)) != std::string::npos) {
                size_t cursor = pos + key.size();
                int index = 0;
                while (cursor < data.size() && std::isdigit(static_cast<unsigned char>(data[cursor]))) {
                    index = index * 10 + (data[cursor] - '0');
                    ++cursor;
                }
                int object = 0;
                if (ReadReference(cursor, object)) refs[index] = object;
                pos = cursor;
            }
            return refs;
        }

        // Reads " 12 0 R" at cursor; leaves cursor past it on success.
        bool ReadReference(size_t& cursor, int& object) const {
            size_t i = cursor;
            while (i < data.size() && std::isspace(static_cast<unsigned char>(data[i]))) ++i;
            size_t digits = i;
            int number = 0;
            while (i < data.size() && std::isdigit(static_cast<unsigned char>(data[i]))) {
                number = number * 10 + (data[i] - '0');
                ++i;
            }
            if (i == digits) return false;
            while (i < data.size() && std::isspace(static_cast<unsigned char>(data[i]))) ++i;
            while (i < data.size() && std::isdigit(static_cast<unsigned char>(data[i]))) ++i;  // generation
            while (i < data.size() && std::isspace(static_cast<unsigned char>(data[i]))) ++i;
            if (i >= data.size() || data[i] != 'R') return false;
            cursor = i + 1;
            object = number;
            return true;
        }

        // The decoded bytes of object `number`'s stream, or "" if it has none.
        std::string ReadStream(int number) {
            const std::string header = std::to_string(number) + " 0 obj";
            size_t pos = 0;
            while ((pos = data.find(header, pos)) != std::string::npos) {
                // Only a match at the start of a line is an object header;
                // "110 0 obj" must not answer for object 10.
                const bool atLineStart =
                        pos == 0 || data[pos - 1] == '\n' || data[pos - 1] == '\r';
                if (!atLineStart) { pos += header.size(); continue; }

                const size_t streamKeyword = data.find("stream", pos + header.size());
                if (streamKeyword == std::string::npos) return {};
                const std::string dict = data.substr(pos, streamKeyword - pos);
                size_t begin = streamKeyword + 6;                 // past "stream"
                if (begin < data.size() && data[begin] == '\r') ++begin;
                if (begin < data.size() && data[begin] == '\n') ++begin;

                // "endstream" is the reliable delimiter here: /Length may be
                // an indirect reference, and these streams are text, so a
                // false hit inside the payload is not a practical concern.
                const size_t end = data.find("endstream", begin);
                if (end == std::string::npos) return {};
                return ApplyFilters(dict, data.substr(begin, end - begin));
            }
            diag.Warn("AI: private data object " + std::to_string(number) + " not found");
            return {};
        }

        std::string ApplyFilters(const std::string& dict, std::string payload) {
            // /Filter [/ASCIIHexDecode /FlateDecode] - applied in the order
            // written, which is the order they were encoded in.
            const size_t filterKey = dict.find("/Filter");
            if (filterKey == std::string::npos) return payload;
            const std::string filters = dict.substr(filterKey);
            struct Step { size_t at; const char* name; };
            std::vector<Step> steps;
            for (const char* name : {"/ASCIIHexDecode", "/ASCII85Decode", "/FlateDecode",
                                     "/LZWDecode", "/RunLengthDecode"}) {
                const size_t at = filters.find(name);
                if (at != std::string::npos) steps.push_back({at, name});
            }
            std::sort(steps.begin(), steps.end(),
                      [](const Step& a, const Step& b) { return a.at < b.at; });
            for (const Step& step : steps) {
                const std::string name = step.name;
                if (name == "/ASCIIHexDecode")      payload = HexDecode(payload);
                else if (name == "/ASCII85Decode")  payload = Ascii85Decode(payload);
                else if (name == "/FlateDecode")    payload = Inflate(payload, diag);
                else {
                    diag.Warn("AI: private data uses " + name + ", which this reader "
                              "does not decode");
                    return {};
                }
            }
            return payload;
        }

        const std::string& data;
        Diagnostics& diag;
    };

    // ParseFloatClassic has std::from_chars parity, so it refuses a leading
    // space - and every header comment writes its numbers separated by one.
    // Returns the cursor past the number, or nullptr when none is there.
    const char* ScanNumber(const char* cursor, const char* end, double& out) {
        while (cursor < end && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
        return ParseFloatClassic(cursor, end, out);
    }

// ===== ART LANGUAGE SCANNER =====

    enum class TokenType { Number, Operator, Name, String, ArrayStart, ArrayEnd, End };

    struct Token {
        TokenType type = TokenType::End;
        double number = 0;
        std::string text;
    };

    class Scanner {
    public:
        Scanner(const std::string& source, Diagnostics& diag) : src(source), diag(diag) {}

        Token Next() {
            for (;;) {
                SkipSpace();
                if (pos >= src.size()) return {TokenType::End, 0, {}};
                const char c = src[pos];
                if (c == '%') { ReadComment(); continue; }
                if (c == '[') { ++pos; return {TokenType::ArrayStart, 0, {}}; }
                if (c == ']') { ++pos; return {TokenType::ArrayEnd, 0, {}}; }
                if (c == '(') return ReadString();
                if (c == '<') { SkipHexString(); continue; }
                if (c == '{') { SkipProcedure(); continue; }
                if (c == '}') { ++pos; continue; }              // unbalanced: ignore
                if (c == '/') return ReadName();
                return ReadWord();
            }
        }

        // Header comments the parser needs: the artboard size and the title.
        const std::string& Title() const { return title; }
        // Artboard size in points, from %AI5_ArtSize / %%BoundingBox.
        double ArtWidth() const { return artWidth; }
        double ArtHeight() const { return artHeight; }
        bool HasArtSize() const { return hasArtSize; }

        // Whether %AI3_Cropmarks declared which way y runs, and which way.
        bool DeclaresYDirection() const { return hasCropmarks; }
        bool DeclaredYDown() const { return artIsYDown; }

    private:
        void SkipSpace() {
            while (pos < src.size()) {
                const unsigned char c = static_cast<unsigned char>(src[pos]);
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == 0) ++pos;
                else break;
            }
        }

        void ReadComment() {
            const size_t lineEnd = std::min(src.find_first_of("\r\n", pos), src.size());
            const std::string line = src.substr(pos, lineEnd - pos);
            pos = lineEnd;
            ReadHeaderComment(line);
            // %%BeginData introduces a block the scanner must not tokenize.
            // Hex and ASCII blocks comment out every line, so they need no
            // help; a binary block is raw bytes and is skipped by its
            // declared length.
            if (line.compare(0, 12, "%%BeginData:") == 0 && line.find("Binary") != std::string::npos) {
                double count = 0;
                const char* first = line.c_str() + 12;
                if (ScanNumber(first, line.c_str() + line.size(), count) && count > 0) {
                    SkipSpace();
                    pos = std::min(pos + static_cast<size_t>(count), src.size());
                }
            }
        }

        void ReadHeaderComment(const std::string& line) {
            auto after = [&line](const char* prefix) -> const char* {
                const size_t n = std::char_traits<char>::length(prefix);
                return line.compare(0, n, prefix) == 0 ? line.c_str() + n : nullptr;
            };
            if (const char* value = after("%%Title:")) {
                title = value;
                while (!title.empty() && (title.front() == ' ' || title.front() == '(')) title.erase(0, 1);
                while (!title.empty() && (title.back() == ' ' || title.back() == ')')) title.pop_back();
            } else if (const char* size = after("%AI5_ArtSize:")) {
                double w = 0, h = 0;
                const char* end = line.c_str() + line.size();
                const char* cursor = ScanNumber(size, end, w);
                if (cursor && ScanNumber(cursor, end, h) && w > 0 && h > 0) {
                    artWidth = w;
                    artHeight = h;
                    hasArtSize = true;
                }
            } else if (const char* marks = after("%AI3_Cropmarks:")) {
                // The artboard rectangle in the file's own coordinate space -
                // "0 0 612 -792" says the page runs downwards from y = 0, so
                // the space is Illustrator's y-down ruler space. This is the
                // file stating its convention, which beats inferring it from
                // where the drawing happens to sit.
                double values[4] = {0, 0, 0, 0};
                const char* cursor = marks;
                const char* end = line.c_str() + line.size();
                bool ok = true;
                for (double& value : values) {
                    cursor = ScanNumber(cursor, end, value);
                    if (!cursor) { ok = false; break; }
                }
                if (ok && (values[1] != values[3])) {
                    artIsYDown = std::min(values[1], values[3]) < 0.0;
                    hasCropmarks = true;
                }
            } else if (const char* box = after("%%BoundingBox:")) {
                double values[4] = {0, 0, 0, 0};
                const char* cursor = box;
                const char* end = line.c_str() + line.size();
                bool ok = true;
                for (double& value : values) {
                    cursor = ScanNumber(cursor, end, value);
                    if (!cursor) { ok = false; break; }
                }
                // The artboard is what %AI5_ArtSize declares; a legacy file
                // that declares none is sized by the top-right corner of its
                // bounding box, which is where its page ends.
                if (ok && !hasArtSize && values[2] > values[0] && values[3] > values[1]) {
                    artWidth = values[2];
                    artHeight = values[3];
                }
            }
        }

        Token ReadString() {
            ++pos;                                     // past '('
            std::string text;
            int depth = 1;
            while (pos < src.size()) {
                const char c = src[pos++];
                if (c == '\\' && pos < src.size()) { text.push_back(src[pos++]); continue; }
                if (c == '(') ++depth;
                if (c == ')' && --depth == 0) break;
                text.push_back(c);
            }
            return {TokenType::String, 0, text};
        }

        // A legacy .ai carries an Illustrator prolog that defines the art
        // operators as PostScript procedures, and a procedure body holds the
        // same tokens the art does. This parser does not execute procedures,
        // so a body is skipped whole - reading into one would build geometry
        // out of a definition.
        void SkipProcedure() {
            int depth = 0;
            while (pos < src.size()) {
                const char c = src[pos];
                if (c == '%') { ReadComment(); continue; }
                if (c == '(') { ReadString(); continue; }
                if (c == '<') { SkipHexString(); continue; }
                ++pos;
                if (c == '{') ++depth;
                else if (c == '}' && --depth == 0) return;
            }
        }

        void SkipHexString() {
            const size_t close = src.find('>', pos);
            pos = (close == std::string::npos) ? src.size() : close + 1;
        }

        Token ReadName() {
            ++pos;                                     // past '/'
            const size_t start = pos;
            while (pos < src.size() && !IsDelimiter(src[pos])) ++pos;
            return {TokenType::Name, 0, src.substr(start, pos - start)};
        }

        Token ReadWord() {
            const size_t start = pos;
            while (pos < src.size() && !IsDelimiter(src[pos])) ++pos;
            const std::string word = src.substr(start, pos - start);
            if (word.empty()) { ++pos; return Next(); }
            // A number, or an operator: the art language's operators never
            // start with a digit, a sign or a dot, so the first character
            // decides and no failed conversion is needed.
            const char c = word[0];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
                double value = 0;
                if (ParseFloatClassic(word.c_str(), word.c_str() + word.size(), value))
                    return {TokenType::Number, value, word};
                diag.WarnOnce("number", "AI: malformed number '" + word + "' read as 0");
                return {TokenType::Number, 0, word};
            }
            return {TokenType::Operator, 0, word};
        }

        static bool IsDelimiter(char c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == 0 ||
                   c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' ||
                   c == '{' || c == '}' || c == '/' || c == '%';
        }

        const std::string& src;
        Diagnostics& diag;
        size_t pos = 0;
        std::string title;
        double artWidth = 612, artHeight = 792;
        bool hasArtSize = false;
        bool hasCropmarks = false;
        bool artIsYDown = true;
    };

// ===== GRAPHICS STATE =====

    struct GraphicsState {
        Color fill{0, 0, 0, 255};
        Color stroke{0, 0, 0, 255};
        double strokeWidth = 1.0;
        StrokeLineCap cap = StrokeLineCap::Butt;
        StrokeLineJoin join = StrokeLineJoin::Miter;
        double miterLimit = 4.0;
        std::vector<double> dashArray;
        double dashOffset = 0.0;
        double opacity = 1.0;
        VectorStorage::BlendMode blend = VectorStorage::BlendMode::Normal;
        VectorStorage::FillRule windingRule = VectorStorage::FillRule::NonZero;
    };

    Color FromGrey(double grey) {
        // PostScript grey: 0 is black, 1 is white.
        const auto level = static_cast<uint8_t>(std::lround(std::clamp(grey, 0.0, 1.0) * 255.0));
        return Color(level, level, level, 255);
    }

    Color FromRGB(double r, double g, double b) {
        auto channel = [](double v) {
            return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
        };
        return Color(channel(r), channel(g), channel(b), 255);
    }

    Color FromCMYK(double c, double m, double y, double k) {
        auto channel = [](double ink, double black) {
            return static_cast<uint8_t>(std::lround(
                    255.0 * (1.0 - std::clamp(ink, 0.0, 1.0)) * (1.0 - std::clamp(black, 0.0, 1.0))));
        };
        return Color(channel(c, k), channel(m, k), channel(y, k), 255);
    }

// ===== PARSER =====

    class ArtParser {
    public:
        ArtParser(const std::string& source, const ConversionOptions& options, Diagnostics& diag)
                : scanner(source, diag), options(options), diag(diag) {}

        std::shared_ptr<VectorDocument> Parse() {
            document = std::make_shared<VectorDocument>();

            for (Token token = scanner.Next(); token.type != TokenType::End; token = scanner.Next()) {
                switch (token.type) {
                    case TokenType::Number:     operands.push_back(token.number); break;
                    case TokenType::String:     lastString = token.text; break;
                    case TokenType::Name:       break;                  // /names carry no art
                    case TokenType::ArrayStart: ReadArray(); break;
                    case TokenType::ArrayEnd:   break;                  // unbalanced ']': ignore
                    case TokenType::Operator:   Execute(token.text); operands.clear(); break;
                    default: break;
                }
            }

            if (painted == 0) {
                diag.Warn("AI: the file's private data holds no artwork");
                return nullptr;
            }

            Finish();
            diag.ReportUnknown();
            return document;
        }

    private:
        // ----- operand access: the art language is postfix, so an operator's
        // arguments are the last n numbers pushed -----
        double Arg(size_t indexFromEnd) const {
            return indexFromEnd < operands.size()
                   ? operands[operands.size() - 1 - indexFromEnd] : 0.0;
        }
        bool HasArgs(size_t count) const { return operands.size() >= count; }

        void ReadArray() {
            lastArray.clear();
            for (Token token = scanner.Next();
                 token.type != TokenType::ArrayEnd && token.type != TokenType::End;
                 token = scanner.Next()) {
                if (token.type == TokenType::Number) lastArray.push_back(token.number);
            }
        }

        // ----- document structure -----

        // Lb starts a layer, but a file that draws before its first Lb (or
        // draws without layers at all, as legacy files do) still needs one,
        // so the layer is made on demand and an empty one is reused rather
        // than left behind.
        void BeginLayer() {
            if (layer && layer->Children.empty()) return;
            layer = document->AddLayer("Layer " + std::to_string(document->Layers.size() + 1));
            groupStack.clear();
            groupStack.push_back(layer);
        }

        std::shared_ptr<VectorGroup> CurrentGroup() {
            if (!layer) BeginLayer();
            return groupStack.empty() ? layer : groupStack.back();
        }

        void PushGroup() {
            auto parent = CurrentGroup();
            auto group = std::make_shared<VectorGroup>();
            if (parent) parent->AddChild(group);
            groupStack.push_back(group);
        }

        void PopGroup() {
            // The layer itself is the bottom of the stack and never pops.
            if (groupStack.size() > 1) groupStack.pop_back();
        }

        // ----- painting -----
        void Execute(const std::string& op) {
            // Path construction. Illustrator's case distinction marks the
            // anchor as a corner or a smooth point, which is editing
            // information: both cases draw the same segment.
            if (op == "m") { StartSubpath(Arg(1), Arg(0)); return; }
            if (op == "l" || op == "L") { LineTo(Arg(1), Arg(0)); return; }
            if (op == "c" || op == "C") {
                CurveTo(Arg(5), Arg(4), Arg(3), Arg(2), Arg(1), Arg(0));
                return;
            }
            if (op == "v" || op == "V") {
                // First control point coincides with the current point.
                CurveTo(currentX, currentY, Arg(3), Arg(2), Arg(1), Arg(0));
                return;
            }
            if (op == "y" || op == "Y") {
                // Second control point coincides with the end point.
                CurveTo(Arg(3), Arg(2), Arg(1), Arg(0), Arg(1), Arg(0));
                return;
            }

            // Painting. The lowercase form closes the subpath first.
            if (op == "n" || op == "N") { PaintPath(op == "N", false, false); return; }
            if (op == "f" || op == "F") { PaintPath(op == "f", true, false); return; }
            if (op == "s" || op == "S") { PaintPath(op == "s", false, true); return; }
            if (op == "b" || op == "B") { PaintPath(op == "b", true, true); return; }

            // The path being built becomes a clipping path for everything
            // that follows it in the enclosing group.
            if (op == "W") { clipNext = true; return; }

            // Compound path: the subpaths between *u and *U paint as one
            // object, which is what gives a filled shape its holes.
            if (op == "*u") { compoundDepth++; return; }
            if (op == "*U") { EndCompound(); return; }

            // Groups and layers.
            if (op == "u") { PushGroup(); return; }
            if (op == "U") { PopGroup(); return; }
            if (op == "Lb") { BeginLayer(); return; }
            if (op == "Ln") { if (layer && !lastString.empty()) layer->Name = lastString; return; }
            if (op == "LB") { return; }

            // Graphics state.
            if (op == "w") { state.strokeWidth = Arg(0); return; }
            if (op == "J") { state.cap = MapCap(Arg(0)); return; }
            if (op == "j") { state.join = MapJoin(Arg(0)); return; }
            if (op == "M") { state.miterLimit = Arg(0); return; }
            if (op == "d") { state.dashArray = lastArray; state.dashOffset = Arg(0); return; }
            if (op == "XR") {
                state.windingRule = (Arg(0) >= 0.5) ? VectorStorage::FillRule::EvenOdd
                                                    : VectorStorage::FillRule::NonZero;
                return;
            }

            // Colour. Fill is the lowercase operator, stroke the uppercase.
            if (op == "g") { state.fill = FromGrey(Arg(0)); return; }
            if (op == "G") { state.stroke = FromGrey(Arg(0)); return; }
            if (op == "k") { state.fill = FromCMYK(Arg(3), Arg(2), Arg(1), Arg(0)); return; }
            if (op == "K") { state.stroke = FromCMYK(Arg(3), Arg(2), Arg(1), Arg(0)); return; }
            if (op == "Xa") { state.fill = FromRGB(Arg(2), Arg(1), Arg(0)); return; }
            if (op == "XA") { state.stroke = FromRGB(Arg(2), Arg(1), Arg(0)); return; }
            if (op == "x" || op == "X" || op == "Xx" || op == "XX") { CustomColor(op); return; }
            if (op == "p" || op == "P") {
                diag.WarnOnce("pattern", "AI: pattern fills are drawn as flat colour");
                return;
            }

            // AI9 transparency: blend mode, opacity, isolate, knockout,
            // alpha-is-shape. Only the first two survive into the document.
            if (op == "Xy") {
                if (HasArgs(5)) {
                    state.blend = MapBlend(Arg(4));
                    state.opacity = std::clamp(Arg(3), 0.0, 1.0);
                }
                return;
            }

            // Recorded but with no effect on what is drawn: overprint (O, R),
            // lock (A), path direction (D), flatness (i), the ordering hints
            // (Ap, Ar) and the trailer's PostScript.
            if (op == "O" || op == "R" || op == "A" || op == "D" || op == "i" ||
                op == "Ap" || op == "Ar" || op == "Z" ||
                op == "gsave" || op == "grestore" || op == "showpage" ||
                op == "annotatepage" || op == "setcustomcolor") {
                return;
            }

            // Features with no equivalent in what has been built so far;
            // counted so the page can say what it could not show.
            if (!op.empty() && (op[0] == 'B' || op[0] == 'T')) {
                diag.WarnOnce(op[0] == 'B' ? "gradient" : "text",
                              op[0] == 'B' ? "AI: gradient and blend objects are not imported"
                                           : "AI: text objects are not imported");
                return;
            }

            diag.Unknown(op);
        }

        void CustomColor(const std::string& op) {
            // "c m y k (name) tint x" and its stroke and variant forms: the
            // tint scales the ink, and the name is a swatch this document
            // model does not carry.
            const bool isFill = (op == "x" || op == "Xx");
            double tint = HasArgs(1) ? Arg(0) : 1.0;
            tint = std::clamp(tint, 0.0, 1.0);
            const Color ink = HasArgs(5) ? FromCMYK(Arg(4), Arg(3), Arg(2), Arg(1))
                                         : Color(0, 0, 0, 255);
            auto mix = [tint](uint8_t channel) {
                return static_cast<uint8_t>(std::lround(255.0 - (255.0 - channel) * tint));
            };
            const Color tinted(mix(ink.r), mix(ink.g), mix(ink.b), 255);
            if (isFill) state.fill = tinted; else state.stroke = tinted;
            diag.WarnOnce("spot", "AI: spot colours are converted to their CMYK equivalent");
        }

        static StrokeLineCap MapCap(double value) {
            if (value >= 1.5) return StrokeLineCap::Square;
            if (value >= 0.5) return StrokeLineCap::Round;
            return StrokeLineCap::Butt;
        }
        static StrokeLineJoin MapJoin(double value) {
            if (value >= 1.5) return StrokeLineJoin::Bevel;
            if (value >= 0.5) return StrokeLineJoin::Round;
            return StrokeLineJoin::Miter;
        }
        static VectorStorage::BlendMode MapBlend(double value) {
            switch (static_cast<int>(std::lround(value))) {
                case 1:  return VectorStorage::BlendMode::Multiply;
                case 2:  return VectorStorage::BlendMode::Screen;
                case 3:  return VectorStorage::BlendMode::Overlay;
                case 4:  return VectorStorage::BlendMode::SoftLight;
                case 5:  return VectorStorage::BlendMode::HardLight;
                case 6:  return VectorStorage::BlendMode::ColorDodge;
                case 7:  return VectorStorage::BlendMode::ColorBurn;
                case 8:  return VectorStorage::BlendMode::Darken;
                case 9:  return VectorStorage::BlendMode::Lighten;
                case 10: return VectorStorage::BlendMode::Difference;
                case 11: return VectorStorage::BlendMode::Exclusion;
                default: return VectorStorage::BlendMode::Normal;
            }
        }

        // ----- path geometry, kept in art coordinates until Finish() -----
        void StartSubpath(double x, double y) {
            segments.push_back({Segment::Move, {x, y, 0, 0, 0, 0}});
            currentX = x;
            currentY = y;
            TrackExtent(x, y);
        }
        void LineTo(double x, double y) {
            if (segments.empty()) return StartSubpath(x, y);
            segments.push_back({Segment::Line, {x, y, 0, 0, 0, 0}});
            currentX = x;
            currentY = y;
            TrackExtent(x, y);
        }
        void CurveTo(double x1, double y1, double x2, double y2, double x, double y) {
            if (segments.empty()) StartSubpath(x1, y1);
            segments.push_back({Segment::Curve, {x1, y1, x2, y2, x, y}});
            currentX = x;
            currentY = y;
            TrackExtent(x1, y1);
            TrackExtent(x2, y2);
            TrackExtent(x, y);
        }
        void TrackExtent(double x, double y) {
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
        }

        void PaintPath(bool close, bool fill, bool stroke) {
            if (close) segments.push_back({Segment::Close, {0, 0, 0, 0, 0, 0}});
            // Inside a compound path every subpath accumulates into one
            // object, and the paint operator of the last one decides how the
            // whole object is painted.
            paintFill = fill;
            paintStroke = stroke;
            paintState = state;
            paintClip = clipNext;
            clipNext = false;
            if (compoundDepth > 0) return;
            EmitObject();
        }

        void EndCompound() {
            if (compoundDepth > 0) --compoundDepth;
            if (compoundDepth == 0 && !segments.empty()) EmitObject();
        }

        void EmitObject() {
            if (segments.empty()) { ResetPath(); return; }
            if (!paintFill && !paintStroke && !paintClip) { ResetPath(); return; }

            if (paintClip) {
                // The clip applies to the rest of the enclosing group, so the
                // objects after it go into a group of their own that carries
                // it, and the clipping path becomes a definition. W does not
                // stop the path being painted as well, so a "W f" gets a
                // drawn copy of its own inside that group - where clipping it
                // to itself changes nothing.
                BeginClip(NewPath());
                if (!paintFill && !paintStroke) { ResetPath(); return; }
            }
            if (auto group = CurrentGroup()) {
                group->AddChild(NewPath());
                ++painted;
            }
            ResetPath();
        }

        // A path carrying the pending geometry and the paint state that the
        // painting operator captured. The geometry is filled in by Finish(),
        // once the file's coordinate space is known.
        std::shared_ptr<VectorPath> NewPath() {
            auto path = std::make_shared<VectorPath>();
            pending.push_back({path, segments});
            path->Style.Opacity = static_cast<float>(paintState.opacity);
            path->Style.Blend = paintState.blend;
            path->Style.ClipRule = paintState.windingRule;
            if (paintFill) {
                path->Style.Fill = FillData(paintState.fill);
                if (paintState.windingRule == VectorStorage::FillRule::EvenOdd) {
                    diag.WarnOnce("evenodd", "AI: even-odd fills are drawn with the "
                                             "non-zero rule");
                }
            } else {
                path->Style.Fill = FillData(std::monostate{});
            }
            if (paintStroke) {
                StrokeData stroke;
                stroke.Fill = FillData(paintState.stroke);
                stroke.Width = static_cast<float>(paintState.strokeWidth);
                stroke.LineCap = paintState.cap;
                stroke.LineJoin = paintState.join;
                stroke.MiterLimit = static_cast<float>(paintState.miterLimit);
                stroke.DashArray = paintState.dashArray;
                stroke.DashOffset = paintState.dashOffset;
                path->Style.Stroke = stroke;
            }
            return path;
        }

        void BeginClip(const std::shared_ptr<VectorPath>& path) {
            const std::string id = "aiClip" + std::to_string(++clipCount);
            auto clip = std::make_shared<VectorClipPath>();
            clip->Id = id;
            clip->Data.ClipRule = paintState.windingRule;
            clip->Data.Elements.push_back(path);
            document->AddDefinition(id, clip);
            PushGroup();
            if (auto group = CurrentGroup()) group->Style.ClipPath = id;
        }

        void ResetPath() {
            segments.clear();
            paintFill = paintStroke = paintClip = false;
        }

        // ----- coordinate mapping, applied once every path is known -----

        // Illustrator's ruler space puts the origin at the top-left of the
        // artboard with y running down as negative numbers; a legacy
        // EPS-based .ai instead uses PostScript's bottom-left origin with y
        // running up. %AI3_Cropmarks states which of the two the file uses;
        // without it, the art's own extent tells them apart, since y-down
        // art never has a positive y of any size.
        bool ArtIsYDown() const {
            if (scanner.DeclaresYDirection()) return scanner.DeclaredYDown();
            if (minY > maxY) return true;                  // no geometry
            if (maxY <= 0.5) return true;                  // entirely at or below the origin
            if (minY >= -0.5) return false;                // entirely at or above it
            // Mixed signs: whichever side carries more of the drawing wins.
            return -minY > maxY;
        }

        void Finish() {
            const double pageWidth = scanner.ArtWidth();
            const double pageHeight = scanner.ArtHeight();
            const bool yDown = ArtIsYDown();
            const auto mapY = [yDown, pageHeight](double y) {
                return yDown ? -y : pageHeight - y;
            };

            for (auto& item : pending) {
                for (const Segment& segment : item.segments) {
                    switch (segment.kind) {
                        case Segment::Move:
                            item.path->MoveTo(static_cast<float>(segment.v[0]),
                                              static_cast<float>(mapY(segment.v[1])));
                            break;
                        case Segment::Line:
                            item.path->LineTo(static_cast<float>(segment.v[0]),
                                              static_cast<float>(mapY(segment.v[1])));
                            break;
                        case Segment::Curve:
                            item.path->CurveTo(static_cast<float>(segment.v[0]),
                                               static_cast<float>(mapY(segment.v[1])),
                                               static_cast<float>(segment.v[2]),
                                               static_cast<float>(mapY(segment.v[3])),
                                               static_cast<float>(segment.v[4]),
                                               static_cast<float>(mapY(segment.v[5])));
                            break;
                        case Segment::Close:
                            item.path->ClosePath();
                            break;
                    }
                }
            }
            pending.clear();

            document->Size = Size2Dd(pageWidth, pageHeight);
            document->ViewBox = Rect2Dd(0, 0, pageWidth, pageHeight);
            document->Title = scanner.Title();
            document->SourceUnit = LengthUnit::Point;
            document->PointsPerSourceUnit = 1.0;
            if (options.ProgressCallback) options.ProgressCallback(1.0f);
        }

        struct Segment {
            enum Kind { Move, Line, Curve, Close } kind;
            double v[6];
        };
        struct PendingPath {
            std::shared_ptr<VectorPath> path;
            std::vector<Segment> segments;
        };

        Scanner scanner;
        const ConversionOptions& options;
        Diagnostics& diag;

        std::shared_ptr<VectorDocument> document;
        std::shared_ptr<VectorLayer> layer;
        std::vector<std::shared_ptr<VectorGroup>> groupStack;
        std::vector<PendingPath> pending;

        std::vector<double> operands;
        std::vector<double> lastArray;
        std::string lastString;

        GraphicsState state;
        GraphicsState paintState;
        std::vector<Segment> segments;
        double currentX = 0, currentY = 0;
        bool paintFill = false, paintStroke = false, paintClip = false, clipNext = false;
        int compoundDepth = 0;
        size_t painted = 0;
        int clipCount = 0;
        double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;
    };

}   // anonymous namespace

// ===== AIConverter import =====

std::shared_ptr<VectorStorage::VectorDocument> AIConverter::ImportFromString(
        const std::string& data, const ConversionOptions& options) {
    Diagnostics diag(options);
    if (data.empty()) {
        diag.Warn("AI: the file is empty");
        return nullptr;
    }

    std::string art;
    if (data.compare(0, 5, "%PDF-") == 0) {
        // PDF-based (Illustrator 9 and later): the artwork is in the private
        // streams, and is the whole drawing whenever "Create PDF Compatible
        // File" was off when the file was written.
        PdfContainer container(data, diag);
        art = container.ReadPrivateData();
        if (art.empty()) {
            // A .ai whose artwork really is in the PDF page: nothing for this
            // reader to do, and the PDF engine renders it correctly.
            diag.Warn("AI: no Illustrator private data in this file - it is a plain "
                      "PDF-compatible .ai, which the PDF engine renders");
            return nullptr;
        }
    } else if (data.compare(0, 4, "%!PS") == 0) {
        art = data;                                    // legacy (v8 and earlier)
    } else {
        diag.Warn("AI: not an Adobe Illustrator file");
        return nullptr;
    }

    ArtParser parser(art, options, diag);
    return parser.Parse();
}

} // namespace VectorConverter
} // namespace UltraCanvas
