// Apps/UltraMail/engine/UltraMailMessageSort.h
// The order of the message list: by sender, by kind of sender (the badge),
// by subject or by date, either way round - what clicking a column header of
// the list chooses. Pure: the list view sorts its rows with it, and the
// streaming insert of new mail finds a row's place with it, so both agree.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailTypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UltraMail {

// What the list is ordered by - one per column of the list.
enum class MessageSortKey {
    Sender,    // the sender's name (its address when it has none)
    Kind,      // the sender badge: contact, business contact, new, advertisement, spam, scam
    Subject,   // the subject, without "Re:" / "Fwd:" / "AW:" in front
    Date       // when it was sent
};

struct MessageSort {
    MessageSortKey key = MessageSortKey::Date;
    bool ascending = false;   // the default: newest first

    bool operator==(const MessageSort& o) const { return key == o.key && ascending == o.ascending; }
    bool operator!=(const MessageSort& o) const { return !(*this == o); }

    // The direction a column starts in when it is clicked first: dates
    // newest first, everything else from A (or the first kind) on.
    static bool StartsAscending(MessageSortKey key) { return key != MessageSortKey::Date; }
    // The order after a click on `key`'s column: the other direction when the
    // list is already ordered by it, else that column in its starting direction.
    MessageSort Clicked(MessageSortKey clicked) const;

    // "date-desc", "sender-asc", ... (preferences.ini); FromString falls back to
    // the default for anything it does not know.
    std::string ToString() const;
    static MessageSort FromString(const std::string& text);
};

// The text a subject sorts by: the reply and forward prefixes mail programs
// put in front (Re:, Fwd:, Fw:, AW:, WG:, SV:, VS:, Antw:, TR:, RE[2]:, ...,
// repeated) left out, so a reply sorts with what it answers; then folded like
// SortFold.
std::string SubjectSortText(const std::string& subject);

// `text` folded for ordering: lower case, and the accented Latin letters
// (U+00C0..U+00FF) as their base letter ("Ärger" sorts as "arger", "ß" as
// "ss"), so "Änderung" comes before "Zahlung" rather than after it.
std::string SortFold(const std::string& text);

// Orders `messages` by `sort`; ties (and all equal keys) newest first, then by
// UID, so the order is the same every time. `kindRank` gives the Kind key's
// rank of a message (lower first when ascending); it is needed only for
// MessageSortKey::Kind. `senderText` / `subjectText`, when given, say what the
// list shows for a message's sender and subject (decoded); by default the
// envelope's fields are used as they are.
struct MessageSortText {
    std::function<std::string(const MessageEnvelope&)> sender;
    std::function<std::string(const MessageEnvelope&)> subject;
    std::function<int(const MessageEnvelope&)>         kindRank;
};
void SortMessages(std::vector<MessageEnvelope>& messages, const MessageSort& sort,
                  const MessageSortText& text = {});

// The same order as a permutation: `order[i]` is the index into `messages` of
// the message that goes at row i. For a list that keeps rows beside the
// messages (their states, badges) and must move them alike.
std::vector<std::size_t> SortOrder(const std::vector<MessageEnvelope>& messages,
                                   const MessageSort& sort, const MessageSortText& text = {});

// The row `m` belongs at in `sorted` (already ordered by `sort`): before the
// first message it sorts ahead of. For new mail streaming into the list.
std::size_t SortedPosition(const std::vector<MessageEnvelope>& sorted,
                           const MessageEnvelope& m, const MessageSort& sort,
                           const MessageSortText& text = {});

} // namespace UltraMail
