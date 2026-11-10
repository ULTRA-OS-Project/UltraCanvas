// Apps/UltraStoreAdmin/core/PageFrame.cpp
// The frame of the pages the publish folder carries (PageFrame.h).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "PageFrame.h"

#include "UltraCanvasConfig.h"
#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace UltraStoreAdmin {

namespace fs = std::filesystem;

// Shared with Server/UltraStorePay/ultrastore_pay/pages.py (STYLESHEET).
const char* const kPageStylesheet = R"css(:root{--bg:#f5f6f8;--surface:#fff;--border:#e2e5ea;--text:#181c23;--muted:#68707d;--accent:#2563eb;--accent-hover:#1d4ed8;--ok:#16a34a}
@media (prefers-color-scheme:dark){:root{--bg:#111418;--surface:#1a1f27;--border:#2a303a;--text:#e6e8ec;--muted:#9aa3b2;--accent:#3b82f6;--accent-hover:#60a5fa}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
.wrap{max-width:38em;margin:0 auto;padding:1.5em 1em 3em}
header{display:flex;align-items:center;gap:.75em;padding:.25em 0 1em}
header img{height:40px;width:auto}
header .brand{font-weight:600;letter-spacing:.02em}
header .brand small{display:block;font-weight:400;color:var(--muted);font-size:.8em}
main{background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:1.5em 1.75em}
h1{font-size:1.4em;margin:0 0 .5em}
p{margin:.6em 0}
code{font-size:1.3em;background:var(--bg);border:1px solid var(--border);padding:.15em .5em;border-radius:.3em}
.amount{font-size:1.6em;font-weight:600;margin:.4em 0}
.muted{color:var(--muted)}
.ok{color:var(--ok)}
.button,button{display:inline-block;background:var(--accent);color:#fff;border:0;padding:.6em 1.2em;border-radius:.5em;font-size:1em;text-decoration:none;cursor:pointer}
.button:hover,button:hover{background:var(--accent-hover)}
table.receipt{border-collapse:collapse;width:100%;margin:.5em 0}
table.receipt th{text-align:left;font-weight:500;color:var(--muted);padding:.3em 1em .3em 0;white-space:nowrap;vertical-align:top}
table.receipt td{padding:.3em 0}
table.receipt tr.total td,table.receipt tr.total th{border-top:1px solid var(--border);padding-top:.6em;font-weight:600;color:var(--text)}
footer{padding:1em .25em;color:var(--muted);font-size:.85em}
footer a{color:inherit}
)css";

namespace {

std::string Escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

} // namespace

std::string FramePage(const std::string& title, const std::string& heading, const std::string& bodyHtml,
                      const std::string& mediaPrefix, const std::string& footer) {
    return "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           "<title>" + Escape(title) + " - UltraStore</title>\n"
           "<style>" + std::string(kPageStylesheet) + "</style>\n</head>\n<body>\n<div class=\"wrap\">\n"
           "<header><picture><source srcset=\"" + Escape(mediaPrefix) + kLogoDarkFile + "\" media=\"(prefers-color-scheme: dark)\">"
           "<img src=\"" + Escape(mediaPrefix) + kLogoLightFile + "\" alt=\"ULTRA OS\" width=\"77\" height=\"40\"></picture>"
           "<div class=\"brand\">UltraStore<small>ULTRA OS</small></div></header>\n"
           "<main>\n<h1>" + Escape(heading) + "</h1>\n" + bodyHtml + "</main>\n"
           "<footer>" + footer + "</footer>\n</div>\n</body>\n</html>\n";
}

UltraStore::Result<void> CopyPageMarks(const std::string& outDirUtf8) {
    using UltraStore::ErrorCode;
    using UltraStore::Result;
    std::error_code ec;
    const fs::path icons = UltraCanvas::PathFromUtf8(UltraCanvas::GetResourcesDir()) / "media" / "icons" / "desktop";
    const fs::path media = UltraCanvas::PathFromUtf8(outDirUtf8) / "media";
    for (const char* name : {kLogoLightFile, kLogoDarkFile}) {
        const fs::path from = icons / name;
        if (!fs::is_regular_file(from, ec)) continue;
        fs::create_directories(media, ec);
        if (ec) return Result<void>::Failure(ErrorCode::IoError, "cannot create " + UltraCanvas::PathToUtf8(media), ec.message());
        const fs::path to = media / name;
        if (fs::exists(to, ec) && fs::file_size(to, ec) == fs::file_size(from, ec)) continue;
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
        if (ec) return Result<void>::Failure(ErrorCode::IoError, "cannot copy " + UltraCanvas::PathToUtf8(from), ec.message());
    }
    return Result<void>::Succeed();
}

} // namespace UltraStoreAdmin
