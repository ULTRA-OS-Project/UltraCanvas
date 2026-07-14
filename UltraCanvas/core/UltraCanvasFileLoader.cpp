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

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

#ifdef ULTRACANVAS_HAS_NET
#include "UltraNet/UltraNetHttp.h"
#endif

#ifdef ULTRACANVAS_HAS_VIRTUALFS
#include <VirtualFS/VirtualFSCompression.h>
// Xlib.h (pulled in via UltraCanvasCommonTypes.h) defines Success as a
// macro, colliding with VirtualFSResult::Success used below. This file
// uses no X11 API, so the macro can be dropped for the rest of the TU.
#ifdef Success
#undef Success
#endif
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

        // Transparent decompression: apps get the plain payload regardless of
        // whether the source was gzip/zlib/Zstd/LZ4 compressed. Detection is
        // by magic bytes; content that only looks compressed (the zlib header
        // has no strong magic) but fails to decode is kept unmodified.
        void MaybeDecompress(FileBytesResult& out, bool autoDecompress) {
#ifdef ULTRACANVAS_HAS_VIRTUALFS
            if (!autoDecompress || !out.success || out.bytes.size() < 4) return;

            auto method = VirtualFS::VirtualFS_DetectCompressionMethod(
                out.bytes.data(), out.bytes.size());
            if (method == VirtualFS::VirtualFSCompressionMethod::Auto) return;

            std::vector<uint8_t> plain;
            if (VirtualFS::VirtualFS_DecompressBuffer(out.bytes.data(), out.bytes.size(),
                                                      plain, method)
                    == VirtualFS::VirtualFSResult::Success) {
                // gzip and zlib both decode through Deflate; report gzip by
                // its distinct magic so callers see the on-disk format
                if (method == VirtualFS::VirtualFSCompressionMethod::Deflate &&
                    out.bytes[0] == 0x1F && out.bytes[1] == 0x8B) {
                    out.decompressedFrom = "GZIP";
                } else {
                    out.decompressedFrom =
                        VirtualFS::VirtualFSCompressionMethodToString(method);
                }
                out.bytes = std::move(plain);
            }
#else
            (void)out;
            (void)autoDecompress;
#endif
        }
    }

    bool UltraCanvasFileLoader::IsUrl(const std::string& s) {
        return StartsWithCaseInsensitive(s, "http://")  ||
               StartsWithCaseInsensitive(s, "https://") ||
               StartsWithCaseInsensitive(s, "ftp://")   ||
               StartsWithCaseInsensitive(s, "ftps://")  ||
               StartsWithCaseInsensitive(s, "sftp://");
    }

    FileBytesResult UltraCanvasFileLoader::LoadFile(const std::string& pathOrUrl,
                                                    bool autoDecompress) {
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
            MaybeDecompress(out, autoDecompress);
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
        MaybeDecompress(out, autoDecompress);
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

    std::vector<MediaFormatInfo> UltraCanvasFileLoader::GetSupportedFormats() {
        return UltraCanvasSupportedFormats::GetAll();
    }

    std::vector<MediaFormatInfo> UltraCanvasFileLoader::GetSupportedFormats(MediaFormatCategory category) {
        return UltraCanvasSupportedFormats::GetByCategory(category);
    }

    std::vector<std::string> UltraCanvasFileLoader::GetSupportedLoadExtensions(MediaFormatCategory category) {
        return UltraCanvasSupportedFormats::GetLoadExtensions(category);
    }

    std::vector<std::string> UltraCanvasFileLoader::GetSupportedSaveExtensions(MediaFormatCategory category) {
        return UltraCanvasSupportedFormats::GetSaveExtensions(category);
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
