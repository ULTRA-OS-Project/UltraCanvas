// Apps/EmailCleaner/ui/EmailCleanerContactsView.h
// The Contacts page: UltraMail's address book, one row per contact, ordered by
// how often the owner wrote to it. Each row carries a tag - Keep for a contact
// written to, Delete for one only ever collected from incoming mail, Review for
// one the user filed but never wrote to - the count of messages sent to it,
// and, as the second figure, the count received from it.
//
// The Delete rows start selected and every selected row is marked red;
// "Delete selected…" removes them from the address book after a confirmation.
// The view only shows and selects: reading and deleting are the app's
// (onDelete), since the address book is UltraMail's file.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first (X11 macro hygiene — see the engine headers).
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

#include "EmailCleanerContacts.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace EmailCleaner {

class ContactsView {
public:
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build(float x, float y,
                                                             float width, float height);

    // Show a fresh review. The Delete rows are selected again.
    void SetReviews(std::vector<ContactReview> reviews);
    // Show why there is no list ("No UltraMail address book found …").
    void SetUnavailable(const std::string& reason);

    // The ids of the selected rows, in list order.
    std::vector<int64_t> SelectedIds() const;

    // "Delete selected…" with the selected contacts; the app confirms and
    // deletes, then hands a fresh review back through SetReviews.
    std::function<void(const std::vector<ContactReview>&)> onDelete;
    // "Reload": read the address book and the counts again.
    std::function<void()> onReload;

private:
    struct Row {
        ContactReview review;
        std::shared_ptr<UltraCanvas::UltraCanvasContainer> container;
        std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>  select;
        std::shared_ptr<UltraCanvas::UltraCanvasLabel>     name;
    };

    void RebuildRows();
    // Colour one row by whether it is selected: red when it is.
    void StyleRow(Row& row);
    // Select exactly the rows whose verdict is Delete (or none).
    void SelectDeleteRows(bool select);
    void UpdateSelectionText();

    float width_ = 0.0f;
    bool  bulkChange_ = false;   // SelectDeleteRows is ticking boxes
    std::vector<Row> rows_;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> container_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     summary_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     selection_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    deleteButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> list_;
};

} // namespace EmailCleaner
