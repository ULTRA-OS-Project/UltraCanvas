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
    class UCAudio;
    class UCRichDocument;

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

        // Opens the native file dialog and decodes the chosen file into a UCAudio
        // PCM buffer (WAV/MP3/FLAC/Vorbis, via the active audio backend). If the
        // caller supplies no filters, a sensible set of audio filters is added.
        // On cancel, onResult fires with dialogResult == Cancel and a null audio;
        // on decode failure, loadError is populated and the audio is null/invalid.
        static void OpenAudio(const FileDialogOptions& opts,
                              std::function<void(const FileLoadResult&, std::shared_ptr<UCAudio>)> onResult);

        // Opens the native file dialog and parses the chosen word-processing
        // document into a UCRichDocument (see Plugins/Documents/Word). Formats:
        // .odt and .docx are fully parsed (detection is signature-based, so
        // renamed files still load); .md/.markdown parse as Markdown; anything
        // else readable as text becomes plain paragraphs. Legacy Word 97-2003
        // .doc files are detected and rejected with a clear loadError telling
        // the user to convert to .docx. If the caller supplies no filters, a
        // sensible set of document filters is added. On cancel, onResult fires
        // with dialogResult == Cancel and a null document; on parse failure,
        // loadError is populated and the document is null.
        static void OpenTextDocument(const FileDialogOptions& opts,
                                     std::function<void(const FileLoadResult&, std::shared_ptr<UCRichDocument>)> onResult);

        // Dialog-free core of OpenTextDocument: parses filePath into a
        // UCRichDocument. Returns null and fills outError on failure.
        static std::shared_ptr<UCRichDocument> LoadTextDocument(const std::string& filePath,
                                                                std::string& outError);
    };

} // namespace UltraCanvas
