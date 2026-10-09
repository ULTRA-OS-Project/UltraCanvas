// Apps/UltraMail/ui/UltraMailSettingsDialog.cpp
// UltraMail settings window - the same window as UltraFiler's settings: the
// settings-page tree on the left, its sections (Mail, Reading, Privacy, Display)
// closed when the window opens, so it opens on the start page that says what
// they hold.
// Pages: Mail > New mail (how often every account is checked for new mail),
// Reading > Layout (the message beside the list or in its place, and
// the folder tree's width - fitted to its names or a fixed number of pixels),
// Reading > Messages (HTML mail formatted or as plain text, and the size of
// the message text), Reading > Waiting for reply (which unanswered mail
// counts: how old it may be, and whether only people written to), Privacy > Images (when pictures on the web are loaded:
// always, only from trusted senders / websites / the address book, or never
// by themselves - plus the lists of trusted websites and senders) and
// Privacy > Sender icons (whether the known senders' icons are downloaded),
// Display > Links (a link's address in the status bar or in a tooltip) and
// Display > Notifications (a notification on screen when new mail arrives) and
// Warnings > Spam/scam warnings (which kinds of warning the content scan gives)
// and Warnings > Trusted and blocked senders (the sender menu's lists).
//
// Every page is built the same way (MakePage): a bold title, the one-line
// caption that says what the choice is about, the controls, and - set apart
// at the foot of the page in its own tinted block - the notes that explain
// the setting. A page's "Restore default ..." button sits at the left end of
// the bottom bar, opposite Close. Changes apply live and are saved at once.
// Version: 1.9.0 - Warnings > Trusted and blocked senders
// Version: 1.8.0 - Warnings > Spam/scam warnings: one switch per kind of warning
// Version: 1.7.0 - Display > Notifications: new mail on screen, or not
// Version: 1.6.0 - Privacy > Sender icons: the website icons of other senders
// Version: 1.5.0 - Mail > New mail: how often new mail is checked (a dropdown,
//                  20 seconds to 10 minutes)
// Version: 1.4.0 - Reading > Waiting for reply: which unanswered mail counts
//                  (its age, only people written to)
// Version: 1.3.0 - Display > Links: a link's address in the status bar or a tooltip
// Version: 1.2.0 - Reading > Layout: the folder tree's width (fit to the names,
//                  or fixed pixels)
// Version: 1.1.0 - MakeGearButton: the one gear, for the toolbar and the start page
// Last Modified: 2026-10-05
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraMailSettingsDialog.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasChip.h"   // UltraCanvasTagInput
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasSpinner.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"

#include <cctype>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace UltraCanvas;

namespace UltraMail {

namespace {

    // ----- one type scale for the whole window (as UltraFiler's) -----
    constexpr float kTitleFontSize = 15.0f;   // page title (bold)
    constexpr float kTextFontSize  = 10.0f;   // captions, choices, buttons, fields
    constexpr float kNoteFontSize  = 9.0f;    // the notes under the controls
    constexpr float kTreeFontSize  = 9.0f;

    const Color kTextColor      = Color(40, 40, 44, 255);
    const Color kNoteTextColor  = Color(96, 96, 104, 255);
    const Color kNoteBackground = Color(244, 245, 248, 255);
    const Color kNoteAccent     = Color(160, 176, 200, 255);

    // Page geometry: the window is 700 wide, the tree takes 180, so a page has
    // 520; with its padding that leaves ~480 for content. Texts wrap at a
    // fixed width: content measuring needs a render context, which the window
    // does not have while it is first laid out.
    constexpr int kPagePadding   = 20;
    constexpr int kTextWidth     = 440;
    constexpr int kNoteWidth     = 430;
    constexpr int kControlHeight = 22;

    // Page ids double as tree node ids.
    constexpr const char* kPageMail        = "mail";
    constexpr const char* kPageNewMail     = "mail/new-mail";
    constexpr const char* kPageReading     = "reading";
    constexpr const char* kPageLayout      = "reading/layout";
    constexpr const char* kPageMessages    = "reading/messages";
    constexpr const char* kPageWaiting     = "reading/waiting";
    constexpr const char* kPagePrivacy     = "privacy";
    constexpr const char* kPageImages      = "privacy/images";
    constexpr const char* kPageSenderIcons = "privacy/sender-icons";
    constexpr const char* kPageDisplay     = "display";
    constexpr const char* kPageLinks       = "display/links";
    constexpr const char* kPageNotify      = "display/notifications";
    constexpr const char* kPageWarnings    = "warnings";
    constexpr const char* kPageScamWarnings = "warnings/spam-scam";
    constexpr const char* kPageSenderLists  = "warnings/senders";
    constexpr const char* kPageStart       = "start";

    // The text sizes offered on Reading > Messages, in CSS px.
    struct TextSizeChoice { int px; const char* label; };
    constexpr TextSizeChoice kTextSizes[] = {
        { 11, "Small" }, { 12, "Normal" }, { 14, "Large" }, { 16, "Extra large" } };

    // The ages offered on Reading > Waiting for reply, in days (0 = any age).
    struct AgeChoice { int days; const char* label; };
    constexpr AgeChoice kWaitingAges[] = {
        { 7, "From the last 7 days" }, { 14, "From the last 14 days" },
        { 30, "From the last 30 days" }, { 0, "Of any age" } };

    struct PageReset {
        std::string           label;
        int                   width = 170;
        std::function<void()> action;
    };

    struct DialogState {
        std::shared_ptr<UltraCanvasWindow>    window;
        bool                                  closed = false;
        // Set while controls are pushed from the preferences: the change
        // handlers that fires must not save and re-apply once more.
        bool                                  syncing = false;
        std::shared_ptr<UltraCanvasTreeView>  tree;
        std::shared_ptr<UltraCanvasContainer> pageArea;
        std::map<std::string, std::shared_ptr<UltraCanvasContainer>> pages;
        std::map<std::string, PageReset>      resets;
        std::shared_ptr<UltraCanvasButton>    restoreButton;
        std::string                           shownPage;

        // Mail > New mail
        std::shared_ptr<UltraCanvasDropdown> checkMailDropdown;

        // Reading > Layout
        std::shared_ptr<UltraCanvasRadio> paneBesideRadio;
        std::shared_ptr<UltraCanvasRadio> paneInPlaceRadio;
        UltraCanvasRadioGroup             paneGroup;
        std::shared_ptr<UltraCanvasRadio>   treeFitRadio;
        std::shared_ptr<UltraCanvasRadio>   treeFixedRadio;
        UltraCanvasRadioGroup               treeWidthGroup;
        std::shared_ptr<UltraCanvasSpinner> treeWidthSpinner;

        // Reading > Messages
        std::shared_ptr<UltraCanvasRadio> htmlRadio;
        std::shared_ptr<UltraCanvasRadio> plainRadio;
        UltraCanvasRadioGroup             viewGroup;
        std::vector<std::pair<int, std::shared_ptr<UltraCanvasRadio>>> sizeRadios;
        UltraCanvasRadioGroup             sizeGroup;

        // Reading > Waiting for reply
        std::vector<std::pair<int, std::shared_ptr<UltraCanvasRadio>>> ageRadios;
        UltraCanvasRadioGroup                ageGroup;
        std::shared_ptr<UltraCanvasCheckbox> writtenToBox;

        // Privacy > Images
        std::shared_ptr<UltraCanvasRadio>    imagesAlwaysRadio;
        std::shared_ptr<UltraCanvasRadio>    imagesTrustedRadio;
        std::shared_ptr<UltraCanvasRadio>    imagesNeverRadio;
        UltraCanvasRadioGroup                imagesGroup;
        std::shared_ptr<UltraCanvasTagInput> domainsInput;
        std::shared_ptr<UltraCanvasTagInput> sendersInput;

        // Privacy > Sender icons
        std::shared_ptr<UltraCanvasCheckbox> senderIconsBox;
        std::shared_ptr<UltraCanvasCheckbox> siteIconsBox;

        // Display > Links
        std::shared_ptr<UltraCanvasRadio> linksStatusRadio;
        std::shared_ptr<UltraCanvasRadio> linksTooltipRadio;
        UltraCanvasRadioGroup             linksGroup;

        // Display > Notifications
        std::shared_ptr<UltraCanvasCheckbox> notifyBox;

        // Warnings > Spam/scam warnings: one box per kind, with the option it sets
        std::vector<std::pair<bool ThreatScanOptions::*,
                              std::shared_ptr<UltraCanvasCheckbox>>> warningBoxes;

        // Warnings > Trusted and blocked senders
        std::shared_ptr<UltraCanvasTagInput> trustedSendersInput;
        std::shared_ptr<UltraCanvasTagInput> blockedSendersInput;

        Preferences*          prefs = nullptr;
        std::function<void()> onChanged;
    };

    std::shared_ptr<DialogState> g_dialog;

    void ApplyAndSave(DialogState* d) {
        if (d->syncing) return;
        if (d->onChanged) d->onChanged();
    }

    // ===== TEXT =====

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
    // same order and spacing on every page.
    struct PageParts {
        std::shared_ptr<UltraCanvasContainer> page;
        std::shared_ptr<UltraCanvasContainer> body;
        std::shared_ptr<UltraCanvasContainer> notes;
    };

    PageParts MakePage(const std::string& id, const std::string& title,
                       const std::string& caption) {
        PageParts parts;
        parts.page = std::make_shared<UltraCanvasContainer>(id);
        parts.page->layout.SetFlexColumn().SetFlexGap(0)
                          .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        parts.page->SetPadding(kPagePadding, kPagePadding, kPagePadding, kPagePadding);
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
        parts.page->AddChild(parts.body);

        // The spacer pushes the notes to the foot of the page, away from the
        // controls, and gives way when the window is made small.
        auto spacer = std::make_shared<UltraCanvasContainer>(id + "-spacer");
        spacer->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        spacer->size.height = CSSLayout::Dimension::Px(24);
        parts.page->AddChild(spacer);

        parts.notes = std::make_shared<UltraCanvasContainer>(id + "-notes");
        parts.notes->layout.SetFlexColumn().SetFlexGap(6)
                           .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        parts.notes->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                               .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        parts.notes->SetBackgroundColor(kNoteBackground);
        parts.notes->SetBorderLeft(3, kNoteAccent);
        parts.notes->SetPadding(10, 12, 10, 12);
        parts.page->AddChild(parts.notes);
        return parts;
    }

    void AddNote(PageParts& parts, const std::string& id, const std::string& text) {
        parts.notes->AddChild(MakeText(id, text, kNoteWidth, kNoteFontSize, kNoteTextColor));
    }

    // A caption inside the body, introducing a further group of controls.
    void AddBodyCaption(PageParts& parts, const std::string& id, const std::string& text) {
        auto l = MakeText(id, text);
        l->SetMargin(10, 0, 0, 0);
        parts.body->AddChild(l);
    }

    // ===== CONTROLS =====

    std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& id,
                                                  const std::string& label, int width,
                                                  std::function<void()> onClick) {
        auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, width, 28, label);
        b->SetFontSize(kTextFontSize);
        b->SetCornerRadius(4.0f);
        b->SetColors(Color(255, 255, 255, 255), Color(233, 238, 244, 255));
        b->SetTextColors(kTextColor);
        b->SetBorder(1.0f, Color(0, 0, 0, 60));
        if (onClick) b->SetOnClick(std::move(onClick));
        b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        return b;
    }

    std::shared_ptr<UltraCanvasRadio> MakeChoice(const std::string& id,
                                                 const std::string& text, bool checked,
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

    std::shared_ptr<UltraCanvasCheckbox> MakeCheckbox(const std::string& id,
                                                      const std::string& text, bool checked,
                                                      std::function<void(bool)> onChange) {
        auto box = std::make_shared<UltraCanvasCheckbox>(id, 0, 0,
                static_cast<float>(kTextWidth), static_cast<float>(kControlHeight), text);
        box->SetFontSize(kTextFontSize);
        CheckboxVisualStyle style = box->GetVisualStyle();
        style.base.textColor      = kTextColor;
        style.base.textHoverColor = kTextColor;
        box->SetVisualStyle(style);
        box->SetChecked(checked);
        box->size.width  = CSSLayout::Dimension::Px(kTextWidth);
        box->size.height = CSSLayout::Dimension::Px(kControlHeight);
        box->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        box->onStateChanged = [onChange](CheckedState, CheckedState now) {
            if (onChange) onChange(now == CheckedState::Checked);
        };
        return box;
    }

    std::shared_ptr<UltraCanvasTagInput> MakeTagField(const std::string& id,
                                                      const std::string& placeholder) {
        auto input = CreateTagInput(id, -1, -1, static_cast<float>(kTextWidth));
        input->SetPlaceholder(placeholder);
        input->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        input->size.width = CSSLayout::Dimension::Px(kTextWidth);
        return input;
    }

    // ===== PUSHING THE PREFERENCES ONTO THE CONTROLS =====

    std::vector<std::string> DomainTags(const Preferences& p) {
        return { p.trustedImageDomains.begin(), p.trustedImageDomains.end() };
    }
    std::vector<std::string> SenderTags(const Preferences& p) {
        return { p.remoteImageSenders.begin(), p.remoteImageSenders.end() };
    }
    std::vector<std::string> ListTags(const std::set<std::string>& list) {
        return { list.begin(), list.end() };
    }

    void SyncControls(DialogState* d) {
        if (!d || !d->prefs) return;
        const Preferences& p = *d->prefs;
        d->syncing = true;
        if (d->checkMailDropdown) {
            const auto& choices = Preferences::CheckMailChoices();
            for (std::size_t i = 0; i < choices.size(); ++i)
                if (choices[i] == p.checkMailEverySec)
                    d->checkMailDropdown->SetSelectedIndex(static_cast<int>(i),
                                                           /*runNotifications=*/false);
        }
        if (d->paneBesideRadio)
            d->paneGroup.SelectButton(p.showReadingPane ? d->paneBesideRadio : d->paneInPlaceRadio);
        if (d->treeFitRadio)
            d->treeWidthGroup.SelectButton(p.folderTreeWidthMode == FolderTreeWidthMode::FitToText
                                           ? d->treeFitRadio : d->treeFixedRadio);
        if (d->treeWidthSpinner) d->treeWidthSpinner->SetValue(p.folderTreeWidth);
        if (d->htmlRadio)
            d->viewGroup.SelectButton(p.showHtml ? d->htmlRadio : d->plainRadio);
        for (const auto& [px, radio] : d->sizeRadios)
            if (px == p.messageTextSize) d->sizeGroup.SelectButton(radio);
        for (const auto& [days, radio] : d->ageRadios)
            if (days == p.needsAnswerMaxAgeDays) d->ageGroup.SelectButton(radio);
        if (d->writtenToBox) d->writtenToBox->SetChecked(p.needsAnswerOnlyWrittenTo);
        if (d->imagesAlwaysRadio) {
            d->imagesGroup.SelectButton(
                p.remoteImages == RemoteImagePolicy::LoadAlways ? d->imagesAlwaysRadio
                : p.remoteImages == RemoteImagePolicy::LoadNever ? d->imagesNeverRadio
                                                             : d->imagesTrustedRadio);
        }
        if (d->domainsInput) d->domainsInput->SetTags(DomainTags(p));
        if (d->sendersInput) d->sendersInput->SetTags(SenderTags(p));
        if (d->senderIconsBox) d->senderIconsBox->SetChecked(p.fetchSenderIcons);
        if (d->siteIconsBox)   d->siteIconsBox->SetChecked(p.fetchSiteIcons);
        if (d->linksStatusRadio)
            d->linksGroup.SelectButton(p.linkDisplay == LinkDisplay::Tooltip
                                       ? d->linksTooltipRadio : d->linksStatusRadio);
        if (d->notifyBox) d->notifyBox->SetChecked(p.notifyNewMail);
        for (const auto& [option, box] : d->warningBoxes)
            if (box) box->SetChecked(p.scamWarnings.*option);
        if (d->trustedSendersInput)
            d->trustedSendersInput->SetTags(ListTags(p.senderLists.trusted));
        if (d->blockedSendersInput)
            d->blockedSendersInput->SetTags(ListTags(p.senderLists.blocked));
        d->syncing = false;
        if (d->window) d->window->RequestRedraw();
    }

    // ===== MAIL > NEW MAIL =====
    std::shared_ptr<UltraCanvasContainer> BuildNewMailPage(DialogState* d) {
        PageParts parts = MakePage("um-set-page-new-mail", "New mail",
                "How often every account is checked for new mail:");

        auto dropdown = CreateDropdown("um-set-check-mail", 0, 0, 160, kControlHeight + 4);
        const auto& choices = Preferences::CheckMailChoices();
        for (int seconds : choices)
            dropdown->AddItem("Every " + Preferences::CheckMailLabel(seconds),
                              std::to_string(seconds));
        {
            DropdownStyle style = dropdown->GetStyle();
            style.fontSize = kTextFontSize;
            // All ten choices at once: the shortest and the longest are the
            // ones a scrolled list would hide.
            style.maxVisibleItems = static_cast<int>(choices.size());
            dropdown->SetStyle(style);
        }
        for (std::size_t i = 0; i < choices.size(); ++i)
            if (choices[i] == d->prefs->checkMailEverySec)
                dropdown->SetSelectedIndex(static_cast<int>(i), /*runNotifications=*/false);
        dropdown->onSelectionChanged = [d](int index, const DropdownItem&) {
            const auto& offered = Preferences::CheckMailChoices();
            if (!d->prefs || index < 0 || index >= static_cast<int>(offered.size())) return;
            d->prefs->checkMailEverySec = offered[static_cast<std::size_t>(index)];
            ApplyAndSave(d);
        };
        dropdown->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        d->checkMailDropdown = dropdown;
        parts.body->AddChild(dropdown);

        d->resets[kPageNewMail] = PageReset{ "Restore default", 140, [d]() {
            if (!d->prefs) return;
            d->prefs->checkMailEverySec = Preferences::kDefaultCheckMailSec;
            SyncControls(d);
            ApplyAndSave(d);
        } };

        AddNote(parts, "um-set-new-mail-note1",
                "Every account is checked in the background at this interval, and "
                "right after UltraMail starts and the computer wakes up. Update in "
                "the toolbar checks the account on screen straight away, whatever "
                "is set here.");
        AddNote(parts, "um-set-new-mail-note2",
                "A check that is still running when the next one is due is left "
                "to finish - an account is never checked twice at once.");
        AddNote(parts, "um-set-new-mail-note3",
                "A short interval signs in to the server more often. Some "
                "providers limit how often that may happen; if an account starts "
                "to report sign-in errors, choose a longer interval.");
        return parts.page;
    }

    // ===== READING > LAYOUT =====
    std::shared_ptr<UltraCanvasContainer> BuildLayoutPage(DialogState* d) {
        PageParts parts = MakePage("um-set-page-layout", "Layout",
                "Where a message opens when it is clicked in the list:");

        const bool beside = d->prefs->showReadingPane;
        d->paneBesideRadio = MakeChoice("um-set-pane-beside",
                "Beside the list, in the preview pane", beside);
        d->paneInPlaceRadio = MakeChoice("um-set-pane-inplace",
                "In place of the list, with \"Back to list\" to return", !beside);
        d->paneGroup.AddRadioButton(d->paneBesideRadio);
        d->paneGroup.AddRadioButton(d->paneInPlaceRadio);
        d->paneGroup.onSelectionChanged = [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->prefs) return;
            d->prefs->showReadingPane = (selected == d->paneBesideRadio);
            ApplyAndSave(d);
        };
        parts.body->AddChild(d->paneBesideRadio);
        parts.body->AddChild(d->paneInPlaceRadio);

        // ----- folder tree width: fitted, or [ 200 px ] -----
        AddBodyCaption(parts, "um-set-tree-width-caption", "Width of the folder list:");
        const bool fit = d->prefs->folderTreeWidthMode == FolderTreeWidthMode::FitToText;
        d->treeFitRadio = MakeChoice("um-set-tree-fit",
                "Auto - 10 px wider than the longest folder or account name", fit);
        d->treeFixedRadio = MakeChoice("um-set-tree-fixed", "Fixed width:", !fit, 110);
        d->treeWidthGroup.AddRadioButton(d->treeFitRadio);
        d->treeWidthGroup.AddRadioButton(d->treeFixedRadio);
        d->treeWidthGroup.onSelectionChanged = [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->prefs) return;
            d->prefs->folderTreeWidthMode = selected == d->treeFitRadio
                    ? FolderTreeWidthMode::FitToText : FolderTreeWidthMode::FixedWidth;
            ApplyAndSave(d);
        };

        d->treeWidthSpinner = CreateIntSpinner("um-set-tree-width", 0, 0, 90,
                static_cast<float>(kControlHeight), Preferences::kFolderTreeMinWidth,
                Preferences::kFolderTreeMaxWidth, d->prefs->folderTreeWidth, 10);
        d->treeWidthSpinner->SetSuffix(" px");
        d->treeWidthSpinner->GetStyle().fontStyle.fontSize = kTextFontSize;
        d->treeWidthSpinner->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        // Setting a width is choosing a fixed one.
        d->treeWidthSpinner->onValueChanged = [d](double value) {
            if (d->syncing || !d->prefs) return;
            d->prefs->folderTreeWidth = static_cast<int>(std::lround(value));
            if (d->prefs->folderTreeWidthMode != FolderTreeWidthMode::FixedWidth) {
                d->prefs->folderTreeWidthMode = FolderTreeWidthMode::FixedWidth;
                d->syncing = true;
                d->treeWidthGroup.SelectButton(d->treeFixedRadio);
                d->syncing = false;
            }
            ApplyAndSave(d);
        };

        auto fixedRow = std::make_shared<UltraCanvasContainer>("um-set-tree-fixed-row");
        fixedRow->layout.SetFlexRow().SetFlexGap(6)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        fixedRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        fixedRow->AddChild(d->treeFixedRadio);
        fixedRow->AddChild(d->treeWidthSpinner);
        parts.body->AddChild(d->treeFitRadio);
        parts.body->AddChild(fixedRow);

        d->resets[kPageLayout] = PageReset{ "Restore default layout", 170, [d]() {
            if (!d->prefs) return;
            d->prefs->showReadingPane = true;
            d->prefs->folderTreeWidthMode = FolderTreeWidthMode::FitToText;
            d->prefs->folderTreeWidth = Preferences::kFolderTreeDefaultWidth;
            SyncControls(d);
            ApplyAndSave(d);
        } };

        AddNote(parts, "um-set-layout-note1",
                "Beside the list, the list stays in view while you read, and the "
                "arrow keys move from message to message.");
        AddNote(parts, "um-set-layout-note2",
                "In place of the list, a message gets the whole width of the "
                "window - better on a small screen - and the list comes back with "
                "\"Back to list\".");
        AddNote(parts, "um-set-layout-note3",
                "Auto fits the folder list to the account and folder names it "
                "shows, and fits it again as folders arrive. Dragging the divider "
                "still resizes the list for the moment.");
        return parts.page;
    }

    // ===== READING > MESSAGES =====
    std::shared_ptr<UltraCanvasContainer> BuildMessagesPage(DialogState* d) {
        PageParts parts = MakePage("um-set-page-messages", "Messages",
                "How the text of a message is shown:");

        const bool html = d->prefs->showHtml;
        d->htmlRadio = MakeChoice("um-set-view-html",
                "Formatted, as the sender designed it (HTML)", html);
        d->plainRadio = MakeChoice("um-set-view-plain",
                "As plain text - no layout, no pictures", !html);
        d->viewGroup.AddRadioButton(d->htmlRadio);
        d->viewGroup.AddRadioButton(d->plainRadio);
        d->viewGroup.onSelectionChanged = [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->prefs) return;
            d->prefs->showHtml = (selected == d->htmlRadio);
            ApplyAndSave(d);
        };
        parts.body->AddChild(d->htmlRadio);
        parts.body->AddChild(d->plainRadio);

        AddBodyCaption(parts, "um-set-size-caption", "Text size:");
        for (const auto& choice : kTextSizes) {
            auto radio = MakeChoice("um-set-size-" + std::to_string(choice.px),
                    std::string(choice.label) + " (" + std::to_string(choice.px) + " px)",
                    d->prefs->messageTextSize == choice.px);
            d->sizeRadios.emplace_back(choice.px, radio);
            d->sizeGroup.AddRadioButton(radio);
            parts.body->AddChild(radio);
        }
        d->sizeGroup.onSelectionChanged = [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->prefs) return;
            for (const auto& [px, radio] : d->sizeRadios)
                if (radio == selected) d->prefs->messageTextSize = px;
            ApplyAndSave(d);
        };

        d->resets[kPageMessages] = PageReset{ "Restore default view", 170, [d]() {
            if (!d->prefs) return;
            d->prefs->showHtml = true;
            d->prefs->messageTextSize = 12;
            SyncControls(d);
            ApplyAndSave(d);
        } };

        AddNote(parts, "um-set-messages-note1",
                "Plain text shows the words of an HTML message without its "
                "layout. Nothing it links to is downloaded, so it is also the "
                "most private way to read mail.");
        AddNote(parts, "um-set-messages-note2",
                "The text size is where a message's text starts from: text, "
                "headings and small print that the message does not size itself "
                "follow it. Sizes a message sets in pixels stay as they are.");
        return parts.page;
    }

    // ===== READING > WAITING FOR REPLY =====
    std::shared_ptr<UltraCanvasContainer> BuildWaitingPage(DialogState* d) {
        PageParts parts = MakePage("um-set-page-waiting", "Waiting for reply",
                "Which unanswered mail sent to you counts as waiting for your reply:");

        for (const auto& choice : kWaitingAges) {
            auto radio = MakeChoice("um-set-age-" + std::to_string(choice.days), choice.label,
                                    d->prefs->needsAnswerMaxAgeDays == choice.days);
            d->ageRadios.emplace_back(choice.days, radio);
            d->ageGroup.AddRadioButton(radio);
            parts.body->AddChild(radio);
        }
        d->ageGroup.onSelectionChanged = [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->prefs) return;
            for (const auto& [days, radio] : d->ageRadios)
                if (radio == selected) d->prefs->needsAnswerMaxAgeDays = days;
            ApplyAndSave(d);
        };

        AddBodyCaption(parts, "um-set-waiting-who-caption", "From whom:");
        d->writtenToBox = MakeCheckbox("um-set-waiting-written-to",
                "Only from people I have written to", d->prefs->needsAnswerOnlyWrittenTo,
                [d](bool on) {
            if (!d->prefs) return;
            d->prefs->needsAnswerOnlyWrittenTo = on;
            ApplyAndSave(d);
        });
        parts.body->AddChild(d->writtenToBox);

        d->resets[kPageWaiting] = PageReset{ "Restore default", 140, [d]() {
            if (!d->prefs) return;
            d->prefs->needsAnswerMaxAgeDays = 14;
            d->prefs->needsAnswerOnlyWrittenTo = true;
            SyncControls(d);
            ApplyAndSave(d);
        } };

        AddNote(parts, "um-set-waiting-note1",
                "Only mail in the inbox that was sent to you by name, is not a "
                "newsletter or an automatic message, and has not been answered "
                "can wait for a reply. These choices narrow that down: the count "
                "on the account and the \xE2\x86\xA9 in the list follow them.");
        AddNote(parts, "um-set-waiting-note2",
                "\"People I have written to\" are the addresses in your Sent "
                "mail. Until the Sent folder has been fetched, this choice "
                "leaves every sender in.");
        AddNote(parts, "um-set-waiting-note3",
                "A message you mark \"Needs an answer\" yourself always counts, "
                "whatever its age or sender.");
        return parts.page;
    }

    // ===== PRIVACY > IMAGES =====
    std::shared_ptr<UltraCanvasContainer> BuildImagesPage(DialogState* d) {
        PageParts parts = MakePage("um-set-page-images", "Images",
                "When the pictures a message links to on the web are downloaded:");

        const RemoteImagePolicy policy = d->prefs->remoteImages;
        d->imagesAlwaysRadio = MakeChoice("um-set-img-always", "Load always",
                policy == RemoteImagePolicy::LoadAlways);
        d->imagesTrustedRadio = MakeChoice("um-set-img-trusted",
                "Load only from trusted websites, trusted senders and my contacts",
                policy == RemoteImagePolicy::LoadTrusted);
        d->imagesNeverRadio = MakeChoice("um-set-img-never",
                "Never load them by themselves - ask each time",
                policy == RemoteImagePolicy::LoadNever);
        d->imagesGroup.AddRadioButton(d->imagesAlwaysRadio);
        d->imagesGroup.AddRadioButton(d->imagesTrustedRadio);
        d->imagesGroup.AddRadioButton(d->imagesNeverRadio);
        d->imagesGroup.onSelectionChanged = [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->prefs) return;
            d->prefs->remoteImages =
                selected == d->imagesAlwaysRadio ? RemoteImagePolicy::LoadAlways
                : selected == d->imagesNeverRadio ? RemoteImagePolicy::LoadNever
                                                  : RemoteImagePolicy::LoadTrusted;
            ApplyAndSave(d);
        };
        parts.body->AddChild(d->imagesAlwaysRadio);
        parts.body->AddChild(d->imagesTrustedRadio);
        parts.body->AddChild(d->imagesNeverRadio);

        // ----- trusted websites -----
        AddBodyCaption(parts, "um-set-img-domains-caption",
                "Trusted websites - type a domain and press Enter:");
        d->domainsInput = MakeTagField("um-set-img-domains", "e.g. example.com");
        d->domainsInput->SetTags(DomainTags(*d->prefs));
        d->domainsInput->onTagsChanged = [d](const std::vector<std::string>& tags) {
            if (d->syncing || !d->prefs) return;
            // Kept as bare domains: "https://www.Example.com/x" is example.com,
            // and something that is no domain at all is dropped.
            std::set<std::string> domains;
            for (const auto& tag : tags) {
                std::string domain = Preferences::NormalizeDomain(tag);
                if (!domain.empty()) domains.insert(domain);
            }
            d->prefs->trustedImageDomains = domains;
            if (std::vector<std::string>(domains.begin(), domains.end()) != tags) {
                d->syncing = true;
                d->domainsInput->SetTags(DomainTags(*d->prefs));
                d->syncing = false;
            }
            ApplyAndSave(d);
        };
        parts.body->AddChild(d->domainsInput);

        // ----- trusted senders -----
        AddBodyCaption(parts, "um-set-img-senders-caption",
                "Trusted senders - type an address and press Enter:");
        d->sendersInput = MakeTagField("um-set-img-senders", "e.g. news@example.com");
        d->sendersInput->SetTags(SenderTags(*d->prefs));
        d->sendersInput->onTagsChanged = [d](const std::vector<std::string>& tags) {
            if (d->syncing || !d->prefs) return;
            std::set<std::string> senders;
            for (std::string tag : tags) {
                std::string addr;
                for (char c : tag)
                    if (!std::isspace(static_cast<unsigned char>(c)))
                        addr += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (addr.size() >= 3 && addr.find('@') != std::string::npos) senders.insert(addr);
            }
            d->prefs->remoteImageSenders = senders;
            if (std::vector<std::string>(senders.begin(), senders.end()) != tags) {
                d->syncing = true;
                d->sendersInput->SetTags(SenderTags(*d->prefs));
                d->syncing = false;
            }
            ApplyAndSave(d);
        };
        parts.body->AddChild(d->sendersInput);

        d->resets[kPageImages] = PageReset{ "Restore default", 140, [d]() {
            if (!d->prefs) return;
            // The choice only - the websites and senders listed are the
            // reader's own, to remove one by one.
            d->prefs->remoteImages = RemoteImagePolicy::LoadTrusted;
            SyncControls(d);
            ApplyAndSave(d);
        } };

        AddNote(parts, "um-set-img-note1",
                "Loading a picture from the web tells its sender that, and when, "
                "you opened the message. Pictures inside the message itself are "
                "always shown.");
        AddNote(parts, "um-set-img-note2",
                "Trusted: mail from a trusted sender, a trusted website's domain "
                "(example.com covers news.example.com too) or a contact shows its "
                "pictures; other mail loads only those hosted on a trusted website. "
                "\"Always from <sender>\" above a message adds the sender here.");
        AddNote(parts, "um-set-img-note3",
                "Junk and suspicious mail never load pictures by themselves; "
                "\"Show images\" above it still can, for that one message.");
        return parts.page;
    }

    // ===== PRIVACY > SENDER ICONS =====
    std::shared_ptr<UltraCanvasContainer> BuildSenderIconsPage(DialogState* d) {
        PageParts parts = MakePage("um-set-page-senders", "Sender icons",
                "The badge beside a message shows who sent it:");

        d->senderIconsBox = MakeCheckbox("um-set-sender-icons",
                "Download the icons of known senders", d->prefs->fetchSenderIcons,
                [d](bool on) {
            if (!d->prefs) return;
            d->prefs->fetchSenderIcons = on;
            ApplyAndSave(d);
        });
        parts.body->AddChild(d->senderIconsBox);

        d->siteIconsBox = MakeCheckbox("um-set-site-icons",
                "Show other senders' website icons", d->prefs->fetchSiteIcons,
                [d](bool on) {
            if (!d->prefs) return;
            d->prefs->fetchSiteIcons = on;
            ApplyAndSave(d);
        });
        parts.body->AddChild(d->siteIconsBox);

        d->resets[kPageSenderIcons] = PageReset{ "Restore default", 140, [d]() {
            if (!d->prefs) return;
            d->prefs->fetchSenderIcons = true;
            d->prefs->fetchSiteIcons   = true;
            SyncControls(d);
            ApplyAndSave(d);
        } };

        AddNote(parts, "um-set-senders-note1",
                "Known senders: the services on UltraMail's own list (banks, "
                "shops, social networks, ...) are asked for their icon once each, "
                "from the address on that list.");
        AddNote(parts, "um-set-senders-note2",
                "Website icons: for any other sender whose mail passed the scam "
                "check, the home page of the domain it writes from is read for "
                "its icon - once, and again after a week if there was none. "
                "That tells the sender's web server that someone looked, though "
                "not which message; never asked for spam, scams, the junk folder "
                "or a mailbox provider such as gmail.com.");
        AddNote(parts, "um-set-senders-note3",
                "Icons are fetched in the background, only for the senders shown "
                "in the list, so they never slow down getting mail. Switched "
                "off, the badge shows the sender's initial instead; icons "
                "already fetched stay. Downloading off turns both off.");
        return parts.page;
    }

    // ===== DISPLAY > LINKS =====
    std::shared_ptr<UltraCanvasContainer> BuildLinksPage(DialogState* d) {
        PageParts parts = MakePage("um-set-page-links", "Links",
                "Where the address behind a link in a message is shown:");

        const bool tooltip = d->prefs->linkDisplay == LinkDisplay::Tooltip;
        d->linksStatusRadio = MakeChoice("um-set-links-status",
                "Show in status bar", !tooltip);
        d->linksTooltipRadio = MakeChoice("um-set-links-tooltip",
                "Show as tooltip", tooltip);
        d->linksGroup.AddRadioButton(d->linksStatusRadio);
        d->linksGroup.AddRadioButton(d->linksTooltipRadio);
        d->linksGroup.onSelectionChanged = [d](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected || !d->prefs) return;
            d->prefs->linkDisplay = selected == d->linksTooltipRadio ? LinkDisplay::Tooltip
                                                                     : LinkDisplay::StatusBar;
            ApplyAndSave(d);
        };
        parts.body->AddChild(d->linksStatusRadio);
        parts.body->AddChild(d->linksTooltipRadio);

        d->resets[kPageLinks] = PageReset{ "Restore default", 140, [d]() {
            if (!d->prefs) return;
            d->prefs->linkDisplay = LinkDisplay::StatusBar;
            SyncControls(d);
            ApplyAndSave(d);
        } };

        AddNote(parts, "um-set-links-note1",
                "In the status bar, the bottom of the window counts a message's "
                "links and names the websites they lead to - its tooltip lists "
                "every link - and shows the full address of the link under the "
                "pointer.");
        AddNote(parts, "um-set-links-note2",
                "As a tooltip, the address appears beside the pointer while it "
                "rests on a link, and the status bar stays free for the mail "
                "itself.");
        AddNote(parts, "um-set-links-note3",
                "Either way, check where a link really goes before you click it: "
                "the words of a link can name one website and lead to another.");
        return parts.page;
    }

    // ===== DISPLAY > NOTIFICATIONS =====
    std::shared_ptr<UltraCanvasContainer> BuildNotificationsPage(DialogState* d) {
        PageParts parts = MakePage("um-set-page-notify", "Notifications",
                "When new mail arrives while UltraMail runs:");

        d->notifyBox = MakeCheckbox("um-set-notify-new-mail",
                "Show a notification on the screen", d->prefs->notifyNewMail,
                [d](bool on) {
            if (!d->prefs) return;
            d->prefs->notifyNewMail = on;
            ApplyAndSave(d);
        });
        parts.body->AddChild(d->notifyBox);

        d->resets[kPageNotify] = PageReset{ "Restore default", 140, [d]() {
            if (!d->prefs) return;
            d->prefs->notifyNewMail = true;
            SyncControls(d);
            ApplyAndSave(d);
        } };

        AddNote(parts, "um-set-notify-note1",
                "The notification names the sender and the subject - or counts "
                "the messages when several came at once - and a click on it "
                "brings UltraMail to the front with the mail open.");
        AddNote(parts, "um-set-notify-note2",
                "It goes out through UltraMessage, the desktop's message "
                "channel, and the desktop's own notification service shows it "
                "like any other program's: its look, its sound and do-not-disturb "
                "are set there.");
        AddNote(parts, "um-set-notify-note3",
                "Mail you have already read elsewhere, and the first download of "
                "a new account, raise no notification.");
        return parts.page;
    }

    // ===== WARNINGS > SPAM/SCAM WARNINGS =====
    std::shared_ptr<UltraCanvasContainer> BuildScamWarningsPage(DialogState* d) {
        PageParts parts = MakePage("um-set-page-scams", "Spam/scam warnings",
                "Warn about a message when it looks like:");

        struct Kind { const char* id; const char* text; bool ThreatScanOptions::* option; };
        static const Kind kinds[] = {
            { "um-set-warn-phishing",
              "Phishing - links or a sender that pretend to be someone else",
              &ThreatScanOptions::phishing },
            { "um-set-warn-romance",
              "A romance scam - a love letter from a stranger",
              &ThreatScanOptions::romance },
            { "um-set-warn-advance-fee",
              "An advance-fee letter - millions waiting for a small fee",
              &ThreatScanOptions::advanceFee },
            { "um-set-warn-government",
              "A letter in the name of the FBI, Interpol, the IMF ...",
              &ThreatScanOptions::government },
            { "um-set-warn-crypto-scams",
              "A cryptocurrency scam - a recovery phrase, a wallet to pay into",
              &ThreatScanOptions::cryptoScams },
            { "um-set-warn-crypto-caution",
              "Any mail about cryptocurrency (a word of caution)",
              &ThreatScanOptions::cryptoCaution },
            { "um-set-warn-attachments",
              "A dangerous attachment - a program, or one dressed as a document",
              &ThreatScanOptions::attachments },
            { "um-set-warn-spam-flag",
              "Spam, as the mail server marked it",
              &ThreatScanOptions::spamFlag },
        };
        d->warningBoxes.clear();
        for (const Kind& kind : kinds) {
            bool ThreatScanOptions::* option = kind.option;
            auto box = MakeCheckbox(kind.id, kind.text, d->prefs->scamWarnings.*option,
                    [d, option](bool on) {
                if (!d->prefs) return;
                d->prefs->scamWarnings.*option = on;
                ApplyAndSave(d);
            });
            d->warningBoxes.emplace_back(option, box);
            parts.body->AddChild(box);
        }

        d->resets[kPageScamWarnings] = PageReset{ "Warn about all", 140, [d]() {
            if (!d->prefs) return;
            d->prefs->scamWarnings = ThreatScanOptions{};
            SyncControls(d);
            ApplyAndSave(d);
        } };

        AddNote(parts, "um-set-scams-note1",
                "A warning is the badge's red or orange frame beside the subject, "
                "the reasons in its tooltip and the strip above the message. A kind "
                "switched off is not looked for: its messages are labelled as if "
                "the check did not exist.");
        AddNote(parts, "um-set-scams-note2",
                "Mail already checked is checked again with the new choice - the "
                "open message at once, the others in the background with each "
                "mail check.");
        AddNote(parts, "um-set-scams-note3",
                "Nothing is ever hidden, moved or deleted either way: UltraMail "
                "only labels mail, and you decide.");
        return parts.page;
    }

    // ===== WARNINGS > TRUSTED AND BLOCKED SENDERS =====
    std::shared_ptr<UltraCanvasContainer> BuildSenderListsPage(DialogState* d) {
        PageParts parts = MakePage("um-set-page-senders", "Trusted and blocked senders",
                "The senders you trust, and those whose mail is spam to you:");

        // One field per list. What is typed is kept as an address (or, on the
        // blocked list, a domain) and shown that way; an address put on one
        // list leaves the other.
        auto field = [d, &parts](const std::string& id, const std::string& caption,
                                 const std::string& placeholder, bool blockList) {
            AddBodyCaption(parts, id + "-caption", caption);
            auto input = MakeTagField(id, placeholder);
            SenderLists& lists = d->prefs->senderLists;
            input->SetTags(ListTags(blockList ? lists.blocked : lists.trusted));
            input->onTagsChanged = [d, blockList](const std::vector<std::string>& tags) {
                if (d->syncing || !d->prefs) return;
                SenderLists& lists = d->prefs->senderLists;
                std::set<std::string>& list  = blockList ? lists.blocked : lists.trusted;
                std::set<std::string>& other = blockList ? lists.trusted : lists.blocked;
                std::set<std::string> entries;
                bool otherChanged = false;
                for (const auto& tag : tags) {
                    const std::string entry = SenderLists::Entry(tag, blockList);
                    if (entry.empty()) continue;
                    entries.insert(entry);
                    if (!list.count(entry) && other.erase(entry)) otherChanged = true;
                }
                list = entries;
                // Both fields shown as kept: this one's tags tidied, the other
                // without an address that moved over.
                if (otherChanged || ListTags(entries) != tags) SyncControls(d);
                ApplyAndSave(d);
            };
            parts.body->AddChild(input);
            return input;
        };
        d->trustedSendersInput = field("um-set-trusted-senders",
                "Always trust - type an address and press Enter:",
                "e.g. friend@example.com", false);
        d->blockedSendersInput = field("um-set-blocked-senders",
                "Blocked - an address, or a domain for everyone there:",
                "e.g. offers@example.com or example.com", true);

        AddNote(parts, "um-set-senders-note1",
                "A trusted sender's mail gets no guessed warnings - no romance or "
                "advance-fee letter, no crypto caution, no spam markers - and its "
                "pictures load like a contact's. What catches a lie still counts: a "
                "link that hides where it goes, a forged sender address, a program "
                "dressed as a document.");
        AddNote(parts, "um-set-senders-note2",
                "A blocked sender's mail is marked as spam: the orange frame and the "
                "strip above the message. A blocked domain blocks its subdomains too. "
                "Nothing is moved or deleted - \"Mark as spam\" moves a message to "
                "the junk folder.");
        AddNote(parts, "um-set-senders-note3",
                "Right-click a sender - in the list or above the message - to trust "
                "or block them, or the whole domain; the same menu undoes it. Their "
                "mail is checked again at once.");
        return parts.page;
    }

    // ===== START PAGE =====
    std::shared_ptr<UltraCanvasContainer> BuildStartPage() {
        PageParts parts = MakePage("um-set-page-start", "Settings",
                "Open a section on the left and choose the page to set:");
        AddNote(parts, "um-set-start-note0",
                "Mail - how often every account is checked for new mail.");
        AddNote(parts, "um-set-start-note1",
                "Reading - where a message opens, how wide the folder list is, "
                "whether HTML mail is shown formatted or as plain text, the size "
                "of its text, and which mail counts as waiting for your reply.");
        AddNote(parts, "um-set-start-note2",
                "Privacy - when pictures on the web are downloaded (always, only "
                "from websites and senders you trust and your contacts, or never "
                "by themselves), and whether the known senders' icons are fetched.");
        AddNote(parts, "um-set-start-note3",
                "Display - whether a link's address is shown in the status bar or "
                "as a tooltip, and whether new mail shows a notification on the "
                "screen.");
        AddNote(parts, "um-set-start-note5",
                "Warnings - which kinds of spam and scam UltraMail warns about: "
                "phishing, romance scams, advance-fee letters, letters in an "
                "agency's name, cryptocurrency, dangerous attachments - and the "
                "senders you trust or block.");
        AddNote(parts, "um-set-start-note4",
                "An account's servers, sign-in and name are in its own Account "
                "Settings. Every change here applies straight away and is saved; "
                "there is nothing to confirm.");
        return parts.page;
    }

    // ===== PAGE SWITCHING =====

    void UpdateRestoreButton(DialogState* d) {
        if (!d->restoreButton) return;
        auto it = d->resets.find(d->shownPage);
        if (it == d->resets.end()) {
            d->restoreButton->SetVisible(false);
            d->restoreButton->RequestRedraw();
            return;
        }
        d->restoreButton->SetText(it->second.label);
        d->restoreButton->size.width = CSSLayout::Dimension::Px(it->second.width);
        d->restoreButton->SetVisible(true);
        d->restoreButton->RequestRedraw();
    }

    void ShowPage(DialogState* d, const std::string& pageId) {
        if (!d->pages.count(pageId)) return;
        d->shownPage = pageId;
        for (auto& [id, page] : d->pages) page->SetVisible(id == pageId);
        UpdateRestoreButton(d);
    }

    void AddPage(DialogState* d, const char* pageId,
                 const std::shared_ptr<UltraCanvasContainer>& page) {
        page->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        d->pages[pageId] = page;
        d->pageArea->AddChild(page);
    }

    void AddTreeNode(DialogState* d, const char* parentId, const char* id, const char* text) {
        TreeNodeData data;
        data.nodeId = id;
        data.text = text;
        d->tree->AddNode(parentId, data);
    }

    void BuildDialog(DialogState* d, UltraCanvasWindowBase* parent) {
        WindowConfig wc;
        wc.title = "UltraMail - Settings";
        wc.width = 700;
        wc.height = 560;
        wc.resizable = true;
        wc.type = WindowType::Dialog;
        wc.parentWindow = parent;
        d->window = CreateWindow(wc);
        if (!d->window || !d->window->IsCreated()) { d->window.reset(); return; }

        d->window->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        d->window->SetBackgroundColor(Color(249, 249, 251, 255));

        // ----- settings-page tree | page area -----
        auto content = std::make_shared<UltraCanvasContainer>("um-set-content");
        content->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        content->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        d->tree = std::make_shared<UltraCanvasTreeView>("um-set-tree");
        d->tree->SetFontSize(kTreeFontSize);
        d->tree->SetRowHeight(24);
        d->tree->SetSelectionMode(TreeSelectionMode::Single);
        d->tree->SetLineStyle(TreeLineStyle::NoLine);
        d->tree->SetBackgroundColor(Color(243, 243, 246, 255));
        // A section has no page of its own: selecting one moves on to its
        // first page.
        d->tree->SetShowFirstChildOnExpand(true);
        d->tree->SetAutoExpandSelectedNode(true);
        d->tree->size.width = CSSLayout::Dimension::Px(180);
        d->tree->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        TreeNodeData rootData;
        rootData.nodeId = "settings";
        rootData.text = "Settings";
        d->tree->SetRootNode(rootData);
        AddTreeNode(d, "settings", kPageMail, "Mail");
        AddTreeNode(d, kPageMail, kPageNewMail, "New mail");
        AddTreeNode(d, "settings", kPageReading, "Reading");
        AddTreeNode(d, kPageReading, kPageLayout, "Layout");
        AddTreeNode(d, kPageReading, kPageMessages, "Messages");
        AddTreeNode(d, kPageReading, kPageWaiting, "Waiting for reply");
        AddTreeNode(d, "settings", kPagePrivacy, "Privacy");
        AddTreeNode(d, kPagePrivacy, kPageImages, "Images");
        AddTreeNode(d, kPagePrivacy, kPageSenderIcons, "Sender icons");
        AddTreeNode(d, "settings", kPageDisplay, "Display");
        AddTreeNode(d, kPageDisplay, kPageLinks, "Links");
        AddTreeNode(d, kPageDisplay, kPageNotify, "Notifications");
        AddTreeNode(d, "settings", kPageWarnings, "Warnings");
        AddTreeNode(d, kPageWarnings, kPageScamWarnings, "Spam/scam warnings");
        AddTreeNode(d, kPageWarnings, kPageSenderLists, "Trusted and blocked senders");
        // After the nodes: hiding the root promotes the sections to the top.
        d->tree->SetRootVisible(false);
        content->AddChild(d->tree);

        d->pageArea = std::make_shared<UltraCanvasContainer>("um-set-pages");
        d->pageArea->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        d->pageArea->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                               .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        d->pageArea->SetBackgroundColor(Color(255, 255, 255, 255));
        content->AddChild(d->pageArea);

        AddPage(d, kPageNewMail, BuildNewMailPage(d));
        AddPage(d, kPageLayout, BuildLayoutPage(d));
        AddPage(d, kPageMessages, BuildMessagesPage(d));
        AddPage(d, kPageWaiting, BuildWaitingPage(d));
        AddPage(d, kPageImages, BuildImagesPage(d));
        AddPage(d, kPageSenderIcons, BuildSenderIconsPage(d));
        AddPage(d, kPageLinks, BuildLinksPage(d));
        AddPage(d, kPageNotify, BuildNotificationsPage(d));
        AddPage(d, kPageScamWarnings, BuildScamWarningsPage(d));
        AddPage(d, kPageSenderLists, BuildSenderListsPage(d));
        AddPage(d, kPageStart, BuildStartPage());
        d->window->AddChild(content);

        // ----- bottom bar: [Restore default ...]            [Close] -----
        auto bottom = std::make_shared<UltraCanvasContainer>("um-set-bottom");
        bottom->layout.SetFlexRow().SetFlexGap(8)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        bottom->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        bottom->SetPadding(8, 12, 8, 12);
        bottom->SetBorderTop(1, Color(225, 225, 230, 255));

        auto bottomLeft = std::make_shared<UltraCanvasContainer>("um-set-bottom-left");
        bottomLeft->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center);
        bottomLeft->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                              .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        d->restoreButton = MakeButton("um-set-restore", "Restore defaults", 170, [d]() {
            auto it = d->resets.find(d->shownPage);
            if (it != d->resets.end() && it->second.action) it->second.action();
        });
        d->restoreButton->SetVisible(false);
        bottomLeft->AddChild(d->restoreButton);
        bottom->AddChild(bottomLeft);
        bottom->AddChild(MakeButton("um-set-close", "Close", 90,
                [d]() { if (d->window) d->window->Close(); }));
        d->window->AddChild(bottom);

        d->tree->onNodeSelected = [d](TreeNode* node) {
            if (!node) return;
            TreeNode* leaf = node;
            while (leaf && leaf->HasChildren()) leaf = leaf->FirstChild();
            if (leaf && leaf != node) {
                d->tree->SelectNode(leaf);
                return;
            }
            ShowPage(d, node->data.nodeId);
        };
        ShowPage(d, kPageStart);

        // Escape closes, matching the framework's dialog convention.
        d->window->SetEventCallback([](const UCEvent& event) {
            if (event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) {
                if (auto tw = event.targetWindow.lock())
                    static_cast<UltraCanvasWindow*>(tw.get())->Close();
                return true;
            }
            return false;
        });
        d->window->onWindowClosed = [d]() { d->closed = true; };
        d->window->Show();
    }

} // namespace

void SettingsDialog::Show(UltraCanvasWindowBase* parent, Preferences* prefs,
                          std::function<void()> onChanged) {
    if (g_dialog && g_dialog->window && !g_dialog->closed) {
        SyncControls(g_dialog.get());
        g_dialog->window->Show();
        return;
    }
    if (!prefs) return;
    auto state = std::make_shared<DialogState>();
    state->prefs = prefs;
    state->onChanged = std::move(onChanged);
    BuildDialog(state.get(), parent);
    if (!state->window) return;
    g_dialog = state;   // keeps the widgets alive
}

std::shared_ptr<UltraCanvasButton> SettingsDialog::MakeGearButton(
        const std::string& id, float height, std::function<void()> onClick) {
    auto gear = std::make_shared<UltraCanvasButton>(id, 0, 0, 30, height, "");
    gear->SetCornerRadius(4.0f);
    gear->SetColors(Color(255, 255, 255, 255), Color(233, 238, 244, 255));
    gear->SetBorder(1.0f, Color(0, 0, 0, 60));
    gear->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/settings.svg"));
    gear->SetIconSize(15, 15);
    gear->SetIconPosition(ButtonIconPosition::Left);
    gear->SetIconSpacing(0);
    gear->SetUseIconAsMask(true);
    gear->SetIconMaskColor(Color(55, 55, 60, 255));
    gear->SetTooltip("Settings");
    if (onClick) gear->onClick = std::move(onClick);
    gear->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return gear;
}

void SettingsDialog::SyncWithPreferences() {
    if (g_dialog && g_dialog->window && !g_dialog->closed) SyncControls(g_dialog.get());
}

void SettingsDialog::Shutdown() {
    g_dialog.reset();
}

} // namespace UltraMail
