// Apps/UltraFiler/UltraFilerRunWindowsDialog.cpp
// Implementation of the environment picker. Construction follows the
// CSV dialogs (flex-column content, custom OK/Cancel buttons).
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraFilerRunWindowsDialog.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

namespace {
constexpr int kNewEnvironmentIndex = 0;  // dropdown item 0 = "new"
}

void UltraFilerRunWindowsDialog::Initialize(
    const std::string& programName,
    const std::vector<std::string>& environments,
    const std::string& suggestion) {
    environments_ = environments;

    DialogConfig cfg;
    cfg.title = "Run Windows program";
    cfg.width = 460;
    cfg.height = 250;
    cfg.resizable = false;
    cfg.buttons = DialogButtons::NoButtons;
    CreateDialog(cfg);

    layout.SetFlexColumn();
    layout.SetFlexGap(10);
    SetPadding(16);

    auto content = std::make_shared<UltraCanvasContainer>(
        "uf-runwin-content", 0, 0, 428, 150);
    content->layout.SetFlexColumn();
    content->layout.SetFlexGap(8);

    auto prompt = std::make_shared<UltraCanvasLabel>(
        "uf-runwin-prompt", 0, 0, 428.0f, 20,
        "Run " + programName + " in which Windows environment?");
    content->AddChild(prompt);

    // Which environment: "new" plus every existing one. The suggestion
    // pre-selects an existing environment when it names one.
    envDropdown_ = std::make_shared<UltraCanvasDropdown>(
        "uf-runwin-env", 0, 0, 428.0f, 30.0f);
    envDropdown_->AddItem("New environment:");
    int preselect = kNewEnvironmentIndex;
    for (size_t i = 0; i < environments_.size(); ++i) {
        envDropdown_->AddItem(environments_[i]);
        if (environments_[i] == suggestion)
            preselect = static_cast<int>(i) + 1;
    }
    envDropdown_->SetSelectedIndex(preselect, false);
    content->AddChild(envDropdown_);

    newNameInput_ = std::make_shared<UltraCanvasTextInput>(
        "uf-runwin-newname", 0, 0, 428.0f, 30.0f);
    newNameInput_->SetPlaceholder("name for the new environment");
    if (preselect == kNewEnvironmentIndex) newNameInput_->SetText(suggestion);
    content->AddChild(newNameInput_);

    rememberCheck_ = std::make_shared<UltraCanvasCheckbox>(
        "uf-runwin-remember", 0, 0, 428.0f, 24.0f,
        "Remember for this program");
    rememberCheck_->SetChecked(true);
    content->AddChild(rememberCheck_);

    AddChild(content);

    // Buttons row (custom, so Run can validate before closing).
    auto buttons = std::make_shared<UltraCanvasContainer>(
        "uf-runwin-buttons", 0, 0, 428, 34);
    buttons->layout.SetFlexRow();
    buttons->layout.SetFlexGap(8);
    auto runBtn = std::make_shared<UltraCanvasButton>(
        "uf-runwin-run", 0, 0, 100, 30);
    runBtn->SetText("Run");
    runBtn->onClick = [this]() {
        const std::string env = ChosenEnvironment();
        if (env.empty()) return;  // nothing chosen/typed — keep the dialog
        if (onAccept) onAccept(env, rememberCheck_->IsChecked());
        CloseDialog(DialogResult::OK);
    };
    auto cancelBtn = std::make_shared<UltraCanvasButton>(
        "uf-runwin-cancel", 0, 0, 100, 30);
    cancelBtn->SetText("Cancel");
    cancelBtn->onClick = [this]() { CloseDialog(DialogResult::Cancel); };
    buttons->AddChild(runBtn);
    buttons->AddChild(cancelBtn);
    AddChild(buttons);
}

std::string UltraFilerRunWindowsDialog::ChosenEnvironment() const {
    int index = envDropdown_ ? envDropdown_->GetSelectedIndex() : -1;
    if (index > kNewEnvironmentIndex &&
        index <= static_cast<int>(environments_.size()))
        return environments_[static_cast<size_t>(index - 1)];
    return newNameInput_ ? newNameInput_->GetText() : std::string();
}

std::shared_ptr<UltraFilerRunWindowsDialog> ShowRunWindowsDialog(
    const std::string& programName,
    const std::vector<std::string>& environments,
    const std::string& suggestion,
    std::function<void(const std::string&, bool)> onAccept,
    UltraCanvasWindowBase* parent) {
    auto dialog = std::make_shared<UltraFilerRunWindowsDialog>();
    dialog->Initialize(programName, environments, suggestion);
    dialog->onAccept = std::move(onAccept);
    dialog->ShowModal(parent);
    return dialog;
}

}  // namespace UltraCanvas
