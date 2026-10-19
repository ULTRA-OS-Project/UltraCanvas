// Apps/UltraMail/ui/UltraMailAccountWizard.cpp
// Version: 0.5.1 - Yahoo asks for an app password first (browser sign-in optional)
// Version: 0.5.0 - "How to set up an iCloud mail account" with a Show info
//                  button: the guide opens in an info area under the sign-in
//                  fields (ICloudSetupGuide), for iCloud addresses and
//                  addresses at domains no preset knows (iCloud+ own domains)
// Version: 0.4.1 - live hint per address: browser sign-in for Gmail / Outlook,
//                  an app password for Yahoo / iCloud (or a typed password
//                  at an OAuth2 provider)
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailAccountWizard.h"

#include "UltraCanvasModalDialog.h"
#include "UltraMailAlerts.h"
#include "UltraMailTheme.h"
#include "UltraMailDiscovery.h"
#include "UltraMailOAuth.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasButton.h"

#include <memory>

using namespace UltraCanvas;

namespace UltraMail {

namespace {
constexpr float kLabelWidth = 90.0f;
constexpr int   kWidth      = 420;
constexpr int   kHeight     = 300;   // the form with the guide closed
// The guide's info area: tall enough for most of it at once, and it scrolls.
constexpr int   kGuideHeight = 320;
} // namespace

void AccountWizard::Show(UltraCanvasWindowBase* parent,
                         std::function<void(const AccountDraft&)> onSubmit) {
    DialogConfig config;
    config.title      = "Add email account";
    config.width      = kWidth;
    config.height     = kHeight;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;  // Custom dialog builds its own.

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    // Raw pointer for button callbacks: the dialog outlives its buttons and owns
    // them, so capturing the shared_ptr would form a reference cycle.
    auto* dlg = dialog.get();

    // A Custom dialog builds its own window layout + children (see the framework's
    // Texter dialogs). Vertical stack: content grows, button row sits at the bottom.
    dialog->layout.SetFlexColumn()
                  .SetFlexGap(Theme::kGap)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(20);
    dialog->SetBackgroundColor(Theme::kCardBackground);

    // ===== CONTENT: intro + labelled input rows =====
    auto content = CreateContainer("wizardForm", 0, 0, 0, 0);
    content->layout.SetFlexColumn()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto intro = Theme::MakeLine("wizIntro",
        "Enter your address and password — UltraMail finds the rest.", 28,
        Theme::kSizeBody, Theme::kTextSecondary);
    intro->SetWrap(TextWrap::WrapWord);
    content->AddChild(intro);
    intro->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // Build a [label + input] flex row and append it to the content column.
    // Returns the row container so callers can hide/show a whole field at once.
    auto addRow = [&content](const std::string& id, const std::string& labelText,
                             const std::shared_ptr<UltraCanvasTextInput>& input) {
        auto row = CreateContainer(id + "Row", 0, 0, 0, Theme::kControlHeight);
        row->layout.SetFlexRow()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);

        auto label = Theme::MakeLine(id + "Label", labelText, Theme::kControlHeight,
                                     Theme::kSizeBody, Theme::kTextSecondary);
        label->SetElementSize(Size2Df(kLabelWidth, Theme::kControlHeight));
        row->AddChild(label);

        Theme::StyleInput(input);
        row->AddChild(input);
        input->layoutItem.SetFlexGrow(1);

        content->AddChild(row);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        return row;
    };

    auto name = CreateTextInput("wizName", 0, 0, 0, Theme::kControlHeight);
    name->SetPlaceholder("Erika Example");
    addRow("wizName", "Your name", name);

    auto email = CreateEmailInput("wizEmail", 0, 0, 0, Theme::kControlHeight);
    email->SetPlaceholder("erika@example.com");
    addRow("wizEmail", "Email address", email);

    auto password = CreatePasswordInput("wizPass", 0, 0, 0, Theme::kControlHeight);
    password->SetPlaceholder("Your password");
    auto passRow = addRow("wizPass", "Password", password);

    // Provider-specific advice that follows the address as it is typed: Gmail
    // and Outlook sign in through the browser (OAuth2) when the password is
    // left empty; they, Yahoo and iCloud need an app password otherwise.
    auto hint = Theme::MakeLine("wizHint", "", 30, Theme::kSizeBody, Theme::kTextSecondary);
    hint->SetWrap(TextWrap::WrapWord);
    content->AddChild(hint);
    hint->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // "How to set up an iCloud mail account" [Show info]: the guide opens in
    // an info area under the sign-in fields, and the dialog grows to hold it.
    // Apple takes only an app-specific password, made on account.apple.com -
    // without the steps, the first sign-in fails and says little about why.
    auto guideRow = CreateContainer("wizGuideRow", 0, 0, 0, Theme::kControlHeight);
    guideRow->layout.SetFlexRow()
                    .SetFlexGap(Theme::kInnerGap)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto guideLabel = Theme::MakeLine("wizGuideLabel", "How to set up an iCloud mail account",
                                      Theme::kControlHeight, Theme::kSizeBody,
                                      Theme::kTextPrimary);
    guideRow->AddChild(guideLabel);
    guideLabel->layoutItem.SetFlexGrow(1);
    auto guideBtn = CreateButton("wizGuideBtn", 0, 0, 90, Theme::kControlHeight, "Show info");
    Theme::FitToLabel(guideBtn, 90);
    Theme::StyleSecondary(guideBtn);
    guideRow->AddChild(guideBtn);
    content->AddChild(guideRow);
    guideRow->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto guide = std::make_shared<UltraCanvasTextArea>("wizGuide");
    guide->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
    guide->SetReadOnly(true);
    guide->SetWordWrap(true);
    guide->SetShowLineNumbers(false);
    guide->SetHighlightCurrentLine(false);
    guide->SetBackgroundColor(Theme::kSidebar);
    guide->SetBorders(1.0f, Theme::kCardBorder, Theme::kControlRadius);
    guide->SetFontSize(Theme::kSizeBody);
    guide->SetTextColor(Theme::kTextPrimary);
    guide->SetText(ICloudSetupGuide(), false);
    guide->SetElementSize(Size2Df(0, static_cast<float>(kGuideHeight)));
    guide->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    guide->SetVisible(false);
    content->AddChild(guide);

    // Raw pointers: the dialog owns the button and the area, and the button's
    // own handler must not hold a reference to it.
    auto* guideBtnRaw = guideBtn.get();
    auto* guideRaw    = guide.get();
    auto* guideRowRaw = guideRow.get();
    auto setGuideOpen = [dlg, guideBtnRaw, guideRaw](bool open) {
        if (guideRaw->IsVisible() == open) return;
        guideRaw->SetVisible(open);
        guideBtnRaw->SetText(open ? "Hide info" : "Show info");
        dlg->SetWindowSize(kWidth, open ? kHeight + kGuideHeight + static_cast<int>(Theme::kInnerGap)
                                        : kHeight);
    };
    guideBtn->onClick = [setGuideOpen, guideRaw]() { setGuideOpen(!guideRaw->IsVisible()); };

    email->onTextChanged = [hint, password, passRow, guideRowRaw, setGuideOpen](const std::string& text) {
        // The guide is offered for iCloud addresses and for domains no preset
        // knows (an own domain on iCloud+); typing a Gmail address closes it.
        const bool offerGuide = OffersICloudSetupGuide(text);
        guideRowRaw->SetVisible(offerGuide);
        if (!offerGuide) setGuideOpen(false);

        const DiscoveryResult d = AutoDiscovery::FromPresets(text);
        const std::string provider = OAuthProviderFor(d);
        // Providers that no longer take a typed password (Google, Microsoft) sign
        // in through the browser only: drop the password field entirely so nothing
        // stale is submitted, rather than asking for a password we cannot use.
        const bool acceptsPassword = ProviderAcceptsPassword(d);
        if (!acceptsPassword) password->SetText("");
        passRow->SetVisible(acceptsPassword);
        if (!provider.empty() && !acceptsPassword) {
            // Microsoft / Google: the browser sign-in is the only way in.
            const std::string name = OAuthProviderDisplayName(provider);
            hint->SetText(OAuthApps::Has(provider)
                ? d.displayName + " signs in with " + name + " in your browser — no "
                  "password needed. Continue to open the sign-in page."
                : d.displayName + " only accepts the " + name + " browser sign-in, which "
                  "needs an OAuth client configured (Docs/UltraMail/AccountSetup.md).");
        } else if (provider == "yahoo") {
            // Yahoo: the app password comes first, because the browser sign-in
            // only works once Yahoo has granted the app Mail API access.
            hint->SetText(d.displayName + ": enter an app password (Yahoo account > "
                          "Account security > Generate app password)."
                          + std::string(OAuthApps::Has(provider)
                              ? " Leave it empty to sign in with Yahoo in your browser instead."
                              : ""));
            password->SetPlaceholder("App password");
        } else if (!provider.empty() && OAuthApps::Has(provider)) {
            const std::string name = OAuthProviderDisplayName(provider);
            hint->SetText(d.displayName + ": leave the password empty to sign in with "
                          + name + " in your browser, or enter an app password.");
            password->SetPlaceholder("Leave empty to sign in with " + name);
        } else if (d.displayName == "iCloud") {
            hint->SetText("iCloud takes an app-specific password, not your Apple Account "
                          "password - Show info below says where to make one.");
            password->SetPlaceholder("App-specific password");
        } else if (ProviderNeedsAppPassword(d)) {
            hint->SetText(d.displayName + " rejects the normal password in mail programs: "
                          "enter an app password generated in your account's security "
                          "settings.");
            password->SetPlaceholder("App password");
        } else {
            hint->SetText("");
            password->SetPlaceholder("Your password");
        }
    };

    dialog->AddChild(content);
    content->layoutItem.SetFlexGrow(1);

    // ===== BUTTON ROW: Continue / Cancel =====
    auto buttonRow = CreateContainer("wizButtons", 0, 0, 0, Theme::kToolbarHeight);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(Theme::kInnerGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);

    auto cancelBtn = CreateButton("wizCancel", 0, 0, 90, Theme::kControlHeight, "Cancel");
    Theme::FitToLabel(cancelBtn, 90);
    Theme::StyleSecondary(cancelBtn);
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);

    auto continueBtn = CreateButton("wizContinue", 0, 0, 110, Theme::kControlHeight, "Continue");
    Theme::FitToLabel(continueBtn, 110);
    Theme::StylePrimary(continueBtn);
    continueBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(continueBtn);

    dialog->AddChild(buttonRow);

    // Enter in the password field confirms the dialog.
    password->onEnterPressed = [dlg](const std::string&) {
        dlg->CloseDialog(DialogResult::OK);
        return true;
    };

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [name, email, password, onSubmit, parent](DialogResult result) {
            if (result != DialogResult::OK) return;
            AccountDraft draft;
            draft.displayName = name->GetText();
            draft.email       = email->GetText();
            draft.password    = password->GetText();

            // Say why nothing happened rather than discarding what was typed.
            if (draft.email.empty()) {
                AlertWarning(parent, "No e-mail address was entered, so no "
                                     "account was added.",
                             "Enter the address of the mailbox you want to add, "
                             "for example you@example.com.");
                return;
            }
            if (!LooksLikeEmailAddress(draft.email)) {
                AlertWarning(parent, "\"" + draft.email + "\" is not a valid "
                             "e-mail address, so no account was added.",
                             "An address looks like you@example.com.");
                return;
            }
            if (onSubmit) onSubmit(draft);
        },
        parent);
}

} // namespace UltraMail
