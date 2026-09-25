// Apps/UltraFiler/UltraFilerFindTextDialog.h
// "Extras > Find text" — the dialog that asks what to look for inside the
// files of the shown folder and its sub folders: the text, which files to
// read (a file-name pattern such as "*.cpp; *.h") and whether the case of
// letters has to match. Also the pattern helpers the search worker uses.
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCheckbox.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// What one Find text search looks for.
struct FilerFindTextOptions {
    std::string text;
    // File-name patterns separated by ';', ',' or blanks - "*" is any run of
    // characters, "?" one character. Empty: every file.
    std::string filePattern;
    bool matchCase = false;
};

// The single patterns of `filePattern` ("*.cpp; *.h" -> {"*.cpp", "*.h"}).
std::vector<std::string> SplitFilePatterns(const std::string& filePattern);

// Whether the file name `name` matches one of `patterns` (from
// SplitFilePatterns); an empty list matches every name. The comparison
// ignores the case of ASCII letters, so "*.JPG" and "*.jpg" are the same.
bool FileNameMatchesPatterns(const std::string& name,
                             const std::vector<std::string>& patterns);

class UltraFilerFindTextDialog : public UltraCanvasModalDialog {
public:
    UltraFilerFindTextDialog() = default;

    // Opens with `initial` filled in (the previous search's options).
    void Initialize(const FilerFindTextOptions& initial);

    // Invoked on Find (button or Enter) with a non-empty text.
    std::function<void(const FilerFindTextOptions&)> onFind;

protected:
    // The text field, not the Find button: the user types first.
    void FocusInitialElement() override;

private:
    // Hands the options to onFind and closes; ignored while the text is empty.
    void Accept();

    std::shared_ptr<UltraCanvasTextInput> textInput_;
    std::shared_ptr<UltraCanvasTextInput> patternInput_;
    std::shared_ptr<UltraCanvasCheckbox>  matchCaseCheck_;
};

// Build and show the dialog; onFind fires only on Find.
std::shared_ptr<UltraFilerFindTextDialog> ShowFindTextDialog(
    const FilerFindTextOptions& initial,
    std::function<void(const FilerFindTextOptions&)> onFind,
    UltraCanvasWindowBase* parent);

}  // namespace UltraCanvas
