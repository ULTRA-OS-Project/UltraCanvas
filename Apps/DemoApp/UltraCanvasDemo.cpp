// Apps/DemoApp/UltraCanvasDemo.cpp
// Comprehensive demonstration program implementation
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include <iostream>
#include <sstream>

namespace UltraCanvas {

// ===== CONSTRUCTOR / DESTRUCTOR =====
    UltraCanvasDemoApplication::UltraCanvasDemoApplication() {
        currentSelectedId = "";
        currentDisplayElement = nullptr;
    }

    UltraCanvasDemoApplication::~UltraCanvasDemoApplication() {
        Shutdown();
    }

// ===== INITIALIZATION =====
    bool UltraCanvasDemoApplication::Initialize() {
        std::cout << "Initializing UltraCanvas Demo Application..." << std::endl;

        // Create main window using proper configuration
        WindowConfig config;
        config.title = "UltraCanvas Framework - Component Demonstration";
        config.width = 1400;
        config.height = 840;
        config.resizable = true;
        config.type = WindowType::Standard;

        mainWindow = std::make_shared<UltraCanvasWindow>();
        if (!mainWindow->Create(config)) {
            std::cerr << "Failed to create main window" << std::endl;
            return false;
        }

        // Create tree view for categories (left side)
        categoryTreeView = std::make_shared<UltraCanvasTreeView>("CategoryTree", 2, 10, 10, 350, 800);
        categoryTreeView->SetRowHeight(24);
        categoryTreeView->SetSelectionMode(TreeSelectionMode::Single);
        categoryTreeView->SetLineStyle(TreeLineStyle::Solid);

        // Create display container (right side)
        displayContainer = std::make_shared<UltraCanvasContainer>("DisplayArea", 3, 370, 10, 1020, 800);
        ContainerStyle displayContainerStyle;
        displayContainerStyle.backgroundColor = Colors::White;
        displayContainerStyle.borderWidth = 1.0f;
        displayContainerStyle.borderColor = Color(200, 200, 200, 255);
        displayContainer->SetContainerStyle(displayContainerStyle);

        // Create status label (bottom left)
        statusLabel = std::make_shared<UltraCanvasLabel>("StatusLabel", 4, 10, 810, 350, 25);
        statusLabel->SetText("Select a component from the tree view to see examples");
        statusLabel->SetBackgroundColor(Color(240, 240, 240, 255));

        // Create description label (bottom right)
        descriptionLabel = std::make_shared<UltraCanvasLabel>("DescriptionLabel", 5, 370, 810, 1020, 25);
        descriptionLabel->SetText("");
        descriptionLabel->SetBackgroundColor(Color(240, 240, 240, 255));

        // Register all demo items
        RegisterAllDemoItems();

        // Setup tree view with categories
        SetupTreeView();

        // Setup event handlers
        categoryTreeView->onNodeSelected = [this](TreeNode* node) {
            OnTreeNodeSelected(node);
        };

        // Add elements to window
        mainWindow->AddChild(categoryTreeView);
        mainWindow->AddChild(displayContainer);
        mainWindow->AddChild(statusLabel);
        mainWindow->AddChild(descriptionLabel);

        std::cout << "✓ Demo application initialized successfully" << std::endl;
        return true;
    }

    void UltraCanvasDemoApplication::RegisterAllDemoItems() {
        std::cout << "Registering demo items..." << std::endl;

        // ===== BASIC UI ELEMENTS =====
        auto basicBuilder = DemoCategoryBuilder(this, DemoCategory::BasicUI);

        basicBuilder.AddItem("button", "Button", "Interactive buttons with various styles and states",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateButtonExamples(); })
                .AddVariant("button", "Standard Button")
                .AddVariant("button", "Icon Button")
                .AddVariant("button", "Toggle Button")
                .AddVariant("button", "Three-Section Button");

        basicBuilder.AddItem("textinput", "Text Input", "Text input fields with validation and formatting",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateTextInputExamples(); })
                .AddVariant("textinput", "Single Line Input")
                .AddVariant("textinput", "Multi-line Text Area")
                .AddVariant("textinput", "Password Field")
                .AddVariant("textinput", "Numeric Input");

        basicBuilder.AddItem("dropdown", "Dropdown/ComboBox", "Dropdown selection controls",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateDropdownExamples(); })
                .AddVariant("dropdown", "Simple Dropdown")
                .AddVariant("dropdown", "Editable ComboBox")
                .AddVariant("dropdown", "Multi-Select");

        basicBuilder.AddItem("slider", "Slider", "Range and value selection sliders",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateSliderExamples(); })
                .AddVariant("slider", "Horizontal Slider")
                .AddVariant("slider", "Vertical Slider")
                .AddVariant("slider", "Range Slider");

        basicBuilder.AddItem("label", "Label", "Text display with formatting and styling",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateLabelExamples(); })
                .AddVariant("label", "Basic Label")
                .AddVariant("label", "Header Text")
                .AddVariant("label", "Status Label");

        basicBuilder.AddItem("menu", "Menus", "Various menu types and styles",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateMenuExamples(); })
                .AddVariant("menu", "Context Menu")
                .AddVariant("menu", "Main Menu Bar")
                .AddVariant("menu", "Popup Menu")
                .AddVariant("menu", "Submenu Navigation")
                .AddVariant("menu", "Checkbox/Radio Items")
                .AddVariant("menu", "Styled Menus");

        basicBuilder.AddItem("toolbar", "Toolbar", "Tool and action bars",
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreateToolbarExamples(); })
                .AddVariant("toolbar", "Horizontal Toolbar")
                .AddVariant("toolbar", "Vertical Toolbar")
                .AddVariant("toolbar", "Ribbon Style");

        basicBuilder.AddItem("tabs", "Tabs", "Tabbed interface containers",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateTabExamples(); })
                .AddVariant("tabs", "Top Tabs")
                .AddVariant("tabs", "Side Tabs")
                .AddVariant("tabs", "Closable Tabs");

        // ===== EXTENDED FUNCTIONALITY =====
        auto extendedBuilder = DemoCategoryBuilder(this, DemoCategory::ExtendedFunctionality);

        extendedBuilder.AddItem("treeview", "Tree View", "Hierarchical data display with icons",
                                ImplementationStatus::FullyImplemented,
                                [this]() { return CreateTreeViewExamples(); })
                .AddVariant("treeview", "File Explorer Style")
                .AddVariant("treeview", "Multi-Selection Tree")
                .AddVariant("treeview", "Checkable Nodes");

        extendedBuilder.AddItem("tableview", "Table View", "Data grid with sorting and editing",
                                ImplementationStatus::NotImplemented,
                                [this]() { return CreateTableViewExamples(); })
                .AddVariant("tableview", "Basic Data Grid")
                .AddVariant("tableview", "Sortable Columns")
                .AddVariant("tableview", "Editable Cells");

        extendedBuilder.AddItem("listview", "List View", "Item lists with custom rendering",
                                ImplementationStatus::NotImplemented,
                                [this]() { return CreateListViewExamples(); })
                .AddVariant("listview", "Simple List")
                .AddVariant("listview", "Icon List")
                .AddVariant("listview", "Detail View");

        extendedBuilder.AddItem("textarea", "Advanced Text Area", "Advanced text editing with syntax highlighting",
                                ImplementationStatus::FullyImplemented,
                                [this]() { return CreateTextAreaExamples(); })
                .AddVariant("textarea", "C++ Syntax Highlighting")
                .AddVariant("textarea", "Python Syntax Highlighting")
                .AddVariant("textarea", "Pascal Syntax Highlighting")
                .AddVariant("textarea", "Line Numbers Display")
                .AddVariant("textarea", "Theme Support");

        // ===== BITMAP ELEMENTS =====
        auto bitmapBuilder = DemoCategoryBuilder(this, DemoCategory::BitmapElements);

        bitmapBuilder.AddItem("pngimages", "PNG Images", "PNG Image display and manipulation",
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreatePNGExamples(); })
                .AddVariant("images", "PNG/JPEG Display");
        bitmapBuilder.AddItem("jpegimages", "JPEG Images", "JPEG Image display and manipulation",
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateJPEGExamples(); })
                .AddVariant("images", "PNG/JPEG Display");
        bitmapBuilder.AddItem("gifimages", "GIF Images", "GIF Image display and manipulation",
                              ImplementationStatus::NotImplemented,
                              [this]() { return CreateBitmapNotImplementedExamples("GIF"); });
        bitmapBuilder.AddItem("avifimages", "AVIF Images", "AVIF Image display and manipulation",
                              ImplementationStatus::NotImplemented,
                              [this]() { return CreateBitmapNotImplementedExamples("AVIF"); });
        bitmapBuilder.AddItem("tiffimages", "TIFF Images", "TIFF Image display and manipulation",
                              ImplementationStatus::NotImplemented,
                              [this]() { return CreateBitmapNotImplementedExamples("TIFF"); });
        bitmapBuilder.AddItem("webpimages", "WEBP Images", "WEBP Image display and manipulation",
                              ImplementationStatus::NotImplemented,
                              [this]() { return CreateBitmapNotImplementedExamples("WEBP"); });
        bitmapBuilder.AddItem("qoiimages", "QOI Images", "QOI Image display and manipulation",
                              ImplementationStatus::NotImplemented,
                              [this]() { return CreateBitmapNotImplementedExamples("QOI"); });
        bitmapBuilder.AddItem("xarimages", "XAR Images", "XAR Image display and manipulation",
                              ImplementationStatus::NotImplemented,
                              [this]() { return CreateBitmapNotImplementedExamples("QOI"); });
        bitmapBuilder.AddItem("rawimages", "RAW Images", "RAW Image display and manipulation",
                              ImplementationStatus::NotImplemented,
                              [this]() { return CreateBitmapNotImplementedExamples("TIFF"); });

        // ===== VECTOR ELEMENTS =====
        auto vectorBuilder = DemoCategoryBuilder(this, DemoCategory::VectorElements);

        vectorBuilder.AddItem("svg", "SVG Graphics", "Scalable vector graphics rendering",
                              ImplementationStatus::PartiallyImplemented,
                              [this]() { return CreateSVGVectorExamples(); })
                .AddVariant("svg", "SVG File Display")
                .AddVariant("svg", "Interactive SVG")
                .AddVariant("svg", "SVG Animations");

        vectorBuilder.AddItem("drawing", "Drawing Surface", "Vector drawing and primitives",
                              ImplementationStatus::NotImplemented,
                              [this]() { return CreateVectorExamples(); });

        // ===== DIAGRAMS =====
        auto diagramBuilder = DemoCategoryBuilder(this, DemoCategory::Diagrams);

        diagramBuilder.AddItem("plantuml", "PlantUML", "UML and diagram generation",
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreateDiagramExamples(); })
                .AddVariant("plantuml", "Class Diagrams")
                .AddVariant("plantuml", "Sequence Diagrams")
                .AddVariant("plantuml", "Activity Diagrams");

        diagramBuilder.AddItem("blockdiagram", "Block diagram", "Block diagram",
                               ImplementationStatus::NotImplemented,
                               [this]() { return nullptr; });

        diagramBuilder.AddItem("nodediagram", "Node diagram", "Node diagram",
                               ImplementationStatus::NotImplemented,
                               [this]() { return nullptr; });

        diagramBuilder.AddItem("sankeydiagram", "Sankey diagram", "Sankey diagram",
                               ImplementationStatus::NotImplemented,
                               [this]() { return nullptr; });

        diagramBuilder.AddItem("venndiagram", "Venn diagram", "Venn diagram",
                               ImplementationStatus::NotImplemented,
                               [this]() { return nullptr; });

        // ===== CHARTS =====
        auto chartBuilder = DemoCategoryBuilder(this, DemoCategory::Charts);

        chartBuilder.AddItem("linecharts", "Line Chart", "Line chart data visualization",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateLineChartsExamples(); });

        chartBuilder.AddItem("barcharts", "Bar Chart", "Bar chart data visualization",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateBarChartsExamples(); });

        chartBuilder.AddItem("scattercharts", "Scatter Plot Chart", "Scatter plot chart data visualization",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateScatterPlotChartsExamples(); });

        chartBuilder.AddItem("areacharts", "Area Chart", "Area chart data visualization",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateAreaChartsExamples(); });

        chartBuilder.AddItem("divergingcharts", "Diverging Bar Charts", "Likert scale and population pyramid charts",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateDivergingChartExamples(); })
                .AddVariant("divergingcharts", "Likert Scale")
                .AddVariant("divergingcharts", "Population Pyramid")
                .AddVariant("divergingcharts", "Tornado Chart");

        chartBuilder.AddItem("waterfallcharts", "Waterfall Charts", "Cumulative flow visualization",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateWaterfallChartExamples(); })
                .AddVariant("waterfallcharts", "Revenue Flow")
                .AddVariant("waterfallcharts", "Cash Flow with Subtotals")
                .AddVariant("waterfallcharts", "Performance Impact");

        chartBuilder.AddItem("sunburstcharts", "Sunburst Chart", "Sunburst Chart",
                             ImplementationStatus::NotImplemented,
                             [this]() { return nullptr; });

        chartBuilder.AddItem("ganttcharts", "Gantt Chart", "Gantt Chart",
                             ImplementationStatus::NotImplemented,
                             [this]() { return nullptr; });

        chartBuilder.AddItem("quadrantcharts", "Quadrant Chart", "Quadrant Chart",
                             ImplementationStatus::NotImplemented,
                             [this]() { return nullptr; });

        chartBuilder.AddItem("circularcharts", "Circular Chart", "Circular Chart",
                             ImplementationStatus::NotImplemented,
                             [this]() { return nullptr; });

        chartBuilder.AddItem("polarcharts", "Polar Chart", "Polar Chart",
                             ImplementationStatus::NotImplemented,
                             [this]() { return nullptr; });

        // ===== INFO GRAPHICS =====
        auto infoBuilder = DemoCategoryBuilder(this, DemoCategory::InfoGraphics);

        infoBuilder.AddItem("infographics", "Info Graphics", "Complex data visualizations",
                            ImplementationStatus::NotImplemented,
                            [this]() { return CreateInfoGraphicsExamples(); })
                .AddVariant("infographics", "Dashboard Widgets")
                .AddVariant("infographics", "Statistical Displays")
                .AddVariant("infographics", "Interactive Maps");

        // ===== 3D ELEMENTS =====
        auto graphics3DBuilder = DemoCategoryBuilder(this, DemoCategory::Graphics3D);

        graphics3DBuilder.AddItem("models3d", "3D Models", "3D model display and interaction",
                                  ImplementationStatus::NotImplemented,
                                  [this]() { return Create3DExamples(); })
                .AddVariant("models3d", "3DS Models")
                .AddVariant("models3d", "3DM Models")
                .AddVariant("models3d", "OBJ Models");

        // ===== VIDEO ELEMENTS =====
        auto videoBuilder = DemoCategoryBuilder(this, DemoCategory::VideoElements);

        videoBuilder.AddItem("video", "Video Player", "Video playback and controls",
                             ImplementationStatus::NotImplemented,
                             [this]() { return CreateVideoExamples(); })
                .AddVariant("video", "MP4 Playback")
                .AddVariant("video", "Custom Controls")
                .AddVariant("video", "Streaming Support");

        // ===== TEXT DOCUMENTS =====
        auto textDocBuilder = DemoCategoryBuilder(this, DemoCategory::TextDocuments);

        textDocBuilder.AddItem("markdown", "Markdown", "Markdown document rendering",
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreateMarkdownExamples(); });

        textDocBuilder.AddItem("codeeditor", "Code Editor", "Syntax highlighting text editor",
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreateCodeEditorExamples(); })
                .AddVariant("codeeditor", "C++ Syntax")
                .AddVariant("codeeditor", "Pascal Syntax")
                .AddVariant("codeeditor", "COBOL Syntax");

        textDocBuilder.AddItem("textdocuments", "Text Documents", "ODT, RTF, TeX document support",
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreateTextDocumentExamples(); });


        // ===== AUDIO ELEMENTS =====
        auto audioBuilder = DemoCategoryBuilder(this, DemoCategory::AudioElements);

        audioBuilder.AddItem("audio", "Audio Player", "Audio playback and waveform display",
                             ImplementationStatus::NotImplemented,
                             [this]() { return CreateAudioExamples(); })
                .AddVariant("audio", "FLAC Support")
                .AddVariant("audio", "MP3 Playback")
                .AddVariant("audio", "Waveform Visualization");

        std::cout << "✓ Registered " << demoItems.size() << " demo items across "
                  << categoryItems.size() << " categories" << std::endl;
    }

    void UltraCanvasDemoApplication::SetupTreeView() {
        // Setup root node
        TreeNodeData rootData("root", "UltraCanvas Components");
        rootData.leftIcon = TreeNodeIcon("assets/icons/ultracanvas.png", 16, 16);
        TreeNode* rootNode = categoryTreeView->SetRootNode(rootData);

        // Add category nodes
        std::map<DemoCategory, std::string> categoryNames = {
                {DemoCategory::BasicUI, "Basic UI Elements"},
                {DemoCategory::ExtendedFunctionality, "Extended Functionality"},
                {DemoCategory::BitmapElements, "Bitmap Elements"},
                {DemoCategory::VectorElements, "Vector Graphics"},
                {DemoCategory::Diagrams, "Diagrams"},
                {DemoCategory::Charts, "Charts"},
                {DemoCategory::InfoGraphics, "Info Graphics"},
                {DemoCategory::Graphics3D, "3D Graphics"},
                {DemoCategory::VideoElements, "Video Elements"},
                {DemoCategory::TextDocuments, "Text Documents"},
                {DemoCategory::AudioElements, "Audio Elements"}
        };

        for (const auto& [category, items] : categoryItems) {
            TreeNodeData categoryData(
                    "cat_" + std::to_string(static_cast<int>(category)),
                    categoryNames[category]
            );
            categoryData.leftIcon = TreeNodeIcon("assets/icons/folder.png", 16, 16);
            TreeNode* categoryNode = categoryTreeView->AddNode("root", categoryData);

            // Add items for this category
            for (const std::string& itemId : items) {
                const auto& demoItem = demoItems[itemId];
                TreeNodeData itemData(itemId, demoItem->displayName);
                itemData.leftIcon = TreeNodeIcon("assets/icons/component.png", 16, 16);
                itemData.rightIcon = TreeNodeIcon(GetStatusIcon(demoItem->status), 12, 12);
                categoryTreeView->AddNode(categoryData.nodeId, itemData);
            }
        }

        // Expand root node
        rootNode->Expand();
    }

// ===== EVENT HANDLERS =====
    void UltraCanvasDemoApplication::OnTreeNodeSelected(TreeNode* node) {
        if (!node || !node->data.nodeId.length()) return;

        std::string nodeId = node->data.nodeId;

        // Check if this is a demo item (not a category)
        if (demoItems.find(nodeId) != demoItems.end()) {
            DisplayDemoItem(nodeId);
            UpdateStatusDisplay(nodeId);
        } else {
            // Category selected - clear display
            ClearDisplay();
            statusLabel->SetText("Category: " + node->data.text + " - Select a specific component to view examples");
        }
    }

    void UltraCanvasDemoApplication::DisplayDemoItem(const std::string& itemId) {
        ClearDisplay();

        auto it = demoItems.find(itemId);
        if (it == demoItems.end()) return;

        const auto& item = it->second;

        if (item->createExample && item->status != ImplementationStatus::NotImplemented) {
            try {
                currentDisplayElement = item->createExample();
                if (currentDisplayElement) {
                    displayContainer->AddChild(currentDisplayElement);
                    currentSelectedId = itemId;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error creating example for " << itemId << ": " << e.what() << std::endl;
            }
        } else {
            // Show placeholder for not implemented items
            auto placeholder = std::make_shared<UltraCanvasLabel>("placeholder", 999, 20, 20, 600, 200);
            placeholder->SetText("This component is not yet implemented.\nPlanned for future release.");
            placeholder->SetAlignment(TextAlignment::Center);
            placeholder->SetBackgroundColor(Color(255, 255, 200, 100));
            placeholder->SetBorderWidth(2.0f);
            placeholder->SetBorderColor(Color(200, 200, 0, 255));

            displayContainer->AddChild(placeholder);
            currentDisplayElement = placeholder;
            currentSelectedId = itemId;
        }
    }

    void UltraCanvasDemoApplication::ClearDisplay() {
        if (currentDisplayElement) {
            displayContainer->RemoveChild(currentDisplayElement);
            currentDisplayElement = nullptr;
        }
        currentSelectedId = "";
    }

    void UltraCanvasDemoApplication::UpdateStatusDisplay(const std::string& itemId) {
        auto it = demoItems.find(itemId);
        if (it == demoItems.end()) return;

        const auto& item = it->second;

        // Update status label
        std::ostringstream statusText;
        statusText << "Status: ";

        switch (item->status) {
            case ImplementationStatus::FullyImplemented:
                statusText << "✓ Fully Implemented";
                break;
            case ImplementationStatus::PartiallyImplemented:
                statusText << "⚠ Partially Implemented";
                break;
            case ImplementationStatus::NotImplemented:
                statusText << "✗ Not Implemented";
                break;
            case ImplementationStatus::Planned:
                statusText << "📋 Planned";
                break;
        }

        if (!item->variants.empty()) {
            statusText << " | Variants: " << item->variants.size();
        }
        statusLabel->SetText(statusText.str());
        statusLabel->SetTextColor(GetStatusColor(item->status));

        // Update description label
        descriptionLabel->SetText(item->description);
    }

// ===== UTILITY METHODS =====
    std::string UltraCanvasDemoApplication::GetStatusIcon(ImplementationStatus status) const {
        switch (status) {
            case ImplementationStatus::FullyImplemented: return "assets/icons/check.png";
            case ImplementationStatus::PartiallyImplemented: return "assets/icons/warning.png";
            case ImplementationStatus::NotImplemented: return "assets/icons/x.png";
            case ImplementationStatus::Planned: return "assets/icons/info.png";
            default: return "assets/icons/unknown.png";
        }
    }

    Color UltraCanvasDemoApplication::GetStatusColor(ImplementationStatus status) const {
        switch (status) {
            case ImplementationStatus::FullyImplemented: return Color(0, 150, 0, 255);      // Green
            case ImplementationStatus::PartiallyImplemented: return Color(200, 150, 0, 255); // Orange
            case ImplementationStatus::NotImplemented: return Color(200, 0, 0, 255);       // Red
            case ImplementationStatus::Planned: return Color(0, 100, 200, 255);           // Blue
            default: return Color(100, 100, 100, 255);                                    // Gray
        }
    }

// ===== APPLICATION LIFECYCLE =====
    void UltraCanvasDemoApplication::Run() {
        // Run application main loop
        std::cout << "Running UltraCanvas Demo Application..." << std::endl;
        std::cout << "Select items from the tree view to see implementation examples." << std::endl;

        if (mainWindow) {
            mainWindow->Show();
            // The application will handle the event loop
        }
        auto app = UltraCanvasApplication::GetInstance();
        app->Run();
    }

    void UltraCanvasDemoApplication::Shutdown() {
        std::cout << "Shutting down Demo Application..." << std::endl;

        ClearDisplay();
        demoItems.clear();
        categoryItems.clear();
    }

// ===== DEMO ITEM REGISTRATION =====
    void UltraCanvasDemoApplication::RegisterDemoItem(std::unique_ptr<DemoItem> item) {
        if (!item) return;

        std::string itemId = item->id;
        DemoCategory category = item->category;

        // Add to items registry
        demoItems[itemId] = std::move(item);

        // Add to category index
        categoryItems[category].push_back(itemId);
    }

// ===== DEMO CATEGORY BUILDER IMPLEMENTATION =====
    DemoCategoryBuilder& DemoCategoryBuilder::AddItem(const std::string& id, const std::string& name,
                                                      const std::string& description, ImplementationStatus status,
                                                      std::function<std::shared_ptr<UltraCanvasUIElement>()> creator) {
        auto item = std::make_unique<DemoItem>(id, name, description, category, status);
        item->createExample = creator;
        app->RegisterDemoItem(std::move(item));
        return *this;
    }

    DemoCategoryBuilder& DemoCategoryBuilder::AddVariant(const std::string& itemId, const std::string& variant) {
        auto it = app->demoItems.find(itemId);
        if (it != app->demoItems.end()) {
            it->second->variants.push_back(variant);
        }
        return *this;
    }

// ===== FACTORY FUNCTION =====
    std::unique_ptr<UltraCanvasDemoApplication> CreateDemoApplication() {
        return std::make_unique<UltraCanvasDemoApplication>();
    }

} // namespace UltraCanvas