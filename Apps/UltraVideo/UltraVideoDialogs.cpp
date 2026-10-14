// Apps/UltraVideo/UltraVideoDialogs.cpp
// The export dialog and the unsaved-changes question (UltraVideoDialogs.h).
// Version: 0.3.0 - no palette parameter: the dialogs follow the theme (surface, message, buttons, captions)
// Version: 0.2.0 - Browse offers the presets' types under FileLoader's names, and a type picked there picks its preset
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework
#include "UltraVideoDialogs.h"
#include "UltraVideoFormats.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasTextInput.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <vector>

using namespace UltraCanvas;
using namespace VideoFX;
namespace fs = std::filesystem;

namespace UltraVideo {

namespace {

constexpr int kDialogWidth = 520;
constexpr int kContentWidth = 470;

struct Preset {
    const char* label;
    const char* extension;
    VideoFXExportSettings settings;
};

std::vector<Preset> Presets() {
    std::vector<Preset> presets;
    presets.push_back({"Web video, 1080p (MP4, H.264)", ".mp4", VideoFXExportSettings::WebMP4(1080)});
    presets.push_back({"Web video, 720p (MP4, H.264)", ".mp4", VideoFXExportSettings::WebMP4(720)});
    presets.push_back({"Web video, 4K (MP4, H.264)", ".mp4", VideoFXExportSettings::WebMP4(2160)});
    presets.push_back({"WebM, 720p (VP9)", ".webm", VideoFXExportSettings::WebM(720)});
    presets.push_back({"Animated GIF, 480 px", ".gif", VideoFXExportSettings::AnimatedGif(480)});
    presets.push_back({"Master (ProRes 422 in MOV)", ".mov", VideoFXExportSettings::MasterProRes()});
    presets.push_back({"Sound only (MP3)", ".mp3", VideoFXExportSettings::AudioOnlyMP3()});
    return presets;
}

std::string DefaultOutput(const Document& doc, const char* extension) {
    fs::path folder;
    if (!doc.path.empty()) folder = PathFromUtf8(doc.path).parent_path();
    if (folder.empty()) {
        std::error_code ec;
        folder = fs::current_path(ec);
    }
    return PathToUtf8(folder / PathFromUtf8(doc.Name() + extension));
}

std::string WithExtension(const std::string& path, const char* extension) {
    fs::path p = PathFromUtf8(path);
    p.replace_extension(extension);
    return PathToUtf8(p);
}

} // namespace

void ShowExportDialog(UltraCanvasWindow* parent, const Document& doc,
                      std::function<void(const ExportChoice&)> onExport) {
    DialogConfig config;
    config.title = "Export " + doc.Name();
    config.message = "The whole timeline becomes one file. " + doc.Describe() + " unless the preset says otherwise.";
    config.dialogType = DialogType::Information;
    config.showIcon = false;
    config.buttons = DialogButtons::NoButtons;
    config.width = kDialogWidth;
    config.height = 330;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    dialog->SetIconVisible(false);

    auto presets = std::make_shared<std::vector<Preset>>(Presets());
    auto state = std::make_shared<ExportChoice>();
    state->settings = presets->front().settings;
    state->outputPath = DefaultOutput(doc, presets->front().extension);

    auto presetRow = std::make_shared<UltraCanvasContainer>("uv.export.presetRow", 0, 0, kContentWidth, 32);
    presetRow->layout.SetFlexRow().SetFlexGap(12).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto presetLabel = CreateLabel("uv.export.presetLabel", 0, 0, 90, 28, "Preset");
    presetLabel->layoutItem.SetFlexShrink(0);
    FollowTheme(presetLabel, LabelVariant::Muted);
    presetRow->AddChild(presetLabel);
    auto preset = CreateDropdown("uv.export.preset", 0, 0, 340, 28);
    for (const Preset& pr : *presets) preset->AddItem(pr.label);
    preset->SetSelectedIndex(0, false);
    presetRow->AddChild(preset);

    auto pathRow = std::make_shared<UltraCanvasContainer>("uv.export.pathRow", 0, 0, kContentWidth, 32);
    pathRow->layout.SetFlexRow().SetFlexGap(12).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto pathLabel = CreateLabel("uv.export.pathLabel", 0, 0, 90, 28, "Save as");
    pathLabel->layoutItem.SetFlexShrink(0);
    FollowTheme(pathLabel, LabelVariant::Muted);
    pathRow->AddChild(pathLabel);
    auto path = CreateTextInput("uv.export.path", 0, 0, 250, 28);
    path->SetText(state->outputPath);
    path->onTextChanged = [state](const std::string& text) { state->outputPath = text; };
    path->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    pathRow->AddChild(path);
    auto browse = MakeTextButton("uv.export.browse", "Browse\xE2\x80\xA6", 84);
    UltraCanvasTextInput* pathField = path.get();
    UltraCanvasDropdown* presetBox = preset.get();
    // The presets' file types, the chosen one's first, under FileLoader's
    // names; a type picked here picks the preset that writes it, so the
    // name and what is written in it always agree
    browse->SetOnClick([state, pathField, presets, presetBox, parent]() {
        FileDialogOptions opts;
        opts.SetTitle("Export to").SetDefaultFileName(PathToUtf8(PathFromUtf8(state->outputPath).filename()))
            .SetConfirmOverwrite(true).SetParentWindow(parent);
        const fs::path folder = PathFromUtf8(state->outputPath).parent_path();
        if (!folder.empty()) opts.SetInitialDirectory(PathToUtf8(folder));
        const int current = std::max(0, presetBox->GetSelectedIndex());
        std::vector<std::string> offered;
        auto offer = [&opts, &offered](const char* extension) {
            const std::string ext = std::string(extension).substr(1);
            if (std::find(offered.begin(), offered.end(), ext) != offered.end()) return;
            offered.push_back(ext);
            opts.AddFilter(FormatName(ext), ext);
        };
        if (static_cast<size_t>(current) < presets->size()) offer((*presets)[static_cast<size_t>(current)].extension);
        for (const Preset& pr : *presets) offer(pr.extension);
        UltraCanvasFileLoader::SaveFileDialog(opts, [state, pathField, presets, presetBox](DialogResult result,
                                                                                            const std::string& chosen) {
            if (result != DialogResult::OK || chosen.empty()) return;
            state->outputPath = chosen;
            pathField->SetText(chosen);
            std::string ext = PathToUtf8(PathFromUtf8(chosen).extension());
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const int current = presetBox->GetSelectedIndex();
            if (current >= 0 && static_cast<size_t>(current) < presets->size() && ext == (*presets)[static_cast<size_t>(current)].extension)
                return;
            for (size_t i = 0; i < presets->size(); ++i)
                if (ext == (*presets)[i].extension) { presetBox->SetSelectedIndex(static_cast<int>(i)); return; }
        });
    });
    pathRow->AddChild(browse);

    preset->onSelectionChanged = [state, presets, pathField](int index, const DropdownItem&) {
        if (index < 0 || static_cast<size_t>(index) >= presets->size()) return;
        state->settings = (*presets)[static_cast<size_t>(index)].settings;
        state->outputPath = WithExtension(state->outputPath, (*presets)[static_cast<size_t>(index)].extension);
        pathField->SetText(state->outputPath);
    };

    auto range = UltraCanvasSwitch::Create("uv.export.range", 0, 0, "Only the work area", false);
    range->SetDisabled(!(doc.view.rangeOut > doc.view.rangeIn));
    range->onStateChanged = [state](CheckedState, CheckedState now) { state->workAreaOnly = now == CheckedState::Checked; };

    auto note = CreateLabel("uv.export.note", 0, 0, kContentWidth, 40,
                            "Music is mixed in under the clips' own sound. A cancelled or failed export leaves no file.");
    FollowTheme(note, LabelVariant::Muted);

    dialog->AddDialogElement(presetRow);
    dialog->AddDialogElement(pathRow);
    dialog->AddDialogElement(range);
    dialog->AddDialogElement(note);
    dialog->AddCustomButton("Cancel", DialogResult::Cancel, DialogButtonRole::Cancel);
    dialog->AddCustomButton("Export", DialogResult::OK, DialogButtonRole::Default);
    dialog->SetFollowTheme(true);        // the dialog's surface, message and buttons with the window's look
    UltraCanvasDialogManager::ShowDialog(dialog, [state, onExport, parent](DialogResult result) {
        if (result != DialogResult::OK) return;
        if (state->outputPath.empty()) {
            UltraCanvasAlert::Error("Choose where to save the video first.", "Export", nullptr, parent);
            return;
        }
        if (onExport) onExport(*state);
    });
}

void AskToSave(UltraCanvasWindow* parent, const std::string& name, std::function<void(DiscardAnswer)> onAnswer) {
    DialogConfig config;
    config.title = "Unsaved changes";
    config.message = "Save the changes to \"" + name + "\" before closing it?";
    config.dialogType = DialogType::Question;
    config.buttons = DialogButtons::NoButtons;
    config.width = 460;
    config.height = 170;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    dialog->AddCustomButton("Cancel", DialogResult::Cancel, DialogButtonRole::Cancel);
    dialog->AddCustomButton("Don't save", DialogResult::No, DialogButtonRole::Normal);
    dialog->AddCustomButton("Save", DialogResult::Yes, DialogButtonRole::Default);
    dialog->SetFollowTheme(true);
    UltraCanvasDialogManager::ShowDialog(dialog, [onAnswer](DialogResult result) {
        if (!onAnswer) return;
        if (result == DialogResult::Yes) onAnswer(DiscardAnswer::Save);
        else if (result == DialogResult::No) onAnswer(DiscardAnswer::Discard);
        else onAnswer(DiscardAnswer::Cancel);
    });
    (void)parent;
}

} // namespace UltraVideo
