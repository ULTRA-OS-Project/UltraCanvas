// UltraCloud/ui/UltraCloudResultText.h
// What a failed UltraCloud call says to a person, in their language.
//
// Result::message is technical and English - built by about ninety error
// sites, many of them passing on the network library's own text ("no folder
// /x", "share /y: no link in the answer") - so it cannot be a translation
// key. DescribeResult picks a whole translated sentence by the result's code
// (and HTTP status); ResultDetail is the technical message, for a tooltip, a
// "Details" view or a bug report.
//
//   status->SetText(UltraCloud::DescribeResult(r));
//   status->SetTooltip(UltraCloud::ResultDetail(r));
//
// Version: 0.1.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraLocalisation/UltraLocalisation.h"

#include <UltraCloud/UltraCloudTypes.h>

#include <string>

namespace UltraCloud {

// One sentence for the code; empty for a success.
inline std::string DescribeResult(const Result& r) {
    using UltraCanvas::Tr;
    using UltraCanvas::TrF;
    switch (r.code) {
        case ResultCode::Ok:              return std::string();
        case ResultCode::NotFound:        return Tr("The file or folder was not found on the server.");
        case ResultCode::AuthFailed:      return Tr("The server did not accept the sign-in - check the user name and the password.");
        case ResultCode::Unsupported:     return Tr("This cloud service cannot do that.");
        case ResultCode::Network:         return Tr("The server could not be reached - check the address and the connection.");
        case ResultCode::Server:
            return r.httpStatus > 0 ? TrF("The server answered with an error (HTTP {0}).", r.httpStatus)
                                    : Tr("The server answered with an error.");
        case ResultCode::InvalidArgument: return Tr("The address, path or name is not valid.");
        case ResultCode::IoError:         return Tr("A file on this computer could not be read or written.");
        case ResultCode::RateLimited:     return Tr("The cloud service is limiting requests - wait a moment and try again.");
        case ResultCode::Cancelled:       return Tr("Cancelled");
        case ResultCode::Unknown:         break;
    }
    return Tr("Something went wrong with the cloud service.");
}

// The technical message as the service gave it (English), with the
// transport's diagnostics when there are any.
inline std::string ResultDetail(const Result& r) {
    if (r.diagnostics.empty()) return r.message;
    return r.message.empty() ? r.diagnostics : r.message + "\n" + r.diagnostics;
}

} // namespace UltraCloud
