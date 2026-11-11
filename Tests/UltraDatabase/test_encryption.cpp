// Tests/UltraDatabase/test_encryption.cpp
// SQLite files encrypted at rest (UltraDatabaseEncryption.h): what reaches
// the disk is ciphertext - the database, its WAL, its journal - and what
// comes back is the database; a wrong key, a plain file, a changed byte and
// a missing key are each refused as what they are; a file is encrypted,
// decrypted and re-keyed in place; and SQLite's own recovery - a WAL left by
// a crash, a hot journal - works through the encryption.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraDatabase/UltraDatabase.h>
#include <UltraCrypt/UltraCryptCore.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const char* kSecret = "erika-mustermann-secret-subject";

std::shared_ptr<const UltraCryptSecureBuffer> NewKey() {
    UltraCryptSecureBuffer key;
    REQUIRE(UltraCrypt_RandomSecureBuffer(32, key).success);
    return std::make_shared<const UltraCryptSecureBuffer>(std::move(key));
}

fs::path Folder(const std::string& tag) {
    fs::path dir = fs::temp_directory_path() / ("ultradb-enc-" + tag);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

std::string Bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool Holds(const fs::path& p, const std::string& text) {
    return Bytes(p).find(text) != std::string::npos;
}

std::string Register(const std::string& name, const fs::path& file,
                     std::shared_ptr<const UltraCryptSecureBuffer> key) {
    UltraDb_CloseConnection(name);
    UltraDbConnectionConfig cfg;
    cfg.name = name;
    cfg.database = file.string();
    cfg.encryptionKey = std::move(key);
    REQUIRE(UltraDb_RegisterConnection(cfg).success);
    return name;
}

int64_t Count(const std::string& c, const std::string& sql = "SELECT count(*) FROM t") {
    UltraDbResultSet rs;
    UltraDbResult r = UltraDb_Query(c, sql, rs);
    if (!r.success || rs.Empty()) return -1;
    return rs.Row(0)[0].AsInt64();
}

void Fill(const std::string& c, int rows) {
    REQUIRE(UltraDb_Exec(c, "CREATE TABLE IF NOT EXISTS t(id INTEGER PRIMARY KEY, subject TEXT, body BLOB)").success);
    REQUIRE(UltraDb_Exec(c, "BEGIN").success);
    for (int i = 0; i < rows; ++i) {
        UltraDbParams p;
        p.push_back(UltraDbValue(std::string(kSecret) + " #" + std::to_string(i)));
        p.push_back(UltraDbValue::Blob(std::vector<uint8_t>(static_cast<size_t>(300 + i % 5000), 'x')));
        REQUIRE(UltraDb_Exec(c, "INSERT INTO t(subject, body) VALUES (?, ?)", p).success);
    }
    REQUIRE(UltraDb_Exec(c, "COMMIT").success);
}

void CopyIfThere(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (fs::exists(from)) fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
}

} // namespace

TEST(encryption_available) {
    REQUIRE(UltraDb_SqliteEncryptionAvailable());
}

TEST(encryption_writes_only_ciphertext) {
    const fs::path dir = Folder("cipher");
    const fs::path file = dir / "mail.db";
    auto key = NewKey();
    const std::string c = Register("enc-cipher", file, key);
    REQUIRE(UltraDb_Exec(c, "PRAGMA journal_mode=WAL").success);
    REQUIRE(UltraDb_Exec(c, "PRAGMA wal_autocheckpoint=0").success);
    Fill(c, 400);
    REQUIRE_EQ(Count(c), int64_t(400));
    // While it runs: the database and its WAL hold no row in the clear.
    REQUIRE(fs::exists(fs::path(file.string() + "-wal")));
    REQUIRE(!Holds(fs::path(file.string() + "-wal"), kSecret));
    REQUIRE(!Holds(file, kSecret));
    REQUIRE(UltraDb_Exec(c, "PRAGMA wal_checkpoint(TRUNCATE)").success);
    UltraDb_CloseConnection(c);
    REQUIRE(!Holds(file, kSecret));
    REQUIRE(!Holds(file, "SQLite format 3"));
    REQUIRE(Bytes(file).substr(0, 15) == "UltraDB:XC20P:1");
    REQUIRE(UltraDb_InspectSqliteFile(file.string()) == UltraDbSqliteFileKind::Encrypted);
    REQUIRE(UltraDb_SqliteKeyOpens(file.string(), *key));

    // Back with the same key: all of it.
    const std::string again = Register("enc-cipher-again", file, key);
    REQUIRE_EQ(Count(again), int64_t(400));
    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(again, "SELECT subject FROM t WHERE id = 7", rs).success);
    REQUIRE_EQ(rs.Row(0)[0].AsString(), std::string(kSecret) + " #6");
    UltraDb_CloseConnection(again);
}

TEST(encryption_refuses_wrong_key_plain_file_and_no_key) {
    const fs::path dir = Folder("refuse");
    const fs::path file = dir / "a.db";
    const std::string c = Register("enc-refuse", file, NewKey());
    Fill(c, 10);
    UltraDb_CloseConnection(c);

    auto other = NewKey();
    REQUIRE(!UltraDb_SqliteKeyOpens(file.string(), *other));
    const std::string wrong = Register("enc-refuse-wrong", file, other);
    UltraDbResult r = UltraDb_OpenConnection(wrong);
    REQUIRE(!r.success);
    REQUIRE(r.code == UltraDbResultCode::CredentialsUnavailable);
    UltraDb_CloseConnection(wrong);

    // Without a key it is not a database at all.
    UltraDbConnectionConfig plain;
    plain.name = "enc-refuse-nokey";
    plain.database = file.string();
    UltraDb_CloseConnection(plain.name);
    REQUIRE(UltraDb_RegisterConnection(plain).success);
    REQUIRE(Count(plain.name) == -1);
    UltraDb_CloseConnection(plain.name);

    // A plain file with a key: refused, with the way to encrypt it.
    const fs::path plainFile = dir / "plain.db";
    plain.name = "enc-refuse-plain";
    plain.database = plainFile.string();
    REQUIRE(UltraDb_RegisterConnection(plain).success);
    REQUIRE(UltraDb_Exec(plain.name, "CREATE TABLE t(x)").success);
    UltraDb_CloseConnection(plain.name);
    REQUIRE(UltraDb_InspectSqliteFile(plainFile.string()) == UltraDbSqliteFileKind::Plain);
    const std::string keyed = Register("enc-refuse-keyed", plainFile, NewKey());
    r = UltraDb_OpenConnection(keyed);
    REQUIRE(!r.success);
    REQUIRE(r.message.find("UltraDb_SetSqliteFileEncryption") != std::string::npos);
    UltraDb_CloseConnection(keyed);
}

TEST(encryption_detects_a_changed_byte) {
    const fs::path dir = Folder("tamper");
    const fs::path file = dir / "a.db";
    auto key = NewKey();
    const std::string c = Register("enc-tamper", file, key);
    Fill(c, 200);
    UltraDb_CloseConnection(c);
    {
        std::fstream f(file, std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(4096 * 2 + 100);
        char b = 0;
        f.seekg(4096 * 2 + 100);
        f.read(&b, 1);
        b ^= 0x01;
        f.seekp(4096 * 2 + 100);
        f.write(&b, 1);
    }
    const std::string again = Register("enc-tamper-again", file, key);
    // Page 3 no longer opens: reading every row has to reach it.
    UltraDbResultSet rs;
    REQUIRE(!UltraDb_Query(again, "SELECT sum(length(body)) FROM t", rs).success);
    UltraDb_CloseConnection(again);
}

TEST(encryption_in_place_encrypt_rekey_decrypt) {
    const fs::path dir = Folder("inplace");
    const fs::path file = dir / "contacts.db";
    UltraDbConnectionConfig plain;
    plain.name = "enc-inplace-plain";
    plain.database = file.string();
    UltraDb_CloseConnection(plain.name);
    REQUIRE(UltraDb_RegisterConnection(plain).success);
    REQUIRE(UltraDb_Exec(plain.name, "PRAGMA journal_mode=WAL").success);
    REQUIRE(UltraDb_Exec(plain.name, "PRAGMA wal_autocheckpoint=0").success);
    Fill(plain.name, 300);
    REQUIRE(UltraDb_Exec(plain.name, "CREATE INDEX t_subject ON t(subject)").success);
    // Rows still in the WAL when the file is converted: a crash before the
    // conversion leaves them there too. Copied, so they are not checkpointed.
    const fs::path crashed = dir / "crashed.db";
    CopyIfThere(file, crashed);
    CopyIfThere(fs::path(file.string() + "-wal"), fs::path(crashed.string() + "-wal"));
    UltraDb_CloseConnection(plain.name);
    REQUIRE(Holds(file, kSecret));

    auto key = NewKey();
    REQUIRE(UltraDb_SetSqliteFileEncryption(crashed.string(), nullptr, key.get()).success);
    REQUIRE(!fs::exists(fs::path(crashed.string() + "-wal")));
    REQUIRE(!Holds(crashed, kSecret));
    std::string c = Register("enc-inplace-crashed", crashed, key);
    REQUIRE_EQ(Count(c), int64_t(300));
    REQUIRE_EQ(Count(c, "SELECT count(*) FROM sqlite_schema WHERE name = 't_subject'"), int64_t(1));
    UltraDb_CloseConnection(c);

    REQUIRE(UltraDb_SetSqliteFileEncryption(file.string(), nullptr, key.get()).success);
    REQUIRE(!fs::exists(fs::path(file.string() + ".rekey")));
    REQUIRE(UltraDb_InspectSqliteFile(file.string()) == UltraDbSqliteFileKind::Encrypted);
    REQUIRE(!Holds(file, kSecret));

    // A second key, then none.
    auto second = NewKey();
    REQUIRE(!UltraDb_SetSqliteFileEncryption(file.string(), second.get(), key.get()).success);
    REQUIRE(UltraDb_SetSqliteFileEncryption(file.string(), key.get(), second.get()).success);
    REQUIRE(!UltraDb_SqliteKeyOpens(file.string(), *key));
    c = Register("enc-inplace-second", file, second);
    REQUIRE_EQ(Count(c), int64_t(300));
    UltraDb_CloseConnection(c);
    REQUIRE(UltraDb_SetSqliteFileEncryption(file.string(), second.get(), nullptr).success);
    REQUIRE(UltraDb_InspectSqliteFile(file.string()) == UltraDbSqliteFileKind::Plain);
    REQUIRE(UltraDb_RegisterConnection(plain).success);
    REQUIRE_EQ(Count(plain.name), int64_t(300));
    UltraDb_CloseConnection(plain.name);

    // A file that is not there: nothing to do.
    REQUIRE(UltraDb_SetSqliteFileEncryption((dir / "none.db").string(), nullptr, key.get()).success);
}

TEST(encryption_recovers_a_wal_left_by_a_crash) {
    const fs::path dir = Folder("walcrash");
    const fs::path file = dir / "mail.db";
    auto key = NewKey();
    const std::string c = Register("enc-walcrash", file, key);
    REQUIRE(UltraDb_Exec(c, "PRAGMA journal_mode=WAL").success);
    REQUIRE(UltraDb_Exec(c, "PRAGMA wal_autocheckpoint=0").success);
    Fill(c, 50);
    REQUIRE(UltraDb_Exec(c, "PRAGMA wal_checkpoint(TRUNCATE)").success);
    Fill(c, 150);   // these only in the WAL
    // The files as a crash leaves them: copied while the connection holds them.
    const fs::path copy = dir / "copy.db";
    CopyIfThere(file, copy);
    CopyIfThere(fs::path(file.string() + "-wal"), fs::path(copy.string() + "-wal"));
    UltraDb_CloseConnection(c);

    const std::string r = Register("enc-walcrash-copy", copy, key);
    REQUIRE_EQ(Count(r), int64_t(200));
    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(r, "PRAGMA integrity_check", rs).success);
    REQUIRE_EQ(rs.Row(0)[0].AsString(), std::string("ok"));
    UltraDb_CloseConnection(r);
}

TEST(encryption_rolls_back_a_hot_journal) {
    const fs::path dir = Folder("journal");
    const fs::path file = dir / "outbox.db";
    auto key = NewKey();
    const std::string c = Register("enc-journal", file, key);
    REQUIRE(UltraDb_Exec(c, "PRAGMA journal_mode=DELETE").success);
    Fill(c, 100);
    REQUIRE_EQ(Count(c), int64_t(100));
    // A transaction big enough to spill pages into the database file before
    // it commits, its originals in the journal - then "the power fails".
    REQUIRE(UltraDb_Exec(c, "PRAGMA cache_size=2").success);
    REQUIRE(UltraDb_Exec(c, "BEGIN").success);
    REQUIRE(UltraDb_Exec(c, "UPDATE t SET subject = 'changed', body = zeroblob(2000)").success);
    REQUIRE(UltraDb_Exec(c, "DELETE FROM t WHERE id > 60").success);
    const fs::path journal(file.string() + "-journal");
    REQUIRE(fs::exists(journal));
    REQUIRE(!Holds(journal, kSecret));
    const fs::path copy = dir / "copy.db";
    CopyIfThere(file, copy);
    CopyIfThere(journal, fs::path(copy.string() + "-journal"));
    REQUIRE(UltraDb_Exec(c, "ROLLBACK").success);
    REQUIRE_EQ(Count(c, "SELECT count(*) FROM t WHERE subject <> 'changed'"), int64_t(100));
    UltraDb_CloseConnection(c);

    // The copy opens with the hot journal beside it, and SQLite rolls it back.
    const std::string r = Register("enc-journal-copy", copy, key);
    REQUIRE_EQ(Count(r, "SELECT count(*) FROM t WHERE subject LIKE 'erika%'"), int64_t(100));
    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(r, "PRAGMA integrity_check", rs).success);
    REQUIRE_EQ(rs.Row(0)[0].AsString(), std::string("ok"));
    UltraDb_CloseConnection(r);
}

TEST(encryption_keeps_temporary_data_off_the_disk) {
    const fs::path dir = Folder("temp");
    const fs::path file = dir / "a.db";
    const std::string c = Register("enc-temp", file, NewKey());
    Fill(c, 3000);
    // A sort with no index, an index built, a VACUUM: each would use a
    // temporary file on disk - refused by the encrypting VFS, so these work
    // only because the connection keeps them in memory.
    UltraDbResultSet rs;
    REQUIRE(UltraDb_Query(c, "SELECT subject FROM t ORDER BY length(body) DESC, subject LIMIT 3", rs).success);
    REQUIRE_EQ(rs.Size(), size_t(3));
    REQUIRE(UltraDb_Exec(c, "CREATE INDEX t_body ON t(length(body))").success);
    REQUIRE(UltraDb_Exec(c, "VACUUM").success);
    REQUIRE_EQ(Count(c), int64_t(3000));
    UltraDb_CloseConnection(c);
}

TEST(encryption_two_connections_one_file) {
    const fs::path dir = Folder("two");
    const fs::path file = dir / "a.db";
    auto key = NewKey();
    const std::string a = Register("enc-two-a", file, key);
    REQUIRE(UltraDb_Exec(a, "PRAGMA journal_mode=WAL").success);
    Fill(a, 20);
    const std::string b = Register("enc-two-b", file, key);
    REQUIRE_EQ(Count(b), int64_t(20));
    Fill(a, 5);
    REQUIRE_EQ(Count(b), int64_t(25));
    UltraDb_CloseConnection(a);
    UltraDb_CloseConnection(b);
}
