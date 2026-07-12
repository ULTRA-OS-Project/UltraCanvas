// Tests/UltraMail/test_composer.cpp
// Draft building (reply / reply-all / forward / new) and sending through a fake
// SMTP plug-in. All headless.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailComposer.h"
#include "UltraMailSender.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>

#include <string>
#include <vector>

using namespace UltraMail;

namespace {

SourceMessage MakeSource() {
    SourceMessage s;
    s.messageId = "<orig@example.com>";
    s.fromName = "Anna Schmidt"; s.fromAddr = "anna@example.com";
    s.to = {"erika@example.com", "team@example.com"};
    s.cc = {"boss@example.com"};
    s.subject = "Meeting notes";
    s.body = "Line one\nLine two";
    s.date = "Tue, 14 Jan 2026 14:02:00 +0000";
    return s;
}

// Records the last sent message.
class FakeSmtp : public IMailProtocolPlugin {
public:
    UltraNetMailMessage last;
    bool sent = false;

    std::string GetName() const override { return "FakeSMTP"; }
    std::string GetVersion() const override { return "0.0.1"; }
    std::vector<std::string> GetSupportedSchemes() const override { return {"smtp", "smtps"}; }
    UltraNetResult Initialize(const UltraNetConfig&) override { return UltraNetResult::Ok(); }
    void Shutdown() override {}

    UltraNetResult SendMail(const UltraNetMailMessage& m, const UltraNetMailOptions&) override {
        last = m; sent = true;
        return UltraNetResult::Ok();
    }
    UltraNetResult FetchMessages(const std::string&, std::vector<UltraNetMailMessage>&,
                                 const UltraNetMailOptions&) override {
        return UltraNetResult::Error(UltraNetResultCode::UnsupportedScheme, "fake");
    }
};

bool Contains(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& e : v) if (e == s) return true;
    return false;
}

} // namespace

TEST(reply_sets_subject_recipient_and_thread) {
    Draft d = Composer::Reply(MakeSource(), "Erika", "erika@example.com", /*replyAll=*/false);
    REQUIRE_EQ(d.subject, std::string("Re: Meeting notes"));
    REQUIRE_EQ(d.to.size(), (size_t)1);
    REQUIRE_EQ(d.to[0], std::string("anna@example.com"));   // original sender
    REQUIRE(d.cc.empty());
    REQUIRE_EQ(d.inReplyTo, std::string("<orig@example.com>"));
    REQUIRE(d.references.find("<orig@example.com>") != std::string::npos);
    REQUIRE(d.body.find("> Line one") != std::string::npos);        // quoted
    REQUIRE(d.body.find("Anna Schmidt wrote:") != std::string::npos);
}

TEST(reply_does_not_double_prefix) {
    SourceMessage s = MakeSource();
    s.subject = "Re: Meeting notes";
    Draft d = Composer::Reply(s, "Erika", "erika@example.com", false);
    REQUIRE_EQ(d.subject, std::string("Re: Meeting notes"));   // not "Re: Re: ..."
}

TEST(reply_all_includes_others_excludes_self) {
    Draft d = Composer::Reply(MakeSource(), "Erika", "erika@example.com", /*replyAll=*/true);
    REQUIRE(Contains(d.to, "anna@example.com"));
    REQUIRE(Contains(d.to, "team@example.com"));   // other To recipient
    REQUIRE(!Contains(d.to, "erika@example.com")); // self excluded
    REQUIRE(Contains(d.cc, "boss@example.com"));   // original Cc
}

TEST(forward_prefixes_and_carries_attachments) {
    SourceMessage s = MakeSource();
    Attachment att; att.filename = "doc.pdf"; att.mediaType = "application/pdf";
    att.data = {1, 2, 3};
    s.attachments.push_back(att);

    Draft d = Composer::Forward(s, "Erika", "erika@example.com");
    REQUIRE_EQ(d.subject, std::string("Fwd: Meeting notes"));
    REQUIRE(d.to.empty());
    REQUIRE(d.body.find("Forwarded message") != std::string::npos);
    REQUIRE(d.body.find("Line one") != std::string::npos);
    REQUIRE_EQ(d.attachments.size(), (size_t)1);
    REQUIRE_EQ(d.attachments[0].filename, std::string("doc.pdf"));
}

TEST(new_message_carries_identity) {
    Draft d = Composer::NewMessage("Erika Example", "erika@example.com");
    REQUIRE_EQ(d.fromAddr, std::string("erika@example.com"));
    REQUIRE(d.to.empty());
    REQUIRE(d.subject.empty());
}

TEST(sender_maps_draft_to_message_and_sends) {
    FakeSmtp fake;
    MailSender sender(fake);

    Draft d = Composer::NewMessage("Erika", "erika@example.com");
    d.to = {"bob@example.com"};
    d.cc = {"carol@example.com"};
    d.subject = "Hello";
    d.body = "Hi Bob";
    Attachment att; att.filename = "note.txt"; att.data = {'h', 'i'};
    d.attachments.push_back(att);

    UltraNetMailOptions opts;
    UltraNetResult r = sender.Send(d, "smtps://smtp.example.com:465/", opts);
    REQUIRE(bool(r));
    REQUIRE(fake.sent);
    REQUIRE(fake.last.from.find("erika@example.com") != std::string::npos);
    REQUIRE_EQ(fake.last.to.size(), (size_t)1);
    REQUIRE_EQ(fake.last.to[0], std::string("bob@example.com"));
    REQUIRE_EQ(fake.last.cc[0], std::string("carol@example.com"));
    REQUIRE_EQ(fake.last.subject, std::string("Hello"));
    REQUIRE(fake.last.contentType.find("text/plain") != std::string::npos);
    REQUIRE_EQ(fake.last.attachments.size(), (size_t)1);
    REQUIRE_EQ(fake.last.attachments[0].first, std::string("note.txt"));
}
