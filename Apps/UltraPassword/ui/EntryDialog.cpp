// Apps/UltraPassword/ui/EntryDialog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "EntryDialog.h"

#include "Theme.h"
#include "core/PasswordGenerator.h"

#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPasswordStrengthMeter.h"
#include "UltraCanvasTextArea.h"

#include <functional>
#include <memory>
#include <vector>

using namespace UltraCanvas;

namespace UltraPassword {

namespace {
constexpr float kLabelWidth = 100.0f;

// Every group as "Work / Cloud", in tree order, with the top level first.
std::vector<std::pair<std::string, std::string>> GroupChoices(const PasswordVault& vault) {
    std::vector<std::pair<std::string, std::string>> out = {
        {kRootGroupId, vault.GetName().empty() ? "(top level)" : vault.GetName() + " (top level)"}};
    std::function<void(const std::string&)> walk = [&](const std::string& parent) {
        for (const PasswordGroup* g : vault.ChildGroups(parent)) {
            out.emplace_back(g->id, vault.GroupPath(g->id));
            walk(g->id);
        }
    };
    walk(kRootGroupId);
    return out;
}

// "https://www.github.com/login" -> "github.com", for an entry saved without a title.
std::string HostOf(const std::string& url) {
    std::string s = url;
    const size_t scheme = s.find("://");
    if (scheme != std::string::npos) s = s.substr(scheme + 3);
    const size_t end = s.find_first_of("/?#:");
    if (end != std::string::npos) s = s.substr(0, end);
    if (s.rfind("www.", 0) == 0) s = s.substr(4);
    return s.empty() ? url : s;
}

std::string MethodLine(SignInMethod m) {
    const auto& info = GetSignInMethodInfo(m);
    return std::string("Security: ") + SecurityLevelName(info.level) + ". " + info.description;
}
} // namespace

void EntryDialog::Show(UltraCanvasWindowBase* parent, const PasswordVault& vault,
                       const PasswordEntry& entry,
                       std::function<void(PasswordEntry&)> onSubmit) {
    const bool editing = !entry.id.empty();

    DialogConfig config;
    config.title      = editing ? "Edit password" : "Add password";
    config.width      = 520;
    config.height     = 480;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    auto* dlg = dialog.get();

    dialog->layout.SetFlexColumn()
                  .SetFlexGap(Theme::kGap)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(20);
    dialog->SetBackgroundColor(Theme::kCardBackground);

    auto content = CreateContainer("edForm", 0, 0, 0, 0);
    content->layout.SetFlexColumn()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto addRow = [&content](const std::string& id, const std::string& labelText,
                             const std::vector<std::shared_ptr<UltraCanvasUIElement>>& elements,
                             float height = Theme::kControlHeight) {
        auto row = CreateContainer(id + "Row", 0, 0, 0, height);
        row->layout.SetFlexRow()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto label = Theme::MakeLine(id + "Label", labelText, Theme::kControlHeight,
                                     Theme::kSizeBody, Theme::kTextSecondary);
        label->SetElementSize(Size2Df(kLabelWidth, Theme::kControlHeight));
        label->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Start);
        row->AddChild(label);
        for (size_t i = 0; i < elements.size(); ++i) {
            row->AddChild(elements[i]);
            if (i == 0) elements[i]->layoutItem.SetFlexGrow(1);
        }
        content->AddChild(row);
        return row;
    };

    auto title = CreateTextInput("edTitle", 0, 0, 0, Theme::kControlHeight);
    title->SetPlaceholder("GitHub");
    title->SetText(entry.title);
    Theme::StyleInput(title);
    addRow("edTitle", "Title", {title});

    auto url = CreateTextInput("edUrl", 0, 0, 0, Theme::kControlHeight);
    url->SetPlaceholder("https://github.com/login");
    url->SetText(entry.url);
    Theme::StyleInput(url);
    addRow("edUrl", "Website", {url});

    auto user = CreateTextInput("edUser", 0, 0, 0, Theme::kControlHeight);
    user->SetPlaceholder("User name or e-mail address");
    user->SetText(entry.username);
    Theme::StyleInput(user);
    addRow("edUser", "User name", {user});

    // ----- sign-in method, with what it means -----
    auto method = CreateDropdown("edMethod", 0, 0, 0, Theme::kControlHeight);
    int selected = 0;
    const auto& methods = AllSignInMethods();
    for (size_t i = 0; i < methods.size(); ++i) {
        const auto& info = GetSignInMethodInfo(methods[i]);
        method->AddItem(std::string(info.name) + "  -  " + SecurityLevelName(info.level));
        if (methods[i] == entry.method) selected = static_cast<int>(i);
    }
    method->SetSelectedIndex(selected, false);
    Theme::StyleDropdown(method);
    addRow("edMethod", "Sign-in", {method});

    auto methodInfo = Theme::MakeLine("edMethodInfo", MethodLine(entry.method), 42,
                                      Theme::kSizeSmall, Theme::kTextSecondary);
    methodInfo->SetWrap(TextWrap::WrapWord);
    methodInfo->SetTextColor(Theme::SecurityColor(GetSignInMethodInfo(entry.method).level));
    content->AddChild(methodInfo);

    auto sso = CreateTextInput("edSso", 0, 0, 0, Theme::kControlHeight);
    sso->SetPlaceholder("Google, Apple, Microsoft, GitHub, ...");
    sso->SetText(entry.ssoProvider);
    Theme::StyleInput(sso);
    auto ssoRow = addRow("edSso", "Signs in with", {sso});
    ssoRow->SetVisible(entry.method == SignInMethod::SingleSignOn);

    // ----- password + generator -----
    auto password = CreatePasswordInput("edPass", 0, 0, 0, Theme::kControlHeight);
    password->SetText(entry.password);
    Theme::StyleInput(password);
    auto generate = Theme::MakeButton("edGenerate", "Generate", 80);
    generate->SetTooltip("Fill in a random 20-character password");
    // Raw pointer: the input lives in the dialog that owns the button.
    generate->onClick = [password = password.get()]() {
        std::string fresh = GeneratePassword();
        password->SetText(fresh);
        WipeString(fresh);
    };
    auto passRow = addRow("edPass", "Password", {password, generate});

    auto meter = CreateBarStrengthMeter("edStrength", 0, 0, 0, 14);
    meter->LinkToInput(password.get());
    auto meterRow = addRow("edStrength", "", {meter}, 16);

    const bool usesPassword = GetSignInMethodInfo(entry.method).usesPassword;
    passRow->SetVisible(usesPassword || !entry.password.empty());
    meterRow->SetVisible(usesPassword || !entry.password.empty());

    method->onSelectionChanged =
        [methodInfo = methodInfo.get(), ssoRow = ssoRow.get(), passRow = passRow.get(),
         meterRow = meterRow.get(), password = password.get()](int index, const DropdownItem&) {
            const auto& all = AllSignInMethods();
            if (index < 0 || static_cast<size_t>(index) >= all.size()) return;
            const SignInMethod m = all[static_cast<size_t>(index)];
            const auto& info = GetSignInMethodInfo(m);
            methodInfo->SetText(MethodLine(m));
            methodInfo->SetTextColor(Theme::SecurityColor(info.level));
            ssoRow->SetVisible(m == SignInMethod::SingleSignOn);
            // A passkey-only or SSO account has no password to store; keep the
            // row while one is filled in, so nothing typed vanishes unseen.
            const bool show = info.usesPassword || !password->GetText().empty();
            passRow->SetVisible(show);
            meterRow->SetVisible(show);
        };

    // ----- group -----
    auto group = CreateDropdown("edGroup", 0, 0, 0, Theme::kControlHeight);
    const auto groups = GroupChoices(vault);
    int groupIndex = 0;
    for (size_t i = 0; i < groups.size(); ++i) {
        group->AddItem(groups[i].second, groups[i].first);
        if (groups[i].first == entry.groupId) groupIndex = static_cast<int>(i);
    }
    group->SetSelectedIndex(groupIndex, false);
    Theme::StyleDropdown(group);
    addRow("edGroup", "Group", {group});

    // ----- notes -----
    auto notes = std::make_shared<UltraCanvasTextArea>("edNotes", 0, 0, 0, 70);
    notes->SetWordWrap(true);
    notes->SetShowLineNumbers(false);
    notes->SetHighlightSyntax(false);
    notes->SetText(entry.notes, false);
    notes->SetPlaceholder("Recovery codes, security questions, ...");
    notes->SetFontSize(Theme::kSizeBody + 1.0f);
    notes->GetStyle().borderColor     = Theme::kCardBorder;
    notes->GetStyle().backgroundColor = Theme::kCardBackground;
    notes->GetStyle().textPadding     = 6.0f;
    addRow("edNotes", "Notes", {notes}, 70);

    auto error = Theme::MakeLine("edError", "", 18, Theme::kSizeBody, Theme::kDanger);
    content->AddChild(error);

    dialog->AddChild(content);
    content->layoutItem.SetFlexGrow(1);

    // ----- Cancel / Save -----
    auto buttonRow = CreateContainer("edButtons", 0, 0, 0, Theme::kToolbarHeight);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(Theme::kInnerGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);
    auto cancelBtn = Theme::MakeButton("edCancel", "Cancel", 90);
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);
    auto saveBtn = Theme::MakeButton("edSave", editing ? "Save" : "Add", 90, /*primary=*/true);
    saveBtn->onClick = [dlg, title = title.get(), url = url.get(), error = error.get()]() {
        if (title->GetText().empty() && url->GetText().empty()) {
            error->SetText("Give the password a title or a website.");
            return;
        }
        dlg->CloseDialog(DialogResult::OK);
    };
    buttonRow->AddChild(saveBtn);
    dialog->AddChild(buttonRow);

    PasswordEntry base;
    base.id = entry.id;
    base.created = entry.created;

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [title, url, user, password, method, sso, group, notes, groups, base,
         onSubmit](DialogResult result) {
            if (result != DialogResult::OK) {
                password->SetText("");
                return;
            }
            PasswordEntry out = base;
            out.title    = title->GetText();
            out.url      = url->GetText();
            out.username = user->GetText();
            out.password = password->GetText();
            password->SetText("");
            out.notes    = notes->GetText();
            const int mi = method->GetSelectedIndex();
            const auto& all = AllSignInMethods();
            if (mi >= 0 && static_cast<size_t>(mi) < all.size()) out.method = all[static_cast<size_t>(mi)];
            out.ssoProvider = out.method == SignInMethod::SingleSignOn ? sso->GetText() : "";
            const int gi = group->GetSelectedIndex();
            out.groupId = (gi >= 0 && static_cast<size_t>(gi) < groups.size())
                              ? groups[static_cast<size_t>(gi)].first : kRootGroupId;
            if (out.title.empty()) out.title = HostOf(out.url);
            if (onSubmit) onSubmit(out);
            out.Wipe();
        },
        parent);
    dlg->SetFocusedElement(title.get());
}

} // namespace UltraPassword
