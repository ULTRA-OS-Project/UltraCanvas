- **SmartHome: the home is kept, and automations run.**
  - `SmartHomeAPI::OpenStore(path)` opens **smarthome.db**, an UltraDatabase
    (SQLite) store: scenes and automations (written through on every
    change), the names and rooms given to devices (`RenameDevice`, new
    `SetDeviceRoom`), the home's location (`SetHomeLocation`), the event log
    (`GetEventLog`, `SetOnEvent`) and sensor history. `ExportStore` writes a
    consistent copy; the manager's `SaveConfiguration` / `LoadConfiguration`
    TODOs are that copy and an open. Built without SQLite, the store refuses
    to open and says why.
  - `GetSensorHistory` returned nothing. It answers from the store: sensor
    values, a thermostat's room temperature and a metered plug's power
    (`SmartHomeSensorType::Power`, new) are recorded past a deadband and at
    least every 15 minutes.
  - `EvaluateAutomations` was a TODO, so no automation ever ran. The new
    engine reads the trigger JSON and condition strings the automation
    editor writes (documented on `SmartHomeAutomation`): device triggers
    that fire once per match, optionally after `forMinutes`; time, sunrise
    and sunset triggers (`GetSunTimes`); time, weekday and device
    conditions; `notify` (`SetOnAutomationNotify`), `delay` and
    `activateScene` actions. It evaluates once a second and at once when a
    device changes, switches off an automation that keeps triggering
    itself, and logs every run with its reason. `RunAutomation` runs one by
    hand.
  - `SmartHome/tests/AutomationTest.cpp`: the store across a restart, every
    trigger and condition kind, the delay, the loop guard and the sun, on a
    clock the test sets.
