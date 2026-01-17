// include/UltraCanvasEBookViewer.h
// Comprehensive eBook viewer component with full navigation, zoom, and display modes
// Version: 1.0.0
// Last Modified: 2025-01-08
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasEBookTypes.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasScrollbar.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasProgressBar.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasContainer.h"

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <future>
#include <unordered_map>

// Forward declaration for engine
namespace UltraCanvas {
    class IEBookEngine;
}

namespace UltraCanvas {

// ===== EBOOK THUMBNAIL COMPONENT =====
class UltraCanvasEBookThumbnail : public UltraCanvasUIElement {
public:
    UltraCanvasEBookThumbnail(const std::string& id, long uid, 
                              long x, long y, long w, long h, int page)
        : UltraCanvasUIElement(id, uid, x, y, w, h), pageNumber(page) {}
    
    void SetThumbnailData(const std::vector<uint8_t>& data) {
        thumbnailData = data;
        isLoading = false;
        RequestRedraw();
    }
    
    void SetLoading(bool loading) { 
        isLoading = loading; 
        RequestRedraw();
    }
    
    void SetSelected(bool selected) { 
        isSelected = selected; 
        RequestRedraw();
    }
    
    int GetPageNumber() const { return pageNumber; }
    
    // Callback when thumbnail is clicked
    std::function<void(int)> onPageSelected;
    
protected:
    void Render(IRenderContext* ctx) override;
    bool OnEvent(const UCEvent& event) override;
    
private:
    int pageNumber;
    std::vector<uint8_t> thumbnailData;
    bool isLoading = true;
    bool isSelected = false;
};

// ===== EBOOK TOC PANEL COMPONENT =====
class UltraCanvasEBookTOCPanel : public UltraCanvasContainer {
public:
    UltraCanvasEBookTOCPanel(const std::string& id, long uid, 
                             long x, long y, long w, long h);
    
    void SetTableOfContents(const std::vector<EBookTOCEntry>& toc);
    void SetCurrentChapter(int chapterIndex);
    void Clear();
    
    // Callback when TOC entry is clicked
    std::function<void(int pageNumber, int chapterIndex)> onChapterSelected;
    
protected:
    void Render(IRenderContext* ctx) override;
    
private:
    std::vector<EBookTOCEntry> tocEntries;
    int currentChapter = -1;
    std::shared_ptr<UltraCanvasListView> listView;
    
    void BuildTOCList();
    void AddTOCEntry(const EBookTOCEntry& entry, int indent);
};

// ===== EBOOK BOOKMARK PANEL COMPONENT =====
class UltraCanvasEBookBookmarkPanel : public UltraCanvasContainer {
public:
    UltraCanvasEBookBookmarkPanel(const std::string& id, long uid,
                                   long x, long y, long w, long h);
    
    void SetBookmarks(const std::vector<EBookBookmark>& bookmarks);
    void AddBookmark(const EBookBookmark& bookmark);
    void RemoveBookmark(int pageNumber);
    void Clear();
    
    // Callbacks
    std::function<void(int pageNumber)> onBookmarkSelected;
    std::function<void(int pageNumber)> onBookmarkRemove;
    
protected:
    void Render(IRenderContext* ctx) override;
    
private:
    std::vector<EBookBookmark> bookmarks;
    std::shared_ptr<UltraCanvasListView> listView;
    
    void BuildBookmarkList();
};

// ===== MAIN EBOOK VIEWER COMPONENT =====
class UltraCanvasEBookViewer : public UltraCanvasUIElement {
public:
    // ===== CONSTRUCTION =====
    UltraCanvasEBookViewer(const std::string& id, long uid, 
                           long x, long y, long w, long h);
    virtual ~UltraCanvasEBookViewer();
    
    // ===== DOCUMENT OPERATIONS =====
    
    // Load eBook from file (auto-detects format)
    bool LoadDocument(const std::string& filePath, const std::string& password = "");
    
    // Load eBook from memory with format hint
    bool LoadDocumentFromMemory(const std::vector<uint8_t>& data, 
                                EBookFormat format,
                                const std::string& password = "");
    
    // Close current document
    void CloseDocument();
    
    // Check if document is loaded
    bool IsDocumentLoaded() const { return state.isDocumentLoaded; }
    
    // Check if loading in progress
    bool IsLoading() const { return state.isLoading; }
    
    // Get current document path
    const std::string& GetCurrentDocument() const { return state.currentDocumentPath; }
    
    // Get document metadata
    const EBookMetadata& GetMetadata() const { return metadata; }
    
    // ===== PAGE NAVIGATION =====
    
    // Go to specific page (1-based)
    void GoToPage(int pageNumber);
    
    // Navigation shortcuts
    void GoToFirstPage();
    void GoToLastPage();
    void GoToPreviousPage();
    void GoToNextPage();
    
    // Chapter navigation
    void GoToChapter(int chapterIndex);
    void GoToChapter(const std::string& chapterTitle);
    void GoToPreviousChapter();
    void GoToNextChapter();
    
    // Bookmark navigation
    void GoToBookmark(int bookmarkIndex);
    
    // Progress-based navigation (0.0 to 1.0)
    void GoToPercentage(float percent);
    
    // Get current position
    int GetCurrentPage() const { return state.currentPage; }
    int GetPageCount() const { return state.totalPages; }
    int GetCurrentChapter() const { return state.currentChapter; }
    float GetReadingProgress() const;
    
    // ===== ZOOM CONTROL =====
    
    // Set zoom level and mode
    void SetZoom(float zoom, EBookZoomMode mode = EBookZoomMode::Custom);
    
    // Zoom shortcuts
    void ZoomIn();
    void ZoomOut();
    void ZoomToFit();
    void ZoomToFitWidth();
    void ZoomToFitHeight();
    void ZoomToFitText();
    void ZoomToActualSize();
    
    // Preset zoom levels
    void SetZoomPreset(EBookZoomMode preset);
    
    // Get current zoom
    float GetZoom() const { return state.currentZoom; }
    EBookZoomMode GetZoomMode() const { return state.zoomMode; }
    
    // ===== DISPLAY MODES =====
    
    // Set display mode
    void SetDisplayMode(EBookDisplayMode mode);
    EBookDisplayMode GetDisplayMode() const { return state.displayMode; }
    
    // Display mode shortcuts
    void SetSinglePageMode() { SetDisplayMode(EBookDisplayMode::SinglePage); }
    void SetDoublePageMode() { SetDisplayMode(EBookDisplayMode::DoublePage); }
    void SetDoublePageCoverMode() { SetDisplayMode(EBookDisplayMode::DoublePageCover); }
    void SetContinuousMode() { SetDisplayMode(EBookDisplayMode::ContinuousVertical); }
    void SetReflowMode() { SetDisplayMode(EBookDisplayMode::ReflowSingle); }
    
    // ===== PAGE FORMAT =====
    
    // Set page format (DIN A0-A8, Letter, Custom, etc.)
    void SetPageFormat(DocumentPageFormat format);
    void SetCustomPageSize(float widthMM, float heightMM);
    DocumentPageFormat GetPageFormat() const { return renderSettings.pageFormat; }
    DocumentPageSize GetPageSize() const;
    
    // ===== READING DIRECTION =====
    
    // Set reading direction (LTR/RTL for manga)
    void SetReadingDirection(EBookReadingDirection direction);
    EBookReadingDirection GetReadingDirection() const;
    void SetRightToLeft(bool rtl);
    bool IsRightToLeft() const { return renderSettings.spread.rightToLeft; }
    
    // ===== READING MODES (COLOR THEMES) =====
    
    // Set reading mode
    void SetReadingMode(EBookReadingMode mode);
    EBookReadingMode GetReadingMode() const { return state.readingMode; }
    
    // Reading mode shortcuts
    void SetNormalMode() { SetReadingMode(EBookReadingMode::Normal); }
    void SetSepiaMode() { SetReadingMode(EBookReadingMode::Sepia); }
    void SetNightMode() { SetReadingMode(EBookReadingMode::Night); }
    void SetHighContrastMode() { SetReadingMode(EBookReadingMode::HighContrast); }
    
    // Custom colors
    void SetCustomColors(Color background, Color text, Color link = Color(0, 0, 238, 255));
    
    // ===== RENDER SETTINGS =====
    
    // Get/Set render settings
    void SetRenderSettings(const EBookRenderSettings& settings);
    const EBookRenderSettings& GetRenderSettings() const { return renderSettings; }
    
    // Individual settings
    void SetDPI(float dpi);
    void SetMargins(const EBookMargins& margins);
    void SetReflowSettings(const EBookReflowSettings& settings);
    void SetSpreadSettings(const EBookSpreadSettings& settings);
    
    // ===== UI PANELS =====
    
    // Thumbnail panel
    void ShowThumbnailPanel(bool show = true);
    void HideThumbnailPanel() { ShowThumbnailPanel(false); }
    void ToggleThumbnailPanel();
    bool IsThumbnailPanelVisible() const { return state.isThumbnailPanelVisible; }
    
    // Table of Contents panel
    void ShowTableOfContents(bool show = true);
    void HideTableOfContents() { ShowTableOfContents(false); }
    void ToggleTableOfContents();
    bool IsTableOfContentsVisible() const { return state.isTOCVisible; }
    
    // Bookmark panel
    void ShowBookmarkPanel(bool show = true);
    void HideBookmarkPanel() { ShowBookmarkPanel(false); }
    void ToggleBookmarkPanel();
    
    // Get TOC
    std::vector<EBookTOCEntry> GetTableOfContents() const;
    
    // ===== BOOKMARKS =====
    
    // Add bookmark at current page
    void AddBookmark(const std::string& note = "");
    
    // Add bookmark at specific page
    void AddBookmarkAtPage(int pageNumber, const std::string& note = "");
    
    // Remove bookmark
    void RemoveBookmark(int pageNumber);
    
    // Check if page is bookmarked
    bool IsPageBookmarked(int pageNumber) const;
    
    // Get all bookmarks
    std::vector<EBookBookmark> GetBookmarks() const { return bookmarks; }
    
    // ===== ANNOTATIONS =====
    
    // Add highlight
    void AddHighlight(const EBookTextRange& range, 
                      Color color = Color(255, 255, 0, 128));
    
    // Add note
    void AddNote(const EBookTextRange& range, const std::string& note);
    
    // Remove annotation
    void RemoveAnnotation(int annotationId);
    
    // Get all annotations
    std::vector<EBookAnnotation> GetAnnotations() const { return annotations; }
    
    // Get annotations for page
    std::vector<EBookAnnotation> GetAnnotationsForPage(int pageNumber) const;
    
    // ===== SEARCH =====
    
    // Search text in document
    void SearchText(const std::string& query, bool caseSensitive = false);
    
    // Navigate search results
    void FindNext();
    void FindPrevious();
    void ClearSearch();
    
    // Get search results
    const std::vector<EBookSearchResult>& GetSearchResults() const { return searchResults; }
    int GetCurrentSearchResultIndex() const { return state.searchResultIndex; }
    
    // ===== TEXT SELECTION =====
    
    // Get selected text
    std::string GetSelectedText() const;
    
    // Clear selection
    void ClearSelection();
    
    // Check if has selection
    bool HasSelection() const { return state.hasSelection; }
    
    // ===== FULLSCREEN =====
    
    void SetFullScreen(bool fullscreen);
    void ToggleFullScreen();
    bool IsFullScreen() const { return state.isFullScreen; }
    
    // ===== CALLBACKS =====
    
    // Page navigation
    EBookPageChangedCallback onPageChanged;
    EBookChapterChangedCallback onChapterChanged;
    
    // Zoom and display
    EBookZoomChangedCallback onZoomChanged;
    std::function<void(EBookDisplayMode)> onDisplayModeChanged;
    std::function<void(EBookReadingMode)> onReadingModeChanged;
    
    // Progress
    EBookProgressCallback onProgressChanged;
    EBookLoadProgressCallback onLoadProgress;
    
    // Bookmarks and annotations
    std::function<void(const EBookBookmark&)> onBookmarkAdded;
    std::function<void(int pageNumber)> onBookmarkRemoved;
    std::function<void(const EBookAnnotation&)> onAnnotationAdded;
    
    // Search
    std::function<void(const std::string& query, int resultCount)> onSearchCompleted;
    std::function<void(int resultIndex, const EBookSearchResult&)> onSearchResultSelected;
    
    // Selection
    std::function<void(const std::string& selectedText)> onSelectionChanged;
    
    // Errors
    EBookErrorCallback onError;
    
    // General events
    EBookEventCallback onViewerEvent;
    
    // ===== RENDERING =====
    void Render(IRenderContext* ctx) override;
    bool OnEvent(const UCEvent& event) override;
    
protected:
    // ===== INTERNAL STATE =====
    EBookViewerState state;
    EBookMetadata metadata;
    EBookRenderSettings renderSettings;
    
    // Engine
    std::shared_ptr<IEBookEngine> engine;
    
    // Page data
    std::vector<EBookPageInfo> pageInfos;
    std::vector<EBookTOCEntry> tableOfContents;
    
    // User data
    std::vector<EBookBookmark> bookmarks;
    std::vector<EBookAnnotation> annotations;
    std::vector<EBookSearchResult> searchResults;
    
    // ===== UI COMPONENTS =====
    
    // Toolbar
    std::shared_ptr<UltraCanvasContainer> toolbar;
    std::shared_ptr<UltraCanvasButton> btnFirstPage;
    std::shared_ptr<UltraCanvasButton> btnPrevPage;
    std::shared_ptr<UltraCanvasButton> btnNextPage;
    std::shared_ptr<UltraCanvasButton> btnLastPage;
    std::shared_ptr<UltraCanvasTextInput> pageNumberInput;
    std::shared_ptr<UltraCanvasLabel> pageCountLabel;
    
    // Zoom buttons
    std::shared_ptr<UltraCanvasButton> btnZoomIn;
    std::shared_ptr<UltraCanvasButton> btnZoomOut;
    std::shared_ptr<UltraCanvasButton> btnZoomFit;
    std::shared_ptr<UltraCanvasButton> btnZoomFitWidth;
    std::shared_ptr<UltraCanvasButton> btnZoomActualSize;
    std::shared_ptr<UltraCanvasLabel> zoomLabel;
    
    // Display mode buttons
    std::shared_ptr<UltraCanvasButton> btnSinglePage;
    std::shared_ptr<UltraCanvasButton> btnDoublePage;
    std::shared_ptr<UltraCanvasButton> btnContinuous;
    std::shared_ptr<UltraCanvasButton> btnReflow;
    
    // Reading mode buttons
    std::shared_ptr<UltraCanvasButton> btnNormalMode;
    std::shared_ptr<UltraCanvasButton> btnSepiaMode;
    std::shared_ptr<UltraCanvasButton> btnNightMode;
    
    // Panel toggle buttons
    std::shared_ptr<UltraCanvasButton> btnToggleTOC;
    std::shared_ptr<UltraCanvasButton> btnToggleThumbnails;
    std::shared_ptr<UltraCanvasButton> btnToggleBookmarks;
    std::shared_ptr<UltraCanvasButton> btnAddBookmark;
    
    // Search
    std::shared_ptr<UltraCanvasTextInput> searchInput;
    std::shared_ptr<UltraCanvasButton> btnSearchPrev;
    std::shared_ptr<UltraCanvasButton> btnSearchNext;
    std::shared_ptr<UltraCanvasLabel> searchResultsLabel;
    
    // Page display
    std::shared_ptr<UltraCanvasImageElement> pageDisplay;
    std::shared_ptr<UltraCanvasImageElement> pageDisplay2;  // For double page mode
    std::shared_ptr<UltraCanvasScrollbar> horizontalScrollbar;
    std::shared_ptr<UltraCanvasScrollbar> verticalScrollbar;
    
    // Side panels
    std::shared_ptr<UltraCanvasEBookTOCPanel> tocPanel;
    std::shared_ptr<UltraCanvasListView> thumbnailPanel;
    std::shared_ptr<UltraCanvasEBookBookmarkPanel> bookmarkPanel;
    
    // Progress and status
    std::shared_ptr<UltraCanvasProgressBar> loadingProgress;
    std::shared_ptr<UltraCanvasLabel> statusLabel;
    
    // Layout rectangles
    Rect2Di toolbarArea;
    Rect2Di sidePanelArea;
    Rect2Di pageArea;
    Rect2Di statusArea;
    
    // Async operations
    std::future<void> loadingTask;
    std::vector<std::future<void>> thumbnailTasks;
    mutable std::mutex viewerMutex;
    
    // ===== INTERNAL METHODS =====
    
    // Initialization
    void InitializeComponents();
    void CreateToolbar();
    void CreatePageDisplay();
    void CreateSidePanels();
    void CreateStatusBar();
    void ConnectEventHandlers();
    
    // Layout
    void LayoutComponents();
    void PositionToolbar();
    void PositionSidePanels();
    void PositionPageDisplays();
    void PositionStatusBar();
    
    // Document loading
    void LoadDocumentAsync(const std::string& filePath, const std::string& password);
    void OnDocumentLoaded();
    void OnDocumentError(const std::string& error);
    
    // Thumbnail loading
    void LoadThumbnails();
    void LoadThumbnailAsync(std::shared_ptr<UltraCanvasEBookThumbnail> thumbnail, 
                            int pageNumber);
    
    // Page display
    void UpdatePageDisplay();
    void UpdateZoomSettings();
    void ClearDisplay();
    
    // Zoom calculations
    float CalculateFitZoom() const;
    float CalculateFitWidthZoom() const;
    float CalculateFitHeightZoom() const;
    float CalculateFitTextZoom() const;
    
    // UI updates
    void UpdateNavigationButtons();
    void UpdatePageNumberInput();
    void UpdateZoomButtons();
    void UpdateZoomLabel();
    void UpdateDisplayModeButtons();
    void UpdateReadingModeButtons();
    void UpdateThumbnailSelection();
    void UpdateStatusBar();
    
    // Event emission
    void EmitViewerEvent(EBookViewerEvent::Type type);
    void EmitError(const std::string& error);
    
    // Keyboard handling
    bool HandleKeyboardNavigation(const UCEvent& event);
    bool HandleKeyboardZoom(const UCEvent& event);
    bool HandleKeyboardShortcuts(const UCEvent& event);
    
    // Mouse handling
    bool HandleMouseNavigation(const UCEvent& event);
    bool HandleMouseZoom(const UCEvent& event);
    bool HandleMouseSelection(const UCEvent& event);
};

// ===== FACTORY FUNCTIONS =====

inline std::shared_ptr<UltraCanvasEBookViewer> CreateEBookViewer(
    const std::string& id, long x, long y, long w, long h) {
    static long nextUid = 50000;
    return std::make_shared<UltraCanvasEBookViewer>(id, nextUid++, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasEBookViewer> CreateEBookViewerWithID(
    const std::string& id, long uid, long x, long y, long w, long h) {
    return std::make_shared<UltraCanvasEBookViewer>(id, uid, x, y, w, h);
}

} // namespace UltraCanvas

/*
=== EBOOK VIEWER FEATURES ===

✅ **Complete eBook Display System**:
- Support for EPUB, FB2, MOBI, AZW3, and more formats
- Multi-page document navigation (First, Previous, Next, Last)
- Chapter-based navigation with Table of Contents
- Bookmark support with notes

✅ **Page Formats**:
- ISO A Series: A0 to A8
- ISO B Series: B0 to B8
- US Sizes: Letter, Legal, Tabloid
- Book Formats: PocketBook, TradeBook, Digest
- eReader Formats: Kindle, Kobo, iPad
- Custom page sizes in mm, inches, or pixels

✅ **Zoom Modes**:
- Actual Size (100%)
- Fit Page (entire page visible)
- Fit Width (page width matches view)
- Fit Height (page height matches view)
- Fit Text (content area only)
- Preset zoom levels (25% to 500%)
- Custom zoom with smooth transitions

✅ **Display Modes**:
- Single Page
- Double Page (book spread)
- Double Page with Cover
- Right-to-Left (manga mode)
- Continuous Vertical Scroll
- Continuous Horizontal Scroll
- Thumbnail Grid
- Text Reflow (single/double column)

✅ **Reading Modes**:
- Normal (white background)
- Sepia (warm tones)
- Night (dark mode)
- Gray (reduced contrast)
- High Contrast
- Custom colors

✅ **Navigation Features**:
- Page navigation with keyboard shortcuts
- Chapter navigation from TOC
- Bookmark navigation
- Progress-based navigation (percentage)
- Search with find next/previous

✅ **Text Features**:
- Full text search
- Text selection and copy
- Text reflow with customizable settings
- Font size and family control
- Line spacing and margins

✅ **Annotations**:
- Highlight with colors
- Notes and comments
- Bookmarks with notes

✅ **UI Components**:
- Comprehensive toolbar
- Thumbnail panel (show/hide)
- Table of Contents panel
- Bookmark panel
- Status bar with progress

✅ **Keyboard Shortcuts**:
- Left/Right: Previous/Next page
- Home/End: First/Last page
- Page Up/Down: Navigate pages
- Ctrl+0/1/2/3: Zoom presets
- Ctrl++/-: Zoom in/out
- Ctrl+F: Search
- F11: Fullscreen

✅ **Mouse Support**:
- Scroll wheel navigation
- Ctrl+Scroll: Zoom
- Middle-click pan
- Selection with drag

✅ **Usage Example**:
```cpp
// Create eBook viewer
auto ebookViewer = UltraCanvas::CreateEBookViewer("viewer", 0, 0, 1200, 800);

// Set up callbacks
ebookViewer->onPageChanged = [](int current, int total) {
    std::cout << "Page " << current << "/" << total << std::endl;
};

ebookViewer->onChapterChanged = [](int index, const std::string& title) {
    std::cout << "Chapter: " << title << std::endl;
};

// Load document
if (ebookViewer->LoadDocument("book.epub")) {
    // Set display preferences
    ebookViewer->SetDisplayMode(EBookDisplayMode::DoublePage);
    ebookViewer->SetReadingMode(EBookReadingMode::Sepia);
    ebookViewer->SetPageFormat(DocumentPageFormat::A5);
    ebookViewer->ZoomToFitWidth();
    
    // Navigate
    ebookViewer->GoToChapter(2);
    
    // Add bookmark
    ebookViewer->AddBookmark("Important section");
}

// Add to window
window->AddChild(ebookViewer);
```
*/
