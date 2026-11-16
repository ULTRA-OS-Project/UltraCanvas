- **SmartHome: a commissioned Matter node is asked what it is.** Before it
  joins, the backend reads every endpoint's Descriptor DeviceTypeList in
  one wildcard read and the node joins with the category its device types
  make - a vacuum, washer, lock or light as itself, Unknown when the read
  fails, never a guess.
  - `protocols/Matter/MatterDeviceTypes.h`: the device type ids, checked
    against the SDK's copy of the specification, now including the Matter
    1.2 / 1.3 appliances (robotic vacuum, laundry washer and dryer,
    dishwasher, refrigerator, oven, cooktop, room air conditioner ... as
    `Appliance`), cameras, air purifiers, leak and air-quality sensors and
    bridges (`Gateway`); a node that is several things takes the type that
    says most. `tests/MatterDeviceTypeTest.cpp`, on every build.
  - `protocols/Matter/MatterDescriptorDecode.h`: one report of the list,
    whole or chunked; `tests/MatterDescriptorDecodeTest.cpp` decodes TLV
    from the SDK's own encoder (built with the Matter backend).
  - `SmartHomeMatterCommission`: commission one device from the command
    line and print what it joined as - the bench for a real device or the
    SDK's example devices.
  - `Docs/Dependencies.md`: the connectedhomeip recipe works on a current
    checkout again (pigweed's submodule is set by name, no constraints
    file, ZAP's pinned version, a one-line patch for Ubuntu's older `gn`),
    and says the stack needs IPv6.
  - Not yet run against a device: the machine this was built on has no
    IPv6 (`SmartHome/STATUS.md`, *What a Matter node is*).
