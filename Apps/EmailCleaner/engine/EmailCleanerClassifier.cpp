// Apps/EmailCleaner/engine/EmailCleanerClassifier.cpp
// Text normalisation, keyword matching and the structural signals that decide
// a message's category and score.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerClassifier.h"

#include "EmailCleanerText.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace EmailCleaner {

namespace {

bool IsWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

// Substring search honouring the rule's word-boundary flags.
bool ContainsTerm(const std::string& haystack, const KeywordRule& rule) {
    if (rule.term.empty() || haystack.empty()) return false;
    size_t pos = haystack.find(rule.term);
    while (pos != std::string::npos) {
        const size_t end = pos + rule.term.size();
        const bool startOk = rule.openStart || pos == 0 || !IsWordChar(haystack[pos - 1]);
        const bool endOk   = rule.openEnd   || end == haystack.size() || !IsWordChar(haystack[end]);
        if (startOk && endOk) return true;
        pos = haystack.find(rule.term, pos + 1);
    }
    return false;
}

// Extensions that execute, script or carry macros. Kept as a sorted lookup so
// adding a type is a one-line change.
bool IsRiskyExtension(const std::string& ext) {
    static const char* kRisky[] = {
        "ace", "app", "bat", "chm", "cmd", "com", "cpl", "dll", "docm", "dotm",
        "exe", "hta", "img", "iso", "jar", "js", "jse", "lnk", "msi", "msc",
        "pif", "pptm", "ps1", "reg", "scf", "scr", "sh", "vb", "vbe", "vbs",
        "wsf", "wsh", "xlam", "xlsm", "xltm"
    };
    for (const char* r : kRisky) {
        if (ext == r) return true;
    }
    return false;
}

bool IsRiskyMediaType(const std::string& mediaType) {
    static const char* kRisky[] = {
        "application/x-msdownload", "application/x-dosexec",
        "application/x-executable", "application/vnd.microsoft.portable-executable",
        "application/x-msdos-program", "application/x-sh",
        "application/javascript", "text/javascript", "application/x-ms-shortcut"
    };
    for (const char* r : kRisky) {
        if (mediaType == r) return true;
    }
    return false;
}

// A "double extension" lure: invoice.pdf.exe, photo.jpg.scr, ...
bool HasDoubleExtension(const std::string& lowerName) {
    const size_t last = lowerName.rfind('.');
    if (last == std::string::npos || last == 0) return false;
    const std::string first = lowerName.substr(0, last);
    const size_t prev = first.rfind('.');
    if (prev == std::string::npos) return false;
    const std::string middle = first.substr(prev + 1);
    if (middle.empty() || middle.size() > 4) return false;
    for (char c : middle) {
        if (!std::isalnum(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

double CapsRatio(const std::string& text) {
    int letters = 0, upper = 0;
    for (char c : text) {
        if (std::isalpha(static_cast<unsigned char>(c))) {
            ++letters;
            if (std::isupper(static_cast<unsigned char>(c))) ++upper;
        }
    }
    return letters > 0 ? static_cast<double>(upper) / letters : 0.0;
}

// A local part like "x7f3kq91zzt" — long, mixed, and not a word.
bool LooksMachineGenerated(const std::string& localPart) {
    if (localPart.size() < 12) return false;
    int digits = 0, letters = 0;
    for (char c : localPart) {
        if (std::isdigit(static_cast<unsigned char>(c))) ++digits;
        else if (std::isalpha(static_cast<unsigned char>(c))) ++letters;
    }
    return digits >= 3 && letters >= 6;
}

// An address hidden inside a display name ("PayPal Service <security@paypal.com>"
// as the *name*, with a different real address) is a classic spoof.
std::string AddressInsideDisplayName(const std::string& displayName) {
    const size_t at = displayName.find('@');
    if (at == std::string::npos) return "";
    size_t start = at;
    while (start > 0 && (IsWordChar(displayName[start - 1]) ||
                         displayName[start - 1] == '.' || displayName[start - 1] == '-' ||
                         displayName[start - 1] == '_' || displayName[start - 1] == '+'))
        --start;
    size_t end = at + 1;
    while (end < displayName.size() && (IsWordChar(displayName[end]) ||
                                        displayName[end] == '.' || displayName[end] == '-'))
        ++end;
    std::string candidate = displayName.substr(start, end - start);
    if (candidate.size() < 5 || candidate.find('.', at - start) == std::string::npos)
        return "";
    std::transform(candidate.begin(), candidate.end(), candidate.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return candidate;
}

} // namespace

// ---- Normalisation ---------------------------------------------------------
// The pipeline itself lives in EmailCleanerText so RuleSet can fold its terms
// through exactly the same steps; these stay as the classifier's documented
// entry points (and are what the tests drive).

std::string Classifier::StripHtml(const std::string& html) {
    return EmailCleaner::StripHtml(html);
}

std::string Classifier::CollapseObfuscation(const std::string& text) {
    return EmailCleaner::CollapseObfuscation(text);
}

std::string Classifier::NormalizeText(const std::string& text) {
    return NormalizeForMatching(text);
}

std::string Classifier::ExtensionOf(const std::string& filename) {
    const size_t dot = filename.rfind('.');
    if (dot == std::string::npos || dot + 1 >= filename.size()) return "";
    std::string ext = filename.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool Classifier::IsRiskyAttachment(const std::string& filename,
                                   const std::string& mediaType) {
    std::string lowerName = filename;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string lowerType = mediaType;
    std::transform(lowerType.begin(), lowerType.end(), lowerType.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (IsRiskyExtension(ExtensionOf(lowerName))) return true;
    if (IsRiskyMediaType(lowerType)) return true;

    // A double extension whose visible part looks like a document is a lure
    // even when the real extension is otherwise harmless.
    if (HasDoubleExtension(lowerName) && IsRiskyExtension(ExtensionOf(lowerName)))
        return true;
    return false;
}

// ---- Classification --------------------------------------------------------

Classification Classifier::Classify(const ClassifierInput& input) const {
    Classification result;

    const std::string subject    = NormalizeText(input.subject);
    const std::string body       = NormalizeText(input.body);
    const std::string sender     = NormalizeText(input.senderName + " " + input.senderAddr);
    std::string attachmentText;
    for (const AttachmentRecord& a : input.attachments)
        attachmentText += a.filename + " " + a.mediaType + " ";
    attachmentText = NormalizeText(attachmentText);

    const std::string anyText = subject + " \n " + body + " \n " + sender;

    std::map<MessageCategory, double> weights;

    // ---- Keyword rules -----------------------------------------------------
    for (const KeywordRule& rule : rules_.Rules()) {
        const std::string* haystack = nullptr;
        switch (rule.field) {
            case MatchField::Subject:    haystack = &subject; break;
            case MatchField::Body:       haystack = &body; break;
            case MatchField::Sender:     haystack = &sender; break;
            case MatchField::Attachment: haystack = &attachmentText; break;
            case MatchField::Any:        haystack = &anyText; break;
        }
        if (!haystack || !ContainsTerm(*haystack, rule)) continue;

        weights[rule.category] += rule.weight;
        result.hits.push_back(KeywordHit{ rule.category, rule.field, rule.term, rule.weight });
    }

    // ---- Structural signals ------------------------------------------------

    // Risky attachments are evidence on their own, and the strongest kind.
    int riskyAttachments = 0;
    for (const AttachmentRecord& a : input.attachments) {
        if (a.risky || IsRiskyAttachment(a.filename, a.mediaType)) ++riskyAttachments;
    }
    if (riskyAttachments > 0) {
        const double weight = 4.0 + 1.0 * (riskyAttachments - 1);
        weights[MessageCategory::MalwareRisk] += weight;
        result.hits.push_back(KeywordHit{ MessageCategory::MalwareRisk,
                                          MatchField::Attachment,
                                          "executable attachment", weight });
    }

    // A display name carrying a different address than the envelope sender.
    const std::string hiddenAddr = AddressInsideDisplayName(input.senderName);
    if (!hiddenAddr.empty() && !input.senderAddr.empty() && hiddenAddr != input.senderAddr) {
        weights[MessageCategory::PhishingScam] += 3.0;
        result.hits.push_back(KeywordHit{ MessageCategory::PhishingScam, MatchField::Sender,
                                          "display name hides " + hiddenAddr, 3.0 });
    }

    // Reply-To pointing at a different domain than the sender.
    if (!input.replyToAddr.empty() && !input.senderAddr.empty()) {
        const std::string fromDomain    = DomainOf(input.senderAddr);
        const std::string replyToDomain = DomainOf(input.replyToAddr);
        if (!fromDomain.empty() && !replyToDomain.empty() && fromDomain != replyToDomain) {
            weights[MessageCategory::PhishingScam] += 2.0;
            result.hits.push_back(KeywordHit{ MessageCategory::PhishingScam, MatchField::Sender,
                                              "reply-to domain " + replyToDomain, 2.0 });
        }
    }

    // A shouting subject, and exclamation-mark pile-ups.
    if (input.subject.size() >= 12 && CapsRatio(input.subject) > 0.7) {
        weights[MessageCategory::ProductSpam] += 1.5;
        result.hits.push_back(KeywordHit{ MessageCategory::ProductSpam, MatchField::Subject,
                                          "subject in capitals", 1.5 });
    }
    if (std::count(input.subject.begin(), input.subject.end(), '!') >= 3) {
        weights[MessageCategory::ProductSpam] += 1.0;
        result.hits.push_back(KeywordHit{ MessageCategory::ProductSpam, MatchField::Subject,
                                          "exclamation marks", 1.0 });
    }

    // A machine-generated local part, typical of throwaway spam senders.
    if (LooksMachineGenerated(LocalPartOf(input.senderAddr))) {
        weights[MessageCategory::ProductSpam] += 1.0;
        result.hits.push_back(KeywordHit{ MessageCategory::ProductSpam, MatchField::Sender,
                                          "random sender address", 1.0 });
    }

    // Bulk headers say "machine to a list" — newsletter evidence, and a mild
    // discount on the message being personal.
    if (input.bulkHeaders) {
        weights[MessageCategory::Newsletter] += 2.5;
        result.hits.push_back(KeywordHit{ MessageCategory::Newsletter, MatchField::Any,
                                          "bulk mail headers", 2.5 });
    }

    // ---- Decide ------------------------------------------------------------
    // The unwanted families win over Newsletter / Notification when they clear
    // the threshold: a newsletter selling pills is still spam.
    MessageCategory bestUnwanted = MessageCategory::Unclassified;
    double bestUnwantedWeight = 0.0;
    MessageCategory bestBenign = MessageCategory::Unclassified;
    double bestBenignWeight = 0.0;

    for (const auto& [category, weight] : weights) {
        if (IsUnwanted(category)) {
            if (weight > bestUnwantedWeight) { bestUnwantedWeight = weight; bestUnwanted = category; }
        } else {
            if (weight > bestBenignWeight) { bestBenignWeight = weight; bestBenign = category; }
        }
    }

    if (bestUnwantedWeight >= kDecisionThreshold) {
        result.category       = bestUnwanted;
        result.categoryWeight = bestUnwantedWeight;
    } else if (bestBenignWeight >= kDecisionThreshold) {
        result.category       = bestBenign;
        result.categoryWeight = bestBenignWeight;
    } else if (input.bulkHeaders) {
        result.category       = MessageCategory::Newsletter;
        result.categoryWeight = bestBenignWeight;
    } else if (input.addressedToOwner) {
        result.category       = MessageCategory::Personal;
        result.categoryWeight = bestBenignWeight;
    } else {
        result.category       = MessageCategory::Unclassified;
        result.categoryWeight = bestBenignWeight;
    }

    // The score always reports unwanted-ness, whatever the winning category:
    // the strongest unwanted family, plus a little from the others, so a
    // message that scores 2.5 in three different scam families still stands
    // out in the map view.
    double unwantedTotal = bestUnwantedWeight;
    for (const auto& [category, weight] : weights) {
        if (IsUnwanted(category) && category != bestUnwanted)
            unwantedTotal += weight * 0.5;
    }
    result.score = std::min(100.0, 100.0 * unwantedTotal / kFullScoreWeight);
    return result;
}

} // namespace EmailCleaner
