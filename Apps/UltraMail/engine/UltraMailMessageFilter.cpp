// Apps/UltraMail/engine/UltraMailMessageFilter.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraMailMessageFilter.h"

#include <cctype>

namespace UltraMail {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

bool LooksLikePaymentSubject(const std::string& subject) {
    // Lower-cased ASCII stems (UTF-8 subjects keep their non-ASCII bytes, which
    // no stem below contains, so matching stays byte-safe).
    static const char* const kStems[] = {
        // English
        "invoice", "receipt", "payment", "your bill", "billing", "statement of account",
        "order confirmation", "amount due", "paid", "refund", "direct debit",
        // German
        "rechnung", "quittung", "zahlung", "beleg", "lastschrift", "gutschrift",
        "zahlungserinnerung", "mahnung", "bestellbest", "kontoauszug",
        // French / Spanish / Italian / Dutch / Portuguese
        "facture", "paiement", "factura", "recibo", "pago", "fattura", "ricevuta",
        "pagamento", "factuur", "betaling", "betaalbewijs", "fatura",
    };
    const std::string s = Lower(subject);
    for (const char* stem : kStems)
        if (s.find(stem) != std::string::npos) return true;
    return false;
}

bool FilterMatches(const MessageFilter& filter, const MessageEnvelope& message,
                   const MessageFacts& facts) {
    switch (filter.kind) {
        case MessageFilterKind::All:         return true;
        case MessageFilterKind::SameSender:  return !filter.sender.empty() &&
                                                    Lower(message.fromAddr) == Lower(filter.sender);
        case MessageFilterKind::Unread:      return facts.unread;
        case MessageFilterKind::NeedsAnswer: return facts.needsAnswer;
        case MessageFilterKind::Spam:        return facts.spam;
        case MessageFilterKind::SocialMedia:
            return facts.brand && (*facts.brand == BrandCategory::Social ||
                                   *facts.brand == BrandCategory::Messaging);
        case MessageFilterKind::Payments:
            return (facts.brand && *facts.brand == BrandCategory::Payment) ||
                   LooksLikePaymentSubject(message.subject);
    }
    return true;
}

std::string Describe(const MessageFilter& filter) {
    switch (filter.kind) {
        case MessageFilterKind::All:         return "All";
        case MessageFilterKind::SameSender:  return "From " + filter.sender;
        case MessageFilterKind::Unread:      return "Unread";
        case MessageFilterKind::NeedsAnswer: return "Needs an answer";
        case MessageFilterKind::Spam:        return "Spam";
        case MessageFilterKind::SocialMedia: return "Social media";
        case MessageFilterKind::Payments:    return "Payments & invoices";
    }
    return "All";
}

} // namespace UltraMail
