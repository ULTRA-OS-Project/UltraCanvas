// Apps/DemoApp/UltraCanvasDemoLayoutAudit.cpp
// `UltraCanvasDemo --audit-layout [--pseudo-loc] [--audit-page <id>]`: opens
// every demo page in turn, lays it out in the main window and lists what does
// not fit - a label whose text is cut (a one-line label narrower than its text,
// a wrapping one shorter than its lines), a button narrower than its text, and
// an element its parent cuts off without a scrollbar to reach the rest. With
// --pseudo-loc every label and button text is first made ~40% longer, the way
// a translation into German or Finnish is, so a page that only just fits in
// English shows where it would break. Exits 0 when nothing was found, 1 when
// something was.
// Version: 1.0.0 (--pseudo-loc with --component: the page shown with the longer texts)
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <cmath>
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

        // ~40% more text, added as whole words at the end so markup stays
        // well formed and a wrapping label can still break between them.
        std::string PseudoLocalize(const std::string& text) {
            if (text.empty()) return text;
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
