// Apps/DemoApp/UltraCanvasFileLoaderExamples.cpp
// FileLoader module demo page. A horizontal tab bar splits the module into
// three views:
//   * Overview  – the short intro, the architecture diagram and Markdown tables
//                 listing every supported format per family (bitmaps, vector, 3D,
//                 text, documents, audio, video).
//   * Details   – the full README documentation.
//   * Examples  – a live playground: pick a file family, then Open / Save through
//                 UltraCanvasFileLoader. The Open and Save buttons advertise the
//                 file types currently available for the chosen family, and the
//                 loaded file is shown in the matching display area (text area,
//                 image, spreadsheet, audio or video player). A file can also be
//                 dragged from the OS file manager and dropped onto the display
//                 area; the family dropdown switches automatically when the
//                 dropped file belongs to another family.
// Version: 1.1.0
// Last Modified: 2026-07-12
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasAudioPlayerElement.h"
#include "UltraCanvasVideoPlayerElement.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath, LoadFile

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

    // How the loaded file of a family is presented in the display area below the
    // toolbar. Documents and 3D models have no inline viewer in this demo, so they
    // fall back to an informational text area.
    enum class DisplayKind { Image, Spreadsheet, Audio, Video, Text };

    struct FileGroup {
        std::string name;                       // dropdown label
        std::vector<std::string> openExts;      // formats offered by the Open dialog
        std::vector<std::string> saveExts;      // formats offered by the Save dialog
        DisplayKind display;
    };

    // The seven file families requested for the demo, each with the extensions the
    // Open/Save dialogs currently advertise for it.
    const std::vector<FileGroup>& FileGroups() {
        static const std::vector<FileGroup> groups = {
            { "Bitmap graphics",
              { "png", "jpg", "jpeg", "webp", "avif", "heic", "gif", "bmp", "tif", "tiff", "tga", "hdr", "jxl", "ppm", "psd" },
              { "png", "jpg", "webp", "avif", "gif", "bmp", "tiff", "tga", "hdr", "jxl", "ppm" },
              DisplayKind::Image },
            { "Vector graphics",
              { "svg", "svgz", "cdr", "xar", "eps", "ps" },
              { "svg", "svgz" },
              DisplayKind::Image },
            { "3D graphics",
              { "obj", "stl", "gltf", "glb", "fbx", "3ds", "ply", "dae" },
              { "obj", "stl", "ply" },
              DisplayKind::Text },
            { "Documents files",
              { "pdf", "docx", "odt", "rtf", "epub", "mobi", "azw3", "fb2", "txt", "md" },
              { "pdf", "txt", "md", "rtf" },
              DisplayKind::Text },
            { "Spreadsheet files",
              { "ods", "xlsx", "csv", "tsv" },
              { "ods", "csv", "tsv" },
              DisplayKind::Spreadsheet },
            { "Audio",
              { "mp3", "flac", "wav", "ogg", "aac", "m4a", "opus", "wma" },
              { "wav", "flac", "mp3", "ogg", "opus" },
              DisplayKind::Audio },
            { "Video",
              { "mp4", "webm", "mkv", "avi", "mov", "flv", "wmv" },
              { "mp4", "webm" },
              DisplayKind::Video },
        };
        return groups;
    }

    std::string JoinExts(const std::vector<std::string>& exts) {
        std::string out;
        for (size_t i = 0; i < exts.size(); ++i) {
            if (i) out += ", ";
            out += "." + exts[i];
        }
        return out.empty() ? "(none)" : out;
    }

    // Extension of a path, lower-cased, without the dot ("" when none).
    std::string LowerExtension(const std::string& path) {
        size_t dot = path.find_last_of('.');
        size_t sep = path.find_last_of("/\\");
        if (dot == std::string::npos || (sep != std::string::npos && dot < sep)) return "";
        std::string ext = path.substr(dot + 1);
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return ext;
    }

    // Family whose Open list accepts the file's extension. The preferred
    // (currently selected) family wins when it matches, so dropping e.g. an .svg
    // while "Vector graphics" is active stays there even though other families
    // could claim the extension too. Returns -1 when no family accepts the file.
    int FindGroupForFile(const std::vector<FileGroup>& groups, const std::string& path, int preferredIndex) {
        const std::string ext = LowerExtension(path);
        if (ext.empty()) return -1;
        auto accepts = [&ext](const FileGroup& g) {
            return std::find(g.openExts.begin(), g.openExts.end(), ext) != g.openExts.end();
        };
        if (preferredIndex >= 0 && preferredIndex < static_cast<int>(groups.size()) &&
            accepts(groups[preferredIndex])) {
            return preferredIndex;
        }
        for (size_t i = 0; i < groups.size(); ++i) {
            if (accepts(groups[i])) return static_cast<int>(i);
        }
        return -1;
    }

    std::shared_ptr<UltraCanvasLabel> CenteredHint(const std::string& id, const std::string& text) {
        auto label = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 0);
        label->SetText(text);
        label->SetFontSize(13);
        label->SetTextColor(Color(120, 120, 120, 255));
        label->SetWrap(TextWrap::WrapWord);
        label->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        label->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        return label;
    }

    // Builds the element that fills the display area for the given family and file.
    // An empty path yields a friendly hint prompting the user to open a file.
    std::shared_ptr<UltraCanvasUIElement> BuildDisplay(const FileGroup& group, const std::string& path) {
        const bool loaded = !path.empty();

        if (!loaded) {
            return CenteredHint("FileLoaderHint",
                                "Click “Open file” or drag & drop a file here to load a " + group.name +
                                " file.\nSupported: " + JoinExts(group.openExts));
        }

        switch (group.display) {
            case DisplayKind::Image: {
                auto image = std::make_shared<UltraCanvasImageElement>("FileLoaderImage", 0.0f, 0.0f);
                image->SetFitMode(ImageFitMode::Contain);
                image->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                                 .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
                if (!image->LoadFromFile(path)) {
                    return CenteredHint("FileLoaderImageErr",
                                        "Could not display image:\n" + image->GetLastError());
                }
                return image;
            }
            case DisplayKind::Spreadsheet: {
                auto sheet = CreateSpreadsheetElement("FileLoaderSheet", 0, 0, 900, 400);
                sheet->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                                 .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
                if (!sheet->LoadFromFile(path)) {
                    return CenteredHint("FileLoaderSheetErr",
                                        "Could not open spreadsheet:\n" + sheet->GetLastError());
                }
                return sheet;
            }
            case DisplayKind::Audio: {
                auto player = CreateAudioPlayer("FileLoaderAudio", 0, 0, 900, 120);
                player->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                                  .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
                player->LoadFromFile(path);
                return player;
            }
            case DisplayKind::Video: {
                auto player = CreateVideoPlayer("FileLoaderVideo", 0, 0, 900, 400);
                player->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                                  .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
                player->LoadFromFile(path);
                return player;
            }
            case DisplayKind::Text:
            default: {
                auto text = std::make_shared<UltraCanvasTextArea>("FileLoaderText");
                text->SetReadOnly(true);
                text->SetWordWrap(true);
                text->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                                .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

                // Show plain-text sources verbatim; binary documents / models are
                // summarised instead of dumping raw bytes into the view.
                std::string ext = LowerExtension(path);
                const bool textual = (ext == "txt" || ext == "md" || ext == "markdown" || ext == "rtf");
                if (textual) {
                    text->SetText(LoadFile(path));
                } else {
                    text->SetText("Loaded " + group.name + " file:\n  " + path +
                                  "\n\nThis " + ext + " file was decoded by the matching FileLoader "
                                  "plug-in. In a full application it renders through the dedicated "
                                  "viewer (e.g. UltraCanvasPDFView for PDF, the GL model viewer for 3D).");
                }
                text->SetCursorPosition(LineColumnIndex::INVALID);
                return text;
            }
        }
    }

    // ---- Overview tab: intro + diagram + the per-family format tables. ----
    std::shared_ptr<UltraCanvasUIElement> BuildOverviewTab() {
        const std::string base    = NormalizePath(GetResourcesDir() + "Docs/Modules/FileLoader/");
        const std::string svgName = "FileLoader.svg";

        std::string combined;
        std::string intro = LoadFile(base + "intro.md");
        if (intro.find_first_not_of(" \t\r\n") != std::string::npos) {
            combined += intro + "\n\n";
        }
        if (std::ifstream(base + svgName).good()) {
            combined += "![FileLoader architecture](" + svgName + ")\n\n";
        }
        combined += LoadFile(base + "formats.md");

        auto text = std::make_shared<UltraCanvasTextArea>("FileLoaderOverview");
        text->size.width  = CSSLayout::Dimension::Pct(100);
        text->size.height = CSSLayout::Dimension::Pct(100);
        text->SetMarkdownBaseDirectory(base);
        text->SetText(combined);
        text->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        text->SetReadOnly(true);
        text->SetWordWrap(true);
        text->SetCursorPosition(LineColumnIndex::INVALID);
        text->SetPadding(0, 5, 0, 7);
        return text;
    }

    // ---- Details tab: the full README documentation. ----
    std::shared_ptr<UltraCanvasUIElement> BuildDetailsTab() {
        const std::string base = NormalizePath(GetResourcesDir() + "Docs/Modules/FileLoader/");

        auto text = std::make_shared<UltraCanvasTextArea>("FileLoaderDetails");
        text->size.width  = CSSLayout::Dimension::Pct(100);
        text->size.height = CSSLayout::Dimension::Pct(100);
        text->SetMarkdownBaseDirectory(base);
        text->SetText(LoadFile(base + "README.md"));
        text->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        text->SetReadOnly(true);
        text->SetWordWrap(true);
        text->SetCursorPosition(LineColumnIndex::INVALID);
        text->SetPadding(0, 5, 0, 7);
        return text;
    }

    // ---- Examples tab: the interactive Open / Save playground. ----
    std::shared_ptr<UltraCanvasUIElement> BuildExamplesTab() {
        const auto& groups = FileGroups();

        auto root = std::make_shared<UltraCanvasContainer>("FileLoaderDemo", 0, 0, 1000, 700);
        root->layout.SetFlexColumn().SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        root->SetPadding(10, 12, 10, 12);
        root->SetBackgroundColor(Colors::White);

        auto title = std::make_shared<UltraCanvasLabel>("FileLoaderDemoTitle", 0, 0, 0, 26);
        title->SetText("FileLoader playground");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        title->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        root->AddChild(title);

        // ----- Toolbar row: family dropdown + Open + Save -----
        auto toolbar = std::make_shared<UltraCanvasContainer>("FileLoaderToolbar", 0, 0, 0, 36);
        toolbar->layout.SetFlexRow().SetFlexGap(10)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        toolbar->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

        auto dropdown = std::make_shared<UltraCanvasDropdown>("FileLoaderGroup", 0, 0, 220, 30);
        for (const auto& g : groups) dropdown->AddItem(g.name);
        dropdown->SetSelectedIndex(0);
        dropdown->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        toolbar->AddChild(dropdown);

        auto openBtn = std::make_shared<UltraCanvasButton>("FileLoaderOpen", 0, 0, 120, 30, "Open file");
        openBtn->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        toolbar->AddChild(openBtn);

        auto saveBtn = std::make_shared<UltraCanvasButton>("FileLoaderSave", 0, 0, 90, 30, "Save");
        saveBtn->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        toolbar->AddChild(saveBtn);

        root->AddChild(toolbar);

        // Advertises the currently available file types for the chosen family and
        // reports Open/Save results.
        auto status = std::make_shared<UltraCanvasLabel>("FileLoaderStatus", 0, 0, 0, 34);
        status->SetFontSize(11);
        status->SetTextColor(Color(90, 90, 90, 255));
        status->SetWrap(TextWrap::WrapWord);
        status->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        root->AddChild(status);

        // ----- Display area (swapped per family / loaded file) -----
        auto displayArea = std::make_shared<UltraCanvasContainer>("FileLoaderDisplay", 0, 0, 0, 0);
        displayArea->layout.SetFlexColumn()
                           .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        displayArea->SetBorders(1.0f, Color(200, 200, 205, 255), 4.0f);
        displayArea->SetPadding(8);
        displayArea->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                               .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        root->AddChild(displayArea);

        // Rebuild the display + status for a family, optionally showing a file.
        auto refresh = std::make_shared<std::function<void(int, const std::string&)>>();
        *refresh = [groups, displayArea, status](int groupIndex, const std::string& path) {
            if (groupIndex < 0 || groupIndex >= static_cast<int>(groups.size())) groupIndex = 0;
            const FileGroup& g = groups[groupIndex];

            status->SetText("Open types: " + JoinExts(g.openExts) +
                            "     |     Save types: " + JoinExts(g.saveExts));
            status->RequestRedraw();

            displayArea->ClearChildren();
            displayArea->AddChild(BuildDisplay(g, path));
            displayArea->InvalidateLayout();
            displayArea->RequestRedraw();
        };

        // Switching family resets the display back to the empty hint.
        dropdown->onSelectionChanged = [refresh](int index, const DropdownItem&) {
            (*refresh)(index, "");
        };

        // ----- Drag & drop: a file dropped onto the display area is loaded like
        // one picked via "Open file". Drag events bubble up from whichever child
        // currently fills the area, so hooking the container catches them all.
        const Color normalBorder(200, 200, 205, 255);
        const Color dropBorder(70, 130, 220, 255);
        // The callback is stored on displayArea itself, so it captures the area
        // and the refresh function as raw pointers to avoid a shared_ptr cycle;
        // both outlive the callback.
        auto* areaPtr = displayArea.get();
        auto* refreshPtr = refresh.get();
        displayArea->SetEventCallback(
            [groups, dropdown, status, areaPtr, refreshPtr, normalBorder, dropBorder]
            (const UCEvent& event) -> bool {
                switch (event.type) {
                    case UCEventType::DragEnter:
                    case UCEventType::DragOver:
                        areaPtr->SetBordersColor(dropBorder);
                        return true;
                    case UCEventType::DragLeave:
                        areaPtr->SetBordersColor(normalBorder);
                        return true;
                    case UCEventType::Drop: {
                        areaPtr->SetBordersColor(normalBorder);
                        if (event.droppedFiles.empty()) return false;
                        const std::string& path = event.droppedFiles.front();
                        int idx = FindGroupForFile(groups, path, dropdown->GetSelectedIndex());
                        if (idx < 0) {
                            status->SetText("Cannot open dropped file: unsupported type \"." +
                                            LowerExtension(path) + "\" (" + path + ")");
                            status->RequestRedraw();
                            return true;
                        }
                        // Keep the family dropdown in sync without re-triggering
                        // onSelectionChanged (refresh below shows the file itself).
                        if (idx != dropdown->GetSelectedIndex()) {
                            dropdown->SetSelectedIndex(idx, false);
                        }
                        (*refreshPtr)(idx, path);
                        status->SetText("Loaded (dropped): " + path);
                        status->RequestRedraw();
                        return true;
                    }
                    default:
                        // Anything else keeps the container's normal handling.
                        return false;
                }
            });

        // ----- Open: native dialog filtered to the current family -----
        openBtn->onClick = [groups, dropdown, status, refresh]() {
            int idx = dropdown->GetSelectedIndex();
            if (idx < 0 || idx >= static_cast<int>(groups.size())) idx = 0;
            const FileGroup& g = groups[idx];

            FileDialogOptions opts;
            opts.SetTitle("Open " + g.name + " file")
                .AddFilter(g.name, g.openExts)
                .AddFilter("All files", "*");

            UltraCanvasFileLoader::OpenFileDialog(opts,
                [status, refresh, idx](DialogResult result, const std::string& path) {
                    if (result != DialogResult::OK || path.empty()) {
                        status->SetText("Open cancelled.");
                        status->RequestRedraw();
                        return;
                    }
                    (*refresh)(idx, path);
                    status->SetText("Loaded: " + path);
                    status->RequestRedraw();
                });
        };

        // ----- Save: native dialog listing the family's writable formats -----
        saveBtn->onClick = [groups, dropdown, status]() {
            int idx = dropdown->GetSelectedIndex();
            if (idx < 0 || idx >= static_cast<int>(groups.size())) idx = 0;
            const FileGroup& g = groups[idx];

            if (g.saveExts.empty()) {
                status->SetText("No writable formats available for " + g.name + ".");
                status->RequestRedraw();
                return;
            }

            FileDialogOptions opts;
            opts.SetTitle("Save " + g.name + " file")
                .SetDefaultFileName("untitled." + g.saveExts.front());
            for (const auto& ext : g.saveExts) {
                opts.AddFilter("." + ext + " file", ext);
            }
            opts.AddFilter("All files", "*");

            UltraCanvasFileLoader::SaveFileDialog(opts,
                [status](DialogResult result, const std::string& path) {
                    if (result != DialogResult::OK || path.empty()) {
                        status->SetText("Save cancelled.");
                    } else {
                        status->SetText("Save target: " + path);
                    }
                    status->RequestRedraw();
                });
        };

        // Prime the display with the first family's hint.
        (*refresh)(0, "");

        return root;
    }

} // anonymous namespace

// ===== FILELOADER MODULE DEMO =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateFileLoaderExamples() {
        auto root = std::make_shared<UltraCanvasContainer>("FileLoaderExamples", 0, 0, 1000, 720);
        root->size.width  = CSSLayout::Dimension::Pct(100);
        root->size.height = CSSLayout::Dimension::Pct(100);
        root->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        auto tabs = std::make_shared<UltraCanvasTabbedContainer>("FileLoaderTabs", 0, 0, 0, 0);
        tabs->SetTabPosition(TabPosition::Top);
        tabs->SetTabStyle(TabStyle::Modern);
        tabs->SetCloseMode(TabCloseMode::NoClose);
        tabs->SetTabHeight(30);
        tabs->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        tabs->AddTab("Overview", BuildOverviewTab());
        tabs->AddTab("Details",  BuildDetailsTab());
        tabs->AddTab("Examples", BuildExamplesTab());
        tabs->SetActiveTab(0);

        root->AddChild(tabs);
        return root;
    }

} // namespace UltraCanvas
