// Apps/UltraVideo/UltraVideoDialogs.h
// The export dialog (Docs/Research/UltraVideoDesignProposal.md §6.10): a
// preset, the output file with a Browse button, a whole-project or work-area
// choice, and Export, which hands the settings back to the window to run the
// job. Also the unsaved-changes question. The story wizard and the project
// settings come in a later release.
// Version: 0.1.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraVideoDocument.h"
#include "UltraVideoTheme.h"

#include "UltraCanvasWindow.h"

#include <functional>
#include <string>

namespace UltraVideo {

struct ExportChoice {
    VideoFX::VideoFXExportSettings settings;
    std::string outputPath;
    bool workAreaOnly = false;
};

// Opens the export dialog over `parent`; `onExport` runs when the user
// chooses Export with a usable path.
void ShowExportDialog(UltraCanvas::UltraCanvasWindow* parent, const Document& document, const Palette& palette,
                      std::function<void(const ExportChoice&)> onExport);

// "Save the changes to <name>?" - Save, Don't save, Cancel
enum class DiscardAnswer { Save, Discard, Cancel };
void AskToSave(UltraCanvas::UltraCanvasWindow* parent, const std::string& name,
               std::function<void(DiscardAnswer)> onAnswer);

} // namespace UltraVideo
