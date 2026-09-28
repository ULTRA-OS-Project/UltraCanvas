// Apps/UltraMail/engine/UltraMailMessageFilter.h
// "Show emails ▸" in the message list's menu: narrow the list to one kind of
// mail - from the same sender, unread, needing an answer, spam, social media,
// payments and invoices. The list computes each message's facts from what it
// already holds (flags, the needs-answer list, the sender badge, the brand
// registry); this decides which messages a filter keeps. Headless.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailSenderBrands.h"
#include "UltraMailTypes.h"

#include <optional>
#include <string>

namespace UltraMail {

enum class MessageFilterKind {
    All = 0,        // no filter
    SameSender,     // from MessageFilter::sender
    Unread,
    NeedsAnswer,
    Spam,           // the sender badge says spam or scam
    SocialMedia,    // from a social network or messaging service
    Payments        // a payment service, or an invoice / receipt / payment subject
};

struct MessageFilter {
    MessageFilterKind kind = MessageFilterKind::All;
    std::string sender;      // SameSender: the address (compared case-insensitively)

    bool Active() const { return kind != MessageFilterKind::All; }
};

// What the list knows about one message, beyond its envelope.
struct MessageFacts {
    bool unread = false;
    bool needsAnswer = false;
    bool spam = false;                          // badge class Spam or Scam
    std::optional<BrandCategory> brand;         // the sender's registered service
};

bool FilterMatches(const MessageFilter& filter, const MessageEnvelope& message,
                   const MessageFacts& facts);

// Whether a subject reads like an invoice, a receipt, a bill or a payment
// notice (English, German, French, Spanish, Italian, Dutch, Portuguese).
bool LooksLikePaymentSubject(const std::string& subject);

// The filter in words, for the list title: "Unread", "From anna@x.example", ...
std::string Describe(const MessageFilter& filter);

} // namespace UltraMail
