// Apps/UltraFiler/UltraFilerSettingsDialog.cpp
// UltraFiler settings window: settings-page tree on the left, the selected
// page on the right. The tree shows the three sections - Display, Handling,
// Extras - as its top level (the root row itself is hidden), each closed when
// the window opens, so it opens on the start page: what the sections hold.
// Pages: Display > Treeview (the folder tree's drive-row
// background and selected-folder highlight, each shown as a colour box that
// opens the colour picker in a popup window), Display > Home folder (what the
// Home folder shows), Display > File extensions (whether a displayed name
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
// Handling > Tabs (what the "+" of the folder tab strip opens - the
// current folder again or the Home folder), Extras > Open prompt (the command
// line program UltraFiler opens, picked with the file dialog and stored with
// "Save app") and Extras > History & Favorites (clearing the recently-used
// lists and the pinned entries). Changes apply live and are saved
// immediately.
//
// Every page is built the same way (MakePage): a bold title, the one-line
// caption that says what the choice is about, the controls, and - set apart
// at the foot of the page in its own tinted block - the notes that explain
// the setting. A page's "Restore default ..." button is not among its
// controls but at the left end of the window's bottom bar, opposite Close,
// where the same spot serves every page that has one. The backdrop behind
// transparent images is no longer a page here: the media viewer's own colour
// strip under the picture chooses it, and the choice is saved from there.
// Version: 1.12.0
// Last Modified: 2026-09-12
// Author: UltraCanvas Framework

#include "UltraFilerSettingsDialog.h"
#include "UltraFilerPrompt.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasFileLock.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

namespace UltraCanvas {

namespace {

    // ----- one type scale for the whole window -----
    // Titles above the text, the text and every control at one size, and the
    // notes a step below it and greyed: what is read first is the largest.
    constexpr float kTitleFontSize = 15.0f;   // page title (bold)
    constexpr float kTextFontSize  = 10.0f;   // captions, choices, buttons, fields
    constexpr float kNoteFontSize  = 9.0f;    // the notes under the controls
    constexpr float kTreeFontSize  = 9.0f;    // matches the main window's UI font

    const Color kTextColor        = Color(40, 40, 44, 255);
    const Color kNoteTextColor    = Color(96, 96, 104, 255);
    const Color kNoteBackground   = Color(244, 245, 248, 255);
    const Color kNoteAccent       = Color(160, 176, 200, 255);

    // Page geometry: the window is 700 wide, the tree takes 180, so a page
    // has 520; with its padding that leaves ~480 for content. Every text is
    // wrapped at a fixed width because content measuring needs a render
    // context, which the dialog does not have while it is first laid out.
    constexpr int kPagePadding  = 20;
    constexpr int kTextWidth    = 440;   // wrapped texts and choice rows
    constexpr int kNoteWidth    = 430;   // wrapped notes inside their block
    constexpr int kControlHeight = 22;   // one checkbox / radio row

    // Page ids double as tree node ids.
    constexpr const char* kPageDisplay = "display";
    constexpr const char* kPageTreeview = "display/treeview";
    constexpr const char* kPageHomeFolder = "display/home";
    constexpr const char* kPagePdfInventory = "display/pdf-inventory";
    constexpr const char* kPageThumbnails = "display/thumbnails";
    constexpr const char* kPageFileExtensions = "display/file-extensions";
    constexpr const char* kPageFilesInUse = "display/files-in-use";
    constexpr const char* kPageDetailView = "display/detail-view";
    constexpr const char* kPageHandling = "handling";
    constexpr const char* kPageDragDrop = "handling/drag-drop";
    constexpr const char* kPageTabs = "handling/tabs";
    constexpr const char* kPageOpeningFiles = "handling/opening-files";
    constexpr const char* kPageExtras = "extras";
    constexpr const char* kPageOpenPrompt = "extras/open-prompt";
    constexpr const char* kPageLists = "extras/history-favorites";
    // The page shown while nothing is selected - the window opens with every
    // section closed, so there is no page to show yet.
    constexpr const char* kPageStart = "start";

    // A page's "Restore default ..." action, shown in the bottom bar while
    // that page is up.
    struct PageReset {
        std::string           label;
        int                   width = 170;
        std::function<void()> action;
    };

    // The one open settings window (or the last closed one, until reopened).
    struct DialogState {
        std::shared_ptr<UltraCanvasWindow>    window;
        bool                                  closed = false;
        std::shared_ptr<UltraCanvasTreeView>  tree;
        std::shared_ptr<UltraCanvasContainer> pageArea;
        std::map<std::string, std::shared_ptr<UltraCanvasContainer>> pages;
        std::map<std::string, PageReset>      resets;
        std::shared_ptr<UltraCanvasButton>    restoreButton;   // bottom bar, left
        std::string                           shownPage;

        // Display > Treeview: the colour boxes that open the picker popup.
        std::shared_ptr<UltraCanvasButton> driveColorBox;
        std::shared_ptr<UltraCanvasButton> selectedColorBox;

        // Display > Home folder: what the Home folder shows.
        std::shared_ptr<UltraCanvasRadio>  homeAllRadio;
        std::shared_ptr<UltraCanvasRadio>  homePredefinedRadio;
        UltraCanvasRadioGroup              homeContentGroup;

        // Display > File extensions: the "keep them in the name" checkbox and
        // one radio per thumbnail tag mode (None / Bar / Icon), in the order
        // the widget lists them.
        std::shared_ptr<UltraCanvasCheckbox> extensionsInNamesBox;
        // Display > Files in use: the "mark held files" checkbox.
        std::shared_ptr<UltraCanvasCheckbox> lockMarkingBox;
        std::vector<std::pair<FilerExtensionBadge,
                              std::shared_ptr<UltraCanvasRadio>>> badgeRadios;
        UltraCanvasRadioGroup                extensionBadgeGroup;

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

        // Handling > Drag & Drop: whether a drop asks first.
        std::shared_ptr<UltraCanvasRadio>       confirmAlwaysRadio;
        std::shared_ptr<UltraCanvasRadio>       confirmMoveRadio;
        std::shared_ptr<UltraCanvasRadio>       confirmNeverRadio;
        UltraCanvasRadioGroup                   dropConfirmGroup;

        // Handling > Tabs: what the tab strip's "+" opens.
        std::shared_ptr<UltraCanvasRadio>       newTabCurrentRadio;
        std::shared_ptr<UltraCanvasRadio>       newTabHomeRadio;
        UltraCanvasRadioGroup                   newTabGroup;

        // Handling > Opening files: what a double-click on a file does when
        // the system has a program registered for it.
        std::shared_ptr<UltraCanvasRadio>       openAppRadio;
        std::shared_ptr<UltraCanvasRadio>       openPreviewRadio;
        UltraCanvasRadioGroup                   openActivationGroup;

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

    // ===== TEXT =====

    // A single-line label that hugs its text.
    std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id,
                                                const std::string& text,
                                                float fontSize = kTextFontSize,
                                                const Color& color = kTextColor) {
        auto l = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 20);
        l->SetText(text);
        l->SetFontSize(fontSize);
        l->SetTextColor(color);
        l->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        l->size.width  = CSSLayout::Dimension::Auto();
        l->size.height = CSSLayout::Dimension::Auto();
        return l;
    }

    // A paragraph: word-wrapped at `width`, as tall as its lines.
    std::shared_ptr<UltraCanvasLabel> MakeText(const std::string& id,
                                               const std::string& text,
                                               int width = kTextWidth,
                                               float fontSize = kTextFontSize,
                                               const Color& color = kTextColor) {
        auto l = MakeLabel(id, text, fontSize, color);
        l->SetWrap(TextWrap::WrapWord);
        l->size.width = CSSLayout::Dimension::Px(width);
        return l;
    }

    // ===== PAGE SKELETON =====
    // Title, caption, the controls, and the notes block at the foot - the
    // same order and the same spacing on every page, so the eye finds the
    // control where it found it on the last page.
    struct PageParts {
        std::shared_ptr<UltraCanvasContainer> page;    // the whole page
        std::shared_ptr<UltraCanvasContainer> body;    // the controls
        std::shared_ptr<UltraCanvasContainer> notes;   // the explanations
    };

    std::shared_ptr<UltraCanvasContainer> MakeNotesBlock(const std::string& id) {
        auto notes = std::make_shared<UltraCanvasContainer>(id);
        notes->layout.SetFlexColumn().SetFlexGap(6)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        notes->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        notes->SetBackgroundColor(kNoteBackground);
        notes->SetBorderLeft(3, kNoteAccent);
        notes->SetPadding(10, 12, 10, 12);
        return notes;
    }

    // `scrolls`: a page longer than the window (the lists of formats) scrolls,
    // and there the notes sit right under the caption, before the list, so
    // they are read before the hundred checkboxes rather than found after
    // them. Every other page keeps its notes at the foot, clear of the
    // controls.
    PageParts MakePage(const std::string& id, const std::string& title,
                       const std::string& caption, bool scrolls = false) {
        PageParts parts;
        parts.page = std::make_shared<UltraCanvasContainer>(id);
        parts.page->layout.SetFlexColumn().SetFlexGap(0)
                          .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        parts.page->SetPadding(kPagePadding, kPagePadding, kPagePadding, kPagePadding);
        if (scrolls) {
            // Vertically only: the vertical bar narrows the viewport, which
            // would otherwise fabricate a horizontal overflow of its own width.
            ContainerStyle cs;
            cs.autoShowHorizontalScrollbar = false;
            parts.page->SetContainerStyle(cs);
        }

        auto titleLabel = MakeLabel(id + "-title", title, kTitleFontSize);
        titleLabel->SetFontWeight(FontWeight::Bold);
        parts.page->AddChild(titleLabel);

        auto captionLabel = MakeText(id + "-caption", caption);
        captionLabel->SetMargin(6, 0, 0, 0);
        parts.page->AddChild(captionLabel);

        parts.body = std::make_shared<UltraCanvasContainer>(id + "-body");
        parts.body->layout.SetFlexColumn().SetFlexGap(8)
                          .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        parts.body->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                              .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        parts.body->SetMargin(14, 0, 0, 0);

        parts.notes = MakeNotesBlock(id + "-notes");

        if (scrolls) {
            parts.notes->SetMargin(12, 0, 0, 0);
            parts.page->AddChild(parts.notes);
            parts.page->AddChild(parts.body);
        } else {
            parts.page->AddChild(parts.body);
            // The spacer pushes the notes to the foot of the page, away from
            // the controls, and gives way when the window is made small.
            auto spacer = std::make_shared<UltraCanvasContainer>(id + "-spacer");
            spacer->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                              .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            spacer->size.height = CSSLayout::Dimension::Px(24);
            parts.page->AddChild(spacer);
            parts.page->AddChild(parts.notes);
        }
        return parts;
    }

    // One paragraph of a page's notes.
    void AddNote(PageParts& parts, const std::string& id, const std::string& text) {
        parts.notes->AddChild(MakeText(id, text, kNoteWidth, kNoteFontSize,
                                       kNoteTextColor));
    }

    // A caption inside the body, introducing a second group of controls.
    void AddBodyCaption(PageParts& parts, const std::string& id,
                        const std::string& text) {
        auto l = MakeText(id, text);
        l->SetMargin(10, 0, 0, 0);
        parts.body->AddChild(l);
    }

    // ===== CONTROLS =====

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
        b->SetFontSize(kTextFontSize);
        b->SetCornerRadius(4.0f);
        b->SetColors(Color(255, 255, 255, 255), Color(233, 238, 244, 255));
        b->SetTextColors(kTextColor);
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

    // A row of buttons inside a page body.
    std::shared_ptr<UltraCanvasContainer> MakeButtonRow(const std::string& id) {
        auto row = std::make_shared<UltraCanvasContainer>(id);
        row->layout.SetFlexRow().SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        row->size.width  = CSSLayout::Dimension::Px(kTextWidth);
        row->size.height = CSSLayout::Dimension::Px(34);
        return row;
    }

    // One choice of a radio group, in the window's text size. Explicit
    // sizes: content measuring needs a render context, which the dialog does
    // not have while it is first laid out.
    std::shared_ptr<UltraCanvasRadio> MakeChoice(const std::string& id,
                                                 const std::string& text,
                                                 bool checked,
                                                 int width = kTextWidth) {
        auto radio = UltraCanvasRadio::Create(id, -1, -1, text, checked);
        RadioVisualStyle style = radio->GetVisualStyle();
        style.base.fontSize       = kTextFontSize;
        style.base.textColor      = kTextColor;
        style.base.textHoverColor = kTextColor;
        radio->SetVisualStyle(style);
        radio->size.width  = CSSLayout::Dimension::Px(width);
        radio->size.height = CSSLayout::Dimension::Px(kControlHeight);
        radio->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        return radio;
    }

    std::shared_ptr<UltraCanvasCheckbox> MakeCheckbox(
            const std::string& id, const std::string& text, int width,
            bool checked, std::function<void(bool)> onChange) {
        auto box = std::make_shared<UltraCanvasCheckbox>(id, 0, 0,
                static_cast<float>(width), static_cast<float>(kControlHeight),
                text);
        box->SetFontSize(kTextFontSize);
        CheckboxVisualStyle style = box->GetVisualStyle();
        style.base.textColor      = kTextColor;
        style.base.textHoverColor = kTextColor;
        box->SetVisualStyle(style);
        box->SetChecked(checked);
        // Explicit sizes: content measuring needs a render context, which the
        // dialog does not have while it is first laid out.
        box->size.width  = CSSLayout::Dimension::Px(width);
        box->size.height = CSSLayout::Dimension::Px(kControlHeight);
        box->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        box->onStateChanged = [onChange](CheckedState, CheckedState now) {
            if (onChange) onChange(now == CheckedState::Checked);
        };
        return box;
    }

    // A colour picker restyled for the dialog's light surface.
    ColorPickerStyle LightPickerStyle() {
        ColorPickerStyle s;
        s.backgroundColor = Color(249, 249, 251, 255);
        s.panelColor      = Color(240, 240, 244, 255);
        s.borderColor     = Color(210, 210, 216, 255);
        s.textColor       = kTextColor;
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
        row->size.width  = CSSLayout::Dimension::Px(kTextWidth);
        row->size.height = CSSLayout::Dimension::Px(30);

        auto label = MakeLabel(id + "-label", caption);
        label->size.width  = CSSLayout::Dimension::Px(190);
        label->size.height = CSSLayout::Dimension::Px(20);
        row->AddChild(label);
        row->AddChild(box);
        return row;
    }

    std::shared_ptr<UltraCanvasContainer> BuildTreeviewPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-treeview", "Treeview",
                "Colours of the folder tree on the left of the main window:");

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
        parts.body->AddChild(MakeColorRow("ufl-set-tv-drive-row",
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
        parts.body->AddChild(MakeColorRow("ufl-set-tv-selected-row",
                "Selected folder colour:", d->selectedColorBox));

        AddNote(parts, "ufl-set-tv-note1",
                "Click a colour box to pick a colour. The folder tree shows "
                "the colour while it is being picked; Cancel puts the previous "
                "one back.");

        d->resets[kPageTreeview] = PageReset{"Restore default colours", 170,
                [d]() {
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
        }};

        return parts.page;
    }

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

    // One "[caption] [slider] [value]" row of the PDF Inventory page,
    // indented under the choice it belongs to.
    std::shared_ptr<UltraCanvasContainer> MakeSliderRow(
            const std::string& id, const std::string& caption,
            const std::shared_ptr<UltraCanvasSlider>& slider,
            const std::shared_ptr<UltraCanvasLabel>& value) {
        auto row = std::make_shared<UltraCanvasContainer>(id);
        row->layout.SetFlexRow().SetFlexGap(10)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        row->size.width  = CSSLayout::Dimension::Px(kTextWidth);
        row->size.height = CSSLayout::Dimension::Px(32);
        row->SetPadding(0, 0, 0, 24);   // under the radio's text

        auto label = MakeLabel(id + "-label", caption);
        label->size.width  = CSSLayout::Dimension::Px(130);
        label->size.height = CSSLayout::Dimension::Px(20);
        row->AddChild(label);
        row->AddChild(slider);
        row->AddChild(value);
        return row;
    }

    // The width slider of one mode. Moving it selects that mode too, so the
    // slider a user reaches for is the one that takes effect rather than a
    // value nothing reads.
    std::shared_ptr<UltraCanvasSlider> MakePdfWidthSlider(
            const std::string& id, int minValue, int maxValue, int value,
            std::function<void(int)> onChange) {
        auto slider = CreateSlider(id, 0, 0, 200, 24);
        slider->SetRange(static_cast<float>(minValue),
                         static_cast<float>(maxValue));
        slider->SetStep(1.0f);
        slider->SetValue(static_cast<float>(value));
        slider->size.width  = CSSLayout::Dimension::Px(200);
        slider->size.height = CSSLayout::Dimension::Px(24);
        slider->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        // Reported both while the handle is dragged and when it is let go, so
        // the preview follows the slider instead of jumping at the end.
        slider->onValueChanging = [onChange](float v) {
            if (onChange) onChange(static_cast<int>(v + 0.5f));
        };
        slider->onValueChanged = [onChange](float v) {
            if (onChange) onChange(static_cast<int>(v + 0.5f));
        };
        return slider;
    }

    std::shared_ptr<UltraCanvasContainer> BuildPdfInventoryPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-pdf", "PDF Inventory",
                "Width of the page thumbnails in the preview's PDF page "
                "inventory, the strip beside the page:");

        const bool absolute = d->settings->pdfThumbnailAbsoluteWidth;

        d->pdfAbsoluteRadio = MakeChoice("ufl-set-pdf-absolute",
                "Fixed width", absolute);
        d->pdfRelativeRadio = MakeChoice("ufl-set-pdf-relative",
                "Relative to the preview's width", !absolute);
        d->pdfWidthGroup.AddRadioButton(d->pdfAbsoluteRadio);
        d->pdfWidthGroup.AddRadioButton(d->pdfRelativeRadio);
        d->pdfWidthGroup.onSelectionChanged =
                [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->settings) return;
            d->settings->pdfThumbnailAbsoluteWidth = (selected == d->pdfAbsoluteRadio);
            ApplyAndSave(d);
        };
        parts.body->AddChild(d->pdfAbsoluteRadio);

        d->pdfWidthValue = MakeLabel("ufl-set-pdf-width-value", "");
        d->pdfWidthValue->size.width  = CSSLayout::Dimension::Px(50);
        d->pdfWidthValue->size.height = CSSLayout::Dimension::Px(20);
        d->pdfWidthSlider = MakePdfWidthSlider("ufl-set-pdf-width",
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
                "Thumbnails width:", d->pdfWidthSlider, d->pdfWidthValue));

        parts.body->AddChild(d->pdfRelativeRadio);

        d->pdfPercentValue = MakeLabel("ufl-set-pdf-percent-value", "");
        d->pdfPercentValue->size.width  = CSSLayout::Dimension::Px(50);
        d->pdfPercentValue->size.height = CSSLayout::Dimension::Px(20);
        d->pdfPercentSlider = MakePdfWidthSlider("ufl-set-pdf-percent",
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
                "Share of the width:", d->pdfPercentSlider, d->pdfPercentValue));

        AddNote(parts, "ufl-set-pdf-note1",
                "Fixed width: the strip stays the same whatever the preview's "
                "size. Relative: it grows and shrinks with the window. Moving "
                "a slider selects its mode.");
        AddNote(parts, "ufl-set-pdf-note2",
                "The inventory only appears for documents with more than one "
                "page. It gives way when it would take more than half of a "
                "very narrow preview.");

        d->resets[kPagePdfInventory] = PageReset{"Restore default widths", 160,
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
                thumbnails ? "Thumbnails" : "Detail view",
                thumbnails
                ? "Which files the display draws a thumbnail of, rendered "
                  "from the file itself:"
                : "Which files the detail pane opens when one of them is "
                  "selected:",
                /*scrolls=*/true);
        parts.body->layout.SetFlexGap(4);

        AddNote(parts, idBase + "-note1", thumbnails
                ? "A kind switched off - or one format ticked off under it - "
                  "keeps its type glyph and is not read at all. Greyed "
                  "formats: nothing in this build renders them."
                : "A kind switched off - or one format ticked off under it - "
                  "leaves the whole width to the file display. Greyed "
                  "formats: this build has no view for them.");

        auto buttons = MakeButtonRow(idBase + "-buttons");
        buttons->AddChild(MakeButton(idBase + "-all-on", "Everything on", 120,
                [d, target]() {
            if (!d->settings) return;
            KindMask(d, target) = kFilerAllPreviewTypes;
            DisabledFormats(d, target).clear();
            RefreshFormatPage(d, target);
            ApplyAndSave(d);
        }));
        buttons->AddChild(MakeButton(idBase + "-all-off", "Everything off", 120,
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
                        : info.label + " - not supported by this build");
                entry.box->SetDisabled(!entry.supported ||
                        (mask & static_cast<uint32_t>(kind)) == 0);
                row->AddChild(entry.box);
                state.formatRows.push_back(entry);
                ++inRow;
            }
        }
        return parts.page;
    }

    // ===== DISPLAY > FILE EXTENSIONS =====
    // Two switches that belong together: whether a displayed name still ends
    // in ".exe", and what a thumbnail tile shows about the type instead - the
    // bar with the extension at its right end, the tag alone, or nothing.
    // Neither touches the file: the widget draws a shortened name, it does not
    // own a second one, so renaming and every file operation keep working on
    // the real name.
    const char* ExtensionBadgeDescription(FilerExtensionBadge badge) {
        switch (badge) {
            case FilerExtensionBadge::Bar:
                return "Bar - a strip across the foot of the icon, extension "
                       "at its right end";
            case FilerExtensionBadge::Icon:
                return "Icon - the extension alone, in the icon's bottom-right "
                       "corner";
            default:
                return "None - the tiles show the name only";
        }
    }

    std::shared_ptr<UltraCanvasContainer> BuildFileExtensionsPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-ext", "File extensions",
                "How the file display names a file and shows what type it is:");

        d->extensionsInNamesBox = MakeCheckbox("ufl-set-ext-in-names",
                "Show the file extension in the name", kTextWidth,
                d->settings->showFileExtensions, [d](bool on) {
            if (!d->settings) return;
            d->settings->showFileExtensions = on;
            ApplyAndSave(d);
        });
        parts.body->AddChild(d->extensionsInNamesBox);

        AddBodyCaption(parts, "ufl-set-ext-badge-caption",
                "In the thumbnail views, show the extension on the tile:");

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
                "With the extension hidden, \"UltraFiler.exe\" is listed as "
                "\"UltraFiler\". Only the drawn name changes - renaming, "
                "sorting and every file operation keep using the real name.");
        AddNote(parts, "ufl-set-ext-note2",
                "The tag is drawn over the foot of the icon, so no tile grows "
                "for it, and a folder - or a name whose tail is a version "
                "rather than a type - never gets one.");
        return parts.page;
    }

    // ===== DISPLAY > FILES IN USE =====
    std::shared_ptr<UltraCanvasContainer> BuildFilesInUsePage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-inuse", "Files in use",
                "Whether the file display marks files another program is "
                "holding open:");

        d->lockMarkingBox = MakeCheckbox("ufl-set-inuse-mark",
                "Mark files that are in use", kTextWidth,
                d->settings->showLockState, [d](bool on) {
            if (!d->settings) return;
            d->settings->showLockState = on;
            ApplyAndSave(d);
        });
        parts.body->AddChild(d->lockMarkingBox);

        if (!FileLockProbeAvailable()) {
            parts.body->AddChild(MakeText("ufl-set-inuse-unsupported",
                    "This system cannot be asked which program holds a file, "
                    "so nothing is marked here.",
                    kTextWidth, kTextFontSize, kNoteTextColor));
            d->lockMarkingBox->SetDisabled(true);
        }

        AddNote(parts, "ufl-set-inuse-note1",
                "A held file wears a padlock on its icon and an X among its "
                "attributes, and the info bar says so - which is the answer "
                "to a copy, a rename or a delete that fails with \"the file "
                "is open in another program\". The Attributes dialog names "
                "that program.");
        AddNote(parts, "ufl-set-inuse-note2",
                "A file merely open elsewhere - which on this kind of system "
                "blocks nothing - is marked O instead, and wears no padlock.");
        AddNote(parts, "ufl-set-inuse-note3",
                "Off, nothing is asked: each shown file costs one open, which "
                "is worth avoiding on a slow network volume.");
        return parts.page;
    }

    // ===== DISPLAY > HOME FOLDER =====
    std::shared_ptr<UltraCanvasContainer> BuildHomeFolderPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-home", "Home folder",
                "What the Home folder shows, in the folder tree and in the "
                "file display:");

        const bool predefined = d->settings->homeShowPredefinedOnly;

        d->homeAllRadio = MakeChoice("ufl-set-home-all",
                "Show all content", !predefined);
        d->homePredefinedRadio = MakeChoice("ufl-set-home-predefined",
                "Show only predefined folders", predefined);
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
                "Predefined folders: Desktop, Documents, Downloads, Music, "
                "Pictures, Videos - resolved through the platform, so a "
                "redirected or localized folder counts.");
        AddNote(parts, "ufl-set-home-note2",
                "Display > Hidden files in the file display always reveals "
                "everything.");
        return parts.page;
    }

    // ===== HANDLING > DRAG & DROP =====
    std::shared_ptr<UltraCanvasContainer> BuildDragDropPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-dragdrop", "Drag & Drop",
                "Drop on folder - what dragging files onto a folder of the "
                "file display does:");

        const bool copies = d->settings->dropOnFolderCopies;

        d->dropMoveRadio = MakeChoice("ufl-set-dd-move", "Move files", !copies);
        d->dropCopyRadio = MakeChoice("ufl-set-dd-copy", "Copy files", copies);
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

        AddBodyCaption(parts, "ufl-set-dd-confirm-caption",
                "Confirmation - whether a drop asks before it is carried out:");

        const FilerDropConfirmation confirm = d->settings->dropConfirmation;

        d->confirmAlwaysRadio = MakeChoice("ufl-set-dd-confirm-always",
                "Always", confirm == FilerDropConfirmation::AlwaysConfirm);
        d->confirmMoveRadio = MakeChoice("ufl-set-dd-confirm-move",
                "Only when files are moved",
                confirm == FilerDropConfirmation::MoveOnly);
        d->confirmNeverRadio = MakeChoice("ufl-set-dd-confirm-none",
                "None", confirm == FilerDropConfirmation::NeverConfirm);
        d->dropConfirmGroup.AddRadioButton(d->confirmAlwaysRadio);
        d->dropConfirmGroup.AddRadioButton(d->confirmMoveRadio);
        d->dropConfirmGroup.AddRadioButton(d->confirmNeverRadio);
        d->dropConfirmGroup.onSelectionChanged =
                [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->settings) return;
            d->settings->dropConfirmation =
                    selected == d->confirmAlwaysRadio
                            ? FilerDropConfirmation::AlwaysConfirm
                    : selected == d->confirmNeverRadio
                            ? FilerDropConfirmation::NeverConfirm
                            : FilerDropConfirmation::MoveOnly;
            ApplyAndSave(d);
        };
        parts.body->AddChild(d->confirmAlwaysRadio);
        parts.body->AddChild(d->confirmMoveRadio);
        parts.body->AddChild(d->confirmNeverRadio);

        AddNote(parts, "ufl-set-dd-note1",
                "Whatever is chosen here, Ctrl while dropping always copies "
                "and Shift always moves.");
        AddNote(parts, "ufl-set-dd-note2",
                "The question names what is about to happen - how many "
                "entries, moved or copied, and into which folder - and nothing "
                "is touched until it is answered. A drag is the one file "
                "operation that can be started by accident, which is why moves "
                "ask by default.");
        AddNote(parts, "ufl-set-dd-note3",
                "Files dragged in from another program are copied, so only "
                "\"Always\" asks about those.");
        return parts.page;
    }

    // ===== HANDLING > OPENING FILES =====
    std::shared_ptr<UltraCanvasContainer> BuildOpeningFilesPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-opening", "Opening files",
                "Double-click (or Enter) on a file - what it does when this "
                "system has a program registered for that kind of file:");

        const bool app = d->settings->doubleClickOpensRegisteredApp;

        d->openAppRadio = MakeChoice("ufl-set-open-app",
                "Start the registered program", app);
        d->openPreviewRadio = MakeChoice("ufl-set-open-preview",
                "Show it in the preview", !app);
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

        AddNote(parts, "ufl-set-open-note1",
                "\"Start the registered program\" is what a double-click does "
                "in Explorer and the Finder: the file opens in the program its "
                "type is assigned to, and UltraFiler stays as it is.");
        AddNote(parts, "ufl-set-open-note2",
                "\"Show it in the preview\" keeps the file inside UltraFiler "
                "whenever it can show it - pictures, documents, spreadsheets, "
                "3D models, e-books, fonts, video, audio and text - which is "
                "faster than starting a program for a look at a file.");
        AddNote(parts, "ufl-set-open-note3",
                "A file this system has no program for is shown in the preview "
                "either way, so this never turns a double-click into nothing "
                "happening. Whichever is chosen, the context menu's \"Open "
                "with\" still starts a program, and a file that cannot be "
                "previewed - a program, an installer, a file type UltraFiler "
                "does not read - always goes to the system.");
        return parts.page;
    }

    // ===== HANDLING > TABS =====
    std::shared_ptr<UltraCanvasContainer> BuildTabsPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-tabs", "Tabs",
                "New tab - what the \"+\" at the end of the tab strip opens:");

        const bool home = d->settings->newTabOpensHome;

        d->newTabCurrentRadio = MakeChoice("ufl-set-tab-current",
                "New view of the current folder", !home);
        d->newTabHomeRadio = MakeChoice("ufl-set-tab-home",
                "Open the Home folder", home);
        d->newTabGroup.AddRadioButton(d->newTabCurrentRadio);
        d->newTabGroup.AddRadioButton(d->newTabHomeRadio);
        d->newTabGroup.onSelectionChanged =
                [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->settings) return;
            d->settings->newTabOpensHome = (selected == d->newTabHomeRadio);
            ApplyAndSave(d);
        };
        parts.body->AddChild(d->newTabCurrentRadio);
        parts.body->AddChild(d->newTabHomeRadio);

        AddNote(parts, "ufl-set-tab-note1",
                "A new view of the current folder opens the tab on the folder "
                "the active tab is showing, so the work carries on where it "
                "is; the Home folder opens every new tab on the same starting "
                "point instead.");
        AddNote(parts, "ufl-set-tab-note2",
                "Only the \"+\" follows this. A tab opened on a named folder - "
                "the containing folder of a search result, an entry of the "
                "History or Favorites view - still opens on that folder.");
        return parts.page;
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
        PageParts parts = MakePage("ufl-set-page-prompt", "Open prompt",
                "Command line application started by Extras > Open prompt. It "
                "opens in the folder of the active tab:");

        // ----- application path + browse -----
        auto pathRow = std::make_shared<UltraCanvasContainer>("ufl-set-op-row");
        pathRow->layout.SetFlexRow().SetFlexGap(6)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        pathRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        pathRow->size.width  = CSSLayout::Dimension::Px(kTextWidth);
        pathRow->size.height = CSSLayout::Dimension::Px(32);

        d->promptInput = CreateTextInput("ufl-set-op-path", 0, 0, 400, 26);
        d->promptInput->SetFontSize(kTextFontSize);
        d->promptInput->SetPlaceholder("Path to the application");
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

        pathRow->AddChild(MakeButton("ufl-set-op-browse", "", 32,
                [d]() { BrowseForPromptApplication(d); }, "folder-open.svg"));
        parts.body->AddChild(pathRow);

        // ----- save / reset -----
        auto buttonRow = MakeButtonRow("ufl-set-op-buttons");
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
        parts.body->AddChild(buttonRow);

        // What will be started - feedback, so it stays with the controls.
        d->promptStatus = MakeText("ufl-set-op-status", "", kTextWidth,
                                   kTextFontSize, kNoteTextColor);
        parts.body->AddChild(d->promptStatus);
        UpdatePromptStatus(d);

        AddNote(parts, "ufl-set-op-note1",
                "Leave the field empty to use the command line program of "
                "this operating system.");
        AddNote(parts, "ufl-set-op-note2",
                "The folder button opens the file dialog filtered to "
                "applications. The choice only lands in the field - Save app "
                "(or Enter in the field) is what keeps it, and Test starts it "
                "right away.");
        return parts.page;
    }

    // ===== HISTORY & FAVORITES =====
    std::shared_ptr<UltraCanvasContainer> BuildListsPage(DialogState* d) {
        PageParts parts = MakePage("ufl-set-page-lists", "History & Favorites",
                "Clear the lists UltraFiler keeps:");

        auto buttonRow = MakeButtonRow("ufl-set-hf-buttons");

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
        parts.body->AddChild(buttonRow);

        // What was just cleared - feedback, so it stays with the buttons.
        d->listsStatus = MakeText("ufl-set-hf-status", "", kTextWidth,
                                  kTextFontSize, kNoteTextColor);
        parts.body->AddChild(d->listsStatus);

        AddNote(parts, "ufl-set-hf-note1",
                "The History view lists the recently used files, folders and "
                "applications; the Favorites view lists the pinned ones, "
                "including the tree's Pinned section.");
        AddNote(parts, "ufl-set-hf-note2",
                "Folder views are the view type and sort order each folder "
                "was last looked at with. Cleared, every folder opens with "
                "the current view again.");
        return parts.page;
    }

    // ===== THE PAGE SHOWN WHILE NOTHING IS SELECTED =====
    // The window opens with the three sections closed, so it opens on this
    // rather than on whichever page happened to be first.
    std::shared_ptr<UltraCanvasContainer> BuildStartPage(DialogState* /*d*/) {
        PageParts parts = MakePage("ufl-set-page-start", "Settings",
                "Open a section on the left and choose the page to set:");

        AddNote(parts, "ufl-set-start-note1",
                "Display - what the folder tree, the file display and the "
                "preview beside it show: colours, the Home folder, file "
                "extensions, files in use, and which file kinds get a "
                "thumbnail or a preview.");
        AddNote(parts, "ufl-set-start-note2",
                "Handling - what an action does: dropping dragged files onto "
                "a folder, and what the \"+\" of the tab strip opens.");
        AddNote(parts, "ufl-set-start-note3",
                "Extras - the command line program \"Open prompt\" starts, and "
                "clearing the recently-used lists, the pinned entries and the "
                "remembered folder views.");
        AddNote(parts, "ufl-set-start-note4",
                "Every change applies straight away and is saved; there is "
                "nothing to confirm.");
        return parts.page;
    }

    // ===== PAGE SWITCHING =====

    // The bottom bar's "Restore default ..." button stands for the shown
    // page: labelled and wired for that page, or hidden while the page has
    // nothing to restore.
    void UpdateRestoreButton(DialogState* d) {
        if (!d->restoreButton) return;
        auto it = d->resets.find(d->shownPage);
        if (it == d->resets.end()) {
            d->restoreButton->SetVisible(false);
            d->restoreButton->RequestRedraw();
            return;
        }
        const PageReset& reset = it->second;
        d->restoreButton->SetText(reset.label);
        d->restoreButton->size.width = CSSLayout::Dimension::Px(reset.width);
        d->restoreButton->SetVisible(true);
        d->restoreButton->RequestRedraw();
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
        d->shownPage = target;
        for (auto& [id, pageContainer] : d->pages)
            pageContainer->SetVisible(id == target);
        UpdateRestoreButton(d);
    }

    // Selects the tree row of `pageId` - and shows its page, through the
    // tree's selection callback.
    void SelectPage(DialogState* d, const std::string& pageId) {
        TreeNode* node = d->tree ? d->tree->FindNode(pageId) : nullptr;
        if (node) {
            // The sections start closed, so the one holding this page has to
            // be opened for its row to be on screen at all.
            for (TreeNode* parent = node->parent; parent; parent = parent->parent)
                d->tree->ExpandNode(parent);
            d->tree->SelectNode(node);
        }
        ShowPage(d, pageId);
    }

    void AddPage(DialogState* d, const char* pageId,
                 const std::shared_ptr<UltraCanvasContainer>& page) {
        page->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        d->pages[pageId] = page;
        d->pageArea->AddChild(page);
    }

    void AddTreeNode(DialogState* d, const char* parentId, const char* id,
                     const char* text) {
        TreeNodeData data;
        data.nodeId = id;
        data.text = text;
        d->tree->AddNode(parentId, data);
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
        d->tree->SetFontSize(kTreeFontSize);
        d->tree->SetRowHeight(24);
        d->tree->SetSelectionMode(TreeSelectionMode::Single);
        d->tree->SetLineStyle(TreeLineStyle::NoLine);
        d->tree->SetBackgroundColor(Color(243, 243, 246, 255));
        // A heading (Display, Handling, ...) has no page of its own - it shows
        // its first sub page - so selecting one moves straight on to that
        // first entry rather than leaving two rows that show the same thing.
        d->tree->SetShowFirstChildOnExpand(true);
        d->tree->SetAutoExpandSelectedNode(true);
        d->tree->size.width = CSSLayout::Dimension::Px(180);
        d->tree->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        // The root row itself is not shown: its children - Display, Handling,
        // Extras - are the top level of the list, so the tree opens on the
        // sections instead of on one row that holds them.
        TreeNodeData rootData;
        rootData.nodeId = "settings";
        rootData.text = "Settings";
        d->tree->SetRootNode(rootData);

        AddTreeNode(d, "settings", kPageDisplay, "Display");
        AddTreeNode(d, kPageDisplay, kPageTreeview, "Treeview");
        AddTreeNode(d, kPageDisplay, kPageHomeFolder, "Home folder");
        AddTreeNode(d, kPageDisplay, kPageFileExtensions, "File extensions");
        AddTreeNode(d, kPageDisplay, kPageFilesInUse, "Files in use");
        AddTreeNode(d, kPageDisplay, kPagePdfInventory, "PDF Inventory");
        AddTreeNode(d, kPageDisplay, kPageThumbnails, "Thumbnails");
        AddTreeNode(d, kPageDisplay, kPageDetailView, "Detail view");
        AddTreeNode(d, "settings", kPageHandling, "Handling");
        AddTreeNode(d, kPageHandling, kPageDragDrop, "Drag & Drop");
        AddTreeNode(d, kPageHandling, kPageOpeningFiles, "Opening files");
        AddTreeNode(d, kPageHandling, kPageTabs, "Tabs");
        AddTreeNode(d, "settings", kPageExtras, "Extras");
        AddTreeNode(d, kPageExtras, kPageOpenPrompt, "Open prompt");
        AddTreeNode(d, kPageExtras, kPageLists, "History & Favorites");

        // After the nodes: hiding the root promotes its children to the top
        // level, which it can only do once they exist. The sections themselves
        // stay closed - the window opens on the list of sections, and opening
        // one is the first step of finding a setting.
        d->tree->SetRootVisible(false);
        content->AddChild(d->tree);

        d->pageArea = std::make_shared<UltraCanvasContainer>("ufl-set-pages");
        d->pageArea->layout.SetFlexColumn()
                           .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        d->pageArea->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                               .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        d->pageArea->SetBackgroundColor(Color(255, 255, 255, 255));
        content->AddChild(d->pageArea);

        // ----- pages -----
        AddPage(d, kPageTreeview, BuildTreeviewPage(d));
        AddPage(d, kPageHomeFolder, BuildHomeFolderPage(d));
        AddPage(d, kPageFileExtensions, BuildFileExtensionsPage(d));
        AddPage(d, kPageFilesInUse, BuildFilesInUsePage(d));
        AddPage(d, kPagePdfInventory, BuildPdfInventoryPage(d));
        AddPage(d, kPageThumbnails,
                BuildFormatSwitchPage(d, FilerPreviewTarget::Thumbnails));
        AddPage(d, kPageDetailView,
                BuildFormatSwitchPage(d, FilerPreviewTarget::DetailView));
        AddPage(d, kPageDragDrop, BuildDragDropPage(d));
        AddPage(d, kPageOpeningFiles, BuildOpeningFilesPage(d));
        AddPage(d, kPageTabs, BuildTabsPage(d));
        AddPage(d, kPageOpenPrompt, BuildOpenPromptPage(d));
        AddPage(d, kPageLists, BuildListsPage(d));
        AddPage(d, kPageStart, BuildStartPage(d));

        d->window->AddChild(content);

        // ----- bottom bar: [Restore default ...]            [Close] -----
        auto bottom = std::make_shared<UltraCanvasContainer>("ufl-set-bottom");
        bottom->layout.SetFlexRow().SetFlexGap(8)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        bottom->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        bottom->SetPadding(8, 12, 8, 12);
        bottom->SetBorderTop(1, Color(225, 225, 230, 255));

        // The left half holds the page's restore button and takes the width
        // the Close button leaves, so Close keeps its right-hand place whether
        // or not a restore button is showing.
        auto bottomLeft = std::make_shared<UltraCanvasContainer>("ufl-set-bottom-left");
        bottomLeft->layout.SetFlexRow()
                          .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        bottomLeft->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                              .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        d->restoreButton = MakeButton("ufl-set-restore", "Restore defaults", 170,
                [d]() {
            auto it = d->resets.find(d->shownPage);
            if (it != d->resets.end() && it->second.action) it->second.action();
        });
        d->restoreButton->SetVisible(false);
        bottomLeft->AddChild(d->restoreButton);
        bottom->AddChild(bottomLeft);

        bottom->AddChild(MakeButton("ufl-set-close", "Close", 90,
                [d]() { if (d->window) d->window->Close(); }));
        d->window->AddChild(bottom);

        d->tree->onNodeSelected = [d](TreeNode* node) {
            if (!node) return;
            // A section (Display, Handling, Extras) has no page of its own:
            // selecting one moves on to its first page. The tree jumps one
            // level per selection, so this takes the remaining steps.
            TreeNode* leaf = node;
            while (leaf && leaf->HasChildren()) leaf = leaf->FirstChild();
            if (leaf && leaf != node) {
                d->tree->SelectNode(leaf);
                return;
            }
            ShowPage(d, node->data.nodeId);
        };
        // Nothing is selected while every section is closed, so the page
        // area opens on the start page. Show() selects a page from here when
        // the caller asked for one.
        ShowPage(d, kPageStart);

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

        d->window->Show();
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
    state->onChanged = std::move(onChanged);
    state->onClearHistory = std::move(onClearHistory);
    state->onClearFavorites = std::move(onClearFavorites);
    state->onClearFolderViews = std::move(onClearFolderViews);
    BuildDialog(state.get(), parent);
    if (!state->window) return;
    g_dialog = state;   // keeps the widgets alive
    if (pageId) SelectPage(state.get(), pageId);
}

void UltraFilerSettingsDialog::Shutdown() {
    g_dialog.reset();
}

} // namespace UltraCanvas
