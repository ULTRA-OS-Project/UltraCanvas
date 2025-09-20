// UltraCanvasDemoExamples.cpp
// Implementation of all component example creators
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
//#include "UltraCanvasButton3Sections.h"
#include "UltraCanvasFormulaEditor.h"
#include <sstream>

namespace UltraCanvas {

// ===== BASIC UI ELEMENTS =====

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateButtonExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("ButtonExamples", 100, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("ButtonTitle", 101, 10, 10, 300, 30);
        title->SetText("Button Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Standard Button
        auto standardBtn = std::make_shared<UltraCanvasButton>("StandardButton", 102, 20, 50, 120, 35);
        standardBtn->SetText("Standard Button");
        standardBtn->onClick = [](const UCEvent& ev) { std::cout << "Standard button clicked!" << std::endl; };
        container->AddChild(standardBtn);

        // Icon Button
//        auto iconBtn = std::make_shared<UltraCanvasButton>("IconButton", 103, 160, 50, 40, 35);
//        iconBtn->SetIcon("assets/icons/save.png");
//        iconBtn->SetTooltip("Save File");
//        iconBtn->onClick = [](const UCEvent& ev) { std::cout << "Save button clicked!" << std::endl; };
//        container->AddChild(iconBtn);

        // Toggle Button
//        auto toggleBtn = std::make_shared<UltraCanvasButton>("ToggleButton", 104, 220, 50, 100, 35);
//        toggleBtn->SetText("Toggle");
//        toggleBtn->SetToggleable(true);
//        toggleBtn->onClick = [toggleBtn](const UCEvent& ev) {
//            std::cout << "Toggle state: " << (toggleBtn->IsToggled() ? "ON" : "OFF") << std::endl;
//        };
//        container->AddChild(toggleBtn);

        // Three-Section Button
//        auto threeSectionBtn = std::make_shared<UltraCanvasButton3Sections>("ThreeSectionButton", 105, 340, 50, 180, 35);
//        threeSectionBtn->SetLeftText("File");
//        threeSectionBtn->SetCenterText("Edit");
//        threeSectionBtn->SetRightText("View");
//        threeSectionBtn->onLeftClicked = []() { std::cout << "File section clicked!" << std::endl; };
//        threeSectionBtn->onCenterClicked = []() { std::cout << "Edit section clicked!" << std::endl; };
//        threeSectionBtn->onRightClicked = []() { std::cout << "View section clicked!" << std::endl; };
//        container->AddChild(threeSectionBtn);

        // Disabled Button
        auto disabledBtn = std::make_shared<UltraCanvasButton>("DisabledButton", 106, 20, 100, 120, 35);
        disabledBtn->SetText("Disabled");
        disabledBtn->SetEnabled(false);
        container->AddChild(disabledBtn);

        // Colored Buttons
        auto primaryBtn = std::make_shared<UltraCanvasButton>("PrimaryButton", 107, 160, 100, 100, 35);
        primaryBtn->SetText("Primary");
        primaryBtn->SetColors(Color(0, 123, 255, 255), Color(0, 100, 225, 255), Color(0, 90, 215, 255), Color(100, 133, 255, 255));
        primaryBtn->SetTextColors(Colors::White, Colors::White, Colors::White, Colors::LightGray);
        container->AddChild(primaryBtn);

        auto dangerBtn = std::make_shared<UltraCanvasButton>("DangerButton", 108, 280, 100, 100, 35);
        dangerBtn->SetText("Danger");
        dangerBtn->SetColors(Color(220, 53, 69, 255), Color(210, 43, 59, 255), Color(200, 33, 49, 255), Color(255, 153, 169, 255));
        dangerBtn->SetTextColors(Colors::White, Colors::White, Colors::White, Colors::LightGray);
        container->AddChild(dangerBtn);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateTextInputExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TextInputExamples", 200, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("TextInputTitle", 201, 10, 10, 300, 30);
        title->SetText("Text Input Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Single Line Input
        auto singleLineInput = std::make_shared<UltraCanvasTextInput>("SingleLineInput", 202, 20, 50, 300, 30);
        singleLineInput->SetPlaceholder("Enter single line text...");
        singleLineInput->SetMaxLength(100);
        container->AddChild(singleLineInput);

        auto singleLineLabel = std::make_shared<UltraCanvasLabel>("SingleLineLabel", 203, 20, 85, 200, 20);
        singleLineLabel->SetText("Single Line Input");
        singleLineLabel->SetFontSize(12);
        container->AddChild(singleLineLabel);

        // Multi-line Text Area
        auto multiLineInput = std::make_shared<UltraCanvasTextInput>("MultiLineInput", 204, 20, 120, 400, 100);
        multiLineInput->SetInputType(TextInputType::Multiline);
        multiLineInput->SetPlaceholder("Enter multi-line text...\nSupports line breaks.");
//        multiLineInput->SetWordWrap(true);
        container->AddChild(multiLineInput);

        auto multiLineLabel = std::make_shared<UltraCanvasLabel>("MultiLineLabel", 205, 20, 225, 200, 20);
        multiLineLabel->SetText("Multi-line Text Area");
        multiLineLabel->SetFontSize(12);
        container->AddChild(multiLineLabel);

        // Password Field
        auto passwordInput = std::make_shared<UltraCanvasTextInput>("PasswordInput", 206, 450, 50, 300, 30);
        passwordInput->SetInputType(TextInputType::Password);
        passwordInput->SetPlaceholder("Enter password...");
        container->AddChild(passwordInput);

        auto passwordLabel = std::make_shared<UltraCanvasLabel>("PasswordLabel", 207, 450, 85, 200, 20);
        passwordLabel->SetText("Password Field");
        passwordLabel->SetFontSize(12);
        container->AddChild(passwordLabel);

        // Numeric Input
        auto numericInput = std::make_shared<UltraCanvasTextInput>("NumericInput", 208, 450, 120, 200, 30);
        numericInput->SetInputType(TextInputType::Number);
        numericInput->SetPlaceholder("0.00");
//        numericInput->SetMinValue(0.0);
//        numericInput->SetMaxValue(1000.0);
        container->AddChild(numericInput);

        auto numericLabel = std::make_shared<UltraCanvasLabel>("NumericLabel", 209, 450, 155, 200, 20);
        numericLabel->SetText("Numeric Input (0-1000)");
        numericLabel->SetFontSize(12);
        container->AddChild(numericLabel);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateDropdownExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("DropdownExamples", 300, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("DropdownTitle", 301, 10, 10, 300, 30);
        title->SetText("Dropdown/ComboBox Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Simple Dropdown
        auto simpleDropdown = std::make_shared<UltraCanvasDropdown>("SimpleDropdown", 302, 20, 50, 200, 30);
        simpleDropdown->AddItem("Option 1");
        simpleDropdown->AddItem("Option 2");
        simpleDropdown->AddItem("Option 3");
        simpleDropdown->AddItem("Very Long Option Text");
        simpleDropdown->SetSelectedIndex(0);
        container->AddChild(simpleDropdown);

        auto simpleLabel = std::make_shared<UltraCanvasLabel>("SimpleLabel", 303, 20, 85, 200, 20);
        simpleLabel->SetText("Simple Dropdown");
        simpleLabel->SetFontSize(12);
        container->AddChild(simpleLabel);

        // Editable ComboBox
//        auto editableCombo = std::make_shared<UltraCanvasDropdown>("EditableCombo", 304, 250, 50, 200, 30);
//        editableCombo->SetEditable(true);
//        editableCombo->AddItem("Predefined Item 1");
//        editableCombo->AddItem("Predefined Item 2");
//        editableCombo->SetText("Type or select...");
//        container->AddChild(editableCombo);
//
//        auto editableLabel = std::make_shared<UltraCanvasLabel>("EditableLabel", 305, 250, 85, 200, 20);
//        editableLabel->SetText("Editable ComboBox");
//        editableLabel->SetFontSize(12);
//        container->AddChild(editableLabel);

        // Multi-Select Dropdown
//        auto multiSelect = std::make_shared<UltraCanvasDropdown>("MultiSelect", 306, 480, 50, 200, 30);
//        multiSelect->SetMultiSelect(true);
//        multiSelect->AddItem("Apple");
//        multiSelect->AddItem("Banana");
//        multiSelect->AddItem("Cherry");
//        multiSelect->AddItem("Date");
//        multiSelect->AddItem("Elderberry");
//        container->AddChild(multiSelect);

//        auto multiLabel = std::make_shared<UltraCanvasLabel>("MultiLabel", 307, 480, 85, 200, 20);
//        multiLabel->SetText("Multi-Select Dropdown");
//        multiLabel->SetFontSize(12);
//        container->AddChild(multiLabel);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateSliderExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("SliderExamples", 400, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("SliderTitle", 401, 10, 10, 300, 30);
        title->SetText("Slider Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Horizontal Slider
        auto hSlider = std::make_shared<UltraCanvasSlider>("HorizontalSlider", 402, 20, 60, 300, 20);
        hSlider->SetOrientation(SliderOrientation::Horizontal);
        hSlider->SetRange(0.0f, 100.0f);
        hSlider->SetValue(50.0f);
        hSlider->SetStep(10.0f);
//        hSlider->SetShowTicks(true);
        container->AddChild(hSlider);

        auto hSliderLabel = std::make_shared<UltraCanvasLabel>("HSliderLabel", 403, 20, 85, 200, 20);
        hSliderLabel->SetText("Horizontal Slider (0-100)");
        hSliderLabel->SetFontSize(12);
        container->AddChild(hSliderLabel);

        // Value display for horizontal slider
        auto hValueLabel = std::make_shared<UltraCanvasLabel>("HValueLabel", 404, 340, 60, 80, 20);
        hValueLabel->SetText("50");
        hValueLabel->SetAlignment(TextAlignment::Center);
        hValueLabel->SetBackgroundColor(Color(240, 240, 240, 255));
        container->AddChild(hValueLabel);

        hSlider->onValueChanged = [hValueLabel](float value) {
            hValueLabel->SetText(std::to_string(static_cast<int>(value)));
        };

        // Vertical Slider
        auto vSlider = std::make_shared<UltraCanvasSlider>("VerticalSlider", 405, 500, 60, 20, 200);
        vSlider->SetOrientation(SliderOrientation::Vertical);
        vSlider->SetRange(0.0f, 10.0f);
        vSlider->SetValue(5.0f);
        vSlider->SetStep(0.5f);
        container->AddChild(vSlider);

        auto vSliderLabel = std::make_shared<UltraCanvasLabel>("VSliderLabel", 406, 530, 60, 150, 20);
        vSliderLabel->SetText("Vertical Slider");
        vSliderLabel->SetFontSize(12);
        container->AddChild(vSliderLabel);

        // Range Slider
//        auto rangeSlider = std::make_shared<UltraCanvasSlider>("RangeSlider", 407, 20, 150, 400, 20);
//        rangeSlider->SetOrientation(SliderOrientation::Horizontal);
//        rangeSlider->SetRangeMode(true);
//        rangeSlider->SetRange(0.0f, 1000.0f);
//        rangeSlider->SetLowerValue(200.0f);
//        rangeSlider->SetUpperValue(800.0f);
//        container->AddChild(rangeSlider);
//
//        auto rangeLabel = std::make_shared<UltraCanvasLabel>("RangeLabel", 408, 20, 175, 200, 20);
//        rangeLabel->SetText("Range Slider (0-1000)");
//        rangeLabel->SetFontSize(12);
//        container->AddChild(rangeLabel);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateLabelExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("LabelExamples", 500, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("LabelTitle", 501, 10, 10, 300, 30);
        title->SetText("Label Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Basic Label
        auto basicLabel = std::make_shared<UltraCanvasLabel>("BasicLabel", 502, 20, 50, 400, 25);
        basicLabel->SetText("This is a basic label with default styling.");
        container->AddChild(basicLabel);

        // Header Text
        auto headerLabel = std::make_shared<UltraCanvasLabel>("HeaderLabel", 503, 20, 90, 500, 35);
        headerLabel->SetText("Header Label Example");
        headerLabel->SetFontSize(24);
        headerLabel->SetFontWeight(FontWeight::Bold);
        headerLabel->SetTextColor(Color(0, 100, 200, 255));
        container->AddChild(headerLabel);

        // Status Labels
        auto successLabel = std::make_shared<UltraCanvasLabel>("SuccessLabel", 504, 20, 140, 150, 25);
        successLabel->SetText("✓ Success");
        successLabel->SetBackgroundColor(Color(200, 255, 200, 255));
        successLabel->SetTextColor(Color(0, 150, 0, 255));
        successLabel->SetAlignment(TextAlignment::Center);
        container->AddChild(successLabel);

        auto warningLabel = std::make_shared<UltraCanvasLabel>("WarningLabel", 505, 180, 140, 150, 25);
        warningLabel->SetText("⚠ Warning");
        warningLabel->SetBackgroundColor(Color(255, 255, 200, 255));
        warningLabel->SetTextColor(Color(200, 150, 0, 255));
        warningLabel->SetAlignment(TextAlignment::Center);
        container->AddChild(warningLabel);

        auto errorLabel = std::make_shared<UltraCanvasLabel>("ErrorLabel", 506, 340, 140, 150, 25);
        errorLabel->SetText("✗ Error");
        errorLabel->SetBackgroundColor(Color(255, 200, 200, 255));
        errorLabel->SetTextColor(Color(200, 0, 0, 255));
        errorLabel->SetAlignment(TextAlignment::Center);
        container->AddChild(errorLabel);

        // Multi-line Label
        auto multiLabel = std::make_shared<UltraCanvasLabel>("MultiLabel", 507, 20, 190, 450, 80);
        multiLabel->SetText("This is a multi-line label that demonstrates\nhow text wrapping works with longer content.\nIt supports multiple lines and proper alignment.");
        multiLabel->SetWordWrap(true);
        multiLabel->SetAlignment(TextAlignment::Left);
        multiLabel->SetBackgroundColor(Color(245, 245, 245, 255));
//        multiLabel->SetBorderStyle(BorderStyle::Solid);
        multiLabel->SetBorderWidth(1.0f);
        multiLabel->SetPadding(10.0f);
        container->AddChild(multiLabel);

        return container;
    }

// ===== EXTENDED FUNCTIONALITY =====

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateTreeViewExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TreeViewExamples", 600, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("TreeViewTitle", 601, 10, 10, 300, 30);
        title->SetText("TreeView Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // File Explorer Style Tree
        auto fileTree = std::make_shared<UltraCanvasTreeView>("FileTree", 602, 20, 50, 300, 400);
        fileTree->SetRowHeight(22);
        fileTree->SetSelectionMode(TreeSelectionMode::Single);

        // Setup file tree structure
        TreeNodeData rootData("root", "My Computer");
        rootData.leftIcon = TreeNodeIcon("assets/icons/computer.png", 16, 16);
        TreeNode* root = fileTree->SetRootNode(rootData);

        TreeNodeData driveC("drive_c", "Local Disk (C:)");
        driveC.leftIcon = TreeNodeIcon("assets/icons/drive.png", 16, 16);
        fileTree->AddNode("root", driveC);

        TreeNodeData documents("documents", "Documents");
        documents.leftIcon = TreeNodeIcon("assets/icons/folder.png", 16, 16);
        fileTree->AddNode("drive_c", documents);

        TreeNodeData file1("file1", "Document.txt");
        file1.leftIcon = TreeNodeIcon("assets/icons/text.png", 16, 16);
        fileTree->AddNode("documents", file1);

        TreeNodeData pictures("pictures", "Pictures");
        pictures.leftIcon = TreeNodeIcon("assets/icons/folder.png", 16, 16);
        fileTree->AddNode("drive_c", pictures);

        fileTree->onNodeSelected = [](TreeNode* node) {
            std::cout << "Selected: " << node->data.text << std::endl;
        };

        root->Expand();
        container->AddChild(fileTree);

        // Multi-Selection Tree
        auto multiTree = std::make_shared<UltraCanvasTreeView>("MultiTree", 603, 350, 50, 300, 200);
        multiTree->SetRowHeight(20);
        multiTree->SetSelectionMode(TreeSelectionMode::Multiple);

        TreeNodeData multiRoot("multi_root", "Categories");
        TreeNode* multiRootNode = multiTree->SetRootNode(multiRoot);

        TreeNodeData category1("cat1", "Category 1");
        multiTree->AddNode("multi_root", category1);
        multiTree->AddNode("cat1", TreeNodeData("item1", "Item 1"));
        multiTree->AddNode("cat1", TreeNodeData("item2", "Item 2"));

        TreeNodeData category2("cat2", "Category 2");
        multiTree->AddNode("multi_root", category2);
        multiTree->AddNode("cat2", TreeNodeData("item3", "Item 3"));

        multiRootNode->Expand();
        container->AddChild(multiTree);

        auto multiLabel = std::make_shared<UltraCanvasLabel>("MultiTreeLabel", 604, 350, 260, 300, 20);
        multiLabel->SetText("Multi-Selection TreeView (Ctrl+Click)");
        multiLabel->SetFontSize(12);
        container->AddChild(multiLabel);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateTabExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TabExamples", 700, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("TabTitle", 701, 10, 10, 300, 30);
        title->SetText("Tabbed Container Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Top Tabs
        auto topTabs = std::make_shared<UltraCanvasTabbedContainer>("TopTabs", 702, 20, 50, 600, 250);
        topTabs->SetTabPosition(TabPosition::Top);

        // Create tab content
        auto tab1Content = std::make_shared<UltraCanvasLabel>("Tab1Content", 703, 10, 10, 580, 200);
        tab1Content->SetText("This is the content of Tab 1.\nYou can put any UltraCanvas elements here.");
        tab1Content->SetAlignment(TextAlignment::Center);
        tab1Content->SetBackgroundColor(Color(250, 250, 250, 255));

        auto tab2Content = std::make_shared<UltraCanvasLabel>("Tab2Content", 704, 10, 10, 580, 200);
        tab2Content->SetText("Tab 2 contains different content.\nTabs can hold complex layouts and controls.");
        tab2Content->SetAlignment(TextAlignment::Center);
        tab2Content->SetBackgroundColor(Color(240, 255, 240, 255));

        auto tab3Content = std::make_shared<UltraCanvasLabel>("Tab3Content", 705, 10, 10, 580, 200);
        tab3Content->SetText("Third tab with more information.\nEach tab maintains its own state.");
        tab3Content->SetAlignment(TextAlignment::Center);
        tab3Content->SetBackgroundColor(Color(240, 240, 255, 255));

        topTabs->AddTab("General", tab1Content);
        topTabs->AddTab("Settings", tab2Content);
        topTabs->AddTab("Advanced", tab3Content);
        topTabs->SetActiveTab(0);

        container->AddChild(topTabs);

        // Side Tabs
        auto sideTabs = std::make_shared<UltraCanvasTabbedContainer>("SideTabs", 706, 20, 320, 400, 200);
        sideTabs->SetTabPosition(TabPosition::Left);
        sideTabs->SetTabHeight(25);
        sideTabs->SetTabMinWidth(80);

//        auto sideTab1 = std::make_shared<UltraCanvasLabel>("SideTab1", 707, 10, 10, 300, 170);
//        sideTab1->SetText("Left-side tabs are useful\nfor tool palettes and\nnavigation panels.");
//        sideTab1->SetBackgroundColor(Color(255, 250, 240, 255));
//
//        auto sideTab2 = std::make_shared<UltraCanvasLabel>("SideTab2", 708, 10, 10, 300, 170);
//        sideTab2->SetText("Second side tab content.\nVertical orientation.");
//        sideTab2->SetBackgroundColor(Color(240, 250, 255, 255));
//
//        sideTabs->AddTab("Tools", sideTab1);
//        sideTabs->AddTab("Props", sideTab2);
//        sideTabs->SetActiveTab(0);
//
//        container->AddChild(sideTabs);

        return container;
    }

// ===== GRAPHICS AND MEDIA EXAMPLES =====

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreatePDFExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("PDFExamples", 800, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("PDFTitle", 801, 10, 10, 300, 30);
        title->SetText("PDF Viewer Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

//        // PDF Viewer
//        auto pdfViewer = std::make_shared<UltraCanvasPDFViewer>("PDFViewer", 802, 20, 50, 800, 500);
//
//        // Set up PDF viewer events
//        pdfViewer->onPageChanged = [](int current, int total) {
//            std::cout << "PDF Page " << current << " of " << total << std::endl;
//        };
//
//        pdfViewer->onZoomChanged = [](float zoom, PDFZoomMode mode) {
//            std::cout << "PDF Zoom: " << (zoom * 100) << "%" << std::endl;
//        };
//
//        // Note: In a real implementation, you would load an actual PDF file
//        // pdfViewer->LoadDocument("sample.pdf");
//
//        container->AddChild(pdfViewer);
//
//        // Info label since we can't load a real PDF in demo
//        auto infoLabel = std::make_shared<UltraCanvasLabel>("PDFInfo", 803, 30, 70, 600, 100);
//        infoLabel->SetText("PDF Viewer Component Features:\n• Page navigation and thumbnails\n• Zoom controls (fit width/height/page)\n• Search functionality\n• Double-page view mode\n• Async loading with progress\n• Password-protected PDF support");
//        infoLabel->SetBackgroundColor(Color(255, 255, 200, 100));
//        infoLabel->SetBorderStyle(BorderStyle::Dashed);
//        infoLabel->SetPadding(10.0f);
//        container->AddChild(infoLabel);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateVectorExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("VectorExamples", 900, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("VectorTitle", 901, 10, 10, 300, 30);
        title->SetText("Vector Graphics Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Drawing Surface
//        auto drawingSurface = std::make_shared<UltraCanvasDrawingSurface>("DrawingSurface", 902, 20, 50, 600, 400);
//        drawingSurface->SetBackgroundColor(Colors::White);
//        drawingSurface->SetBorderStyle(BorderStyle::Solid);
//        drawingSurface->SetBorderWidth(2.0f);
//
//        // Draw some example shapes
//        drawingSurface->SetForegroundColor(Color(255, 0, 0, 255));
//        drawingSurface->DrawRectangle(50, 50, 100, 80);
//
//        drawingSurface->SetForegroundColor(Color(0, 255, 0, 255));
//        drawingSurface->DrawCircle(200, 100, 40);
//
//        drawingSurface->SetForegroundColor(Color(0, 0, 255, 255));
//        drawingSurface->SetLineWidth(3.0f);
//        drawingSurface->DrawLine(Point2D(300, 50), Point2D(400, 150));
//
//        container->AddChild(drawingSurface);
//
//        // Drawing tools info
//        auto toolsLabel = std::make_shared<UltraCanvasLabel>("VectorTools", 903, 650, 70, 320, 200);
//        toolsLabel->SetText("Drawing Surface Features:\n• Vector primitives (lines, circles, rectangles)\n• Bezier curves and paths\n• Fill and stroke styling\n• Layer management\n• Undo/redo support\n• Selection and manipulation\n• Export to SVG/PNG");
//        toolsLabel->SetBackgroundColor(Color(240, 255, 240, 255));
////        toolsLabel->SetBorderStyle(BorderStyle::Solid);
//        toolsLabel->SetPadding(10.0f);
//        container->AddChild(toolsLabel);

        return container;
    }

// ===== NOT IMPLEMENTED PLACEHOLDERS =====

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateToolbarExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("ToolbarExamples", 1000, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("ToolbarTitle", 1001, 10, 10, 300, 30);
        title->SetText("Toolbar Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Placeholder for toolbar implementation
        auto placeholder = std::make_shared<UltraCanvasLabel>("ToolbarPlaceholder", 1002, 20, 50, 800, 400);
        placeholder->SetText("Toolbar Component - Partially Implemented\n\nPlanned Features:\n• Horizontal and vertical toolbars\n• Icon buttons with tooltips\n• Separator elements\n• Dropdown menu buttons\n• Overflow handling\n• Customizable button groups\n• Ribbon-style layout\n• Drag and drop reordering");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 255, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorderWidth(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateTableViewExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TableViewExamples", 1100, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("TableViewTitle", 1101, 10, 10, 300, 30);
        title->SetText("Table View Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("TableViewPlaceholder", 1102, 20, 50, 800, 400);
        placeholder->SetText("Table View Component - Not Implemented\n\nPlanned Features:\n• Data grid with rows and columns\n• Sortable column headers\n• Cell editing capabilities\n• Row selection (single/multiple)\n• Virtual scrolling for large datasets\n• Custom cell renderers\n• Column resizing and reordering\n• Filtering and search\n• Export to CSV/Excel");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorderWidth(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateListViewExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("ListViewExamples", 1200, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("ListViewTitle", 1201, 10, 10, 300, 30);
        title->SetText("List View Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("ListViewPlaceholder", 1202, 20, 50, 800, 400);
        placeholder->SetText("List View Component - Not Implemented\n\nPlanned Features:\n• Vertical and horizontal lists\n• Icon and text display modes\n• Custom item templates\n• Virtual scrolling\n• Item selection and highlighting\n• Drag and drop reordering\n• Search and filtering\n• Group headers\n• Context menus");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorderWidth(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateMenuExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("MenuExamples", 1300, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("MenuTitle", 1301, 10, 10, 300, 30);
        title->SetText("Menu Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("MenuPlaceholder", 1302, 20, 50, 800, 400);
        placeholder->SetText("Menu Component - Not Implemented\n\nPlanned Features:\n• Menu bars and popup menus\n• Hierarchical submenus\n• Menu item icons and shortcuts\n• Separator lines\n• Checkable menu items\n• Disabled menu items\n• Context-sensitive menus\n• Keyboard navigation\n• Custom menu styling");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorderWidth(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateDialogExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("DialogExamples", 1400, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("DialogTitle", 1401, 10, 10, 300, 30);
        title->SetText("Dialog Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("DialogPlaceholder", 1402, 20, 50, 800, 400);
        placeholder->SetText("Dialog Component - Not Implemented\n\nPlanned Features:\n• Modal and modeless dialogs\n• Message boxes (Info, Warning, Error)\n• File open/save dialogs\n• Color picker dialogs\n• Font selection dialogs\n• Custom dialog layouts\n• Dialog result handling\n• Animation effects\n• Keyboard shortcuts (ESC, Enter)");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorderWidth(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateBitmapExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("BitmapExamples", 1500, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("BitmapTitle", 1501, 10, 10, 300, 30);
        title->SetText("Bitmap Graphics Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("BitmapPlaceholder", 1502, 20, 50, 800, 400);
        placeholder->SetText("Bitmap Graphics - Partially Implemented\n\nSupported Formats:\n• PNG - Full support with transparency\n• JPEG - Standard image display\n• BMP - Basic bitmap support\n• GIF - Including animation support\n• AVIF - Modern format with animation\n\nFeatures:\n• Image scaling and cropping\n• Rotation and transformation\n• Filter effects\n• Multi-frame animation\n• Memory-efficient loading");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 255, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorderWidth(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::Create3DExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("Graphics3DExamples", 1600, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("Graphics3DTitle", 1601, 10, 10, 300, 30);
        title->SetText("3D Graphics Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("Graphics3DPlaceholder", 1602, 20, 50, 800, 400);
        placeholder->SetText("3D Graphics - Partially Implemented\n\nSupported Features:\n• 3DS model loading and display\n• 3DM model support\n• Basic scene graph\n• Camera controls (orbit, pan, zoom)\n• Basic lighting and shading\n• Texture mapping\n\nOpenGL/Vulkan Backend:\n• Hardware accelerated rendering\n• Vertex and fragment shaders\n• Multiple render targets\n• Anti-aliasing support");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 255, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorderWidth(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateVideoExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("VideoExamples", 1700, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("VideoTitle", 1701, 10, 10, 300, 30);
        title->SetText("Video Player Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("VideoPlaceholder", 1702, 20, 50, 800, 400);
        placeholder->SetText("Video Player Component - Not Implemented\n\nPlanned Features:\n• MP4, AVI, MOV playback\n• Hardware accelerated decoding\n• Custom playback controls\n• Fullscreen mode\n• Volume and timeline controls\n• Subtitle support\n• Frame-by-frame stepping\n• Video filters and effects\n• Streaming protocol support\n• Audio track selection");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorderWidth(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateTextDocumentExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TextDocumentExamples", 1800, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("TextDocumentTitle", 1801, 10, 10, 300, 30);
        title->SetText("Text Document Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("TextDocumentPlaceholder", 1802, 20, 50, 800, 400);
        placeholder->SetText("Text Document Support - Not Implemented\n\nPlanned Document Types:\n• ODT (OpenDocument Text)\n• RTF (Rich Text Format)\n• TeX/LaTeX documents\n• Plain text with syntax highlighting\n• HTML document rendering\n\nFeatures:\n• Document structure navigation\n• Text formatting display\n• Search and replace\n• Print preview\n• Export capabilities\n• Embedded images and tables");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorderWidth(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateMarkdownExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("MarkdownExamples", 1900, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("MarkdownTitle", 1901, 10, 10, 300, 30);
        title->SetText("Markdown Viewer Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("MarkdownPlaceholder", 1902, 20, 50, 800, 400);
        placeholder->SetText("Markdown Viewer - Not Implemented\n\nPlanned Features:\n• CommonMark specification support\n• GitHub Flavored Markdown\n• Syntax highlighting for code blocks\n• Table rendering\n• Math formula support (KaTeX)\n• Mermaid diagram integration\n• Live preview mode\n• Custom CSS styling\n• Export to HTML/PDF\n• Plugin system for extensions");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorderWidth(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateCodeEditorExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("CodeEditorExamples", 2000, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("CodeEditorTitle", 2001, 10, 10, 300, 30);
        title->SetText("Code Editor Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Formula Editor (partially implemented)
        auto formulaEditor = std::make_shared<UltraCanvasFormulaEditor>("FormulaEditor", 2002, 20, 50, 600, 100);
        ProceduralFormula f;
        f.formula = "SUM(A1:A10) + AVERAGE(B1:B10) * 2.5";
        formulaEditor->SetFormula(f);
//        formulaEditor->SetBackgroundColor(Color(248, 248, 248, 255));
        container->AddChild(formulaEditor);

        auto formulaLabel = std::make_shared<UltraCanvasLabel>("FormulaLabel", 2003, 20, 160, 600, 20);
        formulaLabel->SetText("Formula Editor (Partially Implemented) - Supports mathematical expressions");
        formulaLabel->SetFontSize(12);
        container->AddChild(formulaLabel);

        auto placeholder = std::make_shared<UltraCanvasLabel>("CodeEditorPlaceholder", 2004, 20, 200, 800, 300);
        placeholder->SetText("Code Editor Component - Partially Implemented\n\nCurrent Features:\n• Formula/Expression editing\n• Basic syntax validation\n\nPlanned Features:\n• C++ syntax highlighting\n• Pascal/Delphi support\n• COBOL syntax support\n• Line numbers and folding\n• Auto-completion\n• Error markers and tooltips\n• Find and replace\n• Multiple cursors\n• Code formatting\n• Plugin architecture for languages");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 255, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorderWidth(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateAudioExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("AudioExamples", 2100, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("AudioTitle", 2101, 10, 10, 300, 30);
        title->SetText("Audio Player Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("AudioPlaceholder", 2102, 20, 50, 800, 400);
        placeholder->SetText("Audio Player Component - Not Implemented\n\nPlanned Features:\n• FLAC lossless audio support\n• MP3, WAV, OGG playback\n• Waveform visualization\n• Spectrum analyzer\n• Playback controls (play/pause/stop)\n• Volume and position sliders\n• Playlist management\n• Audio effects and filters\n• Recording capabilities\n• Audio format conversion\n• Metadata display (ID3 tags)");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorderWidth(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateDiagramExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("DiagramExamples", 2200, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("DiagramTitle", 2201, 10, 10, 300, 30);
        title->SetText("Diagram Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("DiagramPlaceholder", 2202, 20, 50, 800, 400);
        placeholder->SetText("Diagram Support - Partially Implemented\n\nPlantUML Integration:\n• Class diagrams\n• Sequence diagrams\n• Activity diagrams\n• Use case diagrams\n• Component diagrams\n• State diagrams\n\nNative Diagram Engine:\n• Flowcharts\n• Organizational charts\n• Network diagrams\n• Mind maps\n• Interactive diagram editing\n• Export to SVG/PNG\n• Automatic layout algorithms");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 255, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorderWidth(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateChartExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("ChartExamples", 2300, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("ChartTitle", 2301, 10, 10, 300, 30);
        title->SetText("Chart Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("ChartPlaceholder", 2302, 20, 50, 800, 400);
        placeholder->SetText("Charts Component - Not Implemented\n\nPlanned Chart Types:\n• Line charts with multiple series\n• Bar charts (vertical/horizontal)\n• Pie and donut charts\n• Scatter plots with trend lines\n• Area charts (stacked/overlapped)\n• Candlestick charts for financial data\n• Heatmaps and bubble charts\n• Real-time updating charts\n\nFeatures:\n• Interactive legends\n• Zooming and panning\n• Data point tooltips\n• Animation effects\n• Custom styling themes\n• Export capabilities\n• Data binding from multiple sources");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorderWidth(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasElement> UltraCanvasDemoApplication::CreateInfoGraphicsExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("InfoGraphicsExamples", 2400, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("InfoGraphicsTitle", 2401, 10, 10, 300, 30);
        title->SetText("Info Graphics Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("InfoGraphicsPlaceholder", 2402, 20, 50, 800, 400);
        placeholder->SetText("Info Graphics Component - Not Implemented\n\nPlanned Widget Types:\n• Dashboard tiles and KPI widgets\n• Gauge and meter displays\n• Progress indicators and health meters\n• Statistical summary panels\n• Interactive data cards\n• Geographic data maps\n• Timeline visualizations\n• Comparison matrices\n\nAdvanced Features:\n• Real-time data updates\n• Responsive layout adaptation\n• Custom color schemes and branding\n• Animation and transition effects\n• Touch and gesture support\n• Export and sharing capabilities\n• Template library");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorderWidth(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

} // namespace UltraCanvas