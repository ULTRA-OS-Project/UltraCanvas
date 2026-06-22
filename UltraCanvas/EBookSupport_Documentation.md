# UltraCanvas EBook Support Documentation

```cpp
// Version: 1.0.0
// Last Modified: 2025-01-10
// Author: UltraCanvas Framework
```

---

## Table of Contents

1. [Overview](#1-overview)
2. [Architecture](#2-architecture)
3. [EBook Support Functionality](#3-ebook-support-functionality)
4. [UI Handling Features](#4-ui-handling-features)
5. [Complete Function Reference](#5-complete-function-reference)
6. [Example Code](#6-example-code)
7. [File Structure](#7-file-structure)

---

## 1. Overview

The UltraCanvas EBook Support system provides a comprehensive, cross-platform solution for reading and displaying electronic books. It supports multiple eBook formats, offers professional-grade rendering, and integrates seamlessly with the UltraCanvas UI framework.

### Key Features

| Feature | Description |
|---------|-------------|
| **Multi-Format Support** | EPUB, FB2, MOBI, AZW3, PDF, CBZ, CBR, DJVU, TXT, RTF, HTML |
| **Professional Rendering** | High-quality text and image rendering via HTMLConverter |
| **Flexible Display** | Single page, double page, continuous scroll modes |
| **Reading Comfort** | Normal, Sepia, Night, Gray, High Contrast modes |
| **Navigation** | Page, chapter, bookmark, and search-based navigation |
| **Annotations** | Bookmarks, highlights, and notes support |
| **Responsive UI** | Toolbar, TOC panel, thumbnail panel, status bar |

### Supported Formats

| Format | Extension | Engine | Status |
|--------|-----------|--------|--------|
| EPUB 2.0/3.0 | `.epub` | EPUBEngine | ✅ Full Support |
| FictionBook 2 | `.fb2` | FB2Engine | ✅ Full Support |
| Mobipocket | `.mobi` | MOBIEngine | 🔄 Planned |
| Amazon Kindle | `.azw`, `.azw3` | AZW3Engine | 🔄 Planned |
| PDF | `.pdf` | PDFEngine | ✅ Via PDFViewer |
| Comic Book ZIP | `.cbz` | CBZEngine | 🔄 Planned |
| Comic Book RAR | `.cbr` | CBREngine | 🔄 Planned |
| DjVu | `.djvu` | DJVUEngine | 🔄 Planned |
| Plain Text | `.txt` | TXTEngine | 🔄 Planned |
| Rich Text | `.rtf` | RTFEngine | 🔄 Planned |
| HTML | `.html`, `.htm` | HTMLEngine | ✅ Via HTMLConverter |

---

## 2. Architecture

### System Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          USER APPLICATION                                │
└─────────────────────────────────────┬───────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                      UltraCanvasEBookViewer                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │
│  │   Toolbar   │  │  TOC Panel  │  │  Thumbnail  │  │  Bookmark   │    │
│  │             │  │             │  │   Panel     │  │   Panel     │    │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘    │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                     Page Display Area                            │    │
│  │  ┌─────────────────┐  ┌─────────────────┐                       │    │
│  │  │   Page View 1   │  │   Page View 2   │  (Double Page Mode)   │    │
│  │  └─────────────────┘  └─────────────────┘                       │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                        Status Bar                                │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────┬───────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         IEBookEngine Interface                           │
└───────────────┬─────────────────────┬─────────────────────┬─────────────┘
                │                     │                     │
                ▼                     ▼                     ▼
┌───────────────────┐   ┌───────────────────┐   ┌───────────────────┐
│    EPUBEngine     │   │    FB2Engine      │   │   Other Engines   │
│  ┌─────────────┐  │   │  ┌─────────────┐  │   │                   │
│  │ ZIP Handler │  │   │  │ XML Parser  │  │   │   MOBI, AZW3,     │
│  │ OPF Parser  │  │   │  │ FB2 Tags    │  │   │   CBZ, DJVU...    │
│  │ NCX/Nav     │  │   │  │ Base64 Img  │  │   │                   │
│  │ XHTML       │  │   │  │ Sections    │  │   │                   │
│  └─────────────┘  │   │  └─────────────┘  │   │                   │
└─────────┬─────────┘   └─────────┬─────────┘   └───────────────────┘
          │                       │
          └───────────┬───────────┘
                      ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                       HTMLConverter System                               │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐         │
│  │  HTMLParser     │  │ HTMLStyleResolver│  │ HTMLLayoutEngine│         │
│  │  (Tag Parsing)  │  │  (CSS Styling)  │  │  (Block/Flex)   │         │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘         │
└─────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    UltraCanvas UI Elements                               │
│  Label, ImageElement, Container, Button, ListView, ProgressBar, etc.    │
└─────────────────────────────────────────────────────────────────────────┘
```

### Component Relationships

```
UltraCanvasEBookViewer
├── UltraCanvasEBookThumbnail      (Page thumbnails)
├── UltraCanvasEBookTOCPanel       (Table of Contents)
├── UltraCanvasEBookBookmarkPanel  (Bookmark management)
├── IEBookEngine                   (Format engine interface)
│   ├── EPUBEngine                 (EPUB format)
│   └── FB2Engine                  (FB2 format)
├── HTMLConverter                  (HTML → UI Elements)
│   ├── HTMLParser                 (Parse HTML/XHTML)
│   ├── HTMLStyleResolver          (Apply CSS styles)
│   └── HTMLLayoutEngine           (Layout calculation)
└── EBookTypes                     (Enums, structs, callbacks)
```

---

## 3. EBook Support Functionality

### 3.1 Document Operations

| Operation | Description |
|-----------|-------------|
| **Load Document** | Load eBook from file path with auto-format detection |
| **Load from Memory** | Load eBook from byte array with explicit format |
| **Close Document** | Close current document and release resources |
| **Validate File** | Check if file is valid eBook format |

### 3.2 Navigation Features

| Feature | Description |
|---------|-------------|
| **Page Navigation** | Go to specific page, first, last, next, previous |
| **Chapter Navigation** | Jump to chapters via Table of Contents |
| **Progress Navigation** | Navigate by percentage (0.0 to 1.0) |
| **Bookmark Navigation** | Jump to saved bookmarks |
| **Search Navigation** | Find text and navigate between results |

### 3.3 Zoom Modes

| Mode | Enum Value | Description |
|------|------------|-------------|
| Fit Page | `EBookZoomMode::FitPage` | Entire page visible in viewport |
| Fit Width | `EBookZoomMode::FitWidth` | Page width matches viewport width |
| Fit Height | `EBookZoomMode::FitHeight` | Page height matches viewport height |
| Actual Size | `EBookZoomMode::ActualSize` | 100% zoom (1:1 pixel ratio) |
| Custom | `EBookZoomMode::Custom` | User-defined zoom level (10%-1000%) |

**Preset Zoom Levels:** 25%, 50%, 75%, 100%, 125%, 150%, 200%, 300%, 400%, 500%

### 3.4 Display Modes

| Mode | Enum Value | Description |
|------|------------|-------------|
| Single Page | `EBookDisplayMode::SinglePage` | One page at a time |
| Double Page | `EBookDisplayMode::DoublePage` | Two-page spread (book view) |
| Double Page + Cover | `EBookDisplayMode::DoublePageCover` | First page alone, then spreads |
| Right-to-Left | `EBookDisplayMode::RightToLeft` | Manga/RTL reading order |
| Continuous Vertical | `EBookDisplayMode::ContinuousVertical` | Scrolling vertical layout |
| Continuous Horizontal | `EBookDisplayMode::ContinuousHorizontal` | Scrolling horizontal layout |
| Thumbnail Grid | `EBookDisplayMode::ThumbnailGrid` | Grid of page thumbnails |
| Text Reflow | `EBookDisplayMode::TextReflow` | Reflowable text mode |

### 3.5 Reading Modes

| Mode | Enum Value | Background | Text Color |
|------|------------|------------|------------|
| Normal | `EBookReadingMode::Normal` | White (#FFFFFF) | Black (#000000) |
| Sepia | `EBookReadingMode::Sepia` | Warm (#FAF0DC) | Brown (#5A4632) |
| Night | `EBookReadingMode::Night` | Dark (#1E1E1E) | Light Gray (#C8C8C8) |
| Gray | `EBookReadingMode::Gray` | Light Gray (#E6E6E6) | Dark Gray (#323232) |
| High Contrast | `EBookReadingMode::HighContrast` | Black (#000000) | White (#FFFFFF) |
| Custom | `EBookReadingMode::Custom` | User-defined | User-defined |

### 3.6 Page Formats

#### ISO A Series
| Format | Size (mm) | Enum Value |
|--------|-----------|------------|
| A0 | 841 × 1189 | `DocumentPageFormat::A0` |
| A1 | 594 × 841 | `DocumentPageFormat::A1` |
| A2 | 420 × 594 | `DocumentPageFormat::A2` |
| A3 | 297 × 420 | `DocumentPageFormat::A3` |
| A4 | 210 × 297 | `DocumentPageFormat::A4` |
| A5 | 148 × 210 | `DocumentPageFormat::A5` |
| A6 | 105 × 148 | `DocumentPageFormat::A6` |
| A7 | 74 × 105 | `DocumentPageFormat::A7` |
| A8 | 52 × 74 | `DocumentPageFormat::A8` |

#### US Sizes
| Format | Size (inches) | Enum Value |
|--------|---------------|------------|
| Letter | 8.5 × 11 | `DocumentPageFormat::Letter` |
| Legal | 8.5 × 14 | `DocumentPageFormat::Legal` |
| Tabloid | 11 × 17 | `DocumentPageFormat::Tabloid` |

#### eReader Formats
| Format | Size (inches) | Enum Value |
|--------|---------------|------------|
| Kindle | 3.6 × 4.8 | `DocumentPageFormat::Kindle` |
| Kindle Paperwhite | 3.5 × 4.7 | `DocumentPageFormat::KindlePaperwhite` |
| Kobo | 4.3 × 5.7 | `DocumentPageFormat::Kobo` |
| iPad | 5.8 × 7.8 | `DocumentPageFormat::iPad` |
| iPad Mini | 4.7 × 6.3 | `DocumentPageFormat::iPadMini` |

### 3.7 Bookmark & Annotation Features

| Feature | Description |
|---------|-------------|
| **Add Bookmark** | Save current page with optional title/note |
| **Remove Bookmark** | Delete bookmark by page number |
| **Check Bookmark** | Query if page is bookmarked |
| **List Bookmarks** | Get all bookmarks sorted by page |
| **Highlight Text** | Mark text with color (planned) |
| **Add Note** | Attach note to text selection (planned) |

### 3.8 Search Features

| Feature | Description |
|---------|-------------|
| **Full-Text Search** | Search across entire document |
| **Find Next** | Navigate to next search result |
| **Find Previous** | Navigate to previous search result |
| **Clear Search** | Reset search state and highlights |
| **Result Count** | Get number of matches |

---

## 4. UI Handling Features

### 4.1 Toolbar Components

```
┌─────────────────────────────────────────────────────────────────────────┐
│ ⏮ ◀ ▶ ⏭ │ [  1  ] / 100 │ − [100%] + ⬜ ↔ 1:1 │ 📄 📖 📜 │ ☀ │ 📑 🖼 🔖 │
└─────────────────────────────────────────────────────────────────────────┘
  │  │  │  │    │      │      │   │    │  │  │   │  │  │   │   │  │  │
  │  │  │  │    │      │      │   │    │  │  │   │  │  │   │   │  │  └─ Bookmarks
  │  │  │  │    │      │      │   │    │  │  │   │  │  │   │   │  └─ Thumbnails
  │  │  │  │    │      │      │   │    │  │  │   │  │  │   │   └─ TOC
  │  │  │  │    │      │      │   │    │  │  │   │  │  │   └─ Reading Mode
  │  │  │  │    │      │      │   │    │  │  │   │  │  └─ Continuous
  │  │  │  │    │      │      │   │    │  │  │   │  └─ Double Page
  │  │  │  │    │      │      │   │    │  │  │   └─ Single Page
  │  │  │  │    │      │      │   │    │  │  └─ Actual Size
  │  │  │  │    │      │      │   │    │  └─ Fit Width
  │  │  │  │    │      │      │   │    └─ Fit Page
  │  │  │  │    │      │      │   └─ Zoom In
  │  │  │  │    │      │      └─ Zoom Label
  │  │  │  │    │      └─ Zoom Out
  │  │  │  │    └─ Page Count
  │  │  │  └─ Page Input
  │  │  └─ Last Page
  │  └─ Next Page
  └─ Previous Page
  First Page
```

### 4.2 Side Panels

#### Table of Contents Panel
- Hierarchical chapter display
- Current chapter highlighting
- Click to navigate to chapter
- Expandable/collapsible entries
- Search within TOC (planned)

#### Thumbnail Panel
- Grid of page previews
- Current page highlighting
- Click to navigate to page
- Async thumbnail generation
- Configurable thumbnail size

#### Bookmark Panel
- List of saved bookmarks
- Display page number and title
- Click to navigate to bookmark
- Delete bookmark option
- Sort by page or date

### 4.3 Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `←` / `Page Up` | Previous page |
| `→` / `Page Down` | Next page |
| `Home` | First page |
| `End` | Last page |
| `Ctrl` + `+` | Zoom in |
| `Ctrl` + `-` | Zoom out |
| `Ctrl` + `0` | Fit page |
| `Ctrl` + `1` | Fit width |
| `Ctrl` + `2` | Actual size (100%) |
| `Ctrl` + `F` | Open search |
| `Ctrl` + `G` | Find next |
| `Ctrl` + `Shift` + `G` | Find previous |
| `Ctrl` + `B` | Add bookmark |
| `F11` | Toggle fullscreen |
| `Escape` | Clear search / Close panel |

### 4.4 Mouse Interactions

| Action | Result |
|--------|--------|
| Scroll wheel | Navigate pages |
| `Ctrl` + Scroll | Zoom in/out |
| Double-click | Toggle fit/actual size |
| Click thumbnail | Go to page |
| Click TOC entry | Go to chapter |
| Click bookmark | Go to bookmark |
| Drag scrollbar | Scroll content |

### 4.5 Touch Gestures (Planned)

| Gesture | Action |
|---------|--------|
| Swipe left/right | Next/previous page |
| Pinch | Zoom in/out |
| Double-tap | Toggle zoom |
| Long press | Context menu |

### 4.6 Event Callbacks

| Callback | Signature | Description |
|----------|-----------|-------------|
| `onPageChanged` | `void(int current, int total)` | Page navigation occurred |
| `onChapterChanged` | `void(int index, string title)` | Chapter changed |
| `onZoomChanged` | `void(float zoom, EBookZoomMode mode)` | Zoom level changed |
| `onProgressChanged` | `void(float progress)` | Reading progress changed |
| `onLoadProgress` | `void(float progress, string status)` | Document loading progress |
| `onBookmarkAdded` | `void(EBookBookmark bookmark)` | Bookmark was added |
| `onBookmarkRemoved` | `void(int pageNumber)` | Bookmark was removed |
| `onSearchCompleted` | `void(string query, int count)` | Search finished |
| `onSearchResultSelected` | `void(int index, EBookSearchResult)` | Search result selected |
| `onSelectionChanged` | `void(string selectedText)` | Text selection changed |
| `onError` | `void(string error)` | Error occurred |
| `onViewerEvent` | `void(EBookViewerEvent event)` | General viewer event |

---

## 5. Complete Function Reference

### 5.1 UltraCanvasEBookViewer Class

#### Constructor & Destructor

```cpp
UltraCanvasEBookViewer(const std::string& id, long uid, long x, long y, long w, long h);
virtual ~UltraCanvasEBookViewer();
```

#### Document Operations

| Function | Return Type | Description |
|----------|-------------|-------------|
| `LoadDocument(filePath, password)` | `bool` | Load eBook from file |
| `LoadDocumentFromMemory(data, format, password)` | `bool` | Load from memory |
| `CloseDocument()` | `void` | Close current document |
| `IsDocumentLoaded()` | `bool` | Check if document loaded |
| `GetMetadata()` | `const EBookMetadata&` | Get document metadata |
| `GetTableOfContents()` | `const vector<EBookTOCEntry>&` | Get TOC |

#### Navigation Functions

| Function | Return Type | Description |
|----------|-------------|-------------|
| `GoToPage(pageNumber)` | `void` | Navigate to page |
| `GoToFirstPage()` | `void` | Go to first page |
| `GoToLastPage()` | `void` | Go to last page |
| `GoToNextPage()` | `void` | Go to next page |
| `GoToPreviousPage()` | `void` | Go to previous page |
| `GoToChapter(chapterIndex)` | `void` | Navigate to chapter |
| `GoToProgress(progress)` | `void` | Navigate by progress (0.0-1.0) |
| `GetCurrentPage()` | `int` | Get current page number |
| `GetTotalPages()` | `int` | Get total page count |
| `GetProgress()` | `float` | Get reading progress |

#### Zoom Functions

| Function | Return Type | Description |
|----------|-------------|-------------|
| `SetZoom(zoom)` | `void` | Set custom zoom level |
| `SetZoomMode(mode)` | `void` | Set zoom mode |
| `GetZoom()` | `float` | Get current zoom level |
| `GetZoomMode()` | `EBookZoomMode` | Get current zoom mode |
| `ZoomIn()` | `void` | Zoom in one step |
| `ZoomOut()` | `void` | Zoom out one step |
| `ZoomToFit()` | `void` | Fit entire page |
| `ZoomToFitWidth()` | `void` | Fit page width |
| `ZoomToFitHeight()` | `void` | Fit page height |
| `ZoomToActualSize()` | `void` | Set 100% zoom |

#### Display Functions

| Function | Return Type | Description |
|----------|-------------|-------------|
| `SetDisplayMode(mode)` | `void` | Set display mode |
| `GetDisplayMode()` | `EBookDisplayMode` | Get current display mode |
| `SetReadingMode(mode)` | `void` | Set reading mode |
| `GetReadingMode()` | `EBookReadingMode` | Get current reading mode |

#### Panel Functions

| Function | Return Type | Description |
|----------|-------------|-------------|
| `ShowToolbar(show)` | `void` | Show/hide toolbar |
| `ShowTOC(show)` | `void` | Show/hide TOC panel |
| `ShowThumbnails(show)` | `void` | Show/hide thumbnails |
| `ShowBookmarks(show)` | `void` | Show/hide bookmarks |
| `ToggleTOCPanel()` | `void` | Toggle TOC panel |
| `ToggleThumbnailPanel()` | `void` | Toggle thumbnails |
| `ToggleBookmarkPanel()` | `void` | Toggle bookmarks |

#### Bookmark Functions

| Function | Return Type | Description |
|----------|-------------|-------------|
| `AddBookmark(title)` | `void` | Add bookmark to current page |
| `RemoveBookmark(pageNumber)` | `void` | Remove bookmark |
| `IsBookmarked(pageNumber)` | `bool` | Check if page bookmarked |
| `GetBookmarks()` | `const vector<EBookBookmark>&` | Get all bookmarks |

#### Search Functions

| Function | Return Type | Description |
|----------|-------------|-------------|
| `Search(query)` | `void` | Search document |
| `FindNext()` | `void` | Go to next result |
| `FindPrevious()` | `void` | Go to previous result |
| `ClearSearch()` | `void` | Clear search results |

#### Settings Functions

| Function | Return Type | Description |
|----------|-------------|-------------|
| `SetPageFormat(format)` | `void` | Set page format |
| `SetCustomPageSize(widthMM, heightMM)` | `void` | Set custom page size |
| `SetRenderSettings(settings)` | `void` | Set render settings |
| `GetRenderSettings()` | `const EBookRenderSettings&` | Get render settings |

#### Core Virtual Functions

| Function | Return Type | Description |
|----------|-------------|-------------|
| `Render(ctx)` | `void` | Render component |
| `OnEvent(event)` | `bool` | Handle event |

### 5.2 IEBookEngine Interface

| Function | Return Type | Description |
|----------|-------------|-------------|
| `LoadFile(filePath, password)` | `bool` | Load from file |
| `LoadFromMemory(data, password)` | `bool` | Load from memory |
| `Close()` | `void` | Close document |
| `IsLoaded()` | `bool` | Check if loaded |
| `GetFormat()` | `EBookFormat` | Get format type |
| `GetMetadata()` | `EBookMetadata` | Get metadata |
| `GetPageCount()` | `int` | Get page count |
| `GetPageInfo(pageNumber)` | `EBookPageInfo` | Get page info |
| `GetTableOfContents()` | `vector<EBookTOCEntry>` | Get TOC |
| `GetPageContent(pageNumber)` | `string` | Get page HTML |
| `RenderPageToImage(page, w, h, dpi)` | `vector<uint8_t>` | Render page |
| `Search(query)` | `vector<EBookSearchResult>` | Search text |
| `GetCoverImage()` | `vector<uint8_t>` | Get cover image |
| `GetLastError()` | `string` | Get last error |
| `SetProgressCallback(callback)` | `void` | Set progress callback |

### 5.3 Factory Functions

```cpp
// Create eBook viewer with auto-generated UID
std::shared_ptr<UltraCanvasEBookViewer> CreateEBookViewer(
    const std::string& id, long x, long y, long w, long h);

// Create eBook viewer with specific UID
std::shared_ptr<UltraCanvasEBookViewer> CreateEBookViewerWithID(
    const std::string& id, long uid, long x, long y, long w, long h);
```

### 5.4 DocumentPageSize Static Methods

```cpp
// Create from predefined format
static DocumentPageSize FromFormat(DocumentPageFormat format, float dpi = 96.0f);

// Create from millimeters
static DocumentPageSize FromMillimeters(float widthMM, float heightMM, float dpi = 96.0f);

// Create from inches
static DocumentPageSize FromInches(float widthIn, float heightIn, float dpi = 96.0f);

// Create from pixels
static DocumentPageSize FromPixels(int widthPx, int heightPx, float dpi = 96.0f);

// Get dimensions
float GetWidthInPixels() const;
float GetHeightInPixels() const;
float GetWidthInMillimeters() const;
float GetHeightInMillimeters() const;
float GetWidthInInches() const;
float GetHeightInInches() const;

// Orientation
DocumentPageSize ToLandscape() const;
DocumentPageSize ToPortrait() const;
```

---

## 6. Example Code

### 6.1 Basic eBook Viewer Setup

```cpp
#include "UltraCanvasEBookViewer.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasApplication.h"

using namespace UltraCanvas;

int main() {
    // Initialize application
    auto app = UltraCanvasApplication::GetInstance();
    app->Initialize();
    
    // Create main window
    auto window = CreateWindow("eBook Reader", 1280, 800);
    
    // Create eBook viewer
    auto ebookViewer = CreateEBookViewer("mainViewer", 0, 0, 1280, 800);
    
    // Add to window
    window->AddChild(ebookViewer);
    
    // Load a document
    if (ebookViewer->LoadDocument("/path/to/book.epub")) {
        std::cout << "Document loaded successfully!" << std::endl;
        std::cout << "Title: " << ebookViewer->GetMetadata().title << std::endl;
        std::cout << "Pages: " << ebookViewer->GetTotalPages() << std::endl;
    }
    
    // Run application
    app->Run();
    
    return 0;
}
```

### 6.2 Setting Up Event Callbacks

```cpp
#include "UltraCanvasEBookViewer.h"

void SetupEBookViewer(std::shared_ptr<UltraCanvasEBookViewer> viewer) {
    
    // Page change callback
    viewer->onPageChanged = [](int currentPage, int totalPages) {
        std::cout << "Page " << currentPage << " of " << totalPages << std::endl;
        
        // Update external progress bar or UI
        float progress = static_cast<float>(currentPage) / totalPages;
        UpdateReadingProgress(progress);
    };
    
    // Chapter change callback
    viewer->onChapterChanged = [](int chapterIndex, const std::string& title) {
        std::cout << "Chapter " << chapterIndex << ": " << title << std::endl;
        
        // Update chapter display in app
        UpdateChapterTitle(title);
    };
    
    // Zoom change callback
    viewer->onZoomChanged = [](float zoom, EBookZoomMode mode) {
        int percent = static_cast<int>(zoom * 100);
        std::cout << "Zoom: " << percent << "%" << std::endl;
    };
    
    // Loading progress callback
    viewer->onLoadProgress = [](float progress, const std::string& status) {
        std::cout << "Loading: " << (progress * 100) << "% - " << status << std::endl;
    };
    
    // Bookmark callback
    viewer->onBookmarkAdded = [](const EBookBookmark& bookmark) {
        std::cout << "Bookmark added: Page " << bookmark.pageNumber << std::endl;
        SaveBookmarkToDatabase(bookmark);
    };
    
    // Search callback
    viewer->onSearchCompleted = [](const std::string& query, int resultCount) {
        std::cout << "Found " << resultCount << " results for '" << query << "'" << std::endl;
    };
    
    // Error callback
    viewer->onError = [](const std::string& error) {
        std::cerr << "Error: " << error << std::endl;
        ShowErrorDialog(error);
    };
    
    // General event callback
    viewer->onViewerEvent = [](const EBookViewerEvent& event) {
        switch (event.type) {
            case EBookViewerEvent::DocumentLoaded:
                std::cout << "Document loaded!" << std::endl;
                break;
            case EBookViewerEvent::DocumentClosed:
                std::cout << "Document closed." << std::endl;
                break;
            case EBookViewerEvent::Error:
                std::cerr << "Viewer error: " << event.message << std::endl;
                break;
            default:
                break;
        }
    };
}
```

### 6.3 Customizing Display Settings

```cpp
#include "UltraCanvasEBookViewer.h"

void ConfigureViewerDisplay(std::shared_ptr<UltraCanvasEBookViewer> viewer) {
    
    // Set display mode to double page (book spread)
    viewer->SetDisplayMode(EBookDisplayMode::DoublePage);
    
    // Set sepia reading mode for comfortable reading
    viewer->SetReadingMode(EBookReadingMode::Sepia);
    
    // Set page format to A5 (common eBook size)
    viewer->SetPageFormat(DocumentPageFormat::A5);
    
    // Or set custom page size (width x height in mm)
    viewer->SetCustomPageSize(120.0f, 180.0f);
    
    // Set zoom to fit width
    viewer->ZoomToFitWidth();
    
    // Or set specific zoom level
    viewer->SetZoom(1.5f);  // 150%
    
    // Configure render settings
    EBookRenderSettings settings;
    settings.dpi = 150.0f;
    settings.backgroundColor = Color(250, 245, 230, 255);  // Custom sepia
    settings.textColor = Color(60, 50, 40, 255);
    settings.fontFamily = "Georgia";
    settings.fontSize = 16.0f;
    settings.lineSpacing = 1.5f;
    settings.marginLeft = 20;
    settings.marginRight = 20;
    settings.marginTop = 15;
    settings.marginBottom = 15;
    viewer->SetRenderSettings(settings);
    
    // Show/hide UI elements
    viewer->ShowToolbar(true);
    viewer->ShowTOC(false);
    viewer->ShowThumbnails(false);
    viewer->ShowBookmarks(false);
}
```

### 6.4 Navigation and Bookmarks

```cpp
#include "UltraCanvasEBookViewer.h"

void NavigationExample(std::shared_ptr<UltraCanvasEBookViewer> viewer) {
    
    // Basic page navigation
    viewer->GoToFirstPage();
    viewer->GoToNextPage();
    viewer->GoToPreviousPage();
    viewer->GoToLastPage();
    viewer->GoToPage(42);
    
    // Chapter navigation
    const auto& toc = viewer->GetTableOfContents();
    if (!toc.empty()) {
        // Go to first chapter
        viewer->GoToChapter(0);
        
        // Print all chapters
        for (size_t i = 0; i < toc.size(); ++i) {
            std::cout << i << ": " << toc[i].title 
                      << " (page " << toc[i].pageNumber << ")" << std::endl;
        }
    }
    
    // Progress-based navigation
    viewer->GoToProgress(0.5f);  // Go to 50%
    
    // Get current position
    int currentPage = viewer->GetCurrentPage();
    int totalPages = viewer->GetTotalPages();
    float progress = viewer->GetProgress();
    
    std::cout << "Position: Page " << currentPage << "/" << totalPages 
              << " (" << (progress * 100) << "%)" << std::endl;
    
    // Bookmark management
    viewer->AddBookmark("Important section");
    
    // Check if current page is bookmarked
    if (viewer->IsBookmarked(currentPage)) {
        std::cout << "This page is bookmarked!" << std::endl;
    }
    
    // List all bookmarks
    const auto& bookmarks = viewer->GetBookmarks();
    for (const auto& bm : bookmarks) {
        std::cout << "Bookmark: Page " << bm.pageNumber 
                  << " - " << bm.title << std::endl;
    }
    
    // Remove bookmark
    viewer->RemoveBookmark(currentPage);
}
```

### 6.5 Search Functionality

```cpp
#include "UltraCanvasEBookViewer.h"

void SearchExample(std::shared_ptr<UltraCanvasEBookViewer> viewer) {
    
    // Set up search callbacks
    viewer->onSearchCompleted = [](const std::string& query, int count) {
        if (count > 0) {
            std::cout << "Found " << count << " occurrences of '" << query << "'" << std::endl;
        } else {
            std::cout << "No results found for '" << query << "'" << std::endl;
        }
    };
    
    viewer->onSearchResultSelected = [](int index, const EBookSearchResult& result) {
        std::cout << "Result " << (index + 1) << ": Page " << result.pageNumber << std::endl;
        std::cout << "Context: " << result.contextBefore 
                  << "[" << result.matchedText << "]"
                  << result.contextAfter << std::endl;
    };
    
    // Perform search
    viewer->Search("adventure");
    
    // Navigate through results
    viewer->FindNext();      // Go to next result
    viewer->FindNext();      // Go to next result
    viewer->FindPrevious();  // Go back to previous result
    
    // Clear search
    viewer->ClearSearch();
}
```

### 6.6 Loading Different Formats

```cpp
#include "UltraCanvasEBookViewer.h"
#include <fstream>

void LoadDifferentFormats(std::shared_ptr<UltraCanvasEBookViewer> viewer) {
    
    // Load EPUB file
    if (viewer->LoadDocument("/books/novel.epub")) {
        std::cout << "EPUB loaded: " << viewer->GetMetadata().title << std::endl;
    }
    
    // Load FB2 file
    if (viewer->LoadDocument("/books/story.fb2")) {
        std::cout << "FB2 loaded: " << viewer->GetMetadata().title << std::endl;
    }
    
    // Load password-protected EPUB
    if (viewer->LoadDocument("/books/protected.epub", "secretPassword123")) {
        std::cout << "Protected EPUB loaded successfully!" << std::endl;
    }
    
    // Load from memory
    std::ifstream file("/books/book.epub", std::ios::binary);
    if (file) {
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        
        if (viewer->LoadDocumentFromMemory(data, EBookFormat::EPUB)) {
            std::cout << "Loaded from memory!" << std::endl;
        }
    }
    
    // Close document when done
    viewer->CloseDocument();
}
```

### 6.7 Complete Application Example

```cpp
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasEBookViewer.h"
#include "UltraCanvasMenuBar.h"
#include "UltraCanvasFileDialog.h"

using namespace UltraCanvas;

class EBookReaderApp {
private:
    std::shared_ptr<UltraCanvasWindow> mainWindow;
    std::shared_ptr<UltraCanvasEBookViewer> viewer;
    std::shared_ptr<UltraCanvasMenuBar> menuBar;
    
public:
    void Initialize() {
        // Create main window
        mainWindow = CreateWindow("UltraCanvas eBook Reader", 1400, 900);
        mainWindow->SetMinSize(800, 600);
        
        // Create menu bar
        CreateMenuBar();
        
        // Create eBook viewer
        viewer = CreateEBookViewer("ebookViewer", 0, 30, 1400, 870);
        
        // Configure viewer
        ConfigureViewer();
        
        // Add components to window
        mainWindow->AddChild(menuBar);
        mainWindow->AddChild(viewer);
        
        // Handle window resize
        mainWindow->onResize = [this](int width, int height) {
            if (viewer) {
                viewer->SetBounds(Rect2Di(0, 30, width, height - 30));
            }
        };
    }
    
    void CreateMenuBar() {
        menuBar = std::make_shared<UltraCanvasMenuBar>("menuBar", 1, 0, 0, 1400, 30);
        
        // File menu
        auto fileMenu = menuBar->AddMenu("File");
        fileMenu->AddItem("Open...", "Ctrl+O", [this]() { OpenFile(); });
        fileMenu->AddItem("Close", "Ctrl+W", [this]() { CloseFile(); });
        fileMenu->AddSeparator();
        fileMenu->AddItem("Exit", "Alt+F4", []() { 
            UltraCanvasApplication::GetInstance()->Quit(); 
        });
        
        // View menu
        auto viewMenu = menuBar->AddMenu("View");
        viewMenu->AddItem("Single Page", "", [this]() { 
            viewer->SetDisplayMode(EBookDisplayMode::SinglePage); 
        });
        viewMenu->AddItem("Double Page", "", [this]() { 
            viewer->SetDisplayMode(EBookDisplayMode::DoublePage); 
        });
        viewMenu->AddItem("Continuous", "", [this]() { 
            viewer->SetDisplayMode(EBookDisplayMode::ContinuousVertical); 
        });
        viewMenu->AddSeparator();
        viewMenu->AddItem("Table of Contents", "F5", [this]() { 
            viewer->ToggleTOCPanel(); 
        });
        viewMenu->AddItem("Thumbnails", "F6", [this]() { 
            viewer->ToggleThumbnailPanel(); 
        });
        viewMenu->AddItem("Bookmarks", "F7", [this]() { 
            viewer->ToggleBookmarkPanel(); 
        });
        
        // Reading menu
        auto readingMenu = menuBar->AddMenu("Reading");
        readingMenu->AddItem("Normal Mode", "", [this]() { 
            viewer->SetReadingMode(EBookReadingMode::Normal); 
        });
        readingMenu->AddItem("Sepia Mode", "", [this]() { 
            viewer->SetReadingMode(EBookReadingMode::Sepia); 
        });
        readingMenu->AddItem("Night Mode", "", [this]() { 
            viewer->SetReadingMode(EBookReadingMode::Night); 
        });
        
        // Help menu
        auto helpMenu = menuBar->AddMenu("Help");
        helpMenu->AddItem("About", "", []() {
            ShowMessageBox("About", "UltraCanvas eBook Reader v1.0\n© 2025 UltraCanvas Framework");
        });
    }
    
    void ConfigureViewer() {
        // Default settings
        viewer->SetDisplayMode(EBookDisplayMode::SinglePage);
        viewer->SetReadingMode(EBookReadingMode::Normal);
        viewer->SetPageFormat(DocumentPageFormat::A5);
        
        // Callbacks
        viewer->onPageChanged = [this](int current, int total) {
            mainWindow->SetTitle("UltraCanvas eBook Reader - Page " + 
                                 std::to_string(current) + "/" + std::to_string(total));
        };
        
        viewer->onError = [](const std::string& error) {
            ShowMessageBox("Error", error);
        };
    }
    
    void OpenFile() {
        UltraCanvasFileDialog dialog;
        dialog.SetTitle("Open eBook");
        dialog.AddFilter("eBook Files", "*.epub;*.fb2;*.mobi");
        dialog.AddFilter("EPUB Files", "*.epub");
        dialog.AddFilter("FB2 Files", "*.fb2");
        dialog.AddFilter("All Files", "*.*");
        
        if (dialog.ShowOpen()) {
            std::string filePath = dialog.GetSelectedFile();
            
            if (viewer->LoadDocument(filePath)) {
                const auto& meta = viewer->GetMetadata();
                mainWindow->SetTitle("UltraCanvas eBook Reader - " + meta.title);
            }
        }
    }
    
    void CloseFile() {
        viewer->CloseDocument();
        mainWindow->SetTitle("UltraCanvas eBook Reader");
    }
    
    void Run() {
        UltraCanvasApplication::GetInstance()->Run();
    }
};

int main() {
    auto app = UltraCanvasApplication::GetInstance();
    app->Initialize();
    
    EBookReaderApp reader;
    reader.Initialize();
    reader.Run();
    
    return 0;
}
```

---

## 7. File Structure

### Source Files

```
UltraCanvas/
├── include/
│   ├── UltraCanvasEBookViewer.h      (21 KB)  - Main viewer header
│   ├── UltraCanvasEBookTypes.h       (28 KB)  - Types, enums, structs
│   └── UltraCanvasHTMLConverter.h    (24 KB)  - HTML conversion
│
├── core/
│   ├── UltraCanvasEBookViewer.cpp    (~1,580 lines) - Viewer implementation
│   ├── UltraCanvasEBookTypes.cpp     (~331 lines)   - Type implementations
│   ├── UltraCanvasHTMLConverter.cpp  (~2,183 lines) - HTML conversion
│   └── UltraCanvasHTMLLayout.cpp     (~1,009 lines) - Layout engine
│
└── Plugins/Documents/eBook/
    ├── IEBookEngine.h                (~471 lines)   - Engine interface
    ├── EPUBEngine.h                  (~565 lines)   - EPUB header
    ├── EPUBEngine.cpp                (~1,812 lines) - EPUB implementation
    ├── FB2Engine.h                   (~543 lines)   - FB2 header
    └── FB2Engine.cpp                 (~1,641 lines) - FB2 implementation
```

### Total Lines of Code

| Component | Lines |
|-----------|-------|
| Headers | ~3,000 |
| Core Implementation | ~5,100 |
| Engine Plugins | ~4,500 |
| **Total** | **~12,600** |

---

## Summary

The UltraCanvas EBook Support system provides a complete, professional-grade solution for reading electronic books. Key highlights:

- **Multi-format support** with extensible engine architecture
- **Rich UI** with toolbar, panels, and comprehensive controls
- **Flexible display** with multiple page layouts and reading modes
- **Full navigation** via pages, chapters, bookmarks, and search
- **Cross-platform** compatible with all UltraCanvas-supported platforms
- **High-quality rendering** through HTML conversion and native UI elements

The system integrates seamlessly with the UltraCanvas framework, following all coding guidelines and architectural patterns.

---

*Document Version: 1.0.0 | Last Updated: 2025-01-10 | Author: UltraCanvas Framework*
