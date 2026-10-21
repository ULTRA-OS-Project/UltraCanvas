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
- **SmartHome: the device dialog, the pairing wizard and the scene and
  automation editors are built from framework elements too.** The dialog
  holds the device's control as a real child - before, it painted the
  control itself and passed it events by hand, so a container-based control
  in it got none. The editors take the name in a text input, list their
  actions, triggers and conditions in list views, pick a device from a
  dropdown, and enable an automation with a switch. Getters reach the
  elements (`GetCloseButton()`, `GetSaveButton()`, `GetNameInput()`, ...).
  - The pairing wizard is protocol buttons, then a search with a progress
    bar and Cancel, then "Added ..." or what went wrong. It used to wait on a
    "choose a found device" step nothing ever filled; it now watches for the
    device that joins the home, on an application timer (`Poll()` for a host
    without an event loop), and leaves SmartHomeAPI's callbacks to the host.
    `SetSearchTimeout`, `IsSearching` and `GetErrorMessage` are new.
- **SmartHome: the dashboard panel and the topology, energy, scheduler and
  group widgets are built from framework elements.** `SmartHomePanel` is a
  container: a toolbar (a segmented control for its six views, a search
  field, "Add device"), a pairing banner, and a scrolling body whose
  children are the device and scene cards themselves, in rows that wrap.
  It used to lay the cards out and draw them by hand and pass them events
  itself. The module's callbacks, which arrive on its threads, now only note
  the change; `Poll()` applies it on the UI thread from an application
  timer. Its automations are a list view (double-click turns one on or off)
  and its network view is a `SmartHomeNetworkTopology`.
  - `SmartHomeNetworkTopology` is an `UltraCanvasNodeDiagram` under a
    layout switch and a radio filter; `SetAutoRefresh` uses an application
    timer instead of refreshing from `Render`.
  - `SmartHomeEnergyMonitor` plots power, energy or cost as a line chart
    with a segmented control; `SmartHomeGroupControl` has buttons, sliders, a
    list view and a dropdown to add a device; `SmartHomeScheduler` has a
    segmented control for day / week / list, a list view, Turn off / Remove,
    and draws only its week grid.
  - New getters reach the elements; `GetNodes()`, `GetLinks()`,
    `GetEvents()`, `GetViewMode()`, `GetSelectedEvent()`, `GetGroup()`,
    `GetDeviceCard()`, `GetSceneCard()` and `IsPairing()` are new.
  - The demo application's Smart Home entry drops its own view buttons: the
    panel's toolbar has them.
  - `scripts/check_ui_reuse.py` scans `SmartHome/ui`; the element catalogue
    lists the Smart Home widgets and what each is built from.
  - `SmartHomeWidgetTest` drives the controls, the dialog, the wizard (a
    simulated device pairs through it), the editors, the advanced widgets and
    the panel through a real window
    and the application's dispatch; CI's Linux row now builds the Smart Home suite
    (`BUILD_TESTS` turns it on) and runs that test on a virtual display.
- **SmartHome: device icons, and a network map that reads.**
  - Sixteen new line icons in `media/icons/smarthome/` beside the existing
    ones (TV, fridge, kettle, computer, heater, gate, camera, speaker, fan,
    appliance, gateway, CO, air quality, light level, pressure, vibration),
    and `UltraCanvasSmartHomeIcons.h`: `SmartHomeDeviceIconName` picks a
    device's icon by category, by its kind of reading for a sensor, and by
    its name for a plug or switch (a plug called "Kettle" gets the kettle);
    `SmartHomeCategoryColor` gives the disc colour.
  - `SmartHomeNetworkTopology` draws each device as a white card with its
    icon, name and room (routers larger, bordered in their radio's colour)
    on a light canvas, and no two cards overlap in any layout. A device is
    linked to a router of its radio in its own room when there is one (the
    rest are shared out evenly) instead of every device to the first
    router; radial puts each router inside the arc of its own devices, and
    tree puts each router over a column of its devices.
  - The dashboard's device cards show the icon on their colour disc, and a
    list row now shows the device's name and state.
  - The radial map is drawn tighter, and zoomed out past 70 % the topology
    shows its devices as icons, named on hover (`SetDetailZoom`); a
    double-click on a device reports through the new `SetOnNodeActivated`.
