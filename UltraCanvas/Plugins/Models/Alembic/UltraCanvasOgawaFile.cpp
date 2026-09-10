// Plugins/Models/Alembic/UltraCanvasOgawaFile.cpp
// The Ogawa container and Alembic's object/property model. Declared in
// UltraCanvasOgawaFile.h.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/Alembic/UltraCanvasOgawaFile.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace UltraCanvas {
namespace Ogawa {

namespace {

// Every sample in an Ogawa file is preceded by a 128-bit key that Alembic uses
// to share identical samples between properties. It carries no information a
// reader needs, but it does shift the data.
constexpr size_t kSampleKeySize = 16;
constexpr uint64_t kDataFlag = uint64_t(1) << 63;

uint64_t ReadU64(const uint8_t* at) {
    uint64_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}
uint32_t ReadU32(const uint8_t* at) {
    uint32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

} // namespace

size_t PodSize(Pod pod) {
    switch (pod) {
        case Pod::Boolean: case Pod::Uint8: case Pod::Int8:   return 1;
        case Pod::Uint16:  case Pod::Int16: case Pod::Float16: return 2;
        case Pod::Uint32:  case Pod::Int32: case Pod::Float32: return 4;
        case Pod::Uint64:  case Pod::Int64: case Pod::Float64: return 8;
        default: return 0;
    }
}

const char* PodName(Pod pod) {
    switch (pod) {
        case Pod::Boolean: return "bool";     case Pod::Uint8:   return "uint8";
        case Pod::Int8:    return "int8";     case Pod::Uint16:  return "uint16";
        case Pod::Int16:   return "int16";    case Pod::Uint32:  return "uint32";
        case Pod::Int32:   return "int32";    case Pod::Uint64:  return "uint64";
        case Pod::Int64:   return "int64";    case Pod::Float16: return "float16";
        case Pod::Float32: return "float32";  case Pod::Float64: return "float64";
        case Pod::String:  return "string";   case Pod::Wstring: return "wstring";
        default: return "unknown";
    }
}

std::string MetadataValue(const std::string& metadata, const std::string& key) {
    size_t at = 0;
    while (at < metadata.size()) {
        const size_t end = metadata.find(';', at);
        const std::string entry = metadata.substr(at, end == std::string::npos
                                                         ? std::string::npos : end - at);
        const size_t equals = entry.find('=');
        if (equals != std::string::npos && entry.compare(0, equals, key) == 0)
            return entry.substr(equals + 1);
        if (end == std::string::npos) break;
        at = end + 1;
    }
    return {};
}

// ===== RAW CONTAINER =====

uint64_t Archive::GroupChildCount(uint64_t group) const {
    if (group == 0 || group + 8 > bytes_.size()) return 0;
    const uint64_t count = ReadU64(bytes_.data() + group);
    // A corrupt pointer read as a count would ask for terabytes of children;
    // the file itself bounds how many there can be.
    if (count > (bytes_.size() - group) / 8) return 0;
    return count;
}

uint64_t Archive::ChildPointer(uint64_t group, uint64_t index) const {
    const uint64_t at = group + 8 + index * 8;
    if (at + 8 > bytes_.size()) return 0;
    return ReadU64(bytes_.data() + at);
}

bool Archive::ChildIsData(uint64_t group, uint64_t index) const {
    return (ChildPointer(group, index) & kDataFlag) != 0;
}

uint64_t Archive::ChildGroup(uint64_t group, uint64_t index) const {
    const uint64_t pointer = ChildPointer(group, index);
    return (pointer & kDataFlag) ? 0 : pointer;
}

bool Archive::ChildDataView(uint64_t group, uint64_t index,
                            const uint8_t*& out, size_t& size) const {
    out = nullptr;
    size = 0;
    const uint64_t pointer = ChildPointer(group, index);
    if ((pointer & kDataFlag) == 0) return false;
    const uint64_t at = pointer & ~kDataFlag;
    if (at == 0 || at + 8 > bytes_.size()) return false;
    const uint64_t length = ReadU64(bytes_.data() + at);
    if (length > bytes_.size() - at - 8) return false;
    out = bytes_.data() + at + 8;
    size = static_cast<size_t>(length);
    return true;
}

std::vector<uint8_t> Archive::ChildData(uint64_t group, uint64_t index) const {
    const uint8_t* at = nullptr;
    size_t size = 0;
    if (!ChildDataView(group, index, at, size)) return {};
    return std::vector<uint8_t>(at, at + size);
}

// ===== HEADERS =====

std::string Archive::MetadataAt(size_t index) const {
    // 0 means the property or object simply has none; the pool is 1-based.
    if (index == 0 || index > pool_.size()) return {};
    return pool_[index - 1];
}

std::vector<Object::ChildHeader> Archive::ReadObjectHeaders(const uint8_t* data,
                                                            size_t size) const {
    std::vector<Object::ChildHeader> headers;
    if (!data) return headers;
    // The blob ends with two 128-bit hashes Alembic uses to tell whether a
    // subtree changed. They carry nothing a reader needs.
    const size_t end = size >= 32 ? size - 32 : size;

    size_t at = 0;
    while (at + 4 <= end) {
        const uint32_t nameSize = ReadU32(data + at);
        at += 4;
        if (nameSize > end - at) break;
        Object::ChildHeader header;
        header.Name.assign(reinterpret_cast<const char*>(data + at), nameSize);
        at += nameSize;
        if (at >= end) { headers.push_back(std::move(header)); break; }

        const uint8_t index = data[at++];
        if (index == 0xff) {
            if (at + 4 > end) break;
            const uint32_t length = ReadU32(data + at);
            at += 4;
            if (length > end - at) break;
            header.Metadata.assign(reinterpret_cast<const char*>(data + at), length);
            at += length;
        } else {
            header.Metadata = MetadataAt(index);
        }
        headers.push_back(std::move(header));
    }
    return headers;
}

std::vector<PropertyHeader> Archive::ReadPropertyHeaders(const uint8_t* data, size_t size) const {
    std::vector<PropertyHeader> headers;
    if (!data) return headers;

    size_t at = 0;
    while (at + 4 <= size) {
        const uint32_t info = ReadU32(data + at);
        at += 4;

        PropertyHeader header;
        header.Kind = static_cast<PropertyKind>(info & 0x3);

        if (header.Kind != PropertyKind::Compound) {
            header.Type = static_cast<Pod>((info >> 4) & 0xF);
            header.Extent = static_cast<int>((info >> 12) & 0xFF);

            // Two bits choose how wide the sample count is, so a property with
            // three samples costs one byte and one with a million costs four.
            const uint32_t widths[4] = {1, 2, 4, 8};
            const uint32_t width = widths[(info >> 2) & 0x3];
            if (at + width > size) break;
            uint64_t samples = 0;
            std::memcpy(&samples, data + at, width);
            at += width;
            header.SampleCount = static_cast<uint32_t>(samples);

            // A property whose samples do not all differ records the range
            // that does; skip it, since only the first sample is read here.
            if ((info >> 9) & 0x1) at += static_cast<size_t>(width) * 2;
            // An explicit time-sampling index, for anything not on the default
            // clock.
            if ((info >> 8) & 0x1) at += 4;
        }

        if (at >= size) break;
        const uint8_t nameSize = data[at++];
        if (nameSize > size - at) break;
        header.Name.assign(reinterpret_cast<const char*>(data + at), nameSize);
        at += nameSize;

        const uint32_t metadataIndex = (info >> 20) & 0xFFF;
        if (metadataIndex == 0xFFF) {
            if (at >= size) break;
            const uint8_t inlineIndex = data[at++];
            if (inlineIndex == 0xff) {
                if (at + 4 > size) break;
                const uint32_t length = ReadU32(data + at);
                at += 4;
                if (length > size - at) break;
                header.Metadata.assign(reinterpret_cast<const char*>(data + at), length);
                at += length;
            } else {
                header.Metadata = MetadataAt(inlineIndex);
            }
        } else {
            header.Metadata = MetadataAt(metadataIndex);
        }

        headers.push_back(std::move(header));
    }
    return headers;
}

// ===== ARCHIVE =====

bool Archive::Open(std::istream& stream, const std::function<void(const std::string&)>& warn) {
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    const std::string text = buffer.str();
    return Open(std::vector<uint8_t>(text.begin(), text.end()), warn);
}

bool Archive::Open(std::vector<uint8_t> bytes,
                   const std::function<void(const std::string&)>& warn) {
    auto complain = [&warn](const std::string& message) { if (warn) warn(message); };

    bytes_ = std::move(bytes);
    valid_ = false;
    pool_.clear();
    metadata_.clear();

    if (!LooksLikeOgawaFile(bytes_)) {
        if (LooksLikeHdf5File(bytes_))
            complain("Alembic: this archive uses the HDF5 backend, which this reader does not "
                     "implement; re-export it with the Ogawa backend, which every Alembic since "
                     "1.1 writes by default");
        else
            complain("Alembic: the file does not begin with the Ogawa magic");
        return false;
    }
    if (bytes_.size() < 16) return false;

    root_ = ReadU64(bytes_.data() + 8);
    if (root_ == 0 || root_ >= bytes_.size()) {
        complain("Alembic: the archive's root group is outside the file");
        return false;
    }

    // The root's six children are fixed by the format: two version numbers,
    // the top object, the archive metadata, the time samplings, and the string
    // pool every other metadata reference indexes into.
    const uint64_t count = GroupChildCount(root_);
    if (count < 3) {
        complain("Alembic: the archive's root group is malformed");
        return false;
    }

    const std::vector<uint8_t> version = ChildData(root_, 1);
    if (version.size() >= 4) fileVersion_ = static_cast<int>(ReadU32(version.data()));

    if (count > 3) {
        const std::vector<uint8_t> archiveMetadata = ChildData(root_, 3);
        metadata_.assign(archiveMetadata.begin(), archiveMetadata.end());
    }

    if (count > 5) {
        // The pool is simply length-prefixed strings back to back.
        const uint8_t* at = nullptr;
        size_t size = 0;
        if (ChildDataView(root_, 5, at, size)) {
            size_t offset = 0;
            while (offset < size) {
                const uint8_t length = at[offset++];
                if (length > size - offset) break;
                pool_.emplace_back(reinterpret_cast<const char*>(at + offset), length);
                offset += length;
            }
        }
    }

    valid_ = true;
    return true;
}

Object Archive::Top() const {
    if (!valid_) return {};
    const uint64_t top = ChildGroup(root_, 2);
    if (top == 0) return {};
    return Object(this, top, "", "");
}

// ===== OBJECT =====

Object::Object(const Archive* archive, uint64_t groupOffset, std::string name,
               std::string metadata)
        : archive_(archive), group_(groupOffset), name_(std::move(name)),
          metadata_(std::move(metadata)) {
    // An object group is: its property compound, then its child objects, then
    // one data block naming those children.
    const uint64_t count = archive_->GroupChildCount(group_);
    if (count == 0) return;
    const uint8_t* at = nullptr;
    size_t size = 0;
    if (archive_->ChildDataView(group_, count - 1, at, size))
        children_ = archive_->ReadObjectHeaders(at, size);
}

std::string Object::Schema() const {
    std::string schema = MetadataValue(metadata_, "schema");
    // "AbcGeom_PolyMesh_v1:.geom" on an object; the part before the colon is
    // the kind.
    const size_t colon = schema.find(':');
    if (colon != std::string::npos) schema.erase(colon);
    if (schema.empty()) schema = MetadataValue(metadata_, "schemaObjTitle");
    const size_t colon2 = schema.find(':');
    if (colon2 != std::string::npos) schema.erase(colon2);
    return schema;
}

Compound Object::Properties() const {
    if (!archive_) return {};
    const uint64_t count = archive_->GroupChildCount(group_);
    if (count == 0 || archive_->ChildIsData(group_, 0)) return {};
    return Compound(archive_, archive_->ChildGroup(group_, 0));
}

Object Object::Child(size_t index) const {
    if (!archive_ || index >= children_.size()) return {};
    // Child object i is group child i + 1: child 0 is the property compound.
    const uint64_t group = archive_->ChildGroup(group_, index + 1);
    if (group == 0) return {};
    return Object(archive_, group, children_[index].Name, children_[index].Metadata);
}

// ===== COMPOUND =====

Compound::Compound(const Archive* archive, uint64_t groupOffset)
        : archive_(archive), group_(groupOffset) {
    const uint64_t count = archive_->GroupChildCount(group_);
    if (count == 0) return;
    const uint8_t* at = nullptr;
    size_t size = 0;
    if (archive_->ChildDataView(group_, count - 1, at, size))
        headers_ = archive_->ReadPropertyHeaders(at, size);
}

const PropertyHeader& Compound::At(size_t index) const {
    static const PropertyHeader empty;
    return index < headers_.size() ? headers_[index] : empty;
}

int Compound::Find(const std::string& name) const {
    for (size_t i = 0; i < headers_.size(); ++i)
        if (headers_[i].Name == name) return static_cast<int>(i);
    return -1;
}

Compound Compound::Child(size_t index) const {
    if (!archive_ || index >= headers_.size()) return {};
    if (headers_[index].Kind != PropertyKind::Compound) return {};
    const uint64_t group = archive_->ChildGroup(group_, index);
    if (group == 0) return {};
    return Compound(archive_, group);
}

std::vector<uint8_t> Compound::Sample(size_t index, size_t sample) const {
    if (!archive_ || index >= headers_.size()) return {};
    if (headers_[index].Kind == PropertyKind::Compound) return {};

    const uint64_t group = archive_->ChildGroup(group_, index);
    if (group == 0) return {};
    const uint8_t* at = nullptr;
    size_t size = 0;
    if (!archive_->ChildDataView(group, sample, at, size)) return {};
    if (size < kSampleKeySize) return {};
    return std::vector<uint8_t>(at + kSampleKeySize, at + size);
}

namespace {

// Reinterpret a sample as an array of T, but only when the property really
// holds that type and the bytes divide evenly. Anything else gives nothing:
// a wrong-typed read of a mesh is a crash or garbage geometry, and both are
// worse than an empty array the caller can report.
template <typename T>
std::vector<T> As(const std::vector<uint8_t>& bytes, Pod want, Pod actual) {
    if (actual != want || bytes.size() % sizeof(T) != 0) return {};
    std::vector<T> values(bytes.size() / sizeof(T));
    if (!values.empty()) std::memcpy(values.data(), bytes.data(), bytes.size());
    return values;
}

} // namespace

std::vector<float> Compound::Floats(size_t index, size_t sample) const {
    return As<float>(Sample(index, sample), Pod::Float32, At(index).Type);
}
std::vector<double> Compound::Doubles(size_t index, size_t sample) const {
    return As<double>(Sample(index, sample), Pod::Float64, At(index).Type);
}
std::vector<int32_t> Compound::Int32s(size_t index, size_t sample) const {
    return As<int32_t>(Sample(index, sample), Pod::Int32, At(index).Type);
}
std::vector<uint32_t> Compound::Uint32s(size_t index, size_t sample) const {
    return As<uint32_t>(Sample(index, sample), Pod::Uint32, At(index).Type);
}

// ===== SIGNATURES =====

bool LooksLikeOgawaFile(const std::vector<uint8_t>& data) {
    static const char kMagic[] = "Ogawa";
    if (data.size() < 16) return false;
    for (size_t i = 0; i < 5; ++i)
        if (static_cast<char>(data[i]) != kMagic[i]) return false;
    return true;
}

bool LooksLikeHdf5File(const std::vector<uint8_t>& data) {
    static const uint8_t kMagic[] = {0x89, 'H', 'D', 'F', '\r', '\n', 0x1a, '\n'};
    if (data.size() < sizeof(kMagic)) return false;
    return std::memcmp(data.data(), kMagic, sizeof(kMagic)) == 0;
}

} // namespace Ogawa
} // namespace UltraCanvas
