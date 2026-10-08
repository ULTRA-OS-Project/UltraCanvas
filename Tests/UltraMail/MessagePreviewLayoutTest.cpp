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
// Opens a real window, so it runs under Xvfb (xvfb-run -a) and skips itself
// without a DISPLAY.
// Version: 1.0.1 - the temp folder is named by a clock stamp (no getpid on Windows)
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
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
        !WriteMessage(mailDir, 3, WideBodyMessage()))
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

    std::error_code ec;
    fs::remove_all(mailDir, ec);
    std::cerr << std::endl << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
