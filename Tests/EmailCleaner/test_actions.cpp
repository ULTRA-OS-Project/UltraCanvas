// Tests/EmailCleaner/test_actions.cpp
// The blocklist, the action planner (what *would* happen), and the executor
// driven against a recording backend — no server, no network.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "EmailCleanerActions.h"

#include <UltraDatabase/UltraDatabase.h>

#include <algorithm>
#include <string>

using namespace EmailCleaner;

namespace {

int NextConnectionId() {
    static int counter = 0;
    return ++counter;
}

AnalysisStore OpenStore() {
    AnalysisStore store;
    const std::string name = "ec_actions_" + std::to_string(NextConnectionId());
    const UltraDbResult r = store.Open(name, ":memory:");
    if (!r) throw emailcleaner_test::Failure{ "store.Open failed: " + r.message };
    return store;
}

AnalyzedMessage Message(const std::string& sender, int64_t uid, MessageCategory category,
                        const std::string& folder = "INBOX") {
    AnalyzedMessage m;
    m.accountId    = "erika";
    m.folder       = folder;
    m.uid          = uid;
    m.senderAddr   = sender;
    m.senderName   = sender;
    m.senderDomain = DomainOf(sender);
    m.subject      = "Subject " + std::to_string(uid);
    m.date         = 86400 * uid;
    m.sizeBytes    = 1000;
    m.category     = category;
    return m;
}

// Records every outward step instead of taking it.
class RecordingBackend : public IActionBackend {
public:
    struct Move { std::string accountId, folder; int64_t uid; };
    std::vector<Move> moves;
    std::vector<std::string> posts;
    std::vector<std::string> mails;

    bool failMoves = false;
    bool failPost  = false;

    bool MoveToTrash(const std::string& accountId, const std::string& folder,
                     int64_t uid, std::string& outError) override {
        if (failMoves) { outError = "server said no"; return false; }
        moves.push_back({accountId, folder, uid});
        return true;
    }
    bool PostOneClick(const std::string& url, std::string& outError) override {
        if (failPost) { outError = "503"; return false; }
        posts.push_back(url);
        return true;
    }
    bool SendUnsubscribeMail(const std::string&, const std::string& address,
                             const std::string& subject, std::string&) override {
        mails.push_back(address + "|" + subject);
        return true;
    }
};

} // namespace

// ---- Blocklist -------------------------------------------------------------

TEST(Blocklist_AddListAndRemove) {
    AnalysisStore store = OpenStore();

    BlockEntry entry;
    entry.pattern = "Spam@Bad.Example";   // stored lowercased
    entry.reason  = "Product spam";
    entry.added   = 1000;
    REQUIRE(store.AddBlock(entry));

    std::vector<BlockEntry> blocks;
    REQUIRE(store.ListBlocks(blocks));
    REQUIRE_EQ(blocks.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(blocks[0].pattern, std::string("spam@bad.example"));
    REQUIRE(!blocks[0].isDomain);

    REQUIRE(store.IsBlocked("spam@bad.example", "bad.example"));
    REQUIRE(!store.IsBlocked("friend@good.example", "good.example"));

    REQUIRE(store.RemoveBlock("SPAM@BAD.EXAMPLE"));
    REQUIRE(store.ListBlocks(blocks));
    REQUIRE(blocks.empty());
}

TEST(Blocklist_DomainBlockCoversItsSenders) {
    AnalysisStore store = OpenStore();

    BlockEntry entry;
    entry.pattern  = "bad.example";
    entry.isDomain = true;
    REQUIRE(store.AddBlock(entry));

    REQUIRE(store.IsBlocked("anyone@bad.example", "bad.example"));
    REQUIRE(store.IsBlocked("someone.else@bad.example", ""));   // domain derived
    REQUIRE(!store.IsBlocked("friend@good.example", "good.example"));

    // An address block must not be confused with a domain block of the same
    // text, and vice versa.
    REQUIRE(!store.IsBlocked("bad.example", "nowhere.example"));
}

TEST(Blocklist_RejectsAnEmptyPattern) {
    AnalysisStore store = OpenStore();
    REQUIRE(!store.AddBlock(BlockEntry{}));
}

TEST(Blocklist_ApplyStampsStoredMessages) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessages({
        Message("spam@bad.example", 1, MessageCategory::ProductSpam),
        Message("other@bad.example", 2, MessageCategory::ProductSpam),
        Message("friend@good.example", 3, MessageCategory::Personal),
    }));

    BlockEntry entry;
    entry.pattern  = "bad.example";
    entry.isDomain = true;
    REQUIRE(store.AddBlock(entry));
    REQUIRE(store.ApplyBlocklistToMessages());

    std::vector<AnalyzedMessage> messages;
    REQUIRE(store.ListMessages(MessageFilter{}, messages));
    for (const AnalyzedMessage& m : messages)
        REQUIRE_EQ(m.blocked, m.senderDomain == "bad.example");

    // Unblocking clears the stamp again.
    REQUIRE(store.RemoveBlock("bad.example"));
    REQUIRE(store.ApplyBlocklistToMessages());
    REQUIRE(store.ListMessages(MessageFilter{}, messages));
    for (const AnalyzedMessage& m : messages) REQUIRE(!m.blocked);
}

// ---- Unsubscribe offers in the store ---------------------------------------

TEST(Store_UnsubscribeOfferTakesTheNewest) {
    AnalysisStore store = OpenStore();

    AnalyzedMessage old = Message("news@list.example", 1, MessageCategory::Newsletter);
    old.unsubUrl = "https://list.example/old";
    AnalyzedMessage recent = Message("news@list.example", 2, MessageCategory::Newsletter);
    recent.unsubUrl = "https://list.example/new";
    recent.unsubOneClick = true;
    // A later message with no offer must not erase the one that exists.
    AnalyzedMessage silent = Message("news@list.example", 3, MessageCategory::Newsletter);
    REQUIRE(store.UpsertMessages({ old, recent, silent }));

    MessageFilter filter;
    filter.senderAddr = "news@list.example";

    std::string mailto, subject, url;
    bool oneClick = false, found = false;
    REQUIRE(store.GetUnsubscribeOffer(filter, mailto, subject, url, oneClick, found));
    REQUIRE(found);
    REQUIRE_EQ(url, std::string("https://list.example/new"));
    REQUIRE(oneClick);
}

TEST(Store_UnsubscribeOfferAbsentIsNotAnError) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessage(Message("quiet@list.example", 1, MessageCategory::Newsletter)));

    std::string mailto, subject, url;
    bool oneClick = false, found = true;
    REQUIRE(store.GetUnsubscribeOffer(MessageFilter{}, mailto, subject, url, oneClick, found));
    REQUIRE(!found);
    REQUIRE(url.empty());
}

// ---- Planning --------------------------------------------------------------

TEST(Plan_BlockAndDeleteForOneSender) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessages({
        Message("spam@bad.example", 1, MessageCategory::ProductSpam),
        Message("spam@bad.example", 2, MessageCategory::ProductSpam),
        Message("friend@good.example", 3, MessageCategory::Personal),
    }));

    ActionRequest request;
    request.target.senderAddr = "spam@bad.example";
    request.block = true;
    request.deleteMail = true;

    const ActionPlan plan = ActionPlanner(store).Plan(request);
    REQUIRE(plan.willBlock);
    REQUIRE(!plan.alreadyBlocked);
    REQUIRE_EQ(plan.blockPattern, std::string("spam@bad.example"));
    REQUIRE(plan.willDelete);
    REQUIRE_EQ(plan.MessageCount(), 2);          // the other sender is untouched
    REQUIRE(plan.category == MessageCategory::ProductSpam);
    REQUIRE(plan.Describe().find("2 messages") != std::string::npos);
}

TEST(Plan_UnwantedOnlyLeavesWantedMailAlone) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessages({
        Message("mixed@shop.example", 1, MessageCategory::ProductSpam),
        Message("mixed@shop.example", 2, MessageCategory::Notification),
        Message("mixed@shop.example", 3, MessageCategory::ProductSpam),
    }));

    ActionRequest request;
    request.target.senderAddr = "mixed@shop.example";
    request.deleteMail   = true;
    request.unwantedOnly = true;

    const ActionPlan plan = ActionPlanner(store).Plan(request);
    REQUIRE_EQ(plan.MessageCount(), 2);
    REQUIRE_EQ(plan.skippedWanted, 1);
    for (const PlannedMessage& m : plan.messages) REQUIRE(IsUnwanted(m.category));
    REQUIRE(plan.Describe().find("left alone") != std::string::npos);
}

TEST(Plan_WarnsWhenWantedMailWouldBeMoved) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessages({
        Message("mixed@shop.example", 1, MessageCategory::ProductSpam),
        Message("mixed@shop.example", 2, MessageCategory::Personal),
    }));

    ActionRequest request;
    request.target.senderAddr = "mixed@shop.example";
    request.deleteMail = true;      // unwantedOnly deliberately off

    const ActionPlan plan = ActionPlanner(store).Plan(request);
    REQUIRE_EQ(plan.MessageCount(), 2);
    const bool warned = std::any_of(
        plan.warnings.begin(), plan.warnings.end(),
        [](const std::string& w) { return w.find("classified as wanted") != std::string::npos; });
    REQUIRE(warned);
}

TEST(Plan_DomainTargetCoversEverySenderAndSaysSo) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessages({
        Message("a@bulk.example", 1, MessageCategory::ProductSpam),
        Message("b@bulk.example", 2, MessageCategory::ProductSpam),
        Message("c@other.example", 3, MessageCategory::Personal),
    }));

    ActionRequest request;
    request.target.domain = "bulk.example";
    request.block = true;
    request.deleteMail = true;

    const ActionPlan plan = ActionPlanner(store).Plan(request);
    REQUIRE(plan.target.IsDomain());
    REQUIRE_EQ(plan.blockPattern, std::string("bulk.example"));
    REQUIRE_EQ(plan.MessageCount(), 2);
    const bool warned = std::any_of(
        plan.warnings.begin(), plan.warnings.end(),
        [](const std::string& w) { return w.find("every sender under it") != std::string::npos; });
    REQUIRE(warned);
}

TEST(Plan_RefusesToUnsubscribeFromSpamButStillBlocks) {
    AnalysisStore store = OpenStore();
    AnalyzedMessage spam = Message("deals@spammer.example", 1, MessageCategory::ProductSpam);
    spam.unsubUrl = "https://spammer.example/u?id=42";
    spam.unsubOneClick = true;
    REQUIRE(store.UpsertMessage(spam));

    ActionRequest request;
    request.target.senderAddr = "deals@spammer.example";
    request.block = true;
    request.unsubscribe = true;

    const ActionPlan plan = ActionPlanner(store).Plan(request);
    REQUIRE(plan.advice == UnsubscribeAdvice::RefuseSpam);
    REQUIRE(!plan.willUnsubscribe);       // the step is not scheduled
    REQUIRE(plan.willBlock);              // but blocking still is
    const bool warned = std::any_of(
        plan.warnings.begin(), plan.warnings.end(),
        [](const std::string& w) { return w.find("confirms someone reads") != std::string::npos; });
    REQUIRE(warned);
}

TEST(Plan_UnsubscribesFromARealNewsletter) {
    AnalysisStore store = OpenStore();
    AnalyzedMessage news = Message("news@museum.example", 1, MessageCategory::Newsletter);
    news.unsubMailto = "bye@museum.example";
    news.unsubMailtoSubject = "unsub-77";
    REQUIRE(store.UpsertMessage(news));

    ActionRequest request;
    request.target.senderAddr = "news@museum.example";
    request.unsubscribe = true;

    const ActionPlan plan = ActionPlanner(store).Plan(request);
    REQUIRE(plan.willUnsubscribe);
    REQUIRE(plan.method == UnsubscribeMethod::MailTo);
    REQUIRE(plan.advice == UnsubscribeAdvice::Recommended);
    REQUIRE(plan.warnings.empty());
}

TEST(Plan_EmptySelectionAndNoMatchesAreHandled) {
    AnalysisStore store = OpenStore();

    const ActionPlan none = ActionPlanner(store).Plan(ActionRequest{});
    REQUIRE(none.Empty());
    REQUIRE(!none.warnings.empty());

    ActionRequest request;
    request.target.senderAddr = "nobody@nowhere.example";
    request.deleteMail = true;
    const ActionPlan empty = ActionPlanner(store).Plan(request);
    REQUIRE(!empty.willDelete);
    REQUIRE_EQ(empty.MessageCount(), 0);
}

TEST(Plan_NoticesTheSenderIsAlreadyBlocked) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessage(Message("spam@bad.example", 1, MessageCategory::ProductSpam)));
    BlockEntry entry;
    entry.pattern = "spam@bad.example";
    REQUIRE(store.AddBlock(entry));

    ActionRequest request;
    request.target.senderAddr = "spam@bad.example";
    request.block = true;

    const ActionPlan plan = ActionPlanner(store).Plan(request);
    REQUIRE(plan.alreadyBlocked);
    REQUIRE(plan.Describe().find("keep") != std::string::npos);
}

TEST(Plan_ScopesToOneAccount) {
    AnalysisStore store = OpenStore();
    AnalyzedMessage mine = Message("spam@bad.example", 1, MessageCategory::ProductSpam);
    AnalyzedMessage other = Message("spam@bad.example", 2, MessageCategory::ProductSpam);
    other.accountId = "jonas";
    REQUIRE(store.UpsertMessages({ mine, other }));

    ActionRequest request;
    request.target.senderAddr = "spam@bad.example";
    request.deleteMail = true;

    const ActionPlan all = ActionPlanner(store).Plan(request);
    REQUIRE_EQ(all.MessageCount(), 2);

    const ActionPlan scoped = ActionPlanner(store).Plan(request, "erika");
    REQUIRE_EQ(scoped.MessageCount(), 1);
    REQUIRE_EQ(scoped.messages[0].accountId, std::string("erika"));
}

// ---- Execution -------------------------------------------------------------

TEST(Execute_BlocksMovesAndReportsWhatHappened) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessages({
        Message("spam@bad.example", 1, MessageCategory::ProductSpam),
        Message("spam@bad.example", 2, MessageCategory::ProductSpam, "Archive"),
    }));

    ActionRequest request;
    request.target.senderAddr = "spam@bad.example";
    request.block = true;
    request.deleteMail = true;
    const ActionPlan plan = ActionPlanner(store).Plan(request);

    RecordingBackend backend;
    const ActionOutcome outcome = ActionExecutor(store, &backend).Execute(plan);

    REQUIRE(outcome.ok);
    REQUIRE_EQ(outcome.blocked, 1);
    REQUIRE_EQ(outcome.moved, 2);
    REQUIRE_EQ(outcome.failed, 0);
    REQUIRE_EQ(backend.moves.size(), static_cast<std::size_t>(2));
    // The folder travels with each message rather than being assumed.
    REQUIRE(backend.moves[0].folder == "INBOX" || backend.moves[1].folder == "INBOX");
    REQUIRE(backend.moves[0].folder == "Archive" || backend.moves[1].folder == "Archive");

    // The block landed and was stamped onto the stored rows.
    REQUIRE(store.IsBlocked("spam@bad.example", "bad.example"));
    std::vector<AnalyzedMessage> messages;
    REQUIRE(store.ListMessages(MessageFilter{}, messages));
    for (const AnalyzedMessage& m : messages) REQUIRE(m.blocked);

    REQUIRE(outcome.Describe().find("moved 2 messages") != std::string::npos);
}

TEST(Execute_UnsubscribeUsesTheChosenMethod) {
    AnalysisStore store = OpenStore();
    AnalyzedMessage news = Message("news@museum.example", 1, MessageCategory::Newsletter);
    news.unsubUrl = "https://museum.example/u?t=1";
    news.unsubOneClick = true;
    REQUIRE(store.UpsertMessage(news));

    ActionRequest request;
    request.target.senderAddr = "news@museum.example";
    request.unsubscribe = true;
    const ActionPlan plan = ActionPlanner(store).Plan(request);
    REQUIRE(plan.method == UnsubscribeMethod::OneClickPost);

    RecordingBackend backend;
    const ActionOutcome outcome = ActionExecutor(store, &backend).Execute(plan);
    REQUIRE(outcome.ok);
    REQUIRE_EQ(outcome.unsubscribed, 1);
    REQUIRE_EQ(backend.posts.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(backend.posts[0], std::string("https://museum.example/u?t=1"));
    REQUIRE(backend.mails.empty());
}

TEST(Execute_MailtoCarriesTheListsOwnSubject) {
    AnalysisStore store = OpenStore();
    AnalyzedMessage news = Message("news@museum.example", 1, MessageCategory::Newsletter);
    news.unsubMailto = "bye@museum.example";
    news.unsubMailtoSubject = "unsub-77";
    REQUIRE(store.UpsertMessage(news));

    ActionRequest request;
    request.target.senderAddr = "news@museum.example";
    request.unsubscribe = true;
    const ActionPlan plan = ActionPlanner(store).Plan(request);

    RecordingBackend backend;
    ActionExecutor(store, &backend).Execute(plan);
    REQUIRE_EQ(backend.mails.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(backend.mails[0], std::string("bye@museum.example|unsub-77"));
}

TEST(Execute_WithoutABackendStillBlocksAndSaysWhatItCouldNotDo) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessage(Message("spam@bad.example", 1, MessageCategory::ProductSpam)));

    ActionRequest request;
    request.target.senderAddr = "spam@bad.example";
    request.block = true;
    request.deleteMail = true;
    const ActionPlan plan = ActionPlanner(store).Plan(request);

    const ActionOutcome outcome = ActionExecutor(store, nullptr).Execute(plan);
    REQUIRE_EQ(outcome.blocked, 1);        // the local half still happened
    REQUIRE_EQ(outcome.moved, 0);
    REQUIRE(!outcome.ok);
    REQUIRE_EQ(outcome.failed, 1);
    REQUIRE(outcome.Describe().find("no mail connection") != std::string::npos);
}

TEST(Execute_ReportsMoveFailuresWithoutLosingTheBlock) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessages({
        Message("spam@bad.example", 1, MessageCategory::ProductSpam),
        Message("spam@bad.example", 2, MessageCategory::ProductSpam),
    }));

    ActionRequest request;
    request.target.senderAddr = "spam@bad.example";
    request.block = true;
    request.deleteMail = true;
    const ActionPlan plan = ActionPlanner(store).Plan(request);

    RecordingBackend backend;
    backend.failMoves = true;
    const ActionOutcome outcome = ActionExecutor(store, &backend).Execute(plan);

    REQUIRE(!outcome.ok);
    REQUIRE_EQ(outcome.blocked, 1);
    REQUIRE_EQ(outcome.moved, 0);
    REQUIRE_EQ(outcome.failed, 2);
    REQUIRE(store.IsBlocked("spam@bad.example", ""));
    REQUIRE(outcome.Describe().find("failed") != std::string::npos);
}

TEST(Execute_ProgressIsReportedPerMessage) {
    AnalysisStore store = OpenStore();
    REQUIRE(store.UpsertMessages({
        Message("spam@bad.example", 1, MessageCategory::ProductSpam),
        Message("spam@bad.example", 2, MessageCategory::ProductSpam),
        Message("spam@bad.example", 3, MessageCategory::ProductSpam),
    }));

    ActionRequest request;
    request.target.senderAddr = "spam@bad.example";
    request.deleteMail = true;
    const ActionPlan plan = ActionPlanner(store).Plan(request);

    RecordingBackend backend;
    ActionExecutor executor(store, &backend);
    int calls = 0;
    executor.onProgress = [&calls](int done, int total) {
        ++calls;
        REQUIRE_EQ(done, calls);
        REQUIRE_EQ(total, 3);
    };
    executor.Execute(plan);
    REQUIRE_EQ(calls, 3);
}

TEST(Execute_AnEmptyPlanDoesNothing) {
    AnalysisStore store = OpenStore();
    RecordingBackend backend;
    const ActionOutcome outcome = ActionExecutor(store, &backend).Execute(ActionPlan{});
    REQUIRE(outcome.ok);
    REQUIRE_EQ(outcome.moved, 0);
    REQUIRE_EQ(outcome.blocked, 0);
    REQUIRE(backend.moves.empty());
    REQUIRE_EQ(outcome.Describe(), std::string("Nothing to do."));
}
