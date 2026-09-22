// Apps/UltraSocial/ui/UltraSocialAccountWizard.cpp
// Version: 0.3.0 - the form is a UltraCanvasFormLayout grid: captions size
//                  themselves to the wording the chosen network uses, and a
//                  field the network has no use for leaves the form
// Last Modified: 2026-09-22
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialAccountWizard.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFormLayout.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasSpacer.h"
#include "UltraCanvasTextInput.h"

#include <array>
#include <memory>

using namespace UltraCanvas;

namespace UltraSocial {

namespace {

// Per-network form wording. The fields keep their positions; only labels,
// placeholders and the hint change with the dropdown.
//
// An EMPTY label means this network has no such field, and the row leaves the
// form: no network needs all four, and a row captioned "(not used)" over an
// input that does nothing is a question the user has to answer ("do I fill
// this in?") where there should be no question at all.
struct NetworkForm {
    SocialNetwork network;
    const char* dropdownText;
    const char* hint;
    const char* serverLabel;
    const char* serverPlaceholder;
    const char* identifierLabel;
    const char* identifierPlaceholder;
    const char* secretLabel;
    const char* secretPlaceholder;
    const char* clientIdLabel;
    const char* clientIdPlaceholder;
};

constexpr std::array<NetworkForm, 7> kForms{{
    { SocialNetwork::Mastodon, "Mastodon / Fediverse",
      "Enter your instance — UltraSocial registers itself there and signs "
      "you in through your browser; no keys needed. Advanced: paste an "
      "access token instead to skip the browser.",
      "Instance", "mastodon.social",
      "", "",
      "Access token", "optional — empty opens the browser",
      "", "" },
    { SocialNetwork::Bluesky, "Bluesky",
      "Create an app password under Settings → App Passwords on "
      "Bluesky, then sign in with it here — never your main password.",
      "Server (PDS)", "bsky.social (default)",
      "Handle / e-mail", "erika.bsky.social",
      "App password", "xxxx-xxxx-xxxx-xxxx",
      "", "" },
    { SocialNetwork::Telegram, "Telegram channel",
      "Create a bot with @BotFather, add it to your channel as an "
      "administrator, and paste its token here.",
      "Server", "(default)",
      "Channel", "@mychannel",
      "Bot token", "123456:ABC-DEF…",
      "", "" },
    { SocialNetwork::Reddit, "Reddit",
      "Register an 'installed app' at reddit.com/prefs/apps with redirect "
      "http://127.0.0.1:17995/callback, paste its client id, then sign in "
      "through your browser. Text posts go to the subreddit below (empty = "
      "your profile).",
      "Server", "(default)",
      "Subreddit", "optional, e.g. r/test",
      "", "",
      "Client ID", "from reddit.com/prefs/apps" },
    { SocialNetwork::X, "X (Twitter)",
      "Register an app in the X developer portal (OAuth 2.0 public client, "
      "redirect http://127.0.0.1:17996/callback), paste its client id, then "
      "sign in through your browser. Mind the free tier's monthly write cap.",
      "Server", "(default)",
      "", "",
      "", "",
      "Client ID", "from the X developer portal" },
    { SocialNetwork::LinkedIn, "LinkedIn",
      "Register an app at linkedin.com/developers with the 'Sign In with "
      "LinkedIn' and 'Share on LinkedIn' products and redirect "
      "http://127.0.0.1:17997/callback, paste its client id and secret, "
      "then sign in through your browser.",
      "Server", "(default)",
      "", "",
      "Client secret", "from your LinkedIn app",
      "Client ID", "from your LinkedIn app" },
    { SocialNetwork::Facebook, "Facebook Page",
      "Posting goes to a Page you manage (personal profiles have no "
      "posting API). Create a Meta app, grant it pages_manage_posts for "
      "your Page, and paste the Page id and a long-lived Page access "
      "token.",
      "Server", "(default)",
      "Page ID", "e.g. 103245...",
      "Page access token", "EAAB…",
      "", "" },
}};

} // namespace

void AccountWizard::Show(UltraCanvasWindowBase* parent,
                         std::function<void(const WizardInput&)> onSubmit) {
    DialogConfig config;
    config.title      = "Add social account";
    config.width      = 500;
    config.height     = 430;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;  // Custom dialog builds its own.

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    // Raw pointer for button callbacks: the dialog outlives its buttons and
    // owns them, so capturing the shared_ptr would form a reference cycle.
    auto* dlg = dialog.get();

    dialog->layout.SetFlexColumn()
                  .SetFlexGap(12)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(16);

    // One two-column grid rather than a flex container per field. The
    // captions here are rewritten every time the network changes - "Access
    // token" becomes "Page access token" becomes "(not used)" - so a caption
    // column that measures its own text is not a nicety: a fixed 120 px would
    // have to be wide enough for the longest wording of all seven networks,
    // and would cut off the first one that grows past it. The grid is also
    // flex-shrink: 0 and never scrolls, so a field can no longer be squeezed
    // below the input inside it and raise a scrollbar across its own caption.
    auto content = CreateFormGrid("swForm", 8.0f, 8.0f);

    // A [caption | input] row. The input gets no width of its own: the `1fr`
    // column hands it whatever is left after the caption column.
    struct Row {
        std::shared_ptr<UltraCanvasLabel> label;
        std::shared_ptr<UltraCanvasTextInput> input;
    };
    auto addRow = [&content](const std::string& id, const std::string& labelText,
                             const std::shared_ptr<UltraCanvasTextInput>& input) {
        auto label = CreateFormCaption(id + "Label", labelText);
        AddFormRow(content, label, input);
        return Row{label, input};
    };

    // Network picker row.
    auto network = CreateDropdown("swNetwork", 0, 0, 0, 28);
    for (const auto& form : kForms) network->AddItem(form.dropdownText);
    AddFormRow(content, CreateFormCaption("swNetworkLabel", "Network"), network);

    // The hint is its own caption, so it spans both columns.
    auto hint = CreateLabel("swHint", 0, 0, 0, 78, kForms[0].hint);
    hint->SetWrap(TextWrap::WrapWord);
    AddFormWideRow(content, hint);

    auto server = CreateTextInput("swServer", 0, 0, 0, 28);
    Row serverRow = addRow("swServer", kForms[0].serverLabel, server);

    auto identifier = CreateTextInput("swIdentifier", 0, 0, 0, 28);
    Row identifierRow = addRow("swIdentifier", kForms[0].identifierLabel, identifier);

    auto secret = CreatePasswordInput("swSecret", 0, 0, 0, 28);
    Row secretRow = addRow("swSecret", kForms[0].secretLabel, secret);

    auto clientId = CreateTextInput("swClientId", 0, 0, 0, 28);
    Row clientIdRow = addRow("swClientId", kForms[0].clientIdLabel, clientId);

    // A row this network does not use (empty caption) leaves the grid
    // entirely - hiding both cells is display:none, so no gap is left behind -
    // and its input is emptied on the way out. That last part is not tidiness:
    // a Bluesky app password typed before switching to Reddit would otherwise
    // still be in the box at submit time, now invisible, and would be sent as
    // Reddit's secret.
    auto applyRow = [](const Row& row, const char* caption, const char* placeholder) {
        const bool used = caption && *caption;
        row.label->SetVisible(used);
        row.input->SetVisible(used);
        if (!used) {
            row.input->SetText("");
            return;
        }
        row.label->SetText(caption);
        row.input->SetPlaceholder(placeholder);
    };

    auto applyForm = [hint, applyRow, serverRow, identifierRow, secretRow,
                      clientIdRow](int index) {
        const auto& form = kForms[static_cast<std::size_t>(index)];
        hint->SetText(form.hint);
        applyRow(serverRow,     form.serverLabel,     form.serverPlaceholder);
        applyRow(identifierRow, form.identifierLabel, form.identifierPlaceholder);
        applyRow(secretRow,     form.secretLabel,     form.secretPlaceholder);
        applyRow(clientIdRow,   form.clientIdLabel,   form.clientIdPlaceholder);
    };
    applyForm(0);
    network->SetSelectedIndex(0, /*runNotifications=*/false);
    network->onSelectionChanged = [applyForm](int index, const DropdownItem&) {
        if (index >= 0 && index < static_cast<int>(kForms.size())) applyForm(index);
    };

    dialog->AddChild(content);
    // The grid keeps its rows at their own height; the spacer takes the slack
    // so the buttons stay at the bottom of the dialog.
    dialog->AddChild(std::make_shared<UltraCanvasSpacer>(0, 0, 1.0f));

    // ===== BUTTON ROW: Connect / Cancel =====
    auto buttonRow = CreateContainer("swButtons", 0, 0, 0, 36);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(10)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);

    auto connectBtn = CreateButton("swConnect", 0, 0, 100, 28, "Connect");
    connectBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(connectBtn);

    auto cancelBtn = CreateButton("swCancel", 0, 0, 80, 28, "Cancel");
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);

    dialog->AddChild(buttonRow);

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [network, server, identifier, secret, clientId,
         onSubmit](DialogResult result) {
            if (result != DialogResult::OK || !onSubmit) return;
            int index = network->GetSelectedIndex();
            if (index < 0 || index >= static_cast<int>(kForms.size())) index = 0;
            WizardInput input;
            input.network    = kForms[static_cast<std::size_t>(index)].network;
            input.server     = server->GetText();
            input.identifier = identifier->GetText();
            input.secret     = secret->GetText();
            input.clientId   = clientId->GetText();
            onSubmit(input);
        },
        parent);
}

} // namespace UltraSocial
