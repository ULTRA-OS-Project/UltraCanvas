// Tests/EmailCleaner/test_classifier.cpp
// Text normalisation, keyword matching and the structural spam signals.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "EmailCleanerClassifier.h"

#include <algorithm>

using namespace EmailCleaner;

namespace {

bool HasHit(const Classification& verdict, const std::string& term) {
    return std::any_of(verdict.hits.begin(), verdict.hits.end(),
                       [&term](const KeywordHit& h) { return h.term == term; });
}

ClassifierInput Message(const std::string& subject, const std::string& body,
                        const std::string& fromName = "Someone",
                        const std::string& fromAddr = "someone@example.net") {
    ClassifierInput input;
    input.subject    = subject;
    input.body       = body;
    input.senderName = fromName;
    input.senderAddr = fromAddr;
    return input;
}

} // namespace

// ---- Normalisation ---------------------------------------------------------

TEST(Normalize_LowercasesAndFoldsLeetSpelling) {
    REQUIRE_EQ(Classifier::NormalizeText("V1AGRA"), std::string("viagra"));
    REQUIRE_EQ(Classifier::NormalizeText("C1AL15"), std::string("cialis"));
    REQUIRE_EQ(Classifier::NormalizeText("C4SIN0"), std::string("casino"));
}

TEST(Normalize_UndoesSeparatorObfuscation) {
    REQUIRE_EQ(Classifier::NormalizeText("v.i.a.g.r.a"), std::string("viagra"));
    REQUIRE_EQ(Classifier::NormalizeText("v-i-a-g-r-a"), std::string("viagra"));
    REQUIRE_EQ(Classifier::NormalizeText("v i a g r a"), std::string("viagra"));
}

TEST(Normalize_LeavesOrdinaryProseAlone) {
    // Short letter runs are prose, not obfuscation: "a b" and "I am" survive.
    REQUIRE_EQ(Classifier::NormalizeText("a b test"), std::string("a b test"));
    REQUIRE_EQ(Classifier::NormalizeText("Plan B is ready"),
               std::string("plan b is ready"));
    // Real words keep their internal punctuation semantics.
    REQUIRE_EQ(Classifier::NormalizeText("e-mail me"), std::string("e-mail me"));
}

TEST(Normalize_StripsHtmlAndDecodesEntities) {
    const std::string html =
        "<html><head><style>.x{color:red}</style></head><body>"
        "<p>Buy&nbsp;now</p><script>alert('viagra')</script></body></html>";
    const std::string text = Classifier::NormalizeText(html);
    REQUIRE(text.find("buy now") != std::string::npos);
    REQUIRE(text.find("color") == std::string::npos);   // style contents dropped
    REQUIRE(text.find("alert") == std::string::npos);   // script contents dropped
}

TEST(Normalize_JoinsWordsSplitByInlineTags) {
    // The classic tag-splitting trick must not defeat the keyword list:
    // inline formatting disappears without leaving a word boundary.
    REQUIRE_EQ(Classifier::NormalizeText("<b>via</b>gra"), std::string("viagra"));
    REQUIRE_EQ(Classifier::NormalizeText("vi<span style=\"x\">a</span>gra"),
               std::string("viagra"));
    // Block-level elements are real boundaries and keep the words apart.
    REQUIRE_EQ(Classifier::NormalizeText("<p>buy</p><p>now</p>"), std::string("buy now"));
}

TEST(Normalize_CollapsesWhitespaceAcrossLines) {
    REQUIRE_EQ(Classifier::NormalizeText("lonely\n   singles\there"),
               std::string("lonely singles here"));
}

// ---- Attachments -----------------------------------------------------------

TEST(RiskyAttachments_ByExtensionAndMediaType) {
    REQUIRE(Classifier::IsRiskyAttachment("invoice.exe", "application/octet-stream"));
    REQUIRE(Classifier::IsRiskyAttachment("photo.jpg.scr", "image/jpeg"));
    REQUIRE(Classifier::IsRiskyAttachment("report.docm", "application/vnd.ms-word"));
    REQUIRE(Classifier::IsRiskyAttachment("setup.MSI", "application/octet-stream"));
    REQUIRE(Classifier::IsRiskyAttachment("thing.dat", "application/x-msdownload"));

    REQUIRE(!Classifier::IsRiskyAttachment("invoice.pdf", "application/pdf"));
    REQUIRE(!Classifier::IsRiskyAttachment("photo.jpg", "image/jpeg"));
    REQUIRE(!Classifier::IsRiskyAttachment("notes.txt", "text/plain"));
    REQUIRE(!Classifier::IsRiskyAttachment("", ""));
}

TEST(ExtensionOf_HandlesOddNames) {
    REQUIRE_EQ(Classifier::ExtensionOf("a.b.PDF"), std::string("pdf"));
    REQUIRE_EQ(Classifier::ExtensionOf("noextension"), std::string(""));
    REQUIRE_EQ(Classifier::ExtensionOf("trailing."), std::string(""));
}

// ---- Categories ------------------------------------------------------------

TEST(Classify_ProductSpam) {
    Classifier classifier;
    const Classification verdict = classifier.Classify(Message(
        "Canadian Pharmacy - cheap pills, no prescription",
        "Order now and get free shipping on all our products."));
    REQUIRE(verdict.category == MessageCategory::ProductSpam);
    REQUIRE(verdict.Unwanted());
    REQUIRE(verdict.score > 50.0);
}

TEST(Classify_ProductSpamSurvivesObfuscation) {
    Classifier classifier;
    const Classification verdict = classifier.Classify(Message(
        "Best V-I-A-G-R-A prices",
        "<p>C1alis and v.i.a.g.r.a with no prescription</p>"));
    REQUIRE(verdict.category == MessageCategory::ProductSpam);
    REQUIRE(HasHit(verdict, "viagra"));
}

TEST(Classify_AdultContent) {
    Classifier classifier;
    const Classification verdict = classifier.Classify(Message(
        "Live cams waiting for you",
        "Watch adult videos and nude photos from webcam girls tonight."));
    REQUIRE(verdict.category == MessageCategory::AdultContent);
    REQUIRE(verdict.Unwanted());
}

TEST(Classify_DatingScam) {
    Classifier classifier;
    const Classification verdict = classifier.Classify(Message(
        "I saw your profile",
        "Lonely singles in your area are waiting. She wants to meet you tonight!"));
    REQUIRE(verdict.category == MessageCategory::DatingScam);
    REQUIRE(verdict.score > 60.0);
}

TEST(Classify_Phishing) {
    Classifier classifier;
    const Classification verdict = classifier.Classify(Message(
        "Security alert: unusual sign-in activity",
        "Your account has been suspended. Verify your account to restore access."));
    REQUIRE(verdict.category == MessageCategory::PhishingScam);
}

TEST(Classify_FinancialScam) {
    Classifier classifier;
    const Classification verdict = classifier.Classify(Message(
        "Confidential business proposal",
        "You are the beneficiary of the sum of ten million dollars from an "
        "unclaimed inheritance; I need your bank account details."));
    REQUIRE(verdict.category == MessageCategory::FinancialScam);
}

TEST(Classify_MalwareRiskFromAttachment) {
    Classifier classifier;
    ClassifierInput input = Message("Invoice 4451", "Please see the attached invoice.");
    AttachmentRecord attachment;
    attachment.filename  = "invoice_4451.pdf.exe";
    attachment.mediaType = "application/octet-stream";
    attachment.sizeBytes = 90000;
    input.attachments.push_back(attachment);

    const Classification verdict = classifier.Classify(input);
    REQUIRE(verdict.category == MessageCategory::MalwareRisk);
    REQUIRE(HasHit(verdict, "executable attachment"));
}

TEST(Classify_PhishingFromSpoofedDisplayName) {
    Classifier classifier;
    ClassifierInput input = Message("Your invoice", "The invoice is ready.",
                                    "Billing <billing@paypal.com>",
                                    "x9f2kd@mail.example.ru");
    const Classification verdict = classifier.Classify(input);
    REQUIRE(verdict.category == MessageCategory::PhishingScam);
    REQUIRE(HasHit(verdict, "display name hides billing@paypal.com"));
}

TEST(Classify_ReplyToOnAnotherDomainIsEvidence) {
    Classifier classifier;
    ClassifierInput input = Message("Please confirm your password",
                                    "Follow the link to continue.",
                                    "Support", "support@bank.example");
    input.replyToAddr = "collect@elsewhere.example";
    const Classification verdict = classifier.Classify(input);
    REQUIRE(verdict.category == MessageCategory::PhishingScam);
    REQUIRE(HasHit(verdict, "reply-to domain elsewhere.example"));
}

TEST(Classify_ShoutingSubjectAndExclamations) {
    Classifier classifier;
    const Classification verdict = classifier.Classify(
        Message("URGENT SPECIAL OFFER TODAY!!!", "Limited time offer, act now."));
    REQUIRE(HasHit(verdict, "subject in capitals"));
    REQUIRE(HasHit(verdict, "exclamation marks"));
    REQUIRE(verdict.category == MessageCategory::ProductSpam);
}

TEST(Classify_OrdinaryMailStaysPersonal) {
    Classifier classifier;
    ClassifierInput input = Message(
        "Lunch on Thursday?",
        "Hi Erika, are you free for lunch on Thursday around one? - Jonas",
        "Jonas Meyer", "jonas@partner.example");
    input.addressedToOwner = true;

    const Classification verdict = classifier.Classify(input);
    REQUIRE(verdict.category == MessageCategory::Personal);
    REQUIRE(!verdict.Unwanted());
    REQUIRE_EQ(verdict.score, 0.0);
}

TEST(Classify_NewsletterFromBulkHeaders) {
    Classifier classifier;
    ClassifierInput input = Message("This week at the museum",
                                    "Our programme for the coming week. "
                                    "You are receiving this email because you subscribed. "
                                    "Unsubscribe at any time.",
                                    "Museum News", "news@museum.example");
    input.bulkHeaders = true;

    const Classification verdict = classifier.Classify(input);
    REQUIRE(verdict.category == MessageCategory::Newsletter);
    REQUIRE(!verdict.Unwanted());
}

TEST(Classify_NotificationFromNoReplySender) {
    Classifier classifier;
    ClassifierInput input = Message("Your order has shipped",
                                    "Your parcel is on its way. Do not reply to this message.",
                                    "Shop", "no-reply@shop.example");
    const Classification verdict = classifier.Classify(input);
    REQUIRE(verdict.category == MessageCategory::Notification);
}

TEST(Classify_SpamInsideANewsletterStillReadsAsSpam) {
    // A bulk sender with strong spam content must not hide behind its
    // List-Unsubscribe header.
    Classifier classifier;
    ClassifierInput input = Message("Weekly deals: cheap pills and replica watches",
                                    "Unsubscribe at any time. No prescription needed!",
                                    "Deals", "deals@bulk.example");
    input.bulkHeaders = true;

    const Classification verdict = classifier.Classify(input);
    REQUIRE(verdict.category == MessageCategory::ProductSpam);
}

TEST(Classify_WordBoundariesPreventFalsePositives) {
    Classifier classifier;
    // "sex" is a rule term, but Essex and unisex are not adult content.
    ClassifierInput input = Message("Essex office move",
                                    "The unisex facilities on the third floor reopen Monday.",
                                    "Facilities", "facilities@work.example");
    const Classification verdict = classifier.Classify(input);
    REQUIRE(verdict.category != MessageCategory::AdultContent);
    REQUIRE(!HasHit(verdict, "sex"));
}

TEST(Classify_UsesTheSuppliedRuleSetOnly) {
    RuleSet custom;
    custom.AddTerm(MessageCategory::ProductSpam, "flurgle", 5.0);
    Classifier classifier(custom);

    const Classification spam = classifier.Classify(Message("Flurgle!", "flurgle flurgle"));
    REQUIRE(spam.category == MessageCategory::ProductSpam);

    // A built-in term the custom set does not carry must not fire.
    const Classification other =
        classifier.Classify(Message("Canadian pharmacy", "cheap pills"));
    REQUIRE(other.category != MessageCategory::ProductSpam);
}

TEST(Classify_ScoreIsBoundedAndOrdered) {
    Classifier classifier;
    const Classification mild = classifier.Classify(
        Message("Special offer inside", "Free shipping this week.",
                "Shop", "shop@example.net"));
    const Classification severe = classifier.Classify(
        Message("V1AGRA and c1alis - no prescription, lowest price",
                "Canadian pharmacy, cheap pills, make money fast, act now!!!"));

    REQUIRE(severe.score > mild.score);
    REQUIRE(severe.score <= 100.0);
    REQUIRE(mild.score >= 0.0);
}

TEST(Classify_FieldScopedRulesRespectTheirField) {
    RuleSet rules;
    rules.AddTerm(MessageCategory::ProductSpam, "deal", 5.0, MatchField::Subject);
    Classifier classifier(rules);

    const Classification inSubject = classifier.Classify(Message("A deal for you", "hello"));
    REQUIRE(inSubject.category == MessageCategory::ProductSpam);

    const Classification inBodyOnly = classifier.Classify(Message("hello", "A deal for you"));
    REQUIRE(inBodyOnly.category != MessageCategory::ProductSpam);
}
