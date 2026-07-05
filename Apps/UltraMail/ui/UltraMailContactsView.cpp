// Apps/UltraMail/ui/UltraMailContactsView.cpp
// Version: 0.1.0 (Phase 2)
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
    config.width      = 420;
    config.height     = 260;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::OKCancel;

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);

    auto form = CreateContainer("contactForm", 0, 0, 400, 180);
    form->AddChild(CreateLabel("cName", 12, 12, 120, 22, "Name"));
    auto name = CreateTextInput("cNameIn", 130, 12, 250, 28);
    name->SetText(contact.displayName);
    name->SetPlaceholder("Erika Example");
    form->AddChild(name);

    form->AddChild(CreateLabel("cEmail", 12, 52, 120, 22, "Email"));
    auto email = CreateEmailInput("cEmailIn", 130, 52, 250, 28);
    email->SetText(contact.PrimaryEmail());
    email->SetPlaceholder("erika@example.com");
    form->AddChild(email);

    form->AddChild(CreateLabel("cSection", 12, 92, 120, 22,
                               "Section: " + DisplayName(contact.section)));
    dialog->AddDialogElement(form);

    // Capture by value; `contact` carries id + section through the callback.
    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [this, contact, name, email](DialogResult result) mutable {
            if (result != DialogResult::OK) return;
            contact.displayName = name->GetText();
            if (contact.displayName.empty()) return;

            const std::string addr = email->GetText();
            contact.emails.clear();
            if (!addr.empty()) {
                ContactEmail e; e.address = addr; e.primary = true;
                contact.emails.push_back(e);
            }
            if (store_->Save(contact)) Refresh();
        },
        root_ ? root_->GetWindow() : nullptr);
}

} // namespace UltraMail
