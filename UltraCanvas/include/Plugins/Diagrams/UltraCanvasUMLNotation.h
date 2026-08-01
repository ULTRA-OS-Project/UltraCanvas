// include/Plugins/Diagrams/UltraCanvasUMLNotation.h
// UML member-notation grammar: parse and format class-box member rows
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework
//
// The short form of a member row is what makes demo code readable:
//
//     diagram.AddClass("Customer", { "+Id: int", "-notes [0..*]: string" },
//                                  { "+Deposit(amount: double): bool" });
//
// The structured form (UMLMember) is what makes export, validation and
// reverse engineering possible. This header is the bridge: it parses the
// text form into UMLMember on entry and formats it back on the way out, so
// neither representation is authoritative and nothing is lost.
//
// Grammar accepted (all parts optional except the name):
//
//   attribute:  [+-#~] [/]name [ '[' multiplicity ']' ] [: Type] [ '[' multiplicity ']' ]
//                            [= default] [{property}]
//   operation:  [+-#~] name '(' [param [, param]*] ')' [: ReturnType] [{property}]
//   parameter:  [in|out|inout] name : Type [= default]     — or just  Type
//   literal:    NAME [= value]
//
// Recognised property strings: {static} and {abstract} set the corresponding
// flags; anything else is preserved verbatim in UMLMember::propertyString.
// A missing visibility glyph yields UMLVisibility::Unspecified — UML draws no
// glyph in that case, and inventing "public" would be a lie about the source.

#pragma once

#include "Plugins/Diagrams/UltraCanvasUMLModel.h"

#include <string>

namespace UltraCanvas {

namespace UMLNotation {

// Parses one member row. Returns false only when `text` has no name at all
// (empty or punctuation-only), in which case `out` is left untouched.
//
// The member kind is inferred: a '(' outside brackets means Operation,
// otherwise Attribute. Callers that know better may override out.kind.
bool ParseMember(const std::string& text, UMLMember& out);

// Convenience wrappers that force the compartment.
bool ParseAttribute(const std::string& text, UMLMember& out);
bool ParseOperation(const std::string& text, UMLMember& out);

// Parses "NAME = value" / "NAME" into an enumeration literal.
UMLMember ParseLiteral(const std::string& text);

// Formats a member back into the text form. Round-trips: parsing the result
// of FormatMember yields an equal UMLMember.
std::string FormatMember(const UMLMember& member);

// Formats just the parameter list body (no parentheses).
std::string FormatParameters(const std::vector<UMLParameter>& parameters);

// Formats the full signature without the visibility glyph — useful for
// tooltips and for search.
std::string FormatSignature(const UMLMember& member);

// Visibility helpers.
UMLVisibility VisibilityFromGlyph(char glyph);
char VisibilityToGlyph(UMLVisibility visibility);

// Trims whitespace and drops a decorative leading '+' from the informal
// multiplicity spellings used by many diagram tools ("+1..*" -> "1..*").
std::string NormalizeMultiplicity(const std::string& text);

// True if `text` is a well-formed multiplicity: "1", "0..1", "*", "0..*",
// "1..*", "n..m", or a comma-separated list of those.
bool IsValidMultiplicity(const std::string& text);

// Utility shared with the reverse engineer.
std::string Trim(const std::string& text);

} // namespace UMLNotation

} // namespace UltraCanvas
