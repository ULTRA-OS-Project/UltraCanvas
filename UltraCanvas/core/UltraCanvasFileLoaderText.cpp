// core/UltraCanvasFileLoaderText.cpp
// UltraCanvasFileLoader::ExtractText - the words in a file as plain text: a
// word processor document or a deck, a PDF (text recognition for a scanned
// one), a spreadsheet, a text file, a picture (text recognition) and a
// recording (speech to text). The two recognition engines are hooks a plugin
// or the application installs (SetImageTextRecognizer, SetSpeechTranscriber).
// Version: 1.1.0 - the messages in the person's language (Tr)
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasRichDocument.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSupportedFormats.h"
#include "UltraLocalisation/UltraLocalisation.h"
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"
#ifdef ULTRACANVAS_PLUGIN_PDF
#include "Plugins/Documents/UltraCanvasPDF.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <mutex>

namespace UltraCanvas {

    namespace {
        std::mutex& HookMutex() {
            static std::mutex mutex;
            return mutex;
        }
        UltraCanvasFileLoader::ImageTextRecognizer& Recognizer() {
            static UltraCanvasFileLoader::ImageTextRecognizer recognizer;
            return recognizer;
        }
        UltraCanvasFileLoader::SpeechTranscriber& Transcriber() {
            static UltraCanvasFileLoader::SpeechTranscriber transcriber;
            return transcriber;
        }
        UltraCanvasFileLoader::ImageTextRecognizer CurrentRecognizer() {
            std::lock_guard<std::mutex> lock(HookMutex());
            return Recognizer();
        }
        UltraCanvasFileLoader::SpeechTranscriber CurrentTranscriber() {
            std::lock_guard<std::mutex> lock(HookMutex());
            return Transcriber();
        }

        std::string LowerExtension(const std::string& filePath) {
            std::string extension = PathToUtf8(PathFromUtf8(filePath).extension());
            if (!extension.empty() && extension[0] == '.') extension.erase(0, 1);
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension;
        }

        // Up to `limit` bytes of the file; false when it cannot be read.
        bool ReadFileBytes(const std::string& filePath, std::string& out, size_t limit = 0) {
            out.clear();
            std::FILE* file = OpenFileUtf8(filePath, "rb");
            if (!file) return false;
            char buffer[65536];
            size_t read = 0;
            while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
                out.append(buffer, read);
                if (limit > 0 && out.size() >= limit) {
                    out.resize(limit);
                    break;
                }
            }
            const bool failed = std::ferror(file) != 0;
            std::fclose(file);
            return !failed;
        }

        // UTF-8 without NUL bytes: text. A sequence cut at the end of the
        // sample (the sample's limit, not the file's end) still counts.
        bool LooksLikeUtf8Text(const std::string& sample) {
            if (sample.empty()) return false;
            size_t i = 0;
            while (i < sample.size()) {
                const auto c = static_cast<unsigned char>(sample[i]);
                if (c == 0) return false;
                int follow = 0;
                if (c < 0x80) follow = 0;
                else if ((c & 0xE0) == 0xC0 && c >= 0xC2) follow = 1;
                else if ((c & 0xF0) == 0xE0) follow = 2;
                else if ((c & 0xF8) == 0xF0 && c <= 0xF4) follow = 3;
                else return false;
                for (int k = 1; k <= follow; ++k) {
                    if (i + k >= sample.size()) return true;
                    if ((static_cast<unsigned char>(sample[i + k]) & 0xC0) != 0x80) return false;
                }
                i += follow + 1;
            }
            return true;
        }

        // Ends of lines as \n, no byte-order mark, no trailing blank lines.
        std::string Tidy(std::string text) {
            if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
                static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
                text.erase(0, 3);
            std::string out;
            out.reserve(text.size());
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '\r') {
                    out += '\n';
                    if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
                } else {
                    out += text[i];
                }
            }
            while (!out.empty() && (out.back() == '\n' || out.back() == ' ' || out.back() == '\t' ||
                                    out.back() == '\f'))
                out.pop_back();
            return out;
        }

        bool IsBlank(const std::string& text) {
            return std::all_of(text.begin(), text.end(),
                               [](unsigned char c) { return std::isspace(c) != 0; });
        }

        // The picture's pixels on white (a transparent screenshot's text is
        // otherwise black on black), 3 bytes a pixel, for the recognizer.
        bool PictureOnWhite(const std::string& filePath, std::vector<uint8_t>& rgb, int& width, int& height,
                            std::string& error) {
            const std::shared_ptr<UCImage> image = UCImage::Load(filePath, false);
            if (!image || !image->errorMessage.empty() || image->GetWidth() <= 0) {
                error = image && !image->errorMessage.empty()
                                ? TrF("The picture could not be read: {0}", image->errorMessage)
                                : Tr("The picture could not be read.");
                return false;
            }
            const std::shared_ptr<UCPixmapCairo> pixmap = image->GetPixmap();
            if (!pixmap || !pixmap->IsValid()) {
                error = Tr("The picture could not be decoded.");
                return false;
            }
            pixmap->Flush();
            width = pixmap->GetRawWidth();
            height = pixmap->GetRawHeight();
            const uint32_t* pixels = pixmap->GetPixelData();
            if (!pixels || width <= 0 || height <= 0) {
                error = Tr("The picture is empty.");
                return false;
            }
            // Cairo's surfaces are rows of premultiplied ARGB words, a stride
            // of 4 * width for the pixmaps an image is decoded into.
            rgb.resize(static_cast<size_t>(width) * height * 3);
            size_t o = 0;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const uint32_t p = pixels[static_cast<size_t>(y) * width + x];
                    const int a = static_cast<int>(p >> 24);
                    const int white = 255 - (pixmap->IsOpaque() ? 255 : a);
                    rgb[o++] = static_cast<uint8_t>(std::min(255, static_cast<int>((p >> 16) & 0xFF) + white));
                    rgb[o++] = static_cast<uint8_t>(std::min(255, static_cast<int>((p >> 8) & 0xFF) + white));
                    rgb[o++] = static_cast<uint8_t>(std::min(255, static_cast<int>(p & 0xFF) + white));
                }
            }
            return true;
        }

        std::string SpreadsheetText(const std::string& filePath, const std::string& password, std::string& error) {
            UltraCanvasSpreadsheet sheet;
            if (!sheet.LoadFromFile(filePath, password)) {
                error = sheet.GetLastError().empty() ? Tr("The spreadsheet could not be read.") : sheet.GetLastError();
                return "";
            }
            CSVExportOptions options;
            options.fieldSeparator = '\t';
            options.textDelimiter = '"';
            const std::vector<std::string> names = sheet.GetSheetNames();
            const int count = sheet.GetSheetCount();
            std::string text;
            for (int i = 0; i < count; ++i) {
                const std::string body = Tidy(sheet.ExportCSVToString(options, i));
                if (IsBlank(body)) continue;
                if (!text.empty()) text += "\n\n";
                if (count > 1 && i < static_cast<int>(names.size())) text += names[static_cast<size_t>(i)] + "\n";
                text += body;
            }
            return text;
        }

#ifdef ULTRACANVAS_PLUGIN_PDF
        // The PDF's own text, page by page; a PDF without any (a scan) is
        // read by the recognizer, when there is one, at 300 DPI.
        std::string PdfText(const std::string& filePath, const std::string& password, std::string& error) {
            auto document = PDFEngineFactory::Create(PDFEngineKind::Auto);
            if (!document) {
                error = Tr("This build has no PDF reader.");
                return "";
            }
            if (!document->OpenInMemory(filePath, password)) {
                error = document->GetLastOpenError() == PDFOpenError::NoError
                                ? Tr("The PDF could not be read.")
                                : Tr("The PDF is password-protected or damaged.");
                return "";
            }
            const int pages = document->GetPageCount();
            std::string text;
            for (int page = 1; page <= pages; ++page) {   // 1-based
                const std::string body = Tidy(document->GetPageText(page));
                if (IsBlank(body)) continue;
                if (!text.empty()) text += "\n\n";
                text += body;
            }
            if (!IsBlank(text)) return text;
            const UltraCanvasFileLoader::ImageTextRecognizer recognizer = CurrentRecognizer();
            if (!recognizer) return "";   // a scan, and nothing to read it with: no text
            PDFRenderSettings settings;
            settings.dpi = 300.0f;
            settings.colorMode = PDFColorMode::Gray;
            for (int page = 1; page <= pages; ++page) {   // 1-based
                const PDFRenderedPage rendered = document->RenderPage(page, settings);
                if (!rendered.IsValid()) continue;
                UltraCanvasFileLoader::TextRecognitionImage image;
                image.pixels = rendered.pixels.data();
                image.width = rendered.width;
                image.height = rendered.height;
                image.bytesPerPixel = rendered.colorMode == PDFColorMode::Gray ? 1 : 4;
                image.bytesPerLine = rendered.stride;
                std::string pageText, pageError;
                if (!recognizer(image, pageText, pageError)) {
                    error = pageError.empty() ? Tr("Text recognition failed.") : pageError;
                    return "";
                }
                pageText = Tidy(pageText);
                if (IsBlank(pageText)) continue;
                if (!text.empty()) text += "\n\n";
                text += pageText;
            }
            return text;
        }
#endif
    } // namespace

    void UltraCanvasFileLoader::SetImageTextRecognizer(ImageTextRecognizer recognizer) {
        std::lock_guard<std::mutex> lock(HookMutex());
        Recognizer() = std::move(recognizer);
    }

    void UltraCanvasFileLoader::SetSpeechTranscriber(SpeechTranscriber transcriber) {
        std::lock_guard<std::mutex> lock(HookMutex());
        Transcriber() = std::move(transcriber);
    }

    bool UltraCanvasFileLoader::HasImageTextRecognizer() {
        std::lock_guard<std::mutex> lock(HookMutex());
        return static_cast<bool>(Recognizer());
    }

    bool UltraCanvasFileLoader::HasSpeechTranscriber() {
        std::lock_guard<std::mutex> lock(HookMutex());
        return static_cast<bool>(Transcriber());
    }

    UltraCanvasFileLoader::TextExtractionMethod UltraCanvasFileLoader::GetTextExtractionMethod(
            const std::string& filePath) {
        const std::string extension = LowerExtension(filePath);
        if (extension == "pdf") return TextExtractionMethod::PDF;
        // CSV and TSV are text already: as they are.
        if (extension == "csv" || extension == "tsv" || extension == "txt" || extension == "md" ||
            extension == "markdown")
            return TextExtractionMethod::PlainText;
        if (IsPptxExtension(extension)) return TextExtractionMethod::Document;
        if (WordDocumentFormatFromExtension(extension) != WordDocumentFormat::Unknown)
            return TextExtractionMethod::Document;
        if (const std::optional<MediaFormatInfo> format = UltraCanvasSupportedFormats::FindByExtension(extension)) {
            switch (format->category) {
                case MediaFormatCategory::Spreadsheet: return TextExtractionMethod::Spreadsheet;
                case MediaFormatCategory::Bitmap:
                    return UltraCanvasSupportedFormats::CanImagePipelineLoad(extension)
                                   ? TextExtractionMethod::Recognition
                                   : TextExtractionMethod::NoText;
                case MediaFormatCategory::Audio:
                case MediaFormatCategory::Video: return TextExtractionMethod::Transcription;
                default: break;
            }
        }
        // A type the inventory does not know (source code, JSON, a log): text
        // when it reads as UTF-8.
        std::string sample;
        if (ReadFileBytes(filePath, sample, 65536) && LooksLikeUtf8Text(sample)) return TextExtractionMethod::PlainText;
        return TextExtractionMethod::NoText;
    }

    bool UltraCanvasFileLoader::CanExtractText(const std::string& filePath) {
        return WhyNoTextExtraction(filePath).empty();
    }

    std::string UltraCanvasFileLoader::WhyNoTextExtraction(const std::string& filePath) {
        switch (GetTextExtractionMethod(filePath)) {
            case TextExtractionMethod::NoText: return Tr("There is no text to read in this type of file.");
            case TextExtractionMethod::Recognition:
                return HasImageTextRecognizer()
                               ? ""
                               : Tr("Text recognition (OCR) is not available: this build has no OCR engine.");
            case TextExtractionMethod::Transcription:
                return HasSpeechTranscriber()
                               ? ""
                               : Tr("Speech to text is not available: no speech recognition (UltraAI STT or "
                                    "another engine) is set up.");
            case TextExtractionMethod::PDF:
#ifdef ULTRACANVAS_PLUGIN_PDF
                return "";
#else
                return Tr("This build has no PDF reader.");
#endif
            default: return "";
        }
    }

    UltraCanvasFileLoader::TextExtractionResult UltraCanvasFileLoader::ExtractText(const std::string& filePath,
                                                                                 const std::string& password) {
        TextExtractionResult result;
        result.method = GetTextExtractionMethod(filePath);
        result.error = WhyNoTextExtraction(filePath);
        if (!result.error.empty()) return result;
        switch (result.method) {
            case TextExtractionMethod::PlainText: {
                std::string bytes;
                if (!ReadFileBytes(filePath, bytes)) result.error = Tr("The file could not be read.");
                else result.text = Tidy(std::move(bytes));
                break;
            }
            case TextExtractionMethod::Document: {
                const std::shared_ptr<UCRichDocument> document = LoadTextDocument(filePath, result.error, password);
                if (document) result.text = Tidy(document->ToPlainText());
                else if (result.error.empty()) result.error = Tr("The document could not be read.");
                break;
            }
            case TextExtractionMethod::Spreadsheet:
                result.text = SpreadsheetText(filePath, password, result.error);
                break;
            case TextExtractionMethod::PDF:
#ifdef ULTRACANVAS_PLUGIN_PDF
                result.text = PdfText(filePath, password, result.error);
#endif
                break;
            case TextExtractionMethod::Recognition: {
                const ImageTextRecognizer recognizer = CurrentRecognizer();
                std::vector<uint8_t> rgb;
                TextRecognitionImage image;
                if (!PictureOnWhite(filePath, rgb, image.width, image.height, result.error)) break;
                image.pixels = rgb.data();
                image.bytesPerPixel = 3;
                image.bytesPerLine = image.width * 3;
                std::string text;
                if (!recognizer || !recognizer(image, text, result.error)) {
                    if (result.error.empty()) result.error = Tr("Text recognition failed.");
                    break;
                }
                result.text = Tidy(std::move(text));
                break;
            }
            case TextExtractionMethod::Transcription: {
                const SpeechTranscriber transcriber = CurrentTranscriber();
                std::string text;
                if (!transcriber || !transcriber(filePath, text, result.error)) {
                    if (result.error.empty()) result.error = Tr("Speech to text failed.");
                    break;
                }
                result.text = Tidy(std::move(text));
                break;
            }
            case TextExtractionMethod::NoText: break;
        }
        if (!result.error.empty()) result.text.clear();
        return result;
    }

} // namespace UltraCanvas
