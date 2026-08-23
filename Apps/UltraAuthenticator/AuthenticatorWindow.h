// Apps/UltraAuthenticator/AuthenticatorWindow.h
// The main window: one card per account, each showing its current code and the
// seconds left before it rolls over.
//
// Cards are built from catalogue elements (UltraCanvasContainer,
// UltraCanvasLabel, UltraCanvasButton) rather than painted, per the framework
// rule in AGENTS.md. That is not bureaucracy here: the code is text a user will
// want to select and copy while typing it into another window, and a painted
// one could not be.
//
// Layout: the account list lives in a scrolling container, so the number of
// accounts is not bounded by the window height. The first version capped the
// list at eight and said so in the status line, which was honest but not much
// use to anyone with nine.
//
// Refresh model: a single 1 Hz periodic timer re-reads the codes for every
// visible row. One timer for the window rather than one per account keeps the
// rows in step — codes that roll over at visibly different moments look
// broken, even when each is individually correct.
//
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef AUTHENTICATORWINDOW_H
#define AUTHENTICATORWINDOW_H

#include "AccountStore.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasWindow.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace Authenticator {

class AuthenticatorWindow {
public:
    AuthenticatorWindow(UltraCanvasApplication& app, AccountStore& store);
    ~AuthenticatorWindow();

    AuthenticatorWindow(const AuthenticatorWindow&) = delete;
    AuthenticatorWindow& operator=(const AuthenticatorWindow&) = delete;

    bool Create();
    void Show();

private:
    // One account's card. Held so the timer can update the code and countdown
    // in place instead of rebuilding the window every second.
    struct Row {
        std::string                           key;
        Otp::Parameters                       params;
        bool                                  isHotp = false;
        std::shared_ptr<UltraCanvasContainer>  card;
        std::shared_ptr<UltraCanvasLabel>      issuerLabel;
        std::shared_ptr<UltraCanvasLabel>      accountLabel;
        std::shared_ptr<UltraCanvasLabel>      codeLabel;
        std::shared_ptr<UltraCanvasLabel>      countdownLabel;
        std::shared_ptr<UltraCanvasButton>     actionButton;   // Next (HOTP)
        std::shared_ptr<UltraCanvasButton>     editButton;
        std::shared_ptr<UltraCanvasButton>     revealButton;
        std::shared_ptr<UltraCanvasButton>     removeButton;
    };

    void RebuildRows();
    void ClearRows();
    void RefreshCodes();
    void OpenAddAccountDialog();
    void OpenScanAccountDialog();
    void OpenEditAccountDialog(const std::string& key);
    void OpenRevealSecretDialog(const std::string& key);
    void OpenChangePasswordDialog();
    void RemoveAccount(const std::string& key);
    void AdvanceHotpRow(const std::string& key);
    void SetStatus(const std::string& text, bool isError = false);

    UltraCanvasApplication& app_;
    AccountStore&           store_;

    std::shared_ptr<UltraCanvasWindow>    window_;
    std::shared_ptr<UltraCanvasContainer> listContainer_;
    std::shared_ptr<UltraCanvasLabel>     statusLabel_;
    std::shared_ptr<UltraCanvasLabel>     emptyLabel_;
    std::vector<Row>                      rows_;

    TimerId refreshTimer_ = 0;
    bool    timerRunning_ = false;

    static constexpr long kWindowWidth  = 720;
    static constexpr long kWindowHeight = 620;
    static constexpr long kHeaderHeight = 108;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // AUTHENTICATORWINDOW_H
