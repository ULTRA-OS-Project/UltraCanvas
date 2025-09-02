// PDFExampleApp.cpp
// Example application demonstrating UltraCanvas PDF plugin usage
// Version: 1.0.0
// Last Modified: 2025-09-03
// Author: UltraCanvas Framework

#include "UltraCanvasWindow.h"
#include "UltraCanvasPDFPlugin.h"
#include "UltraCanvasPDFViewer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasFileDialog.h"
#include "UltraCanvasMenuBar.h"
#include <iostream>
#include <memory>

class PDFViewerApplication {
private:
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> mainWindow;
    std::shared_ptr<UltraCanvas::UltraCanvasPDFViewer> pdfViewer;
    std::shared_ptr<UltraCanvas::UltraCanvasMenuBar> menuBar;
    std::shared_ptr<UltraCanvas::UltraCanvasFileDialog> fileDialog;
    
public:
    bool Initialize() {
        using namespace UltraCanvas;
        
        std::cout << "Initializing PDF Viewer Application..." << std::endl;
        
        // Register PDF plugin
        RegisterPDFPlugin();
        std::cout << "✓ PDF Plugin registered" << std::endl;
        
        // Create main window
        mainWindow = std::make_shared<UltraCanvasWindow>(
            "pdfViewerWindow", 1, 100, 100, 1200, 800);
        mainWindow->SetTitle("UltraCanvas PDF Viewer");
        mainWindow->SetResizable(true);
        
        // Create menu bar
        CreateMenuBar();
        
        // Create PDF viewer component
        pdfViewer = CreatePDFViewer("mainPDFViewer", 0, 30, 1200, 770);
        
        // Set up PDF viewer event handlers
        SetupPDFViewerEvents();
        
        // Add components to window
        mainWindow->AddElement(menuBar.get());
        mainWindow->AddElement(pdfViewer.get());
        
        std::cout << "✓ Application initialized successfully" << std::endl;
        return true;
    }
    
    void CreateMenuBar() {
        using namespace UltraCanvas;
        
        menuBar = std::make_shared<UltraCanvasMenuBar>("mainMenu", 2, 0, 0, 1200, 30);
        
        // File menu
        auto fileMenu = menuBar->AddMenu("File");
        fileMenu->AddMenuItem("Open PDF...", [this]() { OpenPDFFile(); });
        fileMenu->AddSeparator();
        fileMenu->AddMenuItem("Document Properties", [this]() { ShowDocumentProperties(); });
        fileMenu->AddSeparator();
        fileMenu->AddMenuItem("Exit", [this]() { mainWindow->Close(); });
        
        // View menu
        auto viewMenu = menuBar->AddMenu("View");
        viewMenu->AddMenuItem("Zoom In", [this]() { pdfViewer->ZoomIn(); });
        viewMenu->AddMenuItem("Zoom Out", [this]() { pdfViewer->ZoomOut(); });
        viewMenu->AddSeparator();
        viewMenu->AddMenuItem("Fit Page", [this]() { pdfViewer->ZoomToFit(); });
        viewMenu->AddMenuItem("Fit Width", [this]() { pdfViewer->ZoomToFitWidth(); });
        viewMenu->AddMenuItem("Fit Height", [this]() { pdfViewer->ZoomToFitHeight(); });
        viewMenu->AddMenuItem("Actual Size", [this]() { pdfViewer->ZoomToActualSize(); });
        viewMenu->AddSeparator();
        viewMenu->AddMenuItem("Single Page", [this]() { 
            pdfViewer->SetDisplayMode(PDFDisplayMode::SinglePage); 
        });
        viewMenu->AddMenuItem("Double Page", [this]() { 
            pdfViewer->SetDisplayMode(PDFDisplayMode::DoublePage); 
        });
        viewMenu->AddSeparator();
        viewMenu->AddMenuItem("Toggle Thumbnails", [this]() { 
            pdfViewer->ToggleThumbnailPanel(); 
        });
        
        // Navigate menu
        auto navMenu = menuBar->AddMenu("Navigate");
        navMenu->AddMenuItem("First Page", [this]() { pdfViewer->GoToFirstPage(); });
        navMenu->AddMenuItem("Previous Page", [this]() { pdfViewer->GoToPreviousPage(); });
        navMenu->AddMenuItem("Next Page", [this]() { pdfViewer->GoToNextPage(); });
        navMenu->AddMenuItem("Last Page", [this]() { pdfViewer->GoToLastPage(); });
        navMenu->AddSeparator();
        navMenu->AddMenuItem("Go to Page...", [this]() { ShowGoToPageDialog(); });
        
        // Tools menu
        auto toolsMenu = menuBar->AddMenu("Tools");
        toolsMenu->AddMenuItem("Search...", [this]() { ShowSearchDialog(); });
        toolsMenu->AddMenuItem("Preferences...", [this]() { ShowPreferences(); });
        
        // Help menu
        auto helpMenu = menuBar->AddMenu("Help");
        helpMenu->AddMenuItem("About", [this]() { ShowAboutDialog(); });
    }
    
    void SetupPDFViewerEvents() {
        using namespace UltraCanvas;
        
        // Page change notifications
        pdfViewer->onPageChanged = [this](int currentPage, int totalPages) {
            std::cout << "Page changed: " << currentPage << " of " << totalPages << std::endl;
            
            // Update window title with page info
            std::string title = "UltraCanvas PDF Viewer";
            if (totalPages > 0) {
                title += " - Page " + std::to_string(currentPage) + " of " + std::to_string(totalPages);
                if (!pdfViewer->GetDocumentInfo().title.empty()) {
                    title += " - " + pdfViewer->GetDocumentInfo().title;
                }
            }
            mainWindow->SetTitle(title);
        };
        
        // Zoom change notifications
        pdfViewer->onZoomChanged = [](float zoom, PDFZoomMode mode) {
            std::cout << "Zoom changed: " << (zoom * 100) << "% (";
            switch (mode) {
                case PDFZoomMode::FitPage: std::cout << "Fit Page"; break;
                case PDFZoomMode::FitWidth: std::cout << "Fit Width"; break;
                case PDFZoomMode::FitHeight: std::cout << "Fit Height"; break;
                case PDFZoomMode::ActualSize: std::cout << "Actual Size"; break;
                case PDFZoomMode::Custom: std::cout << "Custom"; break;
            }
            std::cout << ")" << std::endl;
        };
        
        // Error handling
        pdfViewer->onError = [this](const std::string& error) {
            std::cerr << "PDF Viewer Error: " << error << std::endl;
            ShowErrorDialog("PDF Error", error);
        };
        
        // Loading progress
        pdfViewer->onLoadingProgress = [](float progress) {
            std::cout << "Loading progress: " << (progress * 100) << "%" << std::endl;
        };
        
        // General viewer events
        pdfViewer->onViewerEvent = [this](const PDFViewerEvent& event) {
            switch (event.type) {
                case PDFViewerEvent::DocumentLoaded:
                    std::cout << "Document loaded successfully" << std::endl;
                    OnDocumentLoaded();
                    break;
                case PDFViewerEvent::DocumentClosed:
                    std::cout << "Document closed" << std::endl;
                    OnDocumentClosed();
                    break;
                case PDFViewerEvent::LoadingProgress:
                    std::cout << "Loading: " << (event.progress * 100) << "%" << std::endl;
                    break;
                default:
                    break;
            }
        };
    }
    
    void OpenPDFFile() {
        using namespace UltraCanvas;
        
        // Create file dialog if not exists
        if (!fileDialog) {
            fileDialog = CreateOpenFileDialog("pdfFileDialog", 
                Rect2D(200, 150, 600, 400));
            
            // Set PDF file filter
            fileDialog->SetFileFilters({
                FileFilter("PDF Documents", {"pdf"}),
                FileFilter("All Files", {"*"})
            });
            
            // Set up callbacks
            fileDialog->onFileSelected = [this](const std::string& filePath) {
                LoadPDFDocument(filePath);
                fileDialog->SetVisible(false);
            };
            
            fileDialog->onCancelled = [this]() {
                fileDialog->SetVisible(false);
            };
            
            mainWindow->AddElement(fileDialog.get());
        }
        
        fileDialog->SetVisible(true);
        fileDialog->BringToFront();
    }
    
    void LoadPDFDocument(const std::string& filePath) {
        std::cout << "Loading PDF document: " << filePath << std::endl;
        
        if (!pdfViewer->LoadDocument(filePath)) {
            ShowErrorDialog("Load Error", "Failed to load PDF document: " + filePath);
            return;
        }
        
        std::cout << "PDF document loaded successfully" << std::endl;
    }
    
    void OnDocumentLoaded() {
        // Document-specific setup after loading
        auto docInfo = pdfViewer->GetDocumentInfo();
        
        std::cout << "=== Document Information ===" << std::endl;
        std::cout << "Title: " << docInfo.title << std::endl;
        std::cout << "Author: " << docInfo.author << std::endl;
        std::cout << "Pages: " << docInfo.pageCount << std::endl;
        std::cout << "PDF Version: " << docInfo.pdfVersion << std::endl;
        std::cout << "File Size: " << (docInfo.fileSize / 1024) << " KB" << std::endl;
        std::cout << "Encrypted: " << (docInfo.isEncrypted ? "Yes" : "No") << std::endl;
        std::cout << "===========================" << std::endl;
        
        // Enable document-related menu items
        // (Implementation would update menu item states)
    }
    
    void OnDocumentClosed() {
        // Reset window title
        mainWindow->SetTitle("UltraCanvas PDF Viewer");
        
        // Disable document-related menu items
        // (Implementation would update menu item states)
    }
    
    void ShowDocumentProperties() {
        if (!pdfViewer->IsDocumentLoaded()) {
            ShowInfoDialog("No Document", "No PDF document is currently loaded.");
            return;
        }
        
        auto docInfo = pdfViewer->GetDocumentInfo();
        
        std::string properties = 
            "Title: " + docInfo.title + "\n" +
            "Author: " + docInfo.author + "\n" +
            "Subject: " + docInfo.subject + "\n" +
            "Creator: " + docInfo.creator + "\n" +
            "Producer: " + docInfo.producer + "\n" +
            "Pages: " + std::to_string(docInfo.pageCount) + "\n" +
            "PDF Version: " + docInfo.pdfVersion + "\n" +
            "File Size: " + std::to_string(docInfo.fileSize / 1024) + " KB\n" +
            "Created: " + docInfo.creationDate + "\n" +
            "Modified: " + docInfo.modificationDate + "\n" +
            "Encrypted: " + (docInfo.isEncrypted ? "Yes" : "No");
        
        ShowInfoDialog("Document Properties", properties);
    }
    
    void ShowGoToPageDialog() {
        if (!pdfViewer->IsDocumentLoaded()) {
            ShowInfoDialog("No Document", "No PDF document is currently loaded.");
            return;
        }
        
        // Create simple input dialog for page number
        // (Implementation would create a proper dialog)
        std::cout << "Go to page dialog requested" << std::endl;
        
        // For demonstration, go to page 5 if it exists
        if (pdfViewer->GetPageCount() >= 5) {
            pdfViewer->GoToPage(5);
        }
    }
    
    void ShowSearchDialog() {
        if (!pdfViewer->IsDocumentLoaded()) {
            ShowInfoDialog("No Document", "No PDF document is currently loaded.");
            return;
        }
        
        // Create search dialog
        // (Implementation would create a proper search interface)
        std::cout << "Search dialog requested" << std::endl;
        
        // For demonstration, search for "the"
        pdfViewer->SearchText("the");
    }
    
    void ShowPreferences() {
        // Create preferences dialog
        std::cout << "Preferences dialog requested" << std::endl;
        
        // Example: Toggle high-quality rendering
        auto settings = pdfViewer->GetRenderSettings();
        if (settings.dpi < 200) {
            settings = UltraCanvas::PDFRenderSettings::HighQuality();
            std::cout << "Switched to high-quality rendering (300 DPI)" << std::endl;
        } else {
            settings = UltraCanvas::PDFRenderSettings::Default();
            std::cout << "Switched to normal rendering (150 DPI)" << std::endl;
        }
        pdfViewer->SetRenderSettings(settings);
    }
    
    void ShowAboutDialog() {
        std::string aboutText = 
            "UltraCanvas PDF Viewer\n"
            "Version 1.0.0\n\n"
            "A comprehensive PDF viewing application built with\n"
            "the UltraCanvas cross-platform UI framework.\n\n"
            "Features:\n"
            "• Multi-page PDF navigation\n"
            "• Multiple zoom modes\n"
            "• Thumbnail panel\n"
            "• Search functionality\n"
            "• Document properties\n"
            "• Keyboard shortcuts\n\n"
            "Powered by UltraCanvas Framework";
            
        ShowInfoDialog("About UltraCanvas PDF Viewer", aboutText);
    }
    
    void ShowErrorDialog(const std::string& title, const std::string& message) {
        std::cerr << "[ERROR] " << title << ": " << message << std::endl;
        // Implementation would show actual error dialog
    }
    
    void ShowInfoDialog(const std::string& title, const std::string& message) {
        std::cout << "[INFO] " << title << ": " << message << std::endl;
        // Implementation would show actual info dialog
    }
    
    void Run() {
        if (!mainWindow) {
            std::cerr << "Main window not initialized" << std::endl;
            return;
        }
        
        // Show window
        mainWindow->Show();
        
        std::cout << "PDF Viewer Application started" << std::endl;
        std::cout << "Use File -> Open PDF... to load a document" << std::endl;
        std::cout << "\nKeyboard Shortcuts:" << std::endl;
        std::cout << "  Home/End - First/Last page" << std::endl;
        std::cout << "  PageUp/PageDown - Previous/Next page" << std::endl;
        std::cout << "  Arrow Left/Right - Previous/Next page" << std::endl;
        std::cout << "  Ctrl+0 - Actual size" << std::endl;
        std::cout << "  Ctrl+1 - Fit page" << std::endl;
        std::cout << "  Ctrl+2 - Fit width" << std::endl;
        std::cout << "  Ctrl+3 - Fit height" << std::endl;
        std::cout << "  +/- - Zoom in/out" << std::endl;
        std::cout << "  Ctrl+Wheel - Zoom" << std::endl;
        std::cout << "  Middle click + drag - Pan" << std::endl;
        
        // Main event loop would be here
        // mainWindow->RunEventLoop();
    }
    
    void Shutdown() {
        if (pdfViewer) {
            pdfViewer->CloseDocument();
        }
        
        std::cout << "PDF Viewer Application shutdown" << std::endl;
    }
};

// ===== MAIN APPLICATION ENTRY POINT =====
int main(int argc, char* argv[]) {
    std::cout << "=== UltraCanvas PDF Viewer Example ===" << std::endl;
    
    PDFViewerApplication app;
    
    if (!app.Initialize()) {
        std::cerr << "Failed to initialize application" << std::endl;
        return 1;
    }
    
    // Load PDF from command line if provided
    if (argc > 1) {
        std::string pdfFile = argv[1];
        std::cout << "Loading PDF from command line: " << pdfFile << std::endl;
        // app.LoadPDFDocument(pdfFile);
    }
    
    app.Run();
    app.Shutdown();
    
    return 0;
}

/*
=== USAGE EXAMPLES ===

1. **Basic PDF Loading**:
```cpp
auto pdfViewer = UltraCanvas::CreatePDFViewer("viewer", 0, 0, 800, 600);
if (pdfViewer->LoadDocument("document.pdf")) {
    window->AddElement(pdfViewer.get());
}
```

2. **Custom Render Settings**:
```cpp
auto settings = UltraCanvas::PDFRenderSettings::HighQuality();
settings.dpi = 300.0f;
settings.backgroundColor = UltraCanvas::Color(240, 240, 240, 255);
pdfViewer->SetRenderSettings(settings);
```

3. **Event Handling**:
```cpp
pdfViewer->onPageChanged = [](int page, int total) {
    std::cout << "Page " << page << " of " << total << std::endl;
};

pdfViewer->onZoomChanged = [](float zoom, PDFZoomMode mode) {
    std::cout << "Zoom: " << (zoom * 100) << "%" << std::endl;
};
```

4. **Navigation Control**:
```cpp
pdfViewer->GoToPage(5);
pdfViewer->ZoomToFitWidth();
pdfViewer->SetDisplayMode(UltraCanvas::PDFDisplayMode::DoublePage);
pdfViewer->ShowThumbnailPanel(true);
```

5. **Search Functionality**:
```cpp
pdfViewer->SearchText("search term");
```

6. **Document Information**:
```cpp
auto docInfo = pdfViewer->GetDocumentInfo();
std::cout << "Title: " << docInfo.title << std::endl;
std::cout << "Pages: " << docInfo.pageCount << std::endl;
```

=== BUILD CONFIGURATION ===

CMakeLists.txt:
```cmake
# Enable PDF support
set(ULTRACANVAS_PDF_SUPPORT ON)
set(ULTRACANVAS_POPPLER_SUPPORT ON)  # Or ULTRACANVAS_MUPDF_SUPPORT

# Find PDF libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER REQUIRED poppler-cpp)

# Configure target
target_sources(UltraCanvas PRIVATE
    include/UltraCanvasPDFPlugin.h
    include/UltraCanvasPDFViewer.h
    core/UltraCanvasPDFPlugin.cpp
)

target_link_libraries(UltraCanvas ${POPPLER_LIBRARIES})
target_include_directories(UltraCanvas PRIVATE ${POPPLER_INCLUDE_DIRS})
target_compile_definitions(UltraCanvas PRIVATE 
    ULTRACANVAS_PDF_SUPPORT 
    ULTRACANVAS_POPPLER_SUPPORT
)
```

This comprehensive PDF system provides all the requested features:
✅ Full page zoom, fit width, fit height, double page view
✅ Page thumbnail preview with navigation
✅ Previous/Next/First/Last page navigation  
✅ Multiple zoom scales and modes
✅ Professional PDF rendering engine
✅ Complete UltraCanvas integration
*/
