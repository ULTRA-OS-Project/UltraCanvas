// Tests/UltraMail/MessagePreviewLayoutTest.cpp
// The message pane's body fills the pane: down to its bottom, and across to
// the vertical scrollbar without a horizontal one.
//
// The attachment strip under the body is a fixed-height row of the pane's
// flex column. It stayed in the column with no attachments in it, so every
// message's body ended 42 px plus the column's gap short of the pane's
// bottom, an empty band under the mail. And a newsletter whose body says
// min-width: 100% (Reddit's digest) measured that 100% against the whole
// pane, under the vertical scrollbar, so it scrolled sideways by the bar's
// width. Real messages are shown here, read from .eml files the way
// UltraMail reads its cache, and measured against the pane.
//
// The text can be selected and copied: a drag across the HTML body runs
// from paragraph to paragraph, a double-click takes a word, Ctrl+A all of it.
// A sign-in code gets a copy button in its own box (Papierkram's mail), or
// the code bar above the body when it has no box (a plain-text mail).
//
// Opens a real window, so it runs under Xvfb (xvfb-run -a) and skips itself
// without a DISPLAY. With ULTRACANVAS_SCREENSHOT_DIR set it writes the pane
// there: a drag's selection, the sign-in code's button and the code bar
// (message-preview-selection.ppm, -code.ppm, -code-bar.ppm).
// Version: 1.1.0 - text selection and the one-time code's copy button
// Version: 1.0.1 - the temp folder is named by a clock stamp (no getpid on Windows)
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextSelection.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasWindow.h"
#include "UltraMailMessagePreview.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

using namespace UltraCanvas;
using namespace UltraMail;
namespace fs = std::filesystem;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

#define SKIP_ALL(reason)                                                      \
    do {                                                                      \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

const char* kAccount = "info-example-eu";

// An HTML newsletter, long enough to scroll.
std::string HtmlMessage() {
    std::string body = "<html><body><h1>Weekly digest</h1>";
    for (int i = 0; i < 40; ++i)
        body += "<p>Paragraph " + std::to_string(i) + " of a message long enough to scroll.</p>";
    body += "</body></html>";
    return "From: Digest <digest@example.com>\r\n"
           "To: info@example.eu\r\n"
           "Subject: Weekly digest\r\n"
           "Date: Wed, 07 Oct 2026 14:35:00 +0000\r\n"
           "MIME-Version: 1.0\r\n"
           "Content-Type: text/html; charset=utf-8\r\n"
           "\r\n" + body + "\r\n";
}

// A newsletter as Reddit sends it: the body at least as wide as the window,
// a 600 px table centred in it, long enough for the vertical scrollbar.
std::string WideBodyMessage() {
    std::string rows;
    for (int i = 0; i < 30; ++i)
        rows += "<tr><td style='font-size:14px;line-height:18px'>Post " + std::to_string(i) +
                ": a title in the digest</td></tr>";
    return "From: Reddit <noreply@redditmail.com>\r\n"
           "To: info@example.eu\r\n"
           "Subject: Digest\r\n"
           "Date: Wed, 07 Oct 2026 14:35:55 +0000\r\n"
           "MIME-Version: 1.0\r\n"
           "Content-Type: text/html; charset=UTF-8\r\n"
           "\r\n"
           "<html><head><style type='text/css' media='screen'>"
           "body { padding:0 !important; margin:0 auto !important; display:block !important;"
           " min-width:100% !important; width:100% !important; background:#ffffff; }"
           "</style></head>"
           "<body style='padding:0 !important; margin:0 auto !important; display:block !important;"
           " min-width:100% !important; width:100% !important; background:#ffffff;'><center>"
           "<table width='100%' border='0' cellspacing='0' cellpadding='0'><tr><td align='center'>"
           "<table width='600px' border='0' cellspacing='0' cellpadding='0'>" + rows +
           "</table></td></tr></table></center></body></html>\r\n";
}

// A short HTML message with a PDF attached.
std::string MessageWithAttachment() {
    return "From: Office <office@example.com>\r\n"
           "To: info@example.eu\r\n"
           "Subject: The report\r\n"
           "Date: Wed, 07 Oct 2026 15:00:00 +0000\r\n"
           "MIME-Version: 1.0\r\n"
           "Content-Type: multipart/mixed; boundary=\"b1\"\r\n"
           "\r\n"
           "--b1\r\n"
           "Content-Type: text/html; charset=utf-8\r\n"
           "\r\n"
           "<html><body><p>The report is attached.</p></body></html>\r\n"
           "--b1\r\n"
           "Content-Type: application/pdf; name=\"report.pdf\"\r\n"
           "Content-Disposition: attachment; filename=\"report.pdf\"\r\n"
           "Content-Transfer-Encoding: base64\r\n"
           "\r\n"
           "JVBERi0xLjQKJcOkw7zDtsOfCg==\r\n"
           "--b1--\r\n";
}

// A sign-in code as Papierkram sends it (2026-10-08): the code in a grey box
// of its own, <p class="email-code">, under "...mit dem folgenden Code:".
std::string SignInCodeMessage() {
    const std::string p = "<p style='line-height: 1.5em; text-align: left; margin-top: 0; "
                          "color: #74787E; font-size: 16px;'>";
    return "From: Papierkram <notifications@papierkram.de>\r\n"
           "To: info@example.eu\r\n"
           "Subject: Dein Anmelde-Code\r\n"
           "Date: Thu, 08 Oct 2026 10:14:54 +0000\r\n"
           "MIME-Version: 1.0\r\n"
           "Content-Type: text/html; charset=UTF-8\r\n"
           "\r\n"
           "<html><body style='font-family: Arial, sans-serif; margin: 0; background-color: #F2F4F6; "
           "color: #74787E;'>"
           "<table width='100%' cellpadding='0' cellspacing='0'><tr><td align='center'>"
           "<table width='570' cellpadding='0' cellspacing='0' style='width: 570px; margin: 0 auto; "
           "background-color: #FFFFFF;'><tr><td style='padding: 35px;'>" +
           p + "Liebe(r) Anna Beispiel,</p>" +
           p + "du hast dich gerade versucht von folgendem Ger\xC3\xA4t einzuloggen:</p>"
           "<dl><dt><strong>Ger\xC3\xA4t</strong></dt><dd><var>Mozilla/5.0 (Windows NT 10.0; "
           "Win64; x64; rv:157.0) Gecko/20100101 Firefox/157.0</var></dd>"
           "<dt><strong>IP</strong></dt><dd><var>192.0.2.48</var></dd></dl>" +
           p + "Bitte best\xC3\xA4tige die Anmeldung mit dem folgenden Code:</p>"
           "<p class='email-code' style='font-family: \"Courier New\", monospace; line-height: 1.5em; "
           "text-align: center; margin: 1em auto; color: #74787E; font-size: 1.5em; font-weight: bold; "
           "background: #EEEEEE; border-radius: 4px; padding: 0.5em 1em; text-transform: uppercase; "
           "width: max-content;'>\n  649082\n</p>" +
           p + "Der Code ist 25 Minuten g\xC3\xBCltig.</p>" +
           p + "Viele Gr\xC3\xBC\xC3\x9F" "e<br>Dein Papierkram-Team</p>"
           "</td></tr></table></td></tr></table></body></html>\r\n";
}

// A plain-text mail with the code inside a sentence: no box to put a button
// in, so the code bar above the body offers it.
std::string PlainCodeMessage() {
    return "From: Shop <noreply@example.com>\r\n"
           "To: info@example.eu\r\n"
           "Subject: Sign in\r\n"
           "Date: Thu, 08 Oct 2026 11:00:00 +0000\r\n"
           "MIME-Version: 1.0\r\n"
           "Content-Type: text/plain; charset=utf-8\r\n"
           "\r\n"
           "Hello,\r\n\r\nYour verification code is 482913. It is valid for ten minutes.\r\n";
}

bool WritePpm(const std::shared_ptr<UltraCanvasWindow>& window, int width, int height,
              const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "P6\n" << width << " " << height << "\n255\n";
    std::vector<unsigned char> row(static_cast<size_t>(width) * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Color px;
            if (!window->GetPixelColor(x, y, px)) px = Colors::Black;
            row[static_cast<size_t>(x) * 3 + 0] = px.r;
            row[static_cast<size_t>(x) * 3 + 1] = px.g;
            row[static_cast<size_t>(x) * 3 + 2] = px.b;
        }
        out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
    return static_cast<bool>(out);
}

bool WriteMessage(const fs::path& mailDir, int64_t uid, const std::string& eml) {
    const fs::path folder = mailDir / kAccount / "INBOX";
    std::error_code ec;
    fs::create_directories(folder, ec);
    std::ofstream out(folder / (std::to_string(uid) + ".eml"), std::ios::binary);
    out << eml;
    return static_cast<bool>(out);
}

MessageEnvelope Envelope(int64_t uid, const std::string& subject) {
    MessageEnvelope env;
    env.accountId = kAccount;
    env.folder = "INBOX";
    env.uid = uid;
    env.subject = subject;
    env.fromAddr = "sender@example.com";
    env.to = {"info@example.eu"};
    env.date = 1791385200;
    return env;
}

std::shared_ptr<UltraCanvasUIElement> Find(const std::shared_ptr<UltraCanvasUIElement>& element,
                                           const std::string& id) {
    if (!element) return nullptr;
    if (element->GetIdentifier() == id) return element;
    if (auto* box = dynamic_cast<UltraCanvasContainer*>(element.get()))
        for (const auto& child : box->GetChildren())
            if (auto hit = Find(child, id)) return hit;
    return nullptr;
}

void CollectLabels(UltraCanvasUIElement* element, std::vector<UltraCanvasLabel*>& labels) {
    if (auto* label = dynamic_cast<UltraCanvasLabel*>(element)) { labels.push_back(label); return; }
    if (auto* box = dynamic_cast<UltraCanvasContainer*>(element))
        for (const auto& child : box->GetChildren()) CollectLabels(child.get(), labels);
}

// A mouse event for `label` at a point in its own coordinates.
UCEvent MouseAt(UCEventType type, UltraCanvasLabel& label, float x, float y) {
    const Rect2Df b = label.GetBoundsInWindow();
    UCEvent ev;
    ev.type = type;
    ev.button = UCMouseButton::Left;
    ev.pointer = Point2Di(static_cast<int>(x), static_cast<int>(y));
    ev.pointerWindow = Point2Di(static_cast<int>(b.x + x), static_cast<int>(b.y + y));
    return ev;
}

float Bottom(const UltraCanvasUIElement& element) {
    const Rect2Df b = element.GetBoundsInWindow();
    return b.y + b.height;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   UltraMail Message Pane Layout Suite"  << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    const fs::path mailDir = fs::temp_directory_path() /
                             ("ultramail-preview-test-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!WriteMessage(mailDir, 1, HtmlMessage()) || !WriteMessage(mailDir, 2, MessageWithAttachment()) ||
        !WriteMessage(mailDir, 3, WideBodyMessage()) || !WriteMessage(mailDir, 4, SignInCodeMessage()) ||
        !WriteMessage(mailDir, 5, PlainCodeMessage()))
        SKIP_ALL("could not write the test messages");

    UltraCanvasApplication app;
    if (!app.Initialize("MessagePreviewLayoutTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "MessagePreviewLayoutTest";
    cfg.width = 780;
    cfg.height = 600;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();   // UpdateAndRender() is a no-op on an unmapped window

    // The pane fills a flex column, stretched, as in the mail view.
    auto page = CreateContainer("page", 0, 0, 780, 600);
    page->layout.SetFlexColumn();
    window->AddChild(page);
    MessagePreview preview;
    preview.SetMailDir(PathToUtf8(mailDir));
    auto root = preview.Build();
    page->AddChild(root);
    root->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto render = [&]() {
        for (int frame = 0; frame < 3; ++frame) {
            page->RequestRedraw();
            window->UpdateAndRender();
        }
    };

    // With ULTRACANVAS_SCREENSHOT_DIR: the window as a PPM file there.
    auto shoot = [&](const std::string& name) {
        const char* shotDir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR");
        if (!shotDir) return;
        const std::string path = std::string(shotDir) + "/" + name;
        std::cerr << (WritePpm(window, cfg.width, cfg.height, path) ? "   wrote " : "   could not write ")
                  << path << std::endl;
    };

    // 1. A message without attachments: nothing below the body.
    preview.Show(Envelope(1, "Weekly digest"));
    render();
    auto body = Find(root, "prevBodyHost");
    auto strip = Find(root, "attachmentStrip");
    TEST("the pane has a body and an attachment strip", body && strip);
    if (!body || !strip) return 1;
    TEST("the HTML body was laid out", body->GetBounds().height > 100);
    TEST("no attachments: the strip is hidden", !strip->IsVisible());
    const float gapEmpty = Bottom(*root) - Bottom(*body);
    TEST("no attachments: the body reaches the pane's bottom (" +
         std::to_string(static_cast<int>(gapEmpty)) + " px left)", gapEmpty < 6.0f);

    // 2. A message with an attachment: the strip shows, under the body.
    preview.Show(Envelope(2, "The report"));
    render();
    TEST("an attachment: the strip is shown", strip->IsVisible());
    TEST("an attachment: the strip holds its chip",
         std::dynamic_pointer_cast<UltraCanvasContainer>(strip)->GetChildren().size() == 1);
    TEST("an attachment: the strip sits under the body",
         strip->GetBoundsInWindow().y >= Bottom(*body));
    TEST("an attachment: the strip ends at the pane's bottom", Bottom(*root) - Bottom(*strip) < 6.0f);

    // 3. And back: the strip leaves again with the attachments.
    preview.Show(Envelope(1, "Weekly digest"));
    render();
    TEST("back to no attachments: the strip is hidden again", !strip->IsVisible());
    TEST("back to no attachments: the body reaches the pane's bottom",
         Bottom(*root) - Bottom(*body) < 6.0f);

    // 4. body { min-width: 100% }: the page stops at the vertical scrollbar,
    //    and nothing scrolls sideways.
    preview.Show(Envelope(3, "Digest"));
    render();
    auto scroll = std::dynamic_pointer_cast<UltraCanvasContainer>(Find(root, "prevBodyScroll"));
    TEST("a wide body: the body has its scroll view", scroll && !scroll->GetChildren().empty());
    if (scroll && !scroll->GetChildren().empty()) {
        TEST("a wide body: the vertical scrollbar is shown",
             scroll->GetVerticalScrollBar().IsVisible());
        TEST("a wide body: no horizontal scrollbar", !scroll->GetHorizontalScrollBar().IsVisible());
        // What the scroll view scrolls: the body, in its page box.
        const Rect2Df view = scroll->GetBoundsInWindow();
        const Rect2Df content = scroll->GetChildren().front()->GetBoundsInWindow();
        const float track = static_cast<float>(scroll->GetContainerStyle().scrollbarStyle.trackSize);
        TEST("a wide body: the page ends at the vertical scrollbar (" +
             std::to_string(static_cast<int>(content.x + content.width - (view.x + view.width - track))) +
             " px past it)", content.x + content.width <= view.x + view.width - track + 0.5f);
    }

    // 5. The body's text can be selected: a drag from one paragraph into the
    //    third, as in a browser.
    preview.Show(Envelope(1, "Weekly digest"));
    render();
    std::vector<UltraCanvasLabel*> labels;
    CollectLabels(Find(root, "prevBodyScroll").get(), labels);
    TEST("selection: the HTML body is built of labels", labels.size() > 4);
    if (labels.size() > 4) {
        auto selection = labels[1]->GetTextSelection();
        TEST("selection: the body's labels are selectable, sharing one selection",
             selection && labels[0]->GetTextSelection() == selection &&
             labels[4]->GetTextSelection() == selection);
        if (selection) {
            UltraCanvasLabel& first = *labels[1];
            UltraCanvasLabel& third = *labels[3];
            const float midFirst = first.GetHeight() / 2.0f;
            first.OnEvent(MouseAt(UCEventType::MouseDown, first, 1.0f, midFirst));
            // Past the end of the third paragraph's line: to its end.
            UCEvent move = MouseAt(UCEventType::MouseMove, third, third.GetWidth() - 2.0f,
                                   third.GetHeight() / 2.0f);
            first.OnEvent(move);
            UCEvent up = move;
            up.type = UCEventType::MouseUp;
            first.OnEvent(up);
            render();
            shoot("message-preview-selection.ppm");
            const std::string dragged = selection->GetSelectedText();
            TEST("selection: a drag selects from paragraph to paragraph",
                 dragged == "Paragraph 0 of a message long enough to scroll.\n"
                            "Paragraph 1 of a message long enough to scroll.\n"
                            "Paragraph 2 of a message long enough to scroll.");
            TEST("selection: the paragraphs in between are highlighted",
                 labels[2]->HasSelectedRange() && !labels[4]->HasSelectedRange());
            TEST("selection: the pressed paragraph takes the keyboard (Ctrl+C)",
                 first.AcceptsFocus() && !third.AcceptsFocus());
            if (selection->CopyToClipboard()) {
                std::string clip;
                TEST("selection: Copy puts the text on the clipboard",
                     GetClipboardText(clip) && clip == dragged);
            }

            // A double-click takes a word.
            first.OnEvent(MouseAt(UCEventType::MouseDoubleClick, first, 1.0f, midFirst));
            TEST("selection: a double-click selects the word",
                 selection->GetSelectedText() == "Paragraph");

            // Ctrl+A: the whole body.
            UCEvent key;
            key.type = UCEventType::KeyDown;
            key.virtualKey = UCKeys::A;
            key.ctrl = true;
            first.OnEvent(key);
            const std::string all = selection->GetSelectedText();
            const std::string last = "Paragraph 39 of a message long enough to scroll.";
            TEST("selection: Ctrl+A selects the whole body",
                 all.rfind("Weekly digest\n", 0) == 0 && all.size() > last.size() &&
                 all.compare(all.size() - last.size(), last.size(), last) == 0);
        }
    }
    auto subjectLabel = std::dynamic_pointer_cast<UltraCanvasLabel>(Find(root, "prevSubject"));
    auto fromLabel = std::dynamic_pointer_cast<UltraCanvasLabel>(Find(root, "prevFrom"));
    TEST("selection: the subject and sender can be selected too",
         subjectLabel && fromLabel && subjectLabel->IsSelectable() &&
         subjectLabel->GetTextSelection() == fromLabel->GetTextSelection());

    // 6. A sign-in code in a box of its own: a copy button inside the box.
    preview.Show(Envelope(4, "Dein Anmelde-Code"));
    render();
    TEST("code: Papierkram's sign-in code is found",
         preview.OneTimeCodes().size() == 1 && preview.OneTimeCodes()[0].code == "649082");
    auto copyButton = std::dynamic_pointer_cast<UltraCanvasButton>(Find(root, "prevCodeCopy0"));
    auto codeBar = Find(root, "prevCodeBar");
    TEST("code: a copy button stands in the code's box", copyButton != nullptr);
    TEST("code: no code bar when the code has its button", codeBar && !codeBar->IsVisible());
    if (copyButton) {
        UltraCanvasContainer* box = copyButton->GetParentContainer();
        const Rect2Df b = copyButton->GetBoundsInWindow();
        const Rect2Df boxBounds = box ? box->GetBoundsInWindow() : Rect2Df();
        TEST("code: the button is inside the box, at its right edge",
             box && b.width > 0 && b.x >= boxBounds.x && b.x + b.width <= boxBounds.x + boxBounds.width + 0.5f &&
             boxBounds.x + boxBounds.width - (b.x + b.width) < 12.0f &&
             b.y >= boxBounds.y - 0.5f && b.y + b.height <= boxBounds.y + boxBounds.height + 0.5f);
        // The code's own text, beside the button and not under it.
        std::vector<UltraCanvasLabel*> boxLabels;
        if (box) CollectLabels(box, boxLabels);
        TEST("code: the button does not cover the code",
             boxLabels.size() == 1 && boxLabels[0]->GetBoundsInWindow().x + boxLabels[0]->GetBoundsInWindow().width <= b.x + 0.5f);
        shoot("message-preview-code.ppm");
        if (copyButton->onClick) copyButton->onClick();
        std::string clip;
        if (GetClipboardText(clip))
            TEST("code: the button copies the code", clip == "649082");
    }

    // 7. A code inside a sentence of a plain-text mail: the code bar offers it.
    preview.Show(Envelope(5, "Sign in"));
    render();
    TEST("code: the plain-text mail's code is found",
         preview.OneTimeCodes().size() == 1 && preview.OneTimeCodes()[0].code == "482913");
    auto codeText = std::dynamic_pointer_cast<UltraCanvasLabel>(Find(root, "prevCodeText"));
    TEST("code: the code bar shows it", codeBar && codeBar->IsVisible() && codeText &&
                                        codeText->GetText() == "482913");
    shoot("message-preview-code-bar.ppm");
    // And a message without one hides the bar again.
    preview.Show(Envelope(1, "Weekly digest"));
    render();
    TEST("code: no code, no bar", preview.OneTimeCodes().empty() && !codeBar->IsVisible());

    std::error_code ec;
    fs::remove_all(mailDir, ec);
    std::cerr << std::endl << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
