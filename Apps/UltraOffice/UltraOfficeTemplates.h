// Apps/UltraOffice/UltraOfficeTemplates.h
// New from template: the templates UltraOffice offers - the ones it ships
// (media/templates/office/, made by Tests/OfficeTemplatesTest.cpp --write)
// and the user's own (UltraOffice/templates/ in the settings folder, where
// "Save as template" puts them) - and the gallery the Start tab shows them
// in, a card per template with a picture of its first page, sheet or slide.
// A template is an ordinary .ott / .dotx, .ots / .xltx, .otp / .potx file
// (UltraCanvasFileLoader::IsOfficeTemplate); opening one starts a new,
// untitled document from it, so saving asks for a name and the template is
// never written over (Docs/Research/UltraOfficeDesignProposal.md, §6).
//
// Version: 0.1.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraOfficeHost.h"

#include "UltraCanvasContainer.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraOffice {

struct OfficeTemplate {
    std::string path;            // UTF-8
    std::string name;            // as the gallery shows it
    OfficeKind kind = OfficeKind::Unknown;
    bool own = false;            // the user's, not shipped
};

// media/templates/office under the resources folder.
std::filesystem::path ShippedTemplatesFolder();

// The templates in the shipped folder, then those in `ownFolder` (which may
// be empty or missing): documents, spreadsheets, presentations, each by name.
std::vector<OfficeTemplate> ListTemplates(const std::filesystem::path& ownFolder);

// The extension "Save as template" proposes for a kind, in the format new
// files are saved in: .ott / .dotx, .ots / .xltx, .otp / .potx.
std::string TemplateExtension(OfficeKind kind, SaveFormat format);

// The gallery: a card per template, `width` wide, four to a row - the name,
// the kind (or "Your template") and a picture of the template drawn a moment
// after the gallery is shown, so the Start tab is not held up. A click on a
// card calls `onPick` with the template's path.
std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildTemplateGallery(
    const std::vector<OfficeTemplate>& templates, int width,
    std::function<void(const std::string& path)> onPick);
// What the gallery for `count` templates needs, `width` wide.
int TemplateGalleryHeight(size_t count, int width);

} // namespace UltraOffice
