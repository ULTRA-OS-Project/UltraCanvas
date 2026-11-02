// Apps/UltraFiler/UltraFilerDeviceProvider.h
// The phones, cameras and players plugged in over USB that are not a drive
// (MTP / PTP - UltraCanvasPortableDevices.h), carried as UltraCloud drives so
// UltraFiler's remote-drive machinery serves them like an FTP server: the
// listing cache and its worker, the preview copies, copying off and onto the
// device, rename, delete, new folder, the status line and the connection log.
//
// A connected device is an UltraCloud account of the "portable-device"
// provider whose serverUrl is the device's id. Those accounts live in a store
// of their own that is never written to disk (UltraFilerRemoteDrives keeps
// it): a phone is a drive while it is plugged in, not a place the user
// configured, and it goes when it is pulled out.
// Version: 1.0.0
// Last Modified: 2026-11-01
// Author: UltraCanvas Framework
#pragma once

#include <string>

namespace UltraCanvas {

// The provider id of a connected device's account.
inline const char* PortableDeviceProviderId() { return "portable-device"; }

// The account id a device with this WPD id gets: "device-" and a hash of the
// id, so it is a short slug a path can carry (ultracloud://device-1a2b.../)
// and the same each time the same device is plugged into the same port.
std::string PortableDeviceAccountId(const std::string& deviceId);

// Registers the provider with UltraCloud, once. Safe to call again.
void RegisterPortableDeviceProvider();

} // namespace UltraCanvas
