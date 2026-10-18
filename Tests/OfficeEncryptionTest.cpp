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
//   * Office 2007's standard encryption (AES-128-ECB, SHA-1, no HMAC);
//   * the PowerPoint reader on a deck python-pptx wrote, plain and encrypted:
//     slide order and size, titles, bullets and levels, numbering, a text
//     box, a table, a picture, notes, a hidden slide, the outline, and
//     LoadTextDocument opening it, and UltraFiler's preview page showing its
//     slide text (a protected deck or .docx keeping its type glyph);
//   * writing: UCCompoundFileWriter read back, EncryptOfficePackage round
//     trips (a package in the mini stream, one past the header's 109 FAT
//     sectors), fresh salts on every save, a changed byte caught, and the
//     spreadsheet, UCWordDocumentIO and UltraCanvasFileLoader saving with a
//     password and loading it back - and refusing a password for .ods / .odt.
// The fixtures are in Tests/fixtures (make-office-encrypted-fixtures.py
// rebuilds them and says what each holds). That msoffcrypto-tool decrypts
// what EncryptOfficePackage writes was checked when the writer was made; the
// test cannot run Python, so it checks the format against the reader here.
// Version: 1.4.0
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework

#include "UltraCanvasCompoundFile.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasOfficeEncryption.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasZipPackage.h"
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"

#include <algorithm>
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

    // The header: another version, damage, an absurd cost.
    {
        std::vector<uint8_t> extensible = info;
        extensible[2] = 3;   // version 4.3
        Check(DecryptOfficePackage(extensible, encrypted, kPassword, package, error) ==
                      OfficeDecryptStatus::Unsupported &&
                  Contains(error, "extensible"),
              "extensible encryption (4.3) is Unsupported, by name");
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

// ===== OFFICE 2007: STANDARD ENCRYPTION =====

void TestStandard() {
    std::cout << "\n=== Office 2007's standard encryption ===\n";
    const std::string xlsx = Fixture("office-standard-encrypted.xlsx");
    std::vector<uint8_t> package;
    std::string error;
    Check(IsEncryptedOfficePackage(xlsx), "a standard-encrypted workbook is recognised");
    Check(DecryptOfficePackageFile(xlsx, kPassword, package, error) == OfficeDecryptStatus::Decrypted,
          "the right password decrypts it");
    if (!error.empty()) std::cout << "         " << error << "\n";
    UCZipPackageReader zip;
    std::string sheetXml;
    Check(zip.OpenFromMemory(package) && zip.ReadEntry("xl/worksheets/sheet1.xml", sheetXml) &&
              Contains(sheetXml, "<c r=\"A403\"><v>2821</v></c>"),
          "into the ZIP package, to its last block");
    Check(DecryptOfficePackageFile(xlsx, "wrong", package, error) == OfficeDecryptStatus::WrongPassword &&
              package.empty(),
          "a wrong password is WrongPassword");
    Check(DecryptOfficePackageFile(xlsx, "", package, error) == OfficeDecryptStatus::PasswordRequired,
          "no password is PasswordRequired");
    Check(CheckOfficePassword(xlsx, kPassword, error) == OfficeDecryptStatus::Decrypted &&
              CheckOfficePassword(xlsx, "wrong", error) == OfficeDecryptStatus::WrongPassword,
          "CheckOfficePassword tells the two apart");

    std::vector<uint8_t> info, encrypted;
    if (ReadStreams(xlsx, info, encrypted)) {
        std::vector<uint8_t> rc4 = info;
        rc4[4] &= static_cast<uint8_t>(~0x20);   // fAES off: RC4 CryptoAPI
        Check(DecryptOfficePackage(rc4, encrypted, kPassword, package, error) ==
                      OfficeDecryptStatus::Unsupported && Contains(error, "RC4"),
              "RC4 standard encryption is Unsupported, by name");
        std::vector<uint8_t> unknownCipher = info;
        unknownCipher[12 + 8] = 0x01;   // AlgID 0x6601 (DES)
        Check(DecryptOfficePackage(unknownCipher, encrypted, kPassword, package, error) ==
                  OfficeDecryptStatus::Unsupported,
              "a cipher other than AES is Unsupported");
        std::vector<uint8_t> cut(info.begin(), info.begin() + 40);
        Check(DecryptOfficePackage(cut, encrypted, kPassword, package, error) ==
                  OfficeDecryptStatus::Damaged,
              "a truncated header is Damaged");
        std::vector<uint8_t> shortPackage(encrypted.begin(), encrypted.end() - 5);
        Check(DecryptOfficePackage(info, shortPackage, kPassword, package, error) ==
                  OfficeDecryptStatus::Damaged,
              "a package that is not whole blocks is Damaged");
    }
    UltraCanvasSpreadsheet book("Std", 0, 0, 400, 300);
    Check(book.LoadFromFile(xlsx, kPassword) && TextOf(book, 1, 0) == "Widget" &&
              NumberOf(book, 402, 0) == 2821.0,
          "the spreadsheet opens it with its content");
}

// ===== POWERPOINT =====

const PptxShape* FindShape(const PptxSlide& slide, PptxShapeKind kind) {
    for (const PptxShape& shape : slide.shapes)
        if (shape.kind == kind) return &shape;
    return nullptr;
}

void TestPresentation() {
    std::cout << "\n=== PowerPoint (.pptx) ===\n";
    const std::string plain = Fixture("office-pptx-sample.pptx");
    Check(IsPptxPackage(plain) && !IsPptxPackage(Fixture("word97-formatting.docx")),
          "a deck is told from a Word package");
    Check(IsPptxExtension("deck.PPTX") && IsPptxExtension("ppsx") && !IsPptxExtension("x.docx"),
          "the PowerPoint extensions are known");

    PptxPresentation deck;
    std::string error;
    const bool ok = ReadPptxPresentation(plain, deck, error);
    Check(ok, "ReadPptxPresentation reads it");
    if (!ok) {
        std::cout << "         " << error << "\n";
        return;
    }
    Check(deck.slideWidth == 12192000 && deck.slideHeight == 6858000, "16:9 slide size");
    Check(deck.slides.size() == 5, "five slides, in order");
    if (deck.slides.size() != 5) return;
    Check(deck.slides[0].title == "Quarterly Review" && deck.slides[1].title == "Agenda" &&
              deck.slides[4].title == "Appendix",
          "each slide's title");
    const PptxShape* subtitle = FindShape(deck.slides[0], PptxShapeKind::Subtitle);
    Check(subtitle && subtitle->paragraphs.size() == 1 && subtitle->paragraphs[0].Text() == "October 2026",
          "the title slide's subtitle");

    const PptxShape* body = FindShape(deck.slides[1], PptxShapeKind::Body);
    Check(body && body->paragraphs.size() == 6, "the agenda's six paragraphs");
    if (body && body->paragraphs.size() == 6) {
        const auto& p = body->paragraphs;
        Check(p[0].Text() == "Results" && p[0].bullet && p[0].level == 0 && p[0].runs[0].bold,
              "a bold top-level bullet");
        Check(p[1].Text() == "Revenue up" && p[1].level == 1 && p[2].level == 1, "two level-2 bullets");
        Check(p[4].Text() == "Hire" && p[4].numbered && p[5].numbered && !p[3].numbered,
              "a numbered pair after a plain bullet");
    }
    const PptxShape* box = FindShape(deck.slides[1], PptxShapeKind::Text);
    Check(box && box->paragraphs[0].Text() == "Confidential" && !box->paragraphs[0].bullet &&
              box->x == 914400,
          "the text box, unbulleted, at its place (1 inch)");
    Check(deck.slides[1].notes.size() == 1 && deck.slides[1].notes[0].Text() == "Remember the Q3 numbers.",
          "the speaker notes");

    const PptxShape* table = FindShape(deck.slides[2], PptxShapeKind::Table);
    Check(table && table->rows.size() == 2 && table->rows[1].size() == 2 &&
              table->rows[1][1][0].Text() == "1.2M",
          "the table's cells");
    const PptxShape* picture = FindShape(deck.slides[2], PptxShapeKind::Picture);
    Check(picture && picture->altText == "Revenue chart" &&
              picture->pictureTarget.rfind("ppt/media/", 0) == 0,
          "the picture's description and part");
    Check(deck.slides[3].hidden && !deck.slides[2].hidden, "the hidden slide is marked");
    const PptxShape* appendix = FindShape(deck.slides[4], PptxShapeKind::Body);
    Check(appendix && appendix->paragraphs.size() == 60 && appendix->paragraphs[59].Text() == "Item 60",
          "sixty bullets on the last slide");
    for (const PptxSlide& slide : deck.slides)
        for (const PptxShape& shape : slide.shapes)
            for (const PptxParagraph& paragraph : shape.paragraphs)
                if (paragraph.Text() == "1" || paragraph.Text() == "<#>")
                    Check(false, "no slide-number placeholder text on slide " + std::to_string(slide.number));

    const UCRichDocument outline = PptxPresentationToRichDocument(deck);
    const std::string text = outline.ToPlainText();
    Check(Contains(text, "1. Quarterly Review") && Contains(text, "4. Backup (hidden)") &&
              Contains(text, "Revenue up") && Contains(text, "Remember the Q3 numbers.") &&
              Contains(text, "[Picture: Revenue chart]"),
          "the outline holds headings, bullets, notes and the picture");
    size_t listItems = 0, headings = 0, tables = 0;
    for (const RichDocBlock& block : outline.blocks) {
        listItems += block.type == RichBlockType::ListItem;
        headings += block.type == RichBlockType::Heading;
        tables += block.type == RichBlockType::Table;
    }
    Check(headings == 5 && tables == 1 && listItems >= 66, "one heading per slide, the table, the bullets");

    // Encrypted: the same deck behind the password.
    const std::string encrypted = Fixture("office-agile-encrypted.pptx");
    PptxPresentation locked;
    Check(!ReadPptxPresentation(encrypted, locked, error) && Contains(error, "password-protected"),
          "a protected deck without its password says so");
    Check(ReadPptxPresentation(encrypted, locked, error, kPassword) && locked.slides.size() == 5 &&
              locked.slides[1].notes.size() == 1,
          "with the password it reads the same");
    auto loaded = UltraCanvasFileLoader::LoadTextDocument(encrypted, error, kPassword);
    Check(loaded && Contains(loaded->ToPlainText(), "Item 60"),
          "UltraCanvasFileLoader::LoadTextDocument opens the protected deck");
    auto plainLoaded = UltraCanvasFileLoader::LoadTextDocument(plain, error);
    Check(plainLoaded && Contains(plainLoaded->ToPlainText(), "2. Agenda"),
          "and the plain one, as an outline");

    // UltraFiler's preview page.
    std::vector<std::string> lines;
    bool tabular = true;
    auto hasLine = [&](const std::string& wanted) {
        return std::find(lines.begin(), lines.end(), wanted) != lines.end();
    };
    Check(UltraCanvasFilerWidget::TextPreviewLines(plain, lines, tabular) && !tabular &&
              !lines.empty() && lines.front() == "1. Quarterly Review" && hasLine("Revenue up") &&
              hasLine("2. Agenda"),
          "the preview page shows the deck's slides, titles first");
    Check(!UltraCanvasFilerWidget::TextPreviewLines(encrypted, lines, tabular),
          "a protected deck has no preview page: it keeps its type glyph");
    Check(!UltraCanvasFilerWidget::TextPreviewLines(Fixture("office-agile-encrypted.docx"), lines, tabular),
          "nor has a protected .docx");
    Check(UltraCanvasFilerWidget::TextPreviewLines(Fixture("word97-formatting.docx"), lines, tabular) &&
              !lines.empty(),
          "an ordinary .docx still previews");
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
    TestStandard();
    TestPresentation();
    TestCompoundFileWriter();
    TestEncryptor();
    TestSaving();
    std::cout << "\n" << (g_failures == 0 ? "All checks passed" : "FAILURES: ")
              << (g_failures == 0 ? "" : std::to_string(g_failures)) << "\n";
    return g_failures == 0 ? 0 : 1;
}
