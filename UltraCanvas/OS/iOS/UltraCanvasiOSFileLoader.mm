// OS/iOS/UltraCanvasiOSFileLoader.mm
// iOS keeps no recent-documents list an app can add to (a document picker
// shows its own recents), so the notification is a no-op, as on Android.
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"

namespace UltraCanvas {

    void UltraCanvasFileLoader::NotifyRecentFile(const std::string&) {}

} // namespace UltraCanvas
