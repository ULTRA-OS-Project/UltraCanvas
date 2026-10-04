// Apps/EmailCleaner/ui/EmailCleanerAccountsDialog.h
// "Accounts…": every account EmailCleaner can analyse, in one list — the ones
// UltraMail shares and the ones added here — and the form that adds a new one
// without UltraMail.
//
// The dialog owns no data and never touches the network itself. Adding raises
// onAdd with the filled-in request; the app checks the sign-in against the
// server off the UI thread and calls back with "" (added: the dialog closes)
// or the reason it failed (shown in place, the form kept). Finding the servers
// for an address works the same way through onDiscover. Removing raises
// onRemove, and is only offered for EmailCleaner's own accounts: an UltraMail
// account is UltraMail's to remove.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first (X11 macro hygiene — see the engine headers).
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"

#include "EmailCleanerAccounts.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace EmailCleaner {

class AccountsDialog {
public:
    // One row of the list: the account, and a line saying where its mail
    // comes from ("from UltraMail", "EmailCleaner · imap.example.com").
    struct Row {
        StoredAccount account;
        std::string   detail;
    };

    void Show(const std::vector<Row>& rows);

    // Rebuild the list after the app added or removed an account, keeping the
    // dialog (and whatever the form holds) open.
    void SetRows(const std::vector<Row>& rows);

    // "Sign in and add". The app validates, checks the sign-in and saves; it
    // must call `done` on the UI thread — with "" when the account was added,
    // else with the reason, which the dialog shows.
    std::function<void(const NewAccountRequest& request,
                       std::function<void(const std::string& error)> done)> onAdd;

    // "Find servers": resolve the incoming server for an address (the
    // provider table, then autoconfig over the network). `done` on the UI
    // thread; found == false still carries a guess worth prefilling.
    std::function<void(const std::string& email,
                       std::function<void(const UltraMail::DiscoveryResult&)> done)> onDiscover;

    // "Remove" on an own account. The app asks for confirmation.
    std::function<void(const StoredAccount& account)> onRemove;

private:
    void RebuildList();
    void FindServers();
    void Prefill(const UltraMail::DiscoveryResult& result);
    void Submit();
    void SetStatus(const std::string& text, bool warning);
    bool ReadForm(NewAccountRequest& out, std::string& error) const;

    std::vector<Row> rows_;
    bool             busy_ = false;

    std::shared_ptr<UltraCanvas::UltraCanvasModalDialog> dialog_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer>   list_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>   email_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>   name_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>   password_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>   host_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>   port_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>    security_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>   username_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>      findButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>      addButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>       status_;

    // What the last discovery said, so the outgoing server (not on the form —
    // EmailCleaner only sends an unsubscribe mail with it) is kept.
    UltraMail::DiscoveryResult discovered_;
};

} // namespace EmailCleaner
