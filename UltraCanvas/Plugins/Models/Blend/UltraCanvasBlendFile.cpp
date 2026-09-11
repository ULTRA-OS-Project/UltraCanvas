// Plugins/Models/Blend/UltraCanvasBlendFile.cpp
// The .blend inspector.
//
// Only three parts of the format are touched, and all three have been stable
// across many Blender releases:
//
//   1. The 12-byte header: "BLENDER", a pointer-size character, an endianness
//      character and a three-character version.
//   2. The block index: every block carries a 4-character code, its length,
//      its original memory address, the SDNA struct index describing it, and
//      how many of that struct it holds. Walking it needs nothing else.
//   3. The SDNA block itself, which names every struct and field in the file.
//
// Struct *semantics* are deliberately not decoded — those change between
// releases, and a reader that follows them is a reader that breaks. The one
// exception is the ID name, which is the first field of the ID struct at the
// start of every datablock, and whose offset is computed from the SDNA rather
// than assumed.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/Blend/UltraCanvasBlendFile.h"

#include <algorithm>
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
        output.insert(output.end(), buffer.data(), buffer.data() + (buffer.size() - stream.avail_out));
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

struct StructField {
    std::string Type;
    std::string Name;
};

struct Sdna {
    std::vector<std::string> StructNames;                  // by struct index
    std::vector<std::vector<StructField>> StructFields;    // parallel
    std::map<std::string, uint16_t> TypeLengths;
};

// A cursor that refuses to read past the end, so a truncated or hostile SDNA
// block ends the parse instead of walking off the buffer.
class Cursor {
public:
    Cursor(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool Has(size_t bytes) const { return position_ + bytes <= size_; }
    size_t Position() const { return position_; }
    void Align4() { position_ = (position_ + 3u) & ~size_t(3); }
    bool Tag(const char* expected) {
        if (!Has(4) || std::memcmp(data_ + position_, expected, 4) != 0) return false;
        position_ += 4;
        return true;
    }
    bool U32(uint32_t& out) {
        if (!Has(4)) return false;
        std::memcpy(&out, data_ + position_, 4);
        position_ += 4;
        return true;
    }
    bool U16(uint16_t& out) {
        if (!Has(2)) return false;
        std::memcpy(&out, data_ + position_, 2);
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
    size_t position_ = 0;
};

bool ParseSdna(const uint8_t* data, size_t size, Sdna& sdna) {
    Cursor cursor(data, size);
    if (!cursor.Tag("SDNA") || !cursor.Tag("NAME")) return false;

    uint32_t count = 0;
    if (!cursor.U32(count)) return false;
    std::vector<std::string> names(count);
    for (uint32_t i = 0; i < count; ++i)
        if (!cursor.String(names[i])) return false;

    cursor.Align4();
    if (!cursor.Tag("TYPE") || !cursor.U32(count)) return false;
    std::vector<std::string> types(count);
    for (uint32_t i = 0; i < count; ++i)
        if (!cursor.String(types[i])) return false;

    cursor.Align4();
    if (!cursor.Tag("TLEN")) return false;
    std::vector<uint16_t> lengths(types.size());
    for (size_t i = 0; i < types.size(); ++i)
        if (!cursor.U16(lengths[i])) return false;
    for (size_t i = 0; i < types.size(); ++i) sdna.TypeLengths[types[i]] = lengths[i];

    cursor.Align4();
    if (!cursor.Tag("STRC") || !cursor.U32(count)) return false;
    sdna.StructNames.reserve(count);
    sdna.StructFields.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t type = 0, fieldCount = 0;
        if (!cursor.U16(type) || !cursor.U16(fieldCount)) return false;
        if (type >= types.size()) return false;

        std::vector<StructField> fields;
        fields.reserve(fieldCount);
        for (uint16_t f = 0; f < fieldCount; ++f) {
            uint16_t fieldType = 0, fieldName = 0;
            if (!cursor.U16(fieldType) || !cursor.U16(fieldName)) return false;
            if (fieldType >= types.size() || fieldName >= names.size()) return false;
            fields.push_back(StructField{types[fieldType], names[fieldName]});
        }
        sdna.StructNames.push_back(types[type]);
        sdna.StructFields.push_back(std::move(fields));
    }
    return true;
}

// The size a field occupies, from its declared name: a leading '*' makes it a
// pointer, and "[n]" suffixes multiply. This is how the SDNA describes layout,
// so it is exact rather than a guess.
size_t FieldSize(const Sdna& sdna, const StructField& field, int pointerSize) {
    size_t base = 0;
    if (!field.Name.empty() && (field.Name[0] == '*' || field.Name.rfind("(*", 0) == 0)) {
        base = static_cast<size_t>(pointerSize);
    } else {
        auto found = sdna.TypeLengths.find(field.Type);
        if (found == sdna.TypeLengths.end()) return 0;
        base = found->second;
    }

    size_t multiplier = 1;
    for (size_t i = 0; i + 1 < field.Name.size(); ++i) {
        if (field.Name[i] != '[') continue;
        const size_t close = field.Name.find(']', i);
        if (close == std::string::npos) break;
        multiplier *= static_cast<size_t>(std::atoi(field.Name.c_str() + i + 1));
        i = close;
    }
    return base * multiplier;
}

// Byte offset of ID.name inside a datablock whose struct begins with an ID.
// Computed from the SDNA rather than assumed, because the ID struct has gained
// fields over the years.
bool IdNameOffset(const Sdna& sdna, size_t structIndex, int pointerSize, size_t& offset) {
    if (structIndex >= sdna.StructFields.size()) return false;
    const std::vector<StructField>& fields = sdna.StructFields[structIndex];
    if (fields.empty() || fields[0].Type != "ID") return false;

    // Find the ID struct, then walk to its "name" field.
    for (size_t i = 0; i < sdna.StructNames.size(); ++i) {
        if (sdna.StructNames[i] != "ID") continue;
        size_t cursor = 0;
        for (const StructField& field : sdna.StructFields[i]) {
            if (field.Name.rfind("name[", 0) == 0) {
                offset = cursor;
                return true;
            }
            const size_t size = FieldSize(sdna, field, pointerSize);
            if (size == 0) return false;
            cursor += size;
        }
        return false;
    }
    return false;
}

// A datablock's name, minus the two-character type prefix Blender puts on it
// ("OBCube.021" -> "Cube.021").
std::string ReadIdName(const std::vector<uint8_t>& data, size_t bodyOffset, size_t nameOffset) {
    const size_t start = bodyOffset + nameOffset;
    if (start >= data.size()) return {};
    size_t end = start;
    const size_t limit = std::min(data.size(), start + 66);
    while (end < limit && data[end] != '\0') ++end;
    std::string name(reinterpret_cast<const char*>(data.data() + start), end - start);
    if (name.size() > 2) name.erase(0, 2);
    return name;
}

void AddUnique(std::vector<std::string>& list, const std::string& value) {
    if (value.empty()) return;
    if (std::find(list.begin(), list.end(), value) == list.end()) list.push_back(value);
}

} // namespace

std::string BlendFileInfo::Summary() const {
    if (!Valid) return "Not a readable Blender file" + (Error.empty() ? "" : ": " + Error);

    std::ostringstream text;
    text << "Blender " << Version << " file";
    if (Compression != "none") text << " (" << Compression << " compressed)";
    // std::map::count answers 0 or 1 for a unique key; the stored value is the
    // count that was wanted.
    const auto objects = StructCounts.find("Object");
    text << " — " << (objects != StructCounts.end() ? objects->second : ObjectNames.size())
         << " object(s), " << MeshNames.size()
         << " mesh(es), " << MaterialNames.size() << " material(s)";
    if (StoredVertexCount > 0) text << ", " << StoredVertexCount << " stored vertices";

    if (HasUnappliedModifiers()) {
        text << ". Modifiers are stored unapplied (";
        for (size_t i = 0; i < Modifiers.size(); ++i) {
            if (i) text << ", ";
            text << Modifiers[i];
        }
        text << "), so the stored mesh is the cage before them and not the model as it looks in "
                "Blender. Export to glTF, COLLADA or OBJ from Blender to get the evaluated "
                "geometry.";
    } else {
        text << ". A .blend stores Blender's own structures rather than an exchange model; "
                "export to glTF, COLLADA or OBJ to import the geometry.";
    }
    return text.str();
}

bool LooksLikeBlendFile(const std::vector<uint8_t>& head) {
    if (head.size() >= 7 && std::memcmp(head.data(), "BLENDER", 7) == 0) return true;
    // A compressed .blend cannot be told from any other gzip or zstd stream by
    // its first bytes alone; the caller's extension is what narrows it, and
    // ReadBlendFileInfo confirms by finding the magic after decompression.
    return StartsWith(head, kGzipMagic, sizeof(kGzipMagic)) ||
           StartsWith(head, kZstdMagic, sizeof(kZstdMagic));
}

BlendFileInfo ReadBlendFileInfo(const std::vector<uint8_t>& data,
                                const std::function<void(const std::string&)>& warn) {
    auto report = [&warn](const std::string& message) { if (warn) warn(message); };

    BlendFileInfo info;
    std::vector<uint8_t> raw;
    const std::vector<uint8_t>* body = &data;

    if (StartsWith(data, kZstdMagic, sizeof(kZstdMagic))) {
        info.Compression = "zstd";
        info.Error = "the file is zstd compressed, which Blender 3.0 and later write by default; "
                     "this reader inflates only gzip";
        report("Blender: " + info.Error);
        return info;
    }
    if (StartsWith(data, kGzipMagic, sizeof(kGzipMagic))) {
        info.Compression = "gzip";
        std::string error;
        if (!Inflate(data, raw, error)) {
            info.Error = "the gzip stream could not be inflated (" + error + ")";
            report("Blender: " + info.Error);
            return info;
        }
        body = &raw;
    } else {
        info.Compression = "none";
    }

    const std::vector<uint8_t>& file = *body;
    if (file.size() < kHeaderSize || std::memcmp(file.data(), "BLENDER", 7) != 0) {
        info.Error = "no BLENDER signature";
        report("Blender: " + info.Error);
        return info;
    }

    const char pointerChar = static_cast<char>(file[7]);
    const char endianChar = static_cast<char>(file[8]);
    info.PointerSize = pointerChar == '-' ? 8 : 4;
    info.BigEndian = endianChar == 'V';
    info.Version.assign(reinterpret_cast<const char*>(file.data() + 9), 3);
    // The header spells 2.78 as "278" and 4.20 as "420".
    if (info.Version.size() == 3) info.Version.insert(1, ".");

    if (info.BigEndian) {
        info.Error = "big-endian .blend files are not read";
        report("Blender: " + info.Error);
        return info;
    }

    // --- pass one: find the SDNA ---
    const size_t headerSize = 16 + static_cast<size_t>(info.PointerSize);
    Sdna sdna;
    bool haveSdna = false;
    size_t offset = kHeaderSize;
    while (offset + headerSize <= file.size()) {
        char code[5] = {0};
        std::memcpy(code, file.data() + offset, 4);
        uint32_t size = 0;
        std::memcpy(&size, file.data() + offset + 4, 4);
        const size_t bodyOffset = offset + headerSize;
        if (bodyOffset + size > file.size()) break;

        if (std::strncmp(code, "DNA1", 4) == 0) {
            haveSdna = ParseSdna(file.data() + bodyOffset, size, sdna);
            if (!haveSdna) report("Blender: the SDNA block could not be parsed");
            break;
        }
        if (std::strncmp(code, "ENDB", 4) == 0) break;
        offset = bodyOffset + size;
    }
    if (!haveSdna) {
        info.Error = "no usable SDNA block";
        report("Blender: " + info.Error);
        return info;
    }

    // --- pass two: classify every block ---
    offset = kHeaderSize;
    while (offset + headerSize <= file.size()) {
        char code[5] = {0};
        std::memcpy(code, file.data() + offset, 4);
        uint32_t size = 0, structIndex = 0, count = 0;
        std::memcpy(&size, file.data() + offset + 4, 4);
        std::memcpy(&structIndex, file.data() + offset + 8 + info.PointerSize, 4);
        std::memcpy(&count, file.data() + offset + 12 + info.PointerSize, 4);
        const size_t bodyOffset = offset + headerSize;
        if (bodyOffset + size > file.size()) break;

        ++info.BlockCount;
        if (std::strncmp(code, "ENDB", 4) == 0) break;

        if (structIndex < sdna.StructNames.size()) {
            const std::string& structName = sdna.StructNames[structIndex];
            const size_t multiplicity = count ? count : 1;
            info.StructCounts[structName] += multiplicity;

            if (structName == "MVert") info.StoredVertexCount += multiplicity;
            else if (structName == "MPoly") info.StoredPolygonCount += multiplicity;

            // Modifier blocks name themselves: "MirrorModifierData" and so on.
            static const std::string suffix = "ModifierData";
            if (structName.size() > suffix.size() &&
                structName.compare(structName.size() - suffix.size(), suffix.size(), suffix) == 0)
                AddUnique(info.Modifiers, structName.substr(0, structName.size() - suffix.size()));

            // Named datablocks: the ID at the head of the struct carries them.
            size_t nameOffset = 0;
            if (IdNameOffset(sdna, structIndex, info.PointerSize, nameOffset)) {
                const std::string name = ReadIdName(file, bodyOffset, nameOffset);
                if (structName == "Object") AddUnique(info.ObjectNames, name);
                else if (structName == "Mesh") AddUnique(info.MeshNames, name);
                else if (structName == "Material") AddUnique(info.MaterialNames, name);
            }
        }
        offset = bodyOffset + size;
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
