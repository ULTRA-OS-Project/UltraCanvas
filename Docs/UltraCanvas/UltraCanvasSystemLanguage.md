# UltraCanvasSystemLanguage

**The languages the person's desktop is set to, best first, as BCP 47
tags.** One detector for every caller: the spell checker picks a
dictionary by it, the localisation module
([`UltraLocalisation`](../Modules/UltraLocalisation/README.md)) picks a
catalogue by it, and its numbers and dates follow the region the platform's
settings name ([`SystemRegionalFormats`](#the-regions-formats)), so a German
desktop is German in every window.

- Header: `UltraCanvas/include/UltraCanvasSystemLanguage.h`
- Implementation: `UltraCanvas/core/UltraCanvasSystemLanguage.cpp`, the
  platform's own answer in `OS/<Platform>/UltraCanvas<Platform>SystemLanguage.*`
- Tests: `Tests/SystemLanguageTest.cpp` (target `SystemLanguageTest`, no display)
- Used by: `UltraCanvasSpellChecker::DetectPreferredLanguage`
  ([`UltraCanvasSpellChecker.md`](UltraCanvasSpellChecker.md)) and the
  application base's start-up, which hands both answers to UltraLocalisation

```cpp
// Best first, each once: {"de-AT", "de", "en"} on a desktop with LANGUAGE=de_AT:de:en
std::vector<std::string> wanted = SystemLanguageTags();
// The first of them, or "en" when nothing names a language
std::string language = SystemLanguage();
// A first run picks the dictionary, catalogue or format that matches
for (const std::string& tag : wanted) {
    if (LanguageTagsEqual(tag, "de-AT") || LanguageTagBase(tag) == "de") break;
}
```

## Where the answer comes from

Two sources, in this order, each tag once:

1. **The POSIX variables, as gettext reads them.** `LC_ALL`, else
   `LC_MESSAGES`, else `LANG` names the locale; when that locale is a real
   one (not `C`, `POSIX` or empty), the `LANGUAGE` list - a colon-separated
   priority list, `de_AT:de:en` - comes first, then the locale. Under the
   C locale `LANGUAGE` is ignored, as gettext ignores it: a list with no
   locale behind it is a shell's leftover, not a choice.
2. **The platform's own list**, `SystemLanguageTagsNative()`, defined under
   `OS/<Platform>/`:

| Platform | Source |
|---|---|
| Windows | `GetUserPreferredUILanguages` (Settings > Language, in the person's order), then `GetUserDefaultLocaleName`. The POSIX variables are normally unset on Windows, so this is usually the whole answer |
| macOS | `NSLocale.preferredLanguages` (Language & Region, in order), then the current locale's identifier |
| Linux, BSD | Nothing beyond the variables, which the display manager and the desktop set for the session from the language the person chose |
| WebAssembly | `navigator.languages`, else `navigator.language` |
| Android | The language and country of the device configuration the activity started with; nothing before the activity exists |

Every spelling is folded to one: `de_DE.UTF-8@euro`, `de-de` and `DE_DE`
are `de-DE`; a four-letter script is title case (`zh-Hant-TW`), a region
upper case, a numeric region kept (`es-419`).

## The pure helpers

The rules are plain functions of their inputs, so `SystemLanguageTest`
holds every platform's rule on the CI machine with no desktop:

```cpp
std::string tag = LanguageTagFromPosixLocale("de_DE.UTF-8@euro");             // "de-DE"
std::string none = LanguageTagFromPosixLocale("C.UTF-8");                       // "" - names no language
std::vector<std::string> tags = LanguageTagsFromEnvironment("de_AT:de", "", "", "de_DE.UTF-8");   // {"de-AT", "de", "de-DE"}
std::string base = LanguageTagBase("zh-Hant-TW");                              // "zh"
bool same = LanguageTagsEqual("de_AT", "de-at");                               // true
bool region = LanguageTagsEqual("de", "de-AT");                                // false: a language is not its region
```

## The region's formats

`SystemRegionalFormats()` is the region's conventions for numbers, dates
and times - an `UltraLocalisation` `LocaleFormats`: the separators, the
group sizes, the date and time patterns - over the built-in CLDR
conventions of the region (`UltraLocalisation::FormatsFor`), with the
person's own changes where the platform keeps them. Its `language` is the
region's tag, empty where the platform has nothing beyond its language.
The application base hands it to `UltraLocalisation::SetRegionalFormats`
before the language is chosen.

```cpp
LocaleFormats region = SystemRegionalFormats();   // region.language "de-DE", region.decimalSeparator ","
if (!region.language.empty()) UltraLocalisation::SetRegionalFormats(region);
```

| Platform | `SystemRegionalFormatsNative()` |
|---|---|
| Windows | `GetLocaleInfoEx` for the user default locale (Settings > Time & language > Region): the decimal and thousands separators, the grouping, the short and long date and the short and long time - the long date is the full one when it has the weekday. Windows' pictures (`dddd, d. MMMM yyyy`, `h:mm tt`) become CLDR patterns through `UltraLocalisation::PatternFromWindows` |
| macOS, iOS | `NSLocale.currentLocale` (Language & Region): its separators, `NSNumberFormatter`'s group sizes, and `NSDateFormatter`'s pattern for each date and time style, the 24-hour switch included |
| Linux, BSD | `LC_TIME` and `LC_NUMERIC`, each under `LC_ALL`, over `LANG` - what the desktops' Formats setting exports - with the CLDR conventions of the regions they name: a person reading English with German dates has `LANG=en_US.UTF-8` and `LC_TIME=de_DE.UTF-8`. glibc's own `nl_langinfo` patterns are not used, being coarser than CLDR's (en_US's 12-hour clock pads the hour, en_GB's date has a two-digit year) and missing where the locale is not installed. `C` and `POSIX` name no region |
| WebAssembly, Android | Nothing: `navigator.language` and the device configuration, which `SystemLanguageTagsNative` gives already, name the region, and UltraLocalisation keeps that tag for its conventions |

A platform's short date with a two-digit year is given the year in full
(`UltraLocalisation::PatternWithFullYear`), since `DateStyle::Short`
writes it so.

## Related

- [`UltraCanvasSpellChecker.md`](UltraCanvasSpellChecker.md) - the first caller; its backend's `GetPreferredLanguageHint` is a tie-breaker after this
- [`UltraLocalisation`](../Modules/UltraLocalisation/README.md) - the language a catalogue is chosen by, and the numbers and dates written in the region's conventions
- [`UltraCanvasLocalisationProposal.md`](UltraCanvasLocalisationProposal.md) - the design
- [`UltraCanvasDesktopEntry.md`](UltraCanvasDesktopEntry.md) - reads the raw POSIX locale itself, because a `.desktop` file's `Name[de_DE@euro]` keys want the modifier the tag drops
