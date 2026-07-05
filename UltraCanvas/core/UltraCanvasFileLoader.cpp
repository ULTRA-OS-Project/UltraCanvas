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

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

#ifdef ULTRACANVAS_HAS_NET
#include "UltraNet/UltraNetHttp.h"
#endif

namespace UltraCanvas {

    namespace {
        bool StartsWithCaseInsensitive(const std::string& s, const std::string& prefix) {
            if (s.size() < prefix.size()) return false;
            for (std::size_t i = 0; i < prefix.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(s[i])) !=
                    std::tolower(static_cast<unsigned char>(prefix[i]))) return false;
            }
            return true;
        }
    }

    bool UltraCanvasFileLoader::IsUrl(const std::string& s) {
        return StartsWithCaseInsensitive(s, "http://")  ||
               StartsWithCaseInsensitive(s, "https://") ||
               StartsWithCaseInsensitive(s, "ftp://")   ||
               StartsWithCaseInsensitive(s, "ftps://")  ||
               StartsWithCaseInsensitive(s, "sftp://");
    }

    FileBytesResult UltraCanvasFileLoader::LoadFile(const std::string& pathOrUrl) {
        FileBytesResult out;
        out.sourceUri = pathOrUrl;
        if (pathOrUrl.empty()) {
            out.error = "empty path or URL";
            return out;
        }

        if (IsUrl(pathOrUrl)) {
#ifdef ULTRACANVAS_HAS_NET
            const bool isHttp = StartsWithCaseInsensitive(pathOrUrl, "http://") ||
                                StartsWithCaseInsensitive(pathOrUrl, "https://");
            if (!isHttp) {
                out.error = "URL scheme not yet supported by FileLoader "
                            "(ftp/sftp arrive with the UltraNet FTP module)";
                return out;
            }
            UltraNetResponse resp;
            UltraNetResult r = UltraNet_HttpGet(pathOrUrl, resp);
            out.httpStatus  = resp.statusCode;
            out.contentType = resp.contentType;
            out.bytes       = std::move(resp.body);
            if (r) {
                out.success = true;
            } else {
                out.error = r.message.empty() ? "HTTP request failed" : r.message;
            }
            return out;
#else
            out.error = "URL load requires ULTRACANVAS_HAS_NET (UltraNet not built)";
            return out;
#endif
        }

        // Local filesystem path
        std::ifstream f(pathOrUrl, std::ios::binary);
        if (!f) {
            out.error = "cannot open file: " + pathOrUrl;
            return out;
        }
        out.bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        out.success = true;
        return out;
    }

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

} // namespace UltraCanvas
