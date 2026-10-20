# UltraCanvas Start Page Documentation

<!-- doc-check: void OpenSite(const std::string& dir); void PickFolder(); std::shared_ptr<UltraCanvas::UltraCanvasWindow> window; -->

## Overview

**UltraCanvasStartPage** is the page an application shows when it has
nothing open yet: a title, the reason this page and not the thing itself,
the entries opened last as buttons, the one action that opens an existing
thing, a section of the application's own fields for making a new one, and
a status line that says why something could not be done.

Started with no argument - a double-click on Windows - a program that
needs a file or a folder used to print its usage into a console that closed
before it could be read, which looked like a crash. UltraPress's start
window (a site folder) and UltraFIBU's (a bookkeeping file) are both this
page with their own words and their own "new" fields.

**File Location**: `include/UltraCanvasStartPage.h`
**Version**: 1.1.0
**Base Class**: `UltraCanvasContainer`
**Author**: UltraCanvas Framework

## Features

- ✅ Parts laid out **top to bottom in the order they are added**, at the
  width the page was made with; `PreferredHeight()` is what the window
  holding it should be
- ✅ A **section of the application's own fields**, laid out row by row:
  a captioned field, a note, a button (`AddField`, `AddNote`, `AddButton`)
- ✅ **Recent entries** as buttons: the last path part and the whole entry,
  the whole entry as the tooltip where the button shortens it
- ✅ The **action** as the theme's primary button, with a muted note under it
- ✅ A **section** for the application's own fields: a heading and a
  container of the named height, the fields placed in it from (0, 0)
- ✅ A **status line**, muted or in the theme's error colour
- ✅ Everything follows the framework theme (`UltraCanvasTheme`): light or
  dark, the headings and notes by their label variant

## Header Include

```cpp
#include "UltraCanvasStartPage.h"
```

## Class Reference

```cpp
explicit UltraCanvasStartPage(const std::string& identifier, int width);

void SetTitle(const std::string& text);
void SetHint(const std::string& text);                       // up to three lines
void SetRecent(const std::string& heading, const std::vector<std::string>& entries,
               std::function<void(const std::string&)> onPick, int shown = 5);
std::shared_ptr<UltraCanvasButton> AddAction(const std::string& text, std::function<void()> onClick,
                                             const std::string& note = std::string(), bool primary = true);
std::shared_ptr<UltraCanvasContainer> AddSection(const std::string& heading, int height = 0);
int AddField(const std::string& caption, const std::shared_ptr<UltraCanvasUIElement>& input,
             int captionWidth = 190);                        // a row of the current section
std::shared_ptr<UltraCanvasLabel> AddNote(const std::string& text, int height = 40);
std::shared_ptr<UltraCanvasButton> AddButton(const std::string& text, std::function<void()> onClick,
                                             int width = 0);  // 0: the column
static int RowHeight();                                      // 34
std::shared_ptr<UltraCanvasLabel> AddStatusLine();
void Say(const std::string& text, bool error);

int PreferredHeight() const;
int Width() const;
static int Margin();            // the content column's distance from the edge
int ContentWidth() const;       // the column's width: the sections are this wide
```

## Usage

A start window: the page in a window of its preferred height. The
application keeps the recent list itself (a list in its settings file,
`UltraCanvasAppSettings.h`) and decides what a picked entry means.

```cpp
auto page = std::make_shared<UltraCanvas::UltraCanvasStartPage>("start", 620);
page->SetTitle("UltraPress");
page->SetHint("No site is open yet. Open a site folder, or create a new site.");
page->SetRecent("Opened last", {"/home/erika/sites/shop", "/home/erika/sites/blog"},
                [](const std::string& dir) { OpenSite(dir); });
page->AddAction("Open a site folder\xE2\x80\xA6", []() { PickFolder(); },
                "A folder with a site.json in it.");

// The application's own fields for a new site: a section the page lays
// out row by row - the caption at the left, the input beside it as wide as
// the rest of the column, a button, a note under them.
page->AddSection("A new site");
auto name = UltraCanvas::CreateTextInput("name", 0, 0, 0, 0);
page->AddField("Site name", name, 96);
page->AddButton("Create it in a folder\xE2\x80\xA6", []() { PickFolder(); }, 300);
page->AddNote("The starter site goes into the folder you choose; an empty or a new one.");

page->AddStatusLine();
window->AddChild(page);
window->SetWindowSize(page->Width(), page->PreferredHeight());
```

Why something could not be done goes on the status line:

```cpp
page->Say("\"/home/erika/sites/old\" holds no site any more.", true);   // the theme's error colour
page->Say("Creating the site\xE2\x80\xA6", false);                        // muted
```

## Notes

- The page is a container with absolute children; add it to a window at
  (0, 0) and size the window to `Width()` by `PreferredHeight()`.
- The hint has room for three lines of 14 px text: a folder's full path and
  what was wrong with it.
- A section grows with the rows `AddField`, `AddNote` and `AddButton` put
  in it, `RowHeight()` tall with 10 px between them, until the next part is
  added; an input with a width of its own (a date picker, a dropdown) keeps
  its size, one without gets the rest of the column. A section made with a
  height is the application's to lay out from (0, 0) in the container
  `AddSection` returns, `ContentWidth()` wide.
