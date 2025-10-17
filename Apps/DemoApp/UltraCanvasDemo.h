// Apps/DemoApp/UltraCanvasDemo.h
// Comprehensive demonstration program showing all UltraCanvas display elements
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasWindow.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasApplication.h"
#include <memory>
#include <map>
#include <functional>

namespace UltraCanvas {

// ===== DEMO COMPONENT CATEGORIES =====
    enum class DemoCategory {
        BasicUI,
        ExtendedFunctionality,
        BitmapElements,
        VectorElements,
        Diagrams,
        Charts,
        InfoGraphics,
        Graphics3D,
        VideoElements,
        TextDocuments,
        AudioElements,
        Tools
    };

// ===== IMPLEMENTATION STATUS =====
    enum class ImplementationStatus {
        FullyImplemented,    // Green checkmark icon
        PartiallyImplemented, // Yellow warning icon
        NotImplemented,      // Red X icon
        Planned             // Blue info icon
    };

    // ===== INFO WINDOW CLASS =====
    class InfoWindow : public UltraCanvasWindow {
    private:
        std::shared_ptr<UltraCanvasLabel> titleLabel;
        std::shared_ptr<UltraCanvasLabel> infoLabel1;
        std::shared_ptr<UltraCanvasLabel> infoLabel2;
        std::shared_ptr<UltraCanvasLabel> infoLabel3;
        std::shared_ptr<UltraCanvasImageElement> programmersGuideIcon;
        std::shared_ptr<UltraCanvasImageElement> exampleCodeIcon;
        std::shared_ptr<UltraCanvasButton> okButton;
        std::function<void()> onOkCallback;

    public:
        InfoWindow();
        ~InfoWindow();

        bool Initialize();
        void SetOkCallback(std::function<void()> callback);

    private:
        void OnOkButtonClick();
        void CreateInfoContent();
    };

// ===== DEMO ITEM STRUCTURE =====
    struct DemoItem {
        std::string id;
        std::string displayName;
        std::string description;
        DemoCategory category;
        ImplementationStatus status;
        std::function<std::shared_ptr<UltraCanvasUIElement>()> createExample;
        std::vector<std::string> variants;

        std::string demoSource;  // File path for demo source code
        std::string demoDoc;      // File path for documentation

        DemoItem(const std::string& itemId, const std::string& name, const std::string& desc,
                 DemoCategory cat, ImplementationStatus stat)
                : id(itemId), displayName(name), description(desc), category(cat), status(stat) {}
    };

    // ===== LEGEND CONTAINER CLASS =====
    class DemoLegendContainer : public UltraCanvasContainer {
    private:
        std::shared_ptr<UltraCanvasLabel> legendTitle;
        std::shared_ptr<UltraCanvasImageElement> implementedIcon;
        std::shared_ptr<UltraCanvasLabel> implementedLabel;
        std::shared_ptr<UltraCanvasImageElement> partialIcon;
        std::shared_ptr<UltraCanvasLabel> partialLabel;
        std::shared_ptr<UltraCanvasImageElement> notImplementedIcon;
        std::shared_ptr<UltraCanvasLabel> notImplementedLabel;

    public:
        DemoLegendContainer(const std::string& identifier, long id, long x, long y, long width, long height);
        void SetupLegend(const std::string& implementedIconPath,
                         const std::string& partialIconPath,
                         const std::string& notImplementedIconPath);
    };

    class DemoHeaderContainer : public UltraCanvasContainer {
    private:
        std::shared_ptr<UltraCanvasLabel> titleLabel;
        std::shared_ptr<UltraCanvasImageElement> sourceButton;
        std::shared_ptr<UltraCanvasImageElement> docButton;
        std::shared_ptr<UltraCanvasContainer> dividerLine;
        std::shared_ptr<UltraCanvasWindow> sourceWindow;
        std::shared_ptr<UltraCanvasWindow> docWindow;
        std::string currentSourceFile;
        std::string currentDocFile;

    public:
        DemoHeaderContainer(const std::string& identifier, long id, long x, long y, long width, long height);

        void SetDemoTitle(const std::string& title);
        void SetSourceFile(const std::string& sourceFile);
        void SetDocFile(const std::string& docFile);

        void ShowSourceWindow();
        void ShowDocumentationWindow();

    private:
        std::string LoadFileContent(const std::string& filePath);
        std::string GetFileExtension(const std::string& filePath);
        void CreateSourceWindow(const std::string& content, const std::string& title);
        void CreateDocumentationWindow(const std::string& content, const std::string& title);
    };
// ===== MAIN DEMO APPLICATION CLASS =====
    class DemoCategoryBuilder;

    class UltraCanvasDemoApplication {
    friend DemoCategoryBuilder;
    private:
        // Core components
        std::shared_ptr<UltraCanvasWindow> mainWindow;
        std::shared_ptr<InfoWindow> infoWindow;
        std::shared_ptr<UltraCanvasTreeView> categoryTreeView;
        std::shared_ptr<UltraCanvasContainer> mainContainer;  // Main container for display area
        std::shared_ptr<DemoHeaderContainer> headerContainer;  // Header with title and buttons
        std::shared_ptr<UltraCanvasContainer> displayContainer;  // Content display area
        std::shared_ptr<DemoLegendContainer> legendContainer;  // Legend container for status icons
        std::shared_ptr<UltraCanvasLabel> statusLabel;
        std::shared_ptr<UltraCanvasLabel> descriptionLabel;

        // Demo items registry
        std::map<std::string, std::unique_ptr<DemoItem>> demoItems;
        std::map<DemoCategory, std::vector<std::string>> categoryItems;

        // Current display state
        std::string currentSelectedId;
        std::shared_ptr<UltraCanvasUIElement> currentDisplayElement;
        bool infoWindowShown;

    public:
        UltraCanvasDemoApplication();
        ~UltraCanvasDemoApplication();

        // ===== INITIALIZATION =====
        bool Initialize();
        void RegisterAllDemoItems();
        void SetupTreeView();
        void SetupLayout();
        void SetupLegendContainer();  // New method for legend setup

        // Info window
        void ShowInfoWindow();
        void CloseInfoWindow();

        // ===== DEMO ITEM MANAGEMENT =====
        void RegisterDemoItem(std::unique_ptr<DemoItem> item);
        void DisplayDemoItem(const std::string& itemId);
        void ClearDisplay();
        void UpdateStatusDisplay(const std::string& itemId);
        void UpdateHeaderDisplay(const std::string& itemId);

        // ===== EVENT HANDLERS =====
        void OnTreeNodeSelected(TreeNode* node);
        void OnWindowResize(int newWidth, int newHeight);

        // ===== UTILITY METHODS =====
        std::string GetStatusIcon(ImplementationStatus status) const;
        Color GetStatusColor(ImplementationStatus status) const;
        std::string GetCategoryDisplayName(DemoCategory category) const;

        // ===== DEMO CREATION METHODS =====

        // Basic UI Elements
        std::shared_ptr<UltraCanvasUIElement> CreateButtonExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateTextInputExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateDropdownExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateSliderExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateLabelExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateCheckboxExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateToolbarExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateTabExamples();

        // Extended Functionality
        std::shared_ptr<UltraCanvasUIElement> CreateTreeViewExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateTableViewExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateListViewExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateMenuExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateDialogExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateTextAreaExamples();

        // Graphics Elements
        std::shared_ptr<UltraCanvasUIElement> CreateBitmapNotImplementedExamples(const std::string& format);
        std::shared_ptr<UltraCanvasUIElement> CreatePNGExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateJPEGExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateVectorExamples();
        std::shared_ptr<UltraCanvasUIElement> Create3DExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateVideoExamples();

        // Document Elements
        std::shared_ptr<UltraCanvasUIElement> CreateTextDocumentExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateMarkdownExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateCodeEditorExamples();

        // Media Elements
        std::shared_ptr<UltraCanvasUIElement> CreateAudioExamples();

        // Data Visualization
        std::shared_ptr<UltraCanvasUIElement> CreateDiagramExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateSankeyExamples();

        std::shared_ptr<UltraCanvasUIElement> CreateLineChartsExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateBarChartsExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateScatterPlotChartsExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateAreaChartsExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateFinancialChartExamples();


        std::shared_ptr<UltraCanvasUIElement> CreateDivergingChartExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateInfoGraphicsExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateWaterfallChartExamples();

//        std::shared_ptr<UltraCanvasUIElement> CreatePDFExamples();
        std::shared_ptr<UltraCanvasUIElement> CreateSVGVectorExamples();

        // ===== APPLICATION LIFECYCLE =====
        void Run();
        void Shutdown();
    };

// ===== DEMO CATEGORY HELPER CLASS =====
    class DemoCategoryBuilder {
    private:
        UltraCanvasDemoApplication* app;
        DemoCategory category;

    public:
        DemoCategoryBuilder(UltraCanvasDemoApplication* application, DemoCategory cat)
                : app(application), category(cat) {}

        DemoCategoryBuilder& AddItem(const std::string& id, const std::string& name,
                                     const std::string& description, ImplementationStatus status,
                                     std::function<std::shared_ptr<UltraCanvasUIElement>()> creator,
                                     const std::string& sourceFile = "",
                                     const std::string& docFile = "");

        DemoCategoryBuilder& AddVariant(const std::string& itemId, const std::string& variant);
    };

// ===== FACTORY FUNCTIONS =====
    std::unique_ptr<UltraCanvasDemoApplication> CreateDemoApplication();

} // namespace UltraCanvas