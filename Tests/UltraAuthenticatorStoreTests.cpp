// Tests/UltraAuthenticatorStoreTests.cpp
// Unit tests for the authenticator's encrypted vault.
//
// The properties under test are the ones that matter if the file is stolen or
// modified: the seeds must never appear on disk in the clear, a wrong password
// must fail closed, any edit to the file must be detected, and an interrupted
// write must not destroy the previous vault. Also where the vault lives by
// default (VaultLocation.h), and that one moved there from where 0.2.6 kept
// it still opens.
//
// Self-contained: no test framework, no UI stack.
//
// Author: UltraCanvas Framework / ULTRA OS
#include "../Apps/UltraAuthenticator/store/EncryptedFileStore.h"
#include "../Apps/UltraAuthenticator/VaultLocation.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "../UltraCanvas/include/UltraCanvasPathUtf8.h"

using namespace UltraCanvas;
using namespace UltraCanvas::Authenticator;

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

static bool BufferEquals(const UltraCryptSecureBuffer& buffer,
                         const std::string& text) {
    return UltraCrypt_ConstantTimeEquals(buffer.Data(), buffer.GetSize(),
                                         text.data(), text.size());
}

static std::vector<uint8_t> ReadFileBytes(const std::string& path) {
    std::ifstream file(UltraCanvas::PathFromUtf8(path), std::ios::binary | std::ios::ate);
    if (!file) return {};
    const std::streamoff size = file.tellg();
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.seekg(0);
    if (size > 0) file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

static void WriteFileBytes(const std::string& path,
                           const std::vector<uint8_t>& data) {
    std::ofstream file(UltraCanvas::PathFromUtf8(path), std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
}

// A representative seed: the RFC 4226 test secret, as it would be stored.
static const char* kSeed = "otpauth://totp/Example:alice@example.com"
                           "?secret=JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP&issuer=Example";
static const char* kPassword = "correct horse battery staple";

static UltraCryptSecureBuffer RandomShare() {
    UltraCryptSecureBuffer share;
    UltraCrypt_RandomSecureBuffer(kRecoveryShareBytes, share);
    return share;
}

// A vault as UltraAuthenticator 0.2 wrote it - container version 1, the
// entries sealed with the password key itself - built here by hand, so the
// reader of version 1 stays tested after the writer moved on.
static void WriteVersion1Vault(const std::string& path, const std::string& password,
                               const std::string& key, const std::string& value) {
    UltraCryptKdfParams kdf = UltraCrypt_RecommendedKdfParams();
    UltraCryptSecureBuffer derived;
    UltraCrypt_DeriveKeyFromPassword(Buf(password), kdf, derived);
    auto u32 = [](std::vector<uint8_t>& out, uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    std::vector<uint8_t> plain;
    u32(plain, 1);
    plain.push_back(static_cast<uint8_t>(key.size()));
    plain.push_back(0);
    plain.insert(plain.end(), key.begin(), key.end());
    u32(plain, static_cast<uint32_t>(value.size()));
    plain.insert(plain.end(), value.begin(), value.end());

    std::vector<uint8_t> header = {'U', 'C', 'A', 'V', 'A', 'U', 'L', 'T', 1, 1, 1, 0};
    u32(header, kdf.iterations);
    u32(header, kdf.memoryKiB);
    header.insert(header.end(), kdf.salt.begin(), kdf.salt.end());
    UltraCryptAeadParams aead;
    aead.nonce.assign(24, 0);
    UltraCrypt_RandomBytes(aead.nonce.data(), aead.nonce.size());
    header.insert(header.end(), aead.nonce.begin(), aead.nonce.end());
    aead.associatedData = header;
    std::vector<uint8_t> cipher;
    UltraCrypt_AeadSeal(derived, aead, plain.data(), plain.size(), cipher);
    header.insert(header.end(), cipher.begin(), cipher.end());
    WriteFileBytes(path, header);
}

// Recovery from a forgotten master password (container version 2): the
// recovery slot opens with the server share and only with it, the header
// names the escrow without the password, a recovery spends the slot and sets
// the new password, an edited slot is refused, and the password slot keeps
// working beside it. Then a version 1 vault: it opens, and its next write
// makes it version 2 with the same entries.
static void TestRecovery(const std::filesystem::path& dir) {
    std::printf("Recovery\n");
    const std::string path = UltraCanvas::PathToUtf8(dir / "recovery.ucvault");
    UltraCryptSecureBuffer share = RandomShare();
    const std::string escrowId = "e-0123456789abcdef0123456789abcdef";
    const std::string service = "https://account.ultraos.example";
    {
        EncryptedFileStore store;
        Check(static_cast<bool>(store.Create(path, Buf(kPassword))), "a vault for recovery is created");
        Check(store.GetContainerVersion() == 2, "a new vault is container version 2");
        store.Put("account-1", Buf(kSeed));
        Check(!store.GetRecoveryInfo().enabled, "recovery starts off");
        Check(!store.EnableRecovery(Buf("short"), escrowId, service), "a share of the wrong size is refused");
        Check(static_cast<bool>(store.EnableRecovery(share, escrowId, service)), "recovery is turned on");
        Check(store.GetRecoveryInfo().enabled && store.GetRecoveryInfo().escrowId == escrowId,
              "the open vault reports the escrow");
    }
    {
        VaultRecoveryInfo info;
        Check(static_cast<bool>(EncryptedFileStore::ReadRecoveryInfo(path, info)) && info.enabled &&
                  info.escrowId == escrowId && info.serviceUrl == service,
              "the header names the escrow and the service without the password");
        const std::vector<uint8_t> raw = ReadFileBytes(path);
        Check(std::search(raw.begin(), raw.end(), share.Data(), share.Data() + share.GetSize()) == raw.end(),
              "the server share is not in the file");
        EncryptedFileStore store;
        Check(static_cast<bool>(store.Open(path, Buf(kPassword))), "the password still opens a vault with recovery on");
        store.Put("account-2", Buf("second"));
        Check(static_cast<bool>(store.ChangePassword(Buf("an interim password"))), "the password changes");
        Check(store.GetRecoveryInfo().enabled, "a password change keeps the recovery slot");
    }
    {
        EncryptedFileStore store;
        UltraCryptSecureBuffer wrong = RandomShare();
        auto refused = store.OpenWithRecovery(path, wrong, Buf("new password"));
        Check(!refused && refused.code == StoreResultCode::AuthenticationFailed, "a wrong share is refused");
        Check(!store.IsOpen(), "a refused recovery leaves the vault closed");

        // An edited recovery slot: the local share, then the escrow id.
        const std::vector<uint8_t> original = ReadFileBytes(path);
        const std::string tampered = UltraCanvas::PathToUtf8(dir / "recovery-tampered.ucvault");
        for (size_t offset : {size_t(108 + 3), size_t(108 + 32 + 24 + 48 + 1 + 4)}) {
            std::vector<uint8_t> bad = original;
            bad[offset] ^= 0x01;
            WriteFileBytes(tampered, bad);
            EncryptedFileStore probe;
            Check(!probe.OpenWithRecovery(tampered, share, Buf("new password")),
                  "an edited recovery slot is refused (byte " + std::to_string(offset) + ")");
            Check(!probe.Open(tampered, Buf("an interim password")),
                  "an edited recovery slot also fails the password open (byte " + std::to_string(offset) + ")");
        }
    }
    {
        EncryptedFileStore store;
        auto recovered = store.OpenWithRecovery(path, share, Buf("the new password"));
        Check(static_cast<bool>(recovered), "the server share recovers the vault: " + recovered.message);
        Check(store.Count() == 2, "every entry is there after the recovery");
        Check(!store.GetRecoveryInfo().enabled, "the recovery slot is spent");
    }
    {
        EncryptedFileStore store;
        Check(static_cast<bool>(store.Open(path, Buf("the new password"))), "the new password opens it");
        Check(!store.Open(path, Buf("an interim password")), "the forgotten one no longer does");
        EncryptedFileStore again;
        Check(!again.OpenWithRecovery(path, share, Buf("x")), "the spent share opens nothing");
    }
    {
        EncryptedFileStore store;
        store.Open(path, Buf("the new password"));
        store.EnableRecovery(share, "", "");
        Check(store.GetRecoveryInfo().enabled && store.GetRecoveryInfo().escrowId.empty(),
              "a recovery code alone, with no account, is a recovery slot too");
        Check(static_cast<bool>(store.DisableRecovery()) && !store.GetRecoveryInfo().enabled, "recovery turns off");
        EncryptedFileStore probe;
        Check(!probe.OpenWithRecovery(path, share, Buf("x")), "a vault with recovery off refuses every share");
    }

    std::printf("Version 1 vault\n");
    const std::string oldPath = UltraCanvas::PathToUtf8(dir / "version1.ucvault");
    WriteVersion1Vault(oldPath, kPassword, "account-1", kSeed);
    {
        EncryptedFileStore store;
        auto opened = store.Open(oldPath, Buf(kPassword));
        Check(static_cast<bool>(opened), "a version 1 vault opens: " + opened.message);
        Check(store.GetContainerVersion() == 1, "it reports version 1 until it is written");
        UltraCryptSecureBuffer value;
        Check(static_cast<bool>(store.Get("account-1", value)) && BufferEquals(value, kSeed), "its entry reads back");
        Check(!EncryptedFileStore().Open(oldPath, Buf("wrong")), "a wrong password is refused on version 1 too");
        store.Put("account-2", Buf("second"));
        Check(store.GetContainerVersion() == 2, "the first write makes it version 2");
    }
    {
        Check(ReadFileBytes(oldPath)[8] == 2, "the file on disk is version 2");
        EncryptedFileStore store;
        Check(static_cast<bool>(store.Open(oldPath, Buf(kPassword))) && store.Count() == 2,
              "the upgraded vault opens with both entries");
    }
}

// Compared as paths, so the separator the platform joins with does not matter.
static bool SamePath(const std::string& actual, const std::filesystem::path& expected) {
    return UltraCanvas::PathFromUtf8(actual).lexically_normal() == expected.lexically_normal();
}

// The default vault folder (VaultLocation.h): each platform's rule, where
// 0.2.6 left the vault, and moving a real vault into place once - it opens
// there with its password, the settings beside it come along, and a vault
// already in place is never replaced.
static void TestVaultLocation() {
    std::printf("Vault location\n");
    namespace fs = std::filesystem;
    using UltraCanvas::PathToUtf8;

    // Windows: %APPDATA% - ahead of HOME, which 0.2.6 preferred; without it,
    // the folder %APPDATA% would have named.
    Check(SamePath(ResolveDataDir(DataDirPlatform::Windows, "", "C:/Users/erika/AppData/Roaming", "C:/Users/erika"),
                   fs::path("C:/Users/erika/AppData/Roaming") / "UltraAuthenticator"), "Windows: APPDATA ahead of HOME");
    Check(SamePath(ResolveDataDir(DataDirPlatform::Windows, "", "", "C:/Users/erika"),
                   fs::path("C:/Users/erika") / "AppData" / "Roaming" / "UltraAuthenticator"), "Windows without APPDATA");
    // macOS: Application Support, not ~/.local/share.
    Check(SamePath(ResolveDataDir(DataDirPlatform::MacOS, "", "", "/Users/erika"),
                   fs::path("/Users/erika/Library/Application Support/UltraAuthenticator")), "macOS: Application Support");
    // Linux and the rest: XDG, unchanged.
    Check(SamePath(ResolveDataDir(DataDirPlatform::Unix, "", "", "/home/erika"),
                   fs::path("/home/erika/.local/share/UltraAuthenticator")), "Linux: ~/.local/share");
    Check(SamePath(ResolveDataDir(DataDirPlatform::Unix, "", "C:/AppData", "/home/erika"),
                   fs::path("/home/erika/.local/share/UltraAuthenticator")), "APPDATA means nothing outside Windows");
    // XDG_DATA_HOME wins everywhere.
    Check(SamePath(ResolveDataDir(DataDirPlatform::MacOS, "/Volumes/data", "", "/Users/erika"),
                   fs::path("/Volumes/data/UltraAuthenticator")), "XDG_DATA_HOME wins on macOS");
    Check(SamePath(ResolveDataDir(DataDirPlatform::Windows, "D:/portable", "C:/AppData", ""),
                   fs::path("D:/portable/UltraAuthenticator")), "XDG_DATA_HOME wins on Windows");
    Check(ResolveDataDir(DataDirPlatform::Unix, "", "", "").empty(), "no home: nothing");
    Check(VaultPathIn("/data/UltraAuthenticator") == PathToUtf8(fs::path("/data/UltraAuthenticator") / "accounts.vault"),
          "the vault file in the folder");

    // Where 0.2.6 kept it: wherever HOME was set, except on Linux.
    Check(LegacyDataDirs(DataDirPlatform::Unix, "/home/erika").empty(), "Linux kept its folder");
    Check(LegacyDataDirs(DataDirPlatform::Windows, "").empty(), "Windows without HOME: nothing to move");
    const auto mac = LegacyDataDirs(DataDirPlatform::MacOS, "/Users/erika");
    Check(mac.size() == 1 && SamePath(mac[0], fs::path("/Users/erika/.local/share/UltraAuthenticator")),
          "macOS: ~/.local/share/UltraAuthenticator");
    const auto win = LegacyDataDirs(DataDirPlatform::Windows, "C:/Users/erika");
    Check(win.size() == 1 && SamePath(win[0], fs::path("C:/Users/erika/.local/share/UltraAuthenticator")),
          "Windows with HOME: ~/.local/share/UltraAuthenticator");

    // A real vault in the old place moves whole, and opens in the new one.
    const fs::path root   = fs::temp_directory_path() / UltraCanvas::PathFromUtf8("ultraauthenticator-location-ทดสอบ-🚀");
    const fs::path legacy = root / "old" / "UltraAuthenticator";
    const fs::path target = root / "new" / "UltraAuthenticator";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(legacy, ec);
    {
        EncryptedFileStore store;
        Check(static_cast<bool>(store.Create(VaultPathIn(PathToUtf8(legacy)), Buf("correct horse"))),
              "a vault in the old folder");
        Check(static_cast<bool>(store.Put("account-1", Buf("JBSWY3DPEHPK3PXP"))), "with an entry");
    }
    {
        std::ofstream settings(legacy / "settings.ini", std::ios::binary);
        settings << "[lock]\nidle=300\n";
    }
    std::string error;
    Check(SamePath(AdoptLegacyDataDir(PathToUtf8(target), {PathToUtf8(legacy)}, error), legacy),
          "moved from the old folder");
    Check(error.empty(), "moved without error");
    Check(!fs::exists(legacy), "nothing left behind");
    Check(fs::is_regular_file(target / "settings.ini"), "the settings came along");
    {
        EncryptedFileStore store;
        UltraCryptSecureBuffer value;
        Check(static_cast<bool>(store.Open(VaultPathIn(PathToUtf8(target)), Buf("correct horse"))) &&
              static_cast<bool>(store.Get("account-1", value)) && BufferEquals(value, "JBSWY3DPEHPK3PXP"),
              "the moved vault opens with its password");
    }
    // A second start finds it in place and moves nothing.
    Check(AdoptLegacyDataDir(PathToUtf8(target), {PathToUtf8(legacy)}, error).empty() && error.empty(),
          "nothing to move the second time");
    // A vault already in place is never replaced by an old one.
    const fs::path other = root / "older" / "UltraAuthenticator";
    fs::create_directories(other, ec);
    {
        std::ofstream old(other / "accounts.vault", std::ios::binary);
        old << "old";
    }
    Check(AdoptLegacyDataDir(PathToUtf8(target), {PathToUtf8(other)}, error).empty(), "a vault in place is kept");
    Check(fs::is_regular_file(other / "accounts.vault"), "and the old one is untouched");
    fs::remove_all(root, ec);
}

int main() {
    std::printf("Authenticator vault tests — backend: %s\n\n",
                UltraCrypt_GetBackendName().c_str());

    if (!UltraCrypt_IsAvailable()) {
        std::printf("FAIL: no crypto backend; the vault cannot operate.\n");
        return 1;
    }

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "ultraauth-store-tests";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const std::string vaultPath = (dir / "accounts.ucvault").string();

    // ===== Create, put, reopen =====
    std::printf("Create and reopen\n");
    {
        EncryptedFileStore store;
        Check(static_cast<bool>(store.Create(vaultPath, Buf(kPassword))),
              "a new vault is created");
        Check(store.IsOpen(), "the new vault is open");
        Check(store.Count() == 0, "a new vault is empty");
        Check(static_cast<bool>(store.Put("account-1", Buf(kSeed))),
              "an entry is stored");
        Check(store.Count() == 1, "the entry is counted");
    }

    Check(EncryptedFileStore::Exists(vaultPath), "the vault file exists on disk");

    {
        EncryptedFileStore store;
        auto opened = store.Open(vaultPath, Buf(kPassword));
        Check(static_cast<bool>(opened), "the vault reopens: " + opened.message);
        Check(store.Count() == 1, "the entry survived the round trip");

        UltraCryptSecureBuffer value;
        Check(static_cast<bool>(store.Get("account-1", value)), "the entry reads back");
        Check(BufferEquals(value, kSeed), "the entry round-trips exactly");

        std::vector<std::string> keys;
        Check(static_cast<bool>(store.List(keys)) && keys.size() == 1 &&
                  keys[0] == "account-1",
              "List reports the key");
    }

    // ===== The seed must not be on disk in the clear =====
    std::printf("Confidentiality on disk\n");
    {
        const std::vector<uint8_t> raw = ReadFileBytes(vaultPath);
        Check(!raw.empty(), "the vault file is readable");
        const std::string needle = "JBSWY3DPEHPK3PXP";
        Check(std::search(raw.begin(), raw.end(), needle.begin(), needle.end()) ==
                  raw.end(),
              "the Base32 seed does not appear in the file");
        const std::string uriNeedle = "otpauth://";
        Check(std::search(raw.begin(), raw.end(), uriNeedle.begin(), uriNeedle.end()) ==
                  raw.end(),
              "the otpauth URI does not appear in the file");
        const std::string keyNeedle = "account-1";
        Check(std::search(raw.begin(), raw.end(), keyNeedle.begin(), keyNeedle.end()) ==
                  raw.end(),
              "even the entry key is encrypted, not just the value");
        Check(std::memcmp(raw.data(), "UCAVAULT", 8) == 0, "the magic is present");
    }

    // ===== Wrong password =====
    std::printf("Wrong password\n");
    {
        EncryptedFileStore store;
        auto r = store.Open(vaultPath, Buf("wrong password"));
        Check(!r, "a wrong password is refused");
        Check(r.code == StoreResultCode::AuthenticationFailed,
              "a wrong password reports AuthenticationFailed");
        Check(!store.IsOpen(), "a failed open leaves the vault closed");
        Check(store.Count() == 0, "a failed open exposes no entries");

        UltraCryptSecureBuffer value;
        Check(!store.Get("account-1", value), "a closed vault serves nothing");

        auto empty = store.Open(vaultPath, UltraCryptSecureBuffer());
        Check(!empty && empty.code == StoreResultCode::InvalidArgument,
              "an empty password is refused outright");
    }

    // ===== Tamper detection =====
    std::printf("Tamper detection\n");
    {
        const std::vector<uint8_t> original = ReadFileBytes(vaultPath);
        const std::string tampered = (dir / "tampered.ucvault").string();

        auto expectRejected = [&](std::vector<uint8_t> data, const std::string& what) {
            WriteFileBytes(tampered, data);
            EncryptedFileStore store;
            auto r = store.Open(tampered, Buf(kPassword));
            Check(!r, what + " is rejected");
            Check(!store.IsOpen(), what + " leaves the vault closed");
        };

        std::vector<uint8_t> flippedCipher = original;
        flippedCipher[70] ^= 0x01;
        expectRejected(flippedCipher, "a flipped ciphertext bit");

        std::vector<uint8_t> flippedTag = original;
        flippedTag.back() ^= 0x01;
        expectRejected(flippedTag, "a flipped tag bit");

        std::vector<uint8_t> flippedSalt = original;
        flippedSalt[25] ^= 0x01;              // inside the salt
        expectRejected(flippedSalt, "a flipped salt bit");

        std::vector<uint8_t> flippedNonce = original;
        flippedNonce[40] ^= 0x01;             // inside the nonce
        expectRejected(flippedNonce, "a flipped nonce bit");

        // Downgrading the declared Argon2id cost must not be obeyed: the header
        // is authenticated, so this is detected rather than honoured.
        std::vector<uint8_t> weakened = original;
        weakened[12] = 1; weakened[13] = 0; weakened[14] = 0; weakened[15] = 0;
        expectRejected(weakened, "a downgraded iteration count");

        std::vector<uint8_t> truncated(original.begin(), original.end() - 1);
        expectRejected(truncated, "a truncated file");

        std::vector<uint8_t> headerOnly(original.begin(), original.begin() + 60);
        expectRejected(headerOnly, "a file with no ciphertext");

        std::vector<uint8_t> badMagic = original;
        badMagic[0] = 'X';
        expectRejected(badMagic, "a bad magic value");

        std::vector<uint8_t> badVersion = original;
        badVersion[8] = 99;
        expectRejected(badVersion, "an unknown container version");

        // Absurd cost parameters must be refused before any allocation.
        std::vector<uint8_t> hugeMemory = original;
        hugeMemory[16] = 0xFF; hugeMemory[17] = 0xFF;
        hugeMemory[18] = 0xFF; hugeMemory[19] = 0xFF;
        expectRejected(hugeMemory, "an absurd declared memory cost");

        // A wrong password and a modified file must be indistinguishable.
        WriteFileBytes(tampered, flippedCipher);
        EncryptedFileStore a, b;
        const std::string tamperMessage = a.Open(tampered, Buf(kPassword)).message;
        const std::string wrongMessage  = b.Open(vaultPath, Buf("nope")).message;
        Check(tamperMessage == wrongMessage,
              "a modified file and a wrong password report the same message");
    }

    // ===== Multiple entries, update, delete =====
    std::printf("Entry management\n");
    {
        EncryptedFileStore store;
        Check(static_cast<bool>(store.Open(vaultPath, Buf(kPassword))), "vault opens");
        Check(static_cast<bool>(store.Put("account-2", Buf("second secret value"))),
              "a second entry is stored");
        Check(static_cast<bool>(store.Put("account-3", Buf("third secret value"))),
              "a third entry is stored");
        Check(store.Count() == 3, "three entries are held");

        Check(static_cast<bool>(store.Put("account-2", Buf("replacement value"))),
              "an entry is replaced");
        Check(store.Count() == 3, "replacing does not add an entry");
        UltraCryptSecureBuffer value;
        store.Get("account-2", value);
        Check(BufferEquals(value, "replacement value"), "the replacement is stored");

        Check(static_cast<bool>(store.Delete("account-3")), "an entry is deleted");
        Check(store.Count() == 2, "the deletion is counted");
        Check(!store.Get("account-3", value), "a deleted entry is gone");
        auto missing = store.Delete("account-3");
        Check(!missing && missing.code == StoreResultCode::NotFound,
              "deleting a missing entry reports NotFound");

        auto badKey = store.Put("", Buf("x"));
        Check(!badKey && badKey.code == StoreResultCode::InvalidArgument,
              "an empty key is refused");
    }

    // Every mutation must already be durable — no explicit save call.
    {
        EncryptedFileStore store;
        Check(static_cast<bool>(store.Open(vaultPath, Buf(kPassword))), "vault reopens");
        Check(store.Count() == 2, "the mutations were persisted immediately");
        UltraCryptSecureBuffer value;
        store.Get("account-2", value);
        Check(BufferEquals(value, "replacement value"), "the replacement persisted");
    }

    // ===== Create must not clobber =====
    std::printf("Create safety\n");
    {
        EncryptedFileStore store;
        auto r = store.Create(vaultPath, Buf("another password"));
        Check(!r && r.code == StoreResultCode::AlreadyExists,
              "Create refuses to overwrite an existing vault");

        EncryptedFileStore check;
        Check(static_cast<bool>(check.Open(vaultPath, Buf(kPassword))),
              "the original vault is untouched");
        Check(check.Count() == 2, "the original contents survive");

        EncryptedFileStore noPassword;
        auto e = noPassword.Create((dir / "nopass.ucvault").string(),
                                   UltraCryptSecureBuffer());
        Check(!e && e.code == StoreResultCode::InvalidArgument,
              "a vault cannot be created without a password");
        Check(!EncryptedFileStore::Exists((dir / "nopass.ucvault").string()),
              "a refused create leaves no file behind");
    }

    // ===== Password change =====
    std::printf("Password change\n");
    {
        EncryptedFileStore store;
        Check(static_cast<bool>(store.Open(vaultPath, Buf(kPassword))), "vault opens");
        const uint32_t saltBefore = store.GetKdfIterations();
        Check(static_cast<bool>(store.ChangePassword(Buf("a new master password"))),
              "the password changes");
        Check(store.Count() == 2, "the contents survive the change");
        Check(saltBefore == store.GetKdfIterations(), "the cost parameters are kept");
    }
    {
        EncryptedFileStore store;
        Check(static_cast<bool>(store.Open(vaultPath, Buf("a new master password"))),
              "the vault opens with the new password");
        Check(store.Count() == 2, "the contents are intact");
        UltraCryptSecureBuffer value;
        store.Get("account-1", value);
        Check(BufferEquals(value, kSeed), "the seed survived the re-encryption");

        EncryptedFileStore old;
        Check(!old.Open(vaultPath, Buf(kPassword)),
              "the old password no longer opens the vault");
    }

    // ===== Each write is freshly randomised =====
    std::printf("Write freshness\n");
    {
        const std::vector<uint8_t> before = ReadFileBytes(vaultPath);
        EncryptedFileStore store;
        store.Open(vaultPath, Buf("a new master password"));
        store.Put("account-4", Buf("value four"));
        const std::vector<uint8_t> after = ReadFileBytes(vaultPath);
        Check(before != after, "writing changes the file");
        // The nonce lives at offset 36; two writes must never share one.
        Check(!std::equal(before.begin() + 36, before.begin() + 60,
                          after.begin() + 36),
              "each write uses a fresh nonce");
        Check(std::equal(before.begin() + 20, before.begin() + 36,
                         after.begin() + 20),
              "the salt is stable across writes, so the key is not re-derived");
    }

    // ===== Closed-store behaviour =====
    std::printf("Closed store\n");
    {
        EncryptedFileStore store;
        UltraCryptSecureBuffer value;
        std::vector<std::string> keys;
        Check(!store.IsOpen(), "a fresh store is closed");
        Check(!store.Put("k", Buf("v")), "a closed store refuses Put");
        Check(!store.Get("k", value), "a closed store refuses Get");
        Check(!store.Delete("k"), "a closed store refuses Delete");
        Check(!store.List(keys), "a closed store refuses List");
        Check(!store.ChangePassword(Buf("x")), "a closed store refuses ChangePassword");

        auto missing = store.Open((dir / "does-not-exist.ucvault").string(),
                                  Buf(kPassword));
        Check(!missing && missing.code == StoreResultCode::IoError,
              "opening a missing file reports IoError");
    }

    TestRecovery(dir);

    std::filesystem::remove_all(dir, ec);

    TestVaultLocation();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
