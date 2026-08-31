// Apps/EmailCleaner/engine/EmailCleanerTypes.h
// Core data types for the EmailCleaner analysis engine: the category taxonomy,
// the analysed message / attachment / keyword-hit records that go into the
// analysis database, and the aggregate shapes the map view (sender blocks),
// the timetable (weekday x hour grid) and the timeline read back out of it.
//
// Everything here is pure: no database, no network, no UI. The UTC calendar
// helpers at the bottom exist so bucketing is deterministic across machines
// (a timetable must not change because the reader moved timezone).
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace EmailCleaner {

// What a message turned out to be. The first four are legitimate traffic; the
// rest are the families EmailCleaner exists to surface.
enum class MessageCategory {
    Unclassified = 0,
    Personal,        // addressed to the account owner, no bulk markers
    Newsletter,      // opt-in bulk mail (List-Unsubscribe, list-id, ...)
    Notification,    // no-reply automation: receipts, alerts, confirmations
    ProductSpam,     // unsolicited advertising (pharma, replicas, casino, ...)
    AdultContent,    // pornography / adult services
    DatingScam,      // romance and "lonely singles" bait
    PhishingScam,    // credential harvesting, account-suspension bait
    FinancialScam,   // advance-fee, inheritance, crypto-profit fraud
    MalwareRisk      // carries an executable / script / macro attachment
};

std::string      ToString(MessageCategory category);
MessageCategory  CategoryFromString(const std::string& s);

// Human-facing label for a category ("Product spam", "Dating scam", ...).
std::string CategoryLabel(MessageCategory category);

// True for the five families the cleaner treats as unwanted mail.
bool IsUnwanted(MessageCategory category);

// Every category in taxonomy order — for legends, filters and rollups.
const std::vector<MessageCategory>& AllCategories();

// Which part of a message a keyword rule looks at.
enum class MatchField {
    Any = 0,      // subject + body + sender
    Subject,
    Body,
    Sender,       // display name and address
    Attachment    // attachment file names and media types
};

std::string ToString(MatchField field);
MatchField  MatchFieldFromString(const std::string& s);

// One rule term that fired while classifying a message. Kept per message so
// the detail view can explain *why* something was flagged.
struct KeywordHit {
    MessageCategory category = MessageCategory::Unclassified;
    MatchField      field    = MatchField::Any;
    std::string     term;
    double          weight   = 0.0;
};

// An attachment as the index knows it (bytes are not kept — the analysis
// database stores metadata only; the raw message stays in the mail cache).
struct AttachmentRecord {
    std::string filename;
    std::string mediaType;
    int64_t     sizeBytes = 0;
    bool        isInline  = false;
    bool        risky     = false;  // executable / script / macro-bearing type
};

// A message after parsing and classification — the row the analysis database
// stores, plus its attachments and keyword hits.
struct AnalyzedMessage {
    std::string accountId;
    std::string folder;
    int64_t     uid = 0;

    std::string messageId;
    std::string senderName;
    std::string senderAddr;      // lowercased
    std::string senderDomain;    // lowercased
    std::string subject;
    int64_t     date      = 0;   // epoch seconds (UTC)
    int64_t     sizeBytes = 0;   // size of the raw message
    uint32_t    flags     = 0;   // UltraMail::MessageFlag bits
    bool        automated = false;  // bulk / list / auto-submitted headers

    int     attachmentCount = 0;
    int64_t attachmentBytes = 0;

    MessageCategory category = MessageCategory::Unclassified;
    double          score    = 0.0;  // 0..100 unwanted-ness

    // The sender's unsubscribe offer, as the List-Unsubscribe headers gave it
    // (RFC 2369 / RFC 8058). Kept per message because a sender can change or
    // rotate it; the store rolls it up to the newest one per sender.
    std::string unsubMailto;
    std::string unsubMailtoSubject;
    std::string unsubUrl;
    bool        unsubOneClick = false;

    // True when the sender was on the blocklist at ingest time.
    bool blocked = false;

    // True when `category` / `score` are the user's correction rather than the
    // classifier's own verdict, which is then kept in baseCategory / baseScore
    // so taking the correction back restores it.
    //
    // When `overridden` is false the effective verdict *is* the classifier's,
    // so the base fields need not be set — and the store writes them from
    // `category` / `score`. That invariant is what lets a message be built by
    // hand without having to know about any of this.
    bool            overridden   = false;
    MessageCategory baseCategory = MessageCategory::Unclassified;
    double          baseScore    = 0.0;

    // Record the classifier's verdict: the effective one and the base to
    // restore to are the same thing until a correction is applied.
    void SetClassifierVerdict(MessageCategory c, double s) {
        category = baseCategory = c;
        score = baseScore = s;
        overridden = false;
    }
    // Replace the verdict with the user's correction, keeping the base.
    void ApplyOverride(MessageCategory c) {
        category   = c;
        score      = IsUnwanted(c) ? 100.0 : 0.0;
        overridden = true;
    }

    std::vector<AttachmentRecord> attachments;
    std::vector<KeywordHit>       hits;

    // Composite key of a message inside the store.
    bool SameKey(const AnalyzedMessage& other) const {
        return accountId == other.accountId && folder == other.folder && uid == other.uid;
    }
};

// One block of the map view: everything a single sender ever sent.
struct SenderBlock {
    std::string senderAddr;
    std::string displayName;
    std::string domain;

    int     messageCount    = 0;
    int     unwantedCount   = 0;
    int     attachmentCount = 0;
    int64_t totalBytes      = 0;
    int64_t attachmentBytes = 0;
    int64_t firstSeen       = 0;   // epoch seconds
    int64_t lastSeen        = 0;

    MessageCategory topCategory   = MessageCategory::Unclassified;
    double          averageScore  = 0.0;

    double UnwantedRatio() const {
        return messageCount > 0 ? static_cast<double>(unwantedCount) / messageCount : 0.0;
    }
};

// How the map view sizes a block.
enum class SenderMetric {
    MessageCount = 0,
    TotalBytes,
    AttachmentBytes,
    UnwantedCount
};

std::string ToString(SenderMetric metric);
double      MetricValue(const SenderBlock& block, SenderMetric metric);

// Timeline granularity.
enum class TimeBucket { Day = 0, Week, Month, Year };

std::string ToString(TimeBucket bucket);

// One column of the timeline chart.
struct TimelinePoint {
    int64_t     bucketStart   = 0;   // epoch seconds, UTC, start of the bucket
    std::string label;               // "2026-08", "Week of 3 Aug 2026", ...
    int         messageCount  = 0;
    int         unwantedCount = 0;
    int64_t     totalBytes    = 0;
};

// The "timetable": when in the week a sender's mail arrives. Row-major
// weekday x hour grid in UTC, so the same data yields the same picture
// everywhere.
struct Timetable {
    static constexpr int Days  = 7;    // 0 = Monday .. 6 = Sunday
    static constexpr int Hours = 24;

    std::vector<int> counts = std::vector<int>(Days * Hours, 0);
    int total     = 0;
    int peakCount = 0;
    int peakDay   = -1;
    int peakHour  = -1;

    int  At(int day, int hour) const;
    void Add(int day, int hour, int count = 1);
    // Recompute total / peak from counts (after bulk filling).
    void Recompute();
};

// Name of a weekday index (0 = Monday), for axis labels.
std::string WeekdayName(int day, bool shortForm = true);

// Per-category rollup for the legend and the category filter.
struct CategoryTotal {
    MessageCategory category      = MessageCategory::Unclassified;
    int             messageCount  = 0;
    int64_t         totalBytes    = 0;
};

// Per-media-type attachment rollup.
struct AttachmentTypeTotal {
    std::string mediaType;
    int         count       = 0;
    int64_t     totalBytes  = 0;
    int         riskyCount  = 0;
};

// One-glance numbers for the status bar.
struct StoreOverview {
    int     accounts    = 0;
    int     messages    = 0;
    int     senders     = 0;
    int     attachments = 0;
    int     unwanted    = 0;
    int64_t totalBytes      = 0;
    int64_t attachmentBytes = 0;
    int64_t firstDate = 0;
    int64_t lastDate  = 0;
};

// Filter shared by every store query. Empty / zero fields are "no constraint",
// so the default filter selects everything.
struct MessageFilter {
    std::string accountId;
    std::string folder;
    std::string senderAddr;
    std::string senderDomain;

    MessageCategory category    = MessageCategory::Unclassified;
    bool            categorySet = false;   // Unclassified is a real category

    bool unwantedOnly         = false;
    bool withAttachmentsOnly  = false;

    int64_t since = 0;   // inclusive, epoch seconds; 0 = unbounded
    int64_t until = 0;   // exclusive; 0 = unbounded

    std::string search;  // case-insensitive substring on subject / sender
    int         limit = 0;  // 0 = no limit
};

// ---- Address and date helpers (pure, shared by the ingest and the tests) ----

// Split an RFC 5322 address header value ("Erika <erika@example.com>") into a
// display name and a lowercased address. Handles quoted names, angle
// brackets, a bare address, and a leading comment.
void ParseAddress(const std::string& headerValue,
                  std::string& outName, std::string& outAddress);

// Domain part of an address, lowercased ("" when there is no '@').
std::string DomainOf(const std::string& address);

// Local part of an address, lowercased.
std::string LocalPartOf(const std::string& address);

// Parse an RFC 5322 Date: header into epoch seconds. Accepts the optional
// day-of-week, 2- or 4-digit years, numeric zones (+0200) and the obsolete
// alphabetic zones (UT, GMT, EST, ...). Returns false when nothing parses.
bool ParseRfc5322Date(const std::string& value, int64_t& outEpoch);

// ---- UTC calendar helpers --------------------------------------------------

struct UtcParts {
    int year   = 1970;
    int month  = 1;    // 1..12
    int day    = 1;    // 1..31
    int hour   = 0;
    int minute = 0;
    int second = 0;
    int weekday = 3;   // 0 = Monday .. 6 = Sunday (1970-01-01 was a Thursday)
};

int64_t  MakeUtcTime(int year, int month, int day, int hour, int minute, int second);
UtcParts BreakUtcTime(int64_t epochSeconds);

// Start of the bucket containing `epochSeconds`, in UTC. Weeks start Monday.
int64_t  BucketStart(int64_t epochSeconds, TimeBucket bucket);
// Human label for a bucket start ("14 Aug 2026", "Aug 2026", "2026").
std::string BucketLabel(int64_t bucketStart, TimeBucket bucket);

// Compact size string ("1.4 MB") for tooltips and labels.
std::string FormatBytes(int64_t bytes);
// "14 Aug 2026" — the date format used across the app's labels.
std::string FormatDate(int64_t epochSeconds);

} // namespace EmailCleaner
