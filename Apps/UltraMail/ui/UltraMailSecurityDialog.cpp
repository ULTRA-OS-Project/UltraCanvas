// Apps/UltraMail/ui/UltraMailSecurityDialog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSecurityDialog.h"

#include "UltraMailTheme.h"

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSegmentedControl.h"

#include <memory>

using namespace UltraCanvas;

namespace UltraMail {

namespace {

constexpr float kLabelWidth = 110.0f;

std::shared_ptr<UltraCanvasContainer> Row(const std::string& id, const std::string& text,
                                          const std::shared_ptr<UltraCanvasContainer>& column) {
    auto row = CreateContainer(id, 0, 0, 0, Theme::kControlHeight);
    row->layout.SetFlexRow()
               .SetFlexGap(Theme::kInnerGap)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto label = Theme::MakeLine(id + "Lbl", text, Theme::kControlHeight, Theme::kSizeBody,
                                 Theme::kTextSecondary);
    label->SetElementSize(Size2Df(kLabelWidth, Theme::kControlHeight));
    row->AddChild(label);
    column->AddChild(row);
    row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    return row;
}

std::shared_ptr<UltraCanvasLabel> Note(const std::string& id, const std::string& text) {
    auto note = Theme::MakeLine(id, text, Theme::kControlHeight, Theme::kSizeBody, Theme::kTextSecondary);
    note->SetWrap(TextWrap::WrapWord);
    return note;
}

} // namespace

std::string SecurityDialog::Describe(const AccountSecurity& security, bool hasCertificate) {
    std::string what = "Off";
    if (!security.method.empty() && (security.sign || security.encrypt))
        what = security.method + ": " +
               (security.sign && security.encrypt ? "signed, encrypted" : security.encrypt ? "encrypted" : "signed");
    if (hasCertificate) what += " \xC2\xB7 S/MIME certificate";
    return what;
}

void SecurityDialog::Show(UltraCanvasWindowBase* parent, Options options) {
    const bool hasIntro = !options.intro.empty();
    DialogConfig config;
    config.title      = "Mail security";
    config.width      = 560;
    config.height     = 256 + (hasIntro ? 40 : 0);
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    auto* dlg = dialog.get();   // the dialog owns its children; no shared_ptr in their callbacks
    dialog->layout.SetFlexColumn()
                  .SetFlexGap(Theme::kGap)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(20);
    dialog->SetBackgroundColor(Theme::kCardBackground);

    auto content = CreateContainer("secForm", 0, 0, 0, 0);
    content->layout.SetFlexColumn()
                   .SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    content->AddChild(Theme::MakeDialogHeader(
        "secHeader", "lock", "Mail security",
        "How " + options.email + " signs and encrypts mail, and how its new messages start - "
        "each one can still be changed as it is written.",
        32.0f));
    if (hasIntro) {
        auto intro = Note("secIntro", options.intro);
        intro->SetElementSize(Size2Df(0, 36.0f));
        content->AddChild(intro);
        intro->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    }

    // What the method chooser offers is kept in step with the keys: S/MIME
    // once there is a certificate, OpenPGP once there is a secret key.
    struct State {
        bool hasCertificate = false;
        bool hasPgpKey = false;
        std::vector<std::string> methods;   // the segments after "Off"
        UltraCanvasSegmentedControl* method = nullptr;
        UltraCanvasCheckbox* sign = nullptr;
        UltraCanvasCheckbox* encrypt = nullptr;
    };
    auto state = std::make_shared<State>();
    state->hasCertificate = !options.smimeSummary.empty();
    state->hasPgpKey = !options.openPgpKeys.empty();
    if (options.smimeBuilt) state->methods.push_back("S/MIME");
    if (options.openPgpBuilt) state->methods.push_back("OpenPGP");
    auto sync = [state]() {
        if (!state->method) return;
        for (size_t i = 0; i < state->methods.size(); ++i) {
            const bool usable = state->methods[i] == "S/MIME" ? state->hasCertificate : state->hasPgpKey;
            state->method->SetSegmentEnabled(static_cast<int>(i) + 1, usable);
            if (!usable && state->method->GetSelectedIndex() == static_cast<int>(i) + 1)
                state->method->SetSelectedIndex(0);
        }
        const bool on = state->method->GetSelectedIndex() > 0;
        if (state->sign) state->sign->SetDisabled(!on);
        if (state->encrypt) state->encrypt->SetDisabled(!on);
    };

    // S/MIME: the certificate, Import... and Remove (at once).
    {
        auto row = Row("secSmime", "S/MIME", content);
        if (!options.smimeBuilt) {
            auto note = Note("secSmimeNone", "Not built into this UltraMail (its UltraCrypt has no OpenSSL 3)");
            row->AddChild(note);
            note->layoutItem.SetFlexGrow(1);
        } else {
            auto summary = Theme::MakeLine("secSmimeSummary",
                                           options.smimeSummary.empty() ? std::string("No certificate")
                                                                        : options.smimeSummary,
                                           Theme::kControlHeight, Theme::kSizeBody, Theme::kTextPrimary);
            summary->SetTooltip(options.smimeSummary.empty()
                ? std::string("Import your S/MIME certificate (a .p12 or .pfx file) to sign mail and to read "
                              "mail encrypted to you")
                : options.smimeSummary);
            row->AddChild(summary);
            summary->layoutItem.SetFlexGrow(1);
            UltraCanvasLabel* summaryLabel = summary.get();
            auto removeBtn = CreateButton("secSmimeRemove", 0, 0, 90, Theme::kControlHeight, "Remove");
            Theme::FitToLabel(removeBtn, 90);
            Theme::StyleSecondary(removeBtn);
            removeBtn->SetVisible(state->hasCertificate);
            UltraCanvasButton* removeRaw = removeBtn.get();
            auto shown = [state, summaryLabel, removeRaw, sync](const std::string& now) {
                summaryLabel->SetText(now.empty() ? std::string("No certificate") : now);
                summaryLabel->SetTooltip(now);
                removeRaw->SetVisible(!now.empty());
                state->hasCertificate = !now.empty();
                sync();
            };
            auto importBtn = CreateButton("secSmimeImport", 0, 0, 110, Theme::kControlHeight,
                                          "Import\xE2\x80\xA6");
            Theme::FitToLabel(importBtn, 110);
            Theme::StyleSecondary(importBtn);
            Theme::SetButtonIcon(importBtn, "lock");
            auto onImport = options.onImportSmime;
            importBtn->onClick = [dlg, onImport, shown]() {
                if (onImport) onImport(dlg, shown);
            };
            auto onRemove = options.onRemoveSmime;
            removeBtn->onClick = [onRemove, shown]() { shown(onRemove ? onRemove() : std::string()); };
            row->AddChild(importBtn);
            row->AddChild(removeBtn);
        }
    }

    // OpenPGP: the key to sign with.
    std::shared_ptr<UltraCanvasDropdown> keyBox;
    {
        auto row = Row("secPgp", "OpenPGP key", content);
        if (!options.openPgpBuilt) {
            auto note = Note("secPgpNone", "Not available: GnuPG is not installed, or this UltraMail is built "
                                           "without it");
            row->AddChild(note);
            note->layoutItem.SetFlexGrow(1);
        } else if (options.openPgpKeys.empty()) {
            auto note = Note("secPgpNoKey", "No secret key in your GnuPG keyring - make one with "
                                            "gpg --quick-generate-key " + options.email);
            row->AddChild(note);
            note->layoutItem.SetFlexGrow(1);
        } else {
            keyBox = CreateDropdown("secPgpKey", 0, 0, 0, Theme::kControlHeight);
            Theme::StyleDropdown(keyBox);
            keyBox->AddItem("The key for " + options.email, "");
            int selected = 0;
            for (size_t i = 0; i < options.openPgpKeys.size(); ++i) {
                keyBox->AddItem(options.openPgpKeys[i].second, options.openPgpKeys[i].first);
                if (options.openPgpKeys[i].first == options.current.openPgpKey) selected = static_cast<int>(i) + 1;
            }
            keyBox->SetSelectedIndex(selected, /*runNotifications=*/false);
            keyBox->SetTooltip("The secret key messages from this account are signed with; GnuPG asks for "
                               "its passphrase itself");
            row->AddChild(keyBox);
            keyBox->layoutItem.SetFlexGrow(1);
        }
    }

    // How new messages start.
    if (!state->methods.empty()) {
        auto row = Row("secDefault", "New messages", content);
        auto method = CreateSegmentedControl("secMethod", 0, 0, 90.0f * (state->methods.size() + 1),
                                             Theme::kControlHeight);
        Theme::StyleSegmented(method);
        method->AddSegment("Off");
        int chosen = 0;
        for (size_t i = 0; i < state->methods.size(); ++i) {
            method->AddSegment(state->methods[i]);
            if (state->methods[i] == options.current.method) chosen = static_cast<int>(i) + 1;
        }
        method->SetSelectedIndex(chosen);
        method->SetTooltip("How a new message, reply or forward from this account starts out");
        row->AddChild(method);
        state->method = method.get();
        auto box = [&row](const std::string& id, const std::string& text, bool checked, const std::string& tip) {
            auto b = std::make_shared<UltraCanvasCheckbox>(id, 0, 0, 90.0f, Theme::kControlHeight, text);
            b->SetFontSize(Theme::kSizeBody);
            b->SetChecked(checked);
            b->SetTooltip(tip);
            row->AddChild(b);
            return b;
        };
        auto signBox = box("secSign", "Sign", options.current.sign,
                           "Signed: the recipient can check that it is from you and unchanged");
        auto encryptBox = box("secEncrypt", "Encrypt", options.current.encrypt,
                              "Encrypted: only the recipients (and you) can read it; every recipient needs a "
                              "certificate or key, and a message to one without is not sent until it is "
                              "changed");
        state->sign = signBox.get();
        state->encrypt = encryptBox.get();
        method->onSegmentSelected = [sync](int) { sync(); };
        sync();
    } else {
        auto row = Row("secDefault", "New messages", content);
        auto note = Note("secDefaultNone", "Neither S/MIME nor OpenPGP is available in this UltraMail");
        row->AddChild(note);
        note->layoutItem.SetFlexGrow(1);
    }

    dialog->AddChild(content);
    content->layoutItem.SetFlexGrow(1);

    auto buttonRow = CreateContainer("secButtons", 0, 0, 0, Theme::kToolbarHeight);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(Theme::kInnerGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);
    auto cancelBtn = CreateButton("secCancel", 0, 0, 90, Theme::kControlHeight, options.cancelLabel);
    Theme::FitToLabel(cancelBtn, 90);
    Theme::StyleSecondary(cancelBtn);
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);
    auto saveBtn = CreateButton("secSave", 0, 0, 100, Theme::kControlHeight, "Save");
    Theme::FitToLabel(saveBtn, 100);
    Theme::StylePrimary(saveBtn);
    Theme::SetButtonIcon(saveBtn, "check");
    saveBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(saveBtn);
    dialog->AddChild(buttonRow);

    auto onSave = options.onSave;
    auto onClosed = options.onClosed;
    AccountSecurity start = options.current;
    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [state, keyBox, onSave, onClosed, start](DialogResult result) {
            struct AtEnd {
                std::function<void()> run;
                ~AtEnd() { if (run) run(); }
            } atEnd{ onClosed };
            if (result != DialogResult::OK || !onSave) return;
            AccountSecurity chosen = start;
            if (state->method) {
                const int index = state->method->GetSelectedIndex();
                chosen.method = index > 0 ? state->methods[static_cast<size_t>(index) - 1] : std::string();
                if (state->sign) chosen.sign = state->sign->IsChecked();
                if (state->encrypt) chosen.encrypt = state->encrypt->IsChecked();
                if (!chosen.method.empty() && !chosen.sign && !chosen.encrypt) chosen.method.clear();
            }
            if (keyBox) {
                const DropdownItem* item = keyBox->GetSelectedItem();
                chosen.openPgpKey = item ? item->value : std::string();
            }
            onSave(chosen);
        },
        parent);
}

} // namespace UltraMail
