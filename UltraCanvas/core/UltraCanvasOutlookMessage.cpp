// core/UltraCanvasOutlookMessage.cpp
// Outlook .msg reader, compressed-RTF decompressor and RTF-encapsulated HTML
// extractor - see UltraCanvasOutlookMessage.h.
// Version: 1.1.0 - messageId, inReplyTo
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasOutlookMessage.h"
#include "UltraCanvasCompoundFile.h"
#include "UltraCanvasTextUtils.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>

namespace UltraCanvas {

namespace {

// Property types ([MS-OXCDATA] 2.11.1) and the tags read here ([MS-OXPROPS]).
constexpr uint16_t kTypeInt32   = 0x0003;
constexpr uint16_t kTypeBoolean = 0x000B;
constexpr uint16_t kTypeString8 = 0x001E;
constexpr uint16_t kTypeUnicode = 0x001F;
constexpr uint16_t kTypeTime    = 0x0040;
constexpr uint16_t kTypeBinary  = 0x0102;

constexpr uint16_t kSubject              = 0x0037;
constexpr uint16_t kClientSubmitTime     = 0x0039;
constexpr uint16_t kSentRepresentingName = 0x0042;
constexpr uint16_t kSentRepresentingAddr = 0x0065;
constexpr uint16_t kSenderName           = 0x0C1A;
constexpr uint16_t kSenderEmail          = 0x0C1F;
constexpr uint16_t kRecipientType        = 0x0C15;
constexpr uint16_t kDisplayCc            = 0x0E03;
constexpr uint16_t kDisplayTo            = 0x0E04;
constexpr uint16_t kDeliveryTime         = 0x0E06;
constexpr uint16_t kBody                 = 0x1000;
constexpr uint16_t kRtfCompressed        = 0x1009;
constexpr uint16_t kHtml                 = 0x1013;
constexpr uint16_t kInternetMessageId    = 0x1035;
constexpr uint16_t kInReplyToId          = 0x1042;
constexpr uint16_t kDisplayName          = 0x3001;
constexpr uint16_t kCreationTime         = 0x3007;
constexpr uint16_t kLastModificationTime = 0x3008;
constexpr uint16_t kEmailAddress         = 0x3003;
constexpr uint16_t kAttachData           = 0x3701;
constexpr uint16_t kAttachFilename      = 0x3704;
constexpr uint16_t kAttachMethod         = 0x3705;
constexpr uint16_t kAttachLongFilename   = 0x3707;
constexpr uint16_t kAttachMimeTag        = 0x370E;
constexpr uint16_t kAttachContentId      = 0x3712;
constexpr uint16_t kSmtpAddress          = 0x39FE;
constexpr uint16_t kInternetCodepage     = 0x3FDE;
constexpr uint16_t kMessageCodepage      = 0x3FFD;
constexpr uint16_t kSenderSmtpAddress    = 0x5D01;
constexpr uint16_t kSentRepresentingSmtp = 0x5D02;
constexpr uint16_t kAttachmentHidden     = 0x7FFE;

constexpr int kAttachByValue    = 1;
constexpr int kAttachEmbeddedMsg = 5;

std::string Utf16LeToUtf8(const std::vector<uint8_t>& bytes) {
    std::string out;
    out.reserve(bytes.size() / 2);
    auto append = [&out](uint32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    };
    for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
        uint32_t unit = bytes[i] | (bytes[i + 1] << 8);
        if (unit >= 0xD800 && unit < 0xDC00 && i + 3 < bytes.size()) {
            const uint32_t low = bytes[i + 2] | (bytes[i + 3] << 8);
            if (low >= 0xDC00 && low < 0xE000) {
                unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        } else if (unit >= 0xD800 && unit < 0xE000) {
            unit = 0xFFFD;   // an unpaired surrogate
        }
        append(unit);
    }
    // A string property ends with its terminating NUL.
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

void AppendCodepoint(std::string& out, uint32_t cp) {
    std::vector<uint8_t> unit = { static_cast<uint8_t>(cp & 0xFF), static_cast<uint8_t>((cp >> 8) & 0xFF) };
    out += Utf16LeToUtf8(unit);
}

std::string StreamName(uint16_t id, uint16_t type) {
    char name[32];
    std::snprintf(name, sizeof(name), "__substg1.0_%04X%04X", id, type);
    return name;
}

uint32_t ReadU32(const std::vector<uint8_t>& b, size_t at) {
    return at + 4 <= b.size() ? (b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (uint32_t(b[at + 3]) << 24)) : 0;
}

// One storage of the message - the message itself, a recipient or an
// attachment: its variable-length properties are streams, its fixed-size
// ones entries of the properties stream after a header whose size depends
// on what the storage is ([MS-OXMSG] 2.4).
class PropertyStorage {
public:
    PropertyStorage(const UCCompoundFileReader& file, std::string path, size_t headerSize, int codePage)
        : file_(file), path_(std::move(path)), codePage_(codePage) {
        std::vector<uint8_t> props;
        if (!file_.ReadStreamAt(Path("__properties_version1.0"), props)) return;
        for (size_t at = headerSize; at + 16 <= props.size(); at += 16) {
            const uint32_t tag = ReadU32(props, at);
            const uint64_t value = ReadU32(props, at + 8) | (uint64_t(ReadU32(props, at + 12)) << 32);
            fixed_[tag] = value;
        }
    }
    void SetCodePage(int codePage) { codePage_ = codePage; }

    bool Fixed(uint16_t id, uint16_t type, uint64_t& value) const {
        auto it = fixed_.find((uint32_t(id) << 16) | type);
        if (it == fixed_.end()) return false;
        value = it->second;
        return true;
    }
    int Int(uint16_t id, int fallback = 0) const {
        uint64_t value = 0;
        return Fixed(id, kTypeInt32, value) ? static_cast<int>(static_cast<uint32_t>(value)) : fallback;
    }
    // A string property: UTF-16, else 8-bit in the code page.
    std::string String(uint16_t id) const {
        std::vector<uint8_t> bytes;
        if (file_.ReadStreamAt(Path(StreamName(id, kTypeUnicode)), bytes)) return Utf16LeToUtf8(bytes);
        if (file_.ReadStreamAt(Path(StreamName(id, kTypeString8)), bytes)) {
            while (!bytes.empty() && bytes.back() == 0) bytes.pop_back();
            return DecodeWindowsCodePage(bytes.data(), bytes.size(), codePage_);
        }
        return std::string();
    }
    bool Binary(uint16_t id, std::vector<uint8_t>& out) const {
        return file_.ReadStreamAt(Path(StreamName(id, kTypeBinary)), out);
    }
    bool HasStorage(const std::string& name) const {
        for (const auto& storage : file_.StorageNamesAt(path_)) {
            if (storage.size() == name.size() &&
                std::equal(storage.begin(), storage.end(), name.begin(), [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                })) return true;
        }
        return false;
    }
    std::string Path(const std::string& name) const { return path_.empty() ? name : path_ + "/" + name; }

private:
    const UCCompoundFileReader& file_;
    std::string path_;
    int codePage_;
    std::map<uint32_t, uint64_t> fixed_;
};

// "Name <address>", or whichever of the two there is. An Exchange address
// ("/O=EXCHANGE/OU=...") is no address a reader can use.
std::string Mailbox(const std::string& name, std::string address) {
    if (!address.empty() && address[0] == '/') address.clear();
    if (name.empty()) return address;
    if (address.empty() || address == name) return name;
    return name + " <" + address + ">";
}

// A FILETIME (100 ns since 1601, UTC) in RFC 5322 form.
std::string FormatFileTime(uint64_t filetime) {
    const int64_t seconds = static_cast<int64_t>(filetime / 10000000ULL) - 11644473600LL;
    int64_t days = seconds / 86400;
    int64_t rest = seconds % 86400;
    if (rest < 0) { rest += 86400; --days; }
    // Civil date from days since 1970-01-01 (H. Hinnant's algorithm).
    const int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    const int month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    const int64_t year = yoe + era * 400 + (month <= 2 ? 1 : 0);
    static const char* const kDays[] = { "Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed" };
    static const char* const kMonths[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    char text[64];
    std::snprintf(text, sizeof(text), "%s, %d %s %lld %02d:%02d:%02d +0000",
                  kDays[((days % 7) + 7) % 7], day, kMonths[month - 1], static_cast<long long>(year),
                  static_cast<int>(rest / 3600), static_cast<int>(rest / 60 % 60), static_cast<int>(rest % 60));
    return text;
}

// Recipients: the message's __recip storages, in their order; the display
// lists (PR_DISPLAY_TO / _CC) when there are none.
void ReadRecipients(const UCCompoundFileReader& file, const PropertyStorage& message, int codePage,
                    UCOutlookMessage& out) {
    std::vector<std::string> storages = file.StorageNamesAt("");
    std::sort(storages.begin(), storages.end());
    for (const auto& name : storages) {
        if (name.rfind("__recip_version1.0_", 0) != 0) continue;
        PropertyStorage recipient(file, name, 8, codePage);
        std::string address = recipient.String(kSmtpAddress);
        if (address.empty()) address = recipient.String(kEmailAddress);
        const std::string shown = Mailbox(recipient.String(kDisplayName), address);
        if (shown.empty()) continue;
        switch (recipient.Int(kRecipientType, 1)) {
            case 2:  out.cc.push_back(shown); break;
            case 3:  break;   // Bcc: not a header the message carries
            default: out.to.push_back(shown); break;
        }
    }
    auto split = [](const std::string& list, std::vector<std::string>& into) {
        size_t start = 0;
        while (start < list.size()) {
            size_t end = list.find(';', start);
            if (end == std::string::npos) end = list.size();
            std::string item = list.substr(start, end - start);
            item.erase(0, item.find_first_not_of(' '));
            item.erase(item.find_last_not_of(' ') + 1);
            if (!item.empty()) into.push_back(item);
            start = end + 1;
        }
    };
    if (out.to.empty()) split(message.String(kDisplayTo), out.to);
    if (out.cc.empty()) split(message.String(kDisplayCc), out.cc);
}

void ReadAttachments(const UCCompoundFileReader& file, int codePage, UCOutlookMessage& out) {
    std::vector<std::string> storages = file.StorageNamesAt("");
    std::sort(storages.begin(), storages.end());
    for (const auto& name : storages) {
        if (name.rfind("__attach_version1.0_", 0) != 0) continue;
        PropertyStorage storage(file, name, 8, codePage);
        UCOutlookAttachment attachment;
        attachment.filename = storage.String(kAttachLongFilename);
        if (attachment.filename.empty()) attachment.filename = storage.String(kAttachFilename);
        if (attachment.filename.empty()) attachment.filename = storage.String(kDisplayName);
        attachment.mediaType = storage.String(kAttachMimeTag);
        std::transform(attachment.mediaType.begin(), attachment.mediaType.end(), attachment.mediaType.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        attachment.contentId = storage.String(kAttachContentId);
        if (attachment.contentId.size() > 1 && attachment.contentId.front() == '<' &&
            attachment.contentId.back() == '>')
            attachment.contentId = attachment.contentId.substr(1, attachment.contentId.size() - 2);
        uint64_t hidden = 0;
        attachment.hidden = storage.Fixed(kAttachmentHidden, kTypeBoolean, hidden) && (hidden & 0xFF) != 0;
        const int method = storage.Int(kAttachMethod, kAttachByValue);
        if (method == kAttachEmbeddedMsg || storage.HasStorage("__substg1.0_3701000D")) {
            attachment.embeddedMessage = true;
            if (attachment.filename.empty()) attachment.filename = "Attached message";
        } else {
            storage.Binary(kAttachData, attachment.data);
        }
        out.attachments.push_back(std::move(attachment));
    }
}

// What a byte string of the HTML body is: text in the code page the
// message's HTML was written in.
std::string DecodeHtmlBytes(const std::vector<uint8_t>& bytes, int codePage) {
    size_t size = bytes.size();
    while (size > 0 && bytes[size - 1] == 0) --size;
    return DecodeWindowsCodePage(bytes.data(), size, codePage);
}

} // namespace

// ===== COMPRESSED RTF ([MS-OXRTFCP]) =========================================

bool UCDecompressRtf(const std::vector<uint8_t>& compressed, std::string& rtf) {
    rtf.clear();
    if (compressed.size() < 16) return false;
    const uint32_t compSize = ReadU32(compressed, 0);
    const uint32_t rawSize = ReadU32(compressed, 4);
    const uint32_t compType = ReadU32(compressed, 8);
    const size_t end = std::min<size_t>(compressed.size(), static_cast<size_t>(compSize) + 4);
    if (compType == 0x414C454Du) {                 // "MELA": stored as it is
        const size_t available = end > 16 ? end - 16 : 0;
        rtf.assign(compressed.begin() + 16, compressed.begin() + 16 +
                   static_cast<std::ptrdiff_t>(std::min<size_t>(rawSize, available)));
        return true;
    }
    if (compType != 0x75465A4Cu) return false;      // "LZFu"

    static const char kInitialDictionary[] =
        "{\\rtf1\\ansi\\mac\\deff0\\deftab720{\\fonttbl;}{\\f0\\fnil \\froman \\fswiss \\fmodern "
        "\\fscript \\fdecor MS Sans SerifSymbolArialTimes New RomanCourier{\\colortbl\\red0\\green0\\blue0"
        "\r\n\\par \\pard\\plain\\f0\\fs20\\b\\i\\u\\tab\\tx";
    constexpr size_t kDictionarySize = 4096;
    uint8_t dictionary[kDictionarySize] = {};
    const size_t prefill = sizeof(kInitialDictionary) - 1;   // 207
    std::copy(kInitialDictionary, kInitialDictionary + prefill, dictionary);
    size_t write = prefill;

    rtf.reserve(rawSize);
    size_t at = 16;
    while (at < end && rtf.size() < rawSize) {
        const uint8_t control = compressed[at++];
        for (int bit = 0; bit < 8 && at < end && rtf.size() < rawSize; ++bit) {
            if (!(control & (1 << bit))) {
                const uint8_t c = compressed[at++];
                rtf.push_back(static_cast<char>(c));
                dictionary[write] = c;
                write = (write + 1) % kDictionarySize;
                continue;
            }
            if (at + 1 >= end) return true;
            const uint16_t reference = static_cast<uint16_t>((compressed[at] << 8) | compressed[at + 1]);
            at += 2;
            const size_t offset = reference >> 4;
            const size_t length = (reference & 0x0F) + 2;
            if (offset == write) return true;       // the end marker
            for (size_t i = 0; i < length && rtf.size() < rawSize; ++i) {
                const uint8_t c = dictionary[(offset + i) % kDictionarySize];
                rtf.push_back(static_cast<char>(c));
                dictionary[write] = c;
                write = (write + 1) % kDictionarySize;
            }
        }
    }
    return true;
}

// ===== HTML ENCAPSULATED IN RTF ([MS-OXRTFEX]) ===============================

bool UCHtmlFromEncapsulatedRtf(const std::string& rtf, std::string& html) {
    html.clear();
    // The header says it: \fromhtml1 among the first control words.
    const size_t marker = rtf.find("\\fromhtml1");
    if (rtf.compare(0, 5, "{\\rtf") != 0 || marker == std::string::npos) return false;
    {
        size_t firstText = rtf.find("{\\*\\htmltag");
        if (firstText != std::string::npos && marker > firstText) return false;
    }

    struct Group {
        bool htmltag = false;     // an \*\htmltag group: HTML as written
        bool skip = false;        // a destination that is not part of the HTML
        bool htmlrtf = false;     // \htmlrtf: RTF-only content, not the HTML's
        int uc = 1;               // characters a \u's fallback takes
    };
    std::vector<Group> stack(1);
    int codePage = 1252;
    std::string pending;          // 8-bit text not yet decoded
    int skipFallback = 0;         // fallback characters of a \u still to skip
    bool groupStart = false;      // the next token is the group's first
    bool starred = false;         // a \* opened this group

    auto flush = [&] {
        if (pending.empty()) return;
        html += DecodeWindowsCodePage(reinterpret_cast<const uint8_t*>(pending.data()), pending.size(), codePage);
        pending.clear();
    };
    auto emitting = [&] {
        const Group& g = stack.back();
        return !g.skip && (g.htmltag || !g.htmlrtf);
    };
    auto byte = [&](uint8_t c) {
        if (skipFallback > 0) { --skipFallback; return; }
        if (emitting()) pending.push_back(static_cast<char>(c));
    };

    static const char* const kSkippedDestinations[] = {
        "fonttbl", "colortbl", "stylesheet", "info", "pict", "header", "footer",
        "listtable", "listoverridetable", "rsidtbl", "generator", "xmlnstbl", "themedata",
        "colorschememapping", "latentstyles", "datastore", "object", "fldinst"
    };

    for (size_t i = 0; i < rtf.size();) {
        const char c = rtf[i];
        if (c == '{') {
            stack.push_back(stack.back());
            stack.back().htmltag = false;
            groupStart = true;
            starred = false;
            ++i;
            continue;
        }
        if (c == '}') {
            if (stack.size() > 1) stack.pop_back();
            groupStart = false;
            ++i;
            continue;
        }
        if (c == '\r' || c == '\n') { ++i; continue; }
        if (c != '\\') {
            byte(static_cast<uint8_t>(c));
            groupStart = false;
            ++i;
            continue;
        }
        // A control symbol or a control word.
        if (i + 1 >= rtf.size()) break;
        const char next = rtf[i + 1];
        if (!std::isalpha(static_cast<unsigned char>(next))) {
            i += 2;
            switch (next) {
                case '*':
                    if (groupStart) starred = true;
                    continue;   // the group may still start with its word
                case '\'': {
                    auto hex = [](char h) -> int {
                        if (h >= '0' && h <= '9') return h - '0';
                        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                        return -1;
                    };
                    if (i + 1 < rtf.size() && hex(rtf[i]) >= 0 && hex(rtf[i + 1]) >= 0) {
                        byte(static_cast<uint8_t>(hex(rtf[i]) * 16 + hex(rtf[i + 1])));
                        i += 2;
                    }
                    break;
                }
                case '{': case '}': case '\\':
                    byte(static_cast<uint8_t>(next));
                    break;
                case '~':
                    if (emitting()) { flush(); html += "\xC2\xA0"; }
                    break;
                case '\r': case '\n':
                    if (emitting()) { flush(); html += "\r\n"; }
                    break;
                default:
                    break;   // \- \_ \: and the like
            }
            groupStart = false;
            continue;
        }
        size_t j = i + 1;
        while (j < rtf.size() && std::isalpha(static_cast<unsigned char>(rtf[j]))) ++j;
        const std::string word = rtf.substr(i + 1, j - i - 1);
        bool hasParam = false;
        long param = 0;
        bool negative = false;
        if (j < rtf.size() && rtf[j] == '-') { negative = true; ++j; }
        while (j < rtf.size() && std::isdigit(static_cast<unsigned char>(rtf[j]))) {
            hasParam = true;
            if (param < 1000000000L) param = param * 10 + (rtf[j] - '0');
            ++j;
        }
        if (negative) param = -param;
        if (j < rtf.size() && rtf[j] == ' ') ++j;   // the delimiter belongs to the word
        i = j;

        Group& g = stack.back();
        if (groupStart) {
            groupStart = false;
            if (starred) {
                // {\*\htmltagN ...}: the HTML; any other starred destination is not.
                if (word == "htmltag") { flush(); g.htmltag = true; g.htmlrtf = false; }
                else g.skip = true;
                starred = false;
                continue;
            }
            for (const char* skipped : kSkippedDestinations) {
                if (word == skipped) { g.skip = true; break; }
            }
            if (g.skip) continue;
        }
        if (g.skip) continue;

        if (word == "htmlrtf") { flush(); g.htmlrtf = !hasParam || param != 0; continue; }
        if (word == "ansicpg" && hasParam) { flush(); codePage = static_cast<int>(param); continue; }
        if (word == "uc" && hasParam) { g.uc = static_cast<int>(std::max(0L, param)); continue; }
        if (word == "u" && hasParam) {
            if (emitting()) {
                flush();
                AppendCodepoint(html, static_cast<uint32_t>(param < 0 ? param + 65536 : param));
            }
            skipFallback = g.uc;
            continue;
        }
        if (!emitting()) continue;
        if (word == "par" || word == "line") { flush(); html += "\r\n"; }
        else if (word == "tab") { flush(); html += "\t"; }
    }
    flush();
    return true;
}

// ===== THE MESSAGE ===========================================================

bool UCOutlookMessage::Load(const std::string& path, std::string& error) {
    std::ifstream in(PathFromUtf8(path), std::ios::binary);
    if (!in) {
        error = "the file cannot be opened";
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return LoadFromMemory(std::move(data), error);
}

bool UCOutlookMessage::LoadFromMemory(std::vector<uint8_t> data, std::string& error) {
    UCCompoundFileReader file;
    if (!file.OpenFromMemory(std::move(data))) {
        error = file.GetLastError().empty() ? "not an Outlook message" : file.GetLastError();
        return false;
    }
    *this = UCOutlookMessage();
    // The message's own properties: a 32-byte header before the fixed ones.
    PropertyStorage message(file, "", 32, 1252);
    int codePage = message.Int(kInternetCodepage, 0);
    if (codePage == 0) codePage = message.Int(kMessageCodepage, 1252);
    message.SetCodePage(codePage);

    subject = message.String(kSubject);
    messageId = message.String(kInternetMessageId);
    inReplyTo = message.String(kInReplyToId);
    std::string fromName = message.String(kSenderName);
    std::string fromAddress = message.String(kSenderSmtpAddress);
    if (fromAddress.empty()) fromAddress = message.String(kSenderEmail);
    if (fromName.empty() && fromAddress.empty()) {
        fromName = message.String(kSentRepresentingName);
        fromAddress = message.String(kSentRepresentingSmtp);
        if (fromAddress.empty()) fromAddress = message.String(kSentRepresentingAddr);
    }
    from = Mailbox(fromName, fromAddress);
    uint64_t time = 0;
    // Delivered, else sent; a draft was neither, and has when it was last
    // changed (else made) instead.
    if (message.Fixed(kDeliveryTime, kTypeTime, time) || message.Fixed(kClientSubmitTime, kTypeTime, time) ||
        message.Fixed(kLastModificationTime, kTypeTime, time) || message.Fixed(kCreationTime, kTypeTime, time))
        date = FormatFileTime(time);

    ReadRecipients(file, message, codePage, *this);

    bodyText = message.String(kBody);
    std::vector<uint8_t> bytes;
    if (message.Binary(kHtml, bytes)) {
        bodyHtml = DecodeHtmlBytes(bytes, codePage);
    } else {
        bodyHtml = message.String(kHtml);
    }
    if (bodyHtml.empty() && message.Binary(kRtfCompressed, bytes)) {
        std::string rtf;
        if (UCDecompressRtf(bytes, rtf)) UCHtmlFromEncapsulatedRtf(rtf, bodyHtml);
    }

    ReadAttachments(file, codePage, *this);

    if (subject.empty() && from.empty() && to.empty() && bodyText.empty() && bodyHtml.empty() &&
        attachments.empty()) {
        error = "the compound file holds no Outlook message";
        return false;
    }
    return true;
}

} // namespace UltraCanvas
