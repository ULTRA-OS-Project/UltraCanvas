// Apps/UltraWeb/host/UltraWebPages.h
// What the HTML reader needs from the outside world to show web pages
// (HTML::PageLoaderServices), the real ones: http(s) through UltraNet with
// TLS verification on and no cookies, file:// from the disk, RFC 3986 URL
// resolution and charset conversion from UltraNet. Every answer arrives on
// the UI thread, through `post`.
//
// Also the address-bar decision: which addresses are WebAssembly apps and
// which are pages.
// Version: 0.4.0 - AcceptExtensions (file uploads)
// Version: 0.3.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "HTMLReader/HTMLPageLoader.h"

#include <functional>
#include <string>
#include <vector>

namespace UltraWeb {

// Pages larger than this are refused (HTML, style sheets and pictures).
constexpr int64_t kMaxPageBytes = 16ll * 1024 * 1024;

UltraCanvas::HTML::PageLoaderServices MakePageServices(std::function<void(std::function<void()>)> post);

// True for an address that names a WebAssembly app rather than a page:
// about: addresses, and paths and URLs ending in .wasm or .wat.
bool IsAppAddress(const std::string& address);

// The file extensions (undotted, lower case) an <input accept=""> list
// names: ".pdf,.txt" -> pdf, txt; image/*, audio/*, video/* -> their common
// extensions. Empty when it names none (any file).
std::vector<std::string> AcceptExtensions(const std::string& accept);

// A local path as a file:// URL (absolute, '/' separators, the bytes a URL
// may not carry percent-encoded). A URL comes back as it is.
std::string ToPageUrl(const std::string& address);

} // namespace UltraWeb
