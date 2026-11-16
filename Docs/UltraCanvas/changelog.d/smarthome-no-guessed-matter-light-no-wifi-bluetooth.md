- **SmartHome: a commissioned Matter node was always a light.**
  `MatterProtocol::OnCommissioningComplete` put `OnOffLight` in every new
  node's device types, and nothing else ever filled them, so a lock, a
  vacuum or a washer showed up with a light's controls. The guess is gone:
  the node's category is Unknown until the Descriptor cluster's
  DeviceTypeList is read, which is not done yet (`SmartHome/STATUS.md`,
  *Known gaps*).
- **SmartHome: `SmartHomeProtocolType::WiFi` and `::Bluetooth` removed.**
  No backend implemented either, so `EnableProtocol` could only fail on
  them, and the Network topology carried colours for radios that cannot
  exist. A Wi-Fi device that speaks Matter is Matter; a vendor's own Wi-Fi
  protocol (Xiaomi miIO, LG ThinQ) will be a type of its own, added with
  its backend. `Docs/Modules/Smarthome/README.md` and the framework README
  no longer list them as supported, and name KNX, which they left out.
