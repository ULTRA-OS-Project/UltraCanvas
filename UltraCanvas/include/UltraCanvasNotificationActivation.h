// include/UltraCanvasNotificationActivation.h
// A click on one of an application's notifications reaching that application -
// even when the program that put the notification on screen has quit since,
// and even when the application itself is not running.
//
// On Windows the notifications of the bus are toasts under the posting
// application's own AppUserModelID (the windows-presenter, UltraMessage). While
// the process that showed a toast runs, a click on it comes back over the bus.
// Once that process has quit, the toast still waits in the Action Center, and
// Windows hands a click on it to the COM activator registered for the
// AppUserModelID - starting the program that registered it when it is not
// running. RegisterNotificationActivator makes the calling application that
// activator, and the toast carries what the application needs to act on the
// click by itself: the notification's `launch` string (SystemNotification).
//
//   // Once, early in the start - Windows waits for the registration when it
//   // started the program for a click:
//   UltraCanvas::RegisterNotificationActivator("org.ultraos.ultramail",
//       [](const UltraCanvas::NotificationActivation& click) {
//           OpenMessage(click.launch);   // on the UI thread
//       });
//
// Everything the framework writes for a notification identity is listed under
// HKCU\Software\UltraCanvas\Notifications (one value per AppUserModelID, its
// activator's CLSID as the data), which the uninstaller walks to remove it all;
// RemoveNotificationRegistrations() does the same from code - UOS-Settings'
// Notifications page and `UOS-Settings --remove-notification-registrations`,
// which the ZIP package's uc-remove-registrations.cmd runs, since a folder
// unpacked from a ZIP has no uninstaller. Elsewhere than
// Windows there is nothing to register: the notification service reports the
// click over the bus while the application runs, and both calls do nothing.
//
// From an MSIX package Windows reads the activator from the package manifest
// instead - a packaged program's HKEY_CURRENT_USER writes stay inside the
// package - so RegisterNotificationActivator writes nothing there and only
// serves the class, and the toasts are shown under the application's package
// identity. The manifest's declaration comes from NOTIFICATION_ACTIVATORS in
// scripts/make_msix_layout.py, which scripts/check_notification_activators.py
// holds to the applications that call RegisterNotificationActivator: a new
// application that calls it adds its row there. See
// Docs/UltraCanvas/UltraCanvasWindowsMSIX.md.
//
// The helpers below are platform-neutral and header-only, so the presenter
// that writes a toast and the application that reads the click agree on the
// format without linking each other.
// Version: 1.2.0 - from an MSIX package: the manifest declares the activator
// Version: 1.1.0 - ListNotificationRegistrations; RemoveNotificationRegistrations
//                 says how many it removed
// Version: 1.0.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {

    // What the user clicked.
    struct NotificationActivation {
        std::string notificationId;   // the bus message that was shown; "" when not known
        std::string actionId;         // "default" for the notification itself, a button's id,
                                      // "" for a click on a notification without a default action
        std::string launch;           // the notification's own `launch` string, "" when it had none
    };

    // Windows: registers this executable as the activator of the toasts shown
    // under `appId` (HKCU: the CLSID's LocalServer32 names this program with
    // -ToastActivated, the AppUserModelID's CustomActivator names the CLSID)
    // and starts serving it. From an MSIX package it only serves it: the
    // package manifest names the class (scripts/make_msix_layout.py). `onActivated` runs on the UI thread
    // (PostToUIThread) for every click Windows hands over. Call it once, early.
    // False when nothing could be registered; elsewhere it does nothing and
    // returns false.
    bool RegisterNotificationActivator(const std::string& appId,
                                       std::function<void(const NotificationActivation&)> onActivated);

    // One notification identity the framework registered for this user.
    struct NotificationRegistration {
        std::string appId;              // the AppUserModelID, "org.ultraos.ultramail"
        std::string activatorClassId;   // its activator's CLSID, "" when none registered one
    };

    // Windows: the notification identities the framework registered for this
    // user, as the list under HKCU\Software\UltraCanvas\Notifications names
    // them, sorted by ID. Empty elsewhere.
    std::vector<NotificationRegistration> ListNotificationRegistrations();

    // Windows: removes every notification identity the framework registered
    // for this user - the AppUserModelID keys, the activators' CLSID keys and
    // the list of them - and returns how many identities it removed. What an
    // uninstaller does. An application registers again the next time it
    // starts (its activator) or shows a notification (its name and icon).
    // Nothing, and 0, elsewhere.
    size_t RemoveNotificationRegistrations();

    // ----- platform-neutral helpers -----

    // `key=value&key=value`, every key and value percent-encoded: the form a
    // toast's activation arguments take, and a convenient one for `launch`.
    inline std::string EncodeNotificationQuery(const std::vector<std::pair<std::string, std::string>>& fields) {
        static const char* hex = "0123456789ABCDEF";
        auto encode = [](const std::string& text) {
            std::string out;
            for (unsigned char c : text) {
                const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                   c == '-' || c == '_' || c == '.' || c == '~';
                if (plain) {
                    out += static_cast<char>(c);
                } else {
                    out += '%';
                    out += hex[c >> 4];
                    out += hex[c & 0x0F];
                }
            }
            return out;
        };
        std::string query;
        for (const auto& [key, value] : fields) {
            if (!query.empty()) query += '&';
            query += encode(key) + "=" + encode(value);
        }
        return query;
    }

    // The fields of an EncodeNotificationQuery string; a malformed escape is
    // kept as it stands, a field without '=' has an empty value.
    inline std::map<std::string, std::string> DecodeNotificationQuery(const std::string& query) {
        auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        auto decode = [&digit](const std::string& text) {
            std::string out;
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '%' && i + 2 < text.size()) {
                    const int high = digit(text[i + 1]);
                    const int low = digit(text[i + 2]);
                    if (high >= 0 && low >= 0) {
                        out += static_cast<char>(high * 16 + low);
                        i += 2;
                        continue;
                    }
                }
                out += text[i];
            }
            return out;
        };
        std::map<std::string, std::string> fields;
        size_t start = 0;
        while (start <= query.size()) {
            size_t end = query.find('&', start);
            if (end == std::string::npos) end = query.size();
            const std::string field = query.substr(start, end - start);
            if (!field.empty()) {
                const size_t eq = field.find('=');
                if (eq == std::string::npos) fields[decode(field)] = std::string();
                else fields[decode(field.substr(0, eq))] = decode(field.substr(eq + 1));
            }
            start = end + 1;
        }
        return fields;
    }

    // A toast's activation arguments for one of its actions ("default" for the
    // toast itself, a button's id, "" for a toast without a default action).
    inline std::string FormatNotificationArguments(const NotificationActivation& activation) {
        std::vector<std::pair<std::string, std::string>> fields = {{"action", activation.actionId},
                                                                   {"id", activation.notificationId}};
        if (!activation.launch.empty()) fields.emplace_back("launch", activation.launch);
        return EncodeNotificationQuery(fields);
    }

    // The click a toast's activation arguments describe. Arguments in no form
    // these helpers wrote are taken as a click on the toast itself.
    inline NotificationActivation ParseNotificationArguments(const std::string& arguments) {
        NotificationActivation activation;
        const auto fields = DecodeNotificationQuery(arguments);
        auto field = [&fields](const char* key) {
            auto it = fields.find(key);
            return it == fields.end() ? std::string() : it->second;
        };
        if (fields.count("action") == 0 && fields.count("id") == 0) {
            activation.actionId = arguments.empty() ? std::string() : std::string("default");
            return activation;
        }
        activation.actionId = field("action");
        activation.notificationId = field("id");
        activation.launch = field("launch");
        return activation;
    }

    // The CLSID of the activator of `appId`'s toasts, "{XXXXXXXX-XXXX-...}":
    // derived from the ID, so every start of the application, and the
    // uninstaller, name the same class. A name-based (version 5 layout) UUID.
    inline std::string NotificationActivatorClassId(const std::string& appId) {
        auto fnv = [&appId](uint64_t seed) {
            uint64_t hash = seed;
            const std::string text = "UltraCanvas notification activator:" + appId;
            for (unsigned char c : text) {
                hash ^= c;
                hash *= 1099511628211ULL;
            }
            return hash;
        };
        const uint64_t high = fnv(1469598103934665603ULL);
        const uint64_t low = fnv(0x9E3779B97F4A7C15ULL);
        unsigned char bytes[16];
        for (int i = 0; i < 8; ++i) {
            bytes[i] = static_cast<unsigned char>(high >> (56 - 8 * i));
            bytes[8 + i] = static_cast<unsigned char>(low >> (56 - 8 * i));
        }
        bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x50);   // version 5
        bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);   // RFC 4122 variant
        static const char* hex = "0123456789ABCDEF";
        std::string id = "{";
        for (int i = 0; i < 16; ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10) id += '-';
            id += hex[bytes[i] >> 4];
            id += hex[bytes[i] & 0x0F];
        }
        id += "}";
        return id;
    }

    // The argument the activator's LocalServer32 command passes, and the one
    // COM adds: a program started for a click sees them in its command line
    // and takes them for nothing else (no file to open).
    inline bool IsNotificationActivationArgument(const std::string& argument) {
        return argument == "-ToastActivated" || argument == "-Embedding" || argument == "/Embedding";
    }

} // namespace UltraCanvas
