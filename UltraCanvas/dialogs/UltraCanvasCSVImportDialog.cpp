// dialogs/UltraCanvasCSVImportDialog.cpp
// Implementation of the CSV/TSV "Text Import" options dialog.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasCSVImportDialog.h"
#include "CSSLayout/CSSLayout.h"
#include <algorithm>

namespace UltraCanvas {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

std::shared_ptr<UltraCanvasCheckbox>
UltraCanvasCSVImportDialog::MakeCheck(const std::string& id, const std::string& label,
                                      bool checked, int width) {
    auto cb = std::make_shared<UltraCanvasCheckbox>(id, 0, 0, (float)width, 20, label);
    cb->SetFontSize(11);
    cb->SetChecked(checked);
    return cb;
}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

void UltraCanvasCSVImportDialog::Initialize(const std::string& filePath) {
    filePath_ = filePath;

    DialogConfig config;
    config.title = "Text Import";
    config.width = 660;
    config.height = 640;
    config.resizable = true;
    config.buttons = DialogButtons::NoButtons;  // custom OK/Cancel below

    CreateDialog(config);
    BuildLayout();
    WireCallbacks();

    // Seed the controls from auto-detection, then show the first preview.
    CSVImportOptions detected;
    if (!CSVDetectOptionsFromFile(filePath_, detected)) {
        detected = CSVImportOptions{};
    }
    ApplyOptionsToControls(detected);
    RefreshPreview();
}

void UltraCanvasCSVImportDialog::BuildLayout() {
    layout.SetFlexColumn();
    layout.SetFlexGap(10);
    SetPadding(16);

    contentSection = std::make_shared<UltraCanvasContainer>("CSVImportContent", 0, 0, 628, 540);
    contentSection->layout.SetFlexColumn();
    contentSection->layout.SetFlexGap(8);

    auto makeLabel = [](const std::string& id, const std::string& text, int w, bool bold) {
        auto l = std::make_shared<UltraCanvasLabel>(id, 0, 0, (float)w, 22);
        l->SetText(text);
        l->SetFontSize(11);
        if (bold) l->SetFontWeight(FontWeight::Bold);
        return l;
    };

    // ===== Import (charset + start row) =====
    contentSection->AddChild(makeLabel("lblImport", "Import", 200, true));

    auto charsetRow = std::make_shared<UltraCanvasContainer>("CharsetRow", 0, 0, 628, 28);
    charsetRow->layout.SetFlexRow();
    charsetRow->layout.SetFlexGap(8);
    charsetRow->AddChild(makeLabel("lblCharset", "Character set:", 110, false));
    encodingDropdown_ = std::make_shared<UltraCanvasDropdown>("EncodingDD", 0, 0, 240, 24);
    encodingDropdown_->AddItem("Unicode (UTF-8)", "utf8");
    encodingDropdown_->AddItem("Western Europe (ISO-8859-1)", "latin1");
    encodingDropdown_->AddItem("Western Europe (Windows-1252)", "cp1252");
    encodingDropdown_->AddItem("Unicode (UTF-16)", "utf16");
    charsetRow->AddChild(encodingDropdown_);
    contentSection->AddChild(charsetRow);

    auto rowRow = std::make_shared<UltraCanvasContainer>("StartRowRow", 0, 0, 628, 28);
    rowRow->layout.SetFlexRow();
    rowRow->layout.SetFlexGap(8);
    rowRow->AddChild(makeLabel("lblFromRow", "From row:", 110, false));
    startRowInput_ = std::make_shared<UltraCanvasTextInput>("StartRowInput", 0, 0, 70, 24);
    startRowInput_->SetShowValidationState(false);
    startRowInput_->SetText("1");
    rowRow->AddChild(startRowInput_);
    contentSection->AddChild(rowRow);

    // ===== Separator options =====
    contentSection->AddChild(makeLabel("lblSep", "Separator options", 300, true));

    auto sepRow1 = std::make_shared<UltraCanvasContainer>("SepRow1", 0, 0, 628, 24);
    sepRow1->layout.SetFlexRow();
    sepRow1->layout.SetFlexGap(16);
    tabCheck_       = MakeCheck("sepTab", "Tab", false);
    commaCheck_     = MakeCheck("sepComma", "Comma", true);
    semicolonCheck_ = MakeCheck("sepSemicolon", "Semicolon", false);
    spaceCheck_     = MakeCheck("sepSpace", "Space", false);
    sepRow1->AddChild(tabCheck_);
    sepRow1->AddChild(commaCheck_);
    sepRow1->AddChild(semicolonCheck_);
    sepRow1->AddChild(spaceCheck_);
    contentSection->AddChild(sepRow1);

    auto sepRow2 = std::make_shared<UltraCanvasContainer>("SepRow2", 0, 0, 628, 24);
    sepRow2->layout.SetFlexRow();
    sepRow2->layout.SetFlexGap(8);
    otherCheck_ = MakeCheck("sepOther", "Other:", false);
    otherInput_ = std::make_shared<UltraCanvasTextInput>("OtherInput", 0, 0, 40, 24);
    otherInput_->SetShowValidationState(false);
    sepRow2->AddChild(otherCheck_);
    sepRow2->AddChild(otherInput_);
    contentSection->AddChild(sepRow2);

    auto sepRow3 = std::make_shared<UltraCanvasContainer>("SepRow3", 0, 0, 628, 28);
    sepRow3->layout.SetFlexRow();
    sepRow3->layout.SetFlexGap(8);
    mergeCheck_ = MakeCheck("mergeSep", "Merge delimiters", false);
    sepRow3->AddChild(mergeCheck_);
    sepRow3->AddChild(makeLabel("lblTextDelim", "Text delimiter:", 100, false));
    textDelimDropdown_ = std::make_shared<UltraCanvasDropdown>("TextDelimDD", 0, 0, 90, 24);
    textDelimDropdown_->AddItem("\"", "\"");
    textDelimDropdown_->AddItem("'", "'");
    textDelimDropdown_->AddItem("(none)", "");
    sepRow3->AddChild(textDelimDropdown_);
    contentSection->AddChild(sepRow3);

    // ===== Other options =====
    contentSection->AddChild(makeLabel("lblOther", "Other options", 300, true));

    auto optRow = std::make_shared<UltraCanvasContainer>("OtherOptRow", 0, 0, 628, 24);
    optRow->layout.SetFlexRow();
    optRow->layout.SetFlexGap(16);
    quotedAsTextCheck_  = MakeCheck("quotedText", "Quoted field as text", false, 180);
    detectNumbersCheck_ = MakeCheck("detectNum", "Detect special numbers", true, 200);
    optRow->AddChild(quotedAsTextCheck_);
    optRow->AddChild(detectNumbersCheck_);
    contentSection->AddChild(optRow);

    // ===== Preview =====
    contentSection->AddChild(makeLabel("lblFields", "Fields", 300, true));
    previewSheet_ = CreateSpreadsheetElement("CSVPreview", 0, 0, 628, 300);
    previewSheet_->SetShowFormulaBar(false);
    previewSheet_->SetShowSheetTabs(false);
    contentSection->AddChild(previewSheet_);

    AddChild(contentSection);

    // ===== Buttons =====
    buttonSection = std::make_shared<UltraCanvasContainer>("CSVImportButtons", 0, 0, 628, 36);
    buttonSection->layout.SetFlexRow();
    buttonSection->layout.SetFlexGap(8);
    buttonSection->AddStretchSpacer(1);

    okButton_ = std::make_shared<UltraCanvasButton>("CSVImportOK", 0, 0, 110, 28);
    okButton_->SetText("Import");
    cancelButton_ = std::make_shared<UltraCanvasButton>("CSVImportCancel", 0, 0, 110, 28);
    cancelButton_->SetText("Cancel");

    buttonSection->AddChild(cancelButton_);
    buttonSection->AddChild(okButton_);
    AddChild(buttonSection);
}

void UltraCanvasCSVImportDialog::WireCallbacks() {
    auto refresh = [this]() { if (!building_) RefreshPreview(); };

    encodingDropdown_->onSelectionChanged  = [refresh](int, const DropdownItem&) { refresh(); };
    textDelimDropdown_->onSelectionChanged = [refresh](int, const DropdownItem&) { refresh(); };

    startRowInput_->onTextChanged = [refresh](const std::string&) { refresh(); };
    otherInput_->onTextChanged    = [refresh](const std::string&) { refresh(); };

    auto onToggle = [refresh](CheckedState, CheckedState) { refresh(); };
    tabCheck_->onStateChanged           = onToggle;
    commaCheck_->onStateChanged         = onToggle;
    semicolonCheck_->onStateChanged     = onToggle;
    spaceCheck_->onStateChanged         = onToggle;
    otherCheck_->onStateChanged         = onToggle;
    mergeCheck_->onStateChanged         = onToggle;
    quotedAsTextCheck_->onStateChanged  = onToggle;
    detectNumbersCheck_->onStateChanged = onToggle;

    okButton_->onClick = [this]() {
        CSVImportOptions opt = BuildOptions();
        if (onAccept) onAccept(opt);
        CloseDialog(DialogResult::OK);
    };
    cancelButton_->onClick = [this]() {
        CloseDialog(DialogResult::Cancel);
    };
}

// ---------------------------------------------------------------------------
// Options <-> controls
// ---------------------------------------------------------------------------

void UltraCanvasCSVImportDialog::ApplyOptionsToControls(const CSVImportOptions& opt) {
    building_ = true;

    int encIndex = 0;
    switch (opt.encoding) {
        case CSVImportOptions::Encoding::Latin1:      encIndex = 1; break;
        case CSVImportOptions::Encoding::Windows1252: encIndex = 2; break;
        case CSVImportOptions::Encoding::UTF16LE:
        case CSVImportOptions::Encoding::UTF16BE:     encIndex = 3; break;
        default:                                      encIndex = 0; break;
    }
    encodingDropdown_->SetSelectedIndex(encIndex, false);

    startRowInput_->SetText(std::to_string(std::max(1, opt.startRow)));

    tabCheck_->SetChecked(opt.HasSeparator('\t'));
    commaCheck_->SetChecked(opt.HasSeparator(','));
    semicolonCheck_->SetChecked(opt.HasSeparator(';'));
    spaceCheck_->SetChecked(opt.HasSeparator(' '));

    // Any separator that is not one of the standard four goes into "Other".
    char other = 0;
    for (char c : opt.separators) {
        if (c != '\t' && c != ',' && c != ';' && c != ' ') { other = c; break; }
    }
    otherCheck_->SetChecked(other != 0);
    otherInput_->SetText(other ? std::string(1, other) : std::string());

    mergeCheck_->SetChecked(opt.mergeDelimiters);

    int delimIndex = 2;  // (none)
    if (opt.textDelimiter == '"') delimIndex = 0;
    else if (opt.textDelimiter == '\'') delimIndex = 1;
    textDelimDropdown_->SetSelectedIndex(delimIndex, false);

    quotedAsTextCheck_->SetChecked(opt.quotedAsText);
    detectNumbersCheck_->SetChecked(opt.detectNumbers);

    building_ = false;
}

CSVImportOptions UltraCanvasCSVImportDialog::BuildOptions() const {
    CSVImportOptions opt;

    int enc = encodingDropdown_ ? encodingDropdown_->GetSelectedIndex() : 0;
    switch (enc) {
        case 1:  opt.encoding = CSVImportOptions::Encoding::Latin1; break;
        case 2:  opt.encoding = CSVImportOptions::Encoding::Windows1252; break;
        case 3:  opt.encoding = CSVImportOptions::Encoding::UTF16LE; break;
        default: opt.encoding = CSVImportOptions::Encoding::UTF8; break;
    }

    int startRow = 1;
    if (startRowInput_) {
        try { startRow = std::stoi(startRowInput_->GetText()); } catch (...) { startRow = 1; }
    }
    opt.startRow = std::max(1, startRow);

    opt.separators.clear();
    if (tabCheck_ && tabCheck_->IsChecked())             opt.separators.insert('\t');
    if (commaCheck_ && commaCheck_->IsChecked())         opt.separators.insert(',');
    if (semicolonCheck_ && semicolonCheck_->IsChecked()) opt.separators.insert(';');
    if (spaceCheck_ && spaceCheck_->IsChecked())         opt.separators.insert(' ');
    if (otherCheck_ && otherCheck_->IsChecked() && otherInput_) {
        std::string s = otherInput_->GetText();
        if (!s.empty()) opt.separators.insert(s[0]);
    }
    if (opt.separators.empty()) opt.separators.insert(',');

    opt.mergeDelimiters = mergeCheck_ && mergeCheck_->IsChecked();

    opt.textDelimiter = '"';
    if (textDelimDropdown_) {
        const DropdownItem* it = textDelimDropdown_->GetSelectedItem();
        if (it) opt.textDelimiter = it->value.empty() ? 0 : it->value[0];
    }

    opt.quotedAsText  = quotedAsTextCheck_ && quotedAsTextCheck_->IsChecked();
    opt.detectNumbers = detectNumbersCheck_ && detectNumbersCheck_->IsChecked();

    // Decimal/thousands convention: a comma field-separator implies dot decimals,
    // otherwise assume the European comma decimal (matches auto-detection).
    if (opt.separators.count(',')) {
        opt.decimalSeparator = '.'; opt.thousandsSeparator = ',';
    } else {
        opt.decimalSeparator = ','; opt.thousandsSeparator = '.';
    }

    return opt;
}

void UltraCanvasCSVImportDialog::RefreshPreview() {
    if (!previewSheet_) return;
    previewSheet_->LoadCSVWithOptions(filePath_, BuildOptions());
    previewSheet_->RequestRedraw();
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::shared_ptr<UltraCanvasCSVImportDialog>
ShowCSVImportDialog(const std::string& filePath,
                    std::function<void(const CSVImportOptions&)> onAccept,
                    UltraCanvasWindowBase* parent) {
    auto dialog = std::make_shared<UltraCanvasCSVImportDialog>();
    dialog->Initialize(filePath);
    dialog->onAccept = std::move(onAccept);
    dialog->ShowModal(parent);
    return dialog;
}

} // namespace UltraCanvas
