// Tests/UltraMail/test_logincheck.cpp
// The login check behind the server settings page: session options from the
// settings, and the plug-in's answer passed through unchanged.
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailLoginCheck.h"

using namespace UltraMail;

namespace {

// A mailbox that records what it was asked and answers as told.
class RecordingMailbox : public IMailboxProtocolPlugin {
public:
    UltraNetResult answer = UltraNetResult::Ok();
    std::string lastUrl;
    UltraNetMailOptions lastOptions;

    std::string GetName() const override { return "Recording"; }
    std::string GetVersion() const override { return "0"; }
    std::vector<std::string> GetSupportedSchemes() const override { return {"imap", "imaps"}; }
    UltraNetResult Initialize(const UltraNetConfig&) override { return UltraNetResult::Ok(); }
    void Shutdown() override {}
    UltraNetResult SendMail(const UltraNetMailMessage&, const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }
    UltraNetResult FetchMessages(const std::string&, std::vector<UltraNetMailMessage>&,
                                 const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
    UltraNetResult ListFolders(const std::string& url, std::vector<UltraNetMailFolder>&,
                               const UltraNetMailOptions& o) override {
        lastUrl = url; lastOptions = o; return answer;
    }
    UltraNetResult GetMailboxStatus(const std::string&, const std::string&, UltraNetMailboxStatus&,
                                    const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
    UltraNetResult FetchEnvelopes(const std::string&, const std::string&, uint32_t,
                                  std::vector<UltraNetMailEnvelope>&, const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }
    UltraNetResult FetchMessage(const std::string&, const std::string&, uint32_t, std::string&,
                                const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
    UltraNetResult StoreFlags(const std::string&, const std::string&, uint32_t, UltraNetMailFlags,
                              bool, const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
    UltraNetResult MoveMessage(const std::string&, const std::string&, uint32_t, const std::string&,
                               const UltraNetMailOptions&) override { return UltraNetResult::Ok(); }
    UltraNetResult AppendMessage(const std::string&, const std::string&, const std::string&,
                                 UltraNetMailFlags, const UltraNetMailOptions&) override {
        return UltraNetResult::Ok();
    }
};

MailServerSettings Server(const std::string& host, int port, MailSecurity sec) {
    MailServerSettings s;
    s.host = host; s.port = port; s.security = sec; s.username = "erika@example.org";
    return s;
}

} // namespace

TEST(login_check_options_follow_the_security_setting) {
    UltraNetCredentials c; c.type = UltraNetAuthType::Basic; c.password = "pw";

    UltraNetMailOptions ssl = LoginCheck::OptionsFor(Server("mail.example.org", 993, MailSecurity::SslTls), c);
    REQUIRE(ssl.useTls); REQUIRE(ssl.implicitTls);
    REQUIRE_EQ(ssl.credentials.username, std::string("erika@example.org"));   // from the settings
    REQUIRE_EQ(ssl.credentials.password, std::string("pw"));

    UltraNetMailOptions start = LoginCheck::OptionsFor(Server("mail.example.org", 143, MailSecurity::StartTls), c);
    REQUIRE(start.useTls); REQUIRE(!start.implicitTls);

    UltraNetMailOptions plain = LoginCheck::OptionsFor(Server("mail.example.org", 143, MailSecurity::Plain), c);
    REQUIRE(!plain.useTls); REQUIRE(!plain.implicitTls);

    c.username = "erika";   // an explicit username wins
    REQUIRE_EQ(LoginCheck::OptionsFor(Server("h", 1, MailSecurity::SslTls), c).credentials.username,
               std::string("erika"));
}

TEST(login_check_lists_once_and_passes_the_answer_through) {
    RecordingMailbox mailbox;
    UltraNetCredentials c; c.type = UltraNetAuthType::Basic; c.password = "pw";

    REQUIRE(LoginCheck::Imap(mailbox, Server("mail.example.org", 993, MailSecurity::SslTls), c));
    REQUIRE_EQ(mailbox.lastUrl, std::string("imaps://mail.example.org:993/"));
    REQUIRE(mailbox.lastOptions.implicitTls);

    mailbox.answer = UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed, "LOGIN failed");
    UltraNetResult r = LoginCheck::Imap(mailbox, Server("mail.example.org", 143, MailSecurity::StartTls), c);
    REQUIRE(!r);
    REQUIRE(r.code == UltraNetResultCode::AuthenticationFailed);
    REQUIRE_EQ(r.message, std::string("LOGIN failed"));
    REQUIRE_EQ(mailbox.lastUrl, std::string("imap://mail.example.org:143/"));

    // Incomplete settings never reach the network.
    mailbox.lastUrl.clear();
    REQUIRE(!LoginCheck::Imap(mailbox, MailServerSettings{}, c));
    REQUIRE(mailbox.lastUrl.empty());
}
