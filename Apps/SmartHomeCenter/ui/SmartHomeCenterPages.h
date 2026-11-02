// Apps/SmartHomeCenter/ui/SmartHomeCenterPages.h
// SmartHome Center's pages beyond Home, Devices, Energy and Activity (which
// the window builds itself): Rooms, a device's own page, Scenes, Settings and
// the first-run guide. Each is a class that builds its container once and
// refreshes it from SmartHomeAPI; the window owns them, shows one at a time
// and tells them when the home changed. What a page needs from the window -
// to open a device, change page, the window for dialogs, the devices as
// described - it asks through PageHost.
//
// Built from framework elements only: containers, labels, buttons, text
// inputs, dropdowns, checkboxes, list views, a line chart.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "SmartHomeCenterModel.h"

#include "UltraCanvasBadge.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
class UltraCanvasLineChartElement;
class ChartDataVector;
}

namespace SmartHomeCenter {

class DeviceControls;
class RoomDevicesModel;
class SceneActionsModel;

enum class Page { Home, Rooms, Devices, Scenes, Energy, Network, Activity, Settings, Device, FirstRun };

class PageHost {
public:
    virtual ~PageHost() = default;
    virtual void ShowPage(Page page) = 0;
    virtual void OpenDevice(const std::string& deviceId) = 0;
    virtual void GoBack() = 0;                       // to the page before the device's
    virtual void AddDevice() = 0;
    virtual void Note(const std::string& text) = 0;  // a line in Activity
    virtual UltraCanvas::UltraCanvasWindow* DialogParent() = 0;
    virtual const std::vector<DeviceView>& Devices() const = 0;
    virtual const std::string& IconsDir() const = 0;
    virtual bool IsDemoHome() const = 0;
};

// The radios this build carries and what each is doing, one row each:
// on, built in but not started (no adapter), or not in this build.
void FillRadioList(const std::shared_ptr<UltraCanvas::UltraCanvasContainer>& list, const std::string& idPrefix);

// ===== ROOMS =====
// The rooms down the left, with how many devices each has; the chosen
// room's devices on the right. Add, rename and delete rooms; move a device to
// another room; double-click one to open it.
class RoomsPage {
public:
    explicit RoomsPage(PageHost& host);
    ~RoomsPage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();
    void Refresh();        // the rooms or the devices in them changed
    void UpdateStates();   // states changed: the table's words
    std::string Subtitle() const;

private:
    void RebuildRoomList();
    void SelectRoom(const std::string& roomId);
    void AddRoom();
    void RenameRoom();
    void DeleteRoom();
    void MoveSelected();
    std::vector<DeviceView> Members() const;

    PageHost& host;
    std::string selectedRoom;   // a RoomId, or kNoRoomKey
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> roomList;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> roomTitle;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> roomDetail;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> renameButton;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> deleteButton;
    std::shared_ptr<RoomDevicesModel> model;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> list;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> moveTo;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> moveButton;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> openButton;
    std::string listSignature;   // the rooms and counts the list was built for
};

// ===== ONE DEVICE =====
// Everything about one device: its controls, its recorded history as a
// chart, every detail the backend gives, and its name, room and removal.
class DevicePage {
public:
    explicit DevicePage(PageHost& host);
    ~DevicePage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();
    void Show(const std::string& deviceId);
    void UpdateStates();   // the device's state changed
    void Tick();           // once a second while shown
    void RoomsChanged();
    const std::string& DeviceId() const { return deviceId; }

private:
    const DeviceView* View() const;
    void LoadHistory();
    void FillRooms();
    void SaveName();
    void Remove();

    PageHost& host;
    std::string deviceId;
    std::unique_ptr<DeviceControls> controls;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> badge;
    std::shared_ptr<UltraCanvas::UltraCanvasImageElement> icon;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> name;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> type;
    std::shared_ptr<UltraCanvas::UltraCanvasBadge> status;
    std::vector<std::pair<std::string, std::shared_ptr<UltraCanvas::UltraCanvasLabel>>> facts;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> historyTitle;
    std::shared_ptr<UltraCanvas::UltraCanvasLineChartElement> chart;
    std::shared_ptr<UltraCanvas::ChartDataVector> chartData;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> nameInput;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> roomChoice;
    std::vector<std::string> roomChoiceIds;   // by dropdown index; "" = no room
    bool fillingRooms = false;
    int ticks = 0;
};

// ===== SCENES =====
// A card per scene - run, edit, delete - and an editor beside them: a name,
// the actions as a list, and a row to add one (a device, what to do, a
// value). "Capture current state" starts a scene from the devices as they
// are now.
class ScenesPage {
public:
    explicit ScenesPage(PageHost& host);
    ~ScenesPage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();
    void Refresh();
    std::string Subtitle() const;

    // The commands that put `devices` back in their present state.
    static std::vector<UltraCanvas::SmartHome::SmartHomeCommand> CaptureState(const std::vector<DeviceView>& devices);
    // "Turn on", "Brightness 40 %", "Lock", "Target 21.5 °C" ...
    static std::string DescribeAction(const UltraCanvas::SmartHome::SmartHomeCommand& action);

private:
    void RebuildCards();
    void Edit(const UltraCanvas::SmartHome::SmartHomeScene& scene, bool isNew);
    void CloseEditor();
    void Save();
    void FillWhatChoices();
    void AddAction();
    void RemoveAction();

    PageHost& host;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> cards;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> editor;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> editorTitle;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> nameInput;
    std::shared_ptr<SceneActionsModel> actionsModel;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> actionsList;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> deviceChoice;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> whatChoice;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> valueInput;
    std::vector<std::string> deviceChoiceIds;
    UltraCanvas::SmartHome::SmartHomeScene editing;
    bool editingNew = false;
    std::string cardsSignature;
};

// ===== SETTINGS =====
// Where the home is (for sunrise and sunset), the home's data file and a
// backup of it, and which radios this build carries.
class SettingsPage {
public:
    explicit SettingsPage(PageHost& host);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();
    void Refresh();

private:
    void SaveLocation();
    void ExportBackup();

    PageHost& host;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> latitude;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> longitude;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> sunText;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> storeText;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> radios;
};

// ===== FIRST RUN =====
// Four steps the first time the window opens: welcome, where the home is,
// the rooms to start with (ticked from a suggested list), and the radios.
// Finish goes to Home; "Add a device" starts pairing.
class FirstRunPage {
public:
    explicit FirstRunPage(PageHost& host);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();
    void Start();

private:
    void ShowStep(int step);
    void Next();
    void Back();
    void Finish();

    PageHost& host;
    int step = 0;
    std::vector<std::shared_ptr<UltraCanvas::UltraCanvasContainer>> steps;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> stepText;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> backButton;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> nextButton;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> latitude;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> longitude;
    std::vector<std::pair<std::string, std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>>> roomBoxes;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> roomsHolder;
};

} // namespace SmartHomeCenter
