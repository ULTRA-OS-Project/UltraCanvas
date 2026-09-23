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
constexpr float kFensterH   = 790.0f;
constexpr float kMenueH     = 30.0f;
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

// "20" -> 200, "8,1" -> 81. Integer arithmetic: a tax rate read through a
// double comes back a fraction off, and that fraction reaches every invoice.
bool ProzentNachPromille(const std::string& text, int& out) {
    std::string ganz, bruch;
    bool nachKomma = false;
    for (const char c : text) {
        if (c == ' ' || c == '%') continue;
        if ((c == ',' || c == '.') && !nachKomma) { nachKomma = true; continue; }
        if (c < '0' || c > '9') return false;
        (nachKomma ? bruch : ganz).push_back(c);
    }
    if (ganz.empty() && bruch.empty()) return false;
    if (bruch.size() > 1) return false;
    const long promille = (ganz.empty() ? 0 : std::atol(ganz.c_str())) * 10 +
                          (bruch.empty() ? 0 : (bruch[0] - '0'));
    if (promille < 0 || promille > 1000) return false;
    out = static_cast<int>(promille);
    return true;
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

    // The configuration menu. It is a menu rather than another button because
    // what goes in it is setup rather than daily work: a rate a member state
    // changed, the shipped rates taken over once. Putting those next to
    // "Beleg buchen" would invite a mis-click into master data.
    menue_ = CreateMenu("fibuMenue", 0, 0, kFensterB, kMenueH);
    menue_->SetMenuType(MenuType::Menubar);
    {
        MenuItemData konfiguration;
        konfiguration.type  = MenuItemType::Submenu;
        konfiguration.label = "Konfiguration";
        konfiguration.subItems = {
            MenuItemData::Action("EU-Steuersätze ...",
                                 [this]() { KonfigurationOeffnen(); }),
            MenuItemData::Action("Neuen Steuersatz setzen ...",
                                 [this]() { EuSatzDialogOeffnen(); }),
            MenuItemData::Separator(),
            MenuItemData::Action("Mitgelieferte Sätze übernehmen",
                                 [this]() { EuSaetzeUebernehmen(); }),
        };
        menue_->AddItem(konfiguration);
    }
    fenster_->AddChild(menue_);

    kopf_ = CreateLabel("fibuKopf", 12, kMenueH + 4, kFensterB - 24, kKopfH - 12, "");
    fenster_->AddChild(kopf_);

    // The two actions sit above the screens rather than inside the Belege
    // panel, so their enabled state is one place and the panel stays the
    // reusable thing it is.
    buchenKnopf_ = CreateButton("fibuBuchen", 12, kMenueH + kKopfH, 150, kLeisteH - 6,
                                "Beleg buchen");
    buchenKnopf_->SetOnClick([this]() { GewaehltenBelegBuchen(); });
    fenster_->AddChild(buchenKnopf_);

    druckenKnopf_ = CreateButton("fibuDrucken", 170, kMenueH + kKopfH, 170, kLeisteH - 6,
                                 "Rechnung als PDF");
    druckenKnopf_->SetOnClick([this]() { GewaehltenBelegDrucken(); });
    fenster_->AddChild(druckenKnopf_);

    hochladenKnopf_ = CreateButton("fibuHochladen", 348, kMenueH + kKopfH, 170, kLeisteH - 6,
                                   "Beleg hochladen");
    hochladenKnopf_->SetOnClick([this]() { BelegDialogOeffnen(); });
    fenster_->AddChild(hochladenKnopf_);

    // The two that open the entry form. Separate buttons rather than one with a
    // type dropdown, because the direction decides which partners and which tax
    // keys the form offers - it is not a field on the document, it is which
    // document this is.
    auto rechnungKnopf = CreateButton("fibuNeueRechnung", 526, kMenueH + kKopfH, 160,
                                      kLeisteH - 6, "Neue Rechnung");
    rechnungKnopf->SetOnClick([this]() {
        BelegFormularOeffnen(BelegArt::Ausgangsrechnung);
    });
    fenster_->AddChild(rechnungKnopf);

    auto eingangKnopf = CreateButton("fibuNeuerEingang", 694, kMenueH + kKopfH, 190,
                                     kLeisteH - 6, "Eingangsrechnung erfassen");
    eingangKnopf->SetOnClick([this]() {
        BelegFormularOeffnen(BelegArt::Eingangsrechnung);
    });
    fenster_->AddChild(eingangKnopf);

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

    const float reiterY = kMenueH + kKopfH + kLeisteH;
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

    // The rate editor. Its two buttons sit on the page rather than in the
    // window's toolbar: they belong to this screen, and a "Steuersatz löschen"
    // button visible while the Belege list is showing would be an invitation.
    {
        auto seite = CreateContainer("fibuEuSeite", 0, 0, seiteB, seiteH);
        satzNeuKnopf_ = CreateButton("fibuSatzNeu", 4, 4, 230, kLeisteH - 6,
                                     "Neuen Steuersatz setzen");
        satzNeuKnopf_->SetOnClick([this]() { EuSatzDialogOeffnen(); });
        seite->AddChild(satzNeuKnopf_);

        satzLoeschenKnopf_ = CreateButton("fibuSatzWeg", 242, 4, 190, kLeisteH - 6,
                                          "Steuersatz löschen");
        satzLoeschenKnopf_->SetOnClick([this]() { GewaehltenEuSatzLoeschen(); });
        seite->AddChild(satzLoeschenKnopf_);

        seite->AddChild(euSaetze_.Bauen("fibuEuSaetze", 0, kLeisteH, seiteB,
                                        seiteH - kLeisteH,
                                        "Land, Satz oder Quelle suchen ..."));
        euSaetzeReiter_ = reiter_->AddTab("EU-Steuersätze", seite);
    }

    // The entry form, as a tab. See UltraFIBUBelegDialog.h for why the tax
    // dropdown rather than the layout is the interesting part of it.
    {
        formular_ = std::make_unique<BelegDialog>(store_, mandant_, akteur_);
        formular_->onMeldung     = [this](const std::string& text) { Melden(text); };
        formular_->onGespeichert = [this](const Beleg&) { Aktualisieren(); };
        formularReiter_ = reiter_->AddTab(
            "Beleg erfassen",
            formular_->Bauen("fibuFormular", seiteB, seiteH, BelegArt::Ausgangsrechnung));
    }
    fenster_->AddChild(reiter_);

    status_ = CreateLabel("fibuStatus", 12, kFensterH - kStatusH,
                          kFensterB - 24, kStatusH - 4, "");
    fenster_->AddChild(status_);

    // The selection carries a record id, never a row number: the proxy has
    // re-ordered the rows by the time this fires.
    belege_.onAuswahlGeaendert  = [this](int64_t id) { gewaehlterBeleg_ = id; };
    euSaetze_.onAuswahlGeaendert = [this](int64_t id) { gewaehlterEuSatz_ = id; };
    belege_.onZeileAktiviert   = [this](int64_t id) {
        gewaehlterBeleg_ = id;
        // A draft opens in the form; a posted document only reports itself,
        // because it cannot be changed any more.
        GewaehltenBelegBearbeiten();
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
    EuSaetzeFuellen();
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

void FibuApp::EuSaetzeFuellen() {
    euSaetze_.SetSpalten({
        ListColumnDef("Land", 70, TextAlignment::Left),
        ListColumnDef("Art", 110, TextAlignment::Left),
        ListColumnDef("Satz", 90, TextAlignment::Right),
        ListColumnDef("gilt ab", 110, TextAlignment::Left),
        ListColumnDef("gilt bis", 110, TextAlignment::Left),
        ListColumnDef("Prüfung", 110, TextAlignment::Left),
        ListColumnDef("Quelle", 380, TextAlignment::Left),
    });

    geladeneEuSaetze_ = store_.EuSteuersaetzeAlle();
    std::vector<TabellenZeile> zeilen;
    zeilen.reserve(geladeneEuSaetze_.size());
    for (const EuSteuersatz& satz : geladeneEuSaetze_) {
        char prozent[16];
        std::snprintf(prozent, sizeof(prozent), "%d,%d %%",
                      satz.satzPromille / 10, satz.satzPromille % 10);
        TabellenZeile zeile;
        zeile.id = satz.id;
        zeile.zellen = {
            TabellenZelle(satz.land),
            TabellenZelle(satz.art),
            // Sorted by the rate itself, not by its text: "8,1 %" before
            // "20,0 %" is the ordering a string comparison would give.
            TabellenZelle(prozent, satz.satzPromille),
            DatumsZelle(satz.gueltigVon),
            satz.gueltigBis.Valid() ? DatumsZelle(satz.gueltigBis)
                                    : TabellenZelle(std::string("offen")),
            // The word that decides whether the rate is used at all.
            TabellenZelle(satz.geprueft ? "geprüft" : "ungeprüft"),
            TabellenZelle(satz.quelle),
        };
        zeilen.push_back(std::move(zeile));
    }
    euSaetze_.SetZeilen(std::move(zeilen));
    euSaetze_.SetFussFunktion([this](const std::vector<int64_t>& ids) {
        int geprueft = 0;
        for (const int64_t id : ids)
            for (const EuSteuersatz& satz : geladeneEuSaetze_)
                if (satz.id == id && satz.geprueft) { ++geprueft; break; }
        std::string text = Zahl(static_cast<int64_t>(ids.size())) + " Steuersatz/Sätze";
        if (ids.size() != geladeneEuSaetze_.size())
            text += " von " + Zahl(static_cast<int64_t>(geladeneEuSaetze_.size())) +
                    " (gefiltert)";
        // Said on every refresh, because it is the thing that decides whether
        // the OSS return compares anything at all - and an unverified rate in
        // a table looks exactly like a verified one until it is spelled out.
        text += ", davon " + Zahl(geprueft) + " geprüft und damit im Vergleich "
                "der OSS-Meldung verwendet";
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
    text += ". Betrag und Konto fehlen noch - aus dem Beleg wird nichts ausgelesen.";
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

// ===== KONFIGURATION: EU-STEUERSAETZE =====

void FibuApp::KonfigurationOeffnen() {
    if (reiter_ && euSaetzeReiter_ >= 0) reiter_->SetActiveTab(euSaetzeReiter_);
    EuSaetzeFuellen();
    if (geladeneEuSaetze_.empty()) {
        Melden("Noch kein EU-Steuersatz erfasst - \"Mitgelieferte Sätze übernehmen\" "
               "holt die Normalsätze der Mitgliedstaaten herein.");
    } else {
        Melden(Zahl(static_cast<int64_t>(geladeneEuSaetze_.size())) +
               " Steuersätze. Ein geänderter Satz wird mit \"Neuen Steuersatz setzen\" "
               "ab seinem Datum erfasst - der bisherige bleibt erhalten.");
    }
}

void FibuApp::EuSatzDialogOeffnen() {
    DialogConfig config;
    config.title  = "Neuen Steuersatz setzen";
    config.width  = 520;
    config.height = 400;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    if (!dialog) { Melden("Der Dialog konnte nicht geöffnet werden."); return; }

    dialog->SetDialogType(DialogType::Question);
    dialog->SetIconVisible(false);
    dialog->SetMessage("Ab welchem Tag gilt welcher Satz?");
    // Spelled out in the dialog, because it is the rule that makes the date
    // field matter and the one a user would otherwise work around by editing
    // the existing row - which this deliberately does not offer.
    dialog->SetDetails("Ein geänderter Satz wird **zusätzlich** erfasst, nicht "
                       "überschrieben: der bisherige gilt bis zum Vortag weiter, "
                       "damit eine bereits abgegebene Meldung unverändert "
                       "nachrechenbar bleibt.");
    // Custom buttons rather than OKCancel: the framework's stock labels are
    // English, and the rest of this window is not. They still carry the OK and
    // Cancel results, so the handler below is unchanged.
    dialog->SetDialogButtons(DialogButtons::NoButtons);
    dialog->AddCustomButton("Steuersatz setzen", DialogResult::OK);
    dialog->AddCustomButton("Abbrechen", DialogResult::Cancel);

    auto landFeld = CreateTextInput("euLand", 0, 0, 480, 28);
    landFeld->SetPlaceholder("Land, z. B. AT");
    dialog->AddDialogElement(landFeld);

    auto satzFeld = CreateTextInput("euSatz", 0, 0, 480, 28);
    satzFeld->SetPlaceholder("Steuersatz in Prozent, z. B. 20 oder 8,1");
    dialog->AddDialogElement(satzFeld);

    auto abFeld = CreateTextInput("euAb", 0, 0, 480, 28);
    abFeld->SetPlaceholder("gilt ab (TT.MM.JJJJ)");
    dialog->AddDialogElement(abFeld);

    auto quelleFeld = CreateTextInput("euQuelle", 0, 0, 480, 28);
    quelleFeld->SetPlaceholder("Quelle - nur mit Quelle wird der Satz verglichen");
    dialog->AddDialogElement(quelleFeld);

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [this, landFeld, satzFeld, abFeld, quelleFeld](DialogResult ergebnis) {
            if (ergebnis != DialogResult::OK) return;
            const std::string quelle = quelleFeld->GetText();
            // **A source is what makes a rate usable.** An unverified rate is
            // stored but never compared against, so requiring the source here
            // is the difference between entering a rate and entering a note -
            // and the user finds that out now rather than at the next return.
            EuSatzAnlegen(landFeld->GetText(), satzFeld->GetText(), abFeld->GetText(),
                          !quelle.empty(), quelle);
        },
        fenster_.get());
}

void FibuApp::EuSatzAnlegen(const std::string& land, const std::string& satzText,
                            const std::string& abDatum, bool geprueft,
                            const std::string& quelle) {
    EuSteuersatz satz;
    satz.land     = land;
    satz.art      = "standard";
    satz.quelle   = quelle;
    satz.geprueft = geprueft;

    if (!ProzentNachPromille(satzText, satz.satzPromille)) {
        Melden("\"" + satzText + "\" ist kein Steuersatz zwischen 0 und 100 "
               "(höchstens eine Nachkommastelle).");
        return;
    }
    // German input first, ISO second: a bookkeeper types 01.01.2027, and a
    // pasted 2027-01-01 should not be rejected for it.
    if (!TryParseDateGerman(abDatum, satz.gueltigVon) &&
        !Date::TryParseIso(abDatum, satz.gueltigVon)) {
        Melden("\"" + abDatum + "\" ist kein Datum (TT.MM.JJJJ).");
        return;
    }

    const StoreResult r = store_.EuSteuersatzSetzen(satz, akteur_);
    if (!r) { Melden(r.fehler); return; }

    EuSaetzeFuellen();
    char prozent[16];
    std::snprintf(prozent, sizeof(prozent), "%d,%d %%",
                  satz.satzPromille / 10, satz.satzPromille % 10);
    std::string text = satz.land + ": " + prozent + " ab " +
                       FormatDateGerman(satz.gueltigVon) + " erfasst.";
    if (!geprueft)
        text += " Ohne Quelle gilt der Satz als ungeprüft und wird NICHT zum "
                "Vergleich herangezogen.";
    Melden(text);
}

void FibuApp::GewaehltenEuSatzLoeschen() {
    const int64_t id = euSaetze_.AusgewaehlteId();
    if (id == 0) { Melden("Kein Steuersatz ausgewählt."); return; }

    const EuSteuersatz* satz = nullptr;
    for (const EuSteuersatz& kandidat : geladeneEuSaetze_)
        if (kandidat.id == id) { satz = &kandidat; break; }
    if (satz == nullptr) { Melden("Diesen Steuersatz gibt es nicht mehr."); return; }

    // Asked rather than done, because deleting the wrong row here silently
    // changes what the next OSS return compares against.
    const std::string frage =
        "Steuersatz " + satz->land + " (" + satz->art + ") ab " +
        FormatDateGerman(satz->gueltigVon) + " löschen?\n\n"
        "Ein geänderter Satz wird nicht gelöscht, sondern ab seinem Datum neu "
        "erfasst. Löschen ist für einen Satz, der so nie gegolten hat.";
    UltraCanvasDialogManager::ShowConfirmation(
        frage, "Steuersatz löschen",
        [this, id](bool bestaetigt) {
            if (!bestaetigt) return;
            const StoreResult r = store_.EuSteuersatzLoeschen(id, akteur_);
            if (!r) { Melden(r.fehler); return; }
            EuSaetzeFuellen();
            Melden("Steuersatz gelöscht. Ein Satz, den er abgelöst hatte, gilt "
                   "wieder unbefristet.");
        },
        fenster_.get());
}

void FibuApp::EuSaetzeUebernehmen() {
    const std::string pfad = EuSteuersaetzePfad();
    if (pfad.empty()) {
        Melden("EU-Steuersaetze.csv wurde nicht gefunden.");
        return;
    }
    int neu = 0, bekannt = 0;
    const StoreResult r = store_.EuSteuersaetzeAusDatei(pfad, akteur_, neu, bekannt);
    if (!r) { Melden(r.fehler); return; }
    EuSaetzeFuellen();
    std::string text = Zahl(neu) + " Sätze übernommen, " + Zahl(bekannt) +
                       " waren bereits erfasst und bleiben unverändert.";
    if (neu > 0)
        text += " Keiner davon ist geprüft: sie werden erst verglichen, wenn sie "
                "gegen eine amtliche Quelle bestätigt und mit Quelle neu erfasst "
                "wurden.";
    Melden(text);
}

// ===== BELEGE ERFASSEN =====

void FibuApp::BelegFormularOeffnen(BelegArt art) {
    if (!formular_) return;
    formular_->Neu(art);
    if (reiter_ && formularReiter_ >= 0) reiter_->SetActiveTab(formularReiter_);
    Melden(BelegArtLabel(art) + ": zuerst den Partner wählen - er entscheidet, "
           "welche Steuerschlüssel je Position in Frage kommen.");
}

void FibuApp::GewaehltenBelegBearbeiten() {
    if (!formular_ || gewaehlterBeleg_ == 0) return;
    Beleg beleg;
    if (!store_.BelegById(gewaehlterBeleg_, beleg)) return;
    if (beleg.status != BelegStatus::Entwurf) {
        Melden(BelegArtLabel(beleg.art) + " " + beleg.nummer + " ist " +
               BelegStatusLabel(beleg.status) + " und nicht mehr änderbar - "
               "eine Änderung ist eine Stornierung.");
        return;
    }
    if (!formular_->Laden(gewaehlterBeleg_)) return;
    if (reiter_ && formularReiter_ >= 0) reiter_->SetActiveTab(formularReiter_);
    Melden(BelegArtLabel(beleg.art) + " " + beleg.nummer + " im Formular geöffnet.");
}

} // namespace UltraFIBU
