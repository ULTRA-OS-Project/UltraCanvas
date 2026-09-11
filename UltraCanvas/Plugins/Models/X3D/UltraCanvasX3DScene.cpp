// Plugins/Models/X3D/UltraCanvasX3DScene.cpp
// The Classic VRML encoding of X3D, and VRML97 before it.
//
// The grammar is small enough to state in full:
//
//     scene     : statement*
//     statement : nodeStmt | 'ROUTE' id '.' id 'TO' id '.' id | proto
//     nodeStmt  : 'DEF' id node | 'USE' id | node
//     node      : typeId '{' body '}'
//     body      : ( fieldId fieldValue | statement )*
//     value     : literal+ | nodeStmt | '[' ( literal* | nodeStmt* ) ']'
//
// The one thing that needs deciding rather than reading is whether a field's
// value is a *node* or a *literal*, because both start with a bare word:
//
//     appearance Appearance { ... }        a node
//     solid      TRUE                      a literal
//
// One token of lookahead settles it. A word followed by '{' opens a node, and
// DEF and USE always do; every other word is a literal (TRUE, FALSE, NULL, or
// an enumerant like a texture mode). The same test applied after '[' says
// whether the brackets hold a node list or a number list, so nothing here has
// to know which fields are MFNode - which matters, because a reader that knew
// would have to be taught every node type in the standard before it could
// parse one.
//
// Field values are rendered back into the text form the XML encoding stores,
// so the reader above this layer sees one representation. Numbers keep their
// original spelling rather than being parsed and reprinted: a round trip
// through double and back is both slower and lossy, and nothing here needs
// their value.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework

#include "Models/X3D/UltraCanvasX3DScene.h"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace X3D {

namespace {

// A VRML file may nest this deep before the parser calls it malformed. Real
// scenes are a handful deep; this is a guard against a crafted file recursing
// the stack away, not a limit anything legitimate meets.
constexpr int kMaxDepth = 128;

bool IsIdentStart(char c) {
    // The spec excludes digits, the punctuation below and anything under 0x21.
    // Everything else, including most of UTF-8's high bytes, may start a name.
    const unsigned char u = static_cast<unsigned char>(c);
    if (u <= 0x20) return false;
    return std::strchr("0123456789{}[]()\"'#,.\\+-", c) == nullptr;
}

bool IsIdentRest(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u <= 0x20) return false;
    return std::strchr("{}[]()\"'#,\\", c) == nullptr;
}

class Lexer {
public:
    enum class Kind { End, Ident, Number, String, OpenBrace, CloseBrace, OpenBracket,
                      CloseBracket, Unknown };
    struct Token {
        Kind What = Kind::End;
        std::string Text;   // the identifier, the number as written, or the string's body
    };

    Lexer(const char* data, size_t size) : data_(data), size_(size) {
        current_ = Scan();
        next_ = Scan();
    }

    const Token& Peek() const { return current_; }
    const Token& PeekSecond() const { return next_; }
    Token Take() {
        Token taken = current_;
        current_ = next_;
        next_ = Scan();
        return taken;
    }

private:
    void SkipSpace() {
        while (position_ < size_) {
            const char c = data_[position_];
            // A comma is whitespace in this encoding, exactly as it is inside
            // an XML attribute's value.
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',') {
                ++position_;
                continue;
            }
            if (c == '#') {
                while (position_ < size_ && data_[position_] != '\n') ++position_;
                continue;
            }
            break;
        }
    }

    Token Scan() {
        SkipSpace();
        if (position_ >= size_) return {};
        const char c = data_[position_];

        if (c == '{') { ++position_; return {Kind::OpenBrace, "{"}; }
        if (c == '}') { ++position_; return {Kind::CloseBrace, "}"}; }
        if (c == '[') { ++position_; return {Kind::OpenBracket, "["}; }
        if (c == ']') { ++position_; return {Kind::CloseBracket, "]"}; }

        if (c == '"') {
            ++position_;
            std::string text;
            while (position_ < size_ && data_[position_] != '"') {
                // Only a quote and a backslash are escapable. Treating every
                // backslash as an escape eats the separators out of the Windows
                // paths exporters put in an ImageTexture url, which is the one
                // place a real file relies on this being right.
                if (data_[position_] == '\\' && position_ + 1 < size_ &&
                    (data_[position_ + 1] == '"' || data_[position_ + 1] == '\\'))
                    ++position_;
                text.push_back(data_[position_++]);
            }
            if (position_ < size_) ++position_;
            return {Kind::String, std::move(text)};
        }

        // A number keeps the spelling it was written with, hex included: the
        // spec allows 0x7fffffff for an SFInt32 and the XML encoding does too.
        if (c == '-' || c == '+' || c == '.' || (c >= '0' && c <= '9')) {
            const size_t start = position_;
            ++position_;
            while (position_ < size_) {
                const char n = data_[position_];
                if ((n >= '0' && n <= '9') || n == '.' || n == 'e' || n == 'E' ||
                    n == 'x' || n == 'X' || (n >= 'a' && n <= 'f') || (n >= 'A' && n <= 'F') ||
                    ((n == '-' || n == '+') && (data_[position_ - 1] == 'e' ||
                                                data_[position_ - 1] == 'E')))
                    ++position_;
                else
                    break;
            }
            return {Kind::Number, std::string(data_ + start, position_ - start)};
        }

        if (IsIdentStart(c)) {
            const size_t start = position_;
            while (position_ < size_ && IsIdentRest(data_[position_])) ++position_;
            return {Kind::Ident, std::string(data_ + start, position_ - start)};
        }

        ++position_;
        return {Kind::Unknown, std::string(1, c)};
    }

    const char* data_;
    size_t size_;
    size_t position_ = 0;
    Token current_;
    Token next_;
};

class Parser {
public:
    Parser(Lexer& lexer, const std::function<void(const std::string&)>& warn)
        : lexer_(lexer), warn_(warn) {}

    const std::string& Error() const { return error_; }

    // Reads statements until the closing token, appending nodes to `out`.
    bool ReadStatements(std::vector<Node>& out, Lexer::Kind until) {
        while (true) {
            const Lexer::Token& token = lexer_.Peek();
            if (token.What == Lexer::Kind::End) {
                if (until == Lexer::Kind::End) return true;
                error_ = "the file ends inside a node";
                return false;
            }
            if (token.What == until) return true;

            if (token.What != Lexer::Kind::Ident) {
                // Nothing else can begin a statement; skipping keeps a stray
                // token from stopping an otherwise readable file.
                lexer_.Take();
                continue;
            }

            if (token.Text == "ROUTE") {
                Node route;
                if (!ReadRoute(route)) return false;
                out.push_back(std::move(route));
                continue;
            }
            if (token.Text == "PROTO" || token.Text == "EXTERNPROTO") {
                if (!SkipProto()) return false;
                continue;
            }
            if (token.Text == "META" || token.Text == "PROFILE" ||
                token.Text == "COMPONENT" || token.Text == "UNIT") {
                if (!ReadStatementKeyword(out)) return false;
                continue;
            }

            Node node;
            if (!ReadNodeStatement(node)) return false;
            if (!node.Name.empty()) out.push_back(std::move(node));
        }
    }

private:
    // DEF id node | USE id | node
    bool ReadNodeStatement(Node& out) {
        Lexer::Token first = lexer_.Take();

        if (first.Text == "USE") {
            if (lexer_.Peek().What != Lexer::Kind::Ident) {
                error_ = "USE is not followed by a name";
                return false;
            }
            // A USE has no type of its own. The reader resolves it against the
            // DEF it names, exactly as it does for the XML encoding's USE
            // attribute, so the placeholder only has to carry the name.
            out.Name = "USE";
            out.Set("USE", lexer_.Take().Text);
            return true;
        }

        std::string defName;
        if (first.Text == "DEF") {
            if (lexer_.Peek().What != Lexer::Kind::Ident) {
                error_ = "DEF is not followed by a name";
                return false;
            }
            defName = lexer_.Take().Text;
            if (lexer_.Peek().What != Lexer::Kind::Ident) {
                error_ = "DEF " + defName + " is not followed by a node";
                return false;
            }
            first = lexer_.Take();
        }

        out.Name = first.Text;
        if (!defName.empty()) out.Set("DEF", defName);

        if (lexer_.Peek().What != Lexer::Kind::OpenBrace) {
            error_ = "node '" + out.Name + "' is not followed by '{'";
            return false;
        }
        lexer_.Take();

        if (++depth_ > kMaxDepth) {
            error_ = "nodes nested more than " + std::to_string(kMaxDepth) + " deep";
            return false;
        }
        const bool ok = ReadBody(out);
        --depth_;
        if (!ok) return false;

        if (lexer_.Peek().What != Lexer::Kind::CloseBrace) {
            error_ = "the file ends inside '" + out.Name + "'";
            return false;
        }
        lexer_.Take();
        return true;
    }

    bool ReadBody(Node& node) {
        while (true) {
            const Lexer::Token& token = lexer_.Peek();
            if (token.What == Lexer::Kind::CloseBrace) return true;
            if (token.What == Lexer::Kind::End) {
                error_ = "the file ends inside '" + node.Name + "'";
                return false;
            }
            if (token.What != Lexer::Kind::Ident) {
                lexer_.Take();
                continue;
            }
            if (token.Text == "ROUTE") {
                Node route;
                if (!ReadRoute(route)) return false;
                node.Children.push_back(std::move(route));
                continue;
            }
            if (token.Text == "PROTO" || token.Text == "EXTERNPROTO") {
                if (!SkipProto()) return false;
                continue;
            }

            const std::string field = lexer_.Take().Text;
            if (!ReadFieldValue(node, field)) return false;
        }
    }

    // True when what is ahead opens a node rather than a literal.
    bool AheadIsNode() const {
        const Lexer::Token& token = lexer_.Peek();
        if (token.What != Lexer::Kind::Ident) return false;
        if (token.Text == "DEF" || token.Text == "USE") return true;
        return lexer_.PeekSecond().What == Lexer::Kind::OpenBrace;
    }

    bool ReadFieldValue(Node& node, const std::string& field) {
        // `field IS implementationField` inside a PROTO body. Nothing outside
        // a prototype can act on it, and prototypes are skipped.
        if (lexer_.Peek().What == Lexer::Kind::Ident && lexer_.Peek().Text == "IS") {
            lexer_.Take();
            if (lexer_.Peek().What == Lexer::Kind::Ident) lexer_.Take();
            return true;
        }

        if (lexer_.Peek().What == Lexer::Kind::OpenBracket) {
            lexer_.Take();
            if (lexer_.Peek().What == Lexer::Kind::CloseBracket) {
                lexer_.Take();
                node.Set(field, std::string());   // stated, and empty
                return true;
            }
            if (AheadIsNode()) {
                std::vector<Node> children;
                if (!ReadStatements(children, Lexer::Kind::CloseBracket)) return false;
                if (lexer_.Peek().What != Lexer::Kind::CloseBracket) {
                    error_ = "the file ends inside field '" + field + "'";
                    return false;
                }
                lexer_.Take();
                for (Node& child : children) {
                    child.ContainerField = field;
                    node.Children.push_back(std::move(child));
                }
                return true;
            }
            std::string text;
            if (!ReadLiterals(text, Lexer::Kind::CloseBracket)) return false;
            if (lexer_.Peek().What != Lexer::Kind::CloseBracket) {
                error_ = "the file ends inside field '" + field + "'";
                return false;
            }
            lexer_.Take();
            node.Set(field, std::move(text));
            return true;
        }

        if (AheadIsNode()) {
            Node child;
            if (!ReadNodeStatement(child)) return false;
            child.ContainerField = field;
            node.Children.push_back(std::move(child));
            return true;
        }

        // NULL is how the encoding states an SFNode that holds nothing, which
        // is the same as not stating the field at all.
        if (lexer_.Peek().What == Lexer::Kind::Ident && lexer_.Peek().Text == "NULL") {
            lexer_.Take();
            return true;
        }

        std::string text;
        if (!ReadLiterals(text, Lexer::Kind::CloseBrace)) return false;
        node.Set(field, std::move(text));
        return true;
    }

    // Literals up to `until`, rendered into the XML encoding's text form. Stops
    // at the next field name, which is an identifier that is not a literal
    // keyword - the one place this needs to know that TRUE, FALSE and the
    // enumerants are values rather than names.
    //
    // One string on its own is rendered *unquoted*, because that is what the
    // same value looks like in an XML attribute: `title='the "E-45"'`. Two or
    // more are quoted, because that is how the XML encoding writes an MFString
    // and how the reader above parses one back. Getting this wrong leaves the
    // quotes inside every single-valued string field.
    bool ReadLiterals(std::string& out, Lexer::Kind until) {
        std::vector<Lexer::Token> items;
        bool first = true;
        while (true) {
            const Lexer::Token& token = lexer_.Peek();
            if (token.What == until || token.What == Lexer::Kind::End) break;

            if (token.What == Lexer::Kind::Ident) {
                if (!first) break;               // the next field's name
                const std::string& text = token.Text;
                if (text == "TRUE" || text == "FALSE") {
                    // The XML encoding spells these in lower case, and that is
                    // what the reader above parses.
                    out = text == "TRUE" ? "true" : "false";
                    lexer_.Take();
                    return true;
                }
                // An enumerant, such as a MovieTexture's loop mode.
                out = text;
                lexer_.Take();
                return true;
            }

            if (token.What == Lexer::Kind::String || token.What == Lexer::Kind::Number) {
                items.push_back(lexer_.Take());
                first = false;
                continue;
            }

            // A brace or bracket where a literal was expected: let the caller
            // see it rather than consuming it here.
            break;
        }

        if (items.size() == 1 && items[0].What == Lexer::Kind::String) {
            out = items[0].Text;
            return true;
        }
        for (size_t i = 0; i < items.size(); ++i) {
            if (i) out.push_back(' ');
            if (items[i].What == Lexer::Kind::Number) {
                out += items[i].Text;
                continue;
            }
            // Re-quoted so the MFString parser above reads back the same items,
            // with an embedded quote escaped as the spec requires.
            out.push_back('"');
            for (char c : items[i].Text) {
                if (c == '"' || c == '\\') out.push_back('\\');
                out.push_back(c);
            }
            out.push_back('"');
        }
        return true;
    }

    // ROUTE a.b TO c.d, rendered as the XML encoding's four attributes so the
    // reader's ROUTE handling serves both.
    bool ReadRoute(Node& out) {
        lexer_.Take();   // ROUTE
        std::string from, to;
        if (!ReadRoutePath(from)) return false;
        if (lexer_.Peek().What != Lexer::Kind::Ident || lexer_.Peek().Text != "TO") {
            error_ = "a ROUTE is not followed by TO";
            return false;
        }
        lexer_.Take();
        if (!ReadRoutePath(to)) return false;

        const size_t fromDot = from.rfind('.');
        const size_t toDot = to.rfind('.');
        if (fromDot == std::string::npos || toDot == std::string::npos) {
            error_ = "a ROUTE names no field";
            return false;
        }
        out.Name = "ROUTE";
        out.Set("fromNode", from.substr(0, fromDot));
        out.Set("fromField", from.substr(fromDot + 1));
        out.Set("toNode", to.substr(0, toDot));
        out.Set("toField", to.substr(toDot + 1));
        return true;
    }

    // `name.field`. The lexer does not make '.' a token of its own, so a path
    // usually arrives as one identifier; a file that spaces it out still reads.
    bool ReadRoutePath(std::string& out) {
        if (lexer_.Peek().What != Lexer::Kind::Ident) {
            error_ = "a ROUTE names no node";
            return false;
        }
        out = lexer_.Take().Text;
        if (out.find('.') != std::string::npos) return true;
        if (lexer_.Peek().What == Lexer::Kind::Unknown && lexer_.Peek().Text == ".") {
            lexer_.Take();
            if (lexer_.Peek().What != Lexer::Kind::Ident) {
                error_ = "a ROUTE names no field";
                return false;
            }
            out += "." + lexer_.Take().Text;
        }
        return true;
    }

    // PROTO name [ interface ] { body }, or EXTERNPROTO name [ interface ] url.
    // A prototype is a node type defined in the file, and instantiating one
    // means running its body per instance. Nothing here does that, so the
    // declaration is skipped whole and the instance is reported where it is
    // met - the same answer the XML encoding's <ProtoInstance> gets.
    bool SkipProto() {
        const std::string keyword = lexer_.Take().Text;
        std::string name;
        if (lexer_.Peek().What == Lexer::Kind::Ident) name = lexer_.Take().Text;
        if (warn_)
            warn_("X3D: " + keyword + " " + (name.empty() ? std::string("(unnamed)") : name) +
                  " is a prototype, which this reader does not instantiate; nodes of that "
                  "type are skipped");
        if (!SkipBalanced(Lexer::Kind::OpenBracket, Lexer::Kind::CloseBracket)) return false;
        if (keyword == "EXTERNPROTO") {
            // The url, which may be bracketed or a bare string.
            if (lexer_.Peek().What == Lexer::Kind::OpenBracket)
                return SkipBalanced(Lexer::Kind::OpenBracket, Lexer::Kind::CloseBracket);
            if (lexer_.Peek().What == Lexer::Kind::String) lexer_.Take();
            return true;
        }
        return SkipBalanced(Lexer::Kind::OpenBrace, Lexer::Kind::CloseBrace);
    }

    bool SkipBalanced(Lexer::Kind open, Lexer::Kind close) {
        if (lexer_.Peek().What != open) return true;
        lexer_.Take();
        int depth = 1;
        while (depth > 0) {
            const Lexer::Token& token = lexer_.Peek();
            if (token.What == Lexer::Kind::End) {
                error_ = "the file ends inside a prototype";
                return false;
            }
            if (token.What == open) ++depth;
            if (token.What == close) --depth;
            lexer_.Take();
        }
        return true;
    }

    // PROFILE, COMPONENT, META and UNIT are X3D statements the classic
    // encoding writes at the top level. META and UNIT carry information this
    // reader uses, so they become nodes shaped like the XML encoding's.
    bool ReadStatementKeyword(std::vector<Node>& out) {
        const std::string keyword = lexer_.Take().Text;
        std::vector<std::string> words;
        while (lexer_.Peek().What == Lexer::Kind::Ident ||
               lexer_.Peek().What == Lexer::Kind::String ||
               lexer_.Peek().What == Lexer::Kind::Number) {
            // Stop before the next statement: a bare word that opens a node is
            // not part of this one.
            if (lexer_.Peek().What == Lexer::Kind::Ident && AheadIsNode()) break;
            words.push_back(lexer_.Take().Text);
            if (keyword == "META" && words.size() == 2) break;
            if (keyword == "PROFILE" && words.size() == 1) break;
            if (keyword == "COMPONENT" && words.size() == 1) break;
            if (keyword == "UNIT" && words.size() == 3) break;
        }

        if (keyword == "META" && words.size() == 2) {
            Node meta;
            meta.Name = "meta";
            meta.Set("name", words[0]);
            meta.Set("content", words[1]);
            out.push_back(std::move(meta));
        } else if (keyword == "UNIT" && words.size() == 3) {
            Node unit;
            unit.Name = "unit";
            unit.Set("category", words[0]);
            unit.Set("name", words[1]);
            unit.Set("conversionFactor", words[2]);
            out.push_back(std::move(unit));
        } else if (keyword == "PROFILE" && words.size() == 1) {
            Node profile;
            profile.Name = "PROFILE";
            profile.Set("name", words[0]);
            out.push_back(std::move(profile));
        }
        return true;
    }

    Lexer& lexer_;
    const std::function<void(const std::string&)>& warn_;
    std::string error_;
    int depth_ = 0;
};

// The header comment both revisions are required to open with:
//     #VRML V2.0 utf8
//     #X3D V3.3 utf8
bool ReadHeaderLine(const std::string& head, std::string& version, bool& vrml97) {
    if (head.compare(0, 6, "#VRML ") == 0) {
        vrml97 = true;
    } else if (head.compare(0, 5, "#X3D ") == 0) {
        vrml97 = false;
    } else {
        return false;
    }
    const size_t v = head.find_first_of("Vv", 4);
    if (v != std::string::npos) {
        size_t end = v + 1;
        while (end < head.size() && (std::isdigit(static_cast<unsigned char>(head[end])) ||
                                     head[end] == '.'))
            ++end;
        version = head.substr(v + 1, end - v - 1);
    }
    // VRML 1.0 is a different node set entirely - Separator, Coordinate3,
    // IndexedFaceSet with a different field set - and reading it as VRML97
    // would produce an empty scene rather than an error. The caller refuses it
    // by name instead.
    return true;
}

} // namespace

bool LooksLikeX3DXml(const std::string& head) {
    if (head.find("<X3D") != std::string::npos) return true;
    // An XML declaration or a DOCTYPE, with the X3D name close behind.
    if (head.compare(0, 5, "<?xml") == 0 || head.compare(0, 9, "<!DOCTYPE") == 0)
        return head.find("X3D") != std::string::npos;
    return false;
}

bool LooksLikeClassicVrml(const std::string& head) {
    return head.compare(0, 6, "#VRML ") == 0 || head.compare(0, 5, "#X3D ") == 0;
}

bool ParseClassicVrml(const std::vector<uint8_t>& data, Scene& out, std::string& error,
                      const std::function<void(const std::string&)>& warn) {
    const size_t sniff = data.size() < 64 ? data.size() : 64;
    const std::string head(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(sniff));
    if (!ReadHeaderLine(head, out.Version, out.Vrml97)) {
        error = "X3D: not the classic encoding - the file does not open '#VRML' or '#X3D'";
        return false;
    }
    if (out.Vrml97 && !out.Version.empty() && out.Version[0] == '1') {
        error = "X3D: this is VRML 1.0, whose node set (Separator, Coordinate3, ...) is not "
                "VRML97's and is not read; re-export as VRML97 or X3D";
        return false;
    }

    Lexer lexer(reinterpret_cast<const char*>(data.data()), data.size());
    Parser parser(lexer, warn);

    out.How = Encoding::ClassicVrml;
    out.Root.Name = "X3D";

    // The XML encoding wraps everything in <Scene>; the classic one has no such
    // node, so one is synthesised. Doing it here rather than in the reader is
    // what lets the reader have a single entry point.
    Node scene;
    scene.Name = "Scene";
    std::vector<Node> statements;
    if (!parser.ReadStatements(statements, Lexer::Kind::End)) {
        error = "X3D: " + parser.Error();
        return false;
    }

    Node head_;
    head_.Name = "head";
    for (Node& statement : statements) {
        if (statement.Name == "meta" || statement.Name == "unit") {
            head_.Children.push_back(std::move(statement));
        } else if (statement.Name == "PROFILE") {
            const char* name = statement.Attribute("name");
            if (name) out.Profile = name;
        } else {
            scene.Children.push_back(std::move(statement));
        }
    }

    out.Root.Set("version", out.Version);
    if (!out.Profile.empty()) out.Root.Set("profile", out.Profile);
    out.Root.Children.push_back(std::move(head_));
    out.Root.Children.push_back(std::move(scene));
    return true;
}

} // namespace X3D
} // namespace UltraCanvas
