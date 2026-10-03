// Apps/UltraMail/ui/UltraMailPreferences.cpp
// Version: 0.4.0 - link_display (status-bar / tooltip)
// Version: 0.3.0 - folder_tree_width_mode (auto / fixed) and folder_tree_width (px)
// Version: 0.2.0 - remote_images, trusted_image_domains, message_view,
//                  message_text_size
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailPreferences.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include "../../../UltraCanvas/include/UltraCanvasPathUtf8.h"

namespace UltraMail {

namespace {

std::string Trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// "true"/"1"/"yes"/"on" (case-insensitive) → true; anything else → false.
bool ParseBool(const std::string& v) {
    std::string t = Trim(v);
    for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return t == "true" || t == "1" || t == "yes" || t == "on";
}

} // namespace

bool Preferences::Load(const std::string& path) {
    std::ifstream file(UltraCanvas::PathFromUtf8(path));
    if (!file.is_open()) return false;   // absent file: caller keeps defaults

    std::string line;
    while (std::getline(file, line)) {
        // Strip comments and blank lines; split on the first '='.
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';' ||
            trimmed[0] == '[') {
            continue;   // comments and any [section] headers are ignored
        }
        const std::size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        const std::string key   = Trim(trimmed.substr(0, eq));
        const std::string value = trimmed.substr(eq + 1);
        if (key == "reading_pane")       showReadingPane  = ParseBool(value);
        if (key == "fetch_sender_icons") fetchSenderIcons = ParseBool(value);
        if (key == "remote_images") {
            const std::string v = Trim(value);
            remoteImages = v == "always" ? RemoteImagePolicy::LoadAlways
                         : v == "never"  ? RemoteImagePolicy::LoadNever
                                         : RemoteImagePolicy::LoadTrusted;
        }
        if (key == "message_view") showHtml = Trim(value) != "plain";
        if (key == "message_text_size") {
            try { messageTextSize = std::clamp(std::stoi(Trim(value)), 9, 24); }
            catch (...) { /* keeps the default */ }
        }
        // The width is kept while the tree fits its names, so switching back
        // to a fixed width finds the number last chosen.
        if (key == "folder_tree_width_mode")
            folderTreeWidthMode = Trim(value) == "fixed" ? FolderTreeWidthMode::FixedWidth
                                                         : FolderTreeWidthMode::FitToText;
        if (key == "folder_tree_width") {
            try {
                folderTreeWidth = std::clamp(std::stoi(Trim(value)), kFolderTreeMinWidth,
                                             kFolderTreeMaxWidth);
            }
            catch (...) { /* keeps the default */ }
        }
        if (key == "link_display")
            linkDisplay = Trim(value) == "tooltip" ? LinkDisplay::Tooltip
                                                   : LinkDisplay::StatusBar;
        if (key == "trusted_image_domains") {
            std::size_t start = 0;
            while (start <= value.size()) {
                std::size_t comma = value.find(',', start);
                if (comma == std::string::npos) comma = value.size();
                std::string domain = NormalizeDomain(value.substr(start, comma - start));
                if (!domain.empty()) trustedImageDomains.insert(domain);
                start = comma + 1;
            }
        }
        if (key == "remote_images_from") {
            // Comma-separated addresses.
            std::size_t start = 0;
            while (start <= value.size()) {
                std::size_t comma = value.find(',', start);
                if (comma == std::string::npos) comma = value.size();
                std::string addr = Trim(value.substr(start, comma - start));
                for (char& c : addr) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (!addr.empty()) remoteImageSenders.insert(addr);
                start = comma + 1;
            }
        }
    }
    return true;
}

bool Preferences::Save(const std::string& path) const {
    std::ofstream file(UltraCanvas::PathFromUtf8(path), std::ios::trunc);
    if (!file.is_open()) return false;
    file << "# UltraMail preferences — view options remembered between runs.\n";
    file << "reading_pane = " << (showReadingPane ? "true" : "false") << "\n";
    file << "fetch_sender_icons = " << (fetchSenderIcons ? "true" : "false") << "\n";
    file << "remote_images_from = ";
    bool first = true;
    for (const auto& addr : remoteImageSenders) {
        file << (first ? "" : ", ") << addr;
        first = false;
    }
    file << "\n";
    file << "remote_images = "
         << (remoteImages == RemoteImagePolicy::LoadAlways ? "always"
             : remoteImages == RemoteImagePolicy::LoadNever ? "never" : "trusted") << "\n";
    file << "trusted_image_domains = ";
    first = true;
    for (const auto& domain : trustedImageDomains) {
        file << (first ? "" : ", ") << domain;
        first = false;
    }
    file << "\n";
    file << "message_view = " << (showHtml ? "html" : "plain") << "\n";
    file << "message_text_size = " << messageTextSize << "\n";
    file << "folder_tree_width_mode = "
         << (folderTreeWidthMode == FolderTreeWidthMode::FixedWidth ? "fixed" : "auto") << "\n";
    file << "folder_tree_width = " << folderTreeWidth << "\n";
    file << "link_display = "
         << (linkDisplay == LinkDisplay::Tooltip ? "tooltip" : "status-bar") << "\n";
    return static_cast<bool>(file);
}

std::string Preferences::NormalizeDomain(const std::string& text) {
    std::string d = Trim(text);
    for (char& c : d) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (std::size_t scheme = d.find("://"); scheme != std::string::npos) d = d.substr(scheme + 3);
    else if (d.rfind("//", 0) == 0) d = d.substr(2);   // protocol-relative "//cdn.example.com/x"
    // user@host in a URL, or an address: the part after the '@', before any path.
    if (std::size_t at = d.rfind('@'); at != std::string::npos) d = d.substr(at + 1);
    if (std::size_t cut = d.find_first_of("/?#:"); cut != std::string::npos) d = d.substr(0, cut);
    if (d.rfind("*.", 0) == 0) d = d.substr(2);
    if (d.rfind("www.", 0) == 0) d = d.substr(4);
    while (!d.empty() && d.front() == '.') d.erase(d.begin());
    while (!d.empty() && d.back() == '.') d.pop_back();
    if (d.find('.') == std::string::npos) return std::string();   // not a domain
    for (char c : d) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.' ||
              static_cast<unsigned char>(c) >= 0x80))
            return std::string();
    }
    return d;
}

bool Preferences::DomainMatches(const std::string& host, const std::string& domain) {
    if (host.empty() || domain.empty() || host.size() < domain.size()) return false;
    if (host.compare(host.size() - domain.size(), domain.size(), domain) != 0) return false;
    return host.size() == domain.size() || host[host.size() - domain.size() - 1] == '.';
}

bool Preferences::IsTrustedDomain(const std::string& host) const {
    const std::string h = NormalizeDomain(host);
    for (const auto& domain : trustedImageDomains)
        if (DomainMatches(h, domain)) return true;
    return false;
}

} // namespace UltraMail
