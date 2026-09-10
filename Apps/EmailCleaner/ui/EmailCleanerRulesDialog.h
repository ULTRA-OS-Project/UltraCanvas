// Apps/EmailCleaner/ui/EmailCleanerRulesDialog.h
// Editing the keyword rules without leaving the app.
//
// Rules have always been data — a plain-text file layered over the built-in
// table — but editing them meant finding rules.txt in the data directory and
// opening it in something else. This is that file, in a dialog: the user's own
// rules listed, added, edited and removed, with the built-ins visible but not
// editable (they are the floor the user's rules are layered on, and a rule set
// the user can break is one they can also silently disarm).
//
// Saving writes the file and hands it back to the caller, which re-reads the
// rules and re-classifies the corpus — the same path the "Re-analyse" button
// already takes, so a rule change is visible immediately rather than at the
// next scan.
// Version: 0.3.0 (Phase 3)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first (X11 macro hygiene — see the engine headers).
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"

#include "EmailCleanerRules.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace EmailCleaner {

class RulesDialog {
public:
    // `userRulesPath` is the editable file; `builtInCount` is shown so the user
    // knows what their rules sit on top of.
    void Show(const std::string& userRulesPath, std::size_t builtInCount);

    // Raised after the file was written, so the app can re-read and re-analyse.
    std::function<void()> onSaved;

    // Pre-fill the "add" row's phrase — the map selection's strongest term,
    // when the caller has one.
    void SetSuggestedPhrase(const std::string& phrase) { suggested_ = phrase; }

private:
    void RebuildList();
    bool AddFromForm(std::string& outError);
    void Save();

    std::string              path_;
    std::string              suggested_;
    std::size_t              builtInCount_ = 0;
    std::vector<KeywordRule> rules_;        // the user's, not the built-ins
    std::vector<std::string> loadErrors_;

    std::shared_ptr<UltraCanvas::UltraCanvasModalDialog> dialog_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer>   list_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>       status_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>    category_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>    field_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>   weight_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>   phrase_;
};

} // namespace EmailCleaner
