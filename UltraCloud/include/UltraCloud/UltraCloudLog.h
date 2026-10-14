// UltraCloud/include/UltraCloud/UltraCloudLog.h
// The session log of the cloud providers - Dropbox, OneDrive, Google Drive,
// WebDAV, Nextcloud - the way an FTP drive's connection log reads:
//
//   Step:      The access token has expired - renewing it
//   Step:      Access token renewed
//   Request:   POST https://api.dropboxapi.com/2/files/list_folder
//   Response:  200 OK - 0.42 s
//   Step:      The listing continues - page 2
//   Request:   POST https://api.dropboxapi.com/2/files/list_folder/continue
//   Response:  429 Too Many Requests - too_many_requests (0.08 s)
//   Step:      The service is limiting requests: it asks to wait 30 s
//
// Every provider request goes through HttpProviderBase::Send, which writes
// the Request and Response lines; CloudService says when it renews an access
// token, and the providers that page through a folder say which page. A
// caller that wants the lines - UltraFiler's drive worker, which shows them
// in its connection log - sets a sink for its thread around the calls it
// makes (SetThreadLog), the way UltraNet_SetThreadFtpLog serves the FTP
// provider. With no sink set nothing is formatted.
//
// What is never logged: an access token, a password, or the value of a query
// parameter that carries one (an upload session URL's "tempauth", a "key", a
// "code"): LoggableUrl masks them. Request and response bodies are not log
// lines either - only the server's own reason when it refuses
// (ServerErrorReason).
// Version: 1.0.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <functional>
#include <string>

namespace UltraCloud {

// What one line of the log is. (Not "Status": Xlib #defines that name.)
enum class LogKind {
    Step,       // what is happening: a token renewed, a page of a listing,
                // the service asking to wait
    Request,    // a request sent: the method and the URL
    Response,   // the service's answer: status, its reason, how long it took
    Error       // a request that got no answer at all
};

struct LogLine {
    LogKind kind = LogKind::Step;
    std::string text;
    // Response lines: the HTTP status (200, 404, 429, ...); 0 otherwise.
    int httpStatus = 0;
};

using LogCallback = std::function<void(const LogLine&)>;

// Sets the log sink for cloud calls made on the CALLING THREAD and returns the
// previous one, so a caller can put it back. Per thread, so two workers never
// read each other's sessions. An empty callback clears it.
LogCallback SetThreadLog(LogCallback sink);

// Whether the calling thread has a sink: a line that costs something to
// build is only built when someone will read it.
bool ThreadLogActive();

// One line to the calling thread's sink; nothing when there is none.
void LogToThread(LogKind kind, const std::string& text, int httpStatus = 0);

// `url` as it may be logged: user name and password removed, and the value of
// every query parameter whose name says it carries a secret (token, auth,
// key, secret, signature, code, password, session) replaced by "***".
std::string LoggableUrl(const std::string& url);

// The service's own reason for refusing a request, from the body of its
// error response; "" when the body holds none. Reads the error formats of
// every provider in the module:
//   Dropbox        {"error_summary": "path/not_found/..", ...}
//   OneDrive       {"error": {"code": "itemNotFound", "message": "..."}}
//   Google Drive   {"error": {"message": "...", "errors": [{"reason": "..."}]}}
//   OAuth          {"error": "invalid_grant", "error_description": "..."}
//   WebDAV         <d:error> ... <s:message>...</s:message></d:error>
// and a short plain-text body as it is. At most a few hundred characters.
std::string ServerErrorReason(const std::string& body);

// The wait a Retry-After header asks for, in seconds - its delta-seconds
// form, or an HTTP date counted from `nowEpoch`; -1 when there is none or it
// cannot be read.
long long RetryAfterSeconds(const std::string& value, long long nowEpoch);

} // namespace UltraCloud
