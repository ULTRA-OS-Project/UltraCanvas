// Apps/UltraMail/ui/UltraMailContactsView.cpp
// Version: 0.3.0 - themed flex layout: a sidebar with selectable section
//                  entries and counts, a titled list of contact cards with
//                  initials, and a dialog that matches the account wizard.
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailContactsView.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasModalDialog.h"
#include "UltraMailAlerts.h"
#include "UltraMailTheme.h"
#include "UltraCanvasEvent.h"

#include <cctype>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace UltraMail {

namespace {
constexpr float kSidebarW     = 200.0f;
constexpr float kRowH         = 56.0f;
constexpr float kSectionRowH  = 32.0f;
constexpr float kRowAvatar    = 34.0f;
constexpr float kDialogLabelW = 100.0f;

// The contact's initial for the row avatar.
std::string Initial(const std::string& name) {
    for (char c : name)
        if (std::isalnum(static_cast<unsigned char>(c)))
            return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return "?";
}
} // namespace

// ---- ContactRow ------------------------------------------------------------

ContactRow::ContactRow(const std::string& id, float x, float y, float w, float h,
                       const Contact& contact,
                       std::function<void()> onEdit,
                       std::function<void()> onDelete)
    : UltraCanvasContainer(id, x, y, w, h),
      onEdit_(std::move(onEdit)), onDelete_(std::move(onDelete)) {
    // A white card: [initial] name / email · phone
    SetBackgroundColor(Theme::kCardBackground);
    SetBorders(1.0f, Theme::kCardBorder, 8.0f);
    SetPadding(0, 12);
    layout.SetFlexRow()
          .SetFlexGap(12)
          .SetFlexAlignItems(CSSLayout::AlignItems::Center);

    const std::string name = contact.displayName.empty() ? "(no name)" : contact.displayName;
    AddChild(Theme::MakeAvatar(id + ".avatar", Initial(contact.displayName), kRowAvatar));

    auto text = CreateContainer(id + ".text", 0, 0, 0, 0);
    text->layout.SetFlexColumn()
                .SetFlexGap(2)
                .SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
    text->AddChild(Theme::MakeText(id + ".name", name, Theme::kSizeBody, Theme::kTextPrimary,
                                   FontWeight::Bold));
    std::string sub = contact.PrimaryEmail();
    if (!contact.phones.empty()) {
        if (!sub.empty()) sub += "  ·  ";
        sub += contact.phones.front().number;
    }
    if (sub.empty() && !contact.organization.empty()) sub = contact.organization;
    text->AddChild(Theme::MakeText(id + ".sub", sub, Theme::kSizeSecondary,
                                   Theme::kTextSecondary));
    AddChild(text);
    text->layoutItem.SetFlexGrow(1);

    if (!contact.organization.empty() && sub != contact.organization) {
        auto org = Theme::MakeText(id + ".org", contact.organization, Theme::kSizeSecondary,
                                   Theme::kTextMuted);
        AddChild(org);
    }
    SetTooltip("Double-click to edit · right-click for more");
}

bool ContactRow::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (event.type == UCEventType::MouseDoubleClick) {
        if (onEdit_) onEdit_();
        return true;
    }
    if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Right) {
        ShowContextMenu(event);
        return true;
    }
    return UltraCanvasContainer::OnEvent(event);
}

void ContactRow::ShowContextMenu(const UCEvent& event) {
    UltraCanvasWindowBase* window = GetWindow();
    if (!window) return;
    menu_ = std::make_shared<UltraCanvasMenu>(GetIdentifier() + ".ctx", 0, 0, 160, 0);
    menu_->SetMenuType(MenuType::PopupMenu);
    menu_->AddItem(MenuItemData::Action("Edit",   [this]() { if (onEdit_)   onEdit_(); }));
    menu_->AddItem(MenuItemData::Action("Delete", [this]() { if (onDelete_) onDelete_(); }));
    PopupElementSettings settings;
    menu_->OpenMenu(event.pointerWindow, *window, settings);
}

// ---- ContactsView ----------------------------------------------------------

std::shared_ptr<UltraCanvasContainer> ContactsView::Build() {
    // [sidebar | main]: the sidebar is a tinted column with a divider, the
    // main column carries the title row and the card list.
    root_ = CreateContainer("contactsView", 0, 0, 0, 0);
    root_->layout.SetFlexRow()
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    sidebar_ = CreateContainer("contactsSidebar", 0, 0, kSidebarW, 0);
    sidebar_->SetBackgroundColor(Theme::kSidebar);
    sidebar_->SetBorders(0.0f, Colors::Transparent, 0.0f);
    sidebar_->SetPadding(Theme::kPagePadding, 10);
    sidebar_->layout.SetFlexColumn()
                    .SetFlexGap(2)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    root_->AddChild(sidebar_);
    sidebar_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto rule = CreateContainer("contactsRule", 0, 0, 1, 0);
    rule->SetBackgroundColor(Theme::kDivider);
    root_->AddChild(rule);
    rule->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto main = CreateContainer("contactsMain", 0, 0, 0, 0);
    main->SetPadding(Theme::kPagePadding);
    main->layout.SetFlexColumn()
                .SetFlexGap(Theme::kGap)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto titleRow = CreateContainer("contactsTitleRow", 0, 0, 0, Theme::kToolbarHeight);
    titleRow->layout.SetFlexRow()
                    .SetFlexGap(Theme::kInnerGap)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    titleLabel_ = Theme::MakeLine("contactsTitle", DisplayName(current_), Theme::kToolbarHeight,
                                  Theme::kSizeTitle, Theme::kTextPrimary, FontWeight::Bold);
    titleRow->AddChild(titleLabel_);
    titleLabel_->layoutItem.SetFlexGrow(1);
    auto addBtn = CreateButton("contactsAdd", 0, 0, 130, Theme::kControlHeight, "Add contact");
    Theme::StylePrimary(addBtn);
    addBtn->onClick = [this]() {
        Contact c; c.section = current_;
        ShowContactDialog(c, /*isNew=*/true);
    };
    titleRow->AddChild(addBtn);
    main->AddChild(titleRow);
    titleRow->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    list_ = CreateContainer("contactsList", 0, 0, 0, 0);
    list_->layout.SetFlexColumn()
                 .SetFlexGap(Theme::kInnerGap)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    main->AddChild(list_);
    list_->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    root_->AddChild(main);
    main->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    Refresh();
    return root_;
}

void ContactsView::Resize(float width, float height) {
    if (root_) root_->SetElementSize(Size2Df(width, height));
}

void ContactsView::SelectSection(ContactSection section) {
    current_ = section;
    if (titleLabel_) titleLabel_->SetText(DisplayName(section));
    RebuildSidebar();   // refresh the selected entry
    RebuildList();
}

void ContactsView::Refresh() {
    RebuildSidebar();
    RebuildList();
}

void ContactsView::RebuildSidebar() {
    if (!sidebar_ || !store_) return;
    sidebar_->ClearChildren();

    auto header = Theme::MakeLine("contactsHeader", "Contacts", 28, Theme::kSizeSecondary,
                                  Theme::kTextMuted, FontWeight::Bold);
    header->SetPadding(0, 10);
    sidebar_->AddChild(header);
    header->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    std::vector<SectionCount> counts;
    store_->GetSectionCounts(counts);

    for (const auto& sc : counts) {
        const ContactSection s = sc.section;
        const bool selected = (s == current_);
        auto entry = std::make_shared<Theme::ClickSurface>("sect_" + ToString(s),
                                                            [this, s]() { SelectSection(s); });
        entry->SetElementSize(CSSLayout::Dimension::Auto(),
                              CSSLayout::Dimension::Px(kSectionRowH));
        entry->SetBackgroundColor(selected ? Theme::kAccentSoft : Colors::Transparent);
        entry->SetBorders(0.0f, Colors::Transparent, Theme::kControlRadius);
        entry->SetPadding(0, 10);
        entry->layout.SetFlexRow()
                     .SetFlexGap(Theme::kInnerGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto name = Theme::MakeText("sectName_" + ToString(s), DisplayName(s), Theme::kSizeBody,
                                    selected ? Theme::kAccent : Theme::kTextPrimary,
                                    selected ? FontWeight::Bold : FontWeight::Normal);
        entry->AddChild(name);
        name->layoutItem.SetFlexGrow(1);
        entry->AddChild(Theme::MakeText("sectCount_" + ToString(s), std::to_string(sc.count),
                                        Theme::kSizeSecondary,
                                        selected ? Theme::kAccent : Theme::kTextMuted));
        sidebar_->AddChild(entry);
        entry->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    }
}

void ContactsView::RebuildList() {
    if (!list_ || !store_) return;
    list_->ClearChildren();

    std::vector<Contact> contacts;
    store_->ListBySection(current_, contacts);

    if (contacts.empty()) {
        auto empty = Theme::MakeLine("contactsEmpty", "No contacts in this section yet.", 24,
                                     Theme::kSizeBody, Theme::kTextMuted);
        list_->AddChild(empty);
        empty->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        return;
    }

    for (const auto& c : contacts) {
        const int64_t id = c.id;
        auto onEdit = [this, c]() { ShowContactDialog(c, /*isNew=*/false); };
        auto onDelete = [this, id]() { if (store_->Remove(id)) Refresh(); };
        auto row = std::make_shared<ContactRow>(
            "contact_" + std::to_string(id), 0, 0, 0, kRowH, c,
            std::move(onEdit), std::move(onDelete));
        list_->AddChild(row);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    }
}

void ContactsView::ShowContactDialog(Contact contact, bool isNew) {
    if (!store_) return;

    DialogConfig config;
    config.title      = isNew ? "Add contact" : "Edit contact";
    config.width      = 460;
    config.height     = 380;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;  // Custom dialog builds its own.

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    // Raw pointer for button callbacks: the dialog owns the buttons, so capturing
    // the shared_ptr would form a reference cycle.
    auto* dlg = dialog.get();

    dialog->layout.SetFlexColumn()
                  .SetFlexGap(Theme::kGap)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(20);
    dialog->SetBackgroundColor(Theme::kCardBackground);

    // ===== CONTENT: labelled input rows =====
    auto content = CreateContainer("contactForm", 0, 0, 0, 0);
    content->layout.SetFlexColumn()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // Build a [label + input] flex row and append it to the content column.
    auto addRow = [&content](const std::string& id, const std::string& labelText,
                             const std::shared_ptr<UltraCanvasTextInput>& input) {
        auto row = CreateContainer(id + "Row", 0, 0, 0, Theme::kControlHeight);
        row->layout.SetFlexRow()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto label = Theme::MakeLine(id + "Label", labelText, Theme::kControlHeight,
                                     Theme::kSizeBody, Theme::kTextSecondary);
        label->SetElementSize(Size2Df(kDialogLabelW, Theme::kControlHeight));
        row->AddChild(label);
        Theme::StyleInput(input);
        row->AddChild(input);
        input->layoutItem.SetFlexGrow(1);
        content->AddChild(row);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    };

    auto name = CreateTextInput("cNameIn", 0, 0, 0, Theme::kControlHeight);
    name->SetText(contact.displayName);
    name->SetPlaceholder("Erika Example");
    addRow("cName", "Name", name);

    auto email = CreateEmailInput("cEmailIn", 0, 0, 0, Theme::kControlHeight);
    email->SetText(contact.PrimaryEmail());
    email->SetPlaceholder("erika@example.com");
    addRow("cEmail", "Email", email);

    auto phone = CreateTextInput("cPhoneIn", 0, 0, 0, Theme::kControlHeight);
    if (!contact.phones.empty()) phone->SetText(contact.phones.front().number);
    phone->SetPlaceholder("+49 170 1234567");
    addRow("cPhone", "Phone", phone);

    auto org = CreateTextInput("cOrgIn", 0, 0, 0, Theme::kControlHeight);
    org->SetText(contact.organization);
    org->SetPlaceholder("Company / group");
    addRow("cOrg", "Organization", org);

    auto notes = CreateTextInput("cNotesIn", 0, 0, 0, Theme::kControlHeight);
    notes->SetText(contact.notes);
    addRow("cNotes", "Notes", notes);

    auto section = Theme::MakeLine("cSection", "Section: " + DisplayName(contact.section), 22,
                                   Theme::kSizeSecondary, Theme::kTextMuted);
    content->AddChild(section);
    section->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    dialog->AddChild(content);
    content->layoutItem.SetFlexGrow(1);

    // ===== BUTTON ROW: Save / Cancel =====
    auto buttonRow = CreateContainer("cButtons", 0, 0, 0, Theme::kToolbarHeight);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(Theme::kInnerGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);

    auto cancelBtn = CreateButton("cCancel", 0, 0, 90, Theme::kControlHeight, "Cancel");
    Theme::StyleSecondary(cancelBtn);
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);

    auto saveBtn = CreateButton("cSave", 0, 0, 100, Theme::kControlHeight, "Save");
    Theme::StylePrimary(saveBtn);
    saveBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(saveBtn);

    dialog->AddChild(buttonRow);

    // Capture by value; `contact` carries id + section through the callback.
    UltraCanvas::UltraCanvasWindowBase* parent = root_ ? root_->GetWindow() : nullptr;
    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [this, contact, name, email, phone, org, notes, parent](DialogResult result) mutable {
            if (result != DialogResult::OK) return;
            contact.displayName = name->GetText();
            if (contact.displayName.empty()) {
                AlertWarning(parent, "The contact was not saved because it has "
                                     "no name.",
                             "Enter a name for the contact and try again.");
                return;
            }

            contact.organization = org->GetText();
            contact.notes = notes->GetText();

            const std::string addr = email->GetText();
            contact.emails.clear();
            if (!addr.empty()) {
                ContactEmail e; e.address = addr; e.primary = true;
                contact.emails.push_back(e);
            }

            const std::string num = phone->GetText();
            contact.phones.clear();
            if (!num.empty()) {
                ContactPhone p; p.number = num; p.label = "mobile";
                contact.phones.push_back(p);
            }

            if (UltraDbResult saved = store_->Save(contact); saved) {
                Refresh();
            } else {
                AlertError(parent, "\"" + contact.displayName + "\" could not be "
                           "saved to the address book.", DetailLine(saved));
            }
        },
        parent);
}

} // namespace UltraMail
