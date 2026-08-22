// Tests/UltraAuthenticatorStoreTests.cpp
// Unit tests for the authenticator's encrypted vault.
//
// The properties under test are the ones that matter if the file is stolen or
// modified: the seeds must never appear on disk in the clear, a wrong password
// must fail closed, any edit to the file must be detected, and an interrupted
// write must not destroy the previous vault.
//
// Self-contained: no test framework, no UI stack.
//
// Author: UltraCanvas Framework / ULTRA OS
#include "../Apps/UltraAuthenticator/store/EncryptedFileStore.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const std::streamoff size = file.tellg();
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.seekg(0);
    if (size > 0) file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

static void WriteFileBytes(const std::string& path,
                           const std::vector<uint8_t>& data) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
}

// A representative seed: the RFC 4226 test secret, as it would be stored.
static const char* kSeed = "otpauth://totp/Example:alice@example.com"
                           "?secret=JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP&issuer=Example";
static const char* kPassword = "correct horse battery staple";

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

    std::filesystem::remove_all(dir, ec);

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
