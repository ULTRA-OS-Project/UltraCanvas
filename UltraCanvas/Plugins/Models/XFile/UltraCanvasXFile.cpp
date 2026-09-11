// Plugins/Models/XFile/UltraCanvasXFile.cpp
// The .x container: header, the two tokenisers, and one grammar over both.
//
// Text and binary are the same language written two ways, so they share a
// single recursive-descent parser and differ only in how tokens are produced.
// That is worth the small indirection: the grammar has exactly one
// implementation, so a text file and the binary export of the same scene
// cannot drift apart in what they read as.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework

#include "Models/XFile/UltraCanvasXFile.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace UltraCanvas {
namespace XFile {

const Node* Node::FindChild(const std::string& type) const {
    for (const Node& child : Children)
        if (child.Type == type) return &child;
    return nullptr;
}

std::vector<const Node*> Node::ChildrenOfType(const std::string& type) const {
    std::vector<const Node*> found;
    for (const Node& child : Children)
        if (child.Type == type) found.push_back(&child);
    return found;
}

const Node* File::Resolve(const Node& node) const {
    if (!node.IsReference()) return &node;
    auto found = Named.find(node.Reference);
    return found == Named.end() ? nullptr : found->second;
}

namespace {

// ===== TOKENS =====

enum class Kind {
    End,
    Identifier,
    Number,
    String,
    OpenBrace,
    CloseBrace,
    Separator,    // ; and ,
    Punct,        // [ ] ( ) . - anything the grammar steps over
    Guid,         // <....>
    Template,     // the `template` keyword
    Error
};

struct Token {
    Kind What = Kind::End;
    std::string Text;
    double Value = 0.0;
};

// Binary token ids, from the DirectX file format specification.
enum : uint16_t {
    kName = 1, kString = 2, kInteger = 3, kGuid = 5, kIntegerList = 6,
    kFloatList = 7, kOBrace = 10, kCBrace = 11, kOParen = 12, kCParen = 13,
    kOBracket = 14, kCBracket = 15, kOAngle = 16, kCAngle = 17, kDot = 18,
    kComma = 19, kSemicolon = 20, kTemplate = 31
};

// One lexer for both encodings. The binary side expands an INTEGER_LIST or
// FLOAT_LIST into individual Number tokens, so the grammar above it never
// learns that binary has bulk arrays and text does not.
class Lexer {
public:
    Lexer(const uint8_t* data, size_t size, bool binary, int floatBits)
        : data_(data), size_(size), binary_(binary), floatBits_(floatBits) {
        Advance();
    }

    const Token& Peek() const { return current_; }
    Token Take() { Token token = current_; Advance(); return token; }
    const std::string& Error() const { return error_; }

private:
    void Advance() { current_ = binary_ ? NextBinary() : NextText(); }

    // ----- text -----

    void SkipTextSpace() {
        while (position_ < size_) {
            const char c = static_cast<char>(data_[position_]);
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++position_; continue; }
            // Both comment spellings the format allows.
            if (c == '#' || (c == '/' && position_ + 1 < size_ && data_[position_ + 1] == '/')) {
                while (position_ < size_ && data_[position_] != '\n') ++position_;
                continue;
            }
            break;
        }
    }

    Token NextText() {
        SkipTextSpace();
        if (position_ >= size_) return {};
        const char c = static_cast<char>(data_[position_]);

        if (c == '{') { ++position_; return {Kind::OpenBrace, "{", 0.0}; }
        if (c == '}') { ++position_; return {Kind::CloseBrace, "}", 0.0}; }
        if (c == ';' || c == ',') { ++position_; return {Kind::Separator, std::string(1, c), 0.0}; }
        if (c == '[' || c == ']' || c == '(' || c == ')' || c == '.')
            { ++position_; return {Kind::Punct, std::string(1, c), 0.0}; }

        if (c == '<') {
            const size_t start = ++position_;
            while (position_ < size_ && data_[position_] != '>') ++position_;
            std::string text(reinterpret_cast<const char*>(data_ + start), position_ - start);
            if (position_ < size_) ++position_;
            return {Kind::Guid, std::move(text), 0.0};
        }

        if (c == '"') {
            const size_t start = ++position_;
            while (position_ < size_ && data_[position_] != '"') ++position_;
            std::string text(reinterpret_cast<const char*>(data_ + start), position_ - start);
            if (position_ < size_) ++position_;
            return {Kind::String, std::move(text), 0.0};
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            const size_t start = position_;
            while (position_ < size_) {
                const char n = static_cast<char>(data_[position_]);
                if (!std::isalnum(static_cast<unsigned char>(n)) && n != '_') break;
                ++position_;
            }
            std::string text(reinterpret_cast<const char*>(data_ + start), position_ - start);
            if (text == "template") return {Kind::Template, std::move(text), 0.0};
            return {Kind::Identifier, std::move(text), 0.0};
        }

        if (c == '-' || c == '+' || c == '.' || std::isdigit(static_cast<unsigned char>(c))) {
            const char* begin = reinterpret_cast<const char*>(data_ + position_);
            char* end = nullptr;
            const double value = std::strtod(begin, &end);
            if (end == begin) { ++position_; return {Kind::Punct, std::string(1, c), 0.0}; }
            position_ += static_cast<size_t>(end - begin);
            return {Kind::Number, {}, value};
        }

        ++position_;
        return {Kind::Punct, std::string(1, c), 0.0};
    }

    // ----- binary -----

    bool Read(void* out, size_t bytes) {
        if (position_ + bytes > size_) { position_ = size_; return false; }
        std::memcpy(out, data_ + position_, bytes);
        position_ += bytes;
        return true;
    }
    uint16_t ReadU16() { uint16_t v = 0; Read(&v, 2); return v; }
    uint32_t ReadU32() { uint32_t v = 0; Read(&v, 4); return v; }

    Token Fail(const char* why) {
        if (error_.empty()) error_ = why;
        return {Kind::Error, why, 0.0};
    }

    Token NextBinary() {
        // A list token expands one element per call.
        if (pendingNumbers_ > 0) {
            --pendingNumbers_;
            if (pendingAreFloats_) {
                if (floatBits_ == 64) { double v = 0.0; if (!Read(&v, 8)) return Fail("truncated float list"); return {Kind::Number, {}, v}; }
                float v = 0.0f;
                if (!Read(&v, 4)) return Fail("truncated float list");
                return {Kind::Number, {}, static_cast<double>(v)};
            }
            int32_t v = 0;
            if (!Read(&v, 4)) return Fail("truncated integer list");
            return {Kind::Number, {}, static_cast<double>(v)};
        }

        if (position_ + 2 > size_) return {};
        const uint16_t token = ReadU16();
        switch (token) {
            case kName: {
                const uint32_t count = ReadU32();
                if (position_ + count > size_) return Fail("truncated name");
                std::string text(reinterpret_cast<const char*>(data_ + position_), count);
                position_ += count;
                return {Kind::Identifier, std::move(text), 0.0};
            }
            case kString: {
                const uint32_t count = ReadU32();
                if (position_ + count > size_) return Fail("truncated string");
                std::string text(reinterpret_cast<const char*>(data_ + position_), count);
                position_ += count;
                ReadU16();   // the record's own terminator, ; or ,
                return {Kind::String, std::move(text), 0.0};
            }
            case kInteger: {
                int32_t v = 0;
                if (!Read(&v, 4)) return Fail("truncated integer");
                return {Kind::Number, {}, static_cast<double>(v)};
            }
            case kGuid:
                if (position_ + 16 > size_) return Fail("truncated guid");
                position_ += 16;
                return {Kind::Guid, {}, 0.0};
            case kIntegerList:
            case kFloatList: {
                const uint32_t count = ReadU32();
                // A count larger than the bytes left is a corrupt file, not a
                // big array; refusing it here is what keeps the expansion below
                // from reading the whole heap.
                const size_t each = token == kFloatList
                                            ? (floatBits_ == 64 ? 8u : 4u) : 4u;
                if (count > (size_ - position_) / each) return Fail("list longer than the file");
                pendingAreFloats_ = token == kFloatList;
                pendingNumbers_ = static_cast<size_t>(count);
                return NextBinary();
            }
            case kOBrace:    return {Kind::OpenBrace, "{", 0.0};
            case kCBrace:    return {Kind::CloseBrace, "}", 0.0};
            case kSemicolon: return {Kind::Separator, ";", 0.0};
            case kComma:     return {Kind::Separator, ",", 0.0};
            case kTemplate:  return {Kind::Template, "template", 0.0};
            case kOParen: case kCParen: case kOBracket: case kCBracket:
            case kOAngle: case kCAngle: case kDot:
                return {Kind::Punct, {}, 0.0};
            default:
                // 40..52 are the type keywords inside a template body, which
                // the grammar steps over anyway.
                if (token >= 40 && token <= 52) return {Kind::Punct, {}, 0.0};
                return Fail("unknown binary token");
        }
    }

    const uint8_t* data_;
    size_t size_;
    size_t position_ = 0;
    bool binary_;
    int floatBits_;
    Token current_;
    size_t pendingNumbers_ = 0;
    bool pendingAreFloats_ = false;
    std::string error_;
};

// ===== GRAMMAR =====

class Parser {
public:
    explicit Parser(Lexer& lexer) : lexer_(lexer) {}

    bool ParseFile(std::vector<Node>& roots, std::string& error) {
        while (true) {
            const Token& token = lexer_.Peek();
            if (token.What == Kind::End) return true;
            if (token.What == Kind::Error) { error = lexer_.Error(); return false; }

            if (token.What == Kind::Template) {
                lexer_.Take();
                SkipBracedBlock();
                continue;
            }
            if (token.What == Kind::Identifier) {
                Node node;
                if (!ParseObject(node, error)) return false;
                roots.push_back(std::move(node));
                continue;
            }
            // Stray separators and punctuation between objects.
            lexer_.Take();
        }
    }

private:
    // Everything from the next "{" to its matching "}". Used for `template`,
    // which declares a schema this reader does not need: the layouts it would
    // describe are the ones already compiled in, and no exporter writes a
    // template that disagrees with them.
    void SkipBracedBlock() {
        while (lexer_.Peek().What != Kind::OpenBrace) {
            if (lexer_.Peek().What == Kind::End || lexer_.Peek().What == Kind::Error) return;
            lexer_.Take();
        }
        int depth = 0;
        do {
            const Token token = lexer_.Take();
            if (token.What == Kind::OpenBrace) ++depth;
            else if (token.What == Kind::CloseBrace) --depth;
            else if (token.What == Kind::End || token.What == Kind::Error) return;
        } while (depth > 0);
    }

    bool ParseObject(Node& node, std::string& error) {
        node.Type = lexer_.Take().Text;
        if (lexer_.Peek().What == Kind::Identifier) node.Name = lexer_.Take().Text;
        if (lexer_.Peek().What != Kind::OpenBrace) {
            error = "expected '{' after object type '" + node.Type + "'";
            return false;
        }
        lexer_.Take();
        return ParseBody(node, error);
    }

    bool ParseBody(Node& node, std::string& error) {
        if (++depth_ > kMaxDepth) {
            error = "objects nested more than " + std::to_string(kMaxDepth) + " deep";
            return false;
        }
        while (true) {
            const Token& token = lexer_.Peek();
            switch (token.What) {
                case Kind::CloseBrace:
                    lexer_.Take();
                    --depth_;
                    return true;
                case Kind::End:
                    error = "the file ends inside '" + node.Type + "'";
                    return false;
                case Kind::Error:
                    error = lexer_.Error();
                    return false;
                case Kind::Number:
                    node.Numbers.push_back(lexer_.Take().Value);
                    break;
                case Kind::String:
                    node.Strings.push_back(lexer_.Take().Text);
                    break;
                case Kind::Separator:
                case Kind::Punct:
                case Kind::Guid:
                    lexer_.Take();
                    break;
                case Kind::Template:
                    lexer_.Take();
                    SkipBracedBlock();
                    break;
                case Kind::OpenBrace: {
                    // "{ Name }" - a reference to an object defined elsewhere.
                    lexer_.Take();
                    Node reference;
                    if (lexer_.Peek().What == Kind::Identifier)
                        reference.Reference = lexer_.Take().Text;
                    while (lexer_.Peek().What != Kind::CloseBrace) {
                        if (lexer_.Peek().What == Kind::End || lexer_.Peek().What == Kind::Error) {
                            error = "the file ends inside a reference";
                            return false;
                        }
                        lexer_.Take();
                    }
                    lexer_.Take();
                    if (!reference.Reference.empty()) node.Children.push_back(std::move(reference));
                    break;
                }
                case Kind::Identifier: {
                    Node child;
                    if (!ParseObject(child, error)) return false;
                    node.Children.push_back(std::move(child));
                    break;
                }
            }
        }
    }

    static constexpr int kMaxDepth = 64;

    Lexer& lexer_;
    int depth_ = 0;
};

void IndexNames(const Node& node, std::map<std::string, const Node*>& named) {
    if (!node.Name.empty() && named.find(node.Name) == named.end()) named[node.Name] = &node;
    for (const Node& child : node.Children) IndexNames(child, named);
}

} // namespace

// ===== HEADER =====

bool LooksLikeXFile(const std::string& head) {
    if (head.size() < 16) return false;
    if (head.compare(0, 4, "xof ") != 0) return false;
    const std::string encoding = head.substr(8, 4);
    return encoding == "txt " || encoding == "bin " || encoding == "tzip" ||
           encoding == "bzip" || encoding == "comp";
}

bool IsCompressedEncoding(Encoding encoding) {
    return encoding == Encoding::CompressedText || encoding == Encoding::CompressedBinary;
}

const char* EncodingName(Encoding encoding) {
    switch (encoding) {
        case Encoding::Text: return "text";
        case Encoding::Binary: return "binary";
        case Encoding::CompressedText: return "MSZIP-compressed text";
        case Encoding::CompressedBinary: return "MSZIP-compressed binary";
    }
    return "unknown";
}

bool Parse(const std::vector<uint8_t>& data, File& out, std::string& error,
           const std::function<void(const std::string&)>& warn) {
    const std::string head(data.begin(),
                           data.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(data.size(), 16)));
    if (!LooksLikeXFile(head)) {
        error = "not a DirectX .x file: the first sixteen bytes are not an 'xof' header";
        return false;
    }

    out.MajorVersion = (head[4] - '0') * 10 + (head[5] - '0');
    out.MinorVersion = (head[6] - '0') * 10 + (head[7] - '0');

    const std::string encoding = head.substr(8, 4);
    if (encoding == "txt ") out.How = Encoding::Text;
    else if (encoding == "bin ") out.How = Encoding::Binary;
    else if (encoding == "tzip" || encoding == "comp") out.How = Encoding::CompressedText;
    else out.How = Encoding::CompressedBinary;

    const std::string floatSize = head.substr(12, 4);
    out.FloatBits = floatSize == "0064" ? 64 : 32;
    if (floatSize != "0032" && floatSize != "0064") {
        if (warn) warn("X: the header declares a float size of '" + floatSize +
                       "'; 32-bit floats are assumed");
        out.FloatBits = 32;
    }

    if (IsCompressedEncoding(out.How)) {
        error = std::string("X: this file is ") + EncodingName(out.How) +
                ", which this reader does not decompress. Re-export it as text "
                "(xof 0303txt) or binary (xof 0303bin).";
        return false;
    }

    Lexer lexer(data.data() + 16, data.size() - 16, out.How == Encoding::Binary, out.FloatBits);
    Parser parser(lexer);
    if (!parser.ParseFile(out.Roots, error)) return false;

    for (const Node& root : out.Roots) IndexNames(root, out.Named);
    return true;
}

} // namespace XFile
} // namespace UltraCanvas
