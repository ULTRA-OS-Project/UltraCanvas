// Plugins/Models/Alembic/UltraCanvasOgawaFile.h
// Alembic's Ogawa container and its object/property model — the structure
// layer only, with no idea what a mesh is.
//
// An Alembic archive is a tree of *objects*, each carrying a tree of
// *properties*, serialised into Ogawa: a flat file of groups and data blocks
// addressed by absolute offset. A group is a count followed by that many
// 64-bit child pointers, and the pointer's top bit says whether the child is
// another group or a block of bytes. Nothing else — no directory, no names.
//
// Names, types and sample counts live in *header blobs*: the last child of
// every group is a packed record listing what its other children are. Those
// blobs are the part a reader has to get exactly right, and they are why this
// file exists separately from the converter: the encoding is fiddly (a packed
// 32-bit info word, a 1-based index into an archive-wide string pool where 0
// means "none", sample counts whose width depends on two bits of that word)
// and it is worth being able to test it without a mesh in sight.
//
// What this does NOT do, on purpose: interpret AbcGeom. Whether a compound
// named ".geom" is a polygon mesh, what "P" means, which way its faces wind —
// all of that is UltraCanvasAlembicConverter.cpp's problem. This layer would
// serve a reader for any other Alembic schema unchanged.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_OGAWA_FILE_H
#define ULTRACANVAS_OGAWA_FILE_H

#include <cstdint>
#include <functional>
#include <istream>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace Ogawa {

// Alembic's plain-old-data types, in the order the format numbers them — the
// number is what appears in a property's info word, so the order is not
// negotiable.
enum class Pod : uint8_t {
    Boolean = 0, Uint8, Int8, Uint16, Int16, Uint32, Int32,
    Uint64, Int64, Float16, Float32, Float64, String, Wstring,
    Unknown = 255
};

// Bytes one value of that type occupies; 0 for the string types, which are
// length-prefixed rather than fixed.
size_t PodSize(Pod pod);
const char* PodName(Pod pod);

enum class PropertyKind : uint8_t { Compound = 0, Scalar = 1, Array = 2 };

// One entry from a compound's header blob.
struct PropertyHeader {
    PropertyKind Kind = PropertyKind::Compound;
    std::string Name;
    // Alembic's own key=value string: "geoScope=fvr;interpretation=normal;..."
    // Read it with MetadataValue rather than by hand.
    std::string Metadata;
    Pod Type = Pod::Unknown;
    int Extent = 1;              // values per element: 3 for a point, 2 for a UV
    uint32_t SampleCount = 0;    // time samples; this reader only wants the first
};

// The value of one key in an Alembic metadata string, or "" when absent.
std::string MetadataValue(const std::string& metadata, const std::string& key);

class Archive;

// A compound property: an ordered list of sub-properties, each either another
// compound or a stream of samples.
class Compound {
public:
    Compound() = default;

    bool Valid() const { return archive_ != nullptr; }
    size_t Count() const { return headers_.size(); }
    const PropertyHeader& At(size_t index) const;
    // Index of a named sub-property, or -1. Alembic names are exact and
    // case-sensitive ("P", ".faceIndices").
    int Find(const std::string& name) const;

    // The sub-compound at `index`; invalid when that property is not one.
    Compound Child(size_t index) const;

    // The raw bytes of one sample of a scalar or array property, with
    // Alembic's 16-byte sample key already stripped. Empty when the property
    // is a compound, the sample does not exist, or the file is truncated.
    std::vector<uint8_t> Sample(size_t index, size_t sample = 0) const;

    // Sample bytes reinterpreted, with length checking. A type mismatch gives
    // an empty vector rather than nonsense — an Alembic file from a DCC that
    // wrote floats where the schema says doubles is a real thing.
    std::vector<float> Floats(size_t index, size_t sample = 0) const;
    std::vector<double> Doubles(size_t index, size_t sample = 0) const;
    std::vector<int32_t> Int32s(size_t index, size_t sample = 0) const;
    std::vector<uint32_t> Uint32s(size_t index, size_t sample = 0) const;

private:
    friend class Archive;
    friend class Object;
    Compound(const Archive* archive, uint64_t groupOffset);

    const Archive* archive_ = nullptr;
    uint64_t group_ = 0;
    std::vector<PropertyHeader> headers_;
};

// An object in the archive's tree: a transform, a mesh, a face set. What it
// *is* comes from its metadata's "schema" key.
class Object {
public:
    Object() = default;

    bool Valid() const { return archive_ != nullptr; }
    const std::string& Name() const { return name_; }
    const std::string& Metadata() const { return metadata_; }
    // "AbcGeom_PolyMesh_v1", "AbcGeom_Xform_v3", "" — the schema key with any
    // ":.geom" suffix removed, which is what identifies the object's kind.
    std::string Schema() const;

    // This object's own properties.
    Compound Properties() const;

    size_t ChildCount() const { return children_.size(); }
    Object Child(size_t index) const;

private:
    friend class Archive;
    Object(const Archive* archive, uint64_t groupOffset, std::string name, std::string metadata);

    struct ChildHeader { std::string Name; std::string Metadata; };

    const Archive* archive_ = nullptr;
    uint64_t group_ = 0;
    std::string name_;
    std::string metadata_;
    std::vector<ChildHeader> children_;
};

class Archive {
public:
    // Reads the whole file into memory: Ogawa is random-access by absolute
    // offset, so streaming it would mean seeking constantly, and the archives
    // this reader is for are tens of megabytes rather than hundreds.
    bool Open(std::istream& stream, const std::function<void(const std::string&)>& warn);
    bool Open(std::vector<uint8_t> bytes, const std::function<void(const std::string&)>& warn);

    bool Valid() const { return valid_; }
    // Alembic's own version number: 10600 is Alembic 1.6.0.
    int FileVersion() const { return fileVersion_; }
    // "FramesPerTimeUnit=24;_ai_Application=Blender;..." — the writer usually
    // names itself here, which is the first thing to know when geometry looks
    // wrong.
    const std::string& Metadata() const { return metadata_; }

    Object Top() const;

private:
    friend class Object;
    friend class Compound;

    // --- raw container ---
    uint64_t GroupChildCount(uint64_t group) const;
    uint64_t ChildPointer(uint64_t group, uint64_t index) const;
    bool ChildIsData(uint64_t group, uint64_t index) const;
    uint64_t ChildGroup(uint64_t group, uint64_t index) const;
    std::vector<uint8_t> ChildData(uint64_t group, uint64_t index) const;
    // Same, but without copying — a mesh's positions are megabytes.
    bool ChildDataView(uint64_t group, uint64_t index, const uint8_t*& out, size_t& size) const;

    // --- headers ---
    std::vector<Object::ChildHeader> ReadObjectHeaders(const uint8_t* data, size_t size) const;
    std::vector<PropertyHeader> ReadPropertyHeaders(const uint8_t* data, size_t size) const;
    // The 1-based index into the archive's string pool; 0 means "no metadata",
    // 0xff (or 0xfff, packed) means the string follows inline.
    std::string MetadataAt(size_t index) const;

    std::vector<uint8_t> bytes_;
    std::vector<std::string> pool_;
    std::string metadata_;
    uint64_t root_ = 0;
    int fileVersion_ = 0;
    bool valid_ = false;
};

// The five magic bytes, so a caller can recognise an archive without opening it.
bool LooksLikeOgawaFile(const std::vector<uint8_t>& data);
// True for the other Alembic backend, which this reader does not implement —
// worth telling apart so the message can say why rather than "not Alembic".
bool LooksLikeHdf5File(const std::vector<uint8_t>& data);

} // namespace Ogawa
} // namespace UltraCanvas

#endif // ULTRACANVAS_OGAWA_FILE_H
