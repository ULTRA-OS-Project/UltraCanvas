// Apps/EmailCleaner/engine/EmailCleanerClassifier.h
// Turns one parsed message into a category, a 0..100 unwanted-ness score and
// the list of terms that fired. Two sources of evidence are combined:
//
//   * the keyword RuleSet (product spam, adult content, dating scams,
//     phishing, financial fraud, malware lures, newsletters, notifications);
//   * structural signals a keyword list cannot see — risky attachment types,
//     a display name that hides a different address, a Reply-To pointing at
//     another domain, shouting subjects, bulk-mail headers.
//
// The text is normalised first, which is what makes the keyword list hold up
// against the obfuscation spam actually uses: "V1AGRA", "v.i.a.g.r.a" and
// "<b>vi</b>agra" all normalise to "viagra".
//
// Pure and headless: no database, no network, no UI.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "EmailCleanerRules.h"
#include "EmailCleanerTypes.h"

#include <string>
#include <vector>

namespace EmailCleaner {

// Everything the classifier looks at. The ingest fills this from the parsed
// MIME message; tests fill it directly.
struct ClassifierInput {
    std::string subject;
    std::string body;          // display body (HTML is stripped internally)
    std::string senderName;
    std::string senderAddr;
    std::string replyToAddr;   // empty when the message has no Reply-To

    std::vector<AttachmentRecord> attachments;

    // True when the message carries List-Unsubscribe / List-Id / Precedence:
    // bulk / Auto-Submitted — i.e. it was sent by a machine to a list.
    bool bulkHeaders = false;
    // True when the account owner is a direct To:/Cc: recipient.
    bool addressedToOwner = true;
};

struct Classification {
    MessageCategory         category = MessageCategory::Unclassified;
    double                  score    = 0.0;   // 0..100
    std::vector<KeywordHit> hits;

    // Total rule weight accumulated for the winning category — the raw
    // "points" the score is derived from (kept for tests and tooltips).
    double categoryWeight = 0.0;

    bool Unwanted() const { return IsUnwanted(category); }
};

class Classifier {
public:
    Classifier() : rules_(RuleSet::BuiltIn()) {}
    explicit Classifier(RuleSet rules) : rules_(std::move(rules)) {}

    const RuleSet& Rules() const { return rules_; }
    void SetRules(RuleSet rules) { rules_ = std::move(rules); }

    Classification Classify(const ClassifierInput& input) const;

    // Weight at which a category is considered established. Below it a message
    // falls through to the structural defaults (Personal / Newsletter / ...).
    static constexpr double kDecisionThreshold = 3.0;
    // Weight that maps to a score of 100.
    static constexpr double kFullScoreWeight = 8.0;

    // ---- Exposed for tests and for the rule editor -------------------------

    // Lowercase, strip HTML tags and entities, undo leet spelling and
    // letter-separator obfuscation, collapse whitespace.
    static std::string NormalizeText(const std::string& text);

    // Remove HTML tags and decode the handful of entities that matter.
    static std::string StripHtml(const std::string& html);

    // Collapse "v.i.a.g.r.a" / "v i a g r a" style spacing to "viagra".
    static std::string CollapseObfuscation(const std::string& text);

    // Does this attachment carry an executable, script or macro-bearing type?
    static bool IsRiskyAttachment(const std::string& filename,
                                  const std::string& mediaType);

    // Lowercased extension of a filename, without the dot ("" when none).
    static std::string ExtensionOf(const std::string& filename);

private:
    RuleSet rules_;
};

} // namespace EmailCleaner
