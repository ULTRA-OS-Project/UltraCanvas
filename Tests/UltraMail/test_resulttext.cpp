// Tests/UltraMail/test_resulttext.cpp
// A failed network operation in words: the sentence names what was talked to
// (the mail server unless the caller says otherwise), the account advice goes
// with the mail server only, the details line never repeats the summary.
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailResultText.h"

using namespace UltraMail;

namespace {
UltraNetResult Failed(UltraNetResultCode code, const std::string& message = "", int http = 0) {
    UltraNetResult r = UltraNetResult::Error(code, message);
    r.httpStatus = http;
    return r;
}
} // namespace

TEST(result_text_names_the_mail_server_by_default_with_the_account_advice) {
    REQUIRE_EQ(FriendlyMessage(Failed(UltraNetResultCode::HostNotFound)),
               std::string("The mail server could not be found. Check the server address and "
                           "your internet connection."));
    REQUIRE_EQ(FriendlyMessage(Failed(UltraNetResultCode::ConnectionRefused)),
               std::string("The mail server refused the connection. Check the server address "
                           "and port for this account."));
    REQUIRE_EQ(FriendlyMessage(Failed(UltraNetResultCode::AuthenticationFailed)),
               std::string("Sign-in was rejected. Check the e-mail address and password for "
                           "this account."));
    REQUIRE_EQ(FriendlyMessage(Failed(UltraNetResultCode::InvalidUrl)),
               std::string("The server address for this account is not valid."));
    REQUIRE_EQ(FriendlyMessage(Failed(UltraNetResultCode::SendFailed)),
               std::string("The message could not be handed to the mail server."));
    REQUIRE_EQ(FriendlyMessage(Failed(UltraNetResultCode::TlsCertificateInvalid)),
               std::string("The security certificate of the mail server is not valid, so the "
                           "connection was not trusted."));
}

TEST(result_text_names_another_server_without_the_account_advice) {
    const std::string svc = "the sign-in service";
    std::string text = FriendlyMessage(Failed(UltraNetResultCode::HostNotFound), svc);
    REQUIRE_EQ(text, std::string("The sign-in service could not be found. Check your internet "
                                 "connection."));
    REQUIRE(text.find("mail server") == std::string::npos);
    REQUIRE_EQ(FriendlyMessage(Failed(UltraNetResultCode::ConnectionRefused), svc),
               std::string("The sign-in service refused the connection."));
    REQUIRE_EQ(FriendlyMessage(Failed(UltraNetResultCode::AuthenticationFailed), svc),
               std::string("The sign-in service rejected the sign-in."));
    REQUIRE_EQ(FriendlyMessage(Failed(UltraNetResultCode::AuthenticationRequired), svc),
               std::string("The sign-in service asked for a sign-in."));
    REQUIRE_EQ(FriendlyMessage(Failed(UltraNetResultCode::InvalidUrl), svc),
               std::string("The address of the sign-in service is not valid."));
    REQUIRE_EQ(FriendlyMessage(Failed(UltraNetResultCode::SendFailed), svc),
               std::string("The request could not be sent to the sign-in service."));
    REQUIRE_EQ(FriendlyMessage(Failed(UltraNetResultCode::AccessDenied), svc),
               std::string("The sign-in service denied access."));
    REQUIRE_EQ(FriendlyMessage(Failed(UltraNetResultCode::Timeout), "the sender's web server"),
               std::string("The sender's web server did not respond in time."));
    // An HTTP-level refusal names the status; no code-specific advice means
    // the result's own words, or a plain "failed" when it has none.
    REQUIRE_EQ(FriendlyMessage(Failed(UltraNetResultCode::HttpError, "HTTP 503", 503), svc),
               std::string("The sign-in service answered with an error (HTTP 503)."));
    REQUIRE_EQ(FriendlyMessage(Failed(UltraNetResultCode::Unknown, "something odd"), svc),
               std::string("something odd"));
    REQUIRE_EQ(FriendlyMessage(Failed(UltraNetResultCode::Unknown), svc),
               std::string("The operation failed."));
}

TEST(result_text_detail_line_adds_only_what_the_summary_lacks) {
    const UltraNetResult dns = Failed(UltraNetResultCode::HostNotFound, "could not resolve host");
    REQUIRE_EQ(DetailLine(dns), std::string("could not resolve host"));
    REQUIRE_EQ(DetailLine(dns, "the sign-in service"), std::string("could not resolve host"));
    // A message that is the summary already is not drawn twice.
    const UltraNetResult same = Failed(UltraNetResultCode::Unknown, "something odd");
    REQUIRE(DetailLine(same).empty());
    REQUIRE(DetailLine(Failed(UltraNetResultCode::Timeout)).empty());
    UltraDbResult db; db.message = "disk full";
    REQUIRE_EQ(DetailLine(db), std::string("disk full"));
}

TEST(result_text_retryable_is_the_transient_network_conditions) {
    REQUIRE(IsRetryable(Failed(UltraNetResultCode::HostNotFound)));
    REQUIRE(IsRetryable(Failed(UltraNetResultCode::Timeout)));
    REQUIRE(IsRetryable(Failed(UltraNetResultCode::TlsHandshakeFailed)));
    REQUIRE(!IsRetryable(Failed(UltraNetResultCode::AuthenticationFailed)));
    REQUIRE(!IsRetryable(Failed(UltraNetResultCode::PluginNotFound)));
    REQUIRE(!IsRetryable(Failed(UltraNetResultCode::TlsCertificateInvalid)));
}
