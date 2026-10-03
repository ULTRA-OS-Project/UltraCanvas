// Apps/UltraMail/engine/UltraMailRecipientComplete.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailRecipientComplete.h"

#include <cctype>
#include <set>

namespace UltraMail {

namespace {

std::string Trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The bare address of a recipient: inside <...>, or the whole text.
std::string BareLower(const std::string& recipient) {
    const std::size_t lt = recipient.rfind('<');
    const std::size_t gt = recipient.rfind('>');
    if (lt != std::string::npos && gt != std::string::npos && gt > lt)
        return Lower(Trim(recipient.substr(lt + 1, gt - lt - 1)));
    return Lower(Trim(recipient));
}

// Whether a word of `text` (split at anything that is not a letter or digit,
// UTF-8 bytes kept whole) starts with `query`.
bool WordStartsWith(const std::string& text, const std::string& query) {
    std::size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, query.size(), query) == 0) return true;
        while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) ||
                                   static_cast<unsigned char>(text[i]) >= 0x80))
            ++i;
        while (i < text.size() && !(std::isalnum(static_cast<unsigned char>(text[i])) ||
                                    static_cast<unsigned char>(text[i]) >= 0x80))
            ++i;
    }
    return false;
}

// A display name that reads safely in a comma-separated field: one with a
// comma, quote or angle bracket would split or break it, so the address goes
// in alone.
bool NameFitsField(const std::string& name) {
    return !name.empty() && name.find_first_of(",\"<>") == std::string::npos;
}

} // namespace

std::string RecipientQuery(const std::string& fieldText) {
    const std::size_t comma = fieldText.rfind(',');
    return Trim(comma == std::string::npos ? fieldText : fieldText.substr(comma + 1));
}

std::string CompleteRecipient(const std::string& fieldText, const std::string& recipient) {
    const std::size_t comma = fieldText.rfind(',');
    std::string out = comma == std::string::npos ? std::string() : fieldText.substr(0, comma + 1);
    if (!out.empty()) out += ' ';
    return out + recipient + ", ";
}

std::vector<RecipientSuggestion> SuggestRecipients(const std::vector<Contact>& contacts,
                                                   const std::string& query,
                                                   const std::string& fieldText,
                                                   std::size_t limit) {
    std::vector<RecipientSuggestion> first, later;
    const std::string q = Lower(Trim(query));
    if (q.empty() || limit == 0) return first;

    // The recipients already written (all but the one being typed).
    std::set<std::string> present;
    const std::size_t lastComma = fieldText.rfind(',');
    if (lastComma != std::string::npos) {
        std::size_t start = 0;
        while (start < lastComma) {
            std::size_t comma = fieldText.find(',', start);
            if (comma == std::string::npos || comma > lastComma) comma = lastComma;
            present.insert(BareLower(fieldText.substr(start, comma - start)));
            start = comma + 1;
        }
    }

    std::set<std::string> seen;
    for (const Contact& c : contacts) {
        const std::string name = Lower(c.displayName);
        const std::string org  = Lower(c.organization);
        for (const ContactEmail& e : c.emails) {
            const std::string addr = Lower(Trim(e.address));
            if (addr.empty() || present.count(addr) || seen.count(addr)) continue;
            const bool starts = WordStartsWith(name, q) || WordStartsWith(org, q) ||
                                addr.compare(0, q.size(), q) == 0;
            const bool contains = !starts && (name.find(q) != std::string::npos ||
                                              addr.find(q) != std::string::npos);
            if (!starts && !contains) continue;
            seen.insert(addr);
            RecipientSuggestion s;
            s.address = Trim(e.address);
            const std::string display = Trim(c.displayName);
            s.recipient = NameFitsField(display) ? display + " <" + s.address + ">" : s.address;
            (starts ? first : later).push_back(std::move(s));
        }
    }
    for (auto& s : later) first.push_back(std::move(s));
    if (first.size() > limit) first.resize(limit);
    return first;
}

} // namespace UltraMail
