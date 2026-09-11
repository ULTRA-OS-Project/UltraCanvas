// Plugins/Models/XFile/UltraCanvasXFile.h
// The DirectX .x container: header, tokenisers and the object tree.
// It knows nothing about meshes.
//
// Split from the converter for the same reason the STEP reader is split from
// its Part 21 layer and the Alembic reader from Ogawa: an .x file is a generic
// tree of typed objects, and deciding what "Mesh" or "Frame" *means* is a
// separate job from reading the syntax. This layer reads any .x file, including
// ones full of object types this framework has never heard of.
//
// The format has four encodings, announced by a sixteen-byte header:
//
//     xof 0303txt 0032      text, 32-bit floats
//     xof 0303bin 0064      binary token stream, 64-bit floats
//     xof 0303tzip 0032     MSZIP-compressed text
//     xof 0303bzip 0032     MSZIP-compressed binary
//
// Text and binary are both read here. The two compressed encodings are
// recognised and refused with a message naming them, because MSZIP is not
// plain zlib - it is a sequence of 32 KB blocks each with its own raw-deflate
// stream and a two-byte "CK" signature - and a reader that silently produced
// nothing would be indistinguishable from a corrupt file.
//
// An object's data is read positionally. That is not a shortcut: the format
// puts no field names in the data, only in the `template` declarations at the
// top of the file, and those are schema that every real reader ignores in
// favour of the known layouts. So every numeric datum in an object flattens
// into one array in file order, and the converter reads it with a cursor that
// cannot run off the end.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_XFILE_H
#define ULTRACANVAS_XFILE_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace XFile {

enum class Encoding { Text, Binary, CompressedText, CompressedBinary };

// One data object: "Type Name { ... }". Children are the objects nested
// inside it; Numbers and Strings are its own data, flattened in file order.
struct Node {
    std::string Type;
    std::string Name;              // the optional name after the type
    std::string Reference;         // set instead of Type for "{ OtherName }"
    std::vector<double> Numbers;
    std::vector<std::string> Strings;
    std::vector<Node> Children;

    bool IsReference() const { return !Reference.empty(); }
    const Node* FindChild(const std::string& type) const;
    std::vector<const Node*> ChildrenOfType(const std::string& type) const;
};

struct File {
    int MajorVersion = 0;
    int MinorVersion = 0;
    Encoding How = Encoding::Text;
    int FloatBits = 32;
    std::vector<Node> Roots;

    // Named objects, for resolving a "{ Name }" reference. Populated after
    // the whole file is read, so a reference may point forward.
    std::map<std::string, const Node*> Named;

    const Node* Resolve(const Node& node) const;
};

// Reads Numbers positionally without ever running past the end. Once it has
// been asked for more than the object holds, Ok() is false and every further
// read returns zero - so a truncated object produces a warning and an empty
// mesh rather than a crash.
class Cursor {
public:
    explicit Cursor(const std::vector<double>& values) : values_(values) {}

    bool Ok() const { return ok_; }
    size_t Remaining() const { return position_ < values_.size() ? values_.size() - position_ : 0; }

    double Next() {
        if (position_ >= values_.size()) { ok_ = false; return 0.0; }
        return values_[position_++];
    }
    int NextInt() {
        const double value = Next();
        if (value < -2147483648.0 || value > 2147483647.0) { ok_ = false; return 0; }
        return static_cast<int>(value);
    }
    // A count that must fit what is actually left. Returns 0 and clears Ok()
    // when the object claims more items than it carries, which is the one
    // malformation that would otherwise allocate wildly.
    int NextCount(int perItem) {
        const int count = NextInt();
        if (!ok_ || count < 0) { ok_ = false; return 0; }
        if (perItem > 0 && static_cast<size_t>(count) * static_cast<size_t>(perItem) > Remaining()) {
            ok_ = false;
            return 0;
        }
        return count;
    }

private:
    const std::vector<double>& values_;
    size_t position_ = 0;
    bool ok_ = true;
};

// The sixteen-byte magic, without parsing the body.
bool LooksLikeXFile(const std::string& head);
// True for the two MSZIP encodings, which Parse refuses.
bool IsCompressedEncoding(Encoding encoding);
const char* EncodingName(Encoding encoding);

// Reads a whole file. Returns false and fills `error` when the header is not
// an .x header, when the encoding is compressed, or when the body is
// malformed beyond recovery. Recoverable oddities go to `warn` instead.
bool Parse(const std::vector<uint8_t>& data, File& out, std::string& error,
           const std::function<void(const std::string&)>& warn);

} // namespace XFile
} // namespace UltraCanvas

#endif // ULTRACANVAS_XFILE_H
