// dialogs/UltraCanvasCSVExportDialog.cpp
// Implementation of the CSV/TSV "Text Export" options dialog.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasCSVExportDialog.h"
#include "CSSLayout/CSSLayout.h"
#include <algorithm>

namespace UltraCanvas {

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

void UltraCanvasCSVExportDialog::Initialize(UltraCanvasSpreadsheet* source,
                                            const CSVExportOptions& initial) {
    source_ = source;

    DialogConfig config;
    config.title = "Text Export";
    config.width = 620;
    config.height = 560;
    config.resizable = true;
    config.buttons = DialogButtons::NoButtons;  // custom Export/Cancel below

    CreateDialog(config);
    BuildLayout();
    WireCallbacks();

    ApplyOptionsToControls(initial);
    RefreshPreview();
}

void UltraCanvasCSVExportDialog::BuildLayout() {
    layout.SetFlexColumn();
    layout.SetFlexGap(10);
    SetPadding(16);

    contentSection = std::make_shared<UltraCanvasContainer>("CSVExportContent", 0, 0, 588, 460);
    contentSection->layout.SetFlexColumn();
    contentSection->layout.SetFlexGap(8);

    auto makeLabel = [](const std::string& id, const std::string& text, int w, bool bold) {
        auto l = std::make_shared<UltraCanvasLabel>(id, 0, 0, (float)w, 22);
        l->SetText(text);
        l->SetFontSize(11);
        if (bold) l->SetFontWeight(FontWeight::Bold);
        return l;
    };

    // ===== Export (charset) =====
    contentSection->AddChild(makeLabel("lblExport", "Export", 200, true));

    auto charsetRow = std::make_shared<UltraCanvasContainer>("ExpCharsetRow", 0, 0, 588, 28);
    charsetRow->layout.SetFlexRow();
    charsetRow->layout.SetFlexGap(8);
    charsetRow->AddChild(makeLabel("lblExpCharset", "Character set:", 110, false));
    encodingDropdown_ = std::make_shared<UltraCanvasDropdown>("ExpEncodingDD", 0, 0, 240, 24);
    encodingDropdown_->AddItem("Unicode (UTF-8)", "utf8");
    encodingDropdown_->AddItem("Western Europe (ISO-8859-1)", "latin1");
    encodingDropdown_->AddItem("Western Europe (Windows-1252)", "cp1252");
    encodingDropdown_->AddItem("Unicode (UTF-16)", "utf16");
    charsetRow->AddChild(encodingDropdown_);
    contentSection->AddChild(charsetRow);

    // ===== Field options =====
    contentSection->AddChild(makeLabel("lblExpField", "Field options", 300, true));

    auto sepRow = std::make_shared<UltraCanvasContainer>("ExpSepRow", 0, 0, 588, 28);
    sepRow->layout.SetFlexRow();
    sepRow->layout.SetFlexGap(8);
    sepRow->AddChild(makeLabel("lblExpSep", "Field separator:", 110, false));
    separatorDropdown_ = std::make_shared<UltraCanvasDropdown>("ExpSepDD", 0, 0, 160, 24);
    separatorDropdown_->AddItem("Comma  ,", ",");
    separatorDropdown_->AddItem("Semicolon  ;", ";");
    separatorDropdown_->AddItem("Tab", "\t");
    separatorDropdown_->AddItem("Space", " ");
    separatorDropdown_->AddItem("Other", "other");
    sepRow->AddChild(separatorDropdown_);
    otherSepInput_ = std::make_shared<UltraCanvasTextInput>("ExpOtherSep", 0, 0, 40, 24);
    otherSepInput_->SetShowValidationState(false);
    sepRow->AddChild(otherSepInput_);
    contentSection->AddChild(sepRow);

    auto delimRow = std::make_shared<UltraCanvasContainer>("ExpDelimRow", 0, 0, 588, 28);
    delimRow->layout.SetFlexRow();
    delimRow->layout.SetFlexGap(8);
    delimRow->AddChild(makeLabel("lblExpDelim", "Text delimiter:", 110, false));
    textDelimDropdown_ = std::make_shared<UltraCanvasDropdown>("ExpTextDelimDD", 0, 0, 90, 24);
    textDelimDropdown_->AddItem("\"", "\"");
    textDelimDropdown_->AddItem("'", "'");
    textDelimDropdown_->AddItem("(none)", "");
    delimRow->AddChild(textDelimDropdown_);
    contentSection->AddChild(delimRow);

    auto endRow = std::make_shared<UltraCanvasContainer>("ExpEndRow", 0, 0, 588, 28);
    endRow->layout.SetFlexRow();
    endRow->layout.SetFlexGap(8);
    endRow->AddChild(makeLabel("lblExpEnd", "Line ending:", 110, false));
    lineEndingDropdown_ = std::make_shared<UltraCanvasDropdown>("ExpEndDD", 0, 0, 200, 24);
    lineEndingDropdown_->AddItem("Unix (LF)", "lf");
    lineEndingDropdown_->AddItem("Windows (CRLF)", "crlf");
    endRow->AddChild(lineEndingDropdown_);
    contentSection->AddChild(endRow);

    // ===== Other options =====
    contentSection->AddChild(makeLabel("lblExpOther", "Other options", 300, true));

    auto optRow = std::make_shared<UltraCanvasContainer>("ExpOptRow", 0, 0, 588, 24);
    optRow->layout.SetFlexRow();
    optRow->layout.SetFlexGap(16);
    quoteAllCheck_ = std::make_shared<UltraCanvasCheckbox>("ExpQuoteAll", 0, 0, 220, 20,
                                                           "Quote all text cells");
    quoteAllCheck_->SetFontSize(11);
    bomCheck_ = std::make_shared<UltraCanvasCheckbox>("ExpBom", 0, 0, 200, 20,
                                                      "Write byte-order mark");
    bomCheck_->SetFontSize(11);
    optRow->AddChild(quoteAllCheck_);
    optRow->AddChild(bomCheck_);
    contentSection->AddChild(optRow);

    // ===== Preview =====
    contentSection->AddChild(makeLabel("lblExpPreview", "Preview", 300, true));
    previewText_ = std::make_shared<UltraCanvasTextInput>("ExpPreview", 0, 0, 588, 200);
    previewText_->SetInputType(TextInputType::Multiline);
    previewText_->SetReadOnly(true);
    previewText_->SetFontSize(11);
    previewText_->SetShowValidationState(false);
    contentSection->AddChild(previewText_);

    AddChild(contentSection);

    // ===== Buttons =====
    buttonSection = std::make_shared<UltraCanvasContainer>("CSVExportButtons", 0, 0, 588, 36);
    buttonSection->layout.SetFlexRow();
    buttonSection->layout.SetFlexGap(8);
    buttonSection->AddStretchSpacer(1);

    okButton_ = std::make_shared<UltraCanvasButton>("CSVExportOK", 0, 0, 110, 28);
    okButton_->SetText("Export");
    cancelButton_ = std::make_shared<UltraCanvasButton>("CSVExportCancel", 0, 0, 110, 28);
    cancelButton_->SetText("Cancel");

    buttonSection->AddChild(cancelButton_);
    buttonSection->AddChild(okButton_);
    AddChild(buttonSection);
}

void UltraCanvasCSVExportDialog::WireCallbacks() {
    auto refresh = [this]() { if (!building_) RefreshPreview(); };

    encodingDropdown_->onSelectionChanged   = [refresh](int, const DropdownItem&) { refresh(); };
    separatorDropdown_->onSelectionChanged  = [refresh](int, const DropdownItem&) { refresh(); };
    textDelimDropdown_->onSelectionChanged  = [refresh](int, const DropdownItem&) { refresh(); };
    lineEndingDropdown_->onSelectionChanged = [refresh](int, const DropdownItem&) { refresh(); };

    otherSepInput_->onTextChanged = [refresh](const std::string&) { refresh(); };

    auto onToggle = [refresh](CheckedState, CheckedState) { refresh(); };
    quoteAllCheck_->onStateChanged = onToggle;
    bomCheck_->onStateChanged      = onToggle;

    okButton_->onClick = [this]() {
        CSVExportOptions opt = BuildOptions();
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

void UltraCanvasCSVExportDialog::ApplyOptionsToControls(const CSVExportOptions& opt) {
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

    int sepIndex;
    switch (opt.fieldSeparator) {
        case ',':  sepIndex = 0; break;
        case ';':  sepIndex = 1; break;
        case '\t': sepIndex = 2; break;
        case ' ':  sepIndex = 3; break;
        default:   sepIndex = 4; break;  // Other
    }
    separatorDropdown_->SetSelectedIndex(sepIndex, false);
    otherSepInput_->SetText(sepIndex == 4 ? std::string(1, opt.fieldSeparator) : std::string());

    int delimIndex = 2;  // (none)
    if (opt.textDelimiter == '"') delimIndex = 0;
    else if (opt.textDelimiter == '\'') delimIndex = 1;
    textDelimDropdown_->SetSelectedIndex(delimIndex, false);

    lineEndingDropdown_->SetSelectedIndex(
        opt.lineEnding == CSVExportOptions::LineEnding::CRLF ? 1 : 0, false);

    quoteAllCheck_->SetChecked(opt.quoteAllFields);
    bomCheck_->SetChecked(opt.writeBOM);

    building_ = false;
}

CSVExportOptions UltraCanvasCSVExportDialog::BuildOptions() const {
    CSVExportOptions opt;

    int enc = encodingDropdown_ ? encodingDropdown_->GetSelectedIndex() : 0;
    switch (enc) {
        case 1:  opt.encoding = CSVImportOptions::Encoding::Latin1; break;
        case 2:  opt.encoding = CSVImportOptions::Encoding::Windows1252; break;
        case 3:  opt.encoding = CSVImportOptions::Encoding::UTF16LE; break;
        default: opt.encoding = CSVImportOptions::Encoding::UTF8; break;
    }

    opt.fieldSeparator = ',';
    if (separatorDropdown_) {
        const DropdownItem* it = separatorDropdown_->GetSelectedItem();
        if (it) {
            if (it->value == "other") {
                std::string s = otherSepInput_ ? otherSepInput_->GetText() : std::string();
                opt.fieldSeparator = s.empty() ? ',' : s[0];
            } else if (!it->value.empty()) {
                opt.fieldSeparator = it->value[0];
            }
        }
    }

    opt.textDelimiter = '"';
    if (textDelimDropdown_) {
        const DropdownItem* it = textDelimDropdown_->GetSelectedItem();
        if (it) opt.textDelimiter = it->value.empty() ? 0 : it->value[0];
    }

    opt.lineEnding = (lineEndingDropdown_ && lineEndingDropdown_->GetSelectedIndex() == 1)
        ? CSVExportOptions::LineEnding::CRLF
        : CSVExportOptions::LineEnding::LF;

    opt.quoteAllFields = quoteAllCheck_ && quoteAllCheck_->IsChecked();
    opt.writeBOM       = bomCheck_ && bomCheck_->IsChecked();

    return opt;
}

void UltraCanvasCSVExportDialog::RefreshPreview() {
    if (!previewText_ || !source_) return;

    // Build the UTF-8 text (charset/BOM only affect the bytes on disk, not what
    // the user reads here). Cap it so a huge sheet stays responsive.
    std::string text = source_->ExportCSVToString(BuildOptions());
    const size_t kMaxPreview = 16 * 1024;
    if (text.size() > kMaxPreview) {
        text.resize(kMaxPreview);
        text += "\n… (preview truncated)";
    }
    previewText_->SetText(text);
    previewText_->RequestRedraw();
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::shared_ptr<UltraCanvasCSVExportDialog>
ShowCSVExportDialog(UltraCanvasSpreadsheet* source,
                    const CSVExportOptions& initial,
                    std::function<void(const CSVExportOptions&)> onAccept,
                    UltraCanvasWindowBase* parent) {
    auto dialog = std::make_shared<UltraCanvasCSVExportDialog>();
    dialog->Initialize(source, initial);
    dialog->onAccept = std::move(onAccept);
    dialog->ShowModal(parent);
    return dialog;
}

} // namespace UltraCanvas
