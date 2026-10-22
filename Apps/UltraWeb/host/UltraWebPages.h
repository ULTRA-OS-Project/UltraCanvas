// Apps/UltraWeb/host/UltraWebPages.h
// What the HTML reader needs from the outside world to show web pages
// (HTML::PageLoaderServices), the real ones: http(s) through UltraNet with
// TLS verification on and the browser's cookies (UltraWebCookies.h), file://
// from the disk, RFC 3986 URL resolution and charset conversion from UltraNet.
// Redirects are followed here, not by UltraNet, so that the cookies each hop
// sets are kept and the next hop gets its own. Every answer arrives on the UI
// thread, through `post`.
//
// Also the address-bar decision: which addresses are WebAssembly apps and
// which are pages.
// Version: 0.9.0 - cookies: MakePageServices takes the jar; redirects followed
//                  hop by hop
// Version: 0.8.3 - ToPageUrl: '\' is a separator only on Windows
// Version: 0.4.0 - AcceptExtensions (file uploads)
// Version: 0.3.0
// Last Modified: 2026-10-22
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "HTMLReader/HTMLPageLoader.h"

#include <memory>

#include <functional>
#include <string>
#include <vector>

class UltraNetCookieJar;

namespace UltraWeb {

// Pages larger than this are refused (HTML, style sheets and pictures).
constexpr int64_t kMaxPageBytes = 16ll * 1024 * 1024;

// Redirects followed for one fetch before it fails.
constexpr int kMaxRedirects = 20;

// `cookies` null: no cookies are sent or kept (the headless --page).
UltraCanvas::HTML::PageLoaderServices MakePageServices(std::function<void(std::function<void()>)> post,
                                                      std::shared_ptr<UltraNetCookieJar> cookies = nullptr);

// True for an address that names a WebAssembly app rather than a page:
// about: addresses, and paths and URLs ending in .wasm or .wat.
bool IsAppAddress(const std::string& address);

// The file extensions (undotted, lower case) an <input accept=""> list
// names: ".pdf,.txt" -> pdf, txt; image/*, audio/*, video/* -> their common
// extensions. Empty when it names none (any file).
std::vector<std::string> AcceptExtensions(const std::string& accept);

// A local path as a file:// URL (absolute, '/' separators, the bytes a URL
// may not carry percent-encoded). A '\' becomes a '/' only on Windows, where
// it separates folders; elsewhere it is part of a name and is encoded (%5C).
// A URL comes back as it is.
std::string ToPageUrl(const std::string& address);

} // namespace UltraWeb
