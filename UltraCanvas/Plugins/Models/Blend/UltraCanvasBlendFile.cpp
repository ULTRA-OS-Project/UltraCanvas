// Plugins/Models/Blend/UltraCanvasBlendFile.cpp
// The .blend container.
//
// Four parts of the format are touched, and all four have been stable across
// many Blender releases:
//
//   1. The 12-byte header: "BLENDER", a pointer-size character, an endianness
//      character and a three-character version.
//   2. The block index: every block carries a 4-character code, its length,
//      its original memory address, the SDNA struct index describing it, and
//      how many of that struct it holds. Walking it needs nothing else.
//   3. The SDNA block itself, which names every struct and field in the file.
//   4. Pointers, which are resolved against the addresses in (2). That is the
//      whole trick: a .blend is a heap dump, and the block index is its
//      relocation table.
//
// Struct *semantics* still belong upstairs - which field of Mesh holds the
// vertices is Blender's object model, not the container's. What this layer
// guarantees is that a field can be found by name at whatever offset the SDNA
// in *this* file gives it, so a build that inserted a field in the middle of a
// struct reads correctly rather than plausibly.
//
// Version: 2.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework

#include "Models/Blend/UltraCanvasBlendFile.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <zlib.h>

namespace UltraCanvas {

namespace {

constexpr size_t kHeaderSize = 12;
const uint8_t kGzipMagic[2] = {0x1f, 0x8b};
const uint8_t kZstdMagic[4] = {0x28, 0xb5, 0x2f, 0xfd};

bool StartsWith(const std::vector<uint8_t>& data, const uint8_t* magic, size_t length) {
    return data.size() >= length && std::memcmp(data.data(), magic, length) == 0;
}

// Byte swaps, written out rather than taken from a compiler builtin. Every
// compiler this framework builds with turns these into one instruction, and
// unlike __builtin_bswap they are also there for MSVC - a .blend reader is not
// the place to acquire a toolchain dependency for three lines of shifting.
uint16_t Swap16(uint16_t value) {
    return static_cast<uint16_t>((value >> 8) | (value << 8));
}
uint32_t Swap32(uint32_t value) {
    return ((value & 0x000000ffu) << 24) | ((value & 0x0000ff00u) << 8) |
           ((value & 0x00ff0000u) >> 8) | ((value & 0xff000000u) >> 24);
}
uint64_t Swap64(uint64_t value) {
    return (static_cast<uint64_t>(Swap32(static_cast<uint32_t>(value))) << 32) |
           Swap32(static_cast<uint32_t>(value >> 32));
}

// A gzip stream, inflated. Blender writes these when "Compress" is on, which
// was the default through 2.x; zlib's own gzip mode does the work.
bool Inflate(const std::vector<uint8_t>& input, std::vector<uint8_t>& output,
             std::string& error) {
    z_stream stream{};
    // 16 + MAX_WBITS selects gzip framing rather than raw deflate.
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
        error = "zlib refused to start";
        return false;
    }
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());

    // A .blend expands several-fold; growing in large steps keeps the number
    // of reallocations small for an 18 MB file.
    std::vector<uint8_t> buffer(4u << 20);
    int status = Z_OK;
    do {
        stream.next_out = buffer.data();
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
            error = stream.msg ? stream.msg : "corrupt gzip stream";
            inflateEnd(&stream);
            return false;
        }
        output.insert(output.end(), buffer.data(),
                      buffer.data() + (buffer.size() - stream.avail_out));
        if (status == Z_BUF_ERROR && stream.avail_in == 0) break;
    } while (status != Z_STREAM_END);

    inflateEnd(&stream);
    if (output.empty()) {
        error = "the gzip stream held nothing";
        return false;
    }
    return true;
}

// ===== SDNA =====

// A cursor that refuses to read past the end, so a truncated or hostile SDNA
// block ends the parse instead of walking off the buffer.
class Cursor {
public:
    Cursor(const uint8_t* data, size_t size, bool bigEndian)
        : data_(data), size_(size), bigEndian_(bigEndian) {}

    bool Has(size_t bytes) const { return position_ + bytes <= size_; }
    void Align4() { position_ = (position_ + 3u) & ~size_t(3); }
    bool Tag(const char* expected) {
        if (!Has(4) || std::memcmp(data_ + position_, expected, 4) != 0) return false;
        position_ += 4;
        return true;
    }
    bool U32(uint32_t& out) {
        if (!Has(4)) return false;
        std::memcpy(&out, data_ + position_, 4);
        if (bigEndian_) out = Swap32(out);
        position_ += 4;
        return true;
    }
    bool U16(uint16_t& out) {
        if (!Has(2)) return false;
        std::memcpy(&out, data_ + position_, 2);
        if (bigEndian_) out = Swap16(out);
        position_ += 2;
        return true;
    }
    bool String(std::string& out) {
        size_t end = position_;
        while (end < size_ && data_[end] != '\0') ++end;
        if (end >= size_) return false;
        out.assign(reinterpret_cast<const char*>(data_ + position_), end - position_);
        position_ = end + 1;
        return true;
    }

private:
    const uint8_t* data_;
    size_t size_;
    bool bigEndian_;
    size_t position_ = 0;
};

// The name as declared, taken apart: "*mvert" is a pointer named mvert,
// "obmat[4][4]" is sixteen floats named obmat, "(*func)()" is a function
// pointer that nothing here will ever read but still occupies its space.
void ParseFieldName(const std::string& declared, std::string& bare, size_t& elements,
                    bool& isPointer) {
    isPointer = !declared.empty() && (declared[0] == '*' || declared.rfind("(*", 0) == 0);
    elements = 1;

    size_t start = 0;
    while (start < declared.size() && (declared[start] == '*' || declared[start] == '(')) ++start;
    size_t end = start;
    while (end < declared.size() && declared[end] != '[' && declared[end] != ')') ++end;
    bare = declared.substr(start, end - start);

    for (size_t i = 0; i + 1 < declared.size(); ++i) {
        if (declared[i] != '[') continue;
        const size_t close = declared.find(']', i);
        if (close == std::string::npos) break;
        const int count = std::atoi(declared.c_str() + i + 1);
        elements *= count > 0 ? static_cast<size_t>(count) : 0;
        i = close;
    }
}

bool ParseSdna(const uint8_t* data, size_t size, bool bigEndian, int pointerSize,
               std::vector<Blend::Struct>& out) {
    Cursor cursor(data, size, bigEndian);
    if (!cursor.Tag("SDNA") || !cursor.Tag("NAME")) return false;

    uint32_t count = 0;
    if (!cursor.U32(count)) return false;
    if (count > (1u << 20)) return false;
    std::vector<std::string> names(count);
    for (uint32_t i = 0; i < count; ++i)
        if (!cursor.String(names[i])) return false;

    cursor.Align4();
    if (!cursor.Tag("TYPE") || !cursor.U32(count)) return false;
    if (count > (1u << 20)) return false;
    std::vector<std::string> types(count);
    for (uint32_t i = 0; i < count; ++i)
        if (!cursor.String(types[i])) return false;

    cursor.Align4();
    if (!cursor.Tag("TLEN")) return false;
    std::vector<uint16_t> lengths(types.size());
    for (size_t i = 0; i < types.size(); ++i)
        if (!cursor.U16(lengths[i])) return false;

    cursor.Align4();
    if (!cursor.Tag("STRC") || !cursor.U32(count)) return false;
    if (count > (1u << 20)) return false;

    out.clear();
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t type = 0, fieldCount = 0;
        if (!cursor.U16(type) || !cursor.U16(fieldCount)) return false;
        if (type >= types.size()) return false;

        Blend::Struct entry;
        entry.Name = types[type];
        entry.Fields.reserve(fieldCount);

        size_t offset = 0;
        for (uint16_t f = 0; f < fieldCount; ++f) {
            uint16_t fieldType = 0, fieldName = 0;
            if (!cursor.U16(fieldType) || !cursor.U16(fieldName)) return false;
            if (fieldType >= types.size() || fieldName >= names.size()) return false;

            Blend::Field field;
            field.Type = types[fieldType];
            field.Name = names[fieldName];
            ParseFieldName(field.Name, field.BareName, field.ElementCount, field.IsPointer);
            const size_t base = field.IsPointer ? static_cast<size_t>(pointerSize)
                                                : lengths[fieldType];
            field.Size = base * field.ElementCount;
            field.Offset = offset;
            offset += field.Size;
            entry.Fields.push_back(std::move(field));
        }
        // The SDNA states each type's size directly; trusting the sum of the
        // fields instead would hide a disagreement rather than surface it.
        entry.Size = lengths[type];
        out.push_back(std::move(entry));
    }
    return true;
}

void AddUnique(std::vector<std::string>& list, const std::string& value) {
    if (value.empty()) return;
    if (std::find(list.begin(), list.end(), value) == list.end()) list.push_back(value);
}

// Everything Blender may store an integer in, and its width. A field's
// declared type is the authority; nothing here guesses from the value.
int IntegerWidth(const std::string& type, bool& isSigned) {
    isSigned = true;
    if (type == "char" || type == "int8_t") return 1;
    if (type == "uchar" || type == "uint8_t") { isSigned = false; return 1; }
    if (type == "short") return 2;
    if (type == "ushort") { isSigned = false; return 2; }
    if (type == "int" || type == "int32_t") return 4;
    if (type == "uint" || type == "uint32_t") { isSigned = false; return 4; }
    if (type == "long") return 4;
    if (type == "ulong") { isSigned = false; return 4; }
    if (type == "int64_t") return 8;
    if (type == "uint64_t") { isSigned = false; return 8; }
    return 0;
}

} // namespace

namespace Blend {

const Field* Struct::Find(const std::string& bareName) const {
    for (const Field& field : Fields)
        if (field.BareName == bareName) return &field;
    return nullptr;
}

const Struct* File::StructNamed(const std::string& name) const {
    for (const Struct& entry : Structs)
        if (entry.Name == name) return &entry;
    return nullptr;
}

const Block* File::BlockAt(uint64_t address) const {
    if (address == 0) return nullptr;
    // Blocks are written in address order often enough to sort, but not
    // reliably, so this is a scan over a containment test rather than a map
    // lookup: a pointer to the third element of an array lands inside the
    // block that holds the array, not on its head.
    for (const Block& block : Blocks) {
        if (block.OldAddress == address) return &block;
    }
    for (const Block& block : Blocks) {
        if (block.OldAddress == 0 || block.BodySize == 0) continue;
        if (address > block.OldAddress && address < block.OldAddress + block.BodySize)
            return &block;
    }
    return nullptr;
}

Ref File::At(const Block& block, size_t index) const {
    const Struct* type = StructAt(block.StructIndex);
    if (!type || type->Size == 0) return {};
    const size_t offset = block.BodyOffset + index * type->Size;
    if (index >= (block.Count ? block.Count : 1)) return {};
    if (offset + type->Size > Bytes.size()) return {};
    return Ref(this, type, offset);
}

std::vector<const Block*> File::BlocksOfCode(const char* code) const {
    std::vector<const Block*> found;
    for (const Block& block : Blocks)
        if (block.Is(code)) found.push_back(&block);
    return found;
}

bool File::ReadBytes(size_t offset, void* out, size_t size) const {
    if (offset + size > Bytes.size()) return false;
    std::memcpy(out, Bytes.data() + offset, size);
    return true;
}

int File::VersionNumber() const {
    std::string digits;
    for (char c : Version)
        if (c >= '0' && c <= '9') digits.push_back(c);
    return digits.empty() ? 0 : std::atoi(digits.c_str());
}

uint64_t File::ReadPointer(size_t offset) const {
    if (PointerSize == 8) {
        uint64_t value = 0;
        if (!ReadBytes(offset, &value, 8)) return 0;
        return BigEndian ? Swap64(value) : value;
    }
    uint32_t value = 0;
    if (!ReadBytes(offset, &value, 4)) return 0;
    return BigEndian ? Swap32(value) : value;
}

int64_t File::ReadInteger(size_t offset, const std::string& type) const {
    bool isSigned = true;
    const int width = IntegerWidth(type, isSigned);
    if (width == 0) {
        // A float field asked for as an integer, which a caller reading a
        // count from a build that changed the type should still get right.
        if (type == "float" || type == "double")
            return static_cast<int64_t>(ReadNumber(offset, type));
        return 0;
    }
    uint64_t raw = 0;
    if (!ReadBytes(offset, &raw, static_cast<size_t>(width))) return 0;
    if (BigEndian) {
        switch (width) {
            case 2: raw = Swap16(static_cast<uint16_t>(raw)); break;
            case 4: raw = Swap32(static_cast<uint32_t>(raw)); break;
            case 8: raw = Swap64(raw); break;
            default: break;
        }
    }
    if (!isSigned) return static_cast<int64_t>(raw);
    switch (width) {
        case 1: return static_cast<int8_t>(raw);
        case 2: return static_cast<int16_t>(raw);
        case 4: return static_cast<int32_t>(raw);
        default: return static_cast<int64_t>(raw);
    }
}

double File::ReadNumber(size_t offset, const std::string& type) const {
    if (type == "float") {
        uint32_t raw = 0;
        if (!ReadBytes(offset, &raw, 4)) return 0.0;
        if (BigEndian) raw = Swap32(raw);
        float value = 0.0f;
        std::memcpy(&value, &raw, 4);
        return value;
    }
    if (type == "double") {
        uint64_t raw = 0;
        if (!ReadBytes(offset, &raw, 8)) return 0.0;
        if (BigEndian) raw = Swap64(raw);
        double value = 0.0;
        std::memcpy(&value, &raw, 8);
        return value;
    }
    return static_cast<double>(ReadInteger(offset, type));
}

int64_t Ref::Int(const std::string& field, int64_t fallback) const {
    if (!file_ || !type_) return fallback;
    const Field* found = type_->Find(field);
    if (!found || found->IsPointer) return fallback;
    return file_->ReadInteger(offset_ + found->Offset, found->Type);
}

double Ref::Real(const std::string& field, double fallback) const {
    if (!file_ || !type_) return fallback;
    const Field* found = type_->Find(field);
    if (!found || found->IsPointer) return fallback;
    return file_->ReadNumber(offset_ + found->Offset, found->Type);
}

size_t Ref::Reals(const std::string& field, double* out, size_t count) const {
    if (!file_ || !type_ || !out) return 0;
    const Field* found = type_->Find(field);
    if (!found || found->IsPointer || found->ElementCount == 0) return 0;
    const size_t stride = found->Size / found->ElementCount;
    const size_t total = std::min(count, found->ElementCount);
    for (size_t i = 0; i < total; ++i)
        out[i] = file_->ReadNumber(offset_ + found->Offset + i * stride, found->Type);
    return total;
}

size_t Ref::Ints(const std::string& field, int64_t* out, size_t count) const {
    if (!file_ || !type_ || !out) return 0;
    const Field* found = type_->Find(field);
    if (!found || found->IsPointer || found->ElementCount == 0) return 0;
    const size_t stride = found->Size / found->ElementCount;
    const size_t total = std::min(count, found->ElementCount);
    for (size_t i = 0; i < total; ++i)
        out[i] = file_->ReadInteger(offset_ + found->Offset + i * stride, found->Type);
    return total;
}

std::string Ref::Text(const std::string& field) const {
    if (!file_ || !type_) return {};
    const Field* found = type_->Find(field);
    if (!found || found->IsPointer) return {};
    const size_t start = offset_ + found->Offset;
    const size_t limit = std::min(file_->Bytes.size(), start + found->Size);
    size_t end = start;
    while (end < limit && file_->Bytes[end] != '\0') ++end;
    if (start >= file_->Bytes.size()) return {};
    return std::string(reinterpret_cast<const char*>(file_->Bytes.data() + start), end - start);
}

uint64_t Ref::Pointer(const std::string& field) const {
    if (!file_ || !type_) return 0;
    const Field* found = type_->Find(field);
    if (!found || !found->IsPointer) return 0;
    return file_->ReadPointer(offset_ + found->Offset);
}

const Block* Ref::PointedBlock(const std::string& field) const {
    const uint64_t address = Pointer(field);
    return address ? file_->BlockAt(address) : nullptr;
}

Ref Ref::Inner(const std::string& field) const {
    if (!file_ || !type_) return {};
    const Field* found = type_->Find(field);
    if (!found || found->IsPointer) return {};
    const Struct* inner = file_->StructNamed(found->Type);
    if (!inner) return {};
    return Ref(file_, inner, offset_ + found->Offset);
}

bool Parse(const std::vector<uint8_t>& data, File& out,
           const std::function<void(const std::string&)>& warn) {
    auto fail = [&out, &warn](const std::string& message) {
        out.Valid = false;
        out.Error = message;
        if (warn) warn("Blender: " + message);
        return false;
    };

    if (StartsWith(data, kZstdMagic, sizeof(kZstdMagic))) {
        out.Compression = "zstd";
        return fail("the file is zstd compressed, which Blender 3.0 and later write by "
                    "default; this reader inflates only gzip. Re-save with Compress off, or "
                    "with Blender's legacy gzip compression");
    }
    if (StartsWith(data, kGzipMagic, sizeof(kGzipMagic))) {
        out.Compression = "gzip";
        std::string error;
        if (!Inflate(data, out.Bytes, error))
            return fail("the gzip stream could not be inflated (" + error + ")");
    } else {
        out.Compression = "none";
        out.Bytes = data;
    }

    const std::vector<uint8_t>& file = out.Bytes;
    if (file.size() < kHeaderSize || std::memcmp(file.data(), "BLENDER", 7) != 0)
        return fail("no BLENDER signature");

    out.PointerSize = static_cast<char>(file[7]) == '-' ? 8 : 4;
    out.BigEndian = static_cast<char>(file[8]) == 'V';
    out.Version.assign(reinterpret_cast<const char*>(file.data() + 9), 3);

    // --- pass one: the block index, and the SDNA within it ---
    const size_t headerSize = 16 + static_cast<size_t>(out.PointerSize);
    size_t offset = kHeaderSize;
    int sdnaBlock = -1;
    while (offset + headerSize <= file.size()) {
        Block block;
        std::memcpy(block.Code, file.data() + offset, 4);

        uint32_t size = 0, structIndex = 0, count = 0;
        std::memcpy(&size, file.data() + offset + 4, 4);
        std::memcpy(&structIndex, file.data() + offset + 8 + out.PointerSize, 4);
        std::memcpy(&count, file.data() + offset + 12 + out.PointerSize, 4);
        if (out.BigEndian) {
            size = Swap32(size);
            structIndex = Swap32(structIndex);
            count = Swap32(count);
        }

        block.BodyOffset = offset + headerSize;
        block.BodySize = size;
        block.StructIndex = static_cast<int>(structIndex);
        block.Count = count;
        // The address field is read through ReadPointer once Bytes is in place,
        // which it is: Bytes *is* `file`.
        block.OldAddress = out.ReadPointer(offset + 8);

        if (block.BodyOffset + size > file.size()) break;
        if (block.Is("ENDB")) {
            out.Blocks.push_back(block);
            break;
        }
        if (block.Is("DNA1")) sdnaBlock = static_cast<int>(out.Blocks.size());
        out.Blocks.push_back(block);
        offset = block.BodyOffset + size;
    }

    if (sdnaBlock < 0) return fail("no SDNA block");
    const Block& dna = out.Blocks[static_cast<size_t>(sdnaBlock)];
    if (!ParseSdna(file.data() + dna.BodyOffset, dna.BodySize, out.BigEndian, out.PointerSize,
                   out.Structs))
        return fail("the SDNA block could not be parsed");

    if (out.Blocks.empty()) return fail("no blocks");
    out.Valid = true;
    return true;
}

} // namespace Blend

// ===== THE INSPECTOR =====

std::string BlendFileInfo::Summary() const {
    if (!Valid) return "Not a readable .blend" + (Error.empty() ? "" : " - " + Error);

    std::ostringstream text;
    text << "Blender " << Version << " file, " << BlockCount << " blocks";
    if (!ObjectNames.empty()) text << ", " << ObjectNames.size() << " objects";
    if (!MeshNames.empty()) text << ", " << MeshNames.size() << " meshes";
    if (!MaterialNames.empty()) text << ", " << MaterialNames.size() << " materials";
    text << ". Stored geometry: " << StoredVertexCount << " vertices in " << StoredPolygonCount
         << " polygons";
    if (HasUnappliedModifiers()) {
        text << ", which is the cage *before* ";
        for (size_t i = 0; i < Modifiers.size(); ++i) {
            if (i) text << (i + 1 == Modifiers.size() ? " and " : ", ");
            text << Modifiers[i];
        }
        text << " - a .blend stores modifiers unapplied, so the evaluated model is not in the "
                "file and what is read is what the artist modelled";
    }
    text << ".";
    return text.str();
}

bool LooksLikeBlendFile(const std::vector<uint8_t>& head) {
    if (head.size() >= 7 && std::memcmp(head.data(), "BLENDER", 7) == 0) return true;
    // A compressed .blend cannot be told from any other gzip or zstd stream by
    // its first bytes alone; the caller's extension is what narrows it, and the
    // parse confirms by finding the magic after decompression.
    return StartsWith(head, kGzipMagic, sizeof(kGzipMagic)) ||
           StartsWith(head, kZstdMagic, sizeof(kZstdMagic));
}

BlendFileInfo ReadBlendFileInfo(const std::vector<uint8_t>& data,
                                const std::function<void(const std::string&)>& warn) {
    BlendFileInfo info;
    Blend::File file;
    Blend::Parse(data, file, warn);

    info.Compression = file.Compression;
    info.PointerSize = file.PointerSize;
    info.BigEndian = file.BigEndian;
    info.Version = file.Version;
    // The header spells 2.78 as "278" and 4.20 as "420".
    if (info.Version.size() == 3) info.Version.insert(1, ".");
    if (!file.Valid) {
        info.Error = file.Error;
        return info;
    }

    info.BlockCount = file.Blocks.size();
    for (const Blend::Block& block : file.Blocks) {
        const Blend::Struct* type = file.StructAt(block.StructIndex);
        if (!type) continue;
        const size_t multiplicity = block.Count ? block.Count : 1;
        info.StructCounts[type->Name] += multiplicity;

        if (type->Name == "MVert") info.StoredVertexCount += multiplicity;
        else if (type->Name == "MPoly") info.StoredPolygonCount += multiplicity;

        // Modifier blocks name themselves: "MirrorModifierData" and so on.
        static const std::string suffix = "ModifierData";
        if (type->Name.size() > suffix.size() &&
            type->Name.compare(type->Name.size() - suffix.size(), suffix.size(), suffix) == 0)
            AddUnique(info.Modifiers, type->Name.substr(0, type->Name.size() - suffix.size()));

        // Named datablocks: the ID at the head of the struct carries them.
        if (type->Fields.empty() || type->Fields[0].Type != "ID") continue;
        const Blend::Ref ref = file.At(block, 0);
        const Blend::Ref id = ref.Inner("id");
        std::string name = id.Text("name");
        if (name.size() > 2) name.erase(0, 2);
        if (type->Name == "Object") AddUnique(info.ObjectNames, name);
        else if (type->Name == "Mesh") AddUnique(info.MeshNames, name);
        else if (type->Name == "Material") AddUnique(info.MaterialNames, name);
    }

    // 4.x keeps neither vertices nor polygons in a struct of its own; the
    // counts come off the Mesh instead. Reported the same way either way.
    if (info.StoredVertexCount == 0) {
        for (const Blend::Block* block : file.BlocksOfCode("ME")) {
            const Blend::Ref mesh = file.At(*block, 0);
            if (!mesh) continue;
            info.StoredVertexCount += static_cast<size_t>(std::max<int64_t>(
                    0, mesh.Int("totvert", 0)));
            info.StoredPolygonCount += static_cast<size_t>(std::max<int64_t>(
                    0, mesh.Int("totpoly", mesh.Int("faces_num", 0))));
        }
    }

    std::sort(info.Modifiers.begin(), info.Modifiers.end());
    info.Valid = info.BlockCount > 0;
    if (!info.Valid) info.Error = "no blocks";
    return info;
}

BlendFileInfo ReadBlendFileInfo(const std::string& filePath,
                                const std::function<void(const std::string&)>& warn) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file) {
        BlendFileInfo info;
        info.Error = "cannot read " + filePath;
        if (warn) warn("Blender: " + info.Error);
        return info;
    }
    const std::streamoff size = file.tellg();
    std::vector<uint8_t> data(static_cast<size_t>(std::max<std::streamoff>(0, size)));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return ReadBlendFileInfo(data, warn);
}

} // namespace UltraCanvas
