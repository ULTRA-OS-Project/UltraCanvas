// Tests/UltraCloud/test_log.cpp
// The cloud providers' session log (UltraCloudLog.h) and what a refusal says.
//
// Before it, a cloud drive's failure read "list /Photos: HTTP 409" and
// nothing else: the service's own reason - which file, which limit, which
// sign-in - was in the response body and thrown away, a throttled request was
// a bare "HTTP 429", an access token renewed (or refused) left no trace, and
// a caller watching the work had no lines to watch. Checked against fake HTTP
// functions, so nothing is contacted - and against a recorded wait, so a
// throttled request is sent again without the test sleeping.
#include "test_framework.h"

#include <UltraCloud/UltraCloudAccounts.h>
#include <UltraCloud/UltraCloudDropbox.h>
#include <UltraCloud/UltraCloudGoogleDrive.h>
#include <UltraCloud/UltraCloudLog.h>
#include <UltraCloud/UltraCloudNextcloud.h>
#include <UltraCloud/UltraCloudOAuth.h>
#include <UltraCloud/UltraCloudOneDrive.h>
#include <UltraCloud/UltraCloudSecrets.h>
#include <UltraCloud/UltraCloudService.h>
#include <UltraCloud/UltraCloudWebDav.h>

#include <memory>
#include <string>
#include <vector>

using namespace UltraCloud;

namespace {
UltraNetResult Ok() { UltraNetResult r; r.success = true; return r; }
void Answer(UltraNetResponse& resp, int status, const std::string& body,
            const std::string& message = "") {
    resp.statusCode = status;
    resp.statusMessage = message;
    resp.body.assign(body.begin(), body.end());
}

// Collects the calling thread's log for as long as it lives.
struct CollectedLog {
    std::vector<LogLine> lines;
    LogCallback previous;
    CollectedLog() {
        previous = SetThreadLog([this](const LogLine& l) { lines.push_back(l); });
    }
    ~CollectedLog() { SetThreadLog(std::move(previous)); }
    bool Has(LogKind kind, const std::string& prefix) const {
        for (const LogLine& l : lines)
            if (l.kind == kind && l.text.rfind(prefix, 0) == 0) return true;
        return false;
    }
    bool Mentions(const std::string& needle) const {
        for (const LogLine& l : lines)
            if (l.text.find(needle) != std::string::npos) return true;
        return false;
    }
};

// The calling thread's rate-limit policy for as long as it lives, with a
// wait that only writes down how long it was asked to wait - a test never
// sleeps through a Retry-After.
struct RecordedWaits {
    std::vector<long long> seconds;
    RateLimitPolicy previous;
    explicit RecordedWaits(int maxRetries = 3, long long maxWaitSeconds = 30,
                           bool keepWaiting = true) {
        RateLimitPolicy policy;
        policy.maxRetries = maxRetries;
        policy.maxWaitSeconds = maxWaitSeconds;
        policy.wait = [this, keepWaiting](long long s) {
            seconds.push_back(s);
            return keepWaiting;
        };
        previous = SetThreadRateLimitPolicy(std::move(policy));
    }
    ~RecordedWaits() { SetThreadRateLimitPolicy(std::move(previous)); }
};
} // namespace

// ===== The pure helpers =====

TEST(log_url_keeps_secrets_out) {
    // A OneDrive upload session URL carries its own credential.
    REQUIRE_EQ(LoggableUrl("https://my.sharepoint.com/upload?guid=g1&tempauth=eyJ0eXAi&x=1"),
               std::string("https://my.sharepoint.com/upload?guid=g1&tempauth=***&x=1"));
    REQUIRE_EQ(LoggableUrl("https://www.googleapis.com/drive/v3/files?q=x&key=AIza&pageToken=p2"),
               std::string("https://www.googleapis.com/drive/v3/files?q=x&key=***&pageToken=***"));
    REQUIRE_EQ(LoggableUrl("https://erika:hunter2@cloud.example.com/remote.php/dav/files/erika/"),
               std::string("https://cloud.example.com/remote.php/dav/files/erika/"));
    REQUIRE_EQ(LoggableUrl("https://api.dropboxapi.com/2/files/list_folder"),
               std::string("https://api.dropboxapi.com/2/files/list_folder"));
}

TEST(log_reads_every_services_error_body) {
    // Dropbox
    REQUIRE_EQ(ServerErrorReason(R"({"error_summary": "path/not_found/..", "error": {".tag": "path"}})"),
               std::string("path/not_found"));
    // Microsoft Graph (OneDrive)
    REQUIRE_EQ(ServerErrorReason(R"({"error": {"code": "itemNotFound", "message": "The resource could not be found."}})"),
               std::string("itemNotFound: The resource could not be found."));
    // Google Drive: the machine reason, not the numeric code.
    REQUIRE_EQ(ServerErrorReason(R"({"error": {"code": 403, "message": "The user's Drive storage quota has been exceeded.", "errors": [{"reason": "storageQuotaExceeded"}]}})"),
               std::string("storageQuotaExceeded: The user's Drive storage quota has been exceeded."));
    // An OAuth token endpoint
    REQUIRE_EQ(ServerErrorReason(R"({"error": "invalid_grant", "error_description": "Token has been expired or revoked."})"),
               std::string("invalid_grant: Token has been expired or revoked."));
    // Nextcloud / SabreDAV, entities decoded
    REQUIRE_EQ(ServerErrorReason(
                   "<?xml version=\"1.0\"?>\n<d:error xmlns:d=\"DAV:\" xmlns:s=\"http://sabredav.org/ns\">\n"
                   "  <s:exception>Sabre\\DAV\\Exception\\NotFound</s:exception>\n"
                   "  <s:message>File with name &quot;Photos&quot; could not be located</s:message>\n"
                   "</d:error>"),
               std::string("File with name \"Photos\" could not be located"));
    // Short plain text is the reason; a page is not.
    REQUIRE_EQ(ServerErrorReason("Unauthorized\r\n"), std::string("Unauthorized"));
    REQUIRE_EQ(ServerErrorReason("<html><body>" + std::string(500, 'x') + "</body></html>"),
               std::string(""));
    REQUIRE_EQ(ServerErrorReason(""), std::string(""));
    REQUIRE_EQ(ServerErrorReason("{not json"), std::string(""));
}

TEST(log_reads_retry_after) {
    REQUIRE_EQ(RetryAfterSeconds("30", 0), 30LL);
    REQUIRE_EQ(RetryAfterSeconds(" 120 ", 0), 120LL);
    // Sun, 06 Nov 1994 08:49:37 GMT is 784111777.
    REQUIRE_EQ(RetryAfterSeconds("Sun, 06 Nov 1994 08:49:37 GMT", 784111777LL - 45), 45LL);
    REQUIRE_EQ(RetryAfterSeconds("Sun, 06 Nov 1994 08:49:37 GMT", 784111777LL + 10), 0LL);
    REQUIRE_EQ(RetryAfterSeconds("soon", 0), -1LL);
    REQUIRE_EQ(RetryAfterSeconds("", 0), -1LL);
}

// ===== A refusal says why =====

TEST(log_a_refusal_keeps_the_services_reason) {
    HttpFn fake = [](const UltraNetHttpRequest&, UltraNetResponse& resp) {
        Answer(resp, 409, R"({"error_summary": "path/not_found/...", "error": {}})", "Conflict");
        resp.headers.Set("X-Dropbox-Request-Id", "req-7f3a");
        UltraNetResult r; r.success = false; r.message = "HTTP 409"; return r;
    };
    DropboxProvider dropbox(fake);
    Account a; Credentials c; c.token = "secret-token";
    std::vector<Entry> entries;
    const Result r = dropbox.List(a, c, "/Photos", entries);
    REQUIRE(!r);
    REQUIRE(r.code == ResultCode::Server);
    REQUIRE_EQ(r.message, std::string("list /Photos: HTTP 409 - path/not_found"));
    REQUIRE(r.diagnostics.find("Request ID: req-7f3a") != std::string::npos);
    REQUIRE(r.diagnostics.find("HTTP status: 409 Conflict") != std::string::npos);
}

TEST(log_a_throttled_request_says_how_long_to_wait) {
    HttpFn fake = [](const UltraNetHttpRequest&, UltraNetResponse& resp) {
        Answer(resp, 429, R"({"error": {"code": "activityLimitReached", "message": "The app or user has been throttled."}})",
               "Too Many Requests");
        resp.headers.Set("Retry-After", "30");
        UltraNetResult r; r.success = false; r.message = "HTTP 429"; return r;
    };
    OneDriveProvider onedrive(fake);
    Account a; Credentials c; c.token = "t";
    std::vector<Entry> entries;
    RecordedWaits waits(/*maxRetries=*/0);   // the first refusal, as it is
    CollectedLog log;
    const Result r = onedrive.List(a, c, "/", entries);
    REQUIRE(r.code == ResultCode::RateLimited);
    REQUIRE(r.message.find("the service is limiting requests - it asks to wait 30 s") !=
            std::string::npos);
    REQUIRE(r.message.find("activityLimitReached") != std::string::npos);
    REQUIRE(r.diagnostics.find("Retry-After: 30") != std::string::npos);
    REQUIRE(log.Has(LogKind::Step, "The service is limiting requests: it asks to wait 30 s"));
    REQUIRE(waits.seconds.empty());
}

TEST(log_google_rate_limit_403_is_throttling_not_a_refusal) {
    int calls = 0;
    HttpFn fake = [&calls](const UltraNetHttpRequest&, UltraNetResponse& resp) {
        ++calls;
        Answer(resp, 403, R"({"error": {"code": 403, "message": "Rate Limit Exceeded", "errors": [{"reason": "userRateLimitExceeded"}]}})");
        UltraNetResult r; r.success = false; r.message = "HTTP 403"; return r;
    };
    GoogleDriveProvider google(fake);
    Account a; Credentials c; c.token = "t";
    std::vector<Entry> entries;
    RecordedWaits waits;
    CollectedLog log;
    const Result r = google.List(a, c, "/", entries);
    REQUIRE(r.code == ResultCode::RateLimited);
    // No Retry-After: 1, 2 and 4 s between the four tries, then it gives up.
    REQUIRE_EQ(calls, 4);
    REQUIRE(waits.seconds == std::vector<long long>({1, 2, 4}));
    REQUIRE(log.Mentions("it gives no time to wait - trying again in 1 s (try 2 of 4)"));
    REQUIRE(log.Mentions("not trying again after 4 tries"));
}

// ===== Waiting and trying again =====

TEST(log_a_throttled_request_is_sent_again_after_the_wait) {
    int calls = 0;
    HttpFn fake = [&calls](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        ++calls;
        REQUIRE(req.url.find("/me/drive/root/children") != std::string::npos);
        if (calls == 1) {
            Answer(resp, 429, R"({"error": {"code": "activityLimitReached", "message": "Throttled."}})",
                   "Too Many Requests");
            resp.headers.Set("Retry-After", "2");
            UltraNetResult r; r.success = false; r.message = "HTTP 429"; return r;
        }
        Answer(resp, 200, R"({"value": [{"name": "a.txt", "size": 3, "file": {}}]})", "OK");
        return Ok();
    };
    OneDriveProvider onedrive(fake);
    Account a; Credentials c; c.token = "t";
    std::vector<Entry> entries;
    RecordedWaits waits;
    CollectedLog log;
    const Result r = onedrive.List(a, c, "/", entries);
    REQUIRE(static_cast<bool>(r));
    REQUIRE_EQ(calls, 2);
    REQUIRE_EQ(entries.size(), static_cast<std::size_t>(1));
    REQUIRE(waits.seconds == std::vector<long long>({2}));
    REQUIRE(log.Has(LogKind::Step, "The service is limiting requests: it asks to wait 2 s - "
                                   "trying again in 2 s (try 2 of 4)"));
    // The same request twice in the log, the second one answered.
    int requests = 0;
    for (const LogLine& l : log.lines)
        if (l.kind == LogKind::Request) ++requests;
    REQUIRE_EQ(requests, 2);
    REQUIRE(log.Has(LogKind::Response, "200 OK"));
}

TEST(log_a_wait_longer_than_the_policy_allows_is_not_waited_out) {
    int calls = 0;
    HttpFn fake = [&calls](const UltraNetHttpRequest&, UltraNetResponse& resp) {
        ++calls;
        Answer(resp, 429, R"({"error_summary": "too_many_requests/.."})", "Too Many Requests");
        resp.headers.Set("Retry-After", "300");
        UltraNetResult r; r.success = false; r.message = "HTTP 429"; return r;
    };
    DropboxProvider dropbox(fake);
    Account a; Credentials c; c.token = "t";
    std::vector<Entry> entries;
    RecordedWaits waits;
    CollectedLog log;
    const Result r = dropbox.List(a, c, "/", entries);
    REQUIRE(r.code == ResultCode::RateLimited);
    REQUIRE_EQ(calls, 1);
    REQUIRE(waits.seconds.empty());
    REQUIRE(r.message.find("it asks to wait 300 s") != std::string::npos);
    REQUIRE(log.Mentions("longer than a request waits (30 s), so not trying again"));
}

TEST(log_a_wait_that_is_interrupted_gives_the_request_up) {
    int calls = 0;
    HttpFn fake = [&calls](const UltraNetHttpRequest&, UltraNetResponse& resp) {
        ++calls;
        Answer(resp, 503, "Service Unavailable", "Service Unavailable");
        resp.headers.Set("Retry-After", "5");
        UltraNetResult r; r.success = false; r.message = "HTTP 503"; return r;
    };
    WebDavProvider dav(fake);
    Account a; a.serverUrl = "https://dav.example.org/files"; a.username = "erika";
    Credentials c; c.username = "erika"; c.password = "pw";
    std::vector<Entry> entries;
    RecordedWaits waits(3, 30, /*keepWaiting=*/false);   // the worker is stopping
    CollectedLog log;
    const Result r = dav.List(a, c, "/", entries);
    REQUIRE(r.code == ResultCode::RateLimited);
    REQUIRE_EQ(calls, 1);
    REQUIRE(waits.seconds == std::vector<long long>({5}));
    REQUIRE(log.Has(LogKind::Step, "Stopped waiting"));
}

TEST(log_a_sign_in_refusal_names_itself) {
    HttpFn fake = [](const UltraNetHttpRequest&, UltraNetResponse& resp) {
        Answer(resp, 401, R"({"error_summary": "expired_access_token/", "error": {}})");
        UltraNetResult r; r.success = false; r.message = "HTTP 401"; return r;
    };
    DropboxProvider dropbox(fake);
    Account a; Credentials c; c.token = "t";
    std::vector<Entry> entries;
    const Result r = dropbox.List(a, c, "/", entries);
    REQUIRE(r.code == ResultCode::AuthFailed);
    REQUIRE_EQ(r.message, std::string("list /: sign-in rejected (HTTP 401 - expired_access_token)"));
}

TEST(log_a_request_with_no_answer_keeps_the_transports_diagnostics) {
    HttpFn fake = [](const UltraNetHttpRequest&, UltraNetResponse& resp) {
        resp.elapsedTime = 10.0;
        UltraNetResult r; r.success = false; r.message = "Couldn't connect to server";
        r.diagnostics = "Error: Couldn't connect to server (libcurl error 7)\nConnected to: no connection was made\n";
        return r;
    };
    NextcloudProvider nextcloud(fake);
    Account a; a.serverUrl = "https://cloud.example.com"; a.username = "erika";
    Credentials c; c.username = "erika"; c.password = "pw";
    std::vector<Entry> entries;
    CollectedLog log;
    const Result r = nextcloud.List(a, c, "/", entries);
    REQUIRE(r.code == ResultCode::Network);
    REQUIRE(r.diagnostics.find("libcurl error 7") != std::string::npos);
    REQUIRE(log.Has(LogKind::Request, "PROPFIND https://cloud.example.com/"));
    REQUIRE(log.Has(LogKind::Error, "No answer: Couldn't connect to server (after 10.00 s)"));
}

// ===== The lines a watcher sees =====

TEST(log_requests_answers_and_pages_without_the_token) {
    HttpFn fake = [](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        resp.elapsedTime = 0.42;
        if (req.url.find("list_folder/continue") != std::string::npos)
            Answer(resp, 200, R"({"entries":[{".tag":"file","name":"b.txt","path_display":"/Docs/b.txt"}],"has_more":false})", "OK");
        else
            Answer(resp, 200, R"({"entries":[{".tag":"file","name":"a.txt","path_display":"/Docs/a.txt"}],"cursor":"c1","has_more":true})", "OK");
        return Ok();
    };
    DropboxProvider dropbox(fake);
    Account a; Credentials c; c.token = "very-secret-token";
    std::vector<Entry> entries;
    CollectedLog log;
    REQUIRE(dropbox.List(a, c, "/Docs", entries));

    REQUIRE_EQ(log.lines.size(), static_cast<std::size_t>(5));
    REQUIRE(log.lines[0].kind == LogKind::Request);
    REQUIRE_EQ(log.lines[0].text, std::string("POST https://api.dropboxapi.com/2/files/list_folder"));
    REQUIRE(log.lines[1].kind == LogKind::Response);
    REQUIRE_EQ(log.lines[1].text, std::string("200 OK - 420 ms"));
    REQUIRE_EQ(log.lines[1].httpStatus, 200);
    REQUIRE_EQ(log.lines[2].text, std::string("The listing continues - page 2 (1 entries so far)"));
    REQUIRE_EQ(log.lines[3].text, std::string("POST https://api.dropboxapi.com/2/files/list_folder/continue"));
    REQUIRE(!log.Mentions("very-secret-token"));
    REQUIRE(!log.Mentions("Bearer"));
}

TEST(log_nothing_is_built_without_a_sink) {
    REQUIRE(!ThreadLogActive());
    HttpFn fake = [](const UltraNetHttpRequest&, UltraNetResponse& resp) {
        Answer(resp, 200, R"({"entries":[],"has_more":false})");
        return Ok();
    };
    DropboxProvider dropbox(fake);
    Account a; Credentials c; c.token = "t";
    std::vector<Entry> entries;
    REQUIRE(dropbox.List(a, c, "/", entries));   // and no sink was called: there is none
}

TEST(log_an_access_token_renewal_is_said) {
    OAuthApp app; app.clientId = "id";
    SetOAuthApp("onedrive", app);
    bool refuse = false;
    OAuthHooks hooks;
    hooks.refresh = [&refuse](const UltraNetOAuth2Config&, const std::string&,
                              UltraNetOAuth2Token& out) {
        if (refuse) {
            UltraNetResult r; r.success = false; r.message = "invalid_grant"; return r;
        }
        out.accessToken = "fresh"; out.expiresInSeconds = 3600;
        return Ok();
    };
    HttpFn fake = [](const UltraNetHttpRequest&, UltraNetResponse& resp) {
        Answer(resp, 200, R"({"value":[]})");
        return Ok();
    };
    RegisterProvider(std::make_shared<OneDriveProvider>(fake, hooks));

    AccountStore accounts;
    REQUIRE(accounts.Open("uctest-log-renewal", ":memory:"));
    MemorySecretStore secrets;
    CloudService service(accounts, secrets);
    Account a; a.providerId = "onedrive"; a.username = "erika@outlook.com";
    Credentials stale; stale.token = "stale"; stale.refreshToken = "r"; stale.tokenExpiresAt = 1;
    REQUIRE(service.AddAccount(a, stale, /*verify=*/false));

    {
        CollectedLog log;
        std::vector<Entry> entries;
        REQUIRE(service.List(a.accountId, "/", entries));
        REQUIRE(log.Has(LogKind::Step, "The access token has expired - renewing it"));
        REQUIRE(log.Has(LogKind::Step, "Access token renewed"));
        REQUIRE(!log.Mentions("fresh"));
    }

    // A renewal the service refuses is said too - the usual reason a cloud
    // drive that worked yesterday does not today.
    Credentials expiredAgain; expiredAgain.token = "fresh"; expiredAgain.refreshToken = "r";
    expiredAgain.tokenExpiresAt = 1;
    REQUIRE(secrets.Store(a.accountId, expiredAgain));
    refuse = true;
    CollectedLog log;
    std::vector<Entry> entries;
    REQUIRE(!service.List(a.accountId, "/", entries));
    REQUIRE(log.Has(LogKind::Step, "Renewing the access token failed: token refresh failed: invalid_grant"));
}

// ===== Signing in through the browser =====

TEST(log_a_browser_sign_in_says_its_steps_and_keeps_its_state_out) {
    OAuthApp app; app.clientId = "id";
    SetOAuthApp("dropbox", app);
    bool refuse = false;
    OAuthHooks hooks;
    hooks.authorize = [&refuse](const UltraNetOAuth2Config&,
                                const std::function<void(const std::string&)>& openUrl,
                                UltraNetOAuth2Token& out) {
        openUrl("https://www.dropbox.com/oauth2/authorize?client_id=id&state=st-4a1b"
                "&code_challenge=cc-77");
        if (refuse) {
            UltraNetResult r; r.success = false; r.message = "access_denied"; return r;
        }
        out.accessToken = "AT-9f8e"; out.refreshToken = "RT-1c2d"; out.expiresInSeconds = 14400;
        return Ok();
    };
    DropboxProvider dropbox(nullptr, hooks);
    Account a; a.providerId = "dropbox";
    std::string opened;
    auto openUrl = [&opened](const std::string& url) { opened = url; };
    {
        CollectedLog log;
        Credentials creds;
        REQUIRE(static_cast<bool>(dropbox.SignIn(a, openUrl, creds)));
        // The browser gets the whole address; the log only where it points.
        REQUIRE(opened.find("state=st-4a1b") != std::string::npos);
        REQUIRE(log.Has(LogKind::Step, "Signing in to Dropbox through the browser"));
        REQUIRE(log.Has(LogKind::Step, "Opening the sign-in page in the browser: "
                                       "https://www.dropbox.com/oauth2/authorize"));
        REQUIRE(log.Has(LogKind::Step, "Signed in - Dropbox gave an access token"));
        REQUIRE(!log.Mentions("st-4a1b"));
        REQUIRE(!log.Mentions("cc-77"));
        REQUIRE(!log.Mentions("AT-9f8e"));
        REQUIRE(!log.Mentions("RT-1c2d"));
    }
    refuse = true;
    CollectedLog log;
    Credentials creds;
    const Result r = dropbox.SignIn(a, openUrl, creds);
    REQUIRE(r.code == ResultCode::AuthFailed);
    REQUIRE(log.Has(LogKind::Step, "The browser sign-in did not complete: access_denied"));
}
