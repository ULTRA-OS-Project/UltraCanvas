// plugins/Diagrams/UltraCanvasFlowChartPalette.cpp
// Shape palette for FlowChart diagram creation
// Version: 1.0.0

#include "Plugins/Diagrams/UltraCanvasFlowChartPalette.h"

namespace UltraCanvas {

UltraCanvasFlowChartPalette::UltraCanvasFlowChartPalette(const std::string& id,
                                                         int x, int y, int width, int height)
    : UltraCanvasContainer(id, x, y, width, height) {
    SetBackgroundColor(backgroundColor);
}

void UltraCanvasFlowChartPalette::SetTargetDiagram(std::shared_ptr<UltraCanvasFlowChart> diagram) {
    targetDiagram = diagram;
}

void UltraCanvasFlowChartPalette::BuildPalette() {
    shapeButtons.clear();
    ClearChildren();
    
    auto title = std::make_shared<UltraCanvasLabel>("paletteTitle", 10, 10, 
                                                    finalBounds.width - 20, 25);
    title->SetText("Shapes");
    title->SetFont("Arial", 14.0f, FontWeight::Bold);
    title->SetTextColor(Color(40, 40, 40, 255));
    AddChild(title);
    
    int yPos = 45;
    int row = 0;
    
    auto basicLabel = std::make_shared<UltraCanvasLabel>("basicLabel", 10, yPos, 
                                                           finalBounds.width - 20, 20);
    basicLabel->SetText("Basic");
    basicLabel->SetFontSize(11.0f);
    basicLabel->SetTextColor(Color(100, 100, 100, 255));
    AddChild(basicLabel);
    yPos += 25;
    
    CreateShapeButton(FlowChartShape::Rectangle, "Rectangle", row++);
    CreateShapeButton(FlowChartShape::RoundedRectangle, "Rounded", row++);
    CreateShapeButton(FlowChartShape::Oval, "Oval", row++);
    CreateShapeButton(FlowChartShape::Circle, "Circle", row++);
    CreateShapeButton(FlowChartShape::Diamond, "Diamond", row++);
    
    yPos += row * 50 + 15;
    row = 0;
    
    auto flowLabel = std::make_shared<UltraCanvasLabel>("flowLabel", 10, yPos, 
                                                          finalBounds.width - 20, 20);
    flowLabel->SetText("Flowchart");
    flowLabel->SetFontSize(11.0f);
    flowLabel->SetTextColor(Color(100, 100, 100, 255));
    AddChild(flowLabel);
    yPos += 25;
    
    CreateShapeButton(FlowChartShape::Process, "Process", row++);
    CreateShapeButton(FlowChartShape::Decision, "Decision", row++);
    CreateShapeButton(FlowChartShape::Parallelogram, "Parallelogram", row++);
    CreateShapeButton(FlowChartShape::Hexagon, "Hexagon", row++);
    CreateShapeButton(FlowChartShape::Triangle, "Triangle", row++);
    CreateShapeButton(FlowChartShape::Document, "Document", row++);
    CreateShapeButton(FlowChartShape::Database, "Database", row++);
    CreateShapeButton(FlowChartShape::ManualInput, "Manual Input", row++);
}

void UltraCanvasFlowChartPalette::CreateShapeButton(FlowChartShape shape, const std::string& label, int row) {
    int buttonWidth = finalBounds.width - 20;
    int buttonHeight = 45;
    int yPos = 170 + row * 50;
    
    auto button = std::make_shared<UltraCanvasButton>(
        "shape_" + std::to_string(static_cast<int>(shape)),
        10, yPos, buttonWidth, buttonHeight
    );
    
    button->SetText(label);
    button->SetColors(Color(255, 255, 255, 255), Color(240, 245, 250, 255));
    button->SetBorder(1.0f, Color(200, 200, 200, 255));
    button->SetCornerRadius(4.0f);
    button->SetTextAlign(TextAlignment::Left);
    
    button->SetOnClick([this, shape, button]() {
        SelectShape(shape, button);
    });
    
    AddChild(button);
    shapeButtons.push_back(button);
    
    if (shape == FlowChartShape::Rectangle) {
        currentSelectedButton = button;
        button->SetColors(Color(220, 235, 255, 255), Color(220, 235, 255, 255));
    }
}

void UltraCanvasFlowChartPalette::SelectShape(FlowChartShape shape, std::shared_ptr<UltraCanvasButton> button) {
    currentSelectedShape = shape;
    
    if (currentSelectedButton) {
        currentSelectedButton->SetColors(Color(255, 255, 255, 255), Color(240, 245, 250, 255));
    }
    
    currentSelectedButton = button;
    button->SetColors(Color(220, 235, 255, 255), Color(220, 235, 255, 255));
    
    if (targetDiagram) {
        targetDiagram->SetPendingNodeShape(shape);
    }
    
    if (onShapeSelected) {
        onShapeSelected(shape);
    }
}

} // namespace UltraCanvas
