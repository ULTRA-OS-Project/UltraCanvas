// Apps/UltraAuthenticator/BrandHeader.h
// The app logo, centred, with the app's name in small type beneath it.
//
// Shown at the top of the two screens the app opens on — the first-launch
// NewVaultDialog and the LockScreenDialog — so the password prompt a user
// meets first is recognisably this app's and not some other program's. It is
// the same media/appicon/UltraAuthenticator.png the window and taskbar use.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef AUTHENTICATOR_BRANDHEADER_H
#define AUTHENTICATOR_BRANDHEADER_H

#include "Theme.h"

#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasUtils.h"

#include <memory>
#include <string>

namespace UltraCanvas {
namespace Authenticator {

constexpr long kBrandLogoSize   = 56;
constexpr long kBrandNameHeight = 16;
// Logo, gap, name, and the space before whatever follows.
constexpr long kBrandHeaderHeight = kBrandLogoSize + 4 + kBrandNameHeight + 14;

// Adds the logo and name to `dialog`, centred in `dialogWidth`, starting at
// `y`. Returns the y at which the dialog's own content starts.
inline long AddBrandHeader(UltraCanvasModalDialog& dialog, const std::string& idPrefix,
                           long dialogWidth, long y) {
    auto logo = std::make_shared<UltraCanvasImageElement>(
        idPrefix + "-logo", (dialogWidth - kBrandLogoSize) / 2, y,
        kBrandLogoSize, kBrandLogoSize);
    logo->LoadFromFile(
        NormalizePath(GetResourcesDir() + "media/appicon/UltraAuthenticator.png"));
    logo->SetFitMode(ImageFitMode::Contain);
    dialog.AddChild(logo);
    y += kBrandLogoSize + 4;

    auto name = std::make_shared<UltraCanvasLabel>(
        idPrefix + "-appname", 0, y, dialogWidth, kBrandNameHeight, "UltraAuthenticator");
    name->SetFont(Theme::kUiFont, Theme::kSizeSmall);
    name->SetTextColor(Theme::kTextMuted);
    name->SetAlignment(TextAlignment::Center);
    dialog.AddChild(name);
    y += kBrandNameHeight + 14;

    return y;
}

} // namespace Authenticator
} // namespace UltraCanvas

#endif // AUTHENTICATOR_BRANDHEADER_H
