// Apps/UltraMail/ui/UltraMailAlerts.h
// UltraMail's error-reporting surface — a thin layer over UltraCanvasAlert
// (UltraCanvas/include/UltraCanvasAlert.h). Every failure gets a severity, a
// one-line summary the user can act on, and the underlying diagnostic on the
// alert's secondary `details` line.
//
// The engine already carries the diagnosis: UltraNetResult::message,
// UltraDbResult::message and SyncOutcome::message. These helpers are how that
// text reaches the screen instead of being dropped at the UI boundary, and they
// implement the error-handling contract in Docs/UltraMail/Concept.md —
// AuthenticationFailed / TlsCertificateInvalid / HostNotFound and the rest map
// to friendly text, with a retry option where a retry can succeed.
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasAlert.h"

#include <UltraDatabase/UltraDatabaseCore.h>
#include <UltraNet/UltraNetCore.h>

#include <functional>
#include <string>

namespace UltraMail {

// ===== RESULT -> TEXT =====

// A short, actionable sentence for a failed network / mail operation, chosen
// from the result code. Falls back to the result's own message when the code
// carries no specific advice.
std::string FriendlyMessage(const UltraNetResult& result);

// The raw diagnostic for the alert's details line. Empty when the result says
// nothing beyond the summary (so no empty second line is drawn).
std::string DetailLine(const UltraNetResult& result);
std::string DetailLine(const UltraDbResult& result);

// True when retrying the same operation could plausibly succeed (a transient
// network condition rather than a wrong password or a missing plug-in).
bool IsRetryable(const UltraNetResult& result);

// ===== ALERTS =====
// All are non-blocking and titled "UltraMail". `detail` is optional and is
// drawn as the alert's secondary line.

void AlertError(UltraCanvas::UltraCanvasWindowBase* parent,
                const std::string& summary, const std::string& detail = "");

void AlertWarning(UltraCanvas::UltraCanvasWindowBase* parent,
                  const std::string& summary, const std::string& detail = "");

void AlertSuccess(UltraCanvas::UltraCanvasWindowBase* parent,
                  const std::string& summary, const std::string& detail = "");

// An error alert offering Retry / Cancel. `onRetry` runs only when the user
// chooses Retry.
void AlertErrorRetry(UltraCanvas::UltraCanvasWindowBase* parent,
                     const std::string& summary, const std::string& detail,
                     std::function<void()> onRetry);

} // namespace UltraMail
