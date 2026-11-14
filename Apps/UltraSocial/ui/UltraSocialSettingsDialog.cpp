// Apps/UltraSocial/ui/UltraSocialSettingsDialog.cpp
// Version: 0.1.0 - sign-in settings set as standard, editable here
// Last Modified: 2026-11-14
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialSettingsDialog.h"
#include "UltraSocialTheme.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasFormLayout.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasSpacer.h"
#include "UltraCanvasTextInput.h"

#include <memory>
#include <string>

using namespace UltraCanvas;

namespace UltraSocial {

void SettingsDialog::Show(UltraCanvasWindowBase* parent,
                          const SignInSettings& current,
                          std::function<void(const SignInSettings&)> onSave) {
    DialogConfig config;
    config.title      = "Settings";
    config.width      = 540;
    config.height     = 470;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    // Raw pointer for the button callbacks: the dialog owns its buttons.
    auto* dlg = dialog.get();

    dialog->layout.SetFlexColumn()
                  .SetFlexGap(12)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(16);

    dialog->AddChild(Theme::MakeWrapped(
        "stIntro",
        "Used for every account you add. They come set as standard - change "
        "them only to use a different server or your own app registration.",
        Theme::kSizeBody, Theme::kTextSecondary));

    auto form = CreateFormGrid("stForm", 8.0f, 8.0f);

    auto heading = [&form](const std::string& id, const std::string& text) {
        AddFormWideRow(form, Theme::MakeText(id, text, Theme::kSizeBody,
                                             Theme::kTextPrimary, FontWeight::Bold));
    };
    auto field = [&form](const std::string& id, const std::string& caption,
                         const std::string& value, const std::string& placeholder,
                         bool secret) {
        auto input = secret ? CreatePasswordInput(id, 0, 0, 0, 28)
                            : CreateTextInput(id, 0, 0, 0, 28);
        Theme::StyleInput(input);
        input->SetText(value);
        input->SetPlaceholder(placeholder);
        auto label = CreateFormCaption(id + "Label", caption);
        label->SetFontSize(Theme::kSizeBody);
        label->SetTextColor(Theme::kTextSecondary);
        AddFormRow(form, label, input);
        return input;
    };

    heading("stServers", "Default servers");
    auto mastodon = field("stMastodon", "Mastodon instance", current.mastodonInstance,
                          "mastodon.social", false);
    auto bluesky  = field("stBluesky", "Bluesky server", current.blueskyServer,
                          "bsky.social", false);

    heading("stApps", "App registrations");
    auto reddit   = field("stReddit", "Reddit client ID", current.redditClientId,
                          "from reddit.com/prefs/apps", false);
    auto x        = field("stX", "X client ID", current.xClientId,
                          "from the X developer portal", false);
    auto linkedIn = field("stLinkedIn", "LinkedIn client ID", current.linkedInClientId,
                          "from linkedin.com/developers", false);
    auto linkedInSecret = field("stLinkedInSecret", "LinkedIn client secret",
                                current.linkedInClientSecret,
                                "from your LinkedIn app", true);
    dialog->AddChild(form);

    // Where the registrations come from, for anyone using their own.
    auto hint = Theme::MakeWrapped(
        "stHint",
        "Own app registration: use the redirect http://127.0.0.1:17995/callback "
        "for Reddit, :17996 for X and :17997 for LinkedIn. A field left empty "
        "goes back to its standard value.",
        Theme::kSizeSecondary, Theme::kTextSecondary);
    auto hintBox = CreateContainer("stHintBox", 0, 0, 0, 0);
    hintBox->SetBackgroundColor(Theme::kAccentSoft);
    hintBox->SetBorders(0.0f, Colors::Transparent, Theme::kControlRadius);
    hintBox->SetPadding(10, 12);
    hintBox->layout.SetFlexColumn()
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    hintBox->layoutItem.SetFlexShrink(0);
    Theme::NoScrollbars(hintBox);
    hintBox->AddChild(hint);
    dialog->AddChild(hintBox);

    dialog->AddChild(std::make_shared<UltraCanvasSpacer>(0, 0, 1.0f));

    // ===== BUTTON ROW: Reset to standard | Save / Cancel =====
    auto buttonRow = CreateContainer("stButtons", 0, 0, 0, 36);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(10)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);

    auto resetBtn = Theme::MakeButton("stReset", "Reset to standard", false, "", 0.0f, 28.0f);
    resetBtn->onClick = [mastodon, bluesky, reddit, x, linkedIn, linkedInSecret]() {
        const SignInSettings standard = DefaultSignInSettings();
        mastodon->SetText(standard.mastodonInstance);
        bluesky->SetText(standard.blueskyServer);
        reddit->SetText(standard.redditClientId);
        x->SetText(standard.xClientId);
        linkedIn->SetText(standard.linkedInClientId);
        linkedInSecret->SetText(standard.linkedInClientSecret);
    };
    buttonRow->AddChild(resetBtn);
    buttonRow->AddStretchSpacer(1);

    auto saveBtn = Theme::MakeButton("stSave", "Save", true, "", 80.0f, 28.0f);
    saveBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(saveBtn);

    auto cancelBtn = Theme::MakeButton("stCancel", "Cancel", false, "", 80.0f, 28.0f);
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);

    dialog->AddChild(buttonRow);

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [mastodon, bluesky, reddit, x, linkedIn, linkedInSecret,
         onSave](DialogResult result) {
            if (result != DialogResult::OK || !onSave) return;
            // A pasted id often carries a stray space or line break.
            auto text = [](const std::shared_ptr<UltraCanvasTextInput>& in) {
                std::string s = in->GetText();
                const auto first = s.find_first_not_of(" \t\r\n");
                if (first == std::string::npos) return std::string();
                const auto last = s.find_last_not_of(" \t\r\n");
                return s.substr(first, last - first + 1);
            };
            SignInSettings edited;
            edited.mastodonInstance     = text(mastodon);
            edited.blueskyServer        = text(bluesky);
            edited.redditClientId       = text(reddit);
            edited.xClientId            = text(x);
            edited.linkedInClientId     = text(linkedIn);
            edited.linkedInClientSecret = text(linkedInSecret);
            onSave(edited);
        },
        parent);
}

} // namespace UltraSocial
