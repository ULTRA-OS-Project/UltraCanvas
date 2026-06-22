// core/UltraCanvasEBookViewer.cpp
// Comprehensive eBook viewer component implementation
// Version: 1.0.0
// Last Modified: 2025-01-08
// Author: UltraCanvas Framework

#include "UltraCanvasEBookViewer.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasFlexLayout.h"
#include "IEBookEngine.h"
#include "EPUBEngine.h"
#include "FB2Engine.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace UltraCanvas {

// ============================================================================
// EBOOK THUMBNAIL IMPLEMENTATION
// ============================================================================

void UltraCanvasEBookThumbnail::Render(IRenderContext* ctx) {
    if (!IsVisible() || !ctx) return;
    
    ctx->PushState();
    
    // Background
    Color bgColor = isSelected ? Color(51, 153, 255, 255) : Color(245, 245, 245, 255);
    ctx->DrawFilledRectangle(GetBounds(), bgColor);
    
    // Border
    Color borderColor = isSelected ? Color(30, 120, 220, 255) : Color(200, 200, 200, 255);
    ctx->SetStrokeColor(borderColor);
    ctx->SetStrokeWidth(isSelected ? 2.0f : 1.0f);
    ctx->DrawRectangle(GetBounds());
    
    // Thumbnail image or loading indicator
    Rect2Di imageRect(GetX() + 4, GetY() + 4, GetWidth() - 8, GetHeight() - 24);
    
    if (isLoading) {
        // Loading indicator
        ctx->DrawFilledRectangle(imageRect, Color(230, 230, 230, 255));
        ctx->SetTextColor(Color(128, 128, 128, 255));
        ctx->SetFontSize(10.0f);
        ctx->DrawTextCentered("Loading...", imageRect);
    } else if (!thumbnailData.empty()) {
        // Draw thumbnail image
        ctx->DrawImageFromMemory(thumbnailData.data(), thumbnailData.size(), imageRect);
    } else {
        // Placeholder
        ctx->DrawFilledRectangle(imageRect, Color(240, 240, 240, 255));
        ctx->SetTextColor(Color(180, 180, 180, 255));
        ctx->SetFontSize(12.0f);
        ctx->DrawTextCentered("No Preview", imageRect);
    }
    
    // Page number
    Rect2Di labelRect(GetX(), GetY() + GetHeight() - 20, GetWidth(), 20);
    ctx->SetTextColor(isSelected ? Color(255, 255, 255, 255) : Color(60, 60, 60, 255));
    ctx->SetFontSize(11.0f);
    ctx->DrawTextCentered(std::to_string(pageNumber), labelRect);
    
    ctx->PopState();
}

bool UltraCanvasEBookThumbnail::OnEvent(const UCEvent& event) {
    if (event.type == UCEventType::MouseDown && event.mouse.button == UCMouseButton::Left) {
        if (Contains(event.x, event.y)) {
            if (onPageSelected) {
                onPageSelected(pageNumber);
            }
            return true;
        }
    }
    return UltraCanvasUIElement::OnEvent(event);
}

// ============================================================================
// EBOOK TOC PANEL IMPLEMENTATION
// ============================================================================

UltraCanvasEBookTOCPanel::UltraCanvasEBookTOCPanel(const std::string& id, long uid,
                                                     long x, long y, long w, long h)
    : UltraCanvasContainer(id, uid, x, y, w, h) {
    
    SetBackgroundColor(Color(250, 250, 250, 255));
    
    // Create list view for TOC entries
    listView = std::make_shared<UltraCanvasListView>("tocList", uid + 1, 0, 0, w, h);
    listView->SetSelectionMode(ListSelectionMode::Single);
    
    listView->onItemSelected = [this](int index) {
        if (index >= 0 && index < static_cast<int>(tocEntries.size())) {
            if (onChapterSelected) {
                onChapterSelected(tocEntries[index].pageNumber, index);
            }
        }
    };
    
    AddChild(listView);
}

void UltraCanvasEBookTOCPanel::SetTableOfContents(const std::vector<EBookTOCEntry>& toc) {
    tocEntries = toc;
    BuildTOCList();
}

void UltraCanvasEBookTOCPanel::SetCurrentChapter(int chapterIndex) {
    currentChapter = chapterIndex;
    if (listView && chapterIndex >= 0) {
        listView->SetSelectedIndex(chapterIndex);
    }
    RequestRedraw();
}

void UltraCanvasEBookTOCPanel::Clear() {
    tocEntries.clear();
    currentChapter = -1;
    if (listView) {
        listView->ClearItems();
    }
}

void UltraCanvasEBookTOCPanel::Render(IRenderContext* ctx) {
    if (!IsVisible() || !ctx) return;
    UltraCanvasContainer::Render(ctx);
}

void UltraCanvasEBookTOCPanel::BuildTOCList() {
    if (!listView) return;
    
    listView->ClearItems();
    
    for (const auto& entry : tocEntries) {
        AddTOCEntry(entry, 0);
    }
}

void UltraCanvasEBookTOCPanel::AddTOCEntry(const EBookTOCEntry& entry, int indent) {
    if (!listView) return;
    
    // Create indented title
    std::string displayTitle = std::string(indent * 2, ' ') + entry.title;
    
    // Add to list view
    listView->AddItem(displayTitle);
    
    // Recursively add children
    for (const auto& child : entry.children) {
        AddTOCEntry(child, indent + 1);
    }
}

// ============================================================================
// EBOOK BOOKMARK PANEL IMPLEMENTATION
// ============================================================================

UltraCanvasEBookBookmarkPanel::UltraCanvasEBookBookmarkPanel(const std::string& id, long uid,
                                                               long x, long y, long w, long h)
    : UltraCanvasContainer(id, uid, x, y, w, h) {
    
    SetBackgroundColor(Color(250, 250, 250, 255));
    
    // Create list view for bookmarks
    listView = std::make_shared<UltraCanvasListView>("bookmarkList", uid + 1, 0, 0, w, h);
    listView->SetSelectionMode(ListSelectionMode::Single);
    
    listView->onItemSelected = [this](int index) {
        if (index >= 0 && index < static_cast<int>(bookmarks.size())) {
            if (onBookmarkSelected) {
                onBookmarkSelected(bookmarks[index].pageNumber);
            }
        }
    };
    
    AddChild(listView);
}

void UltraCanvasEBookBookmarkPanel::SetBookmarks(const std::vector<EBookBookmark>& bm) {
    bookmarks = bm;
    BuildBookmarkList();
}

void UltraCanvasEBookBookmarkPanel::AddBookmark(const EBookBookmark& bookmark) {
    bookmarks.push_back(bookmark);
    BuildBookmarkList();
}

void UltraCanvasEBookBookmarkPanel::RemoveBookmark(int pageNumber) {
    auto it = std::remove_if(bookmarks.begin(), bookmarks.end(),
        [pageNumber](const EBookBookmark& bm) { return bm.pageNumber == pageNumber; });
    
    if (it != bookmarks.end()) {
        bookmarks.erase(it, bookmarks.end());
        BuildBookmarkList();
    }
}

void UltraCanvasEBookBookmarkPanel::Clear() {
    bookmarks.clear();
    if (listView) {
        listView->ClearItems();
    }
}

void UltraCanvasEBookBookmarkPanel::Render(IRenderContext* ctx) {
    if (!IsVisible() || !ctx) return;
    UltraCanvasContainer::Render(ctx);
}

void UltraCanvasEBookBookmarkPanel::BuildBookmarkList() {
    if (!listView) return;
    
    listView->ClearItems();
    
    for (const auto& bookmark : bookmarks) {
        std::string displayText = "Page " + std::to_string(bookmark.pageNumber);
        if (!bookmark.title.empty()) {
            displayText += ": " + bookmark.title;
        }
        listView->AddItem(displayText);
    }
}

// ============================================================================
// MAIN EBOOK VIEWER - CONSTRUCTOR & DESTRUCTOR
// ============================================================================

UltraCanvasEBookViewer::UltraCanvasEBookViewer(const std::string& id, long uid,
                                                 long x, long y, long w, long h)
    : UltraCanvasUIElement(id, uid, x, y, w, h) {
    
    // Initialize state
    state.isLoaded = false;
    state.currentPage = 1;
    state.totalPages = 0;
    state.zoomLevel = 1.0f;
    state.zoomMode = EBookZoomMode::FitPage;
    state.displayMode = EBookDisplayMode::SinglePage;
    state.readingMode = EBookReadingMode::Normal;
    state.showToolbar = true;
    state.showThumbnails = false;
    state.showTOC = false;
    state.showBookmarks = false;
    
    // Initialize render settings
    renderSettings.pageFormat = DocumentPageFormat::A5;
    renderSettings.pageSize = DocumentPageSize::FromFormat(DocumentPageFormat::A5);
    renderSettings.dpi = 96.0f;
    renderSettings.backgroundColor = Color(255, 255, 255, 255);
    renderSettings.textColor = Color(0, 0, 0, 255);
    
    // Initialize components
    InitializeComponents();
    
    // Layout components
    LayoutComponents();
}

UltraCanvasEBookViewer::~UltraCanvasEBookViewer() {
    CloseDocument();
}

// ============================================================================
// COMPONENT INITIALIZATION
// ============================================================================

void UltraCanvasEBookViewer::InitializeComponents() {
    CreateToolbar();
    CreatePageDisplay();
    CreateSidePanels();
    CreateStatusBar();
    ConnectEventHandlers();
}

void UltraCanvasEBookViewer::CreateToolbar() {
    long toolbarHeight = 40;
    long btnSize = 32;
    long btnSpacing = 4;
    long currentX = 8;
    long toolbarUid = GetUID() + 100;
    
    // Toolbar container
    toolbar = std::make_shared<UltraCanvasContainer>("toolbar", toolbarUid++,
        0, 0, GetWidth(), toolbarHeight);
    toolbar->SetBackgroundColor(Color(245, 245, 245, 255));
    
    // Navigation buttons
    btnFirstPage = std::make_shared<UltraCanvasButton>("btnFirst", toolbarUid++,
        currentX, 4, btnSize, btnSize);
    btnFirstPage->SetText("⏮");
    btnFirstPage->SetTooltip("First Page (Home)");
    toolbar->AddChild(btnFirstPage);
    currentX += btnSize + btnSpacing;
    
    btnPrevPage = std::make_shared<UltraCanvasButton>("btnPrev", toolbarUid++,
        currentX, 4, btnSize, btnSize);
    btnPrevPage->SetText("◀");
    btnPrevPage->SetTooltip("Previous Page (Left)");
    toolbar->AddChild(btnPrevPage);
    currentX += btnSize + btnSpacing;
    
    btnNextPage = std::make_shared<UltraCanvasButton>("btnNext", toolbarUid++,
        currentX, 4, btnSize, btnSize);
    btnNextPage->SetText("▶");
    btnNextPage->SetTooltip("Next Page (Right)");
    toolbar->AddChild(btnNextPage);
    currentX += btnSize + btnSpacing;
    
    btnLastPage = std::make_shared<UltraCanvasButton>("btnLast", toolbarUid++,
        currentX, 4, btnSize, btnSize);
    btnLastPage->SetText("⏭");
    btnLastPage->SetTooltip("Last Page (End)");
    toolbar->AddChild(btnLastPage);
    currentX += btnSize + btnSpacing + 8;
    
    // Page number input
    pageNumberInput = std::make_shared<UltraCanvasTextInput>("pageInput", toolbarUid++,
        currentX, 6, 60, 28);
    pageNumberInput->SetText("1");
    pageNumberInput->SetTextAlignment(TextAlignment::Center);
    toolbar->AddChild(pageNumberInput);
    currentX += 60 + btnSpacing;
    
    // Page count label
    pageCountLabel = std::make_shared<UltraCanvasLabel>("pageCount", toolbarUid++,
        currentX, 6, 60, 28);
    pageCountLabel->SetText("/ 0");
    toolbar->AddChild(pageCountLabel);
    currentX += 60 + 16;
    
    // Zoom buttons
    btnZoomOut = std::make_shared<UltraCanvasButton>("btnZoomOut", toolbarUid++,
        currentX, 4, btnSize, btnSize);
    btnZoomOut->SetText("−");
    btnZoomOut->SetTooltip("Zoom Out (Ctrl+-)");
    toolbar->AddChild(btnZoomOut);
    currentX += btnSize + btnSpacing;
    
    zoomLabel = std::make_shared<UltraCanvasLabel>("zoomLabel", toolbarUid++,
        currentX, 6, 50, 28);
    zoomLabel->SetText("100%");
    zoomLabel->SetTextAlignment(TextAlignment::Center);
    toolbar->AddChild(zoomLabel);
    currentX += 50 + btnSpacing;
    
    btnZoomIn = std::make_shared<UltraCanvasButton>("btnZoomIn", toolbarUid++,
        currentX, 4, btnSize, btnSize);
    btnZoomIn->SetText("+");
    btnZoomIn->SetTooltip("Zoom In (Ctrl++)");
    toolbar->AddChild(btnZoomIn);
    currentX += btnSize + btnSpacing;
    
    btnZoomFit = std::make_shared<UltraCanvasButton>("btnZoomFit", toolbarUid++,
        currentX, 4, btnSize, btnSize);
    btnZoomFit->SetText("⬜");
    btnZoomFit->SetTooltip("Fit Page (Ctrl+0)");
    toolbar->AddChild(btnZoomFit);
    currentX += btnSize + btnSpacing;
    
    btnZoomFitWidth = std::make_shared<UltraCanvasButton>("btnZoomFitWidth", toolbarUid++,
        currentX, 4, btnSize, btnSize);
    btnZoomFitWidth->SetText("↔");
    btnZoomFitWidth->SetTooltip("Fit Width (Ctrl+1)");
    toolbar->AddChild(btnZoomFitWidth);
    currentX += btnSize + btnSpacing;
    
    btnZoomActualSize = std::make_shared<UltraCanvasButton>("btnZoomActual", toolbarUid++,
        currentX, 4, btnSize, btnSize);
    btnZoomActualSize->SetText("1:1");
    btnZoomActualSize->SetTooltip("Actual Size (Ctrl+2)");
    toolbar->AddChild(btnZoomActualSize);
    currentX += btnSize + 16;
    
    // Display mode buttons
    btnSinglePage = std::make_shared<UltraCanvasButton>("btnSingle", toolbarUid++,
        currentX, 4, btnSize, btnSize);
    btnSinglePage->SetText("📄");
    btnSinglePage->SetTooltip("Single Page");
    toolbar->AddChild(btnSinglePage);
    currentX += btnSize + btnSpacing;
    
    btnDoublePage = std::make_shared<UltraCanvasButton>("btnDouble", toolbarUid++,
        currentX, 4, btnSize, btnSize);
    btnDoublePage->SetText("📖");
    btnDoublePage->SetTooltip("Double Page");
    toolbar->AddChild(btnDoublePage);
    currentX += btnSize + btnSpacing;
    
    btnContinuousScroll = std::make_shared<UltraCanvasButton>("btnContinuous", toolbarUid++,
        currentX, 4, btnSize, btnSize);
    btnContinuousScroll->SetText("📜");
    btnContinuousScroll->SetTooltip("Continuous Scroll");
    toolbar->AddChild(btnContinuousScroll);
    currentX += btnSize + 16;
    
    // Reading mode button
    btnReadingMode = std::make_shared<UltraCanvasButton>("btnReadingMode", toolbarUid++,
        currentX, 4, btnSize, btnSize);
    btnReadingMode->SetText("☀");
    btnReadingMode->SetTooltip("Reading Mode");
    toolbar->AddChild(btnReadingMode);
    currentX += btnSize + 16;
    
    // Panel toggle buttons
    btnToggleTOC = std::make_shared<UltraCanvasButton>("btnTOC", toolbarUid++,
        currentX, 4, btnSize, btnSize);
    btnToggleTOC->SetText("📑");
    btnToggleTOC->SetTooltip("Table of Contents");
    toolbar->AddChild(btnToggleTOC);
    currentX += btnSize + btnSpacing;
    
    btnToggleThumbnails = std::make_shared<UltraCanvasButton>("btnThumbs", toolbarUid++,
        currentX, 4, btnSize, btnSize);
    btnToggleThumbnails->SetText("🖼");
    btnToggleThumbnails->SetTooltip("Thumbnails");
    toolbar->AddChild(btnToggleThumbnails);
    currentX += btnSize + btnSpacing;
    
    btnToggleBookmarks = std::make_shared<UltraCanvasButton>("btnBookmarks", toolbarUid++,
        currentX, 4, btnSize, btnSize);
    btnToggleBookmarks->SetText("🔖");
    btnToggleBookmarks->SetTooltip("Bookmarks");
    toolbar->AddChild(btnToggleBookmarks);
}

void UltraCanvasEBookViewer::CreatePageDisplay() {
    long displayUid = GetUID() + 200;
    
    // Main page display
    pageDisplay = std::make_shared<UltraCanvasImageElement>("pageDisplay", displayUid++,
        0, 0, 100, 100);
    pageDisplay->SetScaleMode(ImageScaleMode::FitCenter);
    pageDisplay->SetBackgroundColor(Color(128, 128, 128, 255));
    
    // Second page display (for double page mode)
    pageDisplay2 = std::make_shared<UltraCanvasImageElement>("pageDisplay2", displayUid++,
        0, 0, 100, 100);
    pageDisplay2->SetScaleMode(ImageScaleMode::FitCenter);
    pageDisplay2->SetBackgroundColor(Color(128, 128, 128, 255));
    pageDisplay2->SetVisible(false);
    
    // Scrollbars
    horizontalScrollbar = std::make_shared<UltraCanvasScrollbar>("hScroll", displayUid++,
        0, 0, 100, 16, ScrollbarOrientation::Horizontal);
    horizontalScrollbar->SetVisible(false);
    
    verticalScrollbar = std::make_shared<UltraCanvasScrollbar>("vScroll", displayUid++,
        0, 0, 16, 100, ScrollbarOrientation::Vertical);
    verticalScrollbar->SetVisible(false);
}

void UltraCanvasEBookViewer::CreateSidePanels() {
    long panelUid = GetUID() + 300;
    long panelWidth = 200;
    
    // TOC Panel
    tocPanel = std::make_shared<UltraCanvasEBookTOCPanel>("tocPanel", panelUid++,
        0, 0, panelWidth, 100);
    tocPanel->SetVisible(false);
    
    tocPanel->onChapterSelected = [this](int pageNumber, int chapterIndex) {
        GoToPage(pageNumber);
        if (onChapterChanged) {
            onChapterChanged(chapterIndex, tableOfContents[chapterIndex].title);
        }
    };
    
    // Thumbnail Panel
    thumbnailPanel = std::make_shared<UltraCanvasListView>("thumbPanel", panelUid++,
        0, 0, panelWidth, 100);
    thumbnailPanel->SetVisible(false);
    
    // Bookmark Panel
    bookmarkPanel = std::make_shared<UltraCanvasEBookBookmarkPanel>("bookmarkPanel", panelUid++,
        0, 0, panelWidth, 100);
    bookmarkPanel->SetVisible(false);
    
    bookmarkPanel->onBookmarkSelected = [this](int pageNumber) {
        GoToPage(pageNumber);
    };
}

void UltraCanvasEBookViewer::CreateStatusBar() {
    long statusUid = GetUID() + 400;
    long statusHeight = 24;
    
    // Loading progress bar
    loadingProgress = std::make_shared<UltraCanvasProgressBar>("loadProgress", statusUid++,
        0, 0, GetWidth(), 4);
    loadingProgress->SetVisible(false);
    loadingProgress->SetStyle(ProgressBarStyle::Standard);
    
    // Status label
    statusLabel = std::make_shared<UltraCanvasLabel>("statusLabel", statusUid++,
        0, 0, GetWidth(), statusHeight);
    statusLabel->SetText("No document loaded");
    statusLabel->SetFontSize(11.0f);
    statusLabel->SetTextColor(Color(100, 100, 100, 255));
}

void UltraCanvasEBookViewer::ConnectEventHandlers() {
    // Navigation buttons
    if (btnFirstPage) {
        btnFirstPage->SetOnClick([this]() { GoToFirstPage(); });
    }
    if (btnPrevPage) {
        btnPrevPage->SetOnClick([this]() { GoToPreviousPage(); });
    }
    if (btnNextPage) {
        btnNextPage->SetOnClick([this]() { GoToNextPage(); });
    }
    if (btnLastPage) {
        btnLastPage->SetOnClick([this]() { GoToLastPage(); });
    }
    
    // Page number input
    if (pageNumberInput) {
        pageNumberInput->onTextChanged = [this](const std::string& text) {
            try {
                int page = std::stoi(text);
                if (page >= 1 && page <= state.totalPages) {
                    GoToPage(page);
                }
            } catch (...) {
                // Invalid input, ignore
            }
        };
    }
    
    // Zoom buttons
    if (btnZoomIn) {
        btnZoomIn->SetOnClick([this]() { ZoomIn(); });
    }
    if (btnZoomOut) {
        btnZoomOut->SetOnClick([this]() { ZoomOut(); });
    }
    if (btnZoomFit) {
        btnZoomFit->SetOnClick([this]() { ZoomToFit(); });
    }
    if (btnZoomFitWidth) {
        btnZoomFitWidth->SetOnClick([this]() { ZoomToFitWidth(); });
    }
    if (btnZoomActualSize) {
        btnZoomActualSize->SetOnClick([this]() { ZoomToActualSize(); });
    }
    
    // Display mode buttons
    if (btnSinglePage) {
        btnSinglePage->SetOnClick([this]() { SetDisplayMode(EBookDisplayMode::SinglePage); });
    }
    if (btnDoublePage) {
        btnDoublePage->SetOnClick([this]() { SetDisplayMode(EBookDisplayMode::DoublePage); });
    }
    if (btnContinuousScroll) {
        btnContinuousScroll->SetOnClick([this]() { SetDisplayMode(EBookDisplayMode::ContinuousVertical); });
    }
    
    // Reading mode button
    if (btnReadingMode) {
        btnReadingMode->SetOnClick([this]() {
            // Cycle through reading modes
            int currentMode = static_cast<int>(state.readingMode);
            int nextMode = (currentMode + 1) % 6;
            SetReadingMode(static_cast<EBookReadingMode>(nextMode));
        });
    }
    
    // Panel toggle buttons
    if (btnToggleTOC) {
        btnToggleTOC->SetOnClick([this]() { ToggleTOCPanel(); });
    }
    if (btnToggleThumbnails) {
        btnToggleThumbnails->SetOnClick([this]() { ToggleThumbnailPanel(); });
    }
    if (btnToggleBookmarks) {
        btnToggleBookmarks->SetOnClick([this]() { ToggleBookmarkPanel(); });
    }
}

// ============================================================================
// LAYOUT
// ============================================================================

void UltraCanvasEBookViewer::LayoutComponents() {
    long toolbarHeight = state.showToolbar ? 40 : 0;
    long statusHeight = 24;
    long sidePanelWidth = 200;
    long scrollbarSize = 16;
    
    // Toolbar area
    toolbarArea = Rect2Di(0, 0, GetWidth(), toolbarHeight);
    if (toolbar) {
        toolbar->SetBounds(toolbarArea);
    }
    
    // Calculate side panel visibility
    bool showSidePanel = state.showTOC || state.showThumbnails || state.showBookmarks;
    long sidePanelActualWidth = showSidePanel ? sidePanelWidth : 0;
    
    // Side panel area
    sidePanelArea = Rect2Di(0, toolbarHeight, sidePanelActualWidth, 
                            GetHeight() - toolbarHeight - statusHeight);
    
    // Page area (main content)
    pageArea = Rect2Di(sidePanelActualWidth, toolbarHeight,
                       GetWidth() - sidePanelActualWidth,
                       GetHeight() - toolbarHeight - statusHeight);
    
    // Status area
    statusArea = Rect2Di(0, GetHeight() - statusHeight, GetWidth(), statusHeight);
    
    // Position toolbar
    PositionToolbar();
    
    // Position side panels
    PositionSidePanels();
    
    // Position page displays
    PositionPageDisplays();
    
    // Position status bar
    PositionStatusBar();
}

void UltraCanvasEBookViewer::PositionToolbar() {
    if (toolbar) {
        toolbar->SetBounds(toolbarArea);
        toolbar->SetVisible(state.showToolbar);
    }
}

void UltraCanvasEBookViewer::PositionSidePanels() {
    // TOC Panel
    if (tocPanel) {
        tocPanel->SetBounds(sidePanelArea);
        tocPanel->SetVisible(state.showTOC);
    }
    
    // Thumbnail Panel
    if (thumbnailPanel) {
        thumbnailPanel->SetBounds(sidePanelArea);
        thumbnailPanel->SetVisible(state.showThumbnails);
    }
    
    // Bookmark Panel
    if (bookmarkPanel) {
        bookmarkPanel->SetBounds(sidePanelArea);
        bookmarkPanel->SetVisible(state.showBookmarks);
    }
}

void UltraCanvasEBookViewer::PositionPageDisplays() {
    // Calculate display area accounting for scrollbars
    Rect2Di displayArea = pageArea;
    
    bool needHScroll = false;
    bool needVScroll = false;
    
    // Check if scrollbars are needed based on zoom level
    if (state.isLoaded && state.zoomMode == EBookZoomMode::Custom) {
        // Calculate page size at current zoom
        float pageWidth = renderSettings.pageSize.GetWidthInPixels() * state.zoomLevel;
        float pageHeight = renderSettings.pageSize.GetHeightInPixels() * state.zoomLevel;
        
        needHScroll = pageWidth > displayArea.width;
        needVScroll = pageHeight > displayArea.height;
    }
    
    // Adjust display area for scrollbars
    if (needVScroll) displayArea.width -= 16;
    if (needHScroll) displayArea.height -= 16;
    
    // Position main page display
    if (pageDisplay) {
        if (state.displayMode == EBookDisplayMode::DoublePage ||
            state.displayMode == EBookDisplayMode::DoublePageCover) {
            // Split display area for double page mode
            long halfWidth = displayArea.width / 2;
            pageDisplay->SetBounds(Rect2Di(displayArea.x, displayArea.y, 
                                           halfWidth, displayArea.height));
            
            if (pageDisplay2) {
                pageDisplay2->SetBounds(Rect2Di(displayArea.x + halfWidth, displayArea.y,
                                                halfWidth, displayArea.height));
                pageDisplay2->SetVisible(true);
            }
        } else {
            pageDisplay->SetBounds(displayArea);
            if (pageDisplay2) {
                pageDisplay2->SetVisible(false);
            }
        }
    }
    
    // Position scrollbars
    if (horizontalScrollbar) {
        horizontalScrollbar->SetBounds(Rect2Di(pageArea.x, pageArea.y + pageArea.height - 16,
                                               pageArea.width - (needVScroll ? 16 : 0), 16));
        horizontalScrollbar->SetVisible(needHScroll);
    }
    
    if (verticalScrollbar) {
        verticalScrollbar->SetBounds(Rect2Di(pageArea.x + pageArea.width - 16, pageArea.y,
                                             16, pageArea.height - (needHScroll ? 16 : 0)));
        verticalScrollbar->SetVisible(needVScroll);
    }
}

void UltraCanvasEBookViewer::PositionStatusBar() {
    if (loadingProgress) {
        loadingProgress->SetBounds(Rect2Di(statusArea.x, statusArea.y, statusArea.width, 4));
    }
    
    if (statusLabel) {
        statusLabel->SetBounds(Rect2Di(statusArea.x + 8, statusArea.y + 4, 
                                       statusArea.width - 16, statusArea.height - 4));
    }
}

// ============================================================================
// DOCUMENT OPERATIONS
// ============================================================================

bool UltraCanvasEBookViewer::LoadDocument(const std::string& filePath, const std::string& password) {
    CloseDocument();
    
    // Show loading state
    state.isLoading = true;
    if (loadingProgress) {
        loadingProgress->SetVisible(true);
        loadingProgress->SetValue(0.0f);
    }
    UpdateStatusBar();
    
    // Detect format from file extension
    EBookFormat format = DetectFormatFromPath(filePath);
    
    // Create appropriate engine
    engine = CreateEngineForFormat(format);
    if (!engine) {
        EmitError("Unsupported eBook format");
        state.isLoading = false;
        if (loadingProgress) loadingProgress->SetVisible(false);
        return false;
    }
    
    // Set progress callback
    engine->SetProgressCallback([this](float progress, const std::string& status) {
        if (loadingProgress) {
            loadingProgress->SetValue(progress);
        }
        if (statusLabel) {
            statusLabel->SetText(status);
        }
        if (onLoadProgress) {
            onLoadProgress(progress, status);
        }
    });
    
    // Load document
    if (!engine->LoadFile(filePath, password)) {
        EmitError("Failed to load document: " + engine->GetLastError());
        engine.reset();
        state.isLoading = false;
        if (loadingProgress) loadingProgress->SetVisible(false);
        return false;
    }
    
    // Get document info
    OnDocumentLoaded();
    
    return true;
}

bool UltraCanvasEBookViewer::LoadDocumentFromMemory(const std::vector<uint8_t>& data,
                                                     EBookFormat format,
                                                     const std::string& password) {
    CloseDocument();
    
    state.isLoading = true;
    if (loadingProgress) {
        loadingProgress->SetVisible(true);
        loadingProgress->SetValue(0.0f);
    }
    
    engine = CreateEngineForFormat(format);
    if (!engine) {
        EmitError("Unsupported eBook format");
        state.isLoading = false;
        if (loadingProgress) loadingProgress->SetVisible(false);
        return false;
    }
    
    if (!engine->LoadFromMemory(data, password)) {
        EmitError("Failed to load document from memory");
        engine.reset();
        state.isLoading = false;
        if (loadingProgress) loadingProgress->SetVisible(false);
        return false;
    }
    
    OnDocumentLoaded();
    return true;
}

void UltraCanvasEBookViewer::CloseDocument() {
    if (engine) {
        engine->Close();
        engine.reset();
    }
    
    // Reset state
    state.isLoaded = false;
    state.isLoading = false;
    state.currentPage = 1;
    state.totalPages = 0;
    state.currentChapter = -1;
    
    // Clear data
    metadata = EBookMetadata();
    pageInfos.clear();
    tableOfContents.clear();
    bookmarks.clear();
    annotations.clear();
    searchResults.clear();
    
    // Clear UI
    ClearDisplay();
    if (tocPanel) tocPanel->Clear();
    if (bookmarkPanel) bookmarkPanel->Clear();
    
    UpdateNavigationButtons();
    UpdateStatusBar();
    
    EmitViewerEvent(EBookViewerEvent::DocumentClosed);
}

bool UltraCanvasEBookViewer::IsDocumentLoaded() const {
    return state.isLoaded && engine != nullptr;
}

EBookFormat UltraCanvasEBookViewer::DetectFormatFromPath(const std::string& filePath) const {
    std::string ext = filePath.substr(filePath.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == "epub") return EBookFormat::EPUB;
    if (ext == "fb2") return EBookFormat::FB2;
    if (ext == "mobi") return EBookFormat::MOBI;
    if (ext == "azw" || ext == "azw3") return EBookFormat::AZW3;
    if (ext == "pdf") return EBookFormat::PDF;
    if (ext == "cbz") return EBookFormat::CBZ;
    if (ext == "cbr") return EBookFormat::CBR;
    if (ext == "djvu") return EBookFormat::DJVU;
    if (ext == "txt") return EBookFormat::TXT;
    if (ext == "rtf") return EBookFormat::RTF;
    if (ext == "html" || ext == "htm") return EBookFormat::HTML;
    
    return EBookFormat::Unknown;
}

std::shared_ptr<IEBookEngine> UltraCanvasEBookViewer::CreateEngineForFormat(EBookFormat format) {
    switch (format) {
        case EBookFormat::EPUB:
            return std::make_shared<EPUBEngine>();
        case EBookFormat::FB2:
            return std::make_shared<FB2Engine>();
        // Add other engines as they become available
        default:
            return nullptr;
    }
}

void UltraCanvasEBookViewer::OnDocumentLoaded() {
    state.isLoading = false;
    state.isLoaded = true;
    
    if (loadingProgress) {
        loadingProgress->SetVisible(false);
    }
    
    // Get metadata
    if (engine) {
        metadata = engine->GetMetadata();
        state.totalPages = engine->GetPageCount();
        tableOfContents = engine->GetTableOfContents();
        
        // Get page info
        pageInfos.clear();
        for (int i = 1; i <= state.totalPages; ++i) {
            pageInfos.push_back(engine->GetPageInfo(i));
        }
    }
    
    // Update TOC panel
    if (tocPanel) {
        tocPanel->SetTableOfContents(tableOfContents);
    }
    
    // Go to first page
    GoToPage(1);
    
    // Apply initial zoom
    ZoomToFit();
    
    // Update UI
    UpdateNavigationButtons();
    UpdatePageNumberInput();
    UpdateZoomLabel();
    UpdateStatusBar();
    
    // Load thumbnails asynchronously
    LoadThumbnails();
    
    EmitViewerEvent(EBookViewerEvent::DocumentLoaded);
}

void UltraCanvasEBookViewer::OnDocumentError(const std::string& error) {
    state.isLoading = false;
    
    if (loadingProgress) {
        loadingProgress->SetVisible(false);
    }
    
    if (statusLabel) {
        statusLabel->SetText("Error: " + error);
    }
    
    EmitError(error);
}

// ============================================================================
// NAVIGATION
// ============================================================================

void UltraCanvasEBookViewer::GoToPage(int pageNumber) {
    if (!state.isLoaded || pageNumber < 1 || pageNumber > state.totalPages) {
        return;
    }
    
    state.currentPage = pageNumber;
    
    // Update page display
    UpdatePageDisplay();
    
    // Update chapter info
    UpdateCurrentChapter();
    
    // Update UI
    UpdateNavigationButtons();
    UpdatePageNumberInput();
    UpdateThumbnailSelection();
    UpdateStatusBar();
    
    // Emit event
    if (onPageChanged) {
        onPageChanged(state.currentPage, state.totalPages);
    }
    
    EmitViewerEvent(EBookViewerEvent::PageChanged);
}

void UltraCanvasEBookViewer::GoToFirstPage() {
    GoToPage(1);
}

void UltraCanvasEBookViewer::GoToLastPage() {
    GoToPage(state.totalPages);
}

void UltraCanvasEBookViewer::GoToNextPage() {
    int step = (state.displayMode == EBookDisplayMode::DoublePage ||
                state.displayMode == EBookDisplayMode::DoublePageCover) ? 2 : 1;
    GoToPage(std::min(state.currentPage + step, state.totalPages));
}

void UltraCanvasEBookViewer::GoToPreviousPage() {
    int step = (state.displayMode == EBookDisplayMode::DoublePage ||
                state.displayMode == EBookDisplayMode::DoublePageCover) ? 2 : 1;
    GoToPage(std::max(state.currentPage - step, 1));
}

void UltraCanvasEBookViewer::GoToChapter(int chapterIndex) {
    if (chapterIndex < 0 || chapterIndex >= static_cast<int>(tableOfContents.size())) {
        return;
    }
    
    int pageNumber = tableOfContents[chapterIndex].pageNumber;
    GoToPage(pageNumber);
    
    if (onChapterChanged) {
        onChapterChanged(chapterIndex, tableOfContents[chapterIndex].title);
    }
}

void UltraCanvasEBookViewer::GoToProgress(float progress) {
    progress = std::clamp(progress, 0.0f, 1.0f);
    int pageNumber = static_cast<int>(progress * state.totalPages);
    GoToPage(std::max(1, pageNumber));
}

int UltraCanvasEBookViewer::GetCurrentPage() const {
    return state.currentPage;
}

int UltraCanvasEBookViewer::GetTotalPages() const {
    return state.totalPages;
}

float UltraCanvasEBookViewer::GetProgress() const {
    if (state.totalPages <= 0) return 0.0f;
    return static_cast<float>(state.currentPage) / static_cast<float>(state.totalPages);
}

void UltraCanvasEBookViewer::UpdateCurrentChapter() {
    if (tableOfContents.empty()) {
        state.currentChapter = -1;
        return;
    }
    
    // Find the chapter containing the current page
    int chapterIndex = -1;
    for (int i = 0; i < static_cast<int>(tableOfContents.size()); ++i) {
        if (tableOfContents[i].pageNumber <= state.currentPage) {
            chapterIndex = i;
        } else {
            break;
        }
    }
    
    if (chapterIndex != state.currentChapter) {
        state.currentChapter = chapterIndex;
        if (tocPanel) {
            tocPanel->SetCurrentChapter(chapterIndex);
        }
    }
}

// ============================================================================
// ZOOM
// ============================================================================

void UltraCanvasEBookViewer::SetZoom(float zoom) {
    zoom = std::clamp(zoom, 0.1f, 10.0f);
    
    if (std::abs(state.zoomLevel - zoom) < 0.001f) {
        return;
    }
    
    state.zoomLevel = zoom;
    state.zoomMode = EBookZoomMode::Custom;
    
    UpdatePageDisplay();
    UpdateZoomLabel();
    LayoutComponents();
    
    if (onZoomChanged) {
        onZoomChanged(state.zoomLevel, state.zoomMode);
    }
    
    EmitViewerEvent(EBookViewerEvent::ZoomChanged);
}

void UltraCanvasEBookViewer::SetZoomMode(EBookZoomMode mode) {
    state.zoomMode = mode;
    UpdateZoomSettings();
}

float UltraCanvasEBookViewer::GetZoom() const {
    return state.zoomLevel;
}

EBookZoomMode UltraCanvasEBookViewer::GetZoomMode() const {
    return state.zoomMode;
}

void UltraCanvasEBookViewer::ZoomIn() {
    // Preset zoom levels
    static const float zoomLevels[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f };
    
    float currentZoom = state.zoomLevel;
    for (float level : zoomLevels) {
        if (level > currentZoom + 0.01f) {
            SetZoom(level);
            return;
        }
    }
}

void UltraCanvasEBookViewer::ZoomOut() {
    static const float zoomLevels[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f };
    
    float currentZoom = state.zoomLevel;
    for (int i = sizeof(zoomLevels)/sizeof(zoomLevels[0]) - 1; i >= 0; --i) {
        if (zoomLevels[i] < currentZoom - 0.01f) {
            SetZoom(zoomLevels[i]);
            return;
        }
    }
}

void UltraCanvasEBookViewer::ZoomToFit() {
    state.zoomMode = EBookZoomMode::FitPage;
    state.zoomLevel = CalculateFitZoom();
    
    UpdatePageDisplay();
    UpdateZoomLabel();
    LayoutComponents();
    
    if (onZoomChanged) {
        onZoomChanged(state.zoomLevel, state.zoomMode);
    }
}

void UltraCanvasEBookViewer::ZoomToFitWidth() {
    state.zoomMode = EBookZoomMode::FitWidth;
    state.zoomLevel = CalculateFitWidthZoom();
    
    UpdatePageDisplay();
    UpdateZoomLabel();
    LayoutComponents();
    
    if (onZoomChanged) {
        onZoomChanged(state.zoomLevel, state.zoomMode);
    }
}

void UltraCanvasEBookViewer::ZoomToFitHeight() {
    state.zoomMode = EBookZoomMode::FitHeight;
    state.zoomLevel = CalculateFitHeightZoom();
    
    UpdatePageDisplay();
    UpdateZoomLabel();
    LayoutComponents();
    
    if (onZoomChanged) {
        onZoomChanged(state.zoomLevel, state.zoomMode);
    }
}

void UltraCanvasEBookViewer::ZoomToActualSize() {
    SetZoom(1.0f);
    state.zoomMode = EBookZoomMode::ActualSize;
}

float UltraCanvasEBookViewer::CalculateFitZoom() const {
    if (!state.isLoaded) return 1.0f;
    
    float pageWidth = renderSettings.pageSize.GetWidthInPixels();
    float pageHeight = renderSettings.pageSize.GetHeightInPixels();
    
    float zoomX = static_cast<float>(pageArea.width) / pageWidth;
    float zoomY = static_cast<float>(pageArea.height) / pageHeight;
    
    return std::min(zoomX, zoomY) * 0.95f; // 95% to add some margin
}

float UltraCanvasEBookViewer::CalculateFitWidthZoom() const {
    if (!state.isLoaded) return 1.0f;
    
    float pageWidth = renderSettings.pageSize.GetWidthInPixels();
    return (static_cast<float>(pageArea.width) / pageWidth) * 0.95f;
}

float UltraCanvasEBookViewer::CalculateFitHeightZoom() const {
    if (!state.isLoaded) return 1.0f;
    
    float pageHeight = renderSettings.pageSize.GetHeightInPixels();
    return (static_cast<float>(pageArea.height) / pageHeight) * 0.95f;
}

void UltraCanvasEBookViewer::UpdateZoomSettings() {
    switch (state.zoomMode) {
        case EBookZoomMode::FitPage:
            state.zoomLevel = CalculateFitZoom();
            break;
        case EBookZoomMode::FitWidth:
            state.zoomLevel = CalculateFitWidthZoom();
            break;
        case EBookZoomMode::FitHeight:
            state.zoomLevel = CalculateFitHeightZoom();
            break;
        case EBookZoomMode::ActualSize:
            state.zoomLevel = 1.0f;
            break;
        default:
            break;
    }
    
    UpdatePageDisplay();
    UpdateZoomLabel();
    LayoutComponents();
}

// ============================================================================
// DISPLAY MODE
// ============================================================================

void UltraCanvasEBookViewer::SetDisplayMode(EBookDisplayMode mode) {
    if (state.displayMode == mode) return;
    
    state.displayMode = mode;
    
    // Update page display visibility
    bool showSecondPage = (mode == EBookDisplayMode::DoublePage ||
                           mode == EBookDisplayMode::DoublePageCover);
    if (pageDisplay2) {
        pageDisplay2->SetVisible(showSecondPage);
    }
    
    UpdatePageDisplay();
    LayoutComponents();
    
    EmitViewerEvent(EBookViewerEvent::DisplayModeChanged);
}

EBookDisplayMode UltraCanvasEBookViewer::GetDisplayMode() const {
    return state.displayMode;
}

void UltraCanvasEBookViewer::SetReadingMode(EBookReadingMode mode) {
    state.readingMode = mode;
    
    // Update colors based on reading mode
    switch (mode) {
        case EBookReadingMode::Normal:
            renderSettings.backgroundColor = Color(255, 255, 255, 255);
            renderSettings.textColor = Color(0, 0, 0, 255);
            break;
        case EBookReadingMode::Sepia:
            renderSettings.backgroundColor = Color(250, 240, 220, 255);
            renderSettings.textColor = Color(90, 70, 50, 255);
            break;
        case EBookReadingMode::Night:
            renderSettings.backgroundColor = Color(30, 30, 30, 255);
            renderSettings.textColor = Color(200, 200, 200, 255);
            break;
        case EBookReadingMode::Gray:
            renderSettings.backgroundColor = Color(230, 230, 230, 255);
            renderSettings.textColor = Color(50, 50, 50, 255);
            break;
        case EBookReadingMode::HighContrast:
            renderSettings.backgroundColor = Color(0, 0, 0, 255);
            renderSettings.textColor = Color(255, 255, 255, 255);
            break;
        default:
            break;
    }
    
    UpdatePageDisplay();
    RequestRedraw();
}

EBookReadingMode UltraCanvasEBookViewer::GetReadingMode() const {
    return state.readingMode;
}

// ============================================================================
// PAGE DISPLAY
// ============================================================================

void UltraCanvasEBookViewer::UpdatePageDisplay() {
    if (!state.isLoaded || !engine) {
        ClearDisplay();
        return;
    }
    
    // Render current page
    std::vector<uint8_t> pageData = engine->RenderPageToImage(
        state.currentPage,
        static_cast<int>(renderSettings.pageSize.GetWidthInPixels() * state.zoomLevel),
        static_cast<int>(renderSettings.pageSize.GetHeightInPixels() * state.zoomLevel),
        renderSettings.dpi * state.zoomLevel
    );
    
    if (!pageData.empty() && pageDisplay) {
        pageDisplay->SetImageData(pageData);
        pageDisplay->SetBackgroundColor(renderSettings.backgroundColor);
    }
    
    // Render second page for double page mode
    if ((state.displayMode == EBookDisplayMode::DoublePage ||
         state.displayMode == EBookDisplayMode::DoublePageCover) &&
        state.currentPage + 1 <= state.totalPages) {
        
        std::vector<uint8_t> page2Data = engine->RenderPageToImage(
            state.currentPage + 1,
            static_cast<int>(renderSettings.pageSize.GetWidthInPixels() * state.zoomLevel),
            static_cast<int>(renderSettings.pageSize.GetHeightInPixels() * state.zoomLevel),
            renderSettings.dpi * state.zoomLevel
        );
        
        if (!page2Data.empty() && pageDisplay2) {
            pageDisplay2->SetImageData(page2Data);
            pageDisplay2->SetBackgroundColor(renderSettings.backgroundColor);
        }
    }
    
    RequestRedraw();
}

void UltraCanvasEBookViewer::ClearDisplay() {
    if (pageDisplay) {
        pageDisplay->ClearImage();
    }
    if (pageDisplay2) {
        pageDisplay2->ClearImage();
    }
}

// ============================================================================
// UI UPDATES
// ============================================================================

void UltraCanvasEBookViewer::UpdateNavigationButtons() {
    bool hasDoc = state.isLoaded;
    bool canGoPrev = hasDoc && state.currentPage > 1;
    bool canGoNext = hasDoc && state.currentPage < state.totalPages;
    
    if (btnFirstPage) btnFirstPage->SetEnabled(canGoPrev);
    if (btnPrevPage) btnPrevPage->SetEnabled(canGoPrev);
    if (btnNextPage) btnNextPage->SetEnabled(canGoNext);
    if (btnLastPage) btnLastPage->SetEnabled(canGoNext);
}

void UltraCanvasEBookViewer::UpdatePageNumberInput() {
    if (pageNumberInput) {
        pageNumberInput->SetText(std::to_string(state.currentPage));
    }
    if (pageCountLabel) {
        pageCountLabel->SetText("/ " + std::to_string(state.totalPages));
    }
}

void UltraCanvasEBookViewer::UpdateZoomLabel() {
    if (zoomLabel) {
        int percent = static_cast<int>(state.zoomLevel * 100.0f);
        zoomLabel->SetText(std::to_string(percent) + "%");
    }
}

void UltraCanvasEBookViewer::UpdateStatusBar() {
    if (!statusLabel) return;
    
    if (!state.isLoaded) {
        statusLabel->SetText("No document loaded");
        return;
    }
    
    if (state.isLoading) {
        statusLabel->SetText("Loading...");
        return;
    }
    
    std::stringstream ss;
    ss << "Page " << state.currentPage << " of " << state.totalPages;
    
    if (!metadata.title.empty()) {
        ss << " | " << metadata.title;
    }
    
    if (state.currentChapter >= 0 && state.currentChapter < static_cast<int>(tableOfContents.size())) {
        ss << " | " << tableOfContents[state.currentChapter].title;
    }
    
    statusLabel->SetText(ss.str());
}

void UltraCanvasEBookViewer::UpdateThumbnailSelection() {
    // Update thumbnail selection in panel
    // This would iterate through thumbnail items and highlight current page
}

// ============================================================================
// THUMBNAILS
// ============================================================================

void UltraCanvasEBookViewer::LoadThumbnails() {
    if (!state.isLoaded || !engine) return;
    
    // Clear existing thumbnails
    thumbnailTasks.clear();
    
    // Generate thumbnails asynchronously
    for (int i = 1; i <= state.totalPages; ++i) {
        auto task = std::async(std::launch::async, [this, i]() {
            LoadThumbnailAsync(i);
        });
        thumbnailTasks.push_back(std::move(task));
    }
}

void UltraCanvasEBookViewer::LoadThumbnailAsync(int pageNumber) {
    if (!engine) return;
    
    // Render small thumbnail
    std::vector<uint8_t> thumbData = engine->RenderPageToImage(
        pageNumber, 120, 160, 72.0f);
    
    // Store thumbnail data
    std::lock_guard<std::mutex> lock(viewerMutex);
    // Update thumbnail panel with new data
}

// ============================================================================
// PANEL TOGGLES
// ============================================================================

void UltraCanvasEBookViewer::ToggleTOCPanel() {
    state.showTOC = !state.showTOC;
    if (state.showTOC) {
        state.showThumbnails = false;
        state.showBookmarks = false;
    }
    LayoutComponents();
    RequestRedraw();
}

void UltraCanvasEBookViewer::ToggleThumbnailPanel() {
    state.showThumbnails = !state.showThumbnails;
    if (state.showThumbnails) {
        state.showTOC = false;
        state.showBookmarks = false;
    }
    LayoutComponents();
    RequestRedraw();
}

void UltraCanvasEBookViewer::ToggleBookmarkPanel() {
    state.showBookmarks = !state.showBookmarks;
    if (state.showBookmarks) {
        state.showTOC = false;
        state.showThumbnails = false;
    }
    LayoutComponents();
    RequestRedraw();
}

void UltraCanvasEBookViewer::ShowToolbar(bool show) {
    state.showToolbar = show;
    LayoutComponents();
    RequestRedraw();
}

void UltraCanvasEBookViewer::ShowTOC(bool show) {
    state.showTOC = show;
    if (show) {
        state.showThumbnails = false;
        state.showBookmarks = false;
    }
    LayoutComponents();
    RequestRedraw();
}

void UltraCanvasEBookViewer::ShowThumbnails(bool show) {
    state.showThumbnails = show;
    if (show) {
        state.showTOC = false;
        state.showBookmarks = false;
    }
    LayoutComponents();
    RequestRedraw();
}

void UltraCanvasEBookViewer::ShowBookmarks(bool show) {
    state.showBookmarks = show;
    if (show) {
        state.showTOC = false;
        state.showThumbnails = false;
    }
    LayoutComponents();
    RequestRedraw();
}

// ============================================================================
// BOOKMARKS
// ============================================================================

void UltraCanvasEBookViewer::AddBookmark(const std::string& title) {
    EBookBookmark bookmark;
    bookmark.pageNumber = state.currentPage;
    bookmark.title = title.empty() ? ("Page " + std::to_string(state.currentPage)) : title;
    bookmark.timestamp = std::chrono::system_clock::now();
    
    // Check if bookmark already exists for this page
    auto it = std::find_if(bookmarks.begin(), bookmarks.end(),
        [this](const EBookBookmark& bm) { return bm.pageNumber == state.currentPage; });
    
    if (it != bookmarks.end()) {
        *it = bookmark; // Update existing
    } else {
        bookmarks.push_back(bookmark);
    }
    
    // Sort by page number
    std::sort(bookmarks.begin(), bookmarks.end(),
        [](const EBookBookmark& a, const EBookBookmark& b) {
            return a.pageNumber < b.pageNumber;
        });
    
    if (bookmarkPanel) {
        bookmarkPanel->SetBookmarks(bookmarks);
    }
    
    if (onBookmarkAdded) {
        onBookmarkAdded(bookmark);
    }
}

void UltraCanvasEBookViewer::RemoveBookmark(int pageNumber) {
    auto it = std::remove_if(bookmarks.begin(), bookmarks.end(),
        [pageNumber](const EBookBookmark& bm) { return bm.pageNumber == pageNumber; });
    
    if (it != bookmarks.end()) {
        bookmarks.erase(it, bookmarks.end());
        
        if (bookmarkPanel) {
            bookmarkPanel->SetBookmarks(bookmarks);
        }
        
        if (onBookmarkRemoved) {
            onBookmarkRemoved(pageNumber);
        }
    }
}

bool UltraCanvasEBookViewer::IsBookmarked(int pageNumber) const {
    return std::any_of(bookmarks.begin(), bookmarks.end(),
        [pageNumber](const EBookBookmark& bm) { return bm.pageNumber == pageNumber; });
}

const std::vector<EBookBookmark>& UltraCanvasEBookViewer::GetBookmarks() const {
    return bookmarks;
}

// ============================================================================
// SEARCH
// ============================================================================

void UltraCanvasEBookViewer::Search(const std::string& query) {
    if (!state.isLoaded || !engine || query.empty()) {
        searchResults.clear();
        return;
    }
    
    searchResults = engine->Search(query);
    state.currentSearchResult = searchResults.empty() ? -1 : 0;
    
    if (onSearchCompleted) {
        onSearchCompleted(query, static_cast<int>(searchResults.size()));
    }
    
    // Navigate to first result
    if (!searchResults.empty()) {
        GoToPage(searchResults[0].pageNumber);
    }
}

void UltraCanvasEBookViewer::FindNext() {
    if (searchResults.empty()) return;
    
    state.currentSearchResult = (state.currentSearchResult + 1) % searchResults.size();
    const auto& result = searchResults[state.currentSearchResult];
    GoToPage(result.pageNumber);
    
    if (onSearchResultSelected) {
        onSearchResultSelected(state.currentSearchResult, result);
    }
}

void UltraCanvasEBookViewer::FindPrevious() {
    if (searchResults.empty()) return;
    
    state.currentSearchResult = (state.currentSearchResult - 1 + searchResults.size()) % searchResults.size();
    const auto& result = searchResults[state.currentSearchResult];
    GoToPage(result.pageNumber);
    
    if (onSearchResultSelected) {
        onSearchResultSelected(state.currentSearchResult, result);
    }
}

void UltraCanvasEBookViewer::ClearSearch() {
    searchResults.clear();
    state.currentSearchResult = -1;
    RequestRedraw();
}

// ============================================================================
// METADATA & INFO
// ============================================================================

const EBookMetadata& UltraCanvasEBookViewer::GetMetadata() const {
    return metadata;
}

const std::vector<EBookTOCEntry>& UltraCanvasEBookViewer::GetTableOfContents() const {
    return tableOfContents;
}

// ============================================================================
// SETTINGS
// ============================================================================

void UltraCanvasEBookViewer::SetPageFormat(DocumentPageFormat format) {
    renderSettings.pageFormat = format;
    renderSettings.pageSize = DocumentPageSize::FromFormat(format, renderSettings.dpi);
    
    UpdateZoomSettings();
    UpdatePageDisplay();
}

void UltraCanvasEBookViewer::SetCustomPageSize(float widthMM, float heightMM) {
    renderSettings.pageSize = DocumentPageSize::FromMillimeters(widthMM, heightMM, renderSettings.dpi);
    renderSettings.pageFormat = DocumentPageFormat::Custom;
    
    UpdateZoomSettings();
    UpdatePageDisplay();
}

void UltraCanvasEBookViewer::SetRenderSettings(const EBookRenderSettings& settings) {
    renderSettings = settings;
    UpdatePageDisplay();
}

const EBookRenderSettings& UltraCanvasEBookViewer::GetRenderSettings() const {
    return renderSettings;
}

// ============================================================================
// EVENTS
// ============================================================================

void UltraCanvasEBookViewer::EmitViewerEvent(EBookViewerEvent::Type type) {
    if (!onViewerEvent) return;
    
    EBookViewerEvent event(type);
    event.pageNumber = state.currentPage;
    event.totalPages = state.totalPages;
    event.chapterIndex = state.currentChapter;
    event.zoomLevel = state.zoomLevel;
    event.progress = GetProgress();
    event.zoomMode = state.zoomMode;
    event.displayMode = state.displayMode;
    
    if (state.currentChapter >= 0 && state.currentChapter < static_cast<int>(tableOfContents.size())) {
        event.chapterTitle = tableOfContents[state.currentChapter].title;
    }
    
    onViewerEvent(event);
}

void UltraCanvasEBookViewer::EmitError(const std::string& error) {
    if (onError) {
        onError(error);
    }
    
    if (onViewerEvent) {
        EBookViewerEvent event(EBookViewerEvent::Error);
        event.message = error;
        onViewerEvent(event);
    }
}

// ============================================================================
// RENDERING
// ============================================================================

void UltraCanvasEBookViewer::Render(IRenderContext* ctx) {
    if (!IsVisible() || !ctx) return;
    
    ctx->PushState();
    
    // Background
    ctx->DrawFilledRectangle(GetBounds(), Color(240, 240, 240, 255));
    
    // Render toolbar
    if (toolbar && state.showToolbar) {
        toolbar->Render(ctx);
    }
    
    // Render side panels
    if (tocPanel && state.showTOC) {
        tocPanel->Render(ctx);
    }
    if (thumbnailPanel && state.showThumbnails) {
        thumbnailPanel->Render(ctx);
    }
    if (bookmarkPanel && state.showBookmarks) {
        bookmarkPanel->Render(ctx);
    }
    
    // Render page display area background
    ctx->DrawFilledRectangle(pageArea, renderSettings.backgroundColor);
    
    // Render page displays
    if (pageDisplay) {
        pageDisplay->Render(ctx);
    }
    if (pageDisplay2 && pageDisplay2->IsVisible()) {
        pageDisplay2->Render(ctx);
    }
    
    // Render scrollbars
    if (horizontalScrollbar && horizontalScrollbar->IsVisible()) {
        horizontalScrollbar->Render(ctx);
    }
    if (verticalScrollbar && verticalScrollbar->IsVisible()) {
        verticalScrollbar->Render(ctx);
    }
    
    // Render status bar
    if (loadingProgress && loadingProgress->IsVisible()) {
        loadingProgress->Render(ctx);
    }
    if (statusLabel) {
        statusLabel->Render(ctx);
    }
    
    ctx->PopState();
}

// ============================================================================
// EVENT HANDLING
// ============================================================================

bool UltraCanvasEBookViewer::OnEvent(const UCEvent& event) {
    if (IsDisabled() || !IsVisible()) return false;
    
    // Forward to toolbar
    if (toolbar && state.showToolbar && toolbar->Contains(event.x, event.y)) {
        if (toolbar->OnEvent(event)) return true;
    }
    
    // Forward to side panels
    if (state.showTOC && tocPanel && tocPanel->Contains(event.x, event.y)) {
        if (tocPanel->OnEvent(event)) return true;
    }
    if (state.showThumbnails && thumbnailPanel && thumbnailPanel->Contains(event.x, event.y)) {
        if (thumbnailPanel->OnEvent(event)) return true;
    }
    if (state.showBookmarks && bookmarkPanel && bookmarkPanel->Contains(event.x, event.y)) {
        if (bookmarkPanel->OnEvent(event)) return true;
    }
    
    // Handle keyboard events
    if (event.type == UCEventType::KeyDown) {
        if (HandleKeyboardNavigation(event)) return true;
        if (HandleKeyboardZoom(event)) return true;
        if (HandleKeyboardShortcuts(event)) return true;
    }
    
    // Handle mouse events in page area
    if (pageArea.Contains(Point2Di(event.x, event.y))) {
        if (HandleMouseNavigation(event)) return true;
        if (HandleMouseZoom(event)) return true;
    }
    
    return UltraCanvasUIElement::OnEvent(event);
}

bool UltraCanvasEBookViewer::HandleKeyboardNavigation(const UCEvent& event) {
    switch (event.key.code) {
        case UCKeys::Left:
        case UCKeys::PageUp:
            GoToPreviousPage();
            return true;
            
        case UCKeys::Right:
        case UCKeys::PageDown:
            GoToNextPage();
            return true;
            
        case UCKeys::Home:
            GoToFirstPage();
            return true;
            
        case UCKeys::End:
            GoToLastPage();
            return true;
            
        default:
            break;
    }
    return false;
}

bool UltraCanvasEBookViewer::HandleKeyboardZoom(const UCEvent& event) {
    bool ctrl = event.key.modifiers & UCKeyModifiers::Control;
    
    if (ctrl) {
        switch (event.key.code) {
            case UCKeys::Plus:
            case UCKeys::NumpadPlus:
                ZoomIn();
                return true;
                
            case UCKeys::Minus:
            case UCKeys::NumpadMinus:
                ZoomOut();
                return true;
                
            case UCKeys::Num0:
                ZoomToFit();
                return true;
                
            case UCKeys::Num1:
                ZoomToFitWidth();
                return true;
                
            case UCKeys::Num2:
                ZoomToActualSize();
                return true;
                
            default:
                break;
        }
    }
    return false;
}

bool UltraCanvasEBookViewer::HandleKeyboardShortcuts(const UCEvent& event) {
    bool ctrl = event.key.modifiers & UCKeyModifiers::Control;
    
    if (ctrl) {
        switch (event.key.code) {
            case UCKeys::F:
                // Open search (would need search UI)
                return true;
                
            case UCKeys::G:
                FindNext();
                return true;
                
            case UCKeys::B:
                AddBookmark("");
                return true;
                
            default:
                break;
        }
    }
    
    switch (event.key.code) {
        case UCKeys::F11:
            // Toggle fullscreen (would need window support)
            return true;
            
        case UCKeys::Escape:
            ClearSearch();
            return true;
            
        default:
            break;
    }
    
    return false;
}

bool UltraCanvasEBookViewer::HandleMouseNavigation(const UCEvent& event) {
    if (event.type == UCEventType::MouseWheel) {
        bool ctrl = event.key.modifiers & UCKeyModifiers::Control;
        
        if (ctrl) {
            // Zoom with Ctrl+Wheel
            if (event.mouse.wheelDelta > 0) {
                ZoomIn();
            } else {
                ZoomOut();
            }
            return true;
        } else {
            // Scroll navigation
            if (event.mouse.wheelDelta > 0) {
                GoToPreviousPage();
            } else {
                GoToNextPage();
            }
            return true;
        }
    }
    return false;
}

bool UltraCanvasEBookViewer::HandleMouseZoom(const UCEvent& event) {
    // Double-click to toggle fit/actual size
    if (event.type == UCEventType::MouseDoubleClick) {
        if (state.zoomMode == EBookZoomMode::ActualSize) {
            ZoomToFit();
        } else {
            ZoomToActualSize();
        }
        return true;
    }
    return false;
}

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

std::shared_ptr<UltraCanvasEBookViewer> CreateEBookViewer(
    const std::string& id, long x, long y, long w, long h) {
    static long nextUid = 50000;
    return std::make_shared<UltraCanvasEBookViewer>(id, nextUid++, x, y, w, h);
}

std::shared_ptr<UltraCanvasEBookViewer> CreateEBookViewerWithID(
    const std::string& id, long uid, long x, long y, long w, long h) {
    return std::make_shared<UltraCanvasEBookViewer>(id, uid, x, y, w, h);
}

} // namespace UltraCanvas
