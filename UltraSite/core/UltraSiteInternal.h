// UltraSite/core/UltraSiteInternal.h
// Helpers the two UltraSite sources share and the public header does not
// offer.
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraSite {

bool Sha256(const std::string& content, std::vector<uint8_t>& digest);
// Whether SHA-256 works in this build (libsodium); ContentHash falls back
// to FNV-1a without it.
bool HashAvailable();
// "'sha256-<base64>'", the Content-Security-Policy source of an inline
// script; empty without SHA-256.
std::string CspHash(const std::string& inlineScript);
// Whether CompressedSize compresses (zlib) or returns the raw size.
bool CompressionAvailable();

} // namespace UltraSite
