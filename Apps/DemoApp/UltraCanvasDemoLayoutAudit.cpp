// Apps/DemoApp/UltraCanvasDemoLayoutAudit.cpp
// `UltraCanvasDemo --audit-layout [--pseudo-loc] [--audit-page <id>]`: opens
// every demo page in turn, lays it out in the main window and lists what does
// not fit - a label whose text is cut (a one-line label narrower than its text,
// a wrapping one shorter than its lines), a button narrower than its text, and
// an element its parent cuts off without a scrollbar to reach the rest. With
// --pseudo-loc every label and button text is first made ~40% longer, the way
// a translation into German or Finnish is, so a page that only just fits in
// English shows where it would break; text without a letter (a time readout,
// a "+" button) is no language's and stays as it is. Exits 0 when nothing was
// found, 1 when something was. CI runs both forms on Linux (build.yml).
// Version: 1.1.0 - --pseudo-loc leaves text without a letter as it is
// Version: 1.0.0 (--pseudo-loc with --component: the page shown with the longer texts)
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace UltraCanvas {

    namespace {

        // A report line's text: one line, shortened.
        std::string Excerpt(const std::string& text) {
            std::string out;
            for (char c : text) {
                // Stop at a character's first byte, never inside one.
                const bool continuation = (static_cast<unsigned char>(c) & 0xC0) == 0x80;
                if (out.size() >= 48 && !continuation) { out += "..."; break; }
                out += (c == '\n' || c == '\t') ? ' ' : c;
            }
            return out;
        }

        // Whether `text` has a letter in it. Text without one - a time
        // readout "0:00 / 2:08", a "+" or "−" button, a "–" between two
        // dates - reads the same in every language. A code point counts as
        // a letter when it is an ASCII letter or lies in the scripts' blocks:
        // Latin-1 letters to Greek extended, and CJK onwards; the
        // punctuation, arrows, maths and box symbols between (U+2000 -
        // U+2FFF) and emoji do not.
        bool HasLetter(const std::string& text) {
            for (size_t i = 0; i < text.size();) {
                const auto c = static_cast<unsigned char>(text[i]);
                if (c < 0x80) {
                    if (std::isalpha(c)) return true;
                    ++i;
                    continue;
                }
                const int len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
                uint32_t cp = len == 4 ? (c & 0x07) : len == 3 ? (c & 0x0F) : (c & 0x1F);
                for (int k = 1; k < len && i + k < text.size(); ++k)
                    cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
                i += len;
                if (cp == 0xD7 || cp == 0xF7) continue;   // × ÷
                if ((cp >= 0xC0 && cp < 0x2000) || (cp >= 0x3040 && cp < 0xE000) ||
                    (cp >= 0xF900 && cp < 0xFE00) || (cp >= 0x20000 && cp < 0x30000))
                    return true;
            }
            return false;
        }

        // ~40% more text, added as whole words at the end so markup stays
        // well formed and a wrapping label can still break between them.
        // Text without a letter is not translated, so it stays as it is.
        std::string PseudoLocalize(const std::string& text) {
            if (!HasLetter(text)) return text;
            size_t extra = static_cast<size_t>(std::ceil(text.size() * 0.4));
            std::string out = text;
            while (extra > 0) {
                const size_t n = std::min<size_t>(extra, 6);
                out += ' ';
                out.append(n, 'w');
                extra -= n;
            }
            return out;
        }

        void PseudoLocalizeTree(UltraCanvasUIElement* element) {
            if (auto* label = dynamic_cast<UltraCanvasLabel*>(element)) {
                label->SetText(PseudoLocalize(label->GetText()));
            } else if (auto* button = dynamic_cast<UltraCanvasButton*>(element)) {
                if (!button->GetText().empty()) button->SetText(PseudoLocalize(button->GetText()));
            }
            for (auto& child : element->Children()) {
                if (auto* ui = dynamic_cast<UltraCanvasUIElement*>(child.get())) PseudoLocalizeTree(ui);
            }
        }

        struct Finding {
            std::string kind;
            std::string id;
            std::string detail;
        };

        void Describe(std::vector<Finding>& out, const std::string& kind, UltraCanvasUIElement* el,
                      const std::string& text, float needW, float needH, float haveW, float haveH) {
            std::ostringstream d;
            d.setf(std::ios::fixed);
            d.precision(0);
            if (!text.empty()) d << "\"" << Excerpt(text) << "\" ";
            d << "needs " << needW << "x" << needH << ", has " << haveW << "x" << haveH;
            out.push_back({kind, el->GetIdentifier(), d.str()});
        }

        // Whether `container` scrolls along an axis: its bar shows, so the
        // overflow is reachable.
        bool ScrollsX(UltraCanvasContainer* c) { return c->GetHorizontalScrollBar().IsVisible(); }
        bool ScrollsY(UltraCanvasContainer* c) { return c->GetVerticalScrollBar().IsVisible(); }

        void AuditTree(UltraCanvasUIElement* element, const CSSLayout::LayoutContext& lctx,
                       std::vector<Finding>& out) {
            if (!element->IsVisible() || element->IsPopupElement()) return;
            const Rect2Df b = element->GetBounds();
            const float haveW = b.width - element->GetTotalPaddingHorizontal() - element->GetTotalBorderHorizontal();
            const float haveH = b.height - element->GetTotalPaddingVertical() - element->GetTotalBorderVertical();

            if (auto* label = dynamic_cast<UltraCanvasLabel*>(element)) {
                if (!label->GetText().empty() && b.width > 0 && b.height > 0) {
                    const bool wraps = label->GetStyle().wrap != TextWrap::WrapNone;
                    const Size2Df natural = label->MeasureOwnContent(std::nullopt, lctx);
                    if (!wraps && natural.width > haveW + 1.5f) {
                        Describe(out, "label cut", element, label->GetText(), natural.width, natural.height, haveW, haveH);
                    } else {
                        const Size2Df atWidth = label->MeasureOwnContent(std::max(0.f, haveW), lctx);
                        if (atWidth.height > haveH + 1.5f)
                            Describe(out, wraps ? "label lines cut" : "label too short", element,
                                     label->GetText(), haveW, atWidth.height, haveW, haveH);
                    }
                }
            } else if (auto* button = dynamic_cast<UltraCanvasButton*>(element)) {
                if (!button->GetText().empty() && b.width > 0) {
                    const Size2Df content = button->MeasureOwnContent(std::nullopt, lctx);
                    if (content.width > haveW + 1.5f)
                        Describe(out, "button cut", element, button->GetText(), content.width, content.height, haveW, haveH);
                }
            }

            auto* container = dynamic_cast<UltraCanvasContainer*>(element);
            const bool clips = container && container->GetContainerStyle().clipChildren;
            const float originX = element->GetBorderLeftWidth() + element->GetPaddingLeft();
            const float originY = element->GetBorderTopWidth() + element->GetPaddingTop();
            for (auto& child : element->Children()) {
                auto* ui = dynamic_cast<UltraCanvasUIElement*>(child.get());
                if (!ui) continue;
                if (clips && ui->IsVisible() && !ui->IsPopupElement()) {
                    const Rect2Df cb = ui->GetBounds();
                    const float overRight  = (cb.x - originX) + cb.width  - haveW;
                    const float overBottom = (cb.y - originY) + cb.height - haveH;
                    const bool cutX = overRight > 2.f && !ScrollsX(container);
                    const bool cutY = overBottom > 2.f && !ScrollsY(container);
                    if ((cutX || cutY) && cb.width > 0 && cb.height > 0) {
                        std::ostringstream d;
                        d.setf(std::ios::fixed);
                        d.precision(0);
                        d << "in \"" << element->GetIdentifier() << "\": ";
                        if (cutX) d << overRight << " px past the right edge ";
                        if (cutY) d << overBottom << " px past the bottom ";
                        d << "with no scrollbar";
                        out.push_back({"cut off", ui->GetIdentifier(), d.str()});
                    }
                }
                AuditTree(ui, lctx, out);
            }
        }

    } // namespace

    void UltraCanvasDemoApplication::PseudoLocalizeCurrentPage() {
        if (currentDisplayElement) PseudoLocalizeTree(currentDisplayElement.get());
        if (auto* page = dynamic_cast<DemoKit::DemoPage*>(currentDisplayElement.get())) page->RecheckMinWidths();
        if (demoItems.count(currentSelectedId))
            headerContainer->SetDemoTitle(PseudoLocalize(demoItems[currentSelectedId]->description));
    }

    int UltraCanvasDemoApplication::RunLayoutAudit(bool pseudoLocalize, const std::string& onlyPage) {
        if (!mainWindow) return 2;
        mainWindow->Show();
        // RunOnce would wait for a native event that never comes; the
        // window is laid out and drawn directly, which is all a page needs
        // to reach its final size.
        auto* app = UltraCanvasApplication::GetInstance();
        auto pump = [&]() {
            for (int i = 0; i < 3; ++i) {
                app->ProcessEvents();
                // A kit page re-lays itself out through a task posted to the
                // UI thread, which this loop does not run: ask for it here.
                if (auto* page = dynamic_cast<DemoKit::DemoPage*>(currentDisplayElement.get()))
                    page->InvalidateLayout();
                mainWindow->UpdateAndRender();
            }
        };
        pump();

        std::vector<std::string> ids;
        for (const auto& [id, item] : demoItems) {
            if (!onlyPage.empty() && id != onlyPage) continue;
            if (item->createExample && item->status != ImplementationStatus::NotImplemented) ids.push_back(id);
        }

        CSSLayout::LayoutContext lctx;
        lctx.viewportWidth = mainWindow->GetWidth();
        lctx.viewportHeight = mainWindow->GetHeight();

        int pagesWithFindings = 0;
        size_t total = 0;
        for (const auto& id : ids) {
            SelectDemoItem(id);
            pump();
            if (pseudoLocalize) {
                PseudoLocalizeCurrentPage();
                pump();
            }
            std::vector<Finding> findings;
            if (currentDisplayElement) AuditTree(currentDisplayElement.get(), lctx, findings);
            AuditTree(headerContainer.get(), lctx, findings);
            if (findings.empty()) {
                std::cout << "[ OK ] " << id << std::endl;
                continue;
            }
            ++pagesWithFindings;
            total += findings.size();
            std::cout << "[FAIL] " << id << " (" << findings.size() << ")" << std::endl;
            for (const auto& f : findings)
                std::cout << "       " << f.kind << " - " << f.id << ": " << f.detail << std::endl;
        }
        std::cout << ids.size() << " pages, " << pagesWithFindings << " with findings, "
                  << total << " findings" << (pseudoLocalize ? " (pseudo-localized +40%)" : "") << std::endl;
        return pagesWithFindings == 0 ? 0 : 1;
    }

} // namespace UltraCanvas
