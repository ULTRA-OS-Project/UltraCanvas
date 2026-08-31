// Tests/EmailCleaner/test_overrides.cpp
// The user's correction of a verdict: setting one, which senders it covers,
// what it does to the stored corpus, and — the part that matters — that taking
// it back restores what the classifier actually said.
// Version: 0.3.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "EmailCleanerStore.h"

#include <UltraDatabase/UltraDatabase.h>

#include <string>
#include <vector>

using namespace EmailCleaner;

namespace {

int NextConnectionId() {
    static int counter = 0;
    return ++counter;
}

AnalysisStore OpenStore() {
    AnalysisStore store;
    const std::string name = "ec_ovr_" + std::to_string(NextConnectionId());
    const UltraDbResult r = store.Open(name, ":memory:");
    if (!r) throw emailcleaner_test::Failure{ "store.Open failed: " + r.message };
    return store;
}

// A message carrying a classifier verdict, the way the ingest writes one.
AnalyzedMessage Message(const std::string& sender, int64_t uid,
                        MessageCategory category, double score) {
    AnalyzedMessage m;
    m.accountId    = "erika";
    m.folder       = "INBOX";
    m.uid          = uid;
    m.senderAddr   = sender;
    m.senderDomain = DomainOf(sender);
    m.subject      = "Subject " + std::to_string(uid);
    m.date         = 86400 * uid;
    m.SetClassifierVerdict(category, score);
    return m;
}

VerdictOverride Override(const std::string& pattern, MessageCategory category,
                         bool isDomain = false) {
    VerdictOverride o;
    o.pattern  = pattern;
    o.isDomain = isDomain;
    o.category = category;
    o.reason   = "marked by hand";
    o.added    = 1000;
    return o;
}

// The stored verdict for one message, by uid.
MessageCategory CategoryOf(const AnalysisStore& store, int64_t uid) {
    std::vector<AnalyzedMessage> all;
    store.ListMessages(MessageFilter{}, all);
    for (const AnalyzedMessage& m : all)
        if (m.uid == uid) return m.category;
    return MessageCategory::Unclassified;
}

AnalyzedMessage Stored(const AnalysisStore& store, int64_t uid) {
    std::vector<AnalyzedMessage> all;
    store.ListMessages(MessageFilter{}, all);
    for (const AnalyzedMessage& m : all)
        if (m.uid == uid) return m;
    return AnalyzedMessage{};
}

} // namespace

// ---- The table -------------------------------------------------------------

TEST(Override_LandedInItsOwnMigration) {
    // v3 added the override table; the constant and the migration list must
    // agree, which Open() itself now checks.
    AnalysisStore store = OpenStore();
    REQUIRE(AnalysisStore::kSchemaVersion >= 3);
    int version = 0;
    REQUIRE(UltraDb_GetSchemaVersion(store.Connection(), version));
    REQUIRE_EQ(version, AnalysisStore::kSchemaVersion);
}

TEST(Override_RoundTripsAndReplaces) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.SetOverride(Override("news@museum.example", MessageCategory::Newsletter)));

    std::vector<VerdictOverride> list;
    REQUIRE(store.ListOverrides(list));
    REQUIRE_EQ(list.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(list[0].pattern, std::string("news@museum.example"));
    REQUIRE(list[0].category == MessageCategory::Newsletter);
    REQUIRE(list[0].Wanted());

    // Changing your mind about the same sender replaces rather than duplicates.
    REQUIRE(store.SetOverride(Override("news@museum.example", MessageCategory::ProductSpam)));
    REQUIRE(store.ListOverrides(list));
    REQUIRE_EQ(list.size(), static_cast<std::size_t>(1));
    REQUIRE(list[0].category == MessageCategory::ProductSpam);
    REQUIRE(!list[0].Wanted());
}

TEST(Override_PatternIsLowercased) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.SetOverride(Override("News@Museum.Example", MessageCategory::Newsletter)));

    VerdictOverride found;
    REQUIRE(store.FindOverride("news@museum.example", "", found));
    REQUIRE_EQ(found.pattern, std::string("news@museum.example"));
}

TEST(Override_NeedsAPattern) {
    AnalysisStore store = OpenStore();
    REQUIRE(!store.SetOverride(Override("", MessageCategory::Personal)));
}

// ---- Which sender an override covers ---------------------------------------

TEST(Override_DomainCoversEverySenderUnderIt) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.SetOverride(Override("museum.example", MessageCategory::Newsletter, true)));

    VerdictOverride found;
    REQUIRE(store.FindOverride("anyone@museum.example", "", found));
    REQUIRE(found.isDomain);
    REQUIRE(!store.FindOverride("someone@other.example", "", found));
}

TEST(Override_TheAddressBeatsTheDomain) {
    // The more specific statement is the one the user made about that exact
    // sender, so it has to win — otherwise "all of this domain is spam except
    // this one address" is unsayable.
    AnalysisStore store = OpenStore();
    REQUIRE(store.SetOverride(Override("museum.example", MessageCategory::ProductSpam, true)));
    REQUIRE(store.SetOverride(Override("news@museum.example", MessageCategory::Newsletter)));

    VerdictOverride found;
    REQUIRE(store.FindOverride("news@museum.example", "", found));
    REQUIRE(found.category == MessageCategory::Newsletter);
    REQUIRE(!found.isDomain);

    REQUIRE(store.FindOverride("other@museum.example", "", found));
    REQUIRE(found.category == MessageCategory::ProductSpam);
    REQUIRE(found.isDomain);
}

// ---- What it does to the corpus --------------------------------------------

TEST(Override_ThisIsFineClearsAnUnwantedVerdict) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessage(Message("news@museum.example", 1,
                                        MessageCategory::ProductSpam, 88.0)));
    REQUIRE(store.SetOverride(Override("news@museum.example", MessageCategory::Newsletter)));
    REQUIRE(store.ApplyOverridesToMessages());

    const AnalyzedMessage m = Stored(store, 1);
    REQUIRE(m.category == MessageCategory::Newsletter);
    REQUIRE_EQ(m.score, 0.0);
    REQUIRE(m.overridden);
    // The classifier's own answer is kept, not thrown away.
    REQUIRE(m.baseCategory == MessageCategory::ProductSpam);
    REQUIRE_EQ(m.baseScore, 88.0);
}

TEST(Override_ThisIsSpamMarksItOutright) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessage(Message("friend@example.com", 1,
                                        MessageCategory::Personal, 0.0)));
    REQUIRE(store.SetOverride(Override("friend@example.com", MessageCategory::ProductSpam)));
    REQUIRE(store.ApplyOverridesToMessages());

    const AnalyzedMessage m = Stored(store, 1);
    REQUIRE(m.category == MessageCategory::ProductSpam);
    REQUIRE_EQ(m.score, 100.0);
    REQUIRE(m.overridden);
    REQUIRE(m.baseCategory == MessageCategory::Personal);
}

TEST(Override_RemovingItRestoresTheClassifiersVerdict) {
    // The property the whole design exists for: a correction must be
    // reversible, or marking something by mistake is unrecoverable.
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessage(Message("news@museum.example", 1,
                                        MessageCategory::ProductSpam, 88.0)));
    REQUIRE(store.SetOverride(Override("news@museum.example", MessageCategory::Newsletter)));
    REQUIRE(store.ApplyOverridesToMessages());
    REQUIRE(CategoryOf(store, 1) == MessageCategory::Newsletter);

    REQUIRE(store.RemoveOverride("news@museum.example"));
    REQUIRE(store.ApplyOverridesToMessages());

    const AnalyzedMessage m = Stored(store, 1);
    REQUIRE(m.category == MessageCategory::ProductSpam);
    REQUIRE_EQ(m.score, 88.0);
    REQUIRE(!m.overridden);
}

TEST(Override_OnlyTouchesTheSendersItNames) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessage(Message("news@museum.example", 1,
                                        MessageCategory::ProductSpam, 90.0)));
    REQUIRE(store.UpsertMessage(Message("spam@bulk.example", 2,
                                        MessageCategory::ProductSpam, 90.0)));
    REQUIRE(store.SetOverride(Override("news@museum.example", MessageCategory::Newsletter)));
    REQUIRE(store.ApplyOverridesToMessages());

    REQUIRE(CategoryOf(store, 1) == MessageCategory::Newsletter);
    REQUIRE(CategoryOf(store, 2) == MessageCategory::ProductSpam);
    REQUIRE(!Stored(store, 2).overridden);
}

TEST(Override_AddressPassBeatsDomainPassOnTheCorpusToo) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessage(Message("news@museum.example", 1,
                                        MessageCategory::Personal, 0.0)));
    REQUIRE(store.UpsertMessage(Message("ads@museum.example", 2,
                                        MessageCategory::Personal, 0.0)));
    REQUIRE(store.SetOverride(Override("museum.example", MessageCategory::ProductSpam, true)));
    REQUIRE(store.SetOverride(Override("news@museum.example", MessageCategory::Newsletter)));
    REQUIRE(store.ApplyOverridesToMessages());

    REQUIRE(CategoryOf(store, 1) == MessageCategory::Newsletter);
    REQUIRE(CategoryOf(store, 2) == MessageCategory::ProductSpam);
}

TEST(Override_ReapplyingIsIdempotent) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessage(Message("news@museum.example", 1,
                                        MessageCategory::ProductSpam, 88.0)));
    REQUIRE(store.SetOverride(Override("news@museum.example", MessageCategory::Newsletter)));
    for (int i = 0; i < 3; ++i) REQUIRE(store.ApplyOverridesToMessages());

    const AnalyzedMessage m = Stored(store, 1);
    REQUIRE(m.category == MessageCategory::Newsletter);
    // Three passes must not let the corrected value become the base.
    REQUIRE(m.baseCategory == MessageCategory::ProductSpam);
    REQUIRE_EQ(m.baseScore, 88.0);
}

TEST(Override_ReanalysisDoesNotLoseTheCorrection) {
    // Re-analysing writes the classifier's fresh verdict over the row. The
    // correction is re-stamped afterwards, exactly as the blocklist stamp is.
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessage(Message("news@museum.example", 1,
                                        MessageCategory::ProductSpam, 88.0)));
    REQUIRE(store.SetOverride(Override("news@museum.example", MessageCategory::Newsletter)));
    REQUIRE(store.ApplyOverridesToMessages());

    // A re-analysis that now says AdultContent instead.
    REQUIRE(store.UpsertMessage(Message("news@museum.example", 1,
                                        MessageCategory::AdultContent, 95.0)));
    REQUIRE(CategoryOf(store, 1) == MessageCategory::AdultContent);   // before re-stamping

    REQUIRE(store.ApplyOverridesToMessages());
    const AnalyzedMessage m = Stored(store, 1);
    REQUIRE(m.category == MessageCategory::Newsletter);
    REQUIRE(m.baseCategory == MessageCategory::AdultContent);   // the newer classifier verdict
}

TEST(Override_AndBlocklistAreIndependent) {
    // "I do not want to hear from them" and "your verdict about them is wrong"
    // are different statements; one must not imply the other.
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessage(Message("news@museum.example", 1,
                                        MessageCategory::Newsletter, 0.0)));

    BlockEntry block;
    block.pattern = "news@museum.example";
    block.reason  = "too much of it";
    REQUIRE(store.AddBlock(block));
    REQUIRE(store.ApplyBlocklistToMessages());

    REQUIRE(Stored(store, 1).blocked);
    REQUIRE(!Stored(store, 1).overridden);
    REQUIRE(Stored(store, 1).category == MessageCategory::Newsletter);

    VerdictOverride found;
    REQUIRE(!store.FindOverride("news@museum.example", "", found));
}
