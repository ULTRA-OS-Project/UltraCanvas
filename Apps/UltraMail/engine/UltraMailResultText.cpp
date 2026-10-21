// Apps/UltraMail/engine/UltraMailResultText.cpp
// Version: 1.0.0 - moved out of ui/UltraMailAlerts.cpp, with the server named
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailResultText.h"

#include <cctype>

namespace UltraMail {

namespace {

// "the mail server" -> "The mail server", to start a sentence.
std::string Capitalised(std::string s) {
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

} // namespace

std::string FriendlyMessage(const UltraNetResult& result, const std::string& server) {
    // The account's own server gets the advice that goes with an account: its
    // password, its address and port on the settings page. Anything else gets
    // the fact alone - there is no settings page for it.
    const bool mail = server == kMailServer;
    const std::string The = Capitalised(server);
    switch (result.code) {
        case UltraNetResultCode::HostNotFound:
            return The + " could not be found. Check "
                 + (mail ? "the server address and your internet connection."
                         : "your internet connection.");
        case UltraNetResultCode::ConnectionRefused:
            return The + " refused the connection."
                 + (mail ? " Check the server address and port for this account." : "");
        case UltraNetResultCode::ConnectionReset:
            return The + " closed the connection unexpectedly.";
        case UltraNetResultCode::ConnectionTimeout:
        case UltraNetResultCode::Timeout:
            return The + " did not respond in time.";
        case UltraNetResultCode::TlsHandshakeFailed:
            return "A secure connection to " + server + " could not be established.";
        case UltraNetResultCode::TlsCertificateInvalid:
            return "The security certificate of " + server + " is not valid, so the "
                   "connection was not trusted.";
        case UltraNetResultCode::TlsCertificateExpired:
            return "The security certificate of " + server + " has expired, so the "
                   "connection was not trusted.";
        case UltraNetResultCode::TlsPublicKeyMismatch:
            return The + " did not present the security key it was trusted with, so the "
                   "connection was not trusted.";
        case UltraNetResultCode::AuthenticationRequired:
            return mail ? "The mail server asked for a sign-in. Add the password for "
                          "this account."
                        : The + " asked for a sign-in.";
        case UltraNetResultCode::AuthenticationFailed:
            return mail ? "Sign-in was rejected. Check the e-mail address and password "
                          "for this account."
                        : The + " rejected the sign-in.";
        case UltraNetResultCode::AccessDenied:
            return The + " denied access" + (mail ? " to this account." : ".");
        case UltraNetResultCode::InvalidUrl:
        case UltraNetResultCode::UnsupportedScheme:
            return mail ? "The server address for this account is not valid."
                        : "The address of " + server + " is not valid.";
        case UltraNetResultCode::PluginNotFound:
            return mail ? "The mail protocol plug-in is not installed, so UltraMail "
                          "cannot reach the server."
                        : "The protocol plug-in is not installed, so UltraMail cannot reach "
                          + server + ".";
        case UltraNetResultCode::PluginError:
            return "The protocol plug-in reported a problem.";
        case UltraNetResultCode::SendFailed:
            return mail ? "The message could not be handed to the mail server."
                        : "The request could not be sent to " + server + ".";
        case UltraNetResultCode::ReceiveFailed:
            return "The reply from " + server + " could not be read.";
        case UltraNetResultCode::HttpError:
            return The + " answered with an error"
                 + (result.httpStatus > 0 ? " (HTTP " + std::to_string(result.httpStatus) + ")" : "")
                 + ".";
        case UltraNetResultCode::NotFound:
            return The + " reported that the item no longer exists.";
        case UltraNetResultCode::Cancelled:
            return "The operation was cancelled.";
        default:
            break;
    }
    return result.message.empty() ? "The operation failed." : result.message;
}

std::string DetailLine(const UltraNetResult& result, const std::string& server) {
    // Only worth showing when it adds to the summary FriendlyMessage produced.
    if (result.message.empty()) return {};
    if (result.message == FriendlyMessage(result, server)) return {};
    return result.message;
}

std::string DetailLine(const UltraDbResult& result) {
    return result.message;
}

bool IsRetryable(const UltraNetResult& result) {
    switch (result.code) {
        case UltraNetResultCode::HostNotFound:
        case UltraNetResultCode::ConnectionRefused:
        case UltraNetResultCode::ConnectionReset:
        case UltraNetResultCode::ConnectionTimeout:
        case UltraNetResultCode::Timeout:
        case UltraNetResultCode::SendFailed:
        case UltraNetResultCode::ReceiveFailed:
        case UltraNetResultCode::TlsHandshakeFailed:
            return true;
        default:
            return false;
    }
}

} // namespace UltraMail
