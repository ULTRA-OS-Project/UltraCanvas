// Tests/UltraPasswordTests.cpp
// Unit tests for UltraPassword's core: the group/entry model, the encrypted
// vault file (which is also the export format), the CSV exchange and the
// password generator.
//
// The properties that matter if the file is stolen or edited: no password is
// on disk in the clear, a wrong password fails closed, any change to the file
// (the Argon2id cost in the header included) is detected, and an export opens
// with its own password and nothing else.
//
// The tests derive keys at a small Argon2id cost so they run in a second; the
// production profiles are checked by value, not by running them.
//
// Self-contained: no test framework, no UI stack.
//
// Author: UltraCanvas Framework / ULTRA OS
#include "core/CsvExchange.h"
#include "core/PasswordGenerator.h"
#include "core/PasswordVault.h"
#include "core/VaultFile.h"

#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace UltraPassword;
namespace fs = std::filesystem;

static int g_failures = 0;
static int g_checks   = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

static UltraCryptSecureBuffer Buf(const std::string& text) {
    return UltraCryptSecureBuffer(text.data(), text.size());
}

static UltraCryptKdfParams FastCost() {
    UltraCryptKdfParams p;
    p.iterations = 1;
    p.memoryKiB  = 8 * 1024;
    return p;
}

static std::vector<uint8_t> ReadBytes(const std::string& path) {
    std::ifstream file(UltraCanvas::PathFromUtf8(path), std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

static void WriteBytes(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream file(UltraCanvas::PathFromUtf8(path), std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

static bool Contains(const std::vector<uint8_t>& hay, const std::string& needle) {
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end()) != hay.end();
}

// A small vault: two top-level groups, a sub-group, entries at every level.
static void FillSample(PasswordVault& v) {
    v.SetName("Sample");
    const std::string work = v.AddGroup("Work");
    const std::string cloud = v.AddGroup("Cloud", work);
    v.AddGroup("Private");

    PasswordEntry mail;
    mail.groupId = work; mail.title = "Company mail"; mail.url = "https://mail.example.com";
    mail.username = "erika"; mail.password = "S3cret-Mail-Passw0rd!";
    mail.method = SignInMethod::PasswordAuthenticatorApp;
    v.AddEntry(mail);

    PasswordEntry aws;
    aws.groupId = cloud; aws.title = "AWS console"; aws.username = "root";
    aws.password = "Cl0ud,with \"quotes\"\nand a newline"; aws.method = SignInMethod::PasswordHardwareKey;
    aws.notes = "=cmd|' /C calc'!A0";   // a formula, for the CSV injection check
    v.AddEntry(aws);

    PasswordEntry gh;
    gh.title = "GitHub"; gh.url = "https://github.com"; gh.username = "erika@example.com";
    gh.method = SignInMethod::Passkey;
    v.AddEntry(gh);   // top level

    PasswordEntry figma;
    figma.title = "Figma"; figma.method = SignInMethod::SingleSignOn; figma.ssoProvider = "Google";
    v.AddEntry(figma);
}

static void TestModel() {
    std::printf("model\n");
    PasswordVault v;
    FillSample(v);
    Check(v.GroupCount() == 3, "three groups");
    Check(v.EntryCount() == 4, "four entries");

    const PasswordGroup* work = nullptr;
    for (const auto* g : v.ChildGroups(kRootGroupId)) if (g->name == "Work") work = g;
    Check(work != nullptr, "Work is a top-level group");
    if (!work) return;
    Check(v.EntriesIn(work->id).size() == 1, "Work holds one entry directly");
    Check(v.ChildGroups(work->id).size() == 1, "Work holds one sub-group");
    const std::string cloud = v.ChildGroups(work->id).front()->id;
    Check(v.GroupPath(cloud) == "Work / Cloud", "group path");
    Check(v.EntriesIn(kRootGroupId).size() == 2, "two entries at the top level");

    Check(!v.MoveGroup(work->id, cloud), "a group cannot move into its own child");
    Check(v.Search("github").size() == 1, "search by title");
    Check(v.Search("S3cret").empty(), "search never matches a password");
    Check(v.Search("google").size() == 1, "search by SSO provider");

    Check(v.RemoveGroup(work->id), "remove Work");
    Check(v.GroupCount() == 1, "removing a group removes its sub-groups");
    Check(v.EntryCount() == 2, "and the entries inside them");

    Check(v.AddGroup("x", "no-such-group").empty(), "a group needs an existing parent");

    // Every method has a description and a level; passkeys rate strongest.
    for (SignInMethod m : AllSignInMethods()) {
        const auto& info = GetSignInMethodInfo(m);
        Check(info.method == m && std::strlen(info.description) > 20, "method info");
    }
    Check(GetSignInMethodInfo(SignInMethod::Passkey).level == SecurityLevel::VeryStrong,
          "passkey is very strong");
    Check(GetSignInMethodInfo(SignInMethod::Password).level == SecurityLevel::Weak,
          "password alone is weak");
}

static void TestSerialisation() {
    std::printf("serialisation\n");
    PasswordVault v;
    FillSample(v);
    UltraCryptSecureBuffer blob;
    v.Serialize(blob);

    PasswordVault back;
    Check(back.Deserialize(blob), "payload round trip");
    Check(back.GetName() == "Sample", "vault name survives");
    Check(back.GroupCount() == v.GroupCount() && back.EntryCount() == v.EntryCount(),
          "counts survive");
    for (const auto& e : v.Entries()) {
        const PasswordEntry* b = back.FindEntry(e.id);
        Check(b && b->password == e.password && b->method == e.method &&
              b->groupId == e.groupId && b->ssoProvider == e.ssoProvider &&
              b->created == e.created, "entry fields survive: " + e.title);
    }

    // Truncated input fails cleanly and leaves the vault empty.
    UltraCryptSecureBuffer cut(blob.Data(), blob.GetSize() - 3);
    PasswordVault bad;
    Check(!bad.Deserialize(cut), "truncated payload is refused");
    Check(bad.EntryCount() == 0, "and leaves nothing behind");
}

static void TestVaultFile(const fs::path& dir) {
    std::printf("vault file\n");
    const std::string path = UltraCanvas::PathToUtf8(dir / "test.upwvault");
    PasswordVault v;
    FillSample(v);

    {
        VaultFile file;
        VaultResult r = file.Create(path, Buf("correct horse"), v, FastCost());
        Check(r.Ok(), "create: " + r.message);
        Check(!file.Create(path, Buf("x"), v, FastCost()).Ok(), "create refuses to overwrite");
    }

    const auto bytes = ReadBytes(path);
    Check(bytes.size() > 60, "file written");
    Check(!Contains(bytes, "S3cret-Mail-Passw0rd!"), "no password on disk in the clear");
    Check(!Contains(bytes, "Company mail"), "no title on disk in the clear");

    {
        VaultFile file;
        PasswordVault out;
        VaultResult r = file.Open(path, Buf("wrong"), out);
        Check(r.code == VaultError::AuthenticationFailed, "wrong password fails closed");
        Check(out.EntryCount() == 0, "and decrypts nothing");
        Check(!file.IsOpen(), "and leaves the file closed");
    }

    {
        VaultFile file;
        PasswordVault out;
        VaultResult r = file.Open(path, Buf("correct horse"), out);
        Check(r.Ok(), "open: " + r.message);
        Check(out.EntryCount() == 4, "entries back");
        // Edit and save; a fresh nonce per save means the bytes change.
        PasswordEntry e;
        e.title = "Bank"; e.password = "b4nk";
        out.AddEntry(e);
        Check(file.Save(out).Ok(), "save");
        Check(ReadBytes(path) != bytes, "save rewrites the file");
    }

    {
        VaultFile file;
        PasswordVault out;
        Check(file.Open(path, Buf("correct horse"), out).Ok() && out.EntryCount() == 5,
              "saved entry persists");

        // Export copy: same password, different file.
        const std::string copy = UltraCanvas::PathToUtf8(dir / "copy.upwvault");
        Check(file.ExportCopy(copy, out).Ok(), "export copy");
        Check(!file.ExportCopy(path, out).Ok(), "export refuses to overwrite the open vault");
        PasswordVault imported;
        Check(VaultFile::ReadFile(copy, Buf("correct horse"), imported).Ok() &&
              imported.EntryCount() == 5, "export copy opens with the master password");

        // Export under its own password.
        const std::string shared = UltraCanvas::PathToUtf8(dir / "shared.upwvault");
        Check(VaultFile::ExportWithPassword(shared, Buf("other pass"), out, FastCost()).Ok(),
              "export with a different password");
        PasswordVault s;
        Check(!VaultFile::ReadFile(shared, Buf("correct horse"), s).Ok(),
              "that export does not open with the master password");
        Check(VaultFile::ReadFile(shared, Buf("other pass"), s).Ok() && s.EntryCount() == 5,
              "it opens with its own");

        // Import merges under a new group without touching what is there.
        const size_t before = out.EntryCount();
        const std::string into = out.ImportFrom(s, "Imported");
        Check(!into.empty(), "import creates a group");
        Check(out.EntryCount() == before * 2, "import adds every entry");
        Check(out.GroupCount() == 4 + 3 + 0, "and every group, nested under the new one");

        // Change password.
        Check(file.ChangePassword(Buf("new pass"), out).Ok(), "change password");
    }
    {
        PasswordVault out;
        Check(!VaultFile::ReadFile(path, Buf("correct horse"), out).Ok(), "old password stops working");
        Check(VaultFile::ReadFile(path, Buf("new pass"), out).Ok(), "new password works");
    }

    // Tampering anywhere is detected: in the header's cost, in the salt, in the
    // ciphertext, in the tag.
    const auto good = ReadBytes(path);
    for (size_t at : {size_t(12), size_t(16), size_t(25), size_t(40), size_t(70), good.size() - 1}) {
        auto bad = good;
        bad[at] ^= 0x01;
        WriteBytes(path, bad);
        PasswordVault out;
        VaultResult r = VaultFile::ReadFile(path, Buf("new pass"), out);
        Check(!r.Ok(), "flipped byte " + std::to_string(at) + " is detected");
    }
    WriteBytes(path, good);

    // Not a vault at all.
    const std::string junk = UltraCanvas::PathToUtf8(dir / "junk.upwvault");
    WriteBytes(junk, std::vector<uint8_t>(100, 'x'));
    PasswordVault out;
    Check(VaultFile::ReadFile(junk, Buf("x"), out).code == VaultError::NotAVault, "junk is not a vault");

    // Empty password refused.
    VaultFile f2;
    Check(f2.Create(UltraCanvas::PathToUtf8(dir / "empty.upwvault"), UltraCryptSecureBuffer(), v,
                    FastCost()).code == VaultError::InvalidArgument, "empty password refused");

    // The production profiles are the strong ones.
    Check(KdfParamsFor(KdfProfile::Maximum).memoryKiB == 1024 * 1024 &&
          KdfParamsFor(KdfProfile::Maximum).iterations == 4, "maximum profile is 1 GiB x 4");
    Check(KdfParamsFor(KdfProfile::Strong).memoryKiB == 256 * 1024, "strong profile is 256 MiB");
}

static void TestThrottle() {
    std::printf("throttle\n");
    UnlockThrottle t;
    t.RecordFailure(100);
    t.RecordFailure(100);
    Check(t.SecondsToWait(100) == 0, "two typos cost nothing");
    t.RecordFailure(100);
    Check(t.SecondsToWait(100) == 1, "third failure waits 1 s");
    t.RecordFailure(101);
    Check(t.SecondsToWait(101) == 2, "then 2 s");
    for (int i = 0; i < 20; ++i) t.RecordFailure(200);
    Check(t.SecondsToWait(200) == 60, "capped at a minute");
    t.RecordSuccess();
    Check(t.SecondsToWait(200) == 0, "success resets");
}

static void TestCsv(const fs::path& dir) {
    std::printf("csv\n");
    PasswordVault v;
    FillSample(v);
    std::string csv = ExportCsv(v);
    Check(csv.rfind("name,url,username,password,note", 0) == 0, "browser-compatible header");
    Check(csv.find("\"'=cmd") != std::string::npos, "a formula cell is defused");

    PasswordVault back;
    std::string error;
    const int n = ImportCsv(csv, back, kRootGroupId, error);
    Check(n == 4, "all rows import back: " + error);
    bool foundAws = false;
    for (const auto& e : back.Entries()) {
        if (e.title == "AWS console") {
            foundAws = true;
            Check(e.password == "Cl0ud,with \"quotes\"\nand a newline", "quoted password survives");
            Check(e.method == SignInMethod::PasswordHardwareKey, "method survives");
            Check(back.GroupPath(e.groupId) == "Work / Cloud", "nested group recreated");
        }
        if (e.title == "Figma") Check(e.ssoProvider == "Google", "SSO provider survives");
    }
    Check(foundAws, "AWS row imported");

    // A Chrome export: different column order, no group or method columns.
    const std::string chrome =
        "name,url,username,password,note\r\n"
        "example.com,https://www.example.com/login,me,pw1,\r\n"
        ",https://www.shop.test/,buyer,pw2,hello\r\n";
    PasswordVault c;
    Check(ImportCsv(chrome, c, kRootGroupId, error) == 2, "chrome csv");
    Check(c.Search("shop.test").size() == 1 && c.Search("shop.test")[0]->title == "shop.test",
          "an untitled row is named after its host");

    // Bitwarden-style columns.
    const std::string bw =
        "folder,favorite,type,name,notes,fields,reprompt,login_uri,login_username,login_password,login_totp\n"
        "Social,,login,Mastodon,,,0,https://mastodon.social,me,pw3,\n";
    PasswordVault b;
    Check(ImportCsv(bw, b, kRootGroupId, error) == 1, "bitwarden csv");
    Check(b.Entries().size() == 1 && b.Entries()[0].password == "pw3" &&
          b.GroupPath(b.Entries()[0].groupId) == "Social", "bitwarden columns mapped");

    Check(ImportCsv("hello,world\n1,2\n", b, kRootGroupId, error) == -1, "a non-password CSV is refused");

    const std::string file = UltraCanvas::PathToUtf8(dir / "export.csv");
    Check(WriteCsvFile(file, v, error), "write csv file");
    PasswordVault f;
    Check(ReadCsvFile(file, f, kRootGroupId, error) == 4, "read csv file");
    WipeString(csv);
}

static void TestGenerator() {
    std::printf("generator\n");
    std::set<std::string> seen;
    for (int i = 0; i < 50; ++i) {
        const std::string p = GeneratePassword();
        Check(p.size() == 20, "default length 20");
        bool lower = false, upper = false, digit = false, symbol = false;
        for (unsigned char ch : p) {
            if (std::islower(ch)) lower = true;
            else if (std::isupper(ch)) upper = true;
            else if (std::isdigit(ch)) digit = true;
            else symbol = true;
        }
        Check(lower && upper && digit && symbol, "every class present");
        Check(p.find_first_of("l1IO0o") == std::string::npos, "no ambiguous characters");
        seen.insert(p);
    }
    Check(seen.size() == 50, "no repeats");
    GeneratorOptions o;
    o.length = 3;
    Check(GeneratePassword(o).size() == 8, "length clamped to 8");
    o.lowercase = o.uppercase = o.digits = o.symbols = false;
    Check(GeneratePassword(o).empty(), "no classes, no password");
    Check(EstimateEntropyBits(GeneratePassword()) > 100.0, "20 mixed chars > 100 bits");
}

int main() {
    if (!UltraCrypt_IsAvailable()) {
        std::printf("UltraPasswordTests: no crypto backend, skipped\n");
        return 0;
    }
    const fs::path dir = fs::temp_directory_path() / "ultrapassword-tests";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    TestModel();
    TestSerialisation();
    TestVaultFile(dir);
    TestThrottle();
    TestCsv(dir);
    TestGenerator();

    fs::remove_all(dir, ec);
    std::printf("UltraPasswordTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
