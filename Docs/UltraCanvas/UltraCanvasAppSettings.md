# UltraCanvas App Settings Documentation

## Overview

**UltraCanvasAppSettings** is an application's own preferences in one JSON
file of the shared settings folder: `<settings folder>/<App>/settings.json`
(`UltraCanvasSettingsFolder.h` names the folder per platform:
`%APPDATA%\UltraCanvas` on Windows, `~/Library/Application Support/UltraCanvas`
on macOS, `~/.config/UltraCanvas` elsewhere). The look chosen last, the
window's size, the things opened last - every application has a few, and
before this each kept them its own way. This class is the one place:
UltraPress's `settings.json` (`ui/UltraPressSettings.h`) is one, UltraFIBU's
list of the bookkeepings opened last is another.

The values are typed, so a program reads `GetInt("window.width", 0)` and
never parses. A dotted key is a path into nested objects, so
`"window.width"` is `{"window": {"width": 1200}}` in the file: it stays
readable, and a group of values has a name. A list of strings is a JSON
array, and two calls keep a recent list on one: `Remember` puts an entry
first, once, and keeps the newest eight (or as many as asked); `Forget`
drops one. The entries are the application's strings - a path it
normalised, a name - compared as text.

**File Location**: `include/UltraCanvasAppSettings.h`
**Version**: 1.0.0
**Author**: UltraCanvas Framework

## Features

- ✅ **One file per application**, under the shared settings folder, named
  from the application's name
- ✅ **Typed values** - string, int, bool, number, list of strings - each
  with a fallback for a value that is missing or of another type
- ✅ **Dotted keys** as nested objects (`"window.width"`)
- ✅ **A recent list** on any key: `Remember` (first, once, capped) and
  `Forget`
- ✅ **Nothing read or written on its own**: `Load()` reads the file,
  `Save()` writes it whole, pretty, and makes the folder first
- ✅ A missing, unreadable or broken file is **the defaults**, and `Load()`
  says so; without a home the file is empty and both calls do nothing
- ✅ The whole **document** for what the typed calls do not cover
  (`Document()`, an `UltraCanvasJSON` value)

## API

```cpp
explicit UltraCanvasAppSettings(const std::string& app);   // <settings folder>/<app>/settings.json
static UltraCanvasAppSettings AtFile(const std::string& file);   // a test's temp file
const std::string& File() const;        // UTF-8; empty without a settings folder

bool Load();                            // false: none, unreadable, not an object - the defaults
bool Save() const;                      // false: cannot be written

bool Has(const std::string& key) const;
std::string GetString(const std::string& key, const std::string& fallback = "") const;
int GetInt(const std::string& key, int fallback = 0) const;
bool GetBool(const std::string& key, bool fallback = false) const;
double GetNumber(const std::string& key, double fallback = 0.0) const;
std::vector<std::string> GetList(const std::string& key) const;   // each once, the empty ones dropped

void Set(const std::string& key, const std::string& value);
void Set(const std::string& key, const char* value);
void Set(const std::string& key, int value);
void Set(const std::string& key, bool value);
void Set(const std::string& key, double value);
void Set(const std::string& key, const std::vector<std::string>& values);
bool Remove(const std::string& key);

void Remember(const std::string& key, const std::string& entry, std::size_t keep = 8);
void Forget(const std::string& key, const std::string& entry);

JSONValue& Document();
```

## Usage

The program's settings, read before its window opens and written when
something changes. Read again before a write when another part of the
program may have written meanwhile - the file is small, and the last
writer wins otherwise.

```cpp
UltraCanvas::UltraCanvasAppSettings settings("UltraPress");   // <settings folder>/UltraPress/settings.json
settings.Load();                                              // false: nothing saved yet
const bool dark = settings.GetBool("dark", false);
const int width = settings.GetInt("window.width", 1500);
const int height = settings.GetInt("window.height", 920);
const std::vector<std::string> recent = settings.GetList("recent");

settings.Set("dark", true);
settings.Set("window.width", 1200);
settings.Set("window.height", 700);
settings.Remember("recent", "/home/erika/sites/shop");   // first, once, eight kept
settings.Forget("recent", "/home/erika/sites/old");      // gone since
settings.Save();
```

The file this writes:

```json
{
  "dark": true,
  "window": {
    "width": 1200,
    "height": 700
  },
  "recent": [
    "/home/erika/sites/shop"
  ]
}
```

An application with a typed view of its own, like UltraPress's
`UltraPressSettings` struct, reads the file through this class and keeps
the typed fields itself; what it gains is the file's place, its format and
the list handling, which are then the same in every application.

A test runs it on a temp folder:

```cpp
UltraCanvas::UltraCanvasAppSettings test = UltraCanvas::UltraCanvasAppSettings::AtFile("/tmp/t/settings.json");
test.Set("look", "dark");
test.Save();
```

## Notes

- Every path in and out is UTF-8, as every path in an UltraCanvas
  application is (`UltraCanvasPathUtf8.h`).
- A value of another type than asked for is the fallback: `GetInt` on a
  string is the fallback, not 0 parsed out of it.
- `Set` on a dotted key makes the objects on the way and replaces a scalar
  in the way; `Set` with a scalar on a key that holds an object replaces
  the object.
- `AppSettingsTest` (`Tests/AppSettingsTest.cpp`) covers it.

## Related

- `UltraCanvasSettingsFolder.h` - the folder the file is in
- `UltraCanvasDataFolder.h` - the data folder, for what is not a preference
- `UltraCanvasStartPage` - the page that shows the recent list
