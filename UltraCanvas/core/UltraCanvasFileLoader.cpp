// core/UltraCanvasFileLoader.cpp
// Cross-platform implementation of the file selection dialog facade.
// NotifyRecentFile is implemented per-platform in OS/<platform>/UltraCanvas*FileLoader.cpp.
// Version: 1.0.0
// Last Modified: 2026-05-12
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasImage.h"

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

} // namespace UltraCanvas
