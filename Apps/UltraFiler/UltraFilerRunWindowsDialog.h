// Apps/UltraFiler/UltraFilerRunWindowsDialog.h
// "Run in which Windows environment?" — the one-time picker shown when a
// Windows program outside every environment is launched for the first
// time. Offers the existing environments plus a new-environment name
// (pre-filled from UltraWin_SuggestEnvironment, so sibling programs of an
// already-associated app default to its environment), and a "remember"
// checkbox that stores the choice in the UltraWin association store —
// later launches skip the dialog entirely.
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"

#include <functional>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraFilerRunWindowsDialog : public UltraCanvasModalDialog {
public:
    UltraFilerRunWindowsDialog() = default;

    // programName: shown in the prompt. environments: the existing names.
    // suggestion: pre-selected when it names an existing environment,
    // otherwise pre-fills the new-environment field.
    void Initialize(const std::string& programName,
                    const std::vector<std::string>& environments,
                    const std::string& suggestion);

    // Invoked on Run with the chosen environment name and whether to
    // remember it for this program.
    std::function<void(const std::string& environment, bool remember)>
        onAccept;

private:
    // The environment the controls currently describe ("" = invalid).
    std::string ChosenEnvironment() const;

    std::shared_ptr<UltraCanvasDropdown>  envDropdown_;
    std::shared_ptr<UltraCanvasTextInput> newNameInput_;
    std::shared_ptr<UltraCanvasCheckbox>  rememberCheck_;
    std::vector<std::string> environments_;
};

// Build and show the picker; onAccept fires only on Run.
std::shared_ptr<UltraFilerRunWindowsDialog> ShowRunWindowsDialog(
    const std::string& programName,
    const std::vector<std::string>& environments,
    const std::string& suggestion,
    std::function<void(const std::string&, bool)> onAccept,
    UltraCanvasWindowBase* parent);

}  // namespace UltraCanvas
