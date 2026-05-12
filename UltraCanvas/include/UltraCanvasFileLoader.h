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

    class UltraCanvasFileLoader {
    public:
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
