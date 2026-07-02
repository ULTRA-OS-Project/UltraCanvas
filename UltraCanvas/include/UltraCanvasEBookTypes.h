// include/UltraCanvasEBookTypes.h
// Comprehensive eBook types, enums, and structures for document viewing
// Version: 1.0.0
// Last Modified: 2025-01-08
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"

// UltraCanvasCommonTypes.h pulls in X11/Xlib.h on Linux, whose None/Status/
// Success/Always macros collide with eBook enum members and factory methods
#ifdef None
#undef None
#endif
#ifdef Status
#undef Status
#endif
#ifdef Success
#undef Success
#endif
#ifdef Always
#undef Always
#endif
#ifdef Bool
#undef Bool
#endif

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <cmath>

namespace UltraCanvas {

// ===== DOCUMENT PAGE FORMATS =====
enum class DocumentPageFormat {
    // ISO A Series (International Standard)
    A0,             // 841 × 1189 mm
    A1,             // 594 × 841 mm
    A2,             // 420 × 594 mm
    A3,             // 297 × 420 mm
    A4,             // 210 × 297 mm (most common)
    A5,             // 148 × 210 mm (book size)
    A6,             // 105 × 148 mm (postcard/pocket book)
    A7,             // 74 × 105 mm
    A8,             // 52 × 74 mm
    
    // ISO B Series
    B0,             // 1000 × 1414 mm
    B1,             // 707 × 1000 mm
    B2,             // 500 × 707 mm
    B3,             // 353 × 500 mm
    B4,             // 250 × 353 mm
    B5,             // 176 × 250 mm (common book size)
    B6,             // 125 × 176 mm
    B7,             // 88 × 125 mm
    B8,             // 62 × 88 mm
    
    // ISO C Series (Envelopes)
    C4,             // 229 × 324 mm
    C5,             // 162 × 229 mm
    C6,             // 114 × 162 mm
    
    // US/North American Sizes
    Letter,         // 8.5 × 11 inch (215.9 × 279.4 mm)
    Legal,          // 8.5 × 14 inch (215.9 × 355.6 mm)
    Tabloid,        // 11 × 17 inch (279.4 × 431.8 mm)
    Ledger,         // 17 × 11 inch (landscape tabloid)
    Executive,      // 7.25 × 10.5 inch (184.15 × 266.7 mm)
    
    // Book-Specific Formats
    PocketBook,     // 4.25 × 6.87 inch (108 × 175 mm)
    MassMarket,     // 4.25 × 6.75 inch (108 × 171 mm) - Paperback
    TradeBook,      // 6 × 9 inch (152 × 229 mm)
    Digest,         // 5.5 × 8.5 inch (140 × 216 mm)
    Royal,          // 6.14 × 9.21 inch (156 × 234 mm)
    Crown,          // 7.5 × 10 inch (191 × 254 mm)
    
    // eReader-Specific Formats
    KindlePaperwhite,   // 6" display (90 × 122 mm visible)
    KindleOasis,        // 7" display
    KoboClara,          // 6" display
    KoboLibra,          // 7" display
    KoboElipsa,         // 10.3" display
    ReMarkable2,        // 10.3" E Ink tablet
    
    // Tablet Formats
    iPadMini,       // 7.9" display
    iPad,           // 10.2" display
    iPadPro11,      // 11" display
    iPadPro13,      // 12.9" display
    
    // Japanese Book Formats
    Bunko,          // 105 × 148 mm (A6 equivalent)
    Shinsho,        // 103 × 182 mm
    Tankobon,       // 128 × 182 mm
    
    // Custom
    Custom          // User-defined dimensions
};

// ===== DOCUMENT PAGE SIZE STRUCTURE =====
struct DocumentPageSize {
    float widthMM = 0.0f;       // Width in millimeters
    float heightMM = 0.0f;      // Height in millimeters
    float widthInch = 0.0f;     // Width in inches
    float heightInch = 0.0f;    // Height in inches
    float widthPx = 0.0f;       // Width in pixels at current DPI
    float heightPx = 0.0f;      // Height in pixels at current DPI
    float dpi = 96.0f;          // Current DPI setting
    DocumentPageFormat format = DocumentPageFormat::A5;
    std::string formatName;
    bool isLandscape = false;
    
    DocumentPageSize() = default;
    
    DocumentPageSize(float wMM, float hMM, float currentDpi = 96.0f)
        : widthMM(wMM), heightMM(hMM), dpi(currentDpi), format(DocumentPageFormat::Custom) {
        widthInch = wMM / 25.4f;
        heightInch = hMM / 25.4f;
        widthPx = widthInch * dpi;
        heightPx = heightInch * dpi;
        formatName = "Custom";
        isLandscape = wMM > hMM;
    }
    
    // Convert to landscape/portrait
    DocumentPageSize ToLandscape() const {
        if (isLandscape) return *this;
        DocumentPageSize landscape = *this;
        std::swap(landscape.widthMM, landscape.heightMM);
        std::swap(landscape.widthInch, landscape.heightInch);
        std::swap(landscape.widthPx, landscape.heightPx);
        landscape.isLandscape = true;
        return landscape;
    }
    
    DocumentPageSize ToPortrait() const {
        if (!isLandscape) return *this;
        DocumentPageSize portrait = *this;
        std::swap(portrait.widthMM, portrait.heightMM);
        std::swap(portrait.widthInch, portrait.heightInch);
        std::swap(portrait.widthPx, portrait.heightPx);
        portrait.isLandscape = false;
        return portrait;
    }
    
    // Update pixel dimensions for new DPI
    void SetDPI(float newDpi) {
        dpi = newDpi;
        widthPx = widthInch * dpi;
        heightPx = heightInch * dpi;
    }
    
    float GetAspectRatio() const {
        return (heightMM > 0) ? (widthMM / heightMM) : 1.0f;
    }
    
    // Factory methods
    static DocumentPageSize FromFormat(DocumentPageFormat format, float dpi = 96.0f);
    static DocumentPageSize Custom(float widthMM, float heightMM, float dpi = 96.0f);
    static DocumentPageSize FromInches(float widthInch, float heightInch, float dpi = 96.0f);
    static DocumentPageSize FromPixels(float widthPx, float heightPx, float dpi = 96.0f);
    
    // Get all available formats
    static std::vector<std::pair<DocumentPageFormat, std::string>> GetAllFormats();
};

// ===== EBOOK FORMAT TYPES =====
enum class EBookFormat {
    Unknown,
    
    // Open Standards
    EPUB,           // .epub - Open eBook Publication (IDPF/W3C)
    EPUB2,          // .epub - EPUB 2.x
    EPUB3,          // .epub - EPUB 3.x with enhanced features
    
    // XML-Based
    FB2,            // .fb2 - FictionBook 2.0
    FB3,            // .fb3 - FictionBook 3.0
    
    // Amazon/Kindle
    MOBI,           // .mobi - Mobipocket eBook
    AZW,            // .azw - Amazon Kindle (original)
    AZW3,           // .azw3 - Amazon KF8 (Kindle Format 8)
    KFX,            // .kfx - Amazon KFX (latest Kindle format)
    
    // Palm/PDA
    PML,            // .pml - Palm Markup Language
    PDB,            // .pdb - Palm Database (eReader)
    
    // Sony
    LRF,            // .lrf - Sony Portable Reader Format
    LRX,            // .lrx - Sony BBeB (encrypted)
    
    // Microsoft
    LIT,            // .lit - Microsoft eBook (discontinued)
    
    // Legacy/Other
    RB,             // .rb - RocketEdition eBook
    TCR,            // .tcr - Psion eBook (compressed text)
    OEB,            // .oeb - Open eBook (EPUB predecessor)
    
    // Apple
    PAGES,          // .pages - Apple iWork Pages
    IBOOKS,         // .ibooks - Apple iBooks Author
    
    // Comic/Manga
    CBZ,            // .cbz - Comic Book Archive (ZIP)
    CBR,            // .cbr - Comic Book Archive (RAR)
    CB7,            // .cb7 - Comic Book Archive (7z)
    
    // Plain Text
    TXT,            // .txt - Plain text
    RTF,            // .rtf - Rich Text Format
    
    // Web
    HTML,           // .html - HTML document
    XHTML           // .xhtml - XHTML document
};

// ===== EBOOK ZOOM MODES =====
enum class EBookZoomMode {
    // Standard zoom modes
    ActualSize,         // 100% - original document size
    FitPage,            // Fit entire page in view
    FitWidth,           // Fit page width to view width
    FitHeight,          // Fit page height to view height
    
    // eBook-specific zoom modes
    FitText,            // Fit text column width (ignore margins)
    FitContent,         // Fit content area (exclude headers/footers)
    FitTwoPages,        // Fit two pages side by side
    
    // Preset zoom levels
    Zoom25,             // 25%
    Zoom50,             // 50%
    Zoom75,             // 75%
    Zoom100,            // 100%
    Zoom125,            // 125%
    Zoom150,            // 150%
    Zoom175,            // 175%
    Zoom200,            // 200%
    Zoom300,            // 300%
    Zoom400,            // 400%
    Zoom500,            // 500%
    
    // Custom
    Custom              // User-defined percentage
};

// ===== EBOOK DISPLAY MODES =====
enum class EBookDisplayMode {
    // Page-based modes
    SinglePage,             // One page at a time
    DoublePage,             // Two pages side-by-side (book spread)
    DoublePageCover,        // Double page with cover as single first page
    DoublePageRTL,          // Double page, right-to-left (manga)
    
    // Scroll-based modes
    ContinuousVertical,     // Continuous vertical scrolling
    ContinuousHorizontal,   // Continuous horizontal scrolling
    
    // Grid modes
    ThumbnailGrid,          // Grid of page thumbnails
    ThumbnailStrip,         // Horizontal strip of thumbnails
    
    // Navigation modes
    TableOfContents,        // TOC navigation view
    BookmarkView,           // Show bookmarked pages
    AnnotationView,         // Show annotated pages
    
    // Reflow modes (for EPUB/eBooks)
    ReflowSingle,           // Reflowed text, single column
    ReflowDouble,           // Reflowed text, two columns
    OriginalLayout          // Preserve original fixed layout
};

// ===== READING MODE (COLOR THEMES) =====
enum class EBookReadingMode {
    Normal,         // Normal white background, black text
    Sepia,          // Warm sepia tones (cream background)
    Night,          // Dark mode (dark background, light text)
    Gray,           // Gray background, reduced contrast
    Green,          // Green tint (reduces eye strain)
    Blue,           // Blue light filter
    HighContrast,   // Maximum contrast (pure black/white)
    Custom          // User-defined colors
};

// ===== READING DIRECTION =====
enum class EBookReadingDirection {
    LeftToRight,    // Western languages (default)
    RightToLeft,    // Arabic, Hebrew, manga
    TopToBottom,    // Traditional Chinese/Japanese
    Automatic       // Detect from content
};

// ===== TEXT ALIGNMENT =====
enum class EBookTextAlignment {
    Left,
    Right,
    Center,
    Justify,
    Automatic       // Use document default
};

// ===== EBOOK PAGE INFO =====
struct EBookPageInfo {
    int pageNumber = 0;
    int totalPages = 0;
    float widthMM = 0.0f;
    float heightMM = 0.0f;
    float aspectRatio = 1.0f;
    std::string pageLabel;          // Custom page label (e.g., "ix", "Preface")
    std::string chapterTitle;       // Chapter this page belongs to
    int chapterIndex = -1;
    bool isLoaded = false;
    bool hasThumbnail = false;
    bool hasAnnotations = false;
    bool isBookmarked = false;
    
    EBookPageInfo() = default;
    
    EBookPageInfo(int num, float w, float h)
        : pageNumber(num), widthMM(w), heightMM(h) {
        aspectRatio = (h > 0) ? (w / h) : 1.0f;
        pageLabel = std::to_string(num);
    }
};

// ===== EBOOK DOCUMENT METADATA =====
struct EBookMetadata {
    // Basic info
    std::string title;
    std::string subtitle;
    std::vector<std::string> authors;
    std::string publisher;
    std::string language;           // ISO 639-1 code (e.g., "en", "de", "ja")
    std::string isbn;
    std::string asin;               // Amazon Standard Identification Number
    
    // Dates
    std::string publicationDate;
    std::string modificationDate;
    std::string creationDate;
    
    // Classification
    std::vector<std::string> subjects;
    std::vector<std::string> categories;
    std::string series;
    int seriesIndex = 0;
    
    // Description
    std::string description;
    std::string synopsis;
    
    // Rights
    std::string rights;
    std::string copyright;
    bool hasDRM = false;
    
    // Technical
    EBookFormat format = EBookFormat::Unknown;
    std::string formatVersion;
    int pageCount = 0;
    int chapterCount = 0;
    long fileSizeBytes = 0;
    bool hasImages = false;
    bool hasAudio = false;
    bool hasVideo = false;
    bool isFixedLayout = false;
    EBookReadingDirection readingDirection = EBookReadingDirection::LeftToRight;
    
    // Cover
    bool hasCover = false;
    std::vector<uint8_t> coverImageData;
    int coverWidth = 0;
    int coverHeight = 0;
    
    // Custom metadata
    std::unordered_map<std::string, std::string> customMetadata;
    
    std::string GetPrimaryAuthor() const {
        return authors.empty() ? "" : authors[0];
    }
    
    std::string GetAuthorsString(const std::string& separator = ", ") const {
        std::string result;
        for (size_t i = 0; i < authors.size(); ++i) {
            if (i > 0) result += separator;
            result += authors[i];
        }
        return result;
    }
};

// ===== TABLE OF CONTENTS ENTRY =====
struct EBookTOCEntry {
    std::string title;
    std::string href;               // Internal link reference
    int pageNumber = 0;
    int level = 0;                  // Nesting level (0 = top level)
    int index = 0;                  // Sequential index
    std::vector<EBookTOCEntry> children;
    bool isExpanded = true;
    
    EBookTOCEntry() = default;
    
    EBookTOCEntry(const std::string& t, const std::string& h, int page, int lvl = 0)
        : title(t), href(h), pageNumber(page), level(lvl) {}
    
    bool HasChildren() const { return !children.empty(); }
    int GetTotalEntries() const {
        int count = 1;
        for (const auto& child : children) {
            count += child.GetTotalEntries();
        }
        return count;
    }
};

// ===== BOOKMARK =====
struct EBookBookmark {
    int pageNumber = 0;
    std::string title;
    std::string note;
    std::string dateCreated;
    std::string dateModified;
    float scrollPosition = 0.0f;    // Vertical scroll position on page
    Color color = Color(255, 215, 0, 255);  // Gold default
    
    EBookBookmark() = default;
    
    EBookBookmark(int page, const std::string& t = "", const std::string& n = "")
        : pageNumber(page), title(t), note(n) {}
};

// ===== TEXT RANGE FOR ANNOTATIONS =====
struct EBookTextRange {
    int startPage = 0;
    int endPage = 0;
    int startOffset = 0;            // Character offset from page start
    int endOffset = 0;
    std::string selectedText;
    
    bool IsValid() const {
        return startPage > 0 && endPage >= startPage;
    }
    
    bool IsSinglePage() const {
        return startPage == endPage;
    }
};

// ===== ANNOTATION TYPES =====
enum class EBookAnnotationType {
    Highlight,
    Underline,
    Strikethrough,
    Note,
    Bookmark,
    Link,
    Drawing
};

// ===== ANNOTATION =====
struct EBookAnnotation {
    int id = 0;
    EBookAnnotationType type = EBookAnnotationType::Highlight;
    EBookTextRange range;
    std::string text;               // Selected text
    std::string note;               // User's note
    Color color = Color(255, 255, 0, 128);  // Default highlight yellow
    std::string dateCreated;
    std::string dateModified;
    bool isVisible = true;
    
    EBookAnnotation() = default;
    
    static EBookAnnotation CreateHighlight(const EBookTextRange& range, Color color = Color(255, 255, 0, 128)) {
        EBookAnnotation ann;
        ann.type = EBookAnnotationType::Highlight;
        ann.range = range;
        ann.color = color;
        return ann;
    }
    
    static EBookAnnotation CreateNote(const EBookTextRange& range, const std::string& note) {
        EBookAnnotation ann;
        ann.type = EBookAnnotationType::Note;
        ann.range = range;
        ann.note = note;
        return ann;
    }
};

// ===== SEARCH RESULT =====
struct EBookSearchResult {
    int pageNumber = 0;
    int chapterIndex = -1;
    std::string chapterTitle;
    int offsetInPage = 0;
    std::string contextBefore;      // Text before match
    std::string matchedText;        // The matched text
    std::string contextAfter;       // Text after match
    float relevanceScore = 0.0f;
    
    std::string GetFullContext() const {
        return contextBefore + matchedText + contextAfter;
    }
};

// ===== ZOOM SETTINGS =====
struct EBookZoomSettings {
    float zoomLevel = 1.0f;         // 1.0 = 100%
    float minZoom = 0.1f;           // 10% minimum
    float maxZoom = 10.0f;          // 1000% maximum
    float zoomStep = 0.25f;         // 25% increment per zoom in/out
    EBookZoomMode mode = EBookZoomMode::FitPage;
    bool smoothZoom = true;         // Animated zoom transitions
    bool pinchToZoom = true;        // Touch gesture support
    bool scrollWheelZoom = true;    // Ctrl+scroll to zoom
    Point2Df zoomCenter;            // Point to zoom towards
    
    float GetZoomPercent() const { return zoomLevel * 100.0f; }
    void SetZoomPercent(float percent) { zoomLevel = percent / 100.0f; }
    
    static EBookZoomSettings Default() { return EBookZoomSettings(); }
};

// ===== PAGE MARGIN SETTINGS =====
struct EBookMargins {
    float top = 20.0f;
    float bottom = 20.0f;
    float left = 25.0f;
    float right = 25.0f;
    float binding = 10.0f;          // Extra margin at binding edge
    float header = 15.0f;           // Space for header
    float footer = 15.0f;           // Space for footer
    
    static EBookMargins Default() { return EBookMargins(); }
    static EBookMargins Narrow() { return {10, 10, 15, 15, 5, 10, 10}; }
    static EBookMargins Wide() { return {30, 30, 40, 40, 15, 20, 20}; }
    // Not named "None": X11's Xlib.h (pulled in via UltraCanvasCommonTypes.h)
    // defines None as a macro.
    static EBookMargins NoMargins() { return {0, 0, 0, 0, 0, 0, 0}; }
};

// ===== TEXT REFLOW SETTINGS =====
struct EBookReflowSettings {
    std::string fontFamily = "Georgia";
    std::string fallbackFonts = "Times New Roman, serif";
    float fontSize = 16.0f;
    float lineSpacing = 1.4f;       // Line height multiplier
    float paragraphSpacing = 1.0f;  // Space between paragraphs (em)
    float letterSpacing = 0.0f;     // Additional letter spacing
    float wordSpacing = 0.0f;       // Additional word spacing
    EBookTextAlignment alignment = EBookTextAlignment::Justify;
    bool hyphenation = true;
    std::string hyphenationLanguage = "en-US";
    float textIndent = 1.5f;        // First line indent (em)
    bool dropCaps = false;          // Enable drop caps for chapters
    int columnsCount = 1;           // Number of text columns
    float columnGap = 20.0f;        // Gap between columns
    
    static EBookReflowSettings Default() { return EBookReflowSettings(); }
    static EBookReflowSettings Compact() {
        EBookReflowSettings s;
        s.fontSize = 14.0f;
        s.lineSpacing = 1.2f;
        s.paragraphSpacing = 0.5f;
        return s;
    }
    static EBookReflowSettings Large() {
        EBookReflowSettings s;
        s.fontSize = 20.0f;
        s.lineSpacing = 1.6f;
        return s;
    }
};

// ===== PAGE SPREAD SETTINGS =====
struct EBookSpreadSettings {
    bool rightToLeft = false;           // Manga/Arabic reading direction
    bool showPageGap = true;            // Gap between pages in double mode
    int pageGapPixels = 20;             // Gap size in pixels
    bool showBindingShadow = true;      // Shadow effect at page binding
    bool coverAsSinglePage = true;      // First page alone in double mode
    bool lastPageAsSingle = true;       // Last page alone if odd count
    bool showPageBorder = true;         // Border around pages
    Color pageBorderColor = Color(200, 200, 200, 255);
    float pageShadowSize = 5.0f;        // Drop shadow size
    float pageShadowOpacity = 0.3f;     // Shadow opacity
    
    static EBookSpreadSettings Default() { return EBookSpreadSettings(); }
    static EBookSpreadSettings Manga() {
        EBookSpreadSettings s;
        s.rightToLeft = true;
        return s;
    }
};

// ===== READING MODE COLORS =====
struct EBookReadingModeColors {
    Color backgroundColor;
    Color textColor;
    Color linkColor;
    Color highlightColor;
    Color selectionColor;
    std::string modeName;
    
    static EBookReadingModeColors Normal() {
        return {
            Color(255, 255, 255, 255),      // White background
            Color(0, 0, 0, 255),            // Black text
            Color(0, 0, 238, 255),          // Blue links
            Color(255, 255, 0, 128),        // Yellow highlight
            Color(51, 153, 255, 128),       // Blue selection
            "Normal"
        };
    }
    
    static EBookReadingModeColors Sepia() {
        return {
            Color(251, 241, 219, 255),      // Cream background
            Color(90, 70, 50, 255),         // Brown text
            Color(120, 80, 40, 255),        // Brown links
            Color(255, 220, 150, 128),      // Orange highlight
            Color(200, 170, 120, 128),      // Tan selection
            "Sepia"
        };
    }
    
    static EBookReadingModeColors Night() {
        return {
            Color(30, 30, 30, 255),         // Dark background
            Color(200, 200, 200, 255),      // Light gray text
            Color(100, 150, 255, 255),      // Light blue links
            Color(80, 80, 0, 128),          // Dark yellow highlight
            Color(60, 80, 120, 128),        // Dark blue selection
            "Night"
        };
    }
    
    static EBookReadingModeColors Gray() {
        return {
            Color(220, 220, 220, 255),      // Light gray background
            Color(50, 50, 50, 255),         // Dark gray text
            Color(0, 100, 200, 255),        // Blue links
            Color(200, 200, 100, 128),      // Muted yellow highlight
            Color(150, 180, 210, 128),      // Light blue selection
            "Gray"
        };
    }
    
    static EBookReadingModeColors HighContrast() {
        return {
            Color(0, 0, 0, 255),            // Black background
            Color(255, 255, 255, 255),      // White text
            Color(255, 255, 0, 255),        // Yellow links
            Color(255, 0, 0, 128),          // Red highlight
            Color(255, 255, 255, 128),      // White selection
            "High Contrast"
        };
    }
};

// ===== COMPREHENSIVE RENDER SETTINGS =====
struct EBookRenderSettings {
    // DPI and quality
    float dpi = 150.0f;
    float zoomLevel = 1.0f;
    EBookZoomMode zoomMode = EBookZoomMode::FitPage;
    EBookDisplayMode displayMode = EBookDisplayMode::SinglePage;
    
    // Page format
    DocumentPageFormat pageFormat = DocumentPageFormat::A5;
    DocumentPageSize customPageSize;
    bool useDocumentPageSize = true;
    
    // Rendering quality
    bool antialiasing = true;
    bool subpixelRendering = false;
    bool fontHinting = true;
    bool smoothImages = true;
    int jpegQuality = 90;
    
    // Layout
    EBookMargins margins;
    EBookSpreadSettings spread;
    EBookReflowSettings reflow;
    
    // Colors and reading mode
    EBookReadingMode readingMode = EBookReadingMode::Normal;
    EBookReadingModeColors colors = EBookReadingModeColors::Normal();
    
    // Thumbnails and caching
    int thumbnailSize = 200;
    bool enableThumbnails = true;
    bool preloadPages = true;
    int preloadRange = 2;
    int maxCachedPages = 10;
    
    // Headers and footers
    bool showPageNumbers = true;
    bool showChapterTitle = true;
    bool showProgressBar = false;
    
    // Factory methods
    static EBookRenderSettings Default() { return EBookRenderSettings(); }
    
    static EBookRenderSettings HighQuality() {
        EBookRenderSettings s;
        s.dpi = 300.0f;
        s.antialiasing = true;
        s.subpixelRendering = true;
        s.fontHinting = true;
        return s;
    }
    
    static EBookRenderSettings FastPreview() {
        EBookRenderSettings s;
        s.dpi = 72.0f;
        s.antialiasing = false;
        s.preloadPages = false;
        s.enableThumbnails = false;
        return s;
    }
    
    static EBookRenderSettings EInkOptimized() {
        EBookRenderSettings s;
        s.dpi = 300.0f;
        s.antialiasing = true;
        s.subpixelRendering = false;    // E-Ink doesn't benefit
        s.colors = EBookReadingModeColors::Normal();
        s.preloadPages = false;         // Save memory
        return s;
    }
    
    static EBookRenderSettings MobileOptimized() {
        EBookRenderSettings s;
        s.dpi = 150.0f;
        s.displayMode = EBookDisplayMode::SinglePage;
        s.preloadRange = 1;
        s.maxCachedPages = 5;
        return s;
    }
};

// ===== EBOOK VIEWER STATE =====
struct EBookViewerState {
    // Document state
    std::string currentDocumentPath;
    bool isDocumentLoaded = false;
    bool isLoading = false;
    float loadingProgress = 0.0f;
    
    // Navigation state
    int currentPage = 1;
    int totalPages = 0;
    int currentChapter = 0;
    float scrollPosition = 0.0f;
    float readingProgress = 0.0f;   // 0.0 to 1.0
    
    // View state
    float currentZoom = 1.0f;
    EBookZoomMode zoomMode = EBookZoomMode::FitPage;
    EBookDisplayMode displayMode = EBookDisplayMode::SinglePage;
    EBookReadingMode readingMode = EBookReadingMode::Normal;
    
    // Selection state
    bool hasSelection = false;
    EBookTextRange selection;
    
    // Search state
    bool isSearching = false;
    std::string searchQuery;
    int searchResultIndex = 0;
    int totalSearchResults = 0;
    
    // UI state
    bool isThumbnailPanelVisible = false;
    bool isTOCVisible = false;
    bool isFullScreen = false;
};

// ===== EBOOK VIEWER EVENTS =====
struct EBookViewerEvent {
    enum Type {
        DocumentLoading,
        DocumentLoaded,
        DocumentClosed,
        DocumentError,
        PageChanged,
        ChapterChanged,
        ZoomChanged,
        DisplayModeChanged,
        ReadingModeChanged,
        BookmarkAdded,
        BookmarkRemoved,
        AnnotationAdded,
        AnnotationRemoved,
        SearchStarted,
        SearchCompleted,
        SearchResultSelected,
        SelectionChanged,
        Error
    };
    
    Type type;
    int pageNumber = 0;
    int totalPages = 0;
    int chapterIndex = 0;
    float zoomLevel = 1.0f;
    float progress = 0.0f;
    std::string message;
    std::string chapterTitle;
    EBookZoomMode zoomMode = EBookZoomMode::FitPage;
    EBookDisplayMode displayMode = EBookDisplayMode::SinglePage;
    
    EBookViewerEvent(Type t) : type(t) {}
};

// ===== EBOOK PLUGIN CAPABILITIES =====
struct EBookPluginCapabilities {
    std::string pluginName;
    std::string pluginVersion;
    std::vector<std::string> supportedExtensions;
    std::vector<EBookFormat> supportedFormats;
    
    bool canRead = true;
    bool canWrite = false;
    bool canConvert = false;
    
    bool supportsReflow = false;
    bool supportsFixedLayout = false;
    bool supportsTOC = false;
    bool supportsBookmarks = false;
    bool supportsAnnotations = false;
    bool supportsSearch = false;
    bool supportsImages = false;
    bool supportsAudio = false;
    bool supportsVideo = false;
    bool supportsDRM = false;
    bool supportsEncryption = false;
    
    int maxPageCount = 0;           // 0 = unlimited
    std::string description;
};

// ===== CALLBACK TYPES =====
using EBookPageChangedCallback = std::function<void(int currentPage, int totalPages)>;
using EBookChapterChangedCallback = std::function<void(int chapterIndex, const std::string& title)>;
using EBookZoomChangedCallback = std::function<void(float zoom, EBookZoomMode mode)>;
using EBookProgressCallback = std::function<void(float progress)>;
using EBookErrorCallback = std::function<void(const std::string& error)>;
using EBookEventCallback = std::function<void(const EBookViewerEvent& event)>;
using EBookLoadProgressCallback = std::function<void(float progress, const std::string& status)>;

} // namespace UltraCanvas
