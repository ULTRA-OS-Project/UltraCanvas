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

bool LooksLikeSocialMedia(const std::string& fromAddr, const std::string& subject) {
    // The sender's domain (after '@', without a trailing '>').
    std::string domain = Lower(fromAddr);
    if (const auto at = domain.rfind('@'); at != std::string::npos) domain = domain.substr(at + 1);
    while (!domain.empty() && (domain.back() == '>' || domain.back() == ' ')) domain.pop_back();

    // Domains of social networks and community / messaging services, matched
    // as the domain itself or a subdomain of it ("mail.instagram.com").
    static const char* const kDomains[] = {
        "facebook.com", "facebookmail.com", "fb.com", "instagram.com", "threads.net",
        "twitter.com", "x.com", "linkedin.com", "xing.com", "tiktok.com", "snapchat.com",
        "pinterest.com", "reddit.com", "redditmail.com", "tumblr.com", "quora.com",
        "discord.com", "discordapp.com", "telegram.org", "whatsapp.com", "signal.org",
        "youtube.com", "twitch.tv", "vk.com", "ok.ru", "weibo.com", "bsky.app",
        "bsky.social", "mastodon.social", "mastodon.online", "joinmastodon.org",
        "meetup.com", "nextdoor.com", "strava.com", "goodreads.com", "flickr.com",
        "deviantart.com", "behance.net", "dribbble.com", "medium.com", "substack.com",
        "patreon.com", "vimeo.com", "soundcloud.com", "clubhouse.com", "tinder.com",
        "badoo.com", "viber.com", "line.me", "wechat.com", "kakao.com",
    };
    for (const char* d : kDomains) {
        const std::string suffix = std::string(".") + d;
        if (domain == d || (domain.size() > suffix.size() &&
                            domain.compare(domain.size() - suffix.size(), suffix.size(), suffix) == 0))
            return true;
    }
    // Self-hosted networks name themselves in the host: a Mastodon, Pleroma,
    // Friendica or Lemmy server ("mastodon.example", "social.example").
    static const char* const kHostWords[] = {
        "mastodon", "pleroma", "friendica", "lemmy", "misskey", "pixelfed", "diaspora",
    };
    for (const char* w : kHostWords)
        if (domain.find(w) != std::string::npos) return true;
    if (domain.rfind("social.", 0) == 0) return true;

    // Notification wording, as these services phrase their subjects.
    static const char* const kPhrases[] = {
        // English
        "commented on your", "replied to your", "liked your", "reacted to your",
        "mentioned you", "tagged you", "new follower", "followed you", "is now following you",
        "started following you", "sent you a message", "friend request",
        "wants to connect", "connection request", "invitation to connect",
        "shared a post", "new post from", "favourited your", "boosted your",
        "retweeted", "reposted your", "you have new notifications", "viewed your profile",
        // German
        "hat deinen beitrag", "hat ihren beitrag", "hat dich erw", "gefällt dein",
        "neue follower", "folgt dir", "freundschaftsanfrage", "kontaktanfrage",
        "hat dir eine nachricht", "hat auf deinen", "hat dein profil",
        // French / Spanish / Italian / Dutch / Portuguese
        "a commenté votre", "vous a mentionné", "nouvel abonné", "demande d'ami",
        "comentó tu", "te mencionó", "nuevo seguidor", "solicitud de amistad",
        "ha commentato", "ti ha menzionato", "nuovo follower", "richiesta di amicizia",
        "heeft gereageerd op", "nieuwe volger", "vriendschapsverzoek",
        "comentou", "novo seguidor", "pedido de amizade",
    };
    const std::string s = Lower(subject);
    for (const char* phrase : kPhrases)
        if (s.find(phrase) != std::string::npos) return true;
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
            return (facts.brand && (*facts.brand == BrandCategory::Social ||
                                    *facts.brand == BrandCategory::Messaging)) ||
                   LooksLikeSocialMedia(message.fromAddr, message.subject);
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
