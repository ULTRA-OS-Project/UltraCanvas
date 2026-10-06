// core/UltraCanvasModalDialog.cpp
// Implementation of cross-platform modal dialog system - Window-based
// Supports switching between native OS dialogs and internal UltraCanvas dialogs
// Version: 3.6.0
// Last Modified: 2026-08-23
// Author: UltraCanvas Framework

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"          // GetResourcesDir
#include "UltraCanvasUtils.h"           // NormalizePath, GetWellKnownUserFolders
#include "UltraCanvasVolumeMonitor.h"   // ListMountedVolumes
#include "UltraCanvasFilerWidget.h"     // the file dialog's listing
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasFileDialogSettings.h"
#include <cstdlib>
#include <fstream>
#include <fmt/os.h>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

// ===== STATIC MEMBER DEFINITIONS =====
    std::vector<std::shared_ptr<UltraCanvasModalDialog>> UltraCanvasDialogManager::activeDialogs;
    bool UltraCanvasDialogManager::enabled = true;
    bool UltraCanvasDialogManager::useNativeDialogs = false;  // Default to internal dialogs
    DialogConfig UltraCanvasDialogManager::defaultConfig;
    InputDialogConfig UltraCanvasDialogManager::defaultInputConfig;
    FileDialogConfig UltraCanvasDialogManager::defaultFileConfig;

// ===== MODAL DIALOG IMPLEMENTATION =====

    void UltraCanvasModalDialog::CreateDialog(const DialogConfig& config) {
        dialogConfig = config;
        UltraCanvasWindow::Create(dialogConfig);
        ApplyTypeDefaults();

        // Build layout-based UI structure
        if (dialogConfig.dialogType != DialogType::Custom) {
            BuildDialogLayout();
        }
    }

    void UltraCanvasModalDialog::BuildDialogLayout() {
        // Window: vertical flex; content stretches, footer is fixed height.
        this->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        CreateContentSection();
        CreateFooterSection();

        // Content grows to fill; footer is fixed.
        contentSection->layoutItem.SetFlexGrow(1);

        WireButtonCallbacks();
    }

    void UltraCanvasModalDialog::CreateContentSection() {
        contentSection = std::make_shared<UltraCanvasContainer>(
                "ContentSection");
        contentSection->SetBackgroundColor(dialogConfig.backgroundColor);
        contentSection->SetPadding(static_cast<int>(style.padding));

        // Row flex for icon + message.
        contentSection->layout.SetFlexRow()
                              .SetFlexGap(style.iconMessageSpacing)
                              .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        // ===== ICON CONTAINER =====
        iconContainer = std::make_shared<UltraCanvasContainer>(
                "IconContainer", 0, 0,
                style.iconSize, style.iconSize);
        iconContainer->SetBackgroundColor(GetTypeColor());

        // Center the icon label inside.
        iconContainer->layout.SetFlexColumn()
                .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
                             .SetFlexAlignItems(CSSLayout::AlignItems::Center);

        iconLabel = std::make_shared<UltraCanvasLabel>("IconLabel");
        iconLabel->SetText(GetTypeIcon());
        iconLabel->SetFontSize(style.iconFontSize);
        iconLabel->SetFontWeight(FontWeight::Bold);
        iconLabel->SetTextColor(Colors::White);
        iconLabel->SetAlignment(TextAlignment::Center);
        iconLabel->SetSize(style.iconSize, style.iconSize);

        iconContainer->AddChild(iconLabel);
        contentSection->AddChild(iconContainer);
        iconContainer->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Start);
        // Apply the configured icon visibility. Hiding is display:none, not
        // merely invisible, so the icon reserves neither a column nor the flex
        // gap after it: the message — and anything AddDialogElement() puts in
        // it — gets the full content width and centres in the window.
        UpdateIconAppearance();

        // ===== MESSAGE CONTAINER =====
        messageContainer = std::make_shared<UltraCanvasContainer>(
                "MessageContainer");
        // The message area supplies its own scrollbar, so the container that
        // holds it must never raise a second one of its own.
        {
            ContainerStyle mcStyle = messageContainer->GetContainerStyle();
            mcStyle.autoShowScrollbars = false;
            mcStyle.forceShowVerticalScrollbar = false;
            mcStyle.forceShowHorizontalScrollbar = false;
            messageContainer->SetContainerStyle(mcStyle);
        }

        messageContainer->layout.SetFlexColumn()
                                .SetFlexGap(style.sectionSpacing / 2)
                                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        // Read-only, word-wrapping Markdown message area. It fills the content
        // box (flex-grow) so that when the dialog is capped to the monitor the
        // text taller than the box scrolls inside the area itself.
        messageArea = std::make_shared<UltraCanvasTextArea>("MessageArea");
        messageArea->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        messageArea->SetReadOnly(true);
        messageArea->SetWordWrap(true);
        messageArea->SetShowLineNumbers(false);
        messageArea->SetHighlightCurrentLine(false);
        messageArea->SetBackgroundColor(dialogConfig.backgroundColor);
        messageArea->SetFontSize(style.messageFontSize);
        messageArea->SetTextColor(style.messageTextColor);
        messageArea->SetText(ComposeMessageMarkdown(), false);
        messageContainer->AddChild(messageArea);
        messageArea->layoutItem.SetFlexGrow(1);

        contentSection->AddChild(messageContainer);
        messageContainer->layoutItem.SetFlexGrow(1);

        AddChild(contentSection);
    }

    // Combine the plain message and the optional details line into a single
    // Markdown source. Both are already rendered as Markdown, so existing
    // plain-text callers are unaffected; details are separated by a blank line.
    std::string UltraCanvasModalDialog::ComposeMessageMarkdown() const {
        std::string md = dialogConfig.message;
        if (!dialogConfig.details.empty()) {
            if (!md.empty()) md += "\n\n";
            md += dialogConfig.details;
        }
        return md;
    }

    void UltraCanvasModalDialog::CreateFooterSection() {
        footerSection = std::make_shared<UltraCanvasContainer>(
                "FooterSection", 0, 0, 0, style.buttonAreaHeight);
        footerSection->SetBackgroundColor(dialogConfig.backgroundColor);
        // SetPadding(vertical, horizontal): the button row is only
        // buttonAreaHeight (50px) tall, so vertical padding must be the SMALL
        // value — the taller value belongs on the horizontal axis. Passing the
        // large value as vertical padding left <buttonHeight of content height,
        // pushing the button out of the footer's content box and triggering a
        // spurious vertical scrollbar in the button area.
        footerSection->SetPadding(static_cast<int>(style.padding / 2),
                                  static_cast<int>(style.padding));

        // The footer is a fixed button bar, never a scroll region: keep its
        // scrollbars off so no residual overflow can raise one (same convention
        // as UltraCanvasToolbar).
        ContainerStyle footerStyle = footerSection->GetContainerStyle();
        footerStyle.autoShowScrollbars = false;
        footerStyle.forceShowVerticalScrollbar = false;
        footerStyle.forceShowHorizontalScrollbar = false;
        footerSection->SetContainerStyle(footerStyle);

        footerSection->layout.SetFlexRow()
                .SetFlexGap(style.buttonSpacing)
                .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
                             .SetFlexAlignItems(CSSLayout::AlignItems::Center);

        CreateDialogButtons();
        for (auto& entry : dialogButtons) {
            footerSection->AddChild(entry.button);
        }
        AddChild(footerSection);
    }

    // A footer button is as wide as its label needs, never narrower than the
    // configured button width. The fixed width alone ellipsized every label
    // longer than "Cancel" — "Continue" reached the user as "Conti…".
    void UltraCanvasModalDialog::SizeButtonToLabel(
            const std::shared_ptr<UltraCanvasButton>& button) {
        if (!button) return;
        button->size.width = CSSLayout::Dimension::Auto();
        CSSLayout::BoxConstraints limits =
                button->boxConstraints.value_or(CSSLayout::BoxConstraints{});
        limits.minWidth = CSSLayout::Dimension::Px(style.buttonWidth);
        button->boxConstraints = limits;
        button->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    }

    void UltraCanvasModalDialog::ApplyButtonFont(
            const std::shared_ptr<UltraCanvasButton>& button) {
        if (!button || style.buttonFontSize <= 0.0f) return;
        button->SetFontSize(style.buttonFontSize);
    }

    void UltraCanvasModalDialog::CreateDialogButtons() {
        // Clear existing buttons
        dialogButtons.clear();

        int buttonMask = static_cast<int>(dialogConfig.buttons);

        auto addButton = [this](DialogButton btn, const std::string& text) {
            auto button = std::make_shared<UltraCanvasButton>(
                    fmt::format("DialogBtn_{}", static_cast<int>(btn)), 0, 0,
                    static_cast<long>(style.buttonWidth), static_cast<long>(style.buttonHeight));
            button->SetText(text);
            SizeButtonToLabel(button);
            ApplyButtonFont(button);
            dialogButtons.push_back(DialogButtonEntry{button, btn, ButtonToResult(btn)});
        };

        if (buttonMask & static_cast<int>(DialogButton::OK)) {
            addButton(DialogButton::OK, "OK");
        }
        if (buttonMask & static_cast<int>(DialogButton::Cancel)) {
            addButton(DialogButton::Cancel, "Cancel");
        }
        if (buttonMask & static_cast<int>(DialogButton::Yes)) {
            addButton(DialogButton::Yes, "Yes");
        }
        if (buttonMask & static_cast<int>(DialogButton::No)) {
            addButton(DialogButton::No, "No");
        }
        if (buttonMask & static_cast<int>(DialogButton::Retry)) {
            addButton(DialogButton::Retry, "Retry");
        }
        if (buttonMask & static_cast<int>(DialogButton::Abort)) {
            addButton(DialogButton::Abort, "Abort");
        }
        if (buttonMask & static_cast<int>(DialogButton::Ignore)) {
            addButton(DialogButton::Ignore, "Ignore");
        }

        AssignButtonMnemonics();
    }

    void UltraCanvasModalDialog::WireButtonCallbacks() {
        for (auto& entry : dialogButtons) {
            DialogButton btnType = entry.type;
            entry.button->onClick = [this, btnType]() {
                OnDialogButtonClick(btnType);
            };
        }
    }

// ===== MNEMONICS =====
    void UltraCanvasModalDialog::AssignButtonMnemonics() {
        for (auto& entry : dialogButtons) {
            entry.mnemonic = 0;
            entry.button->SetMnemonicIndex(-1);
        }
        if (!dialogConfig.useButtonMnemonics) return;

        std::vector<char> taken;
        auto isTaken = [&taken](char c) {
            return std::find(taken.begin(), taken.end(), c) != taken.end();
        };

        for (auto& entry : dialogButtons) {
            const std::string& label = entry.button->GetText();

            // Prefer the label's own initial ("OK" -> O, "Cancel" -> C), then
            // the initial of any later word, then any remaining letter. The
            // standard button sets never collide on the first pass; the later
            // passes only matter for custom labels ("Cancel" + "Close").
            std::vector<size_t> candidates;
            for (size_t i = 0; i < label.size(); ++i) {
                unsigned char ch = static_cast<unsigned char>(label[i]);
                if (ch >= 0x80 || !std::isalnum(ch)) continue;
                bool wordStart = (i == 0) || !std::isalnum(static_cast<unsigned char>(label[i - 1]));
                if (wordStart) candidates.push_back(i);
            }
            for (size_t i = 0; i < label.size(); ++i) {
                unsigned char ch = static_cast<unsigned char>(label[i]);
                if (ch >= 0x80 || !std::isalnum(ch)) continue;
                if (std::find(candidates.begin(), candidates.end(), i) == candidates.end()) {
                    candidates.push_back(i);
                }
            }

            for (size_t index : candidates) {
                char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(label[index])));
                if (isTaken(letter)) continue;
                entry.button->SetMnemonicIndex(static_cast<int>(index));
                entry.mnemonic = entry.button->GetMnemonicChar();
                if (entry.mnemonic) taken.push_back(entry.mnemonic);
                break;
            }
        }
    }

    UltraCanvasModalDialog::DialogButtonEntry* UltraCanvasModalDialog::FindButtonByMnemonic(char letter) {
        unsigned char wanted = static_cast<unsigned char>(letter);
        if (wanted >= 0x80 || !std::isalnum(wanted)) return nullptr;
        char upper = static_cast<char>(std::toupper(wanted));

        for (auto& entry : dialogButtons) {
            if (entry.mnemonic && entry.mnemonic == upper) return &entry;
        }
        return nullptr;
    }

    UltraCanvasModalDialog::DialogButtonEntry* UltraCanvasModalDialog::FindButtonEntry(DialogButton button) {
        // NoneButton is what custom buttons carry, so it must never match.
        if (button == DialogButton::NoneButton) return nullptr;
        for (auto& entry : dialogButtons) {
            if (entry.type == button) return &entry;
        }
        return nullptr;
    }

    const UltraCanvasModalDialog::DialogButtonEntry* UltraCanvasModalDialog::FindButtonEntry(DialogButton button) const {
        if (button == DialogButton::NoneButton) return nullptr;
        for (const auto& entry : dialogButtons) {
            if (entry.type == button) return &entry;
        }
        return nullptr;
    }

    UltraCanvasModalDialog::DialogButtonEntry* UltraCanvasModalDialog::FindDefaultButtonEntry() {
        auto usable = [](DialogButtonEntry* entry) {
            return entry && entry->button->IsVisible() && !entry->button->IsDisabled();
        };

        // A custom button given the Default role is the answer Return means,
        // whatever the configured default says (which cannot name it).
        for (auto& candidate : dialogButtons) {
            if ((candidate.role == DialogButtonRole::Default ||
                 candidate.role == DialogButtonRole::DestructiveDefault) &&
                usable(&candidate))
                return &candidate;
        }

        auto* entry = FindButtonEntry(dialogConfig.defaultButton);
        if (usable(entry)) return entry;

        // The configured default (OK by default) is not on this dialog, so fall
        // back to whichever affirmative button it does carry — Enter on a
        // Yes/No/Cancel question should mean Yes, not the first button in the
        // row, which is Cancel.
        for (DialogButton candidate : {DialogButton::OK, DialogButton::Yes,
                                       DialogButton::Retry, DialogButton::Apply,
                                       DialogButton::Close}) {
            auto* fallback = FindButtonEntry(candidate);
            if (usable(fallback)) return fallback;
        }

        for (auto& candidate : dialogButtons) {
            if (usable(&candidate)) return &candidate;
        }
        return nullptr;
    }

    void UltraCanvasModalDialog::ActivateButtonEntry(DialogButtonEntry& entry) {
        if (entry.button->onClick) {
            entry.button->onClick();
        } else {
            CloseDialog(entry.result);
        }
    }

    UltraCanvasModalDialog::DialogButtonEntry* UltraCanvasModalDialog::FindCancelButtonEntry() {
        if (auto* configured = FindButtonEntry(dialogConfig.cancelButton)) return configured;
        // A custom button given the Cancel role ("Stop", "Cancel").
        for (auto& candidate : dialogButtons)
            if (candidate.role == DialogButtonRole::Cancel) return &candidate;

        // No explicit cancel button in this dialog; fall back to whichever
        // dismissive button it does carry so Escape maps to a real action.
        for (DialogButton candidate : {DialogButton::Cancel, DialogButton::No,
                                       DialogButton::Close, DialogButton::Abort,
                                       DialogButton::Ignore}) {
            if (auto* entry = FindButtonEntry(candidate)) return entry;
        }
        return nullptr;
    }

    bool UltraCanvasModalDialog::SetButtonMnemonic(DialogButton button, char letter) {
        auto* entry = FindButtonEntry(button);
        if (!entry || !entry->button->SetMnemonicChar(letter)) return false;
        entry->mnemonic = entry->button->GetMnemonicChar();
        return entry->mnemonic != 0;
    }

    char UltraCanvasModalDialog::GetButtonMnemonic(DialogButton button) const {
        const auto* entry = FindButtonEntry(button);
        return entry ? entry->mnemonic : 0;
    }

    bool UltraCanvasModalDialog::ActivateButton(DialogButton button) {
        auto* entry = FindButtonEntry(button);
        if (!entry || !entry->button->IsVisible() || entry->button->IsDisabled()) return false;

        ActivateButtonEntry(*entry);
        return true;
    }

    void UltraCanvasModalDialog::SetDialogTitle(const std::string& title) {
        dialogConfig.title = title;
        SetWindowTitle(title);
    }

    void UltraCanvasModalDialog::SetMessage(const std::string& message) {
        dialogConfig.message = message;
        if (messageArea) {
            messageArea->SetText(ComposeMessageMarkdown(), false);
        }
    }

    void UltraCanvasModalDialog::SetDetails(const std::string& details) {
        dialogConfig.details = details;
        if (messageArea) {
            messageArea->SetText(ComposeMessageMarkdown(), false);
        }
    }

    void UltraCanvasModalDialog::SetDialogType(DialogType type) {
        dialogConfig.dialogType = type;
        UpdateIconAppearance();
        ApplyTypeDefaults();
    }

    void UltraCanvasModalDialog::SetIconVisible(bool visible) {
        if (dialogConfig.showIcon == visible) return;
        dialogConfig.showIcon = visible;
        UpdateIconAppearance();
        InvalidateLayout();
    }

    bool UltraCanvasModalDialog::IsIconVisible() const {
        return dialogConfig.showIcon &&
               dialogConfig.dialogType != DialogType::Custom;
    }

    void UltraCanvasModalDialog::SetDialogButtons(DialogButtons buttons) {
        dialogConfig.buttons = buttons;

        // Remove old buttons from footer
        if (footerSection) {
            for (auto& entry : dialogButtons) {
                footerSection->RemoveChild(entry.button);
            }
        }

        // Recreate buttons
        CreateDialogButtons();

        if (footerSection) {
            footerSection->ClearChildren();
            footerSection->layout.SetFlexRow()
                                 .SetFlexGap(style.buttonSpacing)
                                 .SetFlexAlignItems(CSSLayout::AlignItems::Center);
            footerSection->AddStretchSpacer(1);
            for (auto& entry : dialogButtons) {
                footerSection->AddChild(entry.button);
            }
        }

        WireButtonCallbacks();
    }

    void UltraCanvasModalDialog::SetDefaultButton(DialogButton button) {
        dialogConfig.defaultButton = button;
    }

    void UltraCanvasModalDialog::SetStyle(const ModalDialogStyle& dialogStyle) {
        style = dialogStyle;

        // Apply style to components
        if (messageArea) {
            messageArea->SetFontSize(style.messageFontSize);
            messageArea->SetTextColor(style.messageTextColor);
        }
        for (auto& entry : dialogButtons) ApplyButtonFont(entry.button);

        UpdateIconAppearance();
    }

    std::string UltraCanvasModalDialog::GetDialogTitle() const {
        return dialogConfig.title;
    }

    std::string UltraCanvasModalDialog::GetMessage() const {
        return dialogConfig.message;
    }

    std::string UltraCanvasModalDialog::GetDetails() const {
        return dialogConfig.details;
    }

    DialogType UltraCanvasModalDialog::GetDialogType() const {
        return dialogConfig.dialogType;
    }

    DialogButtons UltraCanvasModalDialog::GetDialogButtons() const {
        return dialogConfig.buttons;
    }

    DialogButton UltraCanvasModalDialog::GetDefaultButton() const {
        return dialogConfig.defaultButton;
    }

    ModalDialogStyle UltraCanvasModalDialog::GetStyle() const {
        return style;
    }

    void UltraCanvasModalDialog::ShowModal(UltraCanvasWindowBase* parent) {
        // Resolve the window the dialog belongs to. When the caller passes no
        // parent, fall back to the application's focused window, then to any
        // visible non-dialog window, so the dialog always opens on the monitor
        // the application is running on instead of wherever the window manager
        // decides to place it.
        UltraCanvasWindowBase* reference = parent;
        auto* app = UltraCanvasApplication::GetInstance();
        if (!reference && app) {
            reference = app->GetFocusedWindow();
            if (!reference) {
                for (auto& win : app->GetWindows()) {
                    if (win && win.get() != this && win->IsWindowVisible() &&
                        win->GetConfig().type != WindowType::Dialog) {
                        reference = win.get();
                        break;
                    }
                }
            }
        }
        if (reference == this) reference = nullptr;

        if (reference) {
            // Lets the window manager keep the dialog above its parent and
            // treat it as belonging to the parent's monitor.
            SetTransientParent(reference);
        }

        // Fit the dialog height to the message before we position it, so the
        // centering below uses the final size.
        AutoSizeToContent();

        switch (dialogConfig.position) {
            case DialogPosition::CenterParent:
                // Centered over the reference window, clamped to its monitor;
                // falls back to centering on the screen when there is none.
                CenterOnParent(reference);
                break;
            case DialogPosition::Center:
                // Centered on the monitor the reference window resides on.
                CenterOnScreenOfWindow(reference);
                break;
            default:
                break;
        }

        // Register with dialog manager
        UltraCanvasDialogManager::RegisterDialog(
                std::dynamic_pointer_cast<UltraCanvasModalDialog>(shared_from_this()));

        // Whatever was still held down belongs to the action that opened the
        // dialog, not to the dialog. Remember those keys so their remaining
        // events (including the KeyUp that would otherwise click the freshly
        // focused default button) are dropped instead of delivered.
        pendingHeldKeys.clear();
        if (dialogConfig.clearPendingInput && app) {
            pendingHeldKeys = app->GetPressedKeys();
        }
        InstallKeyboardHandling();

        // Show the window
        Show();

        FocusInitialElement();
    }

    void UltraCanvasModalDialog::AutoSizeToContent() {
        if (!autoSizeHeight || !messageArea || !messageContainer) return;
        auto* ctx = GetRenderContext();
        if (!ctx) return;  // headless / creation failed — keep the configured size

        // Run one layout pass at the current size so the message area receives a
        // definite content width; its wrapped/Markdown height is meaningless
        // until then. Nothing is drawn here.
        auto layoutPass = [this]() {
            CSSLayout::LayoutContext lctx;
            lctx.viewportWidth  = GetWidth();
            lctx.viewportHeight = GetHeight();
            CSSLayout::MeasureConstraints mc{
                    { CSSLayout::ConstraintMode::Exact, static_cast<float>(GetWidth())  },
                    { CSSLayout::ConstraintMode::Exact, static_cast<float>(GetHeight()) }
            };
            Measure(mc, lctx);
            Arrange(finalBounds, lctx);
        };
        layoutPass();

        // The footer first: its elements do not shrink, so a checkbox and
        // four labelled buttons that together need more than the configured
        // width run off the right edge, and the last button is simply gone.
        // Widen the window to what the row needs (up to most of the monitor)
        // and lay out again, since the message wraps at the new width.
        int screenW = 0, screenH = 0;
        GetScreenSize(screenW, screenH);
        {
            int desiredWidth = static_cast<int>(std::ceil(FooterContentWidth()));
            if (screenW > 0) desiredWidth = std::min(desiredWidth,
                                                     static_cast<int>(screenW * 0.9f));
            if (desiredWidth > static_cast<int>(GetWidth())) {
                SetWindowSize(desiredWidth, static_cast<int>(GetHeight()));
                InvalidateLayout();
                layoutPass();
            }
        }

        float textHeight = messageArea->MeasureContentHeight();

        // Everything AddDialogElement() put below the message — switches, radio
        // rows, an input label — sits in the same column and needs its own room.
        // Without counting it the dialog is sized for the text alone and the
        // message area, being the one flexible child, is squeezed to a sliver:
        // the caller's message and details then simply are not readable.
        float extrasHeight = 0.0f;
        int   extrasCount  = 0;
        for (const auto& child : messageContainer->GetChildren()) {
            if (!child || child.get() == messageArea.get()) continue;
            if (!child->IsVisible()) continue;
            extrasHeight += child->GetHeight();
            ++extrasCount;
        }
        if (extrasCount > 0)
            extrasHeight += 0.5f * style.sectionSpacing * static_cast<float>(extrasCount);

        // Desired client height = content padding + the taller of the icon and
        // the text (plus the extra elements) + the button bar. textPadding (5px)
        // is baked into the area's own content box, so add a little slack so the
        // last line clears it.
        // The icon only sets a floor for the content block while it is shown;
        // an icon-less dialog is as tall as its own content needs.
        const float iconBlock = IsIconVisible() ? style.iconSize : 0.0f;
        float contentBlock = std::max(iconBlock,
                                      textHeight + 10.0f + extrasHeight);
        int desired = static_cast<int>(std::ceil(
                2.0f * style.padding + contentBlock + style.buttonAreaHeight));

        // Clamp between the configured minimum and (most of) the monitor. Past
        // the cap the message area scrolls instead of the window growing.
        int minH = std::max(dialogConfig.minHeight,
                            static_cast<int>(2.0f * style.padding + iconBlock + style.buttonAreaHeight));
        int capH = desired;
        if (screenH > 0) capH = static_cast<int>(screenH * 0.85f);
        if (dialogConfig.maxHeight > 0) capH = std::min(capH, dialogConfig.maxHeight);
        capH = std::max(capH, minH);

        int newHeight = std::clamp(desired, minH, capH);
        if (newHeight != static_cast<int>(GetHeight())) {
            // Updates config_ + native size + flags a resize; the next render
            // re-arranges everything (and the area scrolls if it was capped).
            SetWindowSize(static_cast<int>(GetWidth()), newHeight);
            // The manual Measure/Arrange above validated layout at the old size;
            // force a fresh pass so the new height propagates to the sections.
            InvalidateLayout();
        }
    }

    float UltraCanvasModalDialog::FooterContentWidth() const {
        if (!footerSection) return 0.0f;
        // The row as laid out: each element at the width it measured itself
        // to (a label-sized button, an explicitly sized checkbox), the
        // spacer at whatever it got - zero once the row overflows - and the
        // footer's own horizontal padding on both sides.
        float width = 2.0f * style.padding;
        int visible = 0;
        for (const auto& child : footerSection->GetChildren()) {
            if (!child || !child->IsVisible()) continue;
            if (child.get() == footerSpacer.get()) continue;
            width += child->GetWidth();
            ++visible;
        }
        if (visible > 1) width += style.buttonSpacing * static_cast<float>(visible - 1);
        if (footerSpacer && footerSpacer->IsVisible()) width += style.buttonSpacing;
        return width;
    }

    void UltraCanvasModalDialog::PerformClose() {
        // Unregister from dialog manager
        UltraCanvasDialogManager::UnregisterDialog(
                std::dynamic_pointer_cast<UltraCanvasModalDialog>(shared_from_this()));

        ResetKeyboardState();

        // The dialog was usually dismissed by a key that is still physically
        // down. Forgetting the keyboard state stops the window that regains
        // focus from treating that key as held, and stops its release from
        // being read as a keystroke aimed at it.
        if (dialogConfig.clearPendingInput) {
            if (auto* app = UltraCanvasApplication::GetInstance()) {
                app->ClearKeyboardState();
            }
        }

        UltraCanvasWindow::PerformClose();

        if (onResult) {
            onResult(result);
        }
    }

    void UltraCanvasModalDialog::CloseDialog(DialogResult dialogResult) {
        result = dialogResult;
        Close();
    }

    bool UltraCanvasModalDialog::OnEvent(const UCEvent& event) {
        // Reached only after the focused element declined the key, so a focused
        // button still owns Return/Space and a multiline field still owns Return.
        if (event.type == UCEventType::KeyDown) {
            if (dialogConfig.closeOnEscape && event.virtualKey == UCKeys::Escape) {
                // Go through the dialog's own cancel button when it has one, so
                // the result and any handler match what clicking it would do.
                if (auto* entry = FindCancelButtonEntry()) {
                    ActivateButtonEntry(*entry);
                } else {
                    CloseDialog(DialogResult::Cancel);
                }
                return true;
            }

            if (dialogConfig.activateDefaultOnEnter &&
                (event.virtualKey == UCKeys::Return || event.virtualKey == UCKeys::Enter) &&
                !event.ctrl && !event.alt && !event.meta) {
                if (auto* entry = FindDefaultButtonEntry()) {
                    ActivateButtonEntry(*entry);
                    return true;
                }
            }
        }
        return UltraCanvasWindow::OnEvent(event);
    }

// ===== KEYBOARD HANDLING =====
    void UltraCanvasModalDialog::InstallKeyboardHandling() {
        if (!keyboardFilterId.empty()) return;

        keyboardFilterId = fmt::format("UltraCanvasModalDialogKeys_{}", static_cast<const void*>(this));
        InstallEventFilter(keyboardFilterId,
                           [this](const UCEvent& event) { return HandleDialogKeyEvent(event); },
                           {UCEventType::KeyDown, UCEventType::KeyUp, UCEventType::TextInput});
    }

    void UltraCanvasModalDialog::ResetKeyboardState() {
        pendingHeldKeys.clear();
    }

    bool UltraCanvasModalDialog::ShouldSwallowStaleInput(const UCEvent& event) {
        if (pendingHeldKeys.empty()) return false;

        auto it = std::find(pendingHeldKeys.begin(), pendingHeldKeys.end(), event.virtualKey);
        if (it == pendingHeldKeys.end()) {
            // A key the user pressed after the dialog appeared: real input, and
            // proof that the earlier keys are no longer part of this gesture.
            if (event.type == UCEventType::KeyDown) pendingHeldKeys.clear();
            return false;
        }

        // Releasing the key ends the gesture that was in flight; the next press
        // of it is genuine input for this dialog.
        if (event.type == UCEventType::KeyUp) pendingHeldKeys.erase(it);
        return true;
    }

    bool UltraCanvasModalDialog::HandleDialogKeyEvent(const UCEvent& event) {
        if (!event.IsKeyboardEvent()) return false;
        // The filter outlives each showing of the dialog (see ResetKeyboardState),
        // so ignore anything that arrives while it is not on screen.
        if (!IsVisible()) return false;

        if (dialogConfig.clearPendingInput && ShouldSwallowStaleInput(event)) {
            return true;
        }

        if (!dialogConfig.useButtonMnemonics || event.type != UCEventType::KeyDown) {
            return false;
        }
        if (event.ctrl || event.meta) return false;

        // Alt+letter is the mnemonic everywhere. A bare letter is one too, but
        // only while nothing in the dialog is waiting to have text typed into it.
        if (!event.alt && HasEditableTextField()) return false;

        unsigned char key = static_cast<unsigned char>(event.virtualKey);
        if (key >= 0x80 || !std::isalnum(key)) return false;

        auto* entry = FindButtonByMnemonic(static_cast<char>(key));
        if (!entry || !entry->button->IsVisible() || entry->button->IsDisabled()) return false;

        ActivateButtonEntry(*entry);
        return true;
    }

    bool UltraCanvasModalDialog::HasEditableTextField() const {
        // The message area is a read-only Markdown view, so it does not count;
        // an input dialog's field does.
        std::function<bool(const UltraCanvasContainer*)> scan =
                [&scan](const UltraCanvasContainer* container) -> bool {
            for (const auto& child : container->GetChildren()) {
                auto* element = child.get();
                if (!element || !element->IsVisible() || element->IsDisabled()) continue;

                if (auto* input = dynamic_cast<UltraCanvasTextInput*>(element)) {
                    if (!input->IsReadOnly()) return true;
                }
                if (auto* area = dynamic_cast<UltraCanvasTextArea*>(element)) {
                    if (!area->IsReadOnly()) return true;
                }
                if (auto* childContainer = dynamic_cast<UltraCanvasContainer*>(element)) {
                    if (scan(childContainer)) return true;
                }
            }
            return false;
        };
        return scan(this);
    }

    void UltraCanvasModalDialog::FocusInitialElement() {
        // Focusing the default button gives it a focus ring and makes Space and
        // Return act on it directly, without going through the dialog.
        if (auto* entry = FindDefaultButtonEntry()) {
            SetFocusedElement(entry->button.get());
        }
    }

    bool UltraCanvasModalDialog::IsModalDialog() const {
        return dialogConfig.modal;
    }

    DialogResult UltraCanvasModalDialog::GetResult() const {
        return result;
    }

    void UltraCanvasModalDialog::AddDialogElement(std::shared_ptr<UltraCanvasUIElement> element) {
        if (element && messageContainer) {
            messageContainer->AddChild(element);
        }
    }

    void UltraCanvasModalDialog::RemoveDialogElement(std::shared_ptr<UltraCanvasUIElement> element) {
        if (element && messageContainer) {
            messageContainer->RemoveChild(element);
        }
    }

    void UltraCanvasModalDialog::ClearDialogElements() {
        if (messageContainer) {
            messageContainer->ClearChildren();
            messageContainer->layout.SetFlexColumn()
                                    .SetFlexGap(style.sectionSpacing / 2)
                                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
            messageContainer->AddChild(messageArea);
            messageArea->layoutItem.SetFlexGrow(1);
        }
    }

    void UltraCanvasModalDialog::RenderCustomContent(IRenderContext* ctx, const Rect2Di& dirtyRect) {
        // With layout-based architecture, child components render themselves
        // via the container's Render() call. No manual drawing needed here.
        // The contentSection and footerSection are already children of the window.
    }

    DialogResult UltraCanvasModalDialog::ButtonToResult(DialogButton button) {
        switch (button) {
            case DialogButton::OK:     return DialogResult::OK;
            case DialogButton::Cancel: return DialogResult::Cancel;
            case DialogButton::Yes:    return DialogResult::Yes;
            case DialogButton::No:     return DialogResult::No;
            case DialogButton::Retry:  return DialogResult::Retry;
            case DialogButton::Abort:  return DialogResult::Abort;
            case DialogButton::Ignore: return DialogResult::Ignore;
            case DialogButton::Apply:  return DialogResult::Apply;
            case DialogButton::Close:  return DialogResult::Close;
            case DialogButton::Help:   return DialogResult::Help;
            default:                   return DialogResult::NoResult;
        }
    }

    void UltraCanvasModalDialog::OnDialogButtonClick(DialogButton button) {
        CloseDialog(ButtonToResult(button));
    }

    void UltraCanvasModalDialog::UpdateIconAppearance() {
        if (iconContainer) {
            iconContainer->SetBackgroundColor(GetTypeColor());
            iconContainer->SetVisible(dialogConfig.showIcon &&
                                      dialogConfig.dialogType != DialogType::Custom);
        }
        if (iconLabel) {
            iconLabel->SetText(GetTypeIcon());
        }
    }

    void UltraCanvasModalDialog::UpdateMessageContent() {
        if (messageArea) {
            messageArea->SetText(ComposeMessageMarkdown(), false);
        }
    }

    std::string UltraCanvasModalDialog::GetButtonText(DialogButton button) const {
        switch (button) {
            case DialogButton::OK:     return "OK";
            case DialogButton::Cancel: return "Cancel";
            case DialogButton::Yes:    return "Yes";
            case DialogButton::No:     return "No";
            case DialogButton::Apply:  return "Apply";
            case DialogButton::Close:  return "Close";
            case DialogButton::Help:   return "Help";
            case DialogButton::Retry:  return "Retry";
            case DialogButton::Ignore: return "Ignore";
            case DialogButton::Abort:  return "Abort";
            default:                   return "";
        }
    }

    Color UltraCanvasModalDialog::GetTypeColor() const {
        switch (dialogConfig.dialogType) {
            case DialogType::Information: return Color(70, 130, 180);   // Steel Blue
            case DialogType::Successful:     return Color(40, 167, 69);    // Green
            case DialogType::Question:    return Color(70, 130, 180);   // Steel Blue
            case DialogType::Warning:     return Color(255, 193, 7);    // Amber
            case DialogType::Error:       return Color(220, 53, 69);    // Red
            default:                      return Colors::Gray;
        }
    }

    std::string UltraCanvasModalDialog::GetTypeIcon() const {
        switch (dialogConfig.dialogType) {
            case DialogType::Information: return "i";
            case DialogType::Successful:     return "\xE2\x9C\x93";  // check mark (U+2713)
            case DialogType::Question:    return "?";
            case DialogType::Warning:     return "!";
            case DialogType::Error:       return "X";
            case DialogType::Custom:
            default:                      return "*";
        }
    }

    void UltraCanvasModalDialog::ApplyTypeDefaults() {
        switch (dialogConfig.dialogType) {
            case DialogType::Information:
                if (dialogConfig.title == "Dialog") dialogConfig.title = "Information";
                break;
            case DialogType::Successful:
                if (dialogConfig.title == "Dialog") dialogConfig.title = "Success";
                break;
            case DialogType::Question:
                if (dialogConfig.title == "Dialog") dialogConfig.title = "Question";
                break;
            case DialogType::Warning:
                if (dialogConfig.title == "Dialog") dialogConfig.title = "Warning";
                break;
            case DialogType::Error:
                if (dialogConfig.title == "Dialog") dialogConfig.title = "Error";
                break;
            default:
                break;
        }
    }

    void UltraCanvasModalDialog::AddCustomButton(const std::string& text, DialogResult buttonResult,
                                                 std::function<void()> callback) {
        AddCustomButton(text, buttonResult, DialogButtonRole::Normal, std::move(callback));
    }

    void UltraCanvasModalDialog::AddCustomButton(const std::string& text, DialogResult buttonResult,
                                                 DialogButtonRole role,
                                                 std::function<void()> callback) {
        auto button = std::make_shared<UltraCanvasButton>(
                "DialogBtn_Custom_" + text, 0, 0,
                static_cast<long>(style.buttonWidth), static_cast<long>(style.buttonHeight));
        button->SetText(text);
        SizeButtonToLabel(button);
        // The role's look: the default answer in the accent colour, a
        // destructive one in red - filled when it is also the default, an
        // outline when it merely sits beside the default.
        switch (role) {
            case DialogButtonRole::Default:
                button->SetStyle(ButtonStyles::PrimaryStyle());
                break;
            case DialogButtonRole::DestructiveDefault:
                button->SetStyle(ButtonStyles::DangerStyle());
                break;
            case DialogButtonRole::Destructive: {
                ButtonStyle red;
                red.borderColor      = Color(163, 38, 31, 255);
                red.borderWidth      = 1.5f;
                red.normalTextColor  = Color(143, 31, 25, 255);
                red.hoverTextColor   = Color(143, 31, 25, 255);
                red.pressedTextColor = Colors::White;
                red.hoverColor       = Color(250, 232, 230, 255);
                red.pressedColor     = Color(163, 38, 31, 255);
                button->SetStyle(red);
                break;
            }
            default:
                break;
        }
        ApplyButtonFont(button);   // after the role style: it sets a size too
        button->onClick = [this, buttonResult, callback]() {
            if (callback) callback();
            CloseDialog(buttonResult);
        };
        DialogButtonEntry entry{button, DialogButton::NoneButton, buttonResult};
        entry.role = role;
        dialogButtons.push_back(std::move(entry));

        // The new label needs a letter that none of the existing buttons uses,
        // which can only be decided across the whole set.
        AssignButtonMnemonics();

        if (footerSection) {
            footerSection->AddChild(button);
        }
    }

    void UltraCanvasModalDialog::AddFooterElement(std::shared_ptr<UltraCanvasUIElement> element) {
        if (!element || !footerSection) return;
        element->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        footerElements.push_back(element);
        // The bar was a centred row of buttons. With something at its left it
        // becomes: the elements, a spacer that takes the slack, the buttons -
        // rebuilt in that order, since the buttons are already in it.
        if (!footerSpacer) {
            footerSpacer = std::make_shared<UltraCanvasContainer>("FooterSpacer");
            footerSpacer->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            footerSpacer->size.height = CSSLayout::Dimension::Px(1);
            footerSection->layout.SetFlexJustifyContent(CSSLayout::JustifyContent::FlexStart);
        }
        footerSection->ClearChildren();
        for (const auto& e : footerElements) footerSection->AddChild(e);
        footerSection->AddChild(footerSpacer);
        for (auto& entry : dialogButtons) footerSection->AddChild(entry.button);
    }

    void UltraCanvasModalDialog::SetButtonDisabled(DialogButton button, bool disabled) {
        if (auto* entry = FindButtonEntry(button)) {
            entry->button->SetDisabled(disabled);
        }
    }

    void UltraCanvasModalDialog::SetButtonVisible(DialogButton button, bool buttonVisible) {
        if (auto* entry = FindButtonEntry(button)) {
            entry->button->SetVisible(buttonVisible);
        }
    }
    void UltraCanvasInputDialog::CreateInputDialog(const InputDialogConfig &config) {
        inputConfig = config;
        CreateDialog(config);

        // Input dialogs carry an input field below the prompt and keep the
        // fixed size from their config; don't auto-fit to the prompt text.
        autoSizeHeight = false;
        // The prompt shares the content box with the input field, so it sizes
        // to its own text instead of growing to fill (which would squeeze the
        // field). Alerts keep the grow so their long text can scroll.
        if (messageArea) messageArea->layoutItem.SetFlexGrow(0);

        SetMessage(inputConfig.inputLabel);
        SetDialogButtons(DialogButtons::OKCancel);

        SetupInputField();
    }

    std::string UltraCanvasInputDialog::GetInputValue() const {
        return inputValue;
    }

    void UltraCanvasInputDialog::SetInputValue(const std::string& value) {
        inputValue = value;
        if (textInput) {
            textInput->SetText(value);
        }
        ValidateInput();
    }

    bool UltraCanvasInputDialog::IsInputValid() const {
        return isValid;
    }

    void UltraCanvasInputDialog::ValidateInput() {
        isValid = true;

        if (static_cast<int>(inputValue.length()) < inputConfig.minLength ||
            static_cast<int>(inputValue.length()) > inputConfig.maxLength) {
            isValid = false;
        }

        if (isValid && inputConfig.validator) {
            isValid = inputConfig.validator(inputValue);
        }
    }

    void UltraCanvasInputDialog::SetupInputField() {
        // Create input label
        inputLabel = std::make_shared<UltraCanvasLabel>("InputLabel");
        inputLabel->SetText(inputConfig.inputLabel);
        inputLabel->SetFontSize(style.messageFontSize);

        // Create text input
        textInput = std::make_shared<UltraCanvasTextInput>("InputField", 0, 0, 300, 25);
        textInput->SetText(inputConfig.defaultValue);
        textInput->SetPlaceholder(inputConfig.inputPlaceholder);
        inputValue = inputConfig.defaultValue;

        switch (inputConfig.inputType) {
            case InputType::Password:
                textInput->SetInputType(TextInputType::Password);
                break;
            case InputType::Number:
                textInput->SetInputType(TextInputType::Number);
                break;
            case InputType::Email:
                textInput->SetInputType(TextInputType::Email);
                break;
            default:
                textInput->SetInputType(TextInputType::Text);
                break;
        }

        textInput->onTextChanged = [this](const std::string& newText) {
            OnInputChanged(newText);
        };

        // Add to content via layout
        AddDialogElement(inputLabel);
        AddDialogElement(textInput);

        ValidateInput();
    }

    void UltraCanvasInputDialog::FocusInitialElement() {
        if (textInput && textInput->IsVisible() && !textInput->IsDisabled()) {
            SetFocusedElement(textInput.get());
            return;
        }
        UltraCanvasModalDialog::FocusInitialElement();
    }

    void UltraCanvasInputDialog::OnInputChanged(const std::string& text) {
        inputValue = text;
        ValidateInput();

        if (inputConfig.onInputChanged) {
            inputConfig.onInputChanged(text);
        }
    }

    void UltraCanvasInputDialog::OnInputValidation() {
    }

// ===== FILE DIALOG IMPLEMENTATION =====
    namespace {
        // Folder-tree node ids. The same folder can be reached both from a
        // place ("Documents") and from its drive, so an id is a tag naming the
        // branch plus the folder's path; a placeholder child (the expander of
        // a folder whose sub-folders have not been read yet) gets its own tag.
        constexpr const char* kTreeRootId = "fd-root";
        constexpr char kPlaceTag = 'p';
        constexpr char kDriveTag = 'd';
        constexpr char kPlaceholderTag = '~';

        std::string TreeNodeId(char tag, const std::string& path) {
            return std::string(1, tag) + "|" + path;
        }
        char TreeNodeTag(const std::string& id) {
            return id.size() > 1 && id[1] == '|' ? id[0] : '\0';
        }
        std::string TreeNodePath(const std::string& id) {
            return id.size() > 1 && id[1] == '|' ? id.substr(2) : std::string();
        }

        const Color kFileDialogBorderColor(160, 160, 160, 255);

        std::string FileDialogIconPath(const std::string& file) {
            return NormalizePath(GetResourcesDir() + "media/icons/" + file);
        }

        // UTF-8, as the dialog's paths are - on Windows from the wide
        // environment, where getenv would answer in the ANSI code page.
        std::string UserHomeDirectory() {
#if defined(_WIN32) || defined(_WIN64)
            return GetEnvUtf8("USERPROFILE");
#else
            return GetEnvUtf8("HOME");
#endif
        }

        bool IsHiddenName(const std::string& name) {
            return !name.empty() && name[0] == '.';
        }

        bool LessIgnoringCase(const std::string& a, const std::string& b) {
            return std::lexicographical_compare(
                    a.begin(), a.end(), b.begin(), b.end(),
                    [](unsigned char x, unsigned char y) {
                        return std::tolower(x) < std::tolower(y);
                    });
        }

        // The sub-folders of `folder`, sorted as the user reads them.
        std::vector<std::string> SubFolderNames(const std::string& folder, bool showHidden) {
            std::vector<std::string> names;
            std::error_code ec;
            for (std::filesystem::directory_iterator it(PathFromUtf8(folder), ec), end;
                 it != end && !ec; it.increment(ec)) {
                std::error_code dec;
                if (!it->is_directory(dec) || dec) continue;
                std::string name = PathToUtf8(it->path().filename());
                if (!showHidden && IsHiddenName(name)) continue;
                names.push_back(std::move(name));
            }
            std::sort(names.begin(), names.end(), LessIgnoringCase);
            return names;
        }

        // Stops at the first sub-folder: whether a row needs an expander.
        bool HasSubFolders(const std::string& folder, bool showHidden) {
            std::error_code ec;
            for (std::filesystem::directory_iterator it(PathFromUtf8(folder), ec), end;
                 it != end && !ec; it.increment(ec)) {
                std::error_code dec;
                if (!it->is_directory(dec) || dec) continue;
                if (!showHidden && IsHiddenName(PathToUtf8(it->path().filename()))) continue;
                return true;
            }
            return false;
        }

        // `path` relative to `root`, or empty when it is not inside it.
        std::filesystem::path RelativeInside(const std::filesystem::path& path,
                                             const std::filesystem::path& root) {
            std::filesystem::path rel = path.lexically_relative(root);
            if (rel.empty() || *rel.begin() == "..") return {};
            return rel;
        }

        // The listing's layouts, in the order of the view buttons.
        const FilerViewType kFileDialogViews[] = {
                FilerViewType::Details, FilerViewType::List,
                FilerViewType::ThumbnailsSmall, FilerViewType::ThumbnailsMedium,
                FilerViewType::ThumbnailsBig, FilerViewType::ThumbnailsMaximized};
        constexpr int kFileDialogViewCount = 6;

        // Bounds for what is written back (FileDialogSettings ignores values
        // outside them when it reads the file).
        constexpr int kFileDialogMinWidth = 520;
        constexpr int kFileDialogMinHeight = 380;
        constexpr int kFileDialogMaxSide = 8000;
        constexpr int kFileDialogMinColumn = 44;     // the widget's own minimum
        constexpr int kFileDialogMaxColumn = 2000;

        // The name the running application registered with
        // UltraCanvasApplication::Initialize: the key of its own last folder.
        std::string CurrentApplicationName() {
            auto* app = UltraCanvasApplicationBase::GetCurrent();
            return app ? app->GetAppName() : std::string();
        }
    } // namespace

    void UltraCanvasFileDialog::CreateFileDialog(const FileDialogConfig &config) {
        fileConfig = config;
        // A file dialog without filters lists every file; a folder picker
        // filters nothing.
        if (fileConfig.filters.empty() && fileConfig.dialogType != FileDialogType::SelectFolder) {
            fileConfig.filters = { FileFilter("All Files", "*") };
            fileConfig.selectedFilterIndex = 0;
        }
        // Opens the way the user left it last time: same view, same size
        // (UltraCanvasFileDialogSettings.h, FileDialog.conf).
        const FileDialogSettings remembered = FileDialogSettings::Load();
        viewIndex = remembered.view;
        sizeColumnWidth = remembered.sizeColumn;
        typeColumnWidth = remembered.typeColumn;
        modifiedColumnWidth = remembered.modifiedColumn;
        if (remembered.width > 0 && remembered.height > 0) {
            fileConfig.width = remembered.width;
            fileConfig.height = remembered.height;
        }
        // The last used folder - the one all applications share, or this
        // application's own, as ULTRA OS settings has it - when the caller
        // names none; a folder the caller does name is the one it means.
        if (fileConfig.initialDirectory.empty()) {
            const std::string last = remembered.LastFolderFor(CurrentApplicationName());
            std::error_code fec;
            if (!last.empty() && std::filesystem::is_directory(PathFromUtf8(last), fec) && !fec)
                fileConfig.initialDirectory = last;
        }
        UltraCanvasModalDialog::CreateDialog(fileConfig);

        // The browser fills the dialog at the configured size; there is no
        // message text to fit the height to.
        autoSizeHeight = false;

        currentDirectory = fileConfig.initialDirectory;
        showHiddenFiles = fileConfig.showHiddenFiles;

        SetDialogButtons(DialogButtons::OKCancel);

        // OK must go through HandleOkButton so the selection is resolved and
        // onFileSelected fires; the inherited handler would only close the
        // dialog. Wiring it here keeps the click, Enter and mnemonic paths
        // identical, because they all run this same callback.
        if (auto* okEntry = FindButtonEntry(DialogButton::OK)) {
            okEntry->button->onClick = [this]() { HandleOkButton(); };
        }

        {
            std::error_code ec;
            if (currentDirectory.empty() ||
                !std::filesystem::is_directory(PathFromUtf8(currentDirectory), ec)) {
                std::filesystem::path cwd = std::filesystem::current_path(ec);
                currentDirectory = ec ? std::string(".") : PathToUtf8(cwd);
            }
            std::filesystem::path canonical =
                    std::filesystem::canonical(PathFromUtf8(currentDirectory), ec);
            if (!ec) currentDirectory = PathToUtf8(canonical);
        }

        BuildFileInterface();
        PopulateFolderTree();
        RefreshFileList();
        SyncFolderTree();
        if (fileNameInput) fileNameInput->SetText(fileConfig.defaultFileName);
    }

    void UltraCanvasFileDialog::BuildFileInterface() {
        // The browser takes the whole content box: no type icon, no message.
        SetIconVisible(false);
        if (messageArea) messageArea->SetVisible(false);

        const float fontSize = style.messageFontSize;
        const long fieldHeight = 28;

        auto browser = std::make_shared<UltraCanvasContainer>("FileDialogBrowser");
        browser->layout.SetFlexColumn().SetFlexGap(8)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        browser->layoutItem.SetFlexGrow(1).SetFlexShrink(1);

        // ----- path field + up button -----
        auto pathRow = std::make_shared<UltraCanvasContainer>("FileDialogPathRow");
        pathRow->size.height = CSSLayout::Dimension::Px(static_cast<float>(fieldHeight));
        pathRow->layout.SetFlexRow().SetFlexGap(6)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        pathRow->layoutItem.SetFlexShrink(0);

        // No text: the constructor's default is "Button", which would lay the
        // arrow out as an icon beside a label and leave it at the left padding.
        upButton = std::make_shared<UltraCanvasButton>("FileDialogUp", 0, 0, fieldHeight, fieldHeight, "");
        upButton->SetIcon(FileDialogIconPath("arrow-up.svg"));
        upButton->SetTooltip("Up one level");
        upButton->layoutItem.SetFlexShrink(0);
        upButton->onClick = [this]() { NavigateToParentDirectory(); };
        pathRow->AddChild(upButton);

        pathInput = std::make_shared<UltraCanvasTextInput>("FileDialogPath", 0, 0, 300, fieldHeight);
        pathInput->SetFontSize(fontSize);
        pathInput->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        // Return in the path field goes to the folder typed there instead of
        // accepting the dialog.
        pathInput->onEnterPressed = [this](const std::string& text) {
            std::error_code ec;
            const std::filesystem::path typed = PathFromUtf8(text);
            if (std::filesystem::is_directory(UltraCanvas::PathFromUtf8(typed), ec)) {
                GoToDirectory(text, true);
            } else if (fileConfig.dialogType != FileDialogType::SelectFolder &&
                       std::filesystem::is_directory(typed.parent_path(), ec)) {
                // A file path: open its folder and put the name in the name field.
                GoToDirectory(PathToUtf8(typed.parent_path()), true);
                if (fileNameInput) fileNameInput->SetText(PathToUtf8(typed.filename()));
            } else if (pathInput) {
                pathInput->SetText(currentDirectory);
            }
            return true;
        };
        pathRow->AddChild(pathInput);

        // How the listing is drawn, as in UltraFiler: one glyph per layout,
        // the current one marked. Created before the listing it drives, which
        // is assigned below - the callback only runs on a click.
        viewSelector = std::make_shared<UltraCanvasSegmentedControl>(
                "FileDialogView", 0, 0, 6 * 30, fieldHeight);
        viewSelector->layoutItem.SetFlexShrink(0);
        viewSelector->SetTooltip("Display: details, list, small / medium / large / "
                                 "extra large icons");
        for (const char* icon : {"view-details.svg", "view-list.svg",
                                 "view-icons-small.svg", "view-icons-medium.svg",
                                 "view-icons-large.svg", "view-icons-xlarge.svg"}) {
            viewSelector->AddSegment("", FileDialogIconPath(icon));
        }
        viewSelector->SetSelectedIndex(viewIndex);
        viewSelector->onSegmentSelected = [this](int index) {
            if (index < 0 || index >= kFileDialogViewCount) return;
            viewIndex = index;
            if (filerView) filerView->SetViewType(kFileDialogViews[index]);
        };
        pathRow->AddChild(viewSelector);
        browser->AddChild(pathRow);

        // ----- folder tree | listing -----
        auto panes = std::make_shared<UltraCanvasContainer>("FileDialogPanes");
        panes->layout.SetFlexRow().SetFlexGap(8)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        panes->layoutItem.SetFlexGrow(1).SetFlexShrink(1);

        folderTree = std::make_shared<UltraCanvasTreeView>("FileDialogFolders");
        folderTree->size.width = CSSLayout::Dimension::Px(260);
        folderTree->layoutItem.SetFlexShrink(0);
        folderTree->SetFontSize(fontSize);
        folderTree->SetRowHeight(22);
        folderTree->SetSelectionMode(TreeSelectionMode::Single);
        folderTree->SetLineStyle(TreeLineStyle::Dotted);
        folderTree->SetShowFirstChildOnExpand(false);
        folderTree->SetShowScrollToTopButton(false);
        folderTree->SetRootVisible(false);
        folderTree->SetBorders(1.0f, kFileDialogBorderColor);
        folderTree->onNodeExpanded = [this](TreeNode* node) { LoadFolderTreeChildren(node); };
        folderTree->onNodeSelected = [this](TreeNode* node) {
            if (syncingTree || !node) return;
            const std::string& id = node->data.nodeId;
            if (TreeNodeTag(id) != kPlaceTag && TreeNodeTag(id) != kDriveTag) return;
            GoToDirectory(TreeNodePath(id), false);
        };
        panes->AddChild(folderTree);

        // The listing is the framework's file display, as in UltraFiler: its
        // icons, columns, sorting, thumbnails and keyboard. Opening a folder
        // there navigates the dialog; opening a file chooses it.
        filerView = std::make_shared<UltraCanvasFilerWidget>("FileDialogListing");
        filerView->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        filerView->SetViewType(kFileDialogViews[viewIndex]);
        // A picker needs the name above all: Details shows Name, Size, Type
        // and Modified, the others narrowed, so the name column gets the rest.
        filerView->SetDetailsColumnVisible(FilerDetailsColumn::CreatedDate, false);
        filerView->SetDetailsColumnVisible(FilerDetailsColumn::Attributes, false);
        filerView->SetDetailsColumnVisible(FilerDetailsColumn::Info, false);
        filerView->SetDetailsColumnWidth(FilerDetailsColumn::Size, sizeColumnWidth);
        filerView->SetDetailsColumnWidth(FilerDetailsColumn::Type, typeColumnWidth);
        filerView->SetDetailsColumnWidth(FilerDetailsColumn::ModifiedDate, modifiedColumnWidth);
        filerView->SetShowHiddenFiles(showHiddenFiles);
        filerView->SetSelectionInfoVisible(false);
        filerView->SetHoverIconMenuEnabled(fileConfig.hoverIconMenu);
        filerView->SetActivateOpensWithDefaultApp(false);
        filerView->SetBorders(1.0f, kFileDialogBorderColor);
        filerView->onSelectionChanged = [this](const std::vector<FilerEntry>& selected) {
            OnListingSelectionChanged(selected);
        };
        filerView->onPathChanged = [this](const std::string& path) {
            OnListingPathChanged(path);
        };
        filerView->onFileActivated = [this](const FilerEntry& entry) {
            if (fileConfig.dialogType == FileDialogType::SelectFolder) return;
            // Closing the dialog from inside the widget's own event handler
            // would free the widget under it: accept on the next loop pass.
            auto* app = UltraCanvasApplicationBase::GetCurrent();
            if (!app) return;
            activationPending = true;
            std::weak_ptr<UltraCanvasUIElement> weak = weak_from_this();
            const std::string path = entry.path;
            app->PostToUIThread([weak, this, path]() {
                if (weak.expired()) return;
                activationPending = false;
                Accept({path});
            });
        };
        ApplyListingFilter();
        panes->AddChild(filerView);
        browser->AddChild(panes);

        // ----- name and type rows -----
        // Two columns, so the labels line up and the fields start at the same
        // x whatever the labels' measured widths are.
        auto fields = std::make_shared<UltraCanvasContainer>("FileDialogFields");
        fields->layout.SetFlexRow().SetFlexGap(8)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        fields->layoutItem.SetFlexShrink(0);

        auto labelColumn = std::make_shared<UltraCanvasContainer>("FileDialogFieldLabels");
        labelColumn->layout.SetFlexColumn().SetFlexGap(6)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        labelColumn->layoutItem.SetFlexShrink(0);

        auto inputColumn = std::make_shared<UltraCanvasContainer>("FileDialogFieldInputs");
        inputColumn->layout.SetFlexColumn().SetFlexGap(6)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        inputColumn->layoutItem.SetFlexGrow(1).SetFlexShrink(1);

        auto addLabel = [&](const std::string& id, const std::string& text) {
            auto label = std::make_shared<UltraCanvasLabel>(id, text);
            label->SetFontSize(fontSize);
            label->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
            label->size.height = CSSLayout::Dimension::Px(static_cast<float>(fieldHeight));
            labelColumn->AddChild(label);
        };

        if (fileConfig.dialogType != FileDialogType::SelectFolder) {
            addLabel("FileDialogNameLabel", "File name:");
            fileNameInput = std::make_shared<UltraCanvasTextInput>(
                    "FileDialogName", 0, 0, 300, fieldHeight);
            fileNameInput->SetFontSize(fontSize);
            fileNameInput->onEnterPressed = [this](const std::string&) {
                HandleOkButton();
                return true;
            };
            inputColumn->AddChild(fileNameInput);

            if (fileConfig.filterToggles) {
                // One button per kind of file, any number on, sized to their
                // names and left-aligned: no extension lists to read through.
                addLabel("FileDialogTypeLabel", "Show:");
                auto toggleRow = std::make_shared<UltraCanvasContainer>("FileDialogTypeRow");
                toggleRow->size.height = CSSLayout::Dimension::Px(static_cast<float>(fieldHeight));
                toggleRow->layout.SetFlexRow()
                        .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
                filterToggleBar = std::make_shared<UltraCanvasSegmentedControl>(
                        "FileDialogTypeToggles", 0, 0, 300, fieldHeight);
                filterToggleBar->SetSelectionMode(SegmentSelectionMode::Toggle);
                filterToggleBar->SetWidthMode(SegmentWidthMode::FitContent);
                filterToggleBar->layoutItem.SetFlexShrink(1);
                toggleRow->AddChild(filterToggleBar);
                inputColumn->AddChild(toggleRow);
                RebuildFilterToggles();
                ApplyListingFilter();   // the listing was set up before the toggles
                filterToggleBar->onSelectionChanged = [this](const std::vector<int>& on) {
                    activeFilters = std::set<int>(on.begin(), on.end());
                    ApplyListingFilter();
                };
            } else {
                addLabel("FileDialogTypeLabel", "Files of type:");
                filterDropdown = std::make_shared<UltraCanvasDropdown>(
                        "FileDialogType", 0, 0, 300, fieldHeight);
                filterDropdown->onSelectionChanged = [this](int index, const DropdownItem&) {
                    if (index < 0 || index >= static_cast<int>(fileConfig.filters.size())) return;
                    if (index == fileConfig.selectedFilterIndex) return;
                    fileConfig.selectedFilterIndex = index;
                    ApplyListingFilter();
                };
                inputColumn->AddChild(filterDropdown);
                RebuildFilterDropdown();
            }

            fields->AddChild(labelColumn);
            fields->AddChild(inputColumn);
            browser->AddChild(fields);
        }

        AddDialogElement(browser);
    }

    void UltraCanvasFileDialog::RebuildFilterDropdown() {
        if (!filterDropdown) return;
        filterDropdown->ClearItems();
        for (const FileFilter& filter : fileConfig.filters) {
            filterDropdown->AddItem(filter.ToDisplayString());
        }
        if (fileConfig.selectedFilterIndex >= 0 &&
            fileConfig.selectedFilterIndex < static_cast<int>(fileConfig.filters.size())) {
            filterDropdown->SetSelectedIndex(fileConfig.selectedFilterIndex, false);
        }
        filterDropdown->SetDisabled(fileConfig.filters.size() < 2);
    }

    void UltraCanvasFileDialog::RebuildFilterToggles() {
        activeFilters.clear();
        const int count = static_cast<int>(fileConfig.filters.size());
        auto isAllFiles = [this](int i) {
            const auto& exts = fileConfig.filters[i].extensions;
            return std::find(exts.begin(), exts.end(), "*") != exts.end();
        };
        // Every kind of file on, "All files" off: it would match everything
        // and make the others pointless. Only an all-"*" list starts with it.
        for (int i = 0; i < count; ++i) {
            if (!isAllFiles(i)) activeFilters.insert(i);
        }
        if (activeFilters.empty()) {
            for (int i = 0; i < count; ++i) activeFilters.insert(i);
        }
        if (!filterToggleBar) return;
        filterToggleBar->ClearSegments();
        std::string tooltip;
        for (int i = 0; i < count; ++i) {
            const FileFilter& filter = fileConfig.filters[i];
            filterToggleBar->AddSegment(filter.description);
            // The extensions stay one hover away, shortened for a long list.
            if (isAllFiles(i)) continue;
            std::string exts;
            const size_t shown = std::min<size_t>(filter.extensions.size(), 24);
            for (size_t k = 0; k < shown; ++k) {
                if (k > 0) exts += " ";
                exts += filter.extensions[k];
            }
            if (filter.extensions.size() > shown) {
                exts += " ... (" + std::to_string(filter.extensions.size()) + " types)";
            }
            if (!tooltip.empty()) tooltip += "\n";
            tooltip += filter.description + ": " + exts;
        }
        filterToggleBar->SetTooltip(tooltip);
        // Set before the callback is wired (or while it is unset by a rebuild
        // the caller follows with ApplyListingFilter).
        auto callback = std::move(filterToggleBar->onSelectionChanged);
        filterToggleBar->onSelectionChanged = nullptr;
        filterToggleBar->SetSelectedIndices(std::vector<int>(activeFilters.begin(), activeFilters.end()));
        filterToggleBar->onSelectionChanged = std::move(callback);
    }

    bool UltraCanvasFileDialog::MatchesTypeFilter(const std::string& fileName) const {
        const int count = static_cast<int>(fileConfig.filters.size());
        if (fileConfig.filterToggles) {
            for (int i : activeFilters) {
                if (i >= 0 && i < count && fileConfig.filters[i].Matches(fileName)) return true;
            }
            return activeFilters.empty();
        }
        if (fileConfig.selectedFilterIndex < 0 || fileConfig.selectedFilterIndex >= count) {
            return true;
        }
        return fileConfig.filters[fileConfig.selectedFilterIndex].Matches(fileName);
    }

    void UltraCanvasFileDialog::FocusInitialElement() {
        if (fileNameInput && fileNameInput->IsVisible()) {
            SetFocusedElement(fileNameInput.get());
            return;
        }
        if (filerView) {
            SetFocusedElement(filerView.get());
            return;
        }
        UltraCanvasModalDialog::FocusInitialElement();
    }

// ----- folder tree -----
    void UltraCanvasFileDialog::PopulateFolderTree() {
        if (!folderTree) return;
        loadedTreeNodes.clear();
        treeDriveRoots.clear();

        TreeNodeData rootData(kTreeRootId, "");
        folderTree->SetRootNode(rootData);

        // The user's places first: home and the well-known folders in it.
        const std::string home = UserHomeDirectory();
        std::error_code ec;
        if (!home.empty() && std::filesystem::is_directory(PathFromUtf8(home), ec)) {
            AddFolderTreeNode(kTreeRootId, TreeNodeId(kPlaceTag, home), "Home",
                              "home-user.svg", false);
        }
        for (const UserFolderInfo& folder : GetWellKnownUserFolders()) {
            if (folder.kind != UserFolderKind::Desktop &&
                folder.kind != UserFolderKind::Documents &&
                folder.kind != UserFolderKind::Downloads) continue;
            AddFolderTreeNode(kTreeRootId, TreeNodeId(kPlaceTag, folder.path), folder.label,
                              "folder.png", false);
        }

        // Then every mounted drive. Drives are not probed for sub-folders
        // here: that would spin up an empty optical drive or wait out a
        // disconnected network share just to open the dialog.
        for (const MountedVolume& volume : ListMountedVolumes()) {
            treeDriveRoots.push_back(volume.path);
            AddFolderTreeNode(kTreeRootId, TreeNodeId(kDriveTag, volume.path),
                              volume.label.empty() ? volume.path : volume.label,
                              "drive.png", false);
        }
    }

    void UltraCanvasFileDialog::AddFolderTreeNode(const std::string& parentId,
                                                  const std::string& nodeId,
                                                  const std::string& label,
                                                  const std::string& iconFile,
                                                  bool probeSubFolders) {
        if (!folderTree || folderTree->FindNode(nodeId)) return;
        TreeNodeData data(nodeId, label);
        data.leftIcon = TreeNodeIcon(FileDialogIconPath(iconFile), 16, 16);
        data.tooltip = TreeNodePath(nodeId);
        if (!folderTree->AddNode(parentId, data)) return;
        // An expander for a folder that (may) hold folders; its children are
        // read when it is opened.
        if (!probeSubFolders || HasSubFolders(TreeNodePath(nodeId), showHiddenFiles)) {
            TreeNodeData placeholder(std::string(1, kPlaceholderTag) + "|" + nodeId, "...");
            placeholder.enabled = false;
            folderTree->AddNode(nodeId, placeholder);
        }
    }

    void UltraCanvasFileDialog::LoadFolderTreeChildren(TreeNode* node) {
        if (!folderTree || !node) return;
        const std::string id = node->data.nodeId;
        const char tag = TreeNodeTag(id);
        if (tag != kPlaceTag && tag != kDriveTag) return;
        if (!loadedTreeNodes.insert(id).second) return;

        const std::string folder = TreeNodePath(id);
        // The real children go in before the placeholder comes out: a node
        // whose last child is removed becomes a leaf and loses its expansion.
        for (const std::string& name : SubFolderNames(folder, showHiddenFiles)) {
            AddFolderTreeNode(id, TreeNodeId(tag, CombinePath(folder, name)), name,
                              "folder.png", true);
        }
        folderTree->RemoveNode(std::string(1, kPlaceholderTag) + "|" + id);
    }

    void UltraCanvasFileDialog::SyncFolderTree() {
        if (!folderTree) return;
        const std::filesystem::path current = PathFromUtf8(currentDirectory);

        // The drive holding the folder: the longest root it lies inside.
        std::string bestRoot;
        std::filesystem::path bestRel;
        for (const std::string& root : treeDriveRoots) {
            const std::filesystem::path rootPath = PathFromUtf8(root);
            std::filesystem::path rel;
            if (current.lexically_normal() == rootPath.lexically_normal()) {
                rel = ".";
            } else {
                rel = RelativeInside(current, rootPath);
                if (rel.empty()) continue;
            }
            if (bestRoot.empty() || root.size() > bestRoot.size()) {
                bestRoot = root;
                bestRel = rel;
            }
        }
        if (bestRoot.empty()) return;

        TreeNode* node = folderTree->FindNode(TreeNodeId(kDriveTag, bestRoot));
        std::filesystem::path walked = PathFromUtf8(bestRoot);
        if (bestRel != ".") {
            for (const auto& part : bestRel) {
                if (!node || part.empty() || part == ".") continue;
                folderTree->ExpandNode(node);   // reads its sub-folders
                walked /= part;
                TreeNode* child = folderTree->FindNode(TreeNodeId(kDriveTag, PathToUtf8(walked)));
                if (!child) break;              // e.g. a hidden folder the tree leaves out
                node = child;
            }
        }
        if (!node) return;

        syncingTree = true;
        folderTree->SelectNode(node);
        syncingTree = false;
        if (folderTree->GetHeight() > 0) {
            folderTree->ScrollTo(node);
        } else {
            revealTreeSelectionPending = true;
        }
    }

    void UltraCanvasFileDialog::PerformClose() {
        // However it closes - OK, Cancel, Escape, the title bar - the view,
        // the size, the column widths and the folder it had are what the next
        // file dialog opens with.
        // Written through Update(): the file is re-read first, so what other
        // applications (and ULTRA OS settings) wrote since this dialog opened
        // is kept.
        int sizeColumn = sizeColumnWidth, typeColumn = typeColumnWidth,
            modifiedColumn = modifiedColumnWidth;
        if (filerView) {
            auto column = [this](FilerDetailsColumn c) {
                return std::clamp(filerView->GetDetailsColumnWidth(c),
                                  kFileDialogMinColumn, kFileDialogMaxColumn);
            };
            sizeColumn = column(FilerDetailsColumn::Size);
            typeColumn = column(FilerDetailsColumn::Type);
            modifiedColumn = column(FilerDetailsColumn::ModifiedDate);
        }
        std::string folder;
        {
            std::error_code fec;
            if (std::filesystem::is_directory(PathFromUtf8(currentDirectory), fec) && !fec)
                folder = currentDirectory;
        }
        const int width = std::clamp(static_cast<int>(std::lround(GetWidth())),
                                     kFileDialogMinWidth, kFileDialogMaxSide);
        const int height = std::clamp(static_cast<int>(std::lround(GetHeight())),
                                      kFileDialogMinHeight, kFileDialogMaxSide);
        const int view = viewIndex;
        const std::string appName = CurrentApplicationName();
        FileDialogSettings::Update([&](FileDialogSettings& s) {
            s.view = view;
            s.width = width;
            s.height = height;
            s.sizeColumn = sizeColumn;
            s.typeColumn = typeColumn;
            s.modifiedColumn = modifiedColumn;
            if (!folder.empty()) s.SetLastFolderFor(appName, folder);
        });
        UltraCanvasModalDialog::PerformClose();
    }

    void UltraCanvasFileDialog::Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) {
        UltraCanvasModalDialog::Arrange(finalRect, ctx);
        if (revealTreeSelectionPending && folderTree && folderTree->GetHeight() > 0) {
            revealTreeSelectionPending = false;
            folderTree->ScrollTo(folderTree->GetFirstSelectedNode());
        }
    }

// ----- listing -----
    std::vector<std::string> UltraCanvasFileDialog::GetSelectedFiles() const {
        return selectedFiles;
    }

    std::string UltraCanvasFileDialog::GetSelectedFile() const {
        return selectedFiles.empty() ? "" : selectedFiles[0];
    }

    void UltraCanvasFileDialog::SetCurrentDirectory(const std::string& directory) {
        GoToDirectory(directory, true);
    }

    bool UltraCanvasFileDialog::GoToDirectory(const std::string& directory, bool syncTree) {
        std::error_code ec;
        const std::filesystem::path path = PathFromUtf8(directory);
        if (!std::filesystem::is_directory(UltraCanvas::PathFromUtf8(path), ec) || ec) {
            debugOutput << "Invalid path: " << directory << std::endl;
            if (pathInput) pathInput->SetText(currentDirectory);
            return false;
        }
        std::filesystem::path canonical = std::filesystem::canonical(UltraCanvas::PathFromUtf8(path), ec);
        currentDirectory = ec ? directory : PathToUtf8(canonical);
        RefreshFileList();
        if (syncTree) SyncFolderTree();
        if (onDirectoryChanged) onDirectoryChanged(currentDirectory);
        return true;
    }

    std::string UltraCanvasFileDialog::GetCurrentDirectory() const {
        return currentDirectory;
    }

    void UltraCanvasFileDialog::RefreshFileList() {
        selectedFiles.clear();
        autoFileName.clear();
        if (pathInput) pathInput->SetText(currentDirectory);
        if (!filerView) return;
        if (filerView->GetPath() == currentDirectory) {
            filerView->Refresh();
            return;
        }
        settingListingPath = true;
        filerView->SetPath(currentDirectory);
        settingListingPath = false;
    }

    void UltraCanvasFileDialog::ApplyListingFilter() {
        if (!filerView) return;
        if (fileConfig.dialogType == FileDialogType::SelectFolder) {
            filerView->SetEntryFilter([](const FilerEntry& e) { return e.isDirectory; });
            return;
        }
        if (!fileConfig.filterToggles &&
            (fileConfig.selectedFilterIndex < 0 ||
             fileConfig.selectedFilterIndex >= static_cast<int>(fileConfig.filters.size()))) {
            filerView->SetEntryFilter(nullptr);
            return;
        }
        // The filters in force, copied: the listing may run the predicate off
        // the UI thread, and a toggle change sets a new one through here.
        std::vector<FileFilter> inForce;
        if (fileConfig.filterToggles) {
            for (int i : activeFilters) {
                if (i >= 0 && i < static_cast<int>(fileConfig.filters.size())) {
                    inForce.push_back(fileConfig.filters[i]);
                }
            }
        } else {
            inForce.push_back(fileConfig.filters[fileConfig.selectedFilterIndex]);
        }
        filerView->SetEntryFilter([inForce](const FilerEntry& e) {
            if (e.isDirectory || e.isArchive) return true;
            for (const FileFilter& filter : inForce) {
                if (filter.Matches(e.name)) return true;
            }
            return inForce.empty();
        });
    }

    void UltraCanvasFileDialog::OnListingPathChanged(const std::string& path) {
        if (settingListingPath) return;
        // The user opened a folder in the listing: the dialog follows it.
        currentDirectory = path;
        selectedFiles.clear();
        if (fileNameInput && !autoFileName.empty() && fileNameInput->GetText() == autoFileName) {
            fileNameInput->SetText("");
        }
        autoFileName.clear();
        if (pathInput) pathInput->SetText(currentDirectory);
        SyncFolderTree();
        if (onDirectoryChanged) onDirectoryChanged(currentDirectory);
    }

    void UltraCanvasFileDialog::OnListingSelectionChanged(const std::vector<FilerEntry>& selected) {
        std::vector<std::string> files;
        for (const FilerEntry& e : selected) {
            if (!e.isDirectory) files.push_back(e.name);
        }
        if (files.empty() || !fileNameInput) return;
        // One file: its name. Several: each quoted, the way the name field
        // of a multi-select dialog shows them.
        std::string text;
        if (files.size() == 1) {
            text = files.front();
        } else {
            for (const std::string& f : files) {
                if (!text.empty()) text += " ";
                text += "\"" + f + "\"";
            }
        }
        autoFileName = text;
        fileNameInput->SetText(text);
    }

// ----- filters and options -----
    void UltraCanvasFileDialog::SetFileFilters(const std::vector<FileFilter>& filters) {
        fileConfig.filters = filters;
        fileConfig.selectedFilterIndex = 0;
        RebuildFilterDropdown();
        if (fileConfig.filterToggles) RebuildFilterToggles();
        ApplyListingFilter();
    }

    void UltraCanvasFileDialog::AddFileFilter(const FileFilter& filter) {
        fileConfig.filters.push_back(filter);
        RebuildFilterDropdown();
        if (fileConfig.filterToggles) {
            RebuildFilterToggles();
            ApplyListingFilter();
        }
    }

    void UltraCanvasFileDialog::AddFileFilter(const std::string& description, const std::vector<std::string>& extensions) {
        AddFileFilter(FileFilter(description, extensions));
    }

    void UltraCanvasFileDialog::AddFileFilter(const std::string& description, const std::string& extension) {
        AddFileFilter(FileFilter(description, extension));
    }

    int UltraCanvasFileDialog::GetSelectedFilterIndex() const {
        return fileConfig.selectedFilterIndex;
    }

    void UltraCanvasFileDialog::SetSelectedFilterIndex(int index) {
        if (index >= 0 && index < static_cast<int>(fileConfig.filters.size())) {
            fileConfig.selectedFilterIndex = index;
            if (filterDropdown) filterDropdown->SetSelectedIndex(index, false);
            ApplyListingFilter();
        }
    }

    const std::vector<FileFilter>& UltraCanvasFileDialog::GetFileFilters() const {
        return fileConfig.filters;
    }

    void UltraCanvasFileDialog::SetShowHiddenFiles(bool show) {
        if (showHiddenFiles == show) return;
        showHiddenFiles = show;
        fileConfig.showHiddenFiles = show;
        if (filerView) filerView->SetShowHiddenFiles(show);
        PopulateFolderTree();
        RefreshFileList();
        SyncFolderTree();
    }

    bool UltraCanvasFileDialog::GetShowHiddenFiles() const {
        return showHiddenFiles;
    }

    void UltraCanvasFileDialog::SetDefaultFileName(const std::string& fileName) {
        fileConfig.defaultFileName = fileName;
        if (fileNameInput) fileNameInput->SetText(fileName);
    }

    std::string UltraCanvasFileDialog::GetDefaultFileName() const {
        return fileConfig.defaultFileName;
    }

    std::string UltraCanvasFileDialog::GetSelectedFilePath() const {
        if (selectedFiles.empty()) return "";
        return CombinePath(currentDirectory, selectedFiles[0]);
    }

    std::vector<std::string> UltraCanvasFileDialog::GetSelectedFilePaths() const {
        std::vector<std::string> paths;
        for (const auto& file : selectedFiles) {
            paths.push_back(CombinePath(currentDirectory, file));
        }
        return paths;
    }

// ----- accepting -----
    void UltraCanvasFileDialog::HandleOkButton() {
        // Return on a row of the listing has already been taken as opening it.
        if (activationPending) return;

        if (fileConfig.dialogType == FileDialogType::SelectFolder) {
            // A folder highlighted in the listing is the answer; otherwise the
            // folder being shown.
            std::string chosen = currentDirectory;
            if (filerView) {
                for (const FilerEntry& e : filerView->GetSelectedEntries()) {
                    if (e.isDirectory) { chosen = e.path; break; }
                }
            }
            Accept({chosen});
            return;
        }

        std::string typed = fileNameInput ? fileNameInput->GetText() : std::string();
        while (!typed.empty() && std::isspace(static_cast<unsigned char>(typed.back()))) typed.pop_back();
        while (!typed.empty() && std::isspace(static_cast<unsigned char>(typed.front()))) typed.erase(0, 1);

        // The name field still shows what the listing's selection put there:
        // take the selection itself (several files for a multi-select).
        if (!typed.empty() && typed == autoFileName && filerView) {
            std::vector<std::string> files;
            for (const FilerEntry& e : filerView->GetSelectedEntries()) {
                if (!e.isDirectory) files.push_back(e.path);
            }
            if (!files.empty()) {
                if (!fileConfig.allowMultipleSelection) files.resize(1);
                Accept(files);
                return;
            }
        }

        if (typed.empty()) {
            // Nothing typed: OK opens a folder highlighted in the listing.
            if (filerView) {
                for (const FilerEntry& e : filerView->GetSelectedEntries()) {
                    if (e.isDirectory) { GoToDirectory(e.path, true); break; }
                }
            }
            return;
        }

        // A typed name or path, relative to the folder being shown.
        const std::string full = CombinePath(currentDirectory, typed);
        std::error_code ec;
        if (std::filesystem::is_directory(PathFromUtf8(full), ec)) {
            fileNameInput->SetText("");
            GoToDirectory(full, true);
            return;
        }
        if (fileConfig.dialogType == FileDialogType::Save) {
            std::filesystem::path parent = PathFromUtf8(full).parent_path();
            if (!parent.empty() && !std::filesystem::is_directory(parent, ec)) return;
            Accept({full});
            return;
        }
        // Open: only a file that is there.
        if (std::filesystem::is_regular_file(PathFromUtf8(full), ec)) {
            Accept({full});
        }
    }

    void UltraCanvasFileDialog::Accept(const std::vector<std::string>& files) {
        if (files.empty() || overwritePromptOpen) return;
        // Save over a file that is there: ask first, as the platforms' own
        // save dialogs do. No answers leaves the dialog open on the name.
        if (fileConfig.dialogType == FileDialogType::Save && fileConfig.confirmOverwrite) {
            const std::filesystem::path target =
                    PathFromUtf8(CombinePath(currentDirectory, files.front()));
            std::error_code ec;
            if (std::filesystem::exists(target, ec) && !ec) {
                overwritePromptOpen = true;
                std::weak_ptr<UltraCanvasUIElement> weak = weak_from_this();
                const std::string name = PathToUtf8(target.filename());
                UltraCanvasDialogManager::ShowConfirmation(
                        "\"" + name + "\" already exists.\nDo you want to replace it?",
                        "Replace File",
                        [weak, this, files](bool replace) {
                            if (weak.expired()) return;
                            overwritePromptOpen = false;
                            if (replace) FinishAccept(files);
                        },
                        this);
                return;
            }
        }
        FinishAccept(files);
    }

    void UltraCanvasFileDialog::FinishAccept(const std::vector<std::string>& files) {
        selectedFiles = files;
        if (fileConfig.allowMultipleSelection) {
            if (onFilesSelected) onFilesSelected(GetSelectedFilePaths());
        } else if (onFileSelected) {
            onFileSelected(GetSelectedFilePath());
        }
        CloseDialog(DialogResult::OK);
    }

// ----- navigation -----
    void UltraCanvasFileDialog::NavigateToDirectory(const std::string& dirName) {
        if (dirName == "..") {
            NavigateToParentDirectory();
            return;
        }
        GoToDirectory(CombinePath(currentDirectory, dirName), true);
    }

    void UltraCanvasFileDialog::NavigateToParentDirectory() {
        const std::filesystem::path current = PathFromUtf8(currentDirectory);
        const std::filesystem::path parentPath = current.parent_path();
        if (!parentPath.empty() && parentPath != current) {
            GoToDirectory(PathToUtf8(parentPath), true);
        }
    }

    bool UltraCanvasFileDialog::IsFileMatchingFilter(const std::string& fileName) const {
        return MatchesTypeFilter(fileName);
    }

    std::string UltraCanvasFileDialog::GetFileExtension(const std::string& fileName) const {
        size_t dotPos = fileName.find_last_of('.');
        if (dotPos != std::string::npos && dotPos < fileName.length() - 1) {
            return fileName.substr(dotPos + 1);
        }
        return "";
    }

    std::string UltraCanvasFileDialog::CombinePath(const std::string& dir, const std::string& file) const {
        std::filesystem::path path = PathFromUtf8(dir);
        path /= PathFromUtf8(file);
        return PathToUtf8(path);
    }

// ===== DIALOG MANAGER IMPLEMENTATION =====

// ===== ASYNC CALLBACK-BASED DIALOGS =====
    void UltraCanvasDialogManager::ShowMessage(const std::string& message, const std::string& title,
                                               DialogType type, DialogButtons buttons,
                                               std::function<void(DialogResult)> onResult,
                                               UltraCanvasWindowBase* parent) {
        if (!enabled) {
            if (onResult) onResult(DialogResult::Cancel);
            return;
        }

        // If native dialogs are enabled, use them (blocking call)
        if (useNativeDialogs) {
            DialogResult result = UltraCanvasNativeDialogs::ShowMessage(message, title, type, buttons, parent);
            if (onResult) onResult(result);
            return;
        }

        // Otherwise, use internal UltraCanvas dialogs (non-blocking)
        auto dialog = CreateMessageDialog(message, title, type, buttons);
        ShowDialog(dialog, onResult, parent);
    }

    void UltraCanvasDialogManager::ShowInformation(const std::string& message, const std::string& title,
                                                   std::function<void(DialogResult)> onResult,
                                                   UltraCanvasWindowBase* parent) {
        if (useNativeDialogs && enabled) {
            DialogResult result = UltraCanvasNativeDialogs::ShowInfo(message, title, parent);
            if (onResult) onResult(result);
            return;
        }
        ShowMessage(message, title, DialogType::Information, DialogButtons::OK, onResult, parent);
    }

    void UltraCanvasDialogManager::ShowQuestion(const std::string& message, const std::string& title,
                                                std::function<void(DialogResult)> onResult,
                                                UltraCanvasWindowBase* parent) {
        if (useNativeDialogs && enabled) {
            DialogResult result = UltraCanvasNativeDialogs::ShowQuestion(message, title, DialogButtons::YesNo, parent);
            if (onResult) onResult(result);
            return;
        }
        ShowMessage(message, title, DialogType::Question, DialogButtons::YesNo, onResult, parent);
    }

    void UltraCanvasDialogManager::ShowWarning(const std::string& message, const std::string& title,
                                               std::function<void(DialogResult)> onResult,
                                               UltraCanvasWindowBase* parent) {
        if (useNativeDialogs && enabled) {
            DialogResult result = UltraCanvasNativeDialogs::ShowWarning(message, title, parent);
            if (onResult) onResult(result);
            return;
        }
        ShowMessage(message, title, DialogType::Warning, DialogButtons::OKCancel, onResult, parent);
    }

    void UltraCanvasDialogManager::ShowError(const std::string& message, const std::string& title,
                                             std::function<void(DialogResult)> onResult,
                                             UltraCanvasWindowBase* parent) {
        if (useNativeDialogs && enabled) {
            DialogResult result = UltraCanvasNativeDialogs::ShowError(message, title, parent);
            if (onResult) onResult(result);
            return;
        }
        ShowMessage(message, title, DialogType::Error, DialogButtons::OK, onResult, parent);
    }

    void UltraCanvasDialogManager::ShowConfirmation(const std::string& message, const std::string& title,
                                                    std::function<void(bool confirmed)> onResult,
                                                    UltraCanvasWindowBase* parent) {
        if (useNativeDialogs && enabled) {
            bool confirmed = UltraCanvasNativeDialogs::ConfirmYesNo(message, title, parent);
            if (onResult) onResult(confirmed);
            return;
        }
        ShowMessage(message, title, DialogType::Question, DialogButtons::YesNo,
                    [onResult](DialogResult r) {
                        if (onResult) onResult(r == DialogResult::Yes);
                    }, parent);
    }

// ===== CUSTOM DIALOGS =====
    std::shared_ptr<UltraCanvasModalDialog> UltraCanvasDialogManager::CreateDialog(const DialogConfig& config) {
        auto dialog = std::make_shared<UltraCanvasModalDialog>();
        dialog->CreateDialog(config);
        return dialog;
    }

    void UltraCanvasDialogManager::ShowDialog(std::shared_ptr<UltraCanvasModalDialog> dialog,
                                              std::function<void(DialogResult)> onResult,
                                              UltraCanvasWindowBase* parent) {
        if (!enabled || !dialog) {
            if (onResult) onResult(DialogResult::Cancel);
            return;
        }

        if (onResult) {
            dialog->onResult = onResult;
        }
        dialog->ShowModal(parent);
    }

    void UltraCanvasDialogManager::ShowInputDialog(const std::string& prompt, const std::string& title,
                                                   const std::string& defaultValue, InputType type,
                                                   std::function<void(DialogResult, const std::string&)> onResult,
                                                   UltraCanvasWindowBase* parent) {
        if (!enabled) {
            if (onResult) onResult(DialogResult::Cancel, "");
            return;
        }

        // If native dialogs are enabled, use them (blocking call)
        if (useNativeDialogs) {
            NativeInputResult result;
            if (type == InputType::Password) {
                result = UltraCanvasNativeDialogs::InputPassword(prompt, title, parent);
            } else {
                result = UltraCanvasNativeDialogs::InputText(prompt, title, defaultValue, parent);
            }
            if (onResult) onResult(result.result, result.value);
            return;
        }

        // Otherwise, use internal UltraCanvas dialogs (non-blocking)
        InputDialogConfig config;
        config.title = title;
        config.inputLabel = prompt;
        config.defaultValue = defaultValue;
        config.inputType = type;

        auto dialog = CreateInputDialog(config);
        ShowDialog(dialog, [onResult, dialog](DialogResult result) {
            if (onResult) {
                onResult(result, dialog->GetInputValue());
            }
        }, parent);
    }

    void UltraCanvasDialogManager::CloseAllDialogs() {
        // Close from a copy. Closing a dialog unregisters it - erases it from
        // activeDialogs - and runs its result callback, which may open or close
        // dialogs itself. Walking activeDialogs itself skipped every second
        // dialog (each erase moved the next one under the loop's iterator) and
        // went on to read the vacated slots past its end; and the clear()
        // after it unregistered a dialog a callback had just opened, which
        // stayed on screen out of the manager's reach.
        const std::vector<std::shared_ptr<UltraCanvasModalDialog>> closing = activeDialogs;
        for (const auto& dialog : closing) {
            if (dialog) {
                dialog->CloseDialog(DialogResult::Cancel);
            }
        }
        // Drop whatever of those is still registered (a dialog that was never
        // shown cannot close), but keep a dialog a result callback opened.
        std::erase_if(activeDialogs, [&closing](const std::shared_ptr<UltraCanvasModalDialog>& dialog) {
            return std::find(closing.begin(), closing.end(), dialog) != closing.end();
        });
    }

    std::shared_ptr<UltraCanvasModalDialog> UltraCanvasDialogManager::GetCurrentModalDialog() {
        auto* app = UltraCanvasApplication::GetInstance();
        auto* modalWin = app ? app->GetCurrentModalWindow() : nullptr;
        if (modalWin) {
            for (auto& dialog : activeDialogs) {
                if (dialog.get() == modalWin) return dialog;
            }
        }
        return nullptr;
    }

    std::vector<std::shared_ptr<UltraCanvasModalDialog>> UltraCanvasDialogManager::GetActiveDialogs() {
        return activeDialogs;
    }

    int UltraCanvasDialogManager::GetActiveDialogCount() {
        return static_cast<int>(activeDialogs.size());
    }

    void UltraCanvasDialogManager::SetDefaultConfig(const DialogConfig& config) {
        defaultConfig = config;
    }

    void UltraCanvasDialogManager::SetDefaultInputConfig(const InputDialogConfig& config) {
        defaultInputConfig = config;
    }

    void UltraCanvasDialogManager::SetDefaultFileConfig(const FileDialogConfig& config) {
        defaultFileConfig = config;
    }

    DialogConfig UltraCanvasDialogManager::GetDefaultConfig() {
        return defaultConfig;
    }

    InputDialogConfig UltraCanvasDialogManager::GetDefaultInputConfig() {
        return defaultInputConfig;
    }

    FileDialogConfig UltraCanvasDialogManager::GetDefaultFileConfig() {
        return defaultFileConfig;
    }

    void UltraCanvasDialogManager::SetEnabled(bool enable) {
        enabled = enable;
        if (!enabled) {
            CloseAllDialogs();
        }
    }

    bool UltraCanvasDialogManager::IsEnabled() {
        return enabled;
    }

    void UltraCanvasDialogManager::SetUseNativeDialogs(bool useNative) {
        useNativeDialogs = useNative;
    }

    bool UltraCanvasDialogManager::GetUseNativeDialogs() {
        return useNativeDialogs;
    }

    void UltraCanvasDialogManager::Update(float deltaTime) {
        if (!enabled) return;

        // Clean up closed dialogs
        activeDialogs.erase(
                std::remove_if(activeDialogs.begin(), activeDialogs.end(),
                               [](const std::shared_ptr<UltraCanvasModalDialog>& dialog) {
                                   return !dialog || !dialog->IsVisible();
                               }),
                activeDialogs.end()
        );
    }

    std::string UltraCanvasDialogManager::DialogResultToString(DialogResult result) {
        switch (result) {
            case DialogResult::OK:       return "OK";
            case DialogResult::Cancel:   return "Cancel";
            case DialogResult::Yes:      return "Yes";
            case DialogResult::No:       return "No";
            case DialogResult::Apply:    return "Apply";
            case DialogResult::Close:    return "Close";
            case DialogResult::Help:     return "Help";
            case DialogResult::Retry:    return "Retry";
            case DialogResult::Ignore:   return "Ignore";
            case DialogResult::Abort:    return "Abort";
            case DialogResult::NoResult:
            default:                     return "NoResult";
        }
    }

    DialogResult UltraCanvasDialogManager::StringToDialogResult(const std::string& str) {
        if (str == "OK") return DialogResult::OK;
        if (str == "Cancel") return DialogResult::Cancel;
        if (str == "Yes") return DialogResult::Yes;
        if (str == "No") return DialogResult::No;
        if (str == "Apply") return DialogResult::Apply;
        if (str == "Close") return DialogResult::Close;
        if (str == "Help") return DialogResult::Help;
        if (str == "Retry") return DialogResult::Retry;
        if (str == "Ignore") return DialogResult::Ignore;
        if (str == "Abort") return DialogResult::Abort;
        return DialogResult::NoResult;
    }

    std::string UltraCanvasDialogManager::DialogButtonToString(DialogButton button) {
        switch (button) {
            case DialogButton::OK:         return "OK";
            case DialogButton::Cancel:     return "Cancel";
            case DialogButton::Yes:        return "Yes";
            case DialogButton::No:         return "No";
            case DialogButton::Apply:      return "Apply";
            case DialogButton::Close:      return "Close";
            case DialogButton::Help:       return "Help";
            case DialogButton::Retry:      return "Retry";
            case DialogButton::Ignore:     return "Ignore";
            case DialogButton::Abort:      return "Abort";
            case DialogButton::NoneButton:
            default:                       return "None";
        }
    }

    DialogButton UltraCanvasDialogManager::StringToDialogButton(const std::string& str) {
        if (str == "OK") return DialogButton::OK;
        if (str == "Cancel") return DialogButton::Cancel;
        if (str == "Yes") return DialogButton::Yes;
        if (str == "No") return DialogButton::No;
        if (str == "Apply") return DialogButton::Apply;
        if (str == "Close") return DialogButton::Close;
        if (str == "Help") return DialogButton::Help;
        if (str == "Retry") return DialogButton::Retry;
        if (str == "Ignore") return DialogButton::Ignore;
        if (str == "Abort") return DialogButton::Abort;
        return DialogButton::NoneButton;
    }

    void UltraCanvasDialogManager::RegisterDialog(std::shared_ptr<UltraCanvasModalDialog> dialog) {
        if (dialog) {
            activeDialogs.push_back(dialog);
        }
    }

    void UltraCanvasDialogManager::UnregisterDialog(std::shared_ptr<UltraCanvasModalDialog> dialog) {
        auto it = std::find(activeDialogs.begin(), activeDialogs.end(), dialog);
        if (it != activeDialogs.end()) {
            activeDialogs.erase(it);
        }
    }

    std::shared_ptr<UltraCanvasModalDialog> UltraCanvasDialogManager::CreateMessageDialog(
            const std::string& message, const std::string& title,
            DialogType type, DialogButtons buttons) {

        DialogConfig config = defaultConfig;
        config.message = message;
        config.title = title;
        config.dialogType = type;
        config.buttons = buttons;

        return CreateDialog(config);
    }

    std::shared_ptr<UltraCanvasInputDialog> UltraCanvasDialogManager::CreateInputDialog(
            const InputDialogConfig& config) {
        auto dialog = std::make_shared<UltraCanvasInputDialog>();
        dialog->CreateInputDialog(config);
        return dialog;
    }

    std::shared_ptr<UltraCanvasFileDialog> UltraCanvasDialogManager::CreateFileDialog(
            const FileDialogConfig& config) {
        auto dialog =  std::make_shared<UltraCanvasFileDialog>();
        dialog->CreateFileDialog(config);
        return dialog;
    }

    void FileDialogConfig::SetFiltersFromString(const std::string &filterString) {
        filters.clear();
        selectedFilterIndex = 0;

        if (filterString.empty()) return;

        std::vector<std::string> parts;
        std::stringstream ss(filterString);
        std::string part;
        while (std::getline(ss, part, '|')) {
            parts.push_back(part);
        }

        for (size_t i = 0; i + 1 < parts.size(); i += 2) {
            std::string desc = parts[i];
            std::string extPattern = parts[i + 1];

            std::vector<std::string> extensions;
            std::stringstream extSs(extPattern);
            std::string ext;
            while (std::getline(extSs, ext, ';')) {
                // Remove "*." prefix if present
                if (ext.substr(0, 2) == "*.") {
                    ext = ext.substr(2);
                }
                if (!ext.empty()) {
                    extensions.push_back(ext);
                }
            }

            if (!extensions.empty()) {
                filters.emplace_back(desc, extensions);
            }
        }
    }

    FileDialogConfig::FileDialogConfig() : DialogConfig() {
        buttons = DialogButtons::OKCancel;
        width = 900;
        height = 560;
        resizable = true;
        // No filters: the caller names the files it wants, and a file dialog
        // left without any lists everything (CreateFileDialog adds "All
        // Files"). A sample list here showed types the caller never asked for.
    }
} // namespace UltraCanvas