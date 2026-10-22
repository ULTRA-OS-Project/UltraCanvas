// UltraCanvas/include/UltraCanvasAppSettings.h
// An application's own preferences, in one JSON file of the shared settings
// folder: <settings folder>/<App>/settings.json (UltraCanvasSettingsFolder.h
// names the folder per platform). The look chosen last, the window's size,
// the things opened last - every application has a few, and before this
// each kept them its own way: UltraPress in a settings.json of its own
// writing, UltraFIBU in a plain file of paths. This class is the one place.
//
// The values are typed, so a program reads `GetInt("window.width", 0)` and
// never parses; a dotted key is a path into nested objects
// (`"window.width"` is `{"window": {"width": 1200}}`), so the file stays
// readable and a group of values has a name. A list of strings is a JSON
// array, and the two list calls keep a recent list on it: Remember puts an
// entry first, once, and keeps the newest `keep`; Forget drops one. The
// entries are the application's strings - a path it normalised, a name -
// compared as text.
//
// Nothing is read or written on its own: Load() reads the file (a missing
// or unreadable one is the defaults, and says so), Save() writes it whole,
// pretty, and makes the folder first. Without a home the file is empty and
// both do nothing. For anything beyond the typed calls the document is
// there (Document()).
//
// doc-check: example
//   UltraCanvas::UltraCanvasAppSettings settings("UltraPress");   // <settings folder>/UltraPress/settings.json
//   settings.Load();
//   const bool dark = settings.GetBool("dark", false);
//   const int width = settings.GetInt("window.width", 1500);
//   const std::vector<std::string> recent = settings.GetList("recent");
//   settings.Set("dark", true);
//   settings.Set("window.width", 1200);
//   settings.Remember("recent", "/home/erika/sites/shop");   // first, once, eight kept
//   settings.Forget("recent", "/home/erika/sites/old");
//   settings.Save();
//
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "DataFormats/UltraCanvasJSON.h"

#include <cstddef>
#include <string>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasAppSettings {
    public:
        // The settings of the application `app`: <settings folder>/<app>/
        // settings.json, nothing read yet. The file is empty when the
        // environment names no settings folder.
        explicit UltraCanvasAppSettings(const std::string& app);
        // The settings in the file `file` (a UTF-8 path), for a test on a
        // temp folder or an application that keeps its file elsewhere.
        static UltraCanvasAppSettings AtFile(const std::string& file);

        // The file, UTF-8; empty without a settings folder.
        const std::string& File() const { return file_; }

        // Reads the file, replacing what is held. False when there is none,
        // it cannot be read, or it is not a JSON object - the settings are
        // the defaults then, and a later Save writes them.
        bool Load();
        // Writes the file, its folder made first. False when it cannot be.
        bool Save() const;

        // ---- typed values; `key` may be dotted ("window.width") ----
        bool Has(const std::string& key) const;
        std::string GetString(const std::string& key, const std::string& fallback = std::string()) const;
        int GetInt(const std::string& key, int fallback = 0) const;
        bool GetBool(const std::string& key, bool fallback = false) const;
        double GetNumber(const std::string& key, double fallback = 0.0) const;
        // The strings of the array at `key`, each once, the empty ones
        // dropped; empty when there is no array there.
        std::vector<std::string> GetList(const std::string& key) const;

        void Set(const std::string& key, const std::string& value);
        void Set(const std::string& key, const char* value);
        void Set(const std::string& key, int value);
        void Set(const std::string& key, bool value);
        void Set(const std::string& key, double value);
        void Set(const std::string& key, const std::vector<std::string>& values);
        // Drops the value at `key`; true when there was one.
        bool Remove(const std::string& key);

        // ---- a recent list at `key` ----
        // Puts `entry` first (once) and keeps the `keep` newest; nothing for
        // an empty entry.
        void Remember(const std::string& key, const std::string& entry, std::size_t keep = 8);
        // Drops `entry` from the list.
        void Forget(const std::string& key, const std::string& entry);

        // The whole document, for what the typed calls do not cover.
        JSONValue& Document() { return document_; }
        const JSONValue& Document() const { return document_; }

    private:
        const JSONValue& At(const std::string& key) const;
        void Put(const std::string& key, JSONValue value);

        std::string file_;
        JSONValue document_ = JSONValue::MakeObject();
    };

} // namespace UltraCanvas
