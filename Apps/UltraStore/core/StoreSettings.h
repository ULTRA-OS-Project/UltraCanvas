// Apps/UltraStore/core/StoreSettings.h
// The store's own preferences - the channel, the language of the texts, the
// servers the catalog comes from, the keys the user trusts beside the
// compiled-in ones, the window's size - in the framework's one place for an
// application's settings (UltraCanvasAppSettings: <settings folder>/UltraStore/
// settings.json, UltraCanvasSettingsFolder.h names the folder per platform).
// The window and the command line read the same file, so `ultrastore fetch`
// without arguments and the window's Refresh ask the same servers.
//
// The compiled-in servers and keys come from the build: every
// Apps/UltraStore/keys/*.pub (hex, one key per file) and the CMake variable
// ULTRASTORE_SERVERS, through the generated UltraStoreBuiltin.h. Until the
// admin backend publishes the first catalog both are empty, and the window
// says so on its Discover page.
// Version: 0.3.0 - the open order
// Version: 0.2.0 - the code in force
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Verifier.h"

#include <string>
#include <vector>

namespace UltraCanvas { class UltraCanvasAppSettings; }

namespace UltraStore {

struct StoreSettings {
    std::string channel = "stable";       // "stable" or "beta"
    std::string language;                 // "de"; empty: the system's (SystemLanguage())
    std::vector<std::string> servers;     // base URLs, tried in order; empty: the built-in ones
    std::vector<std::string> extraKeys;   // hex public keys the user added
    bool refreshOnStart = true;
    int windowWidth = 1180;
    int windowHeight = 760;
    bool downloadsExpanded = false;
    int downloadsHeight = 200;
    // The discount or affiliate code the person entered or arrived with
    // (ultrastore://code/<CODE>), and the day it was entered: it stays in
    // force for kCodeDays, so a partner whose link brought the person is
    // credited by a purchase made within that time, and the price shows the
    // discount until then.
    std::string code;
    std::string codeEnteredAt;   // "YYYY-MM-DD"
    static constexpr int kCodeDays = 30;
    // The code when it was entered within kCodeDays of today, else "".
    std::string CodeInForce(const std::string& today) const;

    // The purchase the store is waiting on: an order the payment service
    // made that is neither paid nor over - kept so a store closed during the
    // payment picks it up again at the next start (within kOrderDays).
    struct OpenOrder {
        std::string orderId, secret, app, amount, currency, checkoutUrl;
        std::string startedAt;   // "YYYY-MM-DD"
        bool Empty() const { return orderId.empty(); }
    };
    OpenOrder openOrder;
    static constexpr int kOrderDays = 2;
    // The open order when it was started within kOrderDays of today, else an empty one.
    OpenOrder OrderToResume(const std::string& today) const;

    // The texts' language: the setting, else the system's.
    std::string Language() const;
    // The servers to ask: the setting, else the built-in list.
    std::vector<std::string> Servers() const;
    // Every key the store trusts: the compiled-in ones and the valid hex keys
    // of extraKeys (32 bytes); a malformed entry is skipped.
    PublicKeys TrustedKeys() const;

    static StoreSettings From(const UltraCanvas::UltraCanvasAppSettings& file);
    void WriteTo(UltraCanvas::UltraCanvasAppSettings& file) const;

    // The store's settings file (UltraCanvasAppSettings("UltraStore")).
    static StoreSettings Load();
    bool Save() const;
};

// What the build compiled in (UltraStoreBuiltin.h).
std::vector<std::string> BuiltinServers();
std::vector<std::string> BuiltinKeysHex();

// The lines of a text, trimmed, empty ones and `#` comments dropped: how the
// settings page takes a list.
std::vector<std::string> Lines(const std::string& text);

// Whether a string is a 64-digit hex public key.
bool IsPublicKeyHex(const std::string& hex);

} // namespace UltraStore
