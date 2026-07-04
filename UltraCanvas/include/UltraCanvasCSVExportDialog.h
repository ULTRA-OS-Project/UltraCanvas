// include/UltraCanvasCSVExportDialog.h
// Modal "Text Export" dialog for saving a spreadsheet as CSV/TSV. Mirrors the
// familiar desktop spreadsheet export dialog: character set, field separator,
// text delimiter, quoting policy and line ending, with a live text preview of
// the generated file.
//
// The preview pane shows the exact CSV/TSV text produced for the current
// options (built through UltraCanvasSpreadsheet::ExportCSVToString), so what the
// user sees is what gets written.
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

class UltraCanvasCSVExportDialog : public UltraCanvasModalDialog {
public:
    UltraCanvasCSVExportDialog() = default;

    // Build the dialog for a given source spreadsheet. 'initial' seeds the
    // controls (e.g. a tab separator when the user picked a .tsv file).
    void Initialize(UltraCanvasSpreadsheet* source, const CSVExportOptions& initial);

    // Invoked with the chosen options when the user presses Export (OK).
    std::function<void(const CSVExportOptions&)> onAccept;

    // The options currently described by the controls.
    CSVExportOptions GetOptions() const { return BuildOptions(); }

private:
    void BuildLayout();
    void WireCallbacks();
    void ApplyOptionsToControls(const CSVExportOptions& opt);
    CSVExportOptions BuildOptions() const;
    void RefreshPreview();

    UltraCanvasSpreadsheet* source_ = nullptr;

    // Controls
    std::shared_ptr<UltraCanvasContainer> contentSection;
    std::shared_ptr<UltraCanvasContainer> buttonSection;

    std::shared_ptr<UltraCanvasDropdown>  encodingDropdown_;
    std::shared_ptr<UltraCanvasDropdown>  separatorDropdown_;
    std::shared_ptr<UltraCanvasTextInput> otherSepInput_;
    std::shared_ptr<UltraCanvasDropdown>  textDelimDropdown_;
    std::shared_ptr<UltraCanvasDropdown>  lineEndingDropdown_;
    std::shared_ptr<UltraCanvasCheckbox>  quoteAllCheck_;
    std::shared_ptr<UltraCanvasCheckbox>  bomCheck_;

    std::shared_ptr<UltraCanvasTextInput> previewText_;

    std::shared_ptr<UltraCanvasButton> okButton_;
    std::shared_ptr<UltraCanvasButton> cancelButton_;

    bool building_ = false;  // suppress preview refresh while populating controls
};

// Convenience factory: build and show the export dialog for a spreadsheet.
// 'onAccept' receives the chosen options (call SaveCSVWithOptions with them).
std::shared_ptr<UltraCanvasCSVExportDialog>
ShowCSVExportDialog(UltraCanvasSpreadsheet* source,
                    const CSVExportOptions& initial,
                    std::function<void(const CSVExportOptions&)> onAccept,
                    UltraCanvasWindowBase* parent = nullptr);

} // namespace UltraCanvas
