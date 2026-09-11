// Plugins/Models/FBX/UltraCanvasFbxFile.h
// The FBX container in both its encodings: header, node records, properties,
// and the deflate-compressed arrays. It knows nothing about meshes.
//
// Split from the converter for the same reason the STEP, Alembic and .x
// readers are split from their container layers: an FBX file is a generic
// tree of named records with typed property lists, and deciding what
// "Geometry" or "LayerElementUV" *means* is a separate job from reading the
// bytes. This layer reads any binary FBX, including one full of record types
// this framework has never heard of.
//
// The container is well specified by observation and has been stable since
// 2011. A record is:
//
//     EndOffset, NumProperties, PropertyListLen   (uint32, or uint64 from 7500)
//     NameLen (uint8), Name
//     NumProperties properties
//     nested records, ended by a null record of the same header size
//
// and a property is a one-byte type code followed by its payload: Y C I F D L
// for scalars, S and R for strings and blobs, and f d l i b for arrays. An
// array carries a length, an encoding word and a byte count; encoding 1 means
// the payload is a zlib stream, which is why this reader needs zlib at all.
//
// Every scalar widens into int64 or double here and every array into a vector
// of one of those two. That costs memory the format did not spend, and buys
// a converter that never writes a switch over five integer widths - the same
// trade the .x reader makes by flattening its numeric data.
//
// The ASCII encoding is read too, by a second tokeniser that produces the same
// node tree. It is not merely a different spelling of the binary one: the 6.x
// files that are usually ASCII have a different *object model* as well, which
// the converter normalises rather than this layer - see
// UltraCanvasFbxConverter.h. What this layer guarantees is that either
// encoding, at either generation, arrives as the same `Node` tree.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_FBX_FILE_H
#define ULTRACANVAS_FBX_FILE_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace Fbx {

// One property. `Code` is the format's own type letter, kept so a converter
// can tell a stored integer from a stored double where that matters.
struct Property {
    char Code = 0;
    int64_t Integer = 0;              // Y C I L
    double Real = 0.0;                // F D
    std::string Text;                 // S R
    std::vector<double> Reals;        // f d
    std::vector<int64_t> Ints;        // i l b

    bool IsScalar() const { return Code && !IsArray() && Code != 'S' && Code != 'R'; }
    bool IsArray() const {
        return Code == 'f' || Code == 'd' || Code == 'l' || Code == 'i' || Code == 'b';
    }
    // Whichever of Integer and Real this property actually holds.
    double AsReal() const { return (Code == 'F' || Code == 'D') ? Real : static_cast<double>(Integer); }
    int64_t AsInteger() const { return (Code == 'F' || Code == 'D') ? static_cast<int64_t>(Real) : Integer; }
    size_t ArraySize() const { return Reals.empty() ? Ints.size() : Reals.size(); }
    // Reads an array element as a double whichever of the two vectors it is in.
    double ArrayReal(size_t index) const {
        if (index < Reals.size()) return Reals[index];
        if (index < Ints.size()) return static_cast<double>(Ints[index]);
        return 0.0;
    }
    int64_t ArrayInteger(size_t index) const {
        if (index < Ints.size()) return Ints[index];
        if (index < Reals.size()) return static_cast<int64_t>(Reals[index]);
        return 0;
    }
};

struct Node {
    std::string Name;
    std::vector<Property> Properties;
    std::vector<Node> Children;

    const Node* Find(const std::string& name) const;
    std::vector<const Node*> FindAll(const std::string& name) const;

    // Property accessors that never read past the end.
    const Property* At(size_t index) const {
        return index < Properties.size() ? &Properties[index] : nullptr;
    }
    std::string TextAt(size_t index) const {
        const Property* property = At(index);
        return property ? property->Text : std::string();
    }
    int64_t IntegerAt(size_t index, int64_t fallback = 0) const {
        const Property* property = At(index);
        return property ? property->AsInteger() : fallback;
    }
    double RealAt(size_t index, double fallback = 0.0) const {
        const Property* property = At(index);
        return property ? property->AsReal() : fallback;
    }
};

enum class Encoding { Binary, Ascii };

struct File {
    uint32_t Version = 0;
    Encoding How = Encoding::Binary;
    std::vector<Node> Roots;
    const Node* Find(const std::string& name) const;
    // The 6.x object model, which is not the 7.x one. See the converter.
    bool IsLegacy() const { return Version > 0 && Version < 7000; }
};

// ===== Properties70 =====
//
// FBX keeps an object's real parameters in a `Properties70` child, one `P`
// record each: name, type, subtype, flags, then the values. These reach into
// that rather than making every caller walk it.

// Where an object keeps its parameters, and where that record's values start:
// `Properties70` holds `P` records of (name, type, subtype, flags, values...)
// and `Properties60` holds `Property` records of (name, type, flags, values...),
// one field shorter. Resolving both here is what lets every accessor below -
// and every caller - stay unaware of which generation it is reading.
struct PropertyRecord {
    const Node* Entry = nullptr;
    size_t ValueIndex = 0;
    explicit operator bool() const { return Entry != nullptr; }
};

PropertyRecord FindPropertyRecord(const Node& owner, const std::string& name);

// ===== A RECORD'S NUMERIC PAYLOAD =====
//
// The one place the two encodings really differ in shape. Binary writes a
// record's numbers as a single array property; ASCII writes one property per
// number, because it has no array type at all. `Vertices` is one property of
// 5760 doubles in a binary file and 5760 properties of one double each in a
// text one. These read either without the caller knowing which it has.

size_t ValueCount(const Node& node);
double ValueAt(const Node& node, size_t index);
int64_t IntegerValueAt(const Node& node, size_t index);
// The record's node alone, for callers that only need to know it is there.
const Node* FindProperty(const Node& owner, const std::string& name);
double PropertyReal(const Node& owner, const std::string& name, double fallback);
bool PropertyVec3(const Node& owner, const std::string& name, double out[3]);
std::string PropertyText(const Node& owner, const std::string& name);
bool HasProperty(const Node& owner, const std::string& name);

// ===== FILE =====

bool LooksLikeFbxBinary(const std::string& head);
// The ASCII encoding, which this reader refuses rather than half-reads.
bool LooksLikeFbxAscii(const std::string& head);

// Reads a whole binary FBX. Returns false and fills `error` when the header is
// not FBX, when it is the ASCII encoding, or when the body is malformed beyond
// recovery. Recoverable oddities go to `warn`.
bool Parse(const std::vector<uint8_t>& data, File& out, std::string& error,
           const std::function<void(const std::string&)>& warn);

} // namespace Fbx
} // namespace UltraCanvas

#endif // ULTRACANVAS_FBX_FILE_H
