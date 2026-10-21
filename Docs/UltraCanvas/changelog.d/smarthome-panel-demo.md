- **SmartHome: the dashboard panel drew every device card on top of the
  first.** `SmartHomePanel` laid its cards out in a grid but drew each at its
  own origin, so 30 devices showed as one card. Each is drawn where it is
  laid out now (grid, list, rooms and scenes), and the room view groups the
  cards by the home's rooms (`SmartHomeAPI::GetRooms`) when the panel was
  given none of its own.
- **Demo application: the Smart Home entry shows the module.** With the
  `SmartHomeUI` widgets in the build it starts the simulated home and opens
  the dashboard panel on it, with buttons for its device, room, list and
  scene views; without them it shows the module's documentation as before.
