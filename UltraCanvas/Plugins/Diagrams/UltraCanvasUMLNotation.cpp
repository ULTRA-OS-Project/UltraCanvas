// Plugins/Diagrams/UltraCanvasUMLNotation.cpp
// UML member-notation grammar implementation
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasUMLNotation.h"

#include <algorithm>
#include <cctype>

namespace UltraCanvas {
namespace UMLNotation {

namespace {

bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Depth counters so a ':' inside "std::map<K, V>" or "[0..*]" is not mistaken
// for the type separator. '<' is only counted when it plausibly opens a
// template argument list, never as less-than in a default value.
struct DepthTracker {
    int paren = 0, bracket = 0, angle = 0;

    void Feed(char c) {
        switch (c) {
            case '(': ++paren; break;
            case ')': if (paren > 0) --paren; break;
            case '[': ++bracket; break;
            case ']': if (bracket > 0) --bracket; break;
            case '<': ++angle; break;
            case '>': if (angle > 0) --angle; break;
            default: break;
        }
    }
    bool AtTopLevel() const { return paren == 0 && bracket == 0 && angle == 0; }
};

// Index of the first occurrence of `needle` at nesting depth 0, or npos.
size_t FindTopLevel(const std::string& s, char needle, size_t from = 0) {
    DepthTracker depth;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i >= from && depth.AtTopLevel() && s[i] == needle) {
            // "::" is a scope operator, not a type separator.
            if (needle == ':') {
                if (i + 1 < s.size() && s[i + 1] == ':') { depth.Feed(s[i]); ++i; depth.Feed(s[i]); continue; }
                if (i > 0 && s[i - 1] == ':') { depth.Feed(s[i]); continue; }
            }
            return i;
        }
        depth.Feed(s[i]);
    }
    return std::string::npos;
}

// Splits on `separator` at nesting depth 0.
std::vector<std::string> SplitTopLevel(const std::string& s, char separator) {
    std::vector<std::string> parts;
    DepthTracker depth;
    std::string current;
    for (char c : s) {
        if (depth.AtTopLevel() && c == separator) {
            parts.push_back(current);
            current.clear();
            continue;
        }
        depth.Feed(c);
        current.push_back(c);
    }
    if (!current.empty() || !parts.empty()) parts.push_back(current);
    return parts;
}

// Pulls every trailing {…} group off the end of `text`, appending them to
// `properties` in source order and setting the flags UML gives meaning to.
void ExtractPropertyStrings(std::string& text, UMLMember& member) {
    std::vector<std::string> groups;
    for (;;) {
        std::string trimmed = Trim(text);
        if (trimmed.size() < 2 || trimmed.back() != '}') break;
        size_t open = trimmed.rfind('{');
        if (open == std::string::npos) break;
        groups.insert(groups.begin(), trimmed.substr(open, trimmed.size() - open));
        text = trimmed.substr(0, open);
    }
    std::string preserved;
    for (const auto& group : groups) {
        std::string body = Trim(group.substr(1, group.size() - 2));
        std::string lowered;
        for (char c : body) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        if (lowered == "static") {
            member.isStatic = true;
        } else if (lowered == "abstract") {
            member.isAbstract = true;
        } else {
            if (!preserved.empty()) preserved += " ";
            preserved += group;
        }
    }
    member.propertyString = preserved;
    text = Trim(text);
}

// Extracts a bracketed multiplicity from `text` (anywhere at top level) and
// removes it. Returns true if one was found.
bool ExtractBracketMultiplicity(std::string& text, std::string& multiplicity) {
    size_t open = std::string::npos;
    DepthTracker depth;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '[' && depth.AtTopLevel()) { open = i; break; }
        depth.Feed(text[i]);
    }
    if (open == std::string::npos) return false;
    size_t close = text.find(']', open);
    if (close == std::string::npos) return false;

    std::string candidate = Trim(text.substr(open + 1, close - open - 1));
    // "int values[8]" is a C array, not a UML multiplicity — but as a UML row
    // "[8]" *is* a legal multiplicity, so accept anything multiplicity-shaped.
    if (!candidate.empty() && !IsValidMultiplicity(candidate)) return false;

    multiplicity = candidate;
    text = Trim(text.substr(0, open) + " " + text.substr(close + 1));
    return true;
}

UMLParameter ParseParameter(const std::string& text) {
    UMLParameter param;
    std::string body = Trim(text);
    if (body.empty()) return param;

    // Optional direction keyword.
    static const char* directions[] = {"inout ", "in ", "out "};
    for (const char* dir : directions) {
        size_t len = std::string(dir).size();
        if (body.size() > len && body.compare(0, len, dir) == 0) {
            param.direction = Trim(std::string(dir));
            body = Trim(body.substr(len));
            break;
        }
    }

    // Default value.
    size_t eq = FindTopLevel(body, '=');
    if (eq != std::string::npos) {
        param.defaultValue = Trim(body.substr(eq + 1));
        body = Trim(body.substr(0, eq));
    }

    size_t colon = FindTopLevel(body, ':');
    if (colon == std::string::npos) {
        // "method(Type)" — an unnamed parameter given by type alone.
        param.type = Trim(body);
        return param;
    }
    param.name = Trim(body.substr(0, colon));
    param.type = Trim(body.substr(colon + 1));
    return param;
}

} // namespace

// =============================================================================
// PUBLIC API
// =============================================================================

std::string Trim(const std::string& text) {
    size_t begin = 0, end = text.size();
    while (begin < end && IsSpace(text[begin])) ++begin;
    while (end > begin && IsSpace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

UMLVisibility VisibilityFromGlyph(char glyph) {
    switch (glyph) {
        case '+': return UMLVisibility::Public;
        case '-': return UMLVisibility::Private;
        case '#': return UMLVisibility::Protected;
        case '~': return UMLVisibility::Package;
        default:  return UMLVisibility::Unspecified;
    }
}

char VisibilityToGlyph(UMLVisibility visibility) {
    switch (visibility) {
        case UMLVisibility::Public:      return '+';
        case UMLVisibility::Private:     return '-';
        case UMLVisibility::Protected:   return '#';
        case UMLVisibility::Package:     return '~';
        case UMLVisibility::Unspecified: return '\0';
    }
    return '\0';
}

std::string NormalizeMultiplicity(const std::string& text) {
    std::string value = Trim(text);
    if (!value.empty() && value.front() == '+') value = Trim(value.substr(1));
    return value;
}

bool IsValidMultiplicity(const std::string& text) {
    std::string value = NormalizeMultiplicity(text);
    if (value.empty()) return false;

    auto validTerm = [](const std::string& term) {
        if (term.empty()) return false;
        if (term == "*") return true;
        size_t dots = term.find("..");
        if (dots == std::string::npos) {
            for (char c : term) {
                if (!std::isdigit(static_cast<unsigned char>(c))) return false;
            }
            return true;
        }
        std::string lo = Trim(term.substr(0, dots));
        std::string hi = Trim(term.substr(dots + 2));
        if (lo.empty() || hi.empty()) return false;
        for (char c : lo) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        if (hi == "*") return true;
        for (char c : hi) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        return true;
    };

    for (const auto& term : SplitTopLevel(value, ',')) {
        if (!validTerm(Trim(term))) return false;
    }
    return true;
}

bool ParseMember(const std::string& text, UMLMember& out) {
    std::string body = Trim(text);
    if (body.empty()) return false;

    UMLMember member;

    // --- visibility -----------------------------------------------------
    if (!body.empty()) {
        UMLVisibility vis = VisibilityFromGlyph(body[0]);
        if (vis != UMLVisibility::Unspecified) {
            member.visibility = vis;
            body = Trim(body.substr(1));
        } else {
            member.visibility = UMLVisibility::Unspecified;
        }
    }

    // --- trailing {property} groups (may set static/abstract) -----------
    ExtractPropertyStrings(body, member);
    if (body.empty()) return false;

    // --- leading {property} groups (PlantUML writes {static} first) -----
    while (!body.empty() && body.front() == '{') {
        size_t close = body.find('}');
        if (close == std::string::npos) break;
        std::string leading = body.substr(0, close + 1);
        std::string rest = Trim(body.substr(close + 1));
        UMLMember probe;
        std::string probeText = leading;
        ExtractPropertyStrings(probeText, probe);
        member.isStatic = member.isStatic || probe.isStatic;
        member.isAbstract = member.isAbstract || probe.isAbstract;
        if (!probe.propertyString.empty()) {
            if (!member.propertyString.empty()) member.propertyString += " ";
            member.propertyString += probe.propertyString;
        }
        body = rest;
    }
    if (body.empty()) return false;

    // --- derived marker -------------------------------------------------
    if (body.front() == '/') {
        member.isDerived = true;
        body = Trim(body.substr(1));
    }

    // --- operation or attribute? ----------------------------------------
    size_t openParen = FindTopLevel(body, '(');
    if (openParen != std::string::npos) {
        member.kind = UMLMemberKind::Operation;

        // Matching close paren.
        int depth = 0;
        size_t closeParen = std::string::npos;
        for (size_t i = openParen; i < body.size(); ++i) {
            if (body[i] == '(') ++depth;
            else if (body[i] == ')') {
                if (--depth == 0) { closeParen = i; break; }
            }
        }
        if (closeParen == std::string::npos) closeParen = body.size();

        member.name = Trim(body.substr(0, openParen));

        std::string params = (closeParen > openParen + 1)
            ? body.substr(openParen + 1, closeParen - openParen - 1)
            : std::string();
        if (!Trim(params).empty()) {
            for (const auto& piece : SplitTopLevel(params, ',')) {
                if (Trim(piece).empty()) continue;
                member.parameters.push_back(ParseParameter(piece));
            }
        }

        std::string tail = (closeParen + 1 <= body.size())
            ? Trim(body.substr(std::min(closeParen + 1, body.size())))
            : std::string();
        if (!tail.empty()) {
            if (tail.front() == ':') tail = Trim(tail.substr(1));
            member.type = tail;
        }
        if (member.name.empty()) return false;
        out = member;
        return true;
    }

    member.kind = UMLMemberKind::Attribute;

    // Default value first: it may itself contain a ':' (rare) but never a
    // top-level one before the type separator.
    size_t colon = FindTopLevel(body, ':');
    std::string namePart = (colon == std::string::npos) ? body : body.substr(0, colon);
    std::string typePart = (colon == std::string::npos) ? std::string() : body.substr(colon + 1);

    size_t eq = FindTopLevel(typePart, '=');
    if (eq != std::string::npos) {
        member.defaultValue = Trim(typePart.substr(eq + 1));
        typePart = typePart.substr(0, eq);
    } else {
        size_t nameEq = FindTopLevel(namePart, '=');
        if (nameEq != std::string::npos) {
            member.defaultValue = Trim(namePart.substr(nameEq + 1));
            namePart = namePart.substr(0, nameEq);
        }
    }

    // Multiplicity may sit after the name ("fees [0..1]: String") or after the
    // type ("fees : String [0..1]"). Both spellings appear in the wild.
    std::string multiplicity;
    namePart = Trim(namePart);
    typePart = Trim(typePart);
    if (!ExtractBracketMultiplicity(namePart, multiplicity)) {
        ExtractBracketMultiplicity(typePart, multiplicity);
    }
    member.multiplicity = NormalizeMultiplicity(multiplicity);

    namePart = Trim(namePart);
    if (!namePart.empty() && namePart.front() == '/') {
        member.isDerived = true;
        namePart = Trim(namePart.substr(1));
    }

    member.name = namePart;
    member.type = Trim(typePart);
    if (member.name.empty()) return false;

    out = member;
    return true;
}

bool ParseAttribute(const std::string& text, UMLMember& out) {
    UMLMember member;
    if (!ParseMember(text, member)) return false;
    member.kind = UMLMemberKind::Attribute;
    out = member;
    return true;
}

bool ParseOperation(const std::string& text, UMLMember& out) {
    UMLMember member;
    if (!ParseMember(text, member)) return false;
    member.kind = UMLMemberKind::Operation;
    out = member;
    return true;
}

UMLMember ParseLiteral(const std::string& text) {
    UMLMember member;
    member.kind = UMLMemberKind::Literal;
    member.visibility = UMLVisibility::Unspecified;

    std::string body = Trim(text);
    size_t eq = FindTopLevel(body, '=');
    if (eq != std::string::npos) {
        member.name = Trim(body.substr(0, eq));
        member.defaultValue = Trim(body.substr(eq + 1));
    } else {
        member.name = body;
    }
    return member;
}

std::string FormatParameters(const std::vector<UMLParameter>& parameters) {
    std::string out;
    for (size_t i = 0; i < parameters.size(); ++i) {
        if (i) out += ", ";
        const UMLParameter& p = parameters[i];
        if (!p.direction.empty()) out += p.direction + " ";
        if (!p.name.empty()) {
            out += p.name;
            if (!p.type.empty()) out += ": " + p.type;
        } else {
            out += p.type;
        }
        if (!p.defaultValue.empty()) out += " = " + p.defaultValue;
    }
    return out;
}

std::string FormatSignature(const UMLMember& member) {
    std::string out;
    if (member.isDerived) out += "/";
    out += member.name;

    if (member.kind == UMLMemberKind::Operation) {
        out += "(" + FormatParameters(member.parameters) + ")";
        if (!member.type.empty()) out += ": " + member.type;
    } else {
        if (!member.multiplicity.empty()) out += " [" + member.multiplicity + "]";
        if (!member.type.empty()) out += ": " + member.type;
        if (!member.defaultValue.empty()) out += " = " + member.defaultValue;
    }
    return out;
}

std::string FormatMember(const UMLMember& member) {
    std::string out;
    char glyph = VisibilityToGlyph(member.visibility);
    if (glyph != '\0') out += glyph;

    if (member.isStatic) out += "{static} ";
    out += FormatSignature(member);

    if (member.isAbstract && member.kind == UMLMemberKind::Operation) {
        out += " {abstract}";
    }
    if (!member.propertyString.empty()) {
        out += " " + member.propertyString;
    }
    return out;
}

} // namespace UMLNotation
} // namespace UltraCanvas
