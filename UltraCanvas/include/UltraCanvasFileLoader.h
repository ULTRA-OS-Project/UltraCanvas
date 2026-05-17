// include/UltraCanvasFileLoader.h
// Facade for native file selection dialogs and typed file loaders (e.g. images).
// Owns FileDialogOptions/FileLoadResult and delegates raw dialog calls to
// UltraCanvasNativeDialogs. NotifyRecentFile is implemented per-platform.
// Version: 1.0.0
// Last Modified: 2026-05-12
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasModalDialog.h"   // FileFilter, DialogResult, UltraCanvasWindowBase
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace UltraCanvas {

    class UCImageRaster;
    using UCImage = UCImageRaster;

    struct FileDialogOptions {
        std::string title;
        std::string initialDirectory;
        std::string defaultFileName;
        std::vector<FileFilter> filters;
        bool showHiddenFiles = false;
        bool registerAsRecent = true;          // opt-out for NotifyRecentFile
        UltraCanvasWindowBase* parentWindow = nullptr;

        FileDialogOptions() = default;

        FileDialogOptions& SetTitle(const std::string& t)            { title = t; return *this; }
        FileDialogOptions& SetInitialDirectory(const std::string& d) { initialDirectory = d; return *this; }
        FileDialogOptions& SetDefaultFileName(const std::string& n)  { defaultFileName = n; return *this; }
        FileDialogOptions& AddFilter(const std::string& desc, const std::string& ext) {
            filters.emplace_back(desc, ext);
            return *this;
        }
        FileDialogOptions& AddFilter(const std::string& desc, const std::vector<std::string>& exts) {
            filters.emplace_back(desc, exts);
            return *this;
        }
        FileDialogOptions& SetShowHidden(bool v)        { showHiddenFiles = v; return *this; }
        FileDialogOptions& SetRegisterAsRecent(bool v)  { registerAsRecent = v; return *this; }
        FileDialogOptions& SetParentWindow(UltraCanvasWindowBase* p) { parentWindow = p; return *this; }
    };

    struct FileLoadResult {
        DialogResult dialogResult = DialogResult::Cancel;
        std::string  loadError;

        bool IsSuccess() const {
            return dialogResult == DialogResult::OK && loadError.empty();
        }
    };

    // Byte-level load result for LoadFile (local path or URL).
    // contentType / httpStatus are only populated for URL loads.
    struct FileBytesResult {
        bool                 success = false;
        std::string          error;
        std::vector<uint8_t> bytes;
        std::string          sourceUri;
        std::string          contentType;
        int                  httpStatus = 0;
    };

    class UltraCanvasFileLoader {
    public:
        // ===== GENERIC FILE LOAD (path or URL) =====
        // Reads a local path or fetches a remote URL into memory. URL schemes
        // http:// and https:// are dispatched to UltraNet when the framework
        // is built with ULTRACANVAS_HAS_NET; ftp:// / sftp:// will be supported
        // once the UltraNet FTP module lands (Stage 3 of the UltraNet plan).
        // Everything else is treated as a local filesystem path.
        static FileBytesResult LoadFile(const std::string& pathOrUrl);

        // True if `s` starts with a recognised URL scheme (http/https/ftp/ftps/sftp).
        static bool IsUrl(const std::string& s);

        // ===== FILE SELECTION DIALOGS =====
        static void OpenFileDialog(const FileDialogOptions& opts,
                                   std::function<void(DialogResult, const std::string&)> onResult);

        static void OpenMultipleFilesDialog(const FileDialogOptions& opts,
                                            std::function<void(DialogResult, const std::vector<std::string>&)> onResult);

        static void SaveFileDialog(const FileDialogOptions& opts,
                                   std::function<void(DialogResult, const std::string&)> onResult);

        static void SelectFolderDialog(const FileDialogOptions& opts,
                                       std::function<void(DialogResult, const std::string&)> onResult);

        // ===== OS RECENT FILES INTEGRATION =====
        // Registers the path with the platform shell's recent-documents list
        // (Windows SHAddToRecentDocs, macOS NSDocumentController, Linux GtkRecentManager).
        // Implemented per platform in OS/<platform>/UltraCanvas*FileLoader.cpp.
        static void NotifyRecentFile(const std::string& filePath);

        // ===== TYPED OBJECT LOADERS =====
        static void OpenImage(const FileDialogOptions& opts,
                              std::function<void(const FileLoadResult&, std::shared_ptr<UCImage>)> onResult);
    };

} // namespace UltraCanvas
