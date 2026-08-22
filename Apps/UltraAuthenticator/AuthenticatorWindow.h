// Apps/UltraAuthenticator/AuthenticatorWindow.h
// The main window: one row per account, each showing its current code and the
// seconds left before it rolls over.
//
// Rows are built from catalogue elements (UltraCanvasLabel, UltraCanvasButton)
// rather than painted, per the framework rule in AGENTS.md. That is not
// bureaucracy here: the code label is text a user will want to select and
// copy, and a painted one could not be.
//
// Refresh model: a single 1 Hz periodic timer re-reads the codes for every
// visible row. One timer for the window rather than one per account keeps the
// rows in step — codes that roll over at visibly different moments look
// broken, even when each is individually correct.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef AUTHENTICATORWINDOW_H
#define AUTHENTICATORWINDOW_H

#include "AccountStore.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
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
    // One account's widgets. Held so the timer can update the code and
    // countdown in place instead of rebuilding the window every second.
    struct Row {
        std::string                        key;
        bool                               isHotp = false;
        std::shared_ptr<UltraCanvasLabel>  nameLabel;
        std::shared_ptr<UltraCanvasLabel>  codeLabel;
        std::shared_ptr<UltraCanvasLabel>  countdownLabel;
        std::shared_ptr<UltraCanvasButton> actionButton;   // Next (HOTP only)
        std::shared_ptr<UltraCanvasButton> removeButton;
    };

    void RebuildRows();
    void ClearRows();
    void RefreshCodes();
    void OpenAddAccountDialog();
    void RemoveAccount(const std::string& key);
    void AdvanceHotpRow(const std::string& key);
    void SetStatus(const std::string& text);

    UltraCanvasApplication& app_;
    AccountStore&           store_;

    std::shared_ptr<UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvasLabel>  statusLabel_;
    std::shared_ptr<UltraCanvasLabel>  emptyLabel_;
    std::vector<Row>                   rows_;

    TimerId refreshTimer_ = 0;
    bool    timerRunning_ = false;

    static constexpr long kWindowWidth  = 560;
    static constexpr long kWindowHeight = 520;
    static constexpr long kMargin       = 20;
    static constexpr long kHeaderHeight = 96;
    static constexpr long kRowHeight    = 44;
    // Beyond this many rows the fixed-height window would overflow. The list
    // needs a scrolling container before the cap can be lifted; until then the
    // limit is stated rather than silently truncating.
    static constexpr size_t kMaxVisibleRows = 8;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // AUTHENTICATORWINDOW_H
