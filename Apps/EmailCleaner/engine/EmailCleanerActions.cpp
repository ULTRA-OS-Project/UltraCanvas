// Apps/EmailCleaner/engine/EmailCleanerActions.cpp
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerActions.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <utility>

namespace EmailCleaner {

namespace {

std::string Plural(int n, const std::string& one, const std::string& many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

// The category a selection reads as: the one carrying the most messages, with
// the unwanted families winning a tie. This is what the unsubscribe advice is
// judged against, so a tie must not quietly land on the benign side.
MessageCategory DominantCategory(const std::vector<AnalyzedMessage>& messages) {
    std::map<MessageCategory, int> counts;
    for (const AnalyzedMessage& m : messages) counts[m.category] += 1;

    MessageCategory best = MessageCategory::Unclassified;
    int bestCount = -1;
    for (const auto& [category, count] : counts) {
        if (count > bestCount ||
            (count == bestCount && IsUnwanted(category) && !IsUnwanted(best))) {
            best = category;
            bestCount = count;
        }
    }
    return best;
}

} // namespace

// ---- Plan ------------------------------------------------------------------

std::string ActionPlan::Describe() const {
    if (Empty()) return "Nothing selected to do.";

    std::vector<std::string> parts;
    if (willBlock) {
        parts.push_back(alreadyBlocked
                            ? ("keep " + blockPattern + " blocked")
                            : ("block " + blockPattern));
    }
    if (willUnsubscribe)
        parts.push_back("unsubscribe by " + ToString(method));
    if (willDelete)
        parts.push_back("move " + Plural(MessageCount(), "message", "messages") + " to Trash");

    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i == 0)                        out += parts[i];
        else if (i + 1 == parts.size())    out += " and " + parts[i];
        else                               out += ", " + parts[i];
    }
    out += " for " + target.Describe() + ".";

    if (skippedWanted > 0) {
        out += " " + Plural(skippedWanted, "message", "messages") +
               " classified as wanted will be left alone.";
    }
    return out;
}

ActionPlan ActionPlanner::Plan(const ActionRequest& request,
                               const std::string& accountId) const {
    ActionPlan plan;
    plan.target = request.target;
    if (!request.target.Valid()) {
        plan.warnings.push_back("No sender or domain selected.");
        return plan;
    }

    // The selection, as a store filter.
    MessageFilter filter;
    filter.accountId = accountId;
    if (request.target.IsDomain()) filter.senderDomain = request.target.domain;
    else                           filter.senderAddr   = request.target.senderAddr;

    std::vector<AnalyzedMessage> messages;
    store_.ListMessages(filter, messages);
    plan.category = DominantCategory(messages);

    // ---- Block -------------------------------------------------------------
    if (request.block) {
        plan.willBlock = true;
        plan.blockPattern = request.target.IsDomain() ? request.target.domain
                                                      : request.target.senderAddr;
        plan.alreadyBlocked =
            store_.IsBlocked(request.target.senderAddr, request.target.domain);
        if (request.target.IsDomain()) {
            plan.warnings.push_back(
                "Blocking the whole of " + request.target.domain +
                " covers every sender under it, including any you still want.");
        }
    }

    // ---- Unsubscribe -------------------------------------------------------
    if (request.unsubscribe) {
        bool found = false;
        store_.GetUnsubscribeOffer(filter, plan.unsubscribe.mailto,
                                   plan.unsubscribe.mailtoSubject,
                                   plan.unsubscribe.httpUrl,
                                   plan.unsubscribe.oneClick, found);
        plan.method = ChooseMethod(plan.unsubscribe);
        plan.advice = AdviseUnsubscribe(plan.category, plan.unsubscribe);

        // Only the two advice values the app can act on unattended actually
        // schedule the step. Everything else is reported, not performed.
        plan.willUnsubscribe = (plan.advice == UnsubscribeAdvice::Recommended);

        if (plan.advice != UnsubscribeAdvice::Recommended)
            plan.warnings.push_back(DescribeAdvice(plan.advice, plan.category));
        if (plan.advice == UnsubscribeAdvice::NeedsBrowser && !plan.unsubscribe.httpUrl.empty())
            plan.warnings.push_back("Open by hand: " + plan.unsubscribe.httpUrl);
    }

    // ---- Delete ------------------------------------------------------------
    if (request.deleteMail) {
        plan.willDelete = true;
        plan.messages.reserve(messages.size());
        for (const AnalyzedMessage& m : messages) {
            if (request.unwantedOnly && !IsUnwanted(m.category)) {
                ++plan.skippedWanted;
                continue;
            }
            plan.messages.push_back(PlannedMessage{ m.accountId, m.folder, m.uid,
                                                    m.subject, m.category });
        }
        if (plan.messages.empty()) {
            plan.willDelete = false;
            plan.warnings.push_back("No messages match — nothing would be moved.");
        }

        // Moving mail the analysis thinks is wanted is the mistake worth
        // catching before it happens, not after.
        const int wanted = static_cast<int>(
            std::count_if(plan.messages.begin(), plan.messages.end(),
                          [](const PlannedMessage& m) { return !IsUnwanted(m.category); }));
        if (wanted > 0) {
            plan.warnings.push_back(
                Plural(wanted, "message is", "messages are") +
                " classified as wanted (personal, newsletter or notification) and "
                "would be moved too.");
        }
    }

    return plan;
}

// ---- Execution -------------------------------------------------------------

std::string ActionOutcome::Describe() const {
    std::vector<std::string> parts;
    if (blocked > 0)      parts.push_back("blocked the sender");
    if (unsubscribed > 0) parts.push_back("sent the unsubscribe request");
    if (moved > 0)        parts.push_back("moved " + Plural(moved, "message", "messages") +
                                          " to Trash");
    if (parts.empty() && failed == 0) return "Nothing to do.";

    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += ", ";
        out += parts[i];
    }
    if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    if (failed > 0) {
        if (!out.empty()) out += ". ";
        out += Plural(failed, "step", "steps") + " failed";
        if (!errors.empty()) out += ": " + errors.front();
    }
    if (!out.empty() && out.back() != '.') out += ".";
    return out;
}

ActionOutcome ActionExecutor::Execute(const ActionPlan& plan) {
    ActionOutcome outcome;
    if (plan.Empty()) return outcome;

    // ---- Block: local, and first, so a failure further on still leaves the
    // sender blocked rather than requiring the user to try again.
    if (plan.willBlock && !plan.blockPattern.empty()) {
        BlockEntry entry;
        entry.pattern  = plan.blockPattern;
        entry.isDomain = plan.target.IsDomain();
        entry.reason   = CategoryLabel(plan.category);
        entry.added    = now;   // 0 unless the caller stamped a clock
        const UltraDbResult r = store_.AddBlock(entry);
        if (r) {
            outcome.blocked = 1;
            store_.ApplyBlocklistToMessages();
        } else {
            outcome.ok = false;
            ++outcome.failed;
            outcome.errors.push_back("could not block: " + r.message);
        }
    }

    // ---- Unsubscribe -------------------------------------------------------
    if (plan.willUnsubscribe) {
        if (!backend_) {
            outcome.ok = false;
            ++outcome.failed;
            outcome.errors.push_back("no mail connection: cannot unsubscribe");
        } else {
            std::string error;
            bool sent = false;
            switch (plan.method) {
                case UnsubscribeMethod::OneClickPost:
                    sent = backend_->PostOneClick(plan.unsubscribe.httpUrl, error);
                    break;
                case UnsubscribeMethod::MailTo: {
                    // The account the mail came to is the one that has to ask
                    // to be removed; any of the planned messages names it.
                    const std::string account =
                        plan.messages.empty() ? std::string() : plan.messages.front().accountId;
                    sent = backend_->SendUnsubscribeMail(
                        account, plan.unsubscribe.mailto,
                        plan.unsubscribe.mailtoSubject.empty() ? "unsubscribe"
                                                               : plan.unsubscribe.mailtoSubject,
                        error);
                    break;
                }
                default:
                    error = "no unattended unsubscribe method";
                    break;
            }
            if (sent) {
                outcome.unsubscribed = 1;
            } else {
                outcome.ok = false;
                ++outcome.failed;
                outcome.errors.push_back("unsubscribe failed: " + error);
            }
        }
    }

    // ---- Delete ------------------------------------------------------------
    if (plan.willDelete) {
        if (!backend_) {
            outcome.ok = false;
            ++outcome.failed;
            outcome.errors.push_back("no mail connection: cannot move messages");
        } else {
            const int total = plan.MessageCount();
            int done = 0;
            for (const PlannedMessage& m : plan.messages) {
                std::string error;
                if (backend_->MoveToTrash(m.accountId, m.folder, m.uid, error)) {
                    ++outcome.moved;
                } else {
                    outcome.ok = false;
                    ++outcome.failed;
                    // One line per failure would drown a large run; keep the
                    // first few and the count carries the rest.
                    if (outcome.errors.size() < 5)
                        outcome.errors.push_back("could not move uid " +
                                                 std::to_string(m.uid) + ": " + error);
                }
                if (onProgress) onProgress(++done, total);
            }
        }
    }

    return outcome;
}

} // namespace EmailCleaner
