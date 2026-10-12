// include/UltraCanvasFileLoader.h
// Facade for native file selection dialogs and typed file loaders (e.g. images).
// Owns FileDialogOptions/FileLoadResult and delegates raw dialog calls to
// UltraCanvasNativeDialogs. NotifyRecentFile is implemented per-platform.
// Save settings (SAVE SETTINGS below): what each format's writer can be
// told, described and kept per format - see UltraCanvasFileSaveSettings.h.
// Version: 1.2.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasModalDialog.h"   // FileFilter, DialogResult, UltraCanvasWindowBase
#include "UltraCanvasSupportedFormats.h"
#include "UltraCanvasFileSaveSettings.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace UltraCanvas {

    class UCImageRaster;
    using UCImage = UCImageRaster;
    class UCAudio;
    class UCRichDocument;
    namespace VectorStorage { class VectorDocument; }
    namespace UCImageSave { struct ImageExportOptions; }

    struct FileDialogOptions {
        std::string title;
        std::string initialDirectory;
        std::string defaultFileName;
        std::vector<FileFilter> filters;
        bool showHiddenFiles = false;
        // The framework dialog shows the filters as toggle buttons rather than
        // a dropdown (FileDialogConfig::filterToggles). A native dialog has no
        // such thing, so it gets the filters as a list headed by one that
        // matches all of them.
        bool filterToggles = false;
        // Save: the framework dialog asks before replacing an existing file
        // (FileDialogConfig::confirmOverwrite). The platforms' save dialogs
        // always ask.
        bool confirmOverwrite = true;
        // The framework dialog's listing shows the Filer's hover icon menu
        // (FileDialogConfig::hoverIconMenu); off by default.
        bool hoverIconMenu = false;
        // Save: the extension ("png", undotted) a name gets when it still has
        // none after the chosen type's - under All files, or with no filters
        // (ApplyDefaultExtension). Empty: such a name stays bare. Every
        // dialog, framework or native, applies it.
        std::string defaultExtension;
        bool registerAsRecent = true;          // opt-out for NotifyRecentFile
        // Save: the framework dialog's Settings button on the type row,
        // which edits the chosen type's save settings (GetSaveSettings below;
        // FileDialogConfig::saveSettingsButton). Turn it off when the file is
        // written by a writer that does not read them.
        bool saveSettingsButton = true;
        // Save: the size of the picture being saved, in pixels (0: not a
        // picture, or not said). With it, the settings of a bitmap type offer
        // to write it at another size, and the size chosen comes back through
        // the SaveFileDialog that reports a FileSaveImageSize
        // (FileDialogConfig::imageWidth / imageHeight).
        int imageWidth = 0;
        int imageHeight = 0;
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
        FileDialogOptions& SetFilterToggles(bool v)     { filterToggles = v; return *this; }
        FileDialogOptions& SetConfirmOverwrite(bool v)  { confirmOverwrite = v; return *this; }
        FileDialogOptions& SetHoverIconMenu(bool v)     { hoverIconMenu = v; return *this; }
        FileDialogOptions& SetDefaultExtension(const std::string& e) { defaultExtension = e; return *this; }
        FileDialogOptions& SetRegisterAsRecent(bool v)  { registerAsRecent = v; return *this; }
        FileDialogOptions& SetSaveSettingsButton(bool v) { saveSettingsButton = v; return *this; }
        FileDialogOptions& SetImageSize(int w, int h)   { imageWidth = w; imageHeight = h; return *this; }
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
        // Compression method the content was transparently decompressed
        // from ("GZIP"/"Deflate"/"Zstandard"/"LZ4"), empty if the file was
        // not compressed or autoDecompress was disabled.
        std::string          decompressedFrom;
    };

    class UltraCanvasFileLoader {
    public:
        // ===== GENERIC FILE LOAD (path or URL) =====
        // Reads a local path or fetches a remote URL into memory. URL schemes
        // http:// and https:// are dispatched to UltraNet when the framework
        // is built with ULTRACANVAS_HAS_NET; ftp:// / sftp:// will be supported
        // once the UltraNet FTP module lands (Stage 3 of the UltraNet plan).
        // Everything else is treated as a local filesystem path.
        //
        // Compressed content (gzip/zlib/Zstandard/LZ4 streams, detected by
        // magic bytes, independent of file extension) is transparently
        // decompressed via VirtualFS, so callers always receive the plain
        // payload; result.decompressedFrom records the method. Pass
        // autoDecompress = false to receive the raw bytes instead (e.g. when
        // copying a .gz file verbatim). Content that only looks compressed
        // but fails to decode is returned unmodified. Archive containers
        // (ZIP, 7z, TAR, ...) are not unpacked here - navigate those with
        // VirtualFS. Requires ULTRACANVAS_USE_VIRTUALFS (default ON);
        // without it the raw bytes are returned.
        static FileBytesResult LoadFile(const std::string& pathOrUrl,
                                        bool autoDecompress = true);

        // True if `s` starts with a recognised URL scheme (http/https/ftp/ftps/sftp).
        static bool IsUrl(const std::string& s);

        // ===== FILE SELECTION DIALOGS =====
        static void OpenFileDialog(const FileDialogOptions& opts,
                                   std::function<void(DialogResult, const std::string&)> onResult);

        static void OpenMultipleFilesDialog(const FileDialogOptions& opts,
                                            std::function<void(DialogResult, const std::vector<std::string>&)> onResult);

        static void SaveFileDialog(const FileDialogOptions& opts,
                                   std::function<void(DialogResult, const std::string&)> onResult);
        // The same, for a picture: `size` is the pixel size chosen for this
        // save in the Settings of a bitmap type (the Size row, offered when
        // opts carries the picture's size - SetImageSize), IsOriginal() when
        // none was chosen, the type is not a bitmap format, or the dialog was
        // the platform's. Write it with GetImageExportOptions(path, size, ...).
        static void SaveFileDialog(const FileDialogOptions& opts,
                                   std::function<void(DialogResult, const std::string&,
                                                      const FileSaveImageSize& size)> onResult);

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
        // renamed files still load); .tex is imported by the LaTeX document
        // reader (article subset, formulas kept for the math engine);
        // .md/.markdown parse as Markdown; anything
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

        // ===== VECTOR DOCUMENTS =====
        // Editable drawings: SVG, XAR, EMF, WMF, DXF, DWG, AI ... read into
        // the shared VectorStorage::VectorDocument model and written back from
        // it, by the Vector plugin's converters - the same readers the media
        // viewer and the Filer preview with. (UCImage also opens .svg, through
        // librsvg, but as pixels: fine to look at, nothing to edit.)
        // Needs RegisterVectorFormatsPlugin() at start-up; without it every
        // call reports that no reader or writer is installed.
        //
        // `notes`, when given, receives what the reader or writer could not
        // carry over ("CSS stylesheets are not supported", "blend flattened").
        static std::shared_ptr<VectorStorage::VectorDocument> LoadVectorDocument(
                const std::string& filePath, std::string& outError,
                std::vector<std::string>* notes = nullptr);
        static bool SaveVectorDocument(const VectorStorage::VectorDocument& document,
                                       const std::string& filePath, std::string& outError,
                                       std::vector<std::string>* notes = nullptr);
        // Lowercase, undotted, sorted: ready for FileDialogOptions::AddFilter.
        static std::vector<std::string> GetVectorLoadExtensions();
        static std::vector<std::string> GetVectorSaveExtensions();
        // True when LoadVectorDocument reads this extension (or path; a path
        // also lets a content-decided format such as a DWG .bak claim it).
        static bool CanLoadVectorDocument(const std::string& extensionOrPath);

        // ===== SUPPORTED FORMAT INVENTORY =====
        // Runtime snapshot of every file format this build can load and/or
        // save, per media category, including the library or plugin providing
        // each (see UltraCanvasSupportedFormats.h). The extension lists are
        // ready to feed into FileDialogOptions::AddFilter.
        static std::vector<MediaFormatInfo> GetSupportedFormats();
        static std::vector<MediaFormatInfo> GetSupportedFormats(MediaFormatCategory category);
        static std::vector<std::string> GetSupportedLoadExtensions(MediaFormatCategory category);
        static std::vector<std::string> GetSupportedSaveExtensions(MediaFormatCategory category);

        // ===== SAVE SETTINGS =====
        // What each format's writer can be told (JPEG quality, PNG
        // compression, TIFF codec ...) and what the user chose, per format,
        // shared by every application and remembered between runs - see
        // UltraCanvasFileSaveSettings.h. `extensionOrPath` is "jpg", ".JPG",
        // "photo.jpeg" or a whole path: whichever of the format's extensions
        // it ends in.
        //
        // The formats: the bitmap formats the image writer takes options for
        // are built in; a plugin or an application adds its own (or replaces
        // a built-in one: same first extension) with RegisterSaveFormat.
        static void RegisterSaveFormat(const FileSaveFormat& format);
        static std::vector<FileSaveFormat> GetSaveFormats();
        // The format `extensionOrPath` is saved as, when it has settings.
        static bool FindSaveFormat(const std::string& extensionOrPath, FileSaveFormat& out);
        static bool HasSaveSettings(const std::string& extensionOrPath);

        // The values: every setting of the format, the user's where they set
        // it and the default elsewhere. Empty (no format) for a type without
        // settings.
        static FileSaveSettings GetSaveSettings(const std::string& extensionOrPath);
        // Whether the user has set any of the format's settings. Until then a
        // writer that has defaults of its own keeps them.
        static bool HasStoredSaveSettings(const std::string& extensionOrPath);
        // Remembers `settings` for the format they name (FileSaveSettings::
        // format, or any of its extensions): every value is normalized to
        // what its setting can hold, and a key the format does not have is
        // dropped. False when there is no such format or the file could not
        // be written.
        static bool SetSaveSettings(const FileSaveSettings& settings);
        // Forgets what the user set for the format: back to its defaults.
        static bool ResetSaveSettings(const std::string& extensionOrPath);

        // A dialog with the format's settings - a checkbox, a slider or a
        // list per setting - which stores them on OK. `onClosed` gets OK or
        // Cancel. Does nothing but call `onClosed` with Cancel for a type
        // without settings. The framework Save dialog's Settings button opens
        // this for the type being saved.
        static void ShowSaveSettingsDialog(const std::string& extensionOrPath,
                                           UltraCanvasWindowBase* parent = nullptr,
                                           std::function<void(DialogResult)> onClosed = nullptr);
        // The same for a picture of `pictureWidth` x `pictureHeight` pixels:
        // a bitmap format's dialog starts with a Size row (width x height,
        // Keep proportions), opened on `size` (IsOriginal: the picture's own),
        // so it is offered even for a bitmap format without settings. The
        // size is not stored with the settings: `onClosed` hands it back, as
        // IsOriginal() when it is the picture's own.
        static void ShowSaveSettingsDialog(const std::string& extensionOrPath,
                                           int pictureWidth, int pictureHeight,
                                           const FileSaveImageSize& size,
                                           UltraCanvasWindowBase* parent,
                                           std::function<void(DialogResult,
                                                              const FileSaveImageSize&)> onClosed);

        // The image writer's options (UCImageRaster::Save, ExportVImage) for
        // `path`: its format from the extension, and that format's save
        // settings. False when the image writer does not write that
        // extension; `out` is then untouched. UCRasterDocument::SaveToFile
        // uses this when the user has stored settings for the format.
        static bool GetImageExportOptions(const std::string& path,
                                          UCImageSave::ImageExportOptions& out);
        // ... written at `size` (targetWidth / targetHeight) unless it is the
        // picture's own.
        static bool GetImageExportOptions(const std::string& path, const FileSaveImageSize& size,
                                          UCImageSave::ImageExportOptions& out);
    };

} // namespace UltraCanvas
