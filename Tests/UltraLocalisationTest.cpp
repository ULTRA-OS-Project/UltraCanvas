// Tests/UltraLocalisationTest.cpp
// UltraLocalisation without a display or a desktop: catalogues written to
// a temporary tree - a shipped folder and a settings folder - and read
// back through Tr, TrC, TrN and TrF; the three layers and their order,
// the settings-folder override, the fallback walk, the base sets from a
// catalogue and from code, Language.conf and the language choice, the
// plural rules, the change handlers, and the catalogue's own round trip.
// Version: 1.1.0 - the added plural rules
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#include "UltraLocalisation/UltraLocalisation.h"

#include "UltraCanvasPathUtf8.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace UltraCanvas;
namespace fs = std::filesystem;

static int testCount = 0, failCount = 0;

static void Check(bool ok, const char* what) {
    ++testCount;
    if (!ok) { ++failCount; std::printf("FAIL: %s\n", what); }
}

static void Write(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

int main() {
    const fs::path root = fs::temp_directory_path() / ("ultralocalisation-test-" + std::to_string(static_cast<long long>(std::rand())));
    const fs::path shipped = root / "media" / "locale";
    const fs::path settings = root / "settings";
    std::error_code ec;
    fs::remove_all(root, ec);

    // ===== THE CATALOGUE FILE =====
    {
        LocalisationCatalogue c;
        std::string error;
        const std::string text =
            "{\n"
            "  \"@language\": \"de\", \"@name\": \"Deutsch\", \"@bases\": [\"Video\", \"Audio\"],\n"
            "  \"Save\": \"Speichern\",\n"
            "  \"button|Open\": \"Öffnen\",\n"
            "  \"{n} file\": { \"one\": \"{n} Datei\", \"other\": \"{n} Dateien\" },\n"
            "  \"ignored\": 12\n"
            "}";
        Check(LocalisationCatalogue::Parse(text, c, error), "a catalogue parses");
        Check(c.language == "de" && c.nativeName == "Deutsch", "metadata read");
        Check(c.bases.size() == 2 && c.bases[0] == "Video", "@bases read in order");
        Check(c.entries.at("Save") == "Speichern" && c.entries.at("button|Open") == "Öffnen", "entries read");
        Check(c.plurals.at("{n} file").at("other") == "{n} Dateien", "plural forms read");
        Check(c.entries.count("ignored") == 0, "a non-string value is left out");
        LocalisationCatalogue again;
        Check(LocalisationCatalogue::Parse(c.Serialize(), again, error), "Serialize parses back");
        Check(again.entries == c.entries && again.plurals == c.plurals && again.bases == c.bases && again.nativeName == c.nativeName,
              "the round trip keeps everything");
        Check(!LocalisationCatalogue::Parse("[1, 2]", c, error) && !error.empty(), "an array is not a catalogue");
        Check(!LocalisationCatalogue::Parse("{ not json", c, error), "broken JSON reported");
    }

    // ===== LANGUAGE.CONF =====
    {
        LanguageSettings s = LanguageSettings::Parse("language=de_at\r\ntranslator=Erika\n");
        Check(s.language == "de-AT" && s.translator == "Erika", "Language.conf parsed, tag canonical");
        Check(LanguageSettings::Parse("").language == "system", "no file is the system's language");
        Check(LanguageSettings::Parse(s.Serialize()) == s, "Language.conf round trip");
        Check(s.Save(PathToUtf8(settings)), "Language.conf saved");
        Check(LanguageSettings::Load(PathToUtf8(settings)) == s, "Language.conf loaded");
        Check(LanguageSettings::FilePath("").empty() && !LanguageSettings::Load("").translator.size(), "no settings folder");
        LanguageSettings system;
        system.Save(PathToUtf8(settings));   // back to "system" for the choice below
    }

    // ===== THE TREE =====
    Write(shipped / "de" / "Standard.json",
          "{ \"@language\": \"de\", \"@name\": \"Deutsch\", \"Save\": \"Speichern\", \"Cancel\": \"Abbrechen\", \"Delete\": \"Löschen\","
          " \"button|Open\": \"Öffnen\", \"state|Open\": \"Geöffnet\", \"Deleted {0} of {1}\": \"{0} von {1} gelöscht\","
          " \"{n} file\": { \"one\": \"{n} Datei\", \"other\": \"{n} Dateien\" } }");
    Write(shipped / "de-AT" / "Standard.json", "{ \"@language\": \"de-AT\", \"Cancel\": \"Abbrechen (AT)\" }");
    Write(shipped / "de" / "base" / "Video.json", "{ \"Trim\": \"Trimmen\", \"Fade out\": \"Ausblenden\", \"Save\": \"Speichern (Video)\" }");
    Write(shipped / "de" / "base" / "Audio.json", "{ \"Trim\": \"Beschneiden\", \"Gain\": \"Verstärkung\" }");
    Write(shipped / "en" / "UltraVideo.json", "{ \"@language\": \"en\", \"@bases\": [\"Video\", \"Audio\"], \"Fade out the audio\": \"Fade out the audio\" }");
    Write(shipped / "de" / "UltraVideo.json", "{ \"@language\": \"de\", \"Fade out the audio\": \"Ton ausblenden\", \"Delete\": \"Clip entfernen\" }");
    Write(shipped / "fr" / "Standard.json", "{ \"@language\": \"fr\", \"@name\": \"Français\", \"Save\": \"Enregistrer\","
          " \"{n} file\": { \"one\": \"{n} fichier\", \"other\": \"{n} fichiers\" } }");
    Write(shipped / "pl" / "Standard.json", "{ \"{n} file\": { \"one\": \"{n} plik\", \"few\": \"{n} pliki\", \"many\": \"{n} plików\" } }");
    Write(shipped / "ja" / "Standard.json", "{ \"{n} file\": \"{n} 個のファイル\" }");
    Write(settings / "locale" / "de" / "UltraVideo.json", "{ \"Fade out the audio\": \"Audio ausblenden\" }");

    UltraLocalisation::Reset();
    UltraLocalisation::SetFolders(PathToUtf8(shipped) + "/", PathToUtf8(settings));
    Check(UltraLocalisation::ShippedFolder() == PathToUtf8(shipped), "the shipped folder, separator dropped");

    // ===== AVAILABLE LANGUAGES, NATIVE NAMES =====
    {
        const std::vector<std::string> langs = UltraLocalisation::AvailableLanguages();
        Check(langs == std::vector<std::string>({"de", "de-AT", "en", "fr", "ja", "pl"}), "the languages with a catalogue, en always, sorted");
        Check(UltraLocalisation::NativeName("de") == "Deutsch" && UltraLocalisation::NativeName("fr") == "Français", "native names");
        Check(UltraLocalisation::NativeName("pl") == "pl", "no @name: the tag");
    }

    // ===== ENGLISH: THE SOURCE IS THE TEXT =====
    Check(UltraLocalisation::Load("en", "UltraVideo"), "English loads");
    Check(Tr("Save") == "Save" && TrN("{n} file", "{n} files", 1) == "1 file" && TrN("{n} file", "{n} files", 2) == "2 files", "English is the source");
    Check(UltraLocalisation::BaseSets() == std::vector<std::string>({"Video", "Audio"}), "@bases read from the en catalogue");
    Check(UltraLocalisation::OriginOf("Save") == LocalisationOrigin::Source, "nothing translated in English");

    // ===== THE LAYERS =====
    Check(UltraLocalisation::Load("de", "UltraVideo"), "German loads");
    Check(UltraLocalisation::CurrentLanguage() == "de" && UltraLocalisation::CurrentApplication() == "UltraVideo", "current language and application");
    Check(Tr("Fade out the audio") == "Audio ausblenden", "the settings folder's file beats the shipped one");
    Check(Tr("Delete") == "Clip entfernen", "the application's set amends the standard");
    Check(Tr("Trim") == "Trimmen", "the first base named wins over the second");
    Check(Tr("Gain") == "Verstärkung", "the second base is read");
    Check(Tr("Fade out") == "Ausblenden", "a base entry");
    Check(Tr("Save") == "Speichern (Video)", "a base amends the standard too");
    Check(Tr("Cancel") == "Abbrechen", "the standard set");
    Check(Tr("Not in any set") == "Not in any set", "an unknown key is shown as it is");
    Check(TrC("button", "Open") == "Öffnen" && TrC("state", "Open") == "Geöffnet", "context keys");
    Check(TrC("menu", "Open") == "Open", "an unknown context falls back to the source");
    Check(UltraLocalisation::OriginOf("Delete") == LocalisationOrigin::Application, "origin: application");
    Check(UltraLocalisation::OriginOf("Trim") == LocalisationOrigin::Base, "origin: base");
    Check(UltraLocalisation::OriginOf("Cancel") == LocalisationOrigin::Standard, "origin: standard");
    Check(UltraLocalisation::OriginOf("{n} file") == LocalisationOrigin::Standard, "origin of a plural entry");
    Check(UltraLocalisation::OriginOf("Nothing") == LocalisationOrigin::Source, "origin: source");
    Check(UltraLocalisation::EntryCount(LocalisationOrigin::Standard) == 7 && UltraLocalisation::EntryCount(LocalisationOrigin::Base) == 5,
          "entry counts per layer");
    {
        const std::vector<std::string> keys = UltraLocalisation::StandardKeys();
        Check(keys.size() == 7 && keys.front() == "Cancel", "standard keys, sorted");
    }

    // ===== PLACEHOLDERS, PLURALS =====
    Check(TrF("Deleted {0} of {1}", 1, 3) == "1 von 3 gelöscht", "TrF with integers");
    Check(TrF("Deleted {0} of {1}", std::string("a"), "b") == "a von b gelöscht", "TrF with strings");
    Check(TrF("{0} and {1}", 2.5, true) == "2.5 and true", "TrF: a double dot-decimal, a bool");
    Check(TrF("{0} {1}", 1) == "1 {1}", "a missing argument leaves its placeholder");
    Check(TrN("{n} file", "{n} files", 1) == "1 Datei" && TrN("{n} file", "{n} files", 5) == "5 Dateien", "German plural");
    Check(TrN("{n} item", "{n} items", 0) == "0 items" && TrN("{n} item", "{n} items", 1) == "1 item", "untranslated plural falls back to English forms");

    // ===== FALLBACK WALK =====
    Check(UltraLocalisation::Load("de-AT", "UltraVideo"), "de-AT loads");
    Check(Tr("Cancel") == "Abbrechen (AT)", "the regional file wins per key");
    Check(Tr("Save") == "Speichern (Video)" && Tr("Trim") == "Trimmen", "the rest comes from de");
    Check(UltraLocalisation::Load("zh-Hant-TW", "UltraVideo") == false, "a language with no catalogue: false");
    Check(Tr("Save") == "Save", "... and everything English");

    // ===== BASES FROM CODE, LIBRARY SETS =====
    UltraLocalisation::UseBaseSets({"Audio"});
    UltraLocalisation::Load("de", "UltraVideo");
    Check(Tr("Trim") == "Beschneiden" && Tr("Fade out") == "Fade out", "UseBaseSets replaces the catalogue's bases");
    Check(UltraLocalisation::BaseSets() == std::vector<std::string>({"Audio"}), "BaseSets reports the ones in use");
    Write(shipped / "de" / "UltraCanvasCharts.json", "{ \"Trim\": \"Zuschneiden (Charts)\", \"Axis\": \"Achse\" }");
    UltraLocalisation::AddSet("UltraCanvasCharts");
    UltraLocalisation::Load("de", "UltraVideo");
    Check(Tr("Axis") == "Achse", "a library set is read");
    Check(Tr("Trim") == "Zuschneiden (Charts)", "a library set comes before the bases");
    Check(UltraLocalisation::OriginOf("Axis") == LocalisationOrigin::Library, "origin: library");
    Check(UltraLocalisation::LibrarySets() == std::vector<std::string>({"UltraCanvasCharts"}), "library sets listed");

    // ===== PLURAL RULES =====
    Check(UltraLocalisation::PluralCategory("en", 1) == "one" && UltraLocalisation::PluralCategory("en", 0) == "other", "en");
    Check(UltraLocalisation::PluralCategory("fr", 0) == "one" && UltraLocalisation::PluralCategory("fr", 2) == "other", "fr: 0 and 1 are one");
    Check(UltraLocalisation::PluralCategory("ja", 1) == "other", "ja has one form");
    Check(UltraLocalisation::PluralCategory("ru", 1) == "one" && UltraLocalisation::PluralCategory("ru", 3) == "few" &&
          UltraLocalisation::PluralCategory("ru", 5) == "many" && UltraLocalisation::PluralCategory("ru", 11) == "many" &&
          UltraLocalisation::PluralCategory("ru", 21) == "one" && UltraLocalisation::PluralCategory("ru", 22) == "few", "ru");
    Check(UltraLocalisation::PluralCategory("pl", 1) == "one" && UltraLocalisation::PluralCategory("pl", 2) == "few" &&
          UltraLocalisation::PluralCategory("pl", 5) == "many" && UltraLocalisation::PluralCategory("pl", 22) == "few" &&
          UltraLocalisation::PluralCategory("pl", 12) == "many", "pl");
    Check(UltraLocalisation::PluralCategory("cs", 3) == "few" && UltraLocalisation::PluralCategory("cs", 5) == "other", "cs");
    Check(UltraLocalisation::PluralCategory("ar", 0) == "zero" && UltraLocalisation::PluralCategory("ar", 2) == "two" &&
          UltraLocalisation::PluralCategory("ar", 7) == "few" && UltraLocalisation::PluralCategory("ar", 15) == "many" &&
          UltraLocalisation::PluralCategory("ar", 100) == "other", "ar");
    Check(UltraLocalisation::PluralCategory("lv", 0) == "zero" && UltraLocalisation::PluralCategory("lv", 21) == "one", "lv");
    Check(UltraLocalisation::PluralCategory("ga", 4) == "few" && UltraLocalisation::PluralCategory("ga", 8) == "many", "ga");
    Check(UltraLocalisation::PluralCategory("sl", 102) == "two" && UltraLocalisation::PluralCategory("sl", 3) == "few", "sl");
    Check(UltraLocalisation::PluralCategory("de-AT", 1) == "one", "a regional tag uses its language's rule");
    Check(UltraLocalisation::PluralCategory("br", 1) == "one" && UltraLocalisation::PluralCategory("br", 71) == "other" &&
          UltraLocalisation::PluralCategory("br", 22) == "two" && UltraLocalisation::PluralCategory("br", 3) == "few" &&
          UltraLocalisation::PluralCategory("br", 13) == "other" && UltraLocalisation::PluralCategory("br", 1000000) == "many", "Breton");
    Check(UltraLocalisation::PluralCategory("gd", 11) == "one" && UltraLocalisation::PluralCategory("gd", 12) == "two" &&
          UltraLocalisation::PluralCategory("gd", 15) == "few" && UltraLocalisation::PluralCategory("gd", 20) == "other", "Scottish Gaelic");
    Check(UltraLocalisation::PluralCategory("shi", 0) == "one" && UltraLocalisation::PluralCategory("shi", 5) == "few" &&
          UltraLocalisation::PluralCategory("shi", 11) == "other", "Tachelhit");
    Check(UltraLocalisation::PluralCategory("tzm", 50) == "one" && UltraLocalisation::PluralCategory("tzm", 5) == "other", "Tamazight");
    Check(UltraLocalisation::PluralCategory("gv", 21) == "one" && UltraLocalisation::PluralCategory("gv", 40) == "few" &&
          UltraLocalisation::PluralCategory("gv", 5) == "other", "Manx");
    Check(UltraLocalisation::PluralCategory("kw", 0) == "zero" && UltraLocalisation::PluralCategory("kw", 22) == "two" &&
          UltraLocalisation::PluralCategory("kw", 43) == "few" && UltraLocalisation::PluralCategory("kw", 21) == "many" &&
          UltraLocalisation::PluralCategory("kw", 7) == "other", "Cornish");
    Check(UltraLocalisation::PluralCategory("fil", 3) == "one" && UltraLocalisation::PluralCategory("fil", 14) == "other", "Filipino");
    Check(UltraLocalisation::PluralCategory("he", 20) == "many" && UltraLocalisation::PluralCategory("he", 3) == "other", "Hebrew's many");
    Check(UltraLocalisation::PluralCategory("pt-BR", 0) == "one" && UltraLocalisation::PluralCategory("pt-PT", 0) == "other", "pt and pt-PT differ");
    Check(UltraLocalisation::PluralCategory("se", 2) == "two" && UltraLocalisation::PluralCategory("ksh", 0) == "zero", "Sami and Colognian");
    UltraLocalisation::UseBaseSets({});
    UltraLocalisation::Load("pl", "");
    Check(TrN("{n} file", "{n} files", 1) == "1 plik" && TrN("{n} file", "{n} files", 3) == "3 pliki" &&
          TrN("{n} file", "{n} files", 5) == "5 plików" && TrN("{n} file", "{n} files", 22) == "22 pliki", "Polish forms picked by rule");
    UltraLocalisation::Load("ja", "");
    Check(TrN("{n} file", "{n} files", 3) == "3 個のファイル", "a one-form language keeps a plain entry");
    UltraLocalisation::Load("fr", "");
    Check(TrN("{n} file", "{n} files", 0) == "0 fichier", "French: zero is singular");

    // ===== THE CHOICE OF LANGUAGE =====
    UltraLocalisation::SetLanguageOverride("");
    Check(UltraLocalisation::ChooseLanguage({"pt-BR", "fr-CA", "en"}) == "fr", "the first desktop language with a catalogue, walked down");
    Check(UltraLocalisation::ChooseLanguage({"pt-BR"}) == "en", "none with a catalogue: en");
    Check(UltraLocalisation::ChooseLanguage({}) == "en", "no desktop language: en");
    {
        LanguageSettings s;
        s.language = "de";
        s.Save(PathToUtf8(settings));
        Check(UltraLocalisation::ChooseLanguage({"fr"}) == "de", "Language.conf beats the desktop");
        s.language = "system";
        s.Save(PathToUtf8(settings));
    }
    UltraLocalisation::SetLanguageOverride("pl");
    Check(UltraLocalisation::ChooseLanguage({"fr"}) == "pl", "the override beats everything");
    UltraLocalisation::SetLanguageOverride("");
    Check(UltraLocalisation::Initialize("UltraVideo", {"de-CH", "fr"}), "Initialize chooses and loads");
    Check(UltraLocalisation::CurrentLanguage() == "de" && Tr("Cancel") == "Abbrechen", "... de for de-CH");

    // ===== CHANGE HANDLERS =====
    {
        std::vector<std::string> seen;
        const int id = UltraLocalisation::AddChangeHandler([&seen](const std::string& language) { seen.push_back(language); });
        UltraLocalisation::SetLanguage("fr");
        Check(seen == std::vector<std::string>({"fr"}) && Tr("Save") == "Enregistrer", "SetLanguage reloads and tells the handler");
        UltraLocalisation::RemoveChangeHandler(id);
        UltraLocalisation::SetLanguage("de");
        Check(seen.size() == 1, "a removed handler is not called");
    }

    UltraLocalisation::Reset();
    Check(Tr("Save") == "Save" && UltraLocalisation::CurrentLanguage() == "en", "Reset is English with nothing loaded");
    fs::remove_all(root, ec);

    std::printf("UltraLocalisationTest: %d checks, %d failed\n", testCount, failCount);
    return failCount == 0 ? 0 : 1;
}
