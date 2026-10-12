// core/UltraCanvasFileSaveSettings.cpp
// Save settings: the formats that have them, the values the user chose
// (SaveSettings.conf) and the image writer's options made from them. See
// UltraCanvasFileSaveSettings.h. The dialog that edits them is in
// UltraCanvasFileSaveSettingsDialog.cpp.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "UltraCanvasFileSaveSettings.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasSettingsFolder.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasDebug.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>

namespace UltraCanvas {

// ===== SPECS AND VALUES =====

    namespace {
        bool ParseWholeNumber(const std::string& text, int& out) {
            if (text.empty()) return false;
            int value = 0;
            const char* first = text.data();
            const char* last = text.data() + text.size();
            auto [end, ec] = std::from_chars(first, last, value);
            if (ec != std::errc() || end != last) return false;
            out = value;
            return true;
        }

        std::string ToLower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        // What a file of `extensionOrPath` is saved as, as an extension:
        // "jpg" from "jpg", ".JPG", "photo.jpg" or "/a.b/photo.jpg". Only the
        // last component is looked at, and its text after the last dot.
        std::string ExtensionOf(const std::string& extensionOrPath) {
            std::string name = extensionOrPath;
            const size_t slash = name.find_last_of("/\\");
            if (slash != std::string::npos) name = name.substr(slash + 1);
            const size_t dot = name.find_last_of('.');
            if (dot != std::string::npos) name = name.substr(dot + 1);
            return ToLower(name);
        }

        // A key or format name inside a line of the file: "<format>.<key>=".
        // A setting key is a word (letters, digits, '-', '_'); a format key
        // is an extension and may hold a dot ("tar.gz"), so the line is split
        // at its last dot.
        bool IsSettingKey(const std::string& key) {
            if (key.empty()) return false;
            for (unsigned char c : key)
                if (!std::isalnum(c) && c != '-' && c != '_') return false;
            return true;
        }

        std::string OneLine(const std::string& value) {
            std::string out;
            for (char c : value)
                if (c != '\n' && c != '\r') out += c;
            return out;
        }
    } // namespace

    FileSaveSettingSpec FileSaveSettingSpec::Toggle(const std::string& key, const std::string& label,
                                                    bool defaultOn, const std::string& help) {
        FileSaveSettingSpec spec;
        spec.key = key;
        spec.label = label;
        spec.help = help;
        spec.kind = FileSaveSettingKind::Toggle;
        spec.defaultValue = defaultOn ? "true" : "false";
        return spec;
    }

    FileSaveSettingSpec FileSaveSettingSpec::Number(const std::string& key, const std::string& label,
                                                    int minimum, int maximum, int defaultValue,
                                                    const std::string& help) {
        FileSaveSettingSpec spec;
        spec.key = key;
        spec.label = label;
        spec.help = help;
        spec.kind = FileSaveSettingKind::Number;
        spec.minimum = std::min(minimum, maximum);
        spec.maximum = std::max(minimum, maximum);
        spec.defaultValue = std::to_string(std::clamp(defaultValue, spec.minimum, spec.maximum));
        return spec;
    }

    FileSaveSettingSpec FileSaveSettingSpec::Choice(const std::string& key, const std::string& label,
                                                    const std::vector<FileSaveChoice>& choices,
                                                    const std::string& defaultValue,
                                                    const std::string& help) {
        FileSaveSettingSpec spec;
        spec.key = key;
        spec.label = label;
        spec.help = help;
        spec.kind = FileSaveSettingKind::Choice;
        spec.choices = choices;
        spec.defaultValue = defaultValue;
        // A default that is not among the choices would be stored by the
        // dialog as something nothing reads: the first choice instead.
        const bool known = std::any_of(choices.begin(), choices.end(),
                [&](const FileSaveChoice& c) { return c.value == defaultValue; });
        if (!known && !choices.empty()) spec.defaultValue = choices.front().value;
        return spec;
    }

    std::string FileSaveSettingSpec::Normalize(const std::string& value) const {
        switch (kind) {
            case FileSaveSettingKind::Toggle: {
                const std::string lower = ToLower(value);
                if (lower == "true" || lower == "1" || lower == "on" || lower == "yes") return "true";
                if (lower == "false" || lower == "0" || lower == "off" || lower == "no") return "false";
                return defaultValue;
            }
            case FileSaveSettingKind::Number: {
                int n = 0;
                if (!ParseWholeNumber(value, n)) return defaultValue;
                return std::to_string(std::clamp(n, minimum, maximum));
            }
            case FileSaveSettingKind::Choice:
                for (const FileSaveChoice& choice : choices)
                    if (choice.value == value) return value;
                return defaultValue;
        }
        return defaultValue;
    }

    const std::string& FileSaveFormat::Key() const {
        static const std::string none;
        return extensions.empty() ? none : extensions.front();
    }

    const FileSaveSettingSpec* FileSaveFormat::FindSetting(const std::string& key) const {
        for (const FileSaveSettingSpec& spec : settings)
            if (spec.key == key) return &spec;
        return nullptr;
    }

    bool FileSaveFormat::HasExtension(const std::string& extension) const {
        const std::string wanted = ToLower(extension);
        return std::find(extensions.begin(), extensions.end(), wanted) != extensions.end();
    }

    std::string FileSaveSettings::Get(const std::string& key, const std::string& fallback) const {
        auto it = values.find(key);
        return it == values.end() ? fallback : it->second;
    }

    bool FileSaveSettings::GetBool(const std::string& key, bool fallback) const {
        auto it = values.find(key);
        if (it == values.end()) return fallback;
        if (it->second == "true") return true;
        if (it->second == "false") return false;
        return fallback;
    }

    int FileSaveSettings::GetInt(const std::string& key, int fallback) const {
        auto it = values.find(key);
        int n = 0;
        if (it == values.end() || !ParseWholeNumber(it->second, n)) return fallback;
        return n;
    }

// ===== THE BUILT-IN FORMATS =====
// The bitmap formats the image writer (ExportVImage) takes options for, with
// only the settings it actually honours: a setting that changes nothing would
// tell the user something untrue. BMP, ICO, QOI, HDR, FITS and Farbfeld are
// written without options and so have no settings.

    namespace {
        using Spec = FileSaveSettingSpec;

        Spec TransparencySetting() {
            return Spec::Toggle("transparency", "Keep transparency", true,
                                "Write the alpha channel. Off: transparent areas become opaque.");
        }

        std::vector<FileSaveFormat> BuiltInFormats() {
            std::vector<FileSaveFormat> formats;

            formats.push_back({"PNG", {"png"}, {
                Spec::Number("compression", "Compression", 0, 9, 6,
                             "0 writes fastest, 9 writes the smallest file. The picture is the same."),
                Spec::Choice("depth", "Colours", {
                        {"1bit", "Black and white (1 bit)"},
                        {"4bit", "16 colours (4 bit palette)"},
                        {"8bit-indexed", "256 colours (8 bit palette)"},
                        {"8bit", "Full colour, 8 bits a channel"},
                        {"16bit", "Full colour, 16 bits a channel"}},
                             "8bit", "A palette makes a smaller file of a picture with few colours."),
                Spec::Toggle("interlace", "Interlaced", false,
                             "Adam7: a browser shows a rough picture before the file has arrived."),
                TransparencySetting()}});

            formats.push_back({"JPEG", {"jpg", "jpeg", "jpe", "jfif"}, {
                Spec::Number("quality", "Quality", 1, 100, 85,
                             "Higher keeps more detail and makes a bigger file."),
                Spec::Toggle("progressive", "Progressive", false,
                             "A browser shows a rough picture first and sharpens it as it loads."),
                Spec::Toggle("optimize", "Optimise coding", true,
                             "Optimal Huffman tables: a slightly smaller file, the same picture."),
                Spec::Toggle("subsample", "Always subsample colour", false,
                             "Store colour at half resolution even at high quality: smaller, softer edges.")}});

            formats.push_back({"WebP", {"webp"}, {
                Spec::Number("quality", "Quality", 0, 100, 80,
                             "Higher keeps more detail and makes a bigger file. Unused when lossless."),
                Spec::Toggle("lossless", "Lossless", false, "Keep every pixel exactly."),
                Spec::Number("effort", "Effort", 0, 6, 4,
                             "How hard the encoder works: higher is slower and smaller."),
                Spec::Number("alphaQuality", "Transparency quality", 0, 100, 100,
                             "Quality of the alpha channel; 100 keeps it exact."),
                TransparencySetting()}});

            formats.push_back({"AVIF", {"avif"}, {
                Spec::Number("quality", "Quality", 0, 100, 65,
                             "Higher keeps more detail and makes a bigger file. Unused when lossless."),
                Spec::Toggle("lossless", "Lossless", false, "Keep every pixel exactly."),
                Spec::Number("speed", "Speed", 0, 9, 6,
                             "0 encodes slowest and smallest, 9 fastest and largest."),
                Spec::Choice("depth", "Bit depth", {
                        {"8bit", "8 bits a channel"},
                        {"16bit", "12 bits a channel"}}, "8bit"),
                TransparencySetting()}});

            formats.push_back({"HEIF", {"heif", "heic"}, {
                Spec::Number("quality", "Quality", 0, 100, 50,
                             "Higher keeps more detail and makes a bigger file. Unused when lossless."),
                Spec::Toggle("lossless", "Lossless", false, "Keep every pixel exactly."),
                Spec::Choice("depth", "Bit depth", {
                        {"8bit", "8 bits a channel"},
                        {"16bit", "12 bits a channel"}}, "8bit"),
                TransparencySetting()}});

            formats.push_back({"GIF", {"gif"}, {
                Spec::Choice("colours", "Colours", {
                        {"2", "2 colours"},
                        {"16", "16 colours"},
                        {"256", "256 colours"}}, "256"),
                Spec::Toggle("dither", "Dither", true,
                             "Mix the palette's colours in patterns to show the ones it lacks."),
                Spec::Toggle("interlace", "Interlaced", false,
                             "A browser shows a rough picture before the file has arrived."),
                TransparencySetting()}});

            formats.push_back({"TIFF", {"tif", "tiff"}, {
                Spec::Choice("compression", "Compression", {
                        {"none", "None"},
                        {"lzw", "LZW"},
                        {"deflate", "Deflate (ZIP)"},
                        {"packbits", "PackBits"},
                        {"jpeg", "JPEG (lossy)"},
                        {"zstd", "Zstandard"},
                        {"webp", "WebP"}}, "lzw",
                             "LZW and Deflate are lossless and read everywhere."),
                TransparencySetting()}});

            formats.push_back({"JPEG XL", {"jxl"}, {
                Spec::Number("quality", "Quality", 0, 100, 75,
                             "Higher keeps more detail and makes a bigger file. Unused when lossless."),
                Spec::Toggle("lossless", "Lossless", false, "Keep every pixel exactly."),
                Spec::Number("effort", "Effort", 1, 9, 7,
                             "How hard the encoder works: higher is slower and smaller."),
                TransparencySetting()}});

            formats.push_back({"JPEG 2000", {"jp2", "j2k", "jpf"}, {
                Spec::Number("quality", "Quality", 0, 100, 75,
                             "Higher keeps more detail and makes a bigger file. Unused when lossless."),
                Spec::Toggle("lossless", "Lossless", false, "Keep every pixel exactly.")}});

            formats.push_back({"Netpbm", {"ppm", "pgm", "pbm", "pfm"}, {
                Spec::Toggle("binary", "Binary", true,
                             "Off writes the numbers as text: much bigger, but readable.")}});

            formats.push_back({"TGA", {"tga"}, {
                Spec::Toggle("rle", "RLE compression", true, "Run-length encoding, lossless.")}});

            formats.push_back({"PCX", {"pcx"}, {
                Spec::Toggle("rle", "RLE compression", true, "Run-length encoding, lossless.")}});

            formats.push_back({"OpenEXR", {"exr"}, {
                Spec::Choice("compression", "Compression", {
                        {"none", "None"},
                        {"rle", "RLE"},
                        {"zip", "ZIP"},
                        {"piz", "PIZ"},
                        {"pxr24", "PXR24 (lossy)"},
                        {"b44", "B44 (lossy)"}}, "zip")}});

            const std::vector<FileSaveChoice> cineonDepths = {
                    {"8", "8 bits"}, {"10", "10 bits"}, {"12", "12 bits"}, {"16", "16 bits"}};
            formats.push_back({"DPX", {"dpx"}, {
                Spec::Choice("bitDepth", "Bit depth", cineonDepths, "10")}});
            formats.push_back({"Cineon", {"cin"}, {
                Spec::Choice("bitDepth", "Bit depth", cineonDepths, "10")}});

            formats.push_back({"Photoshop", {"psd"}, {
                Spec::Toggle("compressed", "RLE compression", true, "Run-length encoding, lossless.")}});

            formats.push_back({"SGI", {"sgi"}, {
                Spec::Toggle("rle", "RLE compression", false, "Run-length encoding, lossless.")}});

            return formats;
        }

        // The image writer's format for an extension, whether or not it has
        // settings. Autodetect: not one the image writer writes.
        UCImageSaveFormat ImageFormatFor(const std::string& extension) {
            static const std::map<std::string, UCImageSaveFormat> kFormats = {
                    {"png", UCImageSaveFormat::PNG},
                    {"jpg", UCImageSaveFormat::JPEG}, {"jpeg", UCImageSaveFormat::JPEG},
                    {"jpe", UCImageSaveFormat::JPEG}, {"jfif", UCImageSaveFormat::JPEG},
                    {"webp", UCImageSaveFormat::WEBP},
                    {"avif", UCImageSaveFormat::AVIF},
                    {"heif", UCImageSaveFormat::HEIF}, {"heic", UCImageSaveFormat::HEIF},
                    {"gif", UCImageSaveFormat::GIF},
                    {"bmp", UCImageSaveFormat::BMP},
                    {"tif", UCImageSaveFormat::TIFF}, {"tiff", UCImageSaveFormat::TIFF},
                    {"ico", UCImageSaveFormat::ICO},
                    {"jxl", UCImageSaveFormat::JXL},
                    {"jp2", UCImageSaveFormat::JPEG2000}, {"j2k", UCImageSaveFormat::JPEG2000},
                    {"jpf", UCImageSaveFormat::JPEG2000},
                    {"qoi", UCImageSaveFormat::QOI},
                    {"ppm", UCImageSaveFormat::PPM}, {"pgm", UCImageSaveFormat::PGM},
                    {"pbm", UCImageSaveFormat::PBM}, {"pfm", UCImageSaveFormat::PFM},
                    {"tga", UCImageSaveFormat::TGA},
                    {"pcx", UCImageSaveFormat::PCX},
                    {"hdr", UCImageSaveFormat::HDR},
                    {"exr", UCImageSaveFormat::EXR},
                    {"dpx", UCImageSaveFormat::DPX},
                    {"cin", UCImageSaveFormat::CIN},
                    {"fits", UCImageSaveFormat::FITS}, {"fit", UCImageSaveFormat::FITS},
                    {"psd", UCImageSaveFormat::PSD},
                    {"sgi", UCImageSaveFormat::SGI},
                    {"ff", UCImageSaveFormat::FARBFELD},
            };
            auto it = kFormats.find(extension);
            return it == kFormats.end() ? UCImageSaveFormat::Autodetect : it->second;
        }

        UCImageSave::ColorDepth DepthFor(const std::string& value) {
            if (value == "1bit" || value == "2") return UCImageSave::ColorDepth::Monochrome_1bit;
            if (value == "4bit" || value == "16") return UCImageSave::ColorDepth::Indexed_4bit;
            if (value == "8bit-indexed" || value == "256") return UCImageSave::ColorDepth::Indexed_8bit;
            if (value == "16bit") return UCImageSave::ColorDepth::RGB_16bit;
            return UCImageSave::ColorDepth::RGB_8bit;
        }

        UCImageSave::TiffCompression TiffCompressionFor(const std::string& value) {
            if (value == "none") return UCImageSave::TiffCompression::NoCompression;
            if (value == "deflate") return UCImageSave::TiffCompression::DeflateCompression;
            if (value == "packbits") return UCImageSave::TiffCompression::PackBitsCompression;
            if (value == "jpeg") return UCImageSave::TiffCompression::JPEGCompression;
            if (value == "zstd") return UCImageSave::TiffCompression::ZSTDCompression;
            if (value == "webp") return UCImageSave::TiffCompression::WEBPCompression;
            return UCImageSave::TiffCompression::LZWCompression;
        }

        UCImageSave::ExrExportOptions::Compression ExrCompressionFor(const std::string& value) {
            using C = UCImageSave::ExrExportOptions::Compression;
            if (value == "none") return C::NoCompression;
            if (value == "rle") return C::RLE;
            if (value == "piz") return C::PIZ;
            if (value == "pxr24") return C::PXR24;
            if (value == "b44") return C::B44;
            return C::ZIP;
        }

        // ----- the registry -----
        struct SaveFormatRegistry {
            std::mutex mutex;
            std::vector<FileSaveFormat> formats = BuiltInFormats();
        };

        SaveFormatRegistry& Registry() {
            static SaveFormatRegistry registry;
            return registry;
        }

        // Callers hold the registry's mutex.
        const FileSaveFormat* FindLocked(const SaveFormatRegistry& registry,
                                         const std::string& extension) {
            if (extension.empty()) return nullptr;
            for (const FileSaveFormat& format : registry.formats)
                if (format.HasExtension(extension)) return &format;
            return nullptr;
        }

        // ----- SaveSettings.conf -----
        // format key -> setting key -> value, as the user set them.
        using StoredValues = std::map<std::string, std::map<std::string, std::string>>;

        std::filesystem::path StoreFilePath() {
            const std::filesystem::path folder = UltraCanvasSettingsFolder();
            if (folder.empty()) return {};
            return folder / "SaveSettings.conf";
        }

        StoredValues LoadStore() {
            StoredValues stored;
            const std::filesystem::path path = StoreFilePath();
            if (path.empty()) return stored;
            std::ifstream in(path);
            if (!in) {
                std::error_code ec;
                if (std::filesystem::exists(path, ec)) {
                    static bool reported = false;
                    if (!reported) {
                        reported = true;
                        debugOutput << "UltraCanvasFileLoader: could not read " << PathToUtf8(path)
                                    << "; files are saved with each format's default settings"
                                    << std::endl;
                    }
                }
                return stored;
            }
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty() || line.front() == '#') continue;
                const size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                const std::string name = line.substr(0, eq);
                const size_t dot = name.rfind('.');
                if (dot == std::string::npos || dot == 0) continue;
                const std::string key = name.substr(dot + 1);
                if (!IsSettingKey(key)) continue;
                stored[ToLower(name.substr(0, dot))][key] = line.substr(eq + 1);
            }
            return stored;
        }

        bool SaveStore(const StoredValues& stored) {
            const std::filesystem::path path = StoreFilePath();
            if (path.empty()) return false;
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            // Beside the file and renamed over it, as FileDialog.conf is: an
            // application reading it meanwhile sees the old file or the new.
            std::filesystem::path temp = path;
            temp += ".tmp";
            {
                std::ofstream out(temp, std::ios::trunc);
                if (!out) return false;
                out << "# What each file format is saved with: <format>.<setting>=<value>.\n"
                       "# Written by every UltraCanvas application's Save dialog.\n";
                for (const auto& [format, values] : stored)
                    for (const auto& [key, value] : values)
                        out << format << "." << key << "=" << OneLine(value) << "\n";
                if (!out) return false;
            }
            std::filesystem::rename(temp, path, ec);
            if (ec) {
                std::filesystem::remove(temp, ec);
                return false;
            }
            return true;
        }

        // Every application writes the file, so a change re-reads it first
        // and touches only its own format.
        std::mutex& StoreMutex() {
            static std::mutex m;
            return m;
        }
    } // namespace

// ===== ULTRACANVASFILELOADER: SAVE SETTINGS =====

    void UltraCanvasFileLoader::RegisterSaveFormat(const FileSaveFormat& format) {
        FileSaveFormat clean;
        clean.name = format.name;
        for (const std::string& ext : format.extensions) {
            std::string e = ToLower(ext);
            if (!e.empty() && e.front() == '.') e.erase(0, 1);
            if (!e.empty() && std::find(clean.extensions.begin(), clean.extensions.end(), e) ==
                              clean.extensions.end())
                clean.extensions.push_back(e);
        }
        if (clean.extensions.empty()) return;
        // A key that would not survive the file (a dot splits "<format>.<key>")
        // is a setting nothing could read back.
        for (const FileSaveSettingSpec& spec : format.settings) {
            if (!IsSettingKey(spec.key) || clean.FindSetting(spec.key)) {
                debugOutput << "UltraCanvasFileLoader::RegisterSaveFormat: " << clean.name
                            << ": setting \"" << spec.key << "\" left out (a key is letters, digits, "
                            << "'-' and '_', once per format)" << std::endl;
                continue;
            }
            FileSaveSettingSpec s = spec;
            s.defaultValue = s.Normalize(s.defaultValue);
            clean.settings.push_back(std::move(s));
        }

        SaveFormatRegistry& registry = Registry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        // The same first extension replaces the format; the new one's
        // extensions are taken off any other, so one extension names one.
        auto& formats = registry.formats;
        formats.erase(std::remove_if(formats.begin(), formats.end(),
                [&](const FileSaveFormat& f) { return f.Key() == clean.Key(); }), formats.end());
        for (FileSaveFormat& other : formats) {
            auto& exts = other.extensions;
            exts.erase(std::remove_if(exts.begin(), exts.end(), [&](const std::string& e) {
                return clean.HasExtension(e);
            }), exts.end());
        }
        formats.erase(std::remove_if(formats.begin(), formats.end(),
                [](const FileSaveFormat& f) { return f.extensions.empty(); }), formats.end());
        formats.push_back(std::move(clean));
    }

    std::vector<FileSaveFormat> UltraCanvasFileLoader::GetSaveFormats() {
        SaveFormatRegistry& registry = Registry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        return registry.formats;
    }

    bool UltraCanvasFileLoader::FindSaveFormat(const std::string& extensionOrPath, FileSaveFormat& out) {
        SaveFormatRegistry& registry = Registry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        const FileSaveFormat* format = FindLocked(registry, ExtensionOf(extensionOrPath));
        if (!format) return false;
        out = *format;
        return true;
    }

    bool UltraCanvasFileLoader::HasSaveSettings(const std::string& extensionOrPath) {
        FileSaveFormat format;
        return FindSaveFormat(extensionOrPath, format) && !format.settings.empty();
    }

    FileSaveSettings UltraCanvasFileLoader::GetSaveSettings(const std::string& extensionOrPath) {
        FileSaveSettings settings;
        FileSaveFormat format;
        if (!FindSaveFormat(extensionOrPath, format)) return settings;
        settings.format = format.Key();
        StoredValues stored;
        {
            std::lock_guard<std::mutex> lock(StoreMutex());
            stored = LoadStore();
        }
        const auto it = stored.find(format.Key());
        for (const FileSaveSettingSpec& spec : format.settings) {
            std::string value = spec.defaultValue;
            if (it != stored.end()) {
                auto v = it->second.find(spec.key);
                if (v != it->second.end()) value = spec.Normalize(v->second);
            }
            settings.values[spec.key] = value;
        }
        return settings;
    }

    bool UltraCanvasFileLoader::HasStoredSaveSettings(const std::string& extensionOrPath) {
        FileSaveFormat format;
        if (!FindSaveFormat(extensionOrPath, format) || format.settings.empty()) return false;
        std::lock_guard<std::mutex> lock(StoreMutex());
        const StoredValues stored = LoadStore();
        const auto it = stored.find(format.Key());
        if (it == stored.end()) return false;
        for (const FileSaveSettingSpec& spec : format.settings)
            if (it->second.count(spec.key)) return true;
        return false;
    }

    bool UltraCanvasFileLoader::SetSaveSettings(const FileSaveSettings& settings) {
        FileSaveFormat format;
        if (!FindSaveFormat(settings.format, format) || format.settings.empty()) return false;
        std::map<std::string, std::string> values;
        for (const FileSaveSettingSpec& spec : format.settings) {
            auto it = settings.values.find(spec.key);
            if (it != settings.values.end()) values[spec.key] = spec.Normalize(it->second);
        }
        std::lock_guard<std::mutex> lock(StoreMutex());
        StoredValues stored = LoadStore();
        // Merged over what was there, so a caller setting one value does not
        // forget the others.
        auto& kept = stored[format.Key()];
        for (const auto& [key, value] : values) kept[key] = value;
        if (kept.empty()) stored.erase(format.Key());
        return SaveStore(stored);
    }

    bool UltraCanvasFileLoader::ResetSaveSettings(const std::string& extensionOrPath) {
        FileSaveFormat format;
        if (!FindSaveFormat(extensionOrPath, format)) return false;
        std::lock_guard<std::mutex> lock(StoreMutex());
        StoredValues stored = LoadStore();
        if (!stored.erase(format.Key())) return true;
        return SaveStore(stored);
    }

    bool UltraCanvasFileLoader::GetImageExportOptions(const std::string& path,
                                                      UCImageSave::ImageExportOptions& out) {
        const std::string ext = ExtensionOf(path);
        const UCImageSaveFormat imageFormat = ImageFormatFor(ext);
        if (imageFormat == UCImageSaveFormat::Autodetect) return false;

        UCImageSave::ImageExportOptions opts;
        opts.format = imageFormat;
        // Every value has a fallback: a format without settings (BMP), or one
        // re-registered with other keys, keeps the writer's defaults.
        const FileSaveSettings s = GetSaveSettings(ext);
        opts.preserveTransparency = s.GetBool("transparency", opts.preserveTransparency);
        switch (imageFormat) {
            case UCImageSaveFormat::PNG:
                opts.png.compressionLevel = s.GetInt("compression", opts.png.compressionLevel);
                opts.png.interlace = s.GetBool("interlace", opts.png.interlace);
                opts.png.colorDepth = DepthFor(s.Get("depth", "8bit"));
                break;
            case UCImageSaveFormat::JPEG:
                opts.jpeg.quality = s.GetInt("quality", opts.jpeg.quality);
                opts.jpeg.progressive = s.GetBool("progressive", opts.jpeg.progressive);
                opts.jpeg.optimizeHuffman = s.GetBool("optimize", opts.jpeg.optimizeHuffman);
                opts.jpeg.subsampling = s.GetBool("subsample", opts.jpeg.subsampling);
                break;
            case UCImageSaveFormat::WEBP:
                opts.webp.quality = s.GetInt("quality", opts.webp.quality);
                opts.webp.lossless = s.GetBool("lossless", opts.webp.lossless);
                opts.webp.effort = s.GetInt("effort", opts.webp.effort);
                opts.webp.alphaQuality = s.GetInt("alphaQuality", opts.webp.alphaQuality);
                break;
            case UCImageSaveFormat::AVIF:
                opts.avif.quality = s.GetInt("quality", opts.avif.quality);
                opts.avif.lossless = s.GetBool("lossless", opts.avif.lossless);
                opts.avif.speed = s.GetInt("speed", opts.avif.speed);
                opts.avif.colorDepth = DepthFor(s.Get("depth", "8bit"));
                break;
            case UCImageSaveFormat::HEIF:
                opts.heif.quality = s.GetInt("quality", opts.heif.quality);
                opts.heif.lossless = s.GetBool("lossless", opts.heif.lossless);
                opts.heif.colorDepth = DepthFor(s.Get("depth", "8bit"));
                break;
            case UCImageSaveFormat::GIF:
                opts.gif.colorDepth = DepthFor(s.Get("colours", "256"));
                opts.gif.dithering = s.GetBool("dither", opts.gif.dithering);
                opts.gif.interlace = s.GetBool("interlace", opts.gif.interlace);
                break;
            case UCImageSaveFormat::TIFF:
                opts.tiff.compression = TiffCompressionFor(s.Get("compression", "lzw"));
                break;
            case UCImageSaveFormat::JXL:
                opts.jxl.quality = s.GetInt("quality", opts.jxl.quality);
                opts.jxl.lossless = s.GetBool("lossless", opts.jxl.lossless);
                opts.jxl.effort = s.GetInt("effort", opts.jxl.effort);
                break;
            case UCImageSaveFormat::JPEG2000:
                opts.jpeg2000.quality = s.GetInt("quality", opts.jpeg2000.quality);
                opts.jpeg2000.lossless = s.GetBool("lossless", opts.jpeg2000.lossless);
                break;
            case UCImageSaveFormat::PPM:
            case UCImageSaveFormat::PGM:
            case UCImageSaveFormat::PBM:
            case UCImageSaveFormat::PFM:
                opts.pnm.binary = s.GetBool("binary", opts.pnm.binary);
                break;
            case UCImageSaveFormat::TGA:
                opts.tga.rleCompression = s.GetBool("rle", opts.tga.rleCompression);
                break;
            case UCImageSaveFormat::PCX:
                opts.pcx.rleCompression = s.GetBool("rle", opts.pcx.rleCompression);
                break;
            case UCImageSaveFormat::EXR:
                opts.exr.compression = ExrCompressionFor(s.Get("compression", "zip"));
                break;
            case UCImageSaveFormat::DPX:
                opts.dpx.bitDepth = s.GetInt("bitDepth", opts.dpx.bitDepth);
                break;
            case UCImageSaveFormat::CIN:
                opts.cin.bitDepth = s.GetInt("bitDepth", opts.cin.bitDepth);
                break;
            case UCImageSaveFormat::PSD:
                opts.psd.compressed = s.GetBool("compressed", opts.psd.compressed);
                break;
            case UCImageSaveFormat::SGI:
                opts.sgi.rleCompression = s.GetBool("rle", opts.sgi.rleCompression);
                break;
            default:
                break;   // written without options
        }
        out = opts;
        return true;
    }

} // namespace UltraCanvas
