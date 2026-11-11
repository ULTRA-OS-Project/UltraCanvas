// Tests/UltraMail/test_storage.cpp
// The mail kept on the computer, encrypted at rest (UltraMailStorage.h): the
// storage key made once and kept in the vault, a key per database file, the
// message files sealed and opened - a plain one still read, a changed or
// foreign one refused - an earlier version's plain files converted, and the
// mailbox database turned into an encrypted one in place and opened with its
// key.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailLocalStore.h"
#include "UltraMailStorage.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCrypt/UltraCryptCore.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace UltraMail;
namespace fs = std::filesystem;
using UltraCanvas::PathToUtf8;

namespace {

fs::path Fresh(const std::string& tag) {
    const fs::path dir = fs::temp_directory_path() / ("ultramail-storage-" + tag);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

std::string Bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

StorageKey RandomKey() {
    UltraCryptSecureBuffer key;
    UltraCrypt_RandomSecureBuffer(32, key);
    return std::make_shared<const UltraCryptSecureBuffer>(std::move(key));
}

const std::string kMessage =
    "From: Erika <erika@example.com>\r\nTo: max@example.com\r\nSubject: Geheim\r\n\r\n"
    "Die Zugangsdaten liegen im Tresor.\r\n";

} // namespace

TEST(storage_key_made_once_and_kept_in_the_vault) {
    UltraCrypt_Initialize();
    if (!UltraCrypt_IsAvailable()) return;   // a build without libsodium has no vault
    const fs::path dir = Fresh("key");
    StorageKey first;
    {
        CredentialVault vault(PathToUtf8(dir / "vault"));
        REQUIRE(vault.TryAutoUnlock());
        std::string error;
        REQUIRE(!LoadStorageKey(vault, /*create=*/false, error) && error.empty());
        first = LoadStorageKey(vault, /*create=*/true, error);
        REQUIRE(first && first->GetSize() == 32);
        vault.Lock();
    }
    {
        CredentialVault vault(PathToUtf8(dir / "vault"));
        REQUIRE(vault.TryAutoUnlock());
        std::string error;
        const StorageKey again = LoadStorageKey(vault, /*create=*/true, error);
        REQUIRE(again && UltraCrypt_ConstantTimeEquals(again->Data(), 32, first->Data(), 32));
        vault.Lock();
    }
    // Another program reads it with the device key, without writing anything.
    std::string problem;
    const StorageKey read = ReadMailboxStorageKey(PathToUtf8(dir), problem);
    REQUIRE(read && problem.empty());
    REQUIRE(UltraCrypt_ConstantTimeEquals(read->Data(), 32, first->Data(), 32));
    // A mailbox without a vault: no key, no problem, and no vault made.
    const fs::path none = Fresh("key-none");
    REQUIRE(!ReadMailboxStorageKey(PathToUtf8(none), problem) && problem.empty());
    REQUIRE(!fs::exists(none / "vault"));

    // Derived keys: one per file, none equal to another or to the storage key.
    const StorageKey mail = DatabaseKey(first, "mail.db");
    const StorageKey contacts = DatabaseKey(first, "contacts.db");
    const StorageKey bodies = BodyKey(first);
    REQUIRE(mail && contacts && bodies);
    REQUIRE(!UltraCrypt_ConstantTimeEquals(mail->Data(), 32, contacts->Data(), 32));
    REQUIRE(!UltraCrypt_ConstantTimeEquals(mail->Data(), 32, bodies->Data(), 32));
    REQUIRE(!UltraCrypt_ConstantTimeEquals(mail->Data(), 32, first->Data(), 32));
    REQUIRE(UltraCrypt_ConstantTimeEquals(DatabaseKey(first, "mail.db")->Data(), 32, mail->Data(), 32));
    REQUIRE(!DatabaseKey(nullptr, "mail.db"));
}

TEST(storage_message_files_sealed_and_opened) {
    UltraCrypt_Initialize();
    if (!UltraCrypt_IsAvailable()) return;
    const StorageKey key = RandomKey();
    std::string sealed, opened;
    REQUIRE(SealBody(key, kMessage, sealed));
    REQUIRE(IsSealedBody(sealed));
    REQUIRE(sealed.find("Geheim") == std::string::npos);
    REQUIRE(sealed.compare(0, 15, "UltraMail:eml:1") == 0);
    REQUIRE(OpenBody(key, sealed, opened) && opened == kMessage);
    // A plain file - an earlier version's - is read as it is.
    REQUIRE(!IsSealedBody(kMessage));
    REQUIRE(OpenBody(key, kMessage, opened) && opened == kMessage);
    REQUIRE(OpenBody(nullptr, kMessage, opened) && opened == kMessage);
    // A changed byte, another key, no key: refused.
    std::string changed = sealed;
    changed[changed.size() / 2] ^= 0x20;
    REQUIRE(!OpenBody(key, changed, opened));
    REQUIRE(!OpenBody(RandomKey(), sealed, opened));
    REQUIRE(!OpenBody(nullptr, sealed, opened));
    // Two seals of one message differ (a fresh nonce each).
    std::string twice;
    REQUIRE(SealBody(key, kMessage, twice) && twice != sealed);
}

TEST(storage_body_files_and_the_conversion_of_plain_ones) {
    UltraCrypt_Initialize();
    if (!UltraCrypt_IsAvailable()) return;
    const fs::path dir = Fresh("bodies");
    const fs::path inbox = dir / "mail" / "acct" / "INBOX";
    fs::create_directories(inbox);
    // An earlier version's files: plain.
    for (int uid = 1; uid <= 3; ++uid)
        std::ofstream(inbox / (std::to_string(uid) + ".eml"), std::ios::binary) << kMessage;
    { std::ofstream(inbox / "notes.txt") << "not a message"; }

    SetBodyKey(RandomKey());
    std::string raw;
    REQUIRE(ReadBodyFile(PathToUtf8(inbox / "1.eml"), raw) && raw == kMessage);   // plain, still read
    REQUIRE(EncryptStoredBodies(PathToUtf8(dir / "mail")) == 3);
    REQUIRE(EncryptStoredBodies(PathToUtf8(dir / "mail")) == 0);                   // nothing left
    REQUIRE(IsSealedBody(Bytes(inbox / "2.eml")));
    REQUIRE(Bytes(inbox / "notes.txt") == "not a message");
    REQUIRE(ReadBodyFile(PathToUtf8(inbox / "2.eml"), raw) && raw == kMessage);

    // A new message is written sealed, through a .part file that is gone after.
    REQUIRE(WriteBodyFile(PathToUtf8(inbox / "4.eml"), kMessage));
    REQUIRE(IsSealedBody(Bytes(inbox / "4.eml")));
    REQUIRE(!fs::exists(inbox / "4.eml.part"));
    REQUIRE(ReadBodyFile(PathToUtf8(inbox / "4.eml"), raw) && raw == kMessage);

    // Another mailbox's key does not open them.
    SetBodyKey(RandomKey());
    std::string error;
    REQUIRE(!ReadBodyFile(PathToUtf8(inbox / "4.eml"), raw, &error));
    REQUIRE(!error.empty());
    SetBodyKey(nullptr);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(storage_mailbox_database_encrypted_in_place) {
    UltraCrypt_Initialize();
    if (!UltraCrypt_IsAvailable() || !UltraDb_SqliteEncryptionAvailable()) return;
    const fs::path dir = Fresh("database");
    const std::string db = PathToUtf8(dir / "mail.db");
    Account account;
    account.accountId = "erika";
    account.email = "erika.geheim@example.com";
    account.displayName = "Erika";
    {
        LocalStore plain;
        REQUIRE(plain.Open("storage-plain", db).success);
        REQUIRE(plain.UpsertAccount(account).success);
        UltraDb_CloseConnection("storage-plain");
    }
    REQUIRE(UltraDb_InspectSqliteFile(db) == UltraDbSqliteFileKind::Plain);
    REQUIRE(Bytes(dir / "mail.db").find("erika.geheim") != std::string::npos);

    const StorageKey key = DatabaseKey(RandomKey(), "mail.db");
    REQUIRE(EncryptDatabaseFile(db, key).success);
    REQUIRE(EncryptDatabaseFile(db, key).success);   // already encrypted: nothing to do
    REQUIRE(UltraDb_InspectSqliteFile(db) == UltraDbSqliteFileKind::Encrypted);
    REQUIRE(Bytes(dir / "mail.db").find("erika.geheim") == std::string::npos);

    LocalStore encrypted;
    REQUIRE(encrypted.Open("storage-encrypted", db, key).success);
    std::vector<Account> accounts;
    REQUIRE(encrypted.ListAccounts(accounts).success);
    REQUIRE(accounts.size() == 1 && accounts[0].email == "erika.geheim@example.com");
    account.accountId = "max";
    account.email = "max.geheim@example.com";
    REQUIRE(encrypted.UpsertAccount(account).success);
    UltraDb_CloseConnection("storage-encrypted");
    REQUIRE(Bytes(dir / "mail.db").find("max.geheim") == std::string::npos);
    std::error_code ec;
    if (fs::exists(dir / "mail.db-wal")) REQUIRE(Bytes(dir / "mail.db-wal").find("max.geheim") == std::string::npos);
    fs::remove_all(dir, ec);
}
