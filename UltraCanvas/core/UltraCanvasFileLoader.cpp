// core/UltraCanvasFileLoader.cpp
// Cross-platform implementation of the file selection dialog facade.
// NotifyRecentFile is implemented per-platform in OS/<platform>/UltraCanvas*FileLoader.cpp.
// Version: 1.12.1 - RequestDocumentPassword's prompt shows a lock centred above the field
// Version: 1.12.0 - LoadTextDocument / OpenTextDocument read an OpenDocument presentation
//                  (.odp, .otp, .fodp) as an outline, like a .pptx
// Version: 1.11.0 - ConvertVideoFile, CanConvertVideoTo, GetVideoSaveExtensions, AddVideoSaveTypes
// Version: 1.10.0 - SaveAudio, ConvertAudioFile
// Version: 1.9.0 - SaveFile
// Version: 1.8.0 - LoadTextDocument / OpenTextDocument read a .pptx as an outline
// Version: 1.7.0 - RequestNewDocumentPassword; SaveTextDocument saves a
//                  password-protected .docx
// Version: 1.6.0 - RequestDocumentPassword; OpenTextDocument and LoadTextDocument open
//                  password-protected .docx files
// Version: 1.5.0 - AddLoadTypes / AddSaveTypes (the inventory as a dialog's file types),
//                  AddTextDocumentLoadTypes, SaveTextDocument, AddTextDocumentSaveTypes
// Version: 1.4.0 - SaveVectorDocument writes bitmap types (the page drawn at a size),
//                  GetVectorDocumentPixelSize, CanSaveImage, AddVectorDocumentSaveTypes,
//                  AddImageSaveTypes
// Version: 1.3.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasAudio.h"
#include "UltraCanvasMediaCodecRegistry.h"   // SaveAudio's encoders, ConvertVideoFile's converters
#include "UltraCanvasUtils.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir: the prompt's lock icon
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"
#include "UltraCanvasOfficeEncryption.h"   // RequestDocumentPassword
#include "UltraCanvasPresentationPptx.h"    // .pptx, read as an outline
#include "UltraCanvasPresentationOdp.h"     // .odp / .otp / .fodp, the same
#include "UltraCanvasVectorPreview.h"   // the Vector plugin's readers/writers, installed at start-up
#include "UltraCanvasVectorRaster.h"    // RasterizeVectorDocument: a drawing saved as a bitmap
#include "UltraCanvasRasterDocument.h"  // ... written through the image writer

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <map>
#include <fstream>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasFileError.h"   // FileWriteFailureReason

#ifdef ULTRACANVAS_HAS_NET
#include "UltraNet/UltraNetHttp.h"
#endif

#ifdef ULTRACANVAS_HAS_VIRTUALFS
#include <VirtualFS/VirtualFSCompression.h>
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
                    == VirtualFS::VirtualFSResult::Ok) {
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
        std::ifstream f(PathFromUtf8(pathOrUrl), std::ios::binary);
        if (!f) {
            out.error = "cannot open file: " + pathOrUrl;
            return out;
        }
        out.bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        out.success = true;
        MaybeDecompress(out, autoDecompress);
        return out;
    }

    bool UltraCanvasFileLoader::SaveFile(const std::string& path, const std::vector<uint8_t>& bytes,
                                         std::string& error) {
        error.clear();
        if (path.empty()) { error = "empty path"; return false; }
        if (IsUrl(path)) { error = "a URL cannot be saved to"; return false; }
        const std::filesystem::path target = PathFromUtf8(path);
        std::filesystem::path partial = target;
        partial += ".part";
        {
            std::ofstream out(partial, std::ios::binary | std::ios::trunc);
            if (!out) { error = "cannot write " + path; return false; }
            if (!bytes.empty()) out.write(reinterpret_cast<const char*>(bytes.data()),
                                          static_cast<std::streamsize>(bytes.size()));
            out.close();
            if (!out) {
                std::error_code ignored;
                std::filesystem::remove(partial, ignored);
                error = "the file could not be completed (is the disk full?)";
                return false;
            }
        }
        std::error_code ec;
        std::filesystem::rename(partial, target, ec);
        if (ec) {
            std::error_code ignored;
            std::filesystem::remove(partial, ignored);
            error = "cannot put the file in place: " + ec.message();
            return false;
        }
        return true;
    }

    // ===== FILE DIALOGS =====
    // Each of the four follows UltraCanvasDialogManager's native-dialogs
    // setting: the platform's picker when it is on (blocking; the callback
    // runs before the call returns), the framework's UltraCanvasFileDialog
    // when it is off (the callback runs when the dialog closes). The choice
    // used to be the platform's regardless, so an app that turned native
    // dialogs off for one look throughout still got the platform's file
    // picker among its own dialogs.
    namespace {

    FileDialogConfig FileConfigFrom(const FileDialogOptions& opts,
                                    FileDialogType type,
                                    const std::string& defaultTitle) {
        FileDialogConfig config;
        config.title            = opts.title.empty() ? defaultTitle : opts.title;
        config.dialogType       = type;
        config.initialDirectory = opts.initialDirectory;
        config.defaultFileName  = opts.defaultFileName;
        config.defaultExtension = opts.defaultExtension;
        config.showHiddenFiles  = opts.showHiddenFiles;
        config.allowMultipleSelection = type == FileDialogType::OpenMultiple;
        config.filterToggles    = opts.filterToggles && type != FileDialogType::SelectFolder;
        config.confirmOverwrite = opts.confirmOverwrite;
        config.hoverIconMenu    = opts.hoverIconMenu;
        // The framework dialog adds what it accepts to the recent files
        // itself (FileDialogConfig::addToRecent); the native path below does
        // it here.
        config.addToRecent      = opts.registerAsRecent;
        config.saveSettingsButton = opts.saveSettingsButton;
        config.imageWidth       = opts.imageWidth;
        config.imageHeight      = opts.imageHeight;
        config.imageIsDrawing   = opts.imageIsDrawing;
        // A folder has no file filter; a file dialog takes the caller's (the
        // dialog lists every file when there are none).
        if (type != FileDialogType::SelectFolder) config.filters = opts.filters;
        return config;
    }

    // A native dialog has no toggle buttons: the filters become its list,
    // headed by one that matches every kind of file in it (an "All files"
    // entry left out of that, and kept as a choice at the end).
    void FoldFilterTogglesForNative(FileDialogOptions& opts) {
        if (!opts.filterToggles || opts.filters.size() < 2) return;
        std::vector<std::string> all;
        for (const FileFilter& filter : opts.filters) {
            if (std::find(filter.extensions.begin(), filter.extensions.end(), "*") !=
                filter.extensions.end()) continue;
            all.insert(all.end(), filter.extensions.begin(), filter.extensions.end());
        }
        std::sort(all.begin(), all.end());
        all.erase(std::unique(all.begin(), all.end()), all.end());
        if (!all.empty()) opts.filters.insert(opts.filters.begin(), FileFilter("All supported files", all));
    }

    // Shows the framework's dialog and hands its selection on. The result
    // lambda is stored on the dialog, so it holds the dialog raw: the dialog
    // is alive for as long as its own callback can run.
    void ShowFrameworkFileDialog(
            const FileDialogConfig& config,
            UltraCanvasWindowBase* parent,
            std::function<void(DialogResult, const std::vector<std::string>&)> onDone) {
        auto dialog = UltraCanvasDialogManager::CreateFileDialog(config);
        UltraCanvasDialogManager::ShowDialog(
            dialog,
            [dialog = dialog.get(), onDone](DialogResult result) {
                std::vector<std::string> paths;
                if (result == DialogResult::OK) {
                    paths = dialog->GetSelectedFilePaths();
                }
                if (paths.empty()) result = DialogResult::Cancel;
                if (onDone) onDone(result, paths);
            },
            parent);
    }

    } // namespace

    void UltraCanvasFileLoader::OpenFileDialog(
            const FileDialogOptions& opts,
            std::function<void(DialogResult, const std::string&)> onResult) {

        const bool registerAsRecent = opts.registerAsRecent;
        auto deliver = [registerAsRecent, onResult](DialogResult dr,
                                                    const std::string& result) {
            if (dr == DialogResult::OK && registerAsRecent) {
                NotifyRecentFile(result);
            }
            if (onResult) onResult(dr, result);
        };

        if (!UltraCanvasDialogManager::GetUseNativeDialogs()) {
            ShowFrameworkFileDialog(
                FileConfigFrom(opts, FileDialogType::Open, "Open File"),
                opts.parentWindow,
                [onResult](DialogResult dr, const std::vector<std::string>& paths) {
                    if (onResult) onResult(dr, paths.empty() ? std::string() : paths.front());
                });
            return;
        }

        FileDialogOptions effective = opts;
        if (effective.title.empty()) effective.title = "Open File";
        FoldFilterTogglesForNative(effective);

        std::string result = UltraCanvasNativeDialogs::OpenFile(effective);
        deliver(result.empty() ? DialogResult::Cancel : DialogResult::OK, result);
    }

    void UltraCanvasFileLoader::OpenMultipleFilesDialog(
            const FileDialogOptions& opts,
            std::function<void(DialogResult, const std::vector<std::string>&)> onResult) {

        const bool registerAsRecent = opts.registerAsRecent;
        auto deliver = [registerAsRecent, onResult](
                           DialogResult dr, const std::vector<std::string>& results) {
            if (dr == DialogResult::OK && registerAsRecent) {
                for (const auto& path : results) {
                    NotifyRecentFile(path);
                }
            }
            if (onResult) onResult(dr, results);
        };

        if (!UltraCanvasDialogManager::GetUseNativeDialogs()) {
            ShowFrameworkFileDialog(
                FileConfigFrom(opts, FileDialogType::OpenMultiple, "Open Files"),
                opts.parentWindow, onResult);
            return;
        }

        FileDialogOptions effective = opts;
        if (effective.title.empty()) effective.title = "Open Files";
        FoldFilterTogglesForNative(effective);

        std::vector<std::string> results = UltraCanvasNativeDialogs::OpenMultipleFiles(effective);
        deliver(results.empty() ? DialogResult::Cancel : DialogResult::OK, results);
    }

    void UltraCanvasFileLoader::SaveFileDialog(
            const FileDialogOptions& opts,
            std::function<void(DialogResult, const std::string&)> onResult) {
        SaveFileDialog(opts, [onResult](DialogResult dr, const std::string& path,
                                        const FileSaveImageSize&) {
            if (onResult) onResult(dr, path);
        });
    }

    void UltraCanvasFileLoader::SaveFileDialog(
            const FileDialogOptions& opts,
            std::function<void(DialogResult, const std::string&, const FileSaveImageSize&)> onResult) {

        const bool registerAsRecent = opts.registerAsRecent;
        auto deliver = [registerAsRecent, onResult](DialogResult dr,
                                                    const std::string& result) {
            if (dr == DialogResult::OK && registerAsRecent) {
                NotifyRecentFile(result);
            }
            // The platform's dialog has no Settings: the picture's own size.
            if (onResult) onResult(dr, result, FileSaveImageSize());
        };

        if (!UltraCanvasDialogManager::GetUseNativeDialogs()) {
            // As ShowFrameworkFileDialog, but the size chosen in the dialog's
            // Settings is read before the dialog goes.
            auto dialog = UltraCanvasDialogManager::CreateFileDialog(
                    FileConfigFrom(opts, FileDialogType::Save, "Save File"));
            UltraCanvasDialogManager::ShowDialog(
                dialog,
                [dialog = dialog.get(), onResult](DialogResult result) {
                    std::string path;
                    FileSaveImageSize size;
                    if (result == DialogResult::OK) {
                        path = dialog->GetSelectedFilePath();
                        size = dialog->GetSaveImageSize();
                    }
                    if (path.empty()) result = DialogResult::Cancel;
                    if (onResult) onResult(result, path, size);
                },
                opts.parentWindow);
            return;
        }

        FileDialogOptions effective = opts;
        if (effective.title.empty()) effective.title = "Save File";
        FoldFilterTogglesForNative(effective);

        std::string result = UltraCanvasNativeDialogs::SaveFile(effective);
        deliver(result.empty() ? DialogResult::Cancel : DialogResult::OK, result);
    }

    void UltraCanvasFileLoader::SelectFolderDialog(
            const FileDialogOptions& opts,
            std::function<void(DialogResult, const std::string&)> onResult) {

        if (!UltraCanvasDialogManager::GetUseNativeDialogs()) {
            ShowFrameworkFileDialog(
                FileConfigFrom(opts, FileDialogType::SelectFolder, "Select Folder"),
                opts.parentWindow,
                [onResult](DialogResult dr, const std::vector<std::string>& paths) {
                    if (onResult) {
                        onResult(dr, paths.empty() ? std::string() : paths.front());
                    }
                });
            return;
        }

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
            // Filters follow the runtime supported-format inventory, so the
            // dialog only offers what this build can actually decode.
            auto formats = UltraCanvasSupportedFormats::GetByCategory(MediaFormatCategory::Audio);
            std::vector<std::string> loadable;
            for (const auto& f : formats) {
                if (!f.canLoad) continue;
                loadable.push_back(f.extension);
                loadable.insert(loadable.end(), f.aliases.begin(), f.aliases.end());
            }
            if (!loadable.empty()) effective.AddFilter("Audio files", loadable);
            for (const auto& f : formats) {
                if (!f.canLoad) continue;
                std::vector<std::string> exts{ f.extension };
                exts.insert(exts.end(), f.aliases.begin(), f.aliases.end());
                effective.AddFilter(f.description + " (*." + f.extension + ")", exts);
            }
            effective.AddFilter("All files (*.*)", "*");
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

    namespace {
        std::string LowerAudioExtension(const std::string& path) {
            std::string ext = PathToUtf8(PathFromUtf8(path).extension());
            if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return ext;
        }
    }

    bool UltraCanvasFileLoader::SaveAudio(const UCAudio& audio, const std::string& path,
                                          std::string& outError) {
        outError.clear();
        if (!audio.IsValid()) { outError = "No sound to save"; return false; }
        const std::string ext = LowerAudioExtension(path);
        if (ext.empty() || !CanEncodeMediaExtension(MediaCodecKind::Audio, ext)) {
            outError = ext.empty() ? std::string("The name has no audio type (.wav, .mp3 ...)")
                                   : "This build does not write ." + ext + " audio";
            return false;
        }
        if (!audio.SaveToFile(path, AudioFormatFromExtension(ext))) {
            outError = "The " + ext + " encoder could not write " + path;
            return false;
        }
        return true;
    }

    bool UltraCanvasFileLoader::ConvertAudioFile(const std::string& sourcePath, const std::string& targetPath,
                                                 std::string& outError) {
        outError.clear();
        // The same type: the file as it is. Decoding and encoding it again
        // would cost a lossy format quality and gain nothing.
        if (LowerAudioExtension(sourcePath) == LowerAudioExtension(targetPath)) {
            FileBytesResult read = LoadFile(sourcePath, false);
            if (!read.success) { outError = read.error.empty() ? "Cannot read " + sourcePath : read.error; return false; }
            return SaveFile(targetPath, read.bytes, outError);
        }
        std::shared_ptr<UCAudio> audio = UCAudio::LoadFromFile(sourcePath);
        if (!audio || !audio->IsValid()) {
            outError = "Cannot decode " + sourcePath;
            return false;
        }
        return SaveAudio(*audio, targetPath, outError);
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

    namespace {
        // A format's extension and its aliases: one dialog filter's worth.
        std::vector<std::string> ExtensionsOf(const MediaFormatInfo& format) {
            std::vector<std::string> extensions = {format.extension};
            extensions.insert(extensions.end(), format.aliases.begin(), format.aliases.end());
            return extensions;
        }

        // The Save dialog starts on its first type: the one the proposed
        // name ends in goes there, so the two agree ("drawing.svg" opens on
        // SVG, not on the alphabet's first vector type).
        void PutDefaultTypeFirst(FileDialogOptions& opts) {
            std::string ext = PathToUtf8(PathFromUtf8(opts.defaultFileName).extension());
            if (ext.size() < 2) return;
            ext = ext.substr(1);
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            auto holds = [&ext](const FileFilter& filter) {
                return std::find(filter.extensions.begin(), filter.extensions.end(), ext) != filter.extensions.end();
            };
            auto it = std::find_if(opts.filters.begin(), opts.filters.end(), holds);
            if (it != opts.filters.end() && it != opts.filters.begin())
                std::rotate(opts.filters.begin(), it, it + 1);
        }
    } // namespace

    void UltraCanvasFileLoader::AddLoadTypes(FileDialogOptions& opts, const std::string& label,
                                             std::initializer_list<MediaFormatCategory> categories) {
        std::vector<std::string> extensions;
        for (MediaFormatCategory category : categories)
            for (const std::string& ext : GetSupportedLoadExtensions(category))
                if (std::find(extensions.begin(), extensions.end(), ext) == extensions.end())
                    extensions.push_back(ext);
        if (!extensions.empty()) opts.AddFilter(label, extensions);
    }

    void UltraCanvasFileLoader::AddSaveTypes(FileDialogOptions& opts, MediaFormatCategory category) {
        for (const MediaFormatInfo& format : GetSupportedFormats(category))
            if (format.canSave) opts.AddFilter(format.description, ExtensionsOf(format));
        PutDefaultTypeFirst(opts);
    }

    // ===== VECTOR DOCUMENTS =====

    std::shared_ptr<VectorStorage::VectorDocument> UltraCanvasFileLoader::LoadVectorDocument(
            const std::string& filePath, std::string& outError, std::vector<std::string>* notes) {
        outError.clear();
        if (PreviewableVectorExtensions().empty()) {
            outError = "No vector file readers are installed (RegisterVectorFormatsPlugin was not called).";
            return nullptr;
        }
        if (!CanPreviewVectorExtension(filePath)) {
            outError = "No reader for this file type.";
            return nullptr;
        }
        std::vector<std::string> collected;
        auto doc = ImportVectorDocument(filePath, [&collected](const std::string& n) {
            collected.push_back(n);
        });
        if (!doc) {
            outError = "Could not read the file";
            if (!collected.empty()) outError += ": " + collected.front();
        }
        if (notes) *notes = std::move(collected);
        return doc;
    }

    namespace {
        std::string LowerExtensionOf(const std::string& extensionOrPath) {
            std::string ext = extensionOrPath;
            if (ext.find('.') != std::string::npos || ext.find('/') != std::string::npos ||
                ext.find('\\') != std::string::npos) {
                ext = PathToUtf8(PathFromUtf8(extensionOrPath).extension());
                if (ext.empty() && !extensionOrPath.empty() && extensionOrPath[0] == '.') ext = extensionOrPath;
            }
            if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return ext;
        }

        // A vector writer's type as the Save dialog lists it.
        std::string VectorTypeName(const std::string& ext) {
            static const std::map<std::string, std::string> names = {
                {"pdf", "PDF"}, {"ai", "Adobe Illustrator"}, {"eps", "EPS"}, {"ps", "PostScript"},
                {"cdr", "CorelDRAW"}, {"svg", "SVG"}, {"svgz", "SVG (compressed)"}, {"xar", "Xara XAR"},
                {"dxf", "DXF"}, {"dwg", "DWG"}, {"emf", "EMF"}, {"wmf", "WMF"},
                {"odg", "OpenDocument Drawing"}, {"otg", "OpenDocument Drawing Template"},
                {"fodg", "OpenDocument Drawing (flat XML)"}};
            auto it = names.find(ext);
            if (it != names.end()) return it->second;
            std::string upper = ext;
            for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return upper;
        }
    }

    bool UltraCanvasFileLoader::SaveVectorDocument(const VectorStorage::VectorDocument& document,
                                                   const std::string& filePath, std::string& outError,
                                                   std::vector<std::string>* notes) {
        return SaveVectorDocument(document, filePath, FileSaveImageSize(), outError, notes);
    }

    bool UltraCanvasFileLoader::SaveVectorDocument(const VectorStorage::VectorDocument& document,
                                                   const std::string& filePath, const FileSaveImageSize& size,
                                                   std::string& outError, std::vector<std::string>* notes) {
        outError.clear();
        if (notes) notes->clear();
        const std::string ext = LowerExtensionOf(filePath);
        const auto writable = SavableVectorExtensions();
        if (std::find(writable.begin(), writable.end(), ext) == writable.end()) {
            if (!ext.empty() && CanSaveImage(ext)) {
                // A bitmap: the page drawn at the size asked for - a drawing
                // has no pixel size of its own, so it is sharp at any - and
                // written with the format's save settings.
                const Size2Di natural = GetVectorDocumentPixelSize(document);
                VectorRasterOptions options;
                options.width = size.IsOriginal() ? natural.width : size.width;
                options.height = size.IsOriginal() ? natural.height : size.height;
                auto layer = RasterizeVectorDocument(document, options, outError);
                if (!layer) return false;
                UCRasterDocument picture(layer->GetWidth(), layer->GetHeight(), RasterPixel(0, 0, 0, 0));
                picture.AddLayer(layer);
                return picture.SaveToFile(filePath, outError);
            }
            // The extension picks the format, so a name without one (saved
            // under "All files") has none to pick.
            outError = writable.empty() && !CanSaveImage(ext)
                       ? "No vector file writers are installed (RegisterVectorFormatsPlugin was not called)."
                       : ext.empty()
                       ? "The file name has no extension, so there is no format to save it in. "
                         "Add one, such as .svg, or choose the file type."
                       : "No writer for ." + ext;
            return false;
        }
        std::vector<std::string> collected;
        const bool ok = ExportVectorDocument(document, filePath, [&collected](const std::string& n) {
            collected.push_back(n);
        });
        if (!ok) {
            // The writer's own reason when it gave one (the file writers say
            // why the file could not be created or written), else what the
            // destination says.
            outError = "Could not save the file: " +
                       (!collected.empty() ? collected.front() : FileWriteFailureReason(filePath, 0));
        }
        if (notes) *notes = std::move(collected);
        return ok;
    }

    Size2Di UltraCanvasFileLoader::GetVectorDocumentPixelSize(const VectorStorage::VectorDocument& document) {
        Rect2Dd page = document.ViewBox;
        if (!(page.width > 0) || !(page.height > 0)) page = Rect2Dd(0, 0, document.Size.width, document.Size.height);
        if (!(page.width > 0) || !(page.height > 0)) return Size2Di(0, 0);
        constexpr double kPixelsPerPoint = 96.0 / 72.0;
        return Size2Di(std::max(1, static_cast<int>(std::lround(page.width * kPixelsPerPoint))),
                       std::max(1, static_cast<int>(std::lround(page.height * kPixelsPerPoint))));
    }

    bool UltraCanvasFileLoader::CanSaveImage(const std::string& extensionOrPath) {
        const std::string ext = LowerExtensionOf(extensionOrPath);
        if (ext.empty()) return false;
        for (const MediaFormatInfo& format : GetSupportedFormats(MediaFormatCategory::Bitmap))
            if (format.canSave && format.MatchesExtension(ext)) return true;
        return false;
    }

    void UltraCanvasFileLoader::AddImageSaveTypes(FileDialogOptions& opts) {
        AddSaveTypes(opts, MediaFormatCategory::Bitmap);
    }

    void UltraCanvasFileLoader::AddVectorDocumentSaveTypes(FileDialogOptions& opts,
                                                           const VectorStorage::VectorDocument& document,
                                                           bool withBitmaps) {
        for (const std::string& ext : SavableVectorExtensions()) opts.AddFilter(VectorTypeName(ext), ext);
        PutDefaultTypeFirst(opts);
        if (!withBitmaps) return;
        AddImageSaveTypes(opts);
        const Size2Di page = GetVectorDocumentPixelSize(document);
        opts.SetDrawingSize(page.width, page.height);
    }

    std::vector<std::string> UltraCanvasFileLoader::GetVectorLoadExtensions() {
        return PreviewableVectorExtensions();
    }

    std::vector<std::string> UltraCanvasFileLoader::GetVectorSaveExtensions() {
        return SavableVectorExtensions();
    }

    bool UltraCanvasFileLoader::CanLoadVectorDocument(const std::string& extensionOrPath) {
        return CanPreviewVectorExtension(extensionOrPath);
    }

    // ===== VIDEO SAVED =====
    // The core writes no video itself: a type is written from another file
    // by the converter its registration carries (convertVideo), which a
    // module with an encoder registers - VideoFX's, on FFmpeg.

    namespace {
        // The video registration whose converter writes `ext`, or null.
        std::shared_ptr<const MediaCodecRegistration> VideoConverterFor(const std::string& ext) {
            if (ext.empty()) return nullptr;
            auto codec = FindMediaCodecByExtension(MediaCodecKind::Video, ext);
            return codec && codec->convertVideo ? codec : nullptr;
        }

        // Where a conversion writes before the file is put in place: beside
        // the target, so the move is a rename on the same disk, with the
        // target's extension last - a converter picks the container by it -
        // and a name no other save is using.
        std::filesystem::path PartialVideoPath(const std::filesystem::path& target) {
            static std::atomic<unsigned> serial{0};
            for (;;) {
                std::filesystem::path partial = target.parent_path() / target.stem();
                partial += PathFromUtf8(".part" + std::to_string(++serial));
                partial += target.extension();
                std::error_code ec;
                if (!std::filesystem::exists(partial, ec)) return partial;
            }
        }

        bool PutVideoInPlace(const std::filesystem::path& partial, const std::filesystem::path& target,
                             std::string& error) {
            std::error_code ec;
            std::filesystem::rename(partial, target, ec);
            if (!ec) return true;
            std::error_code ignored;
            std::filesystem::remove(partial, ignored);
            error = "cannot put the file in place: " + ec.message();
            return false;
        }
    } // namespace

    bool UltraCanvasFileLoader::ConvertVideoFile(const std::string& sourcePath, const std::string& targetPath,
                                                 std::string& outError,
                                                 const std::function<bool(double fraction)>& progress) {
        namespace fs = std::filesystem;
        outError.clear();
        if (sourcePath.empty() || targetPath.empty()) { outError = "No file to save"; return false; }
        if (IsUrl(targetPath)) { outError = "a URL cannot be saved to"; return false; }
        const fs::path source = PathFromUtf8(sourcePath);
        const fs::path target = PathFromUtf8(targetPath);
        std::error_code ec;
        if (!fs::is_regular_file(source, ec)) { outError = "Cannot read " + sourcePath; return false; }
        // Saved over itself: the file is already there as it is.
        if (fs::equivalent(source, target, ec)) return true;
        const std::string ext = LowerExtensionOf(targetPath);
        if (ext.empty()) { outError = "The name has no video type (.mp4, .webm ...)"; return false; }

        const fs::path partial = PartialVideoPath(target);
        auto discard = [&partial]() {
            std::error_code ignored;
            fs::remove(partial, ignored);
        };
        // The same type: the file as it is. Decoding and encoding it again
        // would only lose quality, and take minutes doing it.
        if (LowerExtensionOf(sourcePath) == ext) {
            if (progress && !progress(0.0)) { outError = "Stopped"; return false; }
            fs::copy_file(source, partial, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                discard();
                outError = "cannot copy the file: " + ec.message();
                return false;
            }
            if (progress) progress(1.0);
            return PutVideoInPlace(partial, target, outError);
        }

        const auto codec = VideoConverterFor(ext);
        if (!codec) {
            outError = "This build does not write ." + ext + " video";
            return false;
        }
        // A converter that does not honour a "stop" is stopped all the same:
        // what it wrote is thrown away.
        bool stopped = false;
        MediaProgressFn report;
        if (progress) {
            report = [&progress, &stopped](double fraction) {
                if (stopped) return false;
                if (!progress(fraction)) stopped = true;
                return !stopped;
            };
        }
        std::string error;
        bool ok = false;
        try {
            ok = codec->convertVideo(sourcePath, PathToUtf8(partial), report, error);
        } catch (const std::exception& e) {
            ok = false;
            error = e.what();
        } catch (...) {
            ok = false;                  // a throwing converter must not take the program down
            error = "the converter failed";
        }
        if (ok && !stopped && !fs::is_regular_file(partial, ec)) {
            ok = false;
            error = "the converter wrote no file";
        }
        if (!ok || stopped) {
            discard();
            outError = stopped ? std::string("Stopped")
                     : error.empty() ? "The ." + ext + " converter could not write " + targetPath
                                     : error;
            return false;
        }
        return PutVideoInPlace(partial, target, outError);
    }

    bool UltraCanvasFileLoader::CanConvertVideoTo(const std::string& extensionOrPath) {
        return VideoConverterFor(LowerExtensionOf(extensionOrPath)) != nullptr;
    }

    std::vector<std::string> UltraCanvasFileLoader::GetVideoSaveExtensions() {
        std::vector<std::string> out;
        for (const MediaCodecRegistration& codec : GetRegisteredMediaCodecs(MediaCodecKind::Video)) {
            if (!codec.convertVideo || codec.probeFile) continue;
            out.push_back(codec.extension);
            out.insert(out.end(), codec.aliases.begin(), codec.aliases.end());
        }
        return out;
    }

    void UltraCanvasFileLoader::AddVideoSaveTypes(FileDialogOptions& opts) {
        // Straight from the registry, as the inventory's video rows are: a
        // type a content probe decides (".ts") is not one a name can pick.
        for (const MediaCodecRegistration& codec : GetRegisteredMediaCodecs(MediaCodecKind::Video)) {
            if (!codec.convertVideo || codec.probeFile) continue;
            std::vector<std::string> extensions = {codec.extension};
            extensions.insert(extensions.end(), codec.aliases.begin(), codec.aliases.end());
            opts.AddFilter(codec.description.empty() ? codec.extension : codec.description, extensions);
        }
        PutDefaultTypeFirst(opts);
    }

    std::shared_ptr<UCRichDocument> UltraCanvasFileLoader::LoadTextDocument(
            const std::string& filePath, std::string& outError, const std::string& password) {
        outError.clear();

        // A PowerPoint deck reads as an outline of its slides. A protected one
        // is a compound file whose content shows only once decrypted, so it
        // goes by its extension.
        if (IsPptxPackage(filePath) || (IsPptxExtension(filePath) && IsEncryptedOfficePackage(filePath))) {
            PptxPresentation deck;
            if (!ReadPptxPresentation(filePath, deck, outError, password)) return nullptr;
            return std::make_shared<UCRichDocument>(PptxPresentationToRichDocument(deck));
        }
        // An OpenDocument presentation the same way, by its content.
        if (IsOdpPresentationFile(filePath)) {
            PptxPresentation deck;
            if (!ReadOdpPresentation(filePath, deck, outError, password)) return nullptr;
            return std::make_shared<UCRichDocument>(PptxPresentationToRichDocument(deck));
        }

        // Package formats (and renamed/legacy files) go by content signature.
        WordDocumentFormat format = DetectWordDocumentFormat(filePath);
        if (format != WordDocumentFormat::Unknown) {
            auto document = std::make_shared<UCRichDocument>();
            if (!UCWordDocumentIO::Load(filePath, *document, outError, password)) {
                return nullptr;
            }
            return document;
        }

        // A deck's name on a file that is no deck (damaged, or something
        // else renamed) is not text to show.
        if (IsPptxExtension(filePath) || IsOdpExtension(filePath)) {
            outError = std::string("The file is not a valid ") +
                       (IsPptxExtension(filePath) ? "PowerPoint" : "OpenDocument") + " presentation: " + filePath;
            return nullptr;
        }

        // Everything else is treated as (markdown-flavored) text.
        std::ifstream file(PathFromUtf8(filePath), std::ios::binary);
        if (!file.is_open()) {
            outError = "Cannot open file: " + filePath;
            return nullptr;
        }
        std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
        std::string baseDirectory = PathToUtf8(PathFromUtf8(filePath).parent_path());
        return std::make_shared<UCRichDocument>(
            UCRichDocument::FromMarkdown(text, baseDirectory));
    }

    namespace {
        // The document types LoadTextDocument reads: the Word document reader's
        // (ODT, DOCX, DOC, LaTeX), PowerPoint and OpenDocument decks as outlines, and the
        // Markdown text path's.
        bool TextDocumentReads(const MediaFormatInfo& format) {
            if (WordDocumentFormatFromExtension(format.extension) != WordDocumentFormat::Unknown) return true;
            if (IsPptxExtension(format.extension) || IsOdpExtension(format.extension)) return true;
            return format.extension == "md" || format.extension == "txt";
        }

        // ... and the ones SaveTextDocument writes: a document, or the
        // document laid out as a PowerPoint deck.
        bool TextDocumentWrites(const MediaFormatInfo& format) {
            const WordDocumentFormat type = WordDocumentFormatFromExtension(format.extension);
            return format.canSave && (type == WordDocumentFormat::Odt || type == WordDocumentFormat::Docx ||
                                      IsPptxExtension(format.extension));
        }
    } // namespace

    void UltraCanvasFileLoader::AddTextDocumentLoadTypes(FileDialogOptions& opts) {
        std::vector<MediaFormatInfo> formats;
        for (const MediaFormatInfo& format : GetSupportedFormats(MediaFormatCategory::Document))
            if (format.canLoad && TextDocumentReads(format)) formats.push_back(format);
        std::vector<std::string> all;
        for (const MediaFormatInfo& format : formats)
            for (const std::string& ext : ExtensionsOf(format)) all.push_back(ext);
        if (all.empty()) return;
        opts.AddFilter("Documents", all);
        for (const MediaFormatInfo& format : formats) opts.AddFilter(format.description, ExtensionsOf(format));
    }

    bool UltraCanvasFileLoader::SaveTextDocument(const UCRichDocument& document, const std::string& filePath,
                                                 std::string& outError, const std::string& password) {
        // A PowerPoint path: the document laid out as a deck, a slide per
        // first-level heading.
        if (IsPptxExtension(filePath))
            return SavePresentation(PptxPresentationFromRichDocument(document), filePath, outError, password);
        return UCWordDocumentIO::Save(filePath, document, outError, password);
    }

    bool UltraCanvasFileLoader::SavePresentation(const PptxPresentation& deck, const std::string& filePath,
                                                 std::string& outError, const std::string& password) {
        outError.clear();
        if (!IsPptxExtension(filePath)) {
            outError = "Presentations are saved as PowerPoint files (.pptx, .potx, .ppsx): " + filePath;
            return false;
        }
        return WritePptxPresentation(deck, filePath, outError, password);
    }

    void UltraCanvasFileLoader::AddTextDocumentSaveTypes(FileDialogOptions& opts) {
        for (const MediaFormatInfo& format : GetSupportedFormats(MediaFormatCategory::Document))
            if (TextDocumentWrites(format)) opts.AddFilter(format.description, ExtensionsOf(format));
        PutDefaultTypeFirst(opts);
    }

    void UltraCanvasFileLoader::OpenTextDocument(
            const FileDialogOptions& opts,
            std::function<void(const FileLoadResult&, std::shared_ptr<UCRichDocument>)> onResult) {

        FileDialogOptions effective = opts;
        if (effective.title.empty()) effective.title = "Open Document";
        if (effective.filters.empty()) {
            AddTextDocumentLoadTypes(effective);
            effective.AddFilter("All files (*.*)", "*");
        }

        UltraCanvasWindowBase* parent = effective.parentWindow;
        OpenFileDialog(effective, [onResult, parent](DialogResult r, const std::string& path) {
            if (r != DialogResult::OK || path.empty()) {
                FileLoadResult loadResult;
                loadResult.dialogResult = r;
                if (onResult) onResult(loadResult, nullptr);
                return;
            }
            RequestDocumentPassword(path, parent,
                [onResult, path](bool proceed, const std::string& password) {
                    FileLoadResult loadResult;
                    loadResult.dialogResult = proceed ? DialogResult::OK : DialogResult::Cancel;
                    std::shared_ptr<UCRichDocument> document;
                    if (proceed) document = LoadTextDocument(path, loadResult.loadError, password);
                    if (onResult) onResult(loadResult, document);
                });
        });
    }

    namespace {
        // One round of RequestDocumentPassword's prompt; `notice` (the last
        // attempt's error) heads the prompt after a wrong password.
        void AskDocumentPassword(const std::string& filePath, UltraCanvasWindowBase* parent,
                                 const std::string& notice,
                                 std::function<void(bool, const std::string&)> onResult) {
            const std::string name = PathToUtf8(PathFromUtf8(filePath).filename());
            const std::string prompt = (notice.empty() ? std::string() : notice + ".\n") +
                                       "\"" + name + "\" is password-protected. Password:";
            InputDialogConfig ask;
            ask.title = "Password";
            ask.inputLabel = prompt;
            ask.inputType = InputType::Password;
            // The lock UltraViewer's Unlock shows, centred above the field.
            ask.iconAboveInput = NormalizePath(GetResourcesDir() + "media/icons/unlock.svg");
            UltraCanvasDialogManager::ShowInputDialog(ask,
                [filePath, parent, onResult](DialogResult result, const std::string& password) {
                    if (result != DialogResult::OK) {
                        onResult(false, "");
                        return;
                    }
                    std::string error;
                    switch (CheckOfficePassword(filePath, password, error)) {
                        case OfficeDecryptStatus::WrongPassword:
                        case OfficeDecryptStatus::PasswordRequired:
                            AskDocumentPassword(filePath, parent, error, onResult);
                            return;
                        default:
                            // Right, or a failure the loader reports in its own words.
                            onResult(true, password);
                            return;
                    }
                }, parent);
        }
    } // namespace

    namespace {
        // One round of RequestNewDocumentPassword: the password, then the
        // same again; `notice` heads the first prompt after a failed round.
        void AskNewDocumentPassword(UltraCanvasWindowBase* parent, const std::string& notice,
                                    std::function<void(bool, const std::string&)> onResult) {
            const std::string prompt = (notice.empty() ? std::string() : notice + "\n") +
                                       "Password to protect the file with:";
            UltraCanvasDialogManager::ShowInputDialog(prompt, "Save with Password", "", InputType::Password,
                [parent, onResult](DialogResult result, const std::string& password) {
                    if (result != DialogResult::OK) {
                        onResult(false, "");
                        return;
                    }
                    if (password.empty()) {
                        AskNewDocumentPassword(parent, "The password cannot be empty.", onResult);
                        return;
                    }
                    UltraCanvasDialogManager::ShowInputDialog("Type the password again:", "Save with Password", "",
                        InputType::Password,
                        [parent, onResult, password](DialogResult again, const std::string& repeated) {
                            if (again != DialogResult::OK) {
                                onResult(false, "");
                            } else if (repeated != password) {
                                AskNewDocumentPassword(parent, "The two passwords differ.", onResult);
                            } else {
                                onResult(true, password);
                            }
                        }, parent);
                }, parent);
        }
    } // namespace

    void UltraCanvasFileLoader::RequestNewDocumentPassword(
            UltraCanvasWindowBase* parent,
            std::function<void(bool proceed, const std::string& password)> onResult) {
        if (onResult) AskNewDocumentPassword(parent, "", std::move(onResult));
    }

    void UltraCanvasFileLoader::RequestDocumentPassword(
            const std::string& filePath, UltraCanvasWindowBase* parent,
            std::function<void(bool proceed, const std::string& password)> onResult) {
        if (!onResult) return;
        // With no password the check stops at the header: PasswordRequired
        // for a file it can open, without deriving any key.
        std::string error;
        if (CheckOfficePassword(filePath, "", error) == OfficeDecryptStatus::PasswordRequired) {
            AskDocumentPassword(filePath, parent, "", std::move(onResult));
        } else {
            onResult(true, "");
        }
    }

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    // The shared desktop definition of a UltraCanvasNativeDialogs static.
    //
    // It lives here rather than being copied into each OS/<platform> dialogs
    // file because on every desktop backend it is the same two steps - ask for
    // a path, write the bytes - and there is nothing platform-specific left to
    // vary. Android needs a genuinely different implementation (the Storage
    // Access Framework never yields a path), so it defines its own in
    // OS/Android/UltraCanvasAndroidNativeDialogs.cpp and this one is compiled
    // out. WebAssembly likewise: a page cannot write to the user's disk, so
    // OS/WASM/UltraCanvasWASMNativeDialogs.cpp hands the bytes to the
    // browser's download manager instead.
    bool UltraCanvasNativeDialogs::SaveContent(const void* data, std::size_t size,
                                               const FileDialogOptions& options) {
        if (!data && size > 0) return false;

        const std::string path = SaveFile(options);
        if (path.empty()) return false;   // user cancelled

        std::ofstream out(UltraCanvas::PathFromUtf8(path), std::ios::binary | std::ios::trunc);
        if (!out) return false;
        if (size > 0) {
            out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        }
        out.close();
        return out.good();
    }
#endif

} // namespace UltraCanvas
