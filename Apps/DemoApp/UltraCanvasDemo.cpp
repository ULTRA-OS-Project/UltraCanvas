// Apps/DemoApp/UltraCanvasDemo.cpp
// Comprehensive demonstration program implementation
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasBoxLayout.h"
#include "UltraCanvasGridLayout.h"
#include "UltraCanvasTextArea.h"
#include "Plugins/Text/UltraCanvasMarkdown.h"
#include <iostream>
#include <sstream>

namespace UltraCanvas {
    DemoLegendContainer::DemoLegendContainer(const std::string& identifier, long id, long x, long y, long width, long height)
            : UltraCanvasContainer(identifier, id, x, y, width, height) {

        // Set container style
//        SetBorders(1, Color(200, 200, 200, 255));
        SetBackgroundColor(Color(245, 245, 245, 255));

        // Create legend title
        legendTitle = std::make_shared<UltraCanvasLabel>("LegendTitle", id + 1, 10, 5, width - 20, 20);
        legendTitle->SetText("Component Status Legend");
        legendTitle->SetFontSize(12);
        legendTitle->SetFontWeight(FontWeight::Bold);
        legendTitle->SetTextColor(Color(80, 80, 80, 255));
        legendTitle->SetAutoResize(true);
        AddChild(legendTitle);

        // Implemented status (row 1)
        implementedIcon = std::make_shared<UltraCanvasImageElement>("ImplementedIcon", id + 2, 10, 30, 16, 16);
        AddChild(implementedIcon);

        implementedLabel = std::make_shared<UltraCanvasLabel>("ImplementedLabel", id + 3, 32, 28, width - 42, 16);
        implementedLabel->SetText("Fully Implemented");
        implementedLabel->SetFontSize(11);
        implementedLabel->SetTextColor(Color(0, 150, 0, 255));
        implementedLabel->SetAutoResize(true);
        AddChild(implementedLabel);

        // Partially implemented status (row 2)
        partialIcon = std::make_shared<UltraCanvasImageElement>("PartialIcon", id + 4, 10, 50, 16, 16);
        AddChild(partialIcon);

        partialLabel = std::make_shared<UltraCanvasLabel>("PartialLabel", id + 5, 32, 48, width - 42, 16);
        partialLabel->SetText("Partially Implemented");
        partialLabel->SetFontSize(11);
        partialLabel->SetTextColor(Color(0x21, 0x96, 0xf3, 255));
        partialLabel->SetAutoResize(true);
        AddChild(partialLabel);

        // Not implemented status (row 3)
        notImplementedIcon = std::make_shared<UltraCanvasImageElement>("NotImplementedIcon", id + 6, 10, 70, 16, 16);
        AddChild(notImplementedIcon);

        notImplementedLabel = std::make_shared<UltraCanvasLabel>("NotImplementedLabel", id + 7, 32, 68, width - 42, 16);
        notImplementedLabel->SetText("Not Implemented Yet");
        notImplementedLabel->SetFontSize(11);
        notImplementedLabel->SetTextColor(Color(200, 0, 0, 255));
        notImplementedLabel->SetAutoResize(true);
        AddChild(notImplementedLabel);
    }

    void DemoLegendContainer::SetupLegend(const std::string& implementedIconPath,
                                          const std::string& partialIconPath,
                                          const std::string& notImplementedIconPath) {
        // Load icon images
        if (implementedIcon) {
            implementedIcon->LoadFromFile(implementedIconPath);
        }

        if (partialIcon) {
            partialIcon->LoadFromFile(partialIconPath);
        }

        if (notImplementedIcon) {
            notImplementedIcon->LoadFromFile(notImplementedIconPath);
        }
    }

    DemoHeaderContainer::DemoHeaderContainer(const std::string& identifier, long id,
                                             long x, long y, long width, long height)
            : UltraCanvasContainer(identifier, id, x, y, width, height) {

        // Create title label (left side)
        titleLabel = std::make_shared<UltraCanvasLabel>("HeaderTitle", id + 1, 10, 5, width - 200, 30);
        titleLabel->SetFontSize(14);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetText("Demo Title");
        titleLabel->SetAutoResize(true);
        titleLabel->SetMargin(2,0,0,0);
        //AddChild(titleLabel);

        // Create documentation button (right side)
        docButton = std::make_shared<UltraCanvasImageElement>("DocBtn", id + 3, width - 90, 5, 21, 21);
        docButton->LoadFromFile("assets/icons/text.png");
        docButton->SetVisible(false);  // Initially disabled
        docButton->SetClickable(true);
        docButton->onClick = [this]() { ShowDocumentationWindow(); };
        //AddChild(docButton);

        // Create source button (right side)
        sourceButton = std::make_shared<UltraCanvasImageElement>("SourceBtn", id + 2, width - 40, 5, 21, 28);
        sourceButton->LoadFromFile("assets/icons/c-plus-plus-icon.png");
        sourceButton->SetVisible(false);  // Initially disabled
        sourceButton->SetClickable(true);
        sourceButton->onClick = [this]() { ShowSourceWindow(); };
        //AddChild(sourceButton);

        // Create divider line at the bottom
        dividerLine = std::make_shared<UltraCanvasContainer>("Divider", id + 4, 0, 38, width, 2);
        dividerLine->SetBackgroundColor(Color(200, 200, 200, 255));
        //AddChild(dividerLine);

        // Set container style
        ContainerStyle containerStyle;
        containerStyle.enableHorizontalScrolling = false;
        containerStyle.enableVerticalScrolling = false;
        SetContainerStyle(containerStyle);

        SetBackgroundColor(Color(245, 245, 245, 255));
        SetPadding(5,10,5,10);
        SetBorderBottom(2, Colors::Gray);

        auto headerLayout = CreateHBoxLayout(this);
        headerLayout->SetSpacing(10);
        headerLayout->AddUIElement(titleLabel)->SetAlignment(LayoutItemAlignment::Center);
        headerLayout->AddStretch(1);
        headerLayout->AddUIElement(docButton)->SetAlignment(LayoutItemAlignment::Center);
        headerLayout->AddSpacing(3);
        headerLayout->AddUIElement(sourceButton)->SetAlignment(LayoutItemAlignment::Center);
        headerLayout->AddSpacing(7);
    }

    void DemoHeaderContainer::SetDemoTitle(const std::string& title) {
        if (titleLabel) {
            titleLabel->SetText(title);
        }
    }

    void DemoHeaderContainer::SetSourceFile(const std::string& sourceFile) {
        currentSourceFile = sourceFile;
        if (sourceButton) {
            sourceButton->SetVisible(!sourceFile.empty());
        }
    }

    void DemoHeaderContainer::SetDocFile(const std::string& docFile) {
        currentDocFile = docFile;
        if (docButton) {
            docButton->SetVisible(!docFile.empty());
        }
    }

    std::string DemoHeaderContainer::LoadFileContent(const std::string& filePath) {
        if (filePath.empty()) return "";

        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << filePath << std::endl;
            return "// Error: Could not load file: " + filePath;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();

        return buffer.str();
    }

    std::string DemoHeaderContainer::GetFileExtension(const std::string& filePath) {
        size_t lastDot = filePath.find_last_of('.');
        if (lastDot != std::string::npos) {
            return filePath.substr(lastDot + 1);
        }
        return "";
    }

    void DemoHeaderContainer::ShowSourceWindow() {
        if (currentSourceFile.empty()) return;

        std::string content = LoadFileContent(currentSourceFile);
        CreateSourceWindow(content, "Source Code: " + currentSourceFile);
    }

    void DemoHeaderContainer::ShowDocumentationWindow() {
        if (currentDocFile.empty()) return;

        std::string content = LoadFileContent(currentDocFile);
        CreateDocumentationWindow(content, "Documentation: " + currentDocFile);
    }


    void DemoHeaderContainer::CreateSourceWindow(const std::string& content, const std::string& title) {
        // Create a new window for source code display
        WindowConfig config;
        config.title = title;
        config.width = 1200;
        config.height = 600;
        config.resizable = true;
        config.type = WindowType::Standard;

        sourceWindow = std::make_shared<UltraCanvasWindow>();
        if (!sourceWindow->Create(config)) {
            std::cerr << "Failed to create source window" << std::endl;
            return;
        }

        // Create text area for source code with syntax highlighting
        auto textArea = std::make_shared<UltraCanvasTextArea>("SourceCode", 1000, 5, 5, 1190, 590);
        textArea->SetText(content);
        //textArea->SetReadOnly(true);
        textArea->SetShowLineNumbers(true);
        textArea->SetHighlightSyntax(true);

        // Determine language from file extension and apply syntax highlighting
        std::string ext = GetFileExtension(currentSourceFile);
        if (ext == "cpp" || ext == "c" || ext == "cc" || ext == "cxx" || ext == "h" || ext == "hpp") {
            textArea->SetProgrammingLanguageByExtension(ext);
        } else {
            textArea->ApplyCodeStyle("text");
        }
        textArea->SetFontSize(10);

        sourceWindow->SetEventCallback([this](const UCEvent& event) {
            if (event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) {
                sourceWindow->Close();
                sourceWindow.reset();
                return true;
            }
            return false;
        });

        sourceWindow->AddChild(textArea);
        sourceWindow->Show();
    }

    void DemoHeaderContainer::CreateDocumentationWindow(const std::string& content, const std::string& title) {
        // Create a new window for documentation display
        WindowConfig config;
        config.title = title;
        config.width = 1200;
        config.height = 600;
        config.resizable = true;
        config.type = WindowType::Standard;

        docWindow = std::make_shared<UltraCanvasWindow>();
        if (!docWindow->Create(config)) {
            std::cerr << "Failed to create documentation window" << std::endl;
            return;
        }
        // Create text area for documentation
        auto markDownTextArea = std::make_shared<UltraCanvasMarkdownDisplay>("Documentation", 2000, 5, 5, 1190, 590);
        markDownTextArea->SetMarkdownText(content);

        docWindow->SetEventCallback([this](const UCEvent& event) {
            if (event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) {
                docWindow->Close();
                docWindow.reset();
                return true;
            }
            return false;
        });

        docWindow->AddChild(markDownTextArea);
        docWindow->Show();
    }

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
        config.height = 880;
        config.resizable = true;
        config.type = WindowType::Standard;

        mainWindow = std::make_shared<UltraCanvasWindow>();
        if (!mainWindow->Create(config)) {
            std::cerr << "Failed to create main window" << std::endl;
            return false;
        }

        // Calculate positions for adjusted layout
        const int treeViewHeight = 740;  // Reduced from 840 to make room for legend
        const int legendHeight = 95;     // Height for legend container
        const int treeViewWidth = 350;   // Width for both treeview and legend

// Create tree view for categories (left side, reduced height)
        categoryTreeView = std::make_shared<UltraCanvasTreeView>("CategoryTree", 2, 0, 0, 100, 100);
        categoryTreeView->SetRowHeight(24);
        categoryTreeView->SetSelectionMode(TreeSelectionMode::Single);
        categoryTreeView->SetLineStyle(TreeLineStyle::Solid);
        categoryTreeView->SetShowFirstChildOnExpand(true);
        categoryTreeView->SetPadding(1,3,1,3);

        // Create legend container below tree view
        legendContainer = std::make_shared<DemoLegendContainer>("LegendContainer", 6, 0, 0, 100, legendHeight);
        legendContainer->SetBorderTop(1, Colors::Gray);
        SetupLegendContainer();

        auto categoryContainer = CreateContainer("catcont", 0, 0, 0, 100, 100);

        mainContainer = std::make_shared<UltraCanvasContainer>("MainDisplayArea", 3, 0, 0, 1030, 840);
        //mainContainer->SetBackgroundColor(Colors::White);
        //mainContainer->SetBorders(1, Color(20, 20, 20, 255));
        mainContainer->SetBorderLeft(1, Colors::Gray);
//        mainContainer->SetBorderBottom(10, Colors::Green);
//        mainContainer->SetBorderTop(10, Colors::Red);

        // Create header container (inside main container)
        headerContainer = std::make_shared<DemoHeaderContainer>("HeaderContainer", 4, 0, 0, 1028, 40);
//        mainContainer->AddChild(headerContainer);

        // Create display container (below header)
        displayContainer = std::make_shared<UltraCanvasContainer>("DisplayArea", 5, 0, 40, 1028, 785);
        displayContainer->SetBackgroundColor(Colors::White);
//        mainContainer->AddChild(displayContainer);

//        // Create display container (right side)
//        displayContainer = std::make_shared<UltraCanvasContainer>("DisplayArea", 3, 370, 10, 1020, 800);
//        ContainerStyle displayContainerStyle;
//        displayContainerStyle.backgroundColor = Colors::White;
//        displayContainerStyle.borderWidth = 1.0f;
//        displayContainerStyle.borderColor = Color(200, 200, 200, 255);
//        displayContainer->SetContainerStyle(displayContainerStyle);

        // Create status label (bottom left)
        statusLabel = std::make_shared<UltraCanvasLabel>("StatusLabel", 4, 10, 850, 850, 25);
        statusLabel->SetText("Select a component from the tree view to see examples");
        statusLabel->SetBackgroundColor(Color(240, 240, 240, 255));
        statusLabel->SetPadding(3, 7, 3, 7);

        // Register all demo items
        RegisterAllDemoItems();

        // Setup tree view with categories
        SetupTreeView();

        // Setup event handlers
        categoryTreeView->onNodeSelected = [this](TreeNode* node) {
            OnTreeNodeSelected(node);
        };

        // Add elements to window
//        mainWindow->AddChild(categoryTreeView);
//        mainWindow->AddChild(legendContainer);  // Add legend container
//        mainWindow->AddChild(mainContainer);
//        mainWindow->AddChild(statusLabel);

        //mainWindow->AddChild(descriptionLabel);

        auto categoryContainerLayout = CreateVBoxLayout(categoryContainer.get());
        categoryContainerLayout->AddUIElement(categoryTreeView, 1)->SetWidthMode(SizeMode::Fill);
        categoryContainerLayout->AddUIElement(legendContainer)->SetWidthMode(SizeMode::Fill);

        auto mainContainerLayout = CreateVBoxLayout(mainContainer.get());
        mainContainerLayout->AddUIElement(headerContainer)->SetWidthMode(SizeMode::Fill)->SetFixedHeight(40);
        mainContainerLayout->AddUIElement(displayContainer, 1)->SetWidthMode(SizeMode::Fill);

        auto mainLayout = CreateGridLayout(mainWindow.get(), 2, 2);
        mainLayout->SetColumnDefinition(0, GridRowColumnDefinition::Fixed(350));
        mainLayout->SetColumnDefinition(1, GridRowColumnDefinition::Star(1));
        mainLayout->SetRowDefinition(0, GridRowColumnDefinition::Star(1));
        mainLayout->SetRowDefinition(1, GridRowColumnDefinition::Fixed(25));

        mainLayout->AddUIElement(categoryContainer, 0, 0)->SetSizeMode(SizeMode::Fill, SizeMode::Fill);
        mainLayout->AddUIElement(mainContainer, 0, 1)->SetSizeMode(SizeMode::Fill, SizeMode::Fill);
        mainLayout->AddUIElement(statusLabel, 1, 0, 1, 2)->SetSizeMode(SizeMode::Fill, SizeMode::Fill);

        std::cout << "✓ Demo application initialized successfully" << std::endl;
        return true;
    }

    // ===== SETUP LEGEND CONTAINER METHOD =====
    void UltraCanvasDemoApplication::SetupLegendContainer() {
        if (legendContainer) {
            // Get icon paths using the existing GetStatusIcon method
            std::string implementedIcon = GetStatusIcon(ImplementationStatus::FullyImplemented);
            std::string partialIcon = GetStatusIcon(ImplementationStatus::PartiallyImplemented);
            std::string notImplementedIcon = GetStatusIcon(ImplementationStatus::NotImplemented);

            // Setup the legend with the appropriate icons
            legendContainer->SetupLegend(implementedIcon, partialIcon, notImplementedIcon);
        }
    }

    void UltraCanvasDemoApplication::RegisterAllDemoItems() {
        std::cout << "Registering demo items..." << std::endl;

        // ===== BASIC UI ELEMENTS =====
        auto basicBuilder = DemoCategoryBuilder(this, DemoCategory::BasicUI);

        basicBuilder.AddItem("button", "Button", "Interactive buttons with various styles and states",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateButtonExamples(); },
                             "Examples/UltraCanvasButtonExamples.cpp",
                             "Docs/UltraCanvasButtonExamples.md"
                             )
                .AddVariant("button", "Standard Button")
                .AddVariant("button", "Icon Button")
                .AddVariant("button", "Toggle Button")
                .AddVariant("button", "Three-Section Button");

        basicBuilder.AddItem("textinput", "Text Input", "Text input fields with validation and formatting",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateTextInputExamples(); },
                            "Examples/UltraCanvasTextInputExamples.cpp",
                            "Docs/UltraCanvasTextInputExamples.md"
                             )
                .AddVariant("textinput", "Single Line Input")
                .AddVariant("textinput", "Multi-line Text Area")
                .AddVariant("textinput", "Password Field")
                .AddVariant("textinput", "Numeric Input");

        basicBuilder.AddItem("dropdown", "Dropdown/ComboBox", "Dropdown selection controls",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateDropdownExamples(); },
                            "Examples/UltraCanvasDropDownExamples.cpp",
                            "Docs/UltraCanvasDropDownExamples.md")
                .AddVariant("dropdown", "Simple Dropdown")
                .AddVariant("dropdown", "Editable ComboBox")
                .AddVariant("dropdown", "Multi-Select");

        basicBuilder
                .AddItem("checkbox", "Checkbox",
                         "Interactive checkbox controls with multiple states and styles",
                         ImplementationStatus::FullyImplemented,
                         [this]() { return CreateCheckboxExamples(); },
                         "Apps/DemoApp/UltraCanvasCheckboxExamples.cpp",
                         "Docs/UltraCanvasCheckbox.md")
                .AddVariant("checkbox", "Standard Checkbox")
                .AddVariant("checkbox", "Tri-State Checkbox")
                .AddVariant("checkbox", "Switch Toggle")
                .AddVariant("checkbox", "Radio Button");

        basicBuilder.AddItem("segmentedcontrol", "Segmented Control",
                             "Compact control for selecting between mutually exclusive options",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateSegmentedControlExamples(); },
                             "Apps/DemoApp/UltraCanvasSegmentedControlExamples.cpp",
                             "Docs/UltraCanvasSegmentedControl.md")
                .AddVariant("segmentedcontrol", "Bordered Style")
                .AddVariant("segmentedcontrol", "iOS Style")
                .AddVariant("segmentedcontrol", "Flat Style")
                .AddVariant("segmentedcontrol", "Bar Style")
                .AddVariant("segmentedcontrol", "Toggle Mode")
                .AddVariant("segmentedcontrol", "FitContent Width");

        basicBuilder.AddItem("slider", "Slider", "Range and value selection sliders",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateSliderExamples(); },
                            "Examples/UltraCanvasSliderExamples.cpp",
                            "Docs/UltraCanvasSliderExamples.md")
                .AddVariant("slider", "Horizontal Slider")
                .AddVariant("slider", "Vertical Slider")
                .AddVariant("slider", "Range Slider");

        basicBuilder.AddItem("label", "Label", "Text display with formatting and styling",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateLabelExamples(); },
                            "Examples/UltraCanvasLabelExamples.cpp",
                            "Docs/UltraCanvasLabelExamples.md")
                .AddVariant("label", "Basic Label")
                .AddVariant("label", "Header Text")
                .AddVariant("label", "Status Label");

        basicBuilder.AddItem("menu", "Menus", "Various menu types and styles",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateMenuExamples(); },
                             "Examples/UltraCanvasMenuExamples.cpp",
                             "Docs/UltraCanvasMenuExamples.md")
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
                             [this]() { return CreateTabExamples(); },
                            "Examples/UltraCanvasTabExamples.cpp",
                            "Docs/UltraCanvasTabExamples.md")
                .AddVariant("tabs", "Top Tabs")
                .AddVariant("tabs", "Side Tabs")
                .AddVariant("tabs", "Closable Tabs");

        basicBuilder.AddItem("layouts", "Layout System",
                             "Box, Grid, and Flex layout examples",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateLayoutExamples(); },
                             "Apps/DemoApp/UltraCanvasLayoutExamples.cpp",
                             "Docs/UltraCanvasLayoutExamples.md")
                .AddVariant("layouts", "Vertical Box Layout")
                .AddVariant("layouts", "Horizontal Box Layout")
                .AddVariant("layouts", "Grid Layout")
                .AddVariant("layouts", "Flex Layout");

        // ===== EXTENDED FUNCTIONALITY =====
        auto extendedBuilder = DemoCategoryBuilder(this, DemoCategory::ExtendedFunctionality);

        extendedBuilder.AddItem("treeview", "Tree View", "Hierarchical data display with icons",
                                ImplementationStatus::FullyImplemented,
                                [this]() { return CreateTreeViewExamples(); },
                                "Examples/UltraCanvasTreeViewExamples.cpp",
                                "Docs/UltraCanvasTreeViewExamples.md")
                .AddVariant("treeview", "File Explorer Style")
                .AddVariant("treeview", "Multi-Selection Tree")
                .AddVariant("treeview", "Checkable Nodes");

        extendedBuilder.AddItem("tableview", "Spreadsheet View", "Data grid with sorting and editing",
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
                                [this]() { return CreateTextAreaExamples(); },
                                "Examples/UltraCanvasTextAreaExamples.cpp",
                                "Docs/UltraCanvasTextAreaExamples.md")
                .AddVariant("textarea", "C++ Syntax Highlighting")
                .AddVariant("textarea", "Python Syntax Highlighting")
                .AddVariant("textarea", "Pascal Syntax Highlighting")
                .AddVariant("textarea", "Line Numbers Display")
                .AddVariant("textarea", "Theme Support");

        // ===== BITMAP ELEMENTS =====
        auto bitmapBuilder = DemoCategoryBuilder(this, DemoCategory::BitmapElements);

        bitmapBuilder.AddItem("pngimages", "PNG Images", "PNG Image display and manipulation",
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreatePNGExamples(); },
                              "Examples/UltraCanvasBitmapExamples.cpp",
                              "Docs/UltraCanvasBitmapExamples.md")
                .AddVariant("images", "PNG/JPEG Display");
        bitmapBuilder.AddItem("jpegimages", "JPEG Images", "JPEG Image display and manipulation",
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateJPEGExamples(); },
                              "Examples/UltraCanvasBitmapExamples.cpp",
                              "Docs/UltraCanvasBitmapExamples.md")
                .AddVariant("images", "PNG/JPEG Display");
        bitmapBuilder.AddItem("gifimages", "GIF Images", "GIF Image display and manipulation",
                              ImplementationStatus::PartiallyImplemented,
                              [this]() { return CreateBitmapNotImplementedExamples("GIF"); });
        bitmapBuilder.AddItem("avifimages", "AVIF Images", "AVIF Image display and manipulation",
                              ImplementationStatus::PartiallyImplemented,
                              [this]() { return CreateBitmapNotImplementedExamples("AVIF"); });
        bitmapBuilder.AddItem("tiffimages", "TIFF Images", "TIFF Image display and manipulation",
                              ImplementationStatus::PartiallyImplemented,
                              [this]() { return CreateBitmapNotImplementedExamples("TIFF"); });
        bitmapBuilder.AddItem("webpimages", "WEBP Images", "WEBP Image display and manipulation",
                              ImplementationStatus::PartiallyImplemented,
                              [this]() { return CreateBitmapNotImplementedExamples("WEBP"); });
        bitmapBuilder.AddItem("qoiimages", "QOI Images", "QOI Image display and manipulation",
                              ImplementationStatus::PartiallyImplemented,
                              [this]() { return CreateBitmapNotImplementedExamples("QOI"); });
        bitmapBuilder.AddItem("rawimages", "RAW Images", "RAW Image display and manipulation",
                              ImplementationStatus::PartiallyImplemented,
                              [this]() { return CreateBitmapNotImplementedExamples("RAW"); });

        // ===== VECTOR ELEMENTS =====
        auto vectorBuilder = DemoCategoryBuilder(this, DemoCategory::VectorElements);

        vectorBuilder.AddItem("svg", "SVG Graphics", "Scalable vector graphics rendering",
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateSVGVectorExamples(); },
                              "Examples/UltraCanvasSVGExamples.cpp",
                              "Docs/UltraCanvasSVGExamples.md")
                .AddVariant("svg", "SVG File Display")
                .AddVariant("svg", "Interactive SVG")
                .AddVariant("svg", "SVG Animations");

        vectorBuilder.AddItem("xarimages", "XAR Images", "XAR Image display and manipulation",
                              ImplementationStatus::NotImplemented,
                              [this]() { return CreateBitmapNotImplementedExamples("QOI"); });

        vectorBuilder.AddItem("drawing", "Drawing Surface", "Vector drawing and primitives",
                              ImplementationStatus::NotImplemented,
                              [this]() { return CreateVectorExamples(); });

        // ===== CHARTS =====
        auto chartBuilder = DemoCategoryBuilder(this, DemoCategory::Charts);

        chartBuilder.AddItem("linecharts", "Line Chart", "Line chart data visualization",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateLineChartsExamples(); },
                             "Examples/UltraCanvasBasicChartsExamples.cpp",
                             "Docs/UltraCanvasLineChartElement.md");

        chartBuilder.AddItem("barcharts", "Bar Chart", "Bar chart data visualization",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateBarChartsExamples(); },
                             "Examples/UltraCanvasBasicChartsExamples.cpp",
                             "Docs/UltraCanvasBarChartElement.md");

        chartBuilder.AddItem("scattercharts", "Scatter Plot Chart", "Scatter plot chart data visualization",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateScatterPlotChartsExamples(); },
                             "Examples/UltraCanvasBasicChartsExamples.cpp",
                             "Docs/UltraCanvasScatterPlotElement.md");

        chartBuilder.AddItem("areacharts", "Area Chart", "Area chart data visualization",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateAreaChartsExamples(); },
                             "Examples/UltraCanvasBasicChartsExamples.cpp",
                             "Docs/UltraCanvasAreaChartElement.md");

        chartBuilder.AddItem("financialcharts", "Financial Chart", "Stock market OHLC and candlestick charts",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateFinancialChartExamples(); },
                             "Examples/UltraCanvasFinancialChartExamples.cpp",
                             "Docs/UltraCanvasFinancialChart.md")
                .AddVariant("financialcharts", "Candlestick Chart")
                .AddVariant("financialcharts", "OHLC Bar Chart")
                .AddVariant("financialcharts", "Heikin-Ashi Chart")
                .AddVariant("financialcharts", "Volume Analysis")
                .AddVariant("financialcharts", "Multi-Market View");

        chartBuilder.AddItem("divergingcharts", "Diverging Bar Charts", "Likert scale and population pyramid charts",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateDivergingChartExamples(); },
                             "Examples/UltraCanvasDivergingChartExamples.cpp",
                             "Docs/UltraCanvasDivergingChartExamples.md")
                .AddVariant("divergingcharts", "Likert Scale")
                .AddVariant("divergingcharts", "Population Pyramid")
                .AddVariant("divergingcharts", "Tornado Chart");

        chartBuilder.AddItem("waterfallcharts", "Waterfall Charts", "Cumulative flow visualization",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateWaterfallChartExamples(); },
                             "Examples/UltraCanvasWatefallChartExamples.cpp",
                             "Docs/UltraCanvasWatefallChartExamples.md")
                .AddVariant("waterfallcharts", "Revenue Flow")
                .AddVariant("waterfallcharts", "Cash Flow with Subtotals")
                .AddVariant("waterfallcharts", "Performance Impact");

        chartBuilder.AddItem("populationcharts", "Population Chart", "Population chart data visualization",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreatePopulationChartExamples(); },
                             "Examples/UltraCanvasPopulationChartsExamples.cpp",
                             "Docs/UltraCanvasPopulationChartElement.md");

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

        chartBuilder.AddItem("flowchart", "Jitter plot", "Jitter plot",
                             ImplementationStatus::NotImplemented,
                             [this]() { return nullptr; });

        // ===== DIAGRAMS =====
        auto diagramBuilder = DemoCategoryBuilder(this, DemoCategory::Diagrams);

        diagramBuilder.AddItem(
                        "sankey",
                        "Sankey Diagram",
                        "Interactive flow diagrams showing relationships and value distributions",
                        ImplementationStatus::FullyImplemented,
                        [this]() { return CreateSankeyExamples(); },
                        "Examples/UltraCanvasSankeyExamples.cpp",
                        "Docs/UltraCanvasSankeyDiagram.md"
                )
                .AddVariant("sankey", "Energy Flow")
                .AddVariant("sankey", "Financial Flow")
                .AddVariant("sankey", "Web Traffic")
                .AddVariant("sankey", "Custom Data")
                .AddVariant("sankey", "Performance Test");

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

        diagramBuilder.AddItem("venndiagram", "Venn diagram", "Venn diagram",
                               ImplementationStatus::NotImplemented,
                               [this]() { return nullptr; });

        diagramBuilder.AddItem("flowchart", "Flow chart", "Flow chart",
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

        infoBuilder.AddItem("heatmap", "Heat map", "Heat map",
                            ImplementationStatus::NotImplemented,
                            [this]() { return CreateInfoGraphicsExamples(); });

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
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateMarkdownExamples(); });

//        textDocBuilder.AddItem("codeeditor", "Code Editor", "Syntax highlighting text editor",
//                               ImplementationStatus::PartiallyImplemented,
//                               [this]() { return CreateCodeEditorExamples(); })
//                .AddVariant("codeeditor", "C++ Syntax")
//                .AddVariant("codeeditor", "Pascal Syntax")
//                .AddVariant("codeeditor", "COBOL Syntax");

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

        auto toolsBuilder = DemoCategoryBuilder(this, DemoCategory::Tools);

        toolsBuilder.AddItem("qrcode", "QR code", "QR code scanner",
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreatePartiallyImplementedExamples("## QR code"); });

        toolsBuilder.AddItem("barcode", "Bar code", "Bar code",
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreatePartiallyImplementedExamples("## Bar code"); });


        auto modulesBuilder = DemoCategoryBuilder(this, DemoCategory::Modules);
        modulesBuilder.AddItem("pixelfx", "Pixel FX", "Pixel FX",
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreatePixeLXExamples(); });
        modulesBuilder.AddItem("fileloader", "File Loader", "File Loader",
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreateFileLoaderExamples(); });
        modulesBuilder.AddItem("gpio", "GPIO support", "GPIO support",
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreatePartiallyImplementedExamples("## GPIO support"); });
        modulesBuilder.AddItem("iodevicemanager", "IODeviceManager support", "IODeviceManager support",
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreateIODeviceManagerExamples(); });

        std::cout << "✓ Registered " << demoItems.size() << " demo items across "
                  << categoryItems.size() << " categories" << std::endl;
    }

    void UltraCanvasDemoApplication::SetupTreeView() {
        // Setup root node
        TreeNodeData rootData("root", "UltraCanvas Components");
        rootData.leftIcon = TreeNodeIcon("assets/icons/ultracanvas.png", 16, 16);
        TreeNode* rootNode = categoryTreeView->SetRootNode(rootData);

        // Add category nodes
        std::vector<std::pair<DemoCategory, std::string>> categoryNames = {
                {DemoCategory::BasicUI, "Basic UI Elements"},
                {DemoCategory::ExtendedFunctionality, "Extended Functionality"},
                {DemoCategory::BitmapElements, "Bitmap Elements"},
                {DemoCategory::VectorElements, "Vector Graphics"},
                {DemoCategory::Charts, "Charts"},
                {DemoCategory::Diagrams, "Diagrams"},
                {DemoCategory::InfoGraphics, "Info Graphics"},
                {DemoCategory::Graphics3D, "3D Graphics"},
                {DemoCategory::VideoElements, "Video Elements"},
                {DemoCategory::TextDocuments, "Text Documents"},
                {DemoCategory::AudioElements, "Audio Elements"},
                {DemoCategory::Tools, "Tools"},
                {DemoCategory::Modules, "Modules"}
        };


        for (const auto& [category, catName] : categoryNames) {
            TreeNodeData categoryData(
                    "cat_" + std::to_string(static_cast<int>(category)),
                    catName
            );
            auto items = categoryItems[category];
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
        rootNode->FirstChild()->Expand();
        rootNode->FirstChild()->FirstChild()->Expand();
        categoryTreeView->SelectNode(rootNode->FirstChild()->FirstChild());
        OnTreeNodeSelected(rootNode->FirstChild()->FirstChild());
    }

// ===== EVENT HANDLERS =====
    void UltraCanvasDemoApplication::OnTreeNodeSelected(TreeNode* node) {
        if (!node || !node->data.nodeId.length()) return;

        std::string nodeId = node->data.nodeId;

        // Check if this is a demo item (not a category)
        if (demoItems.find(nodeId) != demoItems.end()) {
            DisplayDemoItem(nodeId);
            UpdateStatusDisplay(nodeId);
            UpdateHeaderDisplay(nodeId);
        } else {
            // Category selected - clear display
            ClearDisplay();
            statusLabel->SetText("Category: " + node->data.text + " - Select a specific component to view examples");
            headerContainer->SetDemoTitle("Category: " + node->data.text);
            headerContainer->SetSourceFile("");
            headerContainer->SetDocFile("");        }
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
            placeholder->SetBorders(2.0f);
            placeholder->SetBordersColor(Color(200, 200, 0, 255));
            placeholder->SetPadding(10);

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
    }

    void UltraCanvasDemoApplication::UpdateHeaderDisplay(const std::string& itemId) {
        auto it = demoItems.find(itemId);
        if (it != demoItems.end()) {
            const auto& item = it->second;

            // Set title from description field
            headerContainer->SetDemoTitle(item->description);

            // Set source and doc files
            headerContainer->SetSourceFile(item->demoSource);
            headerContainer->SetDocFile(item->demoDoc);
        }
    }

// ===== UTILITY METHODS =====
    std::string UltraCanvasDemoApplication::GetStatusIcon(ImplementationStatus status) const {
        switch (status) {
            case ImplementationStatus::FullyImplemented: return "assets/icons/check.png";
            case ImplementationStatus::PartiallyImplemented: return "assets/icons/warning-blue.png";
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

        // Show the info window at startup
        ShowInfoWindow();

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
                                                      std::function<std::shared_ptr<UltraCanvasUIElement>()> creator,
                                                      const std::string& sourceFile,
                                                      const std::string& docFile) {
        auto item = std::make_unique<DemoItem>(id, name, description, category, status);
        item->createExample = creator;
        item->demoSource = sourceFile;
        item->demoDoc = docFile;
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