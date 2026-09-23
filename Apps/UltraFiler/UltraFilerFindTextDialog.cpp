// Apps/UltraFiler/UltraFilerFindTextDialog.cpp
// Implementation of the Find text dialog and its file-name patterns.
// Construction follows UltraFilerRunWindowsDialog (flex-column content,
// custom Find / Cancel buttons).
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraFilerFindTextDialog.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

namespace {

char FoldPatternChar(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// "*" and "?" wildcard match, ASCII case folded. Iterative with a single
// backtrack point, so a pattern of many stars cannot go exponential.
bool WildcardMatch(const std::string& pattern, const std::string& name) {
    size_t p = 0, n = 0;
    size_t starP = std::string::npos, starN = 0;
    while (n < name.size()) {
        if (p < pattern.size() && pattern[p] == '*') {
            starP = p++;
            starN = n;
        } else if (p < pattern.size() &&
                   (pattern[p] == '?' ||
                    FoldPatternChar(pattern[p]) == FoldPatternChar(name[n]))) {
            ++p;
            ++n;
        } else if (starP != std::string::npos) {
            // Let the last star swallow one more character and retry.
            p = starP + 1;
            n = ++starN;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

}  // namespace

std::vector<std::string> SplitFilePatterns(const std::string& filePattern) {
    std::vector<std::string> patterns;
    std::string current;
    for (char c : filePattern) {
        if (c == ';' || c == ',' || c == ' ' || c == '\t') {
            if (!current.empty()) patterns.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) patterns.push_back(current);
    return patterns;
}

bool FileNameMatchesPatterns(const std::string& name,
                             const std::vector<std::string>& patterns) {
    if (patterns.empty()) return true;
    for (const std::string& pattern : patterns)
        if (WildcardMatch(pattern, name)) return true;
    return false;
}

void UltraFilerFindTextDialog::Initialize(const FilerFindTextOptions& initial) {
    DialogConfig cfg;
    cfg.title = "Find text";
    cfg.width = 460;
    cfg.height = 270;
    cfg.resizable = false;
    cfg.buttons = DialogButtons::NoButtons;
    // Custom type: skip the built-in icon/message/footer layout so its
    // grow-section can't eat the space our own controls need.
    cfg.dialogType = DialogType::Custom;
    CreateDialog(cfg);

    layout.SetFlexColumn();
    layout.SetFlexGap(10);
    SetPadding(16);

    auto content = std::make_shared<UltraCanvasContainer>(
        "uf-findtext-content", 0, 0, 428, 176);
    content->layout.SetFlexColumn();
    content->layout.SetFlexGap(6);

    content->AddChild(std::make_shared<UltraCanvasLabel>(
        "uf-findtext-text-label", 0, 0, 428.0f, 20,
        "Find files in this folder and its sub folders that contain:"));
    textInput_ = std::make_shared<UltraCanvasTextInput>(
        "uf-findtext-text", 0, 0, 428.0f, 30.0f);
    textInput_->SetText(initial.text);
    textInput_->onEnterPressed = [this](const std::string&) {
        Accept();
        return true;
    };
    content->AddChild(textInput_);

    content->AddChild(std::make_shared<UltraCanvasLabel>(
        "uf-findtext-pattern-label", 0, 0, 428.0f, 20,
        "In files named:"));
    patternInput_ = std::make_shared<UltraCanvasTextInput>(
        "uf-findtext-pattern", 0, 0, 428.0f, 30.0f);
    patternInput_->SetPlaceholder("e.g. *.cpp; *.h  (empty: all files)");
    patternInput_->SetText(initial.filePattern);
    patternInput_->onEnterPressed = [this](const std::string&) {
        Accept();
        return true;
    };
    content->AddChild(patternInput_);

    matchCaseCheck_ = std::make_shared<UltraCanvasCheckbox>(
        "uf-findtext-matchcase", 0, 0, 428.0f, 24.0f, "Match case");
    matchCaseCheck_->SetChecked(initial.matchCase);
    content->AddChild(matchCaseCheck_);

    AddChild(content);

    // Buttons row (custom, so Find can refuse an empty text).
    auto buttons = std::make_shared<UltraCanvasContainer>(
        "uf-findtext-buttons", 0, 0, 428, 34);
    buttons->layout.SetFlexRow();
    buttons->layout.SetFlexGap(8);
    auto findBtn = std::make_shared<UltraCanvasButton>(
        "uf-findtext-find", 0, 0, 100, 30);
    findBtn->SetText("Find");
    findBtn->onClick = [this]() { Accept(); };
    auto cancelBtn = std::make_shared<UltraCanvasButton>(
        "uf-findtext-cancel", 0, 0, 100, 30);
    cancelBtn->SetText("Cancel");
    cancelBtn->onClick = [this]() { CloseDialog(DialogResult::Cancel); };
    buttons->AddChild(findBtn);
    buttons->AddChild(cancelBtn);
    AddChild(buttons);
}

void UltraFilerFindTextDialog::FocusInitialElement() {
    if (textInput_) {
        SetFocusedElement(textInput_.get());
        return;
    }
    UltraCanvasModalDialog::FocusInitialElement();
}

void UltraFilerFindTextDialog::Accept() {
    FilerFindTextOptions options;
    options.text = textInput_ ? textInput_->GetText() : std::string();
    if (options.text.empty()) return;   // nothing to look for - keep the dialog
    options.filePattern = patternInput_ ? patternInput_->GetText() : std::string();
    options.matchCase = matchCaseCheck_ && matchCaseCheck_->IsChecked();
    // Copied: closing may release the last reference to this dialog.
    auto callback = onFind;
    CloseDialog(DialogResult::OK);
    if (callback) callback(options);
}

std::shared_ptr<UltraFilerFindTextDialog> ShowFindTextDialog(
    const FilerFindTextOptions& initial,
    std::function<void(const FilerFindTextOptions&)> onFind,
    UltraCanvasWindowBase* parent) {
    auto dialog = std::make_shared<UltraFilerFindTextDialog>();
    dialog->Initialize(initial);
    dialog->onFind = std::move(onFind);
    dialog->ShowModal(parent);
    return dialog;
}

}  // namespace UltraCanvas
