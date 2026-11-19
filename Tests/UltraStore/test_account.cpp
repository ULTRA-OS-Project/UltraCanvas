// Tests/UltraStore/test_account.cpp
// The store's side of the account service, against a call that answers
// from a map: the sign-up and the code, the sign-in with the bearer token
// on every request afterwards and the second factor asked for, the
// profile, the devices and the licences parsed from the service's answers,
// the session file round trip and its permissions, and the licences kept
// in step - this machine's attached with a seat, the account's fetched
// into the registry once verified.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "fixtures.h"
#include "helpers.h"

#include "Account.h"
#include "Registry.h"
#include "Verifier.h"

#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <map>
#include <random>

using namespace UltraStore;
using namespace ultrastore_test;

namespace {

struct FakeAccountService {
    std::map<std::string, std::string> answers;   // "METHOD path" -> body
    std::vector<std::string> calls;                // "METHOD path | authorization | body"
    int status = 200;
    AccountCall Call() {
        return [this](const std::string& method, const std::string& url, const std::string& authorization, const std::string& body) {
            const size_t at = url.find("/v1/");
            const std::string path = at == std::string::npos ? url.substr(url.find("/healthz")) : url.substr(at);
            calls.push_back(method + " " + path + " | " + authorization + " | " + body);
            HttpReply r;
            auto it = answers.find(method + " " + path);
            if (it == answers.end()) { r.status = 404; r.body = R"({"error":"no_such","error_description":"No such thing"})"; return r; }
            r.status = status;
            r.body = it->second;
            return r;
        };
    }
};

std::string B64Url(const std::string& bytes) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    uint32_t buffer = 0;
    int bits = 0;
    for (unsigned char c : bytes) {
        buffer = (buffer << 8) | c;
        bits += 8;
        while (bits >= 6) { bits -= 6; out += alphabet[(buffer >> bits) & 63]; }
    }
    if (bits > 0) out += alphabet[(buffer << (6 - bits)) & 63];
    return out;
}

std::string MakeLicence(const std::vector<uint8_t>& seed, const std::string& licenceId, const std::string& app) {
    const std::string payload = R"({"app":")" + app + R"(","issued":"2026-11-19","licence":")" + licenceId + R"(","order":"o-1","updates":"always","v":1})";
    const auto sig = SignCatalog(payload, seed);
    return "ULIC1." + B64Url(payload) + "." + B64Url(std::string(sig.Value().begin(), sig.Value().end()));
}

const char* kUser = R"({"id":"u-1","email":"anna@example.org","email_verified":true,"display_name":"Anna K.","handle":"annak","language":"de","country":"DE",
    "year_of_birth":1990,"about":"Edits video.","state":"active","created_at":"2026-11-01T10:00:00Z","password_changed_at":"2026-11-16","two_factor":true,
    "recovery_codes_left":7,"settings":{"notifications":{"security":"email","deals":"off"},"history_on_account":false,"public_profile":true,"marketing":false},
    "deletion_requested_at":"","delete_after":""})";

std::string TempDir() {
    std::mt19937_64 rng{std::random_device{}()};
    const auto dir = std::filesystem::temp_directory_path() / ("ultrastore-account-" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    return UltraCanvas::PathToUtf8(dir);
}

} // namespace

TEST(signup_verify_and_signin_carry_the_device_and_keep_the_token) {
    FakeAccountService s;
    s.answers["POST /v1/signup"] = R"({"user":{"id":"u-1","email":"anna@example.org"},"verify":"email_code"})";
    s.answers["POST /v1/signup/verify"] = std::string(R"({"token":"us_abc","device":{"id":"d-1","device_name":"Anna's laptop","os":"linux","client":"UltraStore 0.2","current":true},"user":)") + kUser + "}";
    const AccountClient client("https://account.example/", s.Call());
    DeviceInfo device{"Anna's laptop", "linux", "UltraStore 0.2"};
    REQUIRE(client.SignUp("anna@example.org", "correct horse battery staple", "DE", 1990, "de", false));
    REQUIRE(s.calls[0].find("POST /v1/signup |  | {") == 0);   // no bearer before signing in
    REQUIRE(s.calls[0].find("\"country\":\"DE\"") != std::string::npos);
    const auto signedIn = client.VerifySignUp("anna@example.org", "123456", device);
    REQUIRE(signedIn);
    REQUIRE_EQ(signedIn.Value().token, "us_abc");
    REQUIRE_EQ(signedIn.Value().device.id, "d-1");
    REQUIRE_EQ(signedIn.Value().profile.handle, "annak");
    REQUIRE(signedIn.Value().profile.twoFactor);
    REQUIRE_EQ(signedIn.Value().profile.Initials(), "AK");
    REQUIRE(s.calls[1].find("\"device\":{") != std::string::npos && s.calls[1].find("Anna's laptop") != std::string::npos);

    // The second factor asked for: the service's error code is the detail.
    s.status = 401;
    s.answers["POST /v1/signin"] = R"({"error":"two_factor_required","error_description":"The six-digit code from your authenticator app"})";
    const auto refused = client.SignIn("anna@example.org", "correct horse battery staple", device);
    REQUIRE(!refused);
    REQUIRE_EQ(refused.error.detail, "two_factor_required");
    REQUIRE(refused.error.code == ErrorCode::SignatureInvalid);
    s.status = 200;
    s.answers["POST /v1/signin"] = s.answers["POST /v1/signup/verify"];
    const auto ok = client.SignIn("anna@example.org", "correct horse battery staple", device, "654321");
    REQUIRE(ok);
    REQUIRE(s.calls.back().find("\"totp\":\"654321\"") != std::string::npos);

    // Every request afterwards carries the bearer token.
    s.answers["GET /v1/me"] = std::string("{") + std::string(kUser).substr(1, std::string(kUser).size() - 2) +
        R"(,"device":{"id":"d-1","device_name":"Anna's laptop","current":true},"devices":[{"id":"d-1","device_name":"Anna's laptop","current":true},{"id":"d-2","device_name":"Anna's phone","os":"Android 15","last_seen":"2026-11-19T08:00:00Z"}],)"
        R"("licences":[{"id":"lic-1","app":"org.example.app","order":"o-1","issued":"2026-11-19","updates":"always","seats":3,"seats_used":1,"licence":"ULIC1.x.y","installs":[{"session":"d-1","removed_at":""},{"session":"d-9","removed_at":"2026-11-10"}]}],)"
        R"("unread_notices":2,"archive":{"id":"a-1","state":"built","due":"2026-11-20"}})";
    const auto me = client.GetMe("us_abc");
    REQUIRE(me);
    REQUIRE(s.calls.back().find("GET /v1/me | Bearer us_abc |") == 0);
    REQUIRE_EQ(me.Value().devices.size(), size_t(2));
    REQUIRE(me.Value().devices[0].current && !me.Value().devices[1].current);
    REQUIRE_EQ(me.Value().licences.size(), size_t(1));
    REQUIRE(me.Value().licences[0].InstalledOn("d-1") && !me.Value().licences[0].InstalledOn("d-9"));
    REQUIRE_EQ(me.Value().licences[0].seats, 3);
    REQUIRE_EQ(me.Value().unreadNotices, 2);
    REQUIRE(me.Value().archive && me.Value().archive->state == "built");
    REQUIRE_EQ(me.Value().profile.settings.notifications.at("deals"), "off");

    // A signed-out token: the error the window forgets the session on.
    s.status = 401;
    s.answers["GET /v1/me"] = R"({"error":"signed_out","error_description":"This machine is signed out: sign in again"})";
    const auto gone = client.GetMe("us_old");
    REQUIRE(!gone && gone.error.detail == "signed_out");
}

TEST(the_profile_the_devices_and_two_factor_send_what_the_service_reads) {
    FakeAccountService s;
    const std::string meBody = std::string("{") + std::string(kUser).substr(1, std::string(kUser).size() - 2) + R"(,"device":{"id":"d-1","current":true},"devices":[],"licences":[],"unread_notices":0})";
    s.answers["PUT /v1/me"] = meBody;
    s.answers["PUT /v1/me/settings"] = meBody;
    s.answers["POST /v1/me/password"] = R"({"changed":true,"signed_out":2})";
    s.answers["POST /v1/me/two-factor/setup"] = R"({"secret":"JBSWY3DPEHPK3PXP","otpauth_url":"otpauth://totp/UltraStore:anna?secret=JBSWY3DPEHPK3PXP"})";
    s.answers["POST /v1/me/two-factor/enable"] = R"({"enabled":true,"recovery_codes":["aaaa2222","bbbb3333"]})";
    s.answers["PUT /v1/me/devices/d-2"] = R"({"device":{"id":"d-2","device_name":"Kitchen Pi"}})";
    s.answers["DELETE /v1/me/devices/d-2"] = R"({"signed_out":true})";
    s.answers["POST /v1/me/signout-all"] = R"({"signed_out":3})";
    s.answers["GET /v1/me/notices"] = R"({"notices":[{"id":"n-1","kind":"security","subject":"New sign-in on Anna's phone","text":"","created_at":"2026-11-19T08:00:00Z","created_by":"service","read_at":""}]})";
    const AccountClient client("https://account.example", s.Call());
    ProfileChange change;
    change.displayName = "Anna Kessler";
    change.yearOfBirth = 1991;
    REQUIRE(client.UpdateProfile("us_abc", change));
    REQUIRE(s.calls.back().find(R"("display_name":"Anna Kessler")") != std::string::npos);
    REQUIRE(s.calls.back().find("\"year_of_birth\":1991") != std::string::npos);
    REQUIRE(s.calls.back().find("handle") == std::string::npos);   // not changed, not sent
    AccountSettings settings;
    settings.notifications["deals"] = "off";
    settings.historyOnAccount = true;
    REQUIRE(client.UpdateSettings("us_abc", settings));
    REQUIRE(s.calls.back().find(R"("history_on_account":true)") != std::string::npos);
    const auto changed = client.ChangePassword("us_abc", "old password here", "new password here 1");
    REQUIRE(changed && changed.Value() == 2);
    const auto setup = client.TwoFactorSetupStart("us_abc");
    REQUIRE(setup && setup.Value().secret == "JBSWY3DPEHPK3PXP");
    const auto codes = client.TwoFactorEnable("us_abc", "123456");
    REQUIRE(codes && codes.Value().size() == 2 && codes.Value()[1] == "bbbb3333");
    REQUIRE(client.RenameDevice("us_abc", "d-2", "Kitchen Pi"));
    REQUIRE(s.calls.back().find("PUT /v1/me/devices/d-2 | Bearer us_abc | {\"name\":\"Kitchen Pi\"}") == 0);
    REQUIRE(client.SignOutDevice("us_abc", "d-2"));
    REQUIRE(s.calls.back().find("DELETE /v1/me/devices/d-2 | Bearer us_abc |") == 0);
    const auto others = client.SignOutOthers("us_abc");
    REQUIRE(others && others.Value() == 3);
    const auto notices = client.Notices("us_abc");
    REQUIRE(notices && notices.Value().size() == 1 && !notices.Value()[0].Read() && notices.Value()[0].kind == "security");
}

TEST(the_session_file_round_trips_and_is_the_owners_alone) {
    const std::string root = TempDir();
    REQUIRE(!AccountSession::Load(root).SignedIn());
    AccountSession s;
    s.url = "https://account.example/";
    s.token = "us_abc";
    s.userId = "u-1";
    s.email = "anna@example.org";
    s.displayName = "Anna K.";
    s.deviceId = "d-1";
    REQUIRE(s.Save(root));
    const AccountSession back = AccountSession::Load(root);
    REQUIRE(back.SignedIn());
    REQUIRE_EQ(back.url, "https://account.example");
    REQUIRE_EQ(back.token, "us_abc");
    REQUIRE_EQ(back.deviceId, "d-1");
#ifndef _WIN32
    const auto perms = std::filesystem::status(UltraCanvas::PathFromUtf8(AccountSession::Path(root))).permissions();
    REQUIRE((perms & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) == std::filesystem::perms::none);
#endif
    AccountSession::Clear(root);
    REQUIRE(!AccountSession::Load(root).SignedIn());
    std::filesystem::remove_all(UltraCanvas::PathFromUtf8(root));
}

TEST(the_licences_are_kept_in_step_both_ways) {
    std::vector<uint8_t> seed, pk;
    REQUIRE(GenerateSigningKey(seed, pk));
    const std::string key = ToHex(pk);
    const std::string root = TempDir();
    SqliteRegistry registry;
    REQUIRE(registry.Open(UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(root) / "registry.sqlite")));
    // This machine bought app A; the account holds app B (bought elsewhere).
    LicenceRecord local;
    local.app = "org.example.a";
    local.licenceId = "lic-a";
    local.orderId = "o-a";
    local.token = MakeLicence(seed, "lic-a", "org.example.a");
    REQUIRE(registry.PutLicence(local));
    const std::string tokenB = MakeLicence(seed, "lic-b", "org.example.b");
    FakeAccountService s;
    s.answers["GET /v1/me/licences"] = R"({"licences":[{"id":"lic-b","app":"org.example.b","order":"o-b","issued":"2026-11-01","updates":"always","seats":3,"seats_used":1,"licence":")" + tokenB + R"(","installs":[]}]})";
    s.answers["POST /v1/me/licences"] = R"({"licence":{"id":"lic-a","app":"org.example.a","order":"o-a","seats":3,"seats_used":0,"installs":[]}})";
    s.answers["POST /v1/me/licences/lic-a/install"] = R"({"licence":{"id":"lic-a","app":"org.example.a","seats":3,"seats_used":1,"installs":[{"session":"d-1","removed_at":""}]}})";
    s.answers["POST /v1/me/licences/lic-b/install"] = R"({"licence":{"id":"lic-b","app":"org.example.b","seats":3,"seats_used":2,"installs":[{"session":"d-1","removed_at":""}]}})";
    const AccountClient client("https://account.example", s.Call());
    const auto sync = SyncLicencesWithAccount(registry, client, "us_abc", key);
    REQUIRE(sync);
    REQUIRE_EQ(sync.Value().attached.size(), size_t(1));
    REQUIRE_EQ(sync.Value().attached[0], "org.example.a");
    REQUIRE_EQ(sync.Value().fetched.size(), size_t(1));
    REQUIRE_EQ(sync.Value().fetched[0], "org.example.b");
    REQUIRE(sync.Value().failed.empty() && sync.Value().noSeat.empty());
    const auto b = registry.GetLicence("org.example.b");
    REQUIRE(b && b.Value().licenceId == "lic-b" && b.Value().token == tokenB && b.Value().orderSecret.empty());
    // A licence signed by another key is not fetched; a seat refused is said.
    std::vector<uint8_t> seed2, pk2;
    REQUIRE(GenerateSigningKey(seed2, pk2));
    s.answers["GET /v1/me/licences"] = R"({"licences":[{"id":"lic-c","app":"org.example.c","licence":")" + MakeLicence(seed2, "lic-c", "org.example.c") + R"(","installs":[]},)"
                                       R"({"id":"lic-d","app":"org.example.d","licence":")" + MakeLicence(seed, "lic-d", "org.example.d") + R"(","installs":[]}]})";
    s.answers["POST /v1/me/licences"] = R"({"licence":{"id":"lic-a","app":"org.example.a","installs":[]}})";
    const auto again = SyncLicencesWithAccount(registry, client, "us_abc", key);
    REQUIRE(again);
    REQUIRE(again.Value().fetched.empty());
    REQUIRE_EQ(again.Value().failed.size(), size_t(2));   // lic-c's key, and lic-d's install a 404 from the map: a failure, not a seat refusal
    REQUIRE_EQ(again.Value().failed[0].first, "org.example.c");
    REQUIRE_EQ(again.Value().failed[1].first, "org.example.d");
    REQUIRE_EQ(again.Value().noSeat.size(), size_t(0));
    std::filesystem::remove_all(UltraCanvas::PathFromUtf8(root));
}
