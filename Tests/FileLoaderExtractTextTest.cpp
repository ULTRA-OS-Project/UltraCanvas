// Tests/FileLoaderExtractTextTest.cpp
// UltraCanvasFileLoader::ExtractText - the words in a file as plain text:
//
//   - a text file as it is (byte-order mark and CR LF gone), also a type the
//     inventory does not know when it reads as UTF-8; a binary file has none;
//   - a word processor document through LoadTextDocument (a DOCX written by
//     SaveTextDocument), a spreadsheet as tab-separated rows, a PDF's text;
//   - a picture through the installed recognizer, as 3-byte pixels on white -
//     and, with no recognizer, CanExtractText false and a reason to show;
//   - a recording through the installed transcriber, the same way;
//   - with the OCR plugin built, Tesseract itself reads media/images/OCR-demo.png
//     (RegisterOCRTextRecognition), and RecognizeFile reads it by a Thai name
//     and as dark text on a transparent picture.
//
// The files sit in a folder with a Thai name (UTF-8 paths). Headless.
// Version: 1.2.0 - the language data in a folder with a Thai name
// Version: 1.1.0 - RecognizeFile: a UTF-8 name, a transparent picture
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasRichDocument.h"
#include "UltraCanvasSpreadsheet.h"
#ifdef ULTRACANVAS_OCR_SUPPORT
#include "UltraCanvasOCRPlugin.h"
#endif

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace UltraCanvas;
using Method = UltraCanvasFileLoader::TextExtractionMethod;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

bool WriteFile(const fs::path& path, const std::string& bytes) {
    std::FILE* file = OpenFileUtf8(PathToUtf8(path), "wb");
    if (!file) return false;
    const bool written = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    return std::fclose(file) == 0 && written;
}

} // namespace

int main() {
    std::cout << "FileLoaderExtractTextTest" << std::endl;
    std::error_code ec;
    const fs::path folder = fs::temp_directory_path(ec) / PathFromUtf8("ExtractText-\xE0\xB8\x97\xE0\xB8\x94\xE0\xB8\xAA\xE0\xB8\xAD\xE0\xB8\x9A");
    fs::remove_all(folder, ec);
    fs::create_directories(folder, ec);
    const std::string mediaDir = UC_MEDIA_DIR;

    // ----- text -----
    const fs::path note = folder / PathFromUtf8("note.txt");
    WriteFile(note, "\xEF\xBB\xBFLine one\r\nLine two\r\n\r\n");
    auto result = UltraCanvasFileLoader::ExtractText(PathToUtf8(note));
    Check(result.Ok() && result.method == Method::PlainText && result.text == "Line one\nLine two",
          "a text file: as it is, without the byte-order mark, CR LF or trailing blank lines");

    const fs::path code = folder / PathFromUtf8("main.cpp");
    WriteFile(code, "int main() { return 0; } // \xC3\xA4\n");
    Check(UltraCanvasFileLoader::GetTextExtractionMethod(PathToUtf8(code)) == Method::PlainText,
          "a type the inventory does not know, reading as UTF-8: text");

    const fs::path binary = folder / PathFromUtf8("data.bin");
    WriteFile(binary, std::string("\x00\x01\x02\xFF\xFE", 5));
    Check(UltraCanvasFileLoader::GetTextExtractionMethod(PathToUtf8(binary)) == Method::NoText &&
          !UltraCanvasFileLoader::CanExtractText(PathToUtf8(binary)) &&
          !UltraCanvasFileLoader::WhyNoTextExtraction(PathToUtf8(binary)).empty(),
          "a binary file: no text, and a reason to say so");

    // ----- a document -----
    const fs::path docx = folder / PathFromUtf8("letter.docx");
    std::string error;
    const UCRichDocument letter = UCRichDocument::FromMarkdown("# Dear reader\n\nThe quick brown fox.\n", "");
    Check(UltraCanvasFileLoader::SaveTextDocument(letter, PathToUtf8(docx), error), "a DOCX to read is written");
    result = UltraCanvasFileLoader::ExtractText(PathToUtf8(docx));
    Check(result.Ok() && result.method == Method::Document && Contains(result.text, "Dear reader") &&
          Contains(result.text, "The quick brown fox."),
          "a DOCX: its words, through LoadTextDocument");

    // ----- a spreadsheet -----
    const fs::path xlsx = folder / PathFromUtf8("figures.xlsx");
    {
        UltraCanvasSpreadsheet sheet;
        sheet.SetCellValue(0, 0, std::string("Item"));
        sheet.SetCellValue(0, 1, std::string("Price"));
        sheet.SetCellValue(1, 0, std::string("Apple"));
        sheet.SetCellValue(1, 1, std::string("1.25"));
        Check(sheet.SaveToFile(PathToUtf8(xlsx)), "an XLSX to read is written");
    }
    result = UltraCanvasFileLoader::ExtractText(PathToUtf8(xlsx));
    Check(result.Ok() && result.method == Method::Spreadsheet && Contains(result.text, "Item\tPrice") &&
          Contains(result.text, "Apple\t1.25"),
          "an XLSX: its rows, tab-separated");

    // ----- a PDF -----
#ifdef ULTRACANVAS_PLUGIN_PDF
    result = UltraCanvasFileLoader::ExtractText(mediaDir + "/sample.pdf");
    Check(result.Ok() && result.method == Method::PDF && !result.text.empty(), "media/sample.pdf: its text");
#endif

    // ----- a picture -----
    const std::string picture = mediaDir + "/images/OCR-demo.png";
    UltraCanvasFileLoader::SetImageTextRecognizer(nullptr);
    Check(UltraCanvasFileLoader::GetTextExtractionMethod(picture) == Method::Recognition &&
          !UltraCanvasFileLoader::CanExtractText(picture) &&
          Contains(UltraCanvasFileLoader::WhyNoTextExtraction(picture), "OCR"),
          "a picture without a recognizer: Recognition, not possible, and why");
    int seenWidth = 0, seenBytes = 0;
    bool seenWhite = false;
    UltraCanvasFileLoader::SetImageTextRecognizer(
            [&](const UltraCanvasFileLoader::TextRecognitionImage& image, std::string& text, std::string&) {
        seenWidth = image.width;
        seenBytes = image.bytesPerPixel;
        seenWhite = image.pixels && image.pixels[0] > 200;   // the corner is (almost) white paper
        text = "Recognised\r\n";
        return true;
    });
    result = UltraCanvasFileLoader::ExtractText(picture);
    Check(result.Ok() && result.method == Method::Recognition && result.text == "Recognised",
          "a picture with a recognizer: its text, tidied");
    Check(seenWidth == 720 && seenBytes == 3 && seenWhite, "... handed over as RGB pixels, at its size");

    // ----- a recording -----
    const fs::path sound = folder / PathFromUtf8("memo.wav");
    WriteFile(sound, "RIFF");
    UltraCanvasFileLoader::SetSpeechTranscriber(nullptr);
    result = UltraCanvasFileLoader::ExtractText(PathToUtf8(sound));
    Check(!result.Ok() && result.method == Method::Transcription && Contains(result.error, "Speech to text"),
          "a recording without a transcriber: an error saying so");
    UltraCanvasFileLoader::SetSpeechTranscriber([](const std::string& path, std::string& text, std::string&) {
        text = "Hello from " + PathToUtf8(PathFromUtf8(path).filename());
        return true;
    });
    result = UltraCanvasFileLoader::ExtractText(PathToUtf8(sound));
    Check(result.Ok() && result.text == "Hello from memo.wav", "a recording with a transcriber: its words");
    UltraCanvasFileLoader::SetSpeechTranscriber(nullptr);

    // ----- the real recognizer -----
#ifdef ULTRACANVAS_OCR_SUPPORT
    RegisterOCRTextRecognition({"eng"});
    result = UltraCanvasFileLoader::ExtractText(picture);
    if (!result.Ok() && Contains(result.error, "traineddata")) {
        std::cout << "  [SKIP] Tesseract has no English data here: " << result.error << "\n";
    } else {
        Check(result.Ok() && Contains(result.text, "OCR demo") && Contains(result.text, "Receipt"),
              "Tesseract (RegisterOCRTextRecognition) reads OCR-demo.png");

        // RecognizeFile by a UTF-8 name (the Thai folder): read by the
        // name, not through the ANSI code page.
        const fs::path copy = folder / PathFromUtf8("\xE0\xB8\xA0\xE0\xB8\xB2\xE0\xB8\x9E.png");
        fs::copy_file(PathFromUtf8(picture), copy, fs::copy_options::overwrite_existing, ec);
        OCRConfig config;
        config.languages = {"eng"};
        UltraCanvasOCR ocr(config);
        const OCRResult byName = ocr.RecognizeFile(PathToUtf8(copy));
        Check(byName.Ok() && Contains(byName.plainText, "OCR demo"),
              "UltraCanvasOCR::RecognizeFile reads a picture in a folder with a Thai name");

        // The language data in a folder with a Thai name: Tesseract 5 reads
        // it through the engine's FileReader (OpenFileUtf8).
        fs::path english;
        for (const char* candidate : {"/usr/share/tesseract-ocr/5/tessdata", "/usr/share/tesseract-ocr/4.00/tessdata",
                                      "/usr/share/tessdata", "/usr/local/share/tessdata",
                                      "/opt/homebrew/share/tessdata"}) {
            if (fs::exists(fs::path(candidate) / "eng.traineddata", ec)) {
                english = fs::path(candidate) / "eng.traineddata";
                break;
            }
        }
        if (!english.empty()) {
            const fs::path data = folder / PathFromUtf8("\xE0\xB8\x82\xE0\xB9\x89\xE0\xB8\xAD\xE0\xB8\xA1\xE0\xB8\xB9\xE0\xB8\xA5");
            fs::create_directories(data, ec);
            fs::copy_file(english, data / "eng.traineddata", fs::copy_options::overwrite_existing, ec);
            OCRConfig thaiData;
            thaiData.languages = {"eng"};
            thaiData.dataPath = PathToUtf8(data);
            UltraCanvasOCR fromThaiFolder(thaiData);
            const OCRResult read = fromThaiFolder.RecognizeFile(picture);
            Check(read.Ok() && Contains(read.plainText, "OCR demo"),
                  "... with its language data in a folder with a Thai name");
        } else {
            std::cout << "  [SKIP] no eng.traineddata in the usual folders to copy\n";
        }

        // Dark text on a transparent picture: flattened onto white, not black.
        const std::shared_ptr<UCImage> clear = UCImage::Load(picture, false);
        const std::shared_ptr<UCPixmapCairo> pixmap = clear ? clear->GetPixmap() : nullptr;
        if (pixmap && pixmap->IsValid()) {
            pixmap->Flush();
            const int w = pixmap->GetRawWidth(), h = pixmap->GetRawHeight();
            std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
            const uint32_t* argb = pixmap->GetPixelData();
            for (size_t i = 0, n = static_cast<size_t>(w) * h; i < n; ++i) {
                const int level = static_cast<int>((argb[i] >> 8) & 0xFF);   // green: the ink is dark
                rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 0;      // black ink ...
                rgba[i * 4 + 3] = static_cast<uint8_t>(255 - level);          // ... only where the text is
            }
            const fs::path transparent = folder / PathFromUtf8("transparent.png");
            const std::shared_ptr<UCImage> ink = UCImage::FromRgba(rgba.data(), w, h);
            UCImageSave::ImageExportOptions png;
            const std::string saveError = ink ? ink->Save(PathToUtf8(transparent), png) : std::string("no image");
            const OCRResult onWhite = ocr.RecognizeFile(PathToUtf8(transparent));
            Check(saveError.empty() && onWhite.Ok() && Contains(onWhite.plainText, "OCR demo"),
                  "... and dark text on a transparent picture (alpha onto white)");
        }
    }
#endif
    UltraCanvasFileLoader::SetImageTextRecognizer(nullptr);

    fs::remove_all(folder, ec);
    std::cout << (g_failures == 0 ? "PASS" : "FAIL") << " (" << g_failures << " failure(s))" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
