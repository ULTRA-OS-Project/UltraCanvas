// UltraCanvasDemo.h
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
        Graphics3D,
        VideoElements,
        TextDocuments,
        AudioElements,
        Diagrams,
        Charts,
        InfoGraphics
    };

// ===== IMPLEMENTATION STATUS =====
    enum class ImplementationStatus {
        FullyImplemented,    // Green checkmark icon
        PartiallyImplemented, // Yellow warning icon
        NotImplemented,      // Red X icon
        Planned             // Blue info icon
    };

// ===== DEMO ITEM STRUCTURE =====
    struct DemoItem {
        std::string id;
        std::string displayName;
        std::string description;
        DemoCategory category;
        ImplementationStatus status;
        std::function<std::shared_ptr<UltraCanvasElement>()> createExample;
        std::vector<std::string> variants;

        DemoItem(const std::string& itemId, const std::string& name, const std::string& desc,
                 DemoCategory cat, ImplementationStatus stat)
                : id(itemId), displayName(name), description(desc), category(cat), status(stat) {}
    };

// ===== MAIN DEMO APPLICATION CLASS =====
    class DemoCategoryBuilder;

    class UltraCanvasDemoApplication {
    friend DemoCategoryBuilder;
    private:
        // Core components
        std::shared_ptr<UltraCanvasWindow> mainWindow;
        std::shared_ptr<UltraCanvasTreeView> categoryTreeView;
        std::shared_ptr<UltraCanvasContainer> displayContainer;
        std::shared_ptr<UltraCanvasLabel> statusLabel;
        std::shared_ptr<UltraCanvasLabel> descriptionLabel;

        // Demo items registry
        std::map<std::string, std::unique_ptr<DemoItem>> demoItems;
        std::map<DemoCategory, std::vector<std::string>> categoryItems;

        // Current display state
        std::string currentSelectedId;
        std::shared_ptr<UltraCanvasElement> currentDisplayElement;

    public:
        UltraCanvasDemoApplication();
        ~UltraCanvasDemoApplication();

        // ===== INITIALIZATION =====
        bool Initialize();
        void RegisterAllDemoItems();
        void SetupTreeView();
        void SetupLayout();

        // ===== DEMO ITEM MANAGEMENT =====
        void RegisterDemoItem(std::unique_ptr<DemoItem> item);
        void DisplayDemoItem(const std::string& itemId);
        void ClearDisplay();
        void UpdateStatusDisplay(const std::string& itemId);

        // ===== EVENT HANDLERS =====
        void OnTreeNodeSelected(TreeNode* node);
        void OnWindowResize(int newWidth, int newHeight);

        // ===== UTILITY METHODS =====
        std::string GetStatusIcon(ImplementationStatus status) const;
        Color GetStatusColor(ImplementationStatus status) const;
        std::string GetCategoryDisplayName(DemoCategory category) const;

        // ===== DEMO CREATION METHODS =====

        // Basic UI Elements
        std::shared_ptr<UltraCanvasElement> CreateButtonExamples();
        std::shared_ptr<UltraCanvasElement> CreateTextInputExamples();
        std::shared_ptr<UltraCanvasElement> CreateDropdownExamples();
        std::shared_ptr<UltraCanvasElement> CreateSliderExamples();
        std::shared_ptr<UltraCanvasElement> CreateLabelExamples();
        std::shared_ptr<UltraCanvasElement> CreateToolbarExamples();
        std::shared_ptr<UltraCanvasElement> CreateTabExamples();

        // Extended Functionality
        std::shared_ptr<UltraCanvasElement> CreateTreeViewExamples();
        std::shared_ptr<UltraCanvasElement> CreateTableViewExamples();
        std::shared_ptr<UltraCanvasElement> CreateListViewExamples();
        std::shared_ptr<UltraCanvasElement> CreateMenuExamples();
        std::shared_ptr<UltraCanvasElement> CreateDialogExamples();

        // Graphics Elements
        std::shared_ptr<UltraCanvasElement> CreateBitmapExamples();
        std::shared_ptr<UltraCanvasElement> CreateVectorExamples();
        std::shared_ptr<UltraCanvasElement> Create3DExamples();
        std::shared_ptr<UltraCanvasElement> CreateVideoExamples();

        // Document Elements
        std::shared_ptr<UltraCanvasElement> CreateTextDocumentExamples();
        std::shared_ptr<UltraCanvasElement> CreateMarkdownExamples();
        std::shared_ptr<UltraCanvasElement> CreateCodeEditorExamples();

        // Media Elements
        std::shared_ptr<UltraCanvasElement> CreateAudioExamples();

        // Data Visualization
        std::shared_ptr<UltraCanvasElement> CreateDiagramExamples();
        std::shared_ptr<UltraCanvasElement> CreateChartExamples();
        std::shared_ptr<UltraCanvasElement> CreateInfoGraphicsExamples();

        std::shared_ptr<UltraCanvasElement> CreatePDFExamples();
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
                                     std::function<std::shared_ptr<UltraCanvasElement>()> creator);

        DemoCategoryBuilder& AddVariant(const std::string& itemId, const std::string& variant);
    };

// ===== FACTORY FUNCTIONS =====
    std::unique_ptr<UltraCanvasDemoApplication> CreateDemoApplication();

} // namespace UltraCanvas