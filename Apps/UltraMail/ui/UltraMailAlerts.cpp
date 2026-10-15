// Apps/UltraMail/ui/UltraMailAlerts.cpp
// Version: 0.3.0 - the result-to-text helpers moved to engine/UltraMailResultText.cpp
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailAlerts.h"

#include "UltraCanvasUtils.h"   // UltraCanvas::versionString

using namespace UltraCanvas;

namespace UltraMail {

std::string WithDiagnostics(const std::string& detail, const std::string& diagnostics) {
    if (diagnostics.empty()) return detail;
    std::string out = detail;
    if (!out.empty()) out += "\n\n";
    // The first link of the chain is the program itself: which UltraMail on
    // which framework - the plug-in only knows itself.
    out += "**Connection details**\n\n```\n";
    out += std::string("Application: UltraMail ") + ULTRAMAIL_VERSION + ", UltraCanvas "
         + UltraCanvas::versionString + "\n";
    out += diagnostics;
    if (out.back() != '\n') out += '\n';
    out += "```";
    return out;
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

void AlertWarningRetry(UltraCanvasWindowBase* parent,
                       const std::string& summary, const std::string& detail,
                       std::function<void()> onRetry) {
    AlertOptions opts;
    opts.severity      = AlertSeverity::Warning;
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
