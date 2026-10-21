- **A Network page.** The navigation gains *Network*: the module's map of
  the radios (`SmartHomeNetworkTopology`), every device a card with its
  icon, name and room, linked to a router of its radio - in its own room
  where there is one. Force, radial and tree layouts, a filter by radio,
  no card on another; zoomed far out the devices show as their icons, named
  on hover. Double-click a device, or choose it and *Open device*, for its
  page. `--page network` opens on it.
- **Devices look as they do in the module's widgets.** The icon is the
  module's choice (`SmartHomeDeviceIconName`): a plug called "TV" shows a TV,
  the fridge a fridge, the kettle a kettle, a garden gate a gate; it sits
  white on a round disc of the device's category colour on the tiles, in the
  inspector and on the device's page.
- The app links the module's widget library (`SmartHomeUI`) for the two.
- The app hears the module through a listener (`SmartHomeAPI::AddListener`)
  rather than its single callbacks, so a `SmartHomePanel` in the same
  process no longer takes them over.
