// Apps/UltraFIBU/ui/UltraFIBUTabelle.h
// The table every UltraFIBU screen is made of: a model that knows how its
// columns sort, and a panel that puts a search box, that table and a summary
// line together.
//
// **Why a model of its own rather than UltraCanvasMultiColumnListModel.** The
// stock model answers DisplayRole and nothing else, so a column showing
// `1.232,80` would sort as *text* - putting 1.000,00 before 999,00 - and a
// column of `15.06.2026` would sort by day-of-month. `ListDataRole::SortRole`
// exists precisely for that, and this model fills it: an amount sorts by its
// minor units, a date by its day number, and everything else falls back to the
// text. That is the whole reason UltraCanvasListSortFilterProxy was built, and
// a bookkeeping table is the case it was built for.
//
// **A row carries its record's id.** The proxy re-orders rows, so a proxy row
// index is not a source row index and neither is a database id. Anything that
// acts on a selection goes through `ZeileId`, never through the row number -
// forgetting that is how a sorted table opens or deletes the wrong record, and
// it is the one mistake the proxy's own documentation calls out.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first: they pull in X11, which defines Bool and
// Status as macros, and the engine headers below use those words as
// identifiers. Same ordering rule as EmailCleaner's views.
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasListSortFilterProxy.h"
#include "UltraCanvasTextInput.h"

#include "UltraFIBUTypes.h"   // Date, Money - after the UI headers, see UltraFIBUApp.h

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraFIBU {

// One cell: what is shown, and - when the two differ - what it sorts by.
struct TabellenZelle {
    std::string text;
    int64_t     sortwert    = 0;
    bool        hatSortwert = false;

    TabellenZelle() = default;
    TabellenZelle(std::string t) : text(std::move(t)) {}
    TabellenZelle(std::string t, int64_t sort)
        : text(std::move(t)), sortwert(sort), hatSortwert(true) {}
};

struct TabellenZeile {
    // The database id of the record behind this row. Not the row number: the
    // proxy moves rows around, and a screen that acts on the row number acts
    // on whatever happens to be there after a sort.
    int64_t                    id = 0;
    std::vector<TabellenZelle> zellen;
};

class TabellenModell : public UltraCanvas::IListModel {
public:
    int  GetRowCount() const override { return static_cast<int>(zeilen_.size()); }
    int  GetColumnCount() const override { return static_cast<int>(spalten_.size()); }
    UltraCanvas::ListDataValue GetData(const UltraCanvas::ListIndex& index,
                                       UltraCanvas::ListDataRole role) const override;
    bool SetData(const UltraCanvas::ListIndex&, UltraCanvas::ListDataRole,
                 const UltraCanvas::ListDataValue&) override { return false; }
    UltraCanvas::ListColumnDef GetColumnDef(int column) const override;

    void SetSpalten(std::vector<UltraCanvas::ListColumnDef> spalten);
    void SetZeilen(std::vector<TabellenZeile> zeilen);
    void Leeren();

    // The record id behind a *source* row. Callers holding a proxy row map it
    // through the proxy first; see the views for the two-line idiom.
    int64_t ZeileId(int sourceRow) const;

    // The exact sort value of a cell, if it has one. This exists because
    // ListDataValue cannot carry an int64: its alternatives are string, int,
    // float and Color. An amount in minor units passes 2^31 at 21.474.836,47 -
    // a number a company can genuinely invoice in a year - and would then
    // truncate and sort wrongly, and a float would lose cents above 16,7
    // million. So the panel installs a comparator that reads the value through
    // here instead, and SortRole below is only the fallback for anything
    // driving this model without one.
    bool Sortwert(int row, int column, int64_t& out) const;

private:
    std::vector<UltraCanvas::ListColumnDef> spalten_;
    std::vector<TabellenZeile>              zeilen_;
};

// A search box, a sortable and filterable table, and a line underneath saying
// what is in it. Every screen in the application is this, which is why it is
// one class and not four copies.
// ---- cells and numbers every screen formats the same way ----------------

// A date for a column: the German text a bookkeeper reads, and the day number
// it sorts by. Sorting 15.06.2026 as text would order it by day of month.
TabellenZelle DatumsZelle(const Date& datum);
// An amount: German formatting, sorted by its minor units.
TabellenZelle BetragsZelle(const Money& betrag);
// "20" -> 200, "8,1" -> 81 (per mille). Integer arithmetic: a tax rate read
// through a double comes back a fraction off, and that reaches every invoice.
bool ProzentNachPromille(const std::string& text, int& out);
// 190 -> "19 %", 81 -> "8,1 %" - the inverse, for display and for a field.
std::string ProzentText(int promille);
std::string Zahl(int64_t wert);

class TabellenPanel {
public:
    // `id` prefixes the element ids so two panels in one window stay distinct.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Bauen(
        const std::string& id, float x, float y, float breite, float hoehe,
        const std::string& suchHinweis = "Suchen ...");

    void SetSpalten(std::vector<UltraCanvas::ListColumnDef> spalten);
    void SetZeilen(std::vector<TabellenZeile> zeilen);
    void SetFusszeile(const std::string& text);

    // The summary line, computed from the rows that are actually visible.
    //
    // It takes a function rather than a string because of a bug that only
    // showed up on screen: with a static footer, filtering the list to one
    // customer left "4 Beleg(e), offen insgesamt 3.904,60 EUR" underneath a
    // single row. A total that does not describe what is above it is worse
    // than no total - somebody reads it. The panel calls this again on every
    // filter change, passing the record ids still showing, in display order.
    void SetFussFunktion(std::function<std::string(const std::vector<int64_t>&)> fn);

    // The record ids currently visible, in the order they are displayed.
    std::vector<int64_t> SichtbareIds() const;

    // The record id under the current selection, or 0. Maps the proxy row back
    // to the source row for the caller, which is the point of having it here.
    int64_t AusgewaehlteId() const;
    // Select the row carrying `id`, for when the program rather than a click
    // decides which record is current - a record just saved, for instance.
    // Does nothing when a filter hides that row.
    void Auswaehlen(int64_t id);

    // A row was activated (double-click or Enter). Carries the record id, not
    // a row number, for the reason in this file's header.
    std::function<void(int64_t id)> onZeileAktiviert;
    // The selection changed; 0 when nothing is selected.
    std::function<void(int64_t id)> onAuswahlGeaendert;

    std::shared_ptr<UltraCanvas::UltraCanvasListView> Liste() const { return liste_; }

private:
    void Sortieren(int spalte);
    void FussAktualisieren();

    std::shared_ptr<UltraCanvas::UltraCanvasContainer>              wurzel_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput>              suche_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView>               liste_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>                  fuss_;
    std::shared_ptr<TabellenModell>                                 modell_;
    std::shared_ptr<UltraCanvas::UltraCanvasListSortFilterProxy>    proxy_;
    std::function<std::string(const std::vector<int64_t>&)>          fussFunktion_;
};

} // namespace UltraFIBU
