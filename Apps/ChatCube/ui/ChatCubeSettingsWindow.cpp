// Apps/ChatCube/ui/ChatCubeSettingsWindow.cpp
// ChatCube's settings window: a tree of sections and pages on the left, the
// chosen page on the right, the page's "Restore defaults" and Close at the
// foot - the layout of UltraFiler's settings window, in ChatCube's colours.
//
// Pages: Appearance > Look (light or dark, from the next start), Appearance >
// Time & date (a 24- or 12-hour clock; the date as day.month.year,
// year-month-day or month/day/year), Messages > Sending (Enter or Ctrl+Enter
// sends), Messages > Typing (whether the other side is told I am typing, and
// whether I see when they are), Networks > Accounts (each network's state,
// my ChatCube ID to copy, signing out of Matrix and Telegram), Networks >
// Local network (whether the people found on this network are listed in the
// sidebar), Networks > Relay (the relay server ChatCube Direct signs in
// through, ULTRA OS's own by default), Extras > Data folder (where ChatCube keeps its identity, its
// history and its settings, to open or copy) and Extras > About.
//
// Version: 0.4.1 - every text goes through Tr (UltraLocalisation)
// Version: 0.4.0 - Networks > Relay: the relay server, no longer asked on the sign-in page
// Version: 0.3.0 - Appearance > Look: System beside Light and Dark
// Version: 0.2.0 - Networks > Proxy: the proxy Matrix and the relay are
//                  reached through, shared by every ULTRA OS application
// Version: 0.1.0
// Last Modified: 2026-11-12
// Author: UltraCanvas Framework / ULTRA OS

#include "ChatCubeSettingsWindow.h"
#include "ChatCubeTheme.h"
#include "ChatCubeViews.h"      // GroupUserId

#include "UltraCanvasAlert.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasFileAssociations.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasProxySettingsDialog.h"   // the shared Proxy page
#include "UltraCanvasRadio.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <cctype>
#include <functional>
#include <map>
#include <utility>

using namespace UltraCanvas;

namespace ChatCube {

namespace {

// ----- one type scale for the whole window, as in UltraFiler's -----
constexpr float kTitleFontSize = 15.0f;   // the page's title (bold)
constexpr float kTextFontSize  = 10.0f;   // captions, choices, buttons
constexpr float kNoteFontSize  = 9.0f;    // the notes at the foot
constexpr float kTreeFontSize  = 9.5f;

// The window is 720 wide and the tree takes 180, so a page has 540; with
// its padding that leaves 500 for content. Texts wrap at a fixed width:
// content measuring needs a render context, which the window does not have
// while it is first laid out.
constexpr int kWindowWidth   = 720;
constexpr int kWindowHeight  = 540;
constexpr int kTreeWidth     = 180;
constexpr int kPagePadding   = 20;
constexpr int kTextWidth     = 460;
constexpr int kNoteWidth     = 440;
constexpr int kControlHeight = 24;

// Page ids double as tree node ids.
constexpr const char* kPageStart       = "start";
constexpr const char* kPageAppearance  = "appearance";
constexpr const char* kPageLook        = "appearance/look";
constexpr const char* kPageTime        = "appearance/time";
constexpr const char* kPageMessages    = "messages";
constexpr const char* kPageSending     = "messages/sending";
constexpr const char* kPageTyping      = "messages/typing";
constexpr const char* kPageNetworks    = "networks";
constexpr const char* kPageAccounts    = "networks/accounts";
constexpr const char* kPageLocal       = "networks/local";
constexpr const char* kPageProxy       = "networks/proxy";
constexpr const char* kPageRelay       = "networks/relay";
constexpr const char* kPageExtras      = "extras";
constexpr const char* kPageDataFolder  = "extras/data";
constexpr const char* kPageAbout       = "extras/about";

// The colours of the look ChatCube started in.
const Look& L() {
    static const Look look = CurrentLook();
    return look;
}

} // namespace

// The open window and its controls. The controls' callbacks hold a raw
// pointer to it: it owns the window, so it is there for as long as they can run.
struct ChatCubeSettingsWindow::State {
    Settings*   settings = nullptr;
    Hooks*      hooks = nullptr;
    std::string version;
    std::string dataFolder;

    std::shared_ptr<UltraCanvasWindow>    window;
    bool                                  closed = false;
    // Set while the controls are given the settings' values: the handlers
    // that fires must not save or apply them once more.
    bool                                  syncing = false;
    std::shared_ptr<UltraCanvasTreeView>  tree;
    std::shared_ptr<UltraCanvasContainer> pageArea;
    std::map<std::string, std::shared_ptr<UltraCanvasContainer>> pages;
    std::map<std::string, std::function<void()>> resets;   // the page's Restore defaults
    std::shared_ptr<UltraCanvasButton>    restoreButton;
    std::string                           shownPage;

    // Appearance > Look
    std::shared_ptr<UltraCanvasRadio> systemRadio, lightRadio, darkRadio;
    UltraCanvasRadioGroup             lookGroup;
    // Appearance > Time & date
    std::shared_ptr<UltraCanvasRadio> clock24Radio, clock12Radio;
    UltraCanvasRadioGroup             clockGroup;
    std::vector<std::pair<DateStyle, std::shared_ptr<UltraCanvasRadio>>> dateRadios;
    UltraCanvasRadioGroup             dateGroup;
    // Messages > Sending
    std::shared_ptr<UltraCanvasRadio> enterRadio, ctrlEnterRadio;
    UltraCanvasRadioGroup             sendKeyGroup;
    // Messages > Typing
    std::shared_ptr<UltraCanvasSwitch> sendTypingSwitch, showTypingSwitch;
    // Networks > Accounts: filled again by RefreshAccounts
    std::shared_ptr<UltraCanvasContainer> accountsBox;
    // Networks > Local network
    std::shared_ptr<UltraCanvasSwitch> showNearbySwitch;
    // Networks > Relay
    std::shared_ptr<UltraCanvasTextInput> relayInput;
    // Extras > Data folder
    std::shared_ptr<UltraCanvasLabel>  dataStatus;
};

namespace {

using State = ChatCubeSettingsWindow::State;

void ApplyAndSave(State* d) {
    if (d->syncing) return;
    d->settings->Save();
    if (d->hooks->applied) d->hooks->applied();
}

// ===== TEXT =====

std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id, const std::string& text,
                                            float fontSize = kTextFontSize, const Color& color = L().ink) {
    auto l = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 20);
    l->SetText(text);
    l->SetFontSize(fontSize);
    l->SetTextColor(color);
    l->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    l->size.width = CSSLayout::Dimension::Auto();
    l->size.height = CSSLayout::Dimension::Auto();
    return l;
}

// A paragraph: word-wrapped at `width`, as tall as its lines.
std::shared_ptr<UltraCanvasLabel> MakeText(const std::string& id, const std::string& text, int width = kTextWidth,
                                           float fontSize = kTextFontSize, const Color& color = L().ink) {
    auto l = MakeLabel(id, text, fontSize, color);
    l->SetWrap(TextWrap::WrapWord);
    l->size.width = CSSLayout::Dimension::Px(static_cast<float>(width));
    return l;
}

// ===== PAGE SKELETON =====
// Title, caption, the controls, and the notes at the foot - the same order
// and spacing on every page, as in UltraFiler's settings.
struct PageParts {
    std::shared_ptr<UltraCanvasContainer> page;
    std::shared_ptr<UltraCanvasContainer> body;    // the controls
    std::shared_ptr<UltraCanvasContainer> notes;   // what they are about
};

PageParts MakePage(const std::string& id, const std::string& title, const std::string& caption) {
    PageParts parts;
    parts.page = std::make_shared<UltraCanvasContainer>(id);
    parts.page->layout.SetFlexColumn().SetFlexGap(0).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    parts.page->SetPadding(kPagePadding, kPagePadding, kPagePadding, kPagePadding);

    auto titleLabel = MakeLabel(id + "-title", title, kTitleFontSize);
    titleLabel->SetFontWeight(FontWeight::Bold);
    parts.page->AddChild(titleLabel);
    auto captionLabel = MakeText(id + "-caption", caption, kTextWidth, kTextFontSize, L().inkMuted);
    captionLabel->SetMargin(6, 0, 0, 0);
    parts.page->AddChild(captionLabel);

    parts.body = std::make_shared<UltraCanvasContainer>(id + "-body");
    parts.body->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    parts.body->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    parts.body->SetMargin(14, 0, 0, 0);
    parts.page->AddChild(parts.body);

    // The spacer pushes the notes to the foot of the page, and gives way
    // when the window is made small.
    auto spacer = std::make_shared<UltraCanvasContainer>(id + "-spacer");
    spacer->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    spacer->size.height = CSSLayout::Dimension::Px(24);
    parts.page->AddChild(spacer);

    parts.notes = std::make_shared<UltraCanvasContainer>(id + "-notes");
    parts.notes->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    parts.notes->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    parts.notes->SetBackgroundColor(L().well);
    parts.notes->SetBorderLeft(3, L().accent);
    parts.notes->SetPadding(10, 12, 10, 12);
    parts.page->AddChild(parts.notes);
    return parts;
}

void AddNote(PageParts& parts, const std::string& id, const std::string& text) {
    parts.notes->AddChild(MakeText(id, text, kNoteWidth, kNoteFontSize, L().inkMuted));
}

// A caption inside the body, introducing a second group of controls.
void AddBodyCaption(PageParts& parts, const std::string& id, const std::string& text) {
    auto l = MakeText(id, text);
    l->SetMargin(10, 0, 0, 0);
    parts.body->AddChild(l);
}

// ===== CONTROLS =====

// A button in ChatCube's look: rounded, a hairline border; an icon from
// media/icons/chatcube beside the words when `icon` is given.
std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& id, const std::string& text, int width,
                                              std::function<void()> onClick, const std::string& icon = "") {
    auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, width, 30, text);
    ButtonStyle st = b->GetStyle();
    st.cornerRadius = 8.0f;
    st.fontSize = kTextFontSize;
    st.borderWidth = 1.0f;
    st.borderColor = L().line;
    st.normalColor = L().panel;
    st.hoverColor = L().hover;
    st.pressedColor = L().pressed;
    st.normalTextColor = st.hoverTextColor = st.pressedTextColor = L().ink;
    st.disabledColor = L().well;
    st.disabledTextColor = L().inkFaint;
    b->SetStyle(st);
    if (!icon.empty()) {
        b->SetIcon(IconPath(icon));
        b->SetIconSize(16, 16);
        b->SetUseIconAsMask(true);
        b->SetIconSpacing(6);
    }
    if (onClick) b->SetOnClick(std::move(onClick));
    b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return b;
}

std::shared_ptr<UltraCanvasContainer> MakeButtonRow(const std::string& id) {
    auto row = std::make_shared<UltraCanvasContainer>(id);
    row->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->size.width = CSSLayout::Dimension::Px(kTextWidth);
    row->size.height = CSSLayout::Dimension::Px(36);
    return row;
}

// One choice of a radio group, in the window's text size and colours.
std::shared_ptr<UltraCanvasRadio> MakeChoice(const std::string& id, const std::string& text, bool checked) {
    auto radio = UltraCanvasRadio::Create(id, -1, -1, text, checked);
    RadioVisualStyle style = radio->GetVisualStyle();
    style.base.fontSize = kTextFontSize;
    style.base.textColor = L().ink;
    style.base.textHoverColor = L().ink;
    style.base.textDisabledColor = L().inkFaint;
    style.outerColor = L().panel;
    style.outerBorderColor = L().lineStrong;
    style.outerHoverColor = L().hover;
    style.outerPressedColor = L().pressed;
    style.innerDotColor = L().accent;
    radio->SetVisualStyle(style);
    radio->size.width = CSSLayout::Dimension::Px(kTextWidth);
    radio->size.height = CSSLayout::Dimension::Px(kControlHeight);
    radio->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return radio;
}

// An on-off switch with its words, in the theme's colours (ChatCube's).
std::shared_ptr<UltraCanvasSwitch> MakeSwitch(const std::string& id, const std::string& text, bool checked,
                                              std::function<void(bool)> onChange) {
    auto sw = UltraCanvasSwitch::Create(id, 0, 0, text, checked);
    SwitchVisualStyle style = SwitchVisualStyle::FromTheme(UltraCanvasTheme::Current(), sw->GetVisualStyle());
    style.base.fontSize = kTextFontSize;
    style.base.textColor = L().ink;
    style.base.textHoverColor = L().ink;
    sw->SetVisualStyle(style);
    sw->size.width = CSSLayout::Dimension::Px(kTextWidth);
    sw->size.height = CSSLayout::Dimension::Px(kControlHeight);
    sw->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    sw->onStateChanged = [onChange](CheckedState, CheckedState now) {
        if (onChange) onChange(now == CheckedState::Checked);
    };
    return sw;
}

// A one-line text field in ChatCube's look.
std::shared_ptr<UltraCanvasTextInput> MakeTextField(const std::string& id, const std::string& placeholder,
                                                    int width) {
    auto t = std::make_shared<UltraCanvasTextInput>(id, 0, 0, width, 28);
    t->SetPlaceholder(placeholder);
    t->SetFontSize(kTextFontSize);
    t->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    TextInputStyle st = t->GetStyle();
    st.backgroundColor = L().panel;
    st.borderColor = L().line;
    st.focusBorderColor = L().accent;
    st.textColor = L().ink;
    st.placeholderColor = L().inkFaint;
    st.caretColor = L().ink;
    st.selectionColor = Color(L().accent.r, L().accent.g, L().accent.b, 90);
    st.borderRadius = 8;
    st.paddingLeft = st.paddingRight = 10;
    t->SetStyle(st);
    return t;
}

// ===== APPEARANCE > LOOK =====

std::shared_ptr<UltraCanvasContainer> BuildLookPage(State* d) {
    PageParts parts = MakePage("cc-set-page-look", Tr("Look"), Tr("The colours of the ChatCube window:"));
    d->systemRadio = MakeChoice("cc-set-look-system", Tr("System - the computer's look, light or dark"),
                                d->settings->look == "system");
    d->lightRadio = MakeChoice("cc-set-look-light", Tr("Light - dark text on warm white"), d->settings->look == "light");
    d->darkRadio = MakeChoice("cc-set-look-dark", Tr("Dark - light text on a dark ground"), d->settings->look == "dark");
    d->lookGroup.AddRadioButton(d->systemRadio);
    d->lookGroup.AddRadioButton(d->lightRadio);
    d->lookGroup.AddRadioButton(d->darkRadio);
    d->lookGroup.onSelectionChanged = [d](std::shared_ptr<UltraCanvasRadio> selected) {
        if (!selected) return;
        d->settings->look = selected.get() == d->darkRadio.get() ? "dark"
                          : selected.get() == d->lightRadio.get() ? "light" : "system";
        ApplyAndSave(d);
    };
    parts.body->AddChild(d->systemRadio);
    parts.body->AddChild(d->lightRadio);
    parts.body->AddChild(d->darkRadio);
    d->resets[kPageLook] = [d]() {
        d->settings->look = "system";
        d->lookGroup.SelectButton(d->systemRadio);
    };

    AddNote(parts, "cc-set-look-note1",
            Tr("The look is chosen when ChatCube starts, since every part of the window takes its colours as "
            "it is built: a change here, or of the computer's look under System, shows the next time "
            "ChatCube is started. System is the computer's own light or dark mode, and the Look page of "
            "UOS-Settings over it."));
    AddNote(parts, "cc-set-look-note2",
            Tr("Started with --dark, ChatCube is dark whatever is chosen here."));
    return parts.page;
}

// ===== APPEARANCE > TIME & DATE =====

std::shared_ptr<UltraCanvasContainer> BuildTimePage(State* d) {
    PageParts parts = MakePage("cc-set-page-time", Tr("Time & date"),
                               Tr("How a message's time is written, in the bubbles and the list of conversations:"));
    d->clock24Radio = MakeChoice("cc-set-clock-24", Tr("14:05  (24-hour clock)"), d->settings->clock24);
    d->clock12Radio = MakeChoice("cc-set-clock-12", Tr("2:05 PM  (12-hour clock)"), !d->settings->clock24);
    d->clockGroup.AddRadioButton(d->clock24Radio);
    d->clockGroup.AddRadioButton(d->clock12Radio);
    d->clockGroup.onSelectionChanged = [d](std::shared_ptr<UltraCanvasRadio> selected) {
        if (!selected) return;
        d->settings->clock24 = selected.get() == d->clock24Radio.get();
        ApplyAndSave(d);
    };
    parts.body->AddChild(d->clock24Radio);
    parts.body->AddChild(d->clock12Radio);

    AddBodyCaption(parts, "cc-set-date-caption", Tr("How a date is written, over the messages of each day:"));
    const DateStyle styles[] = {DateStyle::DayMonthYear, DateStyle::YearMonthDay, DateStyle::MonthDayYear};
    int n = 0;
    for (DateStyle style : styles) {
        auto radio = MakeChoice("cc-set-date-" + std::to_string(n++), DateStyleExample(style),
                                d->settings->dateStyle == style);
        d->dateRadios.emplace_back(style, radio);
        d->dateGroup.AddRadioButton(radio);
        parts.body->AddChild(radio);
    }
    d->dateGroup.onSelectionChanged = [d](std::shared_ptr<UltraCanvasRadio> selected) {
        for (const auto& [style, radio] : d->dateRadios) {
            if (radio == selected) d->settings->dateStyle = style;
        }
        ApplyAndSave(d);
    };
    d->resets[kPageTime] = [d]() {
        d->settings->clock24 = true;
        d->settings->dateStyle = DateStyle::DayMonthYear;
        d->clockGroup.SelectButton(d->clock24Radio);
        d->dateGroup.SelectButton(d->dateRadios.front().second);
    };

    AddNote(parts, "cc-set-time-note1",
            Tr("Times are this computer's local time. A change shows at once, in the open conversation too."));
    return parts.page;
}

// ===== MESSAGES > SENDING =====

std::shared_ptr<UltraCanvasContainer> BuildSendingPage(State* d) {
    PageParts parts = MakePage("cc-set-page-sending", Tr("Sending"), Tr("The key that sends what is typed in the message field:"));
    d->enterRadio = MakeChoice("cc-set-send-enter", Tr("Enter sends - Shift+Enter starts a new line"),
                               !d->settings->ctrlEnterSends);
    d->ctrlEnterRadio = MakeChoice("cc-set-send-ctrl", Tr("Ctrl+Enter sends - Enter starts a new line"),
                                   d->settings->ctrlEnterSends);
    d->sendKeyGroup.AddRadioButton(d->enterRadio);
    d->sendKeyGroup.AddRadioButton(d->ctrlEnterRadio);
    d->sendKeyGroup.onSelectionChanged = [d](std::shared_ptr<UltraCanvasRadio> selected) {
        if (!selected) return;
        d->settings->ctrlEnterSends = selected.get() == d->ctrlEnterRadio.get();
        ApplyAndSave(d);
    };
    parts.body->AddChild(d->enterRadio);
    parts.body->AddChild(d->ctrlEnterRadio);
    d->resets[kPageSending] = [d]() {
        d->settings->ctrlEnterSends = false;
        d->sendKeyGroup.SelectButton(d->enterRadio);
    };

    AddNote(parts, "cc-set-send-note1",
            Tr("Ctrl+Enter suits long messages of several paragraphs: Enter then breaks the line, as in a "
            "text editor, and nothing goes before it is finished."));
    AddNote(parts, "cc-set-send-note2",
            Tr("The send button beside the field sends either way, with the pictures and files waiting above it."));
    return parts.page;
}

// ===== MESSAGES > TYPING =====

std::shared_ptr<UltraCanvasContainer> BuildTypingPage(State* d) {
    PageParts parts = MakePage("cc-set-page-typing", Tr("Typing"), Tr("What \"typing\xE2\x80\xA6\" tells, either way:"));
    d->sendTypingSwitch = MakeSwitch("cc-set-typing-send", Tr("Tell the other side while I am typing"),
                                     d->settings->sendTyping, [d](bool on) {
        d->settings->sendTyping = on;
        ApplyAndSave(d);
    });
    parts.body->AddChild(d->sendTypingSwitch);
    d->showTypingSwitch = MakeSwitch("cc-set-typing-show", Tr("Show when the other side is typing"),
                                     d->settings->showTyping, [d](bool on) {
        d->settings->showTyping = on;
        ApplyAndSave(d);
    });
    parts.body->AddChild(d->showTypingSwitch);
    d->resets[kPageTyping] = [d]() {
        d->settings->sendTyping = true;
        d->settings->showTyping = true;
        d->sendTypingSwitch->SetChecked(true);
        d->showTypingSwitch->SetChecked(true);
    };

    AddNote(parts, "cc-set-typing-note1",
            Tr("Off, the other side sees nothing of your typing - only the message once it is sent. Every "
            "network passes it on: ChatCube Direct, Matrix and Telegram."));
    AddNote(parts, "cc-set-typing-note2",
            Tr("\"typing\xE2\x80\xA6\" shows under the name in the conversation's header, for a few seconds "
               "after the other side's last key."));
    return parts.page;
}

// ===== NETWORKS > ACCOUNTS =====

void FillAccounts(State* d) {
    if (!d->accountsBox) return;
    d->accountsBox->ClearChildren();
    const std::vector<ChatCubeSettingsWindow::Account> accounts =
        d->hooks->accounts ? d->hooks->accounts() : std::vector<ChatCubeSettingsWindow::Account>();
    int n = 0;
    for (const auto& account : accounts) {
        const std::string id = "cc-set-account-" + std::to_string(n++);
        auto block = std::make_shared<UltraCanvasContainer>(id);
        block->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Start);
        block->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        block->size.width = CSSLayout::Dimension::Px(kTextWidth);
        block->SetBackgroundColor(L().panel);
        block->SetBorders(1.0f, L().line, 8.0f);
        block->SetPadding(8, 12, 8, 12);
        auto name = MakeLabel(id + "-name", account.name, 11.0f);
        name->SetFontWeight(FontWeight::Bold);
        block->AddChild(name);
        block->AddChild(MakeText(id + "-state", account.state, kTextWidth - 26, kTextFontSize, L().inkMuted));
        if (account.providerId == "direct") {
            const std::string myId = d->hooks->myId ? d->hooks->myId() : std::string();
            if (!myId.empty()) {
                auto idText = MakeText(id + "-id", TrF("My ChatCube ID: {0}", GroupUserId(myId)), kTextWidth - 26);
                idText->SetSelectable(true);
                block->AddChild(idText);
                auto row = MakeButtonRow(id + "-row");
                row->size.width = CSSLayout::Dimension::Px(kTextWidth - 26);
                row->AddChild(MakeButton(id + "-copy", Tr("Copy my ID"), 130, [d]() {
                    if (d->hooks->copyMyId) d->hooks->copyMyId();
                }, "copy"));
                block->AddChild(row);
            }
        } else if (account.canSignOut) {
            auto row = MakeButtonRow(id + "-row");
            row->size.width = CSSLayout::Dimension::Px(kTextWidth - 26);
            const std::string providerId = account.providerId;
            const std::string networkName = account.name;
            row->AddChild(MakeButton(id + "-signout", Tr("Sign out"), 110, [d, providerId, networkName]() {
                UltraCanvasAlert::Confirm(
                    TrF("Sign out of {0}? Its conversations leave the list, and signing in again asks for the "
                        "password or the code once more.", networkName),
                    Tr("ChatCube - Sign out"),
                    [d, providerId](bool yes) {
                        if (!yes || d->closed) return;
                        if (d->hooks->signOut) d->hooks->signOut(providerId);
                        FillAccounts(d);
                    },
                    d->window.get());
            }));
            block->AddChild(row);
        }
        d->accountsBox->AddChild(block);
    }
    if (d->window) d->window->RequestRedraw();
}

std::shared_ptr<UltraCanvasContainer> BuildAccountsPage(State* d) {
    PageParts parts = MakePage("cc-set-page-accounts", Tr("Accounts"), Tr("The networks ChatCube talks through, and where each stands:"));
    d->accountsBox = std::make_shared<UltraCanvasContainer>("cc-set-accounts");
    d->accountsBox->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    d->accountsBox->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    parts.body->AddChild(d->accountsBox);
    FillAccounts(d);

    AddNote(parts, "cc-set-accounts-note1",
            Tr("Signing in to Matrix or Telegram is done from the sidebar, with the Matrix and Telegram buttons "
            "under your ChatCube ID."));
    AddNote(parts, "cc-set-accounts-note2",
            Tr("Your ChatCube ID is a key made on this computer and has no password; there is nothing to sign "
            "out of. Send the ID to the people who should reach you."));
    return parts.page;
}

// ===== NETWORKS > RELAY =====

std::shared_ptr<UltraCanvasContainer> BuildRelayPage(State* d) {
    PageParts parts = MakePage("cc-set-page-relay", Tr("Relay"),
                               Tr("The relay server ChatCube Direct signs in through:"));
    d->relayInput = MakeTextField("cc-set-relay", Tr("e.g. https://relay.example.org"), kTextWidth);
    d->relayInput->SetText(d->settings->relayServer);
    // Saved as it is typed; used from the next sign-in.
    d->relayInput->onTextChanged = [d](const std::string& text) {
        std::string url = text;
        while (!url.empty() && std::isspace(static_cast<unsigned char>(url.front()))) url.erase(url.begin());
        while (!url.empty() && std::isspace(static_cast<unsigned char>(url.back()))) url.pop_back();
        if (url == d->settings->relayServer) return;
        d->settings->relayServer = url;
        ApplyAndSave(d);
    };
    parts.body->AddChild(d->relayInput);
    d->resets[kPageRelay] = [d]() {
        d->settings->relayServer = Settings::kDefaultRelay;
        d->relayInput->SetText(d->settings->relayServer);
    };

    AddNote(parts, "cc-set-relay-note1",
            TrF("The relay connects you with your contacts, keeps messages for the people who are offline and "
                "sends the links that bring an ID back. Messages are end-to-end encrypted: the relay cannot read "
                "them. ULTRA OS's own relay is {0}.", Settings::kDefaultRelay));
    AddNote(parts, "cc-set-relay-note2",
            Tr("A change is used from the next sign-in - the next start of ChatCube. Empty: the relay this "
            "computer used last, or, when there was none, the local network only."));
    return parts.page;
}

// ===== NETWORKS > LOCAL NETWORK =====

std::shared_ptr<UltraCanvasContainer> BuildLocalNetworkPage(State* d) {
    PageParts parts = MakePage("cc-set-page-local", Tr("Local network"),
                               Tr("The ChatCubes on this network that are not your contacts yet:"));
    d->showNearbySwitch = MakeSwitch("cc-set-nearby", Tr("List them in the sidebar, under \"On this network\""),
                                     d->settings->showNearby, [d](bool on) {
        d->settings->showNearby = on;
        ApplyAndSave(d);
    });
    parts.body->AddChild(d->showNearbySwitch);
    d->resets[kPageLocal] = [d]() {
        d->settings->showNearby = true;
        d->showNearbySwitch->SetChecked(true);
    };

    AddNote(parts, "cc-set-local-note1",
            Tr("Each one listed has an Add button, so a person in the same room or office becomes a contact "
            "without typing their ID."));
    AddNote(parts, "cc-set-local-note2",
            Tr("Whether ChatCube looks for the people on this network at all is chosen when your ChatCube ID is "
            "made (\"Also find people on this network\"); without a relay (Networks > Relay) it always does. Off here, they "
            "are still found - only not listed."));
    return parts.page;
}

// ===== EXTRAS > DATA FOLDER =====

std::shared_ptr<UltraCanvasContainer> BuildDataFolderPage(State* d) {
    PageParts parts = MakePage("cc-set-page-data", Tr("Data folder"), Tr("Where ChatCube keeps what is yours:"));
    auto path = MakeText("cc-set-data-path", d->dataFolder);
    path->SetFontWeight(FontWeight::Bold);
    path->SetSelectable(true);
    parts.body->AddChild(path);
    auto row = MakeButtonRow("cc-set-data-row");
    row->AddChild(MakeButton("cc-set-data-open", Tr("Open the folder"), 150, [d]() {
        std::string error;
        const bool opened = FileAssociations::OpenWithDefaultApplication({d->dataFolder}, error);
        d->dataStatus->SetText(opened ? std::string() : TrF("The folder could not be opened: {0}", error));
        if (d->window) d->window->RequestRedraw();
    }));
    row->AddChild(MakeButton("cc-set-data-copy", Tr("Copy the path"), 140, [d]() {
        d->dataStatus->SetText(SetClipboardText(d->dataFolder) ? Tr("The path is on the clipboard.")
                                                               : Tr("The clipboard could not be written."));
        if (d->window) d->window->RequestRedraw();
    }, "copy"));
    parts.body->AddChild(row);
    d->dataStatus = MakeText("cc-set-data-status", "", kTextWidth, kTextFontSize, L().inkMuted);
    parts.body->AddChild(d->dataStatus);

    const std::string settingsFile = Settings::File();
    if (!settingsFile.empty()) {
        AddBodyCaption(parts, "cc-set-data-settings-caption", Tr("These settings are kept in:"));
        auto file = MakeText("cc-set-data-settings", settingsFile);
        file->SetSelectable(true);
        parts.body->AddChild(file);
    }

    AddNote(parts, "cc-set-data-note1",
            Tr("The data folder holds your ChatCube ID's key vault, your contacts and the history of your "
            "conversations, the Matrix and Telegram sessions, and in cache/ the pictures and files of the "
            "messages."));
    AddNote(parts, "cc-set-data-note2",
            Tr("Keep it safe: the key vault is your ChatCube ID. Copied to another computer with the folder, "
            "the ID moves with it."));
    return parts.page;
}

// ===== EXTRAS > ABOUT =====

std::shared_ptr<UltraCanvasContainer> BuildAboutPage(State* d) {
    PageParts parts = MakePage("cc-set-page-about", Tr("About"), Tr("ChatCube - the ULTRA OS messenger"));
    auto row = std::make_shared<UltraCanvasContainer>("cc-set-about-row");
    row->layout.SetFlexRow().SetFlexGap(14).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    auto logo = std::make_shared<UltraCanvasImageElement>("cc-set-about-logo", 0, 0, 56, 56);
    logo->LoadFromFile(NormalizePath(GetResourcesDir() + "media/appicon/ChatCube.png"));
    logo->SetFitMode(ImageFitMode::Contain);
    logo->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    logo->size.width = CSSLayout::Dimension::Px(56);
    logo->size.height = CSSLayout::Dimension::Px(56);
    row->AddChild(logo);
    auto column = std::make_shared<UltraCanvasContainer>("cc-set-about-column");
    column->layout.SetFlexColumn().SetFlexGap(2).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    column->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    auto name = MakeLabel("cc-set-about-name", "ChatCube " + d->version, 13.0f);
    name->SetFontWeight(FontWeight::Bold);
    column->AddChild(name);
    column->AddChild(MakeLabel("cc-set-about-line", Tr("UltraCanvas Framework / ULTRA OS"), kTextFontSize, L().inkMuted));
    row->AddChild(column);
    parts.body->AddChild(row);

    AddNote(parts, "cc-set-about-note1",
            Tr("Chat directly with the people you know - end-to-end encrypted, peer to peer when both of you "
            "are online, through a relay when the other person is not. There is no account and no "
            "password: your ChatCube ID is a key made on this computer."));
    AddNote(parts, "cc-set-about-note2",
            Tr("Matrix and Telegram join the same list. Each network is an UltraIM module beside the program "
            "(Plugins/UltraIM)."));
    return parts.page;
}

// ===== START PAGE =====

std::shared_ptr<UltraCanvasContainer> BuildStartPage() {
    PageParts parts = MakePage("cc-set-page-start", Tr("Settings"), Tr("Open a section on the left and choose the page to set:"));
    AddNote(parts, "cc-set-start-note1",
            Tr("Appearance - the light or the dark look, and how times and dates are written."));
    AddNote(parts, "cc-set-start-note2",
            Tr("Messages - the key that sends, and whether typing is shown, to the other side and to you."));
    AddNote(parts, "cc-set-start-note3",
            Tr("Networks - your ChatCube ID, the Matrix and Telegram accounts and signing out of them, the "
            "people on this network, the proxy and the relay server."));
    AddNote(parts, "cc-set-start-note4",
            Tr("Extras - the folder ChatCube keeps your identity, contacts and history in, and the version."));
    AddNote(parts, "cc-set-start-note5",
            Tr("Every change applies straight away and is saved; there is nothing to confirm."));
    return parts.page;
}

// ===== PAGE SWITCHING =====

void UpdateRestoreButton(State* d) {
    if (!d->restoreButton) return;
    d->restoreButton->SetVisible(d->resets.count(d->shownPage) > 0);
    d->restoreButton->RequestRedraw();
}

void ShowPage(State* d, const std::string& nodeId) {
    if (!d->pages.count(nodeId)) return;
    d->shownPage = nodeId;
    for (auto& [id, page] : d->pages) page->SetVisible(id == nodeId);
    if (nodeId == kPageAccounts) FillAccounts(d);     // where each network stands now
    UpdateRestoreButton(d);
    if (d->window) d->window->RequestRedraw();
}

void AddPage(State* d, const char* pageId, const std::shared_ptr<UltraCanvasContainer>& page) {
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    d->pages[pageId] = page;
    d->pageArea->AddChild(page);
}

void AddTreeNode(State* d, const char* parentId, const char* id, const std::string& text) {
    TreeNodeData data;
    data.nodeId = id;
    data.text = text;
    d->tree->AddNode(parentId, data);
}

void BuildWindow(State* d, UltraCanvasWindowBase* parent) {
    WindowConfig wc;
    wc.title = Tr("ChatCube - Settings");
    wc.width = kWindowWidth;
    wc.height = kWindowHeight;
    wc.resizable = true;
    wc.type = WindowType::Dialog;
    wc.parentWindow = parent;
    d->window = CreateWindow(wc);
    if (!d->window || !d->window->IsCreated()) {
        d->window.reset();
        return;
    }
    d->window->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    d->window->SetBackgroundColor(L().ground);

    // ----- the tree of pages | the page -----
    auto content = std::make_shared<UltraCanvasContainer>("cc-set-content");
    content->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    content->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    d->tree = std::make_shared<UltraCanvasTreeView>("cc-set-tree");
    d->tree->SetFontSize(kTreeFontSize);
    d->tree->SetRowHeight(26);
    d->tree->SetSelectionMode(TreeSelectionMode::Single);
    d->tree->SetLineStyle(TreeLineStyle::NoLine);
    d->tree->SetBackgroundColor(L().sidebar);
    d->tree->SetTextColor(L().ink);
    d->tree->SetSelectionColor(L().accentSoft);
    d->tree->SetHoverColor(L().hover);
    // The +/- boxes of UltraFiler's tree, in the look's colours.
    d->tree->SetExpandButtonColors(L().well, L().lineStrong, L().inkMuted);
    d->tree->SetFocusRingColor(L().accent);
    d->tree->SetBorderRight(1, L().line);
    // A section has no page of its own: opening one moves on to its first page.
    d->tree->SetShowFirstChildOnExpand(true);
    d->tree->SetAutoExpandSelectedNode(true);
    d->tree->size.width = CSSLayout::Dimension::Px(kTreeWidth);
    d->tree->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    TreeNodeData rootData;
    rootData.nodeId = "settings";
    rootData.text = Tr("Settings");
    d->tree->SetRootNode(rootData);
    AddTreeNode(d, "settings", kPageAppearance, Tr("Appearance"));
    AddTreeNode(d, kPageAppearance, kPageLook, Tr("Look"));
    AddTreeNode(d, kPageAppearance, kPageTime, Tr("Time & date"));
    AddTreeNode(d, "settings", kPageMessages, Tr("Messages"));
    AddTreeNode(d, kPageMessages, kPageSending, Tr("Sending"));
    AddTreeNode(d, kPageMessages, kPageTyping, Tr("Typing"));
    AddTreeNode(d, "settings", kPageNetworks, Tr("Networks"));
    AddTreeNode(d, kPageNetworks, kPageAccounts, Tr("Accounts"));
    AddTreeNode(d, kPageNetworks, kPageLocal, Tr("Local network"));
    AddTreeNode(d, kPageNetworks, kPageProxy, Tr("Proxy"));
    AddTreeNode(d, kPageNetworks, kPageRelay, Tr("Relay"));
    AddTreeNode(d, "settings", kPageExtras, Tr("Extras"));
    AddTreeNode(d, kPageExtras, kPageDataFolder, Tr("Data folder"));
    AddTreeNode(d, kPageExtras, kPageAbout, Tr("About"));
    // After the nodes: hiding the root makes the sections the top level.
    d->tree->SetRootVisible(false);
    content->AddChild(d->tree);

    d->pageArea = std::make_shared<UltraCanvasContainer>("cc-set-pages");
    d->pageArea->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    d->pageArea->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    d->pageArea->SetBackgroundColor(L().panel);
    content->AddChild(d->pageArea);

    AddPage(d, kPageLook, BuildLookPage(d));
    AddPage(d, kPageTime, BuildTimePage(d));
    AddPage(d, kPageSending, BuildSendingPage(d));
    AddPage(d, kPageTyping, BuildTypingPage(d));
    AddPage(d, kPageAccounts, BuildAccountsPage(d));
    AddPage(d, kPageLocal, BuildLocalNetworkPage(d));
    AddPage(d, kPageProxy, MakeProxySettingsPage("cc-set-proxy", d->window.get()));
    AddPage(d, kPageRelay, BuildRelayPage(d));
    AddPage(d, kPageDataFolder, BuildDataFolderPage(d));
    AddPage(d, kPageAbout, BuildAboutPage(d));
    AddPage(d, kPageStart, BuildStartPage());
    d->window->AddChild(content);

    // ----- the bar at the foot: [Restore defaults]            [Close] -----
    auto bottom = std::make_shared<UltraCanvasContainer>("cc-set-bottom");
    bottom->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bottom->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    bottom->SetPadding(8, 12, 8, 12);
    bottom->SetBackgroundColor(L().ground);
    bottom->SetBorderTop(1, L().line);
    // The left part takes the room Close leaves, so Close keeps its place
    // whether or not the page has defaults to restore.
    auto bottomLeft = std::make_shared<UltraCanvasContainer>("cc-set-bottom-left");
    bottomLeft->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bottomLeft->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    d->restoreButton = MakeButton("cc-set-restore", Tr("Restore defaults"), 150, [d]() {
        auto it = d->resets.find(d->shownPage);
        if (it == d->resets.end()) return;
        // The controls are set with the handlers quiet, then saved and
        // applied once.
        d->syncing = true;
        it->second();
        d->syncing = false;
        ApplyAndSave(d);
        if (d->window) d->window->RequestRedraw();
    });
    d->restoreButton->SetVisible(false);
    bottomLeft->AddChild(d->restoreButton);
    bottom->AddChild(bottomLeft);
    bottom->AddChild(MakeButton("cc-set-close", Tr("Close"), 90, [d]() {
        if (d->window) d->window->Close();
    }));
    d->window->AddChild(bottom);

    d->tree->onNodeSelected = [d](TreeNode* node) {
        if (!node) return;
        // A section moves on to its first page.
        TreeNode* leaf = node;
        while (leaf && leaf->HasChildren()) leaf = leaf->FirstChild();
        if (leaf && leaf != node) {
            d->tree->SelectNode(leaf);
            return;
        }
        ShowPage(d, node->data.nodeId);
    };
    // Every section is closed at first, so the window opens on the start page.
    ShowPage(d, kPageStart);

    // Escape closes, as every dialog of the framework does.
    d->window->SetEventCallback([](const UCEvent& event) {
        if (event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) {
            if (auto tw = event.targetWindow.lock()) static_cast<UltraCanvasWindow*>(tw.get())->Close();
            return true;
        }
        return false;
    });
    d->window->onWindowClosed = [d]() { d->closed = true; };
    d->window->Show();
}

} // namespace

ChatCubeSettingsWindow::ChatCubeSettingsWindow(Settings& settings, std::string version, std::string dataFolder,
                                               Hooks hooks)
    : settings_(settings), version_(std::move(version)), dataFolder_(std::move(dataFolder)),
      hooks_(std::move(hooks)) {}

ChatCubeSettingsWindow::~ChatCubeSettingsWindow() {
    Close();
}

void ChatCubeSettingsWindow::Show(UltraCanvasWindowBase* parent) {
    // Raise the open one instead of opening a second.
    if (state_ && state_->window && !state_->closed) {
        state_->window->Show();
        return;
    }
    auto state = std::make_shared<State>();
    state->settings = &settings_;
    state->hooks = &hooks_;
    state->version = version_;
    state->dataFolder = dataFolder_;
    BuildWindow(state.get(), parent);
    if (!state->window) return;
    state_ = std::move(state);      // keeps the widgets alive, the last closed one's going now
}

void ChatCubeSettingsWindow::Close() {
    if (state_ && state_->window && !state_->closed) state_->window->Close();
}

void ChatCubeSettingsWindow::RefreshAccounts() {
    if (state_ && state_->window && !state_->closed) FillAccounts(state_.get());
}

} // namespace ChatCube
