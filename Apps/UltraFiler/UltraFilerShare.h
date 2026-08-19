// Apps/UltraFiler/UltraFilerShare.h
// "Extras > Share": hands the selected files to the platform's e-mail
// composer as attachments - MAPI on Windows, Mail.app on macOS, xdg-email on
// Linux. Folders cannot be attached; the caller filters them out.
// Version: 1.0.0
// Last Modified: 2026-08-17
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>

namespace UltraCanvas {
namespace UltraFilerShare {

// Opens the platform's e-mail composer with `paths` attached. Returns false
// (with a user-presentable message in `outError`) when no composer is
// available or it could not be started; a user closing the composer without
// sending is not an error.
bool ShareByEmail(const std::vector<std::string>& paths, std::string& outError);

} // namespace UltraFilerShare
} // namespace UltraCanvas
