// Tests/OdfEncryptionTest.cpp
// UltraCanvasOdfEncryption: password-protected OpenDocument files, as
// LibreOffice saves them "with password" (make-odf-encrypted-fixtures.py):
//   * telling an encrypted package from an ordinary one;
//   * decrypting it - no password, a wrong one, the right one (non-ASCII, so
//     the UTF-8 start key) - into a package with mimetype first and a manifest
//     without encryption data, and CheckOdfPassword / CheckOfficePassword on it;
//   * the readers: UCWordDocumentIO::Load on the .odt (the same text as the
//     unencrypted original), UltraCanvasSpreadsheet::LoadFromFile on the .ods,
//     ReadOdpPresentation on the .odp (the same slides), and
//     UltraCanvasFileLoader::LoadTextDocument / ExtractText with the password;
//     UltraFiler's preview keeping the type glyph for each;
//   * ODF 1.4 as LibreOffice writes it in experimental mode
//     (odf14-encrypted.*): the whole package in one entry, AES-256-GCM,
//     Argon2id with 4 lanes - every reader, a wrong password, a changed byte;
//   * a byte changed past the checksummed kilobyte (damaged), Blowfish
//     refused by name, an Argon2id cost past the limit refused, missing
//     parameters damaged.
// Version: 1.1.0 - ODF 1.4: the whole package, AES-GCM, Argon2id (odf14-encrypted.*)
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasOdfEncryption.h"
#include "UltraCanvasOfficeEncryption.h"
#include "UltraCanvasPresentationOdp.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasZipPackage.h"
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"

#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#ifndef ODFCRYPTTEST_FIXTURE_DIR
#error "ODFCRYPTTEST_FIXTURE_DIR must name Tests/fixtures"
#endif

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string Fixture(const char* name) { return std::string(ODFCRYPTTEST_FIXTURE_DIR) + "/" + name; }

// The Office fixtures' password: non-ASCII, so the start key's UTF-8 counts.
const std::string kPassword = "\xC3\x9Cltra-P\xC3\xA4ssw\xC3\xB6rt 1";

// The package `path` holds, rebuilt with `change` applied to each entry.
std::vector<uint8_t> Rebuilt(const std::string& path,
                             const std::function<void(const std::string&, std::string&)>& change) {
    UCZipPackageReader source;
    UCZipPackageWriter out;
    out.OpenInMemory();
    if (!source.Open(path)) return {};
    for (const std::string& name : source.EntryNames()) {
        std::string data;
        source.ReadEntry(name, data);
        change(name, data);
        out.AddEntry(name, data, name != "mimetype");
    }
    out.Finalize();
    return out.TakeBytes();
}

OfficeDecryptStatus DecryptBytes(std::vector<uint8_t> bytes, const std::string& password, std::string& error) {
    UCZipPackageReader zip;
    std::vector<uint8_t> plain;
    if (!zip.OpenFromMemory(std::move(bytes))) {
        error = "the rebuilt package does not open";
        return OfficeDecryptStatus::Damaged;
    }
    return DecryptOdfPackage(zip, password, plain, error);
}

void Replace(std::string& text, const std::string& from, const std::string& to) {
    for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
        text.replace(at, from.size(), to);
}

void TestPackage() {
    std::cout << "\n=== The encrypted package ===\n";
    const std::string odt = Fixture("odf-encrypted.odt");
    Check(IsEncryptedOdfPackage(odt) && IsEncryptedOdfPackage(Fixture("odf-encrypted.ods")) &&
              IsEncryptedOdfPackage(Fixture("odf-encrypted.odp")),
          "LibreOffice's protected .odt, .ods and .odp are told");
    Check(!IsEncryptedOdfPackage(Fixture("word97-formatting.odt")) &&
              !IsEncryptedOdfPackage(Fixture("office-pptx-sample.pptx")) &&
              !IsEncryptedOdfPackage(Fixture("office-agile-encrypted.docx")) &&
              !IsEncryptedOdfPackage(Fixture("does-not-exist.odt")),
          "an ordinary .odt, a .pptx, a protected .docx and a missing file are not");

    std::vector<uint8_t> package;
    std::string error;
    Check(DecryptOdfPackageFile(odt, "", package, error) == OfficeDecryptStatus::PasswordRequired &&
              Contains(error, "password-protected") && package.empty(),
          "without a password: PasswordRequired (\"" + error + "\")");
    Check(DecryptOdfPackageFile(odt, "wrong", package, error) == OfficeDecryptStatus::WrongPassword &&
              Contains(error, "not correct") && package.empty(),
          "a wrong password: WrongPassword");
    Check(DecryptOdfPackageFile(odt, "\xC3\x9Cltra-P\xC3\xA4ssw\xC3\xB6rt 2", package, error) ==
              OfficeDecryptStatus::WrongPassword,
          "a password one character off: WrongPassword");
    Check(DecryptOdfPackageFile(Fixture("word97-formatting.odt"), kPassword, package, error) ==
              OfficeDecryptStatus::NotEncrypted,
          "an ordinary .odt: NotEncrypted");

    const auto started = std::chrono::steady_clock::now();
    const OfficeDecryptStatus status = DecryptOdfPackageFile(odt, kPassword, package, error);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    Check(status == OfficeDecryptStatus::Decrypted && !package.empty(),
          "the right password decrypts it (" + std::to_string(ms.count()) + " ms)");
    UCZipPackageReader plain;
    std::string content, manifest, mimetype;
    Check(plain.OpenFromMemory(package) && plain.ReadEntry("content.xml", content) &&
              Contains(content, "office:document-content") && plain.ReadEntry("META-INF/manifest.xml", manifest) &&
              !Contains(manifest, "encryption-data") && plain.ReadEntry("mimetype", mimetype) &&
              mimetype == "application/vnd.oasis.opendocument.text" && plain.EntryNames().front() == "mimetype",
          "an ordinary package again: content.xml readable, mimetype first, no encryption data");
    Check(!IsEncryptedOdfPackage(plain), "... which is no longer an encrypted one");

    Check(CheckOdfPassword(odt, kPassword, error) == OfficeDecryptStatus::Decrypted &&
              CheckOdfPassword(odt, "wrong", error) == OfficeDecryptStatus::WrongPassword &&
              CheckOdfPassword(odt, "", error) == OfficeDecryptStatus::PasswordRequired,
          "CheckOdfPassword: right, wrong, none");
    Check(CheckOfficePassword(Fixture("odf-encrypted.ods"), kPassword, error) == OfficeDecryptStatus::Decrypted &&
              CheckOfficePassword(Fixture("odf-encrypted.odp"), "", error) ==
                  OfficeDecryptStatus::PasswordRequired &&
              CheckOfficePassword(Fixture("word97-formatting.odt"), "", error) == OfficeDecryptStatus::NotEncrypted,
          "CheckOfficePassword answers for OpenDocument (the password prompt's check)");
    Check(CheckOfficePassword(Fixture("office-agile-encrypted.docx"), kPassword, error) ==
              OfficeDecryptStatus::Decrypted,
          "... and still for Office");
}

void TestReaders() {
    std::cout << "\n=== The readers ===\n";
    std::string error;
    UCRichDocument original, opened;
    UCWordDocumentIO::Load(Fixture("word97-formatting.odt"), original, error);
    Check(!UCWordDocumentIO::Load(Fixture("odf-encrypted.odt"), opened, error) && Contains(error, "password-protected"),
          "UCWordDocumentIO::Load without the password says so");
    const bool loaded = UCWordDocumentIO::Load(Fixture("odf-encrypted.odt"), opened, error, kPassword);
    Check(loaded && !original.ToPlainText().empty() && opened.ToPlainText() == original.ToPlainText(),
          "... with it, the .odt reads as the unencrypted original");
    if (!loaded) std::cout << "         " << error << "\n";

    {
        UltraCanvasSpreadsheet sheet("Protected", 0, 0, 400, 300);
        Check(!sheet.LoadFromFile(Fixture("odf-encrypted.ods")) && Contains(sheet.GetLastError(), "password-protected"),
              "UltraCanvasSpreadsheet::LoadFromFile without the password says so");
        Check(!sheet.LoadFromFile(Fixture("odf-encrypted.ods"), "wrong") &&
                  Contains(sheet.GetLastError(), "not correct"),
              "... a wrong one too");
        const bool ok = sheet.LoadFromFile(Fixture("odf-encrypted.ods"), kPassword);
        Check(ok && sheet.GetCellText(0, 0) == "Name" && sheet.GetCellText(1, 0) == "Widget" &&
                  sheet.GetCellDisplayValue(1, 1) == "42" && sheet.GetCellText(2, 0) == "Gadget",
              "... with it, the .ods's cells");
        if (!ok) std::cout << "         " << sheet.GetLastError() << "\n";
    }

    PptxPresentation plainDeck, deck;
    ReadOdpPresentation(Fixture("office-pptx-sample.odp"), plainDeck, error);
    Check(!ReadOdpPresentation(Fixture("odf-encrypted.odp"), deck, error) && Contains(error, "password-protected"),
          "ReadOdpPresentation without the password says so");
    Check(!ReadOdpPresentation(Fixture("odf-encrypted.odp"), deck, error, "wrong") && Contains(error, "not correct"),
          "... a wrong one too");
    const bool deckOk = ReadOdpPresentation(Fixture("odf-encrypted.odp"), deck, error, kPassword);
    Check(deckOk && deck.slides.size() == 5 && deck.slides.size() == plainDeck.slides.size() &&
              PptxPresentationToRichDocument(deck).ToPlainText() ==
                  PptxPresentationToRichDocument(plainDeck).ToPlainText(),
          "... with it, the .odp's slides, as the unencrypted original's");

    auto document = UltraCanvasFileLoader::LoadTextDocument(Fixture("odf-encrypted.odp"), error, kPassword);
    Check(document && Contains(document->ToPlainText(), "Remember the Q3 numbers."),
          "UltraCanvasFileLoader::LoadTextDocument opens the .odp with the password");
    document = UltraCanvasFileLoader::LoadTextDocument(Fixture("odf-encrypted.odt"), error, kPassword);
    Check(document && document->ToPlainText() == original.ToPlainText(), "... and the .odt");
    document = UltraCanvasFileLoader::LoadTextDocument(Fixture("odf-encrypted.odt"), error);
    Check(!document && Contains(error, "password-protected"), "... and without it says why");
    const auto extracted = UltraCanvasFileLoader::ExtractText(Fixture("odf-encrypted.odp"), kPassword);
    Check(extracted.Ok() && Contains(extracted.text, "Item 60"), "ExtractText reads the .odp with the password");

    std::vector<std::string> lines;
    bool tabular = false;
    Check(!UltraCanvasFilerWidget::TextPreviewLines(Fixture("odf-encrypted.odt"), lines, tabular) &&
              !UltraCanvasFilerWidget::TextPreviewLines(Fixture("odf-encrypted.ods"), lines, tabular) &&
              !UltraCanvasFilerWidget::TextPreviewLines(Fixture("odf-encrypted.odp"), lines, tabular),
          "UltraFiler's preview: no page for a protected .odt, .ods or .odp - each keeps its glyph");
}

void TestDamageAndRefusals() {
    std::cout << "\n=== Damage, and what is refused ===\n";
    const std::string odt = Fixture("odf-encrypted.odt");
    std::string error;

    // A byte of content.xml past the kilobyte the checksum covers.
    const auto flipped = Rebuilt(odt, [](const std::string& name, std::string& data) {
        if (name == "content.xml" && data.size() > 2048) data[data.size() - 40] ^= 0x5A;
    });
    const OfficeDecryptStatus damaged = DecryptBytes(flipped, kPassword, error);
    Check(damaged == OfficeDecryptStatus::Damaged || damaged == OfficeDecryptStatus::WrongPassword,
          "a changed byte past the checksummed kilobyte: refused (\"" + error + "\")");
    Check(damaged == OfficeDecryptStatus::Damaged, "... as damaged, the password being right");

    auto refused = [&](const std::string& from, const std::string& to, const std::string& word, const std::string& what) {
        const auto bytes = Rebuilt(odt, [&](const std::string& name, std::string& data) {
            if (name == "META-INF/manifest.xml") Replace(data, from, to);
        });
        std::string why;
        const OfficeDecryptStatus status = DecryptBytes(bytes, kPassword, why);
        Check(status == OfficeDecryptStatus::Unsupported && Contains(why, word), what + " (\"" + why + "\")");
    };
    refused("http://www.w3.org/2001/04/xmlenc#aes256-cbc", "Blowfish CFB", "Blowfish",
            "Blowfish (OpenOffice.org 1 to 3.1) is refused by name");

    // ODF 1.4: an Argon2id cost past the limit is refused before any memory
    // is taken; its parameters missing are a damaged file.
    auto odf14 = [&](const std::string& from, const std::string& to, OfficeDecryptStatus want, const std::string& word,
                     const std::string& what) {
        const auto bytes = Rebuilt(Fixture("odf14-encrypted.odt"), [&](const std::string& name, std::string& data) {
            if (name == "META-INF/manifest.xml") Replace(data, from, to);
        });
        std::string why;
        const OfficeDecryptStatus status = DecryptBytes(bytes, kPassword, why);
        Check(status == want && Contains(why, word), what + " (\"" + why + "\")");
    };
    odf14("argon2-memory=\"65536\"", "argon2-memory=\"4194304\"", OfficeDecryptStatus::Unsupported, "Argon2id",
          "Argon2id asking for 4 GiB is refused");
    odf14("argon2-lanes=\"4\"", "argon2-lane=\"4\"", OfficeDecryptStatus::Damaged, "Argon2id",
          "Argon2id without its lanes is damaged");
    odf14("manifest:encryption-data", "manifest:not-encryption-data", OfficeDecryptStatus::Damaged, "no encryption data",
          "a whole package without encryption data is damaged");
}

// ODF 1.4, as LibreOffice 24.2 writes it in experimental mode: the whole
// package in one entry, AES-256-GCM, Argon2id with 4 lanes over 64 MiB.
void TestOdf14() {
    std::cout << "\n=== ODF 1.4: the whole package, AES-GCM, Argon2id ===\n";
    const std::string odt = Fixture("odf14-encrypted.odt");
    Check(IsEncryptedOdfPackage(odt) && IsEncryptedOdfPackage(Fixture("odf14-encrypted.ods")) &&
              IsEncryptedOdfPackage(Fixture("odf14-encrypted.odp")),
          "the three are told as encrypted");
    std::vector<uint8_t> package;
    std::string error;
    Check(DecryptOdfPackageFile(odt, "", package, error) == OfficeDecryptStatus::PasswordRequired,
          "without a password: PasswordRequired");
    Check(DecryptOdfPackageFile(odt, "wrong", package, error) == OfficeDecryptStatus::WrongPassword &&
              Contains(error, "not correct") && package.empty(),
          "a wrong password: WrongPassword, nothing released");
    const auto started = std::chrono::steady_clock::now();
    const OfficeDecryptStatus status = DecryptOdfPackageFile(odt, kPassword, package, error);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    Check(status == OfficeDecryptStatus::Decrypted,
          "the right password decrypts it (" + std::to_string(ms.count()) + " ms, Argon2id 64 MiB x 4 lanes)");
    UCZipPackageReader plain;
    std::string content, mimetype, manifest;
    Check(plain.OpenFromMemory(package) && plain.ReadEntry("content.xml", content) &&
              Contains(content, "office:document-content") && plain.ReadEntry("mimetype", mimetype) &&
              mimetype == "application/vnd.oasis.opendocument.text" &&
              plain.ReadEntry("META-INF/manifest.xml", manifest) && !Contains(manifest, "encryption-data"),
          "the inner package: its own mimetype, content and an unencrypted manifest");
    Check(CheckOdfPassword(odt, kPassword, error) == OfficeDecryptStatus::Decrypted &&
              CheckOdfPassword(odt, "wrong", error) == OfficeDecryptStatus::WrongPassword &&
              CheckOfficePassword(Fixture("odf14-encrypted.odp"), "", error) == OfficeDecryptStatus::PasswordRequired,
          "CheckOdfPassword and CheckOfficePassword (the prompt's check)");

    UCRichDocument original, opened;
    UCWordDocumentIO::Load(Fixture("word97-formatting.odt"), original, error);
    Check(UCWordDocumentIO::Load(odt, opened, error, kPassword) && opened.ToPlainText() == original.ToPlainText(),
          "UCWordDocumentIO::Load: the .odt as the unencrypted original");
    {
        UltraCanvasSpreadsheet sheet("Protected14", 0, 0, 400, 300);
        Check(sheet.LoadFromFile(Fixture("odf14-encrypted.ods"), kPassword) && sheet.GetCellText(1, 0) == "Widget" &&
                  sheet.GetCellDisplayValue(1, 1) == "42",
              "UltraCanvasSpreadsheet::LoadFromFile: the .ods's cells");
    }
    PptxPresentation plainDeck, deck;
    ReadOdpPresentation(Fixture("office-pptx-sample.odp"), plainDeck, error);
    Check(ReadOdpPresentation(Fixture("odf14-encrypted.odp"), deck, error, kPassword) && deck.slides.size() == 5 &&
              PptxPresentationToRichDocument(deck).ToPlainText() ==
                  PptxPresentationToRichDocument(plainDeck).ToPlainText(),
          "ReadOdpPresentation: the .odp's slides as the original's");
    auto document = UltraCanvasFileLoader::LoadTextDocument(Fixture("odf14-encrypted.odp"), error, kPassword);
    Check(document && Contains(document->ToPlainText(), "Item 60"), "LoadTextDocument with the password");

    // The tag covers the whole package: one changed byte and nothing opens.
    const auto flipped = Rebuilt(odt, [](const std::string& name, std::string& data) {
        if (name == "encrypted-package" && data.size() > 100) data[data.size() / 2] ^= 0x01;
    });
    Check(DecryptBytes(flipped, kPassword, error) == OfficeDecryptStatus::WrongPassword,
          "one changed byte fails the GCM tag - refused, nothing released");
}

} // namespace

int main() {
    TestPackage();
    TestReaders();
    TestOdf14();
    TestDamageAndRefusals();
    std::cout << "\n" << (g_failures == 0 ? "All checks passed" : "FAILURES: ")
              << (g_failures == 0 ? "" : std::to_string(g_failures)) << "\n";
    return g_failures == 0 ? 0 : 1;
}
