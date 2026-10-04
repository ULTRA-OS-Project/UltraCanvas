// Apps/UltraMail/engine/UltraMailMessageSort.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailMessageSort.h"

#include <algorithm>
#include <cctype>
#include <numeric>

namespace UltraMail {

namespace {

const char* KeyName(MessageSortKey key) {
    switch (key) {
        case MessageSortKey::Sender:  return "sender";
        case MessageSortKey::Kind:    return "kind";
        case MessageSortKey::Subject: return "subject";
        case MessageSortKey::Date:    return "date";
    }
    return "date";
}

// U+00C0..U+00FF as the letters they sort with ("" keeps the character).
const char* const kLatin1Fold[64] = {
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",   // C0-CF
    "d", "n", "o", "o", "o", "o", "o", "",  "o", "u", "u", "u", "u", "y", "th", "ss",  // D0-DF
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",   // E0-EF
    "d", "n", "o", "o", "o", "o", "o", "",  "o", "u", "u", "u", "u", "y", "th", "y",   // F0-FF
};

// One message's sort key, worked out once (decoding and folding per
// comparison would make a sort of a large mailbox slow).
struct Keyed {
    std::string text;
    int         rank = 0;
    int64_t     date = 0;
    int64_t     uid  = 0;
};

Keyed KeyOf(const MessageEnvelope& m, const MessageSort& sort, const MessageSortText& text) {
    Keyed k;
    k.date = m.date;
    k.uid  = m.uid;
    switch (sort.key) {
        case MessageSortKey::Sender:
            k.text = SortFold(text.sender ? text.sender(m)
                                          : (m.fromName.empty() ? m.fromAddr : m.fromName));
            break;
        case MessageSortKey::Subject:
            k.text = SubjectSortText(text.subject ? text.subject(m) : m.subject);
            break;
        case MessageSortKey::Kind:
            k.rank = text.kindRank ? text.kindRank(m) : 0;
            break;
        case MessageSortKey::Date:
            break;
    }
    return k;
}

// Whether `a` goes above `b`.
bool Before(const Keyed& a, const Keyed& b, const MessageSort& sort) {
    int c = 0;
    switch (sort.key) {
        case MessageSortKey::Sender:
        case MessageSortKey::Subject:
            c = a.text.compare(b.text);
            break;
        case MessageSortKey::Kind:
            c = a.rank < b.rank ? -1 : a.rank > b.rank ? 1 : 0;
            break;
        case MessageSortKey::Date:
            c = a.date < b.date ? -1 : a.date > b.date ? 1 : 0;
            break;
    }
    if (c != 0) return sort.ascending ? c < 0 : c > 0;
    // Equal keys: newest first, then the higher UID, so the order is stable
    // from one sort (and one sync) to the next.
    if (a.date != b.date) return a.date > b.date;
    return a.uid > b.uid;
}

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

} // namespace

MessageSort MessageSort::Clicked(MessageSortKey clicked) const {
    MessageSort next;
    next.key = clicked;
    next.ascending = clicked == key ? !ascending : StartsAscending(clicked);
    return next;
}

std::string MessageSort::ToString() const {
    return std::string(KeyName(key)) + (ascending ? "-asc" : "-desc");
}

MessageSort MessageSort::FromString(const std::string& text) {
    std::string t;
    for (char c : text)
        if (!IsSpace(c)) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::size_t dash = t.rfind('-');
    if (dash == std::string::npos) return MessageSort{};
    const std::string name = t.substr(0, dash), dir = t.substr(dash + 1);
    if (dir != "asc" && dir != "desc") return MessageSort{};
    for (MessageSortKey key : { MessageSortKey::Sender, MessageSortKey::Kind,
                                MessageSortKey::Subject, MessageSortKey::Date }) {
        if (name == KeyName(key)) {
            MessageSort sort;
            sort.key = key;
            sort.ascending = dir == "asc";
            return sort;
        }
    }
    return MessageSort{};
}

std::string SortFold(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            out += static_cast<char>(std::tolower(c));
        } else if (c == 0xC3 && i + 1 < text.size() &&
                   (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80) {
            const unsigned char second = static_cast<unsigned char>(text[i + 1]);
            const char* folded = kLatin1Fold[second - 0x80];
            if (*folded) out += folded;
            else         out.append(text, i, 2);
            ++i;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

std::string SubjectSortText(const std::string& subject) {
    // The prefixes mail programs write, in the languages UltraMail meets most:
    // Re / Fwd / Fw (English), AW / WG (German), SV / VS (Nordic), Antw
    // (Dutch), TR (French), RIF (Italian), RES / ENC (Portuguese).
    static const char* const kPrefixes[] = {
        "re", "fwd", "fw", "aw", "wg", "sv", "vs", "antw", "tr", "rif", "res", "enc",
    };
    std::size_t pos = 0;
    for (;;) {
        while (pos < subject.size() && IsSpace(subject[pos])) ++pos;
        std::size_t word = pos;
        while (word < subject.size() && std::isalpha(static_cast<unsigned char>(subject[word])))
            ++word;
        if (word == pos) break;
        std::string lowered;
        for (std::size_t i = pos; i < word; ++i)
            lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(subject[i])));
        bool known = false;
        for (const char* p : kPrefixes) if (lowered == p) { known = true; break; }
        if (!known) break;
        std::size_t after = word;
        // "Re[2]:" / "Re(3):" - a reply counter.
        if (after < subject.size() && (subject[after] == '[' || subject[after] == '(')) {
            const char close = subject[after] == '[' ? ']' : ')';
            std::size_t end = after + 1;
            while (end < subject.size() && std::isdigit(static_cast<unsigned char>(subject[end])))
                ++end;
            if (end == after + 1 || end >= subject.size() || subject[end] != close) break;
            after = end + 1;
        }
        while (after < subject.size() && subject[after] == ' ') ++after;   // "Re :"
        if (after >= subject.size() || subject[after] != ':') break;
        pos = after + 1;
    }
    return SortFold(subject.substr(pos));
}

std::vector<std::size_t> SortOrder(const std::vector<MessageEnvelope>& messages,
                                   const MessageSort& sort, const MessageSortText& text) {
    std::vector<Keyed> keys;
    keys.reserve(messages.size());
    for (const auto& m : messages) keys.push_back(KeyOf(m, sort, text));
    std::vector<std::size_t> order(messages.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return Before(keys[a], keys[b], sort);
    });
    return order;
}

void SortMessages(std::vector<MessageEnvelope>& messages, const MessageSort& sort,
                  const MessageSortText& text) {
    const std::vector<std::size_t> order = SortOrder(messages, sort, text);
    std::vector<MessageEnvelope> sorted;
    sorted.reserve(messages.size());
    for (std::size_t i : order) sorted.push_back(std::move(messages[i]));
    messages = std::move(sorted);
}

std::size_t SortedPosition(const std::vector<MessageEnvelope>& sorted,
                           const MessageEnvelope& m, const MessageSort& sort,
                           const MessageSortText& text) {
    // Binary search: the rows are in order, and working out a row's key
    // (decoding, folding) is the expensive part.
    const Keyed key = KeyOf(m, sort, text);
    std::size_t lo = 0, hi = sorted.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (Before(KeyOf(sorted[mid], sort, text), key, sort)) lo = mid + 1;
        else                                                   hi = mid;
    }
    return lo;
}

} // namespace UltraMail
