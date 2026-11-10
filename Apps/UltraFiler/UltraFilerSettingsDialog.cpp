// Apps/UltraFiler/UltraFilerSettingsDialog.cpp
// UltraFiler settings window: settings-page tree on the left, the selected
// page on the right. The tree shows the three sections - Display, Handling,
// Extras - as its top level (the root row itself is hidden), each closed when
// the window opens, so it opens on the start page: what the sections hold.
// Pages: Display > Treeview (the folder tree's drive-row
// background and selected-folder highlight, each shown as a colour box that
// opens the colour picker in a popup window, and the tree's width - fitted
// to its rows or fixed), Display > Home folder (what the
// Home folder shows), Display > Files (whether hidden files are listed),
// Display > File names (how many lines a name may take under a thumbnail
// tile - the full name, or at most 2, 3 or 4 lines),
// Display > File extensions (whether a displayed name
// still ends in its extension, and whether a thumbnail tile carries that
// extension as a bar or a small tag), Display > Files in use (whether held
// files are marked), Display > PDF Inventory (the width of the page
// thumbnails in the preview's PDF page inventory - a fixed pixel width or a
// share of the preview's width, set with a slider), Display > Thumbnails and
// Display > Detail view (the list of files: which file kinds, and which
// individual formats inside them, are drawn as a thumbnail in the file
// display / opened in the detail pane beside it), Handling > Drag & Drop
// (what a plain drop onto a folder does - move or copy - and whether it asks
// first), Handling > Opening files (what a double-click on a file does when
// the system has a program registered for it - start that program, the way
// Explorer and the Finder do, or show the file in UltraFiler's preview),
// Handling > Protected files (how long UltraFiler may go without input before
// the preview forgets the passwords its Unlock was given), Display > New tab (what the "+" of the folder tab strip opens - the
// current folder again, the Home folder or the Computer page), Extras > Open prompt (the command
// line program UltraFiler opens, picked with the file dialog and stored with
// "Save app"), Extras > History & Favorites (how many entries each of the
// History view's three lists keeps, and clearing the recently-used
// lists and the pinned entries) and Extras > Cache (whether finished
// thumbnails are kept on disk between runs and compressed in memory, what
// each cache is holding against what it may hold, and emptying them), and
// Archives > TAR (how Compress packs a TAR: each compressor's level, the
// threads of xz and Zstandard, Zstandard's long mode, the header format,
// what each file keeps, symbolic links, and the names left out). Changes
// apply live and are saved immediately.
//
// Every page is built the same way (MakePage): a bold title, the one-line
// caption that says what the choice is about, the controls, and - set apart
// at the foot of the page in its own tinted block - the notes that explain
// the setting. A page's "Restore default ..." button is not among its
// controls but at the left end of the window's bottom bar, opposite Close,
// where the same spot serves every page that has one. The backdrop behind
// transparent images is no longer a page here: the media viewer's own colour
// strip under the picture chooses it, and the choice is saved from there.
// Version: 1.21.0 - Display > Path bar: a drive named by its volume on the path bar, or by its letter alone
// Version: 1.20.2 - the texts left in English go through Tr too: captions, tooltips, the cache figures (counts through TrN), the TAR labels; the conflict choices get ids that do not change with the language
// Version: 1.20.1 - the texts the wrapping tool 1.1.0 finds (the page kit's buttons, dropdown items, dialog buttons, tree nodes) go through Tr
// Version: 1.20.0 - the interface strings go through Tr (UltraLocalisation); "Translate UltraFiler..." on the start page opens the localisation editor
// Version: 1.19.0 - built on the framework's UltraCanvasSettingsPageKit: the window, the
//                   page skeleton and the controls come from it, nothing changes on screen
// Version: 1.18.0 - Archives > TAR: how Compress packs a TAR
// Version: 1.17.0 - Handling > Protected files: the minutes without input before the
//                   preview forgets Unlock's passwords (0: never)
// Version: 1.16.1 - the colour popup's picker is the framework's ColorPickerStyle::Light
// Version: 1.16.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraFilerSettingsDialog.h"
#include "UltraFilerPrompt.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasChip.h"   // UltraCanvasTagInput
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasFileLock.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasSettingsPageKit.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasSpinner.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTrash.h"   // TrashDisplayName
#include "UltraCanvasTreeView.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"
#include "UltraCanvasLocalisationEditor.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace UltraCanvas {

namespace {

    // The look - the type scale, the page skeleton, the controls and the
    // window with its tree of pages - is the framework's settings page kit,
    // which this window set the style of before it was shared.
    using namespace SettingsPageKit;

    // Page ids double as tree node ids.
    constexpr const char* kPageDisplay = "display";
    constexpr const char* kPageTreeview = "display/treeview";
    constexpr const char* kPageTabStyle = "display/tab-style";
    constexpr const char* kPageNewTab = "display/new-tab";
    constexpr const char* kPageHomeFolder = "display/home";
    constexpr const char* kPageFiles = "display/files";
    constexpr const char* kPageIgnoredFiles = "display/ignored-files";
    constexpr const char* kPagePdfInventory = "display/pdf-inventory";
    constexpr const char* kPageThumbnails = "display/thumbnails";
    constexpr const char* kPageFileNames = "display/file-names";
    constexpr const char* kPageFileExtensions = "display/file-extensions";
    constexpr const char* kPageFileIcons = "display/file-icons";
    constexpr const char* kPageFilesInUse = "display/files-in-use";
    constexpr const char* kPagePathBar = "display/path-bar";
    constexpr const char* kPageDetailView = "display/detail-view";
    constexpr const char* kPageHandling = "handling";
    constexpr const char* kPageDragDrop = "handling/drag-drop";
    constexpr const char* kPageFileOperations = "handling/file-operations";
    constexpr const char* kPageOpeningFiles = "handling/opening-files";
    constexpr const char* kPageProtectedFiles = "handling/protected-files";
    constexpr const char* kPageExtras = "extras";
    constexpr const char* kPageOpenPrompt = "extras/open-prompt";
    constexpr const char* kPageLists = "extras/history-favorites";
    constexpr const char* kPageCache = "extras/cache";
    constexpr const char* kPageArchives = "archives";
    constexpr const char* kPageTar = "archives/tar";
    // The page shown while nothing is selected - the window opens with every
    // section closed, so there is no page to show yet.
    constexpr const char* kPageStart = "start";

    // The one open settings window (or the last closed one, until reopened).
    struct DialogState : public UltraCanvasSettingsWindow {
        // Set while SyncControlsFromSettings pushes the settings onto the
        // controls: the change handlers that fires write the same values
        // back, and must not save or re-apply them once more.
        bool                                  syncing = false;

        // Display > Treeview: the colour boxes that open the picker popup.
        std::shared_ptr<UltraCanvasButton> driveColorBox;
        std::shared_ptr<UltraCanvasButton> selectedColorBox;
        // ... and the tree's width: fitted to its rows, or [ 280 px ].
        std::shared_ptr<UltraCanvasRadio>   treeFitRadio;
        std::shared_ptr<UltraCanvasRadio>   treeFixedRadio;
        UltraCanvasRadioGroup               treeWidthGroup;
        std::shared_ptr<UltraCanvasSpinner> treeWidthSpinner;

        // Display > Home folder: what the Home folder shows.
        std::shared_ptr<UltraCanvasRadio>  homeAllRadio;
        std::shared_ptr<UltraCanvasRadio>  homePredefinedRadio;
        UltraCanvasRadioGroup              homeContentGroup;

        // Display > File names: one radio per choice (full name, 2, 3, 4
        // lines), paired with the line count it stands for (0 = full name).
        std::vector<std::pair<int, std::shared_ptr<UltraCanvasRadio>>> fileNameRadios;
        UltraCanvasRadioGroup                                         fileNameGroup;

        // Display > File extensions: the "keep them in the name" checkbox and
        // one radio per thumbnail tag mode (None / Bar / Icon), in the order
        // the widget lists them.
        std::shared_ptr<UltraCanvasCheckbox> extensionsInNamesBox;
        // Display > Files in use: the "mark held files" checkbox.
        std::shared_ptr<UltraCanvasCheckbox> lockMarkingBox;
        // Display > Path bar: the "drive's volume name" checkbox.
        std::shared_ptr<UltraCanvasCheckbox> pathBarVolumeNamesBox;
        // Display > Thumbnails: the "pictures inside a folder on its icon"
        // checkbox above the kind list.
        std::shared_ptr<UltraCanvasCheckbox> folderPreviewsBox;
        // Display > Files: the "show hidden files" checkbox.
        std::shared_ptr<UltraCanvasCheckbox> hiddenFilesBox;
        // Display > Ignored files: the master switch, one checkbox per
        // built-in pattern (kept so "Restore the built-in list" can re-tick
        // them), the field the user's own patterns are typed into, and where
        // they apply.
        std::shared_ptr<UltraCanvasCheckbox> clutterBox;
        std::vector<std::pair<std::string,
                              std::shared_ptr<UltraCanvasCheckbox>>> clutterRows;
        std::shared_ptr<UltraCanvasTagInput> ignorePatternsInput;
        std::shared_ptr<UltraCanvasRadio>    ignoreHomeRadio;
        std::shared_ptr<UltraCanvasRadio>    ignoreEverywhereRadio;
        UltraCanvasRadioGroup                ignoreScopeGroup;
        std::vector<std::pair<FilerExtensionBadge,
                              std::shared_ptr<UltraCanvasRadio>>> badgeRadios;
        UltraCanvasRadioGroup                extensionBadgeGroup;
        std::vector<std::pair<FilerFileIconStyle,
                              std::shared_ptr<UltraCanvasRadio>>> fileIconRadios;
        UltraCanvasRadioGroup                fileIconGroup;
        std::vector<std::pair<FilerTabStripStyle,
                              std::shared_ptr<UltraCanvasRadio>>> tabStyleRadios;
        UltraCanvasRadioGroup                tabStyleGroup;

        // Display > Thumbnails / Display > Detail view: the kind checkboxes
        // and the per-format ones of each page, kept so the two "Everything
        // on / off" buttons (and a kind switch) can re-sync the page.
        struct FormatRow {
            std::string      extension;
            FilerPreviewType kind = FilerPreviewType::NonePreview;
            bool             supported = false;   // this build can show it
            std::shared_ptr<UltraCanvasCheckbox> box;
        };
        struct FormatSwitchPage {
            std::vector<std::shared_ptr<UltraCanvasCheckbox>> kindBoxes;
            std::vector<FormatRow> formatRows;
        };
        FormatSwitchPage thumbnailPage;
        FormatSwitchPage detailViewPage;

        // Display > PDF Inventory: thumbnail width mode + the two sliders.
        std::shared_ptr<UltraCanvasRadio>  pdfAbsoluteRadio;
        std::shared_ptr<UltraCanvasRadio>  pdfRelativeRadio;
        UltraCanvasRadioGroup              pdfWidthGroup;
        std::shared_ptr<UltraCanvasSlider> pdfWidthSlider;
        std::shared_ptr<UltraCanvasSlider> pdfPercentSlider;
        std::shared_ptr<UltraCanvasLabel>  pdfWidthValue;
        std::shared_ptr<UltraCanvasLabel>  pdfPercentValue;

        // Handling > Drag & Drop: what a plain drop onto a folder does.
        std::shared_ptr<UltraCanvasRadio>       dropMoveRadio;
        std::shared_ptr<UltraCanvasRadio>       dropCopyRadio;
        UltraCanvasRadioGroup                   dropOnFolderGroup;

        // Handling > File operations: what asks first, the standing answers
        // to a taken name and to a failure, and the progress window's delay.
        std::shared_ptr<UltraCanvasCheckbox>    confirmMovesBox;
        std::shared_ptr<UltraCanvasCheckbox>    confirmCopiesBox;
        std::shared_ptr<UltraCanvasCheckbox>    confirmTrashBox;
        std::shared_ptr<UltraCanvasCheckbox>    confirmPermanentBox;   // always on
        std::vector<std::pair<FilerConflictPolicy,
                              std::shared_ptr<UltraCanvasRadio>>> conflictRadios;
        UltraCanvasRadioGroup                   conflictGroup;
        std::shared_ptr<UltraCanvasRadio>       folderMergeRadio;
        std::shared_ptr<UltraCanvasRadio>       folderAskRadio;
        UltraCanvasRadioGroup                   folderConflictGroup;
        std::shared_ptr<UltraCanvasRadio>       failureAskRadio;
        std::shared_ptr<UltraCanvasRadio>       failureSkipRadio;
        UltraCanvasRadioGroup                   failureGroup;
        std::shared_ptr<UltraCanvasSlider>      progressDelaySlider;
        std::shared_ptr<UltraCanvasLabel>       progressDelayValue;

        // Display > New tab: what the tab strip's "+" opens.
        std::shared_ptr<UltraCanvasRadio>       newTabCurrentRadio;
        std::shared_ptr<UltraCanvasRadio>       newTabHomeRadio;
        std::shared_ptr<UltraCanvasRadio>       newTabComputerRadio;
        UltraCanvasRadioGroup                   newTabGroup;

        // Handling > Opening files: what a double-click on a file does when
        // the system has a program registered for it.
        std::shared_ptr<UltraCanvasRadio>       openAppRadio;
        std::shared_ptr<UltraCanvasRadio>       openPreviewRadio;
        UltraCanvasRadioGroup                   openActivationGroup;
        // ... and whether pictures, audio and video go to UltraViewer first.
        std::shared_ptr<UltraCanvasCheckbox>    mediaUltraViewerBox;

        // Handling > Protected files: minutes without input before the
        // preview forgets Unlock's passwords.
        std::shared_ptr<UltraCanvasSlider> forgetPasswordsSlider;
        std::shared_ptr<UltraCanvasLabel>  forgetPasswordsValue;

        // Extras > Open prompt
        std::shared_ptr<UltraCanvasTextInput> promptInput;   // chosen application
        std::shared_ptr<UltraCanvasLabel>     promptStatus;  // what will be started

        // History & Favorites: the limit of entries per History list, and the
        // line the clear buttons write.
        std::shared_ptr<UltraCanvasSlider> historyLimitSlider;
        std::shared_ptr<UltraCanvasLabel>  historyLimitValue;
        std::shared_ptr<UltraCanvasLabel>  listsStatus;   // "History cleared."

        // Extras > Cache: the two switches, the four "used of max" lines and
        // the line the Empty cache button writes. Kept so the numbers can be
        // refreshed - they are live figures, not a snapshot taken once when
        // the window was built.
        std::shared_ptr<UltraCanvasSwitch> diskCacheSwitch;
        std::shared_ptr<UltraCanvasSwitch> compressedSwitch;
        std::shared_ptr<UltraCanvasLabel>  cacheDiskLine;
        std::shared_ptr<UltraCanvasLabel>  cacheMemoryLine;
        std::shared_ptr<UltraCanvasLabel>  cacheIconLine;
        std::shared_ptr<UltraCanvasLabel>  cacheHotLine;
        std::shared_ptr<UltraCanvasLabel>  cacheMissingLine;
        std::shared_ptr<UltraCanvasLabel>  cacheLocationLine;
        std::shared_ptr<UltraCanvasLabel>  cacheStatus;

        // Archives > TAR: a slider and its value per compressor level and
        // for the threads, the switches, the header format and link radios,
        // and the field the exclusions are typed into.
        struct TarSliderRow {
            std::shared_ptr<UltraCanvasSlider> slider;
            std::shared_ptr<UltraCanvasLabel>  value;
        };
        TarSliderRow tarGzip, tarBzip2, tarXz, tarZstd, tarThreads;
        std::shared_ptr<UltraCanvasCheckbox> tarLongBox;
        std::shared_ptr<UltraCanvasCheckbox> tarTimesBox;
        std::shared_ptr<UltraCanvasCheckbox> tarPermissionsBox;
        std::shared_ptr<UltraCanvasCheckbox> tarOwnerBox;
        std::vector<std::pair<FilerTarHeaderFormat,
                              std::shared_ptr<UltraCanvasRadio>>> tarFormatRadios;
        UltraCanvasRadioGroup                tarFormatGroup;
        std::shared_ptr<UltraCanvasRadio>    tarLinkRadio;
        std::shared_ptr<UltraCanvasRadio>    tarTargetRadio;
        UltraCanvasRadioGroup                tarLinksGroup;
        std::shared_ptr<UltraCanvasTagInput> tarExcludeInput;

        UltraFilerSettings*   settings = nullptr;
        std::function<void()> onChanged;
        UltraFilerSettingsDialog::CacheHooks cacheHooks;
        std::function<void()> onClearHistory;
        std::function<void()> onClearFavorites;
        std::function<void()> onClearFolderViews;
    };

    std::shared_ptr<DialogState> g_dialog;

    // Set by the host before the window is ever opened, and kept outside the
    // dialog because it outlives it: the window is built and torn down again,
    // the application's file displays are not.
    UltraFilerSettingsDialog::CacheHooks g_cacheHooks;

    void ApplyAndSave(DialogState* d) {
        if (d->syncing) return;   // SyncControlsFromSettings: nothing changed
        if (d->onChanged) d->onChanged();
        if (d->settings) d->settings->Save();
    }

    // ===== COLOUR PICKER POPUP =====

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
        const Color popupSurface(249, 249, 251, 255);
        popup->window->SetBackgroundColor(popupSurface);

        popup->picker = CreateColorPicker("ufl-set-popup-picker", initial,
                                          0, 0, 280, 380);
        // The framework's light picker, on the popup's surface.
        ColorPickerStyle pickerStyle = ColorPickerStyle::Light();
        pickerStyle.backgroundColor = popupSurface;
        popup->picker->SetStyle(pickerStyle);
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
        buttons->AddChild(MakeButton("ufl-set-popup-ok", Tr("Use colour"), 110,
                []() { CloseColorPopup(true); }));
        buttons->AddChild(MakeButton("ufl-set-popup-cancel", Tr("Cancel"), 90,
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
        return MakeLabelledRow(id, caption, box);
    }

    std::shared_ptr<UltraCanvasContainer> BuildTreeviewPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-treeview", Tr("Treeview"),
                Tr("Colours and width of the folder tree on the left of the main "
                   "window:"));

        d->driveColorBox = MakeColorBox("ufl-set-tv-drive-color",
                d->settings->treeDriveBackgroundColor,
                Tr("Row background of the drives in the folder tree"),
                [d]() {
            ShowColorPickerPopup(d, Tr("Drive background colour"),
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
        parts.body->AddChild(MakeColorRow("ufl-set-tv-drive-row",
                Tr("Drive background colour:"), d->driveColorBox));

        d->selectedColorBox = MakeColorBox("ufl-set-tv-selected-color",
                d->settings->treeSelectedFolderColor,
                Tr("Highlight of the selected folder in the folder tree"),
                [d]() {
            ShowColorPickerPopup(d, Tr("Selected folder colour"),
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
        parts.body->AddChild(MakeColorRow("ufl-set-tv-selected-row",
                Tr("Selected folder colour:"), d->selectedColorBox));

        // ----- the tree's width: fitted, or [ 280 px ] -----
        AddBodyCaption(parts, "ufl-set-tv-width-caption",
                       Tr("Width of the folder tree:"));
        const bool fit = d->settings->treeWidthAuto;
        d->treeFitRadio = MakeChoice("ufl-set-tv-width-fit",
                Tr("Auto - 10 px wider than the longest name shown"), fit);
        d->treeFixedRadio = MakeChoice("ufl-set-tv-width-fixed", Tr("Fixed width:"),
                                       !fit, 110);
        d->treeWidthGroup.AddRadioButton(d->treeFitRadio);
        d->treeWidthGroup.AddRadioButton(d->treeFixedRadio);
        d->treeWidthGroup.onSelectionChanged =
                [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->settings) return;
            d->settings->treeWidthAuto = (selected == d->treeFitRadio);
            ApplyAndSave(d);
        };

        d->treeWidthSpinner = CreateIntSpinner("ufl-set-tv-width", 0, 0, 90,
                static_cast<float>(kControlHeight),
                UltraFilerSettings::kMinTreeWidth,
                UltraFilerSettings::kMaxTreeWidth,
                d->settings->treeFixedWidth, 10);
        d->treeWidthSpinner->SetSuffix(" px");
        d->treeWidthSpinner->GetStyle().fontStyle.fontSize = kTextFontSize;
        d->treeWidthSpinner->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        // Setting a width is choosing a fixed one.
        d->treeWidthSpinner->onValueChanged = [d](double value) {
            if (d->syncing || !d->settings) return;
            d->settings->treeFixedWidth = static_cast<int>(std::lround(value));
            if (d->settings->treeWidthAuto) {
                d->settings->treeWidthAuto = false;
                d->syncing = true;
                d->treeWidthGroup.SelectButton(d->treeFixedRadio);
                d->syncing = false;
            }
            ApplyAndSave(d);
        };

        auto fixedRow = std::make_shared<UltraCanvasContainer>(
                "ufl-set-tv-width-fixed-row");
        fixedRow->layout.SetFlexRow().SetFlexGap(6)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        fixedRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        fixedRow->AddChild(d->treeFixedRadio);
        fixedRow->AddChild(d->treeWidthSpinner);
        parts.body->AddChild(d->treeFitRadio);
        parts.body->AddChild(fixedRow);

        AddNote(parts, "ufl-set-tv-note1",
                Tr("Click a colour box to pick a colour. The folder tree shows "
                   "the colour while it is being picked; Cancel puts the previous "
                   "one back."));
        AddNote(parts, "ufl-set-tv-note2",
                Tr("Auto fits the tree to the drives and folders it shows, and "
                   "fits it again whenever a branch is opened or closed or a "
                   "drive comes or goes - up to 600 px, and never so wide that "
                   "the file display falls below its own minimum. Dragging the "
                   "divider still resizes the tree for the moment."));

        d->resets[kPageTreeview] = PageReset{Tr("Restore defaults"), 170,
                [d]() {
            if (!d->settings) return;
            d->settings->treeDriveBackgroundColor =
                    UltraFilerSettings::kDefaultTreeDriveBackgroundColor;
            d->settings->treeSelectedFolderColor =
                    UltraFilerSettings::kDefaultTreeSelectedFolderColor;
            d->settings->treeWidthAuto = true;
            d->settings->treeFixedWidth = UltraFilerSettings::kDefaultTreeWidth;
            SetColorBoxColor(d->driveColorBox,
                             d->settings->treeDriveBackgroundColor);
            SetColorBoxColor(d->selectedColorBox,
                             d->settings->treeSelectedFolderColor);
            d->syncing = true;
            d->treeWidthGroup.SelectButton(d->treeFitRadio);
            d->treeWidthSpinner->SetValue(d->settings->treeFixedWidth);
            d->syncing = false;
            ApplyAndSave(d);
        }};

        return parts.page;
    }

    // ===== SLIDERS =====
    // Shared by every page that sets a number: Display > PDF Inventory's two
    // widths and Extras > History & Favorites' limit of entries.

    // One "[caption] [slider] [value]" row. `indent` is what the caption
    // starts at - the PDF widths sit under the radio button they belong to,
    // a row that answers to nothing above it starts at the margin.
    // ===== DISPLAY > PDF INVENTORY =====

    // "56 px" / "25 %" next to the slider it belongs to.
    void UpdatePdfWidthLabels(DialogState* d) {
        if (!d->settings) return;
        if (d->pdfWidthValue) {
            d->pdfWidthValue->SetText(
                    std::to_string(d->settings->pdfThumbnailWidth) + " px");
            d->pdfWidthValue->RequestRedraw();
        }
        if (d->pdfPercentValue) {
            d->pdfPercentValue->SetText(
                    std::to_string(d->settings->pdfThumbnailWidthPercent) + " %");
            d->pdfPercentValue->RequestRedraw();
        }
    }

    // Moving either width slider selects its mode too, so the slider a user
    // reaches for is the one that takes effect rather than a value nothing
    // reads - see the two callbacks below.
    std::shared_ptr<UltraCanvasContainer> BuildPdfInventoryPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-pdf", Tr("PDF Inventory"),
                Tr("Width of the page thumbnails in the preview's PDF page "
                   "inventory, the strip beside the page:"));

        const bool absolute = d->settings->pdfThumbnailAbsoluteWidth;

        d->pdfAbsoluteRadio = MakeChoice("ufl-set-pdf-absolute",
                Tr("Fixed width"), absolute);
        d->pdfRelativeRadio = MakeChoice("ufl-set-pdf-relative",
                Tr("Relative to the preview's width"), !absolute);
        d->pdfWidthGroup.AddRadioButton(d->pdfAbsoluteRadio);
        d->pdfWidthGroup.AddRadioButton(d->pdfRelativeRadio);
        d->pdfWidthGroup.onSelectionChanged =
                [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->settings) return;
            d->settings->pdfThumbnailAbsoluteWidth = (selected == d->pdfAbsoluteRadio);
            ApplyAndSave(d);
        };
        parts.body->AddChild(d->pdfAbsoluteRadio);

        d->pdfWidthValue = MakeSliderValueLabel("ufl-set-pdf-width-value");
        d->pdfWidthSlider = MakeIntSlider("ufl-set-pdf-width",
                UltraFilerSettings::kMinPdfThumbnailWidth,
                UltraFilerSettings::kMaxPdfThumbnailWidth,
                d->settings->pdfThumbnailWidth, [d](int px) {
            if (!d->settings) return;
            d->settings->pdfThumbnailWidth = px;
            // Dragging this slider is a request for the width it sets, so it
            // selects its mode too instead of moving a value nothing uses.
            d->settings->pdfThumbnailAbsoluteWidth = true;
            // Checking the radio runs the group's selection callback, which is
            // what keeps the other one clear; it is a no-op when already checked.
            if (d->pdfAbsoluteRadio) d->pdfAbsoluteRadio->SetChecked(true);
            UpdatePdfWidthLabels(d);
            ApplyAndSave(d);
        });
        parts.body->AddChild(MakeSliderRow("ufl-set-pdf-width-row",
                Tr("Thumbnails width:"), d->pdfWidthSlider, d->pdfWidthValue));

        parts.body->AddChild(d->pdfRelativeRadio);

        d->pdfPercentValue = MakeSliderValueLabel("ufl-set-pdf-percent-value");
        d->pdfPercentSlider = MakeIntSlider("ufl-set-pdf-percent",
                UltraFilerSettings::kMinPdfThumbnailPercent,
                UltraFilerSettings::kMaxPdfThumbnailPercent,
                d->settings->pdfThumbnailWidthPercent, [d](int percent) {
            if (!d->settings) return;
            d->settings->pdfThumbnailWidthPercent = percent;
            d->settings->pdfThumbnailAbsoluteWidth = false;
            if (d->pdfRelativeRadio) d->pdfRelativeRadio->SetChecked(true);
            UpdatePdfWidthLabels(d);
            ApplyAndSave(d);
        });
        parts.body->AddChild(MakeSliderRow("ufl-set-pdf-percent-row",
                Tr("Share of the width:"), d->pdfPercentSlider, d->pdfPercentValue));

        AddNote(parts, "ufl-set-pdf-note1",
                Tr("Fixed width: the strip stays the same whatever the preview's "
                   "size. Relative: it grows and shrinks with the window. Moving "
                   "a slider selects its mode."));
        AddNote(parts, "ufl-set-pdf-note2",
                Tr("The inventory only appears for documents with more than one "
                   "page. It gives way when it would take more than half of a "
                   "very narrow preview."));

        d->resets[kPagePdfInventory] = PageReset{Tr("Restore default widths"), 160,
                [d]() {
            if (!d->settings) return;
            // Moving a slider selects its own mode, so both sliders together
            // would leave the last one's mode behind: the chosen mode is put
            // back afterwards.
            const bool wasAbsolute = d->settings->pdfThumbnailAbsoluteWidth;
            if (d->pdfWidthSlider)
                d->pdfWidthSlider->SetValue(static_cast<float>(
                        UltraFilerSettings::kDefaultPdfThumbnailWidth));
            if (d->pdfPercentSlider)
                d->pdfPercentSlider->SetValue(static_cast<float>(
                        UltraFilerSettings::kDefaultPdfThumbnailPercent));
            // The sliders only report a value they actually moved to, so the
            // settings are written here too.
            d->settings->pdfThumbnailWidth =
                    UltraFilerSettings::kDefaultPdfThumbnailWidth;
            d->settings->pdfThumbnailWidthPercent =
                    UltraFilerSettings::kDefaultPdfThumbnailPercent;
            d->settings->pdfThumbnailAbsoluteWidth = wasAbsolute;
            auto& radio = wasAbsolute ? d->pdfAbsoluteRadio : d->pdfRelativeRadio;
            if (radio) radio->SetChecked(true);
            UpdatePdfWidthLabels(d);
            ApplyAndSave(d);
        }};

        UpdatePdfWidthLabels(d);
        return parts.page;
    }

    // ===== DISPLAY > THUMBNAILS / DISPLAY > DETAIL VIEW =====
    // The two "list of files" pages. Both show the same thing for a different
    // display feature: the nine file kinds as one checkbox each, and under
    // every kind the individual formats belonging to it - between them they
    // hold every format the FileLoader inventory reports for this build. A
    // format the build
    // cannot show at all (no PostScript loader, no PDF plugin, no video
    // backend, a format the media viewer has no view for) is listed too, but
    // greyed: seeing that eps is unsupported here is what explains the missing
    // thumbnail, which an omitted entry would not.

    DialogState::FormatSwitchPage& PageState(DialogState* d,
                                             FilerPreviewTarget target) {
        return target == FilerPreviewTarget::Thumbnails ? d->thumbnailPage
                                                        : d->detailViewPage;
    }

    uint32_t& KindMask(DialogState* d, FilerPreviewTarget target) {
        return target == FilerPreviewTarget::Thumbnails
                       ? d->settings->thumbnailKinds
                       : d->settings->detailViewKinds;
    }

    std::vector<std::string>& DisabledFormats(DialogState* d,
                                              FilerPreviewTarget target) {
        return target == FilerPreviewTarget::Thumbnails
                       ? d->settings->disabledThumbnailFormats
                       : d->settings->disabledDetailViewFormats;
    }

    bool FormatIsOff(const std::vector<std::string>& off, const std::string& ext) {
        return std::find(off.begin(), off.end(), ext) != off.end();
    }

    void SetFormatOff(std::vector<std::string>& off, const std::string& ext,
                      bool switchedOff) {
        auto it = std::find(off.begin(), off.end(), ext);
        if (switchedOff && it == off.end()) {
            off.push_back(ext);
            std::sort(off.begin(), off.end());
        } else if (!switchedOff && it != off.end()) {
            off.erase(it);
        }
    }

    // Whether this build can show the format in the display the page governs.
    bool FormatSupportedFor(const FilerFormatInfo& info,
                            FilerPreviewTarget target) {
        if (target == FilerPreviewTarget::Thumbnails) return info.thumbnailSupported;
        // The detail pane is the media viewer, so it decides for itself; the
        // extension is dressed as a file name because that is what it takes.
        return UltraCanvasMediaViewer::IsSupportedMedia("file." + info.extension);
    }

    // Pushes the settings back onto the page's controls: after a kind switch
    // (which greys the formats under it) and after the two bulk buttons.
    void RefreshFormatPage(DialogState* d, FilerPreviewTarget target) {
        if (!d->settings) return;
        DialogState::FormatSwitchPage& page = PageState(d, target);
        const uint32_t mask = KindMask(d, target);
        const std::vector<std::string>& off = DisabledFormats(d, target);
        const std::vector<FilerPreviewType>& kinds =
                UltraCanvasFilerWidget::AllPreviewTypes();
        for (size_t i = 0; i < page.kindBoxes.size() && i < kinds.size(); ++i) {
            const bool on = (mask & static_cast<uint32_t>(kinds[i])) != 0;
            page.kindBoxes[i]->SetChecked(on);
            page.kindBoxes[i]->RequestRedraw();
        }
        for (DialogState::FormatRow& row : page.formatRows) {
            if (!row.box) continue;
            const bool kindOn = (mask & static_cast<uint32_t>(row.kind)) != 0;
            row.box->SetChecked(!FormatIsOff(off, row.extension));
            row.box->SetDisabled(!row.supported || !kindOn);
            row.box->RequestRedraw();
        }
    }

    std::shared_ptr<UltraCanvasContainer> BuildFormatSwitchPage(
            DialogState* d, FilerPreviewTarget target) {
        const bool thumbnails = target == FilerPreviewTarget::Thumbnails;
        const std::string idBase = thumbnails ? "ufl-set-thumb" : "ufl-set-detail";

        // The list is longer than the window, so the page scrolls.
        PageParts parts = MakePage(idBase + "-page",
                thumbnails ? Tr("Thumbnails") : Tr("Detail view"),
                thumbnails
                ? Tr("Which files the display draws a thumbnail of, rendered "
                     "from the file itself:")
                : Tr("Which files the detail pane opens when one of them is "
                     "selected:"),
                /*scrolls=*/true);
        parts.body->layout.SetFlexGap(4);

        AddNote(parts, idBase + "-note1", thumbnails
                ? Tr("A kind switched off - or one format ticked off under it - "
                     "keeps its type glyph and is not read at all. Greyed "
                     "formats: nothing in this build renders them.")
                : Tr("A kind switched off - or one format ticked off under it - "
                     "leaves the whole width to the file display. Greyed "
                     "formats: this build has no view for them."));

        if (thumbnails) {
            // Display > Folder previews: the first pictures inside a folder
            // peeking out of its icon, drawn with the same thumbnails the
            // switches below govern.
            d->folderPreviewsBox = MakeCheckbox(idBase + "-folder-previews",
                    Tr("Show the first pictures inside a folder on its icon"),
                    kTextWidth, d->settings->folderPreviews, [d](bool on) {
                if (!d->settings) return;
                d->settings->folderPreviews = on;
                ApplyAndSave(d);
            });
            parts.body->AddChild(d->folderPreviewsBox);
        }

        auto buttons = MakeButtonRow(idBase + "-buttons");
        buttons->AddChild(MakeButton(idBase + "-all-on", Tr("Everything on"), 120,
                [d, target]() {
            if (!d->settings) return;
            KindMask(d, target) = kFilerAllPreviewTypes;
            DisabledFormats(d, target).clear();
            RefreshFormatPage(d, target);
            ApplyAndSave(d);
        }));
        buttons->AddChild(MakeButton(idBase + "-all-off", Tr("Everything off"), 120,
                [d, target]() {
            if (!d->settings) return;
            KindMask(d, target) = 0;
            RefreshFormatPage(d, target);
            ApplyAndSave(d);
        }));
        parts.body->AddChild(buttons);

        DialogState::FormatSwitchPage& state = PageState(d, target);
        state.kindBoxes.clear();
        state.formatRows.clear();

        const std::vector<FilerFormatInfo> formats =
                UltraCanvasFilerWidget::GetPreviewableFormats();
        const uint32_t mask = KindMask(d, target);
        const std::vector<std::string>& off = DisabledFormats(d, target);

        int rowIndex = 0;
        for (FilerPreviewType kind : UltraCanvasFilerWidget::AllPreviewTypes()) {
            const std::string kindId = idBase + "-kind-" +
                    std::to_string(static_cast<uint32_t>(kind));
            auto kindBox = MakeCheckbox(kindId,
                    UltraCanvasFilerWidget::PreviewTypeLabel(kind), 300,
                    (mask & static_cast<uint32_t>(kind)) != 0,
                    [d, target, kind](bool on) {
                if (!d->settings) return;
                uint32_t& m = KindMask(d, target);
                const uint32_t bit = static_cast<uint32_t>(kind);
                m = on ? (m | bit) : (m & ~bit);
                RefreshFormatPage(d, target);   // greys the formats under it
                ApplyAndSave(d);
            });
            kindBox->SetMargin(6, 0, 0, 0);   // a kind heads its formats
            state.kindBoxes.push_back(kindBox);
            parts.body->AddChild(kindBox);

            // The formats of this kind, four to a row so the page stays a
            // page instead of a hundred-line column.
            std::shared_ptr<UltraCanvasContainer> row;
            int inRow = 0;
            for (const FilerFormatInfo& info : formats) {
                if (info.kind != kind) continue;
                if (!row || inRow == 4) {
                    row = std::make_shared<UltraCanvasContainer>(
                            idBase + "-row-" + std::to_string(rowIndex++));
                    row->layout.SetFlexRow().SetFlexGap(6)
                               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
                    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                    row->size.width  = CSSLayout::Dimension::Px(470);
                    row->size.height = CSSLayout::Dimension::Px(kControlHeight);
                    row->SetPadding(0, 0, 0, 24);   // indented under the kind
                    parts.body->AddChild(row);
                    inRow = 0;
                }
                DialogState::FormatRow entry;
                entry.extension = info.extension;
                entry.kind      = info.kind;
                entry.supported = FormatSupportedFor(info, target);
                const std::string ext = info.extension;
                entry.box = MakeCheckbox(idBase + "-fmt-" + ext, ext, 104,
                        !FormatIsOff(off, ext), [d, target, ext](bool on) {
                    if (!d->settings) return;
                    SetFormatOff(DisabledFormats(d, target), ext, !on);
                    ApplyAndSave(d);
                });
                entry.box->SetTooltip(entry.supported
                        ? info.label
                        : TrF("{0} - not supported by this build", info.label));
                entry.box->SetDisabled(!entry.supported ||
                        (mask & static_cast<uint32_t>(kind)) == 0);
                row->AddChild(entry.box);
                state.formatRows.push_back(entry);
                ++inRow;
            }
        }
        return parts.page;
    }

    // ===== DISPLAY > FILE NAMES =====
    // How much of a long name a thumbnail tile shows: all of it, wrapped over
    // as many lines as it takes, or at most 2 / 3 / 4 lines, past which the
    // head of the last line gives way to "…" so the extension stays in view.
    std::shared_ptr<UltraCanvasContainer> BuildFileNamesPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-file-names", Tr("File names"),
                Tr("How long names are shown under the tiles of the thumbnail "
                   "views:"));

        const std::pair<int, std::string> choices[] = {
            {UltraFilerSettings::kFileNameLinesFull,
                    Tr("Full name - as many lines as the name needs")},
            {2, Tr("Maximum 2 rows")},
            {3, Tr("Maximum 3 rows")},
            {4, Tr("Maximum 4 rows")},
        };
        d->fileNameRadios.clear();
        for (const auto& [lines, text] : choices) {
            std::string id = "ufl-set-file-names-" +
                    (lines == UltraFilerSettings::kFileNameLinesFull
                             ? std::string("full") : std::to_string(lines));
            auto radio = MakeChoice(id, text,
                    d->settings && d->settings->fileNameMaxLines == lines);
            d->fileNameGroup.AddRadioButton(radio);
            d->fileNameRadios.emplace_back(lines, radio);
            parts.body->AddChild(radio);
        }
        d->fileNameGroup.onSelectionChanged =
                [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->settings) return;
            for (const auto& [lines, radio] : d->fileNameRadios) {
                if (radio != selected) continue;
                d->settings->fileNameMaxLines = lines;
                ApplyAndSave(d);
                return;
            }
        };

        AddNote(parts, "ufl-set-file-names-note1",
                Tr("A name is broken between words where it can. When it needs "
                   "more rows than allowed, the start of the last row is left "
                   "out and replaced by \"…\", so the end of the name and its "
                   "extension stay readable; hovering the tile shows the whole "
                   "name."));
        AddNote(parts, "ufl-set-file-names-note2",
                Tr("Every tile of a row is as tall as its longest caption, so "
                   "more rows per name means fewer tiles on the screen. The "
                   "Details, List and Size bars views keep one line per file."));
        return parts.page;
    }

    // ===== DISPLAY > FILE EXTENSIONS =====
    // Two switches that belong together: whether a displayed name still ends
    // in ".exe", and what a thumbnail tile shows about the type instead - the
    // bar with the extension at its right end, the tag alone, or nothing.
    // Neither touches the file: the widget draws a shortened name, it does not
    // own a second one, so renaming and every file operation keep working on
    // the real name.
    std::string ExtensionBadgeDescription(FilerExtensionBadge badge) {
        switch (badge) {
            case FilerExtensionBadge::Bar:
                return Tr("Bar - a strip across the foot of the icon, extension "
                          "at its right end");
            case FilerExtensionBadge::Icon:
                return Tr("Icon - the extension alone, in the icon's bottom-right "
                          "corner");
            default:
                return Tr("None - the tiles show the name only");
        }
    }

    std::shared_ptr<UltraCanvasContainer> BuildFileExtensionsPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-ext", Tr("File extensions"),
                Tr("How the file display names a file and shows what type it is:"));

        d->extensionsInNamesBox = MakeCheckbox("ufl-set-ext-in-names",
                Tr("Show the file extension in the name"), kTextWidth,
                d->settings->showFileExtensions, [d](bool on) {
            if (!d->settings) return;
            d->settings->showFileExtensions = on;
            ApplyAndSave(d);
        });
        parts.body->AddChild(d->extensionsInNamesBox);

        AddBodyCaption(parts, "ufl-set-ext-badge-caption",
                Tr("In the thumbnail views, show the extension on the tile:"));

        d->badgeRadios.clear();
        for (FilerExtensionBadge badge :
             UltraCanvasFilerWidget::AllExtensionBadges()) {
            auto radio = MakeChoice(
                    std::string("ufl-set-ext-badge-") +
                            UltraCanvasFilerWidget::ExtensionBadgeLabel(badge),
                    ExtensionBadgeDescription(badge),
                    d->settings->extensionBadge == badge);
            d->extensionBadgeGroup.AddRadioButton(radio);
            d->badgeRadios.emplace_back(badge, radio);
            parts.body->AddChild(radio);
        }
        d->extensionBadgeGroup.onSelectionChanged =
                [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->settings) return;
            for (const auto& [badge, radio] : d->badgeRadios) {
                if (radio != selected) continue;
                d->settings->extensionBadge = badge;
                ApplyAndSave(d);
                return;
            }
        };

        AddNote(parts, "ufl-set-ext-note1",
                Tr("With the extension hidden, \"UltraFiler.exe\" is listed as "
                   "\"UltraFiler\". Only the drawn name changes - renaming, "
                   "sorting and every file operation keep using the real name."));
        AddNote(parts, "ufl-set-ext-note2",
                Tr("The tag is drawn over the foot of the icon, so no tile grows "
                   "for it, and a folder - or a name whose tail is a version "
                   "rather than a type - never gets one."));
        return parts.page;
    }

    // ===== DISPLAY > FILE ICONS =====
    std::string FileIconStyleDescription(FilerFileIconStyle style) {
        switch (style) {
            case FilerFileIconStyle::HostOperatingSystem:
                return Tr("Host OS icons - what this desktop draws for the type");
            default:
                return Tr("UltraFiler simple - the drawn folder and sheet icons");
        }
    }

    // The style the page ticks: the setting, except that a system with no
    // desktop to ask draws the simple icons whatever it says - and the host
    // icons are the default - so the page ticks what is actually drawn
    // rather than the greyed-out choice.
    FilerFileIconStyle ShownFileIconStyle(const DialogState* d) {
        if (!UltraCanvasFilerWidget::AreHostFileIconsAvailable())
            return FilerFileIconStyle::Simple;
        return d->settings->fileIconStyle;
    }

    // ===== DISPLAY > TAB STYLE =====

    std::string TabStripStyleDescription(FilerTabStripStyle style) {
        switch (style) {
            case FilerTabStripStyle::SimpleModern:
                return Tr("Simple modern - flat tabs, with a blue line under the "
                          "open one");
            case FilerTabStripStyle::Classic:
                return Tr("Classic - rounded tabs joined to the page below, as "
                          "UltraFiler drew them before 1.71.0");
            default:
                return Tr("Modern - each tab a capsule floating in the strip: the "
                          "open one white with a blue outline, the others plain "
                          "text until the pointer is over them");
        }
    }

    std::shared_ptr<UltraCanvasContainer> BuildTabStylePage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-tab-style", Tr("Tab style"),
                Tr("How the window's tab strip draws its tabs:"));

        d->tabStyleRadios.clear();
        for (FilerTabStripStyle style : {FilerTabStripStyle::Modern,
                                         FilerTabStripStyle::SimpleModern,
                                         FilerTabStripStyle::Classic}) {
            std::string id = std::string("ufl-set-tab-style-") +
                    (style == FilerTabStripStyle::Modern ? "modern" :
                     style == FilerTabStripStyle::SimpleModern ? "simple-modern" : "classic");
            auto radio = MakeChoice(id, TabStripStyleDescription(style),
                                    d->settings && d->settings->tabStripStyle == style);
            d->tabStyleGroup.AddRadioButton(radio);
            d->tabStyleRadios.emplace_back(style, radio);
            parts.body->AddChild(radio);
        }
        d->tabStyleGroup.onSelectionChanged =
                [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->settings) return;
            for (const auto& [style, radio] : d->tabStyleRadios) {
                if (radio != selected) continue;
                d->settings->tabStripStyle = style;
                ApplyAndSave(d);
                return;
            }
        };

        AddNote(parts, "ufl-set-tab-style-note1",
                Tr("The change shows at once on the open window's tab strip. "
                   "Whichever style is chosen, the tabs behave the same: click "
                   "to switch, the X or a middle click to close, drag to reorder, "
                   "and the + at the end opens another tab."));
        return parts.page;
    }

    std::shared_ptr<UltraCanvasContainer> BuildFileIconsPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-file-icons", Tr("File icons"),
                Tr("Whose icons the file display draws for a file that shows no "
                   "picture of its own:"));

        const bool hostAvailable =
                UltraCanvasFilerWidget::AreHostFileIconsAvailable();

        d->fileIconRadios.clear();
        for (FilerFileIconStyle style :
             UltraCanvasFilerWidget::AllFileIconStyles()) {
            auto radio = MakeChoice(
                    std::string("ufl-set-file-icons-") +
                            (style == FilerFileIconStyle::HostOperatingSystem
                                     ? "host" : "simple"),
                    FileIconStyleDescription(style),
                    ShownFileIconStyle(d) == style);
            // Nothing to take icons from on this system: the choice is shown
            // so the page still says what the setting is, and greyed so it
            // does not promise a display it cannot produce.
            if (!hostAvailable &&
                style == FilerFileIconStyle::HostOperatingSystem)
                radio->SetDisabled(true);
            d->fileIconGroup.AddRadioButton(radio);
            d->fileIconRadios.emplace_back(style, radio);
            parts.body->AddChild(radio);
        }
        d->fileIconGroup.onSelectionChanged =
                [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->settings) return;
            for (const auto& [style, radio] : d->fileIconRadios) {
                if (radio != selected) continue;
                d->settings->fileIconStyle = style;
                ApplyAndSave(d);
                return;
            }
        };

        if (!hostAvailable) {
            parts.body->AddChild(MakeText("ufl-set-file-icons-unsupported",
                    Tr("This system has no desktop to take icons from, so the "
                       "simple icons are all there is to draw here."),
                    kTextWidth, kTextFontSize, kNoteTextColor));
        }

        AddNote(parts, "ufl-set-file-icons-note1",
                Tr("Host icons come from the shell on Windows, from Finder on "
                   "macOS and from the installed icon theme on Linux and BSD, so "
                   "a folder listing looks like the rest of the desktop rather "
                   "than like a second file manager."));
        AddNote(parts, "ufl-set-file-icons-note2",
                Tr("Either way, a file that shows a thumbnail of its own content "
                   "keeps showing it, and a program or shortcut keeps the icon "
                   "it carries inside itself - those are the file's own picture, "
                   "not its type's."));
        AddNote(parts, "ufl-set-file-icons-note3",
                Tr("A type this system has no icon for keeps the simple one, and "
                   "so does every icon until its lookup lands: the display never "
                   "waits for the host and never shows an empty box. With host "
                   "icons on, a folder is drawn with the system's folder icon, "
                   "so the pictures inside it (Display > Thumbnails) no longer "
                   "peek out of it."));
        return parts.page;
    }

    // ===== DISPLAY > FILES IN USE =====
    std::shared_ptr<UltraCanvasContainer> BuildFilesInUsePage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-inuse", Tr("Files in use"),
                Tr("Whether the file display marks files another program is "
                   "holding open:"));

        d->lockMarkingBox = MakeCheckbox("ufl-set-inuse-mark",
                Tr("Mark files that are in use"), kTextWidth,
                d->settings->showLockState, [d](bool on) {
            if (!d->settings) return;
            d->settings->showLockState = on;
            ApplyAndSave(d);
        });
        parts.body->AddChild(d->lockMarkingBox);

        if (!FileLockProbeAvailable()) {
            parts.body->AddChild(MakeText("ufl-set-inuse-unsupported",
                    Tr("This system cannot be asked which program holds a file, "
                       "so nothing is marked here."),
                    kTextWidth, kTextFontSize, kNoteTextColor));
            d->lockMarkingBox->SetDisabled(true);
        }

        AddNote(parts, "ufl-set-inuse-note1",
                Tr("A held file wears a padlock on its icon and an X among its "
                   "attributes, and the info bar says so - which is the answer "
                   "to a copy, a rename or a delete that fails with \"the file "
                   "is open in another program\". The Attributes dialog names "
                   "that program."));
        AddNote(parts, "ufl-set-inuse-note2",
                Tr("A file merely open elsewhere - which on this kind of system "
                   "blocks nothing - is marked O instead, and wears no padlock."));
        AddNote(parts, "ufl-set-inuse-note3",
                Tr("Off, nothing is asked: each shown file costs one open, which "
                   "is worth avoiding on a slow network volume."));
        return parts.page;
    }

    // ===== DISPLAY > PATH BAR =====
    std::shared_ptr<UltraCanvasContainer> BuildPathBarPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-pathbar", Tr("Path bar"),
                Tr("How the path bar above the file display names a drive:"));

        d->pathBarVolumeNamesBox = MakeCheckbox("ufl-set-pathbar-volume-names",
                Tr("Show the drive's volume name - \"My Passport Ultra (D:)\" "
                   "instead of \"D:\""), kTextWidth,
                d->settings->pathBarVolumeNames, [d](bool on) {
            if (!d->settings) return;
            d->settings->pathBarVolumeNames = on;
            ApplyAndSave(d);
        });
        parts.body->AddChild(d->pathBarVolumeNamesBox);

        AddNote(parts, "ufl-set-pathbar-note1",
                Tr("The same name the folder tree and the Computer page show, "
                   "also in the list of drives under the path bar's Computer "
                   "entry. A drive whose volume has no name, and a network "
                   "drive, keep the letter alone."));
        AddNote(parts, "ufl-set-pathbar-note2",
                Tr("Off, the path bar is shorter: the letter alone, which "
                   "leaves more room for the folders of a deep path."));
        return parts.page;
    }

    // ===== DISPLAY > FILES =====
    std::shared_ptr<UltraCanvasContainer> BuildFilesPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-files", Tr("Files"),
                Tr("Which files the file display lists:"));

        d->hiddenFilesBox = MakeCheckbox("ufl-set-files-hidden",
                Tr("Show hidden files"), kTextWidth,
                d->settings->showHiddenFiles, [d](bool on) {
            if (!d->settings) return;
            d->settings->showHiddenFiles = on;
            ApplyAndSave(d);
        });
        parts.body->AddChild(d->hiddenFilesBox);

        AddNote(parts, "ufl-set-files-note1",
                Tr("Hidden is what the system calls hidden: a name starting with "
                   "a dot everywhere, plus the hidden attribute on Windows (which "
                   "is what keeps NTUSER.DAT and the profile junctions out of "
                   "sight) and the hidden flag on macOS."));
        AddNote(parts, "ufl-set-files-note2",
                Tr("Showing them also shows the Home folder whole, whatever "
                   "Display > Home folder says - the switch means \"show me "
                   "everything\"."));
        AddNote(parts, "ufl-set-files-note3",
                Tr("This is what every folder display starts with. A single "
                   "display can still be switched on its own - its Display > "
                   "Hidden files menu entry, or the \"Show hidden files\" button "
                   "of the strip the Home folder shows while it is holding "
                   "something back - without changing the setting here."));
        AddNote(parts, "ufl-set-files-note4",
                Tr("The folder tree leaves hidden folders out either way."));
        AddNote(parts, "ufl-set-files-note5",
                Tr("A file the system does not call hidden - Sti_Trace.log, "
                   "Thumbs.db, desktop.ini - is not covered by this switch. "
                   "Display > Ignored files is what leaves those out."));
        return parts.page;
    }

    // ===== DISPLAY > IGNORED FILES =====
    // Keeps the built-in pattern rows in step with the master switch: off,
    // they are all disabled rather than hidden, so the list still says what
    // the switch covers.
    void SyncClutterRows(DialogState* d) {
        if (!d->settings) return;
        const bool on = d->settings->ignoreClutterFiles;
        for (auto& [pattern, box] : d->clutterRows) {
            if (!box) continue;
            box->SetDisabled(!on);
            box->RequestRedraw();
        }
    }

    std::shared_ptr<UltraCanvasContainer> BuildIgnoredFilesPage(DialogState* d) {
        // A scrolling page: the built-in list plus the two blocks under it
        // are taller than the window, and that also puts the notes under the
        // caption, where they are read before the list rather than after it.
        PageParts parts = MakePage("ufl-set-page-ignored", Tr("Ignored files"),
                Tr("Names the file display never lists:"), true);

        // ----- the built-in list -----
        d->clutterBox = MakeCheckbox("ufl-set-ign-builtin",
                Tr("Hide known clutter files"), kTextWidth,
                d->settings->ignoreClutterFiles, [d](bool on) {
            if (!d->settings) return;
            d->settings->ignoreClutterFiles = on;
            SyncClutterRows(d);
            ApplyAndSave(d);
        });
        parts.body->AddChild(d->clutterBox);

        d->clutterRows.clear();
        int index = 0;
        for (const std::string& pattern :
             UltraFilerSettings::BuiltInClutterPatterns()) {
            auto& off = d->settings->disabledClutterPatterns;
            const bool ticked =
                    std::find(off.begin(), off.end(), pattern) == off.end();
            auto box = MakeCheckbox("ufl-set-ign-p" + std::to_string(index++),
                    pattern, kTextWidth - 20, ticked, [d, pattern](bool on) {
                if (!d->settings) return;
                // Persisted as what is switched OFF, so a pattern a later
                // release adds to the list starts on rather than absent.
                auto& list = d->settings->disabledClutterPatterns;
                auto at = std::find(list.begin(), list.end(), pattern);
                if (on && at != list.end()) list.erase(at);
                else if (!on && at == list.end()) list.push_back(pattern);
                ApplyAndSave(d);
            });
            box->SetMargin(0, 0, 0, 20);   // indented under the switch above
            d->clutterRows.emplace_back(pattern, box);
            parts.body->AddChild(box);
        }
        SyncClutterRows(d);

        // ----- the user's own patterns -----
        AddBodyCaption(parts, "ufl-set-ign-own-caption",
                Tr("Own patterns - type one and press Enter:"));
        d->ignorePatternsInput = CreateTagInput("ufl-set-ign-own", -1, -1,
                                                static_cast<float>(kTextWidth));
        d->ignorePatternsInput->SetPlaceholder(Tr("e.g. *.bak"));
        d->ignorePatternsInput->SetTags(d->settings->ignoredNamePatterns);
        d->ignorePatternsInput->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        d->ignorePatternsInput->size.width = CSSLayout::Dimension::Px(kTextWidth);
        d->ignorePatternsInput->onTagsChanged =
                [d](const std::vector<std::string>& patterns) {
            if (!d->settings) return;
            d->settings->ignoredNamePatterns = patterns;
            ApplyAndSave(d);
        };
        parts.body->AddChild(d->ignorePatternsInput);

        // ----- where they apply -----
        AddBodyCaption(parts, "ufl-set-ign-scope-caption", Tr("Apply them:"));
        const bool homeOnly = d->settings->ignoreOnlyInHomeFolder;
        d->ignoreHomeRadio = MakeChoice("ufl-set-ign-home",
                Tr("Only in the Home folder"), homeOnly);
        d->ignoreEverywhereRadio = MakeChoice("ufl-set-ign-all",
                Tr("In every folder"), !homeOnly);
        d->ignoreScopeGroup.AddRadioButton(d->ignoreHomeRadio);
        d->ignoreScopeGroup.AddRadioButton(d->ignoreEverywhereRadio);
        d->ignoreScopeGroup.onSelectionChanged =
                [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->settings) return;
            d->settings->ignoreOnlyInHomeFolder = (selected == d->ignoreHomeRadio);
            ApplyAndSave(d);
        };
        parts.body->AddChild(d->ignoreHomeRadio);
        parts.body->AddChild(d->ignoreEverywhereRadio);

        d->resets[kPageIgnoredFiles] = PageReset{Tr("Restore the built-in list"), 180,
                [d]() {
            if (!d->settings) return;
            // The built-in list only - the patterns the user typed are theirs
            // to remove.
            d->settings->ignoreClutterFiles = true;
            d->settings->disabledClutterPatterns.clear();
            if (d->clutterBox) d->clutterBox->SetChecked(true);
            for (auto& [pattern, box] : d->clutterRows)
                if (box) box->SetChecked(true);
            SyncClutterRows(d);
            ApplyAndSave(d);
        }};

        AddNote(parts, "ufl-set-ign-note1",
                Tr("A pattern is matched against the name, ignoring case; \"*\" "
                   "stands for any run of characters and \"?\" for one, so "
                   "\"*.bak\" hides every backup file. Folders are matched the "
                   "same way."));
        AddNote(parts, "ufl-set-ign-note2",
                Tr("Sti_Trace.log is the trace file the Windows Still Image "
                   "service writes into the profile whenever a program talks to "
                   "a scanner or camera. It carries no hidden attribute, so "
                   "Display > Files cannot reach it - this is where it goes."));
        AddNote(parts, "ufl-set-ign-note3",
                Tr("Nothing is moved or deleted: an ignored file is only left "
                   "out of the display. Display > Files > Show hidden files - and "
                   "the \"Show hidden files\" button on the strip - bring it "
                   "back, a search still finds it, and a path typed into the "
                   "address bar still opens it."));
        AddNote(parts, "ufl-set-ign-note4",
                Tr("While anything is ignored, the display says so along its "
                   "foot, so nothing disappears silently - in every folder, not "
                   "just the Home folder."));
        return parts.page;
    }

    // ===== DISPLAY > HOME FOLDER =====
    std::shared_ptr<UltraCanvasContainer> BuildHomeFolderPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-home", Tr("Home folder"),
                Tr("What the Home folder shows, in the folder tree and in the "
                   "file display:"));

        const bool predefined = d->settings->homeShowPredefinedOnly;

        d->homeAllRadio = MakeChoice("ufl-set-home-all",
                Tr("Show all content"), !predefined);
        d->homePredefinedRadio = MakeChoice("ufl-set-home-predefined",
                Tr("Show only predefined folders"), predefined);
        d->homeContentGroup.AddRadioButton(d->homeAllRadio);
        d->homeContentGroup.AddRadioButton(d->homePredefinedRadio);
        d->homeContentGroup.onSelectionChanged =
                [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->settings) return;
            d->settings->homeShowPredefinedOnly =
                    (selected == d->homePredefinedRadio);
            ApplyAndSave(d);
        };
        parts.body->AddChild(d->homeAllRadio);
        parts.body->AddChild(d->homePredefinedRadio);

        AddNote(parts, "ufl-set-home-note1",
                Tr("Predefined folders: Desktop, Documents, Downloads, Music, "
                   "Pictures, Videos - resolved through the platform, so a "
                   "redirected or localized folder counts."));
        AddNote(parts, "ufl-set-home-note2",
                Tr("Display > Files > Show hidden files - and the file display's "
                   "own Display > Hidden files entry - always reveal everything. "
                   "While the Home folder is holding something back, it says so "
                   "along its foot, with a button that shows it and a gear that "
                   "opens this page."));
        return parts.page;
    }

    // ===== HANDLING > DRAG & DROP =====
    std::shared_ptr<UltraCanvasContainer> BuildDragDropPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-dragdrop", Tr("Drag & Drop"),
                Tr("Drop on folder - what dragging files onto a folder of the "
                   "file display does:"));

        const bool copies = d->settings->dropOnFolderCopies;

        d->dropMoveRadio = MakeChoice("ufl-set-dd-move", Tr("Move files"), !copies);
        d->dropCopyRadio = MakeChoice("ufl-set-dd-copy", Tr("Copy files"), copies);
        d->dropOnFolderGroup.AddRadioButton(d->dropMoveRadio);
        d->dropOnFolderGroup.AddRadioButton(d->dropCopyRadio);
        d->dropOnFolderGroup.onSelectionChanged =
                [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->settings) return;
            d->settings->dropOnFolderCopies = (selected == d->dropCopyRadio);
            ApplyAndSave(d);
        };
        parts.body->AddChild(d->dropMoveRadio);
        parts.body->AddChild(d->dropCopyRadio);

        AddNote(parts, "ufl-set-dd-note1",
                Tr("Whatever is chosen here, Ctrl while dropping always copies "
                   "and Shift always moves."));
        AddNote(parts, "ufl-set-dd-note2",
                Tr("Whether a drop asks before it is carried out is set under "
                   "Handling > File operations, together with the other questions "
                   "a copy, move or delete asks."));
        return parts.page;
    }

    // ===== HANDLING > FILE OPERATIONS =====

    // A row of radio buttons for one of the page's choices.
    void UpdateProgressDelayLabel(DialogState* d) {
        if (!d->progressDelayValue || !d->settings) return;
        const int s = d->settings->progressDelaySeconds;
        d->progressDelayValue->SetText(s == 0 ? Tr("at once")
                                              : TrN("{n} second", "{n} seconds", s));
    }

    std::shared_ptr<UltraCanvasContainer> BuildFileOperationsPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-fileops", Tr("File operations"),
                Tr("The questions a copy, move or delete asks, and the answers "
                   "that stand for good:"), /*scrolls=*/true);
        UltraFilerSettings* st = d->settings;

        AddBodyCaption(parts, "ufl-set-fo-ask-caption", Tr("Ask before:"));
        auto applyConfirmation = [d]() {
            if (!d->settings || !d->confirmMovesBox || !d->confirmCopiesBox) return;
            d->settings->dropConfirmation = UltraFilerSettings::DropConfirmationFor(
                    d->confirmMovesBox->IsChecked(), d->confirmCopiesBox->IsChecked());
            ApplyAndSave(d);
        };
        d->confirmMovesBox = MakeCheckbox("ufl-set-fo-confirm-moves",
                Tr("Moving files, by drag & drop or with Cut and Paste"), kTextWidth,
                UltraFilerSettings::ConfirmsMoves(st->dropConfirmation),
                [applyConfirmation](bool) { applyConfirmation(); });
        d->confirmCopiesBox = MakeCheckbox("ufl-set-fo-confirm-copies",
                Tr("Copying files by drag & drop"), kTextWidth,
                UltraFilerSettings::ConfirmsCopies(st->dropConfirmation),
                [applyConfirmation](bool) { applyConfirmation(); });
        d->confirmTrashBox = MakeCheckbox("ufl-set-fo-confirm-trash",
#if defined(_WIN32) || defined(_WIN64)
                Tr("Moving files to the Recycle Bin"), kTextWidth,
#else
                Tr("Moving files to the Trash"), kTextWidth,
#endif
                st->confirmTrashDelete, [d](bool on) {
            if (!d->settings) return;
            d->settings->confirmTrashDelete = on;
            ApplyAndSave(d);
        });
        d->confirmPermanentBox = MakeCheckbox("ufl-set-fo-confirm-permanent",
                Tr("Deleting files permanently (always asks)"), kTextWidth, true, nullptr);
        d->confirmPermanentBox->SetDisabled(true);
        parts.body->AddChild(d->confirmMovesBox);
        parts.body->AddChild(d->confirmCopiesBox);
        parts.body->AddChild(d->confirmTrashBox);
        parts.body->AddChild(d->confirmPermanentBox);

        AddBodyCaption(parts, "ufl-set-fo-conflict-caption",
                Tr("When a name is already taken:"));
        {
            auto row = MakeChoiceRow("ufl-set-fo-conflict-row");
            // The id from a fixed key, not from the label: a translated
            // label would give each language a different element id.
            const std::vector<std::tuple<FilerConflictPolicy, const char*, std::string>> choices = {
                {FilerConflictPolicy::Ask,      "ask",      Tr("Ask")},
                {FilerConflictPolicy::KeepBoth, "keepboth", Tr("Keep both")},
                {FilerConflictPolicy::Replace,  "replace",  Tr("Replace")},
                {FilerConflictPolicy::Skip,     "skip",     Tr("Skip")},
            };
            for (const auto& [policy, key, label] : choices) {
                auto radio = MakeChoice("ufl-set-fo-conflict-" + std::string(key), label,
                                        st->conflictPolicy == policy, 100);
                d->conflictRadios.emplace_back(policy, radio);
                d->conflictGroup.AddRadioButton(radio);
                row->AddChild(radio);
            }
            d->conflictGroup.onSelectionChanged =
                    [d](std::shared_ptr<UltraCanvasRadio> selected) {
                if (!selected || !d->settings) return;
                for (const auto& [policy, radio] : d->conflictRadios)
                    if (radio == selected) d->settings->conflictPolicy = policy;
                ApplyAndSave(d);
            };
            parts.body->AddChild(row);
        }

        AddBodyCaption(parts, "ufl-set-fo-folder-caption",
                Tr("When a folder is pasted over a folder:"));
        {
            auto row = MakeChoiceRow("ufl-set-fo-folder-row");
            d->folderMergeRadio = MakeChoice("ufl-set-fo-folder-merge",
                    Tr("Merge into the existing folder"),
                    st->folderConflictPolicy == FilerFolderConflictPolicy::Merge, 230);
            d->folderAskRadio = MakeChoice("ufl-set-fo-folder-ask", Tr("Ask"),
                    st->folderConflictPolicy == FilerFolderConflictPolicy::Ask, 100);
            d->folderConflictGroup.AddRadioButton(d->folderMergeRadio);
            d->folderConflictGroup.AddRadioButton(d->folderAskRadio);
            d->folderConflictGroup.onSelectionChanged =
                    [d](std::shared_ptr<UltraCanvasRadio> selected) {
                if (!selected || !d->settings) return;
                d->settings->folderConflictPolicy = selected == d->folderAskRadio
                        ? FilerFolderConflictPolicy::Ask : FilerFolderConflictPolicy::Merge;
                ApplyAndSave(d);
            };
            row->AddChild(d->folderMergeRadio);
            row->AddChild(d->folderAskRadio);
            parts.body->AddChild(row);
        }

        AddBodyCaption(parts, "ufl-set-fo-failure-caption",
                Tr("When a file cannot be copied, moved or deleted:"));
        {
            auto row = MakeChoiceRow("ufl-set-fo-failure-row");
            d->failureAskRadio = MakeChoice("ufl-set-fo-failure-ask", Tr("Ask each time"),
                    st->problemPolicy == FilerProblemPolicy::Ask, 130);
            d->failureSkipRadio = MakeChoice("ufl-set-fo-failure-skip",
                    Tr("Skip it and report at the end"),
                    st->problemPolicy == FilerProblemPolicy::SkipAndReport, 230);
            d->failureGroup.AddRadioButton(d->failureAskRadio);
            d->failureGroup.AddRadioButton(d->failureSkipRadio);
            d->failureGroup.onSelectionChanged =
                    [d](std::shared_ptr<UltraCanvasRadio> selected) {
                if (!selected || !d->settings) return;
                d->settings->problemPolicy = selected == d->failureSkipRadio
                        ? FilerProblemPolicy::SkipAndReport : FilerProblemPolicy::Ask;
                ApplyAndSave(d);
            };
            row->AddChild(d->failureAskRadio);
            row->AddChild(d->failureSkipRadio);
            parts.body->AddChild(row);
        }

        AddBodyCaption(parts, "ufl-set-fo-progress-caption",
                Tr("Progress window - shown once the operation has been running for:"));
        d->progressDelaySlider = MakeIntSlider("ufl-set-fo-progress-delay",
                UltraFilerSettings::kMinProgressDelaySeconds,
                UltraFilerSettings::kMaxProgressDelaySeconds,
                st->progressDelaySeconds, [d](int seconds) {
            if (!d->settings || d->settings->progressDelaySeconds == seconds) return;
            d->settings->progressDelaySeconds = seconds;
            UpdateProgressDelayLabel(d);
            ApplyAndSave(d);
        });
        d->progressDelayValue = MakeSliderValueLabel("ufl-set-fo-progress-value", 90);
        UpdateProgressDelayLabel(d);
        parts.body->AddChild(MakeSliderRow("ufl-set-fo-progress-row", Tr("Delay"),
                d->progressDelaySlider, d->progressDelayValue, 0, 60));

        AddNote(parts, "ufl-set-fo-note1",
                Tr("Every question offers its answers as buttons - Move or Copy, "
                   "Keep both / Replace / Skip, Move to the Trash or Delete "
                   "permanently - with the safe answer coloured; Return takes it, "
                   "Escape stops. An \"Apply to all\" checkbox beside the buttons "
                   "answers the remaining entries of the same operation at once."));
        AddNote(parts, "ufl-set-fo-note2",
                Tr("A move asks because it changes where the files live; a Ctrl+V "
                   "copy is deliberate and never asks. Moving to the Trash can be "
                   "undone from there, which is why its question can be switched "
                   "off; a permanent delete cannot, and always asks."));
        AddNote(parts, "ufl-set-fo-note3",
                Tr("Merging a folder puts its entries into the folder that is "
                   "there and asks about any name that is taken inside it; "
                   "Replace deletes the existing folder first. Until the progress "
                   "window opens, the pointer shows that the work is under way."));
        return parts.page;
    }

    // ===== HANDLING > OPENING FILES =====
    std::shared_ptr<UltraCanvasContainer> BuildOpeningFilesPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-opening", Tr("Opening files"),
                Tr("Double-click (or Enter) on a file - what it does:"));

        d->mediaUltraViewerBox = MakeCheckbox("ufl-set-open-ultraviewer",
                Tr("Open pictures, audio and video in UltraViewer"), kTextWidth,
                d->settings->mediaOpensInUltraViewer, [d](bool on) {
            if (!d->settings) return;
            d->settings->mediaOpensInUltraViewer = on;
            ApplyAndSave(d);
        });
        parts.body->AddChild(d->mediaUltraViewerBox);

        AddBodyCaption(parts, "ufl-set-open-other-caption",
                Tr("Any other file, when this system has a program registered for "
                   "that kind of file:"));

        const bool app = d->settings->doubleClickOpensRegisteredApp;

        d->openAppRadio = MakeChoice("ufl-set-open-app",
                Tr("Start the registered program"), app);
        d->openPreviewRadio = MakeChoice("ufl-set-open-preview",
                Tr("Show it in the preview"), !app);
        d->openActivationGroup.AddRadioButton(d->openAppRadio);
        d->openActivationGroup.AddRadioButton(d->openPreviewRadio);
        d->openActivationGroup.onSelectionChanged =
                [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->settings) return;
            d->settings->doubleClickOpensRegisteredApp =
                    (selected == d->openAppRadio);
            ApplyAndSave(d);
        };
        parts.body->AddChild(d->openAppRadio);
        parts.body->AddChild(d->openPreviewRadio);

        AddNote(parts, "ufl-set-open-note0",
                Tr("UltraViewer shows the picture, audio file or video full size, "
                   "with the rest of its folder a key press away. When UltraViewer "
                   "is not installed, or this is switched off, such a file is "
                   "handled like any other, by the choice below it."));
        AddNote(parts, "ufl-set-open-note1",
                Tr("\"Start the registered program\" is what a double-click does "
                   "in Explorer and the Finder: the file opens in the program its "
                   "type is assigned to, and UltraFiler stays as it is."));
        AddNote(parts, "ufl-set-open-note2",
                Tr("\"Show it in the preview\" keeps the file inside UltraFiler "
                   "whenever it can show it - pictures, documents, spreadsheets, "
                   "3D models, e-books, fonts, video, audio and text - which is "
                   "faster than starting a program for a look at a file."));
        AddNote(parts, "ufl-set-open-note3",
                Tr("A file this system has no program for is shown in the preview "
                   "either way, so this never turns a double-click into nothing "
                   "happening. Whichever is chosen, the context menu's \"Open "
                   "with\" still starts a program, and a file that cannot be "
                   "previewed - a program, an installer, a file type UltraFiler "
                   "does not read - always goes to the system."));
        return parts.page;
    }

    // ===== HANDLING > PROTECTED FILES =====

    // "10 min" beside the slider, "Never" at its zero.
    void UpdateForgetPasswordsLabel(DialogState* d) {
        if (!d->settings || !d->forgetPasswordsValue) return;
        const int minutes = d->settings->forgetPasswordsAfterMinutes;
        d->forgetPasswordsValue->SetText(minutes == 0 ? std::string(Tr("Never"))
                                                      : TrF("{0} min", minutes));
        d->forgetPasswordsValue->RequestRedraw();
    }

    std::shared_ptr<UltraCanvasContainer> BuildProtectedFilesPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-protected", Tr("Protected files"),
                Tr("How long UltraFiler may be left alone before the preview "
                   "forgets the passwords its Unlock was given:"));

        d->forgetPasswordsValue = MakeSliderValueLabel("ufl-set-protected-value", 60);
        d->forgetPasswordsSlider = MakeIntSlider("ufl-set-protected-minutes",
                0, UltraFilerSettings::kMaxForgetPasswordsMinutes,
                d->settings->forgetPasswordsAfterMinutes, [d](int minutes) {
            if (!d->settings || d->settings->forgetPasswordsAfterMinutes == minutes) return;
            d->settings->forgetPasswordsAfterMinutes = minutes;
            UpdateForgetPasswordsLabel(d);
            ApplyAndSave(d);
        });
        parts.body->AddChild(MakeSliderRow("ufl-set-protected-row",
                Tr("Forget passwords after:"), d->forgetPasswordsSlider,
                d->forgetPasswordsValue, 0, 150));

        AddNote(parts, "ufl-set-protected-note1",
                Tr("A password-protected PDF or UltraCanvas image in the preview "
                   "is opened with Unlock, and its password is kept while the "
                   "folder is browsed, so going back to it - or on to another file "
                   "there with the same password - asks nothing."));
        AddNote(parts, "ufl-set-protected-note2",
                Tr("Once UltraFiler has had no key press, click or mouse movement "
                   "for this long, the passwords are forgotten and a file shown "
                   "unlocked goes back behind Unlock. Locking the screen does the "
                   "same at once, whatever is set here; Never leaves only that."));

        d->resets[kPageProtectedFiles] = PageReset{Tr("Restore default"), 120, [d]() {
            if (!d->settings) return;
            if (d->forgetPasswordsSlider)
                d->forgetPasswordsSlider->SetValue(static_cast<float>(
                        UltraFilerSettings::kDefaultForgetPasswordsMinutes));
            // The slider reports only a value it moved to.
            d->settings->forgetPasswordsAfterMinutes =
                    UltraFilerSettings::kDefaultForgetPasswordsMinutes;
            UpdateForgetPasswordsLabel(d);
            ApplyAndSave(d);
        }};

        UpdateForgetPasswordsLabel(d);
        return parts.page;
    }

    // ===== DISPLAY > NEW TAB =====
    std::shared_ptr<UltraCanvasContainer> BuildNewTabPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-new-tab", Tr("New tab"),
                Tr("What the \"+\" at the end of the tab strip opens:"));

        AddBodyCaption(parts, "ufl-set-tab-options-caption", Tr("Display options"));

        const FilerNewTabStart start = d->settings->newTabStart;
        d->newTabCurrentRadio = MakeChoice("ufl-set-tab-current",
                Tr("Show current folder"), start == FilerNewTabStart::CurrentFolder);
        d->newTabHomeRadio = MakeChoice("ufl-set-tab-home",
                Tr("Show Home folder"), start == FilerNewTabStart::Home);
        d->newTabComputerRadio = MakeChoice("ufl-set-tab-computer",
                Tr("Show Computer"), start == FilerNewTabStart::Computer);
        d->newTabGroup.AddRadioButton(d->newTabCurrentRadio);
        d->newTabGroup.AddRadioButton(d->newTabHomeRadio);
        d->newTabGroup.AddRadioButton(d->newTabComputerRadio);
        d->newTabGroup.onSelectionChanged =
                [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->settings) return;
            d->settings->newTabStart =
                    selected == d->newTabHomeRadio     ? FilerNewTabStart::Home
                  : selected == d->newTabComputerRadio ? FilerNewTabStart::Computer
                                                       : FilerNewTabStart::CurrentFolder;
            ApplyAndSave(d);
        };
        parts.body->AddChild(d->newTabCurrentRadio);
        parts.body->AddChild(d->newTabHomeRadio);
        parts.body->AddChild(d->newTabComputerRadio);

        AddNote(parts, "ufl-set-tab-note1",
                Tr("The current folder opens the tab on the folder the active tab "
                   "is showing, so the work carries on where it is; the Home "
                   "folder opens every new tab on the same starting point "
                   "instead. Computer opens the Computer page - the drives with "
                   "their free space, Home and the cloud folders - with the Home "
                   "folder behind it, so a tab opened that way still has a folder "
                   "to show once a drive or folder is picked."));
        AddNote(parts, "ufl-set-tab-note2",
                Tr("Only the \"+\" follows this. A tab opened on a named folder - "
                   "the containing folder of a search result, an entry of the "
                   "History or Favorites view - still opens on that folder."));
        return parts.page;
    }

    // ===== EXTRAS > OPEN PROMPT =====

    // Describes what "Extras > Open prompt" will start right now: the saved
    // application, or the platform default while nothing is saved.
    void UpdatePromptStatus(DialogState* d) {
        if (!d->promptStatus || !d->settings) return;
        const std::string& saved = d->settings->promptApplication;
        if (!saved.empty()) {
            d->promptStatus->SetText(TrF("Saved application: {0}", saved));
        } else {
            const std::string detected = UltraFilerPrompt::DetectSystemPrompt();
            d->promptStatus->SetText(detected.empty()
                    ? Tr("No command line program was found on this system - "
                         "choose one above.")
                    : TrF("System default in use: {0}", detected));
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
                    PathFromUtf8(d->settings->promptApplication).parent_path();
            if (!parent.empty() && std::filesystem::is_directory(parent, ec) && !ec)
                initialDir = PathToUtf8(parent);
        }

        FileDialogOptions opts;
        opts.SetTitle(Tr("Select the command line application"))
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
        if (!showsEverything) opts.AddFilter(Tr("All files"), "*");

        UltraCanvasFileLoader::OpenFileDialog(opts,
                [d](DialogResult result, const std::string& path) {
            if (result != DialogResult::OK || path.empty()) return;
            if (!d->promptInput) return;
            d->promptInput->SetText(path);
            d->promptInput->RequestRedraw();
            if (d->promptStatus) {
                d->promptStatus->SetText(TrF("Selected: {0} - press \"Save app\" to keep it.", path));
                d->promptStatus->RequestRedraw();
            }
        });
    }

    std::shared_ptr<UltraCanvasContainer> BuildOpenPromptPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-prompt", Tr("Open prompt"),
                Tr("Command line application started by Extras > Open prompt. It "
                   "opens in the folder of the active tab:"));

        // ----- application path + browse -----
        auto pathRow = std::make_shared<UltraCanvasContainer>("ufl-set-op-row");
        pathRow->layout.SetFlexRow().SetFlexGap(6)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        pathRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        pathRow->size.width  = CSSLayout::Dimension::Px(kTextWidth);
        pathRow->size.height = CSSLayout::Dimension::Px(32);

        d->promptInput = CreateTextInput("ufl-set-op-path", 0, 0, 400, 26);
        d->promptInput->SetFontSize(kTextFontSize);
        d->promptInput->SetPlaceholder(Tr("Path to the application"));
        d->promptInput->SetText(d->settings ? d->settings->promptApplication
                                            : std::string());
        // Enter in the field saves, like the button next to it.
        d->promptInput->onEnterPressed = [d](const std::string&) {
            SavePromptApplication(d);
            return true;
        };
        d->promptInput->size.width  = CSSLayout::Dimension::Px(400);
        d->promptInput->size.height = CSSLayout::Dimension::Px(26);
        d->promptInput->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        pathRow->AddChild(d->promptInput);

        pathRow->AddChild(MakeButton("ufl-set-op-browse", std::string(), 32,
                [d]() { BrowseForPromptApplication(d); }, "folder-open.svg"));
        parts.body->AddChild(pathRow);

        // ----- save / reset -----
        auto buttonRow = MakeButtonRow("ufl-set-op-buttons");
        buttonRow->AddChild(MakeButton("ufl-set-op-save", Tr("Save app"), 100,
                [d]() { SavePromptApplication(d); }));
        buttonRow->AddChild(MakeButton("ufl-set-op-default", Tr("Use system default"), 150,
                [d]() {
            if (d->promptInput) d->promptInput->SetText("");
            SavePromptApplication(d);
        }));
        buttonRow->AddChild(MakeButton("ufl-set-op-test", Tr("Test"), 70, [d]() {
            std::string error;
            const std::string application = d->promptInput
                    ? Trim(d->promptInput->GetText(), " \t\"") : std::string();
            if (!UltraFilerPrompt::Launch(application, "", error))
                UltraCanvasAlert::Error(error, Tr("Open prompt"), nullptr,
                                        d->window.get());
        }));
        parts.body->AddChild(buttonRow);

        // What will be started - feedback, so it stays with the controls.
        d->promptStatus = MakeText("ufl-set-op-status", std::string(), kTextWidth,
                                   kTextFontSize, kNoteTextColor);
        parts.body->AddChild(d->promptStatus);
        UpdatePromptStatus(d);

        AddNote(parts, "ufl-set-op-note1",
                Tr("Leave the field empty to use the command line program of "
                   "this operating system."));
        AddNote(parts, "ufl-set-op-note2",
                Tr("The folder button opens the file dialog filtered to "
                   "applications. The choice only lands in the field - Save app "
                   "(or Enter in the field) is what keeps it, and Test starts it "
                   "right away."));
        return parts.page;
    }

    // ===== HISTORY & FAVORITES =====

    // "300 entries" beside the limit slider. Always plural: the smallest the
    // slider offers is kMinHistoryEntries, well above one.
    void UpdateHistoryLimitLabel(DialogState* d) {
        if (!d->settings || !d->historyLimitValue) return;
        d->historyLimitValue->SetText(
                TrN("{n} entry", "{n} entries", d->settings->historyMaxEntries));
        d->historyLimitValue->RequestRedraw();
    }

    std::shared_ptr<UltraCanvasContainer> BuildListsPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-lists", Tr("History & Favorites"),
                Tr("How much the History view remembers, and clearing the lists "
                   "UltraFiler keeps:"));

        // ----- History: limit of entries -----
        d->historyLimitValue = MakeSliderValueLabel("ufl-set-hf-limit-value", 80);
        d->historyLimitSlider = MakeIntSlider("ufl-set-hf-limit",
                UltraFilerSettings::kMinHistoryEntries,
                UltraFilerSettings::kMaxHistoryEntries,
                d->settings ? d->settings->historyMaxEntries
                            : UltraFilerSettings::kDefaultHistoryEntries,
                [d](int entries) {
            if (!d->settings || d->settings->historyMaxEntries == entries) return;
            d->settings->historyMaxEntries = entries;
            UpdateHistoryLimitLabel(d);
            // Applied straight away, like every other setting here: a lowered
            // limit drops what is past it and rewrites history.txt now,
            // rather than at the next restart.
            ApplyAndSave(d);
        });
        parts.body->AddChild(MakeSliderRow("ufl-set-hf-limit-row",
                Tr("Limit of entries:"), d->historyLimitSlider,
                d->historyLimitValue, 0, 110));
        UpdateHistoryLimitLabel(d);

        auto buttonRow = MakeButtonRow("ufl-set-hf-buttons");

        auto clearHistory = MakeButton("ufl-set-hf-clear-history", Tr("Clear History"),
                120, [d]() {
            if (d->onClearHistory) d->onClearHistory();
            if (d->listsStatus) {
                d->listsStatus->SetText(Tr("History cleared."));
                d->listsStatus->RequestRedraw();
            }
        });
        clearHistory->SetDisabled(!d->onClearHistory);
        buttonRow->AddChild(clearHistory);

        auto clearFavorites = MakeButton("ufl-set-hf-clear-favorites",
                Tr("Clear Favorites"), 120, [d]() {
            if (d->onClearFavorites) d->onClearFavorites();
            if (d->listsStatus) {
                d->listsStatus->SetText(
                        Tr("Favorites cleared (including the tree's Pinned section)."));
                d->listsStatus->RequestRedraw();
            }
        });
        clearFavorites->SetDisabled(!d->onClearFavorites);
        buttonRow->AddChild(clearFavorites);

        auto clearViews = MakeButton("ufl-set-hf-clear-views",
                Tr("Clear Folder views"), 140, [d]() {
            if (d->onClearFolderViews) d->onClearFolderViews();
            if (d->listsStatus) {
                d->listsStatus->SetText(
                        Tr("Folder views cleared - every folder opens with the "
                        "current view again."));
                d->listsStatus->RequestRedraw();
            }
        });
        clearViews->SetDisabled(!d->onClearFolderViews);
        buttonRow->AddChild(clearViews);
        parts.body->AddChild(buttonRow);

        // What was just cleared - feedback, so it stays with the buttons.
        d->listsStatus = MakeText("ufl-set-hf-status", std::string(), kTextWidth,
                                  kTextFontSize, kNoteTextColor);
        parts.body->AddChild(d->listsStatus);

        AddNote(parts, "ufl-set-hf-note1",
                Tr("The History view lists the recently used files, folders and "
                   "applications; the Favorites view lists the pinned ones, "
                   "including the tree's Pinned section."));
        AddNote(parts, "ufl-set-hf-note2",
                Tr("The limit counts per section: Files, Folders and Apps each "
                   "keep that many entries, so a day of opening documents cannot "
                   "push the remembered applications out. What is past the limit "
                   "is the oldest, and lowering it forgets those entries for "
                   "good. The lists survive restarts - they are kept next to the "
                   "settings as history.txt."));
        AddNote(parts, "ufl-set-hf-note3",
                Tr("Folder views are the view type and sort order each folder "
                   "was last looked at with. Cleared, every folder opens with "
                   "the current view again."));

        d->resets[kPageLists] = PageReset{Tr("Restore default limit"), 170, [d]() {
            if (!d->settings) return;
            d->settings->historyMaxEntries =
                    UltraFilerSettings::kDefaultHistoryEntries;
            // The slider only reports a value it actually moved to, so the
            // setting is written above rather than left to the callback.
            if (d->historyLimitSlider)
                d->historyLimitSlider->SetValue(static_cast<float>(
                        UltraFilerSettings::kDefaultHistoryEntries));
            UpdateHistoryLimitLabel(d);
            ApplyAndSave(d);
        }};
        return parts.page;
    }

    // ===== EXTRAS > CACHE =====
    // What UltraFiler is holding so a folder it has already looked at opens
    // instantly, and the two switches that decide whether it holds it at all.
    //
    // The ceilings are not written here: they come from
    // GetThumbnailCacheStats(), which reports them beside what is used, so
    // this page cannot drift out of step with the widget the way a second
    // copy of "96 MB" would.

    // A size in the unit a person reads it in. 0 is "nothing", not "0 bytes".
    std::string FormatBytes(uint64_t bytes) {
        if (bytes == 0) return "nothing";
        const char* units[] = { "bytes", "KB", "MB", "GB" };
        double value = static_cast<double>(bytes);
        size_t unit = 0;
        while (value >= 1024.0 && unit + 1 < 4) { value /= 1024.0; ++unit; }
        char buffer[64];
        // One decimal from MB up, because "1.4 MB" says more than "1 MB" -
        // but not on a round number: a ceiling of exactly 96 MB should read
        // as "96 MB", not as "96.0 MB".
        const bool round = value == static_cast<double>(static_cast<long long>(value));
        std::snprintf(buffer, sizeof buffer,
                      (unit >= 2 && !round) ? "%.1f %s" : "%.0f %s",
                      value, units[unit]);
        return buffer;
    }

    // "231 thumbnails, 12.4 MB of 96 MB" - how many, and what that leaves of
    // what it may use. Nothing held says so in words: "0 thumbnails, nothing
    // of 96 MB" is three facts where one will do.
    // `count` is the number of entries in words, in the language's plural
    // form ("3 thumbnails"), from the caller's TrN.
    std::string FormatUsage(uint64_t used, uint64_t budget, size_t entries,
                            const std::string& count) {
        // "nothing of 96 MB" read as a riddle; say what it is: what is held
        // now, and the most it may hold.
        if (entries == 0) return TrF("empty (room for {0})", FormatBytes(budget));
        return TrF("{0}, {1} (room for {2})", count, FormatBytes(used), FormatBytes(budget));
    }

    void SetLine(const std::shared_ptr<UltraCanvasLabel>& label,
                 const std::string& text) {
        if (!label) return;
        label->SetText(text);
        label->RequestRedraw();
    }

    // Re-read every figure on the page. Called when the page is shown, after
    // either switch moves and after the cache is emptied - the numbers are
    // live, and a settings page that showed what was true when the window
    // opened would be wrong by the time anyone read it.
    void RefreshCacheUsage(DialogState* d) {
        const DiskCache::Usage disk = ThumbnailDiskCache::GetUsage();
        const std::string directory = ThumbnailDiskCache::Directory();

        // Empty is the only "cannot": switched off is a choice, and the files
        // are still there, still counted, still expiring.
        if (directory.empty()) {
            SetLine(d->cacheDiskLine,
                    Tr("On disk: unavailable - this system offers nowhere to "
                       "write a cache."));
            SetLine(d->cacheLocationLine, "");
        } else {
            // No ceiling on the disk half: it is bounded by the two-week
            // expiry rather than by a byte budget, so what it is holding is
            // the whole answer.
            const std::string held = disk.files == 0
                    ? Tr("nothing yet")
                    : TrN("{n} thumbnail", "{n} thumbnails", static_cast<long long>(disk.files))
                      + ", " + FormatBytes(disk.bytes);
            SetLine(d->cacheDiskLine,
                    d->settings && d->settings->thumbnailDiskCache
                            ? TrF("On disk: {0}", held)
                            : TrF("On disk: {0} (switched off - left to expire)", held));
            SetLine(d->cacheLocationLine, directory);
        }

        if (!d->cacheHooks.memoryStats) {
            SetLine(d->cacheMemoryLine, Tr("In memory: not available."));
            SetLine(d->cacheIconLine, "");
            SetLine(d->cacheHotLine, "");
            SetLine(d->cacheMissingLine, "");
            return;
        }
        const UltraCanvasFilerWidget::ThumbCacheStats memory =
                d->cacheHooks.memoryStats();
        // The icons are counted separately because they have their own
        // budget: they are the file's identity, not a courtesy preview, and
        // a folder of photos must not be able to spend their memory.
        const size_t previewEntries = memory.entries - memory.iconEntries;
        const size_t previewBytes = memory.storedBytes - memory.iconBytes;
        SetLine(d->cacheMemoryLine,
                TrF("In memory, previews: {0}",
                    FormatUsage(previewBytes, memory.contentBudget, previewEntries,
                                TrN("{n} thumbnail", "{n} thumbnails",
                                    static_cast<long long>(previewEntries)))));
        SetLine(d->cacheIconLine,
                TrF("In memory, application icons: {0}",
                    FormatUsage(memory.iconBytes, memory.iconBudget, memory.iconEntries,
                                TrN("{n} icon", "{n} icons",
                                    static_cast<long long>(memory.iconEntries)))));
        // This one is worth a word of explanation wherever it is shown: it
        // holds the tiles being painted at this moment and nothing else, so
        // with a settings window over the file display it is normally empty -
        // which would otherwise read as something not working.
        SetLine(d->cacheHotLine,
                d->settings && d->settings->compressedThumbnails
                        ? TrF("In memory, unpacked for drawing: {0} - only the tiles on "
                              "screen, so this empties while the display is not being drawn.",
                              FormatUsage(memory.hotBytes, memory.hotBudget, memory.hotEntries,
                                          TrN("{n} tile", "{n} tiles",
                                              static_cast<long long>(memory.hotEntries))))
                        : Tr("In memory, unpacked for drawing: not used while "
                             "thumbnails are held uncompressed."));
        // The thumbnails the display in front does NOT have, and why: the
        // one figure that tells "still on its way" from "the workers are
        // stuck" (waiting, nothing being made) and from "the files would not
        // decode" (could not be made - the log names each file).
        const size_t waiting = memory.pendingEntries -
                std::min(memory.pendingEntries, memory.inFlightEntries);
        SetLine(d->cacheMissingLine,
                TrF("Not shown yet: {0} waiting, {1} being made, {2} could not be made "
                    "(the tile keeps its type icon).",
                    waiting, memory.inFlightEntries, memory.failedEntries) +
                // A job running this long is what holds up a whole folder;
                // naming it is how the culprit is found.
                (memory.longestJobSeconds >= 10
                         ? " " + TrF("Slowest job: \"{0}\", running for {1} s ({2} workers).",
                                     memory.longestJobPath, memory.longestJobSeconds,
                                     memory.workerCount)
                         : std::string()));
    }

    // A switch in the window's text size, laid out like the page's other
    // controls.
    std::shared_ptr<UltraCanvasSwitch> MakeSwitch(const std::string& id,
                                                  const std::string& text,
                                                  bool checked,
                                                  std::function<void(bool)> onChange) {
        auto sw = UltraCanvasSwitch::Create(id, 0, 0, text, checked);
        SwitchVisualStyle style = sw->GetVisualStyle();
        style.base.fontSize       = kTextFontSize;
        style.base.textColor      = kTextColor;
        style.base.textHoverColor = kTextColor;
        sw->SetVisualStyle(style);
        // Explicit sizes: content measuring needs a render context, which the
        // dialog does not have while it is first laid out.
        sw->size.width  = CSSLayout::Dimension::Px(kTextWidth);
        sw->size.height = CSSLayout::Dimension::Px(kControlHeight);
        sw->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        sw->onStateChanged = [onChange](CheckedState, CheckedState now) {
            if (onChange) onChange(now == CheckedState::Checked);
        };
        return sw;
    }

    std::shared_ptr<UltraCanvasContainer> BuildCachePage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-cache", Tr("Cache"),
                Tr("What UltraFiler keeps so a folder opens without being read "
                   "again:"));

        d->diskCacheSwitch = MakeSwitch("ufl-set-cache-disk",
                Tr("Keep thumbnails on disk between runs"),
                d->settings->thumbnailDiskCache, [d](bool on) {
            d->settings->thumbnailDiskCache = on;
            UltraCanvasFilerWidget::SetThumbnailDiskCacheEnabled(on);
            if (d->cacheHooks.apply) d->cacheHooks.apply();
            ApplyAndSave(d);
            RefreshCacheUsage(d);
        });
        parts.body->AddChild(d->diskCacheSwitch);

        d->compressedSwitch = MakeSwitch("ufl-set-cache-compressed",
                Tr("Compress thumbnails held in memory"),
                d->settings->compressedThumbnails, [d](bool on) {
            d->settings->compressedThumbnails = on;
            if (d->cacheHooks.apply) d->cacheHooks.apply();
            ApplyAndSave(d);
            RefreshCacheUsage(d);
        });
        parts.body->AddChild(d->compressedSwitch);

        // What is held now, against what it is allowed to hold.
        parts.body->AddChild(MakeText("ufl-set-cache-used-title",
                Tr("In use:"), kTextWidth, kTextFontSize, kTextColor));
        d->cacheDiskLine = MakeText("ufl-set-cache-disk-line", std::string());
        d->cacheMemoryLine = MakeText("ufl-set-cache-memory-line", std::string());
        d->cacheIconLine = MakeText("ufl-set-cache-icon-line", std::string());
        d->cacheHotLine = MakeText("ufl-set-cache-hot-line", std::string());
        d->cacheMissingLine = MakeText("ufl-set-cache-missing-line", std::string());
        parts.body->AddChild(d->cacheDiskLine);
        parts.body->AddChild(d->cacheMemoryLine);
        parts.body->AddChild(d->cacheIconLine);
        parts.body->AddChild(d->cacheHotLine);
        parts.body->AddChild(d->cacheMissingLine);

        d->cacheLocationLine = MakeText("ufl-set-cache-location", std::string(), kTextWidth,
                                        kNoteFontSize, kNoteTextColor);
        parts.body->AddChild(d->cacheLocationLine);

        auto buttonRow = MakeButtonRow("ufl-set-cache-buttons");
        buttonRow->AddChild(MakeButton("ufl-set-cache-empty", Tr("Empty cache"),
                                       120, [d]() {
            const size_t files = UltraCanvasFilerWidget::ClearThumbnailDiskCache();
            if (d->cacheHooks.clearMemory) d->cacheHooks.clearMemory();
            // The figures below are read straight away, and whatever is on
            // screen is already being decoded again behind this window - so
            // they are a true reading of this moment and an understated one a
            // second later. Saying so is better than showing a number that
            // quietly grows, and Refresh is right there.
            SetLine(d->cacheStatus,
                    (files == 0
                        ? Tr("Cache emptied.")
                        : TrN("Cache emptied: {n} thumbnail deleted from disk.",
                              "Cache emptied: {n} thumbnails deleted from disk.",
                              static_cast<long long>(files))) +
                    " " + Tr("What is on screen is being read again and goes back into "
                             "the cache as it lands - press Refresh to see where it "
                             "settles."));
            RefreshCacheUsage(d);
        }));
        buttonRow->AddChild(MakeButton("ufl-set-cache-refresh", Tr("Refresh"), 90,
                                       [d]() { RefreshCacheUsage(d); }));
        parts.body->AddChild(buttonRow);

        d->cacheStatus = MakeText("ufl-set-cache-status", std::string(), kTextWidth,
                                  kTextFontSize, kNoteTextColor);
        parts.body->AddChild(d->cacheStatus);

        AddNote(parts, "ufl-set-cache-note1",
                Tr("A thumbnail costs far more to make than to keep: a folder of "
                   "photos, videos or documents is minutes of work the first time "
                   "it is opened. Kept on disk, that is paid once instead of once "
                   "per start."));
        AddNote(parts, "ufl-set-cache-note2",
                TrF("The disk cache looks after its own size: a thumbnail not used "
                    "for {0} days is deleted when UltraFiler next starts, so a folder "
                    "you keep visiting keeps its thumbnails and one you opened once "
                    "pays for itself and then goes away. Nothing here can show an "
                    "out-of-date picture - a file's size and date are stored with "
                    "its thumbnail, so a file that has changed is read again.",
                    DiskCache::kDefaultMaxAge.count() / 24));
        AddNote(parts, "ufl-set-cache-note3",
                Tr("The memory figures are the file display in front. Previews and "
                   "application icons have separate ceilings so a folder of photos "
                   "cannot push the programs' icons out, and application icons are "
                   "never written to disk: the system produces those faster than "
                   "they could be read back."));
        return parts.page;
    }

    // ===== THE PAGE SHOWN WHILE NOTHING IS SELECTED =====
    // The window opens with the three sections closed, so it opens on this
    // rather than on whichever page happened to be first.
    // ===== ARCHIVES > TAR =====

    // "6 (default)" / "every core" beside the page's sliders.
    void UpdateTarLabels(DialogState* d) {
        if (!d->settings) return;
        const FilerTarSettings& tar = d->settings->tar;
        const FilerTarSettings defaults;
        auto level = [](DialogState::TarSliderRow& row, int value, int byDefault) {
            if (!row.value) return;
            row.value->SetText(value == byDefault ? TrF("{0} (default)", value)
                                                  : std::to_string(value));
            row.value->RequestRedraw();
        };
        level(d->tarGzip, tar.gzipLevel, defaults.gzipLevel);
        level(d->tarBzip2, tar.bzip2Level, defaults.bzip2Level);
        level(d->tarXz, tar.xzLevel, defaults.xzLevel);
        level(d->tarZstd, tar.zstdLevel, defaults.zstdLevel);
        if (d->tarThreads.value) {
            const int n = tar.threads;
            d->tarThreads.value->SetText(n == 0 ? Tr("every core")
                    : TrN("{n} thread", "{n} threads", n));
            d->tarThreads.value->RequestRedraw();
        }
    }

    // One "[caption] [slider] [value]" row setting the int `field` of the
    // TAR settings.
    DialogState::TarSliderRow AddTarSlider(DialogState* d, PageParts& parts,
                                           const std::string& key,
                                           const std::string& caption,
                                           int minValue, int maxValue,
                                           int FilerTarSettings::* field) {
        DialogState::TarSliderRow row;
        row.value = MakeSliderValueLabel("ufl-set-tar-" + key + "-value", 90);
        row.slider = MakeIntSlider("ufl-set-tar-" + key, minValue, maxValue,
                d->settings->tar.*field, [d, field](int value) {
            if (!d->settings || d->settings->tar.*field == value) return;
            d->settings->tar.*field = value;
            UpdateTarLabels(d);
            ApplyAndSave(d);
        });
        parts.body->AddChild(MakeSliderRow("ufl-set-tar-" + key + "-row", caption,
                row.slider, row.value, 0, 100));
        return row;
    }

    // Puts the TAR settings onto the page's controls (Restore defaults).
    void SyncTarControls(DialogState* d) {
        if (!d->settings) return;
        const FilerTarSettings& tar = d->settings->tar;
        d->syncing = true;
        auto slide = [](DialogState::TarSliderRow& row, int value) {
            if (row.slider) row.slider->SetValue(static_cast<float>(value));
        };
        slide(d->tarGzip, tar.gzipLevel);
        slide(d->tarBzip2, tar.bzip2Level);
        slide(d->tarXz, tar.xzLevel);
        slide(d->tarZstd, tar.zstdLevel);
        slide(d->tarThreads, tar.threads);
        if (d->tarLongBox) d->tarLongBox->SetChecked(tar.zstdLongMode);
        for (const auto& [format, radio] : d->tarFormatRadios)
            if (format == tar.headerFormat) d->tarFormatGroup.SelectButton(radio);
        if (d->tarTimesBox) d->tarTimesBox->SetChecked(tar.keepTimestamps);
        if (d->tarPermissionsBox) d->tarPermissionsBox->SetChecked(tar.keepPermissions);
        if (d->tarOwnerBox) d->tarOwnerBox->SetChecked(tar.keepOwner);
        d->tarLinksGroup.SelectButton(tar.storeSymlinks ? d->tarLinkRadio
                                                        : d->tarTargetRadio);
        if (d->tarExcludeInput) d->tarExcludeInput->SetTags(tar.excludePatterns);
        d->syncing = false;
        UpdateTarLabels(d);
    }

    std::shared_ptr<UltraCanvasContainer> BuildTarPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-tar", Tr("TAR"),
                Tr("How Compress packs a .tar, .tar.gz, .tar.bz2, .tar.xz and "
                   ".tar.zst:"), /*scrolls=*/true);
        FilerTarSettings& tar = d->settings->tar;

        // ----- the compressors -----
        AddBodyCaption(parts, "ufl-set-tar-level-caption",
                       Tr("Compression level - higher packs smaller, and slower:"));
        d->tarGzip = AddTarSlider(d, parts, "gzip", "gzip", 1,
                FilerTarSettings::kMaxGzipLevel, &FilerTarSettings::gzipLevel);
        d->tarBzip2 = AddTarSlider(d, parts, "bzip2", "bzip2", 1,
                FilerTarSettings::kMaxBzip2Level, &FilerTarSettings::bzip2Level);
        d->tarXz = AddTarSlider(d, parts, "xz", "xz", 0,
                FilerTarSettings::kMaxXzLevel, &FilerTarSettings::xzLevel);
        d->tarZstd = AddTarSlider(d, parts, "zstd", "Zstandard", 1,
                FilerTarSettings::kMaxZstdLevel, &FilerTarSettings::zstdLevel);

        AddBodyCaption(parts, "ufl-set-tar-threads-caption",
                       Tr("Threads of xz and Zstandard (0: one per core):"));
        // As many as this machine has cores - or more, when the setting was
        // made on a bigger one.
        const int cores = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
        const int maxThreads = std::min(FilerTarSettings::kMaxThreads,
                                        std::max(cores, tar.threads));
        d->tarThreads = AddTarSlider(d, parts, "threads", Tr("Threads"), 0, maxThreads,
                                     &FilerTarSettings::threads);

        d->tarLongBox = MakeCheckbox("ufl-set-tar-long",
                Tr("Zstandard long mode: matches across 128 MB (zstd --long=27)"),
                kTextWidth, tar.zstdLongMode, [d](bool on) {
            if (!d->settings) return;
            d->settings->tar.zstdLongMode = on;
            ApplyAndSave(d);
        });
        parts.body->AddChild(d->tarLongBox);

        // ----- the header format -----
        AddBodyCaption(parts, "ufl-set-tar-format-caption", Tr("Header format:"));
        {
            auto row = MakeChoiceRow("ufl-set-tar-format-row");
            const std::vector<std::tuple<FilerTarHeaderFormat, const char*, int>> choices = {
                {FilerTarHeaderFormat::Pax,   "PAX (POSIX.1-2001)", 160},
                {FilerTarHeaderFormat::Gnu,   "GNU tar",            90},
                {FilerTarHeaderFormat::Ustar, "ustar (POSIX.1-1988)", 170},
            };
            d->tarFormatRadios.clear();
            for (const auto& [format, label, width] : choices) {
                auto radio = MakeChoice("ufl-set-tar-format-" +
                                        UltraFilerSettings::FormatTarHeaderFormat(format),
                                        label, tar.headerFormat == format, width);
                d->tarFormatRadios.emplace_back(format, radio);
                d->tarFormatGroup.AddRadioButton(radio);
                row->AddChild(radio);
            }
            d->tarFormatGroup.onSelectionChanged =
                    [d](std::shared_ptr<UltraCanvasRadio> selected) {
                if (!selected || !d->settings) return;
                for (const auto& [format, radio] : d->tarFormatRadios)
                    if (radio == selected) d->settings->tar.headerFormat = format;
                ApplyAndSave(d);
            };
            parts.body->AddChild(row);
        }

        // ----- what each file keeps -----
        AddBodyCaption(parts, "ufl-set-tar-keep-caption", Tr("Store with each file:"));
        d->tarTimesBox = MakeCheckbox("ufl-set-tar-times",
                Tr("Its modification time (else the time it was packed)"),
                kTextWidth, tar.keepTimestamps, [d](bool on) {
            if (!d->settings) return;
            d->settings->tar.keepTimestamps = on;
            ApplyAndSave(d);
        });
        d->tarPermissionsBox = MakeCheckbox("ufl-set-tar-permissions",
                Tr("Its permissions (else 0644, folders 0755)"),
                kTextWidth, tar.keepPermissions, [d](bool on) {
            if (!d->settings) return;
            d->settings->tar.keepPermissions = on;
            ApplyAndSave(d);
        });
        d->tarOwnerBox = MakeCheckbox("ufl-set-tar-owner",
                Tr("Its owner and group (else 0, without names)"),
                kTextWidth, tar.keepOwner, [d](bool on) {
            if (!d->settings) return;
            d->settings->tar.keepOwner = on;
            ApplyAndSave(d);
        });
        parts.body->AddChild(d->tarTimesBox);
        parts.body->AddChild(d->tarPermissionsBox);
        parts.body->AddChild(d->tarOwnerBox);

        // ----- symbolic links -----
        AddBodyCaption(parts, "ufl-set-tar-links-caption", Tr("A symbolic link:"));
        d->tarLinkRadio = MakeChoice("ufl-set-tar-links-link",
                Tr("Store the link"), tar.storeSymlinks);
        d->tarTargetRadio = MakeChoice("ufl-set-tar-links-target",
                Tr("Store the file or folder it points to (tar -h)"), !tar.storeSymlinks);
        d->tarLinksGroup.AddRadioButton(d->tarLinkRadio);
        d->tarLinksGroup.AddRadioButton(d->tarTargetRadio);
        d->tarLinksGroup.onSelectionChanged =
                [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->settings) return;
            d->settings->tar.storeSymlinks = (selected == d->tarLinkRadio);
            ApplyAndSave(d);
        };
        parts.body->AddChild(d->tarLinkRadio);
        parts.body->AddChild(d->tarTargetRadio);

        // ----- the exclusions -----
        AddBodyCaption(parts, "ufl-set-tar-exclude-caption",
                       Tr("Leave out - type a pattern and press Enter:"));
        d->tarExcludeInput = CreateTagInput("ufl-set-tar-exclude", -1, -1,
                                            static_cast<float>(kTextWidth));
        d->tarExcludeInput->SetPlaceholder(Tr("e.g. .git or *.tmp"));
        d->tarExcludeInput->SetTags(tar.excludePatterns);
        d->tarExcludeInput->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        d->tarExcludeInput->size.width = CSSLayout::Dimension::Px(kTextWidth);
        d->tarExcludeInput->onTagsChanged =
                [d](const std::vector<std::string>& patterns) {
            if (!d->settings) return;
            d->settings->tar.excludePatterns = patterns;
            ApplyAndSave(d);
        };
        parts.body->AddChild(d->tarExcludeInput);

        UpdateTarLabels(d);

        d->resets[kPageTar] = PageReset{Tr("Restore defaults"), 170, [d]() {
            if (!d->settings) return;
            d->settings->tar = FilerTarSettings();
            SyncTarControls(d);
            ApplyAndSave(d);
        }};

        AddNote(parts, "ufl-set-tar-note1",
                Tr("Point at an entry of the Compress menu to see the command "
                   "line it packs with. The defaults are the compressors' own "
                   "levels - gzip 6, bzip2 9, xz 6, Zstandard 3 - on every core."));
        AddNote(parts, "ufl-set-tar-note2",
                Tr("Threads make xz and Zstandard many times faster on a "
                   "multi-core machine; xz then cuts the data into blocks, which "
                   "costs a little size, and needs more memory at level 9. xz's "
                   "extreme mode (-e) is not available. Long mode makes big, "
                   "repetitive data pack smaller; zstd 1.3.2 or later unpacks it "
                   "without options."));
        AddNote(parts, "ufl-set-tar-note3",
                Tr("PAX is today's standard: long names, large files, names in "
                   "any script. GNU tar's own format does the same its own way. "
                   "ustar is for the oldest tar programs and takes names of at "
                   "most 255 bytes and files of at most 8 GB."));
        AddNote(parts, "ufl-set-tar-note4",
                Tr("A pattern is matched against each name inside a packed "
                   "folder, ignoring case: \"*\" stands for any run of "
                   "characters, \"?\" for one; with a \"/\" it is matched "
                   "against the path inside the folder (\"src/*.o\"). A "
                   "matching folder is left out with all it holds; what you "
                   "selected is always packed. ZIP and 7-Zip are not affected "
                   "by this page."));
        return parts.page;
    }

    std::shared_ptr<UltraCanvasContainer> BuildStartPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-start", Tr("Settings"),
                Tr("Open a section on the left and choose the page to set:"));

        AddNote(parts, "ufl-set-start-note1",
                Tr("Display - what the folder tree, the file display and the "
                   "preview beside it show: colours, the Home folder, hidden "
                   "files, file names, file extensions, files in use, and which "
                   "file kinds get a thumbnail or a preview."));
        AddNote(parts, "ufl-set-start-note2",
                Tr("Handling - what an action does: dropping dragged files onto "
                   "a folder, opening a file, how long the passwords of protected "
                   "files are kept, and what the \"+\" of the tab strip opens."));
        AddNote(parts, "ufl-set-start-note-archives",
                Tr("Archives - how Compress packs a TAR: each compressor's level "
                   "and threads, the header format, what is stored of each file, "
                   "and the names left out."));
        AddNote(parts, "ufl-set-start-note3",
                Tr("Extras - the command line program \"Open prompt\" starts, "
                   "clearing the recently-used lists, the pinned entries and the "
                   "remembered folder views, and what UltraFiler keeps cached so "
                   "a folder opens without being read again."));
        AddNote(parts, "ufl-set-start-note4",
                Tr("Every change applies straight away and is saved; there is "
                   "nothing to confirm."));

        // The localisation editor on UltraFiler's own set, in the language it
        // runs in: the "Translate this application..." entry of the
        // localisation proposal (§5), here because UltraFiler has no menu bar.
        auto translateRow = MakeButtonRow("ufl-set-start-translate-row");
        translateRow->AddChild(MakeButton("ufl-set-start-translate", Tr("Translate UltraFiler..."), 200,
                [d]() {
                    ShowLocalisationEditorDialog(d->window.get(), UltraLocalisation::CurrentApplication(),
                                                 UltraLocalisation::CurrentLanguage());
                }));
        parts.body->AddChild(translateRow);
        AddNote(parts, "ufl-set-start-note-translate",
                Tr("Every text UltraFiler shows, in one grid with the sets it comes from: "
                   "its own set, the Computer base set and the standard set. A change is "
                   "saved into the settings folder and shows the next time UltraFiler starts."));
        return parts.page;
    }

    // ===== PAGE SWITCHING =====
    // The kit's, under the names the pages below were written with.
    void UpdateRestoreButton(DialogState* d) { d->UpdateRestoreButton(); }
    void ShowPage(DialogState* d, const std::string& nodeId) { d->ShowPage(nodeId); }
    void SelectPage(DialogState* d, const std::string& pageId) { d->SelectPage(pageId); }
    void AddPage(DialogState* d, const char* pageId,
                 const std::shared_ptr<UltraCanvasContainer>& page) {
        d->AddPage(pageId, page);
    }
    void AddTreeNode(DialogState* d, const char* parentId, const char* id,
                     const std::string& text) {
        d->AddTreeNode(parentId, id, text);
    }

    void BuildDialog(DialogState* d, UltraCanvasWindowBase* parent) {
        if (!d->Create(parent, Tr("UltraFiler - Settings"))) return;
        // The Cache page shows live figures, so it is re-read on the way in
        // rather than left showing what was true when the window was built.
        d->onPageShown = [d](const std::string& pageId) {
            if (pageId == kPageCache) RefreshCacheUsage(d);
        };
        // The popup's callbacks reach back into this dialog state, so it
        // must not outlive the window it was opened from.
        d->onClosed = []() { CloseColorPopup(false); };

        AddTreeNode(d, "settings", kPageDisplay, Tr("Display"));
        AddTreeNode(d, kPageDisplay, kPageTreeview, Tr("Treeview"));
        AddTreeNode(d, kPageDisplay, kPageTabStyle, Tr("Tab style"));
        AddTreeNode(d, kPageDisplay, kPagePathBar, Tr("Path bar"));
        AddTreeNode(d, kPageDisplay, kPageNewTab, Tr("New tab"));
        AddTreeNode(d, kPageDisplay, kPageHomeFolder, Tr("Home folder"));
        AddTreeNode(d, kPageDisplay, kPageFiles, Tr("Files"));
        AddTreeNode(d, kPageDisplay, kPageIgnoredFiles, Tr("Ignored files"));
        AddTreeNode(d, kPageDisplay, kPageFileNames, Tr("File names"));
        AddTreeNode(d, kPageDisplay, kPageFileExtensions, Tr("File extensions"));
        AddTreeNode(d, kPageDisplay, kPageFileIcons, Tr("File icons"));
        AddTreeNode(d, kPageDisplay, kPageFilesInUse, Tr("Files in use"));
        AddTreeNode(d, kPageDisplay, kPagePdfInventory, Tr("PDF Inventory"));
        AddTreeNode(d, kPageDisplay, kPageThumbnails, Tr("Thumbnails"));
        AddTreeNode(d, kPageDisplay, kPageDetailView, Tr("Detail view"));
        AddTreeNode(d, "settings", kPageHandling, Tr("Handling"));
        AddTreeNode(d, kPageHandling, kPageDragDrop, Tr("Drag & Drop"));
        AddTreeNode(d, kPageHandling, kPageFileOperations, Tr("File operations"));
        AddTreeNode(d, kPageHandling, kPageOpeningFiles, Tr("Opening files"));
        AddTreeNode(d, kPageHandling, kPageProtectedFiles, Tr("Protected files"));
        AddTreeNode(d, "settings", kPageArchives, Tr("Archives"));
        AddTreeNode(d, kPageArchives, kPageTar, Tr("TAR"));
        AddTreeNode(d, "settings", kPageExtras, Tr("Extras"));
        AddTreeNode(d, kPageExtras, kPageOpenPrompt, Tr("Open prompt"));
        AddTreeNode(d, kPageExtras, kPageLists, Tr("History & Favorites"));
        AddTreeNode(d, kPageExtras, kPageCache, Tr("Cache"));

        // ----- pages -----
        AddPage(d, kPageTreeview, BuildTreeviewPage(d));
        AddPage(d, kPageTabStyle, BuildTabStylePage(d));
        AddPage(d, kPagePathBar, BuildPathBarPage(d));
        AddPage(d, kPageHomeFolder, BuildHomeFolderPage(d));
        AddPage(d, kPageFiles, BuildFilesPage(d));
        AddPage(d, kPageIgnoredFiles, BuildIgnoredFilesPage(d));
        AddPage(d, kPageFileNames, BuildFileNamesPage(d));
        AddPage(d, kPageFileExtensions, BuildFileExtensionsPage(d));
        AddPage(d, kPageFileIcons, BuildFileIconsPage(d));
        AddPage(d, kPageFilesInUse, BuildFilesInUsePage(d));
        AddPage(d, kPagePdfInventory, BuildPdfInventoryPage(d));
        AddPage(d, kPageThumbnails,
                BuildFormatSwitchPage(d, FilerPreviewTarget::Thumbnails));
        AddPage(d, kPageDetailView,
                BuildFormatSwitchPage(d, FilerPreviewTarget::DetailView));
        AddPage(d, kPageDragDrop, BuildDragDropPage(d));
        AddPage(d, kPageFileOperations, BuildFileOperationsPage(d));
        AddPage(d, kPageOpeningFiles, BuildOpeningFilesPage(d));
        AddPage(d, kPageProtectedFiles, BuildProtectedFilesPage(d));
        AddPage(d, kPageNewTab, BuildNewTabPage(d));
        AddPage(d, kPageOpenPrompt, BuildOpenPromptPage(d));
        AddPage(d, kPageLists, BuildListsPage(d));
        AddPage(d, kPageTar, BuildTarPage(d));
        AddPage(d, kPageCache, BuildCachePage(d));
        AddPage(d, kPageStart, BuildStartPage(d));

        // Nothing is selected while every section is closed, so the page
        // area opens on the start page. Show() selects a page from here when
        // the caller asked for one.
        d->Finish(kPageStart);
    }

} // namespace

void UltraFilerSettingsDialog::Show(UltraCanvasWindowBase* parent,
                                    UltraFilerSettings* settings,
                                    std::function<void()> onChanged,
                                    std::function<void()> onClearHistory,
                                    std::function<void()> onClearFavorites,
                                    std::function<void()> onClearFolderViews,
                                    Page initialPage) {
    const char* pageId = nullptr;
    switch (initialPage) {
        case Page::Thumbnails: pageId = kPageThumbnails; break;
        case Page::DetailView: pageId = kPageDetailView; break;
        case Page::FileExtensions: pageId = kPageFileExtensions; break;
        case Page::Cache: pageId = kPageCache; break;
        case Page::HomeFolder: pageId = kPageHomeFolder; break;
        case Page::IgnoredFiles: pageId = kPageIgnoredFiles; break;
        default: break;
    }
    // Raise the already open window instead of opening a second one - on the
    // page that was asked for, so the Display menu's "File formats..." always
    // lands there.
    if (g_dialog && g_dialog->window && !g_dialog->closed) {
        if (pageId) SelectPage(g_dialog.get(), pageId);
        g_dialog->window->Show();
        return;
    }

    auto state = std::make_shared<DialogState>();
    state->settings = settings;
    state->cacheHooks = g_cacheHooks;   // read while the Cache page is built
    state->onChanged = std::move(onChanged);
    state->onClearHistory = std::move(onClearHistory);
    state->onClearFavorites = std::move(onClearFavorites);
    state->onClearFolderViews = std::move(onClearFolderViews);
    BuildDialog(state.get(), parent);
    if (!state->window) return;
    g_dialog = state;   // keeps the widgets alive
    if (pageId) SelectPage(state.get(), pageId);
}

namespace {
    // The settings a file display's own context menu can change while this
    // window is open (Display > Thumbnails / Detail view / File extensions /
    // File icons / Folder previews - everything the host adopts through
    // onDisplayFormatsChanged), pushed back onto the controls. The pages read
    // the settings once, when they are built, so without this an open window
    // went on showing the tick a menu click had just taken away.
    void SyncControlsFromSettings(DialogState* d) {
        if (!d || !d->settings) return;
        d->syncing = true;
        RefreshFormatPage(d, FilerPreviewTarget::Thumbnails);
        RefreshFormatPage(d, FilerPreviewTarget::DetailView);
        if (d->extensionsInNamesBox)
            d->extensionsInNamesBox->SetChecked(d->settings->showFileExtensions);
        if (d->folderPreviewsBox)
            d->folderPreviewsBox->SetChecked(d->settings->folderPreviews);
        for (const auto& [badge, radio] : d->badgeRadios)
            if (badge == d->settings->extensionBadge) d->extensionBadgeGroup.SelectButton(radio);
        for (const auto& [style, radio] : d->fileIconRadios)
            if (style == ShownFileIconStyle(d)) d->fileIconGroup.SelectButton(radio);
        for (const auto& [style, radio] : d->tabStyleRadios)
            if (style == d->settings->tabStripStyle) d->tabStyleGroup.SelectButton(radio);
        d->syncing = false;
        if (d->window) d->window->RequestRedraw();
    }
}   // namespace

void UltraFilerSettingsDialog::SyncWithSettings() {
    if (g_dialog && g_dialog->window && !g_dialog->closed)
        SyncControlsFromSettings(g_dialog.get());
}

void UltraFilerSettingsDialog::SetCacheHooks(CacheHooks hooks) {
    g_cacheHooks = std::move(hooks);
    // A window that is already open takes them too, so the Cache page works
    // whichever order the host does this in.
    if (g_dialog) g_dialog->cacheHooks = g_cacheHooks;
}

void UltraFilerSettingsDialog::Shutdown() {
    g_dialog.reset();
    g_cacheHooks = {};
}

} // namespace UltraCanvas
