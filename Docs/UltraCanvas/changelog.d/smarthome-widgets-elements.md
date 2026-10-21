- **SmartHome: the device controls are built from framework elements.**
  `SmartHomeLightControl`, `SmartHomeThermostatControl`,
  `SmartHomeLockControl`, `SmartHomeBlindControl` and
  `SmartHomeSensorDisplay` are now `UltraCanvasContainer`s laid out by flex,
  where they used to paint every control and hit-test the pointer against
  fixed positions. The light has a switch, brightness and colour-temperature
  sliders, a colour picker (hue and intensity bars) and a swatch bar for its
  presets; the thermostat a ring gauge for the target, a spinner and a
  segmented control for the mode (the mouse wheel still steps it half a
  degree); the lock a button, a battery bar and its activity in a list view;
  the blind Open / Stop / Close buttons, position and tilt sliders and a
  button per preset. Focus, keyboard, hover and theming are the elements'.
  - The public API is kept. `SetBrightnessEnabled`, `SetColorTempEnabled`,
    `SetColorEnabled`, `SetTiltEnabled`, `SetCelsius` and `SetGraphRange` are
    out-of-line now and show or hide their rows; getters such as
    `GetPowerSwitch()`, `GetBrightnessSlider()`, `GetLockButton()` and
    `GetOpenButton()` reach the elements.
  - `SetState`, `SetPosition` and `SetTilt` put a state into the elements
    without echoing it back to the device as a command.
  - A sensor's history is a line chart with a floor under its value axis,
    `SensorHistoryMinimumSpan` (2 °C, 10 %, 20 W, 50 lx): a room steady at
    20.6-20.7 °C draws a flat line, not a sawtooth across the whole graph.
  - `SmartHomeWidgetTest` drives the controls through a real window and the
    application's dispatch; CI's Linux row now builds the Smart Home suite
    (`BUILD_TESTS` turns it on) and runs that test on a virtual display.
