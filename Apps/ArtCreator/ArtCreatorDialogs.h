// Apps/ArtCreator/ArtCreatorDialogs.h
// ArtCreator's small parameter windows: New Drawing (page size, unit,
// orientation), the Text tool's entry, and Document Setup. Each is an
// UltraCanvasWindow built out of framework elements on an
// UltraCanvasFormLayout grid and reports through callbacks.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasSpinner.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {

// ===== NEW DRAWING / DOCUMENT SETUP =====
// The page in the unit the user chose; the document stores points.
struct ArtCreatorPageResult {
    double width = 210.0;         // in `unit`
    double height = 297.0;
    int unit = 0;                 // 0 mm, 1 cm, 2 in, 3 pt, 4 px (96/in)
    bool landscape = false;
    double PointsPerUnit() const;
    double WidthPoints() const { return width * PointsPerUnit(); }
    double HeightPoints() const { return height * PointsPerUnit(); }
    static const char* UnitSymbol(int unit);
};

class ArtCreatorPageDialog : public UltraCanvasWindow {
public:
    // `setup` true: Document Setup on an existing drawing (the button reads
    // Apply); false: New Drawing (Create).
    ArtCreatorPageDialog(const ArtCreatorPageResult& initial, bool setup);
    std::function<void(const ArtCreatorPageResult&)> onAccept;
private:
    void ApplyPreset(int index);
    void SyncOrientation();
    bool syncing = false;
    int lastUnit = 0;
    std::shared_ptr<UltraCanvasDropdown> presetDrop, unitDrop, orientationDrop;
    std::shared_ptr<UltraCanvasSpinner> widthSpin, heightSpin;
};

// ===== TEXT =====
struct ArtCreatorTextResult {
    std::string text;
    std::string font = "Sans";
    int size = 24;
    bool bold = false;
    bool italic = false;
};

class ArtCreatorTextDialog : public UltraCanvasWindow {
public:
    explicit ArtCreatorTextDialog(const ArtCreatorTextResult& initial);
    std::function<void(const ArtCreatorTextResult&)> onAccept;
private:
    std::shared_ptr<UltraCanvasTextInput> textInput;
    std::shared_ptr<UltraCanvasDropdown> fontDrop;
    std::shared_ptr<UltraCanvasSpinner> sizeSpin;
    std::shared_ptr<UltraCanvasCheckbox> boldBox, italicBox;
};

// ===== SHARED BUILDING BLOCKS =====
namespace ArtCreatorDialogParts {
    std::shared_ptr<UltraCanvasContainer> FormGrid(const std::string& id);
    std::shared_ptr<UltraCanvasLabel> FormRow(const std::shared_ptr<UltraCanvasContainer>& grid,
                                              const std::string& id, const std::string& label,
                                              std::shared_ptr<UltraCanvasUIElement> widget);
    void FormWideRow(const std::shared_ptr<UltraCanvasContainer>& grid, std::shared_ptr<UltraCanvasUIElement> element);
    void SizeButtonToText(const std::shared_ptr<UltraCanvasButton>& button);
    std::shared_ptr<UltraCanvasContainer> ButtonRow(const std::string& id, UltraCanvasWindow* win,
                                                    const std::function<void()>& onOk, const std::string& okLabel = "OK");
}

} // namespace UltraCanvas
