// UltraCanvasToolbarExample.cpp
// Example implementation showing how to create and use toolbar templates
// Version: 1.0.0
// Last Modified: 2025-07-15
// Author: UltraCanvas Framework

#include "../include/UltraCanvasTemplate.h"
#include "../include/UltraCanvasWindow.h"
#include "../include/UltraCanvasApplication.h"
#include <iostream>

using namespace UltraCanvas;

// ===== EXAMPLE 1: SIMPLE TOOLBAR WITH BUILDER =====
std::unique_ptr<UltraCanvasTemplate> CreateSimpleToolbar() {
    return UltraCanvasTemplateBuilder("SimpleToolbar")
        // Configure toolbar dimensions and appearance
        .SetDimensions(TemplateDimensions::Fixed(500, 36))
        .SetAppearance([]() {
            TemplateAppearance app;
            app.backgroundColor = Color(245, 245, 245);
            app.borderColor = Color(200, 200, 200);
            app.borderWidth = 1.0f;
            app.cornerRadius = 4.0f;
            app.SetPadding(6.0f);
            return app;
        }())
        
        // Set horizontal flow layout with spacing
        .SetPlacementRule(TemplatePlacementRule::Flow(LayoutDirection::Horizontal, 6.0f))
        
        // Add drag handle on the left
        .SetDragHandle(TemplateDragHandle::Left(12.0f))
        
        // Add toolbar elements
        .AddButton("btn_new", "New", "icons/new.png", []() {
            std::cout << "New file clicked!" << std::endl;
        })
        .AddButton("btn_open", "Open", "icons/open.png", []() {
            std::cout << "Open file clicked!" << std::endl;
        })
        .AddButton("btn_save", "Save", "icons/save.png", []() {
            std::cout << "Save file clicked!" << std::endl;
        })
        .AddSeparator(true)  // Vertical separator
        .AddButton("btn_cut", "", "icons/cut.png", []() {
            std::cout << "Cut clicked!" << std::endl;
        })
        .AddButton("btn_copy", "", "icons/copy.png", []() {
            std::cout << "Copy clicked!" << std::endl;
        })
        .AddButton("btn_paste", "", "icons/paste.png", []() {
            std::cout << "Paste clicked!" << std::endl;
        })
        .AddSeparator(true)
        .AddDropDown("dd_zoom", {"50%", "75%", "100%", "125%", "150%", "200%"}, [](const std::string& selected) {
            std::cout << "Zoom level changed to: " << selected << std::endl;
        })
        .AddSpacer(16.0f)  // Flexible spacer
        .AddLabel("lbl_status", "Ready")
        .Build();
}

// ===== EXAMPLE 2: ADVANCED TOOLBAR WITH CUSTOM ELEMENTS =====
std::unique_ptr<UltraCanvasTemplate> CreateAdvancedToolbar() {
    auto toolbar = std::make_unique<UltraCanvasTemplate>("AdvancedToolbar", 0, 0, 0, 600, 48);
    
    // Configure advanced appearance
    TemplateAppearance appearance;
    appearance.backgroundColor = Color(250, 250, 250);
    appearance.borderColor = Color(180, 180, 180);
    appearance.borderWidth = 1.0f;
    appearance.cornerRadius = 6.0f;
    appearance.SetPadding(8.0f);
    appearance.hasShadow = true;
    appearance.shadowColor = Color(0, 0, 0, 32);
    appearance.shadowOffset = Point2D(0, 2);
    toolbar->SetAppearance(appearance);
    
    // Set dimensions with auto-height
    TemplateDimensions dims;
    dims.widthMode = TemplateSizeMode::Fixed;
    dims.heightMode = TemplateSizeMode::Auto;
    dims.fixedWidth = 600.0f;
    dims.minHeight = 48.0f;
    toolbar->SetDimensions(dims);
    
    // Configure grid layout
    auto placement = TemplatePlacementRule::Grid(8, 2, 4.0f);
    placement.alignment = LayoutAlignment::Start;
    placement.crossAlignment = LayoutAlignment::Center;
    toolbar->SetPlacementRule(placement);
    
    // Add drag handle
    toolbar->SetDragHandle(TemplateDragHandle::Left(10.0f));
    
    // Add elements with detailed configuration
    toolbar->AddElement(TemplateElementDescriptor::Button("file_new", "New Document", "icons/document_new.png", []() {
        std::cout << "Creating new document..." << std::endl;
    }));
    
    toolbar->AddElement(TemplateElementDescriptor::Button("file_open", "Open", "icons/folder_open.png", []() {
        std::cout << "Opening file dialog..." << std::endl;
    }));
    
    toolbar->AddElement(TemplateElementDescriptor::Button("file_save", "Save", "icons/disk.png", []() {
        std::cout << "Saving current document..." << std::endl;
    }));
    
    toolbar->AddElement(TemplateElementDescriptor::Separator("sep1", true));
    
    // Add formatting tools
    toolbar->AddElement(TemplateElementDescriptor::Button("format_bold", "B", "", []() {
        std::cout << "Toggle bold formatting" << std::endl;
    }));
    
    toolbar->AddElement(TemplateElementDescriptor::Button("format_italic", "I", "", []() {
        std::cout << "Toggle italic formatting" << std::endl;
    }));
    
    toolbar->AddElement(TemplateElementDescriptor::Button("format_underline", "U", "", []() {
        std::cout << "Toggle underline formatting" << std::endl;
    }));
    
    toolbar->AddElement(TemplateElementDescriptor::Separator("sep2", true));
    
    // Add alignment tools in second row
    toolbar->AddElement(TemplateElementDescriptor::Button("align_left", "", "icons/text_align_left.png", []() {
        std::cout << "Align text left" << std::endl;
    }));
    
    toolbar->AddElement(TemplateElementDescriptor::Button("align_center", "", "icons/text_align_center.png", []() {
        std::cout << "Align text center" << std::endl;
    }));
    
    toolbar->AddElement(TemplateElementDescriptor::Button("align_right", "", "icons/text_align_right.png", []() {
        std::cout << "Align text right" << std::endl;
    }));
    
    toolbar->AddElement(TemplateElementDescriptor::Button("align_justify", "", "icons/text_align_justify.png", []() {
        std::cout << "Justify text" << std::endl;
    }));
    
    return toolbar;
}

// ===== EXAMPLE 3: VERTICAL PANEL TEMPLATE =====
std::unique_ptr<UltraCanvasTemplate> CreateToolPanel() {
    return UltraCanvasTemplateBuilder("ToolPanel")
        .SetDimensions(TemplateDimensions::Fixed(200, 400))
        .SetAppearance([]() {
            TemplateAppearance app;
            app.backgroundColor = Color(235, 235, 235);
            app.borderColor = Color(170, 170, 170);
            app.borderWidth = 1.0f;
            app.SetPadding(8.0f);
            return app;
        }())
        
        // Vertical stack layout
        .SetPlacementRule(TemplatePlacementRule::Stack(LayoutDirection::Vertical, 8.0f))
        
        // Top drag handle
        .SetDragHandle(TemplateDragHandle::Top(10.0f))
        
        // Enable vertical scrolling
        .SetScrollSettings([]() {
            TemplateScrollSettings scroll;
            scroll.vertical = TemplateScrollMode::Auto;
            scroll.horizontal = TemplateScrollMode::Off;
            return scroll;
        }())
        
        // Add tool categories
        .AddLabel("lbl_drawing", "Drawing Tools")
        .AddButton("tool_select", "Select", "icons/cursor.png", []() {
            std::cout << "Select tool activated" << std::endl;
        })
        .AddButton("tool_pen", "Pen", "icons/pen.png", []() {
            std::cout << "Pen tool activated" << std::endl;
        })
        .AddButton("tool_brush", "Brush", "icons/brush.png", []() {
            std::cout << "Brush tool activated" << std::endl;
        })
        .AddButton("tool_eraser", "Eraser", "icons/eraser.png", []() {
            std::cout << "Eraser tool activated" << std::endl;
        })
        .AddSeparator(false)  // Horizontal separator
        
        .AddLabel("lbl_shapes", "Shape Tools")
        .AddButton("shape_rect", "Rectangle", "icons/rectangle.png", []() {
            std::cout << "Rectangle tool activated" << std::endl;
        })
        .AddButton("shape_circle", "Circle", "icons/circle.png", []() {
            std::cout << "Circle tool activated" << std::endl;
        })
        .AddButton("shape_line", "Line", "icons/line.png", []() {
            std::cout << "Line tool activated" << std::endl;
        })
        .AddSeparator(false)
        
        .AddLabel("lbl_colors", "Colors")
        .AddButton("color_black", "", "", []() {
            std::cout << "Black color selected" << std::endl;
        })
        .AddButton("color_red", "", "", []() {
            std::cout << "Red color selected" << std::endl;
        })
        .AddButton("color_blue", "", "", []() {
            std::cout << "Blue color selected" << std::endl;
        })
        .Build();
}

// ===== EXAMPLE 4: STATUS BAR TEMPLATE =====
std::unique_ptr<UltraCanvasTemplate> CreateStatusBar() {
    return UltraCanvasTemplateBuilder("StatusBar")
        .SetDimensions(TemplateDimensions::Percent(100.0f, 28.0f))
        .SetAppearance([]() {
            TemplateAppearance app;
            app.backgroundColor = Color(240, 240, 240);
            app.borderColor = Color(190, 190, 190);
            app.borderWidth = 1.0f;
            app.SetPadding(6.0f);
            return app;
        }())
        
        // Horizontal flow layout
        .SetPlacementRule(TemplatePlacementRule::Flow(LayoutDirection::Horizontal, 12.0f))
        
        // Status bar elements
        .AddLabel("status_main", "Ready")
        .AddSeparator(true)
        .AddLabel("status_line", "Line: 1")
        .AddLabel("status_col", "Col: 1")
        .AddSeparator(true)
        .AddLabel("status_zoom", "100%")
        .AddSeparator(true)
        .AddLabel("status_encoding", "UTF-8")
        .AddSpacer(20.0f)  // Push remaining items to the right
        .AddLabel("status_time", "12:00")
        .Build();
}

// ===== EXAMPLE 5: RIBBON TOOLBAR (MULTI-ROW) =====
std::unique_ptr<UltraCanvasTemplate> CreateRibbonToolbar() {
    auto ribbon = std::make_unique<UltraCanvasTemplate>("RibbonToolbar", 0, 0, 0, 800, 80);
    
    // Configure ribbon appearance
    TemplateAppearance appearance;
    appearance.backgroundColor = Color(248, 248, 248);
    appearance.borderColor = Color(180, 180, 180);
    appearance.borderWidth = 1.0f;
    appearance.SetPadding(10.0f);
    ribbon->SetAppearance(appearance);
    
    // Set fixed dimensions
    ribbon->SetDimensions(TemplateDimensions::Fixed(800, 80));
    
    // Configure 3-row grid layout
    auto placement = TemplatePlacementRule::Grid(10, 3, 6.0f);
    placement.alignment = LayoutAlignment::Start;
    placement.crossAlignment = LayoutAlignment::Center;
    ribbon->SetPlacementRule(placement);
    
    // Add drag handle
    ribbon->SetDragHandle(TemplateDragHandle::Left(12.0f));
    
    // Row 1: File operations
    ribbon->AddElement(TemplateElementDescriptor::Label("lbl_file", "File"));
    ribbon->AddElement(TemplateElementDescriptor::Button("file_new", "New", "icons/new.png"));
    ribbon->AddElement(TemplateElementDescriptor::Button("file_open", "Open", "icons/open.png"));
    ribbon->AddElement(TemplateElementDescriptor::Button("file_save", "Save", "icons/save.png"));
    ribbon->AddElement(TemplateElementDescriptor::Button("file_print", "Print", "icons/print.png"));
    ribbon->AddElement(TemplateElementDescriptor::Separator("sep_file", true));
    
    // Row 2: Edit operations
    ribbon->AddElement(TemplateElementDescriptor::Label("lbl_edit", "Edit"));
    ribbon->AddElement(TemplateElementDescriptor::Button("edit_cut", "Cut", "icons/cut.png"));
    ribbon->AddElement(TemplateElementDescriptor::Button("edit_copy", "Copy", "icons/copy.png"));
    ribbon->AddElement(TemplateElementDescriptor::Button("edit_paste", "Paste", "icons/paste.png"));
    ribbon->AddElement(TemplateElementDescriptor::Button("edit_undo", "Undo", "icons/undo.png"));
    ribbon->AddElement(TemplateElementDescriptor::Separator("sep_edit", true));
    
    // Row 3: Format operations
    ribbon->AddElement(TemplateElementDescriptor::Label("lbl_format", "Format"));
    ribbon->AddElement(TemplateElementDescriptor::Button("format_bold", "Bold", "icons/bold.png"));
    ribbon->AddElement(TemplateElementDescriptor::Button("format_italic", "Italic", "icons/italic.png"));
    ribbon->AddElement(TemplateElementDescriptor::DropDown("format_font", {"Arial", "Times", "Helvetica", "Courier"}));
    ribbon->AddElement(TemplateElementDescriptor::DropDown("format_size", {"8", "10", "12", "14", "16", "18", "24"}));
    
    return ribbon;
}

// ===== EXAMPLE 6: FLOATING TOOL PALETTE =====
std::unique_ptr<UltraCanvasTemplate> CreateFloatingPalette() {
    return UltraCanvasTemplateBuilder("FloatingPalette")
        .SetDimensions(TemplateDimensions::Auto())
        .SetAppearance([]() {
            TemplateAppearance app;
            app.backgroundColor = Color(220, 220, 220);
            app.borderColor = Color(160, 160, 160);
            app.borderWidth = 2.0f;
            app.cornerRadius = 8.0f;
            app.SetPadding(6.0f);
            app.hasShadow = true;
            app.shadowColor = Color(0, 0, 0, 80);
            app.shadowOffset = Point2D(3, 3);
            return app;
        }())
        
        // 3x3 grid layout
        .SetPlacementRule(TemplatePlacementRule::Grid(3, 3, 4.0f))
        
        // All-sides drag handle
        .SetDragHandle(TemplateDragHandle::Top(8.0f))
        
        // Add color palette buttons
        .AddButton("color1", "", "", []() { std::cout << "Color 1 selected" << std::endl; })
        .AddButton("color2", "", "", []() { std::cout << "Color 2 selected" << std::endl; })
        .AddButton("color3", "", "", []() { std::cout << "Color 3 selected" << std::endl; })
        .AddButton("color4", "", "", []() { std::cout << "Color 4 selected" << std::endl; })
        .AddButton("color5", "", "", []() { std::cout << "Color 5 selected" << std::endl; })
        .AddButton("color6", "", "", []() { std::cout << "Color 6 selected" << std::endl; })
        .AddButton("color7", "", "", []() { std::cout << "Color 7 selected" << std::endl; })
        .AddButton("color8", "", "", []() { std::cout << "Color 8 selected" << std::endl; })
        .AddButton("color9", "", "", []() { std::cout << "Color 9 selected" << std::endl; })
        .Build();
}

// ===== EXAMPLE USAGE IN APPLICATION =====
class ToolbarExampleApplication : public UltraCanvasApplication {
private:
    std::unique_ptr<UltraCanvasWindow> mainWindow;
    std::unique_ptr<UltraCanvasTemplate> mainToolbar;
    std::unique_ptr<UltraCanvasTemplate> toolPanel;
    std::unique_ptr<UltraCanvasTemplate> statusBar;
    std::unique_ptr<UltraCanvasTemplate> floatingPalette;
    
public:
    bool Initialize() override {
        // Create main window
        mainWindow = std::make_unique<UltraCanvasWindow>("Toolbar Example", 100, 100, 1200, 800);
        
        // Create templates
        mainToolbar = CreateSimpleToolbar();
        toolPanel = CreateToolPanel();
        statusBar = CreateStatusBar();
        floatingPalette = CreateFloatingPalette();
        
        // Position templates in window
        PositionTemplates();
        
        // Add templates to window
        mainWindow->AddChild(mainToolbar);
        mainWindow->AddChild(toolPanel);
        mainWindow->AddChild(statusBar);
        mainWindow->AddChild(floatingPalette);
        
        // Show window
        mainWindow->Show();
        
        return true;
    }
    
    void PositionTemplates() {
        if (!mainWindow) return;
        
        Rect2D windowRect = Rect2D(0, 0, mainWindow->GetWidth(), mainWindow->GetHeight());
        
        // Position main toolbar at top
        mainToolbar->SetX(0);
        mainToolbar->SetY(0);
        mainToolbar->ApplyToContainer(Rect2D(0, 0, windowRect.width, 40));
        
        // Position tool panel on left
        toolPanel->SetX(0);
        toolPanel->SetY(40);
        toolPanel->ApplyToContainer(Rect2D(0, 40, 200, windowRect.height - 80));
        
        // Position status bar at bottom
        statusBar->SetX(0);
        statusBar->SetY(windowRect.height - 28);
        statusBar->ApplyToContainer(Rect2D(0, windowRect.height - 28, windowRect.width, 28));
        
        // Position floating palette
        floatingPalette->SetX(300);
        floatingPalette->SetY(100);
        floatingPalette->FitToContent();
    }
    
    void Run() override {
        while (IsRunning()) {
            // Handle events
            ProcessEvents();
            
            // Update templates if needed
            UpdateTemplates();
            
            // Render
            mainWindow->Render();
            
            // Sleep to maintain frame rate
            Sleep(16); // ~60 FPS
        }
    }
    
    void UpdateTemplates() {
        // Update status bar with current time
        auto timeLabel = statusBar->GetElementAs<UltraCanvasLabel>("status_time");
        if (timeLabel) {
            // Get current time and update label
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            auto tm = *std::localtime(&time_t);
            
            char timeStr[32];
            std::strftime(timeStr, sizeof(timeStr), "%H:%M", &tm);
            timeLabel->SetText(timeStr);
        }
        
        // Example: Update cursor position in status bar
        auto lineLabel = statusBar->GetElementAs<UltraCanvasLabel>("status_line");
        auto colLabel = statusBar->GetElementAs<UltraCanvasLabel>("status_col");
        if (lineLabel && colLabel) {
            // These would be updated based on actual cursor position
            lineLabel->SetText("Line: " + std::to_string(GetCurrentLine()));
            colLabel->SetText("Col: " + std::to_string(GetCurrentColumn()));
        }
    }
    
private:
    int GetCurrentLine() { return 42; }  // Example implementation
    int GetCurrentColumn() { return 15; } // Example implementation
};

// ===== MAIN FUNCTION =====
int main() {
    ToolbarExampleApplication app;
    
    if (!app.Initialize()) {
        std::cerr << "Failed to initialize application!" << std::endl;
        return -1;
    }
    
    std::cout << "Starting UltraCanvas Template Example..." << std::endl;
    std::cout << "Templates created:" << std::endl;
    std::cout << "- Main Toolbar (horizontal flow)" << std::endl;
    std::cout << "- Tool Panel (vertical stack)" << std::endl;
    std::cout << "- Status Bar (horizontal with spacers)" << std::endl;
    std::cout << "- Floating Palette (3x3 grid)" << std::endl;
    
    app.Run();
    
    return 0;
}

// ===== TEMPLATE CUSTOMIZATION EXAMPLES =====
namespace TemplateCustomization {

// Example: Create a custom element factory
void RegisterCustomElementFactory(UltraCanvasTemplate* template_) {
    template_->RegisterElementFactory("ColorButton", [](const TemplateElementDescriptor& desc) -> std::shared_ptr<UltraCanvasElement> {
        auto button = std::make_shared<UltraCanvasButton>(desc.identifier, 0, 0, 0, 24, 24);
        
        // Set button background color from properties
        auto colorIt = desc.properties.find("color");
        if (colorIt != desc.properties.end()) {
            // Parse color string (e.g., "255,0,0" for red)
            // and set button background color
            button->SetBackgroundColor(ParseColorFromString(colorIt->second));
        }
        
        button->SetText(""); // No text for color buttons
        if (desc.onClickCallback) {
            button->SetClickCallback(desc.onClickCallback);
        }
        
        return button;
    });
}

// Example: Dynamic template modification
void ModifyToolbarAtRuntime(UltraCanvasTemplate* toolbar) {
    // Add new element at runtime
    toolbar->AddElement(TemplateElementDescriptor::Button("btn_new_feature", "New Feature", "icons/star.png", []() {
        std::cout << "New feature activated!" << std::endl;
    }));
    
    // Remove an element
    toolbar->RemoveElement("btn_old_feature");
    
    // Rebuild template with new elements
    toolbar->RebuildTemplate();
}

// Example: Template persistence
void SaveTemplateConfiguration(const UltraCanvasTemplate* template_, const std::string& filename) {
    // This would serialize the template configuration to a file
    // Implementation would depend on chosen serialization format (JSON, XML, binary, etc.)
    std::cout << "Saving template configuration to: " << filename << std::endl;
}

void LoadTemplateConfiguration(UltraCanvasTemplate* template_, const std::string& filename) {
    // This would load and apply template configuration from a file
    std::cout << "Loading template configuration from: " << filename << std::endl;
}

Color ParseColorFromString(const std::string& colorStr) {
    // Simple color parsing example (R,G,B format)
    size_t comma1 = colorStr.find(',');
    size_t comma2 = colorStr.find(',', comma1 + 1);
    
    if (comma1 != std::string::npos && comma2 != std::string::npos) {
        int r = std::stoi(colorStr.substr(0, comma1));
        int g = std::stoi(colorStr.substr(comma1 + 1, comma2 - comma1 - 1));
        int b = std::stoi(colorStr.substr(comma2 + 1));
        return Color(r, g, b);
    }
    
    return Color::Black;
}

} // namespace TemplateCustomization