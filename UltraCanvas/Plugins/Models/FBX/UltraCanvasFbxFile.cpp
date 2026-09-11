// Plugins/Models/FBX/UltraCanvasFbxFile.cpp
// The FBX binary container.
//
// Two details are worth stating because getting either wrong reads a file that
// is not there:
//
//   * From version 7500 the three header words are uint64 rather than uint32,
//     and so is the null record that ends a child list. The version in the
//     header is the only thing that says which, and a 7.5 file read as 7.4
//     walks straight off the end of the first record.
//   * A record's child list is present exactly when the bytes between the end
//     of its property list and its EndOffset are more than one null record.
//     There is no child count. Reading it as "any remaining bytes" makes the
//     trailing null record look like a record with a zero-length name.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework

#include "Models/FBX/UltraCanvasFbxFile.h"

#include <cstring>

#include <zlib.h>

namespace UltraCanvas {
namespace Fbx {

const Node* Node::Find(const std::string& name) const {
    for (const Node& child : Children)
        if (child.Name == name) return &child;
    return nullptr;
}

std::vector<const Node*> Node::FindAll(const std::string& name) const {
    std::vector<const Node*> found;
    for (const Node& child : Children)
        if (child.Name == name) found.push_back(&child);
    return found;
}

const Node* File::Find(const std::string& name) const {
    for (const Node& root : Roots)
        if (root.Name == name) return &root;
    return nullptr;
}

// ===== Properties70 =====

const Node* FindProperty(const Node& owner, const std::string& name) {
    const Node* properties = owner.Find("Properties70");
    if (!properties) return nullptr;
    for (const Node& entry : properties->Children) {
        if (entry.Name != "P") continue;
        if (entry.TextAt(0) == name) return &entry;
    }
    return nullptr;
}

bool HasProperty(const Node& owner, const std::string& name) {
    return FindProperty(owner, name) != nullptr;
}

// A P record is (name, type, subtype, flags, values...), so the values start
// at index 4.
double PropertyReal(const Node& owner, const std::string& name, double fallback) {
    const Node* entry = FindProperty(owner, name);
    if (!entry || entry->Properties.size() < 5) return fallback;
    return entry->RealAt(4, fallback);
}

bool PropertyVec3(const Node& owner, const std::string& name, double out[3]) {
    const Node* entry = FindProperty(owner, name);
    if (!entry || entry->Properties.size() < 7) return false;
    for (int i = 0; i < 3; ++i) out[i] = entry->RealAt(4 + static_cast<size_t>(i), 0.0);
    return true;
}

std::string PropertyText(const Node& owner, const std::string& name) {
    const Node* entry = FindProperty(owner, name);
    if (!entry || entry->Properties.size() < 5) return {};
    return entry->TextAt(4);
}

namespace {

// A bounds-checked cursor over the file. Every read goes through it, so a
// truncated or hostile file produces a parse failure rather than a read past
// the buffer.
class Stream {
public:
    Stream(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool Ok() const { return ok_; }
    size_t Position() const { return position_; }
    bool Seek(size_t position) {
        if (position > size_) { ok_ = false; return false; }
        position_ = position;
        return true;
    }
    size_t Remaining() const { return position_ < size_ ? size_ - position_ : 0; }

    bool Read(void* out, size_t bytes) {
        if (bytes > Remaining()) { ok_ = false; return false; }
        std::memcpy(out, data_ + position_, bytes);
        position_ += bytes;
        return true;
    }
    uint8_t U8() { uint8_t v = 0; Read(&v, 1); return v; }
    uint32_t U32() { uint32_t v = 0; Read(&v, 4); return v; }
    uint64_t U64() { uint64_t v = 0; Read(&v, 8); return v; }
    std::string Text(size_t bytes) {
        if (bytes > Remaining()) { ok_ = false; return {}; }
        std::string value(reinterpret_cast<const char*>(data_ + position_), bytes);
        position_ += bytes;
        return value;
    }
    const uint8_t* Raw() const { return data_ + position_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t position_ = 0;
    bool ok_ = true;
};

size_t ElementSize(char code) {
    switch (code) {
        case 'f': case 'i': return 4;
        case 'd': case 'l': return 8;
        case 'b': return 1;
        default: return 0;
    }
}

class Parser {
public:
    Parser(Stream& stream, uint32_t version) : stream_(stream), wide_(version >= 7500) {}

    const std::string& Error() const { return error_; }

    // Reads records until the null record or `stop`. Returns false on a
    // malformed record.
    bool ReadList(std::vector<Node>& out, size_t stop) {
        while (stream_.Ok() && stream_.Position() + HeaderSize() <= stop) {
            const size_t recordStart = stream_.Position();
            const uint64_t end = Word();
            const uint64_t propertyCount = Word();
            const uint64_t propertyBytes = Word();
            const uint8_t nameLength = stream_.U8();
            if (!stream_.Ok()) { error_ = "the file ends inside a record header"; return false; }

            // The null record: every field zero. It ends this list.
            if (end == 0 && propertyCount == 0 && propertyBytes == 0 && nameLength == 0)
                return true;

            if (end <= recordStart || end > stop) {
                error_ = "a record claims to end outside its parent";
                return false;
            }

            Node node;
            node.Name = stream_.Text(nameLength);
            if (!ReadProperties(node, propertyCount)) return false;

            // Children are present exactly when more than a null record's worth
            // of bytes is left before EndOffset.
            const size_t afterProperties = stream_.Position();
            if (afterProperties + HeaderSize() <= static_cast<size_t>(end)) {
                if (++depth_ > kMaxDepth) {
                    error_ = "records nested more than " + std::to_string(kMaxDepth) + " deep";
                    return false;
                }
                if (!ReadList(node.Children, static_cast<size_t>(end))) return false;
                --depth_;
            }

            if (!stream_.Seek(static_cast<size_t>(end))) {
                error_ = "a record's EndOffset is past the end of the file";
                return false;
            }
            out.push_back(std::move(node));
        }
        return stream_.Ok();
    }

private:
    static constexpr int kMaxDepth = 64;

    size_t HeaderSize() const { return wide_ ? 25 : 13; }
    uint64_t Word() { return wide_ ? stream_.U64() : static_cast<uint64_t>(stream_.U32()); }

    bool ReadProperties(Node& node, uint64_t count) {
        // A property count larger than the bytes left is a corrupt file, not a
        // long list: each property is at least two bytes.
        if (count > stream_.Remaining() / 2) {
            error_ = "a record declares more properties than the file can hold";
            return false;
        }
        node.Properties.reserve(static_cast<size_t>(count));
        for (uint64_t i = 0; i < count; ++i) {
            Property property;
            property.Code = static_cast<char>(stream_.U8());
            if (!stream_.Ok()) { error_ = "the file ends inside a property list"; return false; }

            switch (property.Code) {
                case 'Y': { int16_t v = 0; stream_.Read(&v, 2); property.Integer = v; break; }
                case 'C': { property.Integer = stream_.U8() ? 1 : 0; break; }
                case 'I': { int32_t v = 0; stream_.Read(&v, 4); property.Integer = v; break; }
                case 'L': { int64_t v = 0; stream_.Read(&v, 8); property.Integer = v; break; }
                case 'F': { float v = 0.0f; stream_.Read(&v, 4); property.Real = v; break; }
                case 'D': { double v = 0.0; stream_.Read(&v, 8); property.Real = v; break; }
                case 'S': case 'R': {
                    const uint32_t length = stream_.U32();
                    if (!stream_.Ok() || length > stream_.Remaining()) {
                        error_ = "a string or blob is longer than the file";
                        return false;
                    }
                    property.Text = stream_.Text(length);
                    break;
                }
                case 'f': case 'd': case 'l': case 'i': case 'b':
                    if (!ReadArray(property)) return false;
                    break;
                default:
                    error_ = std::string("unknown property type '") + property.Code + "'";
                    return false;
            }
            if (!stream_.Ok()) { error_ = "the file ends inside a property"; return false; }
            node.Properties.push_back(std::move(property));
        }
        return true;
    }

    bool ReadArray(Property& property) {
        const uint32_t length = stream_.U32();
        const uint32_t encoding = stream_.U32();
        const uint32_t compressed = stream_.U32();
        if (!stream_.Ok()) { error_ = "the file ends inside an array header"; return false; }
        if (compressed > stream_.Remaining()) {
            error_ = "an array is longer than the file";
            return false;
        }

        const size_t each = ElementSize(property.Code);
        const size_t plain = static_cast<size_t>(length) * each;
        // An uncompressed array must be exactly its declared size, and a
        // compressed one must not claim to inflate to more than the file could
        // plausibly hold - both are the shape a corrupt length takes.
        if (encoding == 0 && plain != compressed) {
            error_ = "an uncompressed array's byte count disagrees with its length";
            return false;
        }
        if (encoding != 0 && encoding != 1) {
            error_ = "an array uses encoding " + std::to_string(encoding) +
                     ", which is neither raw nor deflate";
            return false;
        }

        std::vector<uint8_t> bytes(plain);
        if (encoding == 0) {
            if (plain && !stream_.Read(bytes.data(), plain)) {
                error_ = "the file ends inside an array";
                return false;
            }
        } else {
            uLongf produced = static_cast<uLongf>(plain);
            const int status = uncompress(bytes.data(), &produced, stream_.Raw(),
                                          static_cast<uLong>(compressed));
            if (status != Z_OK || produced != plain) {
                error_ = "an array's deflate stream does not inflate to its declared length";
                return false;
            }
            stream_.Seek(stream_.Position() + compressed);
        }

        Widen(property, bytes, length);
        return true;
    }

    static void Widen(Property& property, const std::vector<uint8_t>& bytes, uint32_t length) {
        const size_t count = length;
        switch (property.Code) {
            case 'f': {
                property.Reals.resize(count);
                for (size_t i = 0; i < count; ++i) {
                    float v = 0.0f;
                    std::memcpy(&v, bytes.data() + i * 4, 4);
                    property.Reals[i] = v;
                }
                break;
            }
            case 'd': {
                property.Reals.resize(count);
                for (size_t i = 0; i < count; ++i)
                    std::memcpy(&property.Reals[i], bytes.data() + i * 8, 8);
                break;
            }
            case 'i': {
                property.Ints.resize(count);
                for (size_t i = 0; i < count; ++i) {
                    int32_t v = 0;
                    std::memcpy(&v, bytes.data() + i * 4, 4);
                    property.Ints[i] = v;
                }
                break;
            }
            case 'l': {
                property.Ints.resize(count);
                for (size_t i = 0; i < count; ++i)
                    std::memcpy(&property.Ints[i], bytes.data() + i * 8, 8);
                break;
            }
            case 'b': {
                property.Ints.resize(count);
                for (size_t i = 0; i < count; ++i) property.Ints[i] = bytes[i] ? 1 : 0;
                break;
            }
            default: break;
        }
    }

    Stream& stream_;
    bool wide_;
    std::string error_;
    int depth_ = 0;
};

} // namespace

bool LooksLikeFbxBinary(const std::string& head) {
    return head.size() >= 23 && head.compare(0, 20, "Kaydara FBX Binary  ") == 0;
}

bool LooksLikeFbxAscii(const std::string& head) {
    // The ASCII encoding opens with a comment banner, and always names itself
    // in it. Checking for the banner alone would claim any semicolon comment.
    if (head.find("Kaydara FBX Binary") != std::string::npos) return false;
    return head.find("FBXHeaderExtension") != std::string::npos ||
           (head.compare(0, 2, "; ") == 0 && head.find("FBX") != std::string::npos);
}

bool Parse(const std::vector<uint8_t>& data, File& out, std::string& error,
           const std::function<void(const std::string&)>& warn) {
    const std::string head(data.begin(),
                           data.begin() + static_cast<std::ptrdiff_t>(
                                                  data.size() < 64 ? data.size() : 64));
    if (LooksLikeFbxAscii(head)) {
        error = "FBX: this is the ASCII encoding, which this reader does not read. "
                "Re-export as binary FBX.";
        return false;
    }
    if (!LooksLikeFbxBinary(head)) {
        error = "FBX: not a binary FBX - the file does not begin 'Kaydara FBX Binary'";
        return false;
    }

    Stream stream(data.data(), data.size());
    stream.Seek(23);
    out.Version = stream.U32();
    if (!stream.Ok()) {
        error = "FBX: the file ends inside its header";
        return false;
    }
    if (out.Version < 7100) {
        error = "FBX: version " + std::to_string(out.Version) +
                " predates the 7.x binary layout this reader understands";
        return false;
    }
    if (out.Version > 7700 && warn)
        warn("FBX: version " + std::to_string(out.Version) +
             " is newer than this reader was written against; it is read as 7.5+");

    Parser parser(stream, out.Version);
    if (!parser.ReadList(out.Roots, data.size())) {
        error = "FBX: " + parser.Error();
        return false;
    }
    return true;
}

} // namespace Fbx
} // namespace UltraCanvas
