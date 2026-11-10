// Apps/DemoApp/UltraCanvasCardExamples.cpp
// Demonstration of UltraCanvasCard: a header with title, subtitle and a
// trailing badge, content, a row of real buttons; a clickable surface; the
// highlighted and the selected state.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasCard.h"
#include "UltraCanvasBadge.h"
#include "UltraCanvasLabel.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateCardExamples() {
        auto page = DemoKit::CreatePage("CardExamples", Tr("Card Examples"),
                Tr("A raised surface with a header, a content area and a row of real buttons at the foot; "
                "the surface itself can be clicked, highlighted and selected."));

        // ===== 1. HEADER, CONTENT, ACTIONS =====
        auto structureCard = DemoKit::AddCard(page, "CdL1", Tr("Header, content, actions"),
                                              Tr("Title, subtitle and a badge at the right; content below; buttons right-aligned at the foot."));
        auto row = DemoKit::AddRow(structureCard, "CdRow", 12);

        auto video = CreateCard("CdVideo", Tr("UltraVideo"), Tr("The video editor"));
        video->SetTrailing(CreateBadge("CdVideoPrice", 0, 0, "29.90 EUR"));
        // The kit's text wraps and is as tall as its lines; a label with a set
        // height would be cut (the demo's layout audit says so).
        video->AddContent(DemoKit::CreateText("CdVideoText", Tr("Cut, trim and export in one window. Needs the UltraCanvas runtime 0.9.410.")));
        auto status = DemoKit::CreateCaption("CdVideoStatus", "");
        video->AddContent(status);
        video->AddActionButton("CdVideoInstall", Tr("Install"), [status]() { status->SetText(Tr("Install pressed")); });
        video->AddActionButton("CdVideoDetails", Tr("Details"), [status]() { status->SetText(Tr("Details pressed")); });
        video->SetElementSize(Size2Df(300, 0));
        video->size.height = CSSLayout::Dimension::Auto();
        DemoKit::Add(row, video);

        auto mail = CreateCard("CdMail", Tr("UltraMail"), Tr("Email client"));
        mail->SetTrailing(CreateBadge("CdMailFree", 0, 0, Tr("Free")));
        mail->AddContent(DemoKit::CreateText("CdMailText", Tr("IMAP, JMAP and POP3; contacts and calendars; end-to-end encryption.")));
        mail->AddActionButton("CdMailInstall", Tr("Install"), []() {});
        mail->SetElementSize(Size2Df(300, 0));
        mail->size.height = CSSLayout::Dimension::Auto();
        DemoKit::Add(row, mail);

        // ===== 2. CLICKABLE SURFACE =====
        auto clickCard = DemoKit::AddCard(page, "CdL2", Tr("Clickable surface"),
                                          Tr("A press and release on the surface fires onClick; a press on a button is the button's."));
        auto clickRow = DemoKit::AddRow(clickCard, "CdClickRow", 12);
        auto clickOut = DemoKit::CreateText("CdClickOut", Tr("Nothing clicked yet"));
        clickOut->SetBackgroundColor(Color(240, 240, 240));
        clickOut->SetPadding(3);
        for (int i = 0; i < 3; ++i) {
            const std::string name = i == 0 ? "UltraPaint" : (i == 1 ? "UltraFiler" : "Texter");
            auto tile = CreateCard("CdTile" + std::to_string(i), name, i == 0 ? Tr("Bitmap editor") : (i == 1 ? Tr("File manager") : Tr("Word processor")));
            tile->AddContent(DemoKit::CreateText("CdTileText" + std::to_string(i), Tr("Click the surface to open")));
            tile->AddActionButton("CdTileOpen" + std::to_string(i), Tr("Open"), [clickOut, name]() { clickOut->SetText(TrF("Button on {0}", name)); });
            tile->onClick = [clickOut, name]() { clickOut->SetText(TrF("Surface of {0} clicked", name)); };
            tile->SetElementSize(Size2Df(220, 0));
            tile->size.height = CSSLayout::Dimension::Auto();
            DemoKit::Add(clickRow, tile);
        }
        DemoKit::Add(clickCard, clickOut);

        // ===== 3. HIGHLIGHTED / SELECTED =====
        auto stateCard = DemoKit::AddCard(page, "CdL3", Tr("Highlighted and selected"),
                                          Tr("Highlighted: the accent border. Selected: the accent border and a tinted surface. Click a card to select it."));
        auto stateRow = DemoKit::AddRow(stateCard, "CdStateRow", 12);
        auto plain = CreateCard("CdPlain", Tr("Plain"), Tr("No state"));
        auto highlighted = CreateCard("CdHighlighted", Tr("Highlighted"), Tr("SetHighlighted(true)"));
        highlighted->SetHighlighted(true);
        auto selected = CreateCard("CdSelected", Tr("Selected"), Tr("SetSelected(true)"));
        selected->SetSelected(true);
        std::vector<std::shared_ptr<UltraCanvasCard>> all = {plain, highlighted, selected};
        for (auto& c : all) {
            c->SetElementSize(Size2Df(220, 0));
            c->size.height = CSSLayout::Dimension::Auto();
            c->AddContent(DemoKit::CreateText(c->GetIdentifier() + ".text", Tr("Click to select this one")));
            c->onClick = [all, raw = c.get()]() { for (auto& o : all) o->SetSelected(o.get() == raw); };
            DemoKit::Add(stateRow, c);
        }

        DemoKit::AddNotes(page, "CdNotes", Tr("Implementation notes"), {
            Tr("UltraCanvasCard is a container: the header, content and actions are framework labels and buttons in a flex column."),
            Tr("AddActionButton makes a real UltraCanvasButton; AddAction takes any element; AddContent fills the content column."),
            Tr("The header shows when there is a title, subtitle, icon or trailing element; the icon is drawn by the card at the left."),
            Tr("A press a child took never reaches the surface, so buttons stay buttons and the surface stays a surface."),
            Tr("CardStyle holds the colours, radius, padding, gaps and fonts; UltraCanvasGroupBox::Card() frames a section, this is an item."),
        });
        return page;
    }

} // namespace UltraCanvas
