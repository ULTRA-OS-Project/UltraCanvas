// Tests/UltraMail/test_messagesort.cpp
// The message list's order, chosen by clicking a column header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "test_framework.h"

#include "UltraMailMessageSort.h"

#include <string>
#include <vector>

using namespace UltraMail;

namespace {
MessageEnvelope Mail(int64_t uid, const std::string& fromName, const std::string& subject,
                     int64_t date) {
    MessageEnvelope m;
    m.uid = uid;
    m.fromName = fromName;
    m.fromAddr = "x" + std::to_string(uid) + "@example.com";
    m.subject = subject;
    m.date = date;
    return m;
}
std::vector<int64_t> Uids(const std::vector<MessageEnvelope>& ms) {
    std::vector<int64_t> out;
    for (const auto& m : ms) out.push_back(m.uid);
    return out;
}
} // namespace

TEST(sort_clicks_toggle_the_direction_of_the_same_column) {
    MessageSort sort;                                  // the default: newest first
    REQUIRE(sort.key == MessageSortKey::Date && !sort.ascending);
    MessageSort bySender = sort.Clicked(MessageSortKey::Sender);
    REQUIRE(bySender.key == MessageSortKey::Sender && bySender.ascending);   // A first
    REQUIRE(!bySender.Clicked(MessageSortKey::Sender).ascending);           // then Z first
    MessageSort byDate = bySender.Clicked(MessageSortKey::Date);
    REQUIRE(byDate.key == MessageSortKey::Date && !byDate.ascending);       // newest first
    REQUIRE(byDate.Clicked(MessageSortKey::Date).ascending);                // then oldest
}

TEST(sort_round_trips_through_its_preference_text) {
    for (MessageSortKey key : { MessageSortKey::Sender, MessageSortKey::Kind,
                                MessageSortKey::Subject, MessageSortKey::Date }) {
        for (bool asc : { true, false }) {
            MessageSort s; s.key = key; s.ascending = asc;
            REQUIRE(MessageSort::FromString(s.ToString()) == s);
        }
    }
    REQUIRE_EQ(MessageSort{}.ToString(), std::string("date-desc"));
    REQUIRE(MessageSort::FromString("subject-ASC").key == MessageSortKey::Subject);
    REQUIRE(MessageSort::FromString("nonsense") == MessageSort{});
    REQUIRE(MessageSort::FromString("sender-sideways") == MessageSort{});
    REQUIRE(MessageSort::FromString("") == MessageSort{});
}

TEST(sort_subject_text_leaves_out_reply_and_forward_prefixes) {
    REQUIRE_EQ(SubjectSortText("Re: Quick note"), std::string("quick note"));
    REQUIRE_EQ(SubjectSortText("RE: AW: Fwd:  Pitch deck"), std::string("pitch deck"));
    REQUIRE_EQ(SubjectSortText("Re[2]: Invoice"), std::string("invoice"));
    REQUIRE_EQ(SubjectSortText("WG : Rechnung"), std::string("rechnung"));
    // Not a prefix: a word that only starts like one, or one with no colon.
    REQUIRE_EQ(SubjectSortText("Report: Q3"), std::string("report: q3"));
    REQUIRE_EQ(SubjectSortText("Re-launch"), std::string("re-launch"));
    REQUIRE_EQ(SubjectSortText(""), std::string(""));
}

TEST(sort_fold_puts_accented_letters_with_their_base_letter) {
    REQUIRE_EQ(SortFold("\xC3\x84nderung"), std::string("anderung"));   // Änderung
    REQUIRE_EQ(SortFold("Stra\xC3\x9F" "e"), std::string("strasse"));    // Straße
    REQUIRE_EQ(SortFold("\xC3\xA9t\xC3\xA9"), std::string("ete"));      // été
    REQUIRE_EQ(SortFold("ABC"), std::string("abc"));
    // Characters outside Latin-1 are kept as they are.
    REQUIRE_EQ(SortFold("\xE2\x9C\x93 ok"), std::string("\xE2\x9C\x93 ok"));
}

TEST(sort_messages_by_each_column) {
    std::vector<MessageEnvelope> ms = {
        Mail(1, "Maya Bennett", "Re: Quick note",    100),
        Mail(2, "\xC3\x84rztekammer", "Beitrag",     300),   // Ärztekammer
        Mail(3, "Reddit",       "GPT 6 dropped",     200),
        Mail(4, "",             "Zebra",             400),   // no name: its address
    };
    MessageSort sort;
    SortMessages(ms, sort);
    REQUIRE(Uids(ms) == (std::vector<int64_t>{4, 2, 3, 1}));   // newest first

    sort.ascending = true;
    SortMessages(ms, sort);
    REQUIRE(Uids(ms) == (std::vector<int64_t>{1, 3, 2, 4}));   // oldest first

    sort = MessageSort{}.Clicked(MessageSortKey::Sender);
    SortMessages(ms, sort);
    // Ärztekammer with the A's, Maya, Reddit, then the address x4@...
    REQUIRE(Uids(ms) == (std::vector<int64_t>{2, 1, 3, 4}));

    sort = MessageSort{}.Clicked(MessageSortKey::Subject);
    SortMessages(ms, sort);
    // Beitrag, GPT 6 dropped, (Re:) Quick note, Zebra
    REQUIRE(Uids(ms) == (std::vector<int64_t>{2, 3, 1, 4}));
    sort.ascending = false;
    SortMessages(ms, sort);
    REQUIRE(Uids(ms) == (std::vector<int64_t>{4, 1, 3, 2}));
}

TEST(sort_messages_by_kind_uses_the_rank_given_and_date_for_ties) {
    std::vector<MessageEnvelope> ms = {
        Mail(1, "a", "s", 100), Mail(2, "b", "s", 200), Mail(3, "c", "s", 300),
    };
    MessageSortText text;
    text.kindRank = [](const MessageEnvelope& m) { return m.uid == 2 ? 0 : 1; };
    MessageSort sort = MessageSort{}.Clicked(MessageSortKey::Kind);
    SortMessages(ms, sort, text);
    // Rank 0 first; the two of rank 1 newest first.
    REQUIRE(Uids(ms) == (std::vector<int64_t>{2, 3, 1}));
}

TEST(sort_uses_the_text_the_list_shows) {
    std::vector<MessageEnvelope> ms = {
        Mail(1, "=?UTF-8?Q?Zora?=", "s", 100), Mail(2, "Bob", "s", 200),
    };
    MessageSortText text;
    text.sender = [](const MessageEnvelope& m) {
        return m.uid == 1 ? std::string("Anna") : m.fromName;   // as decoded for the row
    };
    SortMessages(ms, MessageSort{}.Clicked(MessageSortKey::Sender), text);
    REQUIRE(Uids(ms) == (std::vector<int64_t>{1, 2}));
}

TEST(sorted_position_keeps_the_list_in_order) {
    MessageSort sort = MessageSort{}.Clicked(MessageSortKey::Subject);
    std::vector<MessageEnvelope> ms = {
        Mail(1, "a", "Alpha", 100), Mail(2, "b", "Delta", 200), Mail(3, "c", "Kilo", 300),
    };
    SortMessages(ms, sort);
    REQUIRE_EQ(SortedPosition(ms, Mail(9, "n", "Re: Charlie", 50), sort), (size_t)1);
    REQUIRE_EQ(SortedPosition(ms, Mail(9, "n", "Zulu", 50), sort), (size_t)3);
    REQUIRE_EQ(SortedPosition(ms, Mail(9, "n", "", 50), sort), (size_t)0);
    // The default order: newest first.
    std::vector<MessageEnvelope> byDate = ms;
    SortMessages(byDate, MessageSort{});
    REQUIRE_EQ(SortedPosition(byDate, Mail(9, "n", "x", 999), MessageSort{}), (size_t)0);
    REQUIRE_EQ(SortedPosition(byDate, Mail(9, "n", "x", 150), MessageSort{}), (size_t)2);
}
