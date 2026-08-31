// Apps/EmailCleaner/engine/EmailCleanerActions.h
// Turning a block on the map into something done about it: block the sender,
// unsubscribe, and clear its mail out of the mailbox.
//
// The split here is deliberate and is what makes destructive work reviewable:
//
//   ActionPlanner  — pure. Given a selection and the store, it says exactly
//                    what *would* happen: how many messages, from which
//                    folders, whether unsubscribing is possible and whether it
//                    is advisable, and what the caller should be warned about.
//                    No side effects, fully unit-tested.
//
//   ActionExecutor — carries a plan out. Every outward-facing step goes
//                    through an interface so the whole path is testable
//                    against a fake, and so the app can run a plan with the
//                    mail side absent (block-only) without special cases.
//
// Deleting moves messages to the account's Trash folder (IMAP UID MOVE) rather
// than setting \Deleted: the point of a cleaner is to get mail out of the way,
// not to make it unrecoverable, and a user who empties their own Trash has
// made that second decision themselves.
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "EmailCleanerStore.h"
#include "EmailCleanerTypes.h"
#include "EmailCleanerUnsubscribe.h"

#include <functional>
#include <string>
#include <vector>

namespace EmailCleaner {

// What the user picked on the map. Exactly one of the two is set; a domain
// selection covers every sender under it.
struct ActionTarget {
    std::string senderAddr;
    std::string domain;

    bool Valid()    const { return !senderAddr.empty() || !domain.empty(); }
    bool IsDomain() const { return senderAddr.empty() && !domain.empty(); }
    // How the target reads in a confirmation prompt.
    std::string Describe() const {
        return senderAddr.empty() ? ("everything from " + domain) : senderAddr;
    }
};

// Which of the three things to do. They combine: "block, unsubscribe and
// clear out" is one plan.
struct ActionRequest {
    ActionTarget target;
    bool block       = false;   // add to the local blocklist
    bool unsubscribe = false;   // take the sender's unsubscribe offer
    bool deleteMail  = false;   // move the sender's mail to Trash

    // Only touch messages the analysis considers unwanted, leaving anything
    // classified as personal or transactional where it is. On by default for
    // a domain target, where one address in the group may be legitimate.
    bool unwantedOnly = false;
};

// One message the plan would move.
struct PlannedMessage {
    std::string accountId;
    std::string folder;
    int64_t     uid = 0;
    std::string subject;
    MessageCategory category = MessageCategory::Unclassified;
};

// What would happen, worked out before anything is touched.
struct ActionPlan {
    ActionTarget target;

    // Blocking
    bool        willBlock = false;
    std::string blockPattern;      // the address or domain that gets blocked
    bool        alreadyBlocked = false;

    // Unsubscribing
    bool              willUnsubscribe = false;
    UnsubscribeInfo   unsubscribe;
    UnsubscribeMethod method = UnsubscribeMethod::None;
    UnsubscribeAdvice advice = UnsubscribeAdvice::NoOffer;

    // Deleting
    bool                        willDelete = false;
    std::vector<PlannedMessage> messages;      // exactly what would move
    int                         skippedWanted = 0;  // left alone by unwantedOnly

    // The dominant category of the selection, which is what the unsubscribe
    // advice was judged against.
    MessageCategory category = MessageCategory::Unclassified;

    // Things the user must see before confirming — an empty list means the
    // plan is routine.
    std::vector<std::string> warnings;

    bool Empty() const { return !willBlock && !willUnsubscribe && !willDelete; }
    int  MessageCount() const { return static_cast<int>(messages.size()); }

    // A one-paragraph summary for the confirmation dialog.
    std::string Describe() const;
};

class ActionPlanner {
public:
    explicit ActionPlanner(const AnalysisStore& store) : store_(store) {}

    // Work out the plan. `accountId` scopes it to one account when set, as the
    // account bar's filter does. Never mutates anything.
    ActionPlan Plan(const ActionRequest& request, const std::string& accountId = "") const;

private:
    const AnalysisStore& store_;
};

// ---- Execution -------------------------------------------------------------

// The outward-facing steps, behind an interface so a plan can be run against a
// fake in tests and so the app can supply only the parts it has wired up.
class IActionBackend {
public:
    virtual ~IActionBackend() = default;

    // Move one message to the account's Trash. Returns false with a reason.
    virtual bool MoveToTrash(const std::string& accountId, const std::string& folder,
                             int64_t uid, std::string& outError) = 0;

    // POST "List-Unsubscribe=One-Click" to the URL (RFC 8058).
    virtual bool PostOneClick(const std::string& url, std::string& outError) = 0;

    // Send an unsubscribe mail to `address` from the given account.
    virtual bool SendUnsubscribeMail(const std::string& accountId,
                                     const std::string& address,
                                     const std::string& subject,
                                     std::string& outError) = 0;
};

struct ActionOutcome {
    bool ok = true;
    int  blocked = 0;        // 1 when the blocklist gained an entry
    int  unsubscribed = 0;   // 1 when an unsubscribe request went out
    int  moved = 0;          // messages moved to Trash
    int  failed = 0;
    std::vector<std::string> errors;

    // What happened, for the status line.
    std::string Describe() const;
};

class ActionExecutor {
public:
    // `backend` may be null: the blocklist half still runs, and anything
    // needing the mailbox is reported as unavailable rather than silently
    // skipped.
    ActionExecutor(AnalysisStore& store, IActionBackend* backend)
        : store_(store), backend_(backend) {}

    // Run a plan. Blocking happens first (it is local and cannot fail
    // outward), then unsubscribing, then the moves.
    ActionOutcome Execute(const ActionPlan& plan);

    // Called once per moved message, for a progress bar.
    std::function<void(int done, int total)> onProgress;

    // Epoch seconds stamped on new blocklist entries. Left at 0 the entries
    // carry no date, which is what the tests want; the app sets a real clock.
    int64_t now = 0;

private:
    AnalysisStore&  store_;
    IActionBackend* backend_ = nullptr;
};

} // namespace EmailCleaner
