// Apps/UltraClaude/ui/UltraClaudeWindow.cpp
// See UltraClaudeWindow.h.
// Version: 0.1.0
// Last Modified: 2026-10-02
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraClaudeWindow.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"

#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iterator>
#include <regex>
#include <sstream>
#include <system_error>
#include <utility>

using namespace UltraCanvas;

namespace UltraClaude {

namespace {
    constexpr unsigned kTimerMs = 40;

    // Sign-in page geometry - UltraMail's start page, with a form under it.
    constexpr float kLogoSize      = 96.0f;
    constexpr float kTitleSize     = 22.0f;
    constexpr float kTaglineSize   = 11.0f;
    constexpr float kFormWidth     = 280.0f;
    constexpr float kButtonHeight  = 34.0f;
    constexpr float kButtonFont    = 11.0f;
    constexpr float kButtonRadius  = 6.0f;

    constexpr float kTextFontSize  = 10.0f;
    constexpr float kTranscriptFontSize = 11.0f;
    constexpr int   kControlHeight = 28;
    constexpr int   kPromptHeight  = 72;   // about three lines of the message box
    constexpr int   kSidebarWidth  = 240;
    const Color kSidebarBackground = Color(244, 242, 237, 255);

    const Color kPageBackground  = Color(250, 249, 246, 255);
    const Color kTextColor       = Color(40, 40, 44, 255);
    const Color kMutedTextColor  = Color(110, 110, 118, 255);
    const Color kRuleColor       = Color(225, 225, 230, 255);
    const Color kAccent          = Color(201, 100, 66, 255);    // warm terracotta
    const Color kAccentHover     = Color(178, 84, 54, 255);
    const Color kSecondary       = Color(238, 236, 231, 255);
    const Color kSecondaryHover  = Color(226, 223, 216, 255);

    // Where "Create account" goes: Anthropic's own site, where accounts and
    // subscriptions are made. Nothing about an account is handled here.
    constexpr const char* kCreateAccountUrl = "https://claude.ai/login";

    struct Choice { const char* label; const char* value; };
    // Aliases the CLI resolves to the newest model of each family.
    const Choice kModels[] = {
        {"Default", ""}, {"Opus", "opus"}, {"Sonnet", "sonnet"}, {"Haiku", "haiku"},
    };
    // What Claude may do in the folder without asking. In -p mode nobody can
    // be asked, so "Ask" means a tool that needs permission is refused and
    // the refusal is reported; the other modes allow more.
    const Choice kPermissionModes[] = {
        {"Ask (read-only)", ""},
        {"Accept edits", "acceptEdits"},
        {"Plan only", "plan"},
        {"Allow everything", "bypassPermissions"},
    };

    std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id, const std::string& text,
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

    std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& id, const std::string& text,
                                                  int width, bool primary = false) {
        auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, width, kControlHeight, text);
        b->SetFontSize(kTextFontSize);
        b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        if (primary) {
            b->SetColors(kAccent, kAccentHover);
            b->SetTextColors(Color(255, 255, 255, 255));
        }
        return b;
    }

    std::shared_ptr<UltraCanvasDropdown> MakeChoiceDropdown(const std::string& id,
                                                            const Choice* choices, size_t count,
                                                            int width) {
        auto d = std::make_shared<UltraCanvasDropdown>(id, 0, 0, width, kControlHeight);
        for (size_t i = 0; i < count; ++i) d->AddItem(choices[i].label, choices[i].value);
        d->SetSelectedIndex(0, false);
        d->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        return d;
    }

    std::string SelectedValue(const std::shared_ptr<UltraCanvasDropdown>& d) {
        if (!d) return std::string();
        const DropdownItem* item = d->GetSelectedItem();
        return item ? item->value : std::string();
    }

    // "4.2 s" - shown to a person, so it follows their locale.
    std::string FormatSeconds(int64_t ms) {
        std::ostringstream s;
        s << std::fixed << std::setprecision(1) << (static_cast<double>(ms) / 1000.0) << " s";  // locale-ok: shown to a person
        return s.str();
    }

    std::string FormatUsd(double usd) {
        std::ostringstream s;
        s << "$" << std::fixed << std::setprecision(usd < 0.1 ? 4 : 2) << usd;  // locale-ok: shown to a person
        return s.str();
    }

    std::string CurrentFolder() {
        std::error_code ec;
        const std::filesystem::path p = std::filesystem::current_path(ec);
        return ec ? std::string() : PathToUtf8(p);
    }

    // Escapes text for label markup.
    std::string EscapeMarkup(const std::string& text) {
        std::string out;
        for (char c : text) {
            switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                default: out += c;
            }
        }
        return out;
    }

    // Turns the Anthropic addresses in a status message (claude.ai,
    // claude.com/claude-code, with or without https://) into links: returns
    // the label markup and fills `links` with their byte ranges in the
    // rendered text - which is the plain text, so the offsets are its own.
    std::string LinkifyClaudeAddresses(const std::string& text,
                                       std::vector<LabelTextLink>& links) {
        static const std::regex kAddress(
                R"((https?://)?(claude\.ai|claude\.com)(/[A-Za-z0-9_\-./?=&%#]*)?)");
        std::string markup;
        size_t done = 0;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), kAddress);
             it != std::sregex_iterator(); ++it) {
            size_t start = static_cast<size_t>(it->position());
            std::string address = it->str();
            // A sentence's full stop or comma after the address is not part of it.
            while (!address.empty() && (address.back() == '.' || address.back() == ','))
                address.pop_back();
            const size_t end = start + address.size();
            markup += EscapeMarkup(text.substr(done, start - done));
            markup += "<span foreground=\"#C9643F\" underline=\"single\">" +
                      EscapeMarkup(address) + "</span>";
            LabelTextLink link;
            link.startByte = static_cast<int>(start);
            link.endByte = static_cast<int>(end);
            link.href = address.rfind("http", 0) == 0 ? address : "https://" + address;
            links.push_back(std::move(link));
            done = end;
        }
        markup += EscapeMarkup(text.substr(done));
        return markup;
    }

    // Puts text in a markdown blockquote line by line, so a multi-line tool
    // summary or error stays inside the quote.
    std::string Quote(const std::string& text) {
        std::string out = "> ";
        for (char c : text) {
            out += c;
            if (c == '\n') out += "> ";
        }
        return out;
    }
} // namespace

UltraClaudeWindow::UltraClaudeWindow(std::string version, std::string executable)
    : version_(std::move(version)), executable_(std::move(executable)),
      chats_(ChatStore::DefaultFolder()) {}

UltraClaudeWindow::~UltraClaudeWindow() {
    // The reader threads post into this object: end them, and wait for
    // them, before any member they touch is destroyed.
    session_.StopAndWait();
    loginProcess_.StopAndWait();
    JoinBackgroundThread();
    // The measuring thread holds its own share of the queue and stops after
    // the git command it is in; it touches nothing else of this window.
    {
        std::lock_guard<std::mutex> lock(measure_->mutex);
        measure_->stop = true;
        measure_->folders.clear();
    }
    measure_->wake.notify_all();
    if (timer_ != 0) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(timer_);
        timer_ = 0;
    }
}

bool UltraClaudeWindow::Create() {
    WindowConfig wc;
    // Every app shows its version in its main window title (AGENTS.md).
    wc.title = "UltraClaude " + version_;
    wc.width = 1120;
    wc.height = 700;
    wc.resizable = true;
    window_ = CreateWindow(wc);
    if (!window_ || !window_->IsCreated()) { window_.reset(); return false; }

    window_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window_->SetBackgroundColor(kPageBackground);

    window_->AddChild(BuildSignInPage());
    window_->AddChild(BuildChatView());
    window_->onWindowClosed = [this]() { if (onClosed) onClosed(); };

    LoadChats();
    ShowSignInPage();
    CheckAuthStatus();
    return true;
}

void UltraClaudeWindow::Show() {
    if (window_) window_->Show();
}

// ===================================================================== views

std::shared_ptr<UltraCanvasContainer> UltraClaudeWindow::BuildSignInPage() {
    signInPage_ = std::make_shared<UltraCanvasContainer>("uc-signin");
    signInPage_->layout.SetFlexColumn()
                       .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Center)
                       .SetFlexGap(12);
    signInPage_->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // The app icon, the same file the window and the desktop entry use.
    auto logo = std::make_shared<UltraCanvasImageElement>("uc-signin-logo", 0, 0,
                                                          kLogoSize, kLogoSize);
    logo->LoadFromFile(NormalizePath(GetResourcesDir() + "media/appicon/UltraClaude.png"));
    logo->SetFitMode(ImageFitMode::Contain);
    logo->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    signInPage_->AddChild(logo);

    auto title = MakeLabel("uc-signin-title", "UltraClaude", kTitleSize);
    title->SetFontWeight(FontWeight::Bold);
    signInPage_->AddChild(title);

    signInPage_->AddChild(MakeLabel("uc-signin-tagline",
            "Chat with Claude on your Pro or Max subscription",
            kTaglineSize, kMutedTextColor));
    signInPage_->AddSpacer(8);

    email_ = std::make_shared<UltraCanvasTextInput>("uc-signin-email", 0, 0,
                                                    kFormWidth, kButtonHeight);
    email_->SetPlaceholder("Email address (optional)");
    email_->SetFontSize(kButtonFont);
    email_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    email_->onEnterPressed = [this](const std::string&) { LogInOrContinue(); return true; };
    signInPage_->AddChild(email_);

    logIn_ = std::make_shared<UltraCanvasButton>("uc-signin-login", 0, 0,
                                                 kFormWidth, kButtonHeight, "Log in");
    logIn_->SetFontSize(kButtonFont);
    logIn_->SetCornerRadius(kButtonRadius);
    logIn_->SetColors(kAccent, kAccentHover);
    logIn_->SetTextColors(Color(255, 255, 255, 255));
    logIn_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    // The same button cancels a sign-in that is waiting for the browser.
    logIn_->SetOnClick([this]() { if (loggingIn_) CancelLogIn(); else LogInOrContinue(); });
    signInPage_->AddChild(logIn_);

    createAccount_ = std::make_shared<UltraCanvasButton>("uc-signin-create", 0, 0,
                                                         kFormWidth, kButtonHeight,
                                                         "Create account");
    createAccount_->SetFontSize(kButtonFont);
    createAccount_->SetCornerRadius(kButtonRadius);
    createAccount_->SetColors(kSecondary, kSecondaryHover);
    createAccount_->SetTextColors(kTextColor);
    createAccount_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    createAccount_->SetOnClick([this]() { CreateAccount(); });
    signInPage_->AddChild(createAccount_);

    signInStatus_ = MakeLabel("uc-signin-status", "", kTextFontSize, kMutedTextColor);
    signInStatus_->SetWrap(TextWrap::WrapWord);
    signInStatus_->SetAlignment(TextAlignment::Center);
    signInStatus_->size.width = CSSLayout::Dimension::Px(460);
    signInStatus_->SetShowLinkTooltips(true);
    signInStatus_->onLinkActivated = [](const std::string& url) { OpenURL(url); };
    signInPage_->AddChild(signInStatus_);
    SetSignInStatus("Checking Claude Code\xE2\x80\xA6");

    // Shown while `claude auth login` waits: when the browser could not be
    // opened, or the sign-in page ends on a code to paste, the URL opens from
    // here and the code goes back to the CLI through its standard input.
    codeRow_ = std::make_shared<UltraCanvasContainer>("uc-signin-code-row");
    codeRow_->layout.SetFlexColumn().SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    codeRow_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    auto openPage = std::make_shared<UltraCanvasButton>("uc-signin-open-page", 0, 0,
                                                        kFormWidth, kButtonHeight,
                                                        "Open sign-in page");
    openPage->SetFontSize(kButtonFont);
    openPage->SetCornerRadius(kButtonRadius);
    openPage->SetColors(kSecondary, kSecondaryHover);
    openPage->SetTextColors(kTextColor);
    openPage->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    openPage->SetOnClick([this]() { if (!signInUrl_.empty()) OpenURL(signInUrl_); });
    codeRow_->AddChild(openPage);
    codeRow_->AddChild(MakeLabel("uc-signin-code-hint",
            "If the page shows a code, paste it here:", kTextFontSize, kMutedTextColor));
    auto codeLine = std::make_shared<UltraCanvasContainer>("uc-signin-code-line");
    codeLine->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    codeLine->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    loginCode_ = std::make_shared<UltraCanvasTextInput>("uc-signin-code", 0, 0,
                                                        kFormWidth - 88, kButtonHeight);
    loginCode_->SetPlaceholder("Login code");
    loginCode_->SetFontSize(kButtonFont);
    loginCode_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    loginCode_->onEnterPressed = [this](const std::string&) { SubmitLoginCode(); return true; };
    codeLine->AddChild(loginCode_);
    submitCode_ = std::make_shared<UltraCanvasButton>("uc-signin-code-submit", 0, 0,
                                                      80, kButtonHeight, "Submit");
    submitCode_->SetFontSize(kButtonFont);
    submitCode_->SetCornerRadius(kButtonRadius);
    submitCode_->SetColors(kAccent, kAccentHover);
    submitCode_->SetTextColors(Color(255, 255, 255, 255));
    submitCode_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    submitCode_->SetOnClick([this]() { SubmitLoginCode(); });
    codeLine->AddChild(submitCode_);
    codeRow_->AddChild(codeLine);
    codeRow_->SetVisible(false);
    signInPage_->AddChild(codeRow_);

    signInPage_->AddChild(MakeLabel("uc-signin-note",
            "You sign in on Anthropic's page in your browser. UltraClaude never sees your password.",
            9.0f, kMutedTextColor));
    return signInPage_;
}

std::shared_ptr<UltraCanvasContainer> UltraClaudeWindow::BuildChatSidebar() {
    auto sidebar = std::make_shared<UltraCanvasContainer>("uc-chats");
    sidebar->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    sidebar->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    sidebar->size.width = CSSLayout::Dimension::Px(kSidebarWidth);
    sidebar->SetPadding(8, 8, 8, 8);
    sidebar->SetBorderRight(1, kRuleColor);
    sidebar->SetBackgroundColor(kSidebarBackground);

    auto head = std::make_shared<UltraCanvasContainer>("uc-chats-head");
    head->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    head->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    auto title = MakeLabel("uc-chats-title", "Chats", 11.0f);
    title->SetFontWeight(FontWeight::Bold);
    title->layoutItem.SetFlexGrow(1);
    head->AddChild(title);
    auto newChat = MakeButton("uc-new-chat", "New chat", 84, /*primary=*/true);
    newChat->SetOnClick([this]() { NewChat(); });
    head->AddChild(newChat);
    sidebar->AddChild(head);

    chatModel_ = std::make_shared<ChatListModel>();
    chatList_ = std::make_shared<UltraCanvasListView>("uc-chat-list", 0, 0, kSidebarWidth - 16, 200);
    chatList_->SetModel(chatModel_);
    chatList_->SetDelegate(std::make_shared<ChatListDelegate>());
    chatList_->SetRowHeight(30);
    chatList_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    chatList_->onSelectionChanged = [this](const std::vector<int>& rows) {
        if (fillingChatList_ || rows.empty()) return;
        const auto& list = chats_.Chats();
        if (rows.front() >= 0 && rows.front() < static_cast<int>(list.size()))
            OpenChat(list[static_cast<size_t>(rows.front())].id);
    };
    sidebar->AddChild(chatList_);

    deleteChat_ = MakeButton("uc-delete-chat", "Delete chat", kSidebarWidth - 16);
    deleteChat_->SetOnClick([this]() { DeleteCurrentChat(); });
    deleteChat_->SetDisabled(true);
    sidebar->AddChild(deleteChat_);
    return sidebar;
}

std::shared_ptr<UltraCanvasContainer> UltraClaudeWindow::BuildChatView() {
    // The chat list on the left, the conversation on the right.
    chatView_ = std::make_shared<UltraCanvasContainer>("uc-chat");
    chatView_->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    chatView_->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    chatView_->AddChild(BuildChatSidebar());

    auto main = std::make_shared<UltraCanvasContainer>("uc-chat-main");
    main->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    main->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    chatView_->AddChild(main);

    // ----- toolbar -----
    auto bar = std::make_shared<UltraCanvasContainer>("uc-chat-bar");
    bar->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bar->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    bar->SetPadding(8, 12, 8, 12);
    bar->SetBorderBottom(1, kRuleColor);

    bar->AddChild(MakeLabel("uc-model-label", "Model"));
    model_ = MakeChoiceDropdown("uc-model", kModels, std::size(kModels), 100);
    bar->AddChild(model_);

    bar->AddChild(MakeLabel("uc-perm-label", "Permissions"));
    permissions_ = MakeChoiceDropdown("uc-perm", kPermissionModes, std::size(kPermissionModes), 140);
    bar->AddChild(permissions_);

    bar->AddChild(MakeLabel("uc-folder-label", "Folder"));
    folder_ = std::make_shared<UltraCanvasTextInput>("uc-folder", 0, 0, 160, kControlHeight);
    folder_->SetText(CurrentFolder());
    folder_->SetFontSize(kTextFontSize);
    folder_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    bar->AddChild(folder_);
    browse_ = MakeButton("uc-folder-browse", "\xE2\x80\xA6", 30);
    browse_->SetOnClick([this]() { BrowseFolder(); });
    bar->AddChild(browse_);

    auto logOut = MakeButton("uc-log-out", "Log out", 72);
    logOut->SetOnClick([this]() { LogOut(); });
    bar->AddChild(logOut);
    main->AddChild(bar);

    // ----- transcript -----
    transcript_ = std::make_shared<UltraCanvasTextArea>("uc-transcript", 0, 0, 400, 300);
    transcript_->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
    transcript_->SetReadOnly(true);
    transcript_->SetWordWrap(true);
    transcript_->SetShowLineNumbers(false);
    transcript_->SetFontSize(kTranscriptFontSize);
    transcript_->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    transcript_->onMarkdownLinkClick = [](const std::string& url) { OpenURL(url); };
    main->AddChild(transcript_);

    // ----- prompt -----
    auto inputRow = std::make_shared<UltraCanvasContainer>("uc-input-row");
    inputRow->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::End);
    inputRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    inputRow->SetPadding(8, 12, 4, 12);
    inputRow->SetBorderTop(1, kRuleColor);

    // Several lines: Enter sends, Shift+Enter starts a new line (the text
    // area's onBeforeKeyDown sees the key before it would insert a break).
    prompt_ = std::make_shared<UltraCanvasTextArea>("uc-prompt", 0, 0, 300, kPromptHeight);
    prompt_->SetEditingMode(TextAreaEditingMode::PlainText);
    prompt_->SetWordWrap(true);
    prompt_->SetShowLineNumbers(false);
    prompt_->SetFontSize(kTranscriptFontSize);
    prompt_->SetPlaceholder("Message Claude\xE2\x80\xA6  (Enter sends, Shift+Enter for a new line)");
    prompt_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    prompt_->onBeforeKeyDown = [this](const UCEvent& e) {
        const bool enter = e.virtualKey == UCKeys::Return || e.virtualKey == UCKeys::NumPadEnter;
        if (!enter || e.shift) return false;   // Shift+Enter: the area inserts the break
        // Enter never inserts a break here; while Claude answers it waits,
        // and the text stays for the next turn.
        if (!busy_) SendCurrentPrompt();
        return true;
    };
    inputRow->AddChild(prompt_);

    send_ = MakeButton("uc-send", "Send", 80, /*primary=*/true);
    send_->SetOnClick([this]() { if (busy_) StopAnswer(); else SendCurrentPrompt(); });
    inputRow->AddChild(send_);
    main->AddChild(inputRow);

    status_ = MakeLabel("uc-status", "", 9.0f, kMutedTextColor);
    status_->SetMargin(0, 12, 6, 12);
    main->AddChild(status_);
    return chatView_;
}

void UltraClaudeWindow::ShowSignInPage() {
    if (chatView_) chatView_->SetVisible(false);
    if (signInPage_) signInPage_->SetVisible(true);
    if (window_) window_->RequestRedraw();
}

void UltraClaudeWindow::ShowChatView() {
    if (signInPage_) signInPage_->SetVisible(false);
    if (chatView_) chatView_->SetVisible(true);
    if (prompt_) prompt_->SetFocus(true);
    if (window_) window_->RequestRedraw();
}

// =================================================================== sign-in

void UltraClaudeWindow::SetSignInStatus(const std::string& text) {
    if (!signInStatus_) return;
    // Anthropic addresses in the message (claude.ai, claude.com/claude-code)
    // are links: coloured, underlined, and opened in the browser on a click.
    std::vector<LabelTextLink> links;
    const std::string markup = LinkifyClaudeAddresses(text, links);
    signInStatus_->SetTextIsMarkup(true);
    signInStatus_->SetText(markup);
    signInStatus_->SetTextLinks(std::move(links));
}

void UltraClaudeWindow::CheckAuthStatus() {
    if (background_.joinable()) return;   // one probe at a time; it reports soon
    EnsureTimer();
    ++activeWork_;
    const std::string executable = executable_;
    background_ = std::thread([this, executable]() {
        // `claude auth status` prints {"loggedIn": true, "authMethod": ...}.
        const ProcessOutput out = RunProcessCaptured({executable, "auth", "status"}, {});
        bool loggedIn = false;
        std::string detail;
        if (out.started) {
            const std::string text(out.standardOutput.begin(), out.standardOutput.end());
            JSONParseResult parsed;
            const JSONValue doc = JSON::Parse(text, &parsed);
            loggedIn = parsed.success && doc["loggedIn"].GetBoolean(false);
            detail = doc["authMethod"].GetString();
        } else {
            detail = out.error;
        }
        const bool found = out.started;
        Post([this, found, loggedIn, detail]() {
            JoinBackgroundThread();   // it has posted its last word
            --activeWork_;
            ApplyAuthStatus(found, loggedIn, detail);
        });
    });
}

void UltraClaudeWindow::ApplyAuthStatus(bool cliFound, bool loggedIn, const std::string& detail) {
    loggedIn_ = loggedIn;
    if (!cliFound) {
        // The reason is a sentence of its own; its full stop would double up
        // with the bracket's.
        std::string reason = detail;
        while (!reason.empty() && (reason.back() == '.' || reason.back() == ' ')) reason.pop_back();
        SetSignInStatus("Claude Code was not found (" + reason + "). Install it from "
                        "claude.com/claude-code, then reopen UltraClaude.");
        logIn_->SetText("Log in");
        logIn_->SetDisabled(true);
        return;
    }
    logIn_->SetDisabled(false);
    if (loggedIn) {
        logIn_->SetText("Continue");
        SetSignInStatus("Claude Code is signed in on this computer.");
        // Demo path, as UltraMail's ULTRAMAIL_DEMO_* variables: continue to
        // the chat and send this prompt - how the chat view is exercised on
        // a display nobody can type into (CI, Xvfb).
        if (const char* demo = std::getenv("ULTRACLAUDE_DEMO_PROMPT"); demo && *demo && !demoSent_) {
            demoSent_ = true;
            ShowChatView();
            prompt_->SetText(demo);
            SendCurrentPrompt();
        }
    } else {
        logIn_->SetText("Log in");
        SetSignInStatus("Log in with the Claude account that has your subscription.");
    }
}

void UltraClaudeWindow::LogInOrContinue() {
    if (loggingIn_) return;
    if (loggedIn_) { ShowChatView(); return; }

    std::vector<std::string> argv = {executable_, "auth", "login", "--claudeai"};
    const std::string email = email_ ? email_->GetText() : std::string();
    if (!email.empty()) { argv.push_back("--email"); argv.push_back(email); }

    // The CLI prints "Opening browser to sign in…", then "If the browser
    // didn't open, visit: <url>", then waits at "Paste code here if
    // prompted > " (no line end, so it never arrives as a line). The URL line
    // is the cue for the code box.
    auto onLine = [this](const std::string& line) {
        const size_t url = line.find("https://");
        if (url != std::string::npos) {
            std::string address = line.substr(url);
            while (!address.empty() && (address.back() == ' ' || address.back() == '\r'))
                address.pop_back();
            Post([this, address]() { ShowLoginCodeBox(address); });
            return;
        }
        const std::string shown = FirstLineShortened(line, 300);
        if (!shown.empty()) Post([this, shown]() { SetSignInStatus(shown); });
    };
    auto onExit = [this](int exitCode, const std::string& standardError) {
        const std::string error = FirstLineShortened(standardError, 300);
        Post([this, exitCode, error]() {
            --activeWork_;
            loggingIn_ = false;
            HideLoginCodeBox();
            createAccount_->SetDisabled(false);
            email_->SetDisabled(false);
            if (exitCode == 0) {
                SetSignInStatus("Signed in.");
                CheckAuthStatus();   // confirms it, then Continue opens the chat
                loggedIn_ = true;
                logIn_->SetText("Continue");
                ShowChatView();
            } else {
                logIn_->SetText("Log in");
                SetSignInStatus(error.empty() ? "Sign-in did not finish." : error);
            }
        });
    };

    std::string error;
    EnsureTimer();
    if (!loginProcess_.Start(argv, std::string(), std::string(), onLine, onExit, error,
                             ClaudeCliProcess::InputMode::KeepOpen)) {
        SetSignInStatus(error);
        return;
    }
    ++activeWork_;
    loggingIn_ = true;
    logIn_->SetText("Cancel");
    createAccount_->SetDisabled(true);
    email_->SetDisabled(true);
    SetSignInStatus("Finish signing in on the page that opened in your browser\xE2\x80\xA6");
}

void UltraClaudeWindow::ShowLoginCodeBox(const std::string& url) {
    if (!loggingIn_) return;
    signInUrl_ = url;
    loginCode_->SetText("");
    loginCode_->SetDisabled(false);
    submitCode_->SetDisabled(false);
    codeRow_->SetVisible(true);
    SetSignInStatus("Sign in on the page in your browser. If it did not open, use "
                    "Open sign-in page.");
    if (window_) window_->RequestRedraw();
}

void UltraClaudeWindow::HideLoginCodeBox() {
    signInUrl_.clear();
    if (loginCode_) loginCode_->SetText("");
    // Hiding the row also takes the focus from the code field in it.
    if (codeRow_) codeRow_->SetVisible(false);
    if (window_) window_->RequestRedraw();
}

void UltraClaudeWindow::SubmitLoginCode() {
    if (!loggingIn_ || !loginCode_) return;
    std::string code = loginCode_->GetText();
    // A pasted code often carries the line break or spaces it was copied with.
    while (!code.empty() && (code.back() == '\n' || code.back() == '\r' || code.back() == ' '))
        code.pop_back();
    const size_t start = code.find_first_not_of(" \t");
    code = start == std::string::npos ? std::string() : code.substr(start);
    if (code.empty()) { SetSignInStatus("Paste the code the sign-in page showed."); return; }
    if (!loginProcess_.WriteInput(code + "\n")) {
        SetSignInStatus("Claude Code is no longer waiting for a code. Choose Log in again.");
        return;
    }
    loginCode_->SetDisabled(true);
    submitCode_->SetDisabled(true);
    SetSignInStatus("Checking the code\xE2\x80\xA6");
}

void UltraClaudeWindow::CancelLogIn() {
    loginProcess_.Stop();
    SetSignInStatus("Cancelling\xE2\x80\xA6");
}

void UltraClaudeWindow::CreateAccount() {
    OpenURL(kCreateAccountUrl);
    SetSignInStatus("Create your account on claude.ai, then come back and choose Log in.");
}

void UltraClaudeWindow::LogOut() {
    if (busy_) StopAnswer();
    // `claude auth logout` signs the CLI out on this computer, for every
    // program that uses it - which is what Log out means here.
    const ProcessOutput out = RunProcessCaptured({executable_, "auth", "logout"}, {});
    session_.Reset();
    loggedIn_ = false;
    logIn_->SetText("Log in");
    ShowSignInPage();
    SetSignInStatus(out.Succeeded() ? "Logged out." : "Log out failed: " +
                    FirstLineShortened(out.error.empty() ? out.standardError : out.error, 200));
}

// ====================================================================== chat

ClaudeChatOptions UltraClaudeWindow::CurrentOptions() const {
    ClaudeChatOptions o;
    o.executable = executable_;
    o.model = SelectedValue(model_);
    o.permissionMode = SelectedValue(permissions_);
    o.workingDirectory = folder_ ? folder_->GetText() : std::string();
    return o;
}

void UltraClaudeWindow::SetStatus(const std::string& text) {
    if (status_) status_->SetText(text);
}

void UltraClaudeWindow::SetBusy(bool busy) {
    busy_ = busy;
    send_->SetText(busy ? "Stop" : "Send");
    model_->SetDisabled(busy);
    permissions_->SetDisabled(busy);
    folder_->SetDisabled(busy || folderLocked_);
    browse_->SetDisabled(busy || folderLocked_);
    deleteChat_->SetDisabled(busy || currentChatId_.empty());
}

void UltraClaudeWindow::AppendTranscript(const std::string& markdown) {
    if (markdown.empty()) return;
    transcript_->AppendText(markdown);
    // Count the line breaks the transcript now ends with.
    size_t i = markdown.size();
    size_t breaks = 0;
    while (i > 0 && markdown[i - 1] == '\n') { --i; ++breaks; }
    trailingBreaks_ = (i == 0) ? trailingBreaks_ + breaks : breaks;
}

void UltraClaudeWindow::AppendBlock(const std::string& markdown) {
    // A block (a quote, a heading) only reads as one after a blank line.
    if (!transcript_->GetText().empty() && trailingBreaks_ < 2)
        AppendTranscript(std::string(2 - trailingBreaks_, '\n'));
    AppendTranscript(markdown + "\n\n");
}

void UltraClaudeWindow::BeginClaudeSection() {
    if (claudeSectionOpen_) return;
    claudeSectionOpen_ = true;
    AppendTranscript("**Claude**\n\n");
}

void UltraClaudeWindow::SendCurrentPrompt() {
    if (busy_ || !prompt_) return;
    const std::string text = prompt_->GetText();
    if (text.find_first_not_of(" \t\r\n") == std::string::npos) return;

    ClaudeChatOptions options = CurrentOptions();
    if (const ChatRecord* chat = chats_.Find(currentChatId_)) {
        // An existing chat resumes in its own folder (the CLI keeps sessions
        // per folder); model and permission mode follow the toolbar.
        options.workingDirectory = chat->folder;
    }
    if (!options.workingDirectory.empty() &&
        !std::filesystem::is_directory(PathFromUtf8(options.workingDirectory))) {
        SetStatus("The folder does not exist: " + options.workingDirectory);
        return;
    }

    if (currentChatId_.empty()) {
        // The first prompt makes the chat, titled by that prompt.
        const ChatRecord& chat = chats_.Create(ChatStore::TitleFromPrompt(text),
                                               options.workingDirectory,
                                               options.model, options.permissionMode);
        currentChatId_ = chat.id;
        SetFolderLocked(true);
        RefreshChatList();
        MeasureFolder(chat.folder);
    } else if (ChatRecord* chat = chats_.Find(currentChatId_)) {
        chat->model = options.model;
        chat->permissionMode = options.permissionMode;
    }

    if (!transcript_->GetText().empty()) AppendTranscript("\n---\n\n");
    AppendTranscript("**You**\n\n" + text + "\n\n");
    prompt_->SetText("");
    claudeSectionOpen_ = false;
    toolCalls_ = 0;

    EnsureTimer();
    ++activeWork_;   // until ProcessExited or ProcessError is applied
    std::string error;
    auto onEvent = [this](const ClaudeStreamEvent& e) {
        Post([this, e]() { ApplyEvent(e); });
    };
    if (!session_.SendPrompt(text, options, onEvent, error)) {
        SetStatus(error);
        return;   // the ProcessError event reports it in the transcript too
    }
    SetBusy(true);
    SetStatus("Claude is thinking\xE2\x80\xA6");
}

void UltraClaudeWindow::StopAnswer() {
    session_.Stop();
    SetStatus("Stopping\xE2\x80\xA6");
}

void UltraClaudeWindow::NewChat() {
    if (busy_) return;
    currentChatId_.clear();
    session_.Reset();
    transcript_->SetText("");
    trailingBreaks_ = 0;
    SetFolderLocked(false);
    RefreshChatList();
    SetStatus("New conversation.");
    prompt_->SetFocus(true);
}

void UltraClaudeWindow::DeleteCurrentChat() {
    if (busy_ || currentChatId_.empty()) return;
    chats_.Remove(currentChatId_);
    std::string error;
    if (!chats_.Save(error)) SetStatus(error);
    NewChat();
    SetStatus("Chat deleted.");
}

// ================================================================ chat list

void UltraClaudeWindow::LoadChats() {
    std::string error;
    if (!chats_.Load(error)) SetStatus(error);
    RefreshChatList();
    for (const ChatRecord& chat : chats_.Chats()) MeasureFolder(chat.folder);
}

void UltraClaudeWindow::RefreshChatList() {
    if (!chatModel_ || !chatList_) return;
    fillingChatList_ = true;
    std::vector<ChatListRow> rows;
    int currentRow = -1;
    const auto& list = chats_.Chats();
    for (size_t i = 0; i < list.size(); ++i) {
        const ChatRecord& chat = list[i];
        rows.push_back(ChatListRow{chat.title, chat.folder, BadgeFor(chat.folder)});
        if (chat.id == currentChatId_) currentRow = static_cast<int>(i);
    }
    chatModel_->SetRows(std::move(rows));
    if (IListSelection* selection = chatList_->GetSelection()) {
        selection->Clear();
        if (currentRow >= 0) selection->Select(currentRow);
    }
    chatList_->RequestRedraw();
    fillingChatList_ = false;
    if (deleteChat_) deleteChat_->SetDisabled(busy_ || currentChatId_.empty());
}

void UltraClaudeWindow::OpenChat(const std::string& chatId) {
    if (chatId == currentChatId_) return;
    if (busy_) {
        // The answer belongs to the chat that asked: switch once it is in.
        RefreshChatList();
        SetStatus("Claude is still answering - wait for it, or Stop it, before switching chats.");
        return;
    }
    const ChatRecord* chat = chats_.Find(chatId);
    if (!chat) return;
    currentChatId_ = chat->id;
    session_.SetSessionId(chat->sessionId);

    const std::string transcript = chats_.LoadTranscript(chat->id);
    transcript_->SetText(transcript);
    transcript_->MoveCursorToEnd();
    trailingBreaks_ = 0;
    for (size_t i = transcript.size(); i > 0 && transcript[i - 1] == '\n'; --i) ++trailingBreaks_;

    folder_->SetText(chat->folder);
    SetFolderLocked(true);
    MeasureFolder(chat->folder);
    for (size_t i = 0; i < std::size(kModels); ++i)
        if (chat->model == kModels[i].value) model_->SetSelectedIndex(static_cast<int>(i), false);
    for (size_t i = 0; i < std::size(kPermissionModes); ++i)
        if (chat->permissionMode == kPermissionModes[i].value)
            permissions_->SetSelectedIndex(static_cast<int>(i), false);

    RefreshChatList();
    SetStatus(chat->sessionId.empty() ? "This chat has no Claude session yet."
                                      : "Continuing \"" + chat->title + "\".");
    prompt_->SetFocus(true);
}

void UltraClaudeWindow::SaveCurrentChat() {
    if (currentChatId_.empty()) return;
    std::string error;
    bool saved = chats_.SaveTranscript(currentChatId_, transcript_->GetText(), error);
    if (saved) {
        chats_.Touch(currentChatId_);
        saved = chats_.Save(error);
    }
    if (!saved) SetStatus("The chat could not be saved: " + error);
    RefreshChatList();
    // The turn may have changed files: count again.
    if (const ChatRecord* chat = chats_.Find(currentChatId_)) MeasureFolder(chat->folder);
}

// ============================================================ badges

ChatBadge UltraClaudeWindow::BadgeFor(const std::string& folder) const {
    ChatBadge badge;
    const auto it = measured_.find(folder);
    if (it == measured_.end()) {
        if (measuring_.count(folder)) badge.kind = ChatBadge::Kind::Measuring;
        if (measuring_.count(folder)) badge.tooltip = "Counting the lines not yet in the default branch\xE2\x80\xA6";
        return badge;
    }
    const RepoLines& r = it->second;
    switch (r.state) {
        case RepoLines::State::Measured:
            badge.kind = ChatBadge::Kind::Lines;
            badge.lines = r.lines;
            badge.tooltip = r.lines == 0
                ? "Everything here is in " + r.base + "."
                : std::to_string(r.lines) + " lines not yet in " + r.base +
                  " (committed, uncommitted and new files)" +
                  (r.branch.empty() ? std::string() : " on " + r.branch) + ".";
            if (!r.fetched) badge.tooltip += " " + r.base + " could not be fetched; compared with it as last fetched.";
            break;
        case RepoLines::State::NoBase:
            badge.kind = ChatBadge::Kind::NoBadge;
            badge.tooltip = "A git folder with no origin/main to compare with.";
            break;
        case RepoLines::State::NotARepo:
            break;   // nothing to count, nothing to say
        case RepoLines::State::Failed:
            badge.kind = ChatBadge::Kind::Unknown;
            badge.tooltip = "Could not count the lines: " + FirstLineShortened(r.error, 200);
            break;
    }
    return badge;
}

void UltraClaudeWindow::MeasureFolder(const std::string& folder) {
    if (folder.empty() || measuring_.count(folder)) return;
    StartMeasureThread();
    measuring_.insert(folder);
    {
        std::lock_guard<std::mutex> lock(measure_->mutex);
        measure_->folders.push_back(folder);
    }
    measure_->wake.notify_one();
    ++activeWork_;   // until its result is applied
    EnsureTimer();
    // A folder never measured shows "…" while it is.
    if (!measured_.count(folder)) RefreshChatList();
}

void UltraClaudeWindow::ApplyMeasurement(const std::string& folder, const RepoLines& lines) {
    measuring_.erase(folder);
    measured_[folder] = lines;
    RefreshChatList();
}

void UltraClaudeWindow::StartMeasureThread() {
    if (measureThreadStarted_) return;
    measureThreadStarted_ = true;
    // Detached on purpose: it owns its share of the queue and nothing else,
    // so the window can close while it waits on git (see ~UltraClaudeWindow).
    std::thread([queue = measure_]() {
        for (;;) {
            std::string folder;
            {
                std::unique_lock<std::mutex> lock(queue->mutex);
                queue->wake.wait(lock, [&] { return queue->stop || !queue->folders.empty(); });
                if (queue->stop) return;
                folder = std::move(queue->folders.front());
                queue->folders.pop_front();
            }
            RepoLines lines = MeasureLinesNotMerged(folder);
            std::lock_guard<std::mutex> lock(queue->mutex);
            if (queue->stop) return;
            queue->results.emplace_back(folder, std::move(lines));
        }
    }).detach();
}

void UltraClaudeWindow::SetFolderLocked(bool locked) {
    folderLocked_ = locked;
    folder_->SetDisabled(busy_ || locked);
    browse_->SetDisabled(busy_ || locked);
}

void UltraClaudeWindow::BrowseFolder() {
    FileDialogOptions options;
    options.title = "Folder Claude works in";
    options.initialDirectory = folder_->GetText();
    options.parentWindow = window_.get();
    UltraCanvasFileLoader::SelectFolderDialog(options, [this](DialogResult result,
                                                              const std::string& path) {
        if (result != DialogResult::OK || path.empty() || !folder_) return;
        folder_->SetText(path);
    });
}

void UltraClaudeWindow::ApplyEvent(const ClaudeStreamEvent& e) {
    switch (e.kind) {
        case ClaudeEventKind::SessionStarted:
            // The id to resume next time; a resumed session may come back
            // under a new one, so the chat follows whatever the CLI reports.
            if (ChatRecord* chat = chats_.Find(currentChatId_);
                chat && !e.sessionId.empty() && chat->sessionId != e.sessionId) {
                chat->sessionId = e.sessionId;
                std::string error;
                if (!chats_.Save(error)) SetStatus(error);
            }
            if (!e.model.empty()) SetStatus("Claude is thinking\xE2\x80\xA6  (" + e.model + ")");
            break;

        case ClaudeEventKind::TextDelta:
        case ClaudeEventKind::AssistantText:
            BeginClaudeSection();
            AppendTranscript(e.text);
            if (e.kind == ClaudeEventKind::AssistantText) AppendTranscript("\n\n");
            break;

        case ClaudeEventKind::ToolUse: {
            BeginClaudeSection();
            ++toolCalls_;
            AppendBlock(Quote("**" + e.toolName + "**" +
                              (e.text.empty() ? "" : "  `" + e.text + "`")));
            SetStatus("Running " + e.toolName + "\xE2\x80\xA6");
            break;
        }

        case ClaudeEventKind::ToolResult:
            // Successful results are left out - the reply says what came of
            // them; a failure is shown, because it explains what follows.
            if (e.isError && !e.text.empty()) AppendBlock(Quote("*" + e.text + "*"));
            break;

        case ClaudeEventKind::TurnFinished: {
            if (e.isError && !e.textAlreadyShown) {
                BeginClaudeSection();
                AppendBlock(Quote("**Error:** " + e.text));
            } else if (!claudeSectionOpen_ && !e.text.empty()) {
                // Nothing was streamed (an old CLI): show the final answer.
                BeginClaudeSection();
                AppendTranscript(e.text + "\n\n");
            }
            if (e.permissionDenials > 0) {
                AppendBlock(Quote("*" + std::to_string(e.permissionDenials) +
                        " tool call(s) were refused. Choose a wider Permissions "
                        "setting to let Claude do this.*"));
            }
            std::string summary = "Done in " + FormatSeconds(e.durationMs);
            if (e.numTurns > 1) summary += " \xC2\xB7 " + std::to_string(e.numTurns) + " steps";
            if (e.costUsd > 0)
                summary += " \xC2\xB7 " + FormatUsd(e.costUsd) +
                           " API-equivalent (covered by your subscription)";
            SetStatus(summary);
            break;
        }

        case ClaudeEventKind::ProcessExited:
            --activeWork_;
            SetBusy(false);
            if (e.isError) {
                // The CLI ended without reporting a result: stopped, crashed,
                // or refused to start (not signed in, unknown flag).
                std::string why = FirstLineShortened(e.text, 400);
                if (why.empty()) why = e.exitCode < 0 ? "Stopped." : "Claude Code exited with code " +
                                                       std::to_string(e.exitCode) + ".";
                AppendBlock(Quote("*" + why + "*"));
                SetStatus(why);
                if (why.find("login") != std::string::npos || why.find("log in") != std::string::npos ||
                    why.find("authenticat") != std::string::npos) {
                    CheckAuthStatus();
                }
            }
            SaveCurrentChat();
            prompt_->SetFocus(true);
            break;

        case ClaudeEventKind::ProcessError:
            --activeWork_;
            SetBusy(false);
            AppendBlock(Quote("*" + e.text + "*"));
            SetStatus(e.text);
            SaveCurrentChat();
            break;
    }
}

// ============================================== reader threads -> UI thread

void UltraClaudeWindow::Post(std::function<void()> work) {
    std::lock_guard<std::mutex> lock(postedMutex_);
    posted_.push_back(std::move(work));
}

void UltraClaudeWindow::RunPosted() {
    std::vector<std::function<void()>> work;
    {
        std::lock_guard<std::mutex> lock(postedMutex_);
        work.swap(posted_);
    }
    for (auto& w : work) w();

    std::vector<std::pair<std::string, RepoLines>> measured;
    {
        std::lock_guard<std::mutex> lock(measure_->mutex);
        measured.swap(measure_->results);
    }
    for (const auto& [folder, lines] : measured) {
        --activeWork_;
        ApplyMeasurement(folder, lines);
    }

    // Idle: nothing is running that could post. The next action that
    // starts something starts the timer again.
    if (activeWork_ > 0 || timer_ == 0) return;
    {
        std::lock_guard<std::mutex> lock(postedMutex_);
        if (!posted_.empty()) return;
    }
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(timer_);
    timer_ = 0;
}

void UltraClaudeWindow::EnsureTimer() {
    if (timer_ != 0) return;
    if (auto* app = UltraCanvasApplicationBase::GetCurrent())
        timer_ = app->StartTimer(kTimerMs, /*periodic=*/true, [this](TimerId) { RunPosted(); });
}

void UltraClaudeWindow::JoinBackgroundThread() {
    if (background_.joinable()) background_.join();
}

} // namespace UltraClaude
