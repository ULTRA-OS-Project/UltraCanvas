- **UltraCanvasPortableDevices: phones, cameras and players over MTP / PTP.**
  A device plugged in over USB that is not a drive gets no drive letter or
  mount point, so nothing in the framework could see it. New
  `UltraCanvasPortableDevices.h`: `List` (name, manufacturer, phone / camera /
  player), `ListFolder`, `Download`, `Upload`, `MakeFolder`, `Delete` and
  `Rename` by path (`"/"` is the device, its folders its storages),
  `GetSpace` (capacity and free space, storages added up), and
  `UltraCanvasPortableDeviceMonitor` for plug-in and pull-out. The Windows
  backend is Windows Portable Devices - built in, no new library - running
  every call on one COM thread of its own, so the devices it opened stay open
  between calls. The macOS backend is Image Capture (`ImageCaptureCore`): an
  iPhone's or iPad's photos and videos and a camera's card, read-only
  (`PortableDeviceInfo::readOnly`), the work handed to the main thread that
  Image Capture answers on. Elsewhere `Available()` is false. `PortableDevicesTest`
  checks the path rules and the backend-less answers.
- **Linux: the phones, cameras and shares the desktop mounted are volumes.**
  GNOME mounts a phone through GVFS as a directory of one FUSE file system
  (`$XDG_RUNTIME_DIR/gvfs/mtp:host=...`), which is no mount point, so
  `ListMountedVolumes` never listed it. `ListGvfsVolumes` adds those places -
  MTP, gphoto2, AFC (iPhone), SMB, SFTP, FTP, WebDAV, NFS, AFP - with readable
  labels ("Google Pixel 8", "photos on nas"); cloud accounts stay with
  `UltraCanvasCloudStorage`. The Linux volume monitor looks at them every two
  seconds, since they change without the mount table changing.
  `VolumeMonitorTest` covers the labels and the listing.
