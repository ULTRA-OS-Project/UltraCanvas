// include/UltraCanvasFileSaveSettings.h
// What a file format's writer can be told - JPEG's quality, PNG's compression,
// TIFF's codec - described so a dialog can offer it without knowing the
// format, and the values the user chose, per format.
//
//   FileSaveSettingSpec  one setting: its key, caption, kind (on / off, a
//                        whole number in a range, one of a list) and default
//   FileSaveFormat       a format (name, extensions) and its settings
//   FileSaveSettings     the values of one format's settings, by key
//
// UltraCanvasFileLoader is the way in (UltraCanvasFileLoader.h, "SAVE
// SETTINGS"): it knows the formats - the bitmap formats the image writer
// takes options for are built in, and a plugin or an application registers
// its own with RegisterSaveFormat - and keeps the values. The framework's
// Save dialog edits them with its Settings button on the type row
// (ShowSaveSettingsDialog), and the writers read them back: the image writer
// through GetImageExportOptions, anything else through GetSaveSettings.
//
// The values are remembered between runs and shared by every application, in
// SaveSettings.conf in the UltraCanvas settings folder (see
// UltraCanvasSettingsFolder.h): one "<format>.<key>=<value>" line per setting
// the user has set. A format with no line there has never been set and its
// writer runs as it always did.
//
// A bitmap format (FileSaveFormat::bitmap) can also be written at another
// pixel size (FileSaveImageSize). That is not one of its settings: it belongs
// to the picture being saved, is chosen for one save - the settings dialog
// shows it when the Save dialog was told the picture's size - and comes back
// with that save, never remembered for the next one.
//
// Version: 1.1.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once

#include <map>
#include <string>
#include <vector>

namespace UltraCanvas {

    enum class FileSaveSettingKind {
        Toggle,   // on or off: the values "true" and "false"
        Number,   // a whole number from `minimum` to `maximum`
        Choice    // one of `choices`, by its value
    };

    struct FileSaveChoice {
        std::string value;   // what is stored and read ("lzw")
        std::string label;   // what the list shows ("LZW")

        bool operator==(const FileSaveChoice&) const = default;
    };

    struct FileSaveSettingSpec {
        std::string key;     // the name it is stored and read by: "quality"
        std::string label;   // the caption: "Quality"
        std::string help;    // one sentence for the tooltip; may be empty
        FileSaveSettingKind kind = FileSaveSettingKind::Toggle;
        int minimum = 0;     // Number only
        int maximum = 100;
        std::vector<FileSaveChoice> choices;   // Choice only
        std::string defaultValue;              // "85", "true", "lzw"

        static FileSaveSettingSpec Toggle(const std::string& key, const std::string& label,
                                          bool defaultOn, const std::string& help = "");
        static FileSaveSettingSpec Number(const std::string& key, const std::string& label,
                                          int minimum, int maximum, int defaultValue,
                                          const std::string& help = "");
        static FileSaveSettingSpec Choice(const std::string& key, const std::string& label,
                                          const std::vector<FileSaveChoice>& choices,
                                          const std::string& defaultValue,
                                          const std::string& help = "");

        // `value` made one this setting can hold: a Toggle is "true" or
        // "false", a Number is clamped into its range, and a value that is
        // none of that (an unknown choice, text where a number goes) is the
        // default.
        std::string Normalize(const std::string& value) const;
    };

    struct FileSaveFormat {
        std::string name;                      // "JPEG"
        // Lowercase, undotted. The first one is the format's key: its values
        // are stored under it, whichever of the extensions a file has.
        std::vector<std::string> extensions;   // {"jpg", "jpeg", "jpe"}
        std::vector<FileSaveSettingSpec> settings;
        // A pixel format: a picture saved in it can be written at another
        // size (FileSaveImageSize). True for the built-in image formats,
        // including the ones without settings (BMP, ICO, QOI ...).
        bool bitmap = false;

        const std::string& Key() const;
        const FileSaveSettingSpec* FindSetting(const std::string& key) const;
        bool HasExtension(const std::string& extension) const;
    };

    // The values of one format's settings, as text by key. Read them with the
    // typed getters: a key that is missing, or holds something that is not
    // what was asked for, gives the fallback.
    struct FileSaveSettings {
        std::string format;                        // the format's key, "jpg"
        std::map<std::string, std::string> values;

        std::string Get(const std::string& key, const std::string& fallback = "") const;
        bool GetBool(const std::string& key, bool fallback = false) const;
        int GetInt(const std::string& key, int fallback = 0) const;

        void Set(const std::string& key, const std::string& value) { values[key] = value; }
        void SetBool(const std::string& key, bool on) { values[key] = on ? "true" : "false"; }
        void SetInt(const std::string& key, int value) { values[key] = std::to_string(value); }

        bool operator==(const FileSaveSettings&) const = default;
    };

    // The pixel size a bitmap is written at, chosen for one save. 0 x 0 - or
    // either side 0 - is the picture's own size (IsOriginal). keepProportions
    // is how the size was entered (one side follows the other); width and
    // height are the result either way.
    struct FileSaveImageSize {
        int width = 0;
        int height = 0;
        bool keepProportions = true;

        bool IsOriginal() const { return width <= 0 || height <= 0; }
        bool operator==(const FileSaveImageSize&) const = default;
    };

} // namespace UltraCanvas
