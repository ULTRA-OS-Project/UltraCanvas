// include/Plugins/Diagrams/UltraCanvasFlowChartPalette.h
// Shape palette for FlowChart diagram creation
// Version: 1.0.0

#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "Plugins/Diagrams/UltraCanvasFlowChart.h"
#include <memory>
#include <vector>
#include <functional>

namespace UltraCanvas {

class UltraCanvasFlowChartPalette : public UltraCanvasContainer {
public:
    struct ShapeCategoryInfo {
        std::string name;
        std::vector<FlowChartShape> shapes;
    };

private:
    std::shared_ptr<UltraCanvasFlowChart> targetDiagram;
    std::vector<std::shared_ptr<UltraCanvasButton>> shapeButtons;
    Color backgroundColor = Color(250, 250, 250, 255);
    Color selectedColor = Color(220, 235, 255, 255);
    Color hoverColor = Color(240, 245, 250, 255);
    
    FlowChartShape currentSelectedShape = FlowChartShape::Rectangle;
    std::shared_ptr<UltraCanvasButton> currentSelectedButton;

public:
    UltraCanvasFlowChartPalette(const std::string& id, int x, int y, int width, int height);
    
    void SetTargetDiagram(std::shared_ptr<UltraCanvasFlowChart> diagram);
    void BuildPalette();
    
    std::function<void(FlowChartShape)> onShapeSelected;

private:
    void CreateShapeButton(FlowChartShape shape, const std::string& label, int row);
    void SelectShape(FlowChartShape shape, std::shared_ptr<UltraCanvasButton> button);
};

inline std::shared_ptr<UltraCanvasFlowChartPalette> CreateFlowChartPalette(
        const std::string& id, int x, int y, int width, int height) {
    return std::make_shared<UltraCanvasFlowChartPalette>(id, x, y, width, height);
}

} // namespace UltraCanvas
