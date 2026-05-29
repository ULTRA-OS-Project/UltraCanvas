// Apps/DemoApp/UltraCanvasDemo.cpp
// Comprehensive demonstration program implementation
// Version: 1.0.1
// Last Modified: 2026-05-01
// Author: UltraCanvas Framework

#include "UltraCanvasLayoutCompat.h"
#include "UltraCanvasDemo.h"
#include "UltraCanvasTextArea.h"
#include "Plugins/Text/UltraCanvasMarkdown.h"
#include <iostream>
#include <sstream>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {
    DemoLegendContainer::DemoLegendContainer(const std::string& identifier, float x, float y, float width, float height)
            : UltraCanvasContainer(identifier, x, y, width, height) {

        // Set container style
//        SetBorders(1, Color(200, 200, 200, 255));
        SetBackgroundColor(Color(245, 245, 245, 255));

        // Create legend title
        legendTitle = std::make_shared<UltraCanvasLabel>("LegendTitle", 10, 5, width - 20, 20);
        legendTitle->SetText("Component Status Legend");
        legendTitle->SetFontSize(12);
        legendTitle->SetFontWeight(FontWeight::Bold);
        legendTitle->SetTextColor(Color(80, 80, 80, 255));
        AddChild(legendTitle);

        // Implemented status (row 1)
        implementedIcon = std::make_shared<UltraCanvasImageElement>("ImplementedIcon", 10, 30, 16, 16);
        AddChild(implementedIcon);

        implementedLabel = std::make_shared<UltraCanvasLabel>("ImplementedLabel", 32, 28, width - 42, 16);
        implementedLabel->SetText("Fully Implemented");
        implementedLabel->SetFontSize(11);
        implementedLabel->SetTextColor(Color(0, 150, 0, 255));
        AddChild(implementedLabel);

        // Partially implemented status (row 2)
        partialIcon = std::make_shared<UltraCanvasImageElement>("PartialIcon", 10, 50, 16, 16);
        AddChild(partialIcon);

        partialLabel = std::make_shared<UltraCanvasLabel>("PartialLabel", 32, 48, width - 42, 16);
        partialLabel->SetText("Partially Implemented");
        partialLabel->SetFontSize(11);
        partialLabel->SetTextColor(Color(0x21, 0x96, 0xf3, 255));
        AddChild(partialLabel);

        // Not implemented status (row 3)
        notImplementedIcon = std::make_shared<UltraCanvasImageElement>("NotImplementedIcon", 10, 70, 16, 16);
        AddChild(notImplementedIcon);

        notImplementedLabel = std::make_shared<UltraCanvasLabel>("NotImplementedLabel", 32, 68, width - 42, 16);
        notImplementedLabel->SetText("Not Implemented Yet");
        notImplementedLabel->SetFontSize(11);
        notImplementedLabel->SetTextColor(Color(200, 0, 0, 255));
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

    DemoHeaderContainer::DemoHeaderContainer(const std::string& identifier,
                                             float x, float y, float width, float height)
            : UltraCanvasContainer(identifier, x, y, width, height) {
        layout.SetFlexRow().SetFlexGap(10);

        // Create title label (left side)
        titleLabel = std::make_shared<UltraCanvasLabel>("HeaderTitle");
        titleLabel->SetFontSize(14);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetText("Demo Title");
        titleLabel->SetMargin(2,0,0,0);
        AddChild(titleLabel);

        AddStretchSpacer(1);

        // Create documentation button (right side)
        docButton = std::make_shared<UltraCanvasImageElement>("DocBtn", 21, 21);
        docButton->LoadFromFile(NormalizePath(GetResourcesDir() + "media/icons/text.png"));
        docButton->SetVisible(false);  // Initially disabled
        docButton->SetClickable(true);
        docButton->onClick = [this]() { ShowDocumentationWindow(); };
        AddChild(docButton);

        // Create source button (right side)
        sourceButton = std::make_shared<UltraCanvasImageElement>("SourceBtn", 21, 28);
        sourceButton->LoadFromFile(NormalizePath(GetResourcesDir() + "media/icons/c-plus-plus-icon.png"));
        sourceButton->SetVisible(false);  // Initially disabled
        sourceButton->SetClickable(true);
        sourceButton->onClick = [this]() { ShowSourceWindow(); };
        AddChild(sourceButton);

        // Divider line pinned at the bottom of the header. Absolute-positioned
        // so it doesn't participate in the in-flow flex row (otherwise its
        // width:100% main-axis hypothetical would push the row into shrink
        // mode and the stretch spacer above would collapse to 0).
        dividerLine = std::make_shared<UltraCanvasUIElement>("Divider");
        dividerLine->size.height = CSSLayout::Dimension::Px(2);
        dividerLine->SetBackgroundColor(Color(200, 200, 200, 255));
        dividerLine->layoutItem.positionType = CSSLayout::PositionType::Absolute;
        dividerLine->layoutItem.position = CSSLayout::Position{
            /*top   */ CSSLayout::Dimension::Auto(),
            /*right */ CSSLayout::Dimension::Px(0),
            /*bottom*/ CSSLayout::Dimension::Px(0),
            /*left  */ CSSLayout::Dimension::Px(0),
        };
        AddChild(dividerLine);

        // Set container style
        ContainerStyle containerStyle;
        containerStyle.forceShowHorizontalScrollbar = false;
        containerStyle.forceShowVerticalScrollbar = false;
        containerStyle.autoShowScrollbars = false;
        SetContainerStyle(containerStyle);

        SetBackgroundColor(Color(245, 245, 245, 255));
        SetPadding(5,10,5,10);
        SetBorderBottom(2, Colors::Gray);

//        auto headerLayout = CreateHBoxLayout(this);
//        headerLayout->SetSpacing(10);
//        headerLayout->AddUIElement(titleLabel)->SetCrossAlignment(LayoutAlignment::Center);
//        headerLayout->AddStretch(1);
//        headerLayout->AddUIElement(docButton)->SetCrossAlignment(LayoutAlignment::Center);
//        headerLayout->AddUIElement(sourceButton)->SetCrossAlignment(LayoutAlignment::Center);
    }

    void DemoHeaderContainer::SetDemoTitle(const std::string& title) {
        if (titleLabel) {
            titleLabel->SetText(title);
        }
    }

    void DemoHeaderContainer::SetSourceFile(const std::string& sourceFile) {
        if (!sourceFile.empty()) {
            currentSourceFile = NormalizePath(GetResourcesDir()+sourceFile);
        } else {
            currentSourceFile.clear();
        }
        if (sourceButton) {
            sourceButton->SetVisible(!sourceFile.empty());
        }
    }

    void DemoHeaderContainer::SetDocFile(const std::string& docFile) {
        if (!docFile.empty()) {
            currentDocFile = NormalizePath(GetResourcesDir()+docFile);
        } else {
            currentDocFile.clear();
        }
        if (docButton) {
            docButton->SetVisible(!docFile.empty());
        }
    }

    std::string DemoHeaderContainer::LoadFileContent(const std::string& filePath) {
        if (filePath.empty()) return "";

        std::ifstream file(filePath);
        if (!file.is_open()) {
            debugOutput << "Failed to open file: " << filePath << std::endl;
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

        sourceWindow = CreateWindow(config);
        if (!sourceWindow->IsCreated()) {
            debugOutput << "Failed to create source window" << std::endl;
            return;
        }

        // Create text area for source code with syntax highlighting
        auto textArea = std::make_shared<UltraCanvasTextArea>("SourceCode", 5, 5, 1190, 590);
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

        docWindow = CreateWindow(config);
        if (!docWindow->IsCreated()) {
            debugOutput << "Failed to create documentation window" << std::endl;
            return;
        }
        // Create text area for documentation
        auto markDownTextArea = std::make_shared<UltraCanvasMarkdownDisplay>("Documentation", 5, 5, 1190, 590);
        markDownTextArea->SetMarkdownText(content);

        docWindow->SetEventCallback([this](const UCEvent& event) {
            if (event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) {
                ((UltraCanvasWindow *)event.targetWindow)->Close();
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
        debugOutput << "Initializing UltraCanvas Demo Application..." << std::endl;

        // Create main window using proper configuration
        WindowConfig config;
        config.title = "UltraCanvas Framework - Component Demonstration";
        config.width = 1400;
        config.height = 880;
        config.resizable = true;
        config.type = WindowType::Standard;

        mainWindow = CreateWindow(config);
        debugOutput << "Creating Main window.." << std::endl;
        if (!mainWindow->IsCreated()) {
            debugOutput << "Failed to create main window" << std::endl;
            return false;
        }
        debugOutput << "Main window created" << std::endl;
        // Calculate positions for adjusted layout
        const int treeViewHeight = 740;  // Reduced from 840 to make room for legend
        const int legendHeight = 95;     // Height for legend container
        const int treeViewWidth = 350;   // Width for both treeview and legend

// Create tree view for categories (left side, reduced height)
        categoryTreeView = std::make_shared<UltraCanvasTreeView>("CategoryTree");
        categoryTreeView->SetRowHeight(24);
        categoryTreeView->SetSelectionMode(TreeSelectionMode::Single);
        categoryTreeView->SetLineStyle(TreeLineStyle::Solid);
        categoryTreeView->SetShowFirstChildOnExpand(true);
        categoryTreeView->SetAutoExpandSelectedNode(true);
        categoryTreeView->SetPadding(1,3,1,3);

        debugOutput << "categoryTreeView created" << std::endl;

        // Create legend container below tree view
        legendContainer = std::make_shared<DemoLegendContainer>("LegendContainer", 0, 0, 100, legendHeight);
        legendContainer->SetBorderTop(1, Colors::Gray);
        SetupLegendContainer();

        // No-size constructor — let the grid cell decide categoryContainer's
        // dimensions. If we passed 100, 100 here, the engine would treat
        // size = 100×100 as a CSS-explicit width/height and shrink the
        // sidebar inside its 350-pixel grid column.
        auto categoryContainer = std::make_shared<UltraCanvasContainer>("catcont");

        mainContainer = std::make_shared<UltraCanvasContainer>("MainDisplayArea");
        mainContainer->SetBorderLeft(1, Colors::Gray);

        // Header sized by its parent row — no explicit w/h.
        headerContainer = std::make_shared<DemoHeaderContainer>("HeaderContainer", 0, 0, 0, 0);

        // Create display container (below header)
        displayContainer = std::make_shared<UltraCanvasContainer>("DisplayArea", 0, 40, 1028, 785);
        displayContainer->SetBackgroundColor(Colors::White);

        // Create status label (bottom left)
        statusLabel = std::make_shared<UltraCanvasLabel>("StatusLabel", 10, 850, 850, 25);
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

        categoryContainer->layout.SetFlexColumn();
        categoryTreeView->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch).SetJustifySelf(CSSLayout::JustifySelf::Stretch);
        categoryContainer->AddChild(categoryTreeView);
        categoryContainer->AddChild(legendContainer);
//        auto categoryContainerLayout = CreateVBoxLayout(categoryContainer.get());
//        categoryContainerLayout->AddUIElement(categoryTreeView, 1)->SetWidthMode(SizeMode::Fill);
//        categoryContainerLayout->AddUIElement(legendContainer)->SetWidthMode(SizeMode::Fill);

        mainContainer->layout.SetFlexColumn();
        mainContainer->AddChild(headerContainer);
        displayContainer->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch).SetJustifySelf(CSSLayout::JustifySelf::Stretch);
        displayContainer->layout.SetFlexColumn();
        mainContainer->AddChild(displayContainer);
//        auto mainContainerLayout = CreateVBoxLayout(mainContainer.get());
//        mainContainerLayout->AddUIElement(headerContainer)->SetWidthMode(SizeMode::Fill)->SetFixedHeight(40);
//        mainContainerLayout->AddUIElement(displayContainer, 1)->SetWidthMode(SizeMode::Fill);

//        auto displayContainerLayout = CreateVBoxLayout(displayContainer.get());

        mainWindow->layout
            .SetGrid()
            .SetGridColumns({CSSLayout::GridTrackSize{.kind=CSSLayout::GridTrackSizeKind::Fixed, .value=CSSLayout::Dimension::Px(350)},
                             CSSLayout::GridTrackSize{.kind=CSSLayout::GridTrackSizeKind::Auto}})
             // Row 0 = Fr(1) so it consumes all available vertical space
             // (otherwise Auto would collapse the sidebar to ~4 tree rows).
             // Row 1 = the 25-pixel status bar at the bottom.
             .SetGridRows({CSSLayout::GridTrackSize{.kind=CSSLayout::GridTrackSizeKind::Fr,    .value=CSSLayout::Dimension::Fr(1)},
                           CSSLayout::GridTrackSize{.kind=CSSLayout::GridTrackSizeKind::Fixed, .value=CSSLayout::Dimension::Px(25)}});

        categoryContainer->layoutItem.SetGridRowColSimplified(0, 0);
        mainWindow->AddChild(categoryContainer);

        mainContainer->layoutItem.SetGridRowColSimplified(0, 1);
        mainWindow->AddChild(mainContainer);

        statusLabel->layoutItem.SetGridRowColSimplified(1, 0, 1, 2);
        mainWindow->AddChild(statusLabel);

//        auto mainLayout = CreateGridLayout(mainWindow.get(), 2, 2);
//        mainLayout->SetColumnDefinition(0, GridRowColumnDefinition::Fixed(350));
//        mainLayout->SetColumnDefinition(1, GridRowColumnDefinition::Star(1));
//        mainLayout->SetRowDefinition(0, GridRowColumnDefinition::Star(1));
//        mainLayout->SetRowDefinition(1, GridRowColumnDefinition::Fixed(25));
//
//        mainLayout->AddUIElement(categoryContainer, 0, 0)->SetSizeMode(SizeMode::Fill, SizeMode::Fill);
//        mainLayout->AddUIElement(mainContainer, 0, 1)->SetSizeMode(SizeMode::Fill, SizeMode::Fill);
//        mainLayout->AddUIElement(statusLabel, 1, 0, 1, 2)->SetSizeMode(SizeMode::Fill, SizeMode::Fill);

        debugOutput << "✓ Demo application initialized successfully" << std::endl;
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
        debugOutput << "Registering demo items..." << std::endl;

        // ===== BASIC UI ELEMENTS =====
        auto basicBuilder = DemoCategoryBuilder(this, DemoCategory::BasicUI);

        basicBuilder.AddItem("menu", "Menus", "Various menu types and styles",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateMenuExamples(); },
                             "DemoApp/UltraCanvasMenuExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasMenuExamples.md")
                .AddVariant("menu", "Context Menu")
                .AddVariant("menu", "Main Menu Bar")
                .AddVariant("menu", "Popup Menu")
                .AddVariant("menu", "Submenu Navigation")
                .AddVariant("menu", "Checkbox/Radio Items")
                .AddVariant("menu", "Styled Menus");

        basicBuilder.AddItem("toolbar", "Toolbar", "Tool and action bars",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateToolbarExamples(); },
                             "Apps/DemoApp/UltraCanvasToolbarExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasToolbarExamples.md")
                .AddVariant("toolbar", "Horizontal Toolbar")
                .AddVariant("toolbar", "Vertical Toolbar")
                .AddVariant("toolbar", "Ribbon Style");

        basicBuilder.AddItem("tabs", "Tabs", "Tabbed interface containers",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateTabExamples(); },
                             "DemoApp/UltraCanvasTabExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasTabExamples.md")
                .AddVariant("tabs", "Top Tabs")
                .AddVariant("tabs", "Side Tabs")
                .AddVariant("tabs", "Closable Tabs");

        basicBuilder.AddItem("splitpane", "Split Pane",
                             "Resizable horizontal/vertical pane splitter (VSCode-style)",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateSplitPaneExamples(); },
                             "Apps/DemoApp/UltraCanvasSplitPaneExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasSplitPane.md")
                .AddVariant("splitpane", "Horizontal Split")
                .AddVariant("splitpane", "Vertical Split")
                .AddVariant("splitpane", "Nested Splits");

        basicBuilder.AddItem("layouts", "Layout System",
                             "Box, Grid, and Flex layout examples",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateLayoutExamples(); },
                             "Apps/DemoApp/UltraCanvasLayoutExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasLayoutExamples.md")
                .AddVariant("layouts", "Vertical Box Layout")
                .AddVariant("layouts", "Horizontal Box Layout")
                .AddVariant("layouts", "Grid Layout")
                .AddVariant("layouts", "Flex Layout");

        basicBuilder.AddItem("segmentedcontrol", "Segmented Control",
                             "Compact control for selecting between mutually exclusive options",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateSegmentedControlExamples(); },
                             "Apps/DemoApp/UltraCanvasSegmentedControlExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasSegmentedControlExamples.md")
                .AddVariant("segmentedcontrol", "Bordered Style")
                .AddVariant("segmentedcontrol", "iOS Style")
                .AddVariant("segmentedcontrol", "Flat Style")
                .AddVariant("segmentedcontrol", "Bar Style")
                .AddVariant("segmentedcontrol", "Toggle Mode")
                .AddVariant("segmentedcontrol", "FitContent Width");

        basicBuilder.AddItem("textinput", "Text Input", "Text input fields with validation and formatting",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateTextInputExamples(); },
                             "DemoApp/UltraCanvasTextInputExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasTextInputExamples.md"
                )
                .AddVariant("textinput", "Single Line Input")
                .AddVariant("textinput", "Multi-line Text Area")
                .AddVariant("textinput", "Password Field")
                .AddVariant("textinput", "Numeric Input");

        basicBuilder.AddItem("autocomplete", "AutoComplete", "Text input with auto-complete suggestions",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateAutoCompleteExamples(); },
                             "Apps/DemoApp/UltraCanvasAutoCompleteExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasAutoCompleteExamples.md")
                .AddVariant("autocomplete", "Static Items")
                .AddVariant("autocomplete", "Dynamic Provider")
                .AddVariant("autocomplete", "Interactive Demo");

        basicBuilder.AddItem("label", "Label", "Text display with formatting and styling",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateLabelExamples(); },
                             "DemoApp/UltraCanvasLabelExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasLabelExamples.md")
                .AddVariant("label", "Basic Label")
                .AddVariant("label", "Header Text")
                .AddVariant("label", "Status Label");

        basicBuilder.AddItem("button", "Button", "Interactive buttons with various styles and states",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateButtonExamples(); },
                             "DemoApp/UltraCanvasButtonExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasButtonExamples.md"
                             )
                .AddVariant("button", "Standard Button")
                .AddVariant("button", "Icon Button")
                .AddVariant("button", "Toggle Button")
                .AddVariant("button", "Three-Section Button");

        basicBuilder.AddItem("dropdown", "Dropdown / ComboBox", "Dropdown selection controls",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateDropdownExamples(); },
                            "DemoApp/UltraCanvasDropDownExamples.cpp",
                            "Docs/UltraCanvas/UltraCanvasDropDownExamples.md")
                .AddVariant("dropdown", "Simple Dropdown")
                .AddVariant("dropdown", "Editable ComboBox")
                .AddVariant("dropdown", "Multi-Select");

        basicBuilder
                .AddItem("checkbox", "Checkbox / Radio / Switch",
                         "Interactive checkbox controls with multiple states and styles",
                         ImplementationStatus::FullyImplemented,
                         [this]() { return CreateCheckboxExamples(); },
                         "Apps/DemoApp/UltraCanvasCheckboxExamples.cpp",
                         "Docs/UltraCanvas/UltraCanvasCheckbox.md")
                .AddVariant("checkbox", "Standard Checkbox")
                .AddVariant("checkbox", "Tri-State Checkbox")
                .AddVariant("checkbox", "Switch Toggle")
                .AddVariant("checkbox", "Radio Button");

        basicBuilder.AddItem("slider", "Slider", "Range and value selection sliders",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateSliderExamples(); },
                            "DemoApp/UltraCanvasSliderExamples.cpp",
                            "Docs/UltraCanvas/UltraCanvasSliderExamples.md")
                .AddVariant("slider", "Horizontal Slider")
                .AddVariant("slider", "Vertical Slider")
                .AddVariant("slider", "Range Slider");

        basicBuilder.AddItem("breadcrumb", "Breadcrumb",
                             "Hierarchical path navigation with clickable segments",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateBreadcrumbExamples(); },
                             "Apps/DemoApp/UltraCanvasBreadcrumbExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasBreadcrumbExamples.md")
                .AddVariant("breadcrumb", "Default Style")
                .AddVariant("breadcrumb", "Compact Style")
                .AddVariant("breadcrumb", "Pills Style")
                .AddVariant("breadcrumb", "File Explorer Style")
                .AddVariant("breadcrumb", "Web Docs Style")
                .AddVariant("breadcrumb", "Separator Styles")
                .AddVariant("breadcrumb", "Dropdown Items")
                .AddVariant("breadcrumb", "Icons")
                .AddVariant("breadcrumb", "Live Navigation")
                .AddVariant("breadcrumb", "Collapse Overflow")
                .AddVariant("breadcrumb", "Ellipsize Overflow")
                .AddVariant("breadcrumb", "ShrinkText Overflow")
                .AddVariant("breadcrumb", "Rounded Strip");

        basicBuilder.AddItem("gauges", "Gauges", "Gauges",
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreatePartiallyImplementedExamples("Gauges is not ready yet"); });

        // ===== EXTENDED FUNCTIONALITY =====
        auto extendedBuilder = DemoCategoryBuilder(this, DemoCategory::ExtendedFunctionality);

        extendedBuilder.AddItem("textarea", "Advanced Text Area", "Advanced text editing with syntax highlighting",
                                ImplementationStatus::FullyImplemented,
                                [this]() { return CreateTextAreaExamples(); },
                                "DemoApp/UltraCanvasTextAreaExamples.cpp",
                                "Docs/UltraCanvas/UltraCanvasTextAreaExamples.md")
                .AddVariant("textarea", "C++ Syntax Highlighting")
                .AddVariant("textarea", "Python Syntax Highlighting")
                .AddVariant("textarea", "Pascal Syntax Highlighting")
                .AddVariant("textarea", "Line Numbers Display")
                .AddVariant("textarea", "Theme Support");

        extendedBuilder.AddItem("treeview", "Tree View", "Hierarchical data display with icons",
                                ImplementationStatus::FullyImplemented,
                                [this]() { return CreateTreeViewExamples(); },
                                "DemoApp/UltraCanvasTreeViewExamples.cpp",
                                "Docs/UltraCanvas/UltraCanvasTreeViewExamples.md")
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
                                ImplementationStatus::FullyImplemented,
                                [this]() { return CreateListViewExamples(); },
                                "Apps/DemoApp/UltraCanvasListViewExamples.cpp",
                                "Docs/UltraCanvas/UltraCanvasListViewExamples.md")
                .AddVariant("listview", "Simple List")
                .AddVariant("listview", "Icon List")
                .AddVariant("listview", "Detail View");

        // ===== BITMAP ELEMENTS =====
        auto bitmapBuilder = DemoCategoryBuilder(this, DemoCategory::BitmapElements);

        bitmapBuilder.AddItem("pngimages", "PNG Images", "PNG Image display and manipulation",
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("PNG", NormalizePath(GetResourcesDir() + "media/images/dice.png")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md")
                .AddVariant("images", "PNG/JPEG Display");
        bitmapBuilder.AddItem("jpegimages", "JPEG Images", "JPEG Image display and manipulation",
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("JPG", NormalizePath(GetResourcesDir() + "media/images/dice.jpg")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md")
                .AddVariant("images", "PNG/JPEG Display");

// AVIF Images
        bitmapBuilder.AddItem("avifimages", "AVIF Images",
                              "AVIF next-gen format with superior compression and HDR support",
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("AVIF", NormalizePath(GetResourcesDir() + "media/images/dice.avif")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md")
                .AddVariant("images", "Modern Formats");

        // WEBP Images
        bitmapBuilder.AddItem("webpimages", "WEBP Images",
                              "Google WebP format with excellent compression and web optimization",
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("WEBP", NormalizePath(GetResourcesDir() + "media/images/dice.webp")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md")
                .AddVariant("images", "Modern Formats");

        // HEIF Images
        bitmapBuilder.AddItem("heifimages", "HEIF/HEIC Images",
                              "HEIF high efficiency format with HEVC compression",
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("HEIC", NormalizePath(GetResourcesDir() + "media/images/dice.heic")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md")
                .AddVariant("images", "Modern Formats");

        // GIF Images
        bitmapBuilder.AddItem("gifimages", "GIF Images",
                              "GIF animated format with 256 color palette",
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("GIF", NormalizePath(GetResourcesDir() + "media/images/dice.gif")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md")
                .AddVariant("images", "Animation Support");

        // TIFF Images
        bitmapBuilder.AddItem("tiffimages", "TIFF Images",
                              "TIFF professional format for archival and print",
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("TIFF", NormalizePath(GetResourcesDir() + "media/images/dice.tiff")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md")
                .AddVariant("images", "Professional Formats");

        // BMP Images
        bitmapBuilder.AddItem("bmpimages", "BMP Images",
                              "BMP Windows native bitmap format",
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("BMP", NormalizePath(GetResourcesDir() + "media/images/dice.bmp")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md")
                .AddVariant("images", "Legacy Formats");

        // QOI Images (kept as partially implemented if it exists)
        bitmapBuilder.AddItem("qoiimages", "QOI Images",
                              "QOI Image display and manipulation",
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateBitmapFormatDemoPage("QOI", NormalizePath(GetResourcesDir() + "media/images/dice.qoi")); },
                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md");

        // PSP Images (Paint Shop Pro native format with layers and vector objects)
//        bitmapBuilder.AddItem("pspimages", "PSP Images",
//                              "PSP Paint Shop Pro native layered image format",
//                              ImplementationStatus::FullyImplemented,
//                              [this]() { return CreateBitmapFormatDemoPage("PSP", NormalizePath(GetResourcesDir() + "media/images/dice.pspimage")); },
//                              "DemoApp/UltraCanvasBitmapFormatDemo.cpp",
//                              "Docs/UltraCanvas/UltraCanvasBitmapExamples.md");

        bitmapBuilder.AddItem("imageperformance", "Image Performance Test",
                              "Benchmark image loading, decompression, and rendering speed",
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateImagePerformanceTest(); },
                              "Apps/DemoApp/UltraCanvasImagePerformanceTest.cpp",
                              "Docs/UltraCanvas/UltraCanvasImagePerformanceTest.md")
                .AddVariant("imageperformance", "Full Pipeline Test")
                .AddVariant("imageperformance", "Decompress + Draw Test")
                .AddVariant("imageperformance", "Draw Only Test");

        // ===== VECTOR ELEMENTS =====
        auto vectorBuilder = DemoCategoryBuilder(this, DemoCategory::VectorElements);

        vectorBuilder.AddItem("svg", "SVG Graphics", "Scalable vector graphics rendering",
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateSVGVectorExamples(); },
                              "DemoApp/UltraCanvasSVGExamples.cpp",
                              "Docs/UltraCanvas/UltraCanvasSVGExamples.md")
                .AddVariant("svg", "SVG File Display")
                .AddVariant("svg", "Interactive SVG")
                .AddVariant("svg", "SVG Animations");
#ifdef ULTRACANVAS_HAS_CDR_PLUGIN
        vectorBuilder.AddItem("cdrimages", "CDR Images", "CDR (CorelDraw) images display and manipulation",
                              ImplementationStatus::FullyImplemented,
                              [this]() { return CreateCDRVectorExamples(); },
                              "Apps/DemoApp/UltraCanvasCDRExamples.cpp",
                              "Docs/UltraCanvas/UltraCanvasCDRExamples.md");
#endif
#ifdef ULTRACANVAS_HAS_XAR_PLUGIN
        vectorBuilder.AddItem("xarimages", "XAR Images", "XAR Image display and manipulation",
                              ImplementationStatus::PartiallyImplemented,
                              [this]() { return CreateXARVectorExamples(); });
#endif

        vectorBuilder.AddItem("drawing", "Drawing Surface", "Vector drawing and primitives",
                              ImplementationStatus::PartiallyImplemented,
                              [this]() { return CreateVectorExamples(); });

        // ===== CHARTS =====
        auto chartBuilder = DemoCategoryBuilder(this, DemoCategory::Charts);

        chartBuilder.AddItem("linecharts", "Line Chart", "Line chart data visualization",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateLineChartsExamples(); },
                             "DemoApp/UltraCanvasBasicChartsExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasLineChartElement.md");

        chartBuilder.AddItem("barcharts", "Bar Chart", "Bar chart data visualization",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateBarChartsExamples(); },
                             "DemoApp/UltraCanvasBasicChartsExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasBarChartElement.md");

        chartBuilder.AddItem("scattercharts", "Scatter Plot Chart", "Scatter plot chart data visualization",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateScatterPlotChartsExamples(); },
                             "DemoApp/UltraCanvasBasicChartsExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasScatterPlotElement.md");

        chartBuilder.AddItem("areacharts", "Area Chart", "Area chart data visualization",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateAreaChartsExamples(); },
                             "DemoApp/UltraCanvasBasicChartsExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasAreaChartElement.md");

        chartBuilder.AddItem("piecharts", "Pie Chart", "Pie chart data visualization",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreatePieChartExamples(); },
                             "DemoApp/UltraCanvasPieChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasPieChartExamples.md");

        chartBuilder.AddItem("financialcharts", "Candlestick Chart", "Stock market OHLC and candlestick charts",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateFinancialChartExamples(); },
                             "DemoApp/UltraCanvasFinancialChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasFinancialChart.md")
                .AddVariant("financialcharts", "Candlestick Chart")
                .AddVariant("financialcharts", "OHLC Bar Chart")
                .AddVariant("financialcharts", "Heikin-Ashi Chart")
                .AddVariant("financialcharts", "Volume Analysis")
                .AddVariant("financialcharts", "Multi-Market View");

        chartBuilder.AddItem("divergingcharts", "Diverging Bar Charts", "Likert scale and population pyramid charts",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateDivergingChartExamples(); },
                             "DemoApp/UltraCanvasDivergingChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasDivergingChartExamples.md")
                .AddVariant("divergingcharts", "Likert Scale")
                .AddVariant("divergingcharts", "Population Pyramid")
                .AddVariant("divergingcharts", "Tornado Chart");

        chartBuilder.AddItem("waterfallcharts", "Waterfall Charts", "Cumulative flow visualization",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateWaterfallChartExamples(); },
                             "DemoApp/UltraCanvasWatefallChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasWatefallChartExamples.md")
                .AddVariant("waterfallcharts", "Revenue Flow")
                .AddVariant("waterfallcharts", "Cash Flow with Subtotals")
                .AddVariant("waterfallcharts", "Performance Impact");

        chartBuilder.AddItem("populationcharts", "Population Chart", "Population chart data visualization",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreatePopulationChartExamples(); },
                             "DemoApp/UltraCanvasPopulationChartExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasPopulationChartExamples.md");

        chartBuilder.AddItem("sunburstcharts", "Sunburst Chart", "Sunburst Chart",
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreatePartiallyImplementedExamples("Sunburst Chart is not ready yet"); });

        chartBuilder.AddItem("ganttcharts", "Gantt Chart", "Gantt Chart",
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreatePartiallyImplementedExamples("Gantt Chart is not ready yet"); });

        chartBuilder.AddItem("quadrantcharts", "Quadrant Chart", "Quadrant Chart",
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreatePartiallyImplementedExamples("Quadrant Chart is not ready yet"); });

        chartBuilder.AddItem("circularcharts", "Circular Chart", "Circular Chart",
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreatePartiallyImplementedExamples("Circular Chart is not ready yet"); });

        chartBuilder.AddItem("polarcharts", "Polar Chart", "Polar Chart",
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreatePartiallyImplementedExamples("Polar Chart is not ready yet"); });

        chartBuilder.AddItem("heatmapchart", "Heat map", "Heat map",
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreatePartiallyImplementedExamples("HeatMap Chart is not ready yet"); });

        chartBuilder.AddItem("jitterchart", "Jitter chart", "Jitter chart",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateJitterPlotExamples(); },
                             "Apps/DemoApp/UltraCanvasJitterPlotExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasJitterPlotExamples.md");

        chartBuilder.AddItem("dumbbell", "Dumbbell chart", "Dumbbell chart",
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreatePartiallyImplementedExamples("Dumbbell Chart is not ready yet"); });

        chartBuilder.AddItem("radarcharts", "Radar Chart", "Radar Chart",
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreatePartiallyImplementedExamples("Radar Chart is not ready yet"); });

        chartBuilder.AddItem("bubblecharts", "Bubble Chart", "Bubble Chart",
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreatePartiallyImplementedExamples("Bubble Chart is not ready yet"); });

        chartBuilder.AddItem("contourplot", "Contour plot", "Contour plot",
                             ImplementationStatus::NotImplemented,
                             [this]() { return CreatePartiallyImplementedExamples("Contour plot is not ready yet"); });

        // ===== DIAGRAMS =====
        auto diagramBuilder = DemoCategoryBuilder(this, DemoCategory::Diagrams);

        diagramBuilder.AddItem(
                        "sankey",
                        "Sankey Flow",
                        "Interactive flow diagrams showing relationships and value distributions",
                        ImplementationStatus::FullyImplemented,
                        [this]() { return CreateSankeyExamples(); },
                        "DemoApp/UltraCanvasSankeyExamples.cpp",
                        "Docs/UltraCanvas/UltraCanvasSankeyExamples.md"
                )
                .AddVariant("sankey", "Energy Flow")
                .AddVariant("sankey", "Financial Flow")
                .AddVariant("sankey", "Web Traffic")
                .AddVariant("sankey", "Custom Data")
                .AddVariant("sankey", "Performance Test");

//        diagramBuilder.AddItem("plantuml", "PlantUML", "UML and diagram generation",
//                               ImplementationStatus::NotImplemented,
//                               [this]() { return CreateDiagramExamples(); })
//                .AddVariant("plantuml", "Class Diagrams")
//                .AddVariant("plantuml", "Sequence Diagrams")
//                .AddVariant("plantuml", "Activity Diagrams");

        diagramBuilder.AddItem("flowchart", "Flow chart", "Interactive flowchart with decision points",
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateFlowChartExamples(); },
                               "Apps/DemoApp/UltraCanvasFlowChartExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasFlowChartExamples.md");

        diagramBuilder.AddItem("venndiagram", "Venn Diagram", "Interactive Venn diagram for set visualization",
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateVennDiagramExamples(); },
                               "Apps/DemoApp/UltraCanvasVennDiagramExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasVennDiagramExamples.md");

        diagramBuilder.AddItem("dendrogram", "Dendrogram", "Interactive dendrogram / phylogenetic tree visualization",
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateDendrogramExamples(); },
                               "Apps/DemoApp/UltraCanvasDendrogramExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasDendrogramExamples.md");

        diagramBuilder.AddItem("treemap", "TreeMap", "Hierarchical TreeMap data visualization",
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateTreeMapExamples(); },
                               "Apps/DemoApp/UltraCanvasTreeMapExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasTreeMapExamples.md");

    diagramBuilder.AddItem("blockdiagram", "Block Diagram", "Interactive block diagram with shapes and connections",
                           ImplementationStatus::FullyImplemented,
                           [this]() { return CreateBlockDiagramExamples(); },
                           "Apps/DemoApp/UltraCanvasBlockDiagramExamples.cpp",
                           "Docs/UltraCanvas/UltraCanvasBlockDiagramExamples.md");

    diagramBuilder.AddItem("nodediagram", "Node Diagram", "Network/graph visualization with nodes and edges",
                           ImplementationStatus::FullyImplemented,
                           [this]() { return CreateNodeDiagramExamples(); },
                           "Apps/DemoApp/UltraCanvasNodeDiagramExamples.cpp",
                           "Docs/UltraCanvas/UltraCanvasNodeDiagramExamples.md");

    diagramBuilder.AddItem("gourcetree", "Force-Directed Tree", "Radial tree visualization for file systems inspired by Gource",
                           ImplementationStatus::FullyImplemented,
                           [this]() { return CreateGourceTreeExamples(); },
                           "Apps/DemoApp/UltraCanvasGourceTreeExamples.cpp",
                           "Docs/UltraCanvas/UltraCanvasGourceTreeExamples.md");
    diagramBuilder.AddItem("adjacencydiagrams", "Adjacency Diagram", "Architectural Adjacency Diagram",
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateAdjacencyDiagramExamples(); },
                               "Apps/DemoApp/UltraCanvasAdjacencyDiagramExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasAdjacencyDiagramExamples.md");
    diagramBuilder.AddItem("arcdiagrams", "Arc Diagram", "Arc Diagram",
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateArcDiagramExamples(); },
                               "Apps/DemoApp/UltraCanvasArcDiagramExamples.cpp",
                               "Docs/UltraCanvas/UltraCanvasArcDiagramExamples.md");
    diagramBuilder.AddItem("mindmap", "MindMap", "MindMap",
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreatePartiallyImplementedExamples("MindMap is not ready yet"); });
    diagramBuilder.AddItem("kanbandiagram", "Kanban Diagram", "Kanban Diagram",
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreatePartiallyImplementedExamples("Kanban Diagram is not ready yet"); });
    diagramBuilder.AddItem("packetdiagram", "Packet Diagram", "Packet Diagram",
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreatePartiallyImplementedExamples("Packet Diagram is not ready yet"); });
    diagramBuilder.AddItem("gitgraph", "Git Graph", "Git Graph",
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreatePartiallyImplementedExamples("Git Graph is not ready yet"); });
    diagramBuilder.AddItem("erdiagram", "ER Diagram", "ER Diagram",
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreatePartiallyImplementedExamples("ER Diagram is not ready yet"); });
    diagramBuilder.AddItem("sequencediagram", "Sequence Diagram", "Sequence Diagram",
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreatePartiallyImplementedExamples("Sequence Diagram is not ready yet"); });
    diagramBuilder.AddItem("classdiagram", "Class Diagram", "Class Diagram",
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreatePartiallyImplementedExamples("Class Diagram is not ready yet"); });
    diagramBuilder.AddItem("quadrantdiagram", "Quadrant Chart (diagram)", "Quadrant Chart (diagram)",
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreatePartiallyImplementedExamples("Quadrant Chart (diagram) is not ready yet"); });
    diagramBuilder.AddItem("requirementdiagram", "Requirement Diagram", "Requirement Diagram",
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreatePartiallyImplementedExamples("Requirement Diagram is not ready yet"); });
    diagramBuilder.AddItem("timelinediagram", "Timeline Diagram", "Timeline Diagram",
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreatePartiallyImplementedExamples("Timeline Diagram is not ready yet"); });


//        diagramBuilder.AddItem("mermaid", "Mermaid", "Mermaid",
//                               ImplementationStatus::NotImplemented,
//                               [this]() { return nullptr; });



        // ===== INFO GRAPHICS =====
        auto infoBuilder = DemoCategoryBuilder(this, DemoCategory::InfoGraphics);

//        infoBuilder.AddItem("infographics", "Info Graphics", "Complex data visualizations",
//                            ImplementationStatus::NotImplemented,
//                            [this]() { return CreateInfoGraphicsExamples(); })
//                .AddVariant("infographics", "Dashboard Widgets")
//                .AddVariant("infographics", "Statistical Displays")
//                .AddVariant("infographics", "Interactive Maps");

        infoBuilder.AddItem("heatmap", "Heat map", "Heat map",
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreatePartiallyImplementedExamples("HeatMap is not ready yet"); });

        infoBuilder.AddItem("circularinfo", "Circular info graphic", "Circular info graphic",
                            ImplementationStatus::PartiallyImplemented,
                            [this]() { return CreatePartiallyImplementedExamples("Circular info graphic is not ready yet"); });

        infoBuilder.AddItem("wavesinfo", "Waves info graphic", "Waves info graphic",
                            ImplementationStatus::PartiallyImplemented,
                            [this]() { return CreatePartiallyImplementedExamples("Waves info graphic is not ready yet"); });

        infoBuilder.AddItem("matrix", "Matrix", "Matrix",
                            ImplementationStatus::PartiallyImplemented,
                            [this]() { return CreatePartiallyImplementedExamples("Matrix is not ready yet"); });

        infoBuilder.AddItem("performancematrix", "Performance Matrix", "Performance Matrix",
                            ImplementationStatus::PartiallyImplemented,
                            [this]() { return CreatePartiallyImplementedExamples("Performance Matrix is not ready yet"); });


        // ===== 3D ELEMENTS =====
        auto graphics3DBuilder = DemoCategoryBuilder(this, DemoCategory::Graphics3D);

        graphics3DBuilder.AddItem("models3d", "3D Models", "3D model display and interaction",
                                  ImplementationStatus::NotImplemented,
                                  [this]() { return Create3DExamples(); })
                .AddVariant("models3d", "3DS Models")
                .AddVariant("models3d", "3DM Models")
                .AddVariant("models3d", "OBJ Models");

#ifdef ULTRACANVAS_ENABLE_GL
        graphics3DBuilder.AddItem("glsurface", "OpenGL Surface", "Hardware-accelerated OpenGL 3D rendering surface",
                                  ImplementationStatus::FullyImplemented,
                                  [this]() { return CreateGLSurfaceExamples(); },
                                  "Apps/DemoApp/UltraCanvasGLSurfaceExamples.cpp",
                                  "Docs/UltraCanvas/UltraCanvasGLSurfaceExamples.md");
#endif

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
                               [this]() { return CreateMarkdownDocScreen(NormalizePath(GetResourcesDir()+"media/MarkdownExample.md")); });

//        textDocBuilder.AddItem("codeeditor", "Code Editor", "Syntax highlighting text editor",
//                               ImplementationStatus::PartiallyImplemented,
//                               [this]() { return CreateCodeEditorExamples(); })
//                .AddVariant("codeeditor", "C++ Syntax")
//                .AddVariant("codeeditor", "Pascal Syntax")
//                .AddVariant("codeeditor", "COBOL Syntax");

        textDocBuilder.AddItem("textdocuments", "Text Documents", "Text document support",
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreateTextDocumentExamples(); });

        textDocBuilder.AddItem("textdocuments_latex", "LaTeX Documents", "LaTeX document support",
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreateTextDocumentExamples(); });

        textDocBuilder.AddItem("textdocuments_pdf", "PDF Documents", "PDF document support",
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreateTextDocumentExamples(); });

        textDocBuilder.AddItem("textdocuments_odf", "ODF Documents", "ODF document support",
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreateTextDocumentExamples(); });

        textDocBuilder.AddItem("textdocuments_odt", "ODT Documents", "ODT document support",
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
                             [this]() { return CreatePartiallyImplementedExamples("## QR code\n"
                                                                                  "Not ready yet"); });

        toolsBuilder.AddItem("barcode", "Bar code", "Bar code",
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreatePartiallyImplementedExamples("## Bar code\n"
                                                                                    "Not ready yet"); });

        toolsBuilder.AddItem("ocr", "OCR", "OCR",
                             ImplementationStatus::NotImplemented,
                             [this]() { return CreatePartiallyImplementedExamples(""); });

        toolsBuilder.AddItem("vectorizer", "Vectorizer", "Vectorizer",
                             ImplementationStatus::NotImplemented,
                             [this]() { return CreatePartiallyImplementedExamples(""); });

        toolsBuilder.AddItem("textrenderingsettings", "Text Rendering",
                             "Configure text antialiasing, hinting style, and hint metrics",
                             ImplementationStatus::FullyImplemented,
                             [this]() { return CreateTextRenderingSettingsExamples(); },
                             "Apps/DemoApp/UltraCanvasTextRenderingExamples.cpp",
                             "Docs/UltraCanvas/UltraCanvasTextRenderingExamples.md");

        auto modulesBuilder = DemoCategoryBuilder(this, DemoCategory::Modules);
        modulesBuilder.AddItem("audiofx", "Audio FX", "Audio FX",
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreateMarkdownDocScreen(NormalizePath(GetResourcesDir()+"Docs/Modules/AudioFX/README.md")); });
        modulesBuilder.AddItem("fileloader", "File Loader", "File Loader",
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreateMarkdownDocScreen(NormalizePath(GetResourcesDir()+"Docs/Modules/FileLoader/README.md")); });
        modulesBuilder.AddItem("iodevicemanager", "IODeviceManager support", "IODeviceManager support",
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreateMarkdownDocScreen(NormalizePath(GetResourcesDir()+"Docs/Modules/IODeviceManager/README.md")); });
        modulesBuilder.AddItem("pixelfx", "Pixel FX", "Pixel FX",
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreateMarkdownDocScreen(NormalizePath(GetResourcesDir()+"Docs/Modules/PixelFX/README.md")); });
        modulesBuilder.AddItem("smarthome", "Smart Home module", "UltraCanvas Smart Home Module",
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreateMarkdownDocScreen(NormalizePath(GetResourcesDir()+"Docs/Modules/Smarthome/README.md")); });
        modulesBuilder.AddItem("ultraai", "Ultra AI", "Ultra AI Module",
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreateMarkdownDocScreen(NormalizePath(GetResourcesDir()+"Docs/Modules/UltraAI/README.md")); });
        modulesBuilder.AddItem("ultranet", "Ultra Net", "Ultra Net Module",
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreateMarkdownDocScreen(NormalizePath(GetResourcesDir()+"Docs/Modules/UltraNet/README.md")); });
        modulesBuilder.AddItem("videofx", "VideoFX", "VideoFX Module",
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreateMarkdownDocScreen(NormalizePath(GetResourcesDir()+"Docs/Modules/VideoFX/README.md")); });
        modulesBuilder.AddItem("virtualfs", "VirtualFS", "VirtualFS Module",
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreateMarkdownDocScreen(NormalizePath(GetResourcesDir()+"Docs/Modules/VirtualFS/README.md")); });
        modulesBuilder.AddItem("gpio", "GPIO support", "GPIO support",
                             ImplementationStatus::PartiallyImplemented,
                             [this]() { return CreatePartiallyImplementedExamples("## GPIO support"); });

        auto widgetsBuilder = DemoCategoryBuilder(this, DemoCategory::Widgets);
        widgetsBuilder.AddItem("datepicker", "Date Picker", "Date Picker",
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreatePartiallyImplementedExamples("## Date picker"); });
        widgetsBuilder.AddItem("colorpicker", "Color Picker", "Color Picker",
                               ImplementationStatus::PartiallyImplemented,
                               [this]() { return CreatePartiallyImplementedExamples("## Color picker"); });

        widgetsBuilder.AddItem("photovideoviewer", "Photo/Video viewer", "Photo/Video viewer",
                               ImplementationStatus::NotImplemented,
                               [this]() { return CreatePartiallyImplementedExamples("Photo/Video viewer"); });

        widgetsBuilder.AddItem("slideshow", "Slideshow",
                               "Timed image diashow with info text panel and selectable indicator styles",
                               ImplementationStatus::FullyImplemented,
                               [this]() { return CreateSlideshowExamples(); },
                               "Apps/DemoApp/UltraCanvasSlideshowExamples.cpp")
                .AddVariant("slideshow", "Bars Indicator")
                .AddVariant("slideshow", "Dots Indicator")
                .AddVariant("slideshow", "Progress Bar")
                .AddVariant("slideshow", "Story Bars")
                .AddVariant("slideshow", "Counter")
                .AddVariant("slideshow", "Thumbnails")
                .AddVariant("slideshow", "Labels");

        debugOutput << "✓ Registered " << demoItems.size() << " demo items across "
                  << categoryItems.size() << " categories" << std::endl;
    }

    void UltraCanvasDemoApplication::SetupTreeView() {
        // Setup root node
        TreeNodeData rootData("root", "UltraCanvas Components");
        rootData.leftIcon = TreeNodeIcon(NormalizePath(GetResourcesDir() + "media/icons/ultracanvas.png"), 16, 16);
        TreeNode* rootNode = categoryTreeView->SetRootNode(rootData);

        // Add category nodes
        std::vector<std::pair<DemoCategory, std::string>> categoryNames = {
                {DemoCategory::BasicUI, "Basic UI Elements"},
                {DemoCategory::ExtendedFunctionality, "Complex UI Elements"},
                {DemoCategory::BitmapElements, "Bitmap Elements"},
                {DemoCategory::VectorElements, "Vector Graphics"},
                {DemoCategory::Charts, "Charts"},
                {DemoCategory::Diagrams, "Diagrams"},
                {DemoCategory::InfoGraphics, "Info Graphics"},
                {DemoCategory::Graphics3D, "3D Graphics"},
                {DemoCategory::VideoElements, "Video Elements"},
                {DemoCategory::TextDocuments, "Text Documents"},
                {DemoCategory::AudioElements, "Audio Elements"},
                {DemoCategory::Widgets, "Widgets"},
                {DemoCategory::Tools, "Tools"},
                {DemoCategory::Modules, "Modules"}
        };


        for (const auto& [category, catName] : categoryNames) {
            TreeNodeData categoryData(
                    "cat_" + std::to_string(static_cast<int>(category)),
                    catName
            );
            auto items = categoryItems[category];
            categoryData.leftIcon = TreeNodeIcon(NormalizePath(GetResourcesDir() + "media/icons/folder.png"), 16, 16);
            TreeNode* categoryNode = categoryTreeView->AddNode("root", categoryData);

            // Add items for this category
            for (const std::string& itemId : items) {
                const auto& demoItem = demoItems[itemId];
                TreeNodeData itemData(itemId, demoItem->displayName);
                if (demoItem->id == "imageperformance") {
                    itemData.leftIcon = TreeNodeIcon(NormalizePath(GetResourcesDir() + "media/icons/clock-five.svg"), 16, 16);
                } else {
                    itemData.leftIcon = TreeNodeIcon(NormalizePath(GetResourcesDir() + "media/icons/document.svg"), 16, 16);
                }
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
                    currentDisplayElement->layoutItem
                        .SetFlexGrow(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
                }
                currentSelectedId = itemId;
            } catch (const std::exception& e) {
                debugOutput << "Error creating example for " << itemId << ": " << e.what() << std::endl;
            }
        } else {
            // Show placeholder for not implemented items
            auto placeholder = std::make_shared<UltraCanvasLabel>("placeholder", 20, 20, 600, 200);
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
            case ImplementationStatus::FullyImplemented: return NormalizePath(GetResourcesDir() + "media/icons/check.png");
            case ImplementationStatus::PartiallyImplemented: return NormalizePath(GetResourcesDir() + "media/icons/warning-blue.png");
            case ImplementationStatus::NotImplemented: return NormalizePath(GetResourcesDir() + "media/icons/x.png");
            case ImplementationStatus::Planned: return NormalizePath(GetResourcesDir() + "media/icons/info.png");
            default: return NormalizePath(GetResourcesDir() + "media/icons/unknown.png");
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
        debugOutput << "Running UltraCanvas Demo Application..." << std::endl;
        debugOutput << "Select items from the tree view to see implementation examples." << std::endl;

        if (mainWindow) {
            //mainWindow->Show();
            // The application will handle the event loop
        }

        // Show the info window at startup
        ShowInfoWindow();

        auto app = UltraCanvasApplication::GetInstance();
        app->Run();
    }

    void UltraCanvasDemoApplication::Shutdown() {
        debugOutput << "Shutting down Demo Application..." << std::endl;

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