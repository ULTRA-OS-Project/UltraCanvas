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
//     calls straight back for one.
// The fixtures are in Tests/fixtures (make-office-encrypted-fixtures.py
// rebuilds them and says what each holds).
// Version: 1.0.0
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
    Check(DecryptOfficePackage(withHeader("hashAlgorithm=\"SHA512\"", "hashAlgorithm=\"SHA1\""),
                               encrypted, kPassword, package, error) ==
              OfficeDecryptStatus::Unsupported,
          "SHA-1 key derivation is Unsupported");
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

} // namespace

int main() {
    TestDecryptor();
    TestSpreadsheet();
    TestDocument();
    std::cout << "\n" << (g_failures == 0 ? "All checks passed" : "FAILURES: ")
              << (g_failures == 0 ? "" : std::to_string(g_failures)) << "\n";
    return g_failures == 0 ? 0 : 1;
}
