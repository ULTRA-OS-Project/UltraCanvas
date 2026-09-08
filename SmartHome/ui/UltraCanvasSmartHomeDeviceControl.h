// UltraCanvasSmartHomeDeviceControl.h
// Smart Home Device Control Widgets
// Version: 1.0.0
// Last Modified: 2025-12-08
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasSmartHome.h"
#include <memory>
#include <functional>

namespace UltraCanvas {
namespace SmartHome {

/**
 * @brief Light control widget
 * 
 * Provides controls for smart lights including:
 * - On/Off toggle
 * - Brightness slider
 * - Color temperature slider
 * - RGB color picker
 * - Color presets
 */
class SmartHomeLightControl : public UIElement {
public:
    SmartHomeLightControl();
    virtual ~SmartHomeLightControl();
    
    /**
     * @brief Set device to control
     * @param deviceId Device ID
     */
    void SetDevice(const std::string& deviceId);
    
    /**
     * @brief Get controlled device ID
     * @return Device ID
     */
    std::string GetDevice() const { return deviceId; }
    
    /**
     * @brief Set current light state
     * @param state Light state
     */
    void SetState(const SmartHomeLightState& state);
    
    /**
     * @brief Get current state
     * @return Light state
     */
    SmartHomeLightState GetState() const { return currentState; }
    
    /**
     * @brief Enable/disable brightness control
     * @param enable Enable flag
     */
    void SetBrightnessEnabled(bool enable) { brightnessEnabled = enable; }
    
    /**
     * @brief Enable/disable color temperature control
     * @param enable Enable flag
     */
    void SetColorTempEnabled(bool enable) { colorTempEnabled = enable; }
    
    /**
     * @brief Enable/disable RGB color control
     * @param enable Enable flag
     */
    void SetColorEnabled(bool enable) { colorEnabled = enable; }
    
    /**
     * @brief Set color temperature range (Kelvin)
     * @param min Minimum (warm, e.g. 2700K)
     * @param max Maximum (cool, e.g. 6500K)
     */
    void SetColorTempRange(uint16_t min, uint16_t max);
    
    /**
     * @brief Add color preset
     * @param name Preset name
     * @param r Red (0-255)
     * @param g Green (0-255)
     * @param b Blue (0-255)
     */
    void AddColorPreset(const std::string& name, uint8_t r, uint8_t g, uint8_t b);
    
    /**
     * @brief Set callback for state changes
     * @param callback Callback function
     */
    void SetOnStateChange(std::function<void(const SmartHomeLightState&)> callback) {
        onStateChange = callback;
    }
    
    void Render(IRenderContext* context) override;
    bool OnMouseDown(int x, int y, int button) override;
    bool OnMouseUp(int x, int y, int button) override;
    bool OnMouseMove(int x, int y) override;
    bool OnTouchStart(int touchId, float x, float y) override;
    bool OnTouchMove(int touchId, float x, float y) override;
    bool OnTouchEnd(int touchId, float x, float y) override;

private:
    void RenderPowerButton(IRenderContext* context);
    void RenderBrightnessSlider(IRenderContext* context);
    void RenderColorTempSlider(IRenderContext* context);
    void RenderColorWheel(IRenderContext* context);
    void RenderColorPresets(IRenderContext* context);
    
    void HandlePowerClick();
    void HandleBrightnessChange(int x);
    void HandleColorTempChange(int x);
    void HandleColorWheelClick(int x, int y);
    void HandlePresetClick(int index);
    
    void SendState();
    
    struct ColorPreset {
        std::string Name;
        uint8_t R, G, B;
    };
    
    std::string deviceId;
    SmartHomeLightState currentState;
    
    bool brightnessEnabled = true;
    bool colorTempEnabled = true;
    bool colorEnabled = true;
    
    uint16_t colorTempMin = 2700;
    uint16_t colorTempMax = 6500;
    
    std::vector<ColorPreset> colorPresets;
    
    // Interaction state
    int dragging = 0;  // 0=none, 1=brightness, 2=colorTemp, 3=colorWheel
    
    std::function<void(const SmartHomeLightState&)> onStateChange;
};

/**
 * @brief Thermostat control widget
 * 
 * Provides controls for smart thermostats including:
 * - Temperature display
 * - Target temperature dial
 * - Mode selection (heat/cool/auto/off)
 * - Schedule preview
 */
class SmartHomeThermostatControl : public UIElement {
public:
    SmartHomeThermostatControl();
    virtual ~SmartHomeThermostatControl();
    
    void SetDevice(const std::string& deviceId);
    std::string GetDevice() const { return deviceId; }
    
    void SetState(const SmartHomeThermostatState& state);
    SmartHomeThermostatState GetState() const { return currentState; }
    
    /**
     * @brief Set temperature unit
     * @param celsius true for Celsius, false for Fahrenheit
     */
    void SetCelsius(bool celsius) { useCelsius = celsius; }
    
    /**
     * @brief Set temperature range
     * @param min Minimum setpoint
     * @param max Maximum setpoint
     */
    void SetTemperatureRange(float min, float max);
    
    /**
     * @brief Set supported modes
     * @param modes List of mode strings (heat, cool, auto, off, etc.)
     */
    void SetSupportedModes(const std::vector<std::string>& modes);
    
    void SetOnStateChange(std::function<void(const SmartHomeThermostatState&)> callback) {
        onStateChange = callback;
    }
    
    void Render(IRenderContext* context) override;
    bool OnMouseDown(int x, int y, int button) override;
    bool OnMouseUp(int x, int y, int button) override;
    bool OnMouseMove(int x, int y) override;
    bool OnMouseWheel(int x, int y, float delta) override;

private:
    void RenderTemperatureDial(IRenderContext* context);
    void RenderCurrentTemperature(IRenderContext* context);
    void RenderTargetTemperature(IRenderContext* context);
    void RenderModeSelector(IRenderContext* context);
    void RenderHumidity(IRenderContext* context);
    
    void HandleDialDrag(int x, int y);
    void HandleModeSelect(const std::string& mode);
    void AdjustTarget(float delta);
    
    void SendState();
    float CelsiusToFahrenheit(float c) const;
    float FahrenheitToCelsius(float f) const;
    
    std::string deviceId;
    SmartHomeThermostatState currentState;
    
    bool useCelsius = true;
    float tempMin = 10.0f;
    float tempMax = 35.0f;
    std::vector<std::string> supportedModes;
    
    bool draggingDial = false;
    
    std::function<void(const SmartHomeThermostatState&)> onStateChange;
};

/**
 * @brief Lock control widget
 * 
 * Provides controls for smart locks including:
 * - Lock/Unlock button with animation
 * - Status display
 * - Battery level
 * - Recent activity log
 */
class SmartHomeLockControl : public UIElement {
public:
    SmartHomeLockControl();
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
    
    /**
     * @brief Clear activity log
     */
    void ClearActivity();
    
    void SetOnLock(std::function<void()> callback) { onLock = callback; }
    void SetOnUnlock(std::function<void()> callback) { onUnlock = callback; }
    
    void Render(IRenderContext* context) override;
    bool OnMouseDown(int x, int y, int button) override;
    bool OnMouseUp(int x, int y, int button) override;

private:
    void RenderLockButton(IRenderContext* context);
    void RenderStatus(IRenderContext* context);
    void RenderBattery(IRenderContext* context);
    void RenderActivityLog(IRenderContext* context);
    
    void HandleLockToggle();
    void AnimateLock(bool locking);
    
    struct ActivityEntry {
        uint64_t Timestamp;
        std::string Action;
        std::string User;
    };
    
    std::string deviceId;
    SmartHomeLockState currentState;
    
    std::vector<ActivityEntry> activityLog;
    
    bool animating = false;
    float animationProgress = 0;
    bool animatingToLocked = false;
    
    std::function<void()> onLock;
    std::function<void()> onUnlock;
};

/**
 * @brief Blind/shade control widget
 * 
 * Provides controls for smart blinds including:
 * - Position slider
 * - Tilt control
 * - Preset positions
 */
class SmartHomeBlindControl : public UIElement {
public:
    SmartHomeBlindControl();
    virtual ~SmartHomeBlindControl();
    
    void SetDevice(const std::string& deviceId);
    std::string GetDevice() const { return deviceId; }
    
    /**
     * @brief Set current position
     * @param position Position 0-100 (0=closed, 100=open)
     */
    void SetPosition(uint8_t position);
    
    /**
     * @brief Get current position
     * @return Position 0-100
     */
    uint8_t GetPosition() const { return currentPosition; }
    
    /**
     * @brief Set current tilt
     * @param tilt Tilt 0-100 (50=horizontal)
     */
    void SetTilt(uint8_t tilt);
    
    /**
     * @brief Get current tilt
     * @return Tilt 0-100
     */
    uint8_t GetTilt() const { return currentTilt; }
    
    /**
     * @brief Enable tilt control
     * @param enable Enable flag
     */
    void SetTiltEnabled(bool enable) { tiltEnabled = enable; }
    
    /**
     * @brief Add position preset
     * @param name Preset name
     * @param position Position 0-100
     */
    void AddPreset(const std::string& name, uint8_t position);
    
    void SetOnPositionChange(std::function<void(uint8_t)> callback) { 
        onPositionChange = callback; 
    }
    void SetOnTiltChange(std::function<void(uint8_t)> callback) { 
        onTiltChange = callback; 
    }
    
    void Render(IRenderContext* context) override;
    bool OnMouseDown(int x, int y, int button) override;
    bool OnMouseUp(int x, int y, int button) override;
    bool OnMouseMove(int x, int y) override;

private:
    void RenderBlindPreview(IRenderContext* context);
    void RenderPositionSlider(IRenderContext* context);
    void RenderTiltSlider(IRenderContext* context);
    void RenderPresets(IRenderContext* context);
    void RenderQuickButtons(IRenderContext* context);
    
    void HandlePositionChange(int y);
    void HandleTiltChange(int x);
    void HandlePresetClick(int index);
    void HandleQuickAction(const std::string& action);
    
    struct BlindPreset {
        std::string Name;
        uint8_t Position;
    };
    
    std::string deviceId;
    uint8_t currentPosition = 0;
    uint8_t currentTilt = 50;
    bool tiltEnabled = false;
    
    std::vector<BlindPreset> presets;
    
    int dragging = 0;  // 0=none, 1=position, 2=tilt
    
    std::function<void(uint8_t)> onPositionChange;
    std::function<void(uint8_t)> onTiltChange;
};

/**
 * @brief Sensor display widget
 * 
 * Displays sensor readings with:
 * - Current value with unit
 * - Historical graph
 * - Threshold alerts
 */
class SmartHomeSensorDisplay : public UIElement {
public:
    SmartHomeSensorDisplay();
    virtual ~SmartHomeSensorDisplay();
    
    void SetDevice(const std::string& deviceId);
    std::string GetDevice() const { return deviceId; }
    
    /**
     * @brief Set sensor type
     * @param type Sensor type
     */
    void SetSensorType(SmartHomeSensorType type);
    
    /**
     * @brief Add reading to history
     * @param reading Sensor reading
     */
    void AddReading(const SmartHomeSensorReading& reading);
    
    /**
     * @brief Set current reading
     * @param reading Current reading
     */
    void SetCurrentReading(const SmartHomeSensorReading& reading);
    
    /**
     * @brief Set alert thresholds
     * @param low Low threshold
     * @param high High threshold
     */
    void SetThresholds(float low, float high);
    
    /**
     * @brief Set display options
     * @param showGraph Show history graph
     * @param showUnit Show unit label
     * @param showTimestamp Show last update time
     */
    void SetDisplayOptions(bool showGraph, bool showUnit, bool showTimestamp);
    
    /**
     * @brief Set graph time range
     * @param hours Hours of history to show
     */
    void SetGraphRange(int hours) { graphHours = hours; }
    
    void Render(IRenderContext* context) override;

private:
    void RenderCurrentValue(IRenderContext* context);
    void RenderGraph(IRenderContext* context);
    void RenderThresholds(IRenderContext* context);
    void RenderStatus(IRenderContext* context);
    
    std::string GetUnitString() const;
    std::string FormatValue(float value) const;
    uint32_t GetValueColor() const;
    
    std::string deviceId;
    SmartHomeSensorType sensorType = SmartHomeSensorType::Unknown;
    SmartHomeSensorReading currentReading;
    std::vector<SmartHomeSensorReading> history;
    
    float lowThreshold = 0;
    float highThreshold = 100;
    bool thresholdsSet = false;
    
    bool showGraph = true;
    bool showUnit = true;
    bool showTimestamp = true;
    int graphHours = 24;
};

/**
 * @brief Generic device detail dialog
 * 
 * Modal dialog showing detailed device information and controls.
 */
class SmartHomeDeviceDialog : public UIElement {
public:
    SmartHomeDeviceDialog();
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
    
    /**
     * @brief Check if dialog is visible
     * @return true if visible
     */
    bool IsVisible() const { return visible; }
    
    void SetOnClose(std::function<void()> callback) { onClose = callback; }
    void SetOnRename(std::function<void(const std::string&)> callback) { onRename = callback; }
    void SetOnRemove(std::function<void()> callback) { onRemove = callback; }
    
    void Render(IRenderContext* context) override;
    bool OnMouseDown(int x, int y, int button) override;
    bool OnMouseUp(int x, int y, int button) override;
    bool OnKeyDown(int keyCode, int modifiers) override;

private:
    void RenderHeader(IRenderContext* context);
    void RenderDeviceInfo(IRenderContext* context);
    void RenderControls(IRenderContext* context);
    void RenderActions(IRenderContext* context);
    
    void LoadDeviceInfo();
    std::shared_ptr<UIElement> CreateControlWidget();
    
    std::string deviceId;
    SmartHomeDeviceInfo deviceInfo;
    bool visible = false;
    
    std::shared_ptr<UIElement> controlWidget;
    
    std::function<void()> onClose;
    std::function<void(const std::string&)> onRename;
    std::function<void()> onRemove;
};

/**
 * @brief Pairing wizard widget
 * 
 * Step-by-step wizard for pairing new devices.
 */
class SmartHomePairingWizard : public UIElement {
public:
    SmartHomePairingWizard();
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
    
    void Render(IRenderContext* context) override;
    bool OnMouseDown(int x, int y, int button) override;
    bool OnKeyDown(int keyCode, int modifiers) override;

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

std::shared_ptr<UIElement> CreateSmartHomeLightControlElement();
std::shared_ptr<UIElement> CreateSmartHomeThermostatControlElement();
std::shared_ptr<UIElement> CreateSmartHomeLockControlElement();
std::shared_ptr<UIElement> CreateSmartHomeBlindControlElement();
std::shared_ptr<UIElement> CreateSmartHomeSensorDisplayElement();
std::shared_ptr<UIElement> CreateSmartHomeDeviceDialogElement();
std::shared_ptr<UIElement> CreateSmartHomePairingWizardElement();

} // namespace SmartHome
} // namespace UltraCanvas
