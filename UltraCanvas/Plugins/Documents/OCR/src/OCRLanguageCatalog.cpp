// OCRLanguageCatalog.cpp
// Language catalogue + on-demand language-pack management for the OCR plugin.
//
// Tesseract can recognise ~130 languages, but shipping every "*.traineddata"
// file would add hundreds of megabytes to the bundle. Instead we ship English
// only and expose the *full* upstream catalogue here, downloading any other
// language's data on demand (via UltraNet) into a writable per-user directory
// the moment it is requested. That is what "support all available languages"
// means in practice: every catalogue language is selectable and is fetched the
// first time it is used, then cached forever.
//
// Version: 0.1.0
// Last Modified: 2026-07-20
// Author: UltraCanvas Framework

#include "UltraCanvasOCRPlugin.h"

#ifdef ULTRACANVAS_OCR_SUPPORT

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <set>
#include <system_error>

#include "UltraCanvasUtils.h"   // GetExecutableDir()
#include "UltraCanvasConfig.h"  // GetResourcesDir()

#ifdef ULTRACANVAS_HAS_NET
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"
#endif

namespace UltraCanvas {

namespace {

namespace fs = std::filesystem;

// The full upstream Tesseract language catalogue (tessdata codes → English
// names), matching the LANGUAGES section of the Tesseract manual page. `osd`
// and `equ` are helper models rather than real languages and are flagged so
// the UI can group or hide them. Kept sorted by English name for display.
const std::vector<OCRLanguageInfo>& Catalogue() {
    static const std::vector<OCRLanguageInfo> kCatalogue = {
        {"afr",      "Afrikaans",                 false},
        {"sqi",      "Albanian",                  false},
        {"amh",      "Amharic",                   false},
        {"ara",      "Arabic",                    false},
        {"hye",      "Armenian",                  false},
        {"asm",      "Assamese",                  false},
        {"aze",      "Azerbaijani",               false},
        {"aze_cyrl", "Azerbaijani (Cyrillic)",    false},
        {"eus",      "Basque",                    false},
        {"bel",      "Belarusian",                false},
        {"ben",      "Bengali",                   false},
        {"bos",      "Bosnian",                   false},
        {"bre",      "Breton",                    false},
        {"bul",      "Bulgarian",                 false},
        {"mya",      "Burmese",                   false},
        {"cat",      "Catalan/Valencian",         false},
        {"ceb",      "Cebuano",                   false},
        {"chr",      "Cherokee",                  false},
        {"chi_sim",  "Chinese (Simplified)",      false},
        {"chi_tra",  "Chinese (Traditional)",     false},
        {"cos",      "Corsican",                  false},
        {"hrv",      "Croatian",                  false},
        {"ces",      "Czech",                     false},
        {"dan",      "Danish",                    false},
        {"div",      "Dhivehi",                   false},
        {"nld",      "Dutch/Flemish",             false},
        {"dzo",      "Dzongkha",                  false},
        {"eng",      "English",                   false},
        {"enm",      "English (Middle)",          false},
        {"epo",      "Esperanto",                 false},
        {"est",      "Estonian",                  false},
        {"fao",      "Faroese",                   false},
        {"fil",      "Filipino",                  false},
        {"fin",      "Finnish",                   false},
        {"fra",      "French",                    false},
        {"frm",      "French (Middle)",           false},
        {"glg",      "Galician",                  false},
        {"kat",      "Georgian",                  false},
        {"kat_old",  "Georgian (Old)",            false},
        {"deu",      "German",                    false},
        {"deu_latf", "German (Fraktur Latin)",    false},
        {"ell",      "Greek (Modern)",            false},
        {"grc",      "Greek (Ancient)",           false},
        {"guj",      "Gujarati",                  false},
        {"hat",      "Haitian Creole",            false},
        {"heb",      "Hebrew",                    false},
        {"hin",      "Hindi",                     false},
        {"hun",      "Hungarian",                 false},
        {"isl",      "Icelandic",                 false},
        {"ind",      "Indonesian",                false},
        {"iku",      "Inuktitut",                 false},
        {"gle",      "Irish",                     false},
        {"ita",      "Italian",                   false},
        {"ita_old",  "Italian (Old)",             false},
        {"jpn",      "Japanese",                  false},
        {"jav",      "Javanese",                  false},
        {"kan",      "Kannada",                   false},
        {"kaz",      "Kazakh",                    false},
        {"khm",      "Khmer (Central)",           false},
        {"kor",      "Korean",                    false},
        {"kor_vert", "Korean (Vertical)",         false},
        {"kmr",      "Kurdish (Kurmanji)",        false},
        {"kir",      "Kyrgyz",                    false},
        {"lao",      "Lao",                       false},
        {"lat",      "Latin",                     false},
        {"lav",      "Latvian",                   false},
        {"lit",      "Lithuanian",                false},
        {"ltz",      "Luxembourgish",             false},
        {"mkd",      "Macedonian",                false},
        {"msa",      "Malay",                     false},
        {"mal",      "Malayalam",                 false},
        {"mlt",      "Maltese",                   false},
        {"mri",      "Maori",                     false},
        {"mar",      "Marathi",                   false},
        {"mon",      "Mongolian",                 false},
        {"nep",      "Nepali",                    false},
        {"nor",      "Norwegian",                 false},
        {"oci",      "Occitan",                   false},
        {"ori",      "Oriya",                     false},
        {"pan",      "Punjabi",                   false},
        {"pus",      "Pashto",                    false},
        {"fas",      "Persian",                   false},
        {"pol",      "Polish",                    false},
        {"por",      "Portuguese",                false},
        {"que",      "Quechua",                   false},
        {"ron",      "Romanian",                  false},
        {"rus",      "Russian",                   false},
        {"san",      "Sanskrit",                  false},
        {"gla",      "Scottish Gaelic",           false},
        {"srp",      "Serbian",                   false},
        {"srp_latn", "Serbian (Latin)",           false},
        {"snd",      "Sindhi",                    false},
        {"sin",      "Sinhala",                   false},
        {"slk",      "Slovak",                    false},
        {"slv",      "Slovenian",                 false},
        {"spa",      "Spanish",                   false},
        {"spa_old",  "Spanish (Old)",             false},
        {"sun",      "Sundanese",                 false},
        {"swa",      "Swahili",                   false},
        {"swe",      "Swedish",                   false},
        {"syr",      "Syriac",                    false},
        {"tgk",      "Tajik",                     false},
        {"tam",      "Tamil",                     false},
        {"tat",      "Tatar",                     false},
        {"tel",      "Telugu",                    false},
        {"tha",      "Thai",                      false},
        {"bod",      "Tibetan",                   false},
        {"tir",      "Tigrinya",                  false},
        {"ton",      "Tonga",                     false},
        {"tur",      "Turkish",                   false},
        {"uig",      "Uyghur",                    false},
        {"ukr",      "Ukrainian",                 false},
        {"urd",      "Urdu",                      false},
        {"uzb",      "Uzbek",                     false},
        {"uzb_cyrl", "Uzbek (Cyrillic)",          false},
        {"vie",      "Vietnamese",                false},
        {"cym",      "Welsh",                     false},
        {"fry",      "West Frisian",              false},
        {"yid",      "Yiddish",                   false},
        {"yor",      "Yoruba",                    false},
        // Helper models — not languages, but valid tessdata downloads.
        {"osd",      "Orientation & Script Detection", true},
        {"equ",      "Math / Equation Detection", true},
    };
    return kCatalogue;
}

// First non-empty environment variable value, else empty.
std::string EnvOr(std::initializer_list<const char*> names) {
    for (const char* n : names) {
        if (const char* v = std::getenv(n); v && *v) return v;
    }
    return {};
}

// The user's platform data root, without the UltraCanvas suffix.
std::string PlatformDataRoot() {
#if defined(_WIN32)
    if (std::string v = EnvOr({"LOCALAPPDATA", "APPDATA"}); !v.empty()) return v;
    if (std::string h = EnvOr({"USERPROFILE"}); !h.empty())
        return (fs::path(h) / "AppData" / "Local").string();
    return ".";
#elif defined(__APPLE__)
    if (std::string h = EnvOr({"HOME"}); !h.empty())
        return (fs::path(h) / "Library" / "Application Support").string();
    return ".";
#else
    if (std::string v = EnvOr({"XDG_DATA_HOME"}); !v.empty()) return v;
    if (std::string h = EnvOr({"HOME"}); !h.empty())
        return (fs::path(h) / ".local" / "share").string();
    return ".";
#endif
}

// Directories that may hold "*.traineddata", most-preferred first. The
// writable user pack dir leads so freshly downloaded languages win; the
// bundled media/ocr/tessdata and system locations follow. Mirrors (and must
// stay in sync with) TesseractOCREngine::ResolveDataPath.
std::vector<std::string> DiscoveryDirs() {
    std::vector<std::string> dirs;
    auto add = [&](const std::string& base) {
        if (base.empty()) return;
        dirs.push_back(base);
        dirs.push_back((fs::path(base) / "tessdata").string());
    };

    add(UltraCanvasOCR::LanguageDataDir()); // writable, downloaded packs
    if (std::string env = EnvOr({"TESSDATA_PREFIX"}); !env.empty()) add(env);

    const std::string res = GetResourcesDir();  // ends with a separator
    const std::string exe = GetExecutableDir();
    add(res + "media/ocr");
    add(exe + "/media/ocr");
    if (!res.empty()) add(res.substr(0, res.size() - 1)); // <Resources>/tessdata
    add(exe + "/ocr");
    add(exe);
#if !defined(_WIN32)
    // Common Linux/macOS system install roots.
    add("/usr/share/tessdata");
    add("/usr/share/tesseract-ocr/tessdata");
    add("/usr/local/share/tessdata");
    add("/opt/homebrew/share/tessdata");
#endif
    return dirs;
}

bool IsKnownCode(const std::string& code) {
    return UltraCanvasOCR::FindLanguage(code) != nullptr;
}

std::string TrainedDataUrl(const std::string& code, OCRDataTier tier) {
    const char* repo = "tessdata_fast";
    switch (tier) {
        case OCRDataTier::Standard: repo = "tessdata";      break;
        case OCRDataTier::Best:     repo = "tessdata_best";  break;
        case OCRDataTier::Fast:
        default:                    repo = "tessdata_fast";  break;
    }
    return std::string("https://raw.githubusercontent.com/tesseract-ocr/")
         + repo + "/main/" + code + ".traineddata";
}

} // namespace

const std::vector<OCRLanguageInfo>& UltraCanvasOCR::SupportedLanguages() {
    return Catalogue();
}

const OCRLanguageInfo* UltraCanvasOCR::FindLanguage(const std::string& code) {
    for (const OCRLanguageInfo& l : Catalogue())
        if (l.code == code) return &l;
    return nullptr;
}

std::string UltraCanvasOCR::LanguageDataDir() {
    return (fs::path(PlatformDataRoot()) / "UltraCanvas" / "ocr" / "tessdata")
        .string();
}

bool UltraCanvasOCR::IsLanguageInstalled(const std::string& code,
                                         const std::string& dataDir) {
    if (code.empty()) return false;
    const std::string leaf = code + ".traineddata";
    std::error_code ec;
    if (!dataDir.empty()) {
        if (fs::exists(fs::path(dataDir) / leaf, ec)) return true;
        return fs::exists(fs::path(dataDir) / "tessdata" / leaf, ec);
    }
    for (const std::string& dir : DiscoveryDirs())
        if (fs::exists(fs::path(dir) / leaf, ec)) return true;
    return false;
}

std::vector<std::string> UltraCanvasOCR::InstalledLanguages() {
    std::set<std::string> found;
    std::error_code ec;
    for (const std::string& dir : DiscoveryDirs()) {
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            const fs::path& p = entry.path();
            if (p.extension() == ".traineddata")
                found.insert(p.stem().string());
        }
    }
    return {found.begin(), found.end()};
}

bool UltraCanvasOCR::DownloadLanguage(const std::string& code,
                                      OCRDataTier tier,
                                      std::string& outError,
                                      const std::string& destDir) {
    outError.clear();
    if (!IsKnownCode(code)) {
        outError = "Unknown language code '" + code + "'";
        return false;
    }
    const std::string dir = destDir.empty() ? LanguageDataDir() : destDir;
    const fs::path target = fs::path(dir) / (code + ".traineddata");

    std::error_code ec;
    if (fs::exists(target, ec)) return true; // already present

#ifndef ULTRACANVAS_HAS_NET
    outError = "Cannot download '" + code +
               "': this build has no network support (UltraNet). Place " +
               code + ".traineddata in \"" + dir + "\" manually.";
    return false;
#else
    fs::create_directories(dir, ec);
    if (ec) {
        outError = "Cannot create language directory \"" + dir +
                   "\": " + ec.message();
        return false;
    }

    if (!UltraNet_IsInitialized()) {
        UltraNet_Initialize();
    }

    // Stream to a ".part" file, then rename so a partial download never
    // masquerades as a valid pack.
    const fs::path tmp = fs::path(dir) / (code + ".traineddata.part");
    fs::remove(tmp, ec);

    UltraNetHttpOptions opts;
    opts.followRedirects = true;
    const std::string url = TrainedDataUrl(code, tier);
    UltraNetResult r = UltraNet_HttpDownloadFile(url, tmp.string(), opts);
    if (!r) {
        fs::remove(tmp, ec);
        outError = "Download failed for '" + code + "' (" + url + "): " +
                   (r.message.empty() ? "network error" : r.message);
        return false;
    }

    // Guard against an HTML error page or empty response being saved as data.
    const auto sz = fs::file_size(tmp, ec);
    if (ec || sz < 1024) {
        fs::remove(tmp, ec);
        outError = "Downloaded '" + code +
                   "' but the file is too small to be valid traineddata.";
        return false;
    }

    fs::rename(tmp, target, ec);
    if (ec) {
        // rename across devices can fail — fall back to copy.
        fs::copy_file(tmp, target,
                      fs::copy_options::overwrite_existing, ec);
        fs::remove(tmp, ec);
        if (!fs::exists(target, ec)) {
            outError = "Could not move downloaded '" + code +
                       "' into \"" + dir + "\".";
            return false;
        }
    }
    return true;
#endif // ULTRACANVAS_HAS_NET
}

bool UltraCanvasOCR::EnsureLanguages(const std::vector<std::string>& codes,
                                     std::string& outError,
                                     OCRDataTier tier) {
    outError.clear();

    // De-duplicate while preserving order; drop empties.
    std::vector<std::string> want;
    std::set<std::string> seen;
    for (const std::string& c : codes) {
        if (c.empty() || seen.count(c)) continue;
        seen.insert(c);
        want.push_back(c);
    }
    if (want.empty()) want.push_back("eng");

    // Consolidate every requested pack into a single directory so Tesseract —
    // which loads all languages from one datapath — can use them together.
    const std::string dir = LanguageDataDir();
    std::error_code ec;
    fs::create_directories(dir, ec); // best-effort; download re-checks

    for (const std::string& code : want) {
        const fs::path target = fs::path(dir) / (code + ".traineddata");
        if (fs::exists(target, ec)) continue;

        // Prefer seeding from an already-present local copy (e.g. the bundled
        // eng.traineddata or a system install) — keeps the offline path fast
        // and avoids re-downloading data we already ship.
        bool seeded = false;
        const std::string leaf = code + ".traineddata";
        for (const std::string& src : DiscoveryDirs()) {
            const fs::path candidate = fs::path(src) / leaf;
            // Skip the destination itself.
            if (fs::equivalent(fs::path(src), fs::path(dir), ec)) continue;
            if (fs::exists(candidate, ec)) {
                fs::create_directories(dir, ec);
                fs::copy_file(candidate, target,
                              fs::copy_options::overwrite_existing, ec);
                if (!ec && fs::exists(target, ec)) { seeded = true; break; }
            }
        }
        if (seeded) continue;

        std::string err;
        if (!DownloadLanguage(code, tier, err, dir)) {
            outError = err;
            return false;
        }
    }

    // Point the engine at the consolidated directory and rebuild with the
    // requested languages active.
    config.dataPath  = dir;
    config.languages = want;
    Rebuild();

    return true;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_OCR_SUPPORT
