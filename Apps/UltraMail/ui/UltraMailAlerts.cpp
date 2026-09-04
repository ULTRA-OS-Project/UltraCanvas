// Apps/UltraMail/ui/UltraMailAlerts.cpp
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailAlerts.h"

using namespace UltraCanvas;

namespace UltraMail {

std::string FriendlyMessage(const UltraNetResult& result) {
    switch (result.code) {
        case UltraNetResultCode::HostNotFound:
            return "The mail server could not be found. Check the server address "
                   "and your internet connection.";
        case UltraNetResultCode::ConnectionRefused:
            return "The mail server refused the connection. Check the server "
                   "address and port for this account.";
        case UltraNetResultCode::ConnectionReset:
            return "The mail server closed the connection unexpectedly.";
        case UltraNetResultCode::ConnectionTimeout:
        case UltraNetResultCode::Timeout:
            return "The mail server did not respond in time.";
        case UltraNetResultCode::TlsHandshakeFailed:
            return "A secure connection to the mail server could not be established.";
        case UltraNetResultCode::TlsCertificateInvalid:
            return "The mail server's security certificate is not valid, so the "
                   "connection was not trusted.";
        case UltraNetResultCode::TlsCertificateExpired:
            return "The mail server's security certificate has expired, so the "
                   "connection was not trusted.";
        case UltraNetResultCode::AuthenticationRequired:
            return "The mail server asked for a sign-in. Add the password for "
                   "this account.";
        case UltraNetResultCode::AuthenticationFailed:
            return "Sign-in was rejected. Check the e-mail address and password "
                   "for this account.";
        case UltraNetResultCode::AccessDenied:
            return "The mail server denied access to this account.";
        case UltraNetResultCode::InvalidUrl:
        case UltraNetResultCode::UnsupportedScheme:
            return "The server address for this account is not valid.";
        case UltraNetResultCode::PluginNotFound:
            return "The mail protocol plug-in is not installed, so UltraMail "
                   "cannot reach the server.";
        case UltraNetResultCode::PluginError:
            return "The mail protocol plug-in reported a problem.";
        case UltraNetResultCode::SendFailed:
            return "The message could not be handed to the mail server.";
        case UltraNetResultCode::ReceiveFailed:
            return "The reply from the mail server could not be read.";
        case UltraNetResultCode::NotFound:
            return "The mail server reported that the item no longer exists.";
        case UltraNetResultCode::Cancelled:
            return "The operation was cancelled.";
        default:
            break;
    }
    return result.message.empty() ? "The operation failed." : result.message;
}

std::string DetailLine(const UltraNetResult& result) {
    // Only worth showing when it adds to the summary FriendlyMessage produced.
    if (result.message.empty()) return {};
    if (result.message == FriendlyMessage(result)) return {};
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

namespace {

void ShowAlert(AlertSeverity severity, UltraCanvasWindowBase* parent,
               const std::string& summary, const std::string& detail,
               std::function<void()> onDismissed) {
    AlertOptions opts;
    opts.severity = severity;
    opts.message  = summary;
    opts.details  = detail;
    opts.title    = "UltraMail";
    opts.parent   = parent;
    if (onDismissed)
        opts.onResult = [onDismissed](DialogResult) { onDismissed(); };
    UltraCanvasAlert::Show(opts);
}

} // namespace

void AlertError(UltraCanvasWindowBase* parent,
                const std::string& summary, const std::string& detail,
                std::function<void()> onDismissed) {
    ShowAlert(AlertSeverity::Error, parent, summary, detail, std::move(onDismissed));
}

void AlertWarning(UltraCanvasWindowBase* parent,
                  const std::string& summary, const std::string& detail,
                  std::function<void()> onDismissed) {
    ShowAlert(AlertSeverity::Warning, parent, summary, detail, std::move(onDismissed));
}

void AlertSuccess(UltraCanvasWindowBase* parent,
                  const std::string& summary, const std::string& detail,
                  std::function<void()> onDismissed) {
    ShowAlert(AlertSeverity::Successful, parent, summary, detail, std::move(onDismissed));
}

void AlertErrorRetry(UltraCanvasWindowBase* parent,
                     const std::string& summary, const std::string& detail,
                     std::function<void()> onRetry) {
    AlertOptions opts;
    opts.severity      = AlertSeverity::Error;
    opts.message       = summary;
    opts.details       = detail;
    opts.title         = "UltraMail";
    opts.parent        = parent;
    opts.buttons       = DialogButtons::RetryCancel;
    opts.defaultButton = DialogButton::Retry;
    opts.onResult      = [onRetry](DialogResult r) {
        if (r == DialogResult::Retry && onRetry) onRetry();
    };
    UltraCanvasAlert::Show(opts);
}

} // namespace UltraMail
