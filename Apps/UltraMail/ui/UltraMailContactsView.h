// Apps/UltraMail/ui/UltraMailContactsView.h
// The contact manager view: a sidebar of sections (Family, Friends, Work, …)
// and the user's own groups, each with its count, beside a filterable,
// scrolling list of the selected one's contacts. Add with the button, edit by
// double-clicking a row, delete with the row's bin icon; the row's right-click
// menu also moves it to another section or group. Backed by ContactStore.
// Version: 0.3.0 - groups of the user's own, filter, virtualised list (a
//                  thousand contacts no longer build a thousand elements),
//                  delete icon, "Move to group".
// Last Modified: 2026-09-28
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailContactStore.h"

#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasTextInput.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraMail {

class ContactsView {
public:
    void SetStore(ContactStore* store) { store_ = store; }

    // Build the whole panel; add the result to a window. Building again (a
    // new window) replaces the previous panel.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Container() const { return root_; }

    // Forget the panel (its window closed): later Refresh calls do nothing
    // until the next Build.
    void Release();

    // Size the view to the window's client area (call on window resize).
    void Resize(float width, float height);

    // Reload the sidebar counts and the selected place's contacts.
    void Refresh();

    void Select(const ContactPlace& place);

    // The contact editor ("Add contact" / "Edit contact"), usable without this
    // view on screen - the mail list opens it for a message's sender. Saves to
    // the store, refreshes this view when it is built, then calls `onSaved`.
    // Only the primary address is edited; a contact's other addresses stay.
    void EditContact(Contact contact, bool isNew, UltraCanvas::UltraCanvasWindowBase* parent,
                     std::function<void(const Contact&)> onSaved = nullptr);

    // Called after any change to the address book made here, so the app can
    // re-read it (the sender badges).
    std::function<void()> onChanged;

private:
    struct SidebarEntry {
        ContactPlace place;
        std::shared_ptr<UltraCanvas::UltraCanvasContainer> surface;
        std::shared_ptr<UltraCanvas::UltraCanvasLabel>     name;
        std::shared_ptr<UltraCanvas::UltraCanvasLabel>     count;
    };

    void RebuildSidebar();
    void RestyleSidebar();
    void LoadContacts();      // the selected place's contacts from the store
    void ApplyFilter();       // contacts_ -> shown_ -> list model
    UltraCanvas::UltraCanvasWindowBase* Window() const;

    void ShowRowMenu(int row, const UltraCanvas::UCEvent& event);
    void ShowPlaceMenu(const ContactPlace& place, const UltraCanvas::UCEvent& event);
    void ConfirmDelete(const Contact& contact);
    void AskNewGroup();
    void ConfirmDeleteGroup(const std::string& group);
    void Changed();           // Refresh + onChanged

    ContactStore* store_ = nullptr;
    ContactPlace  current_;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> sidebar_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> sidebarList_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     titleLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> filter_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView>  list_;
    std::shared_ptr<UltraCanvas::UltraCanvasSimpleListModel> model_;
    std::shared_ptr<UltraCanvas::IItemDelegate>        delegate_;
    std::shared_ptr<UltraCanvas::UltraCanvasMenu>      menu_;
    std::vector<SidebarEntry> entries_;

    std::vector<Contact> contacts_;   // the selected place, as loaded
    std::vector<Contact> shown_;      // after the filter; parallel to the list rows
};

} // namespace UltraMail
