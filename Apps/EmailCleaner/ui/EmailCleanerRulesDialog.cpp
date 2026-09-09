// Apps/EmailCleaner/ui/EmailCleanerRulesDialog.cpp
// Version: 0.3.0 (Phase 3)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerRulesDialog.h"

#include <cstdlib>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace EmailCleaner {

namespace {

const Color kQuietColor(96, 96, 96, 255);
const Color kWarningColor(176, 96, 0, 255);

constexpr float kRowH = 30.0f;

// The phrase as the file format writes it, with its boundary markers back on.
std::string PhraseOf(const KeywordRule& rule) {
    return (rule.openStart ? "*" : "") + rule.term + (rule.openEnd ? "*" : "");
}

std::string WeightText(double weight) {
    std::string s = std::to_string(weight);
    // 3.000000 -> 3, 2.500000 -> 2.5
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

} // namespace

void RulesDialog::Show(const std::string& userRulesPath, std::size_t builtInCount) {
    path_         = userRulesPath;
    builtInCount_ = builtInCount;
    loadErrors_.clear();
    rules_.clear();

    // The file may not exist yet (nothing added), which is not an error.
    RuleSet loaded;
    if (loaded.LoadFile(path_, &loadErrors_)) rules_ = loaded.Rules();

    DialogConfig config;
    config.title      = "Keyword rules";
    config.width      = 780;
    config.height     = 560;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;

    dialog_ = UltraCanvasDialogManager::CreateDialog(config);
    auto* dlg = dialog_.get();
    dialog_->layout.SetFlexColumn()
                   .SetFlexGap(10)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog_->SetPadding(14);

    auto heading = CreateLabel(
        "ecRuleHeading", 0, 0, 740, 58,
        "Your rules are layered on top of " + std::to_string(builtInCount_) +
        " built-in ones, so anything here can only sharpen detection — it never "
        "switches a built-in rule off. Terms are matched after normalisation, so "
        "\"viagra\" already catches V1AGRA and v.i.a.g.r.a.");
    heading->SetWrap(TextWrap::WrapWord);
    dialog_->AddChild(heading);

    // ---- The add row -------------------------------------------------------
    auto form = CreateContainer("ecRuleForm", 0, 0, 740, 62);
    form->AddChild(CreateLabel("ecRuleFormLabel", 0, 0, 300, 18, "Add a rule"));

    float x = 0.0f;
    category_ = CreateDropdown("ecRuleCategory", x, 22, 170, 26);
    int defaultIndex = 0, index = 0;
    for (MessageCategory c : AllCategories()) {
        category_->AddItem(CategoryLabel(c), ToString(c));
        // Open on the family most rules are written for, rather than on
        // "Unclassified", which no rule should ever point at.
        if (c == MessageCategory::ProductSpam) defaultIndex = index;
        ++index;
    }
    category_->SetSelectedIndex(defaultIndex, false);
    form->AddChild(category_);
    x += 178.0f;

    field_ = CreateDropdown("ecRuleField", x, 22, 130, 26);
    for (const char* f : { "any", "subject", "body", "sender", "attachment" })
        field_->AddItem(f, f);
    field_->SetSelectedIndex(0, false);
    form->AddChild(field_);
    x += 138.0f;

    weight_ = CreateTextInput("ecRuleWeight", static_cast<int>(x), 22, 70, 26);
    weight_->SetPlaceholder("weight");
    weight_->SetText("3");
    form->AddChild(weight_);
    x += 78.0f;

    phrase_ = CreateTextInput("ecRulePhrase", static_cast<int>(x), 22, 230, 26);
    phrase_->SetPlaceholder("phrase  ('*' = no word boundary)");
    if (!suggested_.empty()) phrase_->SetText(suggested_);
    form->AddChild(phrase_);
    x += 238.0f;

    auto addBtn = CreateButton("ecRuleAdd", x, 22, 90, 26, "Add");
    addBtn->onClick = [this]() {
        std::string error;
        if (!AddFromForm(error)) {
            if (status_) {
                status_->SetText(error);
                status_->SetTextColor(kWarningColor);
            }
            return;
        }
        RebuildList();
    };
    form->AddChild(addBtn);
    dialog_->AddChild(form);

    // ---- The list ----------------------------------------------------------
    list_ = CreateScrollableContainer("ecRuleList", 0, 0, 740, 320);
    dialog_->AddChild(list_);
    list_->layoutItem.SetFlexGrow(1);

    status_ = CreateLabel("ecRuleStatus", 0, 0, 740, 20, "");
    status_->SetTextColor(kQuietColor);
    dialog_->AddChild(status_);

    // ---- Buttons -----------------------------------------------------------
    auto buttonRow = CreateContainer("ecRuleButtons", 0, 0, 0, 34);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(10)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);

    auto saveBtn = std::make_shared<UltraCanvasButton>("ecRuleSave", 0, 0, 190, 28);
    saveBtn->SetText("Save and re-analyse");
    saveBtn->onClick = [this, dlg]() {
        Save();
        dlg->CloseDialog(DialogResult::OK);
    };
    buttonRow->AddChild(saveBtn);

    auto cancelBtn = std::make_shared<UltraCanvasButton>("ecRuleCancel", 0, 0, 90, 28);
    cancelBtn->SetText("Cancel");
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);
    dialog_->AddChild(buttonRow);

    RebuildList();
    UltraCanvasDialogManager::ShowDialog(dialog_, nullptr, nullptr);
}

void RulesDialog::RebuildList() {
    if (!list_) return;
    list_->ClearChildren();

    if (rules_.empty()) {
        auto empty = CreateLabel("ecRuleEmpty", 0, 0, 700, 40,
            "No rules of your own yet. Add one above, or mark a sender on the "
            "map and come back with a term worth catching.");
        empty->SetWrap(TextWrap::WrapWord);
        list_->AddChild(empty);
    }

    float y = 0.0f;
    for (std::size_t i = 0; i < rules_.size(); ++i) {
        const KeywordRule& rule = rules_[i];
        const std::string id = "ecRuleRow" + std::to_string(i);
        auto row = CreateContainer(id, 0, y, 700, kRowH);

        row->AddChild(CreateLabel(id + ".cat", 0, 5, 150, 20,
                                  CategoryLabel(rule.category)));
        row->AddChild(CreateLabel(id + ".w", 156, 5, 50, 20, WeightText(rule.weight)));
        row->AddChild(CreateLabel(id + ".f", 210, 5, 90, 20, ToString(rule.field)));
        row->AddChild(CreateLabel(id + ".p", 304, 5, 280, 20, PhraseOf(rule)));

        auto removeBtn = CreateButton(id + ".rm", 592, 3, 96, 24, "Remove");
        const std::size_t index = i;
        removeBtn->onClick = [this, index]() {
            if (index < rules_.size()) rules_.erase(rules_.begin() + index);
            RebuildList();
            if (status_) {
                status_->SetText("Removed. Nothing is written until you save.");
                status_->SetTextColor(kQuietColor);
            }
        };
        row->AddChild(removeBtn);

        list_->AddChild(row);
        y += kRowH + 2.0f;
    }

    if (status_ && !loadErrors_.empty()) {
        status_->SetText(std::to_string(loadErrors_.size()) +
                         " line(s) in the file could not be read and were skipped: " +
                         loadErrors_.front());
        status_->SetTextColor(kWarningColor);
    }
}

bool RulesDialog::AddFromForm(std::string& outError) {
    const std::string phrase = phrase_ ? phrase_->GetText() : std::string();
    if (phrase.empty() || phrase == "*" || phrase == "**") {
        outError = "A rule needs a phrase to look for.";
        return false;
    }

    double weight = 1.0;
    if (weight_) {
        const std::string text = weight_->GetText();
        if (!text.empty()) {
            char* end = nullptr;
            weight = std::strtod(text.c_str(), &end);
            if (end == text.c_str() || weight == 0.0) {
                outError = "Weight must be a number, and not zero — a zero-weight "
                           "rule would never change a verdict.";
                return false;
            }
        }
    }

    // GetSelectedItem() returns null when nothing is selected, which the
    // dropdowns above make impossible — but a null deref here would be a crash
    // in a dialog the user opened to be careful.
    const DropdownItem* categoryItem = category_ ? category_->GetSelectedItem() : nullptr;
    const DropdownItem* fieldItem    = field_ ? field_->GetSelectedItem() : nullptr;

    KeywordRule rule;
    rule.category = categoryItem ? CategoryFromString(categoryItem->value)
                                 : MessageCategory::ProductSpam;
    rule.field    = fieldItem ? MatchFieldFromString(fieldItem->value)
                              : MatchField::Any;
    rule.weight = weight;
    // The same parse the file format uses, so what is typed here and what a
    // hand-edited file means are the same thing — including the normalisation,
    // without which a term could never match the normalised message text.
    ParseRulePhrase(phrase, rule.term, rule.openStart, rule.openEnd);
    if (!rule.Valid()) {
        outError = "That phrase has nothing left to match on once normalised.";
        return false;
    }

    for (const KeywordRule& existing : rules_) {
        if (existing.term == rule.term && existing.category == rule.category &&
            existing.field == rule.field) {
            outError = "You already have that rule.";
            return false;
        }
    }

    rules_.push_back(rule);
    if (phrase_) phrase_->SetText("");
    if (status_) {
        status_->SetText("Added. Nothing is written until you save.");
        status_->SetTextColor(kQuietColor);
    }
    return true;
}

void RulesDialog::Save() {
    RuleSet set;
    for (const KeywordRule& rule : rules_) set.Add(rule);
    if (!set.SaveFile(path_)) {
        UltraCanvasDialogManager::ShowWarning(
            "Could not write " + path_ + ". The rules were not saved.",
            "Not saved", nullptr);
        return;
    }
    if (onSaved) onSaved();
}

} // namespace EmailCleaner
