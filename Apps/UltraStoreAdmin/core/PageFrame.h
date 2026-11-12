// Apps/UltraStoreAdmin/core/PageFrame.h
// The look of the pages the publish folder carries for a browser (the
// partner landing pages under go/): the same frame as the payment
// service's pages (Server/UltraStorePay/ultrastore_pay/pages.py) - the
// ULTRA OS mark and the name at the top, the content on a card in the
// store's colours, a dark look where the browser asks for one, no script
// or font from anywhere else - so what a buyer reads in a browser looks
// like one product.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraStoreTypes.h"

#include <string>

namespace UltraStoreAdmin {

// The stylesheet, the one in pages.py (STYLESHEET): change both together.
extern const char* const kPageStylesheet;

// The mark the pages show, <publish folder>/media/<file>: CopyPageMarks
// puts them there from the framework's media/icons/desktop/.
constexpr const char* kLogoLightFile = "ultraos-logo-light.png";
constexpr const char* kLogoDarkFile = "ultraos-logo-dark.png";

// A whole page: the stylesheet inline, the mark from `mediaPrefix` (the
// path of media/ relative to the page, "../../media/" from go/<CODE>/),
// `heading` on the card, `bodyHtml` under it, `footer` beneath the card.
std::string FramePage(const std::string& title, const std::string& heading, const std::string& bodyHtml,
                      const std::string& mediaPrefix, const std::string& footer);

// The marks into <outDirUtf8>/media/, from the framework's media folder
// when the build installed one; without it the pages show the alt text
// and nothing is copied.
UltraStore::Result<void> CopyPageMarks(const std::string& outDirUtf8);

} // namespace UltraStoreAdmin
