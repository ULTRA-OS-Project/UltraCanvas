// Tests/OfficeEncryptionTest.cpp
// Password-protected Office files (ECMA-376 agile encryption), end to end:
//   * DecryptOfficePackage on a workbook and a document encrypted by
//     msoffcrypto-tool, an implementation independent of this one: the right
//     password, a wrong one, none, a non-ASCII one, and what is refused - a
//     changed byte of the encrypted package (the integrity HMAC), a changed
//     header, Office 2007's "standard" encryption, an absurd spin count;
//   * CheckOfficePassword, the prompt's quick check;
//   * UltraCanvasSpreadsheet::LoadFromFile on the .xlsx, and on a copy named
//     .xls, with and without the password;
//   * UCWordDocumentIO::Load and UltraCanvasFileLoader::LoadTextDocument on
//     the .docx;
//   * that ordinary files are not affected, and RequestDocumentPassword
//     calls straight back for one;
//   * Office 2010's SHA-1 / AES-128 keying, in a workbook encrypted that way;
//   * writing: UCCompoundFileWriter read back, EncryptOfficePackage round
//     trips (a package in the mini stream, one past the header's 109 FAT
//     sectors), fresh salts on every save, a changed byte caught, and the
//     spreadsheet, UCWordDocumentIO and UltraCanvasFileLoader saving with a
//     password and loading it back - and refusing a password for .ods / .odt.
// The fixtures are in Tests/fixtures (make-office-encrypted-fixtures.py
// rebuilds them and says what each holds). That msoffcrypto-tool decrypts
// what EncryptOfficePackage writes was checked when the writer was made; the
// test cannot run Python, so it checks the format against the reader here.
// Version: 1.1.0
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework

#include "UltraCanvasCompoundFile.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasOfficeEncryption.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasZipPackage.h"
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#ifndef OFFICETEST_FIXTURE_DIR
#error "OFFICETEST_FIXTURE_DIR must name Tests/fixtures"
#endif

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

// The password the fixtures were encrypted with: not ASCII, so the UTF-16LE
// conversion is part of what is checked.
const std::string kPassword = "\xC3\x9Cltra-P\xC3\xA4ssw\xC3\xB6rt 1";

std::string Fixture(const char* name) { return std::string(OFFICETEST_FIXTURE_DIR) + "/" + name; }

bool ReadStreams(const std::string& path, std::vector<uint8_t>& info, std::vector<uint8_t>& package) {
    UCCompoundFileReader cfb;
    return cfb.Open(path) && cfb.ReadStream("EncryptionInfo", info) &&
           cfb.ReadStream("EncryptedPackage", package);
}

std::string TextOf(const UltraCanvasSpreadsheet& book, int row, int col) {
    const SpreadsheetSheet* s = book.GetSheet(0);
    const SpreadsheetCell* c = s ? s->GetCellIfExists(row, col) : nullptr;
    return c ? c->GetText() : std::string("<no cell>");
}

double NumberOf(const UltraCanvasSpreadsheet& book, int row, int col) {
    const SpreadsheetSheet* s = book.GetSheet(0);
    const SpreadsheetCell* c = s ? s->GetCellIfExists(row, col) : nullptr;
    return c ? c->GetNumber() : std::nan("");
}

bool Contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

// ===== THE DECRYPTOR =====

void TestDecryptor() {
    std::cout << "\n=== DecryptOfficePackage ===\n";
    const std::string xlsx = Fixture("office-agile-encrypted.xlsx");
    Check(IsEncryptedOfficePackage(xlsx), "the encrypted workbook is recognised");
    Check(!IsEncryptedOfficePackage(Fixture("xls-biff8-sample.xls")),
          "an ordinary compound file (.xls) is not");
    Check(!IsEncryptedOfficePackage(Fixture("word97-formatting.docx")), "an ordinary .docx is not");

    std::vector<uint8_t> package;
    std::string error;
    Check(DecryptOfficePackageFile(xlsx, kPassword, package, error) == OfficeDecryptStatus::Decrypted,
          "the right password decrypts it");
    UCZipPackageReader zip;
    std::string sheetXml;
    Check(zip.OpenFromMemory(package) && zip.ReadEntry("xl/worksheets/sheet1.xml", sheetXml) &&
              Contains(sheetXml, "<c r=\"A403\"><v>2821</v></c>"),
          "into a ZIP package whose last segment's data is intact (A403 = 2821)");

    Check(DecryptOfficePackageFile(xlsx, "\xC3\x9Cltra-P\xC3\xA4ssw\xC3\xB6rt 2", package, error) ==
                  OfficeDecryptStatus::WrongPassword &&
              package.empty() && error == "The password is not correct",
          "a wrong password is WrongPassword, with nothing returned");
    Check(DecryptOfficePackageFile(xlsx, "Ultra-Passwort 1", package, error) ==
              OfficeDecryptStatus::WrongPassword,
          "the password without its umlauts is wrong too");
    Check(DecryptOfficePackageFile(xlsx, "", package, error) == OfficeDecryptStatus::PasswordRequired &&
              Contains(error, "password-protected"),
          "no password is PasswordRequired");
    Check(DecryptOfficePackageFile(Fixture("word97-formatting.docx"), kPassword, package, error) ==
              OfficeDecryptStatus::NotEncrypted,
          "an ordinary package is NotEncrypted");
    Check(DecryptOfficePackageFile(Fixture("xls-biff8-sample.xls"), kPassword, package, error) ==
              OfficeDecryptStatus::NotEncrypted,
          "so is an ordinary compound file");

    std::vector<uint8_t> info, encrypted;
    if (!ReadStreams(xlsx, info, encrypted)) {
        Check(false, "the fixture's streams can be read");
        return;
    }
    // One byte of the encrypted package changed - in its last segment, and in
    // the size field: the HMAC catches both before anything is decrypted.
    for (size_t at : {encrypted.size() - 1, size_t(0)}) {
        std::vector<uint8_t> changed = encrypted;
        changed[at] ^= 0x01;
        Check(DecryptOfficePackage(info, changed, kPassword, package, error) ==
                      OfficeDecryptStatus::Tampered &&
                  package.empty(),
              "a changed byte at " + std::to_string(at) + " is Tampered, with nothing returned");
    }
    {
        std::vector<uint8_t> truncated(encrypted.begin(), encrypted.end() - 16);
        Check(DecryptOfficePackage(info, truncated, kPassword, package, error) ==
                  OfficeDecryptStatus::Tampered,
              "a package cut short by a block is Tampered");
    }

    // The header: Office 2007's standard encryption, damage, an absurd cost.
    {
        std::vector<uint8_t> standard = info;
        standard[0] = 3; standard[2] = 2;   // version 3.2
        Check(DecryptOfficePackage(standard, encrypted, kPassword, package, error) ==
                      OfficeDecryptStatus::Unsupported &&
                  Contains(error, "Office 2007"),
              "standard encryption (3.2) is Unsupported, naming Office 2007");
    }
    {
        std::vector<uint8_t> shortInfo(info.begin(), info.begin() + 4);
        Check(DecryptOfficePackage(shortInfo, encrypted, kPassword, package, error) ==
                  OfficeDecryptStatus::Damaged,
              "a truncated header is Damaged");
    }
    auto withHeader = [&](const std::string& from, const std::string& to) {
        std::string xml(info.begin() + 8, info.end());
        const size_t at = xml.find(from);
        if (at != std::string::npos) xml.replace(at, from.size(), to);
        std::vector<uint8_t> out(info.begin(), info.begin() + 8);
        out.insert(out.end(), xml.begin(), xml.end());
        return out;
    };
    Check(DecryptOfficePackage(withHeader("spinCount=\"100000\"", "spinCount=\"4000000000\""),
                               encrypted, kPassword, package, error) == OfficeDecryptStatus::Damaged,
          "a spin count past Office's limit is refused before any hashing");
    Check(DecryptOfficePackage(withHeader("spinCount=\"100000\"", "spinCount=\"99999\""),
                               encrypted, kPassword, package, error) ==
              OfficeDecryptStatus::WrongPassword,
          "a different spin count derives a different key");
    Check(DecryptOfficePackage(withHeader("hashAlgorithm=\"SHA512\"", "hashAlgorithm=\"SHA384\""),
                               encrypted, kPassword, package, error) ==
              OfficeDecryptStatus::Unsupported,
          "a hash UltraCrypt does not have (SHA-384) is Unsupported");
    Check(DecryptOfficePackage(withHeader("<dataIntegrity", "<dataIntegrityX"), encrypted, kPassword,
                               package, error) == OfficeDecryptStatus::Unsupported &&
              Contains(error, "integrity"),
          "a file without an integrity check is not opened");

    std::cout << "\n=== CheckOfficePassword ===\n";
    Check(CheckOfficePassword(xlsx, kPassword, error) == OfficeDecryptStatus::Decrypted,
          "the right password checks out");
    Check(CheckOfficePassword(xlsx, "nope", error) == OfficeDecryptStatus::WrongPassword,
          "a wrong one does not");
    Check(CheckOfficePassword(xlsx, "", error) == OfficeDecryptStatus::PasswordRequired,
          "an empty one asks for one");
    Check(CheckOfficePassword(Fixture("word97-formatting.docx"), "", error) ==
              OfficeDecryptStatus::NotEncrypted,
          "an ordinary file needs none");
}

// ===== THE SPREADSHEET =====

void TestSpreadsheet() {
    std::cout << "\n=== A password-protected .xlsx in the spreadsheet ===\n";
    const std::string xlsx = Fixture("office-agile-encrypted.xlsx");
    {
        UltraCanvasSpreadsheet book("Enc", 0, 0, 400, 300);
        const bool ok = book.LoadFromFile(xlsx, kPassword);
        Check(ok, "LoadFromFile opens it with the password");
        if (!ok) std::cout << "         " << book.GetLastError() << "\n";
        Check(TextOf(book, 1, 0) == "Widget" && NumberOf(book, 1, 1) == 42.0,
              "A2 is \"Widget\", B2 is 42");
        Check(NumberOf(book, 2, 1) == 84.0, "B3 = B2*2 recalculates to 84");
        Check(NumberOf(book, 402, 0) == 2821.0, "A403, in the package's last segment, is 2821");
    }
    {
        UltraCanvasSpreadsheet book("Enc", 0, 0, 400, 300);
        Check(!book.LoadFromFile(xlsx) && Contains(book.GetLastError(), "password-protected"),
              "without a password it says the file is password-protected");
        Check(!book.LoadFromFile(xlsx, "wrong") &&
                  Contains(book.GetLastError(), "The password is not correct"),
              "with a wrong one it says so");
    }
    {
        // Excel opens a password-protected workbook under the .xls name too.
        const fs::path dir = fs::temp_directory_path() / "uc-office-encryption-test";
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path copy = dir / "renamed.xls";
        fs::copy_file(PathFromUtf8(xlsx), copy, fs::copy_options::overwrite_existing, ec);
        UltraCanvasSpreadsheet book("Enc", 0, 0, 400, 300);
        Check(!ec && book.LoadFromFile(PathToUtf8(copy), kPassword) && NumberOf(book, 1, 1) == 42.0,
              "a copy named .xls opens with the password too");
        fs::remove_all(dir, ec);
    }
}

// ===== THE WORD READER =====

void TestDocument() {
    std::cout << "\n=== A password-protected .docx ===\n";
    const std::string docx = Fixture("office-agile-encrypted.docx");
    UCRichDocument doc;
    std::string error;
    const bool ok = UCWordDocumentIO::Load(docx, doc, error, kPassword);
    Check(ok, "UCWordDocumentIO::Load opens it with the password");
    if (!ok) std::cout << "         " << error << "\n";
    const std::string text = doc.ToPlainText();
    Check(Contains(text, "Secret text") && Contains(text, "Paragraph 201"),
          "its first and last paragraphs arrive");

    Check(!UCWordDocumentIO::Load(docx, doc, error) && Contains(error, "password-protected"),
          "without a password it says the document is password-protected, not that it is a .doc");
    Check(!UCWordDocumentIO::Load(docx, doc, error, "wrong") &&
              Contains(error, "The password is not correct"),
          "with a wrong one it says so");

    auto loaded = UltraCanvasFileLoader::LoadTextDocument(docx, error, kPassword);
    Check(loaded && Contains(loaded->ToPlainText(), "Secret text"),
          "UltraCanvasFileLoader::LoadTextDocument opens it with the password");

    // An ordinary document is not affected, and needs no prompt.
    Check(UCWordDocumentIO::Load(Fixture("word97-formatting.docx"), doc, error),
          "an ordinary .docx still loads");
    bool calledBack = false, proceed = false;
    std::string password = "unset";
    UltraCanvasFileLoader::RequestDocumentPassword(Fixture("word97-formatting.docx"), nullptr,
        [&](bool go, const std::string& pw) { calledBack = true; proceed = go; password = pw; });
    Check(calledBack && proceed && password.empty(),
          "RequestDocumentPassword calls straight back for a file without a password");
}

// ===== OFFICE 2010: SHA-1 / AES-128 =====

void TestSha1() {
    std::cout << "\n=== Office 2010's SHA-1 keying ===\n";
    const std::string xlsx = Fixture("office-agile-sha1-encrypted.xlsx");
    std::vector<uint8_t> package;
    std::string error;
    Check(DecryptOfficePackageFile(xlsx, kPassword, package, error) == OfficeDecryptStatus::Decrypted,
          "a SHA-1 / AES-128 workbook decrypts");
    if (!error.empty()) std::cout << "         " << error << "\n";
    Check(DecryptOfficePackageFile(xlsx, "wrong", package, error) == OfficeDecryptStatus::WrongPassword,
          "a wrong password is still told apart");
    std::vector<uint8_t> info, encrypted;
    if (ReadStreams(xlsx, info, encrypted)) {
        encrypted[encrypted.size() / 2] ^= 0x80;
        Check(DecryptOfficePackage(info, encrypted, kPassword, package, error) ==
                  OfficeDecryptStatus::Tampered,
              "its 20-byte HMAC catches a changed byte");
    }
    UltraCanvasSpreadsheet book("Sha1", 0, 0, 400, 300);
    Check(book.LoadFromFile(xlsx, kPassword) && NumberOf(book, 402, 0) == 2821.0,
          "the spreadsheet opens it, A403 intact");
}

// ===== WRITING =====

fs::path TempDir() {
    const fs::path dir = fs::temp_directory_path() / "uc-office-encryption-write";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

void TestCompoundFileWriter() {
    std::cout << "\n=== UCCompoundFileWriter ===\n";
    UCCompoundFileWriter writer;
    std::vector<uint8_t> small(300), big(70000);
    for (size_t i = 0; i < small.size(); ++i) small[i] = static_cast<uint8_t>(i * 7);
    for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<uint8_t>(i * 13 + 1);
    Check(writer.AddStream("Small", small) && writer.AddStream("Big", big) &&
              writer.AddStream("Store/Inner/Deep", {1, 2, 3}) && writer.AddStream("Empty", {}),
          "streams and nested storages are accepted");
    Check(!writer.AddStream("Store", {1}), "a stream cannot take a storage's name");
    Check(!writer.AddStream(std::string(32, 'x'), {1}), "a name over 31 characters is refused");
    UCCompoundFileReader reader;
    std::vector<uint8_t> back;
    Check(reader.OpenFromMemory(writer.Build()), "the reader opens what it builds");
    Check(reader.ReadStream("Small", back) && back == small, "a mini-stream stream reads back");
    Check(reader.ReadStream("Big", back) && back == big, "a sector stream reads back");
    Check(reader.HasStream("Empty"), "an empty stream is listed");
    const std::vector<std::string> names = reader.StreamNames();
    Check(names.size() == 3, "the root lists its three streams, not the storage's");
}

void TestEncryptor() {
    std::cout << "\n=== EncryptOfficePackage ===\n";
    std::string error;
    for (size_t size : {size_t(200), size_t(21000), size_t(9u << 20)}) {
        std::vector<uint8_t> package(size);
        for (size_t i = 0; i < size; ++i) package[i] = static_cast<uint8_t>((i * 31) ^ (i >> 9));
        std::vector<uint8_t> file, back;
        const bool written = EncryptOfficePackage(package, kPassword, file, error);
        UCCompoundFileReader cfb;
        Check(written && cfb.OpenFromMemory(file) && IsEncryptedOfficePackage(cfb) &&
                  DecryptOfficePackage(cfb, kPassword, back, error) == OfficeDecryptStatus::Decrypted &&
                  back == package,
              "a " + std::to_string(size) + "-byte package round-trips");
    }
    std::vector<uint8_t> package(5000, 0x5A), first, second, back;
    EncryptOfficePackage(package, kPassword, first, error);
    EncryptOfficePackage(package, kPassword, second, error);
    Check(!first.empty() && first != second, "every encryption draws new salts and keys");
    Check(!EncryptOfficePackage(package, "", first, error) && first.empty(),
          "an empty password is refused");

    std::vector<uint8_t> file;
    EncryptOfficePackage(package, kPassword, file, error);
    UCCompoundFileReader cfb;
    std::vector<uint8_t> info, encrypted;
    if (cfb.OpenFromMemory(file) && cfb.ReadStream("EncryptionInfo", info) &&
        cfb.ReadStream("EncryptedPackage", encrypted)) {
        Check(DecryptOfficePackage(info, encrypted, "wrong", back, error) ==
                  OfficeDecryptStatus::WrongPassword,
              "what it writes refuses a wrong password");
        encrypted[100] ^= 1;
        Check(DecryptOfficePackage(info, encrypted, kPassword, back, error) ==
                  OfficeDecryptStatus::Tampered,
              "and carries an HMAC that catches a changed byte");
        const std::string xml(info.begin() + 8, info.end());
        Check(Contains(xml, "hashAlgorithm=\"SHA512\"") && Contains(xml, "keyBits=\"256\"") &&
                  Contains(xml, "spinCount=\"100000\""),
              "it writes what Office 2013 writes: SHA-512, AES-256, 100000 rounds");
    }
}

void TestSaving() {
    std::cout << "\n=== Saving with a password ===\n";
    const fs::path dir = TempDir();
    const std::string xlsx = PathToUtf8(dir / "saved.xlsx");
    {
        UltraCanvasSpreadsheet book("Enc", 0, 0, 400, 300);
        book.LoadFromFile(Fixture("office-agile-encrypted.xlsx"), kPassword);
        Check(book.SaveToFile(xlsx, "new p\xC3\xA4ss"), "SaveToFile writes an .xlsx with a password");
        if (!book.GetLastError().empty()) std::cout << "         " << book.GetLastError() << "\n";
        Check(IsEncryptedOfficePackage(xlsx), "the file on disk is encrypted, not a ZIP");
        Check(!book.SaveToFile(PathToUtf8(dir / "saved.ods"), "new p\xC3\xA4ss") &&
                  Contains(book.GetLastError(), ".xlsx") && !fs::exists(dir / "saved.ods"),
              "an .ods with a password is refused, and nothing is written");
    }
    {
        UltraCanvasSpreadsheet book("Enc", 0, 0, 400, 300);
        Check(!book.LoadFromFile(xlsx, kPassword), "the old password no longer opens it");
        Check(book.LoadFromFile(xlsx, "new p\xC3\xA4ss") && TextOf(book, 1, 0) == "Widget" &&
                  NumberOf(book, 402, 0) == 2821.0,
              "the new one does, with the content intact");
    }
    {
        UltraCanvasSpreadsheet book("Plain", 0, 0, 400, 300);
        book.LoadFromFile(xlsx, "new p\xC3\xA4ss");
        const std::string plain = PathToUtf8(dir / "plain.xlsx");
        Check(book.SaveToFile(plain) && !IsEncryptedOfficePackage(plain),
              "without a password it saves an ordinary .xlsx");
    }

    const std::string docx = PathToUtf8(dir / "saved.docx");
    UCRichDocument doc;
    std::string error;
    UCWordDocumentIO::Load(Fixture("office-agile-encrypted.docx"), doc, error, kPassword);
    Check(UCWordDocumentIO::Save(docx, doc, error, kPassword) && IsEncryptedOfficePackage(docx),
          "UCWordDocumentIO::Save writes an encrypted .docx");
    UCRichDocument back;
    Check(UCWordDocumentIO::Load(docx, back, error, kPassword) &&
              Contains(back.ToPlainText(), "Paragraph 201"),
          "which loads back with its text");
    Check(!UCWordDocumentIO::Save(PathToUtf8(dir / "saved.odt"), doc, error, kPassword) &&
              Contains(error, ".docx"),
          "an .odt with a password is refused");
    const std::string viaLoader = PathToUtf8(dir / "loader.docx");
    Check(UltraCanvasFileLoader::SaveTextDocument(doc, viaLoader, error, kPassword) &&
              UltraCanvasFileLoader::LoadTextDocument(viaLoader, error, kPassword) != nullptr,
          "UltraCanvasFileLoader::SaveTextDocument saves one LoadTextDocument opens");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

} // namespace

int main() {
    TestDecryptor();
    TestSpreadsheet();
    TestDocument();
    TestSha1();
    TestCompoundFileWriter();
    TestEncryptor();
    TestSaving();
    std::cout << "\n" << (g_failures == 0 ? "All checks passed" : "FAILURES: ")
              << (g_failures == 0 ? "" : std::to_string(g_failures)) << "\n";
    return g_failures == 0 ? 0 : 1;
}
