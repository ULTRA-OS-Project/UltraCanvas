// Apps/SmartHomeCenter/ui/SmartHomeCenterWindow.h
// SmartHome Center's window: a navigation column on the left (Home, Rooms,
// Devices, Scenes, Energy, Activity, Settings, and the demo-home panel when
// the home is simulated), a header with the page title and Add device, and
// the page itself.
//
// Home is the everyday page: four figures across the top (inside temperature,
// lights, locks, power), the scenes, then every room with a tile per device -
// a switch right on the tile for anything that switches, the value for
// anything that measures - and on the right an inspector with the selected
// device's controls. Rooms, a device's own page, Scenes, Settings and the
// first-run guide are classes of their own (SmartHomeCenterPages.h); Devices
// is the home as a table, Energy the metered plugs, Activity what happened.
//
// Built from framework elements only (containers, labels, buttons, switches,
// sliders, a segmented control, badges, list views, charts); the tiles are
// containers of those, not painted.
//
// SmartHomeAPI reports changes on whichever thread caused them (the command
// worker, the backend's clock). The callbacks only note which device changed;
// a UI timer reads those devices back four times a second and updates their
// tiles in place, so a slider being dragged is never rebuilt under the
// pointer and a burst of changes costs one refresh.
// Version: 0.2.0 - Rooms, a page per device, Scenes, Settings, first run
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "SmartHomeCenterModel.h"
#include "SmartHomeCenterPages.h"

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

namespace UltraCanvas {
class UltraCanvasBarChartElement;
class ChartDataVector;
namespace SmartHome { class SmartHomeNetworkTopology; }
}

namespace SmartHomeCenter {

class DevicesTableModel;
class ActivityTableModel;
class DeviceControls;

// One line of the Activity page.
struct ActivityEntry {
    int64_t time = 0;           // seconds since the epoch
    std::string deviceName;
    std::string room;
    std::string text;
    bool alert = false;
};

class SmartHomeCenterWindow : public PageHost {
public:
    SmartHomeCenterWindow();
    ~SmartHomeCenterWindow() override;

    // `demoHome`: the home is the simulated one, so show the demo panel.
    // `firstRun`: the home's data file is new, so open on the guide.
    bool Initialize(bool demoHome, bool firstRun);
    void Show();

    // For --page / --select on the command line (screenshots, demos).
    void SelectDevice(const std::string& deviceId);
    std::string FindDeviceByName(const std::string& name) const;

    // ===== PageHost =====
    void ShowPage(Page page) override;
    void OpenDevice(const std::string& deviceId) override;
    void GoBack() override;
    void AddDevice() override;
    void Note(const std::string& text) override;
    UltraCanvas::UltraCanvasWindow* DialogParent() override { return window_.get(); }
    const std::vector<DeviceView>& Devices() const override { return views_; }
    const std::string& IconsDir() const override { return iconsDir_; }
    bool IsDemoHome() const override { return demoHome_; }

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
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildNetworkPage();
    void RebuildScenesRow();
    Stat MakeStat(const std::string& id, const std::string& caption, const std::string& icon);
    Tile MakeTile(const DeviceView& view);

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
    Page backPage_ = Page::Home;      // where Back on a device's page goes

    // ===== THE OTHER PAGES =====
    std::unique_ptr<RoomsPage> roomsPage_;
    std::unique_ptr<DevicePage> devicePage_;
    std::unique_ptr<ScenesPage> scenesPage_;
    std::unique_ptr<SettingsPage> settingsPage_;
    std::unique_ptr<FirstRunPage> firstRunPage_;

    // ===== HOME =====
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> scenesRow_;
    std::string scenesRowSignature_;
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
    std::unique_ptr<DeviceControls> inspectorControls_;
    std::vector<std::pair<std::string, std::shared_ptr<UltraCanvas::UltraCanvasLabel>>> inspectorFacts_;
    std::string selectedId_;
    bool updating_ = false;             // writing values into the tiles' switches: ignore their echoes

    // ===== DEVICES / ENERGY / ACTIVITY =====
    std::shared_ptr<DevicesTableModel> devicesModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> devicesList_;
    Stat energyNowStat_, energyTotalStat_, energyTopStat_;
    // One bar per metered plug, live (horizontal: the names are long).
    std::shared_ptr<UltraCanvas::UltraCanvasBarChartElement> energyChart_;
    std::shared_ptr<UltraCanvas::ChartDataVector> energyData_;
    std::string energyChartKey_;                      // what the chart shows now
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> energyNoteText_;   // says how many idle plugs are left out
    std::shared_ptr<ActivityTableModel> activityModel_;

    // ===== NETWORK =====
    // The module's own map of the radios (SmartHomeNetworkTopology): a card
    // per device, linked to a router of its radio. Reloaded when the page is
    // shown, and while shown when devices come or go.
    std::shared_ptr<UltraCanvas::SmartHome::SmartHomeNetworkTopology> topology_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> networkHint_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> networkOpen_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> activityList_;
    std::deque<ActivityEntry> activity_;          // newest first

    // ===== STATE (UI thread) =====
    std::vector<DeviceView> views_;
    std::map<std::string, std::string> eventKeys_;   // last logged state, by id
    std::string roomsSignature_;                      // ids + rooms the Home page was built for
    bool demoHome_ = false;
    int ticks_ = 0;
    UltraCanvas::TimerId uiTimer_ = 0;
    SmartHomeListenerId listenerId_ = 0;
    std::string iconsDir_;

    // ===== FROM API THREADS =====
    std::mutex pendingMutex_;
    std::set<std::string> pendingIds_;   // guarded by pendingMutex_
    bool pendingAll_ = true;             // guarded: the device set changed
    bool pendingHome_ = false;           // guarded: the rooms changed
    std::vector<std::string> pendingNotes_;   // guarded: pairing and errors, for the log
    std::vector<UltraCanvas::SmartHome::SmartHomeEvent> pendingEvents_;   // guarded: the engine's log
};

} // namespace SmartHomeCenter
