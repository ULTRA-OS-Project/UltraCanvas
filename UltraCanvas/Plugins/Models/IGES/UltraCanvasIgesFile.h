// Plugins/Models/IGES/UltraCanvasIgesFile.h
// IGES 5.3 (ANSI/US PRO IPO-100) fixed-format file syntax: the five sections
// of 80-column card images, read into a table of entities and written back
// out of one. The syntax layer only.
//
// An IGES file is a deck of 80-column lines, each tagged in column 73 with the
// section it belongs to and numbered in columns 74-80 within that section:
//
//   S  Start       free text for a person to read
//   G  Global      delimiters, sender, units, precision, dates
//   D  Directory   two lines of nine 8-column fields per entity: its type,
//                  where its parameters are, its form, colour, transform...
//   P  Parameter   the entity's numbers, free-format in columns 1-64, with
//                  the owning directory line in columns 66-72
//   T  Terminate   how many lines each section had
//
// An entity is named by the sequence number of its first directory line —
// always odd — and every pointer in the file is one of those numbers. That
// is what EntityTable is keyed on.
//
// Parameters are free-format: numbers (a real may carry a D exponent, the
// Fortran heritage of the format), Hollerith strings (`5HSteel` is five
// characters after the H) and empty fields, which mean "the default". This
// header splits and types them; what each entity's parameters mean lives in
// UltraCanvasIgesConverter.cpp, exactly the split the STEP reader makes.
//
// The compressed ASCII form (a 'C' in the start section) and the binary form
// are not read: neither has been written by a CAD system this century.
//
// Version: 1.0.0
// Last Modified: 2026-11-12
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_IGES_FILE_H
#define ULTRACANVAS_IGES_FILE_H

#include <cstdint>
#include <functional>
#include <istream>
#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace IgesFile {

// One free-format parameter.
struct Parameter {
    enum class Kind { Empty, Number, String };
    Kind Type = Kind::Empty;
    double Number = 0.0;
    std::string Text;

    bool IsEmpty() const { return Type == Kind::Empty; }
    // Readers that answer with a default rather than failing: one bad
    // parameter should cost one entity, not the file.
    double AsReal(double fallback = 0.0) const {
        return Type == Kind::Number ? Number : fallback;
    }
    int AsInteger(int fallback = 0) const;
    // A directory pointer. IGES writes a negated pointer in a few places
    // (a colour number); the magnitude is the entity either way.
    int AsPointer() const;
    std::string AsText() const { return Type == Kind::String ? Text : std::string(); }
};

// One entity: its directory entry and its parameters.
struct Entity {
    int Directory = 0;          // sequence number of its first D line (odd)
    int Type = 0;
    int Form = 0;
    int Structure = 0;
    int LineFont = 0;
    int Level = 0;
    int View = 0;
    int Transform = 0;          // pointer to a 124 entity, 0 = none
    int LabelDisplay = 0;
    std::string StatusNumber = "00000000";
    int LineWeight = 0;
    int Color = 0;              // > 0 a standard colour, < 0 a pointer to a 314
    int ParameterStart = 0;
    int ParameterLineCount = 0;
    std::string Label;          // up to eight characters
    int Subscript = 0;
    // The parameters after the leading entity type number, so Params[0] is
    // what the specification calls parameter 1.
    std::vector<Parameter> Params;

    const Parameter& Param(size_t index) const;
    double Real(size_t index, double fallback = 0.0) const { return Param(index).AsReal(fallback); }
    int Integer(size_t index, int fallback = 0) const { return Param(index).AsInteger(fallback); }
    int Pointer(size_t index) const { return Param(index).AsPointer(); }
    std::string Text(size_t index) const { return Param(index).AsText(); }

    // The status number's four two-digit fields.
    int BlankStatus() const { return StatusField(0); }
    int SubordinateSwitch() const { return StatusField(1); }   // 0 = independent
    int EntityUseFlag() const { return StatusField(2); }       // 5 = 2D parametric
    int Hierarchy() const { return StatusField(3); }
    int StatusField(int index) const;
};

class Model {
public:
    std::string Start;                       // the start section's text
    std::vector<Parameter> Global;           // Global[0] is global parameter 1
    char ParameterDelimiter = ',';
    char RecordDelimiter = ';';
    std::map<int, Entity> Entities;          // keyed by directory pointer

    using WarningSink = std::function<void(const std::string&)>;
    // False, with the reason sent to `warn`, when the text is not a readable
    // IGES file. Individual malformed entities are skipped with a warning.
    bool Parse(std::istream& stream, const WarningSink& warn = nullptr);
    bool ParseText(const std::string& text, const WarningSink& warn = nullptr);

    const Entity* At(int directory) const;
    // Global parameter n, counted from 1 as the specification counts them.
    std::string GlobalText(int number) const;
    double GlobalReal(int number, double fallback = 0.0) const;
    int GlobalInteger(int number, int fallback = 0) const;
};

// True when the head of a file looks like fixed-format IGES: an 'S' line
// numbered 1 in columns 73-80.
bool LooksLikeIgesFile(const std::vector<uint8_t>& data);

// ===== WRITING =====

// Number formatting in the format's own spelling. Every number goes through
// these, never through a stream: they are dot-decimal whatever the locale.
std::string FormatInteger(long long value);
// A real always carries its decimal point ("5." rather than "5"), because a
// reader is entitled to treat a field without one as an integer.
std::string FormatReal(double value, int significantDigits);
// `nH` followed by the text.
std::string FormatString(const std::string& text);

// Collects entities and lays them out into the five sections.
class FileWriter {
public:
    struct EntityToWrite {
        int Type = 0;
        int Form = 0;
        int Transform = 0;
        int Color = 0;
        std::string StatusNumber = "00000000";
        std::string Label;
        int Subscript = 0;
        // Already formatted, without the leading entity type number.
        std::vector<std::string> Params;
    };

    // Returns the entity's directory pointer, which is what other entities
    // reference it by. Pointers are known as soon as an entity is added, so
    // a writer adds the leaves first and the entities that refer to them
    // after.
    int Add(EntityToWrite spec);
    EntityToWrite& Get(int directory);
    size_t Count() const { return entities_.size(); }

    // `global` is the formatted global parameters 3 onwards; the delimiters
    // (parameters 1 and 2) are written by the writer itself.
    void Write(std::ostream& out, const std::string& start,
               const std::vector<std::string>& global) const;

private:
    std::vector<EntityToWrite> entities_;
};

} // namespace IgesFile
} // namespace UltraCanvas

#endif // ULTRACANVAS_IGES_FILE_H
