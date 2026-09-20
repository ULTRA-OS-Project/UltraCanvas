// Apps/UltraFIBU/ui/UltraFIBUApp.cpp
// The window and the four screens. See the header for why they are read-only
// apart from posting and printing.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUApp.h"

#include <ctime>

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"

#include "UltraFIBURechnungPdf.h"

#include <algorithm>

namespace UltraFIBU {

using namespace UltraCanvas;

namespace {

constexpr float kFensterB   = 1180.0f;
constexpr float kFensterH   = 760.0f;
constexpr float kKopfH      = 52.0f;
constexpr float kLeisteH    = 34.0f;
constexpr float kStatusH    = 24.0f;

// A date for a column: the German text a bookkeeper reads, and the day number
// it sorts by. Sorting 15.06.2026 as text would order it by day of month,
// which is the bug this pair exists to prevent.
TabellenZelle DatumsZelle(const Date& datum) {
    if (!datum.Valid()) return TabellenZelle(std::string());
    return TabellenZelle(FormatDateGerman(datum), datum.ToEpochDay());
}

// An amount: German formatting, sorted by its minor units.
TabellenZelle BetragsZelle(const Money& betrag) {
    if (!betrag.Valid()) return TabellenZelle(std::string());
    return TabellenZelle(betrag.ToString(), betrag.Minor());
}

std::string Zahl(int64_t wert) {
    std::string ziffern;
    int64_t v = wert < 0 ? -wert : wert;
    if (v == 0) ziffern = "0";
    while (v > 0) {
        ziffern.insert(ziffern.begin(), static_cast<char>('0' + (v % 10)));
        v /= 10;
    }
    if (wert < 0) ziffern.insert(ziffern.begin(), '-');
    return ziffern;
}

} // namespace

// ===== OPENING =====

bool FibuApp::Initialisieren(const std::string& datenbank, std::string& fehler) {
    const StoreResult geoeffnet = store_.Open("ultrafibu-ui", datenbank);
    if (!geoeffnet) { fehler = geoeffnet.fehler; return false; }

    const std::vector<Mandant> alle = store_.Mandanten();
    if (alle.empty()) {
        fehler = "In \"" + datenbank + "\" ist noch kein Mandant angelegt.\n"
                 "Mit \"ultrafibu einrichten " + datenbank +
                 " --firma ... --gj-beginn ...\" anlegen.";
        return false;
    }
    mandant_ = alle[0];

    // The application acts as the first administrator in the file and says so
    // in the audit trail, exactly as the CLI does. A real login belongs to
    // server mode.
    akteur_.rolle = BenutzerRolle::Administrator;
    akteur_.anmeldename = "ui";
    for (const Benutzer& benutzer : store_.BenutzerListe()) {
        if (benutzer.rolle == BenutzerRolle::Administrator && benutzer.aktiv) {
            akteur_.benutzerId  = benutzer.id;
            akteur_.anmeldename = benutzer.anmeldename + " (ui)";
            break;
        }
    }

    const std::vector<Geschaeftsjahr> jahre = store_.Geschaeftsjahre(mandant_.id);
    if (!jahre.empty()) { jahr_ = jahre.back(); jahrGefunden_ = true; }
    return true;
}

// ===== WINDOW =====

std::shared_ptr<UltraCanvasWindow> FibuApp::FensterBauen() {
    WindowConfig config;
    config.title  = "UltraFIBU - Buchhaltung";
    config.width  = static_cast<int>(kFensterB);
    config.height = static_cast<int>(kFensterH);
    fenster_ = CreateWindow(config);

    kopf_ = CreateLabel("fibuKopf", 12, 8, kFensterB - 24, kKopfH - 12, "");
    fenster_->AddChild(kopf_);

    // The two actions sit above the screens rather than inside the Belege
    // panel, so their enabled state is one place and the panel stays the
    // reusable thing it is.
    buchenKnopf_ = CreateButton("fibuBuchen", 12, kKopfH, 150, kLeisteH - 6,
                                "Beleg buchen");
    buchenKnopf_->SetOnClick([this]() { GewaehltenBelegBuchen(); });
    fenster_->AddChild(buchenKnopf_);

    druckenKnopf_ = CreateButton("fibuDrucken", 170, kKopfH, 170, kLeisteH - 6,
                                 "Rechnung als PDF");
    druckenKnopf_->SetOnClick([this]() { GewaehltenBelegDrucken(); });
    fenster_->AddChild(druckenKnopf_);

    hochladenKnopf_ = CreateButton("fibuHochladen", 348, kKopfH, 170, kLeisteH - 6,
                                   "Beleg hochladen");
    hochladenKnopf_->SetOnClick([this]() { BelegDialogOeffnen(); });
    fenster_->AddChild(hochladenKnopf_);

    // Dropping files onto the window does the same thing as the button. The
    // filter is the framework's own mechanism rather than a hand-rolled
    // handler, and it sits on the window because a receipt may be dropped
    // anywhere on it - hunting for a small target with a full hand is not how
    // anybody files receipts.
    fenster_->InstallEventFilter(
        "fibuBelegDrop",
        [this](const UltraCanvas::UCEvent& ereignis) -> bool {
            if (ereignis.droppedFiles.empty()) return false;
            BelegeHochladen(ereignis.droppedFiles);
            return true;
        },
        { UltraCanvas::UCEventType::Drop });

    const float reiterY = kKopfH + kLeisteH;
    const float reiterH = kFensterH - reiterY - kStatusH;
    reiter_ = CreateTabbedContainer("fibuReiter", 0, reiterY, kFensterB, reiterH);

    const float seiteB = kFensterB - 8;
    const float seiteH = reiterH - 44;

    reiter_->AddTab("Belege",
                    belege_.Bauen("fibuBelege", 0, 0, seiteB, seiteH,
                                  "Nummer, Partner oder Betrag suchen ..."));
    reiter_->AddTab("Journal",
                    journal_.Bauen("fibuJournal", 0, 0, seiteB, seiteH,
                                   "Konto, Beleg oder Buchungstext suchen ..."));
    reiter_->AddTab("Summen und Salden",
                    salden_.Bauen("fibuSalden", 0, 0, seiteB, seiteH,
                                  "Konto oder Bezeichnung suchen ..."));
    reiter_->AddTab("Partner",
                    partner_.Bauen("fibuPartner", 0, 0, seiteB, seiteH,
                                   "Name, Ort oder USt-IdNr. suchen ..."));
    fenster_->AddChild(reiter_);

    status_ = CreateLabel("fibuStatus", 12, kFensterH - kStatusH,
                          kFensterB - 24, kStatusH - 4, "");
    fenster_->AddChild(status_);

    // The selection carries a record id, never a row number: the proxy has
    // re-ordered the rows by the time this fires.
    belege_.onAuswahlGeaendert = [this](int64_t id) { gewaehlterBeleg_ = id; };
    belege_.onZeileAktiviert   = [this](int64_t id) {
        gewaehlterBeleg_ = id;
        Beleg beleg;
        if (!store_.BelegById(id, beleg)) return;
        Melden(BelegArtLabel(beleg.art) + " " + beleg.nummer + ": " +
               BelegStatusLabel(beleg.status) + ", " + beleg.brutto.ToString() +
               ", " + Zahl(static_cast<int64_t>(beleg.positionen.size())) +
               " Position(en), " +
               Zahl(static_cast<int64_t>(store_.BuchungenZuBeleg(id).size())) +
               " Buchung(en)");
    };

    Aktualisieren();
    return fenster_;
}

void FibuApp::KopfAktualisieren() {
    if (!kopf_) return;
    std::string text = mandant_.name;
    if (!mandant_.ort.empty()) text += ", " + mandant_.ort;
    if (jahrGefunden_) {
        text += "   |   Geschäftsjahr " + jahr_.bezeichnung + " (" +
                FormatDateGerman(jahr_.beginn) + " - " +
                FormatDateGerman(jahr_.ende) + ")";
        text += jahr_.festschreibungBis.Valid()
                    ? "   |   festgeschrieben bis " +
                          FormatDateGerman(jahr_.festschreibungBis)
                    : "   |   nichts festgeschrieben";
    } else {
        text += "   |   kein Geschäftsjahr angelegt";
    }
    kopf_->SetText(text);
}

void FibuApp::Melden(const std::string& text) {
    if (status_) status_->SetText(text);
}

// ===== SCREENS =====

void FibuApp::Aktualisieren() {
    // Re-read the fiscal year too: freezing changes it, and the header shows it.
    const std::vector<Geschaeftsjahr> jahre = store_.Geschaeftsjahre(mandant_.id);
    if (!jahre.empty()) { jahr_ = jahre.back(); jahrGefunden_ = true; }

    KopfAktualisieren();
    BelegeFuellen();
    JournalFuellen();
    SaldenFuellen();
    PartnerFuellen();
}

void FibuApp::BelegeFuellen() {
    belege_.SetSpalten({
        ListColumnDef("Nummer", 130, TextAlignment::Left),
        ListColumnDef("Datum", 100, TextAlignment::Left),
        ListColumnDef("Art", 150, TextAlignment::Left),
        ListColumnDef("Partner / Beleg", 220, TextAlignment::Left),
        ListColumnDef("Brutto", 110, TextAlignment::Right),
        ListColumnDef("Offen", 110, TextAlignment::Right),
        ListColumnDef("Fällig", 100, TextAlignment::Left),
        ListColumnDef("Status", 150, TextAlignment::Left),
    });

    Store::BelegFilter filter;
    filter.mandantId = mandant_.id;
    const std::vector<Beleg> liste = store_.BelegListe(filter);

    std::vector<TabellenZeile> zeilen;
    zeilen.reserve(liste.size());

    for (const Beleg& beleg : liste) {
        TabellenZeile zeile;
        zeile.id = beleg.id;
        zeile.zellen = {
            TabellenZelle(beleg.nummer),
            DatumsZelle(beleg.datum),
            TabellenZelle(BelegArtLabel(beleg.art)),
            // A freshly uploaded receipt has no partner yet - nothing is read
            // out of the PDF - so its file name stands in. Without it a stack
            // of imported receipts is a column of identical rows and the user
            // cannot tell which is which, which makes the import useless.
            TabellenZelle(beleg.partnerName.empty() ? beleg.buchungstext
                                                    : beleg.partnerName),
            BetragsZelle(beleg.brutto),
            BetragsZelle(beleg.Offen()),
            DatumsZelle(beleg.faelligAm),
            TabellenZelle(BelegStatusLabel(beleg.status)),
        };
        zeilen.push_back(std::move(zeile));
    }
    geladeneBelege_ = liste;
    belege_.SetZeilen(std::move(zeilen));
    belege_.SetFussFunktion([this](const std::vector<int64_t>& ids) {
        Money offen = Money::Zero(mandant_.waehrung);
        int   entwuerfe = 0;
        for (int64_t id : ids) {
            for (const Beleg& beleg : geladeneBelege_) {
                if (beleg.id != id) continue;
                if (beleg.Offen().Valid()) offen = offen + beleg.Offen();
                if (beleg.status == BelegStatus::Entwurf) ++entwuerfe;
                break;
            }
        }
        std::string text = Zahl(static_cast<int64_t>(ids.size())) + " Beleg(e)";
        if (ids.size() != geladeneBelege_.size())
            text += " von " + Zahl(static_cast<int64_t>(geladeneBelege_.size())) +
                    " (gefiltert)";
        text += ", davon " + Zahl(entwuerfe) + " Entwurf/Entwürfe   |   offen " +
                offen.ToString() + " " + mandant_.waehrung;
        return text;
    });
}

void FibuApp::JournalFuellen() {
    journal_.SetSpalten({
        ListColumnDef("Nr.", 60, TextAlignment::Right),
        ListColumnDef("Datum", 100, TextAlignment::Left),
        ListColumnDef("Beleg", 130, TextAlignment::Left),
        ListColumnDef("Umsatz", 110, TextAlignment::Right),
        ListColumnDef("S/H", 45, TextAlignment::Left),
        ListColumnDef("Konto", 80, TextAlignment::Left),
        ListColumnDef("Gegenkonto", 90, TextAlignment::Left),
        ListColumnDef("Steuer", 95, TextAlignment::Right),
        ListColumnDef("Buchungstext", 230, TextAlignment::Left),
    });

    const std::vector<Buchung> buchungen = store_.Journal(mandant_.id);
    std::vector<TabellenZeile> zeilen;
    zeilen.reserve(buchungen.size());

    for (const Buchung& b : buchungen) {
        std::string text = b.buchungstext;
        if (b.IstStorniert())   text += "  [storniert]";
        else if (b.IstStorno()) text += "  [Storno]";
        TabellenZeile zeile;
        zeile.id = b.id;
        zeile.zellen = {
            TabellenZelle(Zahl(b.laufendeNummer), b.laufendeNummer),
            DatumsZelle(b.belegdatum),
            TabellenZelle(b.belegfeld1),
            BetragsZelle(b.umsatz),
            TabellenZelle(SollHabenToText(b.sollHaben)),
            TabellenZelle(b.konto),
            TabellenZelle(b.gegenkonto),
            b.steuer.IsZero() ? TabellenZelle(std::string()) : BetragsZelle(b.steuer),
            TabellenZelle(text),
        };
        zeilen.push_back(std::move(zeile));
    }
    geladeneBuchungen_ = buchungen;
    journal_.SetZeilen(std::move(zeilen));

    // The hash chain is checked every time the journal is loaded. It costs one
    // pass over the rows and it is the only thing that makes the chain worth
    // storing; a chain nobody verifies is decoration. The check and the
    // balance are over the *whole* journal on purpose - both are statements
    // about the ledger, and filtering the view does not make half a ledger
    // balance.
    const HashKettenPruefung pruefung = store_.PruefeHashKette(mandant_.id);
    const Money differenz = store_.Buchungskreisdifferenz(mandant_.id);
    const std::string urteil =
        (differenz.IsZero() ? std::string("Soll und Haben gleichen sich aus")
                            : "ACHTUNG: Differenz " + differenz.ToString()) +
        "   |   " +
        (pruefung.ok ? std::string("Prüfsummenkette unversehrt")
                     : "ACHTUNG: " + pruefung.fehler);

    journal_.SetFussFunktion([this, urteil](const std::vector<int64_t>& ids) {
        int storniertSichtbar = 0;
        for (int64_t id : ids) {
            for (const Buchung& b : geladeneBuchungen_) {
                if (b.id != id) continue;
                if (b.IstStorniert()) ++storniertSichtbar;
                break;
            }
        }
        std::string text = Zahl(static_cast<int64_t>(ids.size())) + " Buchung(en)";
        if (ids.size() != geladeneBuchungen_.size())
            text += " von " + Zahl(static_cast<int64_t>(geladeneBuchungen_.size())) +
                    " (gefiltert)";
        text += ", davon " + Zahl(storniertSichtbar) + " storniert   |   " +
                "gesamtes Journal: " + urteil;
        return text;
    });
}

void FibuApp::SaldenFuellen() {
    salden_.SetSpalten({
        ListColumnDef("Konto", 90, TextAlignment::Left),
        ListColumnDef("Bezeichnung", 340, TextAlignment::Left),
        ListColumnDef("Soll", 130, TextAlignment::Right),
        ListColumnDef("Haben", 130, TextAlignment::Right),
        ListColumnDef("Saldo", 130, TextAlignment::Right),
    });

    const std::vector<Store::KontoSaldo> salden = store_.SummenUndSalden(mandant_.id);
    std::vector<TabellenZeile> zeilen;
    zeilen.reserve(salden.size());

    int64_t laufend = 0;
    for (const Store::KontoSaldo& k : salden) {
        TabellenZeile zeile;
        // A Saldenliste line has no database id of its own, so it is
        // identified by its position in the loaded list - which is all the
        // footer needs to add the visible ones back up.
        zeile.id = ++laufend;
        zeile.zellen = {
            TabellenZelle(k.konto),
            TabellenZelle(k.bezeichnung),
            BetragsZelle(k.soll),
            BetragsZelle(k.haben),
            BetragsZelle(k.saldo),
        };
        zeilen.push_back(std::move(zeile));
    }
    geladeneSalden_ = salden;
    salden_.SetZeilen(std::move(zeilen));
    salden_.SetFussFunktion([this](const std::vector<int64_t>& ids) {
        Money soll  = Money::Zero(mandant_.waehrung);
        Money haben = Money::Zero(mandant_.waehrung);
        for (int64_t id : ids) {
            const size_t index = static_cast<size_t>(id - 1);
            if (id <= 0 || index >= geladeneSalden_.size()) continue;
            soll  = soll  + geladeneSalden_[index].soll;
            haben = haben + geladeneSalden_[index].haben;
        }
        std::string text = Zahl(static_cast<int64_t>(ids.size())) + " Konten";
        if (ids.size() != geladeneSalden_.size()) {
            text += " von " + Zahl(static_cast<int64_t>(geladeneSalden_.size())) +
                    " (gefiltert)";
        }
        text += "   |   Soll " + soll.ToString() + "   Haben " + haben.ToString();
        // "Ausgeglichen" is only a meaningful claim about the whole list: a
        // filtered subset has no reason to balance and saying it does not
        // would read as an error.
        if (ids.size() == geladeneSalden_.size()) {
            text += "   |   " + ((soll - haben).IsZero()
                                     ? std::string("ausgeglichen")
                                     : "ACHTUNG: Differenz " + (soll - haben).ToString());
        }
        return text;
    });
}

void FibuApp::PartnerFuellen() {
    partner_.SetSpalten({
        ListColumnDef("Konto", 80, TextAlignment::Left),
        ListColumnDef("Name", 260, TextAlignment::Left),
        ListColumnDef("Ort", 160, TextAlignment::Left),
        ListColumnDef("Land", 55, TextAlignment::Left),
        ListColumnDef("USt-IdNr.", 150, TextAlignment::Left),
        ListColumnDef("Typ", 100, TextAlignment::Left),
        ListColumnDef("Steuerkategorie", 170, TextAlignment::Left),
    });

    const std::vector<Partner> liste =
        store_.PartnerListe(mandant_.id, PartnerTyp::Beides);
    std::vector<TabellenZeile> zeilen;
    zeilen.reserve(liste.size());

    for (const Partner& p : liste) {
        TabellenZeile zeile;
        zeile.id = p.id;
        zeile.zellen = {
            TabellenZelle(p.konto),
            TabellenZelle(p.name),
            TabellenZelle(p.ort),
            TabellenZelle(p.land),
            TabellenZelle(p.ustIdNr),
            TabellenZelle(PartnerTypToText(p.typ)),
            TabellenZelle(SteuerkategorieToText(p.steuerkategorie)),
        };
        zeilen.push_back(std::move(zeile));
    }
    geladenePartner_ = liste;
    partner_.SetZeilen(std::move(zeilen));
    partner_.SetFussFunktion([this](const std::vector<int64_t>& ids) {
        int kundenSichtbar = 0, lieferantenSichtbar = 0;
        for (int64_t id : ids) {
            for (const Partner& p : geladenePartner_) {
                if (p.id != id) continue;
                if (p.IstKunde())     ++kundenSichtbar;
                if (p.IstLieferant()) ++lieferantenSichtbar;
                break;
            }
        }
        std::string text = Zahl(static_cast<int64_t>(ids.size())) + " Partner";
        if (ids.size() != geladenePartner_.size())
            text += " von " + Zahl(static_cast<int64_t>(geladenePartner_.size())) +
                    " (gefiltert)";
        text += "   |   " + Zahl(kundenSichtbar) + " Kunde(n), " +
                Zahl(lieferantenSichtbar) + " Lieferant(en)";
        return text;
    });
}

// ===== ACTIONS =====

void FibuApp::GewaehltenBelegBuchen() {
    if (gewaehlterBeleg_ == 0) {
        Melden("Bitte zuerst einen Beleg in der Liste auswählen.");
        return;
    }
    Beleg beleg;
    if (!store_.BelegById(gewaehlterBeleg_, beleg)) {
        Melden("Der Beleg wurde nicht gefunden.");
        return;
    }
    // Every rule - the draft state, the frozen period, the role - is the
    // store's, so this reports what it decided rather than deciding anything.
    const StoreResult gebucht = store_.Buchen(beleg, akteur_);
    if (!gebucht) { Melden("Nicht gebucht: " + gebucht.fehler); return; }

    Melden("Beleg " + beleg.nummer + " gebucht (" + beleg.brutto.ToString() + ").");
    Aktualisieren();
}

void FibuApp::BelegDialogOeffnen() {
    // Several at once is the point: receipts arrive in batches, not one by one.
    UltraCanvas::FileDialogOptions optionen;
    optionen.SetTitle("Belege hochladen");
    optionen.filters.emplace_back("PDF-Belege", "pdf");
    optionen.parentWindow = fenster_.get();

    UltraCanvas::UltraCanvasFileLoader::OpenMultipleFilesDialog(
        optionen,
        [this](UltraCanvas::DialogResult ergebnis,
               const std::vector<std::string>& pfade) {
            if (ergebnis != UltraCanvas::DialogResult::OK) return;
            BelegeHochladen(pfade);
        });
}

void FibuApp::BelegeHochladen(const std::vector<std::string>& pfade) {
    if (pfade.empty()) return;

    // A receipt that has just arrived is dated today until somebody says
    // otherwise; the draft is editable and the date is the first thing the
    // user corrects. Reading a clock is fine here - this is a UI action, not
    // a report that has to be reproducible.
    Date heute;
    {
        const std::time_t jetzt = std::time(nullptr);
        std::tm teile{};
#if defined(_WIN32)
        localtime_s(&teile, &jetzt);
#else
        localtime_r(&jetzt, &teile);
#endif
        heute = Date(teile.tm_year + 1900, teile.tm_mon + 1, teile.tm_mday);
    }

    const Store::BelegImportBericht bericht = store_.ImportiereBelegDateien(
        mandant_.id, pfade, BelegArt::Eingangsrechnung, heute, "eingang", akteur_);

    if (!bericht.ok) {
        Melden(bericht.fehler.empty() ? "Es konnte nichts übernommen werden."
                                      : bericht.fehler);
        return;
    }

    std::string text = std::to_string(bericht.angelegt) + " Beleg(e) angelegt";
    if (bericht.bekannt > 0)
        text += ", " + std::to_string(bericht.bekannt) + " schon vorhanden";
    if (bericht.abgelehnt > 0) {
        // Name the first rejection rather than only counting: "1 abgelehnt"
        // sends the user looking, and the reason is already known here.
        text += ", " + std::to_string(bericht.abgelehnt) + " abgelehnt";
        for (const Store::BelegImportEintrag& e : bericht.eintraege) {
            if (!e.ok) { text += " (" + e.dateiname + ": " + e.fehler + ")"; break; }
        }
    }
    text += ". Betrag und Konto fehlen noch - aus dem PDF wird nichts ausgelesen.";
    Melden(text);
    Aktualisieren();
}

void FibuApp::GewaehltenBelegDrucken() {
    if (gewaehlterBeleg_ == 0) {
        Melden("Bitte zuerst einen Beleg in der Liste auswählen.");
        return;
    }
    Beleg beleg;
    if (!store_.BelegById(gewaehlterBeleg_, beleg)) {
        Melden("Der Beleg wurde nicht gefunden.");
        return;
    }
    Partner empfaenger;
    if (beleg.partnerId != 0) store_.PartnerById(beleg.partnerId, empfaenger);

    const std::string ziel = beleg.nummer + ".pdf";
    const RechnungPdfErgebnis ergebnis = SchreibeRechnungPdf(
        mandant_, beleg, empfaenger,
        store_.SteuerschluesselListe(mandant_.id, beleg.datum), ziel);

    if (!ergebnis.ok) { Melden("Nicht gedruckt: " + ergebnis.fehler); return; }
    if (!ergebnis.VollstaendigNachUStG()) {
        // The file is written either way - a draft has to be printable before
        // the master data is complete - but a deficient invoice costs the
        // recipient their input-tax deduction, so it is said out loud.
        std::string fehlt;
        for (const std::string& f : ergebnis.fehlendePflichtangaben) {
            if (!fehlt.empty()) fehlt += ", ";
            fehlt += f;
        }
        Melden("\"" + ziel + "\" geschrieben - ACHTUNG, Pflichtangaben nach "
               "§ 14 UStG fehlen: " + fehlt);
        return;
    }
    Melden("\"" + ziel + "\" geschrieben.");
}

} // namespace UltraFIBU
