// core/UltraCanvasPropertyList.cpp
// Both property-list encodings, read without an Apple API: the XML form
// (through the tinyxml2 the framework already carries) and the binary
// "bplist00" form (an object table behind a 32-byte trailer, parsed here).
//
// Only the top-level dictionary is kept, and only its scalars — see the
// header for why. Every offset in a binary plist comes out of the file
// itself, so all of them are bounds-checked: a malformed file produces no
// values, never a read past the end.
// Version: 1.0.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework
#include "UltraCanvasPropertyList.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace UltraCanvas {

    namespace {

        // An Info.plist is a few kilobytes; the cap is against a file that
        // merely carries the name.
        constexpr uintmax_t kMaxPropertyListBytes = 8 * 1024 * 1024;
        // A binary plist is a graph of object references; the depth guard
        // stops one that points back at itself from becoming a walk.
        constexpr int kMaxBinaryDepth = 8;

        bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
            std::error_code ec;
            if (!fs::is_regular_file(path, ec) || ec) return false;
            const uintmax_t size = fs::file_size(path, ec);
            if (ec || size == 0 || size > kMaxPropertyListBytes) return false;
            std::ifstream in(path, std::ios::binary);
            if (!in) return false;
            out.resize(static_cast<size_t>(size));
            in.read(reinterpret_cast<char*>(out.data()),
                    static_cast<std::streamsize>(out.size()));
            return static_cast<size_t>(in.gcount()) == out.size();
        }

        void AppendUtf8(std::string& out, uint32_t cp) {
            if (cp < 0x80) {
                out += static_cast<char>(cp);
            } else if (cp < 0x800) {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        // ===== THE XML FORM =====
        // <plist><dict><key>K</key><string>V</string>…</dict></plist>, with
        // nested containers skipped: a <dict> or <array> under a key is not a
        // scalar and this reader does not model it.
        bool ParseXml(const std::vector<uint8_t>& bytes,
                      std::map<std::string, std::string>& values) {
            tinyxml2::XMLDocument document;
            if (document.Parse(reinterpret_cast<const char*>(bytes.data()),
                               bytes.size()) != tinyxml2::XML_SUCCESS)
                return false;
            const tinyxml2::XMLElement* plist =
                    document.FirstChildElement("plist");
            const tinyxml2::XMLElement* dict =
                    plist ? plist->FirstChildElement("dict")
                          : document.FirstChildElement("dict");
            if (!dict) return false;
            for (const tinyxml2::XMLElement* node = dict->FirstChildElement();
                 node; node = node->NextSiblingElement()) {
                if (std::strcmp(node->Name(), "key") != 0) continue;
                const char* keyText = node->GetText();
                const tinyxml2::XMLElement* value = node->NextSiblingElement();
                if (!keyText || !value) continue;
                const std::string name = value->Name();
                if (name == "dict" || name == "array") continue;   // not a scalar
                std::string text;
                if (name == "true")       text = "true";
                else if (name == "false") text = "false";
                else if (const char* raw = value->GetText()) text = raw;
                values[keyText] = text;
            }
            return true;
        }

        // ===== THE BINARY FORM =====
        struct BinaryReader {
            const std::vector<uint8_t>& bytes;
            size_t offsetSize = 0;      // bytes per offset-table entry
            size_t refSize = 0;         // bytes per object reference
            size_t objectCount = 0;
            size_t offsetTable = 0;

            uint64_t Integer(size_t at, size_t size) const {
                if (size == 0 || at + size > bytes.size()) return 0;
                uint64_t value = 0;
                for (size_t i = 0; i < size; ++i)
                    value = (value << 8) | bytes[at + i];
                return value;
            }

            // Where object `index` starts.
            size_t ObjectOffset(size_t index) const {
                if (index >= objectCount) return bytes.size();
                return static_cast<size_t>(
                        Integer(offsetTable + index * offsetSize, offsetSize));
            }

            // The count a marker's low nibble carries: 0xF means the next
            // object is an integer holding it.
            bool Count(size_t at, size_t& outCount, size_t& outNext) const {
                if (at >= bytes.size()) return false;
                const uint8_t low = bytes[at] & 0x0F;
                if (low != 0x0F) {
                    outCount = low;
                    outNext = at + 1;
                    return true;
                }
                if (at + 1 >= bytes.size()) return false;
                const uint8_t marker = bytes[at + 1];
                if ((marker & 0xF0) != 0x10) return false;
                const size_t width = static_cast<size_t>(1) << (marker & 0x0F);
                outCount = static_cast<size_t>(Integer(at + 2, width));
                outNext = at + 2 + width;
                return outNext <= bytes.size();
            }
        };

        // One object as text. Containers return false: they are not scalars.
        bool ScalarText(const BinaryReader& reader, size_t at, std::string& out,
                        int depth = 0) {
            if (depth > kMaxBinaryDepth || at >= reader.bytes.size()) return false;
            const uint8_t marker = reader.bytes[at];
            const uint8_t type = marker & 0xF0;
            switch (type) {
                case 0x00:
                    if (marker == 0x08) { out = "false"; return true; }
                    if (marker == 0x09) { out = "true"; return true; }
                    return false;
                case 0x10: {   // integer
                    const size_t width = static_cast<size_t>(1) << (marker & 0x0F);
                    out = std::to_string(
                            static_cast<long long>(reader.Integer(at + 1, width)));
                    return true;
                }
                case 0x20: {   // real
                    const size_t width = static_cast<size_t>(1) << (marker & 0x0F);
                    const uint64_t raw = reader.Integer(at + 1, width);
                    double value = 0;
                    if (width == 4) {
                        const uint32_t bits = static_cast<uint32_t>(raw);
                        float f = 0;
                        std::memcpy(&f, &bits, sizeof(f));
                        value = f;
                    } else if (width == 8) {
                        std::memcpy(&value, &raw, sizeof(value));
                    } else {
                        return false;
                    }
                    out = std::to_string(value);
                    return true;
                }
                case 0x50: {   // ASCII string
                    size_t count = 0, next = 0;
                    if (!reader.Count(at, count, next)) return false;
                    if (next + count > reader.bytes.size()) return false;
                    out.assign(reader.bytes.begin() + next,
                               reader.bytes.begin() + next + count);
                    return true;
                }
                case 0x60: {   // UTF-16BE string
                    size_t count = 0, next = 0;
                    if (!reader.Count(at, count, next)) return false;
                    if (next + count * 2 > reader.bytes.size()) return false;
                    out.clear();
                    for (size_t i = 0; i < count; ++i) {
                        uint32_t unit = static_cast<uint32_t>(
                                (reader.bytes[next + i * 2] << 8) |
                                reader.bytes[next + i * 2 + 1]);
                        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < count) {
                            const uint32_t low = static_cast<uint32_t>(
                                    (reader.bytes[next + (i + 1) * 2] << 8) |
                                    reader.bytes[next + (i + 1) * 2 + 1]);
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                unit = 0x10000 + ((unit - 0xD800) << 10) +
                                       (low - 0xDC00);
                                ++i;
                            }
                        }
                        AppendUtf8(out, unit);
                    }
                    return true;
                }
                default:
                    return false;   // data, date, uid, array, dict, set
            }
        }

        bool ParseBinary(const std::vector<uint8_t>& bytes,
                         std::map<std::string, std::string>& values) {
            if (bytes.size() < 8 + 32) return false;
            if (std::memcmp(bytes.data(), "bplist00", 8) != 0) return false;

            BinaryReader reader{bytes};
            const size_t trailer = bytes.size() - 32;
            reader.offsetSize = bytes[trailer + 6];
            reader.refSize = bytes[trailer + 7];
            if (reader.offsetSize == 0 || reader.offsetSize > 8 ||
                reader.refSize == 0 || reader.refSize > 8)
                return false;
            reader.objectCount =
                    static_cast<size_t>(reader.Integer(trailer + 8, 8));
            const size_t topObject =
                    static_cast<size_t>(reader.Integer(trailer + 16, 8));
            reader.offsetTable =
                    static_cast<size_t>(reader.Integer(trailer + 24, 8));
            if (reader.objectCount == 0 || topObject >= reader.objectCount)
                return false;
            if (reader.offsetTable +
                        reader.objectCount * reader.offsetSize > bytes.size())
                return false;

            const size_t dictAt = reader.ObjectOffset(topObject);
            if (dictAt >= bytes.size() || (bytes[dictAt] & 0xF0) != 0xD0)
                return false;   // the top object is not a dictionary
            size_t count = 0, next = 0;
            if (!reader.Count(dictAt, count, next)) return false;
            if (next + count * reader.refSize * 2 > bytes.size()) return false;

            for (size_t i = 0; i < count; ++i) {
                const size_t keyRef = static_cast<size_t>(reader.Integer(
                        next + i * reader.refSize, reader.refSize));
                const size_t valueRef = static_cast<size_t>(reader.Integer(
                        next + (count + i) * reader.refSize, reader.refSize));
                if (keyRef >= reader.objectCount || valueRef >= reader.objectCount)
                    continue;
                std::string key, value;
                if (!ScalarText(reader, reader.ObjectOffset(keyRef), key)) continue;
                if (!ScalarText(reader, reader.ObjectOffset(valueRef), value))
                    continue;   // a nested container: not a scalar, skipped
                values[key] = value;
            }
            return true;
        }

    } // namespace

    bool UCPropertyList::ReadBytes(const uint8_t* data, size_t size,
                                   UCPropertyList& out) {
        if (!data || size == 0) return false;
        const std::vector<uint8_t> bytes(data, data + size);
        std::map<std::string, std::string> values;
        const bool ok = (size >= 8 && std::memcmp(data, "bplist00", 8) == 0)
                                ? ParseBinary(bytes, values)
                                : ParseXml(bytes, values);
        if (!ok) return false;
        out.values = std::move(values);
        return true;
    }

    bool UCPropertyList::Read(const std::string& path, UCPropertyList& out) {
        std::vector<uint8_t> bytes;
        if (!ReadWholeFile(path, bytes)) return false;
        return ReadBytes(bytes.data(), bytes.size(), out);
    }

    bool UCPropertyList::Has(const std::string& key) const {
        return values.find(key) != values.end();
    }

    std::string UCPropertyList::GetString(const std::string& key,
                                          const std::string& fallback) const {
        auto it = values.find(key);
        return it == values.end() || it->second.empty() ? fallback : it->second;
    }

    bool UCPropertyList::GetBool(const std::string& key, bool fallback) const {
        auto it = values.find(key);
        if (it == values.end()) return fallback;
        return it->second == "true" || it->second == "1" ||
               it->second == "YES" || it->second == "yes";
    }

    long long UCPropertyList::GetInteger(const std::string& key,
                                         long long fallback) const {
        auto it = values.find(key);
        if (it == values.end() || it->second.empty()) return fallback;
        try {
            return std::stoll(it->second);
        } catch (const std::exception&) {
            return fallback;
        }
    }

} // namespace UltraCanvas
