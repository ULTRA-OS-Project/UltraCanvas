// Apps/UltraMail/engine/UltraMailResultText.h
// A failed network operation, in words the user can act on: the sentence an
// alert leads with, chosen from the result code, and the raw diagnostic for
// its details line. Plain C++ over UltraNetResult, no UI - the alert layer
// (ui/UltraMailAlerts.h) shows what this produces, and the tests read it.
//
// Every sentence names what the operation was talking to, and the caller
// says what that was: FriendlyMessage(r) means the mail server (IMAP, SMTP -
// "The mail server could not be found"), and a caller that talked to
// something else names it - "the sign-in service", "the sender's web server",
// "the cloud service" - so a failure is never worded as an IMAP problem when
// no mail server was involved. The account-specific advice (the password, the
// server address and port on the settings page) goes with the mail server
// only; another server gets the plain fact.
// Version: 1.0.0 - moved out of ui/UltraMailAlerts.cpp, with the server named
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraDatabase/UltraDatabaseCore.h>
#include <UltraNet/UltraNetCore.h>

#include <string>

namespace UltraMail {

// What FriendlyMessage talks about when the caller names nothing: the
// account's IMAP or SMTP server.
inline constexpr const char* kMailServer = "the mail server";

// A short, actionable sentence for a failed network operation against
// `server` (lower case, with its article: "the mail server", "the sign-in
// service"), chosen from the result code. Falls back to the result's own
// message when the code carries no specific advice.
std::string FriendlyMessage(const UltraNetResult& result,
                            const std::string& server = kMailServer);

// The raw diagnostic for the alert's details line. Empty when the result says
// nothing beyond the summary FriendlyMessage gives for the same `server` (so
// no empty or repeated second line is drawn).
std::string DetailLine(const UltraNetResult& result,
                       const std::string& server = kMailServer);
std::string DetailLine(const UltraDbResult& result);

// True when retrying the same operation could plausibly succeed (a transient
// network condition rather than a wrong password or a missing plug-in).
bool IsRetryable(const UltraNetResult& result);

} // namespace UltraMail
