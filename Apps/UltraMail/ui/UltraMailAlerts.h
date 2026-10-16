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
// Version: 0.3.0 - the result-to-text helpers moved to the engine, naming the server
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasAlert.h"
#include "UltraMailResultText.h"

#include <UltraDatabase/UltraDatabaseCore.h>
#include <UltraNet/UltraNetCore.h>

#include <functional>
#include <string>

namespace UltraMail {

// ===== RESULT -> TEXT =====
// FriendlyMessage, DetailLine and IsRetryable live in the engine
// (engine/UltraMailResultText.h, included here) so the tests read them: the
// sentence for a result code, naming what the operation talked to - the mail
// server unless the caller says otherwise.

// An alert's details for a failure that carries a connection chain
// (UltraNetResult::diagnostics, SyncOutcome::diagnostics): the reason, then
// the chain - component, server, TLS, sign-in, library versions, trusted
// roots, system - as a code block, one line each, so it can be read and
// copied into a bug report. Just `detail` when there is no chain.
std::string WithDiagnostics(const std::string& detail, const std::string& diagnostics);

// ===== ALERTS =====
// All are non-blocking and titled "UltraMail". `detail` is optional and is
// drawn as the alert's secondary line.

// `onDismissed`, when given, runs after the user closes the alert — so a flow
// can show one thing at a time instead of stacking a second dialog on top.
void AlertError(UltraCanvas::UltraCanvasWindowBase* parent,
                const std::string& summary, const std::string& detail = "",
                std::function<void()> onDismissed = nullptr);

void AlertWarning(UltraCanvas::UltraCanvasWindowBase* parent,
                  const std::string& summary, const std::string& detail = "",
                  std::function<void()> onDismissed = nullptr);

void AlertSuccess(UltraCanvas::UltraCanvasWindowBase* parent,
                  const std::string& summary, const std::string& detail = "",
                  std::function<void()> onDismissed = nullptr);

// An error alert offering Retry / Cancel. `onRetry` runs only when the user
// chooses Retry.
void AlertErrorRetry(UltraCanvas::UltraCanvasWindowBase* parent,
                     const std::string& summary, const std::string& detail,
                     std::function<void()> onRetry);

// A warning alert offering Retry / Cancel - for something that did not work
// yet but is not lost (a message waiting in the outbox and in Drafts).
// `onRetry` runs only when the user chooses Retry.
void AlertWarningRetry(UltraCanvas::UltraCanvasWindowBase* parent,
                       const std::string& summary, const std::string& detail,
                       std::function<void()> onRetry);

} // namespace UltraMail
