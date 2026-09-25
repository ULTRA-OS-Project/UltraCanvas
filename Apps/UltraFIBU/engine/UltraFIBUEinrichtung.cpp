// Apps/UltraFIBU/engine/UltraFIBUEinrichtung.cpp
// Setting up a new bookkeeping file. See the header for why it is all or
// nothing, and why it never writes over a file that holds a bookkeeping.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUEinrichtung.h"

#include "UltraFIBUKontenrahmen.h"
#include "UltraFIBUStore.h"

#include <cstdio>
#include <string>

#if defined(_WIN32)
  #include <windows.h>
#endif

namespace UltraFIBU {

namespace {

// Remove a file and anything SQLite may have left beside it. The journal is
// normally gone after the last commit, but a setup that failed mid-transaction
// can leave one, and a stray journal next to a new file of the same name is
// replayed into it on the next open.
void EntferneMitNebendateien(const std::string& pfad) {
    std::remove(pfad.c_str());
    std::remove((pfad + "-journal").c_str());
    std::remove((pfad + "-wal").c_str());
    std::remove((pfad + "-shm").c_str());
}

// Put `von` where `nach` is, replacing it. std::rename does not replace an
// existing file on Windows, and removing first would leave a moment with
// neither file - so Windows gets the call that does both at once.
bool Ersetze(const std::string& von, const std::string& nach) {
#if defined(_WIN32)
    return ::MoveFileExA(von.c_str(), nach.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(von.c_str(), nach.c_str()) == 0;
#endif
}

// Each run uses its own connection name, so checking a target and building the
// temporary file never share - or replace - one another's pooled connection.
std::string Verbindungsname(const char* zweck) {
    static int zaehler = 0;
    return std::string("ultrafibu-einrichtung-") + zweck + "-" + std::to_string(++zaehler);
}

} // namespace

std::string PruefeEinrichtung(const EinrichtungsDaten& daten) {
    if (daten.firma.empty())
        return "Der Firmenname fehlt.";
    if (!daten.gjBeginn.Valid())
        return "Der Beginn des Geschäftsjahres fehlt oder ist kein Datum.";
    // A fiscal year may start on any first of a month; the periods are
    // calendar months counted from there, so anything else has nowhere to go.
    if (daten.gjBeginn.day != 1)
        return "Ein Geschäftsjahr beginnt an einem Monatsersten - " +
               FormatDateGerman(daten.gjBeginn) + " ist keiner.";
    if (daten.skr != "SKR03" && daten.skr != "SKR04")
        return "Der Kontenrahmen muss SKR03 oder SKR04 sein, nicht \"" +
               daten.skr + "\".";
    if (daten.benutzer.empty())
        return "Der Anmeldename des ersten Benutzers fehlt.";
    return std::string();
}

bool EnthaeltBuchhaltung(const std::string& pfad) {
    if (!DateiExistiert(pfad)) return false;
    // Opening migrates the schema, exactly as the application does when it
    // opens the file - so this changes nothing a normal start would not. A file
    // that cannot be opened at all, or is newer than this program, counts as
    // holding one: the safe answer for a question that decides whether a file
    // may be replaced.
    Store store;
    if (!store.Open(Verbindungsname("pruefen"), pfad)) return true;
    const bool hat = !store.Mandanten().empty();
    store.Close();
    return hat;
}

EinrichtungsBericht RichteBuchhaltungEin(const std::string& ziel,
                                         const EinrichtungsDaten& daten) {
    EinrichtungsBericht bericht;

    if (ziel.empty() || ziel == ":memory:") {
        bericht.fehler = "Es ist keine Datei angegeben.";
        return bericht;
    }
    bericht.fehler = PruefeEinrichtung(daten);
    if (!bericht.fehler.empty()) return bericht;

    if (EnthaeltBuchhaltung(ziel)) {
        bericht.fehler = "In \"" + ziel + "\" ist bereits eine Buchhaltung angelegt. "
                         "Sie wird nicht überschrieben.";
        return bericht;
    }

    // The chart of accounts is checked before anything is written. A missing
    // one used to be a "note" after the company had already been created -
    // which produced a company with no accounts at all.
    const std::string skrPfad = FindeDatenDatei(daten.skr + ".csv");
    if (skrPfad.empty()) {
        bericht.fehler = "Der Kontenrahmen " + daten.skr + ".csv wurde nicht "
                         "gefunden. Ohne ihn lässt sich nicht buchen; das "
                         "Datenverzeichnis liegt neben dem Programm unter data/.";
        return bericht;
    }
    const std::string steuerPfad = FindeDatenDatei("Steuerschluessel.csv");
    if (steuerPfad.empty()) {
        bericht.fehler = "Steuerschluessel.csv wurde nicht gefunden.";
        return bericht;
    }

    // Built beside the target and moved into place at the end, so the target
    // path only ever holds a complete bookkeeping or whatever it held before.
    const std::string temp = ziel + ".einrichtung";
    EntferneMitNebendateien(temp);

    auto scheitern = [&](Store& store, const std::string& fehler) {
        store.Close();
        EntferneMitNebendateien(temp);
        bericht.ok = false;
        bericht.fehler = fehler;
        return bericht;
    };

    Store store;
    const StoreResult geoeffnet = store.Open(Verbindungsname("anlegen"), temp, true);
    if (!geoeffnet) {
        EntferneMitNebendateien(temp);
        bericht.fehler = "Die Datei konnte nicht angelegt werden: " + geoeffnet.fehler;
        return bericht;
    }

    // The first user becomes the administrator: there is nobody to authorise
    // them, which is the one case the store allows.
    Benutzer admin;
    admin.anmeldename = daten.benutzer;
    admin.anzeigename = daten.benutzer;
    Akteur niemand;
    StoreResult r = store.SaveBenutzer(admin, niemand);
    if (!r) return scheitern(store, r.fehler);
    Akteur akteur;
    akteur.benutzerId  = admin.id;
    akteur.anmeldename = admin.anmeldename;
    akteur.rolle       = admin.rolle;

    Mandant mandant = daten.stammdaten;
    mandant.id   = 0;
    mandant.name = daten.firma;
    if (mandant.land.empty()) mandant.land = "DE";
    r = store.SaveMandant(mandant, akteur);
    if (!r) return scheitern(store, r.fehler);

    Geschaeftsjahr jahr   = MakeGeschaeftsjahr(daten.gjBeginn);
    jahr.mandantId        = mandant.id;
    jahr.skr              = daten.skr;
    jahr.sachkontenlaenge = 4;
    r = store.SaveGeschaeftsjahr(jahr, akteur);
    if (!r) return scheitern(store, r.fehler);

    // The number ranges a bookkeeping file cannot work without. Their results
    // used to be discarded; a range that failed to save then surfaced only
    // when the first invoice could not be numbered.
    struct Kreis { const char* name; const char* praefix; int stellen; };
    static const Kreis kKreise[] = {
        { "rechnung", "R-{JJJJ}{MM}", 3 },
        { "beleg",    "B-",           5 },
        // Incoming documents get their own range: a supplier's invoice carries
        // the supplier's number, this is our internal one, and mixing it with
        // the outgoing numbers would make both meaningless.
        { "eingang",  "E-{JJJJ}",     5 },
    };
    for (const Kreis& k : kKreise) {
        Nummernkreis kreis;
        kreis.mandantId = mandant.id;
        kreis.kreis     = k.name;
        kreis.praefix   = k.praefix;
        kreis.stellen   = k.stellen;
        r = store.SaveNummernkreis(kreis, akteur);
        if (!r) return scheitern(store, "Nummernkreis \"" + std::string(k.name) +
                                            "\": " + r.fehler);
    }

    std::vector<Konto> konten;
    const LadeErgebnis kontenGeladen = LadeKontenrahmen(skrPfad, daten.skr, konten);
    for (const std::string& w : kontenGeladen.warnungen) bericht.warnungen.push_back(w);
    if (!kontenGeladen.ok)
        return scheitern(store, "Kontenrahmen: " + kontenGeladen.fehler);
    r = store.ImportKonten(mandant.id, konten, akteur, bericht.konten);
    if (!r) return scheitern(store, "Kontenrahmen: " + r.fehler);

    std::vector<Steuerschluessel> schluessel;
    const LadeErgebnis steuerGeladen = LadeSteuerschluesselDatei(steuerPfad, schluessel);
    for (const std::string& w : steuerGeladen.warnungen) bericht.warnungen.push_back(w);
    if (!steuerGeladen.ok)
        return scheitern(store, "Steuerschlüssel: " + steuerGeladen.fehler);
    r = store.ImportSteuerschluessel(mandant.id, schluessel, akteur, bericht.steuerschluessel);
    if (!r) return scheitern(store, "Steuerschlüssel: " + r.fehler);

    store.Close();

    // A journal or write-ahead log still beside the file after the last commit
    // means data that is not yet in the file itself. Moving the file without it
    // would separate the bookkeeping from its own contents, so this refuses
    // loudly rather than move half of it.
    if (DateiExistiert(temp + "-wal") || DateiExistiert(temp + "-journal")) {
        EntferneMitNebendateien(temp);
        bericht.fehler = "Die neue Datei war nach dem Anlegen nicht vollständig "
                         "geschrieben. Es wurde nichts übernommen.";
        return bericht;
    }

    if (!Ersetze(temp, ziel)) {
        EntferneMitNebendateien(temp);
        bericht.fehler = "Die fertige Buchhaltung konnte nicht nach \"" + ziel +
                         "\" verschoben werden - ist die Datei noch geöffnet?";
        return bericht;
    }

    bericht.ok      = true;
    bericht.mandant = mandant;
    bericht.jahr    = jahr;
    bericht.admin   = admin;
    return bericht;
}

} // namespace UltraFIBU
