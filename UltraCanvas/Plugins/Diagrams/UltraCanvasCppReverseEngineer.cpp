// Plugins/Diagrams/UltraCanvasCppReverseEngineer.cpp
// Heuristic C++ header -> UML model scanner
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework
//
// Structure of the scan:
//   1. StripCommentsAndDirectives  — comments, literals and #directives out.
//   2. ScanScope                   — walks a brace scope looking for
//                                    namespace / class / struct / enum
//                                    definitions; recurses into both.
//   3. ParseClassBody              — splits a class body into declarations at
//                                    brace depth 0, tracking the access
//                                    section, and recursing into nested types.
//   4. ParseDeclaration            — turns one declaration into a UMLMember.
//   5. ResolveInferredRelationships— resolves edges recorded by type name.

#include "Plugins/Diagrams/UltraCanvasCppReverseEngineer.h"
#include "Plugins/Diagrams/UltraCanvasUMLNotation.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace UltraCanvas {

using UMLNotation::Trim;

// =============================================================================
// SMALL HELPERS
// =============================================================================

namespace {

bool IsIdentifierChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool IsIdentifierStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

// True if `word` appears in `text` at `pos` as a whole word.
bool IsWordAt(const std::string& text, size_t pos, const std::string& word) {
    if (pos + word.size() > text.size()) return false;
    if (text.compare(pos, word.size(), word) != 0) return false;
    if (pos > 0 && IsIdentifierChar(text[pos - 1])) return false;
    size_t after = pos + word.size();
    if (after < text.size() && IsIdentifierChar(text[after])) return false;
    return true;
}

// Index of the matching close brace for the open brace at `openPos`, or npos.
size_t FindMatchingBrace(const std::string& text, size_t openPos) {
    int depth = 0;
    for (size_t i = openPos; i < text.size(); ++i) {
        if (text[i] == '{') ++depth;
        else if (text[i] == '}') {
            if (--depth == 0) return i;
        }
    }
    return std::string::npos;
}

// Index of the matching '>' for the '<' at `openPos`, or npos.
size_t FindMatchingAngle(const std::string& text, size_t openPos) {
    int depth = 0;
    for (size_t i = openPos; i < text.size(); ++i) {
        if (text[i] == '<') ++depth;
        else if (text[i] == '>') {
            if (--depth == 0) return i;
        } else if (text[i] == ';' || text[i] == '{') {
            return std::string::npos;  // not a template argument list after all
        }
    }
    return std::string::npos;
}

size_t SkipSpace(const std::string& text, size_t pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    return pos;
}

// Reads the identifier starting at `pos` (which must be an identifier start).
std::string ReadIdentifier(const std::string& text, size_t& pos) {
    size_t begin = pos;
    while (pos < text.size() && IsIdentifierChar(text[pos])) ++pos;
    return text.substr(begin, pos - begin);
}

// Collapses runs of whitespace to single spaces.
std::string CollapseWhitespace(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool space = false;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            space = true;
            continue;
        }
        if (space && !out.empty()) out.push_back(' ');
        space = false;
        out.push_back(c);
    }
    return out;
}

// Drops namespace qualification: "UltraCanvas::detail::Node" -> "Node".
std::string UnqualifyName(const std::string& name) {
    size_t pos = name.rfind("::");
    return (pos == std::string::npos) ? name : name.substr(pos + 2);
}

// Strips a leading run of the given keywords from a declaration, reporting
// which of the interesting ones were present.
struct DeclarationSpecifiers {
    bool isVirtual = false;
    bool isStatic = false;
    bool isExplicit = false;
    bool isFriend = false;
};

DeclarationSpecifiers StripLeadingSpecifiers(std::string& declaration) {
    DeclarationSpecifiers specs;
    for (;;) {
        std::string trimmed = Trim(declaration);
        if (trimmed.empty()) { declaration = trimmed; break; }

        // [[nodiscard]] and friends.
        if (trimmed.compare(0, 2, "[[") == 0) {
            size_t close = trimmed.find("]]");
            if (close == std::string::npos) break;
            declaration = trimmed.substr(close + 2);
            continue;
        }

        size_t pos = 0;
        std::string word;
        if (IsIdentifierStart(trimmed[0])) word = ReadIdentifier(trimmed, pos);
        if (word.empty()) { declaration = trimmed; break; }

        bool consumed = true;
        if (word == "virtual")        specs.isVirtual = true;
        else if (word == "static")    specs.isStatic = true;
        else if (word == "explicit")  specs.isExplicit = true;
        else if (word == "friend")    specs.isFriend = true;
        else if (word == "inline" || word == "constexpr" || word == "consteval" ||
                 word == "constinit" || word == "mutable" || word == "extern" ||
                 word == "thread_local" || word == "typename") {
            // recognised, no UML meaning
        } else {
            consumed = false;
        }

        if (!consumed) { declaration = trimmed; break; }
        declaration = Trim(trimmed.substr(pos));
    }
    return specs;
}

// Splits on `separator` at bracket/paren/angle depth 0.
std::vector<std::string> SplitTopLevel(const std::string& text, char separator) {
    std::vector<std::string> parts;
    int paren = 0, angle = 0, bracket = 0;
    std::string current;
    for (char c : text) {
        if (c == separator && paren == 0 && angle == 0 && bracket == 0) {
            parts.push_back(current);
            current.clear();
            continue;
        }
        switch (c) {
            case '(': ++paren; break;
            case ')': if (paren) --paren; break;
            case '<': ++angle; break;
            case '>': if (angle) --angle; break;
            case '[': ++bracket; break;
            case ']': if (bracket) --bracket; break;
            default: break;
        }
        current.push_back(c);
    }
    parts.push_back(current);
    return parts;
}

// Position of the first `needle` at depth 0, or npos.
size_t FindTopLevelChar(const std::string& text, char needle) {
    int paren = 0, angle = 0, bracket = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == needle && paren == 0 && angle == 0 && bracket == 0) return i;
        switch (c) {
            case '(': ++paren; break;
            case ')': if (paren) --paren; break;
            case '<': ++angle; break;
            case '>': if (angle) --angle; break;
            case '[': ++bracket; break;
            case ']': if (bracket) --bracket; break;
            default: break;
        }
    }
    return std::string::npos;
}

const char* const kContainerTemplates[] = {
    "vector", "list", "deque", "set", "multiset", "unordered_set",
    "unordered_multiset", "array", "forward_list", "span", "initializer_list"
};
const char* const kMapTemplates[] = {
    "map", "multimap", "unordered_map", "unordered_multimap"
};

bool IsOneOf(const std::string& name, const char* const* table, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (name == table[i]) return true;
    }
    return false;
}

} // namespace

// =============================================================================
// RESULT
// =============================================================================

void CppReverseEngineerResult::Merge(const CppReverseEngineerResult& other) {
    filesParsed += other.filesParsed;
    classifiersAdded += other.classifiersAdded;
    relationshipsAdded += other.relationshipsAdded;
    declarationsSkipped += other.declarationsSkipped;
    warnings.insert(warnings.end(), other.warnings.begin(), other.warnings.end());
    unreadableFiles.insert(unreadableFiles.end(),
                           other.unreadableFiles.begin(), other.unreadableFiles.end());
}

// =============================================================================
// PREPROCESSING
// =============================================================================

std::string UltraCanvasCppReverseEngineer::StripCommentsAndDirectives(
        const std::string& source) {
    std::string out;
    out.reserve(source.size());

    enum class State { Code, LineComment, BlockComment, String, Char, Directive };
    State state = State::Code;
    bool atLineStart = true;

    for (size_t i = 0; i < source.size(); ++i) {
        char c = source[i];
        char next = (i + 1 < source.size()) ? source[i + 1] : '\0';

        switch (state) {
            case State::Code:
                if (c == '/' && next == '/') { state = State::LineComment; ++i; out.push_back(' '); out.push_back(' '); }
                else if (c == '/' && next == '*') { state = State::BlockComment; ++i; out.push_back(' '); out.push_back(' '); }
                else if (c == '"') { state = State::String; out.push_back('"'); }
                else if (c == '\'') { state = State::Char; out.push_back('\''); }
                else if (c == '#' && atLineStart) { state = State::Directive; out.push_back(' '); }
                else out.push_back(c);
                break;

            case State::LineComment:
                if (c == '\n') { state = State::Code; out.push_back('\n'); }
                else out.push_back(' ');
                break;

            case State::BlockComment:
                if (c == '*' && next == '/') { state = State::Code; ++i; out.push_back(' '); out.push_back(' '); }
                else out.push_back(c == '\n' ? '\n' : ' ');
                break;

            case State::String:
                // Blank the contents so a ';' or '{' inside a literal cannot
                // confuse the scanner, but keep the quotes.
                if (c == '\\' && next != '\0') { ++i; out.push_back(' '); out.push_back(' '); }
                else if (c == '"') { state = State::Code; out.push_back('"'); }
                else out.push_back(c == '\n' ? '\n' : ' ');
                break;

            case State::Char:
                if (c == '\\' && next != '\0') { ++i; out.push_back(' '); out.push_back(' '); }
                else if (c == '\'') { state = State::Code; out.push_back('\''); }
                else out.push_back(' ');
                break;

            case State::Directive:
                // A directive continues while lines end with a backslash.
                if (c == '\n') {
                    bool continued = false;
                    for (size_t j = out.size(); j-- > 0;) {
                        if (out[j] == ' ') continue;
                        continued = (out[j] == '\\');
                        break;
                    }
                    out.push_back('\n');
                    if (!continued) state = State::Code;
                } else {
                    out.push_back(c == '\\' ? '\\' : ' ');
                }
                break;
        }

        atLineStart = (c == '\n') ||
                      (atLineStart && std::isspace(static_cast<unsigned char>(c)));
    }

    // Blank out any backslashes we kept purely for continuation detection.
    for (char& c : out) {
        if (c == '\\') c = ' ';
    }
    return out;
}

// =============================================================================
// TYPE ANALYSIS
// =============================================================================

bool UltraCanvasCppReverseEngineer::ExtractReferencedType(const std::string& declaredType,
                                                          std::string& outCoreType,
                                                          std::string& outMultiplicity,
                                                          TypeOwnership& outOwnership) {
    outCoreType.clear();
    outMultiplicity = "1";
    outOwnership = TypeOwnership::Value;

    std::string type = CollapseWhitespace(declaredType);
    if (type.empty()) return false;

    // Strip cv-qualifiers wherever they appear at the top level.
    for (const char* qualifier : {"const ", "volatile ", "constexpr "}) {
        for (;;) {
            size_t pos = type.find(qualifier);
            if (pos == std::string::npos) break;
            if (pos > 0 && IsIdentifierChar(type[pos - 1])) break;
            type = Trim(type.substr(0, pos) + type.substr(pos + std::string(qualifier).size()));
        }
    }
    type = Trim(type);
    while (!type.empty() && (type.back() == ' ')) type.pop_back();

    // Reference / pointer suffixes.
    bool sawReference = false, sawPointer = false;
    while (!type.empty() && (type.back() == '&' || type.back() == '*')) {
        if (type.back() == '&') sawReference = true;
        else sawPointer = true;
        type.pop_back();
        type = Trim(type);
    }

    // Unwrap one layer of a known template at a time.
    bool unwrapped = true;
    int guard = 0;
    while (unwrapped && guard++ < 8) {
        unwrapped = false;
        size_t angle = FindTopLevelChar(type, '<');
        if (angle == std::string::npos) break;
        size_t close = FindMatchingAngle(type, angle);
        if (close == std::string::npos) break;

        std::string templateName = UnqualifyName(Trim(type.substr(0, angle)));
        std::string arguments = type.substr(angle + 1, close - angle - 1);
        std::vector<std::string> args = SplitTopLevel(arguments, ',');
        for (auto& a : args) a = Trim(a);
        if (args.empty()) break;

        if (templateName == "shared_ptr" || templateName == "weak_ptr") {
            if (outOwnership == TypeOwnership::Value) outOwnership = TypeOwnership::SharedPointer;
            type = args[0];
            unwrapped = true;
        } else if (templateName == "unique_ptr") {
            if (outOwnership == TypeOwnership::Value) outOwnership = TypeOwnership::UniquePointer;
            type = args[0];
            unwrapped = true;
        } else if (templateName == "optional") {
            outMultiplicity = "0..1";
            type = args[0];
            unwrapped = true;
        } else if (IsOneOf(templateName, kContainerTemplates,
                           sizeof(kContainerTemplates) / sizeof(kContainerTemplates[0]))) {
            outMultiplicity = "*";
            type = args[0];
            unwrapped = true;
        } else if (IsOneOf(templateName, kMapTemplates,
                           sizeof(kMapTemplates) / sizeof(kMapTemplates[0]))) {
            outMultiplicity = "*";
            type = args.size() > 1 ? args[1] : args[0];  // the mapped type
            unwrapped = true;
        } else {
            break;  // a user template: keep it as the referenced type
        }
    }

    // Pointer/reference qualifiers may sit on the unwrapped argument too.
    while (!type.empty() && (type.back() == '&' || type.back() == '*')) {
        if (type.back() == '&') sawReference = true;
        else sawPointer = true;
        type.pop_back();
        type = Trim(type);
    }
    for (const char* qualifier : {"const ", "volatile "}) {
        size_t pos = type.find(qualifier);
        if (pos == 0) type = Trim(type.substr(std::string(qualifier).size()));
    }

    if (outOwnership == TypeOwnership::Value) {
        if (sawPointer)        outOwnership = TypeOwnership::RawPointer;
        else if (sawReference) outOwnership = TypeOwnership::Reference;
    }
    if ((outOwnership == TypeOwnership::RawPointer ||
         outOwnership == TypeOwnership::SharedPointer) && outMultiplicity == "1") {
        outMultiplicity = "0..1";  // a pointer may be null
    }

    outCoreType = UnqualifyName(Trim(type));
    return !outCoreType.empty();
}

// =============================================================================
// PARSING
// =============================================================================

namespace {

// Everything the scanner needs to know while walking one scope.
struct ScanContext {
    const CppReverseEngineerOptions* options = nullptr;
    UltraCanvasUMLModel* model = nullptr;
    CppReverseEngineerResult* result = nullptr;
    std::string sourceName;
    std::vector<std::string> namespaceStack;
};

struct PendingEdgeOut {
    UMLRelationshipKind kind;
    std::string sourceClassifierId;
    std::string targetTypeName;
    std::string roleName;
    std::string multiplicity;
};

bool NameExcluded(const std::string& name, const CppReverseEngineerOptions& options) {
    for (const auto& prefix : options.excludeNamePrefixes) {
        if (!prefix.empty() && name.compare(0, prefix.size(), prefix) == 0) return true;
    }
    return false;
}

// Parses a base-clause ("public Base, virtual protected Other<T>") into names.
std::vector<std::string> ParseBaseClause(const std::string& clause) {
    std::vector<std::string> bases;
    for (const auto& piece : SplitTopLevel(clause, ',')) {
        std::string base = Trim(piece);
        // Drop access and virtual keywords.
        for (;;) {
            size_t pos = 0;
            std::string trimmed = Trim(base);
            if (trimmed.empty()) break;
            if (!IsIdentifierStart(trimmed[0])) { base = trimmed; break; }
            std::string word = ReadIdentifier(trimmed, pos);
            if (word == "public" || word == "protected" || word == "private" ||
                word == "virtual") {
                base = Trim(trimmed.substr(pos));
                continue;
            }
            base = trimmed;
            break;
        }
        // Drop template arguments: Base<T> -> Base.
        size_t angle = FindTopLevelChar(base, '<');
        if (angle != std::string::npos) base = Trim(base.substr(0, angle));
        base = UnqualifyName(Trim(base));
        if (!base.empty()) bases.push_back(base);
    }
    return bases;
}

// Turns one declaration (no braces, no trailing ';') into a UMLMember.
// Returns false when the declaration is not a member the diagram should show.
bool ParseDeclaration(const std::string& declarationText,
                      const std::string& enclosingClassName,
                      UMLVisibility visibility,
                      const CppReverseEngineerOptions& options,
                      std::vector<UMLMember>& outMembers,
                      bool& outUnderstood) {
    outUnderstood = true;
    std::string declaration = Trim(CollapseWhitespace(declarationText));
    if (declaration.empty()) return false;

    // Member template: record nothing about the parameters, parse the rest.
    if (declaration.compare(0, 8, "template") == 0 && !IsIdentifierChar(declaration[8])) {
        size_t angle = declaration.find('<');
        if (angle == std::string::npos) { outUnderstood = false; return false; }
        size_t close = FindMatchingAngle(declaration, angle);
        if (close == std::string::npos) { outUnderstood = false; return false; }
        declaration = Trim(declaration.substr(close + 1));
        if (declaration.empty()) return false;
    }

    // Things that are never diagram members.
    static const char* const skipKeywords[] = {
        "using", "typedef", "static_assert", "public", "private", "protected",
        "friend", "namespace", "enum", "class", "struct", "union", "operator"
    };
    {
        size_t pos = 0;
        std::string firstWord;
        if (IsIdentifierStart(declaration[0])) firstWord = ReadIdentifier(declaration, pos);
        for (const char* keyword : skipKeywords) {
            if (firstWord == keyword) {
                // "class Foo* member;" is a member, but "class Foo;" is not.
                // Both are rare in modern headers — skip and count.
                return false;
            }
        }
    }

    DeclarationSpecifiers specs = StripLeadingSpecifiers(declaration);
    if (specs.isFriend) return false;
    if (declaration.empty()) return false;

    // Operation or data member? A '(' alone does not settle it: a member
    // initialiser is full of them ("Color color = Color(255, 0, 0, 255)"). It
    // is an operation only when the '(' comes *before* any top-level '=' —
    // which still covers "= 0", "= default" and "= delete" on functions.
    size_t openParen = FindTopLevelChar(declaration, '(');
    size_t assign = FindTopLevelChar(declaration, '=');
    bool isOperatorDeclaration = declaration.find("operator") != std::string::npos;
    bool looksLikeOperation = openParen != std::string::npos &&
                              (isOperatorDeclaration || assign == std::string::npos ||
                               openParen < assign);

    if (looksLikeOperation) {
        // ---------------- operation ----------------
        std::string head = Trim(declaration.substr(0, openParen));
        if (head.empty()) { outUnderstood = false; return false; }

        // Matching close paren.
        int depth = 0;
        size_t closeParen = std::string::npos;
        for (size_t i = openParen; i < declaration.size(); ++i) {
            if (declaration[i] == '(') ++depth;
            else if (declaration[i] == ')') {
                if (--depth == 0) { closeParen = i; break; }
            }
        }
        if (closeParen == std::string::npos) { outUnderstood = false; return false; }

        std::string parameterText = declaration.substr(openParen + 1, closeParen - openParen - 1);
        std::string tail = Trim(declaration.substr(closeParen + 1));

        // A function pointer member ("void (*callback)(int)") has '(' in the
        // head too; those are data members in UML terms and rare enough to skip.
        if (head.back() == '*' || head.back() == '&') { outUnderstood = false; return false; }

        // Split head into return type + name.
        std::string name, returnType;
        {
            size_t end = head.size();
            while (end > 0 && !IsIdentifierChar(head[end - 1])) --end;
            size_t begin = end;
            while (begin > 0 && IsIdentifierChar(head[begin - 1])) --begin;
            name = head.substr(begin, end - begin);
            returnType = Trim(head.substr(0, begin));
            // Destructor: the '~' sits just before the name.
            if (begin > 0 && head[begin - 1] == '~') {
                name = "~" + name;
                returnType = Trim(head.substr(0, begin - 1));
            }
        }
        if (name.empty()) { outUnderstood = false; return false; }

        if (head.find("operator") != std::string::npos) {
            if (!options.includeOperators) return false;
            name = Trim(head.substr(head.find("operator")));
            returnType = Trim(head.substr(0, head.find("operator")));
        }

        bool isConstructor = (name == enclosingClassName);
        bool isDestructor = (!name.empty() && name[0] == '~');
        if ((isConstructor || isDestructor) && !options.includeConstructors) return false;
        if (!options.includeOperations) return false;

        UMLMember member;
        member.kind = UMLMemberKind::Operation;
        member.visibility = visibility;
        member.name = name;
        member.isStatic = specs.isStatic;

        // Trailing return type wins over the leading "auto".
        size_t arrow = tail.find("->");
        if (arrow != std::string::npos) {
            std::string trailing = Trim(tail.substr(arrow + 2));
            size_t stop = trailing.find_first_of("={");
            if (stop != std::string::npos) trailing = Trim(trailing.substr(0, stop));
            if (!trailing.empty()) returnType = trailing;
        }
        if (!isConstructor && !isDestructor) member.type = returnType;

        // Pure virtual / defaulted / deleted.
        std::string tailNoSpace;
        for (char c : tail) {
            if (!std::isspace(static_cast<unsigned char>(c))) tailNoSpace.push_back(c);
        }
        if (tailNoSpace.find("=0") != std::string::npos) member.isAbstract = true;
        if (tailNoSpace.find("=delete") != std::string::npos) return false;

        std::string properties;
        if (tailNoSpace.find("const") != std::string::npos &&
            tailNoSpace.compare(0, 5, "const") == 0) {
            properties = "{query}";
        }
        member.propertyString = properties;

        for (const auto& piece : SplitTopLevel(parameterText, ',')) {
            std::string param = Trim(piece);
            if (param.empty() || param == "void") continue;
            // Strip a default argument.
            size_t eq = FindTopLevelChar(param, '=');
            std::string defaultValue;
            if (eq != std::string::npos) {
                defaultValue = Trim(param.substr(eq + 1));
                param = Trim(param.substr(0, eq));
            }
            // Split into type and (optional) name: the trailing identifier is
            // the parameter name unless the whole thing is one token.
            std::string type = param, paramName;
            size_t end = param.size();
            while (end > 0 && !IsIdentifierChar(param[end - 1])) --end;
            size_t begin = end;
            while (begin > 0 && IsIdentifierChar(param[begin - 1])) --begin;
            if (begin > 0 && end == param.size()) {
                paramName = param.substr(begin, end - begin);
                type = Trim(param.substr(0, begin));
            }
            UMLParameter p;
            p.name = paramName;
            p.type = Trim(type);
            p.defaultValue = defaultValue;
            if (p.type.empty() && !p.name.empty()) { p.type = p.name; p.name.clear(); }
            member.parameters.push_back(p);
        }

        outMembers.push_back(member);
        return true;
    }

    // ---------------- data member(s) ----------------
    if (!options.includeAttributes) return false;

    // Split off an initialiser first so "int a = f(1,2), b" still splits right.
    std::vector<std::string> declarators = SplitTopLevel(declaration, ',');
    std::string sharedType;
    bool any = false;

    for (size_t d = 0; d < declarators.size(); ++d) {
        std::string piece = Trim(declarators[d]);
        if (piece.empty()) continue;

        std::string defaultValue;
        size_t eq = FindTopLevelChar(piece, '=');
        if (eq != std::string::npos) {
            defaultValue = Trim(piece.substr(eq + 1));
            piece = Trim(piece.substr(0, eq));
        }

        // Array bound -> multiplicity.
        std::string multiplicity;
        size_t bracket = FindTopLevelChar(piece, '[');
        if (bracket != std::string::npos) {
            size_t close = piece.find(']', bracket);
            if (close != std::string::npos) {
                std::string bound = Trim(piece.substr(bracket + 1, close - bracket - 1));
                multiplicity = bound.empty() ? "*" : bound;
                piece = Trim(piece.substr(0, bracket) + piece.substr(close + 1));
            }
        }

        // Bit-field: "unsigned flags : 3".
        size_t colon = FindTopLevelChar(piece, ':');
        if (colon != std::string::npos && (colon + 1 >= piece.size() || piece[colon + 1] != ':')) {
            piece = Trim(piece.substr(0, colon));
        }

        std::string name, type;
        size_t end = piece.size();
        while (end > 0 && !IsIdentifierChar(piece[end - 1])) --end;
        size_t begin = end;
        while (begin > 0 && IsIdentifierChar(piece[begin - 1])) --begin;
        name = piece.substr(begin, end - begin);
        type = Trim(piece.substr(0, begin));

        if (d == 0) {
            sharedType = type;
        } else if (type.empty()) {
            type = sharedType;  // "int a, b;"
        }

        if (name.empty()) { outUnderstood = false; continue; }
        // A bare type with no declarator name is not a member.
        if (type.empty()) { outUnderstood = false; continue; }

        UMLMember member;
        member.kind = UMLMemberKind::Attribute;
        member.visibility = visibility;
        member.name = name;
        member.type = type;
        member.multiplicity = multiplicity;
        member.defaultValue = defaultValue;
        member.isStatic = specs.isStatic;
        outMembers.push_back(member);
        any = true;
    }
    return any;
}

} // namespace

// =============================================================================
// SCOPE WALKER
// =============================================================================

namespace {

// Forward declaration: classes and namespaces nest arbitrarily.
void ScanScope(const std::string& text, size_t begin, size_t end,
               ScanContext& context, std::vector<PendingEdgeOut>& pendingEdges,
               const std::string& enclosingClassifierId);

void ParseEnum(const std::string& text, size_t namePos, size_t bodyOpen, size_t bodyClose,
               bool scoped, ScanContext& context) {
    size_t pos = namePos;
    pos = SkipSpace(text, pos);
    std::string name;
    if (pos < text.size() && IsIdentifierStart(text[pos])) name = ReadIdentifier(text, pos);
    if (name.empty() && !context.options->includeAnonymousTypes) return;
    if (NameExcluded(name, *context.options)) return;

    UMLClassifier classifier;
    classifier.name = name.empty() ? "(anonymous enum)" : name;
    classifier.id = context.model->MakeUniqueClassifierId(classifier.name);
    classifier.kind = UMLClassifierKind::Enumeration;
    classifier.stereotypes.push_back("enumeration");
    if (context.options->recordSourceFile) classifier.sourceFile = context.sourceName;
    if (context.options->qualifyNamesWithNamespace && !context.namespaceStack.empty()) {
        std::string qualified;
        for (const auto& ns : context.namespaceStack) qualified += ns + "::";
        classifier.name = qualified + classifier.name;
    }
    (void)scoped;

    std::string body = text.substr(bodyOpen + 1, bodyClose - bodyOpen - 1);
    for (const auto& piece : SplitTopLevel(body, ',')) {
        std::string literal = Trim(CollapseWhitespace(piece));
        if (literal.empty()) continue;
        classifier.literals.push_back(UMLNotation::ParseLiteral(literal));
    }

    context.model->AddClassifier(classifier);
    context.result->classifiersAdded++;
}

// Splits a class body into declarations and members.
void ParseClassBody(const std::string& text, size_t bodyOpen, size_t bodyClose,
                    const std::string& className, bool isStruct,
                    UMLClassifier& classifier, ScanContext& context,
                    std::vector<PendingEdgeOut>& pendingEdges) {
    UMLVisibility access = isStruct ? UMLVisibility::Public : UMLVisibility::Private;

    size_t i = bodyOpen + 1;
    size_t declStart = i;

    auto flushDeclaration = [&](size_t declEnd) {
        if (declEnd <= declStart) return;
        std::string declaration = text.substr(declStart, declEnd - declStart);
        if (Trim(declaration).empty()) return;

        bool visible = true;
        if (access == UMLVisibility::Private && !context.options->includePrivateMembers) visible = false;
        if (access == UMLVisibility::Protected && !context.options->includeProtectedMembers) visible = false;

        std::vector<UMLMember> members;
        bool understood = true;
        ParseDeclaration(declaration, className, access, *context.options, members, understood);
        if (!understood) context.result->declarationsSkipped++;
        if (!visible) return;

        for (auto& member : members) {
            if (member.isStatic && !context.options->includeStaticMembers) continue;
            if (member.kind == UMLMemberKind::Operation) classifier.operations.push_back(member);
            else classifier.attributes.push_back(member);
        }
    };

    while (i < bodyClose) {
        char c = text[i];

        // Access specifier.
        if (IsIdentifierStart(c)) {
            static const char* const accessWords[] = {"public", "protected", "private"};
            bool matched = false;
            for (const char* word : accessWords) {
                if (!IsWordAt(text, i, word)) continue;
                size_t after = SkipSpace(text, i + std::string(word).size());
                if (after < text.size() && text[after] == ':' &&
                    (after + 1 >= text.size() || text[after + 1] != ':')) {
                    flushDeclaration(i);
                    access = (std::string(word) == "public")    ? UMLVisibility::Public
                           : (std::string(word) == "protected") ? UMLVisibility::Protected
                                                                : UMLVisibility::Private;
                    i = after + 1;
                    declStart = i;
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
        }

        if (c == ';') {
            flushDeclaration(i);
            i++;
            declStart = i;
            continue;
        }

        if (c == '{') {
            size_t close = FindMatchingBrace(text, i);
            if (close == std::string::npos) break;

            std::string head = Trim(CollapseWhitespace(text.substr(declStart, i - declStart)));
            bool isNestedType =
                head.find("class ") != std::string::npos ||
                head.find("struct ") != std::string::npos ||
                head.find("enum ") != std::string::npos ||
                head.find("union ") != std::string::npos;

            if (isNestedType) {
                if (context.options->includeNestedClasses) {
                    ScanScope(text, declStart, close + 1, context, pendingEdges, classifier.id);
                }
            } else {
                // Inline function body or member initialiser: the declaration
                // is the head, the block is not.
                flushDeclaration(i);
            }

            i = close + 1;
            // Swallow the ';' that closes a nested type or an initialiser.
            size_t after = SkipSpace(text, i);
            if (after < text.size() && text[after] == ';') i = after + 1;
            declStart = i;
            continue;
        }

        ++i;
    }
    flushDeclaration(bodyClose);
}

void ScanScope(const std::string& text, size_t begin, size_t end,
               ScanContext& context, std::vector<PendingEdgeOut>& pendingEdges,
               const std::string& enclosingClassifierId) {
    size_t i = begin;
    while (i < end) {
        char c = text[i];

        if (c == '{') {
            // A brace we are not interested in (function body, initialiser).
            size_t close = FindMatchingBrace(text, i);
            if (close == std::string::npos || close >= end) return;
            i = close + 1;
            continue;
        }

        if (!IsIdentifierStart(c)) { ++i; continue; }

        // ---- namespace ----
        if (IsWordAt(text, i, "namespace")) {
            size_t pos = SkipSpace(text, i + 9);
            std::string name;
            while (pos < end && (IsIdentifierChar(text[pos]) || text[pos] == ':')) {
                name.push_back(text[pos]);
                ++pos;
            }
            pos = SkipSpace(text, pos);
            if (pos < end && text[pos] == '{') {
                size_t close = FindMatchingBrace(text, pos);
                if (close == std::string::npos || close > end) return;
                context.namespaceStack.push_back(Trim(name));
                ScanScope(text, pos + 1, close, context, pendingEdges, enclosingClassifierId);
                context.namespaceStack.pop_back();
                i = close + 1;
                continue;
            }
            i = pos;
            continue;
        }

        // ---- enum / enum class ----
        if (IsWordAt(text, i, "enum")) {
            if (!context.options->includeEnums) {
                size_t semi = text.find(';', i);
                size_t brace = text.find('{', i);
                if (brace != std::string::npos && (semi == std::string::npos || brace < semi)) {
                    size_t close = FindMatchingBrace(text, brace);
                    i = (close == std::string::npos) ? end : close + 1;
                } else {
                    i = (semi == std::string::npos) ? end : semi + 1;
                }
                continue;
            }
            size_t pos = SkipSpace(text, i + 4);
            bool scoped = false;
            if (IsWordAt(text, pos, "class") || IsWordAt(text, pos, "struct")) {
                scoped = true;
                pos = SkipSpace(text, pos + (IsWordAt(text, pos, "class") ? 5 : 6));
            }
            size_t namePos = pos;
            size_t brace = text.find('{', pos);
            size_t semi = text.find(';', pos);
            if (brace == std::string::npos || (semi != std::string::npos && semi < brace)) {
                i = (semi == std::string::npos) ? end : semi + 1;  // forward declaration
                continue;
            }
            size_t close = FindMatchingBrace(text, brace);
            if (close == std::string::npos) return;
            ParseEnum(text, namePos, brace, close, scoped, context);
            i = close + 1;
            continue;
        }

        // ---- template<...> ----
        if (IsWordAt(text, i, "template")) {
            size_t angle = text.find('<', i);
            if (angle == std::string::npos) { i += 8; continue; }
            size_t close = FindMatchingAngle(text, angle);
            if (close == std::string::npos) { i += 8; continue; }
            // Fall through to the class/struct that follows.
            i = close + 1;
            continue;
        }

        // ---- class / struct ----
        bool isClass = IsWordAt(text, i, "class");
        bool isStruct = IsWordAt(text, i, "struct");
        if (isClass || isStruct) {
            if ((isClass && !context.options->includeClasses) ||
                (isStruct && !context.options->includeStructs)) {
                size_t brace = text.find('{', i);
                size_t semi = text.find(';', i);
                if (brace != std::string::npos && (semi == std::string::npos || brace < semi)) {
                    size_t close = FindMatchingBrace(text, brace);
                    i = (close == std::string::npos) ? end : close + 1;
                } else {
                    i = (semi == std::string::npos) ? end : semi + 1;
                }
                continue;
            }

            size_t pos = SkipSpace(text, i + (isClass ? 5 : 6));

            // The name is the LAST identifier before '{', ':' or ';'. Reading
            // them in a loop skips an export/attribute macro sitting between
            // the keyword and the name ("class ULTRACANVAS_API Foo"), while
            // "final" terminates the run without becoming the name
            // ("class Panel final : public Widget").
            std::string name;
            for (;;) {
                pos = SkipSpace(text, pos);
                if (pos >= end || !IsIdentifierStart(text[pos])) break;
                size_t probe = pos;
                std::string word = ReadIdentifier(text, probe);
                if (word == "final") { pos = probe; break; }
                name = word;
                pos = probe;
            }

            size_t scan = SkipSpace(text, pos);
            if (IsWordAt(text, scan, "final")) scan = SkipSpace(text, scan + 5);

            std::string baseClause;
            if (scan < end && text[scan] == ':') {
                size_t brace = text.find('{', scan);
                if (brace == std::string::npos) { i = scan + 1; continue; }
                baseClause = text.substr(scan + 1, brace - scan - 1);
                scan = brace;
            }

            if (scan >= end || text[scan] != '{') {
                // Forward declaration or a variable of an elaborated type.
                size_t semi = text.find(';', pos);
                i = (semi == std::string::npos) ? end : semi + 1;
                continue;
            }

            size_t close = FindMatchingBrace(text, scan);
            if (close == std::string::npos || close > end) return;

            if (name.empty() && !context.options->includeAnonymousTypes) {
                i = close + 1;
                continue;
            }
            if (NameExcluded(name, *context.options)) {
                i = close + 1;
                continue;
            }

            UMLClassifier classifier;
            classifier.name = name;
            classifier.id = context.model->MakeUniqueClassifierId(name);
            classifier.kind = UMLClassifierKind::Class;
            classifier.packageId = enclosingClassifierId;
            if (context.options->recordSourceFile) classifier.sourceFile = context.sourceName;
            if (context.options->qualifyNamesWithNamespace && !context.namespaceStack.empty()) {
                std::string qualified;
                for (const auto& ns : context.namespaceStack) {
                    if (!ns.empty()) qualified += ns + "::";
                }
                classifier.name = qualified + classifier.name;
            }

            ParseClassBody(text, scan, close, name, isStruct, classifier, context, pendingEdges);

            // Abstract / interface classification.
            bool anyOperation = !classifier.operations.empty();
            bool allPure = anyOperation;
            bool anyPure = false;
            for (const auto& op : classifier.operations) {
                if (op.isAbstract) anyPure = true;
                else allPure = false;
            }
            if (context.options->detectInterfaces && allPure && anyPure &&
                classifier.attributes.empty()) {
                classifier.kind = UMLClassifierKind::Interface;
                classifier.stereotypes.push_back("interface");
            } else if (context.options->detectAbstractClasses && anyPure) {
                classifier.kind = UMLClassifierKind::AbstractClass;
            }

            // Compartment caps.
            if (context.options->maxMembersPerCompartment > 0) {
                size_t cap = context.options->maxMembersPerCompartment;
                if (classifier.attributes.size() > cap) {
                    context.result->warnings.push_back(
                        classifier.name + ": " + std::to_string(classifier.attributes.size() - cap) +
                        " attribute(s) omitted (maxMembersPerCompartment)");
                    classifier.attributes.resize(cap);
                }
                if (classifier.operations.size() > cap) {
                    context.result->warnings.push_back(
                        classifier.name + ": " + std::to_string(classifier.operations.size() - cap) +
                        " operation(s) omitted (maxMembersPerCompartment)");
                    classifier.operations.resize(cap);
                }
            }

            std::string classifierId = context.model->AddClassifier(classifier);
            context.result->classifiersAdded++;

            // Base classes -> generalization (upgraded to realization later if
            // the base turns out to be an interface).
            if (context.options->inferGeneralization && !Trim(baseClause).empty()) {
                for (const auto& base : ParseBaseClause(baseClause)) {
                    PendingEdgeOut edge;
                    edge.kind = UMLRelationshipKind::Generalization;
                    edge.sourceClassifierId = classifierId;
                    edge.targetTypeName = base;
                    pendingEdges.push_back(edge);
                }
            }

            // Attribute types -> association / aggregation / composition.
            if (context.options->inferAssociations) {
                const UMLClassifier* stored = context.model->GetClassifier(classifierId);
                if (stored) {
                    for (const auto& attribute : stored->attributes) {
                        std::string core, multiplicity;
                        UltraCanvasCppReverseEngineer::TypeOwnership ownership =
                            UltraCanvasCppReverseEngineer::TypeOwnership::Value;
                        if (!UltraCanvasCppReverseEngineer::ExtractReferencedType(
                                attribute.type, core, multiplicity, ownership)) {
                            continue;
                        }
                        if (core == classifier.name) continue;  // self-reference: skip the edge

                        UMLRelationshipKind kind = UMLRelationshipKind::DirectedAssociation;
                        if (context.options->inferOwnership) {
                            switch (ownership) {
                                case UltraCanvasCppReverseEngineer::TypeOwnership::Value:
                                case UltraCanvasCppReverseEngineer::TypeOwnership::UniquePointer:
                                    kind = UMLRelationshipKind::Composition;
                                    break;
                                case UltraCanvasCppReverseEngineer::TypeOwnership::SharedPointer:
                                    kind = UMLRelationshipKind::Aggregation;
                                    break;
                                default:
                                    kind = UMLRelationshipKind::DirectedAssociation;
                                    break;
                            }
                        }
                        // The attribute's own multiplicity (an array bound)
                        // wins over the one implied by the container type.
                        std::string effective = attribute.multiplicity.empty()
                            ? multiplicity : attribute.multiplicity;

                        PendingEdgeOut edge;
                        edge.kind = kind;
                        edge.sourceClassifierId = classifierId;
                        edge.targetTypeName = core;
                        edge.roleName = attribute.name;
                        edge.multiplicity = effective;
                        pendingEdges.push_back(edge);
                    }
                }
            }

            i = close + 1;
            continue;
        }

        // Nothing interesting starts here: skip the whole identifier so a name
        // like "classifier" is not mistaken for the "class" keyword.
        size_t pos = i;
        ReadIdentifier(text, pos);
        i = pos;
    }
}

} // namespace

// =============================================================================
// PUBLIC ENTRY POINTS
// =============================================================================

CppReverseEngineerResult UltraCanvasCppReverseEngineer::ParseSource(
        const std::string& sourceText, UltraCanvasUMLModel& model,
        const std::string& sourceName) {
    CppReverseEngineerResult result;
    result.filesParsed = 1;

    std::string cleaned = StripCommentsAndDirectives(sourceText);

    ScanContext context;
    context.options = &options;
    context.model = &model;
    context.result = &result;
    context.sourceName = sourceName;

    std::vector<PendingEdgeOut> edges;
    ScanScope(cleaned, 0, cleaned.size(), context, edges, std::string());

    for (const auto& e : edges) {
        PendingEdge pending;
        pending.kind = e.kind;
        pending.sourceClassifierId = e.sourceClassifierId;
        pending.targetTypeName = e.targetTypeName;
        pending.roleName = e.roleName;
        pending.multiplicity = e.multiplicity;
        pendingEdges.push_back(pending);
    }

    return result;
}

CppReverseEngineerResult UltraCanvasCppReverseEngineer::ParseFile(
        const std::string& filePath, UltraCanvasUMLModel& model) {
    CppReverseEngineerResult result;

    std::ifstream stream(filePath, std::ios::binary);
    if (!stream) {
        result.unreadableFiles.push_back(filePath);
        return result;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();

    return ParseSource(buffer.str(), model, filePath);
}

CppReverseEngineerResult UltraCanvasCppReverseEngineer::ParseFiles(
        const std::vector<std::string>& filePaths, UltraCanvasUMLModel& model) {
    CppReverseEngineerResult total;
    for (const auto& path : filePaths) {
        total.Merge(ParseFile(path, model));
    }
    return total;
}

CppReverseEngineerResult UltraCanvasCppReverseEngineer::ParseDirectory(
        const std::string& directoryPath, bool recursive, UltraCanvasUMLModel& model) {
    CppReverseEngineerResult total;
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(directoryPath, ec) || !fs::is_directory(directoryPath, ec)) {
        total.unreadableFiles.push_back(directoryPath);
        return total;
    }

    std::vector<std::string> files;
    auto consider = [&](const fs::directory_entry& entry) {
        if (!entry.is_regular_file()) return;
        std::string extension = entry.path().extension().string();
        for (const auto& allowed : options.fileExtensions) {
            if (extension == allowed) {
                files.push_back(entry.path().string());
                return;
            }
        }
    };

    if (recursive) {
        for (fs::recursive_directory_iterator it(directoryPath, ec), last; it != last; it.increment(ec)) {
            if (ec) break;
            consider(*it);
        }
    } else {
        for (fs::directory_iterator it(directoryPath, ec), last; it != last; it.increment(ec)) {
            if (ec) break;
            consider(*it);
        }
    }

    // Deterministic order so repeated runs produce identical models.
    std::sort(files.begin(), files.end());
    total.Merge(ParseFiles(files, model));
    return total;
}

size_t UltraCanvasCppReverseEngineer::ResolveInferredRelationships(UltraCanvasUMLModel& model) {
    size_t dropped = 0;

    for (const auto& edge : pendingEdges) {
        const UMLClassifier* source = model.GetClassifier(edge.sourceClassifierId);
        if (!source) { ++dropped; continue; }

        const UMLClassifier* target = model.FindClassifierByName(edge.targetTypeName);
        if (!target) {
            // Try the qualified spelling too, in case names were qualified.
            for (const auto& candidate : model.Classifiers()) {
                std::string unqualified = UnqualifyName(candidate.name);
                if (unqualified == edge.targetTypeName) { target = &candidate; break; }
            }
        }
        if (!target) { ++dropped; continue; }   // std::string, int, external type
        if (target->id == source->id) { ++dropped; continue; }
        if (!options.inferEdgesToEnumerations &&
            target->kind == UMLClassifierKind::Enumeration &&
            edge.kind != UMLRelationshipKind::Generalization &&
            edge.kind != UMLRelationshipKind::Realization) {
            ++dropped;
            continue;
        }

        UMLRelationshipKind kind = edge.kind;
        // A base that turned out to be an interface is a realization, not a
        // generalization — that is the UML distinction, and it changes the
        // line style from solid to dashed.
        if (kind == UMLRelationshipKind::Generalization &&
            target->kind == UMLClassifierKind::Interface) {
            kind = UMLRelationshipKind::Realization;
        }

        if (model.HasRelationship(kind, source->id, target->id)) { ++dropped; continue; }

        auto handle = model.AddRelationship(kind, source->id, target->id);
        if (!edge.roleName.empty() || !edge.multiplicity.empty()) {
            handle.SetRoles(std::string(), edge.roleName);
            handle.SetMultiplicity(std::string(), edge.multiplicity);
        }
        if (kind == UMLRelationshipKind::DirectedAssociation ||
            kind == UMLRelationshipKind::Aggregation ||
            kind == UMLRelationshipKind::Composition) {
            handle.SetNavigable(false, true);
        }
    }

    pendingEdges.clear();
    return dropped;
}

} // namespace UltraCanvas
