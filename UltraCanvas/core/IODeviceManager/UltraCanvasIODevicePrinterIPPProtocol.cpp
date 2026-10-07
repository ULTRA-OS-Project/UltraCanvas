// core/IODeviceManager/UltraCanvasIODevicePrinterIPPProtocol.cpp
// IPP encoding, decoding and the mapping between IPP attributes and this
// module's printer vocabulary. See the header for what is here and why.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS

#include "IODeviceManager/UltraCanvasIODevicePrinterIPPProtocol.h"
#include "IODeviceManager/UltraCanvasIODeviceDnsSd.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace UltraCanvas {

namespace {

// ============================================================================
// SMALL HELPERS
// ============================================================================

char LowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string Lower(std::string text) {
    for (char& c : text) c = LowerAscii(c);
    return text;
}

bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (LowerAscii(a[i]) != LowerAscii(b[i])) return false;
    }
    return true;
}

bool StartsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() &&
           text.compare(0, prefix.size(), prefix) == 0;
}

bool IsDigits(const std::string& text) {
    if (text.empty()) return false;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

// A non-negative decimal integer, or -1. For numbers in URIs and in attribute
// strings, which are never locale-formatted.
long ParseCount(const std::string& text) {
    if (!IsDigits(text) || text.size() > 9) return -1;
    return std::strtol(text.c_str(), nullptr, 10);
}

bool IsStringTag(IppTag tag) {
    switch (tag) {
        case IppTag::OctetString:
        case IppTag::DateTime:
        case IppTag::TextWithLanguage:
        case IppTag::NameWithLanguage:
        case IppTag::Text:
        case IppTag::Name:
        case IppTag::Keyword:
        case IppTag::Uri:
        case IppTag::UriScheme:
        case IppTag::Charset:
        case IppTag::NaturalLanguage:
        case IppTag::MimeMediaType:
        case IppTag::MemberAttrName:
            return true;
        default:
            return false;
    }
}

bool IsIntegerTag(IppTag tag) {
    return tag == IppTag::Integer || tag == IppTag::Enum || tag == IppTag::Boolean;
}

// ============================================================================
// WRITING
// ============================================================================

void PutU8(std::vector<uint8_t>& out, uint8_t value) { out.push_back(value); }

void PutU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void PutI32(std::vector<uint8_t>& out, int32_t value) {
    const uint32_t bits = static_cast<uint32_t>(value);
    out.push_back(static_cast<uint8_t>(bits >> 24));
    out.push_back(static_cast<uint8_t>((bits >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((bits >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(bits & 0xFF));
}

// A length-prefixed string. Truncated at 32767 bytes, the most RFC 8010
// allows a value to be; nothing this side sends comes near it.
void PutString(std::vector<uint8_t>& out, const std::string& text) {
    const size_t length = std::min<size_t>(text.size(), 32767);
    PutU16(out, static_cast<uint16_t>(length));
    out.insert(out.end(), text.begin(), text.begin() + static_cast<long>(length));
}

void PutValue(std::vector<uint8_t>& out, const std::string& name,
              const IppValue& value);

// A collection: a begin tag carrying the attribute's name, then each member
// as a memberAttrName value naming it followed by its values (nameless), then
// an end tag.
void PutCollection(std::vector<uint8_t>& out, const std::string& name,
                   const IppValue& value) {
    PutU8(out, static_cast<uint8_t>(IppTag::BeginCollection));
    PutString(out, name);
    PutU16(out, 0);

    for (const IppAttribute& member : value.members) {
        PutU8(out, static_cast<uint8_t>(IppTag::MemberAttrName));
        PutU16(out, 0);
        PutString(out, member.name);
        for (const IppValue& memberValue : member.values) {
            PutValue(out, std::string(), memberValue);
        }
    }

    PutU8(out, static_cast<uint8_t>(IppTag::EndCollection));
    PutU16(out, 0);
    PutU16(out, 0);
}

void PutValue(std::vector<uint8_t>& out, const std::string& name,
              const IppValue& value) {
    if (value.tag == IppTag::BeginCollection) {
        PutCollection(out, name, value);
        return;
    }

    PutU8(out, static_cast<uint8_t>(value.tag));
    PutString(out, name);

    switch (value.tag) {
        case IppTag::Integer:
        case IppTag::Enum:
            PutU16(out, 4);
            PutI32(out, value.integer);
            break;
        case IppTag::Boolean:
            PutU16(out, 1);
            PutU8(out, value.integer ? 1 : 0);
            break;
        case IppTag::RangeOfInteger:
            PutU16(out, 8);
            PutI32(out, value.integer);
            PutI32(out, value.upper);
            break;
        case IppTag::Resolution:
            PutU16(out, 9);
            PutI32(out, value.integer);
            PutI32(out, value.upper);
            PutU8(out, value.units);
            break;
        case IppTag::TextWithLanguage:
        case IppTag::NameWithLanguage: {
            // Two length-prefixed strings inside the value's own length.
            const std::string language = "en";
            const size_t textLength = std::min<size_t>(value.text.size(), 32000);
            PutU16(out, static_cast<uint16_t>(4 + language.size() + textLength));
            PutString(out, language);
            PutString(out, value.text.substr(0, textLength));
            break;
        }
        case IppTag::Unsupported:
        case IppTag::Unknown:
        case IppTag::NoValue:
            PutU16(out, 0);
            break;
        default:
            PutString(out, value.text);
            break;
    }
}

// ============================================================================
// READING
// ============================================================================

class Reader {
public:
    Reader(const uint8_t* bytes, size_t length) : data(bytes), size(length) {}

    bool Remaining(size_t count) const { return size - offset >= count; }
    size_t Offset() const { return offset; }

    bool U8(uint8_t& value) {
        if (!Remaining(1)) return false;
        value = data[offset++];
        return true;
    }

    bool U16(uint16_t& value) {
        if (!Remaining(2)) return false;
        value = static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
        offset += 2;
        return true;
    }

    bool Bytes(size_t count, std::string& value) {
        if (!Remaining(count)) return false;
        value.assign(reinterpret_cast<const char*>(data + offset), count);
        offset += count;
        return true;
    }

    // Looks at the next byte without consuming it.
    bool Peek(uint8_t& value) const {
        if (!Remaining(1)) return false;
        value = data[offset];
        return true;
    }

private:
    const uint8_t* data;
    size_t size;
    size_t offset = 0;
};

int32_t ReadI32(const std::string& bytes, size_t at) {
    const uint32_t bits = (static_cast<uint32_t>(static_cast<uint8_t>(bytes[at])) << 24) |
                          (static_cast<uint32_t>(static_cast<uint8_t>(bytes[at + 1])) << 16) |
                          (static_cast<uint32_t>(static_cast<uint8_t>(bytes[at + 2])) << 8) |
                          static_cast<uint32_t>(static_cast<uint8_t>(bytes[at + 3]));
    return static_cast<int32_t>(bits);
}

// One tag-name-value triple as it sits on the wire.
struct RawEntry {
    uint8_t tag = 0;
    std::string name;
    std::string value;
};

bool ReadEntry(Reader& reader, uint8_t tag, RawEntry& entry, std::string& error) {
    entry.tag = tag;
    uint16_t nameLength = 0;
    if (!reader.U16(nameLength) || !reader.Bytes(nameLength, entry.name)) {
        error = "an attribute name runs past the end of the message";
        return false;
    }
    uint16_t valueLength = 0;
    if (!reader.U16(valueLength) || !reader.Bytes(valueLength, entry.value)) {
        error = "the value of '" + entry.name + "' runs past the end of the message";
        return false;
    }
    return true;
}

// Turns a raw (non-collection) value into an IppValue, checking that its
// length is the one its type fixes.
bool ConvertValue(const RawEntry& entry, IppValue& value, std::string& error) {
    value = IppValue();
    value.tag = static_cast<IppTag>(entry.tag);
    const std::string& bytes = entry.value;

    auto wrongLength = [&](size_t wanted) {
        error = "a value of '" + entry.name + "' is " + std::to_string(bytes.size()) +
                " bytes where its type needs " + std::to_string(wanted);
        return false;
    };

    switch (value.tag) {
        case IppTag::Integer:
        case IppTag::Enum:
            if (bytes.size() != 4) return wrongLength(4);
            value.integer = ReadI32(bytes, 0);
            return true;
        case IppTag::Boolean:
            if (bytes.size() != 1) return wrongLength(1);
            value.integer = bytes[0] ? 1 : 0;
            return true;
        case IppTag::RangeOfInteger:
            if (bytes.size() != 8) return wrongLength(8);
            value.integer = ReadI32(bytes, 0);
            value.upper = ReadI32(bytes, 4);
            return true;
        case IppTag::Resolution:
            if (bytes.size() != 9) return wrongLength(9);
            value.integer = ReadI32(bytes, 0);
            value.upper = ReadI32(bytes, 4);
            value.units = static_cast<uint8_t>(bytes[8]);
            return true;
        case IppTag::TextWithLanguage:
        case IppTag::NameWithLanguage: {
            // language-length, language, text-length, text - all inside
            // this value, so each length is checked against what is left.
            if (bytes.size() < 4) return wrongLength(4);
            const size_t languageLength =
                (static_cast<uint8_t>(bytes[0]) << 8) | static_cast<uint8_t>(bytes[1]);
            if (2 + languageLength + 2 > bytes.size()) return wrongLength(2 + languageLength + 2);
            const size_t at = 2 + languageLength;
            const size_t textLength =
                (static_cast<uint8_t>(bytes[at]) << 8) | static_cast<uint8_t>(bytes[at + 1]);
            if (at + 2 + textLength > bytes.size()) return wrongLength(at + 2 + textLength);
            value.text = bytes.substr(at + 2, textLength);
            return true;
        }
        default:
            // Strings, octet strings, date-times, and the out-of-band tags
            // (whose value is empty). An unknown tag is kept as bytes rather
            // than refused: RFC 8010 lets a printer add types, and a reader
            // that stops at the first one it does not know would read nothing
            // from a printer newer than it.
            value.text = bytes;
            return true;
    }
}

bool ReadCollection(Reader& reader, const std::string& attributeName,
                    IppValue& collection, std::string& error, int depth);

// Reads the value an entry starts, recursing into a collection. `owner` is
// the attribute the value belongs to, for messages: a nested collection's own
// entry is nameless.
bool ReadValue(Reader& reader, const RawEntry& entry, const std::string& owner,
               IppValue& value, std::string& error, int depth) {
    if (entry.tag == static_cast<uint8_t>(IppTag::BeginCollection)) {
        return ReadCollection(reader, entry.name.empty() ? owner : entry.name, value, error,
                              depth + 1);
    }
    return ConvertValue(entry, value, error);
}

// The members of a collection, up to its end tag. `depth` bounds nesting so a
// reply built to recurse forever is refused instead of exhausting the stack.
bool ReadCollection(Reader& reader, const std::string& attributeName,
                    IppValue& collection, std::string& error, int depth) {
    if (depth > 16) {
        error = "collections in '" + attributeName + "' are nested too deeply";
        return false;
    }
    collection = IppValue();
    collection.tag = IppTag::BeginCollection;

    for (;;) {
        uint8_t tag = 0;
        if (!reader.U8(tag)) {
            error = "the collection '" + attributeName + "' is never closed";
            return false;
        }
        RawEntry entry;
        if (!ReadEntry(reader, tag, entry, error)) return false;

        if (tag == static_cast<uint8_t>(IppTag::EndCollection)) return true;

        if (tag == static_cast<uint8_t>(IppTag::MemberAttrName)) {
            IppAttribute member;
            member.name = entry.value;
            collection.members.push_back(std::move(member));
            continue;
        }

        if (collection.members.empty()) {
            error = "the collection '" + attributeName +
                    "' has a value before any member name";
            return false;
        }
        IppValue memberValue;
        if (!ReadValue(reader, entry, attributeName, memberValue, error, depth)) return false;
        collection.members.back().values.push_back(std::move(memberValue));
    }
}

// ============================================================================
// ATTRIBUTE MAPPING HELPERS
// ============================================================================

void AddUnique(std::vector<IOPaperSize>& sizes, IOPaperSize size) {
    if (size == IOPaperSize::Unknown) return;
    if (std::find(sizes.begin(), sizes.end(), size) == sizes.end()) sizes.push_back(size);
}

template <typename T>
void AddUniqueValue(std::vector<T>& list, T value) {
    if (std::find(list.begin(), list.end(), value) == list.end()) list.push_back(value);
}

// "all margins zero" on a printer that reports the four margin lists.
IOSupport BorderlessSupport(const IppGroup& printer) {
    const char* names[] = {"media-bottom-margin-supported", "media-left-margin-supported",
                           "media-right-margin-supported", "media-top-margin-supported"};
    bool anyReported = false;
    for (const char* name : names) {
        const IppAttribute* margins = printer.Find(name);
        if (!margins) continue;
        anyReported = true;
        if (!margins->ContainsInteger(0)) return IOSupport::No;
    }
    return anyReported ? IOSupport::Yes : IOSupport::Unknown;
}

IOSupplyType SupplyTypeFromKeyword(const std::string& keyword) {
    const std::string type = Lower(keyword);
    if (type.find("waste") != std::string::npos)    return IOSupplyType::WasteTank;
    if (type.find("toner") != std::string::npos)    return IOSupplyType::Toner;
    if (type.find("ink") != std::string::npos)      return IOSupplyType::Ink;
    if (type.find("opc") != std::string::npos ||
        type.find("drum") != std::string::npos ||
        type.find("developer") != std::string::npos) return IOSupplyType::Drum;
    if (type.find("fuser") != std::string::npos)    return IOSupplyType::Fuser;
    if (type.find("staple") != std::string::npos)   return IOSupplyType::Staples;
    return IOSupplyType::Unknown;
}

// marker-colors are "#RRGGBB", the same form CUPS relays. Recognised by value,
// not by the marker's name, which is localised and vendor-chosen.
IOSupplyColor SupplyColorFromHex(const std::string& hex) {
    if (hex.size() < 7 || hex[0] != '#') return IOSupplyColor::None;
    const std::string rgb = Lower(hex.substr(1, 6));
    if (rgb == "000000") return IOSupplyColor::Black;
    if (rgb == "00ffff") return IOSupplyColor::Cyan;
    if (rgb == "ff00ff") return IOSupplyColor::Magenta;
    if (rgb == "ffff00") return IOSupplyColor::Yellow;
    if (rgb == "e0ffff") return IOSupplyColor::LightCyan;
    if (rgb == "ffe0ff") return IOSupplyColor::LightMagenta;
    if (rgb == "808080") return IOSupplyColor::Gray;
    if (rgb == "ff0000") return IOSupplyColor::Red;
    if (rgb == "0000ff") return IOSupplyColor::Blue;
    return IOSupplyColor::None;
}

// printer-supply's colorantname: "black", "cyan", "light-cyan", ...
IOSupplyColor SupplyColorFromName(const std::string& name) {
    std::string colour;
    for (char c : Lower(name)) {
        if (c != '-' && c != '_' && c != ' ') colour += c;
    }
    if (colour == "black")        return IOSupplyColor::Black;
    if (colour == "cyan")         return IOSupplyColor::Cyan;
    if (colour == "magenta")      return IOSupplyColor::Magenta;
    if (colour == "yellow")       return IOSupplyColor::Yellow;
    if (colour == "lightcyan")    return IOSupplyColor::LightCyan;
    if (colour == "lightmagenta") return IOSupplyColor::LightMagenta;
    if (colour == "lightblack" || colour == "lightgray" || colour == "lightgrey")
        return IOSupplyColor::LightBlack;
    if (colour == "gray" || colour == "grey") return IOSupplyColor::Gray;
    if (colour == "red")          return IOSupplyColor::Red;
    if (colour == "blue")         return IOSupplyColor::Blue;
    if (colour == "photoblack")   return IOSupplyColor::PhotoBlack;
    if (colour == "matteblack")   return IOSupplyColor::MatteBlack;
    return IOSupplyColor::None;
}

// "key=value;key=value;" as printer-supply carries it.
std::string SupplyField(const std::string& entry, const std::string& key) {
    std::istringstream fields(entry);
    std::string field;
    while (std::getline(fields, field, ';')) {
        const size_t equals = field.find('=');
        if (equals == std::string::npos) continue;
        if (EqualsIgnoreCase(field.substr(0, equals), key)) return field.substr(equals + 1);
    }
    return std::string();
}

// A signed decimal from printer-supply, where negative levels have meanings.
bool ParseSigned(const std::string& text, long& value) {
    if (text.empty()) return false;
    const bool negative = text[0] == '-';
    const long magnitude = ParseCount(negative ? text.substr(1) : text);
    if (magnitude < 0) return false;
    value = negative ? -magnitude : magnitude;
    return true;
}

// Percent-decoding for the instance name inside a CUPS device-uri.
std::string PercentDecode(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const std::string hex = text.substr(i + 1, 2);
            char* end = nullptr;
            const long byte = std::strtol(hex.c_str(), &end, 16);
            if (end == hex.c_str() + 2) {
                out.push_back(static_cast<char>(byte));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

struct UriParts {
    std::string scheme;
    std::string host;
    int port = -1;
    std::string path;       // without the leading '/', without the query
    std::string query;
};

// Just enough URI parsing for the forms printers and CUPS use: scheme, host
// (a bracketed IPv6 literal allowed), optional port, path, query.
bool SplitUri(const std::string& uri, UriParts& parts) {
    const size_t schemeEnd = uri.find("://");
    if (schemeEnd == std::string::npos) return false;
    parts.scheme = Lower(uri.substr(0, schemeEnd));

    const size_t authorityStart = schemeEnd + 3;
    const size_t pathStart = uri.find_first_of("/?", authorityStart);
    const std::string authority = uri.substr(authorityStart, pathStart == std::string::npos
                                                                 ? std::string::npos
                                                                 : pathStart - authorityStart);
    std::string rest = pathStart == std::string::npos ? std::string() : uri.substr(pathStart);

    size_t portColon = std::string::npos;
    if (!authority.empty() && authority[0] == '[') {
        const size_t close = authority.find(']');
        if (close == std::string::npos) return false;
        parts.host = authority.substr(0, close + 1);
        if (close + 1 < authority.size() && authority[close + 1] == ':') portColon = close + 1;
    } else {
        portColon = authority.rfind(':');
        parts.host = authority.substr(0, portColon);
    }
    if (portColon != std::string::npos) {
        const long port = ParseCount(authority.substr(portColon + 1));
        if (port < 0 || port > 65535) return false;
        parts.port = static_cast<int>(port);
    }
    if (parts.host.empty()) return false;

    const size_t question = rest.find('?');
    if (question != std::string::npos) {
        parts.query = rest.substr(question + 1);
        rest = rest.substr(0, question);
    }
    while (!rest.empty() && rest.front() == '/') rest.erase(0, 1);
    while (!rest.empty() && rest.back() == '/') rest.pop_back();
    parts.path = rest;
    return true;
}

int DefaultPortFor(const std::string& scheme) {
    if (scheme == "ipp" || scheme == "ipps") return 631;
    if (scheme == "https") return 443;
    if (scheme == "http") return 80;
    return -1;
}

// The service types an IPP printer advertises under, as a DNS-SD name
// carries them.
const std::vector<std::string> kIppServiceTypes = {"._ipp._tcp", "._ipps._tcp"};

// "Office Printer._ipp._tcp.local" -> "Office Printer". Empty for a host
// name that is not a DNS-SD service name.
std::string InstanceFromServiceHost(const std::string& host) {
    const size_t at = DnsSdServiceTypeStart(host, kIppServiceTypes);
    return at == std::string::npos ? std::string() : host.substr(0, at);
}

std::string StripUuidPrefix(const std::string& uuid) {
    return StartsWith(Lower(uuid), "urn:uuid:") ? uuid.substr(9) : uuid;
}

// The keyword IPP's media-type registry (PWG 5100.7) uses for each media type
// that has one, read in both directions. FineArt and Canvas have no entry:
// there is no registered keyword that means them, and a near miss would be
// acted on by the printer.
struct MediaTypeEntry {
    IOMediaType type;
    const char* keyword;
};

const MediaTypeEntry kMediaTypes[] = {
    {IOMediaType::Plain, "stationery"},
    {IOMediaType::Letterhead, "stationery-letterhead"},
    {IOMediaType::Recycled, "stationery-recycled"},
    {IOMediaType::Coated, "stationery-coated"},
    {IOMediaType::Inkjet, "stationery-inkjet"},
    {IOMediaType::PhotoMatte, "photographic-matte"},
    {IOMediaType::PhotoSemiGloss, "photographic-semi-gloss"},
    {IOMediaType::PhotoGlossy, "photographic-glossy"},
    {IOMediaType::PhotoLuster, "photographic-satin"},
    {IOMediaType::PhotoPremiumGlossy, "photographic-high-gloss"},
    {IOMediaType::Transparency, "transparency"},
    {IOMediaType::Envelope, "envelope"},
    {IOMediaType::CardStock, "cardstock"},
    {IOMediaType::Label, "labels"},
    {IOMediaType::CDDVD, "disc"},
};

std::string LowerExtension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return std::string();
    const size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos && dot < slash) return std::string();
    return Lower(path.substr(dot + 1));
}

bool IsSinglePageImage(const std::string& type) {
    return StartsWith(type, "image/") && type != "image/pwg-raster" && type != "image/urf" &&
           type != "image/tiff";
}

// Types the page sources can draw: plain text, and any image the imaging
// stack decodes - which is every image type except the two multi-page
// printer rasters, which are already pages and need no drawing.
bool CanDrawHere(const std::string& type) {
    if (type == "text/plain" || type == kIppDrawnPagesType) return true;
    return StartsWith(type, "image/") && type != "image/pwg-raster" && type != "image/urf";
}

std::string JoinList(const std::vector<std::string>& items) {
    std::string joined;
    for (const std::string& item : items) {
        if (!joined.empty()) joined += ", ";
        joined += item;
    }
    return joined;
}

}  // namespace

// ============================================================================
// VALUES AND ATTRIBUTES
// ============================================================================

IppValue IppValue::Integer(int32_t value) {
    IppValue v; v.tag = IppTag::Integer; v.integer = value; return v;
}
IppValue IppValue::Enum(int32_t value) {
    IppValue v; v.tag = IppTag::Enum; v.integer = value; return v;
}
IppValue IppValue::Boolean(bool value) {
    IppValue v; v.tag = IppTag::Boolean; v.integer = value ? 1 : 0; return v;
}
IppValue IppValue::Keyword(const std::string& value) {
    IppValue v; v.tag = IppTag::Keyword; v.text = value; return v;
}
IppValue IppValue::Name(const std::string& value) {
    IppValue v; v.tag = IppTag::Name; v.text = value; return v;
}
IppValue IppValue::Text(const std::string& value) {
    IppValue v; v.tag = IppTag::Text; v.text = value; return v;
}
IppValue IppValue::Uri(const std::string& value) {
    IppValue v; v.tag = IppTag::Uri; v.text = value; return v;
}
IppValue IppValue::Charset(const std::string& value) {
    IppValue v; v.tag = IppTag::Charset; v.text = value; return v;
}
IppValue IppValue::Language(const std::string& value) {
    IppValue v; v.tag = IppTag::NaturalLanguage; v.text = value; return v;
}
IppValue IppValue::MimeType(const std::string& value) {
    IppValue v; v.tag = IppTag::MimeMediaType; v.text = value; return v;
}
IppValue IppValue::Range(int32_t lower, int32_t upperBound) {
    IppValue v; v.tag = IppTag::RangeOfInteger; v.integer = lower; v.upper = upperBound;
    return v;
}
IppValue IppValue::Dpi(int32_t crossFeed, int32_t feed) {
    IppValue v; v.tag = IppTag::Resolution; v.integer = crossFeed; v.upper = feed; v.units = 3;
    return v;
}
IppValue IppValue::Collection(std::vector<IppAttribute> members) {
    IppValue v; v.tag = IppTag::BeginCollection; v.members = std::move(members); return v;
}

bool IppValue::IsString() const { return IsStringTag(tag); }

IppAttribute::IppAttribute(std::string attributeName, IppValue value)
    : name(std::move(attributeName)) {
    values.push_back(std::move(value));
}

IppAttribute::IppAttribute(std::string attributeName, std::vector<IppValue> attributeValues)
    : name(std::move(attributeName)), values(std::move(attributeValues)) {}

const IppValue* IppAttribute::First() const {
    return values.empty() ? nullptr : &values.front();
}

std::vector<std::string> IppAttribute::Strings() const {
    std::vector<std::string> strings;
    for (const IppValue& value : values) {
        if (value.IsString()) strings.push_back(value.text);
    }
    return strings;
}

std::string IppAttribute::String() const {
    const IppValue* first = First();
    return (first && first->IsString()) ? first->text : std::string();
}

int32_t IppAttribute::IntegerOr(int32_t fallback) const {
    const IppValue* first = First();
    return (first && IsIntegerTag(first->tag)) ? first->integer : fallback;
}

bool IppAttribute::BooleanOr(bool fallback) const {
    const IppValue* first = First();
    return (first && first->tag == IppTag::Boolean) ? first->integer != 0 : fallback;
}

bool IppAttribute::Contains(const std::string& text) const {
    for (const IppValue& value : values) {
        if (value.IsString() && EqualsIgnoreCase(value.text, text)) return true;
    }
    return false;
}

bool IppAttribute::ContainsInteger(int32_t wanted) const {
    for (const IppValue& value : values) {
        if (IsIntegerTag(value.tag) && value.integer == wanted) return true;
        if (value.tag == IppTag::RangeOfInteger && wanted >= value.integer &&
            wanted <= value.upper) {
            return true;
        }
    }
    return false;
}

const IppAttribute* IppAttribute::Member(const std::string& memberName,
                                         size_t valueIndex) const {
    if (valueIndex >= values.size()) return nullptr;
    for (const IppAttribute& member : values[valueIndex].members) {
        if (member.name == memberName) return &member;
    }
    return nullptr;
}

const IppAttribute* IppGroup::Find(const std::string& name) const {
    for (const IppAttribute& attribute : attributes) {
        if (attribute.name == name) return &attribute;
    }
    return nullptr;
}

void IppGroup::Set(const std::string& name, IppValue value) {
    std::vector<IppValue> values;
    values.push_back(std::move(value));
    Set(name, std::move(values));
}

void IppGroup::Set(const std::string& name, std::vector<IppValue> values) {
    for (IppAttribute& attribute : attributes) {
        if (attribute.name == name) {
            attribute.values = std::move(values);
            return;
        }
    }
    attributes.emplace_back(name, std::move(values));
}

IppGroup& IppMessage::AddGroup(IppTag tag) {
    IppGroup group;
    group.tag = tag;
    groups.push_back(std::move(group));
    return groups.back();
}

const IppGroup* IppMessage::FindGroup(IppTag tag) const {
    for (const IppGroup& group : groups) {
        if (group.tag == tag) return &group;
    }
    return nullptr;
}

IppGroup* IppMessage::FindGroup(IppTag tag) {
    for (IppGroup& group : groups) {
        if (group.tag == tag) return &group;
    }
    return nullptr;
}

std::vector<const IppGroup*> IppMessage::FindGroups(IppTag tag) const {
    std::vector<const IppGroup*> found;
    for (const IppGroup& group : groups) {
        if (group.tag == tag) found.push_back(&group);
    }
    return found;
}

const IppAttribute* IppMessage::Find(IppTag groupTag, const std::string& name) const {
    const IppGroup* group = FindGroup(groupTag);
    return group ? group->Find(name) : nullptr;
}

// ============================================================================
// ENCODE / DECODE
// ============================================================================

std::vector<uint8_t> EncodeIppMessage(const IppMessage& message) {
    std::vector<uint8_t> out;
    PutU8(out, message.versionMajor);
    PutU8(out, message.versionMinor);
    PutU16(out, message.code);
    PutI32(out, static_cast<int32_t>(message.requestId));

    for (const IppGroup& group : message.groups) {
        PutU8(out, static_cast<uint8_t>(group.tag));
        for (const IppAttribute& attribute : group.attributes) {
            // The first value carries the name; the rest follow with an
            // empty one, which is how the format says "another value of the
            // same attribute".
            bool first = true;
            for (const IppValue& value : attribute.values) {
                PutValue(out, first ? attribute.name : std::string(), value);
                first = false;
            }
        }
    }
    PutU8(out, static_cast<uint8_t>(IppTag::EndOfAttributes));
    return out;
}

bool DecodeIppMessage(const uint8_t* data, size_t size, IppMessage& out,
                      size_t* outConsumed, std::string* outError) {
    std::string error;
    auto fail = [&](const std::string& why) {
        if (outError) *outError = why;
        return false;
    };

    out = IppMessage();
    if (!data || size < 9) {
        return fail("the reply is too short to be an IPP message");
    }

    Reader reader(data, size);
    uint16_t code = 0;
    std::string requestId;
    reader.U8(out.versionMajor);
    reader.U8(out.versionMinor);
    reader.U16(code);
    reader.Bytes(4, requestId);
    out.code = code;
    out.requestId = static_cast<uint32_t>(ReadI32(requestId, 0));

    IppGroup* group = nullptr;
    IppAttribute* current = nullptr;

    for (;;) {
        uint8_t tag = 0;
        if (!reader.U8(tag)) return fail("the message ends without an end-of-attributes tag");

        if (tag == static_cast<uint8_t>(IppTag::EndOfAttributes)) break;

        if (tag < 0x10) {
            // A new group. Tags this side does not know (subscription,
            // event-notification, document) are kept as groups of their own
            // rather than refused.
            group = &out.AddGroup(static_cast<IppTag>(tag));
            current = nullptr;
            continue;
        }

        RawEntry entry;
        if (!ReadEntry(reader, tag, entry, error)) return fail(error);

        if (!group) return fail("an attribute appears before any attribute group");

        if (tag == static_cast<uint8_t>(IppTag::EndCollection) ||
            tag == static_cast<uint8_t>(IppTag::MemberAttrName)) {
            return fail("a collection member appears outside a collection");
        }

        IppValue value;
        const std::string owner =
            !entry.name.empty() ? entry.name : (current ? current->name : std::string());
        if (!ReadValue(reader, entry, owner, value, error, 0)) return fail(error);

        if (!entry.name.empty()) {
            group->attributes.emplace_back(entry.name, std::move(value));
            current = &group->attributes.back();
        } else if (current) {
            current->values.push_back(std::move(value));
        } else {
            return fail("a value has no attribute to belong to");
        }
    }

    if (outConsumed) *outConsumed = reader.Offset();
    return true;
}

// ============================================================================
// STATUS CODES
// ============================================================================

std::string IppStatusToString(uint16_t status) {
    switch (status) {
        case 0x0000: return "successful-ok";
        case 0x0001: return "successful-ok-ignored-or-substituted-attributes";
        case 0x0002: return "successful-ok-conflicting-attributes";
        case 0x0400: return "client-error-bad-request";
        case 0x0401: return "client-error-forbidden";
        case 0x0402: return "client-error-not-authenticated";
        case 0x0403: return "client-error-not-authorized";
        case 0x0404: return "client-error-not-possible";
        case 0x0405: return "client-error-timeout";
        case 0x0406: return "client-error-not-found";
        case 0x0407: return "client-error-gone";
        case 0x0408: return "client-error-request-entity-too-large";
        case 0x0409: return "client-error-request-value-too-long";
        case 0x040A: return "client-error-document-format-not-supported";
        case 0x040B: return "client-error-attributes-or-values-not-supported";
        case 0x040C: return "client-error-uri-scheme-not-supported";
        case 0x040D: return "client-error-charset-not-supported";
        case 0x040E: return "client-error-conflicting-attributes";
        case 0x040F: return "client-error-compression-not-supported";
        case 0x0410: return "client-error-compression-error";
        case 0x0411: return "client-error-document-format-error";
        case 0x0412: return "client-error-document-access-error";
        case 0x0500: return "server-error-internal-error";
        case 0x0501: return "server-error-operation-not-supported";
        case 0x0502: return "server-error-service-unavailable";
        case 0x0503: return "server-error-version-not-supported";
        case 0x0504: return "server-error-device-error";
        case 0x0505: return "server-error-temporary-error";
        case 0x0506: return "server-error-not-accepting-jobs";
        case 0x0507: return "server-error-busy";
        case 0x0508: return "server-error-job-canceled";
        case 0x0509: return "server-error-multiple-document-jobs-not-supported";
        case 0x050A: return "server-error-printer-is-deactivated";
        default: break;
    }
    static const char digits[] = "0123456789abcdef";
    std::string hex = "0x";
    for (int shift = 12; shift >= 0; shift -= 4) hex += digits[(status >> shift) & 0xF];
    return hex;
}

IODeviceResultCode IppStatusToResultCode(uint16_t status) {
    if (IppStatusSucceeded(status)) return IODeviceResultCode::Success;
    switch (status) {
        case 0x0400:
        case 0x0409:
            return IODeviceResultCode::InvalidArgument;
        case 0x0401:
        case 0x0402:
        case 0x0403:
            return IODeviceResultCode::AccessDenied;
        case 0x0404:
            return IODeviceResultCode::InvalidState;
        case 0x0405:
            return IODeviceResultCode::Timeout;
        case 0x0406:
        case 0x0407:
            return IODeviceResultCode::DeviceNotFound;
        case 0x0408:
        case 0x040A:
        case 0x040B:
        case 0x040C:
        case 0x040D:
        case 0x040E:
        case 0x040F:
        case 0x0501:
        case 0x0503:
        case 0x0509:
            return IODeviceResultCode::NotSupported;
        case 0x0410:
        case 0x0411:
        case 0x0412:
            return IODeviceResultCode::MediaError;
        case 0x0502:
        case 0x0505:
        case 0x0506:
        case 0x0507:
        case 0x050A:
            return IODeviceResultCode::DeviceBusy;
        case 0x0504:
            return IODeviceResultCode::HardwareError;
        case 0x0508:
            return IODeviceResultCode::Cancelled;
        default:
            return IODeviceResultCode::BackendError;
    }
}

IppMessage MakeIppRequest(IppOperation operation, uint32_t requestId,
                          const std::string& printerUri,
                          const std::string& userName) {
    IppMessage request;
    request.code = static_cast<uint16_t>(operation);
    request.requestId = requestId;

    IppGroup& group = request.AddGroup(IppTag::OperationGroup);
    group.attributes.emplace_back("attributes-charset", IppValue::Charset("utf-8"));
    group.attributes.emplace_back("attributes-natural-language", IppValue::Language("en"));
    group.attributes.emplace_back("printer-uri", IppValue::Uri(printerUri));
    if (!userName.empty()) {
        group.attributes.emplace_back("requesting-user-name", IppValue::Name(userName));
    }
    return request;
}

// ============================================================================
// ADDRESSES
// ============================================================================

std::string IppHttpUrlFor(const std::string& printerUri) {
    UriParts parts;
    if (!SplitUri(printerUri, parts)) return std::string();

    std::string scheme;
    if (parts.scheme == "ipp" || parts.scheme == "http") {
        scheme = "http";
    } else if (parts.scheme == "ipps" || parts.scheme == "https") {
        scheme = "https";
    } else {
        return std::string();
    }
    if (parts.scheme == "http" || parts.scheme == "https") return printerUri;

    const int port = parts.port > 0 ? parts.port : 631;
    std::string url = scheme + "://" + parts.host + ":" + std::to_string(port) + "/" + parts.path;
    if (!parts.query.empty()) url += "?" + parts.query;
    return url;
}

std::string IppNormalizePrinterUri(const std::string& address) {
    std::string text = address;
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(0, 1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    while (!text.empty() && text.back() == '/') text.pop_back();

    UriParts parts;
    if (!SplitUri(text, parts)) return std::string();

    std::string scheme;
    if (parts.scheme == "ipp" || parts.scheme == "http") scheme = "ipp";
    else if (parts.scheme == "ipps" || parts.scheme == "https") scheme = "ipps";
    else return std::string();

    // An http(s) URL without a port meant that scheme's port, not IPP's 631.
    int port = parts.port;
    if (port <= 0 && (parts.scheme == "http" || parts.scheme == "https")) {
        port = DefaultPortFor(parts.scheme);
    }

    std::string uri = scheme + "://" + parts.host;
    if (port > 0) uri += ":" + std::to_string(port);
    uri += "/" + parts.path;
    if (!parts.query.empty()) uri += "?" + parts.query;
    return uri;
}

namespace {

// "10.0.0.5" and nothing else: four decimal parts, each 0-255.
bool IsDottedIPv4(const std::string& text) {
    int parts = 0;
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find('.', at);
        if (end == std::string::npos) end = text.size();
        const std::string part = text.substr(at, end - at);
        if (part.empty() || part.size() > 3) return false;
        for (char c : part) {
            if (c < '0' || c > '9') return false;
        }
        if (std::stoi(part) > 255) return false;
        ++parts;
        at = end + 1;
    }
    return parts == 4;
}

// A host as a port monitor reports it: a name or an address, nothing that
// would change the shape of a URI around it.
bool IsPlainHost(const std::string& host) {
    if (host.empty()) return false;
    for (char c : host) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '.' || c == '-' || c == '_' || c == ':')) return false;
    }
    return true;
}

} // namespace

std::vector<std::string> IppUrisForWindowsPort(const std::string& portName,
                                               const std::string& hostAddress) {
    std::string port = portName;
    while (!port.empty() && std::isspace(static_cast<unsigned char>(port.front()))) port.erase(0, 1);
    while (!port.empty() && std::isspace(static_cast<unsigned char>(port.back()))) port.pop_back();

    // An IPP port is already an address.
    const std::string asUri = IppNormalizePrinterUri(port);
    if (!asUri.empty()) return {asUri};

    std::string host = hostAddress;
    while (!host.empty() && std::isspace(static_cast<unsigned char>(host.front()))) host.erase(0, 1);
    while (!host.empty() && std::isspace(static_cast<unsigned char>(host.back()))) host.pop_back();
    if (!IsPlainHost(host)) {
        host.clear();
        // Only a name that *is* an address is read as one: "IP_10.0.0.5" and
        // "10.0.0.5_1" are how Windows names the ports it creates, but a word
        // could be a USB port or a fax as easily as a host.
        std::string name = port;
        if (name.size() > 3 && EqualsIgnoreCase(name.substr(0, 3), "IP_")) name.erase(0, 3);
        const size_t underscore = name.rfind('_');
        if (underscore != std::string::npos && underscore + 1 < name.size() &&
            name.find_first_not_of("0123456789", underscore + 1) == std::string::npos) {
            name.erase(underscore);
        }
        if (IsDottedIPv4(name)) host = name;
    }
    if (host.empty()) return {};

    // A bare IPv6 literal needs brackets to survive in a URI.
    if (host.find(':') != std::string::npos && host.front() != '[') host = "[" + host + "]";
    const std::string base = "ipp://" + host + ":631";
    return {base + "/ipp/print", base + "/ipp", base + "/"};
}

std::string IppTxtValue(const std::vector<std::string>& txt, const std::string& key) {
    for (const std::string& record : txt) {
        const size_t equals = record.find('=');
        const std::string recordKey = record.substr(0, equals);
        if (EqualsIgnoreCase(recordKey, key)) {
            return equals == std::string::npos ? std::string() : record.substr(equals + 1);
        }
    }
    return std::string();
}

std::string IppUriFromMdns(const std::string& host, int port,
                           const std::vector<std::string>& txt, bool useTls) {
    if (host.empty() || port <= 0 || port > 65535) return std::string();

    std::string trimmedHost = host;
    while (!trimmedHost.empty() && trimmedHost.back() == '.') trimmedHost.pop_back();
    // A bare IPv6 literal needs brackets to survive in a URI.
    if (trimmedHost.find(':') != std::string::npos && trimmedHost.front() != '[') {
        trimmedHost = "[" + trimmedHost + "]";
    }

    std::string path = IppTxtValue(txt, "rp");
    while (!path.empty() && path.front() == '/') path.erase(0, 1);

    return std::string(useTls ? "ipps://" : "ipp://") + trimmedHost + ":" +
           std::to_string(port) + "/" + path;
}

std::string IppInstanceFromServiceName(const std::string& serviceName) {
    return DnsSdInstanceName(serviceName, kIppServiceTypes);
}

std::string IppDeviceIdFor(const std::string& uuid, const std::string& printerUri) {
    const std::string bare = StripUuidPrefix(uuid);
    if (!bare.empty()) return "urn:uuid:" + Lower(bare);
    return "ipp:" + printerUri;
}

bool IppCupsQueueReachesPrinter(const std::string& cupsDeviceUri,
                                const std::string& serviceName,
                                const std::string& uuid,
                                const std::string& printerUri) {
    UriParts queue;
    if (!SplitUri(cupsDeviceUri, queue)) return false;

    // 1. A UUID in the query ("dnssd://...?uuid=..."), which is what CUPS
    //    writes for a queue created from a DNS-SD advertisement.
    const std::string wantedUuid = Lower(StripUuidPrefix(uuid));
    if (!wantedUuid.empty() && !queue.query.empty()) {
        std::istringstream query(queue.query);
        std::string pair;
        while (std::getline(query, pair, '&')) {
            const size_t equals = pair.find('=');
            if (equals == std::string::npos) continue;
            if (EqualsIgnoreCase(pair.substr(0, equals), "uuid") &&
                Lower(StripUuidPrefix(pair.substr(equals + 1))) == wantedUuid) {
                return true;
            }
        }
    }

    // 2. The DNS-SD instance name, which is the host part of the URI CUPS
    //    uses for a printer it discovered: "ipps://Office%20Printer._ipps._tcp.local/".
    const std::string queueInstance = InstanceFromServiceHost(PercentDecode(queue.host));
    const std::string instance = IppInstanceFromServiceName(serviceName);
    if (!queueInstance.empty() && !instance.empty() && queueInstance == instance) {
        return true;
    }

    // 3. The same address: host, port and resource path, the scheme's
    //    default port written in so "ipp://h/ipp/print" and
    //    "ipp://h:631/ipp/print" agree.
    UriParts printer;
    if (!SplitUri(printerUri, printer)) return false;
    const bool ippFamily = [](const std::string& scheme) {
        return scheme == "ipp" || scheme == "ipps" || scheme == "http" || scheme == "https";
    }(queue.scheme);
    if (!ippFamily) return false;
    const int queuePort = queue.port > 0 ? queue.port : DefaultPortFor(queue.scheme);
    const int printerPort = printer.port > 0 ? printer.port : DefaultPortFor(printer.scheme);
    std::string queueHost = Lower(queue.host);
    std::string printerHost = Lower(printer.host);
    while (!queueHost.empty() && queueHost.back() == '.') queueHost.pop_back();
    while (!printerHost.empty() && printerHost.back() == '.') printerHost.pop_back();
    return queueHost == printerHost && queuePort == printerPort && queue.path == printer.path;
}

// ============================================================================
// MEDIA NAMES
// ============================================================================

IOPaperDimensions IppMediaDimensionsFromPwgName(const std::string& name) {
    IOPaperDimensions size;

    // The dimensions are the last underscore-separated part: "210x297mm",
    // "8.5x11in".
    const size_t underscore = name.rfind('_');
    std::string dims = underscore == std::string::npos ? name : name.substr(underscore + 1);

    long scale = 0;     // hundredths of a millimetre per unit
    if (dims.size() > 2 && dims.compare(dims.size() - 2, 2, "mm") == 0) {
        scale = 100;
    } else if (dims.size() > 2 && dims.compare(dims.size() - 2, 2, "in") == 0) {
        scale = 2540;
    } else {
        return size;
    }
    dims.resize(dims.size() - 2);

    const size_t x = dims.find('x');
    if (x == std::string::npos) return size;

    // A decimal as integer + fraction, scaled without a float in sight.
    auto parse = [scale](const std::string& text, int& out) {
        const size_t dot = text.find('.');
        const std::string whole = text.substr(0, dot);
        const std::string fraction = dot == std::string::npos ? std::string() : text.substr(dot + 1);
        if (whole.empty() || (dot != std::string::npos && fraction.empty())) return false;
        if (fraction.size() > 4) return false;
        const long wholeValue = ParseCount(whole);
        const long fractionValue = fraction.empty() ? 0 : ParseCount(fraction);
        if (wholeValue < 0 || fractionValue < 0 || wholeValue > 100000) return false;
        long divisor = 1;
        for (size_t i = 0; i < fraction.size(); ++i) divisor *= 10;
        const long numerator = (wholeValue * divisor + fractionValue) * scale;
        out = static_cast<int>((numerator + divisor / 2) / divisor);
        return true;
    };

    int width = 0;
    int height = 0;
    if (!parse(dims.substr(0, x), width) || !parse(dims.substr(x + 1), height)) return size;
    size.widthHundredthsMM = width;
    size.heightHundredthsMM = height;
    return size;
}

// ============================================================================
// PRINTER DESCRIPTION
// ============================================================================

IOPrinterCapabilities IppCapabilitiesFromAttributes(const IppGroup& printer) {
    IOPrinterCapabilities capabilities;

    // --- Paper -------------------------------------------------------------
    if (const IppAttribute* media = printer.Find("media-supported")) {
        for (const std::string& name : media->Strings()) {
            const IOPaperDimensions dims = IppMediaDimensionsFromPwgName(name);
            if (!dims.IsValid()) continue;

            if (StartsWith(name, "custom_min_")) {
                capabilities.minCustomSize = dims;
                continue;
            }
            if (StartsWith(name, "custom_max_")) {
                capabilities.maxCustomSize = dims;
                continue;
            }
            if (StartsWith(name, "roll_")) continue;

            AddUnique(capabilities.paperSizes,
                      IOPaperSizeFromDimensions(dims.widthHundredthsMM, dims.heightHundredthsMM));
        }
    }

    // media-size-supported says the same thing structurally, and is the only
    // place some printers state a custom range.
    if (const IppAttribute* sizes = printer.Find("media-size-supported")) {
        for (size_t i = 0; i < sizes->values.size(); ++i) {
            const IppAttribute* x = sizes->Member("x-dimension", i);
            const IppAttribute* y = sizes->Member("y-dimension", i);
            if (!x || !y || !x->First() || !y->First()) continue;
            const IppValue& xv = *x->First();
            const IppValue& yv = *y->First();
            if (xv.tag == IppTag::RangeOfInteger && yv.tag == IppTag::RangeOfInteger) {
                if (!capabilities.minCustomSize.IsValid()) {
                    capabilities.minCustomSize = {xv.integer, yv.integer};
                }
                if (!capabilities.maxCustomSize.IsValid()) {
                    capabilities.maxCustomSize = {xv.upper, yv.upper};
                }
            } else if (xv.tag == IppTag::Integer && yv.tag == IppTag::Integer) {
                AddUnique(capabilities.paperSizes, IOPaperSizeFromDimensions(xv.integer, yv.integer));
            }
        }
    }

    // --- Colour, duplex, collation, borderless ------------------------------
    if (const IppAttribute* modes = printer.Find("print-color-mode-supported")) {
        capabilities.supportsColor = IOSupportFrom(modes->Contains("color"));
    } else if (const IppAttribute* color = printer.Find("color-supported")) {
        capabilities.supportsColor = IOSupportFrom(color->BooleanOr(false));
    }

    if (const IppAttribute* sides = printer.Find("sides-supported")) {
        capabilities.supportsDuplex = IOSupportFrom(sides->Contains("two-sided-long-edge") ||
                                                    sides->Contains("two-sided-short-edge"));
    }

    if (const IppAttribute* handling = printer.Find("multiple-document-handling-supported")) {
        capabilities.supportsCollate =
            IOSupportFrom(handling->Contains("separate-documents-collated-copies") &&
                          handling->Contains("separate-documents-uncollated-copies"));
    }

    capabilities.supportsBorderless = BorderlessSupport(printer);

    // --- Copies --------------------------------------------------------------
    if (const IppAttribute* copies = printer.Find("copies-supported")) {
        if (const IppValue* range = copies->First()) {
            if (range->tag == IppTag::RangeOfInteger) capabilities.maxCopies = range->upper;
            else if (range->tag == IppTag::Integer) capabilities.maxCopies = range->integer;
        }
    }

    // --- Quality -------------------------------------------------------------
    if (const IppAttribute* qualities = printer.Find("print-quality-supported")) {
        if (qualities->ContainsInteger(3)) capabilities.qualities.push_back(IOPrintQuality::Draft);
        if (qualities->ContainsInteger(4)) capabilities.qualities.push_back(IOPrintQuality::Normal);
        if (qualities->ContainsInteger(5)) capabilities.qualities.push_back(IOPrintQuality::High);
    }

    // --- Media types ----------------------------------------------------------
    // Only the ones with an equivalent here; a printer's own "other" and the
    // vendor keywords have no IOMediaType to become.
    if (const IppAttribute* types = printer.Find("media-type-supported")) {
        for (const std::string& keyword : types->Strings()) {
            for (const MediaTypeEntry& entry : kMediaTypes) {
                if (EqualsIgnoreCase(keyword, entry.keyword)) {
                    AddUniqueValue(capabilities.mediaTypes, entry.type);
                }
            }
        }
    }

    // resolutionModes and inksets are left empty. Both are GutenPrint's
    // vocabulary - inkjet resolution classes, cartridge ink sets - and an
    // empty list reads as "not reported", which is true: IPP states dpi, not
    // classes, and the raster path chooses from those itself.
    return capabilities;
}

std::vector<IOSupplyLevel> IppSuppliesFromAttributes(const IppGroup& printer) {
    std::vector<IOSupplyLevel> supplies;

    if (const IppAttribute* levels = printer.Find("marker-levels")) {
        const IppAttribute* names = printer.Find("marker-names");
        const IppAttribute* types = printer.Find("marker-types");
        const IppAttribute* colors = printer.Find("marker-colors");
        const std::vector<std::string> nameList = names ? names->Strings() : std::vector<std::string>();
        const std::vector<std::string> typeList = types ? types->Strings() : std::vector<std::string>();
        const std::vector<std::string> colorList = colors ? colors->Strings() : std::vector<std::string>();

        for (size_t i = 0; i < levels->values.size(); ++i) {
            IOSupplyLevel supply;
            const int level = levels->values[i].integer;
            // -1 unknown, -2 unavailable, -3 "some left": none of them is a
            // level, and none of them is empty.
            supply.percentRemaining = (level >= 0 && level <= 100) ? level : -1;
            if (i < nameList.size()) supply.description = nameList[i];
            if (i < typeList.size()) supply.type = SupplyTypeFromKeyword(typeList[i]);
            if (i < colorList.size()) supply.color = SupplyColorFromHex(colorList[i]);
            if (supply.description.empty()) supply.description = IOSupplyColorToString(supply.color);
            supplies.push_back(supply);
        }
        return supplies;
    }

    const IppAttribute* entries = printer.Find("printer-supply");
    if (!entries) return supplies;
    const IppAttribute* descriptions = printer.Find("printer-supply-description");
    const std::vector<std::string> descriptionList =
        descriptions ? descriptions->Strings() : std::vector<std::string>();

    const std::vector<std::string> entryList = entries->Strings();
    for (size_t i = 0; i < entryList.size(); ++i) {
        const std::string& entry = entryList[i];
        IOSupplyLevel supply;
        supply.type = SupplyTypeFromKeyword(SupplyField(entry, "type"));
        supply.color = SupplyColorFromName(SupplyField(entry, "colorantname"));

        long level = 0;
        long capacity = 0;
        if (ParseSigned(SupplyField(entry, "level"), level) &&
            ParseSigned(SupplyField(entry, "maxcapacity"), capacity) && level >= 0 &&
            capacity > 0) {
            supply.percentRemaining =
                static_cast<int>(std::min<long>(100, (level * 100 + capacity / 2) / capacity));
        }

        if (i < descriptionList.size()) supply.description = descriptionList[i];
        if (supply.description.empty()) supply.description = IOSupplyColorToString(supply.color);
        supplies.push_back(supply);
    }
    return supplies;
}

IOPrinterStatus IppStatusFromAttributes(const IppGroup& printer) {
    IOPrinterStatus status;

    if (const IppAttribute* state = printer.Find("printer-state")) {
        switch (state->IntegerOr(0)) {
            case 3: status.state = IOPrinterState::Idle; break;
            case 4: status.state = IOPrinterState::Printing; break;
            case 5: status.state = IOPrinterState::Stopped; break;
            default: status.state = IOPrinterState::Unknown; break;
        }
    }

    if (const IppAttribute* reasons = printer.Find("printer-state-reasons")) {
        std::string joined;
        for (const std::string& reason : reasons->Strings()) {
            if (!joined.empty()) joined += ",";
            joined += reason;
        }
        status.stateReason = joined;
    }

    if (const IppAttribute* accepting = printer.Find("printer-is-accepting-jobs")) {
        status.acceptingJobs = accepting->BooleanOr(false);
    }
    if (const IppAttribute* queued = printer.Find("queued-job-count")) {
        status.jobsQueued = queued->IntegerOr(0);
    }

    status.supplies = IppSuppliesFromAttributes(printer);
    return status;
}

IOPrintJobStatus IppJobStatusFromAttributes(const IppGroup& job) {
    IOPrintJobStatus status;
    if (const IppAttribute* id = job.Find("job-id")) status.jobId = id->IntegerOr(0);

    if (const IppAttribute* state = job.Find("job-state")) {
        switch (state->IntegerOr(0)) {
            case 3: status.state = IOPrintJobState::Pending; break;
            case 4: status.state = IOPrintJobState::Held; break;
            case 5: status.state = IOPrintJobState::Processing; break;
            case 6: status.state = IOPrintJobState::Stopped; break;
            case 7: status.state = IOPrintJobState::Cancelled; break;
            case 8: status.state = IOPrintJobState::Aborted; break;
            case 9: status.state = IOPrintJobState::Completed; break;
            default: status.state = IOPrintJobState::Unknown; break;
        }
    }

    if (const IppAttribute* reasons = job.Find("job-state-reasons")) {
        std::string joined;
        for (const std::string& reason : reasons->Strings()) {
            if (!joined.empty()) joined += ",";
            joined += reason;
        }
        status.stateReason = joined;
    }

    if (const IppAttribute* name = job.Find("job-name")) status.jobName = name->String();
    if (const IppAttribute* user = job.Find("job-originating-user-name")) {
        status.user = user->String();
    }
    if (const IppAttribute* done = job.Find("job-impressions-completed")) {
        status.pagesPrinted = done->IntegerOr(0);
    }
    if (const IppAttribute* total = job.Find("job-impressions")) {
        status.pagesTotal = total->IntegerOr(0);
    }
    return status;
}

bool IppDocumentSupport::Accepts(const std::string& mimeType) const {
    for (const std::string& format : documentFormats) {
        if (EqualsIgnoreCase(format, mimeType)) return true;
    }
    return false;
}

IppDocumentSupport IppDocumentSupportFromAttributes(const IppGroup& printer) {
    IppDocumentSupport support;
    if (const IppAttribute* formats = printer.Find("document-format-supported")) {
        support.documentFormats = formats->Strings();
    }
    if (const IppAttribute* types = printer.Find("pwg-raster-document-type-supported")) {
        support.rasterTypes = types->Strings();
    }
    if (const IppAttribute* resolutions = printer.Find("pwg-raster-document-resolution-supported")) {
        for (const IppValue& value : resolutions->values) {
            if (value.tag != IppTag::Resolution || value.integer <= 0 || value.upper <= 0) continue;
            IOResolution dpi;
            if (value.units == 4) {
                // Dots per centimetre, to the nearest dot per inch.
                dpi.dpiX = (value.integer * 254 + 50) / 100;
                dpi.dpiY = (value.upper * 254 + 50) / 100;
            } else {
                dpi.dpiX = value.integer;
                dpi.dpiY = value.upper;
            }
            support.rasterResolutions.push_back(dpi);
        }
    }
    if (const IppAttribute* back = printer.Find("pwg-raster-document-sheet-back")) {
        const std::string keyword = back->String();
        if (!keyword.empty()) support.rasterSheetBack = Lower(keyword);
    }
    if (const IppAttribute* ranges = printer.Find("page-ranges-supported")) {
        support.pageRanges = ranges->BooleanOr(false);
    }

    // Each edge's list, largest value kept; zero in all four is borderless.
    struct Edge { const char* name; int IOPageMargins::*field; };
    const Edge edges[] = {{"media-left-margin-supported", &IOPageMargins::leftHundredthsMM},
                          {"media-top-margin-supported", &IOPageMargins::topHundredthsMM},
                          {"media-right-margin-supported", &IOPageMargins::rightHundredthsMM},
                          {"media-bottom-margin-supported", &IOPageMargins::bottomHundredthsMM}};
    int zeroEdges = 0;
    for (const Edge& edge : edges) {
        const IppAttribute* margins = printer.Find(edge.name);
        if (!margins) continue;
        int largest = -1;
        bool zero = false;
        for (const IppValue& value : margins->values) {
            if (value.tag != IppTag::Integer || value.integer < 0) continue;
            largest = std::max(largest, value.integer);
            if (value.integer == 0) zero = true;
        }
        if (largest >= 0) support.margins.*edge.field = largest;
        if (zero) ++zeroEdges;
    }
    support.borderless = zeroEdges == 4;
    return support;
}

IOPageMargins IppDrawingMargins(const IppDocumentSupport& printer, const IOPageSetup& page) {
    const IOPageMargins hardware =
        (page.borderless && printer.borderless) ? IOPageMargins() : printer.margins;
    IOPageMargins margins;
    margins.leftHundredthsMM = std::max(page.margins.leftHundredthsMM, hardware.leftHundredthsMM);
    margins.topHundredthsMM = std::max(page.margins.topHundredthsMM, hardware.topHundredthsMM);
    margins.rightHundredthsMM = std::max(page.margins.rightHundredthsMM, hardware.rightHundredthsMM);
    margins.bottomHundredthsMM =
        std::max(page.margins.bottomHundredthsMM, hardware.bottomHundredthsMM);
    return margins;
}

// ============================================================================
// JOB TEMPLATE
// ============================================================================

std::string IppMediaTypeKeyword(IOMediaType type) {
    if (type == IOMediaType::PlainFast || type == IOMediaType::Bond) return "stationery";
    for (const MediaTypeEntry& entry : kMediaTypes) {
        if (entry.type == type) return entry.keyword;
    }
    return std::string();
}

void AddIppJobTemplate(IppGroup& job, const IOPrintOptions& options,
                       const std::vector<int>& pageRange, bool drawnHere) {
    job.Set("copies", IppValue::Integer(std::max(1, options.copies)));

    if (options.copies > 1) {
        job.Set("multiple-document-handling",
                IppValue::Keyword(options.collate ? "separate-documents-collated-copies"
                                                  : "separate-documents-uncollated-copies"));
    }

    switch (options.duplex) {
        case IODuplexMode::None:      job.Set("sides", IppValue::Keyword("one-sided")); break;
        case IODuplexMode::LongEdge:  job.Set("sides", IppValue::Keyword("two-sided-long-edge")); break;
        case IODuplexMode::ShortEdge: job.Set("sides", IppValue::Keyword("two-sided-short-edge")); break;
    }

    switch (options.colorMode) {
        case IOPrinterColorMode::Color:
            job.Set("print-color-mode", IppValue::Keyword("color"));
            break;
        case IOPrinterColorMode::Grayscale:
        case IOPrinterColorMode::Monochrome:
            // Not "bi-level" for Monochrome: few printers offer it, and one
            // that does not would substitute its default - often colour.
            job.Set("print-color-mode", IppValue::Keyword("monochrome"));
            break;
        case IOPrinterColorMode::Auto:
            job.Set("print-color-mode", IppValue::Keyword("auto"));
            break;
    }

    switch (options.quality) {
        case IOPrintQuality::Draft:  job.Set("print-quality", IppValue::Enum(3)); break;
        case IOPrintQuality::Normal: job.Set("print-quality", IppValue::Enum(4)); break;
        case IOPrintQuality::High:
        case IOPrintQuality::Photo:  job.Set("print-quality", IppValue::Enum(5)); break;
    }

    // --- Media: a keyword when that says it all, a collection when not ------
    // "media" and "media-col" may not both appear, so one is chosen.
    const std::string mediaType = IppMediaTypeKeyword(options.mediaType);
    const bool custom = options.page.paperSize == IOPaperSize::Custom;
    const IOPaperDimensions sheet =
        custom ? options.page.customSize : IOPaperSizeDimensions(options.page.paperSize);

    if ((custom || !mediaType.empty()) && sheet.IsValid()) {
        std::vector<IppAttribute> size;
        size.emplace_back("x-dimension", IppValue::Integer(sheet.widthHundredthsMM));
        size.emplace_back("y-dimension", IppValue::Integer(sheet.heightHundredthsMM));

        std::vector<IppAttribute> collection;
        collection.emplace_back("media-size", IppValue::Collection(std::move(size)));
        if (!mediaType.empty()) {
            collection.emplace_back("media-type", IppValue::Keyword(mediaType));
        }
        if (options.page.borderless) {
            for (const char* margin : {"media-bottom-margin", "media-left-margin",
                                       "media-right-margin", "media-top-margin"}) {
                collection.emplace_back(margin, IppValue::Integer(0));
            }
        }
        job.Set("media-col", IppValue::Collection(std::move(collection)));
    } else {
        const std::string media = IOPaperSizeToPwgName(options.page.paperSize);
        if (!media.empty()) job.Set("media", IppValue::Keyword(media));
    }

    if (!pageRange.empty()) {
        // Consecutive pages as one range each, the same folding the
        // page-ranges string in IOFormatPageRanges does.
        std::vector<int> pages;
        for (int page : pageRange) {
            if (page >= 1) pages.push_back(page);
        }
        std::sort(pages.begin(), pages.end());
        pages.erase(std::unique(pages.begin(), pages.end()), pages.end());

        std::vector<IppValue> ranges;
        for (size_t i = 0; i < pages.size();) {
            size_t j = i;
            while (j + 1 < pages.size() && pages[j + 1] == pages[j] + 1) ++j;
            ranges.push_back(IppValue::Range(pages[i], pages[j]));
            i = j + 1;
        }
        if (!ranges.empty()) job.Set("page-ranges", std::move(ranges));
    }

    if (!drawnHere) {
        // For a document the printer renders. For raster drawn here, the
        // orientation is already in the pixels and the resolution is in the
        // page header.
        switch (options.page.orientation) {
            case IOPrintOrientation::Portrait: break;
            case IOPrintOrientation::Landscape:
                job.Set("orientation-requested", IppValue::Enum(4)); break;
            case IOPrintOrientation::ReverseLandscape:
                job.Set("orientation-requested", IppValue::Enum(5)); break;
            case IOPrintOrientation::ReversePortrait:
                job.Set("orientation-requested", IppValue::Enum(6)); break;
        }
        if (options.resolutionMode == IOResolutionMode::Custom &&
            options.customResolution.IsValid()) {
            job.Set("printer-resolution", IppValue::Dpi(options.customResolution.dpiX,
                                                        options.customResolution.dpiY));
        }
    }

    for (const auto& option : options.backendOptions) {
        if (option.first.empty()) continue;
        const std::string& text = option.second;
        if (IsDigits(text) && text.size() <= 9) {
            job.Set(option.first, IppValue::Integer(static_cast<int32_t>(ParseCount(text))));
        } else if (text == "true" || text == "false") {
            job.Set(option.first, IppValue::Boolean(text == "true"));
        } else {
            job.Set(option.first, IppValue::Keyword(text));
        }
    }
}

// ============================================================================
// DOCUMENT PLAN
// ============================================================================

const char* const kIppDrawnPagesType = "application/x-ultracanvas-pages";

std::string IppJobDocumentType(const IOPrintJob& job) {
    if (job.pages && job.filePath.empty() && job.data.empty()) return kIppDrawnPagesType;
    if (!job.mimeType.empty()) {
        std::string type = Lower(job.mimeType);
        const size_t semicolon = type.find(';');
        if (semicolon != std::string::npos) type.resize(semicolon);
        while (!type.empty() && type.back() == ' ') type.pop_back();
        return type;
    }

    const std::string extension = LowerExtension(job.filePath);
    if (extension == "pdf") return "application/pdf";
    if (extension == "jpg" || extension == "jpeg") return "image/jpeg";
    if (extension == "png") return "image/png";
    if (extension == "pwg") return "image/pwg-raster";
    if (extension == "urf") return "image/urf";
    if (extension == "txt" || extension == "log" || extension == "md") return "text/plain";
    if (extension == "bmp") return "image/bmp";
    if (extension == "gif") return "image/gif";
    if (extension == "tif" || extension == "tiff") return "image/tiff";
    if (extension == "webp") return "image/webp";
    if (extension == "qoi") return "image/qoi";
    return std::string();
}

IODeviceResult PlanIppDocument(const std::string& documentType,
                               const std::vector<int>& pageRange,
                               const IppDocumentSupport& printer,
                               IppDocumentPlan& out) {
    out = IppDocumentPlan();

    if (documentType.empty()) {
        return IODeviceResult::Error(
            IODeviceResultCode::InvalidArgument,
            "The job says nothing about its type, and its name carries no "
            "extension to infer one from");
    }

    // --- As it is -----------------------------------------------------------
    if (documentType != "text/plain" && printer.Accepts(documentType)) {
        out.passThrough = true;
        out.documentFormat = documentType;

        if (pageRange.empty()) return IODeviceResult::Ok();

        if (IsSinglePageImage(documentType)) {
            // One page: a range either includes it or selects nothing.
            if (IOSelectPages(pageRange, 1).empty()) {
                return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                             "The selected pages are not in this document");
            }
            return IODeviceResult::Ok();
        }

        if (!printer.pageRanges) {
            return IODeviceResult::Error(
                IODeviceResultCode::NotSupported,
                "This printer cannot print a range of pages from a " + documentType +
                    " document, and UltraCanvas cannot select them itself yet; "
                    "print the whole document");
        }
        out.sendPageRange = true;
        return IODeviceResult::Ok();
    }

    // --- Drawn here -----------------------------------------------------------
    if (!CanDrawHere(documentType)) {
        return IODeviceResult::Error(
            IODeviceResultCode::NotSupported,
            "This printer does not take " + documentType + " (it takes " +
                (printer.documentFormats.empty() ? std::string("nothing it names")
                                                 : JoinList(printer.documentFormats)) +
                "), and UltraCanvas can only draw pages for images and plain text");
    }

    if (!printer.Accepts("image/pwg-raster") || printer.rasterResolutions.empty() ||
        (ChoosePwgRasterType(printer.rasterTypes, IOPrinterColorMode::Auto).empty())) {
        return IODeviceResult::Error(
            IODeviceResultCode::NotSupported,
            "UltraCanvas draws " + documentType + " as PWG raster, which this printer "
            "does not take in a form it can use (it takes " +
                (printer.documentFormats.empty() ? std::string("nothing it names")
                                                 : JoinList(printer.documentFormats)) +
                ")");
    }

    out.passThrough = false;
    out.documentFormat = "image/pwg-raster";
    out.sendPageRange = false;
    return IODeviceResult::Ok();
}

std::string ChoosePwgRasterType(const std::vector<std::string>& rasterTypes,
                                IOPrinterColorMode colorMode) {
    auto has = [&](const char* type) {
        for (const std::string& candidate : rasterTypes) {
            if (EqualsIgnoreCase(candidate, type)) return true;
        }
        return false;
    };

    const bool grey = colorMode == IOPrinterColorMode::Grayscale ||
                      colorMode == IOPrinterColorMode::Monochrome;
    if (grey) {
        if (has("sgray_8")) return "sgray_8";
        if (has("srgb_8")) return "srgb_8";
        return std::string();
    }
    if (has("srgb_8")) return "srgb_8";
    if (has("sgray_8")) return "sgray_8";
    return std::string();
}

IOResolution ChoosePwgRasterResolution(const std::vector<IOResolution>& supported,
                                       IOPrintQuality quality,
                                       IOResolution requested) {
    if (supported.empty()) return IOResolution();

    if (requested.IsValid()) {
        for (const IOResolution& candidate : supported) {
            if (candidate.dpiX == requested.dpiX && candidate.dpiY == requested.dpiY) {
                return candidate;
            }
        }
    }

    auto area = [](const IOResolution& r) { return static_cast<long>(r.dpiX) * r.dpiY; };

    if (quality == IOPrintQuality::Draft) {
        return *std::min_element(supported.begin(), supported.end(),
                                 [&](const IOResolution& a, const IOResolution& b) {
                                     return area(a) < area(b);
                                 });
    }

    if (quality == IOPrintQuality::Normal) {
        // Nearest 300 dpi, by the larger of the two axes; a tie goes to the
        // lower resolution, which is the cheaper page.
        auto distance = [](const IOResolution& r) {
            return std::abs(std::max(r.dpiX, r.dpiY) - 300);
        };
        return *std::min_element(supported.begin(), supported.end(),
                                 [&](const IOResolution& a, const IOResolution& b) {
                                     if (distance(a) != distance(b)) return distance(a) < distance(b);
                                     return area(a) < area(b);
                                 });
    }

    // High and Photo: the highest up to 600 dpi, or the lowest if every one
    // is above that.
    const IOResolution* best = nullptr;
    for (const IOResolution& candidate : supported) {
        if (std::max(candidate.dpiX, candidate.dpiY) > 600) continue;
        if (!best || area(candidate) > area(*best)) best = &candidate;
    }
    if (best) return *best;
    return *std::min_element(supported.begin(), supported.end(),
                             [&](const IOResolution& a, const IOResolution& b) {
                                 return area(a) < area(b);
                             });
}

}  // namespace UltraCanvas
