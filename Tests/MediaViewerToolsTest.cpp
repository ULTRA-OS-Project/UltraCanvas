// Tests/MediaViewerToolsTest.cpp
// The media viewer's toolbar offers only the view / edit tools the shown file
// takes. A photo gets zoom, rotate / mirror, the colour adjustments, Curves
// and Save as; a drawing is never offered colour sliders or a histogram; a
// text file, a spreadsheet, a 3D model, a video or an audio file gets none of
// them, because its view brings its own controls. And colour set on one photo
// must not tint the drawing browsed to next - with the sliders hidden there
// would be no way to take it off.
//
// An HTML page, and an .xml that holds XHTML, is rendered in the page view
// (MV_Web) rather than shown as its source; data XML stays text. An .eml is
// shown there as the message: header, body, its pictures, attachments listed;
// an .emlx the same, and a saved web page (.mht) as the page it saved.
//
// A layered UltraCanvas document (*.ucd, UCBitmap) is shown whole - its
// layers composited at full size, not the 256 px thumbnail it carries - and
// zooms like a picture; a password-protected one shows only its thumbnail,
// and a private one nothing at all, until it is unlocked. So does a
// password-protected PDF (Tests/fixtures/pdf-aes256-locked.pdf): the Unlock
// button is offered, a wrong password leaves it locked, the right one shows
// it, and browsing away and back shows it without asking: the password is
// kept for the folder - and opens another file there with the same one -
// until another folder is opened or ForgetPasswords. The session locking,
// or the window going without input for GetForgetPasswordsAfterIdle(),
// forgets them too and shows the unlocked file locked again; while it is
// unlocked the info bar says so beside its kind.
//
// Runs headless: the viewer, its toolbars and the image surface are built
// and loaded without a window; only what decides visibility is checked.
// Version: 1.9.0 - Apple Mail .emlx; saved web pages (.mht) from their own parts
// Version: 1.8.0 - e-mail messages (.eml) shown as the message
// Version: 1.7.0 - HTML / XHTML pages rendered (MV_Web, IsHtmlFile); data XML stays text
// Version: 1.6.0 - SetSettingsAction: the gear in the info bar
// Version: 1.5.0 - forgotten, and the file locked again, on a session lock, ForgetPasswords
//                  and idleness; IsUnlocked and "unlocked" in the info bar
// Version: 1.4.0 - Unlock's passwords kept for the folder: back, the same password, another
//                  folder, ForgetPasswords, SetRememberPasswords(false)
// Version: 1.3.1 - the viewer's notes (a summary, a locked PDF) are its dark note view
// Version: 1.3.0 - Unlock: a protected *.ucd and a protected PDF
// Version: 1.2.0 - layered *.ucd images: full size; protected: the thumbnail
// Version: 1.1.0 - one toolbar row; Details is in the info bar only
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasRasterDocument.h"
#include "UltraCanvasUCDContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasWebView.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <deque>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

void WriteFile(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
}

// The info bar's text: name, kind, size, place in the folder.
std::string InfoText(UltraCanvasMediaViewer& viewer) {
    auto* label = dynamic_cast<UltraCanvasLabel*>(viewer.FindChildById("MV_InfoLabel"));
    return label ? label->GetText() : std::string();
}

bool Has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

// The page view's label holding `text`, or one of its pictures.
UltraCanvasLabel* PageLabel(UltraCanvasUIElement* root, const std::string& text) {
    if (auto* label = dynamic_cast<UltraCanvasLabel*>(root))
        if (label->GetText().find(text) != std::string::npos) return label;
    if (auto* box = dynamic_cast<UltraCanvasContainer*>(root))
        for (auto& child : box->GetChildren())
            if (auto* found = PageLabel(child.get(), text)) return found;
    return nullptr;
}
int PagePictures(UltraCanvasUIElement* root) {
    int count = dynamic_cast<UltraCanvasImageElement*>(root) ? 1 : 0;
    if (auto* box = dynamic_cast<UltraCanvasContainer*>(root))
        for (auto& child : box->GetChildren()) count += PagePictures(child.get());
    return count;
}

bool Shown(UltraCanvasMediaViewer& viewer, const std::string& id) {
    UltraCanvasUIElement* item = viewer.FindChildById(id);
    return item && item->IsVisible();
}

// A separator has no element id of its own; the toolbar knows it by its key.
bool SeparatorShown(UltraCanvasMediaViewer& viewer, const std::string& key) {
    auto* row = dynamic_cast<UltraCanvasToolbar*>(viewer.FindChildById("MV_Toolbar"));
    auto item = row ? row->GetWidget(key) : nullptr;
    return item && item->IsVisible();
}

// The toolbar must say what GetAvailableTools() says, group by group, with
// a separator only between two groups that are both up.
void CheckToolbarMatches(UltraCanvasMediaViewer& viewer, const std::string& label) {
    const MediaViewerTools t = viewer.GetAvailableTools();
    bool zoomOk = true, transformOk = true;
    for (const char* id : { "mv_zoomout", "mv_zoom", "mv_zoomin", "mv_fit" })
        zoomOk = zoomOk && Shown(viewer, id) == t.zoom;
    for (const char* id : { "mv_rotl", "mv_rotr", "mv_mirh", "mv_mirv" })
        transformOk = transformOk && Shown(viewer, id) == t.transform;
    Check(zoomOk, label + ": zoom buttons follow the tool set");
    Check(transformOk, label + ": rotate / mirror buttons follow the tool set");
    Check(Shown(viewer, "mv_adjust") == t.colour && Shown(viewer, "mv_curves") == t.colour,
          label + ": adjustments and Curves follow the tool set");
    Check(Shown(viewer, "mv_save") == t.save, label + ": Save as follows the tool set");
    Check(Shown(viewer, "mv_unlock") == t.unlock && Shown(viewer, "MV_Unlock") == t.unlock,
          label + ": Unlock (toolbar and info bar) follows the tool set");
    Check(!viewer.FindChildById("mv_info"), label + ": Details is not on the toolbar");
    Check(Shown(viewer, "MV_Details"), label + ": Details is always offered (info bar)");
    const bool edit = t.colour || t.save || t.unlock;
    Check(SeparatorShown(viewer, "mv_sep2") == (t.zoom || t.transform || edit) &&
          SeparatorShown(viewer, "mv_sep3") == (t.zoom && t.transform) &&
          SeparatorShown(viewer, "mv_sep4") == ((t.zoom || t.transform) && edit),
          label + ": no separator stands alone");
}

void Expect(UltraCanvasMediaViewer& viewer, const fs::path& file,
            bool zoom, bool transform, bool colour, bool save) {
    const std::string label = PathToUtf8(file.filename());
    viewer.SetFiles({ PathToUtf8(file) });
    const MediaViewerTools t = viewer.GetAvailableTools();
    std::cout << "\n" << label << "\n";
    Check(t.zoom == zoom, label + (zoom ? ": offers zoom" : ": offers no zoom"));
    Check(t.transform == transform,
          label + (transform ? ": offers rotate / mirror" : ": offers no rotate / mirror"));
    Check(t.colour == colour,
          label + (colour ? ": offers colour adjustments and Curves"
                          : ": offers no colour adjustments or Curves"));
    Check(t.save == save, label + (save ? ": offers Save as" : ": offers no Save as"));
    CheckToolbarMatches(viewer, label);
}

} // namespace

int main() {
    std::cout << "MediaViewerToolsTest\n";
    // An application does this on start; a headless test has to.
    UCImage::InitializeImageSubsysterm("MediaViewerToolsTest");

    const fs::path dir = fs::temp_directory_path() / PathFromUtf8(
        "uc-mediaviewer-tools-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);

    const fs::path media = PathFromUtf8(UC_MEDIA_DIR);
    const fs::path photo = media / "images" / "tn_dice.jpg";
    const fs::path svg   = dir / "drawing.svg";
    const fs::path text  = dir / "notes.txt";
    const fs::path sheet = dir / "table.csv";
    const fs::path model = dir / "part.stl";
    // Empty media files: the player fails to open them (or, in a build without
    // that backend, the image fallback does) - no playback starts either way,
    // and neither way may offer the image tools.
    const fs::path video = dir / "clip.mp4";
    const fs::path audio = dir / "song.mp3";

    WriteFile(svg,
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"48\">"
        "<rect x=\"4\" y=\"4\" width=\"56\" height=\"40\" fill=\"#3080e0\"/></svg>");
    WriteFile(text, "Plain text.\n");
    WriteFile(sheet, "a,b\n1,2\n");
    WriteFile(model,
        "solid part\n"
        " facet normal 0 0 1\n  outer loop\n"
        "   vertex 0 0 0\n   vertex 1 0 0\n   vertex 0 1 0\n"
        "  endloop\n endfacet\n"
        "endsolid part\n");
    WriteFile(video, "");
    WriteFile(audio, "");

    auto viewer = CreateMediaViewer("Viewer", 0, 0, 900, 600);

    std::cout << "\nNothing loaded\n";
    {
        const MediaViewerTools t = viewer->GetAvailableTools();
        Check(!t.zoom && !t.transform && !t.colour && !t.save,
              "an empty viewer offers no view / edit tool");
        CheckToolbarMatches(*viewer, "empty");
    }

    Expect(*viewer, photo, true, true, true, true);
    // A drawing on the image surface turns, mirrors, zooms and saves as a
    // bitmap - but its pixels are only a rendering, so no colour, no curves.
    // (A build with no SVG loader shows nothing, and so offers nothing.)
    viewer->SetFiles({ PathToUtf8(svg) });
    auto drawn = viewer->GetSurface()->GetImage();
    const bool svgShown = drawn && drawn->IsValid();
    if (!svgShown) std::cout << "\n(no SVG loader in this build)\n";
    Expect(*viewer, svg, svgShown, svgShown, false, svgShown);
    Expect(*viewer, text,  false, false, false, false);
    Check(Shown(*viewer, "MV_Text") && !Shown(*viewer, "MV_Note"),
          "notes.txt: a text file is on the text page, not in a note");

    // An HTML page - and XML that holds XHTML - is shown rendered, not as
    // its source; other XML is text. None of them takes an image tool.
    const fs::path page  = dir / "page.html";
    const fs::path xpage = dir / "page.xml";
    const fs::path data  = dir / "data.xml";
    WriteFile(dir / "style.css", "p { color: #204080; }");
    WriteFile(page, "<!DOCTYPE html><html><head><title>A page</title>"
                    "<link rel=\"stylesheet\" href=\"style.css\"></head>"
                    "<body><p>Rendered, not source.</p></body></html>");
    WriteFile(xpage, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                     "<!-- an XHTML page saved as .xml -->\n"
                     "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>An XML page</title></head>"
                     "<body><p>Also rendered.</p></body></html>");
    WriteFile(data, "<?xml version=\"1.0\"?>\n<catalog><book id=\"1\">Title</book></catalog>");
    Check(UltraCanvasMediaViewer::IsHtmlFile(PathToUtf8(page)) &&
          UltraCanvasMediaViewer::IsHtmlFile(PathToUtf8(xpage)) &&
          !UltraCanvasMediaViewer::IsHtmlFile(PathToUtf8(data)) &&
          !UltraCanvasMediaViewer::IsHtmlFile(PathToUtf8(text)),
          "IsHtmlFile: .html and XHTML in .xml are pages; data XML and .txt are not");
    auto* web = dynamic_cast<UltraCanvasWebView*>(viewer->FindChildById("MV_Web"));
    Expect(*viewer, page, false, false, false, false);
    Check(Shown(*viewer, "MV_Web") && !Shown(*viewer, "MV_Text"),
          "page.html: rendered in the page view, not shown as source text");
    Check(web && web->GetTitle() == "A page", "page.html: the page itself is loaded (its title)");
    Check(Has(InfoText(*viewer), "HTML"), "page.html: the info bar calls it HTML (" + InfoText(*viewer) + ")");
    Expect(*viewer, xpage, false, false, false, false);
    Check(Shown(*viewer, "MV_Web") && !Shown(*viewer, "MV_Text"),
          "page.xml: XHTML in an .xml file is rendered too");
    Check(web && web->GetTitle() == "An XML page", "page.xml: the page itself is loaded (its title)");
    Expect(*viewer, data, false, false, false, false);
    Check(Shown(*viewer, "MV_Text") && !Shown(*viewer, "MV_Web"),
          "data.xml: XML that is not a page stays text");
    Check(web && web->GetPage() == nullptr, "data.xml: the last page was let go");

    // An e-mail message is shown as the message: its header, its HTML body
    // with the picture it carries (cid:), its attachment listed - and no
    // remote picture fetched. A build without UltraNet's MIME reader shows
    // the message's text.
    const fs::path mail = dir / "message.eml";
    WriteFile(mail,
        "From: Alice Example <alice@example.org>\r\n"
        "To: Bob <bob@example.org>\r\n"
        "Subject: =?UTF-8?Q?Quarterly_r=C3=A9sum=C3=A9?=\r\n"
        "Date: Thu, 29 Oct 2026 10:00:00 +0100\r\n"
        "MIME-Version: 1.0\r\n"
        "Content-Type: multipart/mixed; boundary=\"outer\"\r\n\r\n"
        "--outer\r\n"
        "Content-Type: multipart/related; boundary=\"inner\"\r\n\r\n"
        "--inner\r\n"
        "Content-Type: text/html; charset=utf-8\r\n\r\n"
        "<html><head><style>p { color: #333; }</style></head><body>"
        "<p>Hello Bob, the chart is below.</p><img src=\"cid:chart@example\">"
        "<img src=\"https://tracker.example.com/pixel.gif\"></body></html>\r\n"
        "--inner\r\n"
        "Content-Type: image/png\r\n"
        "Content-ID: <chart@example>\r\n"
        "Content-Transfer-Encoding: base64\r\n\r\n"
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==\r\n"
        "--inner--\r\n"
        "--outer\r\n"
        "Content-Type: application/pdf; name=\"report.pdf\"\r\n"
        "Content-Disposition: attachment; filename=\"report.pdf\"\r\n"
        "Content-Transfer-Encoding: base64\r\n\r\n"
        "JVBERi0xLjQK\r\n"
        "--outer--\r\n");
    Expect(*viewer, mail, false, false, false, false);
#ifdef ULTRACANVAS_HAS_NET
    Check(UltraCanvasMediaViewer::IsMailMessageFile(PathToUtf8(mail)), "IsMailMessageFile: .eml is a message");
    Check(Shown(*viewer, "MV_Web") && !Shown(*viewer, "MV_Text"),
          "message.eml: shown as the message, not as its source");
    Check(web && web->GetTitle() == "Quarterly r\xC3\xA9sum\xC3\xA9", "message.eml: its subject, decoded");
    Check(web && PageLabel(web, "Alice Example") && PageLabel(web, "bob@example.org"),
          "message.eml: the header shows From and To");
    Check(web && PageLabel(web, "Hello Bob, the chart is below."), "message.eml: the HTML body is rendered");
    Check(web && PageLabel(web, "report.pdf"), "message.eml: the attachment is listed");
    Check(web && PagePictures(web) >= 1, "message.eml: the picture it carries (cid:) is shown");
    Check(web && web->GetPage() && web->GetPage()->kind == HTML::PageKind::Html,
          "message.eml: built as a page");
    Check(Has(InfoText(*viewer), "EMAIL"), "message.eml: the info bar calls it EMAIL (" + InfoText(*viewer) + ")");

    // Apple Mail's .emlx: the message's length, the message, Mail's plist.
    {
        const std::string message =
            "From: Carol <carol@example.org>\r\nTo: Dan <dan@example.org>\r\n"
            "Subject: From Apple Mail\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n"
            "Plain words from an emlx.\r\n";
        const fs::path emlx = dir / "12345.emlx";
        WriteFile(emlx, std::to_string(message.size()) + "\n" + message +
                        "<?xml version=\"1.0\"?>\n<plist version=\"1.0\"><dict><key>flags</key>"
                        "<integer>8590195713</integer></dict></plist>\n");
        Expect(*viewer, emlx, false, false, false, false);
        Check(Shown(*viewer, "MV_Web") && web && web->GetTitle() == "From Apple Mail",
              "12345.emlx: shown as the message");
        Check(web && PageLabel(web, "Plain words from an emlx.") && !PageLabel(web, "plist"),
              "12345.emlx: its body, without Mail's property list");
        Check(Has(InfoText(*viewer), "EMAIL"), "12345.emlx: the info bar calls it EMAIL");
    }

    // A saved web page (.mht): the page, its style sheet and pictures read
    // from the archive's parts under the address it was saved from (a
    // relative link reads against that), a cid: picture too. The page view
    // runs its rebuilds on a queue here - a window's next UI turn.
    {
        std::deque<std::function<void()>> turn;
        if (web) web->SetScheduler([&turn](std::function<void()> task) { turn.push_back(std::move(task)); });
        auto settle = [&] {
            for (int i = 0; i < 10 && !turn.empty(); ++i) {
                auto tasks = std::move(turn);
                turn.clear();
                for (auto& task : tasks) task();
            }
        };
        const std::string png =
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";
        const fs::path mht = dir / "saved.mht";
        WriteFile(mht,
            "From: <Saved by a browser>\r\nSubject: Saved article\r\nMIME-Version: 1.0\r\n"
            "Content-Type: multipart/related; type=\"text/html\"; boundary=\"mht\"\r\n\r\n"
            "--mht\r\nContent-Type: text/html; charset=\"utf-8\"\r\n"
            "Content-Location: https://example.org/articles/page.html\r\n\r\n"
            "<html><head><title>Saved article</title><link rel=\"stylesheet\" href=\"../css/site.css\"></head>"
            "<body><p>Saved text.</p><p class=\"gone\">Hidden by the saved style sheet</p>"
            "<img src=\"images/photo.png\"><img src=\"cid:logo@mht\"></body></html>\r\n"
            "--mht\r\nContent-Type: text/css\r\nContent-Location: https://example.org/css/site.css\r\n\r\n"
            ".gone { display: none; }\r\n"
            "--mht\r\nContent-Type: image/png\r\nContent-Transfer-Encoding: base64\r\n"
            "Content-Location: https://example.org/articles/images/photo.png\r\n\r\n" + png + "\r\n"
            "--mht\r\nContent-Type: image/png\r\nContent-Transfer-Encoding: base64\r\n"
            "Content-ID: <logo@mht>\r\n\r\n" + png + "\r\n--mht--\r\n");
        Expect(*viewer, mht, false, false, false, false);
        settle();
        Check(UltraCanvasMediaViewer::IsWebArchiveFile(PathToUtf8(mht)), "IsWebArchiveFile: .mht is a saved page");
        Check(Shown(*viewer, "MV_Web") && web && web->GetTitle() == "Saved article",
              "saved.mht: shown as the page it saved");
        Check(web && PageLabel(web, "Saved text."), "saved.mht: its text");
        Check(web && !PageLabel(web, "Hidden by the saved style sheet"),
              "saved.mht: its style sheet, from the archive (a relative link read against where it was saved)");
        Check(web && PagePictures(web) == 2, "saved.mht: both pictures, by Content-Location and by cid:");
        Check(web && web->GetPage() && web->GetPage()->baseUrl == "https://example.org/articles/page.html",
              "saved.mht: its links read against the address it was saved from");
        Check(Has(InfoText(*viewer), "WEB ARCHIVE"), "saved.mht: the info bar calls it WEB ARCHIVE");
        if (web) web->SetScheduler(nullptr);
    }
#else
    Check(Shown(*viewer, "MV_Text"), "message.eml: without a MIME reader, its text");
#endif

    Expect(*viewer, sheet, false, false, false, false);
    Expect(*viewer, model, false, false, false, false);
    Expect(*viewer, video, false, false, false, false);
    Expect(*viewer, audio, false, false, false, false);

    // A PDF zooms in the PDF view; without the PDF plugin a libvips build
    // rasterizes it onto the image surface instead. Either way it is a
    // document, not a photo.
    {
        const fs::path pdf = media / "sample.pdf";
        viewer->SetFiles({ PathToUtf8(pdf) });
        std::cout << "\nsample.pdf\n";
        Check(!viewer->GetAvailableTools().colour, "sample.pdf: offers no colour adjustments or Curves");
        CheckToolbarMatches(*viewer, "sample.pdf");
    }

    // ----- A layered UltraCanvas document -----
    std::cout << "\nLayered *.ucd images\n";
    {
        UCRasterDocument picture(400, 300);
        picture.AddLayer("Over", RasterPixel(200, 40, 40, 128));
        const fs::path plain = dir / "layered.ucd";
        const fs::path locked = dir / "locked.ucd";
        const fs::path hidden = dir / "hidden.ucd";
        std::string error;
        bool saved = picture.SaveToFile(PathToUtf8(plain), error);
        if (UCD::EncryptionAvailable()) {
            picture.SetUCDPassword("tulip");
            saved = saved && picture.SaveToFile(PathToUtf8(locked), error);
            picture.SetUCDPassword("tulip", true);
            saved = saved && picture.SaveToFile(PathToUtf8(hidden), error);
        }
        Check(saved, "the documents are written" + (error.empty() ? std::string() : ": " + error));

        viewer->SetFiles({ PathToUtf8(plain) });
        auto shown = viewer->GetSurface()->GetImage();
        Check(shown && shown->IsValid() && shown->GetWidth() == 400 && shown->GetHeight() == 300,
              "layered.ucd: shown at its full 400 x 300, not as its thumbnail");
        Expect(*viewer, plain, true, false, false, false);
        viewer->SetFiles({ PathToUtf8(plain) });
        Check(!viewer->IsUnlocked() && !Has(InfoText(*viewer), "unlocked"),
              "layered.ucd: not protected, so not marked unlocked (" + InfoText(*viewer) + ")");

        if (UCD::EncryptionAvailable()) {
            viewer->SetFiles({ PathToUtf8(locked), PathToUtf8(plain) });
            shown = viewer->GetSurface()->GetImage();
            Check(shown && shown->IsValid() && shown->GetWidth() == 256 && shown->GetHeight() == 192,
                  "locked.ucd: password-protected, so its 256 px thumbnail shows");
            Check(viewer->IsLocked() && viewer->GetAvailableTools().unlock,
                  "locked.ucd: locked, and Unlock is offered");
            CheckToolbarMatches(*viewer, "locked.ucd");

            std::string why;
            Check(viewer->UnlockWithPassword("passwort", why) == MediaUnlockResult::WrongPassword &&
                  viewer->IsLocked(), "locked.ucd: a wrong password leaves it locked (" + why + ")");
            Check(viewer->UnlockWithPassword("", why) == MediaUnlockResult::WrongPassword,
                  "locked.ucd: no password is not its password");
            Check(!viewer->IsUnlocked() && Has(InfoText(*viewer), "UC DOCUMENT   \xC2\xB7   password-protected"),
                  "locked.ucd: locked, the info bar says it is protected");
            Check(viewer->UnlockWithPassword("tulip", why) == MediaUnlockResult::Unlocked && !viewer->IsLocked(),
                  "locked.ucd: the right password unlocks it");
            Check(viewer->IsUnlocked() && Has(InfoText(*viewer), "UC DOCUMENT   \xC2\xB7   unlocked"),
                  "locked.ucd: unlocked, the info bar says so (" + InfoText(*viewer) + ")");
            shown = viewer->GetSurface()->GetImage();
            Check(shown && shown->IsValid() && shown->GetWidth() == 400 && shown->GetHeight() == 300,
                  "locked.ucd: unlocked, it shows at its full 400 x 300");
            {
                const MediaViewerTools t = viewer->GetAvailableTools();
                Check(t.zoom && !t.unlock, "locked.ucd: unlocked, it zooms and Unlock goes away");
            }
            CheckToolbarMatches(*viewer, "locked.ucd unlocked");
            Check(viewer->UnlockWithPassword("tulip", why) == MediaUnlockResult::NotLocked,
                  "locked.ucd: there is nothing left to unlock");
            viewer->Next();
            viewer->Previous();
            shown = viewer->GetSurface()->GetImage();
            Check(!viewer->IsLocked() && shown && shown->GetWidth() == 400,
                  "locked.ucd: browsed away and back, it shows without asking - its password is kept");

            // The folder's passwords open another file there with the same one.
            viewer->SetFiles({ PathToUtf8(hidden) });
            Check(!viewer->IsLocked() && viewer->IsUnlocked(),
                  "hidden.ucd: in the same folder with the same password, it shows without asking");

            // The session locks: the passwords go, and what they opened is
            // shown locked again.
            NotifySessionLockChanged(true);
            Check(viewer->IsLocked() && !viewer->IsUnlocked() && !viewer->GetSurface()->GetImage(),
                  "hidden.ucd: the session locked - it is shown locked again");
            NotifySessionLockChanged(false);
            Check(viewer->IsLocked(), "hidden.ucd: the session unlocked - it stays locked until Unlock");
            viewer->SetFiles({ PathToUtf8(locked) });
            Check(viewer->IsLocked(), "locked.ucd: after the session lock its password is forgotten - it asks");

            // ForgetPasswords does the same.
            Check(viewer->UnlockWithPassword("tulip", why) == MediaUnlockResult::Unlocked,
                  "locked.ucd: unlocked again");
            viewer->ForgetPasswords();
            Check(viewer->IsLocked() && !viewer->IsUnlocked(), "locked.ucd: ForgetPasswords shows it locked again");
            viewer->SetFiles({ PathToUtf8(hidden) });
            shown = viewer->GetSurface()->GetImage();
            Check(!shown, "hidden.ucd: private as well - no picture at all");
            Check(Shown(*viewer, "MV_Note") && !Shown(*viewer, "MV_Text"),
                  "hidden.ucd: its summary is the viewer's dark note, not a white text page");
            Check(viewer->IsLocked(), "hidden.ucd: locked, Unlock offered");
            Expect(*viewer, hidden, false, false, false, false);
            Check(viewer->UnlockWithPassword("tulip", why) == MediaUnlockResult::Unlocked,
                  "hidden.ucd: the right password unlocks it");
            shown = viewer->GetSurface()->GetImage();
            Check(shown && shown->IsValid() && shown->GetWidth() == 400,
                  "hidden.ucd: unlocked, the image shows");

            // Left alone for longer than the idle time, the viewer forgets.
            Check(viewer->GetForgetPasswordsAfterIdle() == UltraCanvasMediaViewer::kDefaultForgetPasswordsAfterIdle,
                  "idle: ten minutes unless set");
            viewer->SetForgetPasswordsAfterIdle(std::chrono::seconds(1));
            Check(!viewer->ForgetPasswordsIfIdle() && viewer->IsUnlocked(),
                  "hidden.ucd: a password just given is the user's doing - not idle");
            std::this_thread::sleep_for(std::chrono::milliseconds(1100));
            // Opened by a kept password, as a slideshow would: that is not
            // the user, so the second without input still counts.
            viewer->SetFiles({ PathToUtf8(locked) });
            Check(viewer->IsUnlocked(), "locked.ucd: opened with the kept password");
            Check(viewer->ForgetPasswordsIfIdle() && viewer->IsLocked(),
                  "locked.ucd: a second without input - forgotten and locked again");
            Check(!viewer->ForgetPasswordsIfIdle(), "idle: nothing left to forget");
            viewer->SetForgetPasswordsAfterIdle(std::chrono::seconds(0));
            Check(viewer->UnlockWithPassword("tulip", why) == MediaUnlockResult::Unlocked &&
                  !viewer->ForgetPasswordsIfIdle() && viewer->IsUnlocked(),
                  "locked.ucd: with the idle time at zero it is never forgotten for idleness");
            viewer->SetForgetPasswordsAfterIdle(UltraCanvasMediaViewer::kDefaultForgetPasswordsAfterIdle);

            // Another folder: the passwords are not taken there.
            const fs::path elsewhere = dir / "elsewhere";
            fs::create_directories(elsewhere);
            std::error_code copied;
            fs::copy_file(locked, elsewhere / "locked.ucd", fs::copy_options::overwrite_existing, copied);
            viewer->SetFiles({ PathToUtf8(elsewhere / "locked.ucd") });
            Check(viewer->IsLocked(), "elsewhere/locked.ucd: in another folder the passwords are forgotten - it asks");

            // Kept none when told not to.
            viewer->SetRememberPasswords(false);
            viewer->SetFiles({ PathToUtf8(locked) });
            Check(viewer->UnlockWithPassword("tulip", why) == MediaUnlockResult::Unlocked,
                  "locked.ucd: unlocked with SetRememberPasswords(false)");
            viewer->SetFiles({ PathToUtf8(locked) });
            Check(viewer->IsLocked(), "locked.ucd: with SetRememberPasswords(false) it asks every time it is shown");
            viewer->SetRememberPasswords(true);
        } else {
            std::cout << "  (no libsodium in this build: the protected cases are skipped)\n";
        }
    }

    // ----- A password-protected PDF -----
    std::cout << "\nA password-protected PDF\n";
    {
        const fs::path lockedPdf = PathFromUtf8(UC_FIXTURE_DIR) / "pdf-aes256-locked.pdf";
        viewer->SetFiles({ PathToUtf8(lockedPdf), PathToUtf8(media / "sample.pdf") });
        if (!viewer->IsLocked() && !viewer->GetAvailableTools().zoom) {
            std::cout << "  (no PDF engine in this build: the PDF cases are skipped)\n";
        } else {
            Check(viewer->IsLocked(), "pdf-aes256-locked.pdf: locked, not reported as damaged");
            Check(Shown(*viewer, "MV_Note") && !Shown(*viewer, "MV_Text"),
                  "pdf-aes256-locked.pdf: the note saying so is the viewer's dark note");
            Expect(*viewer, lockedPdf, false, false, false, false);   // (shows it alone)
            viewer->SetFiles({ PathToUtf8(lockedPdf), PathToUtf8(media / "sample.pdf") });
            Check(viewer->GetAvailableTools().unlock, "pdf-aes256-locked.pdf: Unlock is offered");
            std::string why;
            Check(viewer->UnlockWithPassword("tulpi", why) == MediaUnlockResult::WrongPassword &&
                  viewer->IsLocked(), "pdf-aes256-locked.pdf: a wrong password leaves it locked");
            Check(!Has(InfoText(*viewer), "unlocked"), "pdf-aes256-locked.pdf: locked, not marked unlocked");
            Check(viewer->UnlockWithPassword("tulip", why) == MediaUnlockResult::Unlocked &&
                  !viewer->IsLocked(), "pdf-aes256-locked.pdf: the right password opens it");
            Check(viewer->IsUnlocked() && Has(InfoText(*viewer), "PDF   \xC2\xB7   unlocked"),
                  "pdf-aes256-locked.pdf: open, \"unlocked\" beside PDF (" + InfoText(*viewer) + ")");
            Check(!Shown(*viewer, "MV_Note"), "pdf-aes256-locked.pdf: open, the note is gone");
            {
                const MediaViewerTools t = viewer->GetAvailableTools();
                Check(t.zoom && !t.unlock, "pdf-aes256-locked.pdf: open, its pages zoom; Unlock goes away");
            }
            CheckToolbarMatches(*viewer, "pdf-aes256-locked.pdf unlocked");
            viewer->Next();   // sample.pdf: not protected
            Check(!viewer->IsLocked() && viewer->GetAvailableTools().zoom,
                  "sample.pdf: not protected, opens as before");
            Check(!viewer->IsUnlocked() && Has(InfoText(*viewer), "PDF") && !Has(InfoText(*viewer), "unlocked"),
                  "sample.pdf: not protected, not marked unlocked");
            viewer->Previous();
            Check(!viewer->IsLocked() && viewer->GetAvailableTools().zoom,
                  "pdf-aes256-locked.pdf: browsed away and back, it opens without asking");
            Check(viewer->IsUnlocked() && Has(InfoText(*viewer), "unlocked"),
                  "pdf-aes256-locked.pdf: opened with the kept password, still marked unlocked");
            NotifySessionLockChanged(true);
            Check(viewer->IsLocked() && Shown(*viewer, "MV_Note"),
                  "pdf-aes256-locked.pdf: the session locked - closed again, its note back");
            NotifySessionLockChanged(false);
        }
    }

    // ----- A host's settings, behind the gear in the info bar -----
    std::cout << "\nThe host's settings\n";
    {
        Check(!Shown(*viewer, "MV_Settings"), "no settings action: no gear");
        int opened = 0;
        viewer->SetSettingsAction([&opened]() { ++opened; });
        auto* gear = dynamic_cast<UltraCanvasButton*>(viewer->FindChildById("MV_Settings"));
        Check(gear && gear->IsVisible(), "a settings action: the gear is up");
        if (gear && gear->onClick) gear->onClick();
        Check(opened == 1, "the gear runs the host's action");
        viewer->SetSettingsAction(nullptr);
        Check(!Shown(*viewer, "MV_Settings"), "an empty action hides it again");
    }

    // ----- The adjustments panel and the adjustments themselves -----
    std::cout << "\nAdjustments across files\n";
    {
        auto* adjust = dynamic_cast<UltraCanvasButton*>(viewer->FindChildById("mv_adjust"));
        auto* gamma  = dynamic_cast<UltraCanvasSlider*>(viewer->FindChildById("adj_gamma"));
        Check(adjust && adjust->onToggle && gamma, "the adjustments toggle and gamma slider exist");
        if (adjust && adjust->onToggle && gamma) {
            viewer->SetFiles({ PathToUtf8(photo), PathToUtf8(svg), PathToUtf8(text) });
            adjust->onToggle(true);
            gamma->SetValue(2.0f);
            Check(Shown(*viewer, "MV_Adjust"), "photo: the open panel shows");
            Check(viewer->GetSurface()->GetAdjustments().gamma == 2.0,
                  "photo: the surface draws with the gamma set");

            viewer->Next();   // the drawing
            Check(!Shown(*viewer, "MV_Adjust"), "drawing: the panel folds away");
            Check(viewer->GetSurface()->GetAdjustments().IsIdentity(),
                  "drawing: shown without the photo's adjustments");

            viewer->Next();   // the text file
            Check(!Shown(*viewer, "MV_Adjust"), "text: the panel stays folded away");

            viewer->Next();   // round to the photo again
            Check(Shown(*viewer, "MV_Adjust"), "photo again: the panel comes back");
            Check(viewer->GetSurface()->GetAdjustments().gamma == 2.0,
                  "photo again: the gamma carried over");

            viewer->SetTopBarsVisible(false);
            Check(!Shown(*viewer, "MV_Adjust"), "bars hidden: the panel is hidden with them");
            viewer->SetTopBarsVisible(true);
            Check(Shown(*viewer, "MV_Adjust"), "bars shown: the open panel returns");

            adjust->onToggle(false);
            Check(!Shown(*viewer, "MV_Adjust"), "toggle off: the panel closes");
        }
    }

    viewer->CloseFile();
    {
        const MediaViewerTools t = viewer->GetAvailableTools();
        std::cout << "\nAfter CloseFile\n";
        Check(!t.zoom && !t.transform && !t.colour && !t.save,
              "a closed viewer offers no view / edit tool");
    }

    std::error_code ec;
    fs::remove_all(dir, ec);

    std::cout << "\n" << (g_failures == 0 ? "All checks passed" : "FAILURES: ")
              << (g_failures == 0 ? "" : std::to_string(g_failures)) << "\n";
    return g_failures == 0 ? 0 : 1;
}
