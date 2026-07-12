// Apps/UltraMail/ui/UltraMailContactsView.cpp
// Version: 0.2.0
// Last Modified: 2026-07-08
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailContactsView.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasEvent.h"

#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace UltraMail {

namespace {
constexpr float kSidebarW = 190.0f;
constexpr float kRowH     = 46.0f;
} // namespace

// ---- ContactRow ------------------------------------------------------------

ContactRow::ContactRow(const std::string& id, float x, float y, float w, float h,
                       const Contact& contact,
                       std::function<void()> onEdit,
                       std::function<void()> onDelete)
    : UltraCanvasContainer(id, x, y, w, h),
      onEdit_(std::move(onEdit)), onDelete_(std::move(onDelete)) {
    AddChild(CreateLabel(id + ".name", 8, 4, w - 16, 20,
                         contact.displayName.empty() ? "(no name)" : contact.displayName));
    std::string sub = contact.PrimaryEmail();
    if (!contact.phones.empty()) {
        if (!sub.empty()) sub += "  ·  ";
        sub += contact.phones.front().number;
    }
    if (sub.empty() && !contact.organization.empty()) sub = contact.organization;
    AddChild(CreateLabel(id + ".sub", 8, 24, w - 16, 18, sub));
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
    root_ = CreateContainer("contactsView", 0, 0, 0, 0);

    sidebar_ = CreateContainer("contactsSidebar", 0, 0, kSidebarW, 0);
    root_->AddChild(sidebar_);

    titleLabel_ = CreateLabel("contactsTitle", kSidebarW + 16, 12, 300, 24, "Friends");
    root_->AddChild(titleLabel_);

    auto addBtn = CreateButton("contactsAdd", kSidebarW + 16, 44, 150, 30, "＋ Add contact");
    addBtn->onClick = [this]() {
        Contact c; c.section = current_;
        ShowContactDialog(c, /*isNew=*/true);
    };
    root_->AddChild(addBtn);

    list_ = CreateContainer("contactsList", kSidebarW + 16, 84, 0, 0);
    root_->AddChild(list_);

    Refresh();
    return root_;
}

void ContactsView::SelectSection(ContactSection section) {
    current_ = section;
    if (titleLabel_) titleLabel_->SetText(DisplayName(section));
    RebuildSidebar();   // refresh the "▸" marker
    RebuildList();
}

void ContactsView::Refresh() {
    RebuildSidebar();
    RebuildList();
}

void ContactsView::RebuildSidebar() {
    if (!sidebar_ || !store_) return;
    sidebar_->ClearChildren();

    sidebar_->AddChild(CreateLabel("contactsHeader", 12, 12, kSidebarW - 20, 22, "Contacts"));

    std::vector<SectionCount> counts;
    store_->GetSectionCounts(counts);

    float y = 44;
    for (const auto& sc : counts) {
        const ContactSection s = sc.section;
        std::string marker = (s == current_) ? "▸ " : "   ";
        std::string text = marker + DisplayName(s) + "  (" + std::to_string(sc.count) + ")";
        auto label = CreateLabel("sect_" + ToString(s), 12, y, kSidebarW - 20, 24, text);
        label->onClick = [this, s]() { SelectSection(s); };
        sidebar_->AddChild(label);
        y += 30;
    }
}

void ContactsView::RebuildList() {
    if (!list_ || !store_) return;
    list_->ClearChildren();

    std::vector<Contact> contacts;
    store_->ListBySection(current_, contacts);

    if (contacts.empty()) {
        list_->AddChild(CreateLabel("contactsEmpty", 0, 0, 360, 22,
                                    "No contacts in this section yet."));
        return;
    }

    float y = 0;
    for (const auto& c : contacts) {
        const int64_t id = c.id;
        auto onEdit = [this, c]() { ShowContactDialog(c, /*isNew=*/false); };
        auto onDelete = [this, id]() { if (store_->Remove(id)) Refresh(); };
        auto row = std::make_shared<ContactRow>(
            "contact_" + std::to_string(id), 0, y, 360, kRowH, c,
            std::move(onEdit), std::move(onDelete));
        list_->AddChild(row);
        y += kRowH + 4;
    }
}

void ContactsView::ShowContactDialog(Contact contact, bool isNew) {
    if (!store_) return;

    DialogConfig config;
    config.title      = isNew ? "Add contact" : "Edit contact";
    config.width      = 440;
    config.height     = 400;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;  // Custom dialog builds its own.

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    // Raw pointer for button callbacks: the dialog owns the buttons, so capturing
    // the shared_ptr would form a reference cycle.
    auto* dlg = dialog.get();

    dialog->layout.SetFlexColumn()
                  .SetFlexGap(12)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(16);

    // ===== CONTENT: labelled input rows =====
    auto content = CreateContainer("contactForm", 0, 0, 0, 0);
    content->layout.SetFlexColumn()
                   .SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // Build a [label + input] flex row and append it to the content column.
    auto addRow = [&content](const std::string& id, const std::string& labelText,
                             const std::shared_ptr<UltraCanvasTextInput>& input) {
        auto row = CreateContainer(id + "Row", 0, 0, 0, 30);
        row->layout.SetFlexRow()
                   .SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto label = CreateLabel(id + "Label", 0, 0, 110, 26, labelText);
        row->AddChild(label);
        label->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        row->AddChild(input);
        input->layoutItem.SetFlexGrow(1);
        content->AddChild(row);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    };

    auto name = CreateTextInput("cNameIn", 0, 0, 270, 28);
    name->SetText(contact.displayName);
    name->SetPlaceholder("Erika Example");
    addRow("cName", "Name", name);

    auto email = CreateEmailInput("cEmailIn", 0, 0, 270, 28);
    email->SetText(contact.PrimaryEmail());
    email->SetPlaceholder("erika@example.com");
    addRow("cEmail", "Email", email);

    auto phone = CreateTextInput("cPhoneIn", 0, 0, 270, 28);
    if (!contact.phones.empty()) phone->SetText(contact.phones.front().number);
    phone->SetPlaceholder("+49 170 1234567");
    addRow("cPhone", "Phone", phone);

    auto org = CreateTextInput("cOrgIn", 0, 0, 270, 28);
    org->SetText(contact.organization);
    org->SetPlaceholder("Company / group");
    addRow("cOrg", "Organization", org);

    auto notes = CreateTextInput("cNotesIn", 0, 0, 270, 28);
    notes->SetText(contact.notes);
    addRow("cNotes", "Notes", notes);

    auto section = CreateLabel("cSection", 0, 0, 400, 22,
                               "Section: " + DisplayName(contact.section));
    content->AddChild(section);
    section->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    dialog->AddChild(content);
    content->layoutItem.SetFlexGrow(1);

    // ===== BUTTON ROW: Save / Cancel =====
    auto buttonRow = CreateContainer("cButtons", 0, 0, 0, 36);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(10)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);

    auto saveBtn = std::make_shared<UltraCanvasButton>("cSave", 0, 0, 90, 28);
    saveBtn->SetText("Save");
    saveBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(saveBtn);

    auto cancelBtn = std::make_shared<UltraCanvasButton>("cCancel", 0, 0, 80, 28);
    cancelBtn->SetText("Cancel");
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);

    dialog->AddChild(buttonRow);

    // Capture by value; `contact` carries id + section through the callback.
    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [this, contact, name, email, phone, org, notes](DialogResult result) mutable {
            if (result != DialogResult::OK) return;
            contact.displayName = name->GetText();
            if (contact.displayName.empty()) return;

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

            if (store_->Save(contact)) Refresh();
        },
        root_ ? root_->GetWindow() : nullptr);
}

} // namespace UltraMail
