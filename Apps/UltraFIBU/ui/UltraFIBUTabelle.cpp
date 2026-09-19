// Apps/UltraFIBU/ui/UltraFIBUTabelle.cpp
// See the header for why the model answers SortRole and why a row carries its
// record id rather than trusting its position.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUTabelle.h"

namespace UltraFIBU {

using namespace UltraCanvas;

// ===== MODEL =====

ListDataValue TabellenModell::GetData(const ListIndex& index, ListDataRole role) const {
    if (index.row < 0 || index.row >= static_cast<int>(zeilen_.size()))
        return std::monostate{};
    const TabellenZeile& zeile = zeilen_[index.row];
    if (index.column < 0 || index.column >= static_cast<int>(zeile.zellen.size()))
        return std::monostate{};
    const TabellenZelle& zelle = zeile.zellen[index.column];

    switch (role) {
        case ListDataRole::DisplayRole:
            return zelle.text;
        case ListDataRole::SortRole:
            // The number the column really means, so "1.000,00" does not sort
            // before "999,00" the way those look as text. Only reported when
            // it fits an int, because ListDataValue has no int64 alternative -
            // the panel sorts through the comparator in Bauen(), which reads
            // the exact value and has no such limit.
            if (zelle.hatSortwert &&
                zelle.sortwert >= -2147483648LL && zelle.sortwert <= 2147483647LL)
                return static_cast<int>(zelle.sortwert);
            return std::monostate{};
        default:
            return std::monostate{};
    }
}

ListColumnDef TabellenModell::GetColumnDef(int column) const {
    if (column < 0 || column >= static_cast<int>(spalten_.size()))
        return ListColumnDef("", 100);
    return spalten_[column];
}

void TabellenModell::SetSpalten(std::vector<ListColumnDef> spalten) {
    spalten_ = std::move(spalten);
    NotifyDataChanged();
}

void TabellenModell::SetZeilen(std::vector<TabellenZeile> zeilen) {
    zeilen_ = std::move(zeilen);
    NotifyDataChanged();
}

void TabellenModell::Leeren() {
    zeilen_.clear();
    NotifyDataChanged();
}

bool TabellenModell::Sortwert(int row, int column, int64_t& out) const {
    if (row < 0 || row >= static_cast<int>(zeilen_.size())) return false;
    const TabellenZeile& zeile = zeilen_[row];
    if (column < 0 || column >= static_cast<int>(zeile.zellen.size())) return false;
    if (!zeile.zellen[column].hatSortwert) return false;
    out = zeile.zellen[column].sortwert;
    return true;
}

int64_t TabellenModell::ZeileId(int sourceRow) const {
    if (sourceRow < 0 || sourceRow >= static_cast<int>(zeilen_.size())) return 0;
    return zeilen_[sourceRow].id;
}

// ===== PANEL =====

namespace {
constexpr float kSucheHoehe = 26.0f;
constexpr float kFussHoehe  = 22.0f;
constexpr float kAbstand    = 6.0f;
}

std::shared_ptr<UltraCanvasContainer> TabellenPanel::Bauen(
        const std::string& id, float x, float y, float breite, float hoehe,
        const std::string& suchHinweis) {
    wurzel_ = CreateContainer(id, x, y, breite, hoehe);

    suche_ = CreateTextInput(id + "Suche", static_cast<int>(kAbstand),
                             static_cast<int>(kAbstand),
                             static_cast<int>(breite - 2 * kAbstand),
                             static_cast<int>(kSucheHoehe));
    suche_->SetPlaceholder(suchHinweis);
    wurzel_->AddChild(suche_);

    const float listeY = kAbstand + kSucheHoehe + kAbstand;
    const float listeH = hoehe - listeY - kFussHoehe - kAbstand;

    modell_ = std::make_shared<TabellenModell>();
    proxy_  = std::make_shared<UltraCanvasListSortFilterProxy>();
    proxy_->SetSourceModel(modell_);

    liste_ = std::make_shared<UltraCanvasListView>(
        id + "Liste", kAbstand, listeY, breite - 2 * kAbstand, listeH);
    // The view is handed the proxy, never the model. It has no idea either
    // sorting or filtering is happening, which is the whole design.
    liste_->SetModel(proxy_);
    liste_->SetShowHeader(true);
    wurzel_->AddChild(liste_);

    fuss_ = CreateLabel(id + "Fuss", kAbstand, listeY + listeH + 2.0f,
                        breite - 2 * kAbstand, kFussHoehe, "");
    wurzel_->AddChild(fuss_);

    // Typing filters. The proxy does the work over every column; the view is
    // not told, it simply has fewer rows.
    suche_->onTextChanged = [this](const std::string& text) {
        if (!proxy_) return;
        proxy_->SetFilterText(text);
        // The summary describes what is on screen, so it is recomputed here
        // and not only when the data is reloaded.
        FussAktualisieren();
    };

    // Header click sorts. This is the wiring the framework's own example
    // shows - the view reports the click and displays the order, the proxy
    // decides it.
    liste_->onHeaderClicked = [this](int column) { Sortieren(column); };

    liste_->onItemActivated = [this](int proxyRow) {
        if (!onZeileAktiviert) return;
        const int64_t id = modell_->ZeileId(proxy_->MapToSource(proxyRow));
        if (id != 0) onZeileAktiviert(id);
    };
    liste_->onSelectionChanged = [this](const std::vector<int>& rows) {
        if (!onAuswahlGeaendert) return;
        if (rows.empty()) { onAuswahlGeaendert(0); return; }
        onAuswahlGeaendert(modell_->ZeileId(proxy_->MapToSource(rows.front())));
    };

    return wurzel_;
}

void TabellenPanel::Sortieren(int spalte) {
    if (!proxy_ || !liste_) return;
    // Clicking the sorted column turns it round; clicking another starts it
    // ascending.
    const bool aufsteigend =
        !(spalte == liste_->GetSortColumn() && liste_->GetSortAscending());
    proxy_->SortByColumn(spalte, aufsteigend ? ListSortOrder::Ascending
                                             : ListSortOrder::Descending);
    liste_->SetSortIndicator(spalte, aufsteigend);
}

void TabellenPanel::SetSpalten(std::vector<ListColumnDef> spalten) {
    if (!modell_) return;
    const int anzahl = static_cast<int>(spalten.size());
    modell_->SetSpalten(std::move(spalten));
    if (!proxy_) return;

    // One comparator per column, reading the exact int64 the cell carries.
    // A column whose cells have no sort value (a name, a status) falls through
    // to a case-insensitive text comparison, which is what the proxy would have
    // done anyway - so this costs nothing where it is not needed and is exact
    // where it is.
    for (int spalte = 0; spalte < anzahl; ++spalte) {
        proxy_->SetColumnComparator(
            spalte, [](const IListModel& quelle, int links, int rechts, int spaltenNr) {
                const auto* modell = dynamic_cast<const TabellenModell*>(&quelle);
                int64_t a = 0, b = 0;
                if (modell != nullptr && modell->Sortwert(links, spaltenNr, a) &&
                    modell->Sortwert(rechts, spaltenNr, b)) {
                    if (a < b) return -1;
                    if (a > b) return 1;
                    return 0;
                }
                const ListIndex li{ links, spaltenNr };
                const ListIndex ri{ rechts, spaltenNr };
                return UltraCanvasListSortFilterProxy::CompareText(
                    GetStringValue(quelle.GetData(li, ListDataRole::DisplayRole)),
                    GetStringValue(quelle.GetData(ri, ListDataRole::DisplayRole)),
                    false);
            });
    }
}

void TabellenPanel::SetZeilen(std::vector<TabellenZeile> zeilen) {
    if (!modell_) return;
    modell_->SetZeilen(std::move(zeilen));
    // The source changed underneath the proxy, so the mapping it holds is
    // stale until it is rebuilt.
    if (proxy_) proxy_->Invalidate();
    FussAktualisieren();
}

void TabellenPanel::SetFusszeile(const std::string& text) {
    if (fuss_) fuss_->SetText(text);
}

void TabellenPanel::SetFussFunktion(
        std::function<std::string(const std::vector<int64_t>&)> fn) {
    fussFunktion_ = std::move(fn);
    FussAktualisieren();
}

std::vector<int64_t> TabellenPanel::SichtbareIds() const {
    std::vector<int64_t> ids;
    if (!proxy_ || !modell_) return ids;
    const int anzahl = proxy_->GetRowCount();
    ids.reserve(static_cast<size_t>(anzahl));
    for (int zeile = 0; zeile < anzahl; ++zeile)
        ids.push_back(modell_->ZeileId(proxy_->MapToSource(zeile)));
    return ids;
}

void TabellenPanel::FussAktualisieren() {
    if (!fussFunktion_ || !fuss_) return;
    fuss_->SetText(fussFunktion_(SichtbareIds()));
}

int64_t TabellenPanel::AusgewaehlteId() const {
    if (!liste_ || !proxy_ || !modell_) return 0;
    UltraCanvas::IListSelection* auswahl = liste_->GetSelection();
    if (auswahl == nullptr) return 0;
    const std::vector<int> rows = auswahl->GetSelectedRows();
    if (rows.empty()) return 0;
    return modell_->ZeileId(proxy_->MapToSource(rows.front()));
}

} // namespace UltraFIBU
