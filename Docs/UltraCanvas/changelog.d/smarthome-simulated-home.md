- **SmartHome: a simulated demo home, and the engine bugs it exposed.**
  `SmartHome/protocols/Simulated` is a backend with no radio: 30 devices in
  8 rooms covering every device kind (lights, plugs, a relay, thermostats,
  locks, blinds, temperature, humidity, motion, door, leak and smoke
  sensors), on a clock that moves them - thermostats warm their rooms,
  blinds travel, a fridge's compressor cycles, motion fires and clears, the
  meters count. Pairing finds a new device after a few seconds, and four
  `simulate*` commands stage events through `SmartHomeAPI::SendCommand`. It
  is always built and registered, under its own
  `SmartHomeProtocolType::Simulated`, so a fake device is never taken for a
  real one. Driving it through the facade found what no backend had reached:
  - The first device ever to join deadlocked the manager: `RegisterDevice`
    dispatched `DispatchDeviceDiscover` with `deviceMutex` held, and that
    takes it again. No callback runs under a registry lock now.
  - Devices a backend already has when it is enabled were never registered;
    a join registered no device object; pairing never completed; an update
    never refreshed the registry's copy of the device's info.
  - Every state query (`GetLightState`, `GetThermostatState`,
    `GetLockState`, `GetSwitchState`, `GetSensorReading`) returned an empty
    struct. They read the backend's device objects now, and the facade gains
    `GetBlindPosition`, `GetBatteryLevel`, `GetPowerUsage` and
    `GetEnergyUsage`. `SmartHomeDeviceInfo` gains `Room`.
  - `SetThermostatTarget` sent `std::to_string(float)`: "23,500000" under a
    comma-decimal locale. It is dot-decimal now.
  - `SmartHome/tests/SimulatedTest.cpp`: 51 checks through the facade, run
    under `de_DE.UTF-8`.
- **DemoApp: the Smart Home module is "partially implemented"**, no longer
  "planned": the module, its widgets and SmartHome Center exist
  (`Docs/SmartHomeCenter/CHANGELOG.md`).
