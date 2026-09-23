// PixelFXMetadataDecode.cpp
// IPTC-IIM and XMP decoding, EXIF value formatting and display names, for
// PixelFX::Header::ReadMetadata(). See PixelFX/PixelFXMetadataDecode.h.
// Version: 1.2.0
// Last Modified: 2026-09-23
// Author: UltraCanvas Framework

#include "PixelFX/PixelFXMetadataDecode.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "tinyxml2.h"

namespace PixelFX {
namespace Header {

namespace {

    // Longest value handed back. A metadata table is not the place for an
    // embedded thumbnail's base64 or a full edit history.
    constexpr std::size_t kMaxValueLength = 512;

    std::string Trim(const std::string& s) {
        std::size_t a = 0, b = s.size();
        while (a < b && (std::isspace(static_cast<unsigned char>(s[a])) || s[a] == '\0')) ++a;
        while (b > a && (std::isspace(static_cast<unsigned char>(s[b - 1])) || s[b - 1] == '\0')) --b;
        return s.substr(a, b - a);
    }

    // One line, bounded: line breaks become spaces, and a very long value is
    // cut on a UTF-8 character boundary.
    std::string Tidy(std::string s) {
        for (char& c : s) {
            if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        }
        s = Trim(s);
        if (s.size() > kMaxValueLength) {
            std::size_t cut = kMaxValueLength - 3;
            while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
            s = s.substr(0, cut) + "...";
        }
        return s;
    }

    bool IsValidUtf8(const std::string& s) {
        std::size_t i = 0;
        while (i < s.size()) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            const int extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2
                            : (c >> 3) == 0x1E ? 3 : -1;
            if (extra < 0 || i + extra >= s.size() + (extra == 0 ? 1 : 0)) return false;
            for (int k = 1; k <= extra; ++k) {
                if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
            }
            i += extra + 1;
        }
        return true;
    }

    std::string Latin1ToUtf8(const std::string& s) {
        std::string out;
        out.reserve(s.size() + s.size() / 4);
        for (unsigned char c : s) {
            if (c < 0x80) {
                out.push_back(static_cast<char>(c));
            } else {
                out.push_back(static_cast<char>(0xC0 | (c >> 6)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
        }
        return out;
    }

    // Collects tags in first-seen order, joining repeats ("Keywords" appears
    // once per keyword in IIM).
    class TagList {
    public:
        void Add(const std::string& key, const std::string& value) {
            const std::string v = Tidy(value);
            if (key.empty() || v.empty()) return;
            auto it = index.find(key);
            if (it == index.end()) {
                index[key] = tags.size();
                tags.emplace_back(key, v);
            } else {
                std::string& existing = tags[it->second].second;
                if (existing.size() < kMaxValueLength) existing = Tidy(existing + ", " + v);
            }
        }
        std::vector<DecodedTag> Take() { return std::move(tags); }

    private:
        std::vector<DecodedTag> tags;
        std::map<std::string, std::size_t> index;
    };

    // ===== IPTC-IIM =====

    const char* IptcRecord2Name(int dataset) {
        switch (dataset) {
            case 3:   return "Object Type Reference";
            case 4:   return "Object Attribute Reference";
            case 5:   return "Object Name";
            case 7:   return "Edit Status";
            case 10:  return "Urgency";
            case 12:  return "Subject Reference";
            case 15:  return "Category";
            case 20:  return "Supplemental Category";
            case 22:  return "Fixture Identifier";
            case 25:  return "Keywords";
            case 26:  return "Content Location Code";
            case 27:  return "Content Location Name";
            case 30:  return "Release Date";
            case 35:  return "Release Time";
            case 37:  return "Expiration Date";
            case 38:  return "Expiration Time";
            case 40:  return "Special Instructions";
            case 45:  return "Reference Service";
            case 47:  return "Reference Date";
            case 50:  return "Reference Number";
            case 55:  return "Date Created";
            case 60:  return "Time Created";
            case 62:  return "Digital Creation Date";
            case 63:  return "Digital Creation Time";
            case 65:  return "Originating Program";
            case 70:  return "Program Version";
            case 75:  return "Object Cycle";
            case 80:  return "By-line";
            case 85:  return "By-line Title";
            case 90:  return "City";
            case 92:  return "Sub-location";
            case 95:  return "Province/State";
            case 100: return "Country Code";
            case 101: return "Country Name";
            case 103: return "Original Transmission Reference";
            case 105: return "Headline";
            case 110: return "Credit";
            case 115: return "Source";
            case 116: return "Copyright Notice";
            case 118: return "Contact";
            case 120: return "Caption/Abstract";
            case 122: return "Writer/Editor";
            case 130: return "Image Type";
            case 131: return "Image Orientation";
            case 135: return "Language Identifier";
            default:  return nullptr;
        }
    }

    bool IsIptcDate(int dataset) {
        return dataset == 30 || dataset == 37 || dataset == 47 || dataset == 55 || dataset == 62;
    }
    bool IsIptcTime(int dataset) {
        return dataset == 35 || dataset == 38 || dataset == 60 || dataset == 63;
    }

    bool AllDigits(const std::string& s, std::size_t from, std::size_t count) {
        if (from + count > s.size()) return false;
        for (std::size_t i = from; i < from + count; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        }
        return true;
    }

    // CCYYMMDD -> CCYY-MM-DD
    std::string FormatIptcDate(const std::string& v) {
        if (v.size() != 8 || !AllDigits(v, 0, 8)) return v;
        return v.substr(0, 4) + "-" + v.substr(4, 2) + "-" + v.substr(6, 2);
    }

    // HHMMSS±HHMM -> HH:MM:SS±HH:MM
    std::string FormatIptcTime(const std::string& v) {
        if (v.size() < 6 || !AllDigits(v, 0, 6)) return v;
        std::string out = v.substr(0, 2) + ":" + v.substr(2, 2) + ":" + v.substr(4, 2);
        if (v.size() == 11 && (v[6] == '+' || v[6] == '-') && AllDigits(v, 7, 4)) {
            out += v.substr(6, 3) + ":" + v.substr(9, 2);
        }
        return out;
    }

    void DecodeIim(const unsigned char* p, std::size_t n, TagList& out) {
        struct Dataset { int record; int number; std::string value; };
        std::vector<Dataset> datasets;
        bool utf8 = false;

        std::size_t pos = 0;
        while (pos + 5 <= n && p[pos] == 0x1C) {
            const int record = p[pos + 1];
            const int number = p[pos + 2];
            std::size_t length = (static_cast<std::size_t>(p[pos + 3]) << 8) | p[pos + 4];
            pos += 5;
            if (length & 0x8000) {
                // Extended dataset: the low bits count the length bytes that follow.
                const std::size_t count = length & 0x7FFF;
                if (count == 0 || count > 4 || pos + count > n) break;
                length = 0;
                for (std::size_t k = 0; k < count; ++k) length = (length << 8) | p[pos + k];
                pos += count;
            }
            if (length > n - pos) break;
            std::string value(reinterpret_cast<const char*>(p + pos), length);
            pos += length;

            // 1:90 Coded Character Set; ESC % G announces UTF-8.
            if (record == 1 && number == 90 && value.find("\x1B%G") != std::string::npos) utf8 = true;
            datasets.push_back({record, number, std::move(value)});
        }

        for (auto& d : datasets) {
            // Record 2 is what a person reads; record 1 is transmission
            // envelope, dataset 2:0 the record version, 2:200+ binary previews.
            if (d.record != 2 || d.number == 0 || d.number >= 200) continue;
            std::string value = Trim(d.value);
            if (!utf8 && !IsValidUtf8(value)) value = Latin1ToUtf8(value);
            if (IsIptcDate(d.number)) value = FormatIptcDate(value);
            else if (IsIptcTime(d.number)) value = FormatIptcTime(value);
            const char* name = IptcRecord2Name(d.number);
            out.Add(name ? name : "2:" + std::to_string(d.number), value);
        }
    }

    std::uint32_t ReadBE32(const unsigned char* p) {
        return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
               (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
    }

    // Photoshop image resource blocks: "8BIM", id, Pascal name padded to even,
    // size, data padded to even. Resource 0x0404 is the IPTC-IIM.
    void DecodePhotoshopResources(const unsigned char* p, std::size_t n, TagList& out) {
        std::size_t pos = 0;
        while (pos + 12 <= n && std::memcmp(p + pos, "8BIM", 4) == 0) {
            const int id = (p[pos + 4] << 8) | p[pos + 5];
            std::size_t namePart = 1 + p[pos + 6];
            if (namePart % 2) ++namePart;
            std::size_t sizeAt = pos + 6 + namePart;
            if (sizeAt + 4 > n) break;
            std::size_t size = ReadBE32(p + sizeAt);
            std::size_t dataAt = sizeAt + 4;
            if (size > n - dataAt) break;
            if (id == 0x0404) DecodeIim(p + dataAt, size, out);
            pos = dataAt + size + (size % 2);
        }
    }

    // ===== XMP =====

    bool NameIs(const tinyxml2::XMLElement* e, const char* name) {
        return e && e->Name() && std::strcmp(e->Name(), name) == 0;
    }

    bool IsSyntaxAttribute(const char* name) {
        return std::strncmp(name, "xmlns", 5) == 0 || std::strncmp(name, "rdf:", 4) == 0 ||
               std::strncmp(name, "xml:", 4) == 0;
    }

    std::string TextOf(const tinyxml2::XMLElement* e) {
        const char* t = e->GetText();
        return t ? std::string(t) : std::string();
    }

    bool HasFieldAttributes(const tinyxml2::XMLElement* e) {
        for (auto* a = e->FirstAttribute(); a; a = a->Next()) {
            if (!IsSyntaxAttribute(a->Name())) return true;
        }
        return false;
    }

    void DecodeXmpProperty(const tinyxml2::XMLElement* e, const std::string& name, TagList& out, int depth);

    // Attributes and child elements of a structure (rdf:Description or an
    // element with rdf:parseType="Resource") are its fields.
    void DecodeXmpStruct(const tinyxml2::XMLElement* e, const std::string& prefix, TagList& out, int depth) {
        for (auto* a = e->FirstAttribute(); a; a = a->Next()) {
            if (IsSyntaxAttribute(a->Name())) continue;
            out.Add(prefix + a->Name(), a->Value() ? a->Value() : "");
        }
        for (auto* c = e->FirstChildElement(); c; c = c->NextSiblingElement()) {
            DecodeXmpProperty(c, prefix + c->Name(), out, depth + 1);
        }
    }

    void DecodeXmpProperty(const tinyxml2::XMLElement* e, const std::string& name, TagList& out, int depth) {
        if (depth > 8) return;   // nothing a person reads nests this deep

        if (const char* res = e->Attribute("rdf:resource")) {
            out.Add(name, res);
            return;
        }
        const char* parseType = e->Attribute("rdf:parseType");
        if (parseType && std::strcmp(parseType, "Resource") == 0) {
            DecodeXmpStruct(e, name + "/", out, depth);
            return;
        }

        const tinyxml2::XMLElement* child = e->FirstChildElement();
        if (!child) {
            // Simple value, or a structure written only with attributes.
            if (HasFieldAttributes(e)) DecodeXmpStruct(e, name + "/", out, depth);
            else out.Add(name, TextOf(e));
            return;
        }

        if (NameIs(child, "rdf:Alt")) {
            // Language alternative: x-default, else the first one.
            const tinyxml2::XMLElement* pick = nullptr;
            for (auto* li = child->FirstChildElement("rdf:li"); li; li = li->NextSiblingElement("rdf:li")) {
                if (!pick) pick = li;
                const char* lang = li->Attribute("xml:lang");
                if (lang && std::strcmp(lang, "x-default") == 0) { pick = li; break; }
            }
            if (pick) out.Add(name, TextOf(pick));
            return;
        }

        if (NameIs(child, "rdf:Seq") || NameIs(child, "rdf:Bag")) {
            int index = 0;
            for (auto* li = child->FirstChildElement("rdf:li"); li; li = li->NextSiblingElement("rdf:li")) {
                ++index;
                const char* pt = li->Attribute("rdf:parseType");
                const tinyxml2::XMLElement* inner = li->FirstChildElement();
                const std::string item = name + "[" + std::to_string(index) + "]/";
                if ((pt && std::strcmp(pt, "Resource") == 0) || (!inner && HasFieldAttributes(li))) {
                    DecodeXmpStruct(li, item, out, depth + 1);
                } else if (inner && NameIs(inner, "rdf:Description")) {
                    DecodeXmpStruct(inner, item, out, depth + 1);
                } else if (inner) {
                    for (auto* c = inner; c; c = c->NextSiblingElement())
                        DecodeXmpProperty(c, item + c->Name(), out, depth + 1);
                } else {
                    out.Add(name, TextOf(li));   // plain items join into one row
                }
            }
            return;
        }

        if (NameIs(child, "rdf:Description")) {
            DecodeXmpStruct(child, name + "/", out, depth);
            return;
        }

        // Fields written straight inside the property element.
        DecodeXmpStruct(e, name + "/", out, depth);
    }

    const tinyxml2::XMLElement* FindRdf(const tinyxml2::XMLElement* e, int depth) {
        if (!e || depth > 4) return nullptr;
        if (NameIs(e, "rdf:RDF")) return e;
        for (auto* c = e->FirstChildElement(); c; c = c->NextSiblingElement()) {
            if (auto* found = FindRdf(c, depth + 1)) return found;
        }
        return nullptr;
    }

} // namespace

std::vector<DecodedTag> DecodeIPTC(const void* data, std::size_t length) {
    TagList out;
    if (!data || length == 0) return out.Take();
    const auto* p = static_cast<const unsigned char*>(data);

    static const char kPhotoshop[] = "Photoshop 3.0";   // followed by a NUL
    const std::size_t sig = sizeof(kPhotoshop);          // includes that NUL
    if (length >= sig && std::memcmp(p, kPhotoshop, sig) == 0) {
        DecodePhotoshopResources(p + sig, length - sig, out);
    } else if (length >= 4 && std::memcmp(p, "8BIM", 4) == 0) {
        DecodePhotoshopResources(p, length, out);
    } else if (p[0] == 0x1C) {
        DecodeIim(p, length, out);
    }
    return out.Take();
}

std::vector<DecodedTag> DecodeXMP(const void* data, std::size_t length) {
    TagList out;
    if (!data || length == 0) return out.Take();

    // The packet is not NUL-terminated inside the file, and writers pad it
    // with whitespace (and sometimes trailing NULs).
    std::string xml(static_cast<const char*>(data), length);
    while (!xml.empty() && xml.back() == '\0') xml.pop_back();
    // The packet wrapper: <?xpacket begin...?> ... <?xpacket end="w"?>. The
    // closing one follows the root element, which tinyxml2 rejects, and
    // neither carries anything, so both go.
    for (std::size_t at = xml.find("<?xpacket"); at != std::string::npos; at = xml.find("<?xpacket", at)) {
        const std::size_t end = xml.find("?>", at);
        if (end == std::string::npos) { xml.erase(at); break; }
        xml.erase(at, end + 2 - at);
    }

    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS) return out.Take();

    const tinyxml2::XMLElement* rdf = nullptr;
    for (auto* e = doc.FirstChildElement(); e && !rdf; e = e->NextSiblingElement()) {
        rdf = FindRdf(e, 0);
    }
    if (!rdf) return out.Take();

    for (auto* desc = rdf->FirstChildElement("rdf:Description"); desc;
         desc = desc->NextSiblingElement("rdf:Description")) {
        DecodeXmpStruct(desc, "", out, 0);
    }
    return out.Take();
}

// ===== EXIF =====

namespace {

    // "5.60" -> "5.6", "50.0" -> "50", "-0.67" stays.
    std::string FormatNumber(double v, int maxDecimals) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*f", maxDecimals, v);
        std::string s(buf);
        if (s.find('.') != std::string::npos) {
            while (!s.empty() && s.back() == '0') s.pop_back();
            if (!s.empty() && s.back() == '.') s.pop_back();
        }
        if (s == "-0") s = "0";
        return s;
    }

    // "28/5" -> 5.6; "51/1 30/1 0/1" -> {51, 30, 0}. A zero denominator or a
    // token that is not a rational makes the whole parse fail.
    bool ParseRationals(const std::string& text, std::vector<double>& out) {
        out.clear();
        std::size_t pos = 0;
        while (pos < text.size()) {
            while (pos < text.size() && text[pos] == ' ') ++pos;
            if (pos >= text.size()) break;
            std::size_t end = text.find(' ', pos);
            if (end == std::string::npos) end = text.size();
            const std::string token = text.substr(pos, end - pos);
            pos = end;
            const std::size_t slash = token.find('/');
            if (slash == std::string::npos || slash == 0 || slash + 1 >= token.size()) return false;
            char* stop = nullptr;
            const double n = std::strtod(token.c_str(), &stop);
            if (stop != token.c_str() + slash) return false;
            const double d = std::strtod(token.c_str() + slash + 1, &stop);
            if (*stop != '\0' || d == 0.0) return false;
            out.push_back(n / d);
        }
        return !out.empty();
    }

    bool ParseRational(const std::string& text, double& v) {
        std::vector<double> values;
        if (!ParseRationals(text, values) || values.size() != 1) return false;
        v = values[0];
        return true;
    }

    bool IsInteger(const std::string& s) {
        if (s.empty()) return false;
        std::size_t i = (s[0] == '-') ? 1 : 0;
        if (i >= s.size()) return false;
        for (; i < s.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        }
        return true;
    }

    bool LooksNumeric(const std::string& s) {
        bool digit = false;
        for (char c : s) {
            if (std::isdigit(static_cast<unsigned char>(c))) { digit = true; continue; }
            if (c != ' ' && c != '.' && c != ',' && c != '-' && c != '+' && c != '/') return false;
        }
        return digit;
    }

    // 1/250 s, 0.5 s, 2 s, 30 s.
    std::string FormatExposure(double seconds) {
        if (seconds <= 0) return "";
        if (seconds < 0.5) {
            const double inverse = 1.0 / seconds;
            if (std::fabs(inverse - std::round(inverse)) < 0.05 * inverse || inverse >= 10)
                return "1/" + FormatNumber(std::round(inverse), 0) + " s";
        }
        return FormatNumber(seconds, seconds < 10 ? 1 : 0) + " s";
    }

    std::string FormatFNumber(double f) { return f > 0 ? "f/" + FormatNumber(f, 1) : ""; }

    std::string FormatEv(double ev) {
        std::string s = FormatNumber(ev, 2);
        if (ev > 0.005) s = "+" + s;
        return s + " EV";
    }

    // d° m′ s″ H (decimal°). Handles minutes written as a decimal with zero
    // seconds, which some cameras do, by rebuilding from the total.
    std::string FormatCoordinate(const std::vector<double>& dms, const std::string& ref) {
        if (dms.empty()) return "";
        double total = dms[0] + (dms.size() > 1 ? dms[1] / 60.0 : 0) + (dms.size() > 2 ? dms[2] / 3600.0 : 0);
        if (!std::isfinite(total) || total < 0 || total > 180) return "";   // not a coordinate
        const int deg = static_cast<int>(total);
        const double minutesFull = (total - deg) * 60.0;
        int minutes = static_cast<int>(minutesFull);
        double seconds = (minutesFull - minutes) * 60.0;
        if (seconds >= 59.995) { seconds = 0; if (++minutes == 60) minutes = 0; }
        std::string out = std::to_string(deg) + "\xC2\xB0 " + std::to_string(minutes) + "\xE2\x80\xB2 " +
                          FormatNumber(seconds, 2) + "\xE2\x80\xB3";
        std::string hemisphere = Trim(ref);
        if (!hemisphere.empty()) {
            hemisphere = hemisphere.substr(0, 1);
            out += " " + hemisphere;
            if (hemisphere == "S" || hemisphere == "W") total = -total;
        }
        return out + " (" + FormatNumber(total, 6) + "\xC2\xB0)";
    }

    const char* OrientationText(int code) {
        switch (code) {
            case 1: return "Normal";
            case 2: return "Mirrored horizontally";
            case 3: return "Rotated 180\xC2\xB0";
            case 4: return "Mirrored vertically";
            case 5: return "Mirrored horizontally, rotated 90\xC2\xB0 counter-clockwise";
            case 6: return "Rotated 90\xC2\xB0 clockwise";
            case 7: return "Mirrored horizontally, rotated 90\xC2\xB0 clockwise";
            case 8: return "Rotated 90\xC2\xB0 counter-clockwise";
            default: return nullptr;
        }
    }

    // libexif's reading of a coded value, when it is the meaning rather than
    // a number or its complaint about an unknown code.
    bool IsMeaningfulReading(const std::string& reading) {
        if (reading.empty() || LooksNumeric(reading)) return false;
        bool letter = false;
        for (char c : reading) letter = letter || std::isalpha(static_cast<unsigned char>(c));
        if (!letter) return false;   // "?", "-", punctuation: says nothing
        return reading.find("Internal error") == std::string::npos &&
               reading.find("unknown") == std::string::npos &&
               reading.find("Unknown value") == std::string::npos;
    }

    std::string StripSuffix(std::string s, char c) {
        if (!s.empty() && s.back() == c) s.pop_back();
        return s;
    }

    struct ExifValue { std::string value, reading; };

} // namespace

void SplitExifString(const std::string& text, std::string& value, std::string& reading) {
    value = text;
    reading.clear();
    if (text.size() < 8 || text.back() != ')' || text.rfind(" bytes)") != text.size() - 7) return;

    // The parenthesis that opens the annotation, counting depth so that
    // parentheses inside the value ("(c) ACME") are not mistaken for it.
    std::size_t open = std::string::npos;
    int depth = 0;
    for (std::size_t i = text.size(); i-- > 0;) {
        if (text[i] == ')') { ++depth; continue; }
        if (text[i] != '(') continue;
        if (--depth == 0) { open = i; break; }
    }
    if (open == std::string::npos) return;

    // The annotation ends in ", <Type>, <N> components, <M> bytes"; what is in
    // front of those three is the reading, commas and all ("51, 30,  0").
    std::string annotation = text.substr(open + 1, text.size() - open - 2);
    for (int k = 0; k < 3; ++k) {
        const std::size_t comma = annotation.rfind(", ");
        if (comma == std::string::npos) { annotation.clear(); break; }
        annotation.erase(comma);
    }
    value = Trim(text.substr(0, open));
    reading = Trim(annotation);
}

std::vector<DecodedTag> HumanizeExif(const std::vector<ExifField>& fields) {
    // Index by "<ifd>:<name>" so a value can consult the field that
    // qualifies it (GPSLatitudeRef, ResolutionUnit, ...).
    std::map<std::string, ExifValue> byKey;
    for (const auto& f : fields) {
        ExifValue v;
        SplitExifString(f.text, v.value, v.reading);
        byKey[std::to_string(f.ifd) + ":" + f.name] = v;
    }
    auto lookup = [&](int ifd, const char* name) -> const ExifValue* {
        auto it = byKey.find(std::to_string(ifd) + ":" + name);
        return it == byKey.end() ? nullptr : &it->second;
    };

    TagList out;
    for (const auto& f : fields) {
        const ExifValue& ev = byKey[std::to_string(f.ifd) + ":" + f.name];
        const std::string& name = f.name;
        const std::string& raw = ev.value;
        const std::string& reading = ev.reading;
        std::string label = f.ifd == 1 ? "Thumbnail " + name : name;
        std::string value;
        double num = 0;
        std::vector<double> nums;

        // Folded into the value they qualify, or file structure, not content.
        if (name == "GPSLatitudeRef" || name == "GPSLongitudeRef" || name == "GPSAltitudeRef" ||
            name == "GPSImgDirectionRef" || name == "GPSDestBearingRef" || name == "GPSSpeedRef" ||
            name == "GPSTrackRef" || name == "ResolutionUnit" || name == "FocalPlaneResolutionUnit" ||
            name == "JPEGInterchangeFormat" || name == "JPEGInterchangeFormatLength" ||
            name == "StripOffsets" || name == "StripByteCounts" || name == "RowsPerStrip") {
            continue;
        }

        if (name == "XResolution" || name == "YResolution" ||
            name == "FocalPlaneXResolution" || name == "FocalPlaneYResolution") {
            if (!ParseRational(raw, num) || num <= 0) continue;   // 0/1: not set
            const bool focalPlane = name.rfind("FocalPlane", 0) == 0;
            const ExifValue* unit = lookup(f.ifd, focalPlane ? "FocalPlaneResolutionUnit" : "ResolutionUnit");
            const std::string u = unit ? unit->value : "2";   // EXIF's default is inches
            value = FormatNumber(num, 2) + (u == "3" ? " dots/cm" : u == "1" ? "" : " dpi");
        } else if (name == "ExposureTime") {
            if (ParseRational(raw, num)) value = FormatExposure(num);
        } else if (name == "ShutterSpeedValue") {
            if (ParseRational(raw, num)) value = FormatExposure(std::pow(2.0, -num));
        } else if (name == "FNumber") {
            if (ParseRational(raw, num)) value = FormatFNumber(num);
        } else if (name == "ApertureValue" || name == "MaxApertureValue") {
            if (ParseRational(raw, num)) value = FormatFNumber(std::pow(2.0, num / 2.0));
        } else if (name == "ExposureBiasValue" || name == "BrightnessValue") {
            if (ParseRational(raw, num)) value = FormatEv(num);
        } else if (name == "FocalLength") {
            if (ParseRational(raw, num)) value = FormatNumber(num, 1) + " mm";
        } else if (name == "FocalLengthIn35mmFilm") {
            if (IsInteger(raw) && raw != "0") value = raw + " mm";
            else continue;
        } else if (name == "SubjectDistance") {
            if (ParseRational(raw, num)) value = num > 0 ? FormatNumber(num, 2) + " m" : "";
        } else if (name == "DigitalZoomRatio") {
            if (!ParseRational(raw, num) || num <= 0) continue;   // 0: not used
            value = FormatNumber(num, 2) + "\xC3\x97";
        } else if (name == "ISOSpeedRatings" || name == "PhotographicSensitivity") {
            value = IsInteger(raw) ? "ISO " + raw : raw;
        } else if (name == "LensSpecification" || name == "LensInfo") {
            if (ParseRationals(raw, nums) && nums.size() == 4 && nums[0] > 0) {
                value = FormatNumber(nums[0], 1);
                if (nums[1] > nums[0]) value += "\xE2\x80\x93" + FormatNumber(nums[1], 1);
                value += " mm";
                if (nums[2] > 0) {
                    value += " " + FormatFNumber(nums[2]);
                    if (nums[3] > nums[2]) value += "\xE2\x80\x93" + FormatNumber(nums[3], 1);
                }
            }
        } else if (name == "Orientation") {
            const char* text = IsInteger(raw) && raw.size() < 4 ? OrientationText(std::stoi(raw)) : nullptr;
            value = text ? text : raw;
        } else if (name == "GPSLatitude" || name == "GPSLongitude" ||
                   name == "GPSDestLatitude" || name == "GPSDestLongitude") {
            const std::string refName = name + "Ref";
            const ExifValue* ref = lookup(f.ifd, refName.c_str());
            if (ParseRationals(raw, nums)) value = FormatCoordinate(nums, ref ? ref->value : "");
        } else if (name == "GPSAltitude") {
            if (ParseRational(raw, num)) {
                const ExifValue* ref = lookup(f.ifd, "GPSAltitudeRef");
                // libvips writes the reference as libexif reads it
                // ("Sea level" / "Sea level reference"); 1 means below.
                const bool below = ref && (ref->value == "1" || ref->value.find("reference") != std::string::npos);
                value = FormatNumber(num, 1) + " m" + (below ? " below sea level" : "");
            }
        } else if (name == "GPSTimeStamp") {
            if (ParseRationals(raw, nums) && nums.size() == 3 && nums[0] >= 0 && nums[0] < 24 &&
                nums[1] >= 0 && nums[1] < 60 && nums[2] >= 0 && nums[2] < 61) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d UTC", static_cast<int>(nums[0]),
                              static_cast<int>(nums[1]), static_cast<int>(std::lround(nums[2])));
                value = buf;
            }
        } else if (name == "GPSImgDirection" || name == "GPSDestBearing" || name == "GPSTrack") {
            if (ParseRational(raw, num)) {
                const std::string refName = name + "Ref";
                const ExifValue* ref = lookup(f.ifd, refName.c_str());
                value = FormatNumber(num, 1) + "\xC2\xB0";
                if (ref && ref->value == "T") value += " (true north)";
                else if (ref && ref->value == "M") value += " (magnetic north)";
            }
        } else if (name == "GPSSpeed") {
            if (ParseRational(raw, num)) {
                const ExifValue* ref = lookup(f.ifd, "GPSSpeedRef");
                const std::string r = ref ? ref->value : "K";
                value = FormatNumber(num, 1) + (r == "M" ? " mph" : r == "N" ? " knots" : " km/h");
            }
        } else if (name == "GPSDateStamp") {
            value = raw;
            if (value.size() == 10 && value[4] == ':' && value[7] == ':') { value[4] = '-'; value[7] = '-'; }
        } else if (name.rfind("DateTime", 0) == 0) {
            // "2026:09:20 14:32:11" -> "2026-09-20 14:32:11"
            value = raw;
            if (value.size() >= 10 && value[4] == ':' && value[7] == ':') { value[4] = '-'; value[7] = '-'; }
            if (value.rfind("0000-00-00", 0) == 0) continue;   // placeholder for "unknown"
        } else if (name == "ExifVersion" || name == "FlashpixVersion" || name == "InteroperabilityVersion") {
            const std::size_t space = raw.rfind(' ');
            value = space != std::string::npos && LooksNumeric(raw.substr(space + 1)) ? raw.substr(space + 1) : raw;
        }

        if (value.empty()) {
            // Generic: a coded number shows its meaning; a rational becomes a
            // number; anything else keeps the value, plus libexif's reading
            // when that says something the value does not.
            if (IsInteger(raw) && IsMeaningfulReading(reading)) {
                value = StripSuffix(reading, '.');
            } else if (ParseRational(raw, num)) {
                value = FormatNumber(num, 4);
            } else if (!reading.empty() && reading != raw && reading.size() <= 40 &&
                       reading.find('(') == std::string::npos && !LooksNumeric(reading) &&
                       IsMeaningfulReading(reading)) {
                value = raw + " (" + reading + ")";
            } else {
                value = raw;
            }
        }
        out.Add(label, value);
    }
    return out.Take();
}

// ===== DISPLAY NAMES =====

namespace {

    const std::map<std::string, std::string>& ExifNames() {
        static const std::map<std::string, std::string> names = {
            {"Make", "Camera make"}, {"Model", "Camera model"},
            {"BodySerialNumber", "Camera serial number"}, {"CameraOwnerName", "Camera owner"},
            {"LensMake", "Lens make"}, {"LensModel", "Lens"}, {"LensSerialNumber", "Lens serial number"},
            {"LensSpecification", "Lens specification"}, {"LensInfo", "Lens specification"},
            {"Software", "Software"}, {"Artist", "Artist"}, {"Copyright", "Copyright"},
            {"ImageDescription", "Description"}, {"UserComment", "Comment"}, {"ImageUniqueID", "Image ID"},
            {"DateTime", "Date modified"}, {"DateTimeOriginal", "Date taken"},
            {"DateTimeDigitized", "Date digitized"}, {"OffsetTime", "Time zone (modified)"},
            {"OffsetTimeOriginal", "Time zone (taken)"}, {"OffsetTimeDigitized", "Time zone (digitized)"},
            {"SubSecTime", "Sub-second (modified)"}, {"SubSecTimeOriginal", "Sub-second (taken)"},
            {"SubSecTimeDigitized", "Sub-second (digitized)"},
            {"ExposureTime", "Exposure time"}, {"FNumber", "F-number"}, {"ApertureValue", "Aperture"},
            {"MaxApertureValue", "Max aperture"}, {"ShutterSpeedValue", "Shutter speed"},
            {"BrightnessValue", "Brightness"}, {"ExposureBiasValue", "Exposure compensation"},
            {"ExposureProgram", "Exposure program"}, {"ExposureMode", "Exposure mode"},
            {"ExposureIndex", "Exposure index"}, {"ISOSpeedRatings", "ISO"},
            {"PhotographicSensitivity", "ISO"}, {"RecommendedExposureIndex", "Recommended exposure index"},
            {"SensitivityType", "Sensitivity type"}, {"MeteringMode", "Metering mode"}, {"Flash", "Flash"},
            {"FlashEnergy", "Flash energy"}, {"FocalLength", "Focal length"},
            {"FocalLengthIn35mmFilm", "Focal length (35 mm equivalent)"}, {"DigitalZoomRatio", "Digital zoom"},
            {"SubjectDistance", "Subject distance"}, {"SubjectDistanceRange", "Subject distance range"},
            {"WhiteBalance", "White balance"}, {"LightSource", "Light source"}, {"ColorSpace", "Colour space"},
            {"SceneCaptureType", "Scene type"}, {"SceneType", "Scene source"}, {"CustomRendered", "Processing"},
            {"Contrast", "Contrast"}, {"Saturation", "Saturation"}, {"Sharpness", "Sharpness"},
            {"GainControl", "Gain control"}, {"SensingMethod", "Sensor type"}, {"FileSource", "File source"},
            {"Orientation", "Orientation"}, {"XResolution", "Horizontal resolution"},
            {"YResolution", "Vertical resolution"}, {"FocalPlaneXResolution", "Focal plane horizontal resolution"},
            {"FocalPlaneYResolution", "Focal plane vertical resolution"},
            {"PixelXDimension", "Image width"}, {"PixelYDimension", "Image height"},
            {"ImageWidth", "Image width"}, {"ImageLength", "Image height"},
            {"YCbCrPositioning", "YCbCr positioning"}, {"YCbCrSubSampling", "YCbCr subsampling"},
            {"ComponentsConfiguration", "Components configuration"}, {"Compression", "Compression"},
            {"ExifVersion", "EXIF version"}, {"FlashpixVersion", "FlashPix version"},
            {"InteroperabilityIndex", "Interoperability index"},
            {"InteroperabilityVersion", "Interoperability version"}, {"MakerNote", "Maker note"},
            {"GPSLatitude", "Latitude"}, {"GPSLongitude", "Longitude"}, {"GPSAltitude", "Altitude"},
            {"GPSTimeStamp", "GPS time"}, {"GPSDateStamp", "GPS date"}, {"GPSImgDirection", "Camera direction"},
            {"GPSSpeed", "Speed"}, {"GPSTrack", "Direction of travel"}, {"GPSVersionID", "GPS version"},
            {"GPSMapDatum", "Map datum"}, {"GPSSatellites", "GPS satellites"}, {"GPSStatus", "GPS status"},
            {"GPSMeasureMode", "GPS measure mode"}, {"GPSDOP", "GPS precision (DOP)"},
            {"GPSProcessingMethod", "GPS method"}, {"GPSAreaInformation", "GPS area"},
            {"GPSDestLatitude", "Destination latitude"}, {"GPSDestLongitude", "Destination longitude"},
            {"GPSDestBearing", "Destination bearing"}, {"GPSDestDistance", "Destination distance"},
            {"GPSHPositioningError", "GPS accuracy"}, {"GPSDifferential", "GPS differential correction"},
        };
        return names;
    }

    const std::map<std::string, std::string>& IptcNames() {
        static const std::map<std::string, std::string> names = {
            {"Object Name", "Title"}, {"By-line", "Author"}, {"By-line Title", "Author title"},
            {"Caption/Abstract", "Caption"}, {"Writer/Editor", "Caption writer"},
            {"Copyright Notice", "Copyright"}, {"Country Name", "Country"},
            {"Province/State", "State/Province"}, {"Sub-location", "Location"},
            {"Originating Program", "Created with"}, {"Special Instructions", "Instructions"},
            {"Original Transmission Reference", "Job ID"},
        };
        return names;
    }

    const std::map<std::string, std::string>& XmpNames() {
        static const std::map<std::string, std::string> names = {
            {"dc:title", "Title"}, {"dc:description", "Description"}, {"dc:subject", "Keywords"},
            {"dc:creator", "Creator"}, {"dc:rights", "Rights"}, {"dc:format", "Format"},
            {"dc:publisher", "Publisher"}, {"dc:contributor", "Contributor"}, {"dc:date", "Date"},
            {"dc:language", "Language"}, {"dc:identifier", "Identifier"}, {"dc:source", "Source"},
            {"xmp:Rating", "Rating"}, {"xmp:Label", "Label"}, {"xmp:CreatorTool", "Created with"},
            {"xmp:CreateDate", "Date created"}, {"xmp:ModifyDate", "Date modified"},
            {"xmp:MetadataDate", "Metadata date"}, {"xmp:Nickname", "Nickname"},
            {"xmpRights:WebStatement", "Licence URL"}, {"xmpRights:UsageTerms", "Usage terms"},
            {"xmpRights:Marked", "Copyrighted"}, {"xmpRights:Owner", "Rights owner"},
            {"photoshop:City", "City"}, {"photoshop:State", "State/Province"}, {"photoshop:Country", "Country"},
            {"photoshop:Headline", "Headline"}, {"photoshop:Credit", "Credit"}, {"photoshop:Source", "Source"},
            {"photoshop:DateCreated", "Date created"}, {"photoshop:AuthorsPosition", "Author title"},
            {"photoshop:CaptionWriter", "Caption writer"}, {"photoshop:Instructions", "Instructions"},
            {"photoshop:TransmissionReference", "Job ID"}, {"photoshop:Category", "Category"},
            {"photoshop:ColorMode", "Colour mode"}, {"photoshop:ICCProfile", "ICC profile"},
            {"Iptc4xmpCore:Location", "Location"}, {"Iptc4xmpCore:CountryCode", "Country code"},
            {"Iptc4xmpCore:CreatorContactInfo", "Creator contact"},
            {"Iptc4xmpCore:CiAdrExtadr", "Address"}, {"Iptc4xmpCore:CiAdrCity", "City"},
            {"Iptc4xmpCore:CiAdrRegion", "Region"}, {"Iptc4xmpCore:CiAdrPcode", "Postcode"},
            {"Iptc4xmpCore:CiAdrCtry", "Country"}, {"Iptc4xmpCore:CiEmailWork", "Email"},
            {"Iptc4xmpCore:CiTelWork", "Phone"}, {"Iptc4xmpCore:CiUrlWork", "Website"},
            {"xmpMM:DocumentID", "Document ID"}, {"xmpMM:InstanceID", "Instance ID"},
            {"xmpMM:OriginalDocumentID", "Original document ID"}, {"xmpMM:History", "History"},
            {"xmpMM:DerivedFrom", "Derived from"}, {"stEvt:action", "Action"}, {"stEvt:when", "When"},
            {"stEvt:softwareAgent", "Software"}, {"stEvt:changed", "Changed"},
            {"stEvt:instanceID", "Instance ID"}, {"tiff:Make", "Camera make"}, {"tiff:Model", "Camera model"},
            {"aux:Lens", "Lens"}, {"aux:SerialNumber", "Camera serial number"},
            {"crs:Version", "Camera Raw version"}, {"lr:hierarchicalSubject", "Keyword hierarchy"},
        };
        return names;
    }

    const std::map<std::string, std::string>& OtherNames() {
        static const std::map<std::string, std::string> names = {
            {"jpeg-chroma-subsample", "Chroma subsampling"}, {"jpeg-multiscan", "Progressive"},
            {"icc-profile-data", "ICC profile"}, {"exif-data", "EXIF block"},
            {"iptc-data", "IPTC block"}, {"xmp-data", "XMP block"},
            {"heif-primary", "Primary image"}, {"heif-compression", "Compression"},
            {"interlaced", "Interlaced"}, {"palette", "Palette"}, {"bits-per-sample", "Bits per sample"},
            {"gif-loop", "Loop count"}, {"gif-palette", "GIF palette"}, {"loop", "Loop count"},
            {"delay", "Frame delays"}, {"n-pages", "Pages"}, {"page-height", "Page height"},
            {"orientation", "Orientation"}, {"background", "Background"},
        };
        return names;
    }

    bool IsUpper(char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; }
    bool IsLower(char c) { return std::islower(static_cast<unsigned char>(c)) != 0; }
    bool IsDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

    // "SensingMethod" -> "Sensing method", "GPSHPositioningError" -> "GPS H
    // positioning error", "jpeg-multiscan" -> "Jpeg multiscan". A word in
    // capitals (an acronym) keeps them; the first word is capitalised.
    std::string SplitWords(const std::string& raw) {
        std::vector<std::string> words;
        std::string cur;
        auto flush = [&] { if (!cur.empty()) { words.push_back(cur); cur.clear(); } };
        for (std::size_t i = 0; i < raw.size(); ++i) {
            const char c = raw[i];
            if (c == '-' || c == '_' || c == ' ') { flush(); continue; }
            if (!cur.empty()) {
                const char prev = cur.back();
                const bool next = i + 1 < raw.size();
                if (IsUpper(c) && (IsLower(prev) || IsDigit(prev))) flush();                        // aB -> a|B
                else if (IsUpper(c) && IsUpper(prev) && next && IsLower(raw[i + 1])) flush();       // ABc -> A|Bc
                else if (IsDigit(c) != IsDigit(prev) && !(IsDigit(c) && IsUpper(prev) && cur.size() == 1)) flush();
            }
            cur.push_back(c);
        }
        flush();

        std::string out;
        for (std::size_t w = 0; w < words.size(); ++w) {
            std::string word = words[w];
            // All capitals (GPS, ISO, a lone X or H) stays as written.
            bool acronym = !word.empty() && IsUpper(word[0]);
            for (char c : word) acronym = acronym && (IsUpper(c) || IsDigit(c));
            if (!acronym) {
                for (char& c : word) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (w == 0 && !word.empty()) word[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
            if (w) out += ' ';
            out += word;
        }
        return out;
    }

    std::string ExifName(const std::string& key) {
        auto it = ExifNames().find(key);
        return it != ExifNames().end() ? it->second : SplitWords(key);
    }

    // One step of an XMP path: "Iptc4xmpCore:CiAdrCity" or "xmpMM:History[2]".
    std::string XmpStepName(const std::string& step) {
        std::string name = step, index;
        const std::size_t bracket = step.find('[');
        if (bracket != std::string::npos && step.back() == ']') {
            name = step.substr(0, bracket);
            index = step.substr(bracket + 1, step.size() - bracket - 2);
        }
        auto it = XmpNames().find(name);
        std::string out;
        if (it != XmpNames().end()) {
            out = it->second;
        } else {
            const std::size_t colon = name.find(':');
            out = SplitWords(colon == std::string::npos ? name : name.substr(colon + 1));
        }
        return index.empty() ? out : out + " " + index;
    }

} // namespace

std::string FriendlyTagName(const std::string& group, const std::string& key) {
    if (key.empty()) return key;

    if (group == "EXIF") {
        static const std::string thumb = "Thumbnail ";
        if (key.rfind(thumb, 0) == 0) return thumb + ExifName(key.substr(thumb.size()));
        return ExifName(key);
    }
    if (group == "IPTC") {
        auto it = IptcNames().find(key);
        if (it != IptcNames().end()) return it->second;
        // The IIM names are in title case ("Date Created"); the rest of the
        // panel is in sentence case.
        std::string out = key;
        bool wordStart = false;
        for (std::size_t i = 1; i < out.size(); ++i) {
            if (out[i - 1] == ' ') wordStart = true;
            const bool acronym = i + 1 < out.size() && IsUpper(out[i]) && IsUpper(out[i + 1]);
            if (wordStart && IsUpper(out[i]) && !acronym)
                out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
            wordStart = false;
        }
        return out;
    }
    if (group == "XMP") {
        std::string out;
        std::size_t start = 0;
        while (start <= key.size()) {
            std::size_t slash = key.find('/', start);
            if (slash == std::string::npos) slash = key.size();
            if (slash > start) {
                if (!out.empty()) out += " \xE2\x80\xBA ";   // ›
                out += XmpStepName(key.substr(start, slash - start));
            }
            start = slash + 1;
        }
        return out.empty() ? key : out;
    }
    if (group == "Image") return key;   // already written for a person

    auto it = OtherNames().find(key);
    if (it != OtherNames().end()) return it->second;
    // PNG text chunks: "png-comment-0-Title" -> "Title".
    if (key.rfind("png-comment-", 0) == 0) {
        const std::size_t dash = key.find('-', 12);
        if (dash != std::string::npos && dash + 1 < key.size()) return key.substr(dash + 1);
    }
    return SplitWords(key);
}

} // namespace Header
} // namespace PixelFX
