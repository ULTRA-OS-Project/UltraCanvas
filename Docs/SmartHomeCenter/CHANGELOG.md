#### 2026-10-20 *0.1.0*
- **First version: SmartHome Center, the home control window, on the demo
  home.** A navigation column (Home, Devices, Energy, Activity) beside the
  page. *Home* shows the inside temperature, the lights that are on, the
  locks and the power drawn across the top, four scenes (Good morning,
  Evening, Away, Good night), then every room with a tile per device - a
  switch on the tile for whatever switches, the reading for whatever
  measures - and the selected device's controls and details on the right:
  brightness and colour temperature for a light, target and mode for a
  thermostat, position for a blind, lock and unlock. *Devices* is the whole
  home as a table, *Energy* the metered plugs live, *Activity* what
  happened and when. A leak or smoke alarm puts a red banner over Home.
  The Energy page is the framework's bar chart run sideways, one row per
  plug, on an axis marked at whole watts.
- **The demo home.** With no radio enabled the window opens the simulated
  home of the SmartHome module (30 devices in 8 rooms), says so in a panel
  under the menu and in every device's details, and offers three events to
  stage: motion in the hallway, the patio door opening, water under the
  sink. *Add device* pairs a new simulated device after a few seconds.
- **Command line.** `--list` prints the home room by room and exits;
  `--page` and `--select` open the window on a page and a device.
