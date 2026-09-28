// Apps/UltraMail/ui/UltraMailContactsView.cpp
// Version: 0.4.0 - groups of the user's own, a filter, and the contact list
//                  as a virtualised UltraCanvasListView with a delete icon per
//                  row; section changes restyle the sidebar instead of
//                  rebuilding it under the click that asked for it.
// Last Modified: 2026-09-28
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailContactsView.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasUtils.h"
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
constexpr float kSidebarW     = 180.0f;
constexpr int   kRowH         = 46;
constexpr float kSectionRowH  = 24.0f;
constexpr float kDialogLabelW = 84.0f;
constexpr int   kDeleteZone   = 40;    // the bin icon's clickable strip, right edge
constexpr int   kAvatar       = 28;

// The first letter of a name, for the row avatar: the first UTF-8 character
// that is a letter or digit (non-ASCII characters count as letters).
std::string Initial(const std::string& name) {
    for (std::size_t i = 0; i < name.size();) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        std::size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3
                        : (c >> 3) == 0x1E ? 4 : 1;
        if (c < 0x80) {
            if (std::isalnum(c))
                return std::string(1, static_cast<char>(std::toupper(c)));
        } else if (len > 1 && i + len <= name.size()) {
            return name.substr(i, len);
        }
        i += len;
    }
    return "?";
}

std::string LowerAscii(std::string s) {
    for (char& ch : s)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    return s;
}

bool Matches(const Contact& c, const std::string& needle) {
    if (needle.empty()) return true;
    if (LowerAscii(c.displayName).find(needle) != std::string::npos) return true;
    if (LowerAscii(c.organization).find(needle) != std::string::npos) return true;
    for (const auto& e : c.emails)
        if (LowerAscii(e.address).find(needle) != std::string::npos) return true;
    for (const auto& p : c.phones)
        if (p.number.find(needle) != std::string::npos) return true;
    return false;
}

std::string SubLine(const Contact& c) {
    std::string sub = c.PrimaryEmail();
    if (!c.phones.empty()) {
        if (!sub.empty()) sub += "  \xC2\xB7  ";
        sub += c.phones.front().number;
    }
    if (!c.organization.empty()) {
        if (!sub.empty()) sub += "  \xC2\xB7  ";
        sub += c.organization;
    }
    return sub;
}

bool SameAddress(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// One contact per row: [initial] name / address · phone · organisation, and
// the bin icon at the right edge. Reads the view's filtered vector.
class ContactRowDelegate : public IItemDelegate {
public:
    const std::vector<Contact>* contacts = nullptr;
    std::string binIcon;
    int rowWidth = 0;   // as last painted: where the bin's strip starts

    void RenderItem(IRenderContext* ctx, const IListModel*, int row, int,
                    const ListItemStyleOption& option) override {
        if (!ctx || !contacts || row < 0 || row >= static_cast<int>(contacts->size())) return;
        const Contact& c = (*contacts)[static_cast<std::size_t>(row)];
        const Rect2Di& r = option.rect;
        rowWidth = r.width;

        // Avatar.
        const double ax = r.x + 12, ay = r.y + (r.height - kAvatar) / 2.0;
        ctx->DrawFilledRectangle(Rect2Dd(ax, ay, kAvatar, kAvatar), Theme::kAccentSoft,
                                 0.0f, Colors::Transparent, kAvatar * 0.25f);
        ctx->SetFontSize(kAvatar * 0.45f);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextWrap(TextWrap::WrapNone);
        ctx->SetTextAlignment(TextAlignment::Center);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->SetTextPaint(Theme::kAccent);
        ctx->DrawTextInRect(Initial(c.displayName), Rect2Dd(ax, ay, kAvatar, kAvatar));

        // Name and the line under it.
        const double tx = ax + kAvatar + 12;
        const double tw = r.x + r.width - kDeleteZone - tx - 4;
        if (tw > 0) {
            ctx->SetTextAlignment(TextAlignment::Left);
            ctx->SetFontSize(Theme::kSizeBody);
            ctx->SetTextPaint(Theme::kTextPrimary);
            ctx->DrawTextInRect(c.displayName.empty() ? std::string("(no name)") : c.displayName,
                                Rect2Dd(tx, r.y + 4, tw, r.height / 2.0 - 2));
            ctx->SetFontWeight(FontWeight::Normal);
            ctx->SetFontSize(Theme::kSizeSecondary);
            ctx->SetTextPaint(Theme::kTextSecondary);
            ctx->DrawTextInRect(SubLine(c), Rect2Dd(tx, r.y + r.height / 2.0, tw,
                                                    r.height / 2.0 - 4));
        }
        ctx->SetFontWeight(FontWeight::Normal);

        // Bin icon: muted, red on the hovered row.
        if (auto img = UCImage::Get(binIcon)) {
            const double side = 16;
            ctx->DrawMask(option.isHovered ? Theme::kTrustScam : Theme::kTextMuted, *img,
                          Rect2Dd(r.x + r.width - kDeleteZone + (kDeleteZone - side) / 2.0,
                                  r.y + (r.height - side) / 2.0, side, side),
                          ImageFitMode::Contain);
        }

        // Row divider.
        ctx->DrawFilledRectangle(Rect2Dd(r.x + 8, r.y + r.height - 1, r.width - 16, 1),
                                 Theme::kDivider, 0.0f, Colors::Transparent, 0.0f);
    }

    int GetRowHeight(const IListModel*, int) const override { return kRowH; }
};

// A sidebar entry: left click selects, right click asks for its menu.
class PlaceEntry : public UltraCanvasContainer {
public:
    PlaceEntry(const std::string& id, std::function<void()> onSelect,
               std::function<void(const UCEvent&)> onMenu)
        : UltraCanvasContainer(id, 0, 0, 0, 0),
          onSelect_(std::move(onSelect)), onMenu_(std::move(onMenu)) {}

    bool OnEvent(const UCEvent& event) override {
        if (!IsVisible() || IsDisabled()) return false;
        if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Left) {
            if (onSelect_) onSelect_();
            return true;
        }
        if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Right) {
            if (onMenu_) onMenu_(event);
            return true;
        }
        return UltraCanvasContainer::OnEvent(event);
    }

private:
    std::function<void()> onSelect_;
    std::function<void(const UCEvent&)> onMenu_;
};

// Run `task` on the UI thread after the current event has been handled - for
// work that replaces the element the event is still being delivered to.
void Later(std::function<void()> task) {
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->PostToUIThread(std::move(task));
    else task();
}

} // namespace

// ---- ContactsView ----------------------------------------------------------

std::shared_ptr<UltraCanvasContainer> ContactsView::Build() {
    // [sidebar | main]: the sidebar is a tinted column with a divider, the
    // main column carries the title row, the filter and the list.
    root_ = CreateContainer("contactsView", 0, 0, 0, 0);
    root_->layout.SetFlexRow()
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    sidebar_ = CreateContainer("contactsSidebar", 0, 0, kSidebarW, 0);
    sidebar_->SetBackgroundColor(Theme::kSidebar);
    sidebar_->SetBorders(0.0f, Colors::Transparent, 0.0f);
    sidebar_->SetPadding(Theme::kPagePadding, 10);
    sidebar_->layout.SetFlexColumn()
                    .SetFlexGap(Theme::kInnerGap)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    root_->AddChild(sidebar_);
    sidebar_->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    sidebarList_ = CreateContainer("contactsPlaces", 0, 0, 0, 0);
    sidebarList_->layout.SetFlexColumn()
                        .SetFlexGap(2)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    sidebar_->AddChild(sidebarList_);
    sidebarList_->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto addGroup = CreateButton("contactsAddGroup", 0, 0, 120, Theme::kControlHeight,
                                 "Add group");
    Theme::FitToLabel(addGroup, 120);
    Theme::StyleSecondary(addGroup);
    addGroup->onClick = [this]() { AskNewGroup(); };
    sidebar_->AddChild(addGroup);

    auto rule = CreateContainer("contactsRule", 0, 0, 1, 0);
    rule->SetBackgroundColor(Theme::kDivider);
    root_->AddChild(rule);
    rule->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto main = CreateContainer("contactsMain", 0, 0, 0, 0);
    main->SetPadding(Theme::kPagePadding);
    main->layout.SetFlexColumn()
                .SetFlexGap(Theme::kGap)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto titleRow = CreateContainer("contactsTitleRow", 0, 0, 0, Theme::kToolbarHeight);
    titleRow->layout.SetFlexRow()
                    .SetFlexGap(Theme::kInnerGap)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    titleLabel_ = Theme::MakeLine("contactsTitle", current_.Title(), Theme::kToolbarHeight,
                                  Theme::kSizeTitle, Theme::kTextPrimary, FontWeight::Bold);
    titleRow->AddChild(titleLabel_);
    titleLabel_->layoutItem.SetFlexGrow(1);
    auto addBtn = CreateButton("contactsAdd", 0, 0, 130, Theme::kControlHeight, "Add contact");
    Theme::FitToLabel(addBtn, 130);
    Theme::StylePrimary(addBtn);
    addBtn->onClick = [this]() {
        Contact c;
        if (current_.isGroup) c.group = current_.group;
        else                  c.section = current_.section;
        EditContact(c, /*isNew=*/true, Window());
    };
    titleRow->AddChild(addBtn);
    main->AddChild(titleRow);
    titleRow->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    filter_ = CreateTextInput("contactsFilter", 0, 0, 0, Theme::kControlHeight);
    filter_->SetPlaceholder("Filter by name, address, phone or organization");
    Theme::StyleInput(filter_);
    filter_->onTextChanged = [this](const std::string&) { ApplyFilter(); };
    main->AddChild(filter_);
    filter_->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    model_ = std::make_shared<UltraCanvasSimpleListModel>();
    list_ = std::make_shared<UltraCanvasListView>("contactsList");
    list_->SetModel(model_);
    list_->SetSelection(std::make_shared<UltraCanvasSingleSelection>());
    ListViewStyle st;
    st.backgroundColor          = Theme::kCardBackground;
    st.showHeader               = false;
    st.rowHeight                = kRowH;
    st.showGridLines            = false;
    st.selectionBackgroundColor = Theme::kRowSelected;
    st.hoverBackgroundColor     = Theme::kRowHover;
    list_->SetStyle(st);
    auto d = std::make_shared<ContactRowDelegate>();
    d->contacts = &shown_;
    d->binIcon  = NormalizePath(GetResourcesDir() + "media/icons/delete.svg");
    delegate_ = d;
    list_->SetDelegate(delegate_);
    list_->SetBorders(1.0f, Theme::kCardBorder, 8.0f);

    list_->onItemDoubleClicked = [this](int row) {
        if (row >= 0 && row < static_cast<int>(shown_.size()))
            EditContact(shown_[static_cast<std::size_t>(row)], /*isNew=*/false, Window());
    };
    list_->onItemActivated = list_->onItemDoubleClicked;
    list_->onCellClicked = [this](int row, int, const Point2Di& pos) {
        if (!list_ || row < 0 || row >= static_cast<int>(shown_.size())) return;
        auto* d = static_cast<ContactRowDelegate*>(delegate_.get());
        if (d && d->rowWidth > 0 && pos.x >= d->rowWidth - kDeleteZone) ConfirmDelete(shown_[static_cast<std::size_t>(row)]);
    };
    list_->onContextMenu = [this](int row, const UCEvent& event) { ShowRowMenu(row, event); };
    list_->tooltipProvider = [this](int row, int) -> std::string {
        if (row < 0 || row >= static_cast<int>(shown_.size())) return {};
        return "Double-click to edit \xC2\xB7 right-click for more \xC2\xB7 "
               "the bin deletes";
    };
    main->AddChild(list_);
    list_->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    root_->AddChild(main);
    main->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    Refresh();
    return root_;
}

void ContactsView::Release() {
    entries_.clear();
    menu_.reset();
    shown_.clear();
    contacts_.clear();
    list_.reset();
    model_.reset();
    delegate_.reset();
    filter_.reset();
    titleLabel_.reset();
    sidebarList_.reset();
    sidebar_.reset();
    root_.reset();
}

UltraCanvasWindowBase* ContactsView::Window() const {
    return root_ ? root_->GetWindow() : nullptr;
}

void ContactsView::Resize(float width, float height) {
    if (root_) root_->SetElementSize(Size2Df(width, height));
}

void ContactsView::Select(const ContactPlace& place) {
    current_ = place;
    if (titleLabel_) titleLabel_->SetText(place.Title());
    RestyleSidebar();
    LoadContacts();
}

void ContactsView::Refresh() {
    if (!root_) return;
    RebuildSidebar();
    LoadContacts();
}

void ContactsView::Changed() {
    Refresh();
    if (onChanged) onChanged();
}

void ContactsView::RebuildSidebar() {
    if (!sidebarList_ || !store_) return;
    sidebarList_->ClearChildren();
    entries_.clear();

    auto addHeader = [this](const std::string& id, const std::string& text) {
        auto header = Theme::MakeLine(id, text, 22, Theme::kSizeSecondary,
                                      Theme::kTextMuted, FontWeight::Bold);
        header->SetPadding(0, 10);
        sidebarList_->AddChild(header);
        header->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    };
    auto addEntry = [this](const std::string& id, const ContactPlace& place, int count) {
        auto entry = std::make_shared<PlaceEntry>(id,
            [this, place]() { Select(place); },
            [this, place](const UCEvent& e) { ShowPlaceMenu(place, e); });
        entry->SetElementSize(CSSLayout::Dimension::Auto(),
                              CSSLayout::Dimension::Px(kSectionRowH));
        entry->SetBorders(0.0f, Colors::Transparent, Theme::kControlRadius);
        entry->SetPadding(0, 10);
        entry->layout.SetFlexRow()
                     .SetFlexGap(Theme::kInnerGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto name = Theme::MakeText(id + ".name", place.Title(), Theme::kSizeBody,
                                    Theme::kTextPrimary);
        entry->AddChild(name);
        name->layoutItem.SetFlexGrow(1);
        auto num = Theme::MakeText(id + ".count", std::to_string(count),
                                   Theme::kSizeSecondary, Theme::kTextMuted);
        entry->AddChild(num);
        sidebarList_->AddChild(entry);
        entry->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        entries_.push_back({ place, entry, name, num });
    };

    addHeader("contactsHeader", "Contacts");
    std::vector<SectionCount> counts;
    store_->GetSectionCounts(counts);
    bool haveOther = false;
    for (const auto& sc : counts) {
        ContactPlace place; place.section = sc.section;
        addEntry("sect_" + ToString(sc.section), place, sc.count);
        if (sc.section == ContactSection::Other) haveOther = true;
    }
    // Other is listed while it is selected, even when it has just emptied.
    if (!haveOther && !current_.isGroup && current_.section == ContactSection::Other) {
        ContactPlace place; place.section = ContactSection::Other;
        addEntry("sect_other", place, 0);
    }

    std::vector<GroupCount> groups;
    store_->ListGroups(groups);
    bool currentExists = !current_.isGroup;
    if (!groups.empty()) addHeader("contactsGroupsHeader", "Groups");
    for (std::size_t i = 0; i < groups.size(); ++i) {
        ContactPlace place; place.isGroup = true; place.group = groups[i].name;
        if (place == current_) currentExists = true;
        addEntry("group_" + std::to_string(i), place, groups[i].count);
    }
    // The selected group was deleted: back to the first section.
    if (!currentExists) {
        current_ = ContactPlace{};
        current_.section = counts.empty() ? ContactSection::Friends : counts.front().section;
        if (titleLabel_) titleLabel_->SetText(current_.Title());
    }
    RestyleSidebar();
}

void ContactsView::RestyleSidebar() {
    for (auto& e : entries_) {
        const bool selected = e.place == current_;
        e.surface->SetBackgroundColor(selected ? Theme::kAccentSoft : Colors::Transparent);
        e.name->SetTextColor(selected ? Theme::kAccent : Theme::kTextPrimary);
        e.name->SetFontWeight(selected ? FontWeight::Bold : FontWeight::Normal);
        e.count->SetTextColor(selected ? Theme::kAccent : Theme::kTextMuted);
    }
}

void ContactsView::LoadContacts() {
    if (!store_) return;
    if (current_.isGroup) store_->ListByGroup(current_.group, contacts_);
    else                  store_->ListBySection(current_.section, contacts_);
    ApplyFilter();
}

void ContactsView::ApplyFilter() {
    if (!model_) return;
    const std::string needle = filter_ ? LowerAscii(filter_->GetText()) : std::string();
    shown_.clear();
    std::vector<ListItem> items;
    for (const auto& c : contacts_) {
        if (!Matches(c, needle)) continue;
        shown_.push_back(c);
        items.emplace_back(c.displayName);
    }
    model_->SetItems(items);
    if (list_) {
        list_->ResetSelection();
        list_->RequestRedraw();
    }
}

void ContactsView::ConfirmDelete(const Contact& contact) {
    const int64_t id = contact.id;
    const std::string name = contact.displayName.empty() ? contact.PrimaryEmail()
                                                         : contact.displayName;
    UltraCanvasAlert::Confirm("Delete \"" + name + "\" from the address book?",
                              "Delete contact",
        [this, id, name](bool yes) {
            if (!yes || !store_) return;
            if (UltraDbResult r = store_->Remove(id); !r)
                AlertError(Window(), "\"" + name + "\" could not be deleted.", DetailLine(r));
            Changed();
        },
        Window());
}

void ContactsView::ShowRowMenu(int row, const UCEvent& event) {
    UltraCanvasWindowBase* window = Window();
    if (!window || !store_ || row < 0 || row >= static_cast<int>(shown_.size())) return;
    const Contact c = shown_[static_cast<std::size_t>(row)];

    menu_ = std::make_shared<UltraCanvasMenu>("contacts.rowMenu", 0, 0, 190, 0);
    menu_->SetMenuType(MenuType::PopupMenu);
    menu_->AddItem(MenuItemData::Action("Edit", [this, c]() {
        EditContact(c, /*isNew=*/false, Window());
    }));

    // Move to: every section and group but the one it is in.
    std::vector<MenuItemData> targets;
    for (ContactSection s : { ContactSection::Family, ContactSection::Friends,
                              ContactSection::Work, ContactSection::Leisure,
                              ContactSection::Services, ContactSection::Other }) {
        if (c.group.empty() && c.section == s) continue;
        const int64_t id = c.id;
        targets.push_back(MenuItemData::Action(DisplayName(s), [this, id, s]() {
            if (store_ && store_->MoveToSection(id, s)) Changed();
        }));
    }
    std::vector<GroupCount> groups;
    store_->ListGroups(groups);
    if (!groups.empty()) targets.push_back(MenuItemData::Separator());
    for (const auto& g : groups) {
        if (g.name == c.group) continue;
        const int64_t id = c.id;
        const std::string name = g.name;
        targets.push_back(MenuItemData::Action(name, [this, id, name]() {
            if (store_ && store_->MoveToGroup(id, name)) Changed();
        }));
    }
    menu_->AddItem(MenuItemData::Submenu("Move to group", targets));
    menu_->AddItem(MenuItemData::Separator());
    menu_->AddItem(MenuItemData::Action("Delete", [this, c]() { ConfirmDelete(c); }));

    PopupElementSettings settings;
    menu_->OpenMenu(event.pointerWindow, *window, settings);
}

void ContactsView::ShowPlaceMenu(const ContactPlace& place, const UCEvent& event) {
    UltraCanvasWindowBase* window = Window();
    if (!window) return;
    menu_ = std::make_shared<UltraCanvasMenu>("contacts.placeMenu", 0, 0, 170, 0);
    menu_->SetMenuType(MenuType::PopupMenu);
    menu_->AddItem(MenuItemData::Action("Add group", [this]() { AskNewGroup(); }));
    if (place.isGroup) {
        const std::string name = place.group;
        menu_->AddItem(MenuItemData::Action("Delete group", [this, name]() {
            ConfirmDeleteGroup(name);
        }));
    } else {
        // The sections are built in: they cannot be deleted.
        MenuItemData item = MenuItemData::Action("Delete group", []() {});
        item.enabled = false;
        menu_->AddItem(item);
    }
    PopupElementSettings settings;
    menu_->OpenMenu(event.pointerWindow, *window, settings);
}

void ContactsView::AskNewGroup() {
    UltraCanvasDialogManager::ShowInputDialog(
        "Name of the new group:", "Add group", "", InputType::Text,
        [this](DialogResult result, const std::string& text) {
            if (result != DialogResult::OK || !store_) return;
            std::string name = text;
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
                name.pop_back();
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
                name.erase(name.begin());
            if (UltraDbResult r = store_->AddGroup(name); !r) {
                AlertWarning(Window(), "The group was not added.", r.message);
                return;
            }
            // Open the new group, after this dialog's event has been handled.
            Later([this, name]() {
                if (!root_) return;
                ContactPlace place; place.isGroup = true; place.group = name;
                current_ = place;
                if (titleLabel_) titleLabel_->SetText(place.Title());
                Refresh();
            });
        },
        Window());
}

void ContactsView::ConfirmDeleteGroup(const std::string& group) {
    UltraCanvasAlert::Confirm(
        "Delete the group \"" + group + "\"?\n\nIts contacts are kept: they go back to "
        "their sections.",
        "Delete group",
        [this, group](bool yes) {
            if (!yes || !store_) return;
            if (UltraDbResult r = store_->RemoveGroup(group); !r) {
                AlertError(Window(), "The group could not be deleted.", DetailLine(r));
                return;
            }
            Changed();
        },
        Window());
}

void ContactsView::EditContact(Contact contact, bool isNew, UltraCanvasWindowBase* parent,
                               std::function<void(const Contact&)> onSaved) {
    if (!store_) return;

    DialogConfig config;
    config.title      = isNew ? "Add contact" : "Edit contact";
    config.width      = 420;
    config.height     = 300;
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

    auto section = Theme::MakeLine("cSection",
                                   contact.group.empty()
                                       ? "Section: " + DisplayName(contact.section)
                                       : "Group: " + contact.group,
                                   22, Theme::kSizeSecondary, Theme::kTextMuted);
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
    Theme::FitToLabel(cancelBtn, 90);
    Theme::StyleSecondary(cancelBtn);
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);

    auto saveBtn = CreateButton("cSave", 0, 0, 100, Theme::kControlHeight, "Save");
    Theme::FitToLabel(saveBtn, 100);
    Theme::StylePrimary(saveBtn);
    saveBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(saveBtn);

    dialog->AddChild(buttonRow);

    // Capture by value; `contact` carries id + section through the callback.
    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [this, contact, name, email, phone, org, notes, parent, onSaved](DialogResult result) mutable {
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

            // The field edits the primary address only (PrimaryEmail(): the
            // one marked primary, else the first); every other address the
            // contact has stays. Rebuilding the list from the field alone
            // dropped them - including the one a message came from, when the
            // editor was opened for that message's sender.
            const std::string addr = email->GetText();
            std::size_t primaryIdx = 0;
            for (std::size_t i = 0; i < contact.emails.size(); ++i)
                if (contact.emails[i].primary) { primaryIdx = i; break; }
            std::vector<ContactEmail> kept;
            for (std::size_t i = 0; i < contact.emails.size(); ++i) {
                if (i == primaryIdx) continue;
                ContactEmail e = contact.emails[i];
                if (SameAddress(e.address, addr)) continue;   // now the primary
                e.primary = false;
                kept.push_back(e);
            }
            contact.emails.clear();
            if (!addr.empty()) {
                ContactEmail e; e.address = addr; e.primary = true;
                contact.emails.push_back(e);
            }
            contact.emails.insert(contact.emails.end(), kept.begin(), kept.end());

            const std::string num = phone->GetText();
            contact.phones.clear();
            if (!num.empty()) {
                ContactPhone p; p.number = num; p.label = "mobile";
                contact.phones.push_back(p);
            }

            if (UltraDbResult saved = store_->Save(contact); saved) {
                if (root_) Refresh();   // the Contacts window may never have been opened
                if (onSaved) onSaved(contact);
                else if (onChanged) onChanged();
            } else {
                AlertError(parent, "\"" + contact.displayName + "\" could not be "
                           "saved to the address book.", DetailLine(saved));
            }
        },
        parent);
}

} // namespace UltraMail
