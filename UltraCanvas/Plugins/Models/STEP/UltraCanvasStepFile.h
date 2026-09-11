// Plugins/Models/STEP/UltraCanvasStepFile.h
// ISO 10303-21 ("STEP Part 21") physical file parsing — the syntax layer only.
//
// A STEP file is a flat table of numbered entity instances:
//
//   #14 = CARTESIAN_POINT('',(0.,0.,0.));
//   #15 = DIRECTION('',(0.,0.,1.));
//   #16 = AXIS2_PLACEMENT_3D('',#14,#15,#17);
//
// and, for a type that inherits from several supertypes at once, a *complex*
// instance, which is a parenthesised sequence of records rather than one:
//
//   #91 = ( BOUNDED_CURVE() B_SPLINE_CURVE(3,(#1,#2,#3,#4),.UNSPECIFIED.,.F.,.F.)
//           B_SPLINE_CURVE_WITH_KNOTS((4,4),(0.,1.),.UNSPECIFIED.)
//           CURVE() GEOMETRIC_REPRESENTATION_ITEM()
//           RATIONAL_B_SPLINE_CURVE((1.,1.,0.8,1.)) REPRESENTATION_ITEM('') );
//
// That second form is not an oddity to tolerate — every rational NURBS in
// every STEP file is written that way, so a parser that only handles simple
// instances cannot read curved geometry at all. Both are represented here by
// the same StepEntity, which holds a list of records; a simple instance is one
// with a single record.
//
// This header knows nothing about geometry. It parses, resolves nothing, and
// interprets nothing: the AP203/214 entity meanings live in
// UltraCanvasStepConverter.cpp. Keeping the split means the syntax can be
// tested on its own, and that a later reader for another Part 21 application
// protocol reuses it.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_STEP_FILE_H
#define ULTRACANVAS_STEP_FILE_H

#include <cstdint>
#include <functional>
#include <istream>
#include <map>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace StepFile {

enum class ValueType {
    Unset,          // $   — the attribute is not given
    Derived,        // *   — the attribute is derived by a subtype
    Integer,
    Real,
    String,         // '...', with '' as an escaped quote
    Enumeration,    // .TRUE. / .MILLI. / .PCURVE_S1.
    Reference,      // #123
    List,           // (...)
    Typed,          // KEYWORD(...) appearing as a parameter, e.g. LENGTH_MEASURE(25.4)
    Binary          // "0F3A..."
};

// One parameter. Deliberately a plain struct rather than a variant: the
// entity table for a real part is hundreds of thousands of these, and the
// access pattern is "give me the third parameter as a reference" rather than
// exhaustive matching.
struct Value {
    ValueType Type = ValueType::Unset;
    double Number = 0.0;          // Integer and Real both fill this
    long long Whole = 0;          // Integer only
    std::string Text;             // String content, Enumeration name, Typed keyword, Binary digits
    int Reference = -1;           // Reference
    std::vector<Value> Items;     // List elements, or a Typed record's parameters

    bool IsUnset() const { return Type == ValueType::Unset || Type == ValueType::Derived; }
    bool IsReference() const { return Type == ValueType::Reference; }
    bool IsList() const { return Type == ValueType::List; }
    bool IsNumber() const { return Type == ValueType::Integer || Type == ValueType::Real; }

    // Convenience readers that answer with a default rather than throwing:
    // a STEP file in the wild will have an attribute of the wrong shape, and
    // one bad attribute should cost one face, not the whole read.
    double AsNumber(double fallback = 0.0) const { return IsNumber() ? Number : fallback; }
    int AsInteger(int fallback = 0) const {
        return Type == ValueType::Integer ? static_cast<int>(Whole)
             : Type == ValueType::Real    ? static_cast<int>(Number) : fallback;
    }
    int AsReference() const { return Type == ValueType::Reference ? Reference : -1; }
    const std::string& AsText() const { return Text; }
    // .TRUE. / .T. -> true, anything else -> false.
    bool AsBoolean(bool fallback = false) const;
    // The numbers of a list, flattened one level. Empty for a non-list.
    std::vector<double> AsNumbers() const;
    // The references of a list. Empty for a non-list.
    std::vector<int> AsReferences() const;
};

// One record inside an instance: a keyword and its parameters.
struct Record {
    std::string Keyword;
    std::vector<Value> Params;
};

// One numbered instance. A simple instance has exactly one record; a complex
// instance has several, and each carries only its own attributes — the
// inherited ones live in the sibling records, which is why the converter asks
// for a record by name rather than indexing a flat parameter list.
struct Entity {
    int Id = -1;
    std::vector<Record> Records;

    // The keyword of a simple instance, or "" for a complex one.
    const std::string& Keyword() const;
    bool IsComplex() const { return Records.size() > 1; }
    // Does this instance carry a record of that type?
    bool Is(const std::string& keyword) const;
    // That record's parameters, or null.
    const std::vector<Value>* Params(const std::string& keyword) const;
    // The parameters of a simple instance, whatever its keyword.
    const std::vector<Value>& SimpleParams() const;
    // Parameter i of the record `keyword`, or an Unset value.
    const Value& Param(const std::string& keyword, size_t index) const;
    // Parameter i of a simple instance.
    const Value& Param(size_t index) const;
};

// A parsed file: the header instances and the data table.
class Model {
public:
    // Header instances are unnumbered, so they are kept in order of appearance.
    std::vector<Record> Header;
    std::map<int, Entity> Entities;

    // Parses a whole file. Returns false only when the input is not a Part 21
    // file at all; recoverable trouble inside it — an instance that does not
    // parse, a duplicate id — is reported through `warn` and skipped, because
    // one malformed instance in a 400 000-instance assembly should not lose
    // the assembly.
    bool Parse(std::istream& stream, const std::function<void(const std::string&)>& warn);
    bool Parse(const std::string& text, const std::function<void(const std::string&)>& warn);

    const Entity* Find(int id) const;
    // Every instance carrying a record of that type, in id order.
    std::vector<const Entity*> OfType(const std::string& keyword) const;
    // The first header record with that keyword, or null.
    const Record* HeaderRecord(const std::string& keyword) const;

    // The schema names from FILE_SCHEMA, upper-cased ("AP203", "AUTOMOTIVE_DESIGN").
    std::vector<std::string> Schemas() const;
    // FILE_NAME's first attribute, which exporters fill with the source path.
    std::string SourceName() const;
    std::string OriginatingSystem() const;
};

// Enough of a Part 21 file to be recognised without parsing it: the leading
// "ISO-10303-21;", allowing for whitespace and a UTF-8 byte-order mark.
bool LooksLikeStepFile(const std::vector<uint8_t>& data);

} // namespace StepFile
} // namespace UltraCanvas

#endif // ULTRACANVAS_STEP_FILE_H
