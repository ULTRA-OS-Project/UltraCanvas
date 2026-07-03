// core/UltraCanvasFileLoader.cpp
// Cross-platform implementation of the file selection dialog facade.
// NotifyRecentFile is implemented per-platform in OS/<platform>/UltraCanvas*FileLoader.cpp.
// Version: 1.0.0
// Last Modified: 2026-05-12
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasAudio.h"
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace UltraCanvas {

    void UltraCanvasFileLoader::OpenFileDialog(
            const FileDialogOptions& opts,
            std::function<void(DialogResult, const std::string&)> onResult) {

        FileDialogOptions effective = opts;
        if (effective.title.empty()) effective.title = "Open File";

        std::string result = UltraCanvasNativeDialogs::OpenFile(effective);
        DialogResult dr = result.empty() ? DialogResult::Cancel : DialogResult::OK;

        if (dr == DialogResult::OK && opts.registerAsRecent) {
            NotifyRecentFile(result);
        }
        if (onResult) {
            onResult(dr, result);
        }
    }

    void UltraCanvasFileLoader::OpenMultipleFilesDialog(
            const FileDialogOptions& opts,
            std::function<void(DialogResult, const std::vector<std::string>&)> onResult) {

        FileDialogOptions effective = opts;
        if (effective.title.empty()) effective.title = "Open Files";

        std::vector<std::string> results = UltraCanvasNativeDialogs::OpenMultipleFiles(effective);
        DialogResult dr = results.empty() ? DialogResult::Cancel : DialogResult::OK;

        if (dr == DialogResult::OK && opts.registerAsRecent) {
            for (const auto& path : results) {
                NotifyRecentFile(path);
            }
        }
        if (onResult) {
            onResult(dr, results);
        }
    }

    void UltraCanvasFileLoader::SaveFileDialog(
            const FileDialogOptions& opts,
            std::function<void(DialogResult, const std::string&)> onResult) {

        FileDialogOptions effective = opts;
        if (effective.title.empty()) effective.title = "Save File";

        std::string result = UltraCanvasNativeDialogs::SaveFile(effective);
        DialogResult dr = result.empty() ? DialogResult::Cancel : DialogResult::OK;

        if (dr == DialogResult::OK && opts.registerAsRecent) {
            NotifyRecentFile(result);
        }
        if (onResult) {
            onResult(dr, result);
        }
    }

    void UltraCanvasFileLoader::SelectFolderDialog(
            const FileDialogOptions& opts,
            std::function<void(DialogResult, const std::string&)> onResult) {

        // Folder selection bypasses the options-based overload because
        // SelectFolder takes (title, initialDir, parent) directly.
        std::string result = UltraCanvasNativeDialogs::SelectFolder(
                opts.title.empty() ? "Select Folder" : opts.title,
                opts.initialDirectory,
                opts.parentWindow);

        DialogResult dr = result.empty() ? DialogResult::Cancel : DialogResult::OK;
        if (onResult) {
            onResult(dr, result);
        }
    }

    void UltraCanvasFileLoader::OpenImage(
            const FileDialogOptions& opts,
            std::function<void(const FileLoadResult&, std::shared_ptr<UCImage>)> onResult) {

        OpenFileDialog(opts, [onResult](DialogResult r, const std::string& path) {
            FileLoadResult loadResult;
            loadResult.dialogResult = r;
            std::shared_ptr<UCImage> img;

            if (r == DialogResult::OK && !path.empty()) {
                img = UCImage::Get(path);
                if (!img || !img->IsValid()) {
                    loadResult.loadError = img && !img->errorMessage.empty()
                                           ? img->errorMessage
                                           : "Failed to load image";
                }
            }
            if (onResult) {
                onResult(loadResult, img);
            }
        });
    }

    void UltraCanvasFileLoader::OpenAudio(
            const FileDialogOptions& opts,
            std::function<void(const FileLoadResult&, std::shared_ptr<UCAudio>)> onResult) {

        FileDialogOptions effective = opts;
        if (effective.title.empty()) effective.title = "Open Audio File";
        if (effective.filters.empty()) {
            effective
                .AddFilter("Audio files",
                           std::vector<std::string>{"wav", "mp3", "flac", "ogg", "oga"})
                .AddFilter("Wave audio (*.wav)", "wav")
                .AddFilter("MP3 audio (*.mp3)", "mp3")
                .AddFilter("FLAC audio (*.flac)", "flac")
                .AddFilter("Ogg Vorbis (*.ogg)", "ogg")
                .AddFilter("All files (*.*)", "*");
        }

        OpenFileDialog(effective, [onResult](DialogResult r, const std::string& path) {
            FileLoadResult loadResult;
            loadResult.dialogResult = r;
            std::shared_ptr<UCAudio> audio;

            if (r == DialogResult::OK && !path.empty()) {
                audio = UCAudio::LoadFromFile(path);
                if (!audio || !audio->IsValid()) {
                    loadResult.loadError =
                        "Failed to decode audio file (unsupported format or "
                        "audio backend not available)";
                }
            }
            if (onResult) {
                onResult(loadResult, audio);
            }
        });
    }

    std::shared_ptr<UCRichDocument> UltraCanvasFileLoader::LoadTextDocument(
            const std::string& filePath, std::string& outError) {
        outError.clear();

        // Package formats (and renamed/legacy files) go by content signature.
        WordDocumentFormat format = DetectWordDocumentFormat(filePath);
        if (format != WordDocumentFormat::Unknown) {
            auto document = std::make_shared<UCRichDocument>();
            if (!UCWordDocumentIO::Load(filePath, *document, outError)) {
                return nullptr;
            }
            return document;
        }

        // Everything else is treated as (markdown-flavored) text.
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            outError = "Cannot open file: " + filePath;
            return nullptr;
        }
        std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
        std::string baseDirectory = std::filesystem::path(filePath).parent_path().string();
        return std::make_shared<UCRichDocument>(
            UCRichDocument::FromMarkdown(text, baseDirectory));
    }

    void UltraCanvasFileLoader::OpenTextDocument(
            const FileDialogOptions& opts,
            std::function<void(const FileLoadResult&, std::shared_ptr<UCRichDocument>)> onResult) {

        FileDialogOptions effective = opts;
        if (effective.title.empty()) effective.title = "Open Document";
        if (effective.filters.empty()) {
            effective
                .AddFilter("Documents",
                           std::vector<std::string>{"odt", "docx", "doc", "md", "markdown", "txt"})
                .AddFilter("OpenDocument Text (*.odt)", "odt")
                .AddFilter("Word Document (*.docx)", "docx")
                .AddFilter("Markdown (*.md)", std::vector<std::string>{"md", "markdown"})
                .AddFilter("Plain Text (*.txt)", "txt")
                .AddFilter("All files (*.*)", "*");
        }

        OpenFileDialog(effective, [onResult](DialogResult r, const std::string& path) {
            FileLoadResult loadResult;
            loadResult.dialogResult = r;
            std::shared_ptr<UCRichDocument> document;

            if (r == DialogResult::OK && !path.empty()) {
                document = LoadTextDocument(path, loadResult.loadError);
            }
            if (onResult) {
                onResult(loadResult, document);
            }
        });
    }

} // namespace UltraCanvas
