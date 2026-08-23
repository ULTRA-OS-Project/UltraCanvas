// Apps/UltraFiler/UltraFilerSettingsDialog.cpp
// UltraFiler settings window: settings-page tree on the left, the selected
// page on the right. Pages: Display > Treeview (the folder tree's drive-row
// background and selected-folder highlight, each shown as a colour box that
// opens the colour picker in a popup window), Media Viewer > Transparent
// Images (the backdrop behind transparent images — checkered pattern or a
// preset colour chosen with the colour picker), Handling > Drag & Drop (what
// a plain drop onto a folder does - move or copy), Extras > Open prompt (the
// command line program UltraFiler opens, picked with the file dialog and
// stored with "Save app") and History & Favorites (clearing the
// recently-used lists and the pinned entries). Changes apply live and are
// saved immediately.
// Version: 1.5.0
// Last Modified: 2026-08-23
// Author: UltraCanvas Framework

#include "UltraFilerSettingsDialog.h"
#include "UltraFilerPrompt.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <system_error>

namespace UltraCanvas {

namespace {

    constexpr float kFontSize = 9.0f;   // matches the main window's UI font

    // Page ids double as tree node ids.
    constexpr const char* kPageDisplay = "display";
    constexpr const char* kPageTreeview = "display/treeview";
    constexpr const char* kPageMediaViewer = "media-viewer";
    constexpr const char* kPageTransparentImages = "media-viewer/transparent-images";
    constexpr const char* kPageHandling = "handling";
    constexpr const char* kPageDragDrop = "handling/drag-drop";
    constexpr const char* kPageExtras = "extras";
    constexpr const char* kPageOpenPrompt = "extras/open-prompt";
    constexpr const char* kPageLists = "history-favorites";

    // The one open settings window (or the last closed one, until reopened).
    struct DialogState {
        std::shared_ptr<UltraCanvasWindow>    window;
        bool                                  closed = false;
        std::shared_ptr<UltraCanvasTreeView>  tree;
        std::shared_ptr<UltraCanvasContainer> pageArea;
        std::map<std::string, std::shared_ptr<UltraCanvasContainer>> pages;

        // Display > Treeview: the colour boxes that open the picker popup.
        std::shared_ptr<UltraCanvasButton> driveColorBox;
        std::shared_ptr<UltraCanvasButton> selectedColorBox;

        std::shared_ptr<UltraCanvasRadio>       solidRadio;
        std::shared_ptr<UltraCanvasRadio>       checkeredRadio;
        UltraCanvasRadioGroup                   backgroundGroup;
        std::shared_ptr<UltraCanvasColorPicker> colorPicker;

        // Handling > Drag & Drop: what a plain drop onto a folder does.
        std::shared_ptr<UltraCanvasRadio>       dropMoveRadio;
        std::shared_ptr<UltraCanvasRadio>       dropCopyRadio;
        UltraCanvasRadioGroup                   dropOnFolderGroup;

        // Extras > Open prompt
        std::shared_ptr<UltraCanvasTextInput> promptInput;   // chosen application
        std::shared_ptr<UltraCanvasLabel>     promptStatus;  // what will be started

        // History & Favorites
        std::shared_ptr<UltraCanvasLabel>     listsStatus;   // "History cleared."

        UltraFilerSettings*   settings = nullptr;
        std::function<void()> onChanged;
        std::function<void()> onClearHistory;
        std::function<void()> onClearFavorites;
        std::function<void()> onClearFolderViews;
    };

    std::shared_ptr<DialogState> g_dialog;

    void ApplyAndSave(DialogState* d) {
        if (d->onChanged) d->onChanged();
        if (d->settings) d->settings->Save();
    }

    std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id,
                                                const std::string& text,
                                                float fontSize = kFontSize) {
        auto l = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 20);
        l->SetText(text);
        l->SetFontSize(fontSize);
        l->SetTextColor(Color(40, 40, 44, 255));
        l->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        l->size.width  = CSSLayout::Dimension::Auto();
        l->size.height = CSSLayout::Dimension::Auto();
        return l;
    }

    std::string IconPath(const std::string& fileName) {
        return NormalizePath(GetResourcesDir() + "media/icons/" + fileName);
    }

    // Standard dialog button (the Close button and the ones on the pages).
    // `iconFile` may be empty; an empty `label` gives an icon-only button.
    std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& id,
                                                  const std::string& label,
                                                  int width,
                                                  std::function<void()> onClick,
                                                  const std::string& iconFile = "") {
        auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, width, 28, label);
        b->SetFontSize(kFontSize);
        b->SetCornerRadius(4.0f);
        b->SetColors(Color(255, 255, 255, 255), Color(233, 238, 244, 255));
        b->SetTextColors(Color(40, 40, 44, 255));
        b->SetBorder(1.0f, Color(0, 0, 0, 60));
        if (!iconFile.empty()) {
            b->SetIcon(IconPath(iconFile));
            b->SetIconSize(15, 15);
            b->SetIconPosition(ButtonIconPosition::Left);
            b->SetIconSpacing(label.empty() ? 0 : 5);
            b->SetUseIconAsMask(true);
            b->SetIconMaskColor(Color(55, 55, 60, 255));
        }
        if (onClick) b->SetOnClick(std::move(onClick));
        b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        return b;
    }

    // A colour picker restyled for the dialog's light surface.
    ColorPickerStyle LightPickerStyle() {
        ColorPickerStyle s;
        s.backgroundColor = Color(249, 249, 251, 255);
        s.panelColor      = Color(240, 240, 244, 255);
        s.borderColor     = Color(210, 210, 216, 255);
        s.textColor       = Color(40, 40, 44, 255);
        s.mutedTextColor  = Color(120, 120, 126, 255);
        s.fieldColor      = Color(255, 255, 255, 255);
        s.fieldBorderColor = Color(190, 190, 196, 255);
        s.accentColor     = Color(60, 140, 220, 255);
        s.markerOutline   = Color(90, 90, 96, 255);
        return s;
    }

    // ===== COLOUR BOX + PICKER POPUP =====

    // The box showing a configured colour. It is a button filled with that
    // colour, so it hovers, focuses and reports its click like any other
    // control - clicking it opens the picker popup below.
    std::shared_ptr<UltraCanvasButton> MakeColorBox(const std::string& id,
                                                    const Color& color,
                                                    const std::string& tooltip,
                                                    std::function<void()> onClick) {
        auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, 56, 24, "");
        b->SetCornerRadius(3.0f);
        // Same colour in every state: a colour box that changes shade under
        // the pointer no longer shows the colour it stands for.
        b->SetColors(color, color, color, color);
        b->SetBorder(1.0f, Color(120, 120, 128, 255));
        b->SetTooltip(tooltip);
        if (onClick) b->SetOnClick(std::move(onClick));
        b->size.width  = CSSLayout::Dimension::Px(56);
        b->size.height = CSSLayout::Dimension::Px(24);
        b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        return b;
    }

    void SetColorBoxColor(const std::shared_ptr<UltraCanvasButton>& box,
                          const Color& color) {
        if (!box) return;
        box->SetColors(color, color, color, color);
        box->RequestRedraw();
    }

    // The open colour-picker popup (kept alive while its window is up).
    struct ColorPopupState {
        std::shared_ptr<UltraCanvasWindow>      window;
        std::shared_ptr<UltraCanvasColorPicker> picker;
        Color original;                       // restored when the popup is cancelled
        bool  accepted = false;
        std::function<void(const Color&)> onPreview;  // live, not persisted
        std::function<void(const Color&)> onAccept;   // persisted
    };

    std::shared_ptr<ColorPopupState> g_colorPopup;

    void CloseColorPopup(bool accepted) {
        auto popup = g_colorPopup;
        if (!popup) return;
        popup->accepted = accepted;
        if (popup->window) popup->window->Close();
    }

    // Opens the colour picker in its own small window on top of the settings
    // dialog. The colour is previewed live while it is being picked; "Use
    // colour" keeps it, Cancel (and closing the window) puts the previous one
    // back.
    void ShowColorPickerPopup(DialogState* d,
                              const std::string& title,
                              const Color& initial,
                              std::function<void(const Color&)> onPreview,
                              std::function<void(const Color&)> onAccept) {
        // One popup at a time: a second click on a colour box raises the open
        // one rather than stacking another window on it.
        if (g_colorPopup && g_colorPopup->window) {
            g_colorPopup->window->Show();
            return;
        }

        auto popup = std::make_shared<ColorPopupState>();
        popup->original  = initial;
        popup->onPreview = std::move(onPreview);
        popup->onAccept  = std::move(onAccept);

        WindowConfig wc;
        wc.title = title;
        wc.width = 320;
        wc.height = 470;
        wc.resizable = false;
        wc.type = WindowType::Dialog;
        wc.modal = true;
        wc.parentWindow = d->window.get();
        popup->window = CreateWindow(wc);
        if (!popup->window || !popup->window->IsCreated()) return;

        popup->window->layout.SetFlexColumn()
                             .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        popup->window->SetBackgroundColor(Color(249, 249, 251, 255));

        popup->picker = CreateColorPicker("ufl-set-popup-picker", initial,
                                          0, 0, 280, 380);
        popup->picker->SetStyle(LightPickerStyle());
        popup->picker->SetUIScale(0.85f);
        popup->picker->SetShowAlpha(false);   // tree rows are painted opaque
        popup->picker->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        auto preview = [popup](const Color& c) {
            if (popup->onPreview) popup->onPreview(Color(c.r, c.g, c.b, 255));
        };
        popup->picker->onColorChanging = preview;
        popup->picker->onColorChanged  = preview;
        popup->window->AddChild(popup->picker);

        auto buttons = std::make_shared<UltraCanvasContainer>("ufl-set-popup-buttons");
        buttons->layout.SetFlexRow().SetFlexGap(8)
                       .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        buttons->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        buttons->SetPadding(10, 12, 10, 12);
        buttons->AddChild(MakeButton("ufl-set-popup-ok", "Use colour", 110,
                []() { CloseColorPopup(true); }));
        buttons->AddChild(MakeButton("ufl-set-popup-cancel", "Cancel", 90,
                []() { CloseColorPopup(false); }));
        popup->window->AddChild(buttons);

        // Escape cancels, matching the framework's dialog convention.
        popup->window->SetEventCallback([](const UCEvent& event) {
            if (event.type == UCEventType::KeyUp &&
                event.virtualKey == UCKeys::Escape) {
                CloseColorPopup(false);
                return true;
            }
            return false;
        });
        // Closing the window through its title bar cancels too.
        popup->window->onWindowClosed = [popup]() {
            const Color chosen = popup->picker ? popup->picker->GetColor()
                                               : popup->original;
            if (popup->accepted) {
                if (popup->onAccept) popup->onAccept(Color(chosen.r, chosen.g,
                                                           chosen.b, 255));
            } else if (popup->onPreview) {
                popup->onPreview(popup->original);
            }
            if (g_colorPopup == popup) g_colorPopup.reset();
        };

        g_colorPopup = popup;   // keeps the window and its widgets alive
        popup->window->Show();
    }

    // ===== DISPLAY > TREEVIEW =====

    // One "<caption>  [colour box]" row of the Treeview page.
    std::shared_ptr<UltraCanvasContainer> MakeColorRow(
            const std::string& id, const std::string& caption,
            const std::shared_ptr<UltraCanvasButton>& box) {
        auto row = std::make_shared<UltraCanvasContainer>(id);
        row->layout.SetFlexRow().SetFlexGap(10)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        row->size.width  = CSSLayout::Dimension::Px(430);
        row->size.height = CSSLayout::Dimension::Px(30);

        auto label = MakeLabel(id + "-label", caption);
        label->size.width  = CSSLayout::Dimension::Px(190);
        label->size.height = CSSLayout::Dimension::Px(20);
        row->AddChild(label);
        row->AddChild(box);
        return row;
    }

    std::shared_ptr<UltraCanvasContainer> BuildTreeviewPage(DialogState* d) {
        auto page = std::make_shared<UltraCanvasContainer>("ufl-set-page-treeview");
        page->layout.SetFlexColumn().SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        page->SetPadding(16, 18, 16, 18);

        page->AddChild(MakeLabel("ufl-set-tv-title", "Treeview", 12.0f));
        page->AddChild(MakeLabel("ufl-set-tv-caption",
                "Colours of the folder tree on the left of the main window."));
        page->AddChild(MakeLabel("ufl-set-tv-hint",
                "Click a colour box to pick a colour."));

        d->driveColorBox = MakeColorBox("ufl-set-tv-drive-color",
                d->settings->treeDriveBackgroundColor,
                "Row background of the drives in the folder tree",
                [d]() {
            ShowColorPickerPopup(d, "Drive background colour",
                    d->settings->treeDriveBackgroundColor,
                    [d](const Color& c) {       // live preview
                d->settings->treeDriveBackgroundColor = c;
                SetColorBoxColor(d->driveColorBox, c);
                if (d->onChanged) d->onChanged();
            },
                    [d](const Color& c) {       // kept
                d->settings->treeDriveBackgroundColor = c;
                SetColorBoxColor(d->driveColorBox, c);
                ApplyAndSave(d);
            });
        });
        page->AddChild(MakeColorRow("ufl-set-tv-drive-row",
                "Drive background colour:", d->driveColorBox));

        d->selectedColorBox = MakeColorBox("ufl-set-tv-selected-color",
                d->settings->treeSelectedFolderColor,
                "Highlight of the selected folder in the folder tree",
                [d]() {
            ShowColorPickerPopup(d, "Selected folder colour",
                    d->settings->treeSelectedFolderColor,
                    [d](const Color& c) {       // live preview
                d->settings->treeSelectedFolderColor = c;
                SetColorBoxColor(d->selectedColorBox, c);
                if (d->onChanged) d->onChanged();
            },
                    [d](const Color& c) {       // kept
                d->settings->treeSelectedFolderColor = c;
                SetColorBoxColor(d->selectedColorBox, c);
                ApplyAndSave(d);
            });
        });
        page->AddChild(MakeColorRow("ufl-set-tv-selected-row",
                "Selected folder colour:", d->selectedColorBox));

        page->AddChild(MakeButton("ufl-set-tv-defaults", "Restore default colours",
                170, [d]() {
            if (!d->settings) return;
            d->settings->treeDriveBackgroundColor =
                    UltraFilerSettings::kDefaultTreeDriveBackgroundColor;
            d->settings->treeSelectedFolderColor =
                    UltraFilerSettings::kDefaultTreeSelectedFolderColor;
            SetColorBoxColor(d->driveColorBox,
                             d->settings->treeDriveBackgroundColor);
            SetColorBoxColor(d->selectedColorBox,
                             d->settings->treeSelectedFolderColor);
            ApplyAndSave(d);
        }));

        return page;
    }

    // ===== MEDIA VIEWER > TRANSPARENT IMAGES =====
    std::shared_ptr<UltraCanvasContainer> BuildTransparentImagesPage(DialogState* d) {
        auto page = std::make_shared<UltraCanvasContainer>("ufl-set-page-transparent");
        page->layout.SetFlexColumn().SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        page->SetPadding(16, 18, 16, 18);

        page->AddChild(MakeLabel("ufl-set-ti-title", "Transparent images", 12.0f));
        page->AddChild(MakeLabel("ufl-set-ti-caption",
                "Background shown behind images with transparency in the media viewer:"));

        const bool checkered = d->settings->previewCheckeredBackground;

        d->solidRadio = UltraCanvasRadio::Create(
                "ufl-set-ti-solid", -1, -1, "Preset colour (picked below)", !checkered);
        d->checkeredRadio = UltraCanvasRadio::Create(
                "ufl-set-ti-checkered", -1, -1, "Checkered pattern (shows transparency)",
                checkered);
        // Explicit sizes: content measuring needs a render context, which the
        // dialog does not have while it is first laid out.
        for (auto& radio : {d->solidRadio, d->checkeredRadio}) {
            radio->size.width  = CSSLayout::Dimension::Px(340);
            radio->size.height = CSSLayout::Dimension::Px(22);
            radio->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        }
        d->backgroundGroup.AddRadioButton(d->solidRadio);
        d->backgroundGroup.AddRadioButton(d->checkeredRadio);
        d->backgroundGroup.onSelectionChanged =
                [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->settings) return;
            d->settings->previewCheckeredBackground = (selected == d->checkeredRadio);
            ApplyAndSave(d);
        };
        page->AddChild(d->solidRadio);
        page->AddChild(d->checkeredRadio);

        page->AddChild(MakeLabel("ufl-set-ti-color-lbl", "Preset colour:"));

        d->colorPicker = CreateColorPicker("ufl-set-ti-picker",
                d->settings->previewTransparentColor, 0, 0, 280, 380);
        d->colorPicker->SetStyle(LightPickerStyle());
        d->colorPicker->SetUIScale(0.85f);
        d->colorPicker->SetShowAlpha(false);   // the backdrop is always opaque
        d->colorPicker->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        // Live preview while dragging; the final value also selects the
        // solid-colour mode (picking a colour means the user wants it shown)
        // and persists.
        d->colorPicker->onColorChanging = [d](const Color& c) {
            if (!d->settings) return;
            d->settings->previewTransparentColor = Color(c.r, c.g, c.b, 255);
            if (d->onChanged) d->onChanged();
        };
        d->colorPicker->onColorChanged = [d](const Color& c) {
            if (!d->settings) return;
            d->settings->previewTransparentColor = Color(c.r, c.g, c.b, 255);
            if (!d->settings->previewCheckeredBackground) {
                ApplyAndSave(d);
            } else {
                d->backgroundGroup.SelectButton(d->solidRadio);  // applies + saves
            }
        };
        page->AddChild(d->colorPicker);

        return page;
    }

    // ===== HANDLING > DRAG & DROP =====
    std::shared_ptr<UltraCanvasContainer> BuildDragDropPage(DialogState* d) {
        auto page = std::make_shared<UltraCanvasContainer>("ufl-set-page-dragdrop");
        page->layout.SetFlexColumn().SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        page->SetPadding(16, 18, 16, 18);

        page->AddChild(MakeLabel("ufl-set-dd-title", "Drag & Drop", 12.0f));
        page->AddChild(MakeLabel("ufl-set-dd-caption",
                "Drop on folder - what dragging files onto a folder of the file "
                "display does:"));

        const bool copies = d->settings->dropOnFolderCopies;

        d->dropMoveRadio = UltraCanvasRadio::Create(
                "ufl-set-dd-move", -1, -1, "Move files", !copies);
        d->dropCopyRadio = UltraCanvasRadio::Create(
                "ufl-set-dd-copy", -1, -1, "Copy files", copies);
        // Explicit sizes: content measuring needs a render context, which the
        // dialog does not have while it is first laid out.
        for (auto& radio : {d->dropMoveRadio, d->dropCopyRadio}) {
            radio->size.width  = CSSLayout::Dimension::Px(340);
            radio->size.height = CSSLayout::Dimension::Px(22);
            radio->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        }
        d->dropOnFolderGroup.AddRadioButton(d->dropMoveRadio);
        d->dropOnFolderGroup.AddRadioButton(d->dropCopyRadio);
        d->dropOnFolderGroup.onSelectionChanged =
                [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->settings) return;
            d->settings->dropOnFolderCopies = (selected == d->dropCopyRadio);
            ApplyAndSave(d);
        };
        page->AddChild(d->dropMoveRadio);
        page->AddChild(d->dropCopyRadio);

        page->AddChild(MakeLabel("ufl-set-dd-hint",
                "Ctrl while dropping always copies, Shift always moves."));

        return page;
    }

    // ===== EXTRAS > OPEN PROMPT =====

    // Describes what "Extras > Open prompt" will start right now: the saved
    // application, or the platform default while nothing is saved.
    void UpdatePromptStatus(DialogState* d) {
        if (!d->promptStatus || !d->settings) return;
        const std::string& saved = d->settings->promptApplication;
        if (!saved.empty()) {
            d->promptStatus->SetText("Saved application: " + saved);
        } else {
            const std::string detected = UltraFilerPrompt::DetectSystemPrompt();
            d->promptStatus->SetText(detected.empty()
                    ? "No command line program was found on this system - "
                      "choose one above."
                    : "System default in use: " + detected);
        }
        d->promptStatus->RequestRedraw();
    }

    // Stores the application currently in the input field.
    void SavePromptApplication(DialogState* d) {
        if (!d->settings || !d->promptInput) return;
        d->settings->promptApplication = Trim(d->promptInput->GetText(), " \t\"");
        d->promptInput->SetText(d->settings->promptApplication);
        ApplyAndSave(d);
        UpdatePromptStatus(d);
    }

    // The file dialog for picking the application, filtered to the executables
    // of this platform. The choice only lands in the input field - "Save app"
    // is what persists it.
    void BrowseForPromptApplication(DialogState* d) {
        const UltraFilerPrompt::ApplicationFilter filter =
                UltraFilerPrompt::GetApplicationFilter();

        std::string initialDir = UltraFilerPrompt::GetApplicationsDirectory();
        if (d->settings && !d->settings->promptApplication.empty()) {
            std::error_code ec;
            const std::filesystem::path parent =
                    std::filesystem::path(d->settings->promptApplication).parent_path();
            if (!parent.empty() && std::filesystem::is_directory(parent, ec) && !ec)
                initialDir = parent.string();
        }

        FileDialogOptions opts;
        opts.SetTitle("Select the command line application")
            .SetInitialDirectory(initialDir)
            // Picking a program is not a document the shell should remember.
            .SetRegisterAsRecent(false)
            .SetParentWindow(d->window.get())
            .AddFilter(filter.description, filter.extensions);
        // On platforms whose executables carry no extension the application
        // filter already shows everything - a second all-files entry would
        // just be the same list under another name.
        const bool showsEverything =
                filter.extensions.size() == 1 && filter.extensions.front() == "*";
        if (!showsEverything) opts.AddFilter("All files", "*");

        UltraCanvasFileLoader::OpenFileDialog(opts,
                [d](DialogResult result, const std::string& path) {
            if (result != DialogResult::OK || path.empty()) return;
            if (!d->promptInput) return;
            d->promptInput->SetText(path);
            d->promptInput->RequestRedraw();
            if (d->promptStatus) {
                d->promptStatus->SetText("Selected: " + path +
                                         " - press \"Save app\" to keep it.");
                d->promptStatus->RequestRedraw();
            }
        });
    }

    std::shared_ptr<UltraCanvasContainer> BuildOpenPromptPage(DialogState* d) {
        auto page = std::make_shared<UltraCanvasContainer>("ufl-set-page-prompt");
        page->layout.SetFlexColumn().SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        page->SetPadding(16, 18, 16, 18);

        page->AddChild(MakeLabel("ufl-set-op-title", "Open prompt", 12.0f));
        page->AddChild(MakeLabel("ufl-set-op-caption",
                "Command line application UltraFiler opens. It starts in the "
                "folder of the active tab."));
        page->AddChild(MakeLabel("ufl-set-op-hint",
                "Leave the field empty to use the command line program of this "
                "operating system."));

        // ----- application path + browse -----
        auto pathRow = std::make_shared<UltraCanvasContainer>("ufl-set-op-row");
        pathRow->layout.SetFlexRow().SetFlexGap(6)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        pathRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        pathRow->size.width  = CSSLayout::Dimension::Px(430);
        pathRow->size.height = CSSLayout::Dimension::Px(32);

        d->promptInput = CreateTextInput("ufl-set-op-path", 0, 0, 380, 26);
        d->promptInput->SetFontSize(kFontSize);
        d->promptInput->SetPlaceholder("Path to the application");
        d->promptInput->SetText(d->settings ? d->settings->promptApplication
                                            : std::string());
        // Enter in the field saves, like the button next to it.
        d->promptInput->onEnterPressed = [d](const std::string&) {
            SavePromptApplication(d);
            return true;
        };
        d->promptInput->size.width  = CSSLayout::Dimension::Px(380);
        d->promptInput->size.height = CSSLayout::Dimension::Px(26);
        d->promptInput->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        pathRow->AddChild(d->promptInput);

        pathRow->AddChild(MakeButton("ufl-set-op-browse", "", 32,
                [d]() { BrowseForPromptApplication(d); }, "folder-open.svg"));
        page->AddChild(pathRow);

        // ----- save / reset -----
        auto buttonRow = std::make_shared<UltraCanvasContainer>("ufl-set-op-buttons");
        buttonRow->layout.SetFlexRow().SetFlexGap(8)
                         .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        buttonRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        buttonRow->size.width  = CSSLayout::Dimension::Px(430);
        buttonRow->size.height = CSSLayout::Dimension::Px(34);

        buttonRow->AddChild(MakeButton("ufl-set-op-save", "Save app", 100,
                [d]() { SavePromptApplication(d); }));
        buttonRow->AddChild(MakeButton("ufl-set-op-default", "Use system default", 150,
                [d]() {
            if (d->promptInput) d->promptInput->SetText("");
            SavePromptApplication(d);
        }));
        buttonRow->AddChild(MakeButton("ufl-set-op-test", "Test", 70, [d]() {
            std::string error;
            const std::string application = d->promptInput
                    ? Trim(d->promptInput->GetText(), " \t\"") : std::string();
            if (!UltraFilerPrompt::Launch(application, "", error))
                UltraCanvasAlert::Error(error, "Open prompt", nullptr,
                                        d->window.get());
        }));
        page->AddChild(buttonRow);

        d->promptStatus = MakeLabel("ufl-set-op-status", "");
        d->promptStatus->SetTextColor(Color(110, 110, 118, 255));
        d->promptStatus->size.width  = CSSLayout::Dimension::Px(430);
        d->promptStatus->size.height = CSSLayout::Dimension::Px(20);
        page->AddChild(d->promptStatus);
        UpdatePromptStatus(d);

        return page;
    }

    // ===== HISTORY & FAVORITES =====
    std::shared_ptr<UltraCanvasContainer> BuildListsPage(DialogState* d) {
        auto page = std::make_shared<UltraCanvasContainer>("ufl-set-page-lists");
        page->layout.SetFlexColumn().SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        page->SetPadding(16, 18, 16, 18);

        page->AddChild(MakeLabel("ufl-set-hf-title", "History & Favorites", 12.0f));
        page->AddChild(MakeLabel("ufl-set-hf-caption",
                "The History view lists the recently used files, folders and "
                "applications; the Favorites view lists the pinned ones."));
        page->AddChild(MakeLabel("ufl-set-hf-caption2",
                "Folder views are the view type and sort order each folder was "
                "last looked at with."));

        auto buttonRow = std::make_shared<UltraCanvasContainer>("ufl-set-hf-buttons");
        buttonRow->layout.SetFlexRow().SetFlexGap(8)
                         .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        buttonRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        buttonRow->size.width  = CSSLayout::Dimension::Px(430);
        buttonRow->size.height = CSSLayout::Dimension::Px(34);

        auto clearHistory = MakeButton("ufl-set-hf-clear-history", "Clear History",
                120, [d]() {
            if (d->onClearHistory) d->onClearHistory();
            if (d->listsStatus) {
                d->listsStatus->SetText("History cleared.");
                d->listsStatus->RequestRedraw();
            }
        });
        clearHistory->SetDisabled(!d->onClearHistory);
        buttonRow->AddChild(clearHistory);

        auto clearFavorites = MakeButton("ufl-set-hf-clear-favorites",
                "Clear Favorites", 120, [d]() {
            if (d->onClearFavorites) d->onClearFavorites();
            if (d->listsStatus) {
                d->listsStatus->SetText(
                        "Favorites cleared (including the tree's Pinned section).");
                d->listsStatus->RequestRedraw();
            }
        });
        clearFavorites->SetDisabled(!d->onClearFavorites);
        buttonRow->AddChild(clearFavorites);

        auto clearViews = MakeButton("ufl-set-hf-clear-views",
                "Clear Folder views", 140, [d]() {
            if (d->onClearFolderViews) d->onClearFolderViews();
            if (d->listsStatus) {
                d->listsStatus->SetText(
                        "Folder views cleared - every folder opens with the "
                        "current view again.");
                d->listsStatus->RequestRedraw();
            }
        });
        clearViews->SetDisabled(!d->onClearFolderViews);
        buttonRow->AddChild(clearViews);
        page->AddChild(buttonRow);

        d->listsStatus = MakeLabel("ufl-set-hf-status", "");
        d->listsStatus->SetTextColor(Color(110, 110, 118, 255));
        d->listsStatus->size.width  = CSSLayout::Dimension::Px(430);
        d->listsStatus->size.height = CSSLayout::Dimension::Px(20);
        page->AddChild(d->listsStatus);

        return page;
    }

    // Show the page of `nodeId`; a main page without its own panel falls
    // through to its first sub page.
    void ShowPage(DialogState* d, const std::string& nodeId) {
        std::string target = nodeId;
        if (!d->pages.count(target)) {
            // Fall back to the first page below this node in tree order.
            TreeNode* node = d->tree ? d->tree->FindNode(nodeId) : nullptr;
            std::function<TreeNode*(TreeNode*)> firstPage =
                    [&](TreeNode* n) -> TreeNode* {
                if (!n) return nullptr;
                if (d->pages.count(n->data.nodeId)) return n;
                for (auto& child : n->children)
                    if (TreeNode* hit = firstPage(child.get())) return hit;
                return nullptr;
            };
            TreeNode* hit = firstPage(node);
            if (!hit) return;
            target = hit->data.nodeId;
        }
        for (auto& [id, pageContainer] : d->pages)
            pageContainer->SetVisible(id == target);
    }

    void BuildDialog(DialogState* d, UltraCanvasWindowBase* parent) {
        WindowConfig wc;
        wc.title = "UltraFiler - Settings";
        wc.width = 700;
        wc.height = 560;
        wc.resizable = true;
        wc.type = WindowType::Dialog;
        wc.parentWindow = parent;
        d->window = CreateWindow(wc);
        if (!d->window || !d->window->IsCreated()) { d->window.reset(); return; }

        d->window->layout.SetFlexColumn()
                         .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        d->window->SetBackgroundColor(Color(249, 249, 251, 255));

        // ----- settings-page tree | page area -----
        auto content = std::make_shared<UltraCanvasContainer>("ufl-set-content");
        content->layout.SetFlexRow()
                       .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        content->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        d->tree = std::make_shared<UltraCanvasTreeView>("ufl-set-tree");
        d->tree->SetFontSize(kFontSize);
        d->tree->SetRowHeight(24);
        d->tree->SetSelectionMode(TreeSelectionMode::Single);
        d->tree->SetLineStyle(TreeLineStyle::NoLine);
        d->tree->SetBackgroundColor(Color(243, 243, 246, 255));
        d->tree->size.width = CSSLayout::Dimension::Px(180);
        d->tree->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        TreeNodeData rootData;
        rootData.nodeId = "settings";
        rootData.text = "Settings";
        TreeNode* root = d->tree->SetRootNode(rootData);

        TreeNodeData display;
        display.nodeId = kPageDisplay;
        display.text = "Display";
        d->tree->AddNode("settings", display);

        TreeNodeData treeview;
        treeview.nodeId = kPageTreeview;
        treeview.text = "Treeview";
        d->tree->AddNode(kPageDisplay, treeview);

        TreeNodeData mediaViewer;
        mediaViewer.nodeId = kPageMediaViewer;
        mediaViewer.text = "Media Viewer";
        d->tree->AddNode("settings", mediaViewer);

        TreeNodeData transparent;
        transparent.nodeId = kPageTransparentImages;
        transparent.text = "Transparent Images";
        d->tree->AddNode(kPageMediaViewer, transparent);

        TreeNodeData handling;
        handling.nodeId = kPageHandling;
        handling.text = "Handling";
        d->tree->AddNode("settings", handling);

        TreeNodeData dragDrop;
        dragDrop.nodeId = kPageDragDrop;
        dragDrop.text = "Drag & Drop";
        d->tree->AddNode(kPageHandling, dragDrop);

        TreeNodeData extras;
        extras.nodeId = kPageExtras;
        extras.text = "Extras";
        d->tree->AddNode("settings", extras);

        TreeNodeData openPrompt;
        openPrompt.nodeId = kPageOpenPrompt;
        openPrompt.text = "Open prompt";
        d->tree->AddNode(kPageExtras, openPrompt);

        TreeNodeData lists;
        lists.nodeId = kPageLists;
        lists.text = "History & Favorites";
        d->tree->AddNode("settings", lists);

        d->tree->ExpandAll();
        content->AddChild(d->tree);

        d->pageArea = std::make_shared<UltraCanvasContainer>("ufl-set-pages");
        d->pageArea->layout.SetFlexColumn()
                           .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        d->pageArea->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                               .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        d->pageArea->SetBackgroundColor(Color(255, 255, 255, 255));
        content->AddChild(d->pageArea);

        // ----- pages -----
        auto treeviewPage = BuildTreeviewPage(d);
        treeviewPage->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                                .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        d->pages[kPageTreeview] = treeviewPage;
        d->pageArea->AddChild(treeviewPage);

        auto transparentPage = BuildTransparentImagesPage(d);
        transparentPage->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                                   .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        d->pages[kPageTransparentImages] = transparentPage;
        d->pageArea->AddChild(transparentPage);

        auto dragDropPage = BuildDragDropPage(d);
        dragDropPage->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                                .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        d->pages[kPageDragDrop] = dragDropPage;
        d->pageArea->AddChild(dragDropPage);

        auto openPromptPage = BuildOpenPromptPage(d);
        openPromptPage->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                                  .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        d->pages[kPageOpenPrompt] = openPromptPage;
        d->pageArea->AddChild(openPromptPage);

        auto listsPage = BuildListsPage(d);
        listsPage->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                             .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        d->pages[kPageLists] = listsPage;
        d->pageArea->AddChild(listsPage);

        d->tree->onNodeSelected = [d](TreeNode* node) {
            if (node) ShowPage(d, node->data.nodeId);
        };
        if (TreeNode* initial = d->tree->FindNode(kPageTreeview))
            d->tree->SelectNode(initial);
        ShowPage(d, kPageTreeview);

        d->window->AddChild(content);

        // ----- bottom bar -----
        auto bottom = std::make_shared<UltraCanvasContainer>("ufl-set-bottom");
        bottom->layout.SetFlexRow().SetFlexGap(8)
                      .SetFlexJustifyContent(CSSLayout::JustifyContent::FlexEnd)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        bottom->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        bottom->SetPadding(8, 12, 8, 12);
        bottom->SetBorderTop(1, Color(225, 225, 230, 255));

        bottom->AddChild(MakeButton("ufl-set-close", "Close", 90,
                [d]() { if (d->window) d->window->Close(); }));
        d->window->AddChild(bottom);

        // Escape closes, matching the framework's dialog convention.
        d->window->SetEventCallback([](const UCEvent& event) {
            if (event.type == UCEventType::KeyUp &&
                event.virtualKey == UCKeys::Escape) {
                if (auto tw = event.targetWindow.lock())
                    static_cast<UltraCanvasWindow*>(tw.get())->Close();
                return true;
            }
            return false;
        });
        d->window->onWindowClosed = [d]() {
            // The popup's callbacks reach back into this dialog state, so it
            // must not outlive the window it was opened from.
            CloseColorPopup(false);
            d->closed = true;
        };

        (void)root;
        d->window->Show();
    }

} // namespace

void UltraFilerSettingsDialog::Show(UltraCanvasWindowBase* parent,
                                    UltraFilerSettings* settings,
                                    std::function<void()> onChanged,
                                    std::function<void()> onClearHistory,
                                    std::function<void()> onClearFavorites,
                                    std::function<void()> onClearFolderViews) {
    // Raise the already open window instead of opening a second one.
    if (g_dialog && g_dialog->window && !g_dialog->closed) {
        g_dialog->window->Show();
        return;
    }

    auto state = std::make_shared<DialogState>();
    state->settings = settings;
    state->onChanged = std::move(onChanged);
    state->onClearHistory = std::move(onClearHistory);
    state->onClearFavorites = std::move(onClearFavorites);
    state->onClearFolderViews = std::move(onClearFolderViews);
    BuildDialog(state.get(), parent);
    if (state->window) g_dialog = state;   // keeps the widgets alive
}

} // namespace UltraCanvas
