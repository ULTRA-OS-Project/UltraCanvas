// ===== TAB ADDITION: WINDOWS C: DRIVE VISUALIZATION =====
// Add this tab to UltraCanvasGourceTreeExamples.cpp
// Insert after the "Storage" tab section and before "Performance" tab

    // ========================================
    // TAB: WINDOWS C: DRIVE (6 LEVELS)
    // ========================================
    auto windowsContainer = std::make_shared<UltraCanvasContainer>(
        "WindowsTab", 0, 0, 1000, 640
    );
    
    auto windowsDesc = std::make_shared<UltraCanvasLabel>(
        "WindowsDesc", 10, 10, 980, 25
    );
    windowsDesc->SetText("Windows C: Drive: Realistic 6-level folder structure of a Windows installation");
    windowsDesc->SetFontSize(11);
    windowsContainer->AddChild(windowsDesc);
    
    // Control panel for Windows view
    auto windowsControls = std::make_shared<UltraCanvasContainer>(
        "WindowsControls", 10, 40, 980, 45
    );
    windowsControls->SetBackgroundColor(Color(248, 248, 252));
    
    // Theme dropdown
    auto winThemeLabel = std::make_shared<UltraCanvasLabel>(
        "WinThemeLabel", 10, 12, 50, 20
    );
    winThemeLabel->SetText("Theme:");
    winThemeLabel->SetFontSize(11);
    windowsControls->AddChild(winThemeLabel);
    
    auto winThemeDropdown = std::make_shared<UltraCanvasDropdown>(
        "WinThemeDropdown", 65, 8, 100, 28
    );
    winThemeDropdown->AddItem("Dark");
    winThemeDropdown->AddItem("Light");
    winThemeDropdown->AddItem("Colorful");
    winThemeDropdown->AddItem("Mono");
    winThemeDropdown->SetSelectedIndex(1);  // Light theme for Windows
    windowsControls->AddChild(winThemeDropdown);
    
    // Highlight mode
    auto winHighlightLabel = std::make_shared<UltraCanvasLabel>(
        "WinHighlightLabel", 180, 12, 60, 20
    );
    winHighlightLabel->SetText("Highlight:");
    winHighlightLabel->SetFontSize(11);
    windowsControls->AddChild(winHighlightLabel);
    
    auto winHighlightDropdown = std::make_shared<UltraCanvasDropdown>(
        "WinHighlightDropdown", 245, 8, 130, 28
    );
    winHighlightDropdown->AddItem("None");
    winHighlightDropdown->AddItem("Recent Access");
    winHighlightDropdown->AddItem("Old Files");
    winHighlightDropdown->AddItem("By Creation");
    winHighlightDropdown->SetSelectedIndex(0);
    windowsControls->AddChild(winHighlightDropdown);
    
    // File size checkbox
    auto winFileSizeCheck = std::make_shared<UltraCanvasCheckbox>(
        "WinFileSizeCheck", 390, 12, 110, 20
    );
    winFileSizeCheck->SetText("Show Size");
    winFileSizeCheck->SetChecked(true);
    windowsControls->AddChild(winFileSizeCheck);
    
    // Depth limit slider
    auto winDepthLabel = std::make_shared<UltraCanvasLabel>(
        "WinDepthLabel", 510, 12, 50, 20
    );
    winDepthLabel->SetText("Depth:");
    winDepthLabel->SetFontSize(11);
    windowsControls->AddChild(winDepthLabel);
    
    auto winDepthSlider = std::make_shared<UltraCanvasSlider>(
        "WinDepthSlider", 560, 10, 100, 25
    );
    winDepthSlider->SetRange(1, 6);
    winDepthSlider->SetValue(6);
    winDepthSlider->SetStep(1);
    windowsControls->AddChild(winDepthSlider);
    
    auto winDepthValue = std::make_shared<UltraCanvasLabel>(
        "WinDepthValue", 665, 12, 25, 20
    );
    winDepthValue->SetText("6");
    winDepthValue->SetFontSize(11);
    windowsControls->AddChild(winDepthValue);
    
    // Action buttons
    auto winExpandBtn = std::make_shared<UltraCanvasButton>(
        "WinExpandBtn", 710, 8, 60, 28
    );
    winExpandBtn->SetText("Expand");
    windowsControls->AddChild(winExpandBtn);
    
    auto winCollapseBtn = std::make_shared<UltraCanvasButton>(
        "WinCollapseBtn", 775, 8, 65, 28
    );
    winCollapseBtn->SetText("Collapse");
    windowsControls->AddChild(winCollapseBtn);
    
    auto winZoomFitBtn = std::make_shared<UltraCanvasButton>(
        "WinZoomFitBtn", 845, 8, 50, 28
    );
    winZoomFitBtn->SetText("Fit");
    windowsControls->AddChild(winZoomFitBtn);
    
    auto winExportBtn = std::make_shared<UltraCanvasButton>(
        "WinExportBtn", 900, 8, 75, 28
    );
    winExportBtn->SetText("Export");
    windowsControls->AddChild(winExportBtn);
    
    windowsContainer->AddChild(windowsControls);
    
    // Windows C: Drive Gource Tree
    auto windowsTree = std::make_shared<UltraCanvasGourceTree>(
        "WindowsTree", 10, 90, 980, 540
    );
    
    // Generate Windows C: drive data
    GenerateWindowsCDriveData(windowsTree.get());
    windowsTree->SetLayoutMode(GourceLayoutMode::Hybrid);
    windowsTree->SetShowFileSizeArea(true);
    windowsTree->SetTheme(GourceTheme::Light);
    windowsTree->SetMaxDepth(6);
    windowsTree->PerformLayout();
    windowsTree->ZoomToFit();
    
    // Callbacks
    windowsTree->onNodeClick = [statusLabel](const std::string& nodeId) {
        statusLabel->SetText("Selected: " + nodeId);
    };
    
    windowsTree->onNodeExpand = [statusLabel](const std::string& nodeId) {
        statusLabel->SetText("Expanded: " + nodeId);
    };
    
    windowsTree->onNodeCollapse = [statusLabel](const std::string& nodeId) {
        statusLabel->SetText("Collapsed: " + nodeId);
    };
    
    windowsTree->onNodeHover = [statusLabel](const std::string& nodeId) {
        // Only update on hover for files (not constant updates)
        if (nodeId.find('.') != std::string::npos) {
            statusLabel->SetText("File: " + nodeId);
        }
    };
    
    // Control handlers
    winThemeDropdown->onItemSelected = [windowsTree](int index, const DropdownItem& item) {
        switch (index) {
            case 0: windowsTree->SetTheme(GourceTheme::Dark); break;
            case 1: windowsTree->SetTheme(GourceTheme::Light); break;
            case 2: windowsTree->SetTheme(GourceTheme::Colorful); break;
            case 3: windowsTree->SetTheme(GourceTheme::Monochrome); break;
        }
    };
    
    winHighlightDropdown->onItemSelected = [windowsTree, statusLabel](int index, const DropdownItem& item) {
        switch (index) {
            case 0: 
                windowsTree->SetHighlightMode(GourceHighlightMode::NoneHighlight);
                statusLabel->SetText("Highlight: None");
                break;
            case 1: 
                windowsTree->SetHighlightMode(GourceHighlightMode::LastAccess);
                statusLabel->SetText("Highlighting recently accessed files (brighter = more recent)");
                break;
            case 2: 
                windowsTree->SetHighlightMode(GourceHighlightMode::InvertLastAccess);
                statusLabel->SetText("Highlighting old/unused files (brighter = older)");
                break;
            case 3: 
                windowsTree->SetHighlightMode(GourceHighlightMode::CreationDate);
                statusLabel->SetText("Highlighting by creation date (brighter = newer)");
                break;
        }
    };
    
    winFileSizeCheck->onStateChanged = [windowsTree](CheckboxState state) {
        windowsTree->SetShowFileSizeArea(state == CheckboxState::Checked);
    };
    
    winDepthSlider->onValueChanged = [windowsTree, winDepthValue](float value) {
        int depth = static_cast<int>(value);
        winDepthValue->SetText(std::to_string(depth));
        windowsTree->SetMaxDepth(depth);
    };
    
    winExpandBtn->onClick = [windowsTree, statusLabel]() {
        windowsTree->ExpandAll();
        statusLabel->SetText("All nodes expanded");
    };
    
    winCollapseBtn->onClick = [windowsTree, statusLabel]() {
        windowsTree->CollapseAll();
        statusLabel->SetText("All nodes collapsed");
    };
    
    winZoomFitBtn->onClick = [windowsTree]() {
        windowsTree->ZoomToFit();
    };
    
    winExportBtn->onClick = [windowsTree, statusLabel]() {
        if (windowsTree->SaveToSVG("/tmp/windows_c_drive.svg")) {
            statusLabel->SetText("Exported to /tmp/windows_c_drive.svg");
        } else {
            statusLabel->SetText("Export failed!");
        }
    };
    
    windowsContainer->AddChild(windowsTree);

// ===== UPDATE TAB REGISTRATION =====
// Change the tab order to include Windows C: Drive:

    // ===== ADD TABS =====
    tabbedContainer->AddTab("Project", projectContainer);
    tabbedContainer->AddTab("Storage", storageContainer);
    tabbedContainer->AddTab("Windows C:", windowsContainer);  // NEW TAB
    tabbedContainer->AddTab("Performance", perfContainer);
    tabbedContainer->AddTab("Custom", customContainer);


// ===== UPDATE DEMO REGISTRATION (in UltraCanvasDemo.cpp) =====
// Add "Windows C: Drive" as a variant:

diagramBuilder.AddItem(
    "gourcetree",
    "Gource Tree",
    "Radial tree visualization for file systems and storage devices with Gource-style layout",
    ImplementationStatus::FullyImplemented,
    [this]() { return CreateGourceTreeExamples(); },
    "Examples/UltraCanvasGourceTreeExamples.cpp",
    "Docs/UltraCanvasGourceTree.md"
)
.AddVariant("gourcetree", "Project Structure")
.AddVariant("gourcetree", "Storage Device")
.AddVariant("gourcetree", "Windows C: Drive")  // NEW VARIANT
.AddVariant("gourcetree", "Performance Test")
.AddVariant("gourcetree", "Custom Builder");
