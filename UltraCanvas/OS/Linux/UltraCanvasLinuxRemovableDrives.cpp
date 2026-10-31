// OS/Linux/UltraCanvasLinuxRemovableDrives.cpp
// The Linux backend of UltraCanvasRemovableDrives: UDisks2 on the system
// bus, which every desktop's file manager uses to mount and remove drives.
//
//   - List: the Drive objects that hang off a USB, SD/MMC or FireWire bus or
//     call themselves removable, with the Block objects that belong to them
//     (directly, or through an unlocked encrypted container) and those
//     blocks' mount points. A drive with a block mounted at /, /boot or
//     /usr is the system's own and is left out.
//   - SafelyRemove: Filesystem.Unmount on every mounted filesystem of the
//     drive (which writes out what the kernel still held), Encrypted.Lock on
//     every unlocked container, then Drive.PowerOff - or Eject for a disc or
//     a card reader that cannot power off. polkit lets the active local
//     session do all of it without a password.
// Version: 1.0.0
// Last Modified: 2026-10-31
// Author: UltraCanvas Framework

#include "UltraCanvasRemovableDrivesBackend.h"

#if defined(__linux__) && !defined(__ANDROID__)

#include "UltraCanvasLinuxSystemBus.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace RemovableDrivesBackend {

namespace {

    using namespace LinuxSystemBus;

    const char* kUDisks = "org.freedesktop.UDisks2";
    const char* kUDisksPath = "/org/freedesktop/UDisks2";
    const char* kDrive = "org.freedesktop.UDisks2.Drive";
    const char* kBlock = "org.freedesktop.UDisks2.Block";
    const char* kFilesystem = "org.freedesktop.UDisks2.Filesystem";
    const char* kEncrypted = "org.freedesktop.UDisks2.Encrypted";
    constexpr int kUnmountTimeoutMs = 120000;   // a slow stick with much to write out

    struct DriveObject {
        std::string path;
        std::string vendor, model, bus;
        uint64_t size = 0;
        bool removable = false;
        bool canPowerOff = false;
        bool ejectable = false;
    };

    struct BlockObject {
        std::string path;
        std::string drive;              // the Drive it is on ("/" when none)
        std::string backing;            // CryptoBackingDevice: the encrypted block under it
        std::string label;
        std::vector<std::string> mountPoints;
        bool encrypted = false;         // an Encrypted container
        bool unlocked = false;          // ... with a cleartext device open
    };

    struct Tree {
        std::map<std::string, DriveObject> drives;
        std::vector<BlockObject> blocks;
    };

    std::string StringOf(GVariant* dict, const char* key) {
        const gchar* text = nullptr;
        if (g_variant_lookup(dict, key, "&s", &text) || g_variant_lookup(dict, key, "&o", &text)) {
            return text ? text : "";
        }
        return "";
    }

    bool BoolOf(GVariant* dict, const char* key) {
        gboolean value = FALSE;
        return g_variant_lookup(dict, key, "b", &value) && value;
    }

    // a{sa{sv}} interfaces -> the one named, or nullptr (to unref).
    GVariant* Interface(GVariant* interfaces, const char* name) {
        return g_variant_lookup_value(interfaces, name, G_VARIANT_TYPE("a{sv}"));
    }

    bool ReadTree(Tree& tree, std::string& error) {
        GVariant* reply = Call(kUDisks, kUDisksPath, "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
                               nullptr, G_VARIANT_TYPE("(a{oa{sa{sv}}})"), error);
        if (!reply) {
            error = "UDisks does not answer (" + error + ").";
            return false;
        }
        GVariant* objects = g_variant_get_child_value(reply, 0);
        GVariantIter iter;
        g_variant_iter_init(&iter, objects);
        const gchar* path = nullptr;
        GVariant* interfaces = nullptr;
        while (g_variant_iter_loop(&iter, "{&o@a{sa{sv}}}", &path, &interfaces)) {
            if (GVariant* drive = Interface(interfaces, kDrive)) {
                DriveObject entry;
                entry.path = path;
                entry.vendor = StringOf(drive, "Vendor");
                entry.model = StringOf(drive, "Model");
                entry.bus = StringOf(drive, "ConnectionBus");
                guint64 size = 0;
                if (g_variant_lookup(drive, "Size", "t", &size)) entry.size = size;
                entry.removable = BoolOf(drive, "Removable") || BoolOf(drive, "MediaRemovable");
                entry.canPowerOff = BoolOf(drive, "CanPowerOff");
                entry.ejectable = BoolOf(drive, "Ejectable");
                tree.drives[entry.path] = entry;
                g_variant_unref(drive);
            }
            if (GVariant* block = Interface(interfaces, kBlock)) {
                BlockObject entry;
                entry.path = path;
                entry.drive = StringOf(block, "Drive");
                entry.backing = StringOf(block, "CryptoBackingDevice");
                entry.label = StringOf(block, "IdLabel");
                g_variant_unref(block);
                if (GVariant* filesystem = Interface(interfaces, kFilesystem)) {
                    GVariant* points = g_variant_lookup_value(filesystem, "MountPoints", G_VARIANT_TYPE("aay"));
                    if (points) {
                        const gsize count = g_variant_n_children(points);
                        for (gsize i = 0; i < count; ++i) {
                            GVariant* point = g_variant_get_child_value(points, i);
                            std::string text = BytesToString(point);
                            while (!text.empty() && text.back() == '\0') text.pop_back();
                            if (!text.empty()) entry.mountPoints.push_back(text);
                            g_variant_unref(point);
                        }
                        g_variant_unref(points);
                    }
                    g_variant_unref(filesystem);
                }
                if (GVariant* encrypted = Interface(interfaces, kEncrypted)) {
                    entry.encrypted = true;
                    const std::string cleartext = StringOf(encrypted, "CleartextDevice");
                    entry.unlocked = !cleartext.empty() && cleartext != "/";
                    g_variant_unref(encrypted);
                }
                tree.blocks.push_back(entry);
            }
        }
        g_variant_unref(objects);
        g_variant_unref(reply);
        return true;
    }

    // The drive a block is on, following an unlocked container down to the
    // encrypted block that holds it.
    std::string DriveOf(const Tree& tree, const BlockObject& block) {
        if (!block.drive.empty() && block.drive != "/") return block.drive;
        if (block.backing.empty() || block.backing == "/") return "";
        for (const BlockObject& other : tree.blocks) {
            if (other.path == block.backing) return DriveOf(tree, other);
        }
        return "";
    }

    std::string BusName(const std::string& bus) {
        if (bus == "usb") return "USB";
        if (bus == "sdio") return "SD card";
        if (bus == "ieee1394") return "FireWire";
        return "";
    }

    bool IsSystemMount(const std::string& point) {
        return point == "/" || point == "/boot" || point == "/boot/efi" || point == "/usr" || point == "/var" ||
               point == "/home";
    }

    bool Removable(const DriveObject& drive) {
        return drive.removable || !BusName(drive.bus).empty();
    }

} // namespace

std::string BackendName() { return "udisks2"; }

std::vector<RemovableDrive> List(std::string& error) {
    std::vector<RemovableDrive> out;
    Tree tree;
    if (!ReadTree(tree, error)) return out;
    std::set<std::string> system;
    for (const BlockObject& block : tree.blocks) {
        for (const std::string& point : block.mountPoints) {
            if (IsSystemMount(point)) system.insert(DriveOf(tree, block));
        }
    }
    for (const auto& [path, drive] : tree.drives) {
        if (!Removable(drive) || system.count(path)) continue;
        RemovableDrive entry;
        entry.id = path;
        entry.name = drive.vendor.empty() ? drive.model
                     : drive.model.empty() ? drive.vendor
                                           : drive.vendor + " " + drive.model;
        entry.bus = BusName(drive.bus);
        entry.sizeBytes = drive.size;
        for (const BlockObject& block : tree.blocks) {
            if (DriveOf(tree, block) != path) continue;
            for (const std::string& point : block.mountPoints) {
                entry.mountPoints.push_back(point);
                entry.labels.push_back(block.label);
            }
        }
        out.push_back(entry);
    }
    return out;
}

bool SafelyRemove(const std::string& id, std::string& error) {
    Tree tree;
    if (!ReadTree(tree, error)) return false;
    auto found = tree.drives.find(id);
    if (found == tree.drives.end()) {
        error = "The drive is no longer plugged in.";
        return false;
    }
    const DriveObject& drive = found->second;

    // 1. Unmount every filesystem on it, the cleartext ones included.
    for (const BlockObject& block : tree.blocks) {
        if (block.mountPoints.empty() || DriveOf(tree, block) != id) continue;
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
        if (!CallVoid(kUDisks, block.path, kFilesystem, "Unmount", g_variant_new("(a{sv})", &options), error,
                      kUnmountTimeoutMs)) {
            const std::string where = block.mountPoints.front();
            if (error.find("busy") != std::string::npos || error.find("Busy") != std::string::npos) {
                error = "A program still has a file open on " + where + ". Close it and try again.";
            } else {
                error = "Could not unmount " + where + ": " + error;
            }
            return false;
        }
    }
    // 2. Lock what was unlocked.
    for (const BlockObject& block : tree.blocks) {
        if (!block.encrypted || !block.unlocked || DriveOf(tree, block) != id) continue;
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
        if (!CallVoid(kUDisks, block.path, kEncrypted, "Lock", g_variant_new("(a{sv})", &options), error)) {
            error = "Could not lock the encrypted volume: " + error;
            return false;
        }
    }
    // 3. Power it off - or eject it. Everything is written out by now, so
    //    a drive that can do neither is still safe to pull.
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
    std::string ignored;
    if (drive.canPowerOff) {
        CallVoid(kUDisks, id, kDrive, "PowerOff", g_variant_new("(a{sv})", &options), ignored, kUnmountTimeoutMs);
    } else if (drive.ejectable) {
        CallVoid(kUDisks, id, kDrive, "Eject", g_variant_new("(a{sv})", &options), ignored, kUnmountTimeoutMs);
    } else {
        g_variant_builder_clear(&options);
    }
    error.clear();
    return true;
}

} // namespace RemovableDrivesBackend
} // namespace UltraCanvas

#endif // __linux__ && !__ANDROID__
