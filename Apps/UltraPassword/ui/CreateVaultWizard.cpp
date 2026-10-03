// Apps/UltraPassword/ui/CreateVaultWizard.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "CreateVaultWizard.h"

#include "Theme.h"

#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPasswordStrengthMeter.h"

#include <memory>

using namespace UltraCanvas;

namespace UltraPassword {

namespace {
constexpr float kLabelWidth = 120.0f;
} // namespace

void CreateVaultWizard::Show(UltraCanvasWindowBase* parent,
                             std::function<void(VaultDraft&)> onSubmit) {
    DialogConfig config;
    config.title      = "Create password vault";
    config.width      = 470;
    config.height     = 350;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    // Raw pointer for the button callbacks: the dialog owns its buttons.
    auto* dlg = dialog.get();

    dialog->layout.SetFlexColumn()
                  .SetFlexGap(Theme::kGap)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(20);
    dialog->SetBackgroundColor(Theme::kCardBackground);

    auto content = CreateContainer("cvwForm", 0, 0, 0, 0);
    content->layout.SetFlexColumn()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto intro = Theme::MakeLine("cvwIntro",
        "Your passwords are encrypted with a key made from the master password. "
        "Choose one you can remember - nobody can recover it for you.", 30,
        Theme::kSizeBody, Theme::kTextSecondary);
    intro->SetWrap(TextWrap::WrapWord);
    content->AddChild(intro);

    // [label + element] rows, as in UltraMail's wizard.
    auto addRow = [&content](const std::string& id, const std::string& labelText,
                             const std::shared_ptr<UltraCanvasUIElement>& element) {
        auto row = CreateContainer(id + "Row", 0, 0, 0, Theme::kControlHeight);
        row->layout.SetFlexRow()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto label = Theme::MakeLine(id + "Label", labelText, Theme::kControlHeight,
                                     Theme::kSizeBody, Theme::kTextSecondary);
        label->SetElementSize(Size2Df(kLabelWidth, Theme::kControlHeight));
        row->AddChild(label);
        row->AddChild(element);
        element->layoutItem.SetFlexGrow(1);
        content->AddChild(row);
        return row;
    };

    auto name = CreateTextInput("cvwName", 0, 0, 0, Theme::kControlHeight);
    name->SetText("My passwords");
    Theme::StyleInput(name);
    addRow("cvwName", "Vault name", name);

    auto password = CreatePasswordInput("cvwPass", 0, 0, 0, Theme::kControlHeight);
    password->SetPlaceholder("At least " + std::to_string(kMinMasterPasswordLength) + " characters");
    Theme::StyleInput(password);
    addRow("cvwPass", "Master password", password);

    auto confirm = CreatePasswordInput("cvwConfirm", 0, 0, 0, Theme::kControlHeight);
    confirm->SetPlaceholder("Type it again");
    Theme::StyleInput(confirm);
    addRow("cvwConfirm", "Confirm", confirm);

    auto meter = CreateBarStrengthMeter("cvwStrength", 0, 0, 0, 16);
    meter->LinkToInput(password.get());
    addRow("cvwStrength", "Strength", meter);

    auto profile = CreateDropdown("cvwProfile", 0, 0, 0, Theme::kControlHeight);
    profile->AddItem("Maximum - Argon2id 1 GiB (recommended)", "max");
    profile->AddItem("Strong - Argon2id 256 MiB (low-memory computers)", "strong");
    profile->SetSelectedIndex(0, false);
    Theme::StyleDropdown(profile);
    addRow("cvwProfile", "Key protection", profile);

    auto hint = Theme::MakeLine("cvwHint",
        "Encryption: XChaCha20-Poly1305 with a 256-bit key. A long passphrase of "
        "four or more unrelated words is strong and easy to remember.", 30,
        Theme::kSizeSmall, Theme::kTextMuted);
    hint->SetWrap(TextWrap::WrapWord);
    content->AddChild(hint);

    auto error = Theme::MakeLine("cvwError", "", 18, Theme::kSizeBody, Theme::kDanger);
    error->SetWrap(TextWrap::WrapWord);
    content->AddChild(error);

    dialog->AddChild(content);
    content->layoutItem.SetFlexGrow(1);

    // ===== Cancel / Create =====
    auto buttonRow = CreateContainer("cvwButtons", 0, 0, 0, Theme::kToolbarHeight);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(Theme::kInnerGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);

    auto cancelBtn = Theme::MakeButton("cvwCancel", "Cancel", 90);
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);

    auto createBtn = Theme::MakeButton("cvwCreate", "Create vault", 110, /*primary=*/true);
    // Raw pointers: the inputs and the label live in the dialog that owns
    // this button, so they outlive the callback.
    auto validate = [dlg, password = password.get(), confirm = confirm.get(),
                     error = error.get()]() {
        const std::string a = password->GetText();
        const std::string b = confirm->GetText();
        if (a.size() < kMinMasterPasswordLength) {
            error->SetText("The master password needs at least " +
                           std::to_string(kMinMasterPasswordLength) + " characters.");
            return;
        }
        if (a != b) {
            error->SetText("The two passwords are not the same.");
            return;
        }
        dlg->CloseDialog(DialogResult::OK);
    };
    createBtn->onClick = validate;
    buttonRow->AddChild(createBtn);
    dialog->AddChild(buttonRow);

    confirm->onEnterPressed = [validate](const std::string&) { validate(); return true; };

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [name, password, confirm, profile, onSubmit](DialogResult result) {
            VaultDraft draft;
            draft.password = password->GetText();
            // Clear the fields so the dialog's own copies go with it.
            password->SetText("");
            confirm->SetText("");
            if (result != DialogResult::OK) {
                WipeString(draft.password);
                return;
            }
            draft.name = name->GetText();
            if (draft.name.empty()) draft.name = "My passwords";
            draft.profile = profile->GetSelectedIndex() == 1 ? KdfProfile::Strong
                                                             : KdfProfile::Maximum;
            if (onSubmit) onSubmit(draft);
            WipeString(draft.password);
        },
        parent);
    dlg->SetFocusedElement(password.get());
}

} // namespace UltraPassword
