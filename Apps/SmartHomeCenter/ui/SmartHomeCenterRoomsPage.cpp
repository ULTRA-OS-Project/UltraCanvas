// Apps/SmartHomeCenter/ui/SmartHomeCenterRoomsPage.cpp
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "SmartHomeCenterPages.h"
#include "SmartHomeCenterStyle.h"

#include "UltraCanvasListModel.h"
#include "UltraCanvasModalDialog.h"

#include <algorithm>

using namespace UltraCanvas;
using namespace UltraCanvas::SmartHome;

namespace SmartHomeCenter {

namespace {

// The list's entry for the devices in no room; not a RoomId any room has.
const char* const kNoRoomKey = "\x01no-room";

} // namespace

// The chosen room's devices: name, type, state.
class RoomDevicesModel : public IListModel {
public:
    void Set(std::vector<DeviceView> views) { rows = std::move(views); NotifyDataChanged(); }
    const DeviceView* At(int row) const { return row >= 0 && row < static_cast<int>(rows.size()) ? &rows[row] : nullptr; }
    int GetRowCount() const override { return static_cast<int>(rows.size()); }
    int GetColumnCount() const override { return 3; }
    ListColumnDef GetColumnDef(int column) const override {
        static const ListColumnDef columns[] = {{"Name", 220}, {"Type", 180}, {"State", 260}};
        return columns[std::clamp(column, 0, 2)];
    }
    ListDataValue GetData(const ListIndex& index, ListDataRole role) const override {
        const DeviceView* v = At(index.row);
        if (!v || role != ListDataRole::DisplayRole) return {};
        switch (index.column) {
            case 0: return v->info.Name;
            case 1: return v->typeName;
            case 2: return v->value.empty() ? v->state : v->value + " · " + v->state;
            default: return {};
        }
    }
    bool SetData(const ListIndex&, ListDataRole, const ListDataValue&) override { return false; }

private:
    std::vector<DeviceView> rows;
};

RoomsPage::RoomsPage(PageHost& h) : host(h) {}
RoomsPage::~RoomsPage() = default;

std::shared_ptr<UltraCanvasContainer> RoomsPage::Build() {
    auto page = Row("shcRoomsPage", 20, CSSLayout::AlignItems::Stretch);

    // Left: the rooms.
    auto left = Column("shcRoomsLeft", 10);
    Card(left, 14);
    Fixed(left, 270, -1);
    left->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    left->AddChild(Caption("shcRoomsCaption", "ROOMS"));
    roomList = ScrollColumn("shcRoomList", 4);
    Grow(roomList);
    left->AddChild(roomList);
    auto add = MakeButton("shcRoomAdd", "Add room", ButtonKind::Secondary, 38);
    add->SetIcon(host.IconsDir() + "plus.svg");
    add->SetIconSize(16, 16);
    add->SetIconSpacing(8);
    add->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    add->SetOnClick([this]() { AddRoom(); });
    left->AddChild(add);
    page->AddChild(left);

    // Right: the chosen room.
    auto right = Column("shcRoomRight", 12);
    Card(right, 16);
    Grow(right);
    auto head = Row("shcRoomHead", 10);
    Fixed(head, -1, 52);
    head->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto titles = Column("shcRoomTitles", 2);
    roomTitle = Text("shcRoomName", "", 20, kInk, true, 28);
    roomDetail = Text("shcRoomDetail", "", 12, kMuted, false, 18);
    titles->AddChild(roomTitle);
    titles->AddChild(roomDetail);
    Grow(titles);
    head->AddChild(titles);
    renameButton = MakeButton("shcRoomRename", "Rename", ButtonKind::Secondary, 36);
    renameButton->SetOnClick([this]() { RenameRoom(); });
    head->AddChild(renameButton);
    deleteButton = MakeButton("shcRoomDelete", "Delete room", ButtonKind::Danger, 36);
    deleteButton->SetOnClick([this]() { DeleteRoom(); });
    head->AddChild(deleteButton);
    right->AddChild(head);

    model = std::make_shared<RoomDevicesModel>();
    list = std::make_shared<UltraCanvasListView>("shcRoomDevices", 0, 0, 600, 400);
    list->SetModel(model);
    StyleTable(list);
    list->SetShowHeader(true);
    list->SetRowHeight(34);
    list->onItemActivated = [this](int row) {
        if (const DeviceView* v = model->At(row)) host.OpenDevice(v->info.DeviceId);
    };
    Grow(list);
    right->AddChild(list);

    // Move a device: a dropdown of the other rooms rather than a drag, so it
    // works from the keyboard as well.
    auto move = Row("shcRoomMove", 10);
    Fixed(move, -1, 38);
    move->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    auto moveLabel = Text("shcRoomMoveLabel", "Move selected to", 13, kMuted, false, 20);
    Fixed(moveLabel, 150, 20);
    move->AddChild(moveLabel);
    moveTo = std::make_shared<UltraCanvasDropdown>("shcRoomMoveTo", 220, 34);
    Fixed(moveTo, 220, 34);
    move->AddChild(moveTo);
    moveButton = MakeButton("shcRoomMoveGo", "Move", ButtonKind::Primary, 36);
    moveButton->SetOnClick([this]() { MoveSelected(); });
    move->AddChild(moveButton);
    move->AddChild(Spacer("shcRoomMoveSpace"));
    openButton = MakeButton("shcRoomOpen", "Open device", ButtonKind::Secondary, 36);
    openButton->SetOnClick([this]() {
        if (const DeviceView* v = model->At(list->GetSelection() ? list->GetSelection()->GetCurrentRow() : -1))
            host.OpenDevice(v->info.DeviceId);
    });
    move->AddChild(openButton);
    right->AddChild(move);
    page->AddChild(right);
    return page;
}

std::vector<DeviceView> RoomsPage::Members() const {
    std::vector<DeviceView> members;
    for (const DeviceView& v : host.Devices()) {
        const bool inIt = selectedRoom == kNoRoomKey ? v.info.RoomId.empty() : v.info.RoomId == selectedRoom;
        if (inIt) members.push_back(v);
    }
    return members;
}

void RoomsPage::Refresh() {
    auto& api = SmartHomeAPI::Instance();
    const auto rooms = api.GetRooms();
    // Keep the choice while it exists; otherwise the first room.
    bool exists = selectedRoom == kNoRoomKey;
    for (const auto& r : rooms) exists = exists || r.RoomId == selectedRoom;
    if (!exists) selectedRoom = rooms.empty() ? kNoRoomKey : rooms.front().RoomId;
    RebuildRoomList();
    SelectRoom(selectedRoom);
}

void RoomsPage::RebuildRoomList() {
    const auto rooms = SmartHomeAPI::Instance().GetRooms();
    auto count = [this](const std::string& roomId) {
        int n = 0;
        for (const DeviceView& v : host.Devices()) n += (roomId == kNoRoomKey ? v.info.RoomId.empty() : v.info.RoomId == roomId) ? 1 : 0;
        return n;
    };
    struct Entry { std::string id, name; int devices; };
    std::vector<Entry> entries;
    for (const auto& r : rooms) entries.push_back({r.RoomId, r.Name, count(r.RoomId)});
    const int unplaced = count(kNoRoomKey);
    if (unplaced > 0 || selectedRoom == kNoRoomKey) entries.push_back({kNoRoomKey, kNoRoom, unplaced});

    std::string signature = selectedRoom + "|";
    for (const auto& e : entries) signature += e.id + "=" + e.name + ":" + std::to_string(e.devices) + ";";
    if (signature == listSignature) return;
    listSignature = signature;

    roomList->ClearChildren();
    int index = 0;
    for (const auto& e : entries) {
        const std::string key = std::to_string(index++);
        const bool selected = e.id == selectedRoom;
        auto row = Row("shcRoomEntry_" + key, 8);
        Fixed(row, -1, 38);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        row->SetPadding(0, 12);
        row->SetBorderRadius(10);
        row->SetBackgroundColor(selected ? kAccentSoft : kSurface);
        row->SetMouseCursor(UCMouseCursor::Hand);
        auto label = Text("shcRoomEntryName_" + key, e.name, 14, selected ? kAccent : kInk, selected, 20);
        Grow(label);
        label->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        row->AddChild(label);
        auto n = Text("shcRoomEntryCount_" + key, std::to_string(e.devices), 12, kMuted, false, 20);
        n->SetAlignment(TextAlignment::Right);
        Fixed(n, 30, 20);
        row->AddChild(n);
        const std::string id = e.id;
        row->SetEventCallback([this, id](const UCEvent& event) {
            if (event.type != UCEventType::MouseDown) return false;
            SelectRoom(id);
            return true;
        });
        roomList->AddChild(row);
    }
    roomList->InvalidateLayout();
}

void RoomsPage::SelectRoom(const std::string& roomId) {
    selectedRoom = roomId;
    RebuildRoomList();
    const bool none = roomId == kNoRoomKey;
    const SmartHomeRoom room = none ? SmartHomeRoom{} : SmartHomeAPI::Instance().GetRoom(roomId);
    const std::vector<DeviceView> members = Members();
    roomTitle->SetText(none ? std::string(kNoRoom) : room.Name);
    int active = 0;
    for (const DeviceView& v : members) active += v.active ? 1 : 0;
    std::string detail = std::to_string(members.size()) + (members.size() == 1 ? " device" : " devices");
    if (active > 0) detail += " · " + std::to_string(active) + " active";
    if (!room.Floor.empty()) detail += " · " + room.Floor;
    if (members.empty()) detail += " · move devices here from another room";
    roomDetail->SetText(detail);
    renameButton->SetVisible(!none);
    deleteButton->SetVisible(!none);
    model->Set(members);

    // Where the selected device can go: every other room, and "no room".
    moveTo->ClearItems();
    for (const auto& r : SmartHomeAPI::Instance().GetRooms())
        if (r.RoomId != roomId) moveTo->AddItem(r.Name, r.RoomId);
    if (!none) moveTo->AddItem(kNoRoom, "");
    if (moveTo->GetItemCount() > 0) moveTo->SetSelectedIndex(0, false);
    moveButton->SetDisabled(members.empty() || moveTo->GetItemCount() == 0);
    openButton->SetDisabled(members.empty());
}

void RoomsPage::UpdateStates() {
    // Counts move when a device changes room elsewhere (its page, another
    // window); states change all the time.
    RebuildRoomList();
    const std::vector<DeviceView> members = Members();
    if (static_cast<int>(members.size()) != model->GetRowCount()) {
        SelectRoom(selectedRoom);
        return;
    }
    model->Set(members);
}

std::string RoomsPage::Subtitle() const {
    const size_t rooms = SmartHomeAPI::Instance().GetRooms().size();
    return std::to_string(rooms) + (rooms == 1 ? " room" : " rooms") +
           "  ·  pick one to see its devices, double-click a device to open it";
}

void RoomsPage::AddRoom() {
    UltraCanvasDialogManager::ShowInputDialog(
        "Name of the new room", "Add room", "", InputType::Text,
        [this](DialogResult result, const std::string& text) {
            if (result != DialogResult::OK || text.empty()) return;
            std::string id;
            if (!SmartHomeAPI::Instance().CreateRoom({"", text, "", "", 0}, &id)) {
                UltraCanvasDialogManager::ShowWarning("There is a room called \"" + text + "\" already.", "Add room",
                                                      nullptr, host.DialogParent());
                return;
            }
            host.Note("Added the room \"" + text + "\"");
            selectedRoom = id;
            listSignature.clear();
            Refresh();
        },
        host.DialogParent());
}

void RoomsPage::RenameRoom() {
    SmartHomeRoom room = SmartHomeAPI::Instance().GetRoom(selectedRoom);
    if (room.RoomId.empty()) return;
    const std::string old = room.Name;
    UltraCanvasDialogManager::ShowInputDialog(
        "New name for \"" + old + "\"", "Rename room", old, InputType::Text,
        [this, room, old](DialogResult result, const std::string& text) mutable {
            if (result != DialogResult::OK || text.empty() || text == old) return;
            room.Name = text;
            if (!SmartHomeAPI::Instance().UpdateRoom(room)) {
                UltraCanvasDialogManager::ShowWarning("There is a room called \"" + text + "\" already.", "Rename room",
                                                      nullptr, host.DialogParent());
                return;
            }
            host.Note("Renamed the room \"" + old + "\" to \"" + text + "\"");
            listSignature.clear();
            Refresh();
        },
        host.DialogParent());
}

void RoomsPage::DeleteRoom() {
    const SmartHomeRoom room = SmartHomeAPI::Instance().GetRoom(selectedRoom);
    if (room.RoomId.empty()) return;
    const size_t members = Members().size();
    const std::string question = members == 0
        ? "Delete the room \"" + room.Name + "\"?"
        : "Delete the room \"" + room.Name + "\"? Its " + std::to_string(members) +
              (members == 1 ? " device stays, in no room." : " devices stay, in no room.");
    UltraCanvasDialogManager::ShowConfirmation(question, "Delete room", [this, room](bool confirmed) {
        if (!confirmed) return;
        if (SmartHomeAPI::Instance().DeleteRoom(room.RoomId)) host.Note("Deleted the room \"" + room.Name + "\"");
        selectedRoom.clear();
        listSignature.clear();
        Refresh();
    }, host.DialogParent());
}

void RoomsPage::MoveSelected() {
    const int row = list->GetSelection() ? list->GetSelection()->GetCurrentRow() : -1;
    const DeviceView* v = model->At(row);
    const DropdownItem* to = moveTo->GetSelectedItem();
    if (!v || !to) {
        UltraCanvasDialogManager::ShowInformation("Pick a device in the list first.", "Move device", nullptr,
                                                  host.DialogParent());
        return;
    }
    const std::string name = v->info.Name;
    if (SmartHomeAPI::Instance().AssignDeviceToRoom(v->info.DeviceId, to->value))
        host.Note("Moved " + name + " to " + (to->value.empty() ? std::string("no room") : to->text));
    // The registry's update comes back through the window; the table
    // follows at once here.
    listSignature.clear();
    SelectRoom(selectedRoom);
}

} // namespace SmartHomeCenter
