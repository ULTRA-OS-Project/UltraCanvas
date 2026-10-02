// Apps/UltraClaude/ui/UltraClaudeWindow.h
// The UltraClaude window. Two views, one shown at a time:
//
// Sign-in page (what the app opens on), laid out like UltraMail's start page:
//
//                         [ logo ]
//                        UltraClaude
//         Chat with Claude on your Pro or Max subscription
//                  [ email (optional)      ]
//                  [        Log in         ]
//                  [    Create account     ]
//                       status line
//
//   Log in runs `claude auth login --claudeai [--email <address>]`: the
//   Claude Code CLI opens Anthropic's sign-in page in the browser, where the
//   password (or Google / SSO) is entered, and stores the session for
//   itself. UltraClaude never asks for or sees a Claude password or token -
//   collecting one in our own form would breach Anthropic's terms, and the
//   subscription login only works through Anthropic's page anyway. The email
//   field only pre-fills that page. Create account opens claude.ai.
//   When the CLI is already signed in on this computer, the page says so and
//   Log in becomes Continue.
//
// Chat view:
//
//   | Model [Default v]  Permissions [Ask v]  Folder [....] [...]  [New chat] [Log out] |
//   | transcript (markdown, read-only, follows the reply)                                |
//   | [ Message Claude...                                               ] [Send]         |
//   | status line                                                                        |
//
//   Each prompt runs the CLI through ClaudeChatSession. Its events arrive on
//   the process's reader thread; they are queued and applied on the UI thread
//   by a timer that runs only while something is pending.
//
// Version: 0.1.0
// Last Modified: 2026-10-02
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "ClaudeChatSession.h"

#include "UltraCanvasTimer.h"
#include "UltraCanvasWindow.h"   // UltraCanvasWindow is a per-platform typedef

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace UltraCanvas {
    class UltraCanvasButton;
    class UltraCanvasContainer;
    class UltraCanvasDropdown;
    class UltraCanvasLabel;
    class UltraCanvasTextArea;
    class UltraCanvasTextInput;
}

namespace UltraClaude {

class UltraClaudeWindow {
public:
    // executable: the claude program to run (a name on PATH or a full path).
    UltraClaudeWindow(std::string version, std::string executable);
    ~UltraClaudeWindow();

    bool Create();
    void Show();

    // The window was closed.
    std::function<void()> onClosed;

private:
    // ----- views -----
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildSignInPage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildChatView();
    void ShowSignInPage();
    void ShowChatView();

    // ----- sign-in -----
    void LogInOrContinue();
    void CancelLogIn();
    void CreateAccount();
    void LogOut();
    // Runs `claude auth status` in the background; ApplyAuthStatus follows.
    void CheckAuthStatus();
    void ApplyAuthStatus(bool cliFound, bool loggedIn, const std::string& detail);
    void SetSignInStatus(const std::string& text);

    // ----- chat -----
    void SendCurrentPrompt();
    void StopAnswer();
    void NewChat();
    void BrowseFolder();
    void SetBusy(bool busy);
    void SetStatus(const std::string& text);
    void AppendTranscript(const std::string& markdown);
    // Appends a markdown block (a quote) as a paragraph of its own.
    void AppendBlock(const std::string& markdown);
    void BeginClaudeSection();
    ClaudeChatOptions CurrentOptions() const;
    void ApplyEvent(const ClaudeStreamEvent& event);

    // ----- reader threads -> UI thread -----
    // Work posted from another thread, run on the UI thread by the timer.
    void Post(std::function<void()> work);
    void RunPosted();
    void EnsureTimer();
    void JoinBackgroundThread();

    std::string version_;
    std::string executable_;
    ClaudeChatSession session_;
    ClaudeCliProcess loginProcess_;   // `claude auth login`, while it runs

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;

    // sign-in page
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> signInPage_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> email_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> logIn_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> createAccount_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> signInStatus_;
    bool loggedIn_ = false;
    bool loggingIn_ = false;
    bool demoSent_ = false;   // ULTRACLAUDE_DEMO_PROMPT, once

    // chat view
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> chatView_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> model_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> permissions_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> folder_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> transcript_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> prompt_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> send_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> status_;
    bool busy_ = false;
    bool claudeSectionOpen_ = false;   // "**Claude**" written for this turn
    size_t toolCalls_ = 0;             // tool calls in this turn
    size_t trailingBreaks_ = 0;        // line breaks the transcript ends with

    std::mutex postedMutex_;
    std::vector<std::function<void()>> posted_;
    UltraCanvas::TimerId timer_ = 0;
    // Things started that will still post (a prompt, a sign-in, the probe):
    // the timer runs while this is above zero. UI thread only.
    int activeWork_ = 0;
    std::thread background_;           // the auth-status probe
};

} // namespace UltraClaude
