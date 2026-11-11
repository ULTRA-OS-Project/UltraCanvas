// core/UltraIM/UltraIMSearch.cpp
// UltraIM - what searching messages needs on the device: the matcher the
// providers share, the links in a text, case folding, and a search answered
// by reading a conversation's history page by page
// Version: 0.1.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework

#include "UltraIM/UltraIMCore.h"

#include <cstdint>
#include <memory>
#include <utility>

namespace {

// ---- UTF-8 -------------------------------------------------------------------

// The code point at text[i] and how many bytes it takes; a byte that is not
// the start of a well-formed sequence comes back as itself, one byte long,
// with wellFormed false, so broken text passes through unchanged.
char32_t Decode(const std::string& text, size_t i, size_t& length, bool& wellFormed) {
    const auto byte = [&text](size_t at) { return static_cast<unsigned char>(text[at]); };
    const unsigned char first = byte(i);
    length = 1;
    wellFormed = true;
    if (first < 0x80) return first;
    size_t count = 0;
    char32_t value = 0;
    if ((first & 0xE0) == 0xC0) {
        count = 2;
        value = first & 0x1F;
    } else if ((first & 0xF0) == 0xE0) {
        count = 3;
        value = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
        count = 4;
        value = first & 0x07;
    }
    if (count == 0 || i + count > text.size()) {
        wellFormed = false;
        return first;
    }
    for (size_t k = 1; k < count; ++k) {
        if ((byte(i + k) & 0xC0) != 0x80) {
            wellFormed = false;
            return first;
        }
        value = (value << 6) | (byte(i + k) & 0x3F);
    }
    length = count;
    return value;
}

void Encode(char32_t c, std::string& out) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

// The lower-case letter of an upper-case one, where the two are one code
// point each: Basic Latin, Latin-1, Latin Extended-A, Greek, Cyrillic.
char32_t Lower(char32_t c) {
    if (c < 0x80) return (c >= 'A' && c <= 'Z') ? c + 32 : c;
    if (c >= 0xC0 && c <= 0xDE) return c == 0xD7 ? c : c + 32;                  // À..Þ, not ×
    if (c >= 0x100 && c <= 0x17F) {
        if (c == 0x130) return 'i';                                              // İ
        if (c == 0x178) return 0xFF;                                             // Ÿ
        if (c == 0x17F) return 's';                                              // ſ
        if (c == 0x131 || c == 0x138 || c == 0x149) return c;                    // ı ĸ ŉ: lower case already
        // The pairs sit side by side, upper first: on even code points, but
        // on odd ones in the two stretches after a letter without a partner.
        const bool upperIsOdd = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
        const bool isUpper = upperIsOdd ? (c % 2 == 1) : (c % 2 == 0);
        return isUpper ? c + 1 : c;
    }
    if (c >= 0x391 && c <= 0x3A9) return c == 0x3A2 ? c : c + 32;               // Α..Ω
    if (c == 0x3C2) return 0x3C3;                                                // ς: σ at a word's end
    if (c >= 0x400 && c <= 0x40F) return c + 80;                                 // Ѐ..Џ
    if (c >= 0x410 && c <= 0x42F) return c + 32;                                 // А..Я
    return c;
}

bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool IsAsciiAlnum(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Whether text has `word` (lower-case ASCII) at i, in any case.
bool HasAt(const std::string& text, size_t i, const char* word) {
    for (size_t k = 0; word[k]; ++k) {
        if (i + k >= text.size()) return false;
        char c = text[i + k];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        if (c != word[k]) return false;
    }
    return true;
}

std::vector<std::string> Words(const std::string& folded) {
    std::vector<std::string> words;
    std::string word;
    for (char c : folded) {
        if (IsSpace(c)) {
            if (!word.empty()) words.push_back(std::move(word));
            word.clear();
        } else {
            word += c;
        }
    }
    if (!word.empty()) words.push_back(std::move(word));
    return words;
}

bool KindMatches(const UltraIMMessage& message, UltraIMSearchKind kind) {
    const UltraIMMessageType type = message.type;
    switch (kind) {
        case UltraIMSearchKind::AnyKind: return type != UltraIMMessageType::SystemEvent;
        case UltraIMSearchKind::Images:  return type == UltraIMMessageType::Image;
        case UltraIMSearchKind::Videos:  return type == UltraIMMessageType::Video;
        case UltraIMSearchKind::Media:   return type == UltraIMMessageType::Image || type == UltraIMMessageType::Video;
        case UltraIMSearchKind::Files:   return type == UltraIMMessageType::File;
        case UltraIMSearchKind::Links:
            return type != UltraIMMessageType::SystemEvent && !UltraIM_ExtractLinks(message.text).empty();
    }
    return false;
}

// ---- A search by reading history ------------------------------------------------

constexpr int kFetchPageSize = 100;
constexpr int kFetchRounds = 20;        // about two thousand messages a call

struct FetchSearch {
    UltraIMFetchPage      fetchPage;
    UltraIMMessageSearch  search;
    UltraIMSearchCallback onResult;
    UltraIMSearchPage     page;
    std::string           before;       // the oldest message read so far
    int                   limit = 50;
    int                   rounds = 0;
};

const std::string& IdOf(const UltraIMMessage& message) {
    return message.messageId.empty() ? message.clientMessageId : message.messageId;
}

void Finish(const std::shared_ptr<FetchSearch>& s, const std::string& nextCursor) {
    s->page.nextCursor = nextCursor;
    UltraIMSearchCallback done = std::move(s->onResult);
    s->fetchPage = nullptr;
    if (done) done(UltraIMResult::Ok(), s->page);
}

void FetchNext(const std::shared_ptr<FetchSearch>& s) {
    UltraIMFetchPage fetch = s->fetchPage;      // (a copy: Finish clears the member)
    fetch(s->search.conversationId, s->before, kFetchPageSize,
          [s](const UltraIMResult& result, const std::vector<UltraIMMessage>& messages) {
              if (!s->onResult) return;         // answered already
              if (!result) {
                  // What was found so far is still an answer; the cursor
                  // says where to try again.
                  if (!s->page.messages.empty()) return Finish(s, s->before);
                  UltraIMSearchCallback done = std::move(s->onResult);
                  s->fetchPage = nullptr;
                  done(result, UltraIMSearchPage());
                  return;
              }
              if (messages.empty()) return Finish(s, std::string());
              // The page is oldest first; the answer is newest first.
              for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
                  if (!UltraIM_MessageMatchesSearch(*it, s->search)) continue;
                  s->page.messages.push_back(*it);
                  if (static_cast<int>(s->page.messages.size()) >= s->limit) return Finish(s, IdOf(*it));
              }
              const std::string oldest = IdOf(messages.front());
              if (oldest.empty() || oldest == s->before) return Finish(s, std::string());   // no further back
              s->before = oldest;
              if (++s->rounds >= kFetchRounds) return Finish(s, s->before);
              FetchNext(s);
          });
}

} // namespace

std::string UltraIM_FoldCase(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        size_t length = 1;
        bool wellFormed = true;
        const char32_t c = Decode(text, i, length, wellFormed);
        if (wellFormed) {
            Encode(Lower(c), out);
        } else {
            out += text[i];
        }
        i += length;
    }
    return out;
}

std::vector<std::string> UltraIM_ExtractLinks(const std::string& text) {
    std::vector<std::string> links;
    for (size_t i = 0; i < text.size();) {
        size_t schemeLength = 0;
        if (HasAt(text, i, "https://")) {
            schemeLength = 8;
        } else if (HasAt(text, i, "http://")) {
            schemeLength = 7;
        } else if (HasAt(text, i, "www.") && (i == 0 || !(IsAsciiAlnum(text[i - 1]) || text[i - 1] == '.' ||
                                                          text[i - 1] == '-' || text[i - 1] == '/' ||
                                                          text[i - 1] == '@' || text[i - 1] == '_'))) {
            schemeLength = 4;
        }
        if (schemeLength == 0) {
            ++i;
            continue;
        }
        size_t end = i + schemeLength;
        while (end < text.size() && !IsSpace(text[end]) && text[end] != '<' && text[end] != '>' && text[end] != '"')
            ++end;
        // What a sentence puts after a link is not part of it; a closing
        // bracket is, when the link opened it (a Wikipedia title).
        const auto opened = [&text, i](size_t upTo, char open, char close) {
            int depth = 0;
            for (size_t k = i; k < upTo; ++k) depth += text[k] == open ? 1 : (text[k] == close ? -1 : 0);
            return depth > 0;
        };
        while (end > i + schemeLength) {
            const char last = text[end - 1];
            if ((last == ')' && opened(end - 1, '(', ')')) || (last == ']' && opened(end - 1, '[', ']'))) break;
            if (last == '.' || last == ',' || last == ';' || last == ':' || last == '!' || last == '?' ||
                last == ')' || last == ']' || last == '}' || last == '\'') {
                --end;
            } else {
                break;
            }
        }
        const std::string rest = text.substr(i + schemeLength, end - i - schemeLength);
        // Something must follow: a host, and for a bare www. one with a dot in it.
        const bool isLink = !rest.empty() && (IsAsciiAlnum(rest[0]) || static_cast<unsigned char>(rest[0]) >= 0x80 ||
                                              rest[0] == '[') &&
                            (schemeLength != 4 || rest.find('.') != std::string::npos);
        if (isLink) {
            links.push_back(text.substr(i, end - i));
            i = end;
        } else {
            i += schemeLength;
        }
    }
    return links;
}

bool UltraIM_MessageMatchesSearch(const UltraIMMessage& message, const UltraIMMessageSearch& search) {
    if (!KindMatches(message, search.kind)) return false;
    const std::vector<std::string> words = Words(UltraIM_FoldCase(search.text));
    if (words.empty()) return true;
    const std::string where = UltraIM_FoldCase(message.text + "\n" + message.attachment.fileName);
    for (const auto& word : words) {
        if (where.find(word) == std::string::npos) return false;
    }
    return true;
}

void UltraIM_SearchByFetching(UltraIMFetchPage fetchPage, const UltraIMMessageSearch& search,
                              UltraIMSearchCallback onResult) {
    if (!onResult) return;
    if (!fetchPage || search.conversationId.empty()) {
        onResult(UltraIMResult::Error(UltraIMResultCode::CapabilityMissing,
                                      "This provider searches one conversation at a time"),
                 UltraIMSearchPage());
        return;
    }
    auto s = std::make_shared<FetchSearch>();
    s->fetchPage = std::move(fetchPage);
    s->search = search;
    s->onResult = std::move(onResult);
    s->before = search.cursor;
    s->limit = search.limit > 0 ? search.limit : 50;
    FetchNext(s);
}
