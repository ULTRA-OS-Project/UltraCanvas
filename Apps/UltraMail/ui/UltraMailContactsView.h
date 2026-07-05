// Apps/UltraMail/ui/UltraMailContactsView.h
// The contact manager view: a section sidebar (Friends / Work / Leisure /
// Services, with per-section counts) beside the contact list for the selected
// section. Add via a button, edit by double-clicking a row, delete from a
// row's right-click menu. Backed by the engine ContactStore.
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailContactStore.h"

#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMenu.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraMail {

// One contact row: double-click edits, right-click offers Delete.
class ContactRow : public UltraCanvas::UltraCanvasContainer {
public:
    ContactRow(const std::string& id, float x, float y, float w, float h,
               const Contact& contact,
               std::function<void()> onEdit,
               std::function<void()> onDelete);

    bool OnEvent(const UltraCanvas::UCEvent& event) override;

private:
    void ShowContextMenu(const UltraCanvas::UCEvent& event);
    std::function<void()> onEdit_;
    std::function<void()> onDelete_;
    std::shared_ptr<UltraCanvas::UltraCanvasMenu> menu_;
};

class ContactsView {
public:
    void SetStore(ContactStore* store) { store_ = store; }

    // Build the whole panel; add the result to a window.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();

    // Reload section counts and the current section's contacts.
    void Refresh();

    void SelectSection(ContactSection section);

private:
    void RebuildSidebar();
    void RebuildList();
    void ShowContactDialog(Contact contact, bool isNew);

    ContactStore*  store_ = nullptr;
    ContactSection current_ = ContactSection::Friends;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> sidebar_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> list_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     titleLabel_;
};

} // namespace UltraMail
