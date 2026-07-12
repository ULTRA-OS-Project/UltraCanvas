// include/UltraCanvasCSVImportDialog.h
// Modal "Text Import" dialog for CSV/TSV files. Mirrors the familiar desktop
// spreadsheet import dialog: character set, start row, separator options, text
// delimiter and number recognition, with a live preview of the parsed grid.
//
// The preview pane is itself an UltraCanvasSpreadsheet driven through
// LoadCSVWithOptions, so what the user sees in the preview is exactly what the
// import produces.
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasCSVImport.h"
#include <string>
#include <functional>
#include <memory>

namespace UltraCanvas {

class UltraCanvasCSVImportDialog : public UltraCanvasModalDialog {
public:
    UltraCanvasCSVImportDialog() = default;

    // Build the dialog for a specific file. The initial control state is taken
    // from auto-detection so the common case needs no manual tweaking.
    void Initialize(const std::string& filePath);

    // Invoked with the chosen options when the user presses Import (OK).
    std::function<void(const CSVImportOptions&)> onAccept;

    // The options currently described by the controls.
    CSVImportOptions GetOptions() const { return BuildOptions(); }

private:
    void BuildLayout();
    void WireCallbacks();
    void ApplyOptionsToControls(const CSVImportOptions& opt);
    CSVImportOptions BuildOptions() const;
    void RefreshPreview();

    std::shared_ptr<UltraCanvasCheckbox> MakeCheck(const std::string& id,
                                                   const std::string& label, bool checked,
                                                   int width = 160);

    std::string filePath_;

    // Controls
    std::shared_ptr<UltraCanvasContainer> contentSection;
    std::shared_ptr<UltraCanvasContainer> buttonSection;

    std::shared_ptr<UltraCanvasDropdown>  encodingDropdown_;
    std::shared_ptr<UltraCanvasTextInput> startRowInput_;

    std::shared_ptr<UltraCanvasCheckbox>  tabCheck_;
    std::shared_ptr<UltraCanvasCheckbox>  commaCheck_;
    std::shared_ptr<UltraCanvasCheckbox>  semicolonCheck_;
    std::shared_ptr<UltraCanvasCheckbox>  spaceCheck_;
    std::shared_ptr<UltraCanvasCheckbox>  otherCheck_;
    std::shared_ptr<UltraCanvasTextInput> otherInput_;
    std::shared_ptr<UltraCanvasCheckbox>  mergeCheck_;
    std::shared_ptr<UltraCanvasDropdown>  textDelimDropdown_;

    std::shared_ptr<UltraCanvasCheckbox>  quotedAsTextCheck_;
    std::shared_ptr<UltraCanvasCheckbox>  detectNumbersCheck_;

    std::shared_ptr<UltraCanvasSpreadsheet> previewSheet_;

    std::shared_ptr<UltraCanvasButton> okButton_;
    std::shared_ptr<UltraCanvasButton> cancelButton_;

    bool building_ = false;  // suppress preview refresh while populating controls
};

// Convenience factory: build, auto-detect and show the import dialog for a file.
// 'onAccept' receives the chosen options (call LoadCSVWithOptions with them).
std::shared_ptr<UltraCanvasCSVImportDialog>
ShowCSVImportDialog(const std::string& filePath,
                    std::function<void(const CSVImportOptions&)> onAccept,
                    UltraCanvasWindowBase* parent = nullptr);

} // namespace UltraCanvas
