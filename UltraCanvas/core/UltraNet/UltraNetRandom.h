// core/UltraNet/UltraNetRandom.h
// Random bytes from the operating system's source, for UltraNet's own use:
// OAuth 2's PKCE verifier and state, and the key and the frame masks of
// UltraNet's own WebSocket client. Defined in UltraNetOAuth2.cpp. Internal;
// not part of the public surface.
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <cstdint>

namespace ultranet_internal {

// `n` bytes nobody can foresee: the operating system's entropy source, and
// std::random_device only where that cannot be read.
void FillRandom(uint8_t* out, std::size_t n);

} // namespace ultranet_internal
