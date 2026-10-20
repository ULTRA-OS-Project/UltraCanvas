// Apps/SmartHomeCenter/ui/SmartHomeCenterWindow.h
// SmartHome Center's window: a navigation column on the left (Home, Devices,
// Energy, Activity, and the demo-home panel when the home is simulated), a
// header with the page title and Add device, and the page itself.
//
// Home is the everyday page: four figures across the top (inside temperature,
// lights, locks, power), the scenes, then every room with a tile per device -
// a switch right on the tile for anything that switches, the value for
// anything that measures - and on the right an inspector with the selected
// device's full controls and details. Devices is the same home as a table,
// Energy the metered plugs, Activity what happened and when.
//
// Built from framework elements only (containers, labels, buttons, switches,
// sliders, a segmented control, badges, list views, a bar chart); the tiles
// are containers of those, not painted.
//
// SmartHomeAPI reports changes on whichever thread caused them (the command
// worker, the backend's clock). The callbacks only note which device changed;
// a UI timer reads those devices back four times a second and updates their
// tiles in place, so a slider being dragged is never rebuilt under the
// pointer and a burst of changes costs one refresh.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "SmartHomeCenterModel.h"

#include "UltraCanvasBadge.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasWindow.h"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace UltraCanvas { class UltraCanvasBarChartElement; class ChartDataVector; }

namespace SmartHomeCenter {

class DevicesTableModel;
class ActivityTableModel;

enum class Page { Home, Devices, Energy, Activity };

// One line of the Activity page.
struct ActivityEntry {
    int64_t time = 0;           // seconds since the epoch
    std::string deviceName;
    std::string room;
    std::string text;
    bool alert = false;
};

class SmartHomeCenterWindow {
public:
    ~SmartHomeCenterWindow();

    // `demoHome`: the home is the simulated one, so show the demo panel.
    bool Initialize(bool demoHome);
    void Show();

    // For --page / --select on the command line (screenshots, demos).
    void ShowPage(Page page);
    void SelectDevice(const std::string& deviceId);
    std::string FindDeviceByName(const std::string& name) const;

private:
    // A device's tile on the Home page.
    struct Tile {
        std::shared_ptr<UltraCanvas::UltraCanvasContainer> card;
        std::shared_ptr<UltraCanvas::UltraCanvasContainer> badge;
        std::shared_ptr<UltraCanvas::UltraCanvasImageElement> icon;
        std::shared_ptr<UltraCanvas::UltraCanvasLabel> name;
        std::shared_ptr<UltraCanvas::UltraCanvasLabel> state;
        std::shared_ptr<UltraCanvas::UltraCanvasLabel> value;    // measuring devices
        std::shared_ptr<UltraCanvas::UltraCanvasSwitch> toggle;  // switching devices
        std::string iconName;
    };

    // One of the figures across the top of Home and Energy.
    struct Stat {
        std::shared_ptr<UltraCanvas::UltraCanvasContainer> card;
        std::shared_ptr<UltraCanvas::UltraCanvasContainer> badge;
        std::shared_ptr<UltraCanvas::UltraCanvasImageElement> icon;
        std::shared_ptr<UltraCanvas::UltraCanvasLabel> value;
        std::shared_ptr<UltraCanvas::UltraCanvasLabel> detail;
    };

    // ===== CONSTRUCTION =====
    void LayoutForSize(float width, float height);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildSidebar();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildDemoPanel();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildHeader();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildHomePage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildInspector();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildDevicesPage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildEnergyPage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildActivityPage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildScenesRow();
    Stat MakeStat(const std::string& id, const std::string& caption, const std::string& icon);
    Tile MakeTile(const DeviceView& view);
    void CreateDemoScenes();
    void CreateDemoAutomations();

    // ===== DATA FLOW =====
    void ConnectApi();
    void OnTimer();                                  // UI thread
    void ReloadAll();                                // the device set changed
    void ApplyChanged(const std::set<std::string>& ids);
    void RebuildRooms();
    void UpdateRoomCounts();
    void UpdateTile(Tile& tile, const DeviceView& view);
    void UpdateSummary();
    void UpdateDevicesTable();
    void UpdateEnergy();
    void NoteEvent(const DeviceView& view, bool initial);
    void Log(const std::string& device, const std::string& room, const std::string& text, bool alert);

    // ===== INSPECTOR =====
    void RebuildInspector();
    void UpdateInspector();
    const DeviceView* Selected() const;

    // ===== ACTIONS =====
    void ToggleDevice(const std::string& id, bool on);
    void AddDevice();
    void ActivateScene(const std::string& sceneId, const std::string& name);
    void ShowAbout();

    // ===== SHELL =====
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> content_;
    std::map<Page, std::shared_ptr<UltraCanvas::UltraCanvasButton>> navButtons_;
    std::map<Page, std::shared_ptr<UltraCanvas::UltraCanvasContainer>> pages_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> pageTitle_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> pageSubtitle_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> addButton_;
    Page page_ = Page::Home;

    // ===== HOME =====
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> roomsColumn_;   // scrolls
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> roomsHolder_;   // the rooms, rebuilt
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> alertBanner_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> alertText_;
    Stat climateStat_, lightsStat_, securityStat_, powerStat_;
    std::map<std::string, Tile> tiles_;                                // by device id
    std::vector<std::pair<std::string, std::shared_ptr<UltraCanvas::UltraCanvasLabel>>> roomCounts_;

    // ===== INSPECTOR =====
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> inspector_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> inspectorBody_;  // rebuilt per device
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> inspectorBadge_;
    std::shared_ptr<UltraCanvas::UltraCanvasImageElement> inspectorIcon_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> inspectorName_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> inspectorType_;
    std::shared_ptr<UltraCanvas::UltraCanvasBadge> inspectorStatus_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> inspectorValue_;
    std::shared_ptr<UltraCanvas::UltraCanvasSwitch> inspectorToggle_;
    std::shared_ptr<UltraCanvas::UltraCanvasSlider> inspectorLevel_;     // brightness / position
    std::shared_ptr<UltraCanvas::UltraCanvasSlider> inspectorWarmth_;    // colour temperature
    std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl> inspectorMode_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> inspectorTarget_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> inspectorAction_;    // lock / unlock
    std::vector<std::pair<std::string, std::shared_ptr<UltraCanvas::UltraCanvasLabel>>> inspectorFacts_;
    std::string selectedId_;
    std::string inspectorBuiltFor_;     // id + kind the body was built for
    float pendingTarget_ = 0.0f;        // thermostat target being stepped
    bool updating_ = false;             // writing values into controls: ignore their echoes

    // ===== DEVICES / ENERGY / ACTIVITY =====
    std::shared_ptr<DevicesTableModel> devicesModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> devicesList_;
    Stat energyNowStat_, energyTotalStat_, energyTopStat_;
    // One bar per metered plug, live (horizontal: the names are long).
    std::shared_ptr<UltraCanvas::UltraCanvasBarChartElement> energyChart_;
    std::shared_ptr<UltraCanvas::ChartDataVector> energyData_;
    std::string energyChartKey_;                      // what the chart shows now
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> energyNoteText_;   // says how many idle plugs are left out
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> energyIdle_;   // in place of the chart when every plug is idle
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> energyIdleText_;
    std::shared_ptr<ActivityTableModel> activityModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> activityList_;
    std::deque<ActivityEntry> activity_;          // newest first

    // ===== STATE (UI thread) =====
    std::vector<DeviceView> views_;
    std::map<std::string, std::string> eventKeys_;   // last logged state, by id
    std::string roomsSignature_;                      // ids + rooms the Home page was built for
    bool demoHome_ = false;
    int ticks_ = 0;
    UltraCanvas::TimerId uiTimer_ = 0;
    std::string iconsDir_;

    // ===== FROM API THREADS =====
    std::mutex pendingMutex_;
    std::set<std::string> pendingIds_;   // guarded by pendingMutex_
    bool pendingAll_ = true;             // guarded: the device set changed
    std::vector<std::string> pendingNotes_;   // guarded: pairing and errors, for the log
    std::vector<UltraCanvas::SmartHome::SmartHomeEvent> pendingEvents_;   // guarded: the engine's log
};

} // namespace SmartHomeCenter
