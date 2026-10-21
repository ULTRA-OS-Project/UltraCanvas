// UltraCanvasSmartHomeDeviceControl.h
// Smart Home Device Control Widgets
// Version: 2.0.0 - the controls are containers of framework elements (proposal §4.8)
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasSmartHome.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasColorSwatchBar.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasSpinner.h"
#include "UltraCanvasSwitch.h"
#include "Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h"
#include <memory>
#include <functional>

namespace UltraCanvas {
class UltraCanvasLineChartElement;
class ChartDataVector;

namespace SmartHome {

// The five device controls are UltraCanvasContainers of framework elements
// (proposal §4.8): a switch, sliders, buttons, a segmented control, a
// spinner, a colour picker and swatch bar, a gauge, a list view. Each still
// drives its device through SmartHomeAPI and reports through its own
// callback, and keeps the public API it had while it painted its controls.
// The picture of a blind in its window takes no input and is a small view
// element inside the layout; a sensor's history is a line chart.

class SmartHomeBlindPreview;

// How far apart the top and bottom of a sensor's history are drawn at least:
// 2 °C, 10 % humidity, 20 W, 50 lx, 1 for the rest. Without a floor a room
// steady at 20.6-20.7 °C fills the whole graph and reads as a sawtooth.
double SensorHistoryMinimumSpan(SmartHomeSensorType type);

/**
 * @brief Light control widget
 *
 * A switch for on/off, sliders for brightness and colour temperature, a
 * colour picker for RGB lights and a row of colour presets.
 */
class SmartHomeLightControl : public UltraCanvasContainer {
public:
    SmartHomeLightControl(const std::string& identifier, float x, float y, float w, float h);
    SmartHomeLightControl(const std::string& identifier, float w, float h)
        : SmartHomeLightControl(identifier, -1, -1, w, h) {}
    explicit SmartHomeLightControl(const std::string& identifier)
        : SmartHomeLightControl(identifier, -1, -1, -1, -1) {}
    virtual ~SmartHomeLightControl();

    void SetDevice(const std::string& deviceId);
    std::string GetDevice() const { return deviceId; }

    void SetState(const SmartHomeLightState& state);
    SmartHomeLightState GetState() const { return currentState; }

    void SetBrightnessEnabled(bool enable);
    void SetColorTempEnabled(bool enable);
    void SetColorEnabled(bool enable);

    /**
     * @brief Set color temperature range (Kelvin)
     * @param min Minimum (warm, e.g. 2700K)
     * @param max Maximum (cool, e.g. 6500K)
     */
    void SetColorTempRange(uint16_t min, uint16_t max);

    /**
     * @brief Add color preset (a swatch in the preset row)
     */
    void AddColorPreset(const std::string& name, uint8_t r, uint8_t g, uint8_t b);

    void SetOnStateChange(std::function<void(const SmartHomeLightState&)> callback) {
        onStateChange = callback;
    }

    // The elements, for a host that styles or drives them (and for tests).
    UltraCanvasSwitch* GetPowerSwitch() const { return powerSwitch.get(); }
    UltraCanvasSlider* GetBrightnessSlider() const { return brightness.get(); }
    UltraCanvasSlider* GetColorTempSlider() const { return colorTemp.get(); }
    UltraCanvasColorPicker* GetColorPicker() const { return colorPicker.get(); }
    UltraCanvasColorSwatchBar* GetPresetBar() const { return presetBar.get(); }

private:
    void Sync();          // the state into the elements, without echoes
    void SendState();
    void PickColor(const Color& c);

    struct ColorPreset {
        std::string Name;
        uint8_t R, G, B;
    };

    std::string deviceId;
    SmartHomeLightState currentState;
    uint16_t colorTempMin = 2700;
    uint16_t colorTempMax = 6500;
    std::vector<ColorPreset> colorPresets;
    bool syncing = false;

    std::shared_ptr<UltraCanvasSwitch> powerSwitch;
    std::shared_ptr<UltraCanvasLabel> powerLabel;
    std::shared_ptr<UltraCanvasContainer> brightnessRow;
    std::shared_ptr<UltraCanvasLabel> brightnessValue;
    std::shared_ptr<UltraCanvasSlider> brightness;
    std::shared_ptr<UltraCanvasContainer> colorTempRow;
    std::shared_ptr<UltraCanvasLabel> colorTempValue;
    std::shared_ptr<UltraCanvasSlider> colorTemp;
    std::shared_ptr<UltraCanvasColorPicker> colorPicker;
    std::shared_ptr<UltraCanvasColorSwatchBar> presetBar;

    std::function<void(const SmartHomeLightState&)> onStateChange;
};

/**
 * @brief Thermostat control widget
 *
 * A gauge with the target and the room's temperature, a spinner for the
 * target, and a segmented control for the mode.
 */
class SmartHomeThermostatControl : public UltraCanvasContainer {
public:
    SmartHomeThermostatControl(const std::string& identifier, float x, float y, float w, float h);
    SmartHomeThermostatControl(const std::string& identifier, float w, float h)
        : SmartHomeThermostatControl(identifier, -1, -1, w, h) {}
    explicit SmartHomeThermostatControl(const std::string& identifier)
        : SmartHomeThermostatControl(identifier, -1, -1, -1, -1) {}
    virtual ~SmartHomeThermostatControl();

    void SetDevice(const std::string& deviceId);
    std::string GetDevice() const { return deviceId; }

    void SetState(const SmartHomeThermostatState& state);
    SmartHomeThermostatState GetState() const { return currentState; }

    /**
     * @brief Set temperature unit
     * @param celsius true for Celsius, false for Fahrenheit
     */
    void SetCelsius(bool celsius);

    /**
     * @brief Set temperature range (°C)
     */
    void SetTemperatureRange(float min, float max);

    /**
     * @brief Set supported modes (heat, cool, auto, off, ...)
     */
    void SetSupportedModes(const std::vector<std::string>& modes);

    void SetOnStateChange(std::function<void(const SmartHomeThermostatState&)> callback) {
        onStateChange = callback;
    }

    // The wheel over the control steps the target half a degree a notch.
    bool OnEvent(const UCEvent& event) override;

    UltraCanvasGaugeDiagramElement* GetGauge() const { return gauge.get(); }
    UltraCanvasSpinner* GetTargetSpinner() const { return target.get(); }
    UltraCanvasSegmentedControl* GetModeSelector() const { return modes.get(); }

private:
    void Sync();
    void SendState();
    void AdjustTarget(float delta);
    float CelsiusToFahrenheit(float c) const;
    float FahrenheitToCelsius(float f) const;

    std::string deviceId;
    SmartHomeThermostatState currentState;
    bool useCelsius = true;
    float tempMin = 10.0f;
    float tempMax = 35.0f;
    std::vector<std::string> supportedModes;
    bool syncing = false;

    std::shared_ptr<UltraCanvasGaugeDiagramElement> gauge;
    std::shared_ptr<UltraCanvasLabel> nowLabel;
    std::shared_ptr<UltraCanvasSpinner> target;
    std::shared_ptr<UltraCanvasSegmentedControl> modes;

    std::function<void(const SmartHomeThermostatState&)> onStateChange;
};

/**
 * @brief Lock control widget
 *
 * A lock / unlock button, the lock's state and battery, and its recent
 * activity, newest first.
 */
class SmartHomeLockControl : public UltraCanvasContainer {
public:
    SmartHomeLockControl(const std::string& identifier, float x, float y, float w, float h);
    SmartHomeLockControl(const std::string& identifier, float w, float h)
        : SmartHomeLockControl(identifier, -1, -1, w, h) {}
    explicit SmartHomeLockControl(const std::string& identifier)
        : SmartHomeLockControl(identifier, -1, -1, -1, -1) {}
    virtual ~SmartHomeLockControl();

    void SetDevice(const std::string& deviceId);
    std::string GetDevice() const { return deviceId; }

    void SetState(const SmartHomeLockState& state);
    SmartHomeLockState GetState() const { return currentState; }

    /**
     * @brief Add activity log entry
     * @param timestamp Unix timestamp
     * @param action Action description
     * @param user User name (if known)
     */
    void AddActivity(uint64_t timestamp, const std::string& action,
                     const std::string& user = "");
    void ClearActivity();

    void SetOnLock(std::function<void()> callback) { onLock = callback; }
    void SetOnUnlock(std::function<void()> callback) { onUnlock = callback; }

    UltraCanvasButton* GetLockButton() const { return lockButton.get(); }
    UltraCanvasListView* GetActivityList() const { return activityList.get(); }

private:
    void Sync();
    void HandleLockToggle();

    struct ActivityEntry {
        uint64_t Timestamp;
        std::string Action;
        std::string User;
    };
    class ActivityModel;

    std::string deviceId;
    SmartHomeLockState currentState;
    std::vector<ActivityEntry> activityLog;

    std::shared_ptr<UltraCanvasButton> lockButton;
    std::shared_ptr<UltraCanvasLabel> statusLabel;
    std::shared_ptr<UltraCanvasGaugeDiagramElement> battery;
    std::shared_ptr<UltraCanvasLabel> batteryLabel;
    std::shared_ptr<ActivityModel> activityModel;
    std::shared_ptr<UltraCanvasListView> activityList;

    std::function<void()> onLock;
    std::function<void()> onUnlock;
};

/**
 * @brief Blind/shade control widget
 *
 * A picture of the blind in its window, Open / Stop / Close, a position
 * slider, an optional tilt slider and buttons for the presets.
 */
class SmartHomeBlindControl : public UltraCanvasContainer {
public:
    SmartHomeBlindControl(const std::string& identifier, float x, float y, float w, float h);
    SmartHomeBlindControl(const std::string& identifier, float w, float h)
        : SmartHomeBlindControl(identifier, -1, -1, w, h) {}
    explicit SmartHomeBlindControl(const std::string& identifier)
        : SmartHomeBlindControl(identifier, -1, -1, -1, -1) {}
    virtual ~SmartHomeBlindControl();

    void SetDevice(const std::string& deviceId);
    std::string GetDevice() const { return deviceId; }

    /**
     * @brief Set current position, 0 (closed) - 100 (open)
     */
    void SetPosition(uint8_t position);
    uint8_t GetPosition() const { return currentPosition; }

    /**
     * @brief Set current tilt, 0-100 (50 = horizontal)
     */
    void SetTilt(uint8_t tilt);
    uint8_t GetTilt() const { return currentTilt; }

    void SetTiltEnabled(bool enable);

    /**
     * @brief Add position preset (a button)
     */
    void AddPreset(const std::string& name, uint8_t position);

    void SetOnPositionChange(std::function<void(uint8_t)> callback) {
        onPositionChange = callback;
    }
    void SetOnTiltChange(std::function<void(uint8_t)> callback) {
        onTiltChange = callback;
    }

    UltraCanvasButton* GetOpenButton() const { return openButton.get(); }
    UltraCanvasButton* GetStopButton() const { return stopButton.get(); }
    UltraCanvasButton* GetCloseButton() const { return closeButton.get(); }
    UltraCanvasSlider* GetPositionSlider() const { return position.get(); }
    UltraCanvasSlider* GetTiltSlider() const { return tilt.get(); }

private:
    void Sync();
    void MoveTo(uint8_t position);   // sets, sends, reports

    std::string deviceId;
    uint8_t currentPosition = 0;
    uint8_t currentTilt = 50;
    bool syncing = false;

    std::shared_ptr<SmartHomeBlindPreview> preview;
    std::shared_ptr<UltraCanvasButton> openButton;
    std::shared_ptr<UltraCanvasButton> stopButton;
    std::shared_ptr<UltraCanvasButton> closeButton;
    std::shared_ptr<UltraCanvasLabel> positionLabel;
    std::shared_ptr<UltraCanvasSlider> position;
    std::shared_ptr<UltraCanvasContainer> tiltRow;
    std::shared_ptr<UltraCanvasSlider> tilt;
    std::shared_ptr<UltraCanvasContainer> presetRow;

    std::function<void(uint8_t)> onPositionChange;
    std::function<void(uint8_t)> onTiltChange;
};

/**
 * @brief Sensor display widget
 *
 * The reading with its unit, what kind of sensor it is, its recent history
 * as a line chart (with SensorHistoryMinimumSpan as the floor of its value
 * axis), and the alert thresholds. Read only.
 */
class SmartHomeSensorDisplay : public UltraCanvasContainer {
public:
    SmartHomeSensorDisplay(const std::string& identifier, float x, float y, float w, float h);
    SmartHomeSensorDisplay(const std::string& identifier, float w, float h)
        : SmartHomeSensorDisplay(identifier, -1, -1, w, h) {}
    explicit SmartHomeSensorDisplay(const std::string& identifier)
        : SmartHomeSensorDisplay(identifier, -1, -1, -1, -1) {}
    virtual ~SmartHomeSensorDisplay();

    void SetDevice(const std::string& deviceId);
    std::string GetDevice() const { return deviceId; }

    void SetSensorType(SmartHomeSensorType type);
    void AddReading(const SmartHomeSensorReading& reading);
    void SetCurrentReading(const SmartHomeSensorReading& reading);

    /**
     * @brief Set alert thresholds
     */
    void SetThresholds(float low, float high);

    /**
     * @brief Set display options
     * @param showGraph Show history graph
     * @param showUnit Show unit label
     * @param showTimestamp Show the sensor kind line
     */
    void SetDisplayOptions(bool showGraph, bool showUnit, bool showTimestamp);

    /**
     * @brief Set graph time range in hours
     */
    void SetGraphRange(int hours);

    // A readout has nothing to click: events pass through to what is behind.
    bool OnEvent(const UCEvent& event) override;

    UltraCanvasLineChartElement* GetGraph() const { return graph.get(); }

private:
    void Sync();
    void RefreshGraph();
    std::string GetUnitString() const;
    std::string FormatValue(float value) const;
    Color GetValueColor() const;

    std::string deviceId;
    SmartHomeSensorType sensorType = SmartHomeSensorType::Unknown;
    SmartHomeSensorReading currentReading;
    float lowThreshold = 0;
    float highThreshold = 100;
    bool thresholdsSet = false;
    bool showUnit = true;
    bool showTimestamp = true;

    std::shared_ptr<UltraCanvasLabel> valueLabel;
    std::shared_ptr<UltraCanvasLabel> kindLabel;
    std::vector<SmartHomeSensorReading> history;
    int graphHours = 24;
    std::string graphRange;   // "20.6-20.7, 24 h", under the reading
    std::shared_ptr<ChartDataVector> graphData;
    std::shared_ptr<UltraCanvasLineChartElement> graph;
    std::shared_ptr<UltraCanvasLabel> thresholdLabel;
};

/**
 * @brief Generic device detail dialog
 * 
 * Modal dialog showing detailed device information and controls.
 */
class SmartHomeDeviceDialog : public UltraCanvasUIElement {
public:
    // UltraCanvasUIElement has no default constructor: every widget is
    // built with an id and its bounds, like the rest of the framework.
    SmartHomeDeviceDialog(const std::string& identifier, float x, float y, float w, float h);
    SmartHomeDeviceDialog(const std::string& identifier, float w, float h)
        : SmartHomeDeviceDialog(identifier, -1, -1, w, h) {}
    explicit SmartHomeDeviceDialog(const std::string& identifier)
        : SmartHomeDeviceDialog(identifier, -1, -1, -1, -1) {}
    virtual ~SmartHomeDeviceDialog();
    
    /**
     * @brief Show dialog for device
     * @param deviceId Device ID
     */
    void Show(const std::string& deviceId);
    
    /**
     * @brief Hide dialog
     */
    void Hide();
    
    // Visibility is the base class's, deliberately. UltraCanvasUIElement::
    // IsVisible() is NOT virtual, so declaring one here would only hide it:
    // the dialog would think it was hidden while the framework's dispatch and
    // focus handling — which call the base — carried on as if it were not.
    // Show() and Hide() drive SetVisible(), so there is one answer.
    
    void SetOnClose(std::function<void()> callback) { onClose = callback; }
    void SetOnRename(std::function<void(const std::string&)> callback) { onRename = callback; }
    void SetOnRemove(std::function<void()> callback) { onRemove = callback; }
    
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

private:
    void RenderHeader(IRenderContext* context);
    void RenderDeviceInfo(IRenderContext* context);
    void RenderControls(IRenderContext* context);
    void RenderActions(IRenderContext* context);
    
    void LoadDeviceInfo();
    std::shared_ptr<UltraCanvasUIElement> CreateControlWidget();
    
    std::string deviceId;
    SmartHomeDeviceInfo deviceInfo;
    
    std::shared_ptr<UltraCanvasUIElement> controlWidget;
    
    std::function<void()> onClose;
    std::function<void(const std::string&)> onRename;
    std::function<void()> onRemove;
};

/**
 * @brief Pairing wizard widget
 * 
 * Step-by-step wizard for pairing new devices.
 */
class SmartHomePairingWizard : public UltraCanvasUIElement {
public:
    // UltraCanvasUIElement has no default constructor: every widget is
    // built with an id and its bounds, like the rest of the framework.
    SmartHomePairingWizard(const std::string& identifier, float x, float y, float w, float h);
    SmartHomePairingWizard(const std::string& identifier, float w, float h)
        : SmartHomePairingWizard(identifier, -1, -1, w, h) {}
    explicit SmartHomePairingWizard(const std::string& identifier)
        : SmartHomePairingWizard(identifier, -1, -1, -1, -1) {}
    virtual ~SmartHomePairingWizard();
    
    /**
     * @brief Start wizard
     */
    void Start();
    
    /**
     * @brief Cancel wizard
     */
    void Cancel();
    
    /**
     * @brief Set supported protocols
     * @param protocols List of available protocols
     */
    void SetSupportedProtocols(const std::vector<SmartHomeProtocolType>& protocols);
    
    void SetOnComplete(std::function<void(const SmartHomeDeviceInfo&)> callback) {
        onComplete = callback;
    }
    void SetOnCancel(std::function<void()> callback) { onCancel = callback; }
    
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

private:
    enum class WizardStep {
        SelectProtocol,
        Searching,
        DeviceFound,
        Configuring,
        Complete,
        Error
    };
    
    void RenderProtocolSelection(IRenderContext* context);
    void RenderSearching(IRenderContext* context);
    void RenderDeviceFound(IRenderContext* context);
    void RenderConfiguring(IRenderContext* context);
    void RenderComplete(IRenderContext* context);
    void RenderError(IRenderContext* context);
    
    void SelectProtocol(SmartHomeProtocolType protocol);
    void StartSearch();
    void StopSearch();
    void ConfigureDevice();
    void FinishPairing();
    
    WizardStep currentStep = WizardStep::SelectProtocol;
    std::vector<SmartHomeProtocolType> supportedProtocols;
    SmartHomeProtocolType selectedProtocol = SmartHomeProtocolType::Unknown;
    
    std::vector<SmartHomeDeviceInfo> discoveredDevices;
    std::string selectedDeviceId;
    SmartHomeDeviceInfo pairedDevice;
    
    std::string errorMessage;
    int searchProgress = 0;
    
    std::function<void(const SmartHomeDeviceInfo&)> onComplete;
    std::function<void()> onCancel;
};

// ===== FACTORY FUNCTIONS =====

std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeLightControlElement();
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeThermostatControlElement();
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeLockControlElement();
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeBlindControlElement();
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeSensorDisplayElement();
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeDeviceDialogElement();
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomePairingWizardElement();

} // namespace SmartHome
} // namespace UltraCanvas
