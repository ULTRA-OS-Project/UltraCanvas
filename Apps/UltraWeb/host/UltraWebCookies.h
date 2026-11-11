// Apps/UltraWeb/host/UltraWebCookies.h
// UltraWeb's cookies: the one UltraNetCookieJar (UltraNet/UltraNetCookieJar.h,
// which holds the rules) that every tab and window of the process shares,
// kept in <settings folder>/UltraWeb/cookies.txt.
// Version: 0.2.0 - the jar moved to UltraNet (UltraNetCookieJar); this is
//                  where UltraWeb keeps its own
// Version: 0.1.0
// Last Modified: 2026-10-22
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNet/UltraNetCookieJar.h"

#include <memory>

namespace UltraWeb {

// In memory only when there is no settings folder.
std::shared_ptr<UltraNetCookieJar> SharedCookieJar();

} // namespace UltraWeb
