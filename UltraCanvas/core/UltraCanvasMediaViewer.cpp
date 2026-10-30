// core/UltraCanvasMediaViewer.cpp
// Implementation of the comprehensive media / photo / document viewer widget.
// See UltraCanvasMediaViewer.h for the feature overview.
// Version: 1.18.0 - saved web pages (.mht / .mhtml) shown as the page, from their own
//                   parts; Apple Mail messages (.emlx) as messages
// Version: 1.17.0 - e-mail messages (.eml) are shown as the message: its header, its
//                   body rendered (embedded pictures shown, remote ones never fetched)
//                   and its attachments listed
// Version: 1.16.0 - HTML / XHTML files, and XML that holds XHTML, are shown rendered
//                   in a web view (local files only, web links to the browser), not
//                   as their source - the Texter shows the source
// Version: 1.15.0 - SetSettingsAction: a gear in the info bar for the host's settings
// Version: 1.14.0 - Unlock's passwords are forgotten, and an unlocked file locked again,
//                   when the session locks or the window has been idle (ten minutes by
//                   default); an unlocked file says "unlocked" in the info bar and Details
// Version: 1.13.0 - Unlock's passwords are kept for the folder being browsed and tried,
//                   without asking, on each protected file shown there
// Version: 1.12.1 - the viewer's notes - a *.ucd summary, a locked PDF's - are in its dark
//                   colours, laid out like Details (noteView), not on a white text page
// Version: 1.12.0 - password-protected files: a layered *.ucd image or a PDF that needs
//                   its password shows an Unlock button (info bar and toolbar, Enter)
//                   that asks for it - again when it is not right - and then shows it
//                   whole; a PDF that fails to open no longer leaves the last one shown;
//                   a PDF opens in the PDF view again, not as a vector drawing (ClassifyFile)
// Version: 1.11.0 - a layered *.ucd image (UCBitmap) is shown whole: its layers composited at
//                   full size (UCRasterDocument::LoadUCD), not the 256 px thumbnail; a
//                   password-protected one shows its thumbnail and the details say why
// Version: 1.10.0 - one compact toolbar row: flat buttons, 16 px icons, scrolls
//                   when the window is too narrow; the Details button lives in
//                   the info bar only
// Version: 1.9.1 - *.ucd: header and thumbnail through the UCD container engine; the
//                  encryption line names the key source the spec defines
// Version: 1.9.0
// Last Modified: 2026-10-19
// V1.9.0: Save image as offers FileLoader's types - every bitmap type this
//   build writes - instead of six of its own; and another size in a bitmap
//   type's Settings - the
//   picture's own, or for an SVG the size it is drawn at, a drawing having no
//   pixels of its own - and writes with the type's save settings at the size
//   chosen; a drawing is drawn again at that size, so it stays sharp.
// V1.8.0: IsPlayingMedia; images are read with UCImage::GetFresh, so a
//   picture saved over since it was last shown opens as it is now.
// V1.7.1: Save image as offers a name it can write - an SVG's stem as a PNG
//   instead of "<name>.svg", which libvips has no writer for - gives a typed
//   name without a written format ".png", and reports a failed save in a
//   message rather than only in the info bar.
// V1.6.0: Vector documents no renderer here can rasterize (Xara, CorelDRAW,
//   EPS/PostScript) are shown from the preview bitmap they carry inside
//   themselves, the way a *.ucd container is - so a file manager's detail pane
//   shows the drawing instead of folding away.
// V1.5.0: CloseFile() shows nothing and lets go of the file - playback stops and
//   every display backend releases its document, so a host preview pane can move,
//   rename or delete the file it was just showing. StopPlayback() alone never
//   released it, which made a move of the previewed file fail on Windows.
// V1.4.1: The PreviewClip ("5 s clip") video preview is silent for real. The
//   mute is applied before the source is opened (so the engine builds a muted
//   session instead of muting one that may already be wired for sound), and the
//   clip now stays muted when it pauses — the sound returns on the user's own
//   resume, not at a pause the backend may still be deferring.
// Author: UltraCanvas Framework

#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8
#include "UltraCanvasFontViewer.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasBreadcrumb.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "../dialogs/UltraCanvasCurvesDialog.h"  // Curves (tone curve) editing window
#include "UltraCanvasColorSwatchBar.h"  // backdrop palette under transparent images
#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"     // GetResourcesDir, for the toolbar icons
#include "UltraCanvasFileLoader.h"   // FileDialogOptions, DialogResult, FileFilter
#include "UltraCanvasSpreadsheet.h"  // ODS / CSV / TSV (always built into the core lib)
#include "Models/STL/UltraCanvasSTLElement.h"  // STL 3D viewer (GL or 2D fallback)
#include "UltraCanvasModelPreview.h"    // which 3D formats this build reads
#include "UltraCanvasTextArea.h"      // text / source / markdown view
#include "UltraCanvasWebView.h"       // HTML pages, rendered
#include "UltraCanvasUtils.h"         // OpenURL, for a page's web links
#ifdef ULTRACANVAS_HAS_NET
#include "UltraNet/UltraNetMime.h"    // an .eml read as a message
#endif
#include "UltraCanvasLanguageDetect.h" // the language of a .txt
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h" // .tex → rich document → markdown
#include "UltraCanvasSyntaxTokenizer.h" // resolve source language from extension
#include "UltraCanvasEBookViewer.h"   // EPUB / FB2 / MOBI e-book view
#include "UltraCanvasEmbeddedPreview.h" // preview bitmap inside a vector document
#include "UltraCanvasUCDContainer.h"   // a *.ucd's header and thumbnail
#include "UltraCanvasRasterDocument.h"  // ... and a layered image's layers
#include "UltraCrypt/UltraCryptCore.h"   // Unlock's passwords, kept zeroed-on-free
#include "miniz.h"                      // which reach the image surface as a PNG
#include "UltraCanvasSupportedFormats.h" // what the image pipeline can rasterize
#include "UltraCanvasVectorPreview.h"   // the readers a registered plugin lends core
#include "UltraCanvasVectorElement.h"   // the view a drawing is shown in
#include "UltraCanvasVectorRaster.h"     // what a registered plugin can draw
#include "UltraCanvasGraphicsPluginSystem.h" // LoadGraphicsFile, for a plugin view
#include "Documents/eBook/TXTEngine.h" // RegisterBuiltinEBookEngines (idempotent)
#ifdef ULTRACANVAS_PLUGIN_PDF
#include "Plugins/Documents/UltraCanvasPDFView.h"
#endif
#ifdef ULTRACANVAS_ENABLE_VIDEO
#include "UltraCanvasVideoPlayerElement.h"
#endif
#ifdef ULTRACANVAS_ENABLE_AUDIO
#include "UltraCanvasAudioPlayerElement.h"
#endif
// Always: file classification asks the codec registry what this build can
// play, and with neither backend compiled in it correctly answers "nothing".
#include "UltraCanvasMediaCodecRegistry.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace UltraCanvas {

static constexpr double kHalfPi = 1.5707963267948966;

// ===========================================================================
// FILE-LOCAL HELPERS
// ===========================================================================

static std::string LowerExt(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return ext;
}

static std::string LowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return text;
}

static std::string BaseName(const std::string& path) {
    std::error_code ec;
    fs::path p(UltraCanvas::PathFromUtf8(path));
    return PathToUtf8(p.filename());
}

// ----- Save as -----
// The formats Save writes are FileLoader's: every bitmap type this build
// writes (UltraCanvasFileLoader::AddImageSaveTypes / CanSaveImage), PNG
// first - not a list of the viewer's own. The image writer picks the format
// from the name's extension.
static bool HasSaveExtension(const std::string& path) {
    const std::string ext = LowerExt(BaseName(path));
    return !ext.empty() && UltraCanvasFileLoader::CanSaveImage(ext);
}

// The name Save offers for the file being shown: its own name when that is a
// format Save writes, else its stem as a PNG. An SVG is rasterized for display
// and cannot be written back as one - libvips has no SVG writer - so offering
// "drawing.svg" made Save fail on the name it suggested.
static std::string SaveNameFor(const std::string& path) {
    if (path.empty()) return "image.png";
    if (HasSaveExtension(path)) return BaseName(path);
    const std::string stem = PathToUtf8(PathFromUtf8(path).stem());
    return (stem.empty() ? std::string("image") : stem) + ".png";
}

// `path` named so Save can write it: kept when its extension is a format Save
// writes, else given ".png". Added rather than swapped in, because what follows
// the last dot is not always a format ("notes.v2"), and a name the user typed
// is not shortened behind their back.
static std::string WithSaveExtension(const std::string& path) {
    if (HasSaveExtension(path)) return path;
    std::string named = path;
    if (!named.empty() && named.back() == '.') named.pop_back();
    return named + ".png";
}

// ----- Toolbar icons -----
// The toolbars are icons rather than captions: an icon-only row fits the
// narrow pane UltraFiler gives the viewer, and a picture is read before a word.
// The caption moves to the tooltip, so what a button does is still one hover
// away. Icons come from the framework's own set under media/icons/; the
// toolbar draws them as masks, so they take the toolbar's foreground colour
// and grey out with the button.
static std::string ViewerIconPath(const std::string& name) {
    return NormalizePath(GetResourcesDir() + "media/icons/" + name);
}

// Finish an icon-only toolbar button: no text, so no gap reserved for one,
// and the caption as the tooltip.
static std::shared_ptr<UltraCanvasButton> IconOnly(std::shared_ptr<UltraCanvasButton> b,
                                            const std::string& tooltip) {
    if (b) {
        b->SetIconSpacing(0);
        b->SetTooltip(tooltip);
    }
    return b;
}

// The image's error message, cut to what a person can use: libvips reports a
// decode failure as several lines ("<file>: bad seek to 25996", then
// "heif: Unsupported feature: Unsupported codec"), behind the framework's own
// prefixes. The last line names the actual cause.
static std::string DecodeFailureReason(const std::string& error) {
    std::string reason;
    size_t start = 0;
    while (start < error.size()) {
        size_t end = error.find('\n', start);
        if (end == std::string::npos) end = error.size();
        std::string line = error.substr(start, end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) reason = line;
        start = end + 1;
    }
    for (const char* prefix : { "Failed to make pixmap Err:", "Failed to decode image pixels: " }) {
        if (reason.rfind(prefix, 0) == 0) reason.erase(0, std::strlen(prefix));
    }
    return reason.empty() ? std::string("unknown error") : reason;
}

static std::string HumanSize(uintmax_t bytes) {
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    char buf[48];
    if (u == 0) snprintf(buf, sizeof(buf), "%llu %s", (unsigned long long)bytes, units[u]);
    else        snprintf(buf, sizeof(buf), "%.1f %s", v, units[u]);
    return std::string(buf);
}

// "95.9866" -> "95.99", "300.0" -> "300".
static std::string ShortNumber(double v) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%.2f", v);
    std::string s(buf);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

// The image's resolution for the Details panel: "300 dpi", "72 x 96 dpi".
// Empty when the file stores none: libvips then reports 1 pixel per mm,
// which comes out as 25.4 dpi and describes nothing.
static std::string ResolutionText(double dpiX, double dpiY) {
    if (dpiX <= 0 && dpiY <= 0) return "";
    if (std::fabs(dpiX - 25.4) < 0.001 && std::fabs(dpiY - 25.4) < 0.001) return "";
    const std::string x = ShortNumber(dpiX), y = ShortNumber(dpiY);
    return (x == y ? x : x + " x " + y) + " dpi";
}

// Read a text file into `out`, capped at maxBytes so a huge/binary file can't
// stall the viewer. Returns false if the file can't be opened.
static bool ReadTextFile(const std::string& path, std::string& out,
                         size_t maxBytes = 16u * 1024u * 1024u) {
    std::ifstream f(UltraCanvas::PathFromUtf8(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (out.size() > maxBytes) {
        out.resize(maxBytes);
        out += "\n\n[... truncated ...]";
    }
    return true;
}

// ----- HTML PAGES (the web view's local-file services) -----
// The viewer renders an HTML page from disk through UltraCanvasWebView, which
// reads pages by URL. These turn a path into a file:// URL and back, read a
// link against the page it is on, and answer the view's fetches from the
// disk - local files only: the viewer has no network, and a web link goes to
// the browser instead.

// A path as a file:// URL: absolute, '/' separated, every byte but the
// unreserved ones percent-escaped (so any file name survives the trip).
static std::string FileUrlFromPath(const std::string& path) {
    std::error_code ec;
    std::filesystem::path p = std::filesystem::absolute(UltraCanvas::PathFromUtf8(path), ec);
    if (ec) p = UltraCanvas::PathFromUtf8(path);
    std::string text = UltraCanvas::PathToUtf8(p.lexically_normal());
#ifdef _WIN32
    std::replace(text.begin(), text.end(), '\\', '/');
#endif
    if (text.empty() || text[0] != '/') text = "/" + text;        // C:/x -> /C:/x
    std::string url = "file://";
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == ':') {
            url += static_cast<char>(c);
        } else {
            char escaped[4];
            std::snprintf(escaped, sizeof(escaped), "%%%02X", c);
            url += escaped;
        }
    }
    return url;
}

// %XX escapes undone (a URL's path, a cid: reference).
static std::string PercentDecode(const std::string& text) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && hex(text[i + 1]) >= 0 && hex(text[i + 2]) >= 0) {
            out += static_cast<char>(hex(text[i + 1]) * 16 + hex(text[i + 2]));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

// A file:// URL's path: query and fragment cut, percent-escapes undone.
static std::string PathFromFileUrl(const std::string& url) {
    std::string raw = url.substr(7);
    const size_t cut = raw.find_first_of("?#");
    if (cut != std::string::npos) raw.resize(cut);
    // file://host/path: the host part is not a folder.
    if (!raw.empty() && raw[0] != '/') {
        const size_t slash = raw.find('/');
        raw = slash == std::string::npos ? std::string("/") : raw.substr(slash);
    }
    std::string path = PercentDecode(raw);
#ifdef _WIN32
    if (path.size() >= 3 && path[0] == '/' && path[2] == ':') path.erase(0, 1);   // /C:/x -> C:/x
#endif
    return path;
}

// `ref` read against `base` (RFC 3986 reference resolution) - a file://
// URL, or the web address a saved page (.mht) came from. A reference with a
// scheme of its own stands as it is.
static bool ResolveUrl(const std::string& base, const std::string& ref, std::string& out) {
    const size_t colon = ref.find(':');
    if (colon != std::string::npos && colon > 1 && std::isalpha(static_cast<unsigned char>(ref[0])) &&
        std::all_of(ref.begin(), ref.begin() + static_cast<std::ptrdiff_t>(colon), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.';
        })) {
        out = ref;
        return true;
    }
    const size_t schemeEnd = base.find("://");
    if (schemeEnd == std::string::npos) return false;
    const std::string document = base.substr(0, base.find('#'));
    if (ref.empty()) { out = document; return true; }
    if (ref[0] == '#') { out = document + ref; return true; }
    if (ref.size() > 1 && ref[0] == '/' && ref[1] == '/') { out = base.substr(0, schemeEnd + 1) + ref; return true; }
    // "file://" or "https://host", then the path ("/" when there is none).
    const std::string withoutQuery = document.substr(0, document.find('?'));
    const size_t pathAt = withoutQuery.find('/', schemeEnd + 3);
    const std::string origin = withoutQuery.substr(0, pathAt);
    const std::string basePath = pathAt == std::string::npos ? std::string("/") : withoutQuery.substr(pathAt);
    if (ref[0] == '?') { out = origin + basePath + ref; return true; }

    const size_t tailAt = ref.find_first_of("?#");
    const std::string refPath = ref.substr(0, tailAt);
    const std::string tail = tailAt == std::string::npos ? std::string() : ref.substr(tailAt);
    std::string merged = refPath[0] == '/' ? refPath
                                           : basePath.substr(0, basePath.rfind('/') + 1) + refPath;
    if (merged[0] != '/') merged.insert(merged.begin(), '/');
    // The dot segments removed: "a/./b" is "a/b", "a/b/../c" is "a/c".
    std::vector<std::string> segments;
    size_t start = 1;
    bool trailingSlash = false;
    while (start <= merged.size()) {
        size_t end = merged.find('/', start);
        if (end == std::string::npos) end = merged.size();
        const std::string segment = merged.substr(start, end - start);
        const bool last = end == merged.size();
        if (segment == "..") {
            if (!segments.empty()) segments.pop_back();
            trailingSlash = last;
        } else if (segment == ".") {
            trailingSlash = last;
        } else {
            segments.push_back(segment);
            trailingSlash = false;
        }
        start = end + 1;
    }
    std::string path;
    for (const auto& segment : segments) path += "/" + segment;
    if (path.empty() || trailingSlash) path += "/";
    out = origin + path + tail;
    return true;
}

// Whether the text starts as an XHTML document does: past the XML
// declaration, processing instructions and comments, an HTML DOCTYPE or an
// <html> root element (with or without a namespace prefix).
static bool LooksLikeXhtml(const std::string& text) {
    const std::string lower = LowerAscii(text);
    size_t i = 0;
    if (lower.compare(0, 3, "\xEF\xBB\xBF") == 0) i = 3;    // UTF-8 BOM
    while (i < lower.size()) {
        while (i < lower.size() && std::isspace(static_cast<unsigned char>(lower[i]))) ++i;
        if (i >= lower.size() || lower[i] != '<') return false;
        if (lower.compare(i, 2, "<?") == 0) {
            const size_t end = lower.find("?>", i + 2);
            if (end == std::string::npos) return false;
            i = end + 2;
        } else if (lower.compare(i, 4, "<!--") == 0) {
            const size_t end = lower.find("-->", i + 4);
            if (end == std::string::npos) return false;
            i = end + 3;
        } else if (lower.compare(i, 9, "<!doctype") == 0) {
            const size_t end = lower.find('>', i);
            const std::string doctype = lower.substr(i + 9, end == std::string::npos ? std::string::npos
                                                                                      : end - i - 9);
            // "<!DOCTYPE html ..." - HTML5 or the XHTML 1.x DTDs.
            size_t n = 0;
            while (n < doctype.size() && std::isspace(static_cast<unsigned char>(doctype[n]))) ++n;
            return doctype.compare(n, 4, "html") == 0 &&
                   (n + 4 == doctype.size() || !std::isalnum(static_cast<unsigned char>(doctype[n + 4])));
        } else {
            size_t end = i + 1;
            while (end < lower.size() && !std::isspace(static_cast<unsigned char>(lower[end])) &&
                   lower[end] != '>' && lower[end] != '/') ++end;
            std::string name = lower.substr(i + 1, end - i - 1);
            const size_t prefix = name.find(':');
            if (prefix != std::string::npos) name.erase(0, prefix + 1);
            return name == "html";
        }
    }
    return false;
}

// The first `maxBytes` of a file ("" when it cannot be read).
static std::string ReadFileHead(const std::string& path, size_t maxBytes) {
    std::ifstream f(UltraCanvas::PathFromUtf8(path), std::ios::binary);
    if (!f) return std::string();
    std::string head(maxBytes, '\0');
    f.read(&head[0], static_cast<std::streamsize>(maxBytes));
    head.resize(static_cast<size_t>(f.gcount()));
    return head;
}

#ifdef ULTRACANVAS_HAS_NET
// ----- E-MAIL MESSAGES -----
// An .eml is shown as a mail program shows it: one page with the message's
// header (subject, from, to, cc, date), its body - the HTML one rendered,
// else the text - and its attachments listed. Pictures the message carries
// (cid: references) are put in as data: URLs; a remote picture is never
// fetched (the preview reads local files only), so opening a message tells
// its sender nothing.

static std::string DataUrl(const UltraNetMimeAttachmentView& part) {
    return "data:" + (part.mediaType.empty() ? std::string("application/octet-stream") : part.mediaType) +
           ";base64," + UltraNet_Base64Encode(part.data, false);
}

// The body's cid: references replaced by the parts they name (RFC 2392: a
// Content-ID, percent-escaped). `used` collects the Content-IDs shown.
static std::string InlineContentIds(const std::string& html,
                                    const std::vector<UltraNetMimeAttachmentView>& parts,
                                    std::set<std::string>& used) {
    const std::string lower = LowerAscii(html);
    std::string out;
    out.reserve(html.size());
    size_t i = 0;
    for (size_t at = lower.find("cid:"); at != std::string::npos; at = lower.find("cid:", i)) {
        size_t end = at + 4;
        while (end < html.size() && html[end] != '"' && html[end] != '\'' && html[end] != ')' &&
               html[end] != '>' && !std::isspace(static_cast<unsigned char>(html[end]))) ++end;
        const std::string id = LowerAscii(PercentDecode(html.substr(at + 4, end - at - 4)));
        out.append(html, i, at - i);
        const UltraNetMimeAttachmentView* named = nullptr;
        for (const auto& part : parts) {
            if (!part.contentId.empty() && LowerAscii(part.contentId) == id) { named = &part; break; }
        }
        if (named) {
            out += DataUrl(*named);
            used.insert(id);
        } else {
            out.append(html, at, end - at);
        }
        i = end;
    }
    out.append(html, i, std::string::npos);
    return out;
}

// The page an .eml is shown as.
static std::string MailMessageAsHtml(const std::string& raw) {
    UltraNetMimeMessage message;
    if (!UltraNet_MimeParse(raw, message)) {
        return "<!DOCTYPE html><html><head><title>Message</title></head><body>"
               "<pre style=\"white-space:pre-wrap\">" + HTML::EscapeText(raw) + "</pre></body></html>";
    }
    std::vector<UltraNetMimeAttachmentView> parts;
    UltraNet_MimeCollectAttachments(message, parts, /*includeInline=*/true);

    auto join = [](const std::vector<std::string>& list) {
        std::string text;
        for (const auto& item : list) text += (text.empty() ? "" : ", ") + item;
        return text;
    };
    auto row = [](const char* label, const std::string& value) {
        if (value.empty()) return std::string();
        return "<tr><td style=\"color:#666;padding:1px 12px 1px 0;vertical-align:top\">" + std::string(label) +
               "</td><td style=\"padding:1px 0\">" + HTML::EscapeText(value) + "</td></tr>";
    };
    const std::string subject = message.subject.empty() ? std::string("(no subject)") : message.subject;

    std::string body;
    bool isHtml = false;
    std::set<std::string> used;
    if (UltraNet_MimeGetDisplayBody(message, body, isHtml)) {
        if (isHtml) body = InlineContentIds(body, parts, used);
        else body = "<pre style=\"white-space:pre-wrap;font-family:sans-serif;margin:0\">" +
                    HTML::EscapeText(body) + "</pre>";
    }

    // What the body did not show: pictures sent along inline (Apple Mail
    // puts them after the text), then everything else, listed.
    std::string pictures, attachments;
    for (const auto& part : parts) {
        if (!part.contentId.empty() && used.count(LowerAscii(part.contentId))) continue;
        if (part.isInline && part.mediaType.rfind("image/", 0) == 0) {
            pictures += "<p><img src=\"" + DataUrl(part) + "\" style=\"max-width:100%\"></p>";
            continue;
        }
        const std::string name = part.filename.empty() ? std::string("(unnamed)") : part.filename;
        attachments += "<li>" + HTML::EscapeText(name) + " <span style=\"color:#666\">(" +
                       HTML::EscapeText(HumanSize(part.data.size())) + ")</span></li>";
    }

    std::string html =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>" + HTML::EscapeText(subject) +
        "</title></head><body style=\"margin:0\">"
        "<div style=\"background:#f3f4f6;border-bottom:1px solid #d0d4da;padding:12px 16px;"
        "font-family:sans-serif;font-size:13px;color:#222\">"
        "<div style=\"font-size:17px;font-weight:bold;margin-bottom:6px\">" + HTML::EscapeText(subject) + "</div>"
        "<table style=\"border-collapse:collapse\">" +
        row("From", message.from) + row("To", join(message.to)) + row("Cc", join(message.cc)) +
        row("Date", message.date) + "</table>";
    if (!attachments.empty()) {
        html += "<div style=\"margin-top:6px;color:#666\">Attachments</div>"
                "<ul style=\"margin:2px 0 0 0\">" + attachments + "</ul>";
    }
    // The message's own HTML follows as it is: the parser takes a second
    // <html> / <body> inside this one, and its <style> rules still apply.
    html += "</div><div style=\"padding:12px 16px\">" + body + pictures + "</div></body></html>";
    return html;
}

// An Apple Mail .emlx: a line with the message's length in bytes, the
// message, then Mail's own property list - the message is what is shown.
static std::string MessageOfEmlx(const std::string& raw) {
    const size_t lineEnd = raw.find('\n');
    if (lineEnd == std::string::npos) return raw;
    const std::string count = raw.substr(0, lineEnd);
    size_t length = 0;
    bool digits = false;
    for (char c : count) {
        if (std::isdigit(static_cast<unsigned char>(c))) { length = length * 10 + static_cast<size_t>(c - '0'); digits = true; }
        else if (!std::isspace(static_cast<unsigned char>(c))) return raw;   // not an .emlx after all
        if (length > raw.size()) break;
    }
    if (!digits) return raw;
    return raw.substr(lineEnd + 1, length);
}

// ----- SAVED WEB PAGES (.mht / .mhtml) -----
// A web page saved with everything it shows: its HTML, style sheets and
// pictures as the parts of one MIME message, each under the address it was
// saved from (Content-Location) or a Content-ID. The page is shown as it
// was: its links read against the address it came from, and what it asks
// for answered from those parts - the network is never asked.
static std::string HeaderOf(const UltraNetMimePart& part, const std::string& name) {
    for (const auto& [key, value] : part.headers) {
        if (LowerAscii(key) != LowerAscii(name)) continue;
        size_t b = 0, e = value.size();
        while (b < e && std::isspace(static_cast<unsigned char>(value[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(value[e - 1]))) --e;
        return value.substr(b, e - b);
    }
    return std::string();
}

static void CollectLeaves(const UltraNetMimePart& part, std::vector<const UltraNetMimePart*>& out) {
    if (!part.isMultipart) { out.push_back(&part); return; }
    for (const auto& child : part.children) CollectLeaves(child, out);
}

// The page a saved web page is shown as; `parts` gets what it refers to, by
// absolute address without a fragment.
static std::string WebArchiveAsHtml(const std::string& raw, const std::string& fileUrl,
                                    std::map<std::string, HTML::FetchResult>& parts) {
    UltraNetMimeMessage message;
    if (!UltraNet_MimeParse(raw, message)) {
        return "<!DOCTYPE html><html><head><title>Web archive</title></head><body>"
               "<pre style=\"white-space:pre-wrap\">" + HTML::EscapeText(raw) + "</pre></body></html>";
    }
    std::vector<const UltraNetMimePart*> leaves;
    CollectLeaves(message.root, leaves);
    // The page: the first HTML part (the archive's start). An archive with
    // none is shown as the message it also is.
    const UltraNetMimePart* page = nullptr;
    for (const auto* leaf : leaves) {
        if (leaf->mediaType == "text/html") { page = leaf; break; }
    }
    if (!page) return MailMessageAsHtml(raw);

    std::string html = UltraNet_ConvertToUtf8(std::string(page->body.begin(), page->body.end()), page->charset);
    std::string location = HeaderOf(*page, "Content-Location");
    if (location.empty()) location = HeaderOf(message.root, "Content-Location");
    std::string base = fileUrl;
    if (!location.empty()) ResolveUrl(fileUrl, location, base);

    std::vector<UltraNetMimeAttachmentView> byContentId;
    for (const auto* leaf : leaves) {
        if (leaf == page) continue;
        if (!leaf->contentId.empty()) {
            UltraNetMimeAttachmentView view;
            view.mediaType = leaf->mediaType;
            view.contentId = leaf->contentId;
            view.data = leaf->body;
            byContentId.push_back(std::move(view));
        }
        const std::string saved = HeaderOf(*leaf, "Content-Location");
        std::string address;
        if (saved.empty() || !ResolveUrl(base, saved, address)) continue;
        HTML::FetchResult part;
        part.ok = true;
        part.status = 200;
        part.finalUrl = address;
        part.contentType = leaf->mediaType + (leaf->charset.empty() ? std::string() : "; charset=" + leaf->charset);
        part.body = leaf->body;
        parts[address.substr(0, address.find('#'))] = std::move(part);
    }
    std::set<std::string> used;
    html = InlineContentIds(html, byContentId, used);
    // Its links and pictures read against where it was saved from (a
    // <base> of the page's own comes first and wins).
    if (base != fileUrl) html = "<base href=\"" + HTML::EscapeText(base) + "\">" + html;
    return html;
}
#endif

// The largest page or resource the preview reads (PageLoaderLimits'
// maxDocumentBytes): a bigger file is refused before it is read. A message
// or a saved web page may be larger - attachments are listed, not put in
// the page, and a saved page's pictures are served on their own.
static constexpr uintmax_t kMaxPageFileBytes = 16u * 1024u * 1024u;
static constexpr uintmax_t kMaxMailFileBytes = 64u * 1024u * 1024u;

// The parts of the saved web page on show (.mht), by the address each was
// saved from: what its style sheets and pictures are answered with.
struct MediaViewerWebArchive {
    std::map<std::string, HTML::FetchResult> parts;
};

// Files read as MIME messages: e-mail and saved web pages.
static bool IsMimeFileExtension(const std::string& ext) {
    return ext == "eml" || ext == "emlx" || ext == "mht" || ext == "mhtml";
}

// What the web view asks the viewer for: a page, or one of its style sheets
// or pictures - read from the disk, file:// only. A saved web page's parts
// go to `archive`.
static HTML::FetchResult FetchLocalFile(const std::string& url, MediaViewerWebArchive* archive) {
    HTML::FetchResult result;
    if (url.size() < 7 || LowerAscii(url.substr(0, 7)) != "file://") {
        result.error = "The viewer shows local files only.";
        return result;
    }
    const std::string path = PathFromFileUrl(url);
    std::error_code ec;
    const auto fsPath = UltraCanvas::PathFromUtf8(path);
    if (!std::filesystem::is_regular_file(fsPath, ec)) {
        result.error = "There is no file " + path + ".";
        return result;
    }
    const std::string ext = LowerExt(path);
    const uintmax_t size = std::filesystem::file_size(fsPath, ec);
    if (!ec && size > (IsMimeFileExtension(ext) ? kMaxMailFileBytes : kMaxPageFileBytes)) {
        result.tooLarge = true;
        result.error = "The file " + path + " is too large to show.";
        return result;
    }
    std::ifstream f(fsPath, std::ios::binary);
    if (!f) {
        result.error = "The file " + path + " could not be read.";
        return result;
    }
    result.body.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    result.ok = true;
    result.status = 200;
    // The loader tells a page by its name or its first bytes; an .xml says
    // which it is by its content: XHTML a page, anything else its text.
#ifdef ULTRACANVAS_HAS_NET
    if (IsMimeFileExtension(ext)) {
        // A message, or a saved web page: the page it is shown as.
        const std::string raw(result.body.begin(), result.body.end());
        std::string page;
        if (ext == "mht" || ext == "mhtml") {
            MediaViewerWebArchive none;
            page = WebArchiveAsHtml(raw, url, archive ? archive->parts : none.parts);
        } else {
            page = MailMessageAsHtml(ext == "emlx" ? MessageOfEmlx(raw) : raw);
        }
        result.body.assign(page.begin(), page.end());
        result.contentType = "text/html; charset=utf-8";
        return result;
    }
#endif
    if (ext == "xml" || ext == "xht" || ext == "xhtml") {
        const std::string head(result.body.begin(),
                               result.body.begin() + static_cast<std::ptrdiff_t>(
                                   std::min<size_t>(result.body.size(), 4096)));
        result.contentType = (ext != "xml" || LooksLikeXhtml(head)) ? "application/xhtml+xml" : "text/plain";
    } else if (ext == "html" || ext == "htm" || ext == "shtml") {
        result.contentType = "text/html";
    }
    return result;
}

// ----- UCD v2 CONTAINER -----
// The fixed header of an UltraCanvas Document container and the raw HEIC/PNG
// preview thumbnail after it (Docs/UltraCanvas/UCD-FileFormat-v2.md), read
// by the container engine without touching the body: neither is ever
// compressed or encrypted, so a viewer can identify the file and show a
// preview of any content type.
struct UCDPreview {
    bool                 valid = false;   // a UCD v2 header was read
    std::string          problem;         // why not, or why the thumbnail is missing
    UCD::Header          header;
    std::vector<uint8_t> thumbnail;       // raw HEIC / PNG; empty without one
    // A layered image (UCBitmap) read whole - see ReadUCDPicture.
    std::shared_ptr<UCImage> picture;     // its layers composited; null when not shown
    int                  layers = 0;
    std::string          pictureProblem;  // why it is not shown, when it is a UCBitmap
    bool                 lockedByPassword = false;   // it opens with its password: Unlock
    bool                 wrongPassword = false;      // the password given is not right
};

static UCDPreview ReadUCDPreview(const std::string& path) {
    UCDPreview p;
    const bool whole = UCD::ReadHeaderAndThumbnail(path, p.header, p.thumbnail, p.problem);
    // The header parsed even when the thumbnail could not be read.
    p.valid = whole || !p.header.descriptor.empty();
    // A private document shows no preview, even one stored against the rule.
    if (p.header.IsPrivate()) p.thumbnail.clear();
    return p;
}

// A layered image is more than its thumbnail: the picture it holds, its
// layers composited at full size, is what the viewer shows. A
// password-protected one only with its `password` (Unlock asks for it) - it
// is marked lockedByPassword until then; no other content type.
// `pictureProblem` says why it is not shown, for the details.
static void ReadUCDPicture(const std::string& path, UCDPreview& p,
                           const std::string& password = std::string()) {
    if (!p.valid || p.header.descriptor != "UCBitmap") return;
    if (p.header.IsEncrypted() && password.empty()) {
        // A password opens key source 1; SuperVault (2) is not this program's.
        p.lockedByPassword = p.header.keySource == 1;
        p.pictureProblem = p.lockedByPassword ? "password-protected" : "protected by SuperVault";
        return;
    }
    UCRasterDocument doc;
    std::string error;
    if (!doc.LoadUCD(path, error, password)) {
        p.pictureProblem = error;
        p.wrongPassword = error == UCD::kWrongPassword;
        p.lockedByPassword = p.wrongPassword;
        return;
    }
    auto flat = doc.Flatten();
    if (!flat || !flat->IsValid()) {
        p.pictureProblem = "its layers could not be composited";
        return;
    }
    // The surface takes encoded images, so the composite goes there as a
    // PNG - at miniz's fastest level, since it is read straight back.
    size_t length = 0;
    void* png = tdefl_write_image_to_png_file_in_memory_ex(flat->Data(), flat->GetWidth(), flat->GetHeight(),
                                                           4, &length, 1, MZ_FALSE);
    if (!png) {
        p.pictureProblem = "its picture could not be encoded";
        return;
    }
    auto picture = UCImage::LoadFromMemory(static_cast<const uint8_t*>(png), length);
    mz_free(png);
    if (!picture || !picture->IsValid()) {
        p.pictureProblem = "its picture could not be decoded";
        return;
    }
    p.picture = picture;
    p.layers = doc.GetLayerCount();
}

// Friendly name of the primary content type a UCD descriptor announces.
static std::string UCDDescriptorName(const std::string& descriptor) {
    if (descriptor == "UCDoc")    return "multi-page document";
    if (descriptor == "UCForm")   return "saveable / fillable form";
    if (descriptor == "UCVector") return "vector graphics";
    if (descriptor == "UCWindow") return "window / UI description";
    if (descriptor == "UCBitmap") return "bitmap graphics, layered";
    if (descriptor == "UCVideo")  return "video";
    if (descriptor == "UCAudio")  return "audio";
    if (descriptor == "UC3D")     return "3D scene / model";
    return "unknown content type";
}

// Human-readable summary of a *.ucd file for the details popup (and the text
// view when the container carries no preview thumbnail).
static std::string BuildUCDDetailsText(const std::string& path,
                                       const UCDPreview& ucd, bool thumbShown) {
    const UCD::Header& hdr = ucd.header;
    std::ostringstream os;
    os << "UltraCanvas Document (UCD)\n\n";
    os << "File: " << BaseName(path) << "\n";
    os << "Path: " << path << "\n";
    std::error_code ec;
    auto sz = fs::file_size(UltraCanvas::PathFromUtf8(path), ec);
    if (!ec) os << "Size: " << HumanSize(sz) << "\n";
    if (!ucd.valid) {
        os << "\nNot a UCD v2 container: " << ucd.problem << ".\n";
        return os.str();
    }
    os << "Container: UCD v" << static_cast<int>(hdr.versionMajor) << "."
       << static_cast<int>(hdr.versionMinor)
       << " (" << hdr.descriptor << " \xE2\x80\x94 "
       << UCDDescriptorName(hdr.descriptor) << ")\n";
    os << "Body encoding: "
       << (hdr.bodyEncoding == 0 ? "binary" :
           hdr.bodyEncoding == 1 ? "XML text (debug)" :
           hdr.bodyEncoding == 2 ? "JSON text (debug)" : "unknown") << "\n";
    const uint8_t compression = static_cast<uint8_t>(hdr.defaultCompression);
    os << "Compression: "
       << (compression == 0 ? "none" :
           compression == 1 ? "deflate" :
           compression == 2 ? "gzip" :
           compression == 3 ? "LZMA" : "unknown") << "\n";
    // The cipher is always XChaCha20-Poly1305; byte 16 says where its key
    // comes from (spec 2.3).
    os << "Encryption: "
       << (!hdr.IsEncrypted() ? "none" :
           hdr.keySource == 1 ? "XChaCha20-Poly1305, key from a password (Argon2id)" :
           hdr.keySource == 2 ? "XChaCha20-Poly1305, key from SuperVault remote authorization" :
                                "XChaCha20-Poly1305")
       << (hdr.IsPrivate() ? " (private)" : "") << "\n";
    if (ucd.picture) {
        os << "Image: " << ucd.picture->GetWidth() << " x " << ucd.picture->GetHeight() << " px, "
           << ucd.layers << (ucd.layers == 1 ? " layer" : " layers") << "\n";
    }
    if (hdr.HasThumbnail()) {
        os << "Thumbnail: " << (hdr.ThumbnailIsPng() ? "PNG" : "HEIC") << ", "
           << HumanSize(hdr.thumbnailLength)
           << (ucd.picture ? "" : thumbShown ? " (shown)" : " (could not be decoded)") << "\n";
    } else {
        os << "Thumbnail: none\n";
    }
    // What the pane shows, as a row of its own (a line with a colon is one).
    if (ucd.picture) {
        os << "Shown: the image at full size, its layers composited"
           << (hdr.IsEncrypted() ? " (unlocked with its password)" : "") << "\n";
    } else if (ucd.lockedByPassword) {
        os << "Shown: "
           << (thumbShown ? "the thumbnail - Unlock (in the bar below, or Enter) shows the image itself"
                          : "nothing until it is unlocked - Unlock (in the bar below, or Enter) shows the image")
           << "\n";
    } else if (!ucd.pictureProblem.empty()) {
        os << "Shown: "
           << (thumbShown ? "the thumbnail - the image itself is not (" + ucd.pictureProblem + ")"
                          : "nothing - the image is not (" + ucd.pictureProblem + ") and there is no thumbnail")
           << "\n";
    } else {
        os << "\nPreview only \xE2\x80\x94 the viewer shows the thumbnail the document carries.\n";
    }
    return os.str();
}

#ifdef HAS_LIBVIPS
// Apply the tone/colour adjustments to a PixelFX image. Pure colour work — no
// geometry. Wrapped per-op in try/catch so an unsupported colourspace for one
// operation (e.g. sharpen on an exotic format) doesn't abort the whole chain.
static PixelFX::PFXImage ApplyColourAdjustments(PixelFX::PFXImage p,
                                                const MediaAdjustments& a) {
    // Tone curves run first: they are a per-channel remap of the original
    // tones, so the sliders below act on the curve's result, exactly as an
    // image editor stacks a Curves layer under its brightness controls.
    if (!a.curves.IsIdentity()) {
        try {
            std::array<std::array<uint8_t, 256>, 3> luts = a.curves.BuildChannelLuts();
            std::vector<std::vector<uint8_t>> tables;
            tables.reserve(3);
            for (const auto& lut : luts) tables.emplace_back(lut.begin(), lut.end());
            p = PixelFX::Colour::MapLut(p, tables);
        } catch (...) {}
    }
    if (a.autoOptimize) {
        try { p = PixelFX::Colour::HistEqual(p); } catch (...) {}
    }
    if (a.gamma != 1.0) {
        try { p = PixelFX::Colour::Gamma(p, a.gamma); } catch (...) {}
    }
    if (a.brightness != 1.0) {
        try { p = PixelFX::Colour::Brightness(p, a.brightness); } catch (...) {}
    }
    if (a.red != 1.0 || a.green != 1.0 || a.blue != 1.0) {
        try {
            int bands = p.bands();
            std::vector<double> m;
            if (bands >= 3) {
                m = { a.red, a.green, a.blue };
                while (static_cast<int>(m.size()) < bands) m.push_back(1.0);
            } else {
                double avg = (a.red + a.green + a.blue) / 3.0;
                m.assign(bands > 0 ? bands : 1, avg);
            }
            p = PixelFX::Arithmetic::Multiply(p, m);
        } catch (...) {}
    }
    if (a.sharpen > 0.0) {
        try { p = PixelFX::Convolution::Sharpen(p, a.sharpen); } catch (...) {}
    }
    return p;
}
#endif // HAS_LIBVIPS

// ===========================================================================
// MEDIA SURFACE
// ===========================================================================

UltraCanvasMediaSurface::UltraCanvasMediaSurface(const std::string& elemId)
    : UltraCanvasUIElement(elemId, 0.0f, 0.0f, 0.0f, 0.0f) {
    SetMouseCursor(UCMouseCursor::Default);
    animator.onFrameChanged = [this]() { if (IsVisible()) RequestRedraw(); };
}

UltraCanvasMediaSurface::~UltraCanvasMediaSurface() {
    StopTransitionTimer();
}

double UltraCanvasMediaSurface::FitScale(double iw, double ih, int rotQ) const {
    Rect2Df b = GetLocalBounds();
    if (b.width <= 0 || b.height <= 0 || iw <= 0 || ih <= 0) return 0.0;
    bool swap = ((((rotQ % 4) + 4) % 4) % 2) == 1;   // 90° / 270° swap the box
    double effW = swap ? ih : iw;
    double effH = swap ? iw : ih;
    const double fit = std::min(b.width / effW, b.height / effH);
    return fitEnlarges ? fit : std::min(1.0, fit);
}

void UltraCanvasMediaSurface::SetFitEnlarges(bool enlarges) {
    if (fitEnlarges == enlarges) return;
    fitEnlarges = enlarges;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::RebuildProcessed() {
    processed.reset();
    if (!image || !image->IsValid()) return;
    if (adjust.IsIdentity()) return;   // nothing to do — draw the source directly
#ifdef HAS_LIBVIPS
    try {
        vips::VImage v = image->GetVImage();
        PixelFX::PFXImage p(v);
        p = ApplyColourAdjustments(p, adjust);
        processed = CreatePixmapFromVImage(p);
    } catch (...) {
        processed.reset();
    }
#endif
}

void UltraCanvasMediaSurface::SetAdjustments(const MediaAdjustments& adj) {
    adjust = adj;
    RebuildProcessed();
    // Adjustments are baked into one composited pixmap, so they freeze an
    // animated image on its current frame; identity resumes playback.
    if (!adjust.IsIdentity()) animator.Pause();
    else if (animator.GetAnimation()) animator.Play();
    RequestRedraw();
}

void UltraCanvasMediaSurface::ShowImage(std::shared_ptr<UCImage> img,
                                        MediaTransition transition,
                                        int durationMs, bool animated) {
    // A copy: the call below replaces `adjust` with what it is handed.
    const MediaAdjustments keep = adjust;
    ShowImage(std::move(img), transition, durationMs, animated, keep);
}

void UltraCanvasMediaSurface::ShowImage(std::shared_ptr<UCImage> img,
                                        MediaTransition transition,
                                        int durationMs, bool animated,
                                        const MediaAdjustments& adj) {
    bool animate = animated && transition != MediaTransition::NoTransition &&
                   durationMs > 0 && image && image->IsValid();
    if (animate) {
        prevImage = image;
        prevProcessed = processed;
        prevRotQ = rotationQuarters;
        prevFlipH = flipH;
        prevFlipV = flipV;
        transitionStyle = transition;
        StartTransitionTimer(durationMs);
    } else {
        StopTransitionTimer();
        transitionActive = false;
        prevImage.reset();
        prevProcessed.reset();
    }

    image = std::move(img);
    adjust = adj;
    decodeFailureReported = false;
    // Each new image starts fit-to-window with no rotation/mirror.
    zoom = 1.0;
    panX = panY = 0.0;
    rotationQuarters = 0;
    flipH = flipV = false;
    RebuildProcessed();
    // Animated images (GIF / animated WebP) start playing right away unless
    // colour adjustments are active (those bake a single frame).
    animator.SetAnimation((image && image->IsValid() && image->IsAnimated())
                              ? image->GetAnimation() : nullptr);
    if (animator.GetAnimation() && adjust.IsIdentity()) animator.Play();
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::ResetView() {
    zoom = 1.0;
    panX = panY = 0.0;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

double UltraCanvasMediaSurface::GetZoomPercent() const {
    if (!image || !image->IsValid()) return 100.0;
    double fit = FitScale(image->GetWidth(), image->GetHeight(), rotationQuarters);
    return fit * zoom * 100.0;
}

bool UltraCanvasMediaSurface::IsAutoFit() const {
    return std::fabs(zoom - 1.0) < 0.001;
}

void UltraCanvasMediaSurface::SetZoomPercent(double percent) {
    if (!image || !image->IsValid()) return;
    double fit = FitScale(image->GetWidth(), image->GetHeight(), rotationQuarters);
    if (fit <= 0.0) return;
    zoom = std::max(0.02, std::min((percent / 100.0) / fit, 64.0));
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::ZoomBy(double factor) {
    zoom = std::max(0.02, std::min(zoom * factor, 64.0));
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::RotateBy(int quarters) {
    rotationQuarters = (((rotationQuarters + quarters) % 4) + 4) % 4;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::SetRotationQuarters(int q) {
    rotationQuarters = ((q % 4) + 4) % 4;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::ToggleFlipHorizontal() {
    flipH = !flipH;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::ToggleFlipVertical() {
    flipV = !flipV;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::ResetTransforms() {
    rotationQuarters = 0;
    flipH = flipV = false;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::StartTransitionTimer(int durationMs) {
    StopTransitionTimer();
    transitionActive = true;
    transitionProgress = 0.0;
    transitionDurationMs = std::max(1, durationMs);
    transitionStart = std::chrono::steady_clock::now();
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) { transitionActive = false; return; }
    transitionTimer = app->StartTimer(16, true, [this](TimerId) {
        auto now = std::chrono::steady_clock::now();
        double el = std::chrono::duration<double, std::milli>(now - transitionStart).count();
        transitionProgress = el / static_cast<double>(transitionDurationMs);
        if (transitionProgress >= 1.0) {
            transitionProgress = 1.0;
            transitionActive = false;
            prevImage.reset();
            prevProcessed.reset();
            StopTransitionTimer();
        }
        RequestRedraw();
    });
}

void UltraCanvasMediaSurface::StopTransitionTimer() {
    if (transitionTimer) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(transitionTimer);
        transitionTimer = 0;
    }
}

void UltraCanvasMediaSurface::Blit(IRenderContext* ctx,
                                   const std::shared_ptr<UCImage>& img,
                                   const std::shared_ptr<UCPixmap>& pm,
                                   double iw, double ih, double scale,
                                   double cx, double cy, int rotQ,
                                   bool fH, bool fV, double alpha) {
    if (iw <= 0 || ih <= 0 || scale <= 0) return;
    double dispW = iw * scale;
    double dispH = ih * scale;
    ctx->PushState();
    if (alpha < 0.999) ctx->SetAlpha(alpha < 0.0 ? 0.0 : alpha);
    ctx->Translate(cx, cy);
    int q = ((rotQ % 4) + 4) % 4;
    if (q != 0) ctx->Rotate(q * kHalfPi);
    if (fH || fV) ctx->Scale(fH ? -1.0 : 1.0, fV ? -1.0 : 1.0);
    Rect2Dd dst(-dispW / 2.0, -dispH / 2.0, dispW, dispH);
    if (pm) {
        ctx->DrawPixmap(*pm, dst, ImageFitMode::Fill);
    } else if (img) {
        ctx->DrawImage(*img, dst, ImageFitMode::Fill);
    }
    ctx->PopState();
}

void UltraCanvasMediaSurface::DrawBackdrop(IRenderContext* ctx, const Rect2Df& b,
                                           double iw, double ih, double scale,
                                           double cx, double cy, int rotQ,
                                           double alpha) {
    if (iw <= 0 || ih <= 0 || scale <= 0 || alpha <= 0.001) return;
    // Displayed rectangle of the image in widget coordinates: rotation is in
    // 90° steps, so it stays axis-aligned (odd quarters swap the sides).
    bool swap = ((((rotQ % 4) + 4) % 4) % 2) == 1;
    double dispW = (swap ? ih : iw) * scale;
    double dispH = (swap ? iw : ih) * scale;
    double left = cx - dispW / 2.0;
    double top  = cy - dispH / 2.0;
    // Visible part only — at high zoom the image rectangle is far larger than
    // the widget, and the checkerboard cost scales with the area drawn.
    double x0 = std::max(left, static_cast<double>(b.x));
    double y0 = std::max(top,  static_cast<double>(b.y));
    double x1 = std::min(left + dispW, static_cast<double>(b.x) + b.width);
    double y1 = std::min(top  + dispH, static_cast<double>(b.y) + b.height);
    if (x1 <= x0 || y1 <= y0) return;

    ctx->PushState();
    if (alpha < 0.999) ctx->SetAlpha(alpha);
    if (transparentBackground == TransparentImageBackground::SolidColor) {
        ctx->DrawFilledRectangle(Rect2Dd(x0, y0, x1 - x0, y1 - y0),
                                 transparentColor, 0.0f);
    } else {
        constexpr double kCell = 12.0;                   // square size (px)
        const Color light(255, 255, 255, 255);
        const Color dark(203, 203, 203, 255);
        ctx->DrawFilledRectangle(Rect2Dd(x0, y0, x1 - x0, y1 - y0), light, 0.0f);
        // Pattern anchored to the image rectangle so it pans with the image.
        int c0 = static_cast<int>(std::floor((x0 - left) / kCell));
        int r0 = static_cast<int>(std::floor((y0 - top)  / kCell));
        for (int r = r0; top + r * kCell < y1; ++r) {
            double cy0 = std::max(top + r * kCell, y0);
            double cy1 = std::min(top + (r + 1) * kCell, y1);
            for (int c = c0 + ((c0 + r) % 2 == 0 ? 1 : 0); left + c * kCell < x1; c += 2) {
                double cx0 = std::max(left + c * kCell, x0);
                double cx1 = std::min(left + (c + 1) * kCell, x1);
                if (cx1 > cx0 && cy1 > cy0)
                    ctx->DrawFilledRectangle(Rect2Dd(cx0, cy0, cx1 - cx0, cy1 - cy0),
                                             dark, 0.0f);
            }
        }
    }
    ctx->PopState();
}

bool UltraCanvasMediaSurface::HasDecodeFailure() const {
    return image && !image->errorMessage.empty();
}

void UltraCanvasMediaSurface::ReportDecodeFailure() {
    if (decodeFailureReported || !HasDecodeFailure()) return;
    decodeFailureReported = true;
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app || !onDecodeFailed) return;
    std::weak_ptr<UltraCanvasUIElement> self = weak_from_this();
    app->StartTimer(0, false, [self, this](TimerId) {
        if (self.expired()) return;
        if (onDecodeFailed) onDecodeFailed();
    });
}

void UltraCanvasMediaSurface::DrawCurrent(IRenderContext* ctx, const Rect2Df& b) {
    if (!image || !image->IsValid()) {
        // A picture whose pixels would not decode says so; the reason is in
        // the info bar and the Details panel.
        const char* text = HasDecodeFailure() ? "Cannot decode this picture" : "No media";
        ctx->SetFontSize(16);
        ctx->SetTextPaint(Color(130, 130, 140, 255));
        Point2Dd p = ctx->CalculateCenteredTextPosition(
                text, Rect2Dd(b.x, b.y, b.width, b.height));
        ctx->DrawText(text, p);
        return;
    }
    double iw = image->GetWidth();
    double ih = image->GetHeight();
    double fit = FitScale(iw, ih, rotationQuarters);
    double s = fit * zoom;
    double cx = b.width * 0.5 + panX;
    double cy = b.height * 0.5 + panY;
    // Colour-adjusted pixmap wins; otherwise an animated image draws its
    // current frame; a plain still falls through to the image itself.
    std::shared_ptr<UCPixmap> pm = processed ? processed : animator.GetCurrentFramePixmap();
    DrawBackdrop(ctx, b, iw, ih, s, cx, cy, rotationQuarters, 1.0);
    Blit(ctx, image, pm, iw, ih, s, cx, cy, rotationQuarters, flipH, flipV, 1.0);
}

void UltraCanvasMediaSurface::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
    if (!IsVisible()) return;
    Rect2Df b = GetLocalBounds();
    if (b.width <= 0 || b.height <= 0) return;

    ctx->PushState();
    ctx->ClipRect(Rect2Dd(b.x, b.y, b.width, b.height));
    ctx->DrawFilledRectangle(Rect2Dd(b.x, b.y, b.width, b.height), canvasColor, 0.0f);

    if (transitionActive && (prevImage || image)) {
        double t = std::max(0.0, std::min(transitionProgress, 1.0));
        double cx = b.width * 0.5;
        double cy = b.height * 0.5;

        double iwC = image ? image->GetWidth() : 0;
        double ihC = image ? image->GetHeight() : 0;
        double fitC = FitScale(iwC, ihC, rotationQuarters);

        double iwP = prevImage ? prevImage->GetWidth() : 0;
        double ihP = prevImage ? prevImage->GetHeight() : 0;
        double fitP = prevImage ? FitScale(iwP, ihP, prevRotQ) : 0;

        // Per-style opacity / offset / scale for the outgoing (prev) and
        // incoming (current) layers.
        double prevAlpha = 1.0 - t, curAlpha = t;
        double prevOffX = 0, prevOffY = 0, curOffX = 0, curOffY = 0;
        double prevScaleMul = 1.0, curScaleMul = 1.0;

        switch (transitionStyle) {
            case MediaTransition::CrossFade:
                break;
            case MediaTransition::FadeOutIn:
                if (t < 0.5) { prevAlpha = 1.0 - 2.0 * t; curAlpha = 0.0; }
                else         { prevAlpha = 0.0; curAlpha = 2.0 * t - 1.0; }
                break;
            case MediaTransition::SlideHorizontal:
                prevAlpha = curAlpha = 1.0;
                prevOffX = -b.width * t;
                curOffX  =  b.width * (1.0 - t);
                break;
            case MediaTransition::SlideVertical:
                prevAlpha = curAlpha = 1.0;
                prevOffY = -b.height * t;
                curOffY  =  b.height * (1.0 - t);
                break;
            case MediaTransition::ZoomFade:
                curScaleMul = 1.18 - 0.18 * t;   // settle from 1.18x to 1.0x
                break;
            default:
                prevAlpha = 0.0; curAlpha = 1.0;
                break;
        }

        if (prevImage && prevAlpha > 0.001) {
            DrawBackdrop(ctx, b, iwP, ihP, fitP * prevScaleMul,
                         cx + prevOffX, cy + prevOffY, prevRotQ, prevAlpha);
            Blit(ctx, prevImage, prevProcessed, iwP, ihP, fitP * prevScaleMul,
                 cx + prevOffX, cy + prevOffY, prevRotQ, prevFlipH, prevFlipV, prevAlpha);
        }
        if (image && curAlpha > 0.001) {
            std::shared_ptr<UCPixmap> curPm =
                    processed ? processed : animator.GetCurrentFramePixmap();
            DrawBackdrop(ctx, b, iwC, ihC, fitC * curScaleMul,
                         cx + curOffX, cy + curOffY, rotationQuarters, curAlpha);
            Blit(ctx, image, curPm, iwC, ihC, fitC * curScaleMul,
                 cx + curOffX, cy + curOffY, rotationQuarters, flipH, flipV, curAlpha);
        }
    } else {
        DrawCurrent(ctx, b);
    }

    ctx->PopState();

    // Only drawing decodes the pixels, so only now is a failure known. The
    // first frame of a broken picture is empty; the next one carries the
    // message above.
    if (HasDecodeFailure() && !decodeFailureReported) {
        ReportDecodeFailure();
        RequestRedraw();
    }
}

bool UltraCanvasMediaSurface::HandleWheelZoom(const UCEvent& event) {
    if (!image || !image->IsValid()) return false;
    if (!zoomAnim.IsBound()) {
        zoomAnim.Bind([this](double f) { ApplyZoomFactorAtCursor(f, zoomCursor); },
                      [this] {
                          RequestRedraw();
                          if (onViewChanged) onViewChanged();
                      });
    }
    zoomCursor = Point2Di(event.pointer.x, event.pointer.y);
    zoomAnim.ZoomBy((event.wheelDelta > 0) ? 1.15 : (1.0 / 1.15), zoom, 0.05, 64.0);
    return true;
}

// One zoom step about the cursor. A wheel notch is eased in as a run of these
// (UltraCanvasSmoothZoom), and applying them in a row about the same cursor is
// exactly applying their product once.
void UltraCanvasMediaSurface::ApplyZoomFactorAtCursor(double factor,
                                                      const Point2Di& cursor) {
    if (!image || !image->IsValid()) return;
    Rect2Df b = GetLocalBounds();
    double iw = std::max(1, image->GetWidth());
    double ih = std::max(1, image->GetHeight());
    double fit = FitScale(iw, ih, rotationQuarters);
    if (fit <= 0.0) return;

    double newZoom = std::max(0.05, std::min(zoom * factor, 64.0));
    if (newZoom == zoom) return;

    // Anchor the zoom under the cursor only when the image is un-rotated /
    // un-mirrored (the simple screen<->image mapping holds). Otherwise zoom
    // about the centre.
    if (rotationQuarters == 0 && !flipH && !flipV) {
        double sOld = fit * zoom;
        double leftOld = b.width * 0.5 + panX - iw * sOld * 0.5;
        double topOld  = b.height * 0.5 + panY - ih * sOld * 0.5;
        double imgX = (cursor.x - leftOld) / sOld;
        double imgY = (cursor.y - topOld)  / sOld;
        zoom = newZoom;
        double sNew = fit * zoom;
        double cxNew = cursor.x - imgX * sNew + iw * sNew * 0.5;
        double cyNew = cursor.y - imgY * sNew + ih * sNew * 0.5;
        panX = cxNew - b.width * 0.5;
        panY = cyNew - b.height * 0.5;
    } else {
        zoom = newZoom;
    }
}

bool UltraCanvasMediaSurface::OnEvent(const UCEvent& event) {
    auto* app = UltraCanvasApplication::GetInstance();
    switch (event.type) {
        case UCEventType::MouseWheel:
            return HandleWheelZoom(event);

        case UCEventType::MouseDown:
            if (Contains(event.pointer)) {
                pressing = true;
                dragging = false;
                pressX = lastX = event.pointer.x;
                pressY = lastY = event.pointer.y;
                pressButton = event.button;
                SetFocus(true);
                if (app) app->CaptureMouse(this);
                return true;
            }
            return false;

        case UCEventType::MouseMove:
            if (pressing) {
                double dx = event.pointer.x - pressX;
                double dy = event.pointer.y - pressY;
                if (!dragging && (dx * dx + dy * dy) > 16) dragging = true;
                if (dragging) {
                    panX += event.pointer.x - lastX;
                    panY += event.pointer.y - lastY;
                    lastX = event.pointer.x;
                    lastY = event.pointer.y;
                    RequestRedraw();
                }
                return true;
            }
            return false;

        case UCEventType::MouseUp:
            if (pressing) {
                pressing = false;
                if (app) app->ReleaseMouse();
                bool wasDrag = dragging;
                dragging = false;
                // A click (no drag) navigates: left = next, right = previous —
                // works whether or not the image is zoomed in.
                if (!wasDrag && onNavigate) {
                    if (pressButton == UCMouseButton::Left)       onNavigate(1);
                    else if (pressButton == UCMouseButton::Right)  onNavigate(-1);
                }
                return true;
            }
            return false;

        case UCEventType::MouseDoubleClick:
            ResetView();   // back to fit-to-window
            return true;

        case UCEventType::Drop:
            if (onFilesDropped && !event.droppedFiles.empty()) {
                onFilesDropped(event.droppedFiles);
                return true;
            }
            return false;

        case UCEventType::KeyDown: {
            UCKeys k = event.virtualKey;
            if (k == UCKeys::Left)  { if (onNavigate) onNavigate(-1); return true; }
            if (k == UCKeys::Right) { if (onNavigate) onNavigate(1);  return true; }
            if (k == UCKeys::Up)    { ZoomBy(1.15);       return true; }
            if (k == UCKeys::Down)  { ZoomBy(1.0 / 1.15); return true; }
            if (event.character == '+' || event.character == '=') { ZoomBy(1.15); return true; }
            if (event.character == '-') { ZoomBy(1.0 / 1.15); return true; }
            if (event.character == '0') { ResetView(); return true; }
            if (event.character == 'r' || event.character == 'R') { RotateBy(1); return true; }
            return false;
        }

        default:
            return false;
    }
}

#ifdef HAS_LIBVIPS
// An SVG the image pipeline rasterized: a drawing, which its loader draws
// again at any scale. Only one read from a file can be drawn again.
static bool IsRedrawableDrawing(const vips::VImage& v) {
    if (vips_image_get_typeof(v.get_image(), "vips-loader") == 0) return false;
    const char* loader = nullptr;
    if (vips_image_get_string(v.get_image(), "vips-loader", &loader) != 0 || !loader) return false;
    const char* file = v.filename();
    return std::strncmp(loader, "svgload", 7) == 0 && file && *file;
}
#endif

FileSavePicture UltraCanvasMediaSurface::GetSavePicture() const {
    FileSavePicture picture;
    if (!image || !image->IsValid()) return picture;
    int w = image->GetWidth();
    int h = image->GetHeight();
#ifdef HAS_LIBVIPS
    // What Save starts from: the size the pipeline reads the file at (an
    // SVG's is its own size on the page), not the size it is shown at.
    try {
        vips::VImage v = image->GetVImage();
        w = v.width();
        h = v.height();
        picture.drawing = IsRedrawableDrawing(v);
    } catch (...) {
        // The header did not read: the size the image reported.
    }
#endif
    if ((((rotationQuarters % 4) + 4) % 4) % 2 == 1) std::swap(w, h);
    picture.width = w;
    picture.height = h;
    return picture;
}

bool UltraCanvasMediaSurface::SaveProcessed(const std::string& path, std::string& error) {
    return SaveProcessed(path, FileSaveImageSize(), error);
}

bool UltraCanvasMediaSurface::SaveProcessed(const std::string& path, const FileSaveImageSize& size,
                                            std::string& error) {
    if (!image || !image->IsValid()) { error = "No image to save"; return false; }
#ifdef HAS_LIBVIPS
    try {
        vips::VImage v = image->GetVImage();
        int q = ((rotationQuarters % 4) + 4) % 4;
        // A drawing has no pixels of its own: drawn again at the size chosen
        // it stays sharp, where stretching the picture read at its page size
        // would blur it. The size is the one shown, so a quarter turn swaps
        // the sides back; drawn at the larger of the two scales, the writer
        // then only ever shrinks the other side to fit.
        if (!size.IsOriginal() && v.width() > 0 && v.height() > 0 && IsRedrawableDrawing(v)) {
            const int w = (q % 2 == 1) ? size.height : size.width;
            const int h = (q % 2 == 1) ? size.width : size.height;
            const double scale = std::max(static_cast<double>(w) / v.width(),
                                          static_cast<double>(h) / v.height());
            v = vips::VImage::new_from_file(v.filename(), vips::VImage::option()->set("scale", scale));
        }
        PixelFX::PFXImage p(v);
        p = ApplyColourAdjustments(p, adjust);
        if      (q == 1) p = PixelFX::Resample::Rot(p, PixelFX::Angle::D90);
        else if (q == 2) p = PixelFX::Resample::Rot(p, PixelFX::Angle::D180);
        else if (q == 3) p = PixelFX::Resample::Rot(p, PixelFX::Angle::D270);
        if (flipH) p = PixelFX::Resample::FlipHorizontal(p);
        if (flipV) p = PixelFX::Resample::FlipVertical(p);
        // At another size, or once the user has stored settings for the
        // format (the Save dialog's Settings): the image writer's options -
        // the settings, and the size. Otherwise the writer's own defaults,
        // as before.
        UCImageSave::ImageExportOptions options;
        if ((!size.IsOriginal() || UltraCanvasFileLoader::HasStoredSaveSettings(path)) &&
            UltraCanvasFileLoader::GetImageExportOptions(path, size, options)) {
            const std::string err = ExportVImage(p, path, options);
            if (!err.empty()) { error = err; return false; }
            return true;
        }
        if (!PixelFX::FileIO::Save(p, path)) {
            error = PixelFX::GetLastError();
            if (error.empty()) error = "Save failed";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    } catch (...) {
        error = "Unknown error during save";
        return false;
    }
#else
    (void)size;
    error = "Saving requires libvips support";
    return false;
#endif
}

// ===========================================================================
// MEDIA VIEWER
// ===========================================================================

UltraCanvasMediaViewer::UltraCanvasMediaViewer(const std::string& identifier,
                                               float x, float y, float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    BuildUI(w, h);
}

UltraCanvasMediaViewer::~UltraCanvasMediaViewer() {
    // The key filter captures `this`; it must not outlive the widget.
    RemoveKeyFilter();
    // So do the session-lock listener and the idle timer.
    if (sessionLockListener) RemoveSessionLockListener(sessionLockListener);
    if (idleTimer) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(idleTimer);
        idleTimer = 0;
    }
    // Same for the Curves dialog: it lives in the application's window list and
    // would call back into a destroyed viewer.
    if (auto dlg = curvesDialog.lock()) {
        dlg->onCurvesChanged = nullptr;
        dlg->onAccept = nullptr;
        dlg->onCancel = nullptr;
        dlg->Close();
    }
    if (slideshowTimer) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(slideshowTimer);
        slideshowTimer = 0;
    }
    StopVideoClipTimer();
}

std::shared_ptr<UltraCanvasUIElement> UltraCanvasMediaViewer::BuildAdjustSlider(
        const std::string& id, const std::string& caption,
        float minV, float maxV, float value, std::function<void(float)> onChange) {
    auto box = std::make_shared<UltraCanvasContainer>(id + "_box", 0, 0, 124, 64);
    box->layout.SetFlexColumn().SetFlexGap(2)
               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    box->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

    auto lbl = std::make_shared<UltraCanvasLabel>(id + "_lbl", 0, 0, 124, 16, caption);
    lbl->SetFontSize(11);
    lbl->SetTextColor(Color(210, 210, 216, 255));
    lbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    box->AddChild(lbl);

    // "Gamma  1.00" — the caption carries the live value, so a photo edit can
    // be repeated instead of being dialled in by eye.
    auto caption_text = [caption](float v) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s  %.2f", caption.c_str(), v);
        return std::string(buf);
    };
    lbl->SetText(caption_text(value));

    auto sld = std::make_shared<UltraCanvasSlider>(id, 0, 0, 124, 22);
    sld->SetRange(minV, maxV);
    // Continuous: these are fractional ranges (gamma 0.2..3.0, a channel gain
    // 0..2), where any snapping increment would cost most of the scale.
    sld->SetStep(0.0f);
    sld->SetValue(value);
    sld->onValueChanged = [this, onChange, lbl, caption_text](float v) {
        lbl->SetText(caption_text(v));
        if (suppressAdjustCallbacks) return;
        if (onChange) onChange(v);
    };
    sld->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    box->AddChild(sld);

    // Reset puts the control back where it was built.
    adjustResetters.push_back([sld, value]() { sld->SetValue(value); });

    return box;
}

void UltraCanvasMediaViewer::BuildUI(float w, float h) {
    SetBackgroundColor(Color(24, 24, 28, 255));
    layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // ----- FOLDER BREADCRUMB (top, Parallelogram style) -----
    // Built by BuildFolderBreadcrumb, the shared folder path mechanism (see
    // UpdateBreadcrumb): "Computer" + drive + one node per folder. Each segment
    // navigates to that folder; its dropdown lists the folders inside that
    // segment so the path can be extended one level. Long paths collapse their
    // middle segments into a "..." overflow menu, keeping the root and the last
    // two folders visible.
    breadcrumb = std::make_shared<UltraCanvasBreadcrumb>("MV_Breadcrumb", 0, 0, 0, 30);
    {
        BreadcrumbStyle bs = BreadcrumbStyle::Parallelogram();
        bs.overflowMode = BreadcrumbOverflowMode::Collapse;  // collapse middle into "..."
        bs.keepFirstItemOnCollapse = true;
        bs.minVisibleAfterCollapse = 2;
        // Compact segment metrics (as on the filer's path row) so a deep path
        // keeps as many folders as possible before collapsing.
        bs.arrowSize = 8;
        bs.itemPaddingHorizontal = 9;
        bs.itemPaddingVertical = 3;
        bs.fontStyle.fontSize = 11.0f;
        bs.backgroundColor = Color(30, 30, 36, 255);
        breadcrumb->SetStyle(bs);
    }
    breadcrumb->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    breadcrumb->SetVisible(false);   // shown once a folder is known
    AddChild(breadcrumb);

    // ----- TOP TOOLBAR: navigation + slideshow, then view + edit -----
    // One compact row: flat buttons with small icons. A window too narrow for
    // every control scrolls the row (mouse wheel, edge chevron) instead of
    // clipping it. Details is not on it - the info bar under the picture
    // carries that button.
    toolbar = std::make_shared<UltraCanvasToolbar>("MV_Toolbar", 0, 0, 0, 30);
    toolbar->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                       .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    {
        ToolbarAppearance app = toolbar->GetAppearance();
        app.style = ToolbarStyle::Flat;
        app.showIconLabels = false;
        app.hasShadow = false;
        app.iconSize = ToolbarIconSize::Small;
        toolbar->SetAppearance(app);
    }
    toolbar->SetOverflowMode(ToolbarOverflowMode::Scroll);

    IconOnly(toolbar->AddButton("mv_open", "", ViewerIconPath("folder-open.svg"),
                                [this] { ShowOpenDialog(); }),
             "Open a file or folder");
    toolbar->AddSeparator("mv_sep0");
    IconOnly(toolbar->AddButton("mv_prev", "", ViewerIconPath("angle-left.svg"),
                                [this] { Previous(); }),
             "Previous file (Left)");
    IconOnly(toolbar->AddButton("mv_next", "", ViewerIconPath("angle-right.svg"),
                                [this] { Next(); }),
             "Next file (Right)");
    toolbar->AddSeparator("mv_sep1");
    playButton = IconOnly(toolbar->AddToggleButton("mv_play", "", ViewerIconPath("slideshow.svg"),
            [this](bool on) { if (on) PlaySlideshow(); else PauseSlideshow(); }),
            "Slideshow (Space)");
    // The two dropdowns show their current value - "5 s", "Cross fade" - which
    // is what a value picker is for; the tooltip says which value it is.
    {
        auto dd = toolbar->AddDropdownButton("mv_interval", "",
                { "3 s", "5 s", "7 s", "10 s" },
                [this](const std::string& s) {
                    double sec = std::atof(s.c_str());
                    if (sec > 0) SetSlideshowIntervalSeconds(sec);
                });
        dd->SetTooltip("Slideshow interval");
        dd->SetSelectedIndex(1, false);      // 5 s, the default
    }
    {
        auto dd = toolbar->AddDropdownButton("mv_trans", "",
                { "None", "Cross fade", "Fade out/in", "Slide H", "Slide V", "Zoom" },
                [this](const std::string& s) {
                    MediaTransition t = MediaTransition::CrossFade;
                    if      (s == "None")        t = MediaTransition::NoTransition;
                    else if (s == "Cross fade")  t = MediaTransition::CrossFade;
                    else if (s == "Fade out/in") t = MediaTransition::FadeOutIn;
                    else if (s == "Slide H")     t = MediaTransition::SlideHorizontal;
                    else if (s == "Slide V")     t = MediaTransition::SlideVertical;
                    else if (s == "Zoom")        t = MediaTransition::ZoomFade;
                    SetTransition(t);
                });
        dd->SetTooltip("Slideshow transition");
        dd->SetSelectedIndex(1, false);      // Cross fade, the default
    }
    toolbar->AddSeparator("mv_sep2");

    IconOnly(toolbar->AddButton("mv_zoomout", "", ViewerIconPath("zoom-out.svg"),
                                 [this] { ZoomOutAction(); }),
             "Zoom out (-)");
    toolbar->AddDropdownButton("mv_zoom", "",
            { "Fit", "25%", "50%", "75%", "100%", "150%", "200%", "400%" },
            [this](const std::string& s) {
                if (s == "Fit") ZoomFitAction();
                else            ZoomPercentAction(std::atof(s.c_str()));
            })->SetTooltip("Zoom level");
    IconOnly(toolbar->AddButton("mv_zoomin", "", ViewerIconPath("zoom-in.svg"),
                                 [this] { ZoomInAction(); }),
             "Zoom in (+)");
    IconOnly(toolbar->AddButton("mv_fit", "", ViewerIconPath("zoom-fit.svg"),
                                 [this] { ZoomFitAction(); }),
             "Fit to window");
    toolbar->AddSeparator("mv_sep3");
    IconOnly(toolbar->AddButton("mv_rotl", "", ViewerIconPath("rotate-left.svg"),
                                 [this] { if (surface) surface->RotateBy(-1); }),
             "Rotate left");
    IconOnly(toolbar->AddButton("mv_rotr", "", ViewerIconPath("rotate-right.svg"),
                                 [this] { if (surface) surface->RotateBy(1); }),
             "Rotate right");
    IconOnly(toolbar->AddButton("mv_mirh", "", ViewerIconPath("mirror-h.svg"),
                                 [this] { if (surface) surface->ToggleFlipHorizontal(); }),
             "Mirror horizontally");
    IconOnly(toolbar->AddButton("mv_mirv", "", ViewerIconPath("mirror-v.svg"),
                                 [this] { if (surface) surface->ToggleFlipVertical(); }),
             "Mirror vertically");
    toolbar->AddSeparator("mv_sep4");
    // The panel follows the toggle only while the shown file takes colour
    // adjustments (ApplyToolAvailability); the toggle remembers it meanwhile.
    IconOnly(toolbar->AddToggleButton("mv_adjust", "", ViewerIconPath("settings-sliders.svg"),
            [this](bool on) { adjustPanelOpen = on; ApplyToolAvailability(); }),
            "Adjustments: gamma, brightness, colour, sharpen");
    IconOnly(toolbar->AddButton("mv_curves", "", ViewerIconPath("curves.svg"),
                                 [this] { ShowCurvesDialog(); }),
             "Curves");
    IconOnly(toolbar->AddButton("mv_save", "", ViewerIconPath("save.svg"),
                                 [this] { ShowSaveDialog(); }),
             "Save as\xE2\x80\xA6");
    IconOnly(toolbar->AddButton("mv_unlock", "", ViewerIconPath("unlock.svg"),
                                 [this] { Unlock(); }),
             "Unlock: show this password-protected file (Enter)");
    AddChild(toolbar);

    // ----- ADJUSTMENTS PANEL (hidden until "Adjust" is toggled) -----
    adjustPanel = std::make_shared<UltraCanvasContainer>("MV_Adjust", 0, 0, 0, 84);
    adjustPanel->SetBackgroundColor(Color(34, 34, 40, 255));
    adjustPanel->layout.SetFlexRow().SetFlexGap(10)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    adjustPanel->SetPadding(8, 12, 8, 12);
    adjustPanel->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    adjustPanel->SetVisible(false);

    adjustPanel->AddChild(BuildAdjustSlider("adj_gamma", "Gamma", 0.2f, 3.0f, 1.0f,
            [this](float v) { adjustments.gamma = v; ApplyAdjustments(); }));
    adjustPanel->AddChild(BuildAdjustSlider("adj_bright", "Brightness", 0.2f, 2.5f, 1.0f,
            [this](float v) { adjustments.brightness = v; ApplyAdjustments(); }));
    adjustPanel->AddChild(BuildAdjustSlider("adj_red", "Red", 0.0f, 2.0f, 1.0f,
            [this](float v) { adjustments.red = v; ApplyAdjustments(); }));
    adjustPanel->AddChild(BuildAdjustSlider("adj_green", "Green", 0.0f, 2.0f, 1.0f,
            [this](float v) { adjustments.green = v; ApplyAdjustments(); }));
    adjustPanel->AddChild(BuildAdjustSlider("adj_blue", "Blue", 0.0f, 2.0f, 1.0f,
            [this](float v) { adjustments.blue = v; ApplyAdjustments(); }));
    adjustPanel->AddChild(BuildAdjustSlider("adj_sharp", "Sharpen", 0.0f, 3.0f, 0.0f,
            [this](float v) { adjustments.sharpen = v; ApplyAdjustments(); }));

    // Auto-optimise latches (it is a state of the adjustments, not an action),
    // so it is a toggle whose pressed look says whether it is on; Reset is a
    // plain click. Both are icons with the caption as tooltip, like the toolbar.
    auto makeAdjustButton = [](const std::string& id, const std::string& icon,
                               const std::string& tooltip) {
        auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, 34, 28, "");
        b->SetIcon(ViewerIconPath(icon));
        b->SetIconSize(18, 18);
        b->SetIconPosition(ButtonIconPosition::Center);
        b->SetIconSpacing(0);
        b->SetUseIconAsMask(true);
        b->SetTooltip(tooltip);
        b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        return b;
    };
    auto autoBtn = makeAdjustButton("adj_auto", "wand.svg", "Auto-optimise");
    autoBtn->SetCanToggled(true);
    autoBtn->onToggle = [this](bool on) {
        adjustments.autoOptimize = on;
        ApplyAdjustments();
    };
    adjustResetters.push_back([b = autoBtn.get()] { b->SetPressed(false); });
    adjustPanel->AddChild(autoBtn);

    auto resetBtn = makeAdjustButton("adj_reset", "reload.svg", "Reset adjustments");
    resetBtn->onClick = [this] { ResetAdjustments(); };
    adjustPanel->AddChild(resetBtn);
    AddChild(adjustPanel);

    // ----- CENTRAL IMAGE SURFACE -----
    surface = std::make_shared<UltraCanvasMediaSurface>("MV_Surface");
    surface->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                       .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    surface->onNavigate = [this](int d) { if (d < 0) Previous(); else Next(); };
    surface->onViewChanged = [this] { UpdateInfoBar(); };
    surface->onDecodeFailed = [this] {
        // Nothing to zoom, turn or recolour once the pixels did not decode.
        UpdateToolAvailability();
        UpdateInfoBar();
        if (IsDetailsVisible()) UpdateDetailedInfo();
    };
    surface->onFilesDropped = [this](const std::vector<std::string>& files) {
        HandleDroppedFiles(files);
    };
    surface->SetAdjustments(adjustments);
    AddChild(surface);

#ifdef ULTRACANVAS_PLUGIN_PDF
    // ----- PDF DOCUMENT VIEW (shown instead of the image surface for PDFs) -----
    // Built with a zero size so the flex layout (not a fixed CSS size) drives it.
    {
        auto pv = std::make_shared<UltraCanvasPDFView>("MV_PDFView", 0, 0, 0, 0);
        pv->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        pv->onPageChanged = [this](int, int) { UpdateInfoBar(); };
        // The page zooms with the wheel and the keyboard, so the info bar
        // reports the zoom the way it does for images.
        pv->onZoomChanged = [this](float) { UpdateInfoBar(); };
        // Page numbers drawn over the thumbnail pages (not captions beneath).
        pv->SetThumbnailNumberStyle(
            UltraCanvasPDFView::ThumbnailNumberStyle::Overlay);
        pv->SetVisible(false);
        pdfView = pv;
        AddChild(pdfView);
        // Page-inventory width and wheel zoom as configured on the viewer.
        ApplyPDFViewSettings();
    }
#endif

    // ----- SPREADSHEET VIEW (shown for ODS / CSV / TSV) -----
    {
        auto sv = std::make_shared<UltraCanvasSpreadsheet>("MV_Sheet", 0, 0, 0, 0);
        sv->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        sv->SetVisible(false);
        sheetView = sv;
        AddChild(sheetView);
    }

    // ----- 3D MODEL VIEW (shown for STL files) -----
    {
        auto mv = std::make_shared<UltraCanvasSTLElement>("MV_Model", 0, 0, 0, 0);
        mv->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        mv->onLoadError = [this](const std::string& err) {
            if (infoLabel) infoLabel->SetText("Failed to open 3D model: " + err);
        };
        mv->SetVisible(false);
        modelView = mv;
        AddChild(modelView);
    }

    // ----- TEXT / SOURCE / MARKDOWN VIEW (display only) -----
    // Display-only, not merely read-only: the area stays out of the focus chain,
    // so it shows no caret and never captures Left/Right — those keep browsing
    // the folder. The viewer scrolls it from HandleViewerKey() instead.
    {
        auto tv = std::make_shared<UltraCanvasTextArea>("MV_Text", 0, 0, 0, 0);
        tv->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        tv->SetDisplayOnly(true);
        tv->SetVisible(false);
        textView = tv;
        AddChild(textView);
    }

    // ----- HTML PAGE VIEW (rendered, display only) -----
    // An HTML page - or XML that holds XHTML - is shown as a browser shows
    // it, in the text view's place. Local files only: its style sheets and
    // pictures are read from the disk beside it, a link to another local
    // page opens here, and a web link opens in the browser. A preview sends
    // no forms and plays nothing.
    {
        auto wv = std::make_shared<UltraCanvasWebView>("MV_Web", 0, 0);
        wv->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        HTML::PageLoaderServices services;
        // A page replaces the saved web page's parts; what a page asks for
        // is answered from them first, else from the disk.
        auto archive = webArchive = std::make_shared<MediaViewerWebArchive>();
        services.fetch = [archive](const std::string& url, std::function<void(HTML::FetchResult)> done) {
            archive->parts.clear();
            done(FetchLocalFile(url, archive.get()));
        };
        services.fetchResource = [archive](const std::string& url, const std::string& /*pageUrl*/,
                                           std::function<void(HTML::FetchResult)> done) {
            auto saved = archive->parts.find(url.substr(0, url.find('#')));
            if (saved != archive->parts.end()) { done(saved->second); return; }
            done(FetchLocalFile(url, nullptr));
        };
        services.resolve = ResolveUrl;
        wv->SetServices(std::move(services));
        wv->SetInteractiveForms(false);
        wv->SetMediaPlayback(false);
        wv->SetOpensWebLinks(false);
        // A link that leaves for the web or mail opens in the system's
        // program - but one to a place in the page on show (a saved page's
        // #fragment reads against the address it was saved from) scrolls.
        UltraCanvasWebView* view = wv.get();
        wv->onExternalLink = [view](const std::string& url) {
            const HTML::LoadedPage* shown = view->GetPage();
            const size_t hash = url.find('#');
            if (shown && hash != std::string::npos && !shown->baseUrl.empty() &&
                url.compare(0, hash, shown->baseUrl.substr(0, shown->baseUrl.find('#'))) == 0) {
                view->ScrollToFragment(url.substr(hash + 1));
                return;
            }
            OpenURL(url);
        };
        wv->SetVisible(false);
        webView = wv;
        AddChild(webView);
    }

    // ----- NOTE VIEW (the viewer's own words about a file) -----
    // A *.ucd without a picture to show, a PDF that needs its password: what
    // the viewer says about them reads in its own dark colours, laid out as
    // the Details panel lays out its facts - not on the white page a text
    // file is shown on.
    {
        auto nv = std::make_shared<UltraCanvasTextArea>("MV_Note", 0, 0, 0, 0);
        nv->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        nv->ApplyDarkTheme();
        nv->SetBackgroundColor(Color(24, 24, 28, 255));
        nv->SetFontSize(12);
        nv->SetWordWrap(true);
        nv->SetShowLineNumbers(false);
        nv->SetHighlightSyntax(false);
        nv->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        nv->SetDisplayOnly(true);
        nv->SetVisible(false);
        noteView = nv;
        AddChild(noteView);
    }

    // ----- EBOOK VIEW (EPUB / FB2 / MOBI / AZW) -----
    // Complete reading widget: chapter toolbar, table of contents and reflowing
    // chapter content. Engines come from the eBook registry; registering the
    // built-ins here is idempotent, so the viewer works standalone too.
    {
        RegisterBuiltinEBookEngines();
        auto bv = std::make_shared<UltraCanvasEBookViewer>("MV_Book", 0.f, 0.f);
        bv->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        bv->SetVisible(false);
        bookView = bv;
        AddChild(bookView);
    }

    // ----- FONT BROWSER (shown for font files) -----
    // Every glyph in the file, in a scrolling grid, with a picker for the
    // ranges it covers. Rasterized straight from the file, so a font that is
    // not installed previews exactly like one that is.
    {
        auto fv = std::make_shared<UltraCanvasFontViewer>("MV_Font", 0.f, 0.f, 0.f, 0.f);
        fv->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        fv->SetVisible(false);
        fontView = fv;
        AddChild(fontView);
    }

    // ----- VECTOR DRAWING (shown for drawings this build can read) -----
    // The drawing itself, not a picture of it: the document is rendered at
    // the pane's size and stays sharp at any zoom. Which formats arrive here
    // is the Vector plugin's business (UltraCanvasVectorPreview.h) - core
    // owns the document model and this element, but no reader.
    {
        auto vv = CreateVectorElement("MV_Vector", 0, 0, 0, 0);
        vv->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        vv->SetVisible(false);
        vectorView = vv;
        AddChild(vectorView);
    }

#ifdef ULTRACANVAS_ENABLE_VIDEO
    // ----- VIDEO PLAYER (shown for video files) -----
    {
        auto vp = std::make_shared<UltraCanvasVideoPlayerElement>("MV_Video", 0, 0, 0, 0);
        vp->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        // Auto-advance to the next file when a clip ends during a slideshow.
        vp->onEnded = [this] { if (slideshowPlaying) Next(); };
        // A finished PreviewClip is left paused *and muted*; the sound comes
        // back the moment the user resumes it from the transport bar, which is
        // the only point where the pipeline is certain to be playing again.
        vp->onPlay = [this] {
            if (!videoClipUnmutePending) return;
            videoClipUnmutePending = false;
            if (!videoPlayer) return;
            auto* v = static_cast<UltraCanvasVideoPlayerElement*>(videoPlayer.get());
            if (auto p = v->GetPlayer()) p->SetMute(false);
        };
        vp->SetVisible(false);
        videoPlayer = vp;
        AddChild(videoPlayer);
    }
#endif

#ifdef ULTRACANVAS_ENABLE_AUDIO
    // ----- AUDIO PLAYER (shown for audio files) -----
    {
        auto ap = std::make_shared<UltraCanvasAudioPlayerElement>("MV_Audio", 0, 0, 0, 0);
        ap->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        ap->onEnded = [this] { if (slideshowPlaying) Next(); };
        ap->SetVisible(false);
        audioPlayer = ap;
        AddChild(audioPlayer);
    }
#endif

    // ----- BACKDROP PALETTE (directly under the picture) -----
    // Only up for a file that really has transparency (see
    // UpdateTransparencyPalette): the checkered swatch first, then greys and
    // colours. The strip sizes its own swatches to the width it gets, so it
    // fits a narrow preview pane as well as a full window.
    backdropBar = CreateBackdropSwatchBar("MV_Backdrop", 0, 0, 0, 28);
    {
        ColorSwatchBarStyle bs = backdropBar->GetStyle();
        bs.background     = Color(30, 30, 36, 255);
        bs.border         = Color(70, 70, 78, 255);
        bs.hoverBorder    = Color(210, 210, 218, 255);
        bs.selectedBorder = Color(90, 160, 240, 255);
        backdropBar->SetStyle(bs);
    }
    backdropBar->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    backdropBar->SetVisible(false);
    backdropBar->onColorSelected = [this](const Color& c) {
        SetTransparentBackground(TransparentImageBackground::SolidColor);
        SetTransparentColor(c);
        if (onTransparentBackgroundChanged) {
            onTransparentBackgroundChanged(TransparentImageBackground::SolidColor, c);
        }
    };
    backdropBar->onCheckeredSelected = [this]() {
        SetTransparentBackground(TransparentImageBackground::Checkered);
        if (onTransparentBackgroundChanged) {
            onTransparentBackgroundChanged(TransparentImageBackground::Checkered,
                                           GetTransparentColor());
        }
    };
    AddChild(backdropBar);

    // ----- BOTTOM INFO BAR -----
    bottomBar = std::make_shared<UltraCanvasContainer>("MV_Bottom", 0, 0, 0, 26);
    bottomBar->SetBackgroundColor(Color(18, 18, 22, 255));
    bottomBar->layout.SetFlexRow().SetFlexGap(8)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bottomBar->SetPadding(2, 10, 2, 10);
    bottomBar->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    infoLabel = std::make_shared<UltraCanvasLabel>("MV_InfoLabel", 0, 0, 0, 20, "No media");
    infoLabel->SetFontSize(11);
    infoLabel->SetTextColor(Color(200, 200, 206, 255));
    infoLabel->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    bottomBar->AddChild(infoLabel);

    // Up only while the shown file is password-protected and not unlocked:
    // in the info bar, so a host that hides the top bars (a preview pane)
    // still offers it.
    unlockButton = std::make_shared<UltraCanvasButton>("MV_Unlock", 0, 0, 78, 20, "Unlock");
    unlockButton->SetIcon(ViewerIconPath("unlock.svg"));
    unlockButton->SetIconSize(14, 14);
    unlockButton->SetIconPosition(ButtonIconPosition::Left);
    unlockButton->SetUseIconAsMask(true);
    unlockButton->SetFontSize(11);
    unlockButton->SetTooltip("Unlock: show this password-protected file (Enter)");
    unlockButton->onClick = [this] { Unlock(); };
    unlockButton->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    unlockButton->SetVisible(false);
    bottomBar->AddChild(unlockButton);

    auto detailsBtn = std::make_shared<UltraCanvasButton>("MV_Details", 0, 0, 28, 20, "");
    detailsBtn->SetIcon(ViewerIconPath("file-info.svg"));
    detailsBtn->SetIconSize(14, 14);
    detailsBtn->SetIconPosition(ButtonIconPosition::Center);
    detailsBtn->SetIconSpacing(0);
    detailsBtn->SetUseIconAsMask(true);
    detailsBtn->SetTooltip("Details");
    detailsBtn->onClick = [this] { ToggleDetails(); };
    detailsBtn->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    bottomBar->AddChild(detailsBtn);

    // The host's settings (SetSettingsAction); up only once there are some.
    settingsButton = std::make_shared<UltraCanvasButton>("MV_Settings", 0, 0, 28, 20, "");
    settingsButton->SetIcon(ViewerIconPath("settings.svg"));
    settingsButton->SetIconSize(14, 14);
    settingsButton->SetIconPosition(ButtonIconPosition::Center);
    settingsButton->SetIconSpacing(0);
    settingsButton->SetUseIconAsMask(true);
    settingsButton->SetTooltip("Settings");
    settingsButton->onClick = [this] { if (settingsAction) settingsAction(); };
    settingsButton->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    settingsButton->SetVisible(false);
    bottomBar->AddChild(settingsButton);
    AddChild(bottomBar);

    // ----- DETAILS PANEL (over the display area) -----
    // Out of the flex flow (absolute) and above every view; Arrange() fits it
    // over whichever view is active. Display-only like the text view, so it
    // never takes the keyboard from the browsing keys; the wheel scrolls it and
    // HandleViewerKey() gives it Up / Down / PageUp / PageDown while open.
    {
        auto dv = std::make_shared<UltraCanvasTextArea>("MV_Details_Panel", 0, 0, 0, 0);
        dv->layoutItem.SetPositionType(CSSLayout::PositionType::Absolute);
        dv->SetZIndex(OverlayZOrder::Overlays);
        dv->ApplyDarkTheme();
        dv->SetBackgroundColor(Color(22, 22, 28, 255));
        dv->SetBorders(1.0f, Color(255, 255, 255, 50), 8.0f);
        dv->SetFontSize(12);
        dv->SetWordWrap(true);
        dv->SetShowLineNumbers(false);
        dv->SetHighlightSyntax(false);
        dv->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        dv->SetDisplayOnly(true);
        dv->SetVisible(false);
        detailsView = dv;
        AddChild(detailsView);
    }

    // Nothing is on show yet, so no view / edit tool applies until a file loads.
    UpdateToolAvailability();

    (void)w; (void)h;
}

bool UltraCanvasMediaViewer::IsDocumentFile(const std::string& path) {
    // Documents rendered through a dedicated document element rather than the
    // image pipeline. PDF is handled by UltraCanvasPDFView (MuPDF) and is only
    // advertised as supported when that plugin is compiled in.
#ifdef ULTRACANVAS_PLUGIN_PDF
    return LowerExt(path) == "pdf";
#else
    (void)path;
    return false;
#endif
}

bool UltraCanvasMediaViewer::IsSpreadsheetFile(const std::string& path) {
    // Spreadsheets open in UltraCanvasSpreadsheet (ODS / XLSX / XLS / CSV / TSV)
    // - every format its LoadFromFile reads. The engine is always compiled into
    // the core library, so no backend guard is needed. (ODT is an OpenDocument
    // *text* document, not a spreadsheet — not handled.)
    std::string e = LowerExt(path);
    return e == "ods" || e == "xlsx" || e == "xls" || e == "csv" || e == "tsv";
}

bool UltraCanvasMediaViewer::GetModelViewPose(ModelViewPose& out) const {
    if (activeKind != MediaKind::Model || !modelView) return false;
    out = static_cast<const UltraCanvasSTLElement*>(modelView.get())->GetViewPose();
    return true;
}

bool UltraCanvasMediaViewer::SetModelViewPose(const ModelViewPose& pose) {
    if (activeKind != MediaKind::Model || !modelView) return false;
    static_cast<UltraCanvasSTLElement*>(modelView.get())->SetViewPose(pose);
    return true;
}

const Mesh3D* UltraCanvasMediaViewer::GetModelMesh() const {
    if (activeKind != MediaKind::Model || !modelView) return nullptr;
    return &static_cast<const UltraCanvasSTLElement*>(modelView.get())->GetMesh();
}

bool UltraCanvasMediaViewer::IsModelFile(const std::string& path) {
    // 3D models open in UltraCanvasSTLElement (OpenGL viewer, or a 2D info
    // placeholder when GL is disabled). The element is always built into the
    // core library, so no backend guard is needed.
    //
    // Which *formats* reach it is not core's to decide: .stl is core's own,
    // and everything else - OBJ, COLLADA, FBX, X3D/VRML, Alembic, MilkShape,
    // .blend, STEP - arrives when the application has registered the Models
    // plugin. Asking the seam rather than naming an extension is what stops
    // this test going stale every time a reader lands, which is exactly what
    // it did: it still said "stl" after nine other formats had readers.
    return CanPreviewModelExtension(path);
}

bool UltraCanvasMediaViewer::IsFontFile(const std::string& path) {
    // The same gate the filer's thumbnails use, so a file that previews as a
    // font in the display also opens as one in the detail pane.
    return IsFontFileExtension(path);
}

// The e-book container formats IsEBookFile claims, also offered by the Open
// dialog's "All supported media" filter.
static const std::vector<std::string>& EBookExtensions() {
    static const std::vector<std::string> exts = {
        "epub", "fb2", "mobi", "prc", "azw", "azw3"
    };
    return exts;
}

bool UltraCanvasMediaViewer::IsEBookFile(const std::string& path) {
    // e-books open in UltraCanvasEBookViewer through the engine registry
    // (EPUB / FB2 / MOBI and Kindle variants). Plain text stays in the text
    // view even though a TXT e-book engine exists, so only the dedicated
    // e-book container formats are claimed here.
    const auto& b = EBookExtensions();
    std::string e = LowerExt(path);
    return !e.empty() && std::find(b.begin(), b.end(), e) != b.end();
}

bool UltraCanvasMediaViewer::IsUCDFile(const std::string& path) {
    // UltraCanvas Document containers. Shown as the embedded preview thumbnail
    // plus header details until the UCD v2 engine lands (see LoadCurrent).
    return LowerExt(path) == "ucd";
}

bool UltraCanvasMediaViewer::IsVectorDocumentFile(const std::string& path) {
    // Three ways a vector document can be shown, in the order LoadCurrent
    // tries them: the image pipeline rasterizes it (SVG, and EPS on a libvips
    // build with a PostScript loader - those arrive here as images); a reader
    // registered through the vector preview seam turns it into a document
    // this draws itself (DXF, DWG and the rest of the Vector plugin's matrix,
    // once the application has called RegisterVectorFormatsPlugin); or the
    // file carries a preview bitmap of the drawing inside itself, which is
    // the display exactly as for a *.ucd container.
    //
    // Only the last of the three used to count, so a DXF or a DWG was not a
    // previewable file at all and the pane stayed empty for it - even in a
    // build whose Vector plugin had just read the same drawing for the
    // FileLoader.
    return CanPreviewVectorExtension(path) || IsVectorGraphicsPath(path) ||
           FormatCarriesEmbeddedPreview(path);
}

// Image / vector formats the image pipeline can rasterize. Kept in one place
// because both ClassifyFile() and IsSupportedMedia() need the same list.
static const std::vector<std::string>& ImageExtensions() {
    static const std::vector<std::string> exts = {
        "jpg", "jpeg", "jpe", "png", "gif", "bmp", "webp", "tif", "tiff",
        "svg", "svgz", "ico", "cur", "heic", "heif", "avif", "jxl", "jp2", "j2k",
        "ppm", "pgm", "pbm", "pnm", "pfm", "tga", "psd", "qoi", "hdr", "exr",
        "xpm", "xbm", "pcx", "sgi", "dds", "fits"
    };
    return exts;
}

bool UltraCanvasMediaViewer::IsImageFile(const std::string& path) {
    std::string e = LowerExt(path);
    if (e.empty()) return false;
    const auto& exts = ImageExtensions();
    return std::find(exts.begin(), exts.end(), e) != exts.end();
}

// Plain-text and markup extensions IsTextFile claims besides the syntax
// tokenizer's source languages.
static const std::vector<std::string>& PlainTextExtensions() {
    static const std::vector<std::string> exts = {
        "txt", "text", "log", "md", "markdown", "rst", "json", "xml",
        "yaml", "yml", "ini", "cfg", "conf", "toml", "html", "htm", "xhtml",
        "xht", "shtml", "eml", "emlx", "mht", "mhtml", "css",
        "tex", "srt", "vtt", "diff", "patch"
    };
    return exts;
}

bool UltraCanvasMediaViewer::IsTextFile(const std::string& path) {
    // Text / markup / source files open in a read-only UltraCanvasTextArea. A
    // curated set of plain-text & markup extensions, plus any source language
    // the syntax tokenizer recognises (so highlighting matches the editor).
    std::string e = LowerExt(path);
    if (e.empty()) return false;
    const auto& textExts = PlainTextExtensions();
    if (std::find(textExts.begin(), textExts.end(), e) != textExts.end()) return true;
    // Reuse the syntax tokenizer's language registry for source code (cpp, py,
    // js, java, …). Constructed once; mutation of its current-language state is
    // harmless here since we only need the match result.
    static SyntaxTokenizer tokenizer;
    return tokenizer.SetLanguageByExtension(e);
}

bool UltraCanvasMediaViewer::IsHtmlFile(const std::string& path) {
    const std::string e = LowerExt(path);
    if (e == "html" || e == "htm" || e == "xhtml" || e == "xht" || e == "shtml") return true;
    if (e != "xml") return false;
    // An .xml is a page when its content is XHTML; data XML is shown as text.
    return LooksLikeXhtml(ReadFileHead(path, 4096));
}

bool UltraCanvasMediaViewer::IsMailMessageFile(const std::string& path) {
#ifdef ULTRACANVAS_HAS_NET
    const std::string e = LowerExt(path);
    return e == "eml" || e == "emlx";
#else
    (void)path;
    return false;   // no MIME reader in this build: a message is shown as its text
#endif
}

bool UltraCanvasMediaViewer::IsWebArchiveFile(const std::string& path) {
#ifdef ULTRACANVAS_HAS_NET
    const std::string e = LowerExt(path);
    return e == "mht" || e == "mhtml";
#else
    (void)path;
    return false;
#endif
}

bool UltraCanvasMediaViewer::IsVideoFile(const std::string& path) {
    // The codec registry is the single source of truth: it knows which
    // containers the platform video backend was built with, honours the
    // content probe for an extension shared with another kind of file (".ts"
    // is TypeScript far more often than a transport stream), and returns true
    // for a format that is recognised but has no decoder — so the viewer can
    // show a player and the reason it is empty rather than mistaking the file
    // for a picture. With no video backend compiled in nothing is registered,
    // and this is false for everything.
    return IsMediaFileOfKind(MediaCodecKind::Video, path);
}

bool UltraCanvasMediaViewer::IsAudioFile(const std::string& path) {
    return IsMediaFileOfKind(MediaCodecKind::Audio, path);
}

MediaKind UltraCanvasMediaViewer::ClassifyFile(const std::string& path) {
    if (IsUCDFile(path))         return MediaKind::UCDoc;
    // Before the vector check: a libvips with a PDF loader counts a PDF as a
    // drawing it can rasterize, which showed its first page as a flat
    // picture - no pages, no page inventory, no password - where the PDF view
    // (built in) shows the document.
    if (IsDocumentFile(path))    return MediaKind::Document;
    // Before the text check: an EPS is PostScript source, and its own drawing
    // is the more useful answer to "what is in this file".
    if (IsVectorDocumentFile(path)) return MediaKind::Vector;
    if (IsSpreadsheetFile(path)) return MediaKind::Sheet;
    if (IsModelFile(path))       return MediaKind::Model;
    if (IsEBookFile(path))       return MediaKind::Book;
    // Before the text check: a Type 1 .pfa is ASCII the tokenizer would take
    // for source code, and its glyphs are the more useful answer.
    if (IsFontFile(path))        return MediaKind::Font;
    if (IsVideoFile(path))       return MediaKind::Video;
    if (IsAudioFile(path))       return MediaKind::Audio;
    // Images before text: SVG (and XPM / XBM) are markup the syntax tokenizer
    // recognises, so classifying by text first would preview their source code
    // instead of the picture they describe.
    if (IsImageFile(path))       return MediaKind::Image;
    if (IsTextFile(path))        return MediaKind::Text;
    return MediaKind::Image;
}

bool UltraCanvasMediaViewer::IsSupportedMedia(const std::string& path) {
    std::string e = LowerExt(path);
    if (e.empty()) return false;
    if (IsImageFile(path)) return true;
    // Documents / spreadsheets / 3D models / e-books / UCD containers / text /
    // video / audio (video & audio gated by their backend being present).
    return IsDocumentFile(path) || IsSpreadsheetFile(path) || IsModelFile(path) ||
           IsEBookFile(path) || IsUCDFile(path) || IsVectorDocumentFile(path) ||
           IsFontFile(path) ||
           IsVideoFile(path) || IsAudioFile(path) || IsTextFile(path);
}

std::vector<std::string> UltraCanvasMediaViewer::EnumerateFolder(const std::string& folder) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::is_directory(UltraCanvas::PathFromUtf8(folder), ec)) return out;
    for (fs::directory_iterator it(UltraCanvas::PathFromUtf8(folder), ec), end; it != end && !ec; it.increment(ec)) {
        std::error_code fec;
        if (it->is_regular_file(fec)) {
            std::string p = PathToUtf8(it->path());
            if (IsSupportedMedia(p)) out.push_back(p);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string UltraCanvasMediaViewer::GetCurrentPath() const {
    if (currentIndex < playlist.size()) return playlist[currentIndex];
    return "";
}

void UltraCanvasMediaViewer::OpenFolder(const std::string& folderPath,
                                        const std::string& selectFile) {
    KeepPasswordsOnlyFor(folderPath);
    playlist = EnumerateFolder(folderPath);
    currentIndex = 0;
    if (!selectFile.empty()) {
        std::string target = BaseName(selectFile);
        for (size_t i = 0; i < playlist.size(); ++i) {
            if (playlist[i] == selectFile || BaseName(playlist[i]) == target) {
                currentIndex = i;
                break;
            }
        }
    }
    currentFolder = folderPath;
    UpdateBreadcrumb();
    LoadCurrent(false);
    if (slideshowPlaying) { PauseSlideshow(); PlaySlideshow(); }
}

void UltraCanvasMediaViewer::SetFiles(const std::vector<std::string>& files, size_t startIndex) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& f : files) {
        if (fs::is_directory(f, ec)) {
            auto sub = EnumerateFolder(f);
            out.insert(out.end(), sub.begin(), sub.end());
        } else if (IsSupportedMedia(f)) {
            out.push_back(f);
        }
    }
    if (out.empty()) return;
    KeepPasswordsOnlyFor(PathToUtf8(UltraCanvas::PathFromUtf8(out.front()).parent_path()));
    playlist = std::move(out);
    currentIndex = std::min(startIndex, playlist.size() - 1);
    // Reflect the folder the (first) file lives in.
    {
        fs::path p(playlist.front());
        currentFolder = PathToUtf8(p.parent_path());
    }
    UpdateBreadcrumb();
    LoadCurrent(false);
    if (slideshowPlaying) { PauseSlideshow(); PlaySlideshow(); }
}

void UltraCanvasMediaViewer::OpenFile(const std::string& filePath) {
    std::error_code ec;
    fs::path p(UltraCanvas::PathFromUtf8(filePath));
    std::string folder = PathToUtf8(p.parent_path());
    if (folder.empty()) folder = ".";
    OpenFolder(folder, filePath);
}

// Show nothing and let go of the file. Every backend that can keep an operating
// system handle on the shown file is released here: on Windows a document engine
// that still has the file open makes a move / rename of it fail, which is exactly
// what a preview pane hosting this viewer runs into.
void UltraCanvasMediaViewer::CloseFile() {
    StopPlayback();
    ReleaseViewBackends();
    playlist.clear();
    currentIndex = 0;
    currentFolder.clear();
    UpdateBreadcrumb();
    LoadCurrent(false);   // empty playlist: clears the surface and the info bar
}

void UltraCanvasMediaViewer::ReleaseViewBackends() {
#ifdef ULTRACANVAS_PLUGIN_PDF
    // MuPDF keeps the PDF open for as long as the document object lives.
    if (pdfView) static_cast<UltraCanvasPDFView*>(pdfView.get())->SetDocument(nullptr);
#endif
    if (bookView) static_cast<UltraCanvasEBookViewer*>(bookView.get())->CloseDocument();
    if (fontView) static_cast<UltraCanvasFontViewer*>(fontView.get())->CloseFont();
    if (textView) static_cast<UltraCanvasTextArea*>(textView.get())->SetText("");
    if (noteView) static_cast<UltraCanvasTextArea*>(noteView.get())->SetText("");
    if (webView) static_cast<UltraCanvasWebView*>(webView.get())->Clear();
    // The document can be a large drawing; a closed preview must not keep it.
    if (vectorView) static_cast<UltraCanvasVectorElement*>(vectorView.get())->ClearDocument();
    DropPluginView();
    if (surface) surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
    ucdDetails.clear();
    locked = MediaLock::NotLocked;
    unlocked = false;   // what a password opened is closed with the rest
    // A stopped clip is still an OPEN clip: the decoder keeps the file until
    // it is unloaded, and on Windows that handle is what makes the file
    // impossible to rename, replace or delete - the very operations a file
    // manager offers next to the preview it just closed.
#ifdef ULTRACANVAS_ENABLE_VIDEO
    if (videoPlayer) static_cast<UltraCanvasVideoPlayerElement*>(videoPlayer.get())->Unload();
#endif
#ifdef ULTRACANVAS_ENABLE_AUDIO
    if (audioPlayer) static_cast<UltraCanvasAudioPlayerElement*>(audioPlayer.get())->Unload();
#endif
    // Spreadsheets and 3D models are parsed into memory by their loaders, so
    // they hold nothing open once loaded.
}

// Every extension the viewer opens, for the Open dialog's type toggles.
// Several kinds are decided at run time (the codec registry, the model and
// vector readers a plugin registered, the tokenizer's languages), so the
// candidates are gathered from every list those checks use and each is kept
// only when IsSupportedMedia accepts it: the toggles then offer exactly what
// browsing a folder would show. Lowercase, undotted, sorted, unique.
static std::vector<std::string> SupportedOpenExtensions() {
    std::vector<std::string> candidates = ImageExtensions();
    auto add = [&candidates](const std::vector<std::string>& exts) {
        candidates.insert(candidates.end(), exts.begin(), exts.end());
    };
    add(PlainTextExtensions());
    add(EBookExtensions());
    add({ "pdf", "ods", "xlsx", "xls", "csv", "tsv", "ucd" });
    add({ "ttf", "otf", "ttc", "otc", "pfa", "pfb", "woff", "woff2",
          "pcf", "bdf", "fon", "fnt" });
    add(PreviewableModelExtensions());
    add(PreviewableVectorExtensions());
    for (MediaCodecKind kind : { MediaCodecKind::Video, MediaCodecKind::Audio }) {
        for (const auto& codec : GetRegisteredMediaCodecs(kind)) {
            candidates.push_back(codec.extension);
            add(codec.aliases);
        }
    }
    for (MediaFormatCategory category : {
             MediaFormatCategory::Bitmap, MediaFormatCategory::Vector,
             MediaFormatCategory::Model3D, MediaFormatCategory::Document,
             MediaFormatCategory::Spreadsheet, MediaFormatCategory::Audio,
             MediaFormatCategory::Video, MediaFormatCategory::Font }) {
        add(UltraCanvasSupportedFormats::GetLoadExtensions(category));
    }
    static SyntaxTokenizer tokenizer;
    for (const auto& language : tokenizer.GetLanguageExtensions()) add(language.second);

    std::vector<std::string> out;
    for (std::string e : candidates) {
        if (!e.empty() && e.front() == '.') e.erase(0, 1);
        std::transform(e.begin(), e.end(), e.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if (e.empty() || e == "*") continue;
        if (UltraCanvasMediaViewer::IsSupportedMedia("file." + e)) out.push_back(e);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void UltraCanvasMediaViewer::ShowOpenDialog() {
    // One toggle button per kind of file rather than a list of extensions:
    // each supported extension goes to the kind the viewer would show it as.
    enum Group { Images, Audio, Video, Documents, Text, GroupCount };
    static const char* const groupNames[GroupCount] = {
        "Images", "Audio", "Video", "Documents", "Text" };
    std::vector<std::string> groups[GroupCount];
    for (const std::string& e : SupportedOpenExtensions()) {
        switch (ClassifyFile("file." + e)) {
            case MediaKind::Image:
            case MediaKind::Vector:
            case MediaKind::Model:    groups[Images].push_back(e);    break;
            case MediaKind::Audio:    groups[Audio].push_back(e);     break;
            case MediaKind::Video:    groups[Video].push_back(e);     break;
            case MediaKind::Text:     groups[Text].push_back(e);      break;
            default:                  groups[Documents].push_back(e); break;
        }
    }

    FileDialogOptions opts;
    opts.SetTitle("Open media").SetFilterToggles(true);
    // A kind this build cannot show at all (no video backend) gets no button.
    for (int g = 0; g < GroupCount; ++g) {
        if (!groups[g].empty()) opts.AddFilter(groupNames[g], groups[g]);
    }
    opts.AddFilter("All files", std::vector<std::string>{ "*" })
        .SetParentWindow(GetWindow());
    UltraCanvasFileLoader::OpenMultipleFilesDialog(opts,
            [this](DialogResult r, const std::vector<std::string>& files) {
                if (r != DialogResult::OK || files.empty()) return;
                // One picked file browses its whole folder (arrow keys / slideshow
                // then have the siblings); a multi-selection is the playlist.
                if (files.size() == 1) OpenFile(files[0]);
                else                   SetFiles(files, 0);
            });
}

void UltraCanvasMediaViewer::ShowSaveDialog() {
    if (!tools.save || !surface || !surface->GetImage() || !surface->GetImage()->IsValid()) return;

    FileDialogOptions opts;
    opts.SetTitle("Save image as")
        .SetDefaultFileName(SaveNameFor(GetCurrentPath()))
        .SetParentWindow(GetWindow());
    UltraCanvasFileLoader::AddImageSaveTypes(opts);
    // The picture's size - or the drawing's, which has none in pixels - so a
    // bitmap type's Settings offer to write it at another.
    const FileSavePicture picture = surface->GetSavePicture();
    if (picture.drawing) opts.SetDrawingSize(picture.width, picture.height);
    else                 opts.SetImageSize(picture.width, picture.height);
    UltraCanvasFileLoader::SaveFileDialog(opts,
            [this](DialogResult r, const std::string& chosen, const FileSaveImageSize& size) {
                if (r != DialogResult::OK || chosen.empty() || !surface) return;
                // A failed save says so in a message, not only in the info
                // bar: the line at the bottom is easily missed, and then the
                // save looks as if it did nothing at all.
                auto save = [this, size](const std::string& path) {
                    std::string err;
                    if (surface && surface->SaveProcessed(path, size, err)) {
                        if (infoLabel) {
                            infoLabel->SetText(size.IsOriginal()
                                    ? "Saved: " + BaseName(path)
                                    : "Saved: " + BaseName(path) + " at " + std::to_string(size.width) +
                                      " x " + std::to_string(size.height) + " px");
                        }
                        return;
                    }
                    if (infoLabel) infoLabel->SetText("Save failed: " + err);
                    UltraCanvasDialogManager::ShowError(
                            "Could not save \"" + BaseName(path) + "\".\n" + err,
                            "Save image", nullptr, GetWindow());
                };
                // The dialog asked before replacing `chosen`; a name given its
                // extension here is another file, so that one is asked about.
                const std::string path = WithSaveExtension(chosen);
                std::error_code ec;
                if (path != chosen && fs::exists(PathFromUtf8(path), ec)) {
                    UltraCanvasDialogManager::ShowConfirmation(
                            "\"" + BaseName(path) + "\" already exists.\n"
                            "Do you want to replace it?",
                            "Replace File",
                            [save, path](bool replace) { if (replace) save(path); },
                            GetWindow());
                    return;
                }
                save(path);
            });
}

void UltraCanvasMediaViewer::Next() {
    if (playlist.empty()) return;
    currentIndex = (currentIndex + 1) % playlist.size();
    LoadCurrent(true);
}

void UltraCanvasMediaViewer::Previous() {
    if (playlist.empty()) return;
    currentIndex = (currentIndex + playlist.size() - 1) % playlist.size();
    LoadCurrent(true);
}

void UltraCanvasMediaViewer::GoTo(size_t index, bool animated) {
    if (index >= playlist.size()) return;
    currentIndex = index;
    LoadCurrent(animated);
}

void UltraCanvasMediaViewer::DropPluginView() {
    if (!pluginView) return;
    // Detached before the reference goes, so the element is not destroyed
    // while it is still a child being laid out.
    auto element = pluginView;
    pluginView.reset();
    element->SetVisible(false);
    RemoveChild(element);
}

void UltraCanvasMediaViewer::ShowView(MediaKind kind) {
    activeKind = kind;
    if (surface)     surface->SetVisible(kind == MediaKind::Image);
    if (pdfView)     pdfView->SetVisible(kind == MediaKind::Document);
    if (sheetView)   sheetView->SetVisible(kind == MediaKind::Sheet);
    if (modelView)   modelView->SetVisible(kind == MediaKind::Model);
    if (textView)    textView->SetVisible(kind == MediaKind::Text);
    noteShown = false;   // ShowNote puts it up in the text view's place
    if (noteView)    noteView->SetVisible(false);
    // An HTML page is shown there too; whatever comes next, it is let go.
    if (webView && webShown) static_cast<UltraCanvasWebView*>(webView.get())->Clear();
    if (webArchive) webArchive->parts.clear();
    webShown = false;
    if (webView)     webView->SetVisible(false);
    if (bookView)    bookView->SetVisible(kind == MediaKind::Book);
    if (fontView)    fontView->SetVisible(kind == MediaKind::Font);
    if (vectorView)  vectorView->SetVisible(kind == MediaKind::Vector && !pluginView);
    if (pluginView)  pluginView->SetVisible(kind == MediaKind::Vector);
    if (videoPlayer) videoPlayer->SetVisible(kind == MediaKind::Video);
    if (audioPlayer) audioPlayer->SetVisible(kind == MediaKind::Audio);
}

void UltraCanvasMediaViewer::SetInfoBarVisible(bool visible) {
    if (infoBarVisible == visible) return;
    infoBarVisible = visible;
    if (bottomBar) bottomBar->SetVisible(visible);
    RequestRedraw();
}

void UltraCanvasMediaViewer::SetTopBarsVisible(bool visible) {
    if (topBarsVisible == visible) return;
    topBarsVisible = visible;
    if (toolbar) toolbar->SetVisible(visible);
    // The adjustments panel opens through its toolbar toggle; it never stays
    // open while the bars are hidden, and comes back with them if the toggle
    // is still on.
    ApplyToolAvailability();
    UpdateBreadcrumb();
    RequestRedraw();
}

void UltraCanvasMediaViewer::SetTransparentBackground(TransparentImageBackground mode) {
    if (surface) surface->SetTransparentBackground(mode);
    SyncBackdropSelection();
}

TransparentImageBackground UltraCanvasMediaViewer::GetTransparentBackground() const {
    return surface ? surface->GetTransparentBackground()
                   : TransparentImageBackground::SolidColor;
}

void UltraCanvasMediaViewer::SetTransparentColor(const Color& c) {
    if (surface) surface->SetTransparentColor(c);
    SyncBackdropSelection();
}

void UltraCanvasMediaViewer::SetTransparencyPaletteVisible(bool visible) {
    transparencyPaletteEnabled = visible;
    UpdateTransparencyPalette();
}

// ===== BACKDROP PALETTE =====
// The strip is only up while it means something: the shown file is an image
// (the PDF, sheet, text, … views paint their own background) and that image
// really has transparency — an alpha channel that is used, or a vector
// document. Everything else would be a row of colours changing nothing.

void UltraCanvasMediaViewer::SyncBackdropSelection() {
    if (!backdropBar || !surface) return;
    if (surface->GetTransparentBackground() == TransparentImageBackground::Checkered) {
        backdropBar->SelectCheckered();
    } else {
        // A colour the palette does not hold (one picked in a settings dialog)
        // simply leaves no swatch marked.
        backdropBar->SelectColor(surface->GetTransparentColor());
    }
}

void UltraCanvasMediaViewer::UpdateTransparencyPalette() {
    if (!backdropBar) return;
    bool show = transparencyPaletteEnabled && activeKind == MediaKind::Image &&
                surface != nullptr;
    if (show) {
        auto img = surface->GetImage();
        show = img && img->IsValid() && img->HasTransparency();
    }
    if (show != backdropBar->IsVisible()) {
        backdropBar->SetVisible(show);
        RequestRedraw();
    }
    if (show) SyncBackdropSelection();
}

Color UltraCanvasMediaViewer::GetTransparentColor() const {
    return surface ? surface->GetTransparentColor() : Color(255, 255, 255, 255);
}

void UltraCanvasMediaViewer::SetFitEnlarges(bool enlarges) {
    if (surface) surface->SetFitEnlarges(enlarges);
}

bool UltraCanvasMediaViewer::GetFitEnlarges() const {
    return surface ? surface->GetFitEnlarges() : true;
}

void UltraCanvasMediaViewer::UpdateBreadcrumb() {
    if (!breadcrumb) return;
    if (currentFolder.empty() || !topBarsVisible) {
        breadcrumb->Clear();
        breadcrumb->SetVisible(false);
        return;
    }

    // Same path mechanism the Filer uses: a leading "Computer" node listing the
    // drives / volumes, the drive (or root) node, then one node per folder — the
    // path separator never becomes a node of its own. Clicking a node (or an
    // entry of its sub-folder dropdown) browses that folder here.
    BuildFolderBreadcrumb(breadcrumb.get(), currentFolder,
                          [this](const std::string& folder) { OpenFolder(folder); });
    breadcrumb->SetVisible(true);
}

void UltraCanvasMediaViewer::LoadCurrent(bool animated) {
    if (!surface) return;

    // Stop any media that was playing before we switch away from it.
    StopVideoClipTimer();
#ifdef ULTRACANVAS_ENABLE_VIDEO
    if (videoPlayer) static_cast<UltraCanvasVideoPlayerElement*>(videoPlayer.get())->Stop();
#endif
#ifdef ULTRACANVAS_ENABLE_AUDIO
    if (audioPlayer) static_cast<UltraCanvasAudioPlayerElement*>(audioPlayer.get())->Stop();
#endif

    if (playlist.empty()) {
        ShowView(MediaKind::Image);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        ucdDetails.clear();
        locked = MediaLock::NotLocked;
        unlocked = false;
        UpdateTransparencyPalette();   // nothing shown - the strip goes away
        UpdateToolAvailability();      // and no tool has anything to act on
        UpdateInfoBar();
        if (IsDetailsVisible()) UpdateDetailedInfo();
        UpdateAbsenceWatch();          // kept passwords may still be held
        return;
    }
    if (currentIndex >= playlist.size()) currentIndex = playlist.size() - 1;
    const std::string& path = playlist[currentIndex];

    MediaKind kind = ClassifyFile(path);
    (void)kind;
    bool handled = false;
    ucdDetails.clear();   // only set while a *.ucd container is showing
    locked = MediaLock::NotLocked;
    unlocked = false;

#ifdef ULTRACANVAS_PLUGIN_PDF
    if (kind == MediaKind::Document && pdfView) {
        // Render the document through the dedicated PDF element (MuPDF), not the
        // image/libvips path. Drop the now-hidden image so it frees memory.
        ShowView(MediaKind::Document);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        auto* pv = static_cast<UltraCanvasPDFView*>(pdfView.get());
        if (!pv->LoadFromPath(path)) {
            // A failed load leaves the last document in the view: not on show
            // under this file's name.
            pv->SetDocument(nullptr);
            if (pv->GetLastOpenError() == PDFOpenError::NeedsPassword) {
                locked = MediaLock::PDF;
                ShowLockedPDFNote(path);
            } else if (infoLabel) {
                infoLabel->SetText("Failed to open document: " + BaseName(path));
            }
        }
        handled = true;
    }
#endif
    if (!handled && kind == MediaKind::Sheet && sheetView) {
        // Spreadsheets (ODS / XLSX / XLS / CSV / TSV) open in the spreadsheet engine.
        ShowView(MediaKind::Sheet);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        auto* sv = static_cast<UltraCanvasSpreadsheet*>(sheetView.get());
        if (!sv->LoadFromFile(path) && infoLabel)
            infoLabel->SetText("Failed to open spreadsheet: " + BaseName(path) +
                               " (" + sv->GetLastError() + ")");
        handled = true;
    }
    if (!handled && kind == MediaKind::Model && modelView) {
        // 3D models load into the STL element (OpenGL viewer / 2D fallback).
        ShowView(MediaKind::Model);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        auto* mv = static_cast<UltraCanvasSTLElement*>(modelView.get());
        if (!mv->LoadFromFile(path) && infoLabel)
            infoLabel->SetText("Failed to open 3D model: " + BaseName(path));
        handled = true;
    }
    if (!handled && kind == MediaKind::Text && webView && (IsHtmlFile(path) || IsMailMessageFile(path) ||
                                                        IsWebArchiveFile(path))) {
        // An HTML page - and XML that holds XHTML - is shown rendered, as a
        // browser shows it; its source is what the Texter is for. An e-mail
        // message is shown as the message, a saved web page (.mht) as the
        // page it saved (FetchLocalFile builds both).
        ShowView(MediaKind::Text);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        if (textView) textView->SetVisible(false);
        auto* wv = static_cast<UltraCanvasWebView*>(webView.get());
        wv->SetVisible(true);
        webShown = true;
        wv->Navigate(FileUrlFromPath(path));
        handled = true;
    }
    if (!handled && kind == MediaKind::Text && textView) {
        // Text / source / markdown open read-only in the text area.
        ShowView(MediaKind::Text);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        auto* ta = static_cast<UltraCanvasTextArea*>(textView.get());
        std::string content;
        std::string ext = LowerExt(path);
        bool importedDocument = false;
        if (ext == "tex" || ext == "latex" || ext == "ltx") {
            // A LaTeX file is shown as the document it describes: the reader
            // maps the article subset onto the rich-document model, which the
            // Markdown mode renders with its formulas typeset. When the file
            // is not a document (a bare formula fragment, an unreadable file)
            // the source is shown instead.
            UCRichDocument document;
            std::string error;
            if (UCWordDocumentIO::LoadLaTeX(path, document, error)) {
                RichDocumentMarkdownOptions options;
                if (!document.media.empty()) {
                    options.imageDirectory = PathToUtf8(std::filesystem::temp_directory_path()
                        / ("UltraCanvas-media-" + std::to_string(
                               std::chrono::steady_clock::now().time_since_epoch().count())));
                }
                content = document.ToMarkdown(options);
                importedDocument = true;
            }
        }
        if (!importedDocument && !ReadTextFile(path, content)) {
            if (infoLabel) infoLabel->SetText("Failed to open text file: " + BaseName(path));
        } else {
            // A .txt names no language, so the text is asked: a snippet of
            // Python saved as notes.txt is shown as Python, a Markdown
            // README.txt rendered. Prose stays plain.
            const std::string detected =
                    (ext == "txt" || ext == "text") ? DetectLanguageFromContent(content) : std::string();
            if (ext == "md" || ext == "markdown" || importedDocument || detected == "Markdown") {
                ta->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
                ta->SetHighlightSyntax(false);
            } else {
                ta->SetEditingMode(TextAreaEditingMode::PlainText);
                static SyntaxTokenizer tk;
                // A shared extension (.cls, .m) is settled by the text: a
                // LaTeX .cls is not coloured as VBA.
                const std::string sniffed = !detected.empty() ? detected
                                                              : SyntaxTokenizer::LanguageFromContent(ext, content);
                if (sniffed.empty() ? (!ext.empty() && tk.SetLanguageByExtension(ext))
                                    : tk.SetLanguage(sniffed)) {
                    ta->SetHighlightSyntax(true);
                    ta->SetProgrammingLanguage(tk.GetCurrentProgrammingLanguage());
                } else {
                    ta->SetHighlightSyntax(false);
                }
            }
            ta->SetDisplayOnly(true);   // viewer, not editor (also implies read-only)
            ta->SetText(content);
        }
        handled = true;
    }
    if (!handled && kind == MediaKind::Book && bookView) {
        // e-books open in the reading widget (chapter toolbar + TOC + content).
        ShowView(MediaKind::Book);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        auto* bv = static_cast<UltraCanvasEBookViewer*>(bookView.get());
        if (!bv->LoadDocument(path) && infoLabel)
            infoLabel->SetText("Failed to open e-book: " + BaseName(path) +
                               " (" + bv->GetLastError() + ")");
        handled = true;
    }
    if (!handled && kind == MediaKind::Font && fontView) {
        // Font files open in the glyph browser. Nothing is installed or
        // registered to show one - the grid rasterizes from the file itself.
        ShowView(MediaKind::Font);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        auto* fv = static_cast<UltraCanvasFontViewer*>(fontView.get());
        if (!fv->LoadFont(path) && infoLabel)
            infoLabel->SetText("Failed to open font: " + BaseName(path));
        handled = true;
    }
    if (!handled && kind == MediaKind::UCDoc) {
        // UltraCanvas Document container: a layered image's own picture,
        // its layers composited at full size; else the embedded preview
        // thumbnail - stored raw right after the fixed header exactly so
        // viewers can preview without parsing the body - or, without one, a
        // header summary in the text view. The details popup gets the
        // container information either way.
        UCDPreview ucd = ReadUCDPreview(path);
        ReadUCDPicture(path, ucd);
        if (ucd.lockedByPassword) locked = MediaLock::UCDImage;
        std::shared_ptr<UCImage> thumb = ucd.picture;
        if (!thumb && !ucd.thumbnail.empty()) thumb = UCImage::LoadFromMemory(ucd.thumbnail);
        const bool thumbShown = !ucd.picture && thumb && thumb->IsValid();
        ucdDetails = BuildUCDDetailsText(path, ucd, thumbShown);
        if (thumb && thumb->IsValid()) {
            ShowView(MediaKind::Image);
            surface->ShowImage(thumb, transition, transitionDurationMs, animated,
                               SurfaceAdjustmentsFor(path));
        } else {
            surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
            ShowNote(ucdDetails);
        }
        handled = true;
    }
    if (!handled && kind == MediaKind::Vector) {
        // Best picture of the drawing this build can produce, in order:
        //   1. the image pipeline, where it rasterizes the format at the size
        //      asked for (svg/svgz, and eps/ps on a libvips build with a
        //      PostScript loader);
        //   2. the drawing itself, read through the vector preview seam and
        //      rendered here - DXF, DWG and everything else a registered
        //      Vector plugin reads;
        //   3. the preview bitmap the file carries inside itself (Xara,
        //      CorelDRAW, an EPS written with one), shown the way a *.ucd
        //      container is.
        // (2) is above (3) because it is the drawing rather than a picture of
        // it taken at whatever size the authoring program chose.
        const std::string ext = LowerExt(path);
        // (1) The image pipeline, where it rasterizes the format at the size
        // asked for. That is the best picture for svg/svgz and for eps/ps on
        // a build with a PostScript loader, and it comes back as an image.
        // GetFresh: a drawing saved over since it was last shown is read
        // again, not served from the path-keyed image cache as it was.
        std::shared_ptr<UCImage> img;
        if (UltraCanvasSupportedFormats::CanImagePipelineLoad(ext))
            img = UCImage::GetFresh(path);

        // (2) The drawing itself. A reader registered through the vector
        // preview seam turns the file into a VectorDocument, which the vector
        // view draws at whatever size the pane is and keeps sharp at any
        // zoom - so this is a view of its own, not a bitmap on the image
        // surface.
        std::shared_ptr<VectorStorage::VectorDocument> drawing;
        if ((!img || !img->IsValid()) && vectorView &&
            CanPreviewVectorExtension(path)) {
            drawing = LoadVectorPreviewDocument(path);
        }
        // The previous file's plugin element, if there was one, goes now -
        // whatever this file turns out to need, it is not that.
        DropPluginView();

        // (2b) A drawing no reader turns into a document, but a registered
        // graphics plugin draws: CorelDRAW through libcdr, and anything else
        // a plugin claims. The plugin's own element is the best view of it
        // there is, so it is hosted rather than rasterized - the same choice
        // the 3D and PDF views make.
        if (!img || !img->IsValid()) {
            if (!drawing && IsVectorGraphicsPath(path)) {
                if (auto element = LoadGraphicsFile(path)) {
                    element->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                            .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
                    pluginView = element;
                    AddChild(pluginView);
                }
            }
        }
        if (pluginView) {
            ShowView(MediaKind::Vector);
            surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
            handled = true;
        } else if (drawing) {
            ShowView(MediaKind::Vector);
            surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
            auto* vv = static_cast<UltraCanvasVectorElement*>(vectorView.get());
            vv->SetDocument(drawing);
            vv->ResetView();
            handled = true;
        } else {
            ShowView(MediaKind::Image);
            // (3) The preview bitmap the file carries inside itself (Xara,
            // CorelDRAW, an EPS written with one), shown the way a *.ucd
            // container is - the only picture of a drawing this build has no
            // reader for.
            if (!img || !img->IsValid()) {
                std::vector<uint8_t> bytes = ExtractEmbeddedPreviewBytes(path);
                if (!bytes.empty()) img = UCImage::LoadFromMemory(bytes);
            }
            if (img && img->IsValid()) {
                surface->ShowImage(img, transition, transitionDurationMs, animated,
                                   SurfaceAdjustmentsFor(path));
            } else {
                // No reader for the drawing and no preview stored: say so
                // rather than leaving an empty pane the user has to interpret.
                surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
                if (infoLabel)
                    infoLabel->SetText(BaseName(path) +
                            " - no preview stored in this vector document");
            }
            handled = true;
        }
    }
#ifdef ULTRACANVAS_ENABLE_VIDEO
    if (!handled && kind == MediaKind::Video && videoPlayer) {
        ShowView(MediaKind::Video);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        auto* vp = static_cast<UltraCanvasVideoPlayerElement*>(videoPlayer.get());
        // Decide the mute *before* the source is opened, so the engine bakes it
        // into the session it builds. Muting only afterwards leaves a window in
        // which a backend that has not finished wiring its audio renderer drops
        // the request and plays the clip aloud (Media Foundation did exactly
        // that) — the whole point of the muted preview clip.
        if (auto p = vp->GetPlayer())
            p->SetMute(videoPreviewMode == VideoPreviewMode::PreviewClip);
        if (!vp->LoadFromFile(path)) {
            if (infoLabel) infoLabel->SetText("Failed to open video: " + BaseName(path));
        } else {
            ApplyVideoPreviewToCurrent();
        }
        handled = true;
    }
#endif
#ifdef ULTRACANVAS_ENABLE_AUDIO
    if (!handled && kind == MediaKind::Audio && audioPlayer) {
        ShowView(MediaKind::Audio);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        auto* ap = static_cast<UltraCanvasAudioPlayerElement*>(audioPlayer.get());
        if (!ap->LoadFromFile(path)) {
            // The player's reason names the codec and what would decode it,
            // which is the difference between a silent dead transport and an
            // answer the user can act on.
            const std::string& why = ap->GetLastError();
            if (infoLabel) {
                infoLabel->SetText(why.empty()
                                       ? "Failed to open audio: " + BaseName(path)
                                       : BaseName(path) + " - " + why);
            }
        } else {
            ap->Play();
        }
        handled = true;
    }
#endif

    if (!handled) {
        // Image — or a kind whose backend is unavailable, shown best-effort.
        ShowView(MediaKind::Image);
        // GetFresh: the image cache is keyed by path, and a picture edited
        // and saved since it was last shown must open as it is now - a file
        // manager's preview pane shows the same file again after every save.
        auto img = UCImage::GetFresh(path);
        // The adjustments (curves included) carry over to the next bitmap;
        // a drawing shown here (an SVG) takes none of them.
        surface->ShowImage(img, transition, transitionDurationMs, animated,
                           SurfaceAdjustmentsFor(path));
    }
    // The toolbar offers what this file takes - and nothing it does not.
    UpdateToolAvailability();
    // An open Curves dialog shows the new picture's histogram (none at all
    // for a file the curves do not apply to).
    if (auto dlg = curvesDialog.lock()) FillCurveHistograms(*dlg);
    // The strip of backdrop colours belongs to the file just loaded: up for a
    // transparent image, gone for everything else.
    UpdateTransparencyPalette();
    UpdateInfoBar();
    // The Details text costs a metadata read of the file; it is built only
    // while the panel is open, and SetDetailsVisible() builds it on opening.
    if (IsDetailsVisible()) UpdateDetailedInfo();
    // A protected file whose password was given in this folder - its own,
    // or one that opened another file here - shows without asking again.
    if (IsLocked()) UnlockFromRememberedPasswords();
    UpdateAbsenceWatch();
}

void UltraCanvasMediaViewer::ApplyAdjustments() {
    // A file the adjustments do not apply to keeps showing unaltered, even
    // while an open Curves dialog is still being dragged.
    if (surface) surface->SetAdjustments(tools.colour ? adjustments : MediaAdjustments());
}

// ===== TOOLS FOR THE SHOWN FILE =====

bool UltraCanvasMediaViewer::TakesColourAdjustments(const std::string& path) {
    if (ClassifyFile(path) != MediaKind::Image) return false;
    // SVG is markup the image pipeline rasterizes - a drawing, not pixels, so
    // a gamma curve or a sharpen would be edits of a throwaway rendering.
    const std::string ext = LowerExt(path);
    return ext != "svg" && ext != "svgz";
}

MediaAdjustments UltraCanvasMediaViewer::SurfaceAdjustmentsFor(const std::string& path) const {
    return TakesColourAdjustments(path) ? adjustments : MediaAdjustments();
}

void UltraCanvasMediaViewer::UpdateToolAvailability() {
    MediaViewerTools t;
    const std::string path = GetCurrentPath();
    switch (activeKind) {
        case MediaKind::Image: {
            // Only what actually shows: an empty surface, or a file whose
            // pixels did not decode, has nothing to act on.
            auto img = surface ? surface->GetImage() : nullptr;
            if (path.empty() || !img || !img->IsValid() || surface->HasDecodeFailure()) break;
            t.zoom = true;
            // A *.ucd stands in for a document - its thumbnail, or a layered
            // image's layers composited: it can be looked at more closely,
            // but it is not an image to turn or save here.
            if (ClassifyFile(path) == MediaKind::UCDoc) break;
            t.transform = true;
            t.save = true;
            t.colour = TakesColourAdjustments(path);
            break;
        }
        case MediaKind::Document:   // PDF page zoom
        case MediaKind::Book:       // reading text scale
            t.zoom = true;
            break;
        case MediaKind::Vector:
            // The vector view zooms; a graphics plugin's own element is
            // hosted as it is and brings whatever controls it has.
            t.zoom = !pluginView && vectorView != nullptr;
            break;
        default:
            // Text, spreadsheet, font, 3D model, video, audio: their views
            // carry their own controls (or need none from this row).
            break;
    }
    t.unlock = IsLocked();
    tools = t;
    // (The surface already holds the right adjustments: every load that puts
    // a picture on it passes SurfaceAdjustmentsFor() along.)
    ApplyToolAvailability();
}

void UltraCanvasMediaViewer::ApplyToolAvailability() {
    if (toolbar) {
        auto show = [this](const char* id, bool on) {
            if (auto item = toolbar->GetWidget(id)) item->SetVisible(on);
        };
        for (const char* id : { "mv_zoomout", "mv_zoom", "mv_zoomin", "mv_fit" }) show(id, tools.zoom);
        for (const char* id : { "mv_rotl", "mv_rotr", "mv_mirh", "mv_mirv" })   show(id, tools.transform);
        show("mv_adjust", tools.colour);
        show("mv_curves", tools.colour);
        show("mv_save",   tools.save);
        show("mv_unlock", tools.unlock);
        // A separator only between two groups that are both up: the
        // navigation / slideshow group is always there, the view and edit
        // groups (Unlock among the latter) only for a file that takes them.
        const bool edit = tools.colour || tools.save || tools.unlock;
        show("mv_sep2", tools.zoom || tools.transform || edit);
        show("mv_sep3", tools.zoom && tools.transform);
        show("mv_sep4", (tools.zoom || tools.transform) && edit);
    }
    if (adjustPanel) adjustPanel->SetVisible(adjustPanelOpen && tools.colour && topBarsVisible);
    if (unlockButton) unlockButton->SetVisible(tools.unlock);
    RequestRedraw();
}

// ===== PASSWORD-PROTECTED FILES =====

void UltraCanvasMediaViewer::ShowLockedPDFNote(const std::string& path) {
    if (!noteView && infoLabel) infoLabel->SetText(BaseName(path) + " is password-protected");
    ShowNote(BaseName(path) + " is password-protected\n\n"
             "Unlock, in the bar below (or Enter), asks for its password and opens it.\n");
}

void UltraCanvasMediaViewer::Unlock() {
    AskForPassword(std::string());
}

void UltraCanvasMediaViewer::AskForPassword(const std::string& note) {
    // One question at a time: a second click on Unlock while it is up asks nothing.
    if (!IsLocked() || askingForPassword) return;
    const std::string path = GetCurrentPath();
    const std::string prompt = (note.empty() ? std::string() : note + "\n") +
                               "Password for " + BaseName(path) + ":";
    std::weak_ptr<UltraCanvasUIElement> self = weak_from_this();
    askingForPassword = true;
    UltraCanvasDialogManager::ShowInputDialog(prompt, "Unlock " + BaseName(path), std::string(),
        InputType::Password,
        [this, self, path](DialogResult r, const std::string& password) {
            if (self.expired()) return;
            askingForPassword = false;
            if (r != DialogResult::OK) return;
            // Typed in the dialog's window, which the activity watch does not see.
            lastActivity = std::chrono::steady_clock::now();
            // Browsed on while the question was up: the answer was for another file.
            if (GetCurrentPath() != path || !IsLocked()) return;
            std::string error;
            const MediaUnlockResult result = UnlockWithPassword(password, error);
            if (result == MediaUnlockResult::WrongPassword) {
                // Asked again on the next turn of the loop, once the dialog
                // that answered has closed.
                auto* app = UltraCanvasApplicationBase::GetCurrent();
                if (!app) return;
                app->PostToUIThread([this, self, path, error]() {
                    if (!self.expired() && GetCurrentPath() == path) AskForPassword(error);
                });
            } else if (result == MediaUnlockResult::Failed) {
                UltraCanvasDialogManager::ShowError("Could not open " + BaseName(path) + "\n" + error,
                                                    "Unlock", nullptr, GetWindow());
            }
        }, GetWindow());
}

MediaUnlockResult UltraCanvasMediaViewer::UnlockWithPassword(const std::string& password,
                                                             std::string& error) {
    const std::string path = GetCurrentPath();
    if (!IsLocked() || path.empty()) {
        error = "The file is not locked";
        return MediaUnlockResult::NotLocked;
    }
    if (password.empty()) {
        error = "Type the password.";
        return MediaUnlockResult::WrongPassword;
    }

    if (locked == MediaLock::UCDImage) {
        UCDPreview ucd = ReadUCDPreview(path);
        ReadUCDPicture(path, ucd, password);
        if (ucd.wrongPassword) {
            error = "That password is not right.";
            return MediaUnlockResult::WrongPassword;
        }
        if (!ucd.picture) {
            error = ucd.pictureProblem.empty() ? "it holds no picture" : ucd.pictureProblem;
            return MediaUnlockResult::Failed;
        }
        locked = MediaLock::NotLocked;
        ucdDetails = BuildUCDDetailsText(path, ucd, false);
        ShowView(MediaKind::Image);
        surface->ShowImage(ucd.picture, MediaTransition::NoTransition, 0, false,
                           SurfaceAdjustmentsFor(path));
    }
#ifdef ULTRACANVAS_PLUGIN_PDF
    else if (locked == MediaLock::PDF && pdfView) {
        auto* pv = static_cast<UltraCanvasPDFView*>(pdfView.get());
        if (!pv->LoadFromPath(path, password)) {
            if (pv->GetLastOpenError() == PDFOpenError::WrongPassword) {
                error = "That password is not right.";
                return MediaUnlockResult::WrongPassword;
            }
            error = "the document would not open";
            return MediaUnlockResult::Failed;
        }
        locked = MediaLock::NotLocked;
        if (noteView) static_cast<UltraCanvasTextArea*>(noteView.get())->SetText("");
        ShowView(MediaKind::Document);
    }
#endif
    else {
        error = "this build cannot open it";
        return MediaUnlockResult::Failed;
    }

    unlocked = true;
    // A password given - typed, or passed in by the host - is the user's
    // doing; the kept ones tried as a file is shown are not, or an unattended
    // slideshow through protected files would never be idle.
    if (!tryingKeptPasswords) lastActivity = std::chrono::steady_clock::now();
    RememberPassword(path, password);
    UpdateAbsenceWatch();
    UpdateToolAvailability();
    UpdateInfoBar();
    if (IsDetailsVisible()) UpdateDetailedInfo();
    RequestRedraw();
    return MediaUnlockResult::Unlocked;
}

namespace {
    std::string BufferText(const UltraCryptSecureBuffer& buffer) {
        return buffer.IsEmpty() ? std::string()
                                : std::string(reinterpret_cast<const char*>(buffer.Data()), buffer.GetSize());
    }

    // The same folder however it was written: "a/b", "a/b/", "a/./b".
    bool SameFolder(const std::string& a, const std::string& b) {
        auto normal = [](const std::string& s) {
            fs::path p = UltraCanvas::PathFromUtf8(s).lexically_normal();
            if (!p.has_filename() && p.has_parent_path() && p != p.root_path()) p = p.parent_path();
            return p;
        };
        return normal(a) == normal(b);
    }
}

void UltraCanvasMediaViewer::DropKeptPasswords() {
    rememberedPasswords.clear();
    passwordForFile.clear();
    noRememberedPasswordFits.clear();
    passwordFolder.clear();
}

void UltraCanvasMediaViewer::ForgetPasswords() {
    DropKeptPasswords();
    // What a password opened goes back behind its Unlock: loaded again, with
    // nothing kept to open it.
    if (unlocked) LoadCurrent(false);
    UpdateAbsenceWatch();
}

void UltraCanvasMediaViewer::SetRememberPasswords(bool remember) {
    rememberPasswords = remember;
    if (!remember) DropKeptPasswords();
    UpdateAbsenceWatch();
}

void UltraCanvasMediaViewer::KeepPasswordsOnlyFor(const std::string& folder) {
    if (!passwordFolder.empty() && !SameFolder(folder, passwordFolder)) DropKeptPasswords();
}

void UltraCanvasMediaViewer::SetForgetPasswordsAfterIdle(std::chrono::seconds idle) {
    forgetAfterIdle = std::max(idle, std::chrono::seconds(0));
    // The timer's period follows the setting: started again at the new one.
    if (idleTimer) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(idleTimer);
        idleTimer = 0;
    }
    UpdateAbsenceWatch();
}

bool UltraCanvasMediaViewer::ForgetPasswordsIfIdle() {
    if (forgetAfterIdle.count() == 0 || (!unlocked && rememberedPasswords.empty())) return false;
    if (std::chrono::steady_clock::now() - lastActivity < forgetAfterIdle) return false;
    ForgetPasswords();
    return true;
}

void UltraCanvasMediaViewer::SetSettingsAction(std::function<void()> action, const std::string& tooltip) {
    settingsAction = std::move(action);
    if (!settingsButton) return;
    settingsButton->SetTooltip(tooltip);
    settingsButton->SetVisible(static_cast<bool>(settingsAction));
    RequestRedraw();
}

void UltraCanvasMediaViewer::UpdateAbsenceWatch() {
    const bool holding = unlocked || !rememberedPasswords.empty();
    if (holding && !sessionLockListener) {
        // Called on the UI thread; the destructor removes it before `this` goes.
        sessionLockListener = AddSessionLockListener([this](bool sessionLocked) {
            if (sessionLocked) ForgetPasswords();
        });
    } else if (!holding && sessionLockListener) {
        RemoveSessionLockListener(sessionLockListener);
        sessionLockListener = 0;
    }

    auto* app = UltraCanvasApplication::GetInstance();
    const bool timed = holding && forgetAfterIdle.count() > 0;
    if (timed && !idleTimer && app) {
        // Looked at a few times per idle period - at most every 15 s - so a
        // password goes within a quarter of the period after it is due.
        const auto period = std::clamp<std::chrono::milliseconds>(
                std::chrono::duration_cast<std::chrono::milliseconds>(forgetAfterIdle) / 4,
                std::chrono::seconds(1), std::chrono::seconds(15));
        idleTimer = app->StartTimer(static_cast<unsigned int>(period.count()), true,
                                    [this](TimerId) { ForgetPasswordsIfIdle(); });
    } else if (!timed && idleTimer) {
        if (app) app->StopTimer(idleTimer);
        idleTimer = 0;
    }
}

void UltraCanvasMediaViewer::RememberPassword(const std::string& path, const std::string& password) {
    if (!rememberPasswords || password.empty()) return;
    const std::string folder = PathToUtf8(UltraCanvas::PathFromUtf8(path).parent_path());
    KeepPasswordsOnlyFor(folder);
    passwordFolder = folder;
    // One buffer per distinct password, the newest first.
    std::shared_ptr<UltraCryptSecureBuffer> kept;
    for (auto it = rememberedPasswords.begin(); it != rememberedPasswords.end(); ++it) {
        if ((*it)->GetSize() == password.size() &&
            std::memcmp((*it)->Data(), password.data(), password.size()) == 0) {
            kept = *it;
            rememberedPasswords.erase(it);
            break;
        }
    }
    if (!kept) {
        kept = std::make_shared<UltraCryptSecureBuffer>(password.data(), password.size());
        // A password not held before may open files none of the others did.
        noRememberedPasswordFits.clear();
    }
    rememberedPasswords.insert(rememberedPasswords.begin(), kept);
    if (rememberedPasswords.size() > kMaxRememberedPasswords) {
        const auto dropped = rememberedPasswords.back();
        rememberedPasswords.pop_back();
        for (auto it = passwordForFile.begin(); it != passwordForFile.end();) {
            it = it->second == dropped ? passwordForFile.erase(it) : std::next(it);
        }
    }
    passwordForFile[path] = kept;
    noRememberedPasswordFits.erase(path);
}

void UltraCanvasMediaViewer::UnlockFromRememberedPasswords() {
    const std::string path = GetCurrentPath();
    if (!IsLocked() || path.empty() || rememberedPasswords.empty()) return;
    if (!SameFolder(PathToUtf8(UltraCanvas::PathFromUtf8(path).parent_path()), passwordFolder)) return;
    // The one that opened this file before, else - once - every one kept.
    std::vector<std::shared_ptr<UltraCryptSecureBuffer>> order;
    if (auto it = passwordForFile.find(path); it != passwordForFile.end()) order.push_back(it->second);
    else if (noRememberedPasswordFits.count(path)) return;
    else order = rememberedPasswords;
    tryingKeptPasswords = true;
    MediaUnlockResult result = MediaUnlockResult::WrongPassword;
    for (const auto& candidate : order) {
        std::string error;
        result = UnlockWithPassword(BufferText(*candidate), error);
        // Opened - or right, but it would not open: Unlock says so.
        if (result != MediaUnlockResult::WrongPassword) break;
    }
    tryingKeptPasswords = false;
    if (result != MediaUnlockResult::WrongPassword) return;
    // None fits (the file may have been given another password since): not
    // tried again until another password is kept.
    passwordForFile.erase(path);
    noRememberedPasswordFits.insert(path);
}

void UltraCanvasMediaViewer::ResetAdjustments() {
    // Move the controls first (silently — one re-render, not one per slider),
    // then drop the values themselves, curves included.
    suppressAdjustCallbacks = true;
    for (auto& reset : adjustResetters) reset();
    suppressAdjustCallbacks = false;

    adjustments = MediaAdjustments();
    if (auto dlg = curvesDialog.lock()) dlg->SetCurves(adjustments.curves);
    ApplyAdjustments();
}

// ===== CURVES DIALOG =====

void UltraCanvasMediaViewer::FillCurveHistograms(UltraCanvasCurvesDialog& dialog) const {
    // The previous picture's histogram goes first: a file the curves do not
    // apply to (a drawing, a video, a PDF) gets none rather than a stale one.
    for (ToneCurveChannel channel : { ToneCurveChannel::RGB, ToneCurveChannel::Red,
                                      ToneCurveChannel::Green, ToneCurveChannel::Blue }) {
        dialog.SetHistogram(channel, {});
    }
    if (!tools.colour) return;
#ifdef HAS_LIBVIPS
    if (!surface) return;
    auto img = surface->GetImage();
    if (!img || !img->IsValid()) return;
    try {
        vips::VImage v = img->GetVImage();
        PixelFX::PFXImage p(v);
        // Histograms describe the tones the curve maps, so they are taken in
        // sRGB — the space the lookup tables are applied in.
        p = PixelFX::Colour::ToSrgb(p);
        PixelFX::PFXImage hist = PixelFX::Colour::HistFind(p);
        if (hist.bands() <= 0) return;

        std::vector<uint32_t> master(256, 0), red(256, 0), green(256, 0), blue(256, 0);
        for (int level = 0; level < 256; ++level) {
            std::vector<double> counts = PixelFX::Arithmetic::GetPoint(hist, level, 0);
            if (counts.empty()) continue;
            double r = counts[0];
            double g = counts.size() > 1 ? counts[1] : counts[0];
            double b = counts.size() > 2 ? counts[2] : counts[0];
            red[level]    = static_cast<uint32_t>(std::max(0.0, r));
            green[level]  = static_cast<uint32_t>(std::max(0.0, g));
            blue[level]   = static_cast<uint32_t>(std::max(0.0, b));
            master[level] = red[level] + green[level] + blue[level];
        }
        dialog.SetHistogram(ToneCurveChannel::RGB,   master);
        dialog.SetHistogram(ToneCurveChannel::Red,   red);
        dialog.SetHistogram(ToneCurveChannel::Green, green);
        dialog.SetHistogram(ToneCurveChannel::Blue,  blue);
    } catch (...) {
        // A colourspace the histogram pass cannot handle simply leaves the
        // curve grid without its backdrop.
    }
#else
    (void)dialog;
#endif
}

void UltraCanvasMediaViewer::ShowCurvesDialog() {
    // Curves work on the bitmap pipeline; other views have no pixels to map,
    // and a drawing's pixels are only a rendering of it. The toolbar hides
    // the button then; this guards any other way in.
    if (!tools.colour) {
        if (infoLabel) infoLabel->SetText("Curves apply to bitmap images only");
        return;
    }

    // A dialog that is still open comes back to the front with the current
    // curves; one that was closed a moment ago (it is dropped from the
    // application's window list on the next loop pass) is replaced.
    if (auto existing = curvesDialog.lock()) {
        if (existing->GetState() != WindowState::Closed &&
            existing->GetState() != WindowState::Closing) {
            existing->SetCurves(adjustments.curves);
            existing->Show();
            return;
        }
        curvesDialog.reset();
    }

    curvesBeforeDialog = adjustments.curves;
    auto dlg = std::make_shared<UltraCanvasCurvesDialog>(adjustments.curves);
    FillCurveHistograms(*dlg);

    // Live preview on the image itself — the surface is already showing it at
    // full size, so the dialog needs no thumbnail of its own.
    dlg->onCurvesChanged = [this](const ToneCurveSet& set) {
        adjustments.curves = set;
        ApplyAdjustments();
    };
    dlg->onAccept = [this](const ToneCurveSet& set) {
        adjustments.curves = set;
        ApplyAdjustments();
        UpdateInfoBar();
    };
    dlg->onCancel = [this]() {
        adjustments.curves = curvesBeforeDialog;
        ApplyAdjustments();
    };

    curvesDialog = dlg;
    dlg->Create();      // hands ownership to the application window list
    dlg->Show();
}

// ===== PDF DISPLAY SETTINGS =====
// The viewer, not the PDF view, is the host's point of contact: it remembers
// the choice and re-applies it, so a host can set it once and every document
// opened later follows.

void UltraCanvasMediaViewer::ApplyPDFViewSettings() {
#ifdef ULTRACANVAS_PLUGIN_PDF
    if (!pdfView) return;
    auto* pv = static_cast<UltraCanvasPDFView*>(pdfView.get());
    if (pdfThumbAbsolute) pv->SetThumbnailWidth(pdfThumbWidthPx);
    else                  pv->SetThumbnailWidthFraction(pdfThumbWidthFraction);
    pv->SetWheelAction(documentWheelZoom
            ? UltraCanvasPDFView::WheelAction::Zoom
            : UltraCanvasPDFView::WheelAction::Scroll);
#endif
}

void UltraCanvasMediaViewer::SetPDFThumbnailWidth(int pixels) {
    pdfThumbAbsolute = true;
    pdfThumbWidthPx  = std::max(16, pixels);
    ApplyPDFViewSettings();
}

void UltraCanvasMediaViewer::SetPDFThumbnailWidthFraction(float share) {
    pdfThumbAbsolute      = false;
    pdfThumbWidthFraction = std::clamp(share, 0.05f, 0.5f);
    ApplyPDFViewSettings();
}

void UltraCanvasMediaViewer::SetDocumentWheelZoom(bool zoom) {
    documentWheelZoom = zoom;
    ApplyPDFViewSettings();
}

// ===== ZOOM ACTIONS (routed to whichever view is live) =====

UltraCanvasVectorElement* UltraCanvasMediaViewer::ActiveVectorView() const {
    // The drawing view, while it is the one showing. A plugin-built element
    // in its place is not a vector element.
    if (activeKind != MediaKind::Vector || pluginView || !vectorView) return nullptr;
    return static_cast<UltraCanvasVectorElement*>(vectorView.get());
}

void UltraCanvasMediaViewer::ZoomInAction() {
#ifdef ULTRACANVAS_PLUGIN_PDF
    if (activeKind == MediaKind::Document && pdfView) {
        static_cast<UltraCanvasPDFView*>(pdfView.get())->ZoomIn();
        return;
    }
#endif
    if (activeKind == MediaKind::Book && bookView) {
        static_cast<UltraCanvasEBookViewer*>(bookView.get())->ZoomIn();
        return;
    }
    if (auto* vv = ActiveVectorView()) { vv->SetZoom(vv->GetZoom() * 1.25f); return; }
    if (activeKind == MediaKind::Image && surface) surface->ZoomBy(1.25);
}

void UltraCanvasMediaViewer::ZoomOutAction() {
#ifdef ULTRACANVAS_PLUGIN_PDF
    if (activeKind == MediaKind::Document && pdfView) {
        static_cast<UltraCanvasPDFView*>(pdfView.get())->ZoomOut();
        return;
    }
#endif
    if (activeKind == MediaKind::Book && bookView) {
        static_cast<UltraCanvasEBookViewer*>(bookView.get())->ZoomOut();
        return;
    }
    if (auto* vv = ActiveVectorView()) { vv->SetZoom(vv->GetZoom() / 1.25f); return; }
    if (activeKind == MediaKind::Image && surface) surface->ZoomBy(1.0 / 1.25);
}

void UltraCanvasMediaViewer::ZoomFitAction() {
#ifdef ULTRACANVAS_PLUGIN_PDF
    if (activeKind == MediaKind::Document && pdfView) {
        static_cast<UltraCanvasPDFView*>(pdfView.get())->ZoomToFit();
        return;
    }
#endif
    if (activeKind == MediaKind::Book && bookView) {
        // Books reflow, so "fit" means a comfortable line measure across the pane.
        static_cast<UltraCanvasEBookViewer*>(bookView.get())->ZoomToWidth();
        return;
    }
    if (auto* vv = ActiveVectorView()) { vv->ZoomToFit(); return; }
    if (activeKind == MediaKind::Image && surface) surface->ResetView();
}

void UltraCanvasMediaViewer::ZoomPercentAction(double percent) {
#ifdef ULTRACANVAS_PLUGIN_PDF
    if (activeKind == MediaKind::Document && pdfView) {
        static_cast<UltraCanvasPDFView*>(pdfView.get())->SetZoom(static_cast<float>(percent / 100.0));
        return;
    }
#endif
    if (activeKind == MediaKind::Book && bookView) {
        static_cast<UltraCanvasEBookViewer*>(bookView.get())
                ->SetZoom(static_cast<float>(percent / 100.0));
        return;
    }
    if (auto* vv = ActiveVectorView()) { vv->SetZoom(static_cast<float>(percent / 100.0)); return; }
    if (activeKind == MediaKind::Image && surface) surface->SetZoomPercent(percent);
}

void UltraCanvasMediaViewer::UpdateInfoBar() {
    if (!infoLabel) return;
    if (playlist.empty()) {
        infoLabel->SetText("No media");
        return;
    }
    const std::string& path = playlist[currentIndex];

    if (!ucdDetails.empty()) {
        // A *.ucd container is showing (as its preview thumbnail or its header
        // summary) — label it as a UCD regardless of which view carries it.
        std::ostringstream os;
        os << BaseName(path) << "   \xC2\xB7   UC DOCUMENT";
        if (locked != MediaLock::NotLocked) os << "   \xC2\xB7   password-protected";
        else if (unlocked) os << "   \xC2\xB7   unlocked";
        std::error_code ec;
        auto sz = fs::file_size(UltraCanvas::PathFromUtf8(path), ec);
        if (!ec) os << "   \xC2\xB7   " << HumanSize(sz);
        os << "   \xC2\xB7   " << (currentIndex + 1) << " / " << playlist.size();
        infoLabel->SetText(os.str());
        return;
    }

#ifdef ULTRACANVAS_PLUGIN_PDF
    if (locked == MediaLock::PDF) {
        std::ostringstream os;
        os << BaseName(path) << "   \xC2\xB7   PDF   \xC2\xB7   password-protected";
        std::error_code ec;
        auto sz = fs::file_size(UltraCanvas::PathFromUtf8(path), ec);
        if (!ec) os << "   \xC2\xB7   " << HumanSize(sz);
        os << "   \xC2\xB7   " << (currentIndex + 1) << " / " << playlist.size();
        infoLabel->SetText(os.str());
        return;
    }
    if (activeKind == MediaKind::Document && pdfView) {
        auto* pv = static_cast<UltraCanvasPDFView*>(pdfView.get());
        std::ostringstream os;
        os << BaseName(path) << "   \xC2\xB7   PDF";
        // Opened with its password: protected, though it looks like any other.
        if (unlocked) os << "   \xC2\xB7   unlocked";
        if (pv->HasDocument()) {
            os << "   \xC2\xB7   page " << pv->GetCurrentPage()
               << " / " << pv->GetPageCount();
        }
        std::error_code ec;
        auto sz = fs::file_size(UltraCanvas::PathFromUtf8(path), ec);
        if (!ec) os << "   \xC2\xB7   " << HumanSize(sz);
        os << "   \xC2\xB7   " << (currentIndex + 1) << " / " << playlist.size();
        if (pv->HasDocument()) {
            char zbuf[32];
            snprintf(zbuf, sizeof(zbuf), "%.0f%%", pv->GetZoomPercent());
            os << "   \xC2\xB7   " << zbuf;
        }
        infoLabel->SetText(os.str());
        return;
    }
#endif

    if (activeKind == MediaKind::Font && fontView) {
        // A font says what it is and how much of it there is - the glyph count
        // is the number you compare two downloads by, the way a PDF's page
        // count is.
        auto* fv = static_cast<UltraCanvasFontViewer*>(fontView.get());
        const UltraCanvasFontFace& face = fv->GetFace();
        std::ostringstream os;
        os << BaseName(path) << "   \xC2\xB7   FONT";
        if (face.IsOpen()) {
            const FontFaceInfo& info = face.Info();
            if (!info.family.empty()) os << "   \xC2\xB7   " << info.family;
            os << "   \xC2\xB7   " << face.Glyphs().size() << " glyphs";
            if (fv->GetFaceCount() > 1)
                os << "   \xC2\xB7   face " << (fv->GetFaceIndex() + 1)
                   << " / " << fv->GetFaceCount();
        }
        std::error_code ec;
        auto sz = fs::file_size(UltraCanvas::PathFromUtf8(path), ec);
        if (!ec) os << "   \xC2\xB7   " << HumanSize(sz);
        os << "   \xC2\xB7   " << (currentIndex + 1) << " / " << playlist.size();
        infoLabel->SetText(os.str());
        return;
    }

    if (activeKind == MediaKind::Sheet || activeKind == MediaKind::Model ||
        activeKind == MediaKind::Text || activeKind == MediaKind::Book ||
        activeKind == MediaKind::Video || activeKind == MediaKind::Audio) {
        std::string kindLabel = activeKind == MediaKind::Video ? "VIDEO"
                              : activeKind == MediaKind::Audio ? "AUDIO"
                              : activeKind == MediaKind::Model ? "3D MODEL"
                              : activeKind == MediaKind::Book  ? "EBOOK"
                              : activeKind == MediaKind::Text  ? (!webShown ? "TEXT"
                                                                  : IsMailMessageFile(path) ? "EMAIL"
                                                                  : IsWebArchiveFile(path) ? "WEB ARCHIVE" : "HTML")
                                                               : "SHEET";
        std::ostringstream os;
        os << BaseName(path) << "   \xC2\xB7   " << kindLabel;
        std::error_code ec;
        auto sz = fs::file_size(UltraCanvas::PathFromUtf8(path), ec);
        if (!ec) os << "   \xC2\xB7   " << HumanSize(sz);
        os << "   \xC2\xB7   " << (currentIndex + 1) << " / " << playlist.size();
        infoLabel->SetText(os.str());
        return;
    }

    if (surface && surface->HasDecodeFailure()) {
        // The header read (so the size is known) but the pixels did not.
        auto img = surface->GetImage();
        std::ostringstream os;
        os << BaseName(path)
           << "   \xC2\xB7   " << img->GetWidth() << " x " << img->GetHeight();
        std::error_code ec;
        auto sz = fs::file_size(UltraCanvas::PathFromUtf8(path), ec);
        if (!ec) os << "   \xC2\xB7   " << HumanSize(sz);
        os << "   \xC2\xB7   cannot decode - " << DecodeFailureReason(img->errorMessage)
           << "   \xC2\xB7   " << (currentIndex + 1) << " / " << playlist.size();
        infoLabel->SetText(os.str());
        return;
    }
    if (!surface || !surface->GetImage() || !surface->GetImage()->IsValid()) {
        infoLabel->SetText("No media");
        return;
    }
    auto img = surface->GetImage();
    std::ostringstream os;
    os << BaseName(path)
       << "   \xC2\xB7   " << img->GetWidth() << " x " << img->GetHeight();

    std::error_code ec;
    auto sz = fs::file_size(UltraCanvas::PathFromUtf8(path), ec);
    if (!ec) os << "   \xC2\xB7   " << HumanSize(sz);

    std::string ext = LowerExt(path);
    if (!ext.empty()) {
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return (char)std::toupper(c); });
        os << "   \xC2\xB7   " << ext;
    }

    os << "   \xC2\xB7   " << (currentIndex + 1) << " / " << playlist.size();

    char zbuf[32];
    snprintf(zbuf, sizeof(zbuf), "%.0f%%", surface->GetZoomPercent());
    os << "   \xC2\xB7   " << zbuf;

    int rot = surface->GetRotationQuarters();
    if (rot != 0) os << "   \xC2\xB7   " << (rot * 90) << "\xC2\xB0";

    infoLabel->SetText(os.str());
}

void UltraCanvasMediaViewer::UpdateDetailedInfo() {
    if (!surface) return;
    if (playlist.empty()) {
        ShowDetailsText("No media\n");
        return;
    }
    const std::string& path = playlist[currentIndex];

    if (!ucdDetails.empty()) {
        // The UCD container summary was built while loading the file.
        ShowDetailsText(ucdDetails);
        return;
    }

    if (activeKind == MediaKind::Book && bookView) {
        auto* bv = static_cast<UltraCanvasEBookViewer*>(bookView.get());
        std::ostringstream bos;
        bos << "eBook information\n\n";
        bos << "File: " << BaseName(path) << "\n";
        bos << "Path: " << path << "\n";
        std::error_code bec;
        auto bsz = fs::file_size(UltraCanvas::PathFromUtf8(path), bec);
        if (!bec) bos << "Size: " << HumanSize(bsz) << "\n";
        std::string ext = LowerExt(path);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return (char)std::toupper(c); });
        bos << "Type: eBook (" << ext << ")\n";
        if (const EBookMetadata* md = bv->GetMetadata()) {
            if (!md->title.empty()) bos << "Title: " << md->title << "\n";
            if (!md->authors.empty()) {
                bos << "Author: ";
                for (size_t i = 0; i < md->authors.size(); ++i) {
                    if (i) bos << ", ";
                    bos << md->authors[i];
                }
                bos << "\n";
            }
            if (!md->publisher.empty()) bos << "Publisher: " << md->publisher << "\n";
            if (!md->language.empty())  bos << "Language: " << md->language << "\n";
        }
        if (bv->IsDocumentLoaded())
            bos << "Chapters: " << bv->GetChapterCount() << "\n";
        ShowDetailsText(bos.str());
        return;
    }

#ifdef ULTRACANVAS_PLUGIN_PDF
    if (locked == MediaLock::PDF) {
        std::ostringstream dos;
        dos << "Document information\n\n";
        dos << "File: " << BaseName(path) << "\n";
        dos << "Path: " << path << "\n";
        std::error_code dec;
        auto dsz = fs::file_size(UltraCanvas::PathFromUtf8(path), dec);
        if (!dec) dos << "Size: " << HumanSize(dsz) << "\n";
        dos << "Type: PDF document\n";
        dos << "Encryption: password-protected\n";
        dos << "Shown: nothing until it is unlocked - Unlock (in the bar below, or Enter) opens it\n";
        ShowDetailsText(dos.str());
        return;
    }
    if (activeKind == MediaKind::Document && pdfView) {
        // Document metadata comes from the PDF element, never from libvips.
        auto* pv = static_cast<UltraCanvasPDFView*>(pdfView.get());
        std::ostringstream dos;
        dos << "Document information\n\n";
        dos << "File: " << BaseName(path) << "\n";
        dos << "Path: " << path << "\n";
        std::error_code dec;
        auto dsz = fs::file_size(UltraCanvas::PathFromUtf8(path), dec);
        if (!dec) dos << "Size: " << HumanSize(dsz) << "\n";
        dos << "Type: PDF document\n";
        if (unlocked) dos << "Encryption: password-protected - unlocked with its password\n";
        if (pv->HasDocument()) dos << "Pages: " << pv->GetPageCount() << "\n";
        ShowDetailsText(dos.str());
        return;
    }
#endif

    if (activeKind == MediaKind::Sheet || activeKind == MediaKind::Model ||
        activeKind == MediaKind::Text || activeKind == MediaKind::Video ||
        activeKind == MediaKind::Audio) {
        const char* heading = activeKind == MediaKind::Video ? "Video information\n\n"
                            : activeKind == MediaKind::Audio ? "Audio information\n\n"
                            : activeKind == MediaKind::Model ? "3D model information\n\n"
                            : activeKind == MediaKind::Text  ? (!webShown ? "Text information\n\n"
                                                                : IsMailMessageFile(path) ? "Message information\n\n"
                                                                : "Page information\n\n")
                                                             : "Spreadsheet information\n\n";
        const char* typeName = activeKind == MediaKind::Video ? "Video"
                             : activeKind == MediaKind::Audio ? "Audio"
                             : activeKind == MediaKind::Model ? "3D model"
                             : activeKind == MediaKind::Text  ? (!webShown ? "Text"
                                                               : IsMailMessageFile(path) ? "E-mail message"
                                                               : IsWebArchiveFile(path) ? "Saved web page" : "HTML page")
                                                              : "Spreadsheet";
        std::ostringstream mos;
        mos << heading;
        mos << "File: " << BaseName(path) << "\n";
        mos << "Path: " << path << "\n";
        std::error_code mec;
        auto msz = fs::file_size(UltraCanvas::PathFromUtf8(path), mec);
        if (!mec) mos << "Size: " << HumanSize(msz) << "\n";
        std::string ext = LowerExt(path);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return (char)std::toupper(c); });
        mos << "Type: " << typeName << " (" << ext << ")\n";
        ShowDetailsText(mos.str());
        return;
    }

    std::ostringstream os;
    os << "Image information\n\n";
    os << "File: " << BaseName(path) << "\n";
    os << "Path: " << path << "\n";

    std::error_code ec;
    auto sz = fs::file_size(UltraCanvas::PathFromUtf8(path), ec);
    if (!ec) os << "Size: " << HumanSize(sz) << "\n";

    if (auto img = surface->GetImage()) {
        os << "Dimensions: " << img->GetWidth() << " x " << img->GetHeight() << " px\n";
        if (surface->HasDecodeFailure())
            os << "Decoding: failed - " << DecodeFailureReason(img->errorMessage) << "\n";
        if (img->IsAnimated()) {
            os << "Animation: " << img->GetFrameCount() << " frames\n";
        }
    }

#ifdef HAS_LIBVIPS
    try {
        PixelFX::PXImageFileInfo fi = PixelFX::ExtractImageInfo(path);
        os << "Channels: " << fi.channels << "\n";
        os << "Bits/channel: " << fi.bitsPerChannel << "\n";
        if (!fi.colorSpace.empty()) os << "Colour space: " << fi.colorSpace << "\n";
        os << "Alpha: " << (fi.hasAlpha ? "yes" : "no") << "\n";
        const std::string resolution = ResolutionText(fi.dpiX, fi.dpiY);
        if (!resolution.empty()) os << "Resolution: " << resolution << "\n";
        if (!fi.loader.empty()) os << "Loader: " << fi.loader << "\n";
    } catch (...) {
        // Metadata extraction is best-effort.
    }

    // The file's own metadata blocks (EXIF, IPTC, XMP, ICC, PNG text chunks),
    // one sub-heading per block. The "Image" group repeats the facts above.
    try {
        PixelFX::PFXImage header = PixelFX::PFXImage::FromFile(path);
        const auto entries = PixelFX::Header::ReadMetadata(header);
        constexpr size_t kMaxEntries = 400;   // the panel scrolls; this only bounds a runaway file
        size_t shown = 0, total = 0;
        std::string group;
        os << "\n## Metadata\n";
        for (const auto& e : entries) {
            if (e.group == "Image") continue;
            ++total;
            if (shown >= kMaxEntries) continue;
            if (e.group != group) {
                group = e.group;
                os << "\n### " << group << "\n";
            }
            os << e.key << ": " << e.value << "\n";
            ++shown;
        }
        if (total == 0)
            os << "No embedded metadata (EXIF, IPTC, XMP, ICC profile or text chunks).\n";
        else if (total > shown)
            os << "\n" << (total - shown) << " more entries not shown.\n";
    } catch (...) {
        // Metadata extraction is best-effort.
    }
#endif

    ShowDetailsText(os.str());
}

// ===== DETAILS PANEL =====

namespace {
// A Markdown table cell must not hold a bare pipe, and the characters Markdown
// reads as emphasis or links are escaped so a Windows path or an EXIF value
// like "VIPS_CODING_NONE" is shown as written. Parentheses are left alone:
// with the brackets escaped they cannot form a link, and the text area does
// not unescape inside bold, where the tag names are ("Time zone (taken)").
std::string EscapeDetailsCell(const std::string& text) {
    static const std::string specials = "\\`*_[]#<>$~=^";
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        if (c == '|') { out += "\\|"; continue; }
        if (c == '\n' || c == '\r') { out += ' '; continue; }
        if (specials.find(c) != std::string::npos) out += '\\';
        out += c;
    }
    return out;
}

// The details builders write "Title\n\nKey: value" lines, the form the text
// view and a log read well. The panel shows them as Markdown: the title as a
// heading, each run of "Key: value" lines as a two-column table, "## " /
// "### " lines as section headings and anything else as a paragraph.
std::string DetailsToMarkdown(const std::string& plain) {
    std::istringstream in(plain);
    std::ostringstream md;
    std::string line;
    bool first = true, inTable = false;
    auto endTable = [&] { if (inTable) { md << "\n"; inTable = false; } };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first) {
            if (line.empty()) continue;
            md << "## " << EscapeDetailsCell(line) << "\n\n";
            first = false;
            continue;
        }
        if (line.empty()) { endTable(); continue; }
        if (line.rfind("## ", 0) == 0 || line.rfind("### ", 0) == 0) {
            endTable();
            md << "\n" << line << "\n\n";
            continue;
        }
        const size_t colon = line.find(": ");
        if (colon != std::string::npos && colon > 0 && colon < 48) {
            if (!inTable) {
                md << "| Property | Value |\n| --- | --- |\n";
                inTable = true;
            }
            md << "| **" << EscapeDetailsCell(line.substr(0, colon)) << "** | "
               << EscapeDetailsCell(line.substr(colon + 2)) << " |\n";
            continue;
        }
        endTable();
        md << EscapeDetailsCell(line) << "\n\n";
    }
    return md.str();
}
} // namespace

void UltraCanvasMediaViewer::ShowDetailsText(const std::string& plain) {
    detailsMarkdown = DetailsToMarkdown(plain);
    if (!detailsView) return;
    auto* dv = static_cast<UltraCanvasTextArea*>(detailsView.get());
    dv->SetText(detailsMarkdown);
    dv->ScrollTo(0);
}

void UltraCanvasMediaViewer::ShowNote(const std::string& plain) {
    ShowView(MediaKind::Text);
    if (!noteView) return;
    if (textView) textView->SetVisible(false);
    auto* nv = static_cast<UltraCanvasTextArea*>(noteView.get());
    nv->SetText(DetailsToMarkdown(plain));
    nv->ScrollTo(0);
    nv->SetVisible(true);
    noteShown = true;
}

bool UltraCanvasMediaViewer::IsDetailsVisible() const {
    return detailsView && detailsView->IsVisible();
}

void UltraCanvasMediaViewer::SetDetailsVisible(bool visible) {
    if (!detailsView || detailsView->IsVisible() == visible) return;
    // Filled for the file showing now (UpdateDetailedInfo scrolls to the top).
    if (visible) UpdateDetailedInfo();
    detailsView->SetVisible(visible);
    InvalidateLayout();
    RequestRedraw();
}

void UltraCanvasMediaViewer::Arrange(const Rect2Df& finalRect,
                                     const CSSLayout::LayoutContext& ctx) {
    UltraCanvasContainer::Arrange(finalRect, ctx);
    // The zoom the info bar shows is the fit, which the surface's size sets:
    // after a resize - or the first layout of a viewer that loaded its file
    // while hidden, a hover preview - it is reported again.
    if (surface && surface->IsVisible()) {
        const Size2Df now(surface->GetWidth(), surface->GetHeight());
        if (now.width != infoBarSurfaceSize.width || now.height != infoBarSurfaceSize.height) {
            infoBarSurfaceSize = now;
            UpdateInfoBar();
        }
    }
    if (!IsDetailsVisible()) return;
    // Over the active view, inset a little, and no wider than a table of
    // tags and values reads comfortably.
    UltraCanvasUIElement* view = ActiveViewElement();
    Rect2Df area = view && view->IsVisible() ? view->GetBounds()
                                             : Rect2Df(0, 0, finalRect.width, finalRect.height);
    constexpr float kInset = 10.0f, kMaxWidth = 640.0f;
    float w = std::min(kMaxWidth, area.width - 2 * kInset);
    float h = area.height - 2 * kInset;
    if (w < 60.0f || h < 60.0f) return;
    detailsView->Arrange(Rect2Df(area.x + kInset, area.y + kInset, w, h), ctx);
}

// ===== SLIDESHOW =====

void UltraCanvasMediaViewer::PlaySlideshow() {
    slideshowPlaying = true;
    // Space and the API reach here too; the toolbar toggle shows the state.
    if (playButton && !playButton->IsPressed()) playButton->SetPressed(true);
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    if (slideshowTimer) app->StopTimer(slideshowTimer);
    unsigned ms = static_cast<unsigned>(std::max(0.5, slideshowIntervalSec) * 1000.0);
    slideshowTimer = app->StartTimer(ms, true, [this](TimerId) { Next(); });
}

void UltraCanvasMediaViewer::PauseSlideshow() {
    slideshowPlaying = false;
    if (playButton && playButton->IsPressed()) playButton->SetPressed(false);
    if (slideshowTimer) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(slideshowTimer);
        slideshowTimer = 0;
    }
}

void UltraCanvasMediaViewer::ToggleSlideshow() {
    if (slideshowPlaying) PauseSlideshow();
    else                  PlaySlideshow();
}

void UltraCanvasMediaViewer::SetSlideshowIntervalSeconds(double sec) {
    slideshowIntervalSec = std::max(0.5, sec);
    if (slideshowPlaying) { PauseSlideshow(); slideshowPlaying = true; PlaySlideshow(); }
}

// ===== VIDEO PREVIEW =====

void UltraCanvasMediaViewer::StopVideoClipTimer() {
    // Whoever stops the clip decides the mute state itself (a new file, a mode
    // change, StopPlayback), so a finished clip's deferred un-mute is dropped
    // here rather than firing into the next source.
    videoClipUnmutePending = false;
    if (!videoClipTimer) return;
    if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(videoClipTimer);
    videoClipTimer = 0;
}

void UltraCanvasMediaViewer::ApplyVideoPreviewToCurrent() {
    StopVideoClipTimer();
#ifdef ULTRACANVAS_ENABLE_VIDEO
    if (!videoPlayer) return;
    auto* vp = static_cast<UltraCanvasVideoPlayerElement*>(videoPlayer.get());
    auto player = vp->GetPlayer();
    if (!player || !player->IsLoaded()) return;
    switch (videoPreviewMode) {
        case VideoPreviewMode::Autoplay:
            player->SetMute(false);
            vp->Play();
            break;
        case VideoPreviewMode::PreviewClip: {
            // Album hover-preview style: a few seconds of muted playback,
            // then pause on the current frame (the transport bar stays live
            // for the user to continue watching).
            player->SetMute(true);
            vp->Seek(0.0);
            vp->Play();
            auto* app = UltraCanvasApplication::GetInstance();
            if (app) {
                unsigned ms = static_cast<unsigned>(
                        std::max(0.5f, videoPreviewClipSec) * 1000.0f);
                videoClipTimer = app->StartTimer(ms, false, [this](TimerId) {
                    videoClipTimer = 0;
#ifdef ULTRACANVAS_ENABLE_VIDEO
                    if (!videoPlayer) return;
                    auto* v = static_cast<UltraCanvasVideoPlayerElement*>(videoPlayer.get());
                    v->Pause();
                    // Stay muted, and un-mute on the user's own resume instead
                    // (see the onPlay hook). Un-muting here would let the sound
                    // out whenever the pause is deferred — a backend that is
                    // still settling a flushing seek applies the pause after
                    // the fact, and the clip would keep playing, now audibly.
                    videoClipUnmutePending = true;
#endif
                });
            }
            break;
        }
        case VideoPreviewMode::Still:
            // Stay paused and show the first frame. The load-time preroll is
            // not a reliable frame source on its own (its one emission can be
            // lost while the pipeline is still settling), so when no frame of
            // this source has been shown yet, request one explicitly: a paused
            // seek to 0 makes the sink re-preroll and deliver the frame — the
            // same mechanism that refreshes the surface on a paused scrub.
            player->SetMute(false);
            if (!vp->HasVideoFrame()) vp->Seek(0.0);
            break;
    }
#endif
}

void UltraCanvasMediaViewer::SetVideoPreviewMode(VideoPreviewMode mode) {
    if (mode == videoPreviewMode) return;
    videoPreviewMode = mode;
    // Apply to a currently shown video so the change is immediately visible.
#ifdef ULTRACANVAS_ENABLE_VIDEO
    if (videoPlayer && videoPlayer->IsVisible()) {
        if (mode == VideoPreviewMode::Still) {
            StopVideoClipTimer();
            auto* vp = static_cast<UltraCanvasVideoPlayerElement*>(videoPlayer.get());
            vp->Pause();
            if (auto p = vp->GetPlayer()) p->SetMute(false);
            // No frame shown yet (e.g. the mode changed right after a load):
            // a paused seek re-prerolls and delivers the still frame.
            if (!vp->HasVideoFrame()) vp->Seek(0.0);
        } else {
            ApplyVideoPreviewToCurrent();
        }
    }
#endif
}

void UltraCanvasMediaViewer::SetVideoPreviewClipSeconds(float seconds) {
    videoPreviewClipSec = std::max(0.5f, seconds);
}

void UltraCanvasMediaViewer::StopPlayback() {
    StopVideoClipTimer();
#ifdef ULTRACANVAS_ENABLE_VIDEO
    if (videoPlayer) static_cast<UltraCanvasVideoPlayerElement*>(videoPlayer.get())->Stop();
#endif
#ifdef ULTRACANVAS_ENABLE_AUDIO
    if (audioPlayer) static_cast<UltraCanvasAudioPlayerElement*>(audioPlayer.get())->Stop();
#endif
}

bool UltraCanvasMediaViewer::IsPlayingMedia() const {
#ifdef ULTRACANVAS_ENABLE_VIDEO
    if (activeKind == MediaKind::Video && videoPlayer) {
        auto player = static_cast<UltraCanvasVideoPlayerElement*>(videoPlayer.get())->GetPlayer();
        if (player && player->IsPlaying()) return true;
    }
#endif
#ifdef ULTRACANVAS_ENABLE_AUDIO
    if (activeKind == MediaKind::Audio && audioPlayer) {
        auto player = static_cast<UltraCanvasAudioPlayerElement*>(audioPlayer.get())->GetPlayer();
        if (player && player->IsPlaying()) return true;
    }
#endif
    return false;
}

// ===== EVENTS =====

void UltraCanvasMediaViewer::HandleDroppedFiles(const std::vector<std::string>& files) {
    if (files.empty()) return;
    std::error_code ec;
    // A single dropped folder browses that folder; a single dropped file browses
    // the folder it lives in (so the arrow keys have somewhere to go) with that
    // file shown first. Several files are an explicit list (folders expanded).
    if (files.size() == 1 && fs::is_directory(files[0], ec)) {
        OpenFolder(files[0]);
        return;
    }
    if (files.size() == 1 && IsSupportedMedia(files[0])) {
        OpenFile(files[0]);
        return;
    }
    SetFiles(files, 0);
}

// ===========================================================================
// KEYBOARD
// ===========================================================================
// Two paths lead here. When the widget (or one of its toolbar controls) holds
// the focus the key event bubbles into OnEvent(). When the focus is nowhere —
// the common case right after the widget appears — or sits on a display view
// that ignores or swallows the browsing keys, the window key filter catches it
// before normal dispatch. Both end in HandleViewerKey().

std::string UltraCanvasMediaViewer::KeyFilterId() const {
    return GetIdentifier() + "_MediaViewerKeys";
}

bool UltraCanvasMediaViewer::IsEffectivelyVisible() const {
    if (!IsVisible()) return false;
    for (const UltraCanvasUIElement* e = GetParentContainer(); e; e = e->GetParentContainer()) {
        if (!e->IsVisible()) return false;
    }
    return true;
}

UltraCanvasUIElement* UltraCanvasMediaViewer::ActiveViewElement() const {
    switch (activeKind) {
        case MediaKind::Document: return pdfView.get();
        case MediaKind::Sheet:    return sheetView.get();
        case MediaKind::Model:    return modelView.get();
        case MediaKind::Text:     return noteShown ? noteView.get()
                                       : webShown  ? webView.get() : textView.get();
        case MediaKind::Book:     return bookView.get();
        case MediaKind::Font:     return fontView.get();
        case MediaKind::Vector:   return pluginView ? pluginView.get()
                                                    : vectorView.get();
        case MediaKind::Video:    return videoPlayer.get();
        case MediaKind::Audio:    return audioPlayer.get();
        case MediaKind::Image:
        default:                  return surface.get();
    }
}

bool UltraCanvasMediaViewer::IsInWebView(const UltraCanvasUIElement* element) const {
    // The page is built of many elements (labels, pictures, tables), any of
    // which a click can focus; they are all part of the one display view.
    for (const UltraCanvasUIElement* e = element; e && webView; e = e->GetParentContainer()) {
        if (e == webView.get()) return true;
    }
    return false;
}

bool UltraCanvasMediaViewer::IsDisplayView(const UltraCanvasUIElement* element) const {
    if (!element) return false;
    return element == surface.get()     || element == pdfView.get() ||
           element == sheetView.get()   || element == modelView.get() ||
           element == textView.get()    || element == bookView.get() ||
           element == noteView.get()    || IsInWebView(element) ||
           element == vectorView.get()  || element == pluginView.get() ||
           element == videoPlayer.get() || element == audioPlayer.get();
}

bool UltraCanvasMediaViewer::FocusForKeyboard() {
    return SetFocus(true);
}

void UltraCanvasMediaViewer::InstallKeyFilter() {
    auto* win = GetWindow();
    if (!win || keyFilterInstalled) return;
    keyFilterInstalled = true;
    // The same filter notes the user's input, for the idle forgetting of
    // Unlock's passwords; only keys are ever handled here.
    win->InstallEventFilter(KeyFilterId(),
            [this](const UCEvent& e) -> bool {
                lastActivity = std::chrono::steady_clock::now();
                return HandleFilteredKey(e);
            },
            { UCEventType::KeyDown, UCEventType::MouseDown, UCEventType::MouseMove, UCEventType::MouseWheel });
}

void UltraCanvasMediaViewer::RemoveKeyFilter() {
    if (!keyFilterInstalled) return;
    keyFilterInstalled = false;
    if (auto* win = GetWindow()) win->UnInstallWindowEventFilter(KeyFilterId());
}

void UltraCanvasMediaViewer::SetWindow(UltraCanvasWindowBase* win) {
    if (GetWindow() && GetWindow() != win) RemoveKeyFilter();
    UltraCanvasContainer::SetWindow(win);
    if (!win) return;
    InstallKeyFilter();
    // Claim the keyboard so the arrow keys browse straight away instead of only
    // after a click into the picture.
    if (grabFocusOnAttach) FocusForKeyboard();
}

bool UltraCanvasMediaViewer::HandleFilteredKey(const UCEvent& event) {
    if (event.type != UCEventType::KeyDown) return false;
    if (!IsEffectivelyVisible()) return false;
    auto* win = GetWindow();
    if (!win) return false;
    // An open dropdown / menu owns the keyboard while it is up.
    if (win->GetActivePopupElement()) return false;

    UltraCanvasUIElement* focused = win->GetFocusedElement();
    // Step in only when the keyboard is unowned or held by one of the display
    // views. Toolbar buttons, sliders and the breadcrumb keep their own key
    // handling — their events bubble into OnEvent() instead.
    if (focused && focused != this && !IsDisplayView(focused)) return false;

    return HandleViewerKey(event);
}

bool UltraCanvasMediaViewer::HandleViewerKey(const UCEvent& event) {
    if (event.type != UCEventType::KeyDown) return false;

    // The open Details panel takes Escape and the scrolling keys; Left / Right
    // keep browsing, and the panel follows the file.
    if (IsDetailsVisible()) {
        auto* dv = static_cast<UltraCanvasTextArea*>(detailsView.get());
        switch (event.virtualKey) {
            case UCKeys::Escape:   SetDetailsVisible(false); return true;
            case UCKeys::Up:       dv->ScrollUp(1);          return true;
            case UCKeys::Down:     dv->ScrollDown(1);        return true;
            case UCKeys::PageUp:   dv->ScrollUp(10);         return true;
            case UCKeys::PageDown: dv->ScrollDown(10);       return true;
            default: break;
        }
    }

    // File browsing. Alt+Left / Alt+Right browse even while the active view
    // claims the bare arrows for itself (spreadsheet cell movement).
    const bool viewOwnsArrows = ActiveViewUsesArrowKeys();
    switch (event.virtualKey) {
        case UCKeys::Left:
            if (viewOwnsArrows && !event.alt) return false;
            Previous();
            return true;
        case UCKeys::Right:
            if (viewOwnsArrows && !event.alt) return false;
            Next();
            return true;
        case UCKeys::Space:
            if (viewOwnsArrows) return false;
            ToggleSlideshow();
            return true;
        default:
            break;
    }

    // A locked file: Enter does what the Unlock button does.
    if (IsLocked() && event.virtualKey == UCKeys::Return && !event.ctrl && !event.alt) {
        Unlock();
        return true;
    }

    // A rendered page scrolls by the same keys: the viewer keeps the
    // keyboard, so Left / Right still browse.
    if (activeKind == MediaKind::Text && webShown && webView) {
        auto* wv = static_cast<UltraCanvasWebView*>(webView.get());
        const int line = 40;
        const int page = std::max(line, static_cast<int>(wv->GetHeight()) - line);
        switch (event.virtualKey) {
            case UCKeys::Up:       wv->ScrollBy(-line);   return true;
            case UCKeys::Down:     wv->ScrollBy(line);    return true;
            case UCKeys::PageUp:   wv->ScrollBy(-page);   return true;
            case UCKeys::PageDown: wv->ScrollBy(page);    return true;
            case UCKeys::Space:    wv->ScrollBy(event.shift ? -page : page); return true;
            default: break;
        }
        return false;
    }

    // The text view (or a note in its place) is display-only, so scrolling
    // it is the widget's job.
    UltraCanvasUIElement* shownText = noteShown ? noteView.get() : textView.get();
    if (activeKind == MediaKind::Text && shownText) {
        auto* ta = static_cast<UltraCanvasTextArea*>(shownText);
        switch (event.virtualKey) {
            case UCKeys::Up:       ta->ScrollUp(1);     return true;
            case UCKeys::Down:     ta->ScrollDown(1);   return true;
            case UCKeys::PageUp:   ta->ScrollUp(10);    return true;
            case UCKeys::PageDown: ta->ScrollDown(10);  return true;
            default: break;
        }
        if (event.ctrl && (event.character == 'c' || event.character == 'C')) {
            ta->CopySelection();
            return true;
        }
        return false;
    }

    // Everything else belongs to the active view: image zoom / rotate on the
    // surface, page keys in the PDF view, cell movement in the spreadsheet. The
    // view handles those itself while it holds the focus; while the widget holds
    // it (the usual case now) they are forwarded.
    if (UltraCanvasUIElement* view = ActiveViewElement()) {
        if (!view->IsFocused()) return view->OnEvent(event);
    }
    return false;
}

bool UltraCanvasMediaViewer::OnEvent(const UCEvent& event) {
    if (event.type == UCEventType::Drop) {
        HandleDroppedFiles(event.droppedFiles);
        return true;
    }
    if (UltraCanvasContainer::OnEvent(event)) return true;

    return HandleViewerKey(event);
}

} // namespace UltraCanvas
