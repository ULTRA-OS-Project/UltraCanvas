// OS/Linux/UltraCanvasLinuxTrayHost.cpp
// The notification area for a desktop that is the session's shell on Linux:
// the StatusNotifierWatcher and StatusNotifierHost on the D-Bus session bus
// that every program's StatusNotifierItem registers with - the protocol KDE
// wrote and every modern tray program speaks (Telegram, Nextcloud, Steam,
// the network and volume applets, anything built on KStatusNotifierItem,
// libappindicator or Qt's QSystemTrayIcon).
//
// How the protocol goes:
//   - The host owns org.kde.StatusNotifierWatcher and exports
//     /StatusNotifierWatcher. A program that wants an icon calls
//     RegisterStatusNotifierItem(service) there, `service` being its bus
//     name, a bus name and path, or just a path (then the sender's unique
//     name is the bus name; the default path is /StatusNotifierItem). The
//     watcher announces the item to its hosts and watches NameOwnerChanged
//     to drop an item whose connection left - a crashed program says no
//     goodbye.
//   - The host then reads the item's properties over
//     org.freedesktop.DBus.Properties (Id, Status, the icon as a theme name
//     or ARGB pixmaps, the tooltip, whether the item is only a menu, its
//     menu's object path) and listens for the item's New* signals to read
//     them again. Everything is read asynchronously on the host's own
//     GMainContext, so a program that is slow to answer holds up nothing
//     but its own icon.
//   - A program's menu is a dbusmenu (com.canonical.dbusmenu at the Menu
//     path): GetLayout hands the whole tree over as data, which the desktop
//     draws itself and answers with Event(id, "clicked"). AboutToShow is
//     sent once before the first read, as a desktop opening the menu would,
//     since some programs fill their menu only then.
//   - A click goes to the program as Activate, SecondaryActivate or
//     ContextMenu at the pointer's position, read from the X server through
//     the query connection UltraCanvasLinuxDesktopShell.cpp keeps.
//
// The pictures are PNG files in the disk cache's "desktop-icons" folder,
// named after their pixels, so an icon drawn again is written once and an
// icon that stops being asked for is swept by the cache's rule. A theme
// name resolves through the program's own IconThemePath first (programs
// ship their icons that way), then the installed themes.
//
// Only one watcher works per bus, and KDE's or GNOME's would be fighting
// ours for the name, so TrayHostOpen refuses while another owns it.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasDesktopShellBackend.h"
#include "UltraCanvasDesktopEntry.h"
#include "UltraCanvasDiskCache.h"
#include "UltraCanvasLinuxDesktopShellInternal.h"
#include "UltraCanvasPathUtf8.h"

#include <gio/gio.h>
#include <cairo/cairo.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;

namespace UltraCanvas {
namespace DesktopShellBackend {

namespace {

    // ===== NAMES ON THE BUS =====

    constexpr const char* kWatcherName = "org.kde.StatusNotifierWatcher";
    constexpr const char* kWatcherPath = "/StatusNotifierWatcher";
    constexpr const char* kWatcherInterface = "org.kde.StatusNotifierWatcher";
    constexpr const char* kItemInterfaceKde = "org.kde.StatusNotifierItem";
    constexpr const char* kItemInterfaceFreedesktop = "org.freedesktop.StatusNotifierItem";
    constexpr const char* kItemDefaultPath = "/StatusNotifierItem";
    constexpr const char* kMenuInterface = "com.canonical.dbusmenu";
    constexpr const char* kPropertiesInterface = "org.freedesktop.DBus.Properties";
    constexpr const char* kBusName = "org.freedesktop.DBus";
    constexpr const char* kBusPath = "/org/freedesktop/DBus";
    constexpr const char* kBusInterface = "org.freedesktop.DBus";

    // A program that does not answer in this long holds up its own icon
    // only; a click to one that has hung is dropped rather than waited for.
    constexpr int kCallTimeoutMs = 2500;
    // The size the desktop draws the icons at: the frame nearest to it is
    // the one written out.
    constexpr int kIconSize = 22;

    constexpr guint32 kNameFlagDoNotQueue = 4;      // DBUS_NAME_FLAG_DO_NOT_QUEUE
    constexpr guint32 kNameReplyPrimaryOwner = 1;   // DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER

    constexpr const char* kWatcherIntrospection =
        "<node>"
        "  <interface name='org.kde.StatusNotifierWatcher'>"
        "    <method name='RegisterStatusNotifierItem'><arg name='service' type='s' direction='in'/></method>"
        "    <method name='RegisterStatusNotifierHost'><arg name='service' type='s' direction='in'/></method>"
        "    <property name='RegisteredStatusNotifierItems' type='as' access='read'/>"
        "    <property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
        "    <property name='ProtocolVersion' type='i' access='read'/>"
        "    <signal name='StatusNotifierItemRegistered'><arg type='s'/></signal>"
        "    <signal name='StatusNotifierItemUnregistered'><arg type='s'/></signal>"
        "    <signal name='StatusNotifierHostRegistered'/>"
        "    <signal name='StatusNotifierHostUnregistered'/>"
        "  </interface>"
        "</node>";

    // Without a session bus GIO would try to autolaunch one, which a desktop
    // starting as the session's shell must not do twice over.
    bool HaveSessionBus() {
        const std::string address = GetEnvUtf8("DBUS_SESSION_BUS_ADDRESS");
        if (!address.empty()) return true;
        // systemd's per-user bus, which GIO finds without the variable.
        const std::string runtime = GetEnvUtf8("XDG_RUNTIME_DIR");
        return !runtime.empty() && access((runtime + "/bus").c_str(), F_OK) == 0;
    }

    // The program a process runs, by name - "telegram-desktop", "nextcloud"
    // - the closest the kernel has to a window's WM_CLASS.
    std::string ProgramNameOfProcess(int pid) {
        if (pid <= 0) return {};
        std::ifstream in(PathFromUtf8("/proc/" + std::to_string(pid) + "/comm"));
        std::string name;
        std::getline(in, name);
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
        return name;
    }

    struct VariantRef {
        GVariant* value = nullptr;
        VariantRef() = default;
        explicit VariantRef(GVariant* v) : value(v) {}
        VariantRef(const VariantRef&) = delete;
        VariantRef& operator=(const VariantRef&) = delete;
        VariantRef(VariantRef&& other) noexcept : value(other.value) { other.value = nullptr; }
        VariantRef& operator=(VariantRef&& other) noexcept {
            if (this != &other) {
                if (value) g_variant_unref(value);
                value = other.value;
                other.value = nullptr;
            }
            return *this;
        }
        ~VariantRef() { if (value) g_variant_unref(value); }
        explicit operator bool() const { return value != nullptr; }
    };

    std::string StringOf(GVariant* v) {
        if (!v || !g_variant_is_of_type(v, G_VARIANT_TYPE_STRING)) return {};
        const char* text = g_variant_get_string(v, nullptr);
        return text ? text : "";
    }

    // ===== THE PNG CACHE FOR ICONS =====
    // The desktop draws image files. A pixmap a program sends over the bus
    // is written once into the "desktop-icons" folder of the disk cache
    // ($XDG_CACHE_HOME/UltraCanvas), named after its pixels, stamped on
    // every use and swept by the shared disk-cache rule.

    std::string HashHex(const unsigned char* bytes, size_t count, uint64_t seed) {
        uint64_t hash = 1469598103934665603ull ^ seed;   // FNV-1a, 64 bit
        for (size_t i = 0; i < count; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        char buffer[17] = {};
        std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(hash));
        return buffer;
    }

    std::string IconCacheDirectory() {
        const std::string dir = DiskCache::Directory("desktop-icons");
        if (dir.empty()) return dir;
        static std::once_flag sweepOnce;
        std::call_once(sweepOnce, [dir]() { DiskCache::Sweep(dir, {".png", ".tmp"}); });
        return dir;
    }

    // The cached file for `hash`, or "" with `path` set to where it should
    // be written when it is not there yet.
    std::string CachedIconPath(const std::string& hash, std::string& path) {
        const std::string dir = IconCacheDirectory();
        path.clear();
        if (dir.empty()) return {};
        path = PathToUtf8(PathFromUtf8(dir) / PathFromUtf8("sni-" + hash + ".png"));
        std::error_code ec;
        if (fs::exists(PathFromUtf8(path), ec)) {
            DiskCache::Touch(path);
            return path;
        }
        return {};
    }

    // Written to a .tmp beside it and renamed, so a reader never sees half
    // a file and an interrupted write is swept with the .tmp files.
    bool CommitTemporary(const std::string& tmp, const std::string& path) {
        std::error_code ec;
        fs::rename(PathFromUtf8(tmp), PathFromUtf8(path), ec);
        if (ec) {
            fs::remove(PathFromUtf8(tmp), ec);
            return false;
        }
        return true;
    }

    // One frame of an IconPixmap: ARGB32 in network byte order, not
    // premultiplied - cairo wants native-endian premultiplied pixels.
    struct PixmapFrame {
        int width = 0;
        int height = 0;
        const unsigned char* bytes = nullptr;
        size_t count = 0;
    };

    std::string PixmapToPng(const PixmapFrame& frame) {
        if (frame.width <= 0 || frame.height <= 0 || frame.width > 4096 || frame.height > 4096) return {};
        const size_t needed = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 4;
        if (!frame.bytes || frame.count < needed) return {};
        std::string path;
        const std::string cached = CachedIconPath(HashHex(frame.bytes, needed, static_cast<uint64_t>(frame.width) * 65537u + frame.height), path);
        if (!cached.empty() || path.empty()) return cached;

        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, frame.width, frame.height);
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(surface);
            return {};
        }
        cairo_surface_flush(surface);
        unsigned char* data = cairo_image_surface_get_data(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        const unsigned char* src = frame.bytes;
        for (int y = 0; y < frame.height; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(data + static_cast<size_t>(y) * stride);
            for (int x = 0; x < frame.width; ++x, src += 4) {
                const uint32_t a = src[0];
                const uint32_t r = src[1] * a / 255;
                const uint32_t g = src[2] * a / 255;
                const uint32_t b = src[3] * a / 255;
                row[x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
        cairo_surface_mark_dirty(surface);
        const std::string tmp = path + ".tmp";
        const cairo_status_t status = cairo_surface_write_to_png(surface, tmp.c_str());
        cairo_surface_destroy(surface);
        if (status != CAIRO_STATUS_SUCCESS) {
            std::error_code ec;
            fs::remove(PathFromUtf8(tmp), ec);
            return {};
        }
        return CommitTemporary(tmp, path) ? path : std::string();
    }

    // A dbusmenu entry's icon-data is already a PNG: written as it came.
    std::string PngBytesToFile(const unsigned char* bytes, size_t count) {
        if (!bytes || count < 8 || std::memcmp(bytes, "\x89PNG", 4) != 0) return {};
        std::string path;
        const std::string cached = CachedIconPath(HashHex(bytes, count, 0x6d656e75), path);
        if (!cached.empty() || path.empty()) return cached;
        const std::string tmp = path + ".tmp";
        std::FILE* out = OpenFileUtf8(tmp, "wb");
        if (!out) return {};
        const bool written = std::fwrite(bytes, 1, count, out) == count;
        std::fclose(out);
        if (!written) {
            std::error_code ec;
            fs::remove(PathFromUtf8(tmp), ec);
            return {};
        }
        return CommitTemporary(tmp, path) ? path : std::string();
    }

    // The frame of a(iiay) nearest the size the desktop draws, as a PNG.
    std::string PixmapsToPng(GVariant* pixmaps) {
        if (!pixmaps || !g_variant_is_of_type(pixmaps, G_VARIANT_TYPE("a(iiay)"))) return {};
        VariantRef best;
        int bestDistance = -1;
        const gsize frames = g_variant_n_children(pixmaps);
        for (gsize i = 0; i < frames; ++i) {
            VariantRef frame(g_variant_get_child_value(pixmaps, i));
            gint32 width = 0, height = 0;
            g_variant_get_child(frame.value, 0, "i", &width);
            g_variant_get_child(frame.value, 1, "i", &height);
            if (width <= 0 || height <= 0) continue;
            const int distance = std::abs(std::max(width, height) - kIconSize);
            // The nearest; of two as near, the larger (it scales down better).
            if (bestDistance < 0 || distance < bestDistance ||
                (distance == bestDistance && width > kIconSize)) {
                bestDistance = distance;
                best = std::move(frame);
            }
        }
        if (!best) return {};
        PixmapFrame frame;
        gint32 width = 0, height = 0;
        g_variant_get_child(best.value, 0, "i", &width);
        g_variant_get_child(best.value, 1, "i", &height);
        VariantRef bytes(g_variant_get_child_value(best.value, 2));
        gsize count = 0;
        frame.bytes = static_cast<const unsigned char*>(g_variant_get_fixed_array(bytes.value, &count, 1));
        frame.count = count;
        frame.width = width;
        frame.height = height;
        return PixmapToPng(frame);
    }

    // ===== ICON NAMES =====

    // The pixel size a theme directory's name states: "22x22", "22", or a
    // "22x22@2x"; 0 when it states none.
    int SizeInDirectoryName(const std::string& name) {
        size_t i = 0;
        while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) ++i;
        if (i == 0) return 0;
        if (i == name.size() || name[i] == 'x') return std::atoi(name.substr(0, i).c_str());
        return 0;
    }

    // The program's own icons (IconThemePath), walked for `name`.png or
    // .svg; the size nearest the desktop's wins, an exact PNG over an SVG.
    std::string FindIconInThemePath(const std::string& themePath, const std::string& name) {
        if (themePath.empty() || name.empty()) return {};
        std::error_code ec;
        const fs::path root = PathFromUtf8(themePath);
        if (!fs::is_directory(root, ec)) return {};
        std::string best;
        int bestScore = -1;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        const fs::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            if (it.depth() > 6) {
                it.disable_recursion_pending();
                continue;
            }
            const fs::path& file = it->path();
            if (!it->is_regular_file(ec)) continue;
            const std::string ext = PathToUtf8(file.extension());
            if (ext != ".png" && ext != ".svg") continue;
            if (PathToUtf8(file.stem()) != name) continue;
            int size = 0;
            for (fs::path part = file.parent_path(); !part.empty() && part != root; part = part.parent_path()) {
                size = SizeInDirectoryName(PathToUtf8(part.filename()));
                if (size > 0) break;
            }
            int score = 0;
            if (ext == ".svg") score = 1;
            else if (size == kIconSize) score = 0;
            else if (size > 0) score = 2 + std::abs(size - kIconSize);
            else score = 64;   // a flat directory of PNGs: any size
            if (bestScore < 0 || score < bestScore) {
                bestScore = score;
                best = PathToUtf8(file);
            }
        }
        return best;
    }

    // Host thread only: an icon name's file, remembered for the process.
    std::string ResolveIconName(const std::string& name, const std::string& themePath) {
        if (name.empty()) return {};
        if (name.front() == '/') {
            std::error_code ec;
            return fs::is_regular_file(PathFromUtf8(name), ec) ? name : std::string();
        }
        static std::map<std::string, std::string> remembered;
        const std::string key = themePath + "\n" + name;
        const auto found = remembered.find(key);
        if (found != remembered.end()) return found->second;
        std::string file = FindIconInThemePath(themePath, name);
        if (file.empty()) file = FindDesktopIconFile(name, kIconSize);
        remembered[key] = file;
        return file;
    }

    // "_Open" is "Open" with a mnemonic; "__" is a literal underscore.
    std::string StripMnemonic(const std::string& label) {
        std::string out;
        out.reserve(label.size());
        for (size_t i = 0; i < label.size(); ++i) {
            if (label[i] != '_') {
                out += label[i];
                continue;
            }
            if (i + 1 < label.size() && label[i + 1] == '_') {
                out += '_';
                ++i;
            }
        }
        return out;
    }

    // ===== ONE ITEM =====
    // TrayHostState is the one the header declares (DesktopShellBackend's),
    // defined below.

    struct HostedItem {
        TrayHostState* host = nullptr;
        std::string key;            // busName + path: the id
        std::string busName;
        std::string path;
        std::string iface = kItemInterfaceKde;
        // Host thread only.
        guint itemSubscription = 0;
        guint menuSubscription = 0;
        std::string subscribedMenuPath;
        bool reading = false;       // a GetAll is in flight ...
        bool readAgain = false;     // ... and a signal asked for another
        bool menuReading = false;
        bool menuReadAgain = false;
        bool menuAnnounced = false; // AboutToShow was sent for this menu
        guint32 menuRevision = 0;
        // Under the host's mutex.
        bool loaded = false;        // the first GetAll answered
        bool dead = false;
        bool hidden = false;        // Status "Passive"
        bool itemIsMenu = false;
        std::string menuPath;
        std::string id;
        std::string tooltip;
        std::string iconFile;
        std::string appClass;
        int processId = 0;
        std::vector<DesktopTrayMenuItem> menu;
    };

    using ItemRef = std::shared_ptr<HostedItem>;

} // namespace

// ===== THE HOST =====

struct TrayHostState {
    std::mutex mutex;
    std::vector<ItemRef> items;         // in the order registered
    GDBusConnection* bus = nullptr;     // nullptr once taken down; the other threads ref it under the mutex
    std::atomic<bool> wake{false};
    std::atomic<bool> changed{false};
    // The host thread's.
    GMainContext* context = nullptr;
    GCancellable* cancellable = nullptr;
    GDBusNodeInfo* nodeInfo = nullptr;
    guint objectId = 0;
    guint nameOwnerSubscription = 0;
    std::string hostName;
    bool takenDown = false;
};

namespace {

    // The bus for a call from any thread: a reference the caller drops,
    // or nullptr once the host is gone.
    GDBusConnection* BusRef(TrayHostState* state) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->bus) return nullptr;
        return G_DBUS_CONNECTION(g_object_ref(state->bus));
    }

    void MarkChanged(TrayHostState* state) {
        state->changed.store(true);
        if (state->context) g_main_context_wakeup(state->context);
    }

    void EmitWatcherSignal(TrayHostState* state, const char* name, GVariant* args) {
        if (!state->bus) {
            if (args) g_variant_unref(g_variant_ref_sink(args));
            return;
        }
        g_dbus_connection_emit_signal(state->bus, nullptr, kWatcherPath, kWatcherInterface, name, args, nullptr);
    }

    GVariant* RegisteredItemsVariant(TrayHostState* state) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
        std::lock_guard<std::mutex> lock(state->mutex);
        for (const ItemRef& item : state->items) {
            if (!item->dead) g_variant_builder_add(&builder, "s", item->key.c_str());
        }
        return g_variant_builder_end(&builder);
    }

    void EmitItemsChanged(TrayHostState* state) {
        if (!state->bus) return;
        GVariantBuilder changed;
        g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&changed, "{sv}", "RegisteredStatusNotifierItems", RegisteredItemsVariant(state));
        GVariantBuilder invalidated;
        g_variant_builder_init(&invalidated, G_VARIANT_TYPE("as"));
        g_dbus_connection_emit_signal(state->bus, nullptr, kWatcherPath, kPropertiesInterface, "PropertiesChanged",
                                      g_variant_new("(sa{sv}as)", kWatcherInterface, &changed, &invalidated), nullptr);
    }

    // ===== ASYNCHRONOUS CALLS TO AN ITEM =====
    // Each call carries its item; the answer runs on the host's context and
    // is dropped when the item or the host went meanwhile.

    struct Call {
        TrayHostState* state;
        ItemRef item;
    };

    void ReadItem(const ItemRef& item);
    void ReadMenu(const ItemRef& item, bool announce);

    // The item's menu (dbusmenu) layout, into DesktopTrayMenuItems.
    void ParseMenuChildren(GVariant* children, std::vector<DesktopTrayMenuItem>& out, int depth);

    void ParseMenuItem(GVariant* layout, std::vector<DesktopTrayMenuItem>& out, int depth) {
        if (!layout || !g_variant_is_of_type(layout, G_VARIANT_TYPE("(ia{sv}av)"))) return;
        gint32 id = 0;
        g_variant_get_child(layout, 0, "i", &id);
        VariantRef props(g_variant_get_child_value(layout, 1));
        VariantRef children(g_variant_get_child_value(layout, 2));
        if (depth == 0) {
            // The root is the menu itself: only its children are entries.
            ParseMenuChildren(children.value, out, 1);
            return;
        }

        DesktopTrayMenuItem entry;
        entry.id = id;
        bool visible = true;
        bool submenu = false;
        GVariantIter iter;
        g_variant_iter_init(&iter, props.value);
        const char* name = nullptr;
        GVariant* value = nullptr;
        while (g_variant_iter_next(&iter, "{&sv}", &name, &value)) {
            VariantRef hold(value);
            const std::string prop = name ? name : "";
            if (prop == "label") entry.label = StripMnemonic(StringOf(value));
            else if (prop == "enabled" && g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) entry.enabled = g_variant_get_boolean(value);
            else if (prop == "visible" && g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) visible = g_variant_get_boolean(value);
            else if (prop == "type") entry.separator = StringOf(value) == "separator";
            else if (prop == "toggle-type") {
                const std::string type = StringOf(value);
                entry.checkable = type == "checkmark" || type == "radio";
            } else if (prop == "toggle-state" && g_variant_is_of_type(value, G_VARIANT_TYPE_INT32)) entry.checked = g_variant_get_int32(value) == 1;
            else if (prop == "icon-name") {
                if (entry.iconFile.empty()) entry.iconFile = ResolveIconName(StringOf(value), "");
            } else if (prop == "icon-data" && g_variant_is_of_type(value, G_VARIANT_TYPE_BYTESTRING)) {
                gsize count = 0;
                const auto* bytes = static_cast<const unsigned char*>(g_variant_get_fixed_array(value, &count, 1));
                const std::string file = PngBytesToFile(bytes, count);
                if (!file.empty()) entry.iconFile = file;
            } else if (prop == "children-display") submenu = StringOf(value) == "submenu";
        }
        if (!entry.checkable) entry.checked = false;
        if (!visible) return;
        if (submenu && depth < 8) ParseMenuChildren(children.value, entry.children, depth + 1);
        out.push_back(std::move(entry));
    }

    void ParseMenuChildren(GVariant* children, std::vector<DesktopTrayMenuItem>& out, int depth) {
        if (!children || !g_variant_is_of_type(children, G_VARIANT_TYPE("av"))) return;
        const gsize count = g_variant_n_children(children);
        for (gsize i = 0; i < count; ++i) {
            VariantRef wrapped(g_variant_get_child_value(children, i));
            VariantRef child(g_variant_get_variant(wrapped.value));
            ParseMenuItem(child.value, out, depth);
        }
    }

    void OnLayoutRead(GObject* source, GAsyncResult* result, gpointer userData) {
        std::unique_ptr<Call> call(static_cast<Call*>(userData));
        GError* error = nullptr;
        VariantRef reply(g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error));
        if (error) g_error_free(error);
        TrayHostState* state = call->state;
        const ItemRef& item = call->item;
        if (state->takenDown || item->dead) return;
        item->menuReading = false;
        if (reply) {
            guint32 revision = 0;
            g_variant_get_child(reply.value, 0, "u", &revision);
            VariantRef layout(g_variant_get_child_value(reply.value, 1));
            std::vector<DesktopTrayMenuItem> menu;
            ParseMenuItem(layout.value, menu, 0);
            item->menuRevision = revision;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                item->menu = std::move(menu);
            }
            MarkChanged(state);
        }
        if (item->menuReadAgain) {
            item->menuReadAgain = false;
            ReadMenu(item, false);
        }
    }

    void CallGetLayout(const ItemRef& item) {
        TrayHostState* state = item->host;
        g_dbus_connection_call(state->bus, item->busName.c_str(), item->subscribedMenuPath.c_str(), kMenuInterface,
                               "GetLayout", g_variant_new("(ii@as)", 0, -1, g_variant_new_strv(nullptr, 0)),
                               G_VARIANT_TYPE("(u(ia{sv}av))"), G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs,
                               state->cancellable, OnLayoutRead, new Call{state, item});
    }

    void OnAboutToShow(GObject* source, GAsyncResult* result, gpointer userData) {
        std::unique_ptr<Call> call(static_cast<Call*>(userData));
        GError* error = nullptr;
        VariantRef reply(g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error));
        if (error) g_error_free(error);
        TrayHostState* state = call->state;
        const ItemRef& item = call->item;
        if (state->takenDown || item->dead) return;
        // Whatever it answered - a program that fills its menu now says
        // true, one that has no idea errors - the layout is read next.
        CallGetLayout(item);
    }

    // The menu's layout, read once at a time; a request while one is in
    // flight is remembered and served after it. `announce` sends
    // AboutToShow first, as a desktop opening the menu would - only on the
    // first read of a menu, since a program whose AboutToShow rebuilds the
    // menu (LayoutUpdated) would otherwise be read for ever.
    void ReadMenu(const ItemRef& item, bool announce) {
        TrayHostState* state = item->host;
        if (state->takenDown || item->dead || !state->bus || item->subscribedMenuPath.empty()) return;
        if (item->menuReading) {
            item->menuReadAgain = true;
            return;
        }
        item->menuReading = true;
        if (announce && !item->menuAnnounced) {
            item->menuAnnounced = true;
            g_dbus_connection_call(state->bus, item->busName.c_str(), item->subscribedMenuPath.c_str(), kMenuInterface,
                                   "AboutToShow", g_variant_new("(i)", 0), G_VARIANT_TYPE("(b)"),
                                   G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs, state->cancellable, OnAboutToShow,
                                   new Call{state, item});
            return;
        }
        CallGetLayout(item);
    }

    void OnMenuSignal(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* signalName,
                      GVariant* parameters, gpointer userData) {
        const ItemRef& item = *static_cast<ItemRef*>(userData);
        if (!item || item->dead || item->host->takenDown) return;
        const std::string name = signalName ? signalName : "";
        if (name == "LayoutUpdated") {
            guint32 revision = 0;
            if (parameters && g_variant_is_of_type(parameters, G_VARIANT_TYPE("(ui)"))) {
                g_variant_get_child(parameters, 0, "u", &revision);
            }
            // A revision already read says nothing new (a program's answer
            // to our own GetLayout, echoed as a signal).
            if (revision != 0 && revision <= item->menuRevision) return;
            ReadMenu(item, false);
        } else if (name == "ItemsPropertiesUpdated") {
            ReadMenu(item, false);
        }
    }

    void FreeItemRef(gpointer data) { delete static_cast<ItemRef*>(data); }

    // The menu's signals followed at its (new) path, and the menu read.
    void FollowMenu(const ItemRef& item) {
        TrayHostState* state = item->host;
        std::string menuPath;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            menuPath = item->menuPath;
        }
        if (menuPath == item->subscribedMenuPath) return;
        if (item->menuSubscription) {
            g_dbus_connection_signal_unsubscribe(state->bus, item->menuSubscription);
            item->menuSubscription = 0;
        }
        item->subscribedMenuPath = menuPath;
        item->menuAnnounced = false;
        item->menuRevision = 0;
        if (menuPath.empty()) {
            std::lock_guard<std::mutex> lock(state->mutex);
            item->menu.clear();
            return;
        }
        item->menuSubscription = g_dbus_connection_signal_subscribe(
            state->bus, item->busName.c_str(), kMenuInterface, nullptr, menuPath.c_str(), nullptr,
            G_DBUS_SIGNAL_FLAGS_NONE, OnMenuSignal, new ItemRef(item), FreeItemRef);
        ReadMenu(item, true);
    }

    // What GetAll answered, into the item.
    void ApplyProperties(const ItemRef& item, GVariant* dict) {
        std::string id, title, statusText, iconName, iconThemePath, attentionIconName, menuPath;
        std::string tooltipTitle, tooltipText;
        VariantRef iconPixmap, attentionIconPixmap;
        bool itemIsMenu = false;
        GVariantIter iter;
        g_variant_iter_init(&iter, dict);
        const char* name = nullptr;
        GVariant* value = nullptr;
        while (g_variant_iter_next(&iter, "{&sv}", &name, &value)) {
            VariantRef hold(value);
            const std::string prop = name ? name : "";
            if (prop == "Id") id = StringOf(value);
            else if (prop == "Title") title = StringOf(value);
            else if (prop == "Status") statusText = StringOf(value);
            else if (prop == "IconName") iconName = StringOf(value);
            else if (prop == "IconThemePath") iconThemePath = StringOf(value);
            else if (prop == "AttentionIconName") attentionIconName = StringOf(value);
            else if (prop == "IconPixmap") iconPixmap = VariantRef(g_variant_ref(value));
            else if (prop == "AttentionIconPixmap") attentionIconPixmap = VariantRef(g_variant_ref(value));
            else if (prop == "ItemIsMenu" && g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) itemIsMenu = g_variant_get_boolean(value);
            else if (prop == "Menu" && g_variant_is_of_type(value, G_VARIANT_TYPE_OBJECT_PATH)) menuPath = g_variant_get_string(value, nullptr);
            else if (prop == "ToolTip" && g_variant_is_of_type(value, G_VARIANT_TYPE("(sa(iiay)ss)"))) {
                VariantRef t(g_variant_get_child_value(value, 2));
                VariantRef d(g_variant_get_child_value(value, 3));
                tooltipTitle = StringOf(t.value);
                tooltipText = StringOf(d.value);
            }
        }
        // "/" is what a program with no menu says, as often as "".
        if (menuPath == "/") menuPath.clear();

        // The picture: the attention one while the program asks for
        // attention, by theme name where the name resolves, else the pixmap.
        const bool attention = statusText == "NeedsAttention";
        std::string iconFile;
        if (attention) {
            iconFile = ResolveIconName(attentionIconName, iconThemePath);
            if (iconFile.empty()) iconFile = PixmapsToPng(attentionIconPixmap.value);
        }
        if (iconFile.empty()) iconFile = ResolveIconName(iconName, iconThemePath);
        if (iconFile.empty()) iconFile = PixmapsToPng(iconPixmap.value);

        std::string tooltip = tooltipTitle;
        if (!tooltipText.empty()) tooltip += (tooltip.empty() ? "" : ": ") + tooltipText;
        if (tooltip.empty()) tooltip = title;

        TrayHostState* state = item->host;
        std::lock_guard<std::mutex> lock(state->mutex);
        item->loaded = true;
        item->id = id;
        item->hidden = statusText == "Passive";
        item->itemIsMenu = itemIsMenu;
        item->menuPath = menuPath;
        item->tooltip = tooltip;
        item->iconFile = iconFile;
        if (!id.empty()) item->appClass = id;
        else if (item->appClass.empty()) item->appClass = ProgramNameOfProcess(item->processId);
    }

    void CallGetAll(const ItemRef& item);

    void OnPropertiesRead(GObject* source, GAsyncResult* result, gpointer userData) {
        std::unique_ptr<Call> call(static_cast<Call*>(userData));
        GError* error = nullptr;
        VariantRef reply(g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error));
        const bool failed = error != nullptr;
        if (error) g_error_free(error);
        TrayHostState* state = call->state;
        const ItemRef& item = call->item;
        if (state->takenDown || item->dead) return;
        VariantRef dict;
        if (reply) dict = VariantRef(g_variant_get_child_value(reply.value, 0));
        const bool empty = !dict || g_variant_n_children(dict.value) == 0;
        if ((failed || empty) && item->iface == kItemInterfaceKde && !item->loaded) {
            // Not a KDE item: the same properties under the freedesktop name.
            item->iface = kItemInterfaceFreedesktop;
            CallGetAll(item);
            return;
        }
        item->reading = false;
        if (dict && !empty) {
            ApplyProperties(item, dict.value);
            MarkChanged(state);
            FollowMenu(item);
        }
        if (item->readAgain) {
            item->readAgain = false;
            ReadItem(item);
        }
    }

    void CallGetAll(const ItemRef& item) {
        TrayHostState* state = item->host;
        g_dbus_connection_call(state->bus, item->busName.c_str(), item->path.c_str(), kPropertiesInterface, "GetAll",
                               g_variant_new("(s)", item->iface.c_str()), G_VARIANT_TYPE("(a{sv})"),
                               G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs, state->cancellable, OnPropertiesRead,
                               new Call{state, item});
    }

    // The item's properties, read once at a time; a signal while a read is
    // in flight is served by another read after it.
    void ReadItem(const ItemRef& item) {
        TrayHostState* state = item->host;
        if (state->takenDown || item->dead || !state->bus) return;
        if (item->reading) {
            item->readAgain = true;
            return;
        }
        item->reading = true;
        CallGetAll(item);
    }

    void OnProcessIdRead(GObject* source, GAsyncResult* result, gpointer userData) {
        std::unique_ptr<Call> call(static_cast<Call*>(userData));
        GError* error = nullptr;
        VariantRef reply(g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error));
        if (error) g_error_free(error);
        TrayHostState* state = call->state;
        const ItemRef& item = call->item;
        if (state->takenDown || item->dead || !reply) return;
        guint32 pid = 0;
        g_variant_get(reply.value, "(u)", &pid);
        std::lock_guard<std::mutex> lock(state->mutex);
        item->processId = static_cast<int>(pid);
        if (item->appClass.empty()) item->appClass = ProgramNameOfProcess(item->processId);
    }

    void OnItemSignal(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* signalName,
                      GVariant*, gpointer userData) {
        const ItemRef& item = *static_cast<ItemRef*>(userData);
        if (!item || item->dead || item->host->takenDown) return;
        const std::string name = signalName ? signalName : "";
        if (name == "NewIcon" || name == "NewAttentionIcon" || name == "NewToolTip" || name == "NewTitle" ||
            name == "NewStatus" || name == "NewMenu" || name == "NewIconThemePath") {
            ReadItem(item);
        }
    }

    // ===== REGISTERING AND DROPPING ITEMS =====

    // `service` as RegisterStatusNotifierItem gets it, into a bus name and
    // an object path.
    void SplitService(const std::string& service, const std::string& sender, std::string& busName, std::string& path) {
        if (service.empty() || service.front() == '/') {
            busName = sender;
            path = service.empty() ? kItemDefaultPath : service;
            return;
        }
        const size_t slash = service.find('/');
        if (slash == std::string::npos) {
            busName = service;
            path = kItemDefaultPath;
            return;
        }
        busName = service.substr(0, slash);
        path = service.substr(slash);
    }

    void RegisterItem(TrayHostState* state, const std::string& service, const std::string& sender) {
        std::string busName, path;
        SplitService(service, sender, busName, path);
        if (busName.empty() || !g_dbus_is_name(busName.c_str()) || !g_variant_is_object_path(path.c_str())) return;
        const std::string key = busName + path;
        ItemRef item;
        bool fresh = false;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            for (const ItemRef& existing : state->items) {
                if (!existing->dead && existing->key == key) item = existing;
            }
            if (!item) {
                item = std::make_shared<HostedItem>();
                item->host = state;
                item->key = key;
                item->busName = busName;
                item->path = path;
                state->items.push_back(item);
                fresh = true;
            }
        }
        if (fresh) {
            item->itemSubscription = g_dbus_connection_signal_subscribe(
                state->bus, busName.c_str(), nullptr, nullptr, path.c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
                OnItemSignal, new ItemRef(item), FreeItemRef);
            g_dbus_connection_call(state->bus, kBusName, kBusPath, kBusInterface, "GetConnectionUnixProcessID",
                                   g_variant_new("(s)", busName.c_str()), G_VARIANT_TYPE("(u)"),
                                   G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs, state->cancellable, OnProcessIdRead,
                                   new Call{state, item});
            EmitWatcherSignal(state, "StatusNotifierItemRegistered", g_variant_new("(s)", key.c_str()));
            EmitItemsChanged(state);
        }
        // A program registering again has usually started over: read afresh.
        ReadItem(item);
    }

    // Host thread: the item's subscriptions dropped and the item taken out
    // of the list; its pending answers see `dead` and do nothing.
    void DropItem(TrayHostState* state, const ItemRef& item) {
        if (item->itemSubscription) g_dbus_connection_signal_unsubscribe(state->bus, item->itemSubscription);
        if (item->menuSubscription) g_dbus_connection_signal_unsubscribe(state->bus, item->menuSubscription);
        item->itemSubscription = 0;
        item->menuSubscription = 0;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            item->dead = true;
            state->items.erase(std::remove(state->items.begin(), state->items.end(), item), state->items.end());
        }
        EmitWatcherSignal(state, "StatusNotifierItemUnregistered", g_variant_new("(s)", item->key.c_str()));
        EmitItemsChanged(state);
        MarkChanged(state);
    }

    void OnNameOwnerChanged(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                            GVariant* parameters, gpointer userData) {
        auto* state = static_cast<TrayHostState*>(userData);
        if (state->takenDown || !parameters || !g_variant_is_of_type(parameters, G_VARIANT_TYPE("(sss)"))) return;
        const gchar* name = nullptr;
        const gchar* oldOwner = nullptr;
        const gchar* newOwner = nullptr;
        g_variant_get(parameters, "(&s&s&s)", &name, &oldOwner, &newOwner);
        if (!name || !newOwner || *newOwner != '\0') return;
        std::vector<ItemRef> gone;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            for (const ItemRef& item : state->items) {
                if (item->busName == name) gone.push_back(item);
            }
        }
        for (const ItemRef& item : gone) DropItem(state, item);
    }

    // ===== THE WATCHER OBJECT =====

    void OnWatcherMethod(GDBusConnection*, const gchar* sender, const gchar*, const gchar*, const gchar* method,
                         GVariant* parameters, GDBusMethodInvocation* invocation, gpointer userData) {
        auto* state = static_cast<TrayHostState*>(userData);
        const std::string name = method ? method : "";
        if (name == "RegisterStatusNotifierItem") {
            const gchar* service = nullptr;
            g_variant_get(parameters, "(&s)", &service);
            if (!state->takenDown) RegisterItem(state, service ? service : "", sender ? sender : "");
            g_dbus_method_invocation_return_value(invocation, nullptr);
            return;
        }
        if (name == "RegisterStatusNotifierHost") {
            // Another host beside this one is welcome to the same items.
            if (!state->takenDown) EmitWatcherSignal(state, "StatusNotifierHostRegistered", nullptr);
            g_dbus_method_invocation_return_value(invocation, nullptr);
            return;
        }
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                              "No such method: %s", name.c_str());
    }

    GVariant* OnWatcherProperty(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* property,
                                GError** error, gpointer userData) {
        auto* state = static_cast<TrayHostState*>(userData);
        const std::string name = property ? property : "";
        if (name == "RegisteredStatusNotifierItems") return RegisteredItemsVariant(state);
        if (name == "IsStatusNotifierHostRegistered") return g_variant_new_boolean(TRUE);
        if (name == "ProtocolVersion") return g_variant_new_int32(0);
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "No such property: %s", name.c_str());
        return nullptr;
    }

    const GDBusInterfaceVTable kWatcherVTable = {OnWatcherMethod, OnWatcherProperty, nullptr, {nullptr}};

    // RequestName, synchronously: whether this connection became the name's
    // primary owner. Never queued, so a name another program holds is
    // refused at once.
    bool OwnName(GDBusConnection* bus, const char* name) {
        GError* error = nullptr;
        VariantRef reply(g_dbus_connection_call_sync(bus, kBusName, kBusPath, kBusInterface, "RequestName",
                                                     g_variant_new("(su)", name, kNameFlagDoNotQueue),
                                                     G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs,
                                                     nullptr, &error));
        if (error) g_error_free(error);
        if (!reply) return false;
        guint32 result = 0;
        g_variant_get(reply.value, "(u)", &result);
        return result == kNameReplyPrimaryOwner;
    }

    // Who owns `name` on the bus, for the refusal; "" when nobody.
    std::string NameOwner(GDBusConnection* bus, const char* name) {
        GError* error = nullptr;
        VariantRef reply(g_dbus_connection_call_sync(bus, kBusName, kBusPath, kBusInterface, "GetNameOwner",
                                                     g_variant_new("(s)", name), G_VARIANT_TYPE("(s)"),
                                                     G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs, nullptr, &error));
        if (error) g_error_free(error);
        if (!reply) return {};
        const gchar* owner = nullptr;
        g_variant_get(reply.value, "(&s)", &owner);
        return owner ? owner : "";
    }

    int ProcessOfName(GDBusConnection* bus, const std::string& name) {
        GError* error = nullptr;
        VariantRef reply(g_dbus_connection_call_sync(bus, kBusName, kBusPath, kBusInterface,
                                                     "GetConnectionUnixProcessID", g_variant_new("(s)", name.c_str()),
                                                     G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs,
                                                     nullptr, &error));
        if (error) g_error_free(error);
        if (!reply) return 0;
        guint32 pid = 0;
        g_variant_get(reply.value, "(u)", &pid);
        return static_cast<int>(pid);
    }

    // The host thread's part of the take-down: the items' subscriptions,
    // the object, the connection - which releases the names - and the
    // context, with the answers still in flight drained so nothing leaks.
    void TakeDown(TrayHostState* state) {
        state->takenDown = true;
        if (state->cancellable) g_cancellable_cancel(state->cancellable);
        std::vector<ItemRef> items;
        GDBusConnection* bus = nullptr;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            items.swap(state->items);
            bus = state->bus;
            state->bus = nullptr;
        }
        if (bus) {
            for (const ItemRef& item : items) {
                if (item->itemSubscription) g_dbus_connection_signal_unsubscribe(bus, item->itemSubscription);
                if (item->menuSubscription) g_dbus_connection_signal_unsubscribe(bus, item->menuSubscription);
                item->itemSubscription = 0;
                item->menuSubscription = 0;
                item->dead = true;
            }
            if (state->nameOwnerSubscription) g_dbus_connection_signal_unsubscribe(bus, state->nameOwnerSubscription);
            state->nameOwnerSubscription = 0;
            if (state->objectId) g_dbus_connection_unregister_object(bus, state->objectId);
            state->objectId = 0;
            // A private connection: closing it drops the names, so the
            // programs see the watcher go and wait for the next.
            g_dbus_connection_close_sync(bus, nullptr, nullptr);
            g_object_unref(bus);
        }
        items.clear();
        if (state->context) {
            for (int i = 0; i < 100 && g_main_context_iteration(state->context, FALSE); ++i) {
            }
            g_main_context_pop_thread_default(state->context);
            g_main_context_unref(state->context);
            state->context = nullptr;
        }
        if (state->cancellable) g_object_unref(state->cancellable);
        state->cancellable = nullptr;
    }

    // The entry `itemId` in a menu tree.
    bool MenuHasItem(const std::vector<DesktopTrayMenuItem>& menu, int32_t itemId) {
        for (const DesktopTrayMenuItem& entry : menu) {
            if (entry.id == itemId || MenuHasItem(entry.children, itemId)) return true;
        }
        return false;
    }

} // namespace

TrayHostState* TrayHostOpen(std::string& error) {
    if (!HaveSessionBus()) {
        error = "There is no D-Bus session bus, which the notification area's programs talk over.";
        return nullptr;
    }
    GError* gerror = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &gerror);
    if (!bus) {
        error = std::string("The D-Bus session bus cannot be reached: ") + (gerror ? gerror->message : "unknown error");
        if (gerror) g_error_free(gerror);
        return nullptr;
    }
    const std::string owner = NameOwner(bus, kWatcherName);
    if (!owner.empty()) {
        const int pid = ProcessOfName(bus, owner);
        const std::string program = ProgramNameOfProcess(pid);
        if (pid == static_cast<int>(getpid())) {
            error = "This program already hosts the notification area.";
        } else {
            error = "Another notification area runs (" + (program.empty() ? owner : program + ", " + owner) +
                    "), and only one can.";
        }
        g_object_unref(bus);
        return nullptr;
    }
    g_object_unref(bus);
    return new TrayHostState();
}

bool TrayHostAttach(TrayHostState* state, std::string& error) {
    if (!state) return false;
    // Everything the bus delivers - registrations, the programs' signals,
    // the answers to the reads - lands on this thread's context, which
    // TrayHostWait iterates.
    state->context = g_main_context_new();
    g_main_context_push_thread_default(state->context);
    state->cancellable = g_cancellable_new();

    GError* gerror = nullptr;
    gchar* address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, nullptr, &gerror);
    GDBusConnection* bus = nullptr;
    if (address) {
        // A connection of its own rather than the process's shared one:
        // closing it releases the names and every registration at once,
        // and a desktop's other D-Bus traffic never queues behind the tray.
        bus = g_dbus_connection_new_for_address_sync(
            address,
            static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                              G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
            nullptr, nullptr, &gerror);
        g_free(address);
    }
    if (!bus) {
        error = std::string("The D-Bus session bus cannot be reached: ") + (gerror ? gerror->message : "unknown error");
        if (gerror) g_error_free(gerror);
        TakeDown(state);
        return false;
    }
    g_dbus_connection_set_exit_on_close(bus, FALSE);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->bus = bus;
    }

    // The object first, so a program that reacts to the name appearing
    // finds it there.
    state->nodeInfo = g_dbus_node_info_new_for_xml(kWatcherIntrospection, nullptr);
    state->objectId = state->nodeInfo
                          ? g_dbus_connection_register_object(bus, kWatcherPath, state->nodeInfo->interfaces[0],
                                                              &kWatcherVTable, state, nullptr, &gerror)
                          : 0;
    if (!state->objectId) {
        error = std::string("The StatusNotifierWatcher object could not be exported: ") +
                (gerror ? gerror->message : "unknown error");
        if (gerror) g_error_free(gerror);
        TakeDown(state);
        return false;
    }
    state->nameOwnerSubscription = g_dbus_connection_signal_subscribe(
        bus, kBusName, kBusInterface, "NameOwnerChanged", kBusPath, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        OnNameOwnerChanged, state, nullptr);

    if (!OwnName(bus, kWatcherName)) {
        error = "Another notification area started a moment ago (" + NameOwner(bus, kWatcherName) + ").";
        TakeDown(state);
        return false;
    }
    // The host's own name, by which the watcher's clients can tell a host
    // is there; this process is both.
    state->hostName = std::string("org.kde.StatusNotifierHost-") + std::to_string(getpid());
    OwnName(bus, state->hostName.c_str());
    EmitWatcherSignal(state, "StatusNotifierHostRegistered", nullptr);
    // The programs already running watch for the watcher's name and
    // register their items now; nothing more to ask of them.
    return true;
}

bool TrayHostWait(TrayHostState* state) {
    if (!state || !state->context) return false;
    for (;;) {
        if (state->wake.load()) {
            TakeDown(state);
            return false;
        }
        if (state->changed.exchange(false)) return true;
        g_main_context_iteration(state->context, TRUE);
    }
}

void TrayHostWake(TrayHostState* state) {
    if (!state) return;
    state->wake.store(true);
    // Sticky: a wakeup before the iteration starts makes it return at once.
    if (state->context) g_main_context_wakeup(state->context);
}

void TrayHostClose(TrayHostState* state) {
    if (!state) return;
    // TakeDown ran on the host's thread when its wait ended, or when the
    // attach failed; a host that never attached made nothing.
    if (state->nodeInfo) g_dbus_node_info_unref(state->nodeInfo);
    delete state;
}

std::vector<DesktopTrayIcon> TrayHostIcons(TrayHostState* state) {
    std::vector<DesktopTrayIcon> icons;
    if (!state) return icons;
    std::lock_guard<std::mutex> lock(state->mutex);
    for (const ItemRef& item : state->items) {
        if (item->dead || item->hidden || !item->loaded) continue;
        DesktopTrayIcon out;
        out.id = item->key;
        out.tooltip = item->tooltip;
        out.iconFile = item->iconFile;
        out.appClass = item->appClass;
        out.processId = item->processId;
        out.menu = item->menu;
        icons.push_back(std::move(out));
    }
    return icons;
}

bool TrayHostClick(TrayHostState* state, const std::string& id, TrayClick button) {
    if (!state) return false;
    std::string busName, path, iface;
    bool itemIsMenu = false, hasMenu = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        for (const ItemRef& item : state->items) {
            if (item->dead || item->key != id) continue;
            busName = item->busName;
            path = item->path;
            iface = item->iface;
            itemIsMenu = item->itemIsMenu;
            hasMenu = !item->menuPath.empty();
        }
    }
    if (busName.empty()) return false;
    const char* method = nullptr;
    switch (button) {
        case TrayClick::Left:
            // A program that is only a menu opens it on any click.
            method = itemIsMenu ? "ContextMenu" : "Activate";
            break;
        case TrayClick::Middle:
            method = "SecondaryActivate";
            break;
        case TrayClick::Right:
            // With a menu handed over as data, the desktop draws it; the
            // program's own ContextMenu is for the ones without.
            if (hasMenu) return true;
            method = "ContextMenu";
            break;
    }
    GDBusConnection* bus = BusRef(state);
    if (!bus) return false;
    int x = 0, y = 0;
    X11::QueryPointerPosition(x, y);
    // No reply wanted: a program that has hung never holds a click up.
    g_dbus_connection_call(bus, busName.c_str(), path.c_str(), iface.c_str(), method, g_variant_new("(ii)", x, y),
                           nullptr, G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs, nullptr, nullptr, nullptr);
    g_object_unref(bus);
    return true;
}

bool TrayHostMenuActivate(TrayHostState* state, const std::string& id, int32_t itemId) {
    if (!state) return false;
    std::string busName, menuPath;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        for (const ItemRef& item : state->items) {
            if (item->dead || item->key != id || item->menuPath.empty()) continue;
            if (!MenuHasItem(item->menu, itemId)) continue;
            busName = item->busName;
            menuPath = item->menuPath;
        }
    }
    if (busName.empty()) return false;
    GDBusConnection* bus = BusRef(state);
    if (!bus) return false;
    g_dbus_connection_call(bus, busName.c_str(), menuPath.c_str(), kMenuInterface, "Event",
                           g_variant_new("(isvu)", itemId, "clicked", g_variant_new_int32(0),
                                         static_cast<guint32>(std::time(nullptr))),
                           nullptr, G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs, nullptr, nullptr, nullptr);
    g_object_unref(bus);
    return true;
}

} // namespace DesktopShellBackend
} // namespace UltraCanvas
