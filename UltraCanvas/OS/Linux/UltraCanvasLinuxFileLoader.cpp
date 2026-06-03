// OS/Linux/UltraCanvasLinuxFileLoader.cpp
// Linux implementation of UltraCanvasFileLoader::NotifyRecentFile.
// Registers the file with the freedesktop recent-files list via GtkRecentManager,
// which is what Nautilus / GNOME Files / KDE / XFCE read for the "Recent" view.
// Version: 1.0.0
// Last Modified: 2026-05-12
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"

#ifdef __linux__

#include <gtk-3.0/gtk/gtk.h>

namespace UltraCanvas {

    void UltraCanvasFileLoader::NotifyRecentFile(const std::string& filePath) {
        if (filePath.empty()) return;

        // gtk_recent_manager_get_default needs GTK initialized. gtk_init_check
        // is a no-op once GTK has already been initialized by the dialog code,
        // and safely fails in headless environments.
        int argc = 0;
        char** argv = nullptr;
        if (!gtk_init_check(&argc, &argv)) return;

        gchar* uri = g_filename_to_uri(filePath.c_str(), nullptr, nullptr);
        if (!uri) return;

        GtkRecentManager* manager = gtk_recent_manager_get_default();
        if (manager) {
            gtk_recent_manager_add_item(manager, uri);
        }
        g_free(uri);
    }

} // namespace UltraCanvas

#endif // __linux__
