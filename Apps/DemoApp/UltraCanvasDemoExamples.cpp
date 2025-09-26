// Apps/DemoApp/UltraCanvasDemoExamples.cpp
// Implementation of all component example creators
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
//#include "UltraCanvasButton3Sections.h"
#include "UltraCanvasFormulaEditor.h"
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include <sstream>
#include <random>
#include <map>

namespace UltraCanvas {

// ===== BASIC UI ELEMENTS =====


    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateButtonExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTextInputExamples() {
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
        multiLineLabel->SetText("Multi-line Text Input");
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

        auto textAreaInput = std::make_shared<UltraCanvasTextArea>("TextArea", 204, 20, 270, 400, 100);
//        textAreaInput->SetPlaceholder("Enter multi-line text...\nSupports line breaks.");
//        multiLineInput->SetWordWrap(true);
        container->AddChild(textAreaInput);

        auto textAreaLabel = std::make_shared<UltraCanvasLabel>("TextAreaLabel", 205, 20, 375, 200, 20);
        textAreaLabel->SetText("Text Area");
        textAreaLabel->SetFontSize(12);
        container->AddChild(textAreaLabel);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDropdownExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSliderExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateLabelExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTreeViewExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTabExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePDFExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateVectorExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateToolbarExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTableViewExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateListViewExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMenuExamples() {
        std::cout << "Creating Menu Examples..." << std::endl;

        // Create container for menu examples
        auto container = std::make_shared<UltraCanvasContainer>("MenuContainer", 100, 0, 0, 1000, 630);
        ContainerStyle containerStyle;
        containerStyle.backgroundColor = Color(252, 252, 252, 255);
        container->SetContainerStyle(containerStyle);

        // Section label for Context Menus
        auto contextLabel = std::make_shared<UltraCanvasLabel>("ContextLabel", 101, 20, 10, 300, 30);
        contextLabel->SetText("Context Menu Examples:");
        contextLabel->SetFontSize(14);
        contextLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(contextLabel);

        // Button to trigger context menu
        auto contextMenuBtn = std::make_shared<UltraCanvasButton>("ContextMenuBtn", 102, 20, 45, 280, 35);
        contextMenuBtn->SetText("Right-Click for Context Menu");
//        contextMenuBtn->SetTooltipText("Right-click anywhere on this button to show context menu");
        container->AddChild(contextMenuBtn);

        // Create context menu
        auto contextMenu = std::make_shared<UltraCanvasMenu>("ContextMenu1", 103, 0, 0, 200, 0);
        contextMenu->SetMenuType(MenuType::PopupMenu);

        // Add context menu items
        contextMenu->AddItem(MenuItemData::ActionWithShortcut("📋 Copy", "Ctrl+C", []() {
            std::cout << "Copy action triggered" << std::endl;
        }));

        contextMenu->AddItem(MenuItemData::ActionWithShortcut("✂️ Cut", "Ctrl+X", []() {
            std::cout << "Cut action triggered" << std::endl;
        }));

        contextMenu->AddItem(MenuItemData::ActionWithShortcut("📌 Paste", "Ctrl+V", []() {
            std::cout << "Paste action triggered" << std::endl;
        }));

        contextMenu->AddItem(MenuItemData::Separator());

        // Submenu example
        MenuItemData formatItem("🎨 Format");
        formatItem.type = MenuItemType::Submenu;
        formatItem.subItems = {
                MenuItemData::ActionWithShortcut("Bold", "Ctrl+B", []() { std::cout << "Bold" << std::endl; }),
                MenuItemData::ActionWithShortcut("Italic", "Ctrl+I", []() { std::cout << "Italic" << std::endl; }),
                MenuItemData::ActionWithShortcut("Underline", "Ctrl+U", []() { std::cout << "Underline" << std::endl; })
        };
        contextMenu->AddItem(formatItem);

        contextMenu->AddItem(MenuItemData::Separator());

        contextMenu->AddItem(MenuItemData::ActionWithShortcut("🗑️ Delete", "Del", []() {
            std::cout << "Delete action triggered" << std::endl;
        }));

        // Set right-click handler for button
        contextMenuBtn->onClick = [contextMenu, contextMenuBtn, container](const UCEvent& ev) {
            if (ev.button == UCMouseButton::Right) {
                // move menu to window container
                container->GetWindow()->AddChild(contextMenu);
                contextMenu->ShowAt(ev.windowX, ev.windowY);
            }
        };
        container->AddChild(contextMenu);

        // Section label for Main Menu Bar
        auto mainMenuLabel = std::make_shared<UltraCanvasLabel>("MainMenuLabel", 104, 20, 100, 250, 30);
        mainMenuLabel->SetText("Main Menu Bar Example:");
        mainMenuLabel->SetFontSize(14);
        mainMenuLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(mainMenuLabel);

// Create main menu bar using MenuType::Menubar and MenuBuilder
        auto mainMenuBar = MenuBuilder("MainMenuBar", 105, 20, 135, 960, 32)
                .SetType(MenuType::Menubar)
                .AddSubmenu("File", {
                        MenuItemData::ActionWithShortcut("📄 New", "Ctrl+N", []() {
                                std::cout << "New file" << std::endl;
                            }),
                        MenuItemData::ActionWithShortcut("📂 Open...", "Ctrl+O", []() {
                                std::cout << "Open file" << std::endl;
                            }),
                        MenuItemData::Submenu("📁 Recent Files", {
                                MenuItemData::Action("Document1.txt", []() {
                                            std::cout << "Open Document1.txt" << std::endl;
                                        }),
                                MenuItemData::Action("Project.cpp", []() {
                                            std::cout << "Open Project.cpp" << std::endl;
                                        }),
                                MenuItemData::Action("Config.json", []() {
                                            std::cout << "Open Config.json" << std::endl;
                                        })
                            }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("💾 Save", "Ctrl+S", []() {
                                std::cout << "Save file" << std::endl;
                            }),
                        MenuItemData::ActionWithShortcut("💾 Save As...", "Ctrl+Shift+S", []() {
                                std::cout << "Save as" << std::endl;
                            }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("🚪 Exit", "Alt+F4", []() {
                                std::cout << "Exit application" << std::endl;
                            })
                    })
                .AddSubmenu("Edit", {
                            MenuItemData::ActionWithShortcut("↩️ Undo", "Ctrl+Z", []() {
                                std::cout << "Undo" << std::endl;
                            }),
                            MenuItemData::ActionWithShortcut("↪️ Redo", "Ctrl+Y", []() {
                                std::cout << "Redo" << std::endl;
                            }),
                            MenuItemData::Separator(),
                            MenuItemData::ActionWithShortcut("✂️ Cut", "Ctrl+X", []() {
                                std::cout << "Cut" << std::endl;
                            }),
                            MenuItemData::ActionWithShortcut("📋 Copy", "Ctrl+C", []() {
                                std::cout << "Copy" << std::endl;
                            }),
                            MenuItemData::ActionWithShortcut("📌 Paste", "Ctrl+V", []() {
                                std::cout << "Paste" << std::endl;
                            }),
                            MenuItemData::Separator(),
                            MenuItemData::ActionWithShortcut("🔍 Find...", "Ctrl+F", []() {
                                std::cout << "Find" << std::endl;
                            }),
                            MenuItemData::ActionWithShortcut("🔄 Replace...", "Ctrl+H", []() {
                                std::cout << "Replace" << std::endl;
                            })
                })
                .AddSubmenu("View", {
                        MenuItemData::Checkbox("🔧 Toolbar", true, [](bool checked) {
                                std::cout << "Toolbar " << (checked ? "shown" : "hidden") << std::endl;
                            }),
                            MenuItemData::Checkbox("📊 Status Bar", true, [](bool checked) {
                                std::cout << "Status bar " << (checked ? "shown" : "hidden") << std::endl;
                            }),
                            MenuItemData::Checkbox("📁 Sidebar", false, [](bool checked) {
                                std::cout << "Sidebar " << (checked ? "shown" : "hidden") << std::endl;
                            }),
                            MenuItemData::Separator(),
                            MenuItemData::Radio("Zoom 50%", 1, false, [](bool checked) {
                                if (checked) std::cout << "Zoom 50%" << std::endl;
                            }),
                            MenuItemData::Radio("Zoom 100%", 1, true, [](bool checked) {
                                if (checked) std::cout << "Zoom 100%" << std::endl;
                            }),
                            MenuItemData::Radio("Zoom 150%", 1, false, [](bool checked) {
                                if (checked) std::cout << "Zoom 150%" << std::endl;
                            })
                })
                .AddSubmenu("Help", {
                            MenuItemData::ActionWithShortcut("📖 Documentation", "F1", []() {
                                std::cout << "Show documentation" << std::endl;
                            }),
                            MenuItemData::Action("🎓 Tutorials", []() {
                                std::cout << "Show tutorials" << std::endl;
                            }),
                            MenuItemData::Separator(),
                            MenuItemData::Action("ℹ️ About UltraCanvas", []() {
                                std::cout << "About UltraCanvas Framework" << std::endl;
                            })
                })
                .Build();

        container->AddChild(mainMenuBar);

        // Dark theme menu
        auto darkMenuBtn = std::make_shared<UltraCanvasButton>("DarkMenuBtn", 115, 20, 225, 150, 35);
        darkMenuBtn->SetText("Dark Theme Menu");
        container->AddChild(darkMenuBtn);

        auto darkMenu = std::make_shared<UltraCanvasMenu>("DarkMenu", 116, 0, 0, 200, 0);
        darkMenu->SetMenuType(MenuType::PopupMenu);
        darkMenu->SetStyle(MenuStyle::Dark());

        darkMenu->AddItem(MenuItemData::Action("🌙 Dark Mode", []() {
            std::cout << "Dark mode activated" << std::endl;
        }));

        darkMenu->AddItem(MenuItemData::Action("☀️ Light Mode", []() {
            std::cout << "Light mode activated" << std::endl;
        }));

        darkMenu->AddItem(MenuItemData::Action("🎨 Custom Theme", []() {
            std::cout << "Custom theme" << std::endl;
        }));

        darkMenuBtn->onClick = [darkMenu, darkMenuBtn, container](const UCEvent& ev) {
            container->GetWindow()->AddChild(darkMenu);
            Point2Di pos(darkMenuBtn->GetXInWindow(), darkMenuBtn->GetYInWindow() + darkMenuBtn->GetHeight());
            darkMenu->ShowAt(pos);
        };

        container->AddChild(darkMenu);

        // Flat style menu
        auto flatMenuBtn = std::make_shared<UltraCanvasButton>("FlatMenuBtn", 117, 180, 225, 150, 35);
        flatMenuBtn->SetText("Flat Style Menu");
        container->AddChild(flatMenuBtn);

        auto flatMenu = std::make_shared<UltraCanvasMenu>("FlatMenu", 118, 0, 0, 200, 0);
        flatMenu->SetMenuType(MenuType::PopupMenu);
        flatMenu->SetStyle(MenuStyle::Flat());

        flatMenu->AddItem(MenuItemData::Action("📱 Mobile View", []() {
            std::cout << "Mobile view" << std::endl;
        }));

        flatMenu->AddItem(MenuItemData::Action("💻 Desktop View", []() {
            std::cout << "Desktop view" << std::endl;
        }));

        flatMenu->AddItem(MenuItemData::Action("📱 Tablet View", []() {
            std::cout << "Tablet view" << std::endl;
        }));

        flatMenuBtn->onClick = [flatMenu, flatMenuBtn, container](const UCEvent& ev) {
            container->GetWindow()->AddChild(flatMenu);
            Point2Di pos(flatMenuBtn->GetXInWindow(), flatMenuBtn->GetYInWindow() + flatMenuBtn->GetHeight());
            flatMenu->ShowAt(pos);
        };

        container->AddChild(flatMenu);

        // Info label about menu features
        auto infoLabel = std::make_shared<UltraCanvasLabel>("InfoLabel", 119, 20, 270, 960, 140);
        infoLabel->SetText("Menu Features:\n"
                           "• Context menus with right-click\n"
                           "• Main menu bar with dropdowns\n"
                           "• Submenus and nested navigation\n"
                           "• Checkbox and radio button items\n"
                           "• Keyboard shortcuts and icons\n"
                           "• Multiple visual styles (Default, Dark, Flat)");
        infoLabel->SetFontSize(11);
        infoLabel->SetTextColor(Color(80, 80, 80, 255));
        container->AddChild(infoLabel);

        // Popup menu example
        auto popupLabel = std::make_shared<UltraCanvasLabel>("PopupLabel", 120, 20, 405, 200, 30);
        popupLabel->SetText("Popup Menu Example:");
        popupLabel->SetFontSize(14);
        popupLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(popupLabel);

        // Create a sample list for popup menu
        auto listContainer = std::make_shared<UltraCanvasContainer>("ListContainer", 121, 20, 430, 300, 150);
        ContainerStyle listStyle;
        listStyle.backgroundColor = Color(255, 255, 255, 255);
        listStyle.borderWidth = 1.0f;
        listStyle.borderColor = Color(200, 200, 200, 255);
        listContainer->SetContainerStyle(listStyle);
        container->AddChild(listContainer);

        // Add sample items to list
        for (int i = 0; i < 5; i++) {
            auto itemLabel = std::make_shared<UltraCanvasLabel>(
                    "ListItem" + std::to_string(i),
                    122 + i,
                    10, 10 + i * 25, 280, 20
            );
            itemLabel->SetText("Item " + std::to_string(i + 1) + " - Right-click for options");
            itemLabel->SetBackgroundColor(Color(250, 250, 250, 255));

            // Create item-specific popup menu
            auto itemMenu = std::make_shared<UltraCanvasMenu>(
                    "ItemMenu" + std::to_string(i),
                    130 + i,
                    0, 0, 150, 0
            );
            itemMenu->SetMenuType(MenuType::PopupMenu);

            itemMenu->AddItem(MenuItemData::Action("✏️ Edit", [i]() {
                std::cout << "Edit item " << (i + 1) << std::endl;
            }));

            itemMenu->AddItem(MenuItemData::Action("📋 Duplicate", [i]() {
                std::cout << "Duplicate item " << (i + 1) << std::endl;
            }));

            itemMenu->AddItem(MenuItemData::Action("🗑️ Delete", [i]() {
                std::cout << "Delete item " << (i + 1) << std::endl;
            }));

            // Set right-click handler
            itemLabel->onClick = [itemMenu, itemLabel, container](const UCEvent& ev) {
                if (ev.button == UCMouseButton::Right) {
                    container->GetWindow()->AddChild(itemMenu);
                    //Point2Di pos(itemLabel->GetXInWindow() + 50, itemLabel->GetYInWindow() + itemLabel->GetHeight());
                    itemMenu->ShowAt(ev.windowX, ev.windowY);
                }
            };

            listContainer->AddChild(itemLabel);
            container->AddChild(itemMenu);
        }

        std::cout << "✓ Menu examples created" << std::endl;
        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDialogExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBitmapExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::Create3DExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateVideoExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTextDocumentExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMarkdownExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateCodeEditorExamples() {
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
        placeholder->SetText("Code Editor Component - Partially Implemented\n\nCurrent Features:\n• Formula/Expression editing\n• Basic syntax validation\n\nPlanned Features:\n• C++ syntax highlighting\n• Pascal/Delphi support\n• COBOL syntax support\n• Line numbers and folding\n• Auto-completion\n• Error markers and tooltips\n• Find and replace\n• Multiple cursors\n• Code formatting\n• Plugin architecture for languagesRules");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 255, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorderWidth(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateAudioExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDiagramExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateChartExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateInfoGraphicsExamples() {
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDivergingChartExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("DivergingChartExamples", 2500, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("DivergingChartTitle", 2501, 10, 10, 300, 30);
        title->SetText("Diverging Bar Chart Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Create the diverging bar chart
        auto divergingChart = std::make_shared<UltraCanvasDivergingBarChart>("divergingChart", 2502, 100, 50, 700, 450);
        divergingChart->SetChartTitle("Survey Response Distribution");
        divergingChart->SetChartStyle(DivergingChartStyle::LikertScale);
        divergingChart->SetBarHeight(0.85f);
        divergingChart->SetCenterGap(5.0f);
        divergingChart->SetGridEnabled(true);
        divergingChart->SetShowCenterLine(true);
        divergingChart->SetShowRowLabels(true);

        // Set up Likert scale categories
        std::vector<DivergingCategory> categories;
        categories.emplace_back("Strongly Disagree", Color(178, 24, 43, 255), false);
        categories.emplace_back("Disagree", Color(244, 165, 130, 255), false);
        categories.emplace_back("Neutral", Color(220, 220, 220, 255), false);
        categories.emplace_back("Agree", Color(146, 197, 222, 255), true);
        categories.emplace_back("Strongly Agree", Color(33, 102, 172, 255), true);

        divergingChart->SetCategories(categories);

        // Load sample data
        divergingChart->ClearData();
        std::vector<std::string> rowLabels = {"Question 1", "Question 2", "Question 3", "Question 4", "Question 5"};

        // Generate sample Likert scale data
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dist(10.0, 50.0);

        for (const auto& label : rowLabels) {
            std::map<std::string, float> values;
            values["Strongly Disagree"] = dist(gen);
            values["Disagree"] = dist(gen);
            values["Neutral"] = dist(gen) * 0.5f;
            values["Agree"] = dist(gen);
            values["Strongly Agree"] = dist(gen);
            divergingChart->AddDataRow(label, values);
        }

        container->AddChild(divergingChart);

        // Control buttons
        int buttonY = 520;
        int buttonWidth = 100;
        int buttonHeight = 30;
        int buttonSpacing = 10;
        int currentX = 100;

        // Style selection buttons
        auto btnLikert = std::make_shared<UltraCanvasButton>("btnLikert", 2503, currentX, buttonY, buttonWidth, buttonHeight);
        btnLikert->SetText("Likert Scale");
        btnLikert->SetColors(Color(70, 130, 180, 255), Color(90, 150, 200, 255), Color(50, 100, 160, 255), Color(150, 200, 240, 255));
        btnLikert->SetTextColors(Colors::White, Colors::White, Colors::White, Colors::White);
        btnLikert->onClick = [divergingChart](const UCEvent& ev) {
            divergingChart->SetChartStyle(DivergingChartStyle::LikertScale);
        };
        container->AddChild(btnLikert);
        currentX += buttonWidth + buttonSpacing;

        auto btnPyramid = std::make_shared<UltraCanvasButton>("btnPyramid", 2504, currentX, buttonY, buttonWidth, buttonHeight);
        btnPyramid->SetText("Pyramid");
        btnPyramid->SetColors(Color(70, 130, 180, 255), Color(90, 150, 200, 255), Color(50, 100, 160, 255), Color(150, 200, 240, 255));
        btnPyramid->SetTextColors(Colors::White, Colors::White, Colors::White, Colors::White);
        btnPyramid->onClick = [divergingChart](const UCEvent& ev) {
            divergingChart->SetChartStyle(DivergingChartStyle::PopulationPyramid);
        };
        container->AddChild(btnPyramid);
        currentX += buttonWidth + buttonSpacing;

        auto btnTornado = std::make_shared<UltraCanvasButton>("btnTornado", 2505, currentX, buttonY, buttonWidth, buttonHeight);
        btnTornado->SetText("Tornado");
        btnTornado->SetColors(Color(70, 130, 180, 255), Color(90, 150, 200, 255), Color(50, 100, 160, 255), Color(150, 200, 240, 255));
        btnTornado->SetTextColors(Colors::White, Colors::White, Colors::White, Colors::White);
        btnTornado->onClick = [divergingChart](const UCEvent& ev) {
            divergingChart->SetChartStyle(DivergingChartStyle::TornadoChart);
        };
        container->AddChild(btnTornado);
        currentX += buttonWidth + buttonSpacing;

        // Toggle buttons
        auto btnToggleGrid = std::make_shared<UltraCanvasButton>("btnGrid", 2506, currentX, buttonY, buttonWidth, buttonHeight);
        btnToggleGrid->SetText("Toggle Grid");
        btnToggleGrid->SetColors(Color(70, 130, 180, 255), Color(90, 150, 200, 255), Color(50, 100, 160, 255), Color(150, 200, 240, 255));
        btnToggleGrid->SetTextColors(Colors::White, Colors::White, Colors::White, Colors::White);
        btnToggleGrid->onClick = [divergingChart](const UCEvent& ev) {
            static bool showGrid = true;
            showGrid = !showGrid;
            divergingChart->SetGridEnabled(showGrid);
        };
        container->AddChild(btnToggleGrid);
        currentX += buttonWidth + buttonSpacing;

        auto btnGenerateData = std::make_shared<UltraCanvasButton>("btnGenerate", 2507, currentX, buttonY, buttonWidth, buttonHeight);
        btnGenerateData->SetText("Random Data");
        btnGenerateData->SetColors(Color(70, 130, 180, 255), Color(90, 150, 200, 255), Color(50, 100, 160, 255), Color(150, 200, 240, 255));
        btnGenerateData->SetTextColors(Colors::White, Colors::White, Colors::White, Colors::White);
        btnGenerateData->onClick = [divergingChart](const UCEvent& ev) {
            // Generate new random data
            divergingChart->ClearData();
            std::vector<std::string> newLabels = {"Item A", "Item B", "Item C", "Item D", "Item E", "Item F"};
            
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<> dist(5.0, 45.0);

            for (const auto& label : newLabels) {
                std::map<std::string, float> values;
                values["Strongly Disagree"] = dist(gen);
                values["Disagree"] = dist(gen);
                values["Neutral"] = dist(gen) * 0.4f;
                values["Agree"] = dist(gen);
                values["Strongly Agree"] = dist(gen);
                divergingChart->AddDataRow(label, values);
            }
        };
        container->AddChild(btnGenerateData);

        // Info label
        auto infoLabel = std::make_shared<UltraCanvasLabel>("DivergingChartInfo", 2508, 820, 60, 170, 350);
        infoLabel->SetText("Diverging Bar Chart Features:\n\n• Likert scale visualization\n• Population pyramid style\n• Tornado chart format\n• Interactive controls\n• Multiple data categories\n• Customizable colors\n• Grid and center line options\n• Dynamic data updates\n\nClick the buttons below to:\n• Change chart style\n• Toggle grid display\n• Generate random data");
        infoLabel->SetFontSize(11);
        infoLabel->SetTextColor(Color(80, 80, 80, 255));
        infoLabel->SetBackgroundColor(Color(250, 250, 250, 255));
        infoLabel->SetBorderWidth(1.0f);
        infoLabel->SetPadding(10.0f);
        container->AddChild(infoLabel);

        return container;
    }


    // Create TextArea Examples for Extended Functionality category
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTextAreaExamples() {
        // Create main container with fixed size
        auto container = std::make_shared<UltraCanvasContainer>("TextAreaContainer", 1000, 0, 0, 1000, 780);

//        // Title Label - centered at top
//        auto titleLabel = std::make_shared<UltraCanvasLabel>("TitleLabel", 1001, 20, 10, 760, 30);
//        titleLabel->SetText("UltraCanvas TextArea - Syntax Highlighting Demo");
//        titleLabel->SetFontSize(18);
//        titleLabel->SetFontWeight(FontWeight::Bold);
//        titleLabel->SetAlignment(TextAlignment::Center);
//        container->AddChild(titleLabel);

        // Description Label - below title
        auto descLabel = std::make_shared<UltraCanvasLabel>("DescLabel", 1002, 20, 5, 760, 20);
        descLabel->SetText("Advanced text editing with syntax highlighting, line numbers, and theme support");
        descLabel->SetFontSize(12);
        descLabel->SetTextColor(Color(100, 100, 100));
        descLabel->SetAlignment(TextAlignment::Center);
        container->AddChild(descLabel);

        // ===== TEXTAREA 1: C++ Code with Dark Theme =====
        // Left column
        auto cppLabel = std::make_shared<UltraCanvasLabel>("CppLabel", 1011, 20, 30, 245, 20);
        cppLabel->SetText("C++ Syntax (Dark Theme)");
        cppLabel->SetFontSize(12);
        cppLabel->SetFontWeight(FontWeight::Bold);
        cppLabel->SetAlignment(TextAlignment::Left);
        container->AddChild(cppLabel);

        // C++ TextArea
        auto cppTextArea = std::make_shared<UltraCanvasTextArea>("CppTextArea", 1012, 20, 50, 950, 200);
        cppTextArea->ApplyDarkCodeStyle("C++");
        cppTextArea->SetShowLineNumbers(true);
        cppTextArea->SetHighlightCurrentLine(true);
        cppTextArea->SetFontSize(13);

        // Sample C++ code
        std::string cppCode = R"(// UltraCanvas Example
#include <iostream>
#include <vector>
#include <memory>

class Widget {
private:
    std::string name;
    int id;

public:
    Widget(const std::string& n, int i)
        : name(n), id(i) {}

    virtual void Render() {
        std::cout << "Rendering: "
                  << name << std::endl;
    }

    int GetId() const {
        return id;
    }
};

int main() {
    std::vector<std::unique_ptr<Widget>> widgets;

    // Create widgets
    for (int i = 0; i < 10; ++i) {
        auto widget = std::make_unique<Widget>(
            "Widget_" + std::to_string(i), i
        );
        widgets.push_back(std::move(widget));
    }

    // Render all widgets
    for (const auto& w : widgets) {
        w->Render();
    }

    return 0;
})";

        cppTextArea->SetText(cppCode);
        container->AddChild(cppTextArea);

        // ===== TEXTAREA 2: Python Code with Light Theme =====
        // Middle column
        auto pythonLabel = std::make_shared<UltraCanvasLabel>("PythonLabel", 1021, 20, 255, 245, 20);
        pythonLabel->SetText("Python Syntax (Light Theme)");
        pythonLabel->SetFontSize(12);
        pythonLabel->SetFontWeight(FontWeight::Bold);
        pythonLabel->SetAlignment(TextAlignment::Left);
        container->AddChild(pythonLabel);

        // Python TextArea
        auto pythonTextArea = std::make_shared<UltraCanvasTextArea>("PythonTextArea", 1022, 20, 275, 950, 200);
        pythonTextArea->ApplyCodeStyle("Python");
        pythonTextArea->SetShowLineNumbers(true);
        pythonTextArea->SetHighlightCurrentLine(true);
        pythonTextArea->SetFontSize(13);

        // Sample Python code
        std::string pythonCode = R"(# UltraCanvas Python Example
import sys
import json
from typing import List, Dict, Optional

class CanvasElement:
    """Base class for canvas elements"""

    def __init__(self, name: str, x: int, y: int):
        self.name = name
        self.position = (x, y)
        self.visible = True
        self.children: List[CanvasElement] = []

    def render(self) -> None:
        """Render the element"""
        if not self.visible:
            return

        print(f"Rendering {self.name}")
        for child in self.children:
            child.render()

    def add_child(self, element: 'CanvasElement'):
        """Add a child element"""
        self.children.append(element)

    @property
    def child_count(self) -> int:
        return len(self.children)

def main():
    # Create root element
    root = CanvasElement("Root", 0, 0)

    # Create child elements
    for i in range(5):
        child = CanvasElement(
            f"Child_{i}",
            i * 100,
            i * 50
        )
        root.add_child(child)

    # Render all
    root.render()
    print(f"Total children: {root.child_count}")

if __name__ == "__main__":
    main())";

        pythonTextArea->SetText(pythonCode);
        container->AddChild(pythonTextArea);

        // ===== TEXTAREA 3: Pascal Code with Custom Settings =====
        // Right column
        auto pascalLabel = std::make_shared<UltraCanvasLabel>("PascalLabel", 1031, 20, 480, 245, 20);
        pascalLabel->SetText("Pascal Syntax (Custom Theme)");
        pascalLabel->SetFontSize(12);
        pascalLabel->SetFontWeight(FontWeight::Bold);
        pascalLabel->SetAlignment(TextAlignment::Left);
        container->AddChild(pascalLabel);

        // Pascal TextArea with custom settings
        auto pascalTextArea = std::make_shared<UltraCanvasTextArea>("PascalTextArea", 1032, 20, 500, 950, 200);

        // Apply custom style for Pascal
        pascalTextArea->ApplyCodeStyle("Pascal");
        pascalTextArea->SetShowLineNumbers(true);
        pascalTextArea->SetHighlightCurrentLine(true); // Different setting
        pascalTextArea->SetFontSize(13);

        // Custom color scheme for Pascal
        TextAreaStyle pascalStyle = pascalTextArea->GetStyle();
        pascalStyle.backgroundColor = Color(250, 250, 245);  // Cream background
        pascalStyle.fontColor = Color(40, 40, 40);
        pascalStyle.lineNumbersBackgroundColor = Color(240, 240, 235);
        pascalStyle.lineNumbersColor = Color(120, 120, 120);
        pascalStyle.currentLineColor = Color(245, 245, 240);
        pascalStyle.borderColor = Color(180, 180, 180);
        pascalStyle.borderWidth = 2;

        // Custom token colors for Pascal
        pascalStyle.tokenStyles.keywordStyle = TokenStyle(Color(0, 0, 200), true);      // Blue bold
        pascalStyle.tokenStyles.typeStyle = TokenStyle(Color(128, 0, 128));             // Purple
        pascalStyle.tokenStyles.functionStyle = TokenStyle(Color(180, 90, 0));          // Brown
        pascalStyle.tokenStyles.stringStyle = TokenStyle(Color(200, 0, 0));             // Red
        pascalStyle.tokenStyles.commentStyle = TokenStyle(Color(0, 128, 0), false, true); // Green italic
        pascalStyle.tokenStyles.numberStyle = TokenStyle(Color(0, 128, 128));           // Teal
        pascalStyle.tokenStyles.operatorStyle = TokenStyle(Color(80, 80, 80));          // Dark gray

        pascalTextArea->SetStyle(pascalStyle);

        // Sample Pascal code
        std::string pascalCode = R"({ UltraCanvas Pascal Example }
program CanvasDemo;

uses
  SysUtils, Classes, Graphics;

type
  TCanvasElement = class(TObject)
  private
    FName: string;
    FPosition: TPoint;
    FVisible: Boolean;
    FChildren: TList;
  public
    constructor Create(const AName: string);
    destructor Destroy; override;

    procedure Render; virtual;
    procedure AddChild(AElement: TCanvasElement);

    property Name: string read FName;
    property Visible: Boolean
      read FVisible write FVisible;
  end;

constructor TCanvasElement.Create(
  const AName: string);
begin
  inherited Create;
  FName := AName;
  FVisible := True;
  FChildren := TList.Create;
end;

destructor TCanvasElement.Destroy;
var
  I: Integer;
begin
  for I := 0 to FChildren.Count - 1 do
    TCanvasElement(FChildren[I]).Free;
  FChildren.Free;
  inherited;
end;

procedure TCanvasElement.Render;
var
  I: Integer;
  Child: TCanvasElement;
begin
  if not FVisible then
    Exit;

  WriteLn('Rendering: ', FName);

  { Render all children }
  for I := 0 to FChildren.Count - 1 do
  begin
    Child := TCanvasElement(FChildren[I]);
    Child.Render;
  end;
end;

procedure TCanvasElement.AddChild(
  AElement: TCanvasElement);
begin
  if Assigned(AElement) then
    FChildren.Add(AElement);
end;

var
  Root: TCanvasElement;
  Child: TCanvasElement;
  I: Integer;

begin
  Root := TCanvasElement.Create('Root');
  try
    { Create child elements }
    for I := 1 to 5 do
    begin
      Child := TCanvasElement.Create(
        Format('Child_%d', [I])
      );
      Root.AddChild(Child);
    end;

    { Render the tree }
    Root.Render;
  finally
    Root.Free;
  end;
end.)";

        pascalTextArea->SetText(pascalCode);
        container->AddChild(pascalTextArea);

        // ===== CONTROLS SECTION =====
        // Controls positioned at bottom of the window

        // Read-only checkbox
//        auto readOnlyCheckbox = std::make_shared<UltraCanvasCheckBox>("ReadOnlyCheckbox", 1041, 20, 540, 120, 25);
//        readOnlyCheckbox->SetText("Read Only");
//        readOnlyCheckbox->SetChecked(false);
//        readOnlyCheckbox->SetOnCheckedChanged([cppTextArea, pythonTextArea, pascalTextArea](bool checked) {
//            cppTextArea->SetReadOnly(checked);
//            pythonTextArea->SetReadOnly(checked);
//            pascalTextArea->SetReadOnly(checked);
//        });
//        container->AddChild(readOnlyCheckbox);
//
//        // Word wrap checkbox
//        auto wordWrapCheckbox = std::make_shared<UltraCanvasCheckBox>("WordWrapCheckbox", 1042, 150, 540, 120, 25);
//        wordWrapCheckbox->SetText("Word Wrap");
//        wordWrapCheckbox->SetChecked(false);
//        wordWrapCheckbox->SetOnCheckedChanged([cppTextArea, pythonTextArea, pascalTextArea](bool checked) {
//            cppTextArea->SetWordWrap(checked);
//            pythonTextArea->SetWordWrap(checked);
//            pascalTextArea->SetWordWrap(checked);
//        });
//        container->AddChild(wordWrapCheckbox);

        // Theme selector dropdown
//        auto themeDropdown = std::make_shared<UltraCanvasDropdown>("ThemeDropdown", 1043, 280, 540, 150, 25);
//        themeDropdown->AddItem("Keep Current");
//        themeDropdown->AddItem("All Light");
//        themeDropdown->AddItem("All Dark");
//        themeDropdown->SetSelectedIndex(0);
//        themeDropdown->onSelectionChanged = [cppTextArea, pythonTextArea, pascalTextArea](int index, const DropdownItem&) {
//            if (index == 1) {  // All Light
//                cppTextArea->ApplyLightTheme();
//                pythonTextArea->ApplyLightTheme();
//                pascalTextArea->ApplyLightTheme();
//            } else if (index == 2) {  // All Dark
//                cppTextArea->ApplyDarkTheme();
//                pythonTextArea->ApplyDarkTheme();
//                pascalTextArea->ApplyDarkTheme();
//            }
//        };
//        container->AddChild(themeDropdown);

        // Font size label
        auto fontSizeLabel = std::make_shared<UltraCanvasLabel>("FontSizeLabel", 1044, 420, 720, 90, 20);
        fontSizeLabel->SetText("Font Size:");
        fontSizeLabel->SetAlignment(TextAlignment::Right);
        container->AddChild(fontSizeLabel);

        // Decrease font button
        auto decreaseFontBtn = std::make_shared<UltraCanvasButton>("DecreaseFontBtn", 1045, 515, 720, 30, 25);
        decreaseFontBtn->SetText("-");
        decreaseFontBtn->onClick = [cppTextArea, pythonTextArea, pascalTextArea](const UCEvent& ev) {
            int newSize = cppTextArea->GetFontSize() - 1;
            if (newSize >= 8) {
                cppTextArea->SetFontSize(newSize);
                pythonTextArea->SetFontSize(newSize);
                pascalTextArea->SetFontSize(newSize);
            }
        };
        container->AddChild(decreaseFontBtn);

        // Increase font button
        auto increaseFontBtn = std::make_shared<UltraCanvasButton>("IncreaseFontBtn", 1046, 550, 720, 30, 25);
        increaseFontBtn->SetText("+");
        increaseFontBtn->onClick = [cppTextArea, pythonTextArea, pascalTextArea](const UCEvent& ev) {
            int newSize = cppTextArea->GetFontSize() + 1;
            if (newSize <= 20) {
                cppTextArea->SetFontSize(newSize);
                pythonTextArea->SetFontSize(newSize);
                pascalTextArea->SetFontSize(newSize);
            }
        };
        container->AddChild(increaseFontBtn);

        // Clear all button
        auto clearAllBtn = std::make_shared<UltraCanvasButton>("ClearAllBtn", 1047, 590, 720, 100, 25);
        clearAllBtn->SetText("Clear All");
//        clearAllBtn->SetButtonStyle(ButtonStyle::Danger);
        clearAllBtn->onClick = [cppTextArea, pythonTextArea, pascalTextArea](const UCEvent& ev) {
            cppTextArea->Clear();
            pythonTextArea->Clear();
            pascalTextArea->Clear();
        };
        container->AddChild(clearAllBtn);

        // Line numbers toggle button
        auto lineNumBtn = std::make_shared<UltraCanvasButton>("LineNumBtn", 1048, 700, 720, 80, 25);
        lineNumBtn->SetText("Toggle Lines");
//        lineNumBtn->SetButtonStyle(ButtonStyle::Secondary);
        lineNumBtn->onClick = [cppTextArea, pythonTextArea, pascalTextArea](const UCEvent& ev) {
            bool showLines = !cppTextArea->GetShowLineNumbers();
            cppTextArea->SetShowLineNumbers(showLines);
            pythonTextArea->SetShowLineNumbers(showLines);
            pascalTextArea->SetShowLineNumbers(showLines);
        };
        container->AddChild(lineNumBtn);

        auto syntaxToggleBtn = std::make_shared<UltraCanvasButton>("syntaxToggleBtn", 1048, 810, 720, 80, 25);
        syntaxToggleBtn->SetText("Toggle syntax");
//        lineNumBtn->SetButtonStyle(ButtonStyle::Secondary);
        syntaxToggleBtn->onClick = [cppTextArea, pythonTextArea, pascalTextArea](const UCEvent& ev) {
            bool showSyntax = !cppTextArea->GetHighlightSyntax();
            cppTextArea->SetHighlightSyntax(showSyntax);
            pythonTextArea->SetHighlightSyntax(showSyntax);
            pascalTextArea->SetHighlightSyntax(showSyntax);
        };
        container->AddChild(syntaxToggleBtn);

        // Info labels at bottom
        auto infoLabel = std::make_shared<UltraCanvasLabel>("InfoLabel", 1050, 20, 750, 760, 10);
        infoLabel->SetText("Try typing in any text area - supports full editing, selection, copy/paste, and undo/redo");
        infoLabel->SetFontSize(10);
        infoLabel->SetTextColor(Color(128, 128, 128));
        infoLabel->SetAlignment(TextAlignment::Center);
        container->AddChild(infoLabel);

        return container;
    }

} // namespace UltraCanvas