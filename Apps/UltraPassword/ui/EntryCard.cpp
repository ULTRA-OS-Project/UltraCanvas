// Apps/UltraPassword/ui/EntryCard.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "EntryCard.h"

#include "Theme.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"

#include <ctime>

using namespace UltraCanvas;

namespace UltraPassword {

namespace {
constexpr float kFieldLabelWidth = 80.0f;
const char* const kMask = "••••••••••••";

std::string FormatDate(int64_t seconds) {
    if (seconds <= 0) return {};
    std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

std::shared_ptr<UltraCanvasContainer> MakeRow(const std::string& id, float height) {
    auto row = CreateContainer(id, 0, 0, 0, height);
    row->layout.SetFlexRow()
               .SetFlexGap(Theme::kInnerGap)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    return row;
}

// "User name   value" — a fixed-width caption, then the value, which grows.
std::shared_ptr<UltraCanvasLabel> AddField(const std::shared_ptr<UltraCanvasContainer>& row,
                                           const std::string& id, const std::string& caption,
                                           const std::string& value) {
    auto cap = Theme::MakeLine(id + ".cap", caption, Theme::kControlHeight,
                               Theme::kSizeBody, Theme::kTextSecondary);
    cap->SetElementSize(Size2Df(kFieldLabelWidth, Theme::kControlHeight));
    row->AddChild(cap);
    auto val = Theme::MakeLine(id + ".val", value, Theme::kControlHeight, Theme::kSizeBody,
                               Theme::kTextPrimary);
    val->layoutItem.SetFlexGrow(1);
    row->AddChild(val);
    return val;
}
} // namespace

std::shared_ptr<UltraCanvasContainer>
BuildEntryCard(const PasswordEntry& entry, const std::string& groupPath,
               bool highlighted, const EntryCardCallbacks& callbacks) {
    const std::string id = "card." + entry.id;
    const auto& info = GetSignInMethodInfo(entry.method);

    auto card = CreateContainer(id, 0, 0, 0, 0);
    Theme::ApplyCard(card);
    if (highlighted) card->SetBorders(2.0f, Theme::kAccent, Theme::kCardRadius);
    card->SetPadding(12);
    // Keeps the right border inside the scroll pane's clip.
    card->SetMargin(0, 2, 0, 0);
    card->layout.SetFlexColumn()
                .SetFlexGap(4)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    card->size.height = CSSLayout::Dimension::Auto();
    card->layoutItem.SetFlexShrink(0);

    // ----- title + sign-in method + rating -----
    auto head = MakeRow(id + ".head", 26);
    auto title = Theme::MakeLine(id + ".title", entry.title, 26, Theme::kSizeHeading,
                                 Theme::kTextPrimary, FontWeight::Bold);
    title->layoutItem.SetFlexGrow(1);
    head->AddChild(title);
    auto method = Theme::MakePill(id + ".method", info.shortName, Theme::kAccent);
    method->SetTooltip(std::string("Sign-in: ") + info.name);
    head->AddChild(method);
    auto rating = Theme::MakePill(id + ".level", SecurityLevelName(info.level),
                                  Theme::SecurityColor(info.level));
    rating->SetTooltip("How well this sign-in method resists phishing and stolen passwords");
    head->AddChild(rating);
    card->AddChild(head);

    // ----- website -----
    if (!entry.url.empty()) {
        auto row = MakeRow(id + ".urlRow", Theme::kControlHeight);
        AddField(row, id + ".url", "Website", entry.url);
        if (callbacks.openWebsite) {
            auto open = Theme::MakeButton(id + ".open", "Open", 56);
            const std::string url = entry.url;
            open->onClick = [cb = callbacks.openWebsite, url]() { cb(url); };
            row->AddChild(open);
        }
        card->AddChild(row);
    }

    // ----- user name -----
    if (!entry.username.empty()) {
        auto row = MakeRow(id + ".userRow", Theme::kControlHeight);
        AddField(row, id + ".user", "User name", entry.username);
        auto copy = Theme::MakeButton(id + ".copyUser", "Copy", 56);
        const std::string user = entry.username;
        copy->onClick = [cb = callbacks.copy, user]() { if (cb) cb(user, "User name"); };
        row->AddChild(copy);
        card->AddChild(row);
    }

    // ----- password, masked -----
    if (info.usesPassword || !entry.password.empty()) {
        auto row = MakeRow(id + ".passRow", Theme::kControlHeight);
        auto value = AddField(row, id + ".pass", "Password",
                              entry.password.empty() ? "(none stored)" : kMask);
        if (!entry.password.empty()) {
            auto show = Theme::MakeButton(id + ".show", "Show", 56);
            // Raw pointers: the label and the button live in this card, which
            // owns the callback (a shared_ptr here would be a cycle).
            show->onClick = [value = value.get(), button = show.get(),
                             cb = callbacks.passwordOf, entryId = entry.id]() {
                if (button->GetText() == "Show") {
                    std::string pw = cb ? cb(entryId) : std::string();
                    value->SetText(pw);
                    WipeString(pw);
                    button->SetText("Hide");
                } else {
                    value->SetText(kMask);
                    button->SetText("Show");
                }
            };
            row->AddChild(show);
            auto copy = Theme::MakeButton(id + ".copyPass", "Copy", 56);
            copy->onClick = [cbPw = callbacks.passwordOf, cbCopy = callbacks.copy,
                             entryId = entry.id]() {
                if (!cbPw || !cbCopy) return;
                std::string pw = cbPw(entryId);
                cbCopy(pw, "Password");
                WipeString(pw);
            };
            row->AddChild(copy);
        }
        card->AddChild(row);
    }

    // ----- single sign-on provider -----
    if (entry.method == SignInMethod::SingleSignOn) {
        auto row = MakeRow(id + ".ssoRow", Theme::kControlHeight);
        AddField(row, id + ".sso", "Signs in with",
                 entry.ssoProvider.empty() ? "(provider not recorded)" : entry.ssoProvider);
        card->AddChild(row);
    }

    // ----- what the method means -----
    auto why = Theme::MakeLine(id + ".why", info.description, 30, Theme::kSizeSmall,
                               Theme::SecurityColor(info.level));
    why->SetWrap(TextWrap::WrapWord);
    why->size.height = CSSLayout::Dimension::Auto();
    card->AddChild(why);

    if (!entry.notes.empty()) {
        auto notes = Theme::MakeLine(id + ".notes", entry.notes, 32, Theme::kSizeSmall,
                                     Theme::kTextSecondary);
        notes->SetWrap(TextWrap::WrapWord);
        notes->size.height = CSSLayout::Dimension::Auto();
        card->AddChild(notes);
    }

    // ----- footer -----
    auto foot = MakeRow(id + ".foot", Theme::kControlHeight);
    std::string meta = groupPath.empty() ? "Top level" : groupPath;
    const std::string changed = FormatDate(entry.modified);
    if (!changed.empty()) meta += "  ·  changed " + changed;
    auto metaLabel = Theme::MakeLine(id + ".meta", meta, Theme::kControlHeight,
                                     Theme::kSizeSmall, Theme::kTextMuted);
    metaLabel->layoutItem.SetFlexGrow(1);
    foot->AddChild(metaLabel);
    auto edit = Theme::MakeButton(id + ".edit", "Edit", 56);
    edit->onClick = [cb = callbacks.edit, entryId = entry.id]() { if (cb) cb(entryId); };
    foot->AddChild(edit);
    auto del = Theme::MakeButton(id + ".delete", "Delete", 64);
    del->onClick = [cb = callbacks.remove, entryId = entry.id]() { if (cb) cb(entryId); };
    foot->AddChild(del);
    card->AddChild(foot);

    return card;
}

} // namespace UltraPassword
