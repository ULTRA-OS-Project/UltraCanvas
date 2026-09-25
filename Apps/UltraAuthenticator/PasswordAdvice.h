// Apps/UltraAuthenticator/PasswordAdvice.h
// What makes a safe master password, ticked off live as it is typed.
//
// Advice, not a gate. The strength meter above it already follows the field;
// this says *why* the bar is short. None of the rules blocks Create or Change:
// a long passphrase of lower-case words is a good password that fails half of
// them, and the choice belongs to the person typing it. So an unmet rule is a
// grey circle, not a red cross, and the heading says the rules are optional.
//
// Built from the catalogue's UltraCanvasPasswordRuleLegend, in two columns so
// five rules take three rows.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef AUTHENTICATOR_PASSWORDADVICE_H
#define AUTHENTICATOR_PASSWORDADVICE_H

#include "Theme.h"

#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPasswordRuleLegend.h"
#include "UltraCanvasTextInput.h"

#include <algorithm>
#include <memory>
#include <string>

namespace UltraCanvas {
namespace Authenticator {

constexpr int kAdviceMinLength = 12;

// Heading plus three checklist rows (see PasswordRuleLegend::GetContentHeight).
constexpr long kPasswordAdviceHeight = 18 + 70 + 6;

// Adds the heading and the two rule columns at (x, y), `width` wide, linked to
// `input`. Returns the y below them.
inline long AddPasswordAdvice(UltraCanvasModalDialog& dialog, const std::string& idPrefix,
                              long x, long y, long width, UltraCanvasTextInput* input) {
    auto heading = std::make_shared<UltraCanvasLabel>(
        idPrefix + "-advice-lbl", x, y, width, 16,
        "A safe password has (recommended, not required):");
    heading->SetFont(Theme::kUiFont, Theme::kSizeSmall);
    heading->SetTextColor(Theme::kTextMuted);
    dialog.AddChild(heading);
    y += 18;

    PasswordRuleLegendConfig cfg;
    cfg.style        = LegendStyle::Checklist;
    cfg.showMetRules = true;
    cfg.itemSpacing  = 4;
    cfg.iconSize     = 14;
    cfg.metColor     = Theme::kCountdownCalm;
    cfg.unmetColor   = Theme::kTextMuted;
    cfg.textColor    = Theme::kTextSecondary;
    cfg.metIcon      = "✓";
    cfg.unmetIcon    = "○";

    const long columnWidth = width / 2;
    long bottom = y;
    for (int column = 0; column < 2; ++column) {
        auto legend = CreateChecklistLegend(
            idPrefix + "-advice-" + std::to_string(column),
            x + column * columnWidth, y, columnWidth, 70);
        legend->SetConfig(cfg);
        legend->ClearRules();
        if (column == 0) {
            legend->AddRule(ValidationRule::MinLength(
                kAdviceMinLength, std::to_string(kAdviceMinLength) + " or more characters"));
            legend->AddRule(ValidationRule::RequireUppercase(1, "An uppercase letter"));
            legend->AddRule(ValidationRule::RequireLowercase(1, "A lowercase letter"));
        } else {
            legend->AddRule(ValidationRule::RequireDigit(1, "A number"));
            legend->AddRule(ValidationRule::RequireSpecialChar(1, "A symbol, such as ! # %"));
        }
        legend->SetElementSize(Size2Df(static_cast<float>(columnWidth),
                                       legend->GetContentHeight()));
        legend->LinkToInput(input);
        dialog.AddChild(legend);
        bottom = std::max(bottom, y + static_cast<long>(legend->GetContentHeight()));
    }
    return bottom + 6;
}

} // namespace Authenticator
} // namespace UltraCanvas

#endif // AUTHENTICATOR_PASSWORDADVICE_H
