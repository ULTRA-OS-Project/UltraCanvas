// core/UltraNet/UltraNetFtpInternal.h
// Internal listing-parser declarations. Not part of the public include
// surface — exported only so the UltraNet test suite can drive the parsers
// directly with synthetic FTP/SFTP listing bytes.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNet/UltraNetFtp.h"

#include <string>
#include <vector>

namespace ultranet_internal::ftp {

    // Parses an MLSD line (RFC 3659). Returns true if the line is a valid
    // MLSD entry; the caller's `out` is populated. False (no mutation)
    // indicates "this isn't MLSD" — callers fall back to other formats.
    bool ParseMlsdLine(const std::string& line, UltraNetFtpEntry& out);

    // Parses a UNIX ls -l-style line (the LIST format most FTP servers
    // emit and what libcurl's SFTP returns). Returns true on success.
    bool ParseUnixLine(const std::string& line, UltraNetFtpEntry& out);

    // Splits a listing body into trimmed lines (handles \n and \r\n).
    std::vector<std::string> SplitLines(const std::string& body);

} // namespace ultranet_internal::ftp
