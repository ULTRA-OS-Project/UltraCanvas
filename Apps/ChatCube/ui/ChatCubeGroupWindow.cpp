// Apps/ChatCube/ui/ChatCubeGroupWindow.cpp
// The group editor - see the header.
// Version: 0.1.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework / ULTRA OS

#include "ChatCubeGroupWindow.h"
#include "ChatCubeControls.h"
#include "ChatCubeViews.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"
#include "UltraIM/UltraIMCore.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>

using namespace UltraCanvas;

namespace ChatCube {

namespace {

constexpr int kWindowWidth = 560;
constexpr int kWindowHeight = 680;
constexpr int kFieldWidth = 480;
constexpr int kDescriptionHeight = 96;
constexpr size_t kDescriptionMax = 500;     // the relay's limit

std::shared_ptr<UltraCanvasContainer> Card(const std::string& id) {
    auto c = std::make_shared<UltraCanvasContainer>(id);
    c->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    c->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    c->size.width = CSSLayout::Dimension::Px(static_cast<float>(kFieldWidth + 40));
    OnLook(c, [](UltraCanvasContainer& box) {
        box.SetBackgroundColor(L().panel);
        box.SetBorders(1.0f, L().line, 14.0f);
    });
    c->SetPadding(16, 20, 16, 20);
    return c;
}

std::shared_ptr<UltraCanvasLabel> Heading(const std::string& id, const std::string& text) {
    auto h = MakeLabel(id, text, 13.0f);
    h->SetFontWeight(FontWeight::Bold);
    return h;
}

std::shared_ptr<UltraCanvasContainer> Field(const std::string& id, const std::string& caption,
                                            const std::shared_ptr<UltraCanvasUIElement>& field) {
    auto column = std::make_shared<UltraCanvasContainer>(id);
    column->layout.SetFlexColumn().SetFlexGap(3).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    column->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    column->AddChild(MakeLabel(id + "-caption", caption, 9.0f, &Look::inkMuted));
    column->AddChild(field);
    return column;
}

// One person in a list: the name, then the buttons.
std::shared_ptr<UltraCanvasContainer> PersonRow(const std::string& id, const std::string& name) {
    auto row = MakeRow(id, 6);
    row->size.width = CSSLayout::Dimension::Px(static_cast<float>(kFieldWidth));
    auto label = MakeLabel(id + "-name", name, kTextFont);
    label->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    row->AddChild(label);
    return row;
}

} // namespace

ChatCubeGroupWindow::~ChatCubeGroupWindow() {
    Close();
}

void ChatCubeGroupWindow::Close() {
    if (window_) {
        if (!closed_) window_->Close();
        window_.reset();
    }
    closed_ = true;
}

bool ChatCubeGroupWindow::IsOpenFor(const std::string& conversationId) const {
    return window_ && !closed_ && group_.conversationId == conversationId;
}

void ChatCubeGroupWindow::Show(UltraCanvasWindowBase* host, const UltraIMConversation& group, const std::string& me,
                               bool direct) {
    group_ = group;
    me_ = me;
    direct_ = direct;
    if (!window_ || closed_) {
        Close();
        Build(host);
        if (!window_) return;
    } else {
        window_->RaiseAndFocus();
    }
    SetStatus(std::string());
    Fill();
}

void ChatCubeGroupWindow::Update(const UltraIMConversation& group) {
    if (!IsOpenFor(group.conversationId)) return;
    // What is being typed stays: only the fields not touched follow.
    const bool nameTouched = name_->GetText() != group_.title;
    const bool descriptionTouched = description_->GetText() != group_.description;
    const std::string typedName = name_->GetText();
    const std::string typedDescription = description_->GetText();
    group_ = group;
    Fill();
    if (nameTouched) name_->SetText(typedName);
    if (descriptionTouched) description_->SetText(typedDescription);
}

void ChatCubeGroupWindow::Removed(const std::string& conversationId) {
    if (IsOpenFor(conversationId)) Close();
}

std::string ChatCubeGroupWindow::ProviderId() const {
    return group_.providerId;
}

bool ChatCubeGroupWindow::IsOwner() const {
    return !group_.ownerId.empty() && group_.ownerId == me_;
}

bool ChatCubeGroupWindow::CanEdit() const {
    // A public group, which strangers join: its owner alone.
    return !group_.isPublic || IsOwner();
}

void ChatCubeGroupWindow::Build(UltraCanvasWindowBase* host) {
    WindowConfig wc;
    wc.title = Tr("Group - ChatCube");
    wc.width = kWindowWidth;
    wc.height = kWindowHeight;
    wc.resizable = true;
    wc.type = WindowType::Standard;
    wc.backgroundColor = L().ground;
    if (host) {
        wc.parentWindow = host;
        int hx = 0, hy = 0;
        host->GetWindowPosition(hx, hy);
        wc.x = hx + std::max(0, (static_cast<int>(host->GetWidth()) - kWindowWidth) / 2);
        wc.y = hy + 20;
    }
    window_ = CreateWindow(wc);
    if (!window_ || !window_->IsCreated()) {
        window_.reset();
        return;
    }
    closed_ = false;
    OnLook(window_, [](UltraCanvasWindow& w) {
        w.SetBackgroundColor(L().ground);
        w.RequestRedraw();
    });
    window_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window_->onWindowClosed = [this]() { closed_ = true; };

    auto page = std::make_shared<UltraCanvasContainer>("cc-group-page");
    OnLook(page, [](UltraCanvasContainer& c) {
        ContainerStyle pageStyle = ContainerStyle::FromTheme(UltraCanvasTheme::Current(), c.GetContainerStyle());
        pageStyle.autoShowScrollbars = true;
        pageStyle.autoShowHorizontalScrollbar = false;
        c.SetContainerStyle(pageStyle);
    });
    page->layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    page->SetPadding(18, 20, 18, 20);
    window_->AddChild(page);

    // ---- the title over everything ------------------------------------------------
    auto top = std::make_shared<UltraCanvasContainer>("cc-group-top");
    top->layout.SetFlexColumn().SetFlexGap(2).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    top->size.width = CSSLayout::Dimension::Px(static_cast<float>(kFieldWidth + 40));
    heading_ = MakeLabel("cc-group-heading", "", 15.0f);
    heading_->SetFontWeight(FontWeight::Bold);
    top->AddChild(heading_);
    subtitle_ = MakeLabel("cc-group-subtitle", "", 9.0f, &Look::inkMuted);
    top->AddChild(subtitle_);
    page->AddChild(top);

    // ---- name and description -------------------------------------------------
    auto about = Card("cc-group-about");
    about->AddChild(Heading("cc-group-about-heading", Tr("Group")));
    name_ = MakeInput("cc-group-name", Tr("The group's name"), kFieldWidth);
    name_->onEnterPressed = [this](const std::string&) { Save(); return true; };
    about->AddChild(Field("cc-group-name-field", Tr("Name"), name_));
    description_ = std::make_shared<UltraCanvasTextArea>("cc-group-description", 0, 0, kFieldWidth, kDescriptionHeight);
    description_->SetEditingMode(TextAreaEditingMode::PlainText);
    description_->SetWordWrap(true);
    description_->SetShowLineNumbers(false);
    description_->SetFontSize(kTextFont);
    description_->size.width = CSSLayout::Dimension::Px(static_cast<float>(kFieldWidth));
    description_->size.height = CSSLayout::Dimension::Px(static_cast<float>(kDescriptionHeight));
    OnLook(description_, [](UltraCanvasTextArea& area) {
        TextAreaStyle st = area.GetStyle();
        st.backgroundColor = L().well;
        st.borderColor = L().line;
        st.fontColor = L().ink;
        st.placeholderColor = L().inkFaint;
        st.cursorColor = L().ink;
        st.selectionColor = Color(L().accent.r, L().accent.g, L().accent.b, 90);
        st.currentLineHighlightColor = Colors::Transparent;
        area.SetStyle(st);
        area.SetBackgroundColor(L().well);
        area.SetBorders(1.0f, L().line, kControlRadius);
    });
    descriptionField_ = Field("cc-group-description-field",
                              TrF("Description - what the group is about (up to {0} characters)", kDescriptionMax),
                              description_);
    about->AddChild(descriptionField_);
    editNote_ = MakeLabel("cc-group-edit-note", "", 9.0f, &Look::inkMuted);
    editNote_->SetWrap(TextWrap::WrapWord);
    editNote_->size.width = CSSLayout::Dimension::Px(static_cast<float>(kFieldWidth));
    about->AddChild(editNote_);
    auto actions = MakeRow("cc-group-about-actions", 10);
    actions->size.width = CSSLayout::Dimension::Px(static_cast<float>(kFieldWidth));
    save_ = MakeButton("cc-group-save", Tr("Save"), 90, true);
    save_->SetOnClick([this]() { Save(); });
    actions->AddChild(save_);
    publicButton_ = MakeButton("cc-group-public", Tr("Make public"), 130);
    publicButton_->SetOnClick([this]() {
        if (onSetPublic) onSetPublic(group_.conversationId, !group_.isPublic);
    });
    actions->AddChild(publicButton_);
    status_ = MakeLabel("cc-group-status", "", kTextFont, &Look::inkMuted);
    status_->SetWrap(TextWrap::WrapWord);
    status_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    actions->AddChild(status_);
    about->AddChild(actions);
    page->AddChild(about);

    // ---- members ----------------------------------------------------------------
    auto people = Card("cc-group-members-card");
    auto membersTop = MakeRow("cc-group-members-top", 10);
    membersTop->size.width = CSSLayout::Dimension::Px(static_cast<float>(kFieldWidth));
    membersHeading_ = Heading("cc-group-members-heading", Tr("Members"));
    membersHeading_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    membersTop->AddChild(membersHeading_);
    addMembers_ = MakeButton("cc-group-add", Tr("Add members"), 130);
    addMembers_->SetOnClick([this]() {
        if (onAddMembers) onAddMembers(group_.conversationId);
    });
    membersTop->AddChild(addMembers_);
    people->AddChild(membersTop);
    members_ = std::make_shared<UltraCanvasContainer>("cc-group-members");
    members_->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    people->AddChild(members_);
    page->AddChild(people);

    // ---- blocked (the owner's) ------------------------------------------------------
    blockedCard_ = Card("cc-group-blocked-card");
    blockedHeading_ = Heading("cc-group-blocked-heading", Tr("Blocked"));
    blockedCard_->AddChild(blockedHeading_);
    auto blockedNote = MakeLabel("cc-group-blocked-note",
                                 Tr("They cannot join the group again, and nobody else can add them, until you "
                                    "unblock them."),
                                 9.0f, &Look::inkMuted);
    blockedNote->SetWrap(TextWrap::WrapWord);
    blockedNote->size.width = CSSLayout::Dimension::Px(static_cast<float>(kFieldWidth));
    blockedCard_->AddChild(blockedNote);
    blocked_ = std::make_shared<UltraCanvasContainer>("cc-group-blocked");
    blocked_->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Start);
    blockedCard_->AddChild(blocked_);
    page->AddChild(blockedCard_);

    // ---- out of it ---------------------------------------------------------------
    auto bottom = MakeRow("cc-group-bottom", 10);
    bottom->size.width = CSSLayout::Dimension::Px(static_cast<float>(kFieldWidth + 40));
    auto spacer = MakeLabel("cc-group-bottom-spacer", "", kTextFont);
    spacer->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    bottom->AddChild(spacer);
    auto leave = MakeButton("cc-group-leave", Tr("Leave group"), 130);
    leave->SetOnClick([this]() {
        if (onLeave) onLeave(group_.conversationId);
    });
    bottom->AddChild(leave);
    page->AddChild(bottom);
    window_->Show();
}

void ChatCubeGroupWindow::Fill() {
    if (!window_) return;
    heading_->SetText(group_.title);
    std::string sub = TrN("{n} member", "{n} members", MemberCount(group_));
    if (group_.isPublic) sub += " \xC2\xB7 " + Tr("public group");
    subtitle_->SetText(sub);
    name_->SetText(group_.title);
    description_->SetText(group_.description);
    const bool edit = CanEdit();
    name_->SetDisabled(!edit);
    description_->SetReadOnly(!edit || !direct_);
    // A description is ChatCube's: other networks' groups have their own place for it.
    descriptionField_->SetVisible(direct_);
    save_->SetVisible(edit);
    editNote_->SetText(edit ? (group_.isPublic ? Tr("Everyone on ChatCube sees the name and the description under "
                                                    "Public groups.")
                                               : Tr("Every member can change the name and the description."))
                            : Tr("Only the group's owner changes a public group's name and description."));
    publicButton_->SetVisible(direct_ && IsOwner());
    publicButton_->SetText(group_.isPublic ? Tr("Make private") : Tr("Make public"));
    membersHeading_->SetText(TrF("Members ({0})", MemberCount(group_)));
    FillMembers();
    blockedCard_->SetVisible(direct_ && IsOwner());
    if (direct_ && IsOwner()) RefreshBlocked();
    window_->SetWindowTitle(TrF("{0} - ChatCube", group_.title));
    window_->RequestRedraw();
}

void ChatCubeGroupWindow::FillMembers() {
    members_->ClearChildren();
    // The owner takes others out (where the network has an owner; else anyone may).
    const bool owner = IsOwner() || group_.ownerId.empty();
    for (const std::string& member : group_.memberIds) {
        const bool self = member == me_;
        std::string name = self ? Tr("You") : (nameOf ? nameOf(group_, member) : member);
        if (!group_.ownerId.empty() && member == group_.ownerId) name = TrF("{0} (owner)", name);
        auto row = PersonRow("cc-group-member", name);
        if (!self) {
            auto message = MakeButton("cc-group-member-message", Tr("Message"), 84);
            const std::string provider = ProviderId();
            message->SetOnClick([this, provider, member]() {
                if (onMessage) onMessage(provider, member);
            });
            row->AddChild(message);
            if (owner) {
                auto remove = MakeButton("cc-group-member-remove", Tr("Remove"), 80);
                const std::string id = group_.conversationId;
                remove->SetOnClick([this, id, member]() {
                    if (onRemove) onRemove(id, member);
                });
                row->AddChild(remove);
            }
            if (direct_ && IsOwner()) {
                auto block = MakeButton("cc-group-member-block", Tr("Block"), 70);
                const std::string id = group_.conversationId;
                block->SetOnClick([this, id, member]() {
                    if (onBlock) onBlock(id, member);
                });
                row->AddChild(block);
            }
        }
        members_->AddChild(row);
    }
    addMembers_->SetVisible(CanEdit());
}

void ChatCubeGroupWindow::RefreshBlocked() {
    if (!window_ || closed_ || !direct_ || !IsOwner()) return;
    const std::string id = group_.conversationId;
    UltraIM_FetchBlockedMembers(ProviderId(), id,
                                [this, id](const UltraIMResult& r, const std::vector<UltraIMContact>& list) {
        if (!IsOpenFor(id)) return;
        blocked_->ClearChildren();
        if (!r) {
            blocked_->AddChild(MakeLabel("cc-group-blocked-error", r.message, 9.0f, &Look::inkMuted));
        } else if (list.empty()) {
            blocked_->AddChild(MakeLabel("cc-group-blocked-none", Tr("Nobody is blocked."), kTextFont, &Look::inkMuted));
        }
        for (const UltraIMContact& person : list) {
            const std::string name = person.displayName.empty() ? person.contactId : person.displayName;
            auto row = PersonRow("cc-group-blocked-row", name);
            auto unblock = MakeButton("cc-group-unblock", Tr("Unblock"), 90);
            const std::string member = person.contactId;
            unblock->SetOnClick([this, member, name]() { Unblock(member, name); });
            row->AddChild(unblock);
            blocked_->AddChild(row);
        }
        blockedHeading_->SetText(list.empty() ? Tr("Blocked") : TrF("Blocked ({0})", list.size()));
        if (window_) window_->RequestRedraw();
    });
}

void ChatCubeGroupWindow::Unblock(const std::string& memberId, const std::string& name) {
    UltraIM_UnblockGroupMember(ProviderId(), group_.conversationId, memberId, [this, name](const UltraIMResult& r) {
        SetStatus(r ? TrF("{0} is no longer blocked.", name) : TrF("Could not unblock {0}: {1}", name, r.message));
        RefreshBlocked();
    });
}

void ChatCubeGroupWindow::Save() {
    if (!CanEdit()) return;
    const std::string title = Trim(name_->GetText());
    const std::string description = direct_ ? Trim(description_->GetText()) : group_.description;
    if (title.empty()) {
        SetStatus(Tr("The group needs a name."));
        return;
    }
    if (description.size() > kDescriptionMax) {
        SetStatus(TrF("The description is too long - at most {0} characters.", kDescriptionMax));
        return;
    }
    const bool renamed = title != group_.title;
    const bool described = description != group_.description;
    if (!renamed && !described) {
        SetStatus(Tr("Nothing changed."));
        return;
    }
    save_->SetDisabled(true);
    SetStatus(Tr("Saving\xE2\x80\xA6"));
    const std::string provider = ProviderId();
    const std::string id = group_.conversationId;
    // The description after the name: one change at a time, each answered.
    auto describe = [this, provider, id, description, described]() {
        if (!described) {
            save_->SetDisabled(false);
            SetStatus(Tr("Saved."));
            return;
        }
        UltraIM_SetGroupDescription(provider, id, description,
                                    [this, id](const UltraIMResult& r, const UltraIMConversation& c) {
            if (!IsOpenFor(id)) return;
            save_->SetDisabled(false);
            if (!r) return SetStatus(TrF("Could not save the description: {0}", r.message));
            group_ = c;
            if (onChanged) onChanged(c);
            Fill();
            SetStatus(Tr("Saved."));
        });
    };
    if (!renamed) return describe();
    UltraIM_RenameGroup(provider, id, title, [this, id, describe](const UltraIMResult& r, const UltraIMConversation& c) {
        if (!IsOpenFor(id)) return;
        if (!r) {
            save_->SetDisabled(false);
            return SetStatus(TrF("Could not rename the group: {0}", r.message));
        }
        const std::string typed = description_->GetText();
        group_ = c;
        if (onChanged) onChanged(c);
        Fill();
        description_->SetText(typed);       // still to be saved
        describe();
    });
}

void ChatCubeGroupWindow::SetStatus(const std::string& text) {
    if (status_) status_->SetText(text);
    if (window_) window_->RequestRedraw();
}

} // namespace ChatCube
