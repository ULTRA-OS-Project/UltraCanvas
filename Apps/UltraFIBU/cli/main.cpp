// Apps/UltraFIBU/cli/main.cpp
// ultrafibu - the command-line front end to the UltraFIBU engine.
//
// Everything the engine can do today, without waiting for the UI: set up a
// company with a fiscal year that starts on any date, import a chart of
// accounts and a set of tax keys, keep customers and suppliers with their
// European VAT numbers, check a VAT number, list the filing deadlines, write
// and post documents, reverse them, record payments, read the journal and the
// Summen- und Saldenliste, freeze a period and verify the journal's hash
// chain.
//
// The output is German, like the application it belongs to. It is also the
// engine's first real caller, which is what keeps the engine honest about
// being usable without a window.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUBeleg.h"
#include "UltraFIBUDatev.h"
#include "UltraFIBURechnungPdf.h"
#include "UltraFIBUBuchung.h"
#include "UltraFIBUGeschaeftsjahr.h"
#include "UltraFIBUKontenrahmen.h"
#include "UltraFIBUStore.h"
#include "UltraFIBUTypes.h"
#include "UltraFIBUUstIdNr.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef ULTRAFIBU_CLI_VERSION
#define ULTRAFIBU_CLI_VERSION "0.0.0"
#endif

using namespace UltraFIBU;

namespace {

void PrintUsage() {
    std::printf(
        "ultrafibu - Buchhaltung (UltraFIBU %s)\n"
        "\n"
        "Aufruf:  ultrafibu <Befehl> [Optionen]\n"
        "\n"
        "Befehle:\n"
        "  einrichten <datei>      Neue Buchhaltung anlegen\n"
        "        --firma <name>        Firmenname (Pflicht)\n"
        "        --gj-beginn <datum>   Beginn des Geschäftsjahres, z. B. 01.04.2026\n"
        "        --benutzer <name>     Anmeldename des ersten Benutzers (Standard: admin)\n"
        "        --skr <SKR03|SKR04>   Kontenrahmen (Standard: SKR03)\n"
        "        --ust-idnr <nr>       Eigene USt-IdNr.\n"
        "        --strasse <s> --plz <plz> --ort <ort> --land <ISO>\n"
        "        --steuernummer <nr>   Steuernummer des Finanzamts\n"
        "        --telefon <nr> --email <adr> --web <url>\n"
        "        --iban <iban> --bic <bic> --bank <name>\n"
        "        --beraternummer <nr> --mandantennummer <nr>\n"
        "                              von der Kanzlei; ohne sie lehnt\n"
        "                              DATEV den Import ab\n"
        "                              Anschrift und Steuernummer sind\n"
        "                              Pflichtangaben auf jeder Rechnung\n"
        "                              (§ 14 UStG).\n"
        "  info <datei>            Mandant, Geschäftsjahre und Bestände anzeigen\n"
        "  konten <datei>          Kontenrahmen anzeigen\n"
        "  partner <datei> [suche] Kunden und Lieferanten anzeigen\n"
        "  partner-neu <datei>     Kunden oder Lieferanten anlegen\n"
        "        --name <name>         (Pflicht)\n"
        "        --typ <kunde|lieferant|beides>\n"
        "        --ort <ort>  --land <ISO>  --ust-idnr <nr>\n"
        "        --kategorie <inland|eu-unternehmer|eu-privat|drittland>\n"
        "  ustid <nummer>          USt-IdNr. offline prüfen (Format und Prüfziffer)\n"
        "  termine <datei> <jahr>  Abgabetermine der Umsatzsteuer-Voranmeldungen\n"
        "  perioden <datei>        Perioden der Geschäftsjahre\n"
        "  festschreiben <datei> <datum> [--ja]\n"
        "                          Buchungen bis zu diesem Tag unveränderbar machen\n"
        "  protokoll <datei>       Änderungsprotokoll (GoBD) anzeigen\n"
        "\n"
        "Belege und Buchungen:\n"
        "  beleg-neu <datei>       Beleg erfassen (Entwurf)\n"
        "        --art <ausgangsrechnung|eingangsrechnung|\n"
        "               ausgangsgutschrift|eingangsgutschrift|kassenbeleg>\n"
        "        --datum <datum>       Belegdatum (Pflicht)\n"
        "        --partner <konto|name>  Kunde oder Lieferant (Pflicht)\n"
        "        --position \"Text;Menge;Preis;Konto;Steuerschlüssel\"\n"
        "                              mehrfach angebbar (Pflicht)\n"
        "        --extern <nr> --text <text> --faellig <datum> --kreis <name>\n"
        "  belege <datei> [suche]  Belege anzeigen\n"
        "        --offen  --ueberfaellig --stichtag <datum>  --art <art>\n"
        "  buchen <datei> <nummer> Entwurf buchen - danach unveränderbar\n"
        "  storno <datei> <nummer> Gebuchten Beleg stornieren\n"
        "        --datum <datum>       Datum der Stornobuchung (Pflicht)\n"
        "        --grund <text>  --kreis <name>  --ja\n"
        "  zahlung <datei> <nummer>  Zahlung auf einen Beleg buchen\n"
        "        --betrag <betrag>  --datum <datum>  [--konto <geldkonto>]\n"
        "  journal <datei>         Buchungsjournal  [--von] [--bis]\n"
        "  salden <datei>          Summen- und Saldenliste  [--von] [--bis]\n"
        "  pruefen <datei>         Prüfsummenkette des Journals prüfen\n"
        "  rechnung-pdf <datei> <nummer>  Beleg als PDF drucken\n"
        "        --datei <pfad>        Zieldatei (Standard: <Nummer>.pdf)\n"
        "        --zahlungshinweis <text>  --fusszeile <text>\n"
        "\n"
        "DATEV:\n"
        "  datev-export <datei>    Buchungsstapel im DATEV-Format schreiben\n"
        "        --monat <JJJJ-MM>     genau ein Kalendermonat (Pflicht)\n"
        "        --konten              statt dessen die Kontenbeschriftungen\n"
        "        --ziel <verzeichnis>  Zielverzeichnis (Standard: .)\n"
        "  datev-pruefen <EXTF.csv>  Spaltendefinition gegen eine echte\n"
        "                          DATEV-Datei prüfen\n"
        "  datev-import <datei> <EXTF.csv>\n"
        "                          Buchungsstapel einlesen; zeigt nur an,\n"
        "                          bis --uebernehmen angegeben wird\n"
        "        --uebernehmen         Buchungen wirklich schreiben\n"
        "        --nochmal             eine bereits importierte Datei\n"
        "                              erneut zulassen\n"
        "  datev-importe <datei>   Bisherige DATEV-Importe anzeigen\n"
        "\n"
        "Datumsangaben in deutscher (01.04.2026) oder ISO-Schreibweise (2026-04-01).\n",
        ULTRAFIBU_CLI_VERSION);
}

// A --option's value, or the fallback.
std::string Option(int argc, char** argv, const std::string& name,
                   const std::string& fallback = std::string()) {
    for (int i = 1; i < argc - 1; ++i)
        if (name == argv[i]) return argv[i + 1];
    return fallback;
}

bool HasOption(int argc, char** argv, const std::string& name) {
    for (int i = 1; i < argc; ++i) if (name == argv[i]) return true;
    return false;
}

// The n-th positional argument after the command, skipping --options and their
// values.
std::string Positional(int argc, char** argv, int index) {
    int seen = 0;
    for (int i = 2; i < argc; ++i) {
        if (std::strncmp(argv[i], "--", 2) == 0) { ++i; continue; }
        if (seen == index) return argv[i];
        ++seen;
    }
    return std::string();
}

bool OpenStore(Store& store, const std::string& datei) {
    if (datei.empty()) {
        std::printf("Fehler: Keine Datei angegeben.\n");
        return false;
    }
    const StoreResult opened = store.Open("ultrafibu", datei);
    if (!opened) {
        std::printf("Fehler: %s\n", opened.fehler.c_str());
        return false;
    }
    return true;
}

// The one company in the file. The schema carries mandant_id everywhere, so
// several are possible; the CLI works on the first rather than asking each time.
bool ErsterMandant(const Store& store, Mandant& out) {
    const std::vector<Mandant> alle = store.Mandanten();
    if (alle.empty()) {
        std::printf("In dieser Datei ist noch kein Mandant angelegt "
                    "(ultrafibu einrichten ...).\n");
        return false;
    }
    out = alle[0];
    return true;
}

// The CLI acts as the first administrator it finds, and says so in the audit
// trail. A real login belongs to the UI and to server mode.
Akteur AkteurFor(const Store& store) {
    Akteur akteur;
    for (const Benutzer& user : store.BenutzerListe()) {
        if (user.rolle == BenutzerRolle::Administrator && user.aktiv) {
            akteur.benutzerId  = user.id;
            akteur.anmeldename = user.anmeldename + " (cli)";
            akteur.rolle       = user.rolle;
            return akteur;
        }
    }
    akteur.anmeldename = "cli";
    akteur.rolle       = BenutzerRolle::Administrator;
    return akteur;
}

int Einrichten(int argc, char** argv) {
    const std::string datei = Positional(argc, argv, 0);
    const std::string firma = Option(argc, argv, "--firma");
    if (datei.empty() || firma.empty()) {
        std::printf("Fehler: Datei und --firma sind erforderlich.\n");
        return 2;
    }

    const std::string beginnText = Option(argc, argv, "--gj-beginn");
    if (beginnText.empty()) {
        std::printf("Fehler: --gj-beginn fehlt. Das Geschäftsjahr kann an jedem\n"
                    "Monatsersten beginnen, z. B. --gj-beginn 01.04.2026\n");
        return 2;
    }
    Date beginn;
    if (!TryParseDateGerman(beginnText, beginn)) {
        std::printf("Fehler: \"%s\" ist kein Datum.\n", beginnText.c_str());
        return 2;
    }

    Store store;
    if (!OpenStore(store, datei)) return 1;
    if (!store.Mandanten().empty()) {
        std::printf("Fehler: In \"%s\" ist bereits ein Mandant angelegt.\n", datei.c_str());
        return 1;
    }

    // The first user becomes the administrator: there is nobody to authorise
    // them, which is the one case the store allows.
    Benutzer admin;
    admin.anmeldename = Option(argc, argv, "--benutzer", "admin");
    admin.anzeigename = admin.anmeldename;
    Akteur niemand;
    const StoreResult userSaved = store.SaveBenutzer(admin, niemand);
    if (!userSaved) { std::printf("Fehler: %s\n", userSaved.fehler.c_str()); return 1; }
    Akteur akteur;
    akteur.benutzerId  = admin.id;
    akteur.anmeldename = admin.anmeldename;
    akteur.rolle       = admin.rolle;

    Mandant mandant;
    mandant.name    = firma;
    mandant.ustIdNr = Option(argc, argv, "--ust-idnr");
    mandant.ort     = Option(argc, argv, "--ort");
    mandant.land    = Option(argc, argv, "--land", "DE");
    // The company's own address and tax number are not decoration: without
    // them a printed invoice is deficient under § 14 UStG and its recipient
    // cannot deduct the input tax. They are settable here so a file can be set
    // up complete in one go; "ultrafibu rechnung-pdf" names whatever is still
    // missing.
    mandant.strasse      = Option(argc, argv, "--strasse");
    mandant.plz          = Option(argc, argv, "--plz");
    mandant.steuernummer = Option(argc, argv, "--steuernummer");
    mandant.telefon      = Option(argc, argv, "--telefon");
    mandant.email        = Option(argc, argv, "--email");
    mandant.webseite     = Option(argc, argv, "--web");
    mandant.iban         = Option(argc, argv, "--iban");
    mandant.bic          = Option(argc, argv, "--bic");
    mandant.bank         = Option(argc, argv, "--bank");
    mandant.rechtsform   = Option(argc, argv, "--rechtsform");
    // Assigned by the Kanzlei. Without them a DATEV import is refused
    // there, so they belong in the same setup step as everything else
    // that has to be right before the first export.
    mandant.beraternummer   = Option(argc, argv, "--beraternummer");
    mandant.mandantennummer = Option(argc, argv, "--mandantennummer");
    const StoreResult mandantSaved = store.SaveMandant(mandant, akteur);
    if (!mandantSaved) { std::printf("Fehler: %s\n", mandantSaved.fehler.c_str()); return 1; }

    Geschaeftsjahr jahr = MakeGeschaeftsjahr(beginn);
    jahr.mandantId        = mandant.id;
    jahr.skr              = Option(argc, argv, "--skr", "SKR03");
    jahr.sachkontenlaenge = 4;
    const StoreResult jahrSaved = store.SaveGeschaeftsjahr(jahr, akteur);
    if (!jahrSaved) { std::printf("Fehler: %s\n", jahrSaved.fehler.c_str()); return 1; }

    // The number ranges a bookkeeping file cannot work without. Allocated from
    // the database, so two writers can never receive the same invoice number.
    Nummernkreis rechnungen;
    rechnungen.mandantId = mandant.id;
    rechnungen.kreis     = "rechnung";
    rechnungen.praefix   = "R-{JJJJ}{MM}";
    rechnungen.stellen   = 3;
    store.SaveNummernkreis(rechnungen, akteur);
    Nummernkreis belege;
    belege.mandantId = mandant.id;
    belege.kreis     = "beleg";
    belege.praefix   = "B-";
    belege.stellen   = 5;
    store.SaveNummernkreis(belege, akteur);

    std::printf("Buchhaltung angelegt: %s\n", datei.c_str());
    std::printf("  Mandant          %s\n", mandant.name.c_str());
    std::printf("  Geschäftsjahr    %s (%s - %s, %d Perioden)\n",
                jahr.bezeichnung.c_str(), FormatDateGerman(jahr.beginn).c_str(),
                FormatDateGerman(jahr.ende).c_str(), jahr.PeriodCount());
    std::printf("  Benutzer         %s (%s)\n", admin.anmeldename.c_str(),
                BenutzerRolleLabel(admin.rolle).c_str());

    const std::string skrDatei = jahr.skr + ".csv";
    const std::string skrPfad  = FindeDatenDatei(skrDatei);
    if (skrPfad.empty()) {
        std::printf("  Hinweis: %s wurde nicht gefunden - der Kontenrahmen bleibt leer.\n"
                    "           ULTRAFIBU_DATA_DIR auf das Datenverzeichnis setzen.\n",
                    skrDatei.c_str());
    } else {
        std::vector<Konto> konten;
        const LadeErgebnis geladen = LadeKontenrahmen(skrPfad, jahr.skr, konten);
        for (const std::string& warnung : geladen.warnungen)
            std::printf("  Warnung: %s\n", warnung.c_str());
        if (!geladen.ok) {
            std::printf("  Fehler beim Kontenrahmen: %s\n", geladen.fehler.c_str());
        } else {
            int written = 0;
            const StoreResult imported = store.ImportKonten(mandant.id, konten, akteur, written);
            if (!imported) std::printf("  Fehler: %s\n", imported.fehler.c_str());
            else std::printf("  Kontenrahmen     %s, %d Konten\n", jahr.skr.c_str(), written);
        }
    }

    const std::string steuerPfad = FindeDatenDatei("Steuerschluessel.csv");
    if (!steuerPfad.empty()) {
        std::vector<Steuerschluessel> schluessel;
        const LadeErgebnis geladen = LadeSteuerschluesselDatei(steuerPfad, schluessel);
        for (const std::string& warnung : geladen.warnungen)
            std::printf("  Warnung: %s\n", warnung.c_str());
        if (geladen.ok) {
            int written = 0;
            const StoreResult imported =
                store.ImportSteuerschluessel(mandant.id, schluessel, akteur, written);
            if (!imported) std::printf("  Fehler: %s\n", imported.fehler.c_str());
            else std::printf("  Steuerschlüssel  %d\n", written);
        }
    }

    std::printf("\nNächster Schritt: ultrafibu partner-neu %s --name \"...\"\n", datei.c_str());
    return 0;
}

int Info(int argc, char** argv) {
    Store store;
    if (!OpenStore(store, Positional(argc, argv, 0))) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;

    std::printf("Mandant\n");
    std::printf("  Name             %s\n", mandant.name.c_str());
    if (!mandant.ort.empty())     std::printf("  Ort              %s\n", mandant.ort.c_str());
    if (!mandant.ustIdNr.empty()) std::printf("  USt-IdNr.        %s\n", mandant.ustIdNr.c_str());
    std::printf("  Besteuerung      %s\n",
                mandant.besteuerung == Besteuerungsart::Sollversteuerung
                    ? "Sollversteuerung (Steuer mit der Rechnung)"
                    : "Istversteuerung (Steuer mit der Zahlung)");
    std::printf("  Gewinnermittlung %s\n",
                mandant.gewinnermittlung == Gewinnermittlung::Bilanzierung
                    ? "Bilanzierung" : "Einnahmen-Überschuss-Rechnung");
    std::printf("  UStVA            %s%s\n",
                UstvaRhythmusToText(mandant.ustvaRhythmus).c_str(),
                mandant.dauerfristverlaengerung ? ", mit Dauerfristverlängerung" : "");

    std::printf("\nGeschäftsjahre\n");
    for (const Geschaeftsjahr& jahr : store.Geschaeftsjahre(mandant.id)) {
        std::printf("  %-10s %s - %s  %2d Perioden  %s",
                    jahr.bezeichnung.c_str(), FormatDateGerman(jahr.beginn).c_str(),
                    FormatDateGerman(jahr.ende).c_str(), jahr.PeriodCount(),
                    GeschaeftsjahrStatusToText(jahr.status).c_str());
        if (jahr.festschreibungBis.Valid())
            std::printf(" (festgeschrieben bis %s)",
                        FormatDateGerman(jahr.festschreibungBis).c_str());
        if (jahr.IsRumpfjahr()) std::printf("  [Rumpfjahr]");
        std::printf("\n");
    }

    std::printf("\nBestände\n");
    std::printf("  Konten           %zu\n", store.Konten(mandant.id).size());
    std::printf("  Steuerschlüssel  %zu\n", store.SteuerschluesselListe(mandant.id).size());
    std::printf("  Kunden           %zu\n",
                store.PartnerListe(mandant.id, PartnerTyp::Kunde).size());
    std::printf("  Lieferanten      %zu\n",
                store.PartnerListe(mandant.id, PartnerTyp::Lieferant).size());
    std::printf("  Benutzer         %zu\n", store.BenutzerListe().size());
    std::printf("  Schema-Version   %d\n", store.SchemaVersion());
    return 0;
}

int Konten(int argc, char** argv) {
    Store store;
    if (!OpenStore(store, Positional(argc, argv, 0))) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;

    const std::vector<Konto> konten = store.Konten(mandant.id);
    std::printf("%-8s %-52s %-13s %s\n", "Konto", "Bezeichnung", "Typ", "Steuer");
    for (const Konto& konto : konten)
        std::printf("%-8s %-52s %-13s %s\n", konto.nummer.c_str(), konto.bezeichnung.c_str(),
                    KontoTypToText(konto.typ).c_str(), konto.steuerschluessel.c_str());
    std::printf("\n%zu Konten (%s)\n", konten.size(),
                konten.empty() ? "-" : konten[0].skr.c_str());
    return 0;
}

int PartnerListeZeigen(int argc, char** argv) {
    Store store;
    if (!OpenStore(store, Positional(argc, argv, 0))) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;

    const std::string suche = Positional(argc, argv, 1);
    const std::vector<Partner> liste =
        store.PartnerListe(mandant.id, PartnerTyp::Beides, suche);
    std::printf("%-8s %-10s %-34s %-18s %-16s %s\n",
                "Konto", "Typ", "Name", "Ort", "USt-IdNr.", "Kategorie");
    for (const Partner& partner : liste)
        std::printf("%-8s %-10s %-34s %-18s %-16s %s\n",
                    partner.konto.c_str(), PartnerTypToText(partner.typ).c_str(),
                    partner.name.c_str(), partner.ort.c_str(), partner.ustIdNr.c_str(),
                    SteuerkategorieToText(partner.steuerkategorie).c_str());
    if (suche.empty()) std::printf("\n%zu Einträge\n", liste.size());
    else std::printf("\n%zu Einträge für \"%s\"\n", liste.size(), suche.c_str());
    return 0;
}

int PartnerNeu(int argc, char** argv) {
    const std::string name = Option(argc, argv, "--name");
    if (name.empty()) { std::printf("Fehler: --name fehlt.\n"); return 2; }

    Store store;
    if (!OpenStore(store, Positional(argc, argv, 0))) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;

    Partner partner;
    partner.mandantId = mandant.id;
    partner.name      = name;
    partner.ort       = Option(argc, argv, "--ort");
    partner.plz       = Option(argc, argv, "--plz");
    partner.strasse   = Option(argc, argv, "--strasse");
    partner.land      = Option(argc, argv, "--land", "DE");
    partner.ustIdNr   = Option(argc, argv, "--ust-idnr");
    partner.email     = Option(argc, argv, "--email");
    partner.telefon   = Option(argc, argv, "--telefon");
    if (!PartnerTypFromText(Option(argc, argv, "--typ", "kunde"), partner.typ)) {
        std::printf("Fehler: --typ muss kunde, lieferant oder beides sein.\n");
        return 2;
    }
    const std::string kategorie = Option(argc, argv, "--kategorie");
    if (!kategorie.empty() && !SteuerkategorieFromText(kategorie, partner.steuerkategorie)) {
        std::printf("Fehler: --kategorie muss inland, eu-unternehmer, eu-privat "
                    "oder drittland sein.\n");
        return 2;
    }
    if (kategorie.empty() && !partner.ustIdNr.empty()) {
        // A foreign VAT number with the domestic category is a contradiction
        // the store refuses, so the obvious category is filled in rather than
        // making the user guess which one the error meant.
        const UstIdNrPruefung pruefung = PruefeUstIdNr(partner.ustIdNr);
        if (!pruefung.land.empty() && pruefung.land != "DE")
            partner.steuerkategorie = Steuerkategorie::EuUnternehmer;
    }

    const StoreResult saved = store.SavePartner(partner, AkteurFor(store));
    if (!saved) { std::printf("Fehler: %s\n", saved.fehler.c_str()); return 1; }

    std::printf("Angelegt: %s %s (%s)\n", partner.konto.c_str(), partner.name.c_str(),
                PartnerTypToText(partner.typ).c_str());
    if (!partner.ustIdNr.empty())
        std::printf("  USt-IdNr. %s - %s\n", partner.ustIdNr.c_str(),
                    PruefeUstIdNr(partner.ustIdNr).hinweis.c_str());
    return 0;
}

int UstIdPruefen(int argc, char** argv) {
    const std::string nummer = Positional(argc, argv, 0);
    if (nummer.empty()) { std::printf("Fehler: Keine Nummer angegeben.\n"); return 2; }

    const UstIdNrPruefung pruefung = PruefeUstIdNr(nummer);
    std::printf("Eingabe      %s\n", nummer.c_str());
    std::printf("Normalisiert %s\n", pruefung.normalisiert.c_str());
    if (!pruefung.land.empty()) std::printf("Land         %s\n", pruefung.land.c_str());
    std::printf("Ergebnis     %s\n", UstIdNrStatusToText(pruefung.status).c_str());
    std::printf("%s\n", pruefung.hinweis.c_str());
    if (pruefung.Plausibel())
        std::printf("\nEU-weite Abfrage (VIES, ohne Registrierung):\n"
                    "  https://ec.europa.eu/taxation_customs/vies/#/vat-validation\n"
                    "Rechtssicher für innergemeinschaftliche Lieferungen ist nur die\n"
                    "qualifizierte Bestätigung des BZSt (§ 18e UStG).\n");
    return pruefung.Plausibel() ? 0 : 1;
}

int Termine(int argc, char** argv) {
    Store store;
    if (!OpenStore(store, Positional(argc, argv, 0))) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;

    const std::string jahrText = Positional(argc, argv, 1);
    int jahr = 0;
    for (char c : jahrText) if (c >= '0' && c <= '9') jahr = jahr * 10 + (c - '0');
    if (jahr < 1900) { std::printf("Fehler: Bitte ein Jahr angeben, z. B. 2026.\n"); return 2; }

    std::printf("Umsatzsteuer-Voranmeldung %d (%s%s)\n", jahr,
                UstvaRhythmusToText(mandant.ustvaRhythmus).c_str(),
                mandant.dauerfristverlaengerung ? ", mit Dauerfristverlängerung" : "");
    const int perioden = mandant.ustvaRhythmus == UstvaRhythmus::Monatlich ? 12
                       : mandant.ustvaRhythmus == UstvaRhythmus::Vierteljaehrlich ? 4 : 1;
    for (int periode = 1; periode <= perioden; ++periode) {
        Voranmeldungszeitraum zeitraum;
        zeitraum.year     = jahr;
        zeitraum.period   = periode;
        zeitraum.rhythmus = mandant.ustvaRhythmus;
        std::printf("  %-18s %s - %s   abzugeben bis %s   ELSTER-Zeitraum %s\n",
                    zeitraum.Bezeichnung().c_str(),
                    FormatDateGerman(zeitraum.Start()).c_str(),
                    FormatDateGerman(zeitraum.End()).c_str(),
                    FormatDateGerman(zeitraum.Deadline(mandant.dauerfristverlaengerung)).c_str(),
                    zeitraum.ElsterZeitraum().c_str());
    }
    if (mandant.ossRegistriert) {
        std::printf("\nOne-Stop-Shop %d\n", jahr);
        for (int quartal = 1; quartal <= 4; ++quartal) {
            OssQuartal oss;
            oss.year    = jahr;
            oss.quarter = quartal;
            std::printf("  %-18s %s - %s   abzugeben bis %s\n", oss.Bezeichnung().c_str(),
                        FormatDateGerman(oss.Start()).c_str(),
                        FormatDateGerman(oss.End()).c_str(),
                        FormatDateGerman(oss.Deadline()).c_str());
        }
    }
    std::printf("\nHinweis: Feiertage sind nicht berücksichtigt - sie unterscheiden sich je\n"
                "Bundesland. Samstage und Sonntage sind verschoben (§ 108 Abs. 3 AO).\n");
    return 0;
}

int Perioden(int argc, char** argv) {
    Store store;
    if (!OpenStore(store, Positional(argc, argv, 0))) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;

    const std::vector<Geschaeftsjahr> jahre = store.Geschaeftsjahre(mandant.id);
    if (jahre.empty()) { std::printf("Kein Geschäftsjahr angelegt.\n"); return 1; }

    for (const Geschaeftsjahr& jahr : jahre) {
        std::printf("Geschäftsjahr %s (%s - %s)\n", jahr.bezeichnung.c_str(),
                    FormatDateGerman(jahr.beginn).c_str(), FormatDateGerman(jahr.ende).c_str());
        for (int periode = 1; periode <= jahr.PeriodCount(); ++periode)
            std::printf("  Periode %2d  %-16s %s - %s%s\n", periode,
                        jahr.PeriodName(periode).c_str(),
                        FormatDateGerman(jahr.PeriodStart(periode)).c_str(),
                        FormatDateGerman(jahr.PeriodEnd(periode)).c_str(),
                        jahr.IsFrozen(jahr.PeriodEnd(periode)) ? "   festgeschrieben" : "");
        std::printf("\n");
    }
    return 0;
}

int Festschreiben(int argc, char** argv) {
    Store store;
    if (!OpenStore(store, Positional(argc, argv, 0))) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;

    const std::string bisText = Positional(argc, argv, 1);
    Date bis;
    if (!TryParseDateGerman(bisText, bis)) {
        std::printf("Fehler: \"%s\" ist kein Datum.\n", bisText.c_str());
        return 2;
    }
    Geschaeftsjahr jahr;
    if (!store.GeschaeftsjahrAt(mandant.id, bis, jahr)) {
        std::printf("Fehler: Der %s liegt in keinem angelegten Geschäftsjahr.\n",
                    FormatDateGerman(bis).c_str());
        return 1;
    }
    // Irreversible, so it is confirmed rather than just done.
    if (!HasOption(argc, argv, "--ja")) {
        std::printf("Festschreiben macht alle Buchungen bis zum %s unveränderbar.\n"
                    "Korrekturen sind danach nur über eine Stornierung möglich, und die\n"
                    "Festschreibung kann nicht zurückgenommen werden (GoBD).\n"
                    "\nZum Ausführen: ultrafibu festschreiben <datei> %s --ja\n",
                    FormatDateGerman(bis).c_str(), bisText.c_str());
        return 0;
    }
    const StoreResult frozen = store.Festschreiben(jahr.id, bis, AkteurFor(store));
    if (!frozen) { std::printf("Fehler: %s\n", frozen.fehler.c_str()); return 1; }
    std::printf("Festgeschrieben bis %s (Geschäftsjahr %s).\n",
                FormatDateGerman(bis).c_str(), jahr.bezeichnung.c_str());
    return 0;
}

int Protokoll(int argc, char** argv) {
    Store store;
    if (!OpenStore(store, Positional(argc, argv, 0))) return 1;

    const std::vector<Store::AuditEintrag> eintraege = store.AuditListe(100);
    std::printf("%-12s %-18s %-16s %-22s %s\n",
                "Zeit", "Benutzer", "Tabelle", "Aktion", "Details");
    for (const Store::AuditEintrag& eintrag : eintraege)
        std::printf("%-12lld %-18s %-16s %-22s %s\n",
                    static_cast<long long>(eintrag.zeit), eintrag.benutzer.c_str(),
                    eintrag.tabelle.c_str(), eintrag.aktion.c_str(), eintrag.details.c_str());
    std::printf("\n%zu Einträge (neueste zuerst)\n", eintraege.size());
    return 0;
}

// ===== BELEGE UND BUCHUNGEN =====

// A --position argument: "Bezeichnung;Menge;Einzelpreis;Konto;Steuerschluessel".
// Semicolon-separated because that is what the DATEV and Kontenrahmen files
// use and what a German keyboard reaches without a shift. The amount is read
// with the Money parser, so both 1234.56 and 1.234,56 are accepted.
bool ParsePosition(const std::string& text, BelegPosition& out, std::string& fehler) {
    std::vector<std::string> teile;
    std::string aktuell;
    for (char c : text) {
        if (c == ';') { teile.push_back(aktuell); aktuell.clear(); }
        else aktuell.push_back(c);
    }
    teile.push_back(aktuell);
    if (teile.size() < 5) {
        fehler = "Eine Position braucht fünf Felder: "
                 "Bezeichnung;Menge;Einzelpreis;Konto;Steuerschlüssel";
        return false;
    }

    out.bezeichnung = teile[0];

    // The quantity is held in thousandths. Reading it through Money and taking
    // its minor units would give hundredths, so it is read as a Money with two
    // decimals and scaled - which keeps 0,25 hours exact and never involves a
    // double.
    Money menge;
    if (!Money::TryParse(teile[1], menge) || menge.Minor() == 0) {
        fehler = "\"" + teile[1] + "\" ist keine Menge.";
        return false;
    }
    out.mengeTausendstel = menge.Minor() * 10;

    Money preis;
    if (!Money::TryParse(teile[2], preis)) {
        fehler = "\"" + teile[2] + "\" ist kein Betrag.";
        return false;
    }
    out.einzelpreis = preis;
    out.konto            = teile[3];
    out.steuerschluessel = teile[4];
    if (teile.size() > 5) out.kostenstelle = teile[5];
    return true;
}

// Find a partner by account number, by exact name, or by a unique substring of
// the name - so the command line can say --partner "Muster" instead of 10001.
bool FindePartner(const Store& store, int64_t mandantId, const std::string& suche,
                  Partner& out) {
    if (suche.empty()) return false;
    if (store.PartnerByKonto(mandantId, suche, out)) return true;

    const std::vector<Partner> treffer =
        store.PartnerListe(mandantId, PartnerTyp::Beides, suche);
    if (treffer.size() == 1) { out = treffer[0]; return true; }
    if (treffer.size() > 1) {
        for (const Partner& p : treffer) {
            if (p.name == suche) { out = p; return true; }
        }
        std::printf("Fehler: \"%s\" passt auf %d Partner. Bitte das Konto angeben:\n",
                    suche.c_str(), static_cast<int>(treffer.size()));
        for (const Partner& p : treffer)
            std::printf("  %-8s %s\n", p.konto.c_str(), p.name.c_str());
    }
    return false;
}

int BelegNeu(int argc, char** argv) {
    const std::string datei = Positional(argc, argv, 0);
    Store store;
    if (!OpenStore(store, datei)) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;
    const Akteur akteur = AkteurFor(store);

    Beleg beleg;
    beleg.mandantId = mandant.id;

    const std::string artText = Option(argc, argv, "--art", "ausgangsrechnung");
    if (!BelegArtFromText(artText, beleg.art)) {
        std::printf("Fehler: \"%s\" ist keine Belegart. Möglich sind: "
                    "ausgangsrechnung, eingangsrechnung, ausgangsgutschrift, "
                    "eingangsgutschrift, kassenbeleg, sonstiges.\n", artText.c_str());
        return 2;
    }

    const std::string datumText = Option(argc, argv, "--datum");
    if (datumText.empty() || !TryParseDateGerman(datumText, beleg.datum)) {
        std::printf("Fehler: --datum fehlt oder ist kein Datum.\n");
        return 2;
    }

    const std::string partnerSuche = Option(argc, argv, "--partner");
    Partner partner;
    if (!FindePartner(store, mandant.id, partnerSuche, partner)) {
        if (!partnerSuche.empty())
            std::printf("Fehler: Kein Partner gefunden für \"%s\".\n", partnerSuche.c_str());
        else
            std::printf("Fehler: --partner fehlt (Kontonummer oder Name).\n");
        return 2;
    }
    beleg.partnerId    = partner.id;
    beleg.partnerKonto = partner.konto;
    beleg.partnerName  = partner.name;

    beleg.externeNummer = Option(argc, argv, "--extern");
    beleg.buchungstext  = Option(argc, argv, "--text");
    const std::string faellig = Option(argc, argv, "--faellig");
    if (!faellig.empty()) TryParseDateGerman(faellig, beleg.faelligAm);

    // Every --position on the command line, in the order they were given.
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string("--position") != argv[i]) continue;
        BelegPosition pos;
        std::string fehler;
        if (!ParsePosition(argv[i + 1], pos, fehler)) {
            std::printf("Fehler: %s\n", fehler.c_str());
            return 2;
        }
        beleg.positionen.push_back(pos);
    }
    if (beleg.positionen.empty()) {
        std::printf("Fehler: Mindestens eine --position wird gebraucht:\n"
                    "  --position \"Beratung;10;100,00;8400;USt19\"\n");
        return 2;
    }

    const std::string kreis = Option(argc, argv, "--kreis", "rechnung");
    const StoreResult saved = store.SaveBeleg(beleg, kreis, akteur);
    if (!saved) { std::printf("Fehler: %s\n", saved.fehler.c_str()); return 1; }

    std::printf("Beleg %s angelegt (%s, %s).\n", beleg.nummer.c_str(),
                BelegArtLabel(beleg.art).c_str(), beleg.partnerName.c_str());
    for (const BelegPosition& pos : beleg.positionen)
        std::printf("  %-30s %12s netto  %s %s\n", pos.bezeichnung.c_str(),
                    pos.netto.ToString().c_str(), pos.konto.c_str(),
                    pos.steuerschluessel.c_str());
    std::printf("  %-30s %12s netto\n", "Summe", beleg.netto.ToString().c_str());
    std::printf("  %-30s %12s\n", "Umsatzsteuer", beleg.steuer.ToString().c_str());
    std::printf("  %-30s %12s brutto\n", "Gesamt", beleg.brutto.ToString().c_str());
    if (beleg.faelligAm.Valid())
        std::printf("  Fällig am %s\n", FormatDateGerman(beleg.faelligAm).c_str());
    std::printf("\nMit \"ultrafibu buchen %s %s\" wird daraus eine Buchung.\n",
                datei.c_str(), beleg.nummer.c_str());
    return 0;
}

int BelegeZeigen(int argc, char** argv) {
    const std::string datei = Positional(argc, argv, 0);
    Store store;
    if (!OpenStore(store, datei)) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;

    Store::BelegFilter filter;
    filter.mandantId = mandant.id;
    filter.suche     = Positional(argc, argv, 1);
    filter.nurOffene = HasOption(argc, argv, "--offen");
    if (HasOption(argc, argv, "--ueberfaellig")) {
        filter.nurUeberfaellig = true;
        const std::string heute = Option(argc, argv, "--stichtag");
        if (!heute.empty() && !TryParseDateGerman(heute, filter.heute)) {
            std::printf("Fehler: \"%s\" ist kein Datum.\n", heute.c_str());
            return 2;
        }
        if (!filter.heute.Valid()) {
            std::printf("Fehler: --ueberfaellig braucht --stichtag <datum>.\n"
                        "Ein Bericht, der von der Uhr abhängt, lässt sich nicht "
                        "wiederholen.\n");
            return 2;
        }
    }
    const std::string artText = Option(argc, argv, "--art");
    if (!artText.empty()) {
        if (!BelegArtFromText(artText, filter.art)) {
            std::printf("Fehler: \"%s\" ist keine Belegart.\n", artText.c_str());
            return 2;
        }
        filter.artGesetzt = true;
    }

    const std::vector<Beleg> belege = store.BelegListe(filter);
    if (belege.empty()) { std::printf("Keine Belege gefunden.\n"); return 0; }

    std::printf("%-14s %-10s %-24s %13s %13s  %s\n",
                "Nummer", "Datum", "Partner", "Brutto", "Offen", "Status");
    Money summeOffen = Money::Zero(mandant.waehrung);
    for (const Beleg& b : belege) {
        std::printf("%-14s %-10s %-24s %13s %13s  %s\n",
                    b.nummer.c_str(), FormatDateGerman(b.datum).c_str(),
                    b.partnerName.substr(0, 24).c_str(),
                    b.brutto.ToString().c_str(), b.Offen().ToString().c_str(),
                    BelegStatusLabel(b.status).c_str());
        if (b.Offen().Valid()) summeOffen = summeOffen + b.Offen();
    }
    std::printf("\n%d Beleg(e), offen %s\n", static_cast<int>(belege.size()),
                summeOffen.ToString().c_str());
    return 0;
}

int BelegBuchen(int argc, char** argv) {
    const std::string datei  = Positional(argc, argv, 0);
    const std::string nummer = Positional(argc, argv, 1);
    if (nummer.empty()) {
        std::printf("Fehler: Belegnummer fehlt.\n");
        return 2;
    }
    Store store;
    if (!OpenStore(store, datei)) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;
    const Akteur akteur = AkteurFor(store);

    Beleg beleg;
    if (!store.BelegByNummer(mandant.id, nummer, beleg)) {
        std::printf("Fehler: Beleg \"%s\" nicht gefunden.\n", nummer.c_str());
        return 1;
    }
    const StoreResult posted = store.Buchen(beleg, akteur);
    if (!posted) { std::printf("Fehler: %s\n", posted.fehler.c_str()); return 1; }

    std::printf("Beleg %s gebucht:\n", beleg.nummer.c_str());
    for (const Buchung& b : store.BuchungenZuBeleg(beleg.id)) {
        std::printf("  %-8s %s %12s an %-8s", b.konto.c_str(),
                    SollHabenToText(b.sollHaben).c_str(), b.umsatz.ToString().c_str(),
                    b.gegenkonto.c_str());
        if (!b.steuer.IsZero())
            std::printf("  (davon %s USt auf %s)", b.steuer.ToString().c_str(),
                        b.steuerkonto.c_str());
        std::printf("\n");
    }
    return 0;
}

int BelegStornieren(int argc, char** argv) {
    const std::string datei  = Positional(argc, argv, 0);
    const std::string nummer = Positional(argc, argv, 1);
    const std::string datumText = Option(argc, argv, "--datum");
    if (nummer.empty() || datumText.empty()) {
        std::printf("Fehler: Belegnummer und --datum <stornodatum> sind erforderlich.\n");
        return 2;
    }
    Date stornoDatum;
    if (!TryParseDateGerman(datumText, stornoDatum)) {
        std::printf("Fehler: \"%s\" ist kein Datum.\n", datumText.c_str());
        return 2;
    }

    Store store;
    if (!OpenStore(store, datei)) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;
    const Akteur akteur = AkteurFor(store);

    Beleg original;
    if (!store.BelegByNummer(mandant.id, nummer, original)) {
        std::printf("Fehler: Beleg \"%s\" nicht gefunden.\n", nummer.c_str());
        return 1;
    }
    if (!HasOption(argc, argv, "--ja")) {
        std::printf("Beleg %s (%s, %s) wird storniert.\n", original.nummer.c_str(),
                    FormatDateGerman(original.datum).c_str(),
                    original.brutto.ToString().c_str());
        std::printf("Dabei entsteht ein neuer Beleg mit umgekehrten Buchungen zum %s.\n",
                    FormatDateGerman(stornoDatum).c_str());
        std::printf("Eine Stornierung lässt sich nicht zurücknehmen.\n"
                    "Zum Ausführen mit --ja wiederholen.\n");
        return 0;
    }

    Beleg storno;
    const StoreResult done = store.StorniereBeleg(
        original.id, stornoDatum, Option(argc, argv, "--grund"),
        Option(argc, argv, "--kreis", "rechnung"), akteur, storno);
    if (!done) { std::printf("Fehler: %s\n", done.fehler.c_str()); return 1; }

    std::printf("Beleg %s storniert durch %s (%s).\n", original.nummer.c_str(),
                storno.nummer.c_str(), storno.brutto.ToString().c_str());
    return 0;
}

int ZahlungBuchen(int argc, char** argv) {
    const std::string datei  = Positional(argc, argv, 0);
    const std::string nummer = Positional(argc, argv, 1);
    const std::string betragText = Option(argc, argv, "--betrag");
    const std::string datumText  = Option(argc, argv, "--datum");
    const std::string geldkonto  = Option(argc, argv, "--konto", "1200");
    if (nummer.empty() || betragText.empty() || datumText.empty()) {
        std::printf("Fehler: Belegnummer, --betrag und --datum sind erforderlich.\n");
        return 2;
    }
    Money betrag;
    if (!Money::TryParse(betragText, betrag)) {
        std::printf("Fehler: \"%s\" ist kein Betrag.\n", betragText.c_str());
        return 2;
    }
    Date datum;
    if (!TryParseDateGerman(datumText, datum)) {
        std::printf("Fehler: \"%s\" ist kein Datum.\n", datumText.c_str());
        return 2;
    }

    Store store;
    if (!OpenStore(store, datei)) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;
    const Akteur akteur = AkteurFor(store);

    Beleg beleg;
    if (!store.BelegByNummer(mandant.id, nummer, beleg)) {
        std::printf("Fehler: Beleg \"%s\" nicht gefunden.\n", nummer.c_str());
        return 1;
    }
    const StoreResult done = store.ZahlungErfassen(
        beleg.id, datum, betrag, geldkonto, Option(argc, argv, "--text"), akteur);
    if (!done) { std::printf("Fehler: %s\n", done.fehler.c_str()); return 1; }

    Beleg danach;
    store.BelegById(beleg.id, danach);
    std::printf("Zahlung %s auf %s gebucht. Beleg %s ist jetzt %s",
                betrag.ToString().c_str(), geldkonto.c_str(), danach.nummer.c_str(),
                BelegStatusLabel(danach.status).c_str());
    if (!danach.Offen().IsZero())
        std::printf(", offen bleiben %s", danach.Offen().ToString().c_str());
    std::printf(".\n");
    return 0;
}

int JournalZeigen(int argc, char** argv) {
    const std::string datei = Positional(argc, argv, 0);
    Store store;
    if (!OpenStore(store, datei)) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;

    Date von, bis;
    const std::string vonText = Option(argc, argv, "--von");
    const std::string bisText = Option(argc, argv, "--bis");
    if (!vonText.empty() && !TryParseDateGerman(vonText, von)) {
        std::printf("Fehler: \"%s\" ist kein Datum.\n", vonText.c_str());
        return 2;
    }
    if (!bisText.empty() && !TryParseDateGerman(bisText, bis)) {
        std::printf("Fehler: \"%s\" ist kein Datum.\n", bisText.c_str());
        return 2;
    }

    const std::vector<Buchung> journal = store.Journal(mandant.id, von, bis);
    if (journal.empty()) { std::printf("Keine Buchungen im Zeitraum.\n"); return 0; }

    std::printf("%5s %-10s %-14s %13s %-2s %-8s %-8s %10s  %s\n",
                "Nr.", "Datum", "Beleg", "Umsatz", "S/H", "Konto", "Gegenkto",
                "Steuer", "Text");
    for (const Buchung& b : journal) {
        std::printf("%5lld %-10s %-14s %13s %-2s  %-8s %-8s %10s  %s%s\n",
                    static_cast<long long>(b.laufendeNummer),
                    FormatDateGerman(b.belegdatum).c_str(),
                    b.belegfeld1.substr(0, 14).c_str(),
                    b.umsatz.ToString().c_str(),
                    SollHabenToText(b.sollHaben).c_str(),
                    b.konto.c_str(), b.gegenkonto.c_str(),
                    b.steuer.IsZero() ? "" : b.steuer.ToString().c_str(),
                    b.buchungstext.substr(0, 30).c_str(),
                    b.IstStorniert() ? "  [storniert]" : (b.IstStorno() ? "  [Storno]" : ""));
    }
    std::printf("\n%d Buchung(en).\n", static_cast<int>(journal.size()));
    return 0;
}

int SaldenZeigen(int argc, char** argv) {
    const std::string datei = Positional(argc, argv, 0);
    Store store;
    if (!OpenStore(store, datei)) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;

    Date von, bis;
    const std::string vonText = Option(argc, argv, "--von");
    const std::string bisText = Option(argc, argv, "--bis");
    if (!vonText.empty()) TryParseDateGerman(vonText, von);
    if (!bisText.empty()) TryParseDateGerman(bisText, bis);

    const std::vector<Store::KontoSaldo> salden = store.SummenUndSalden(mandant.id, von, bis);
    if (salden.empty()) { std::printf("Keine Buchungen im Zeitraum.\n"); return 0; }

    std::printf("%-8s %-34s %13s %13s %13s\n",
                "Konto", "Bezeichnung", "Soll", "Haben", "Saldo");
    Money summeSoll  = Money::Zero(mandant.waehrung);
    Money summeHaben = Money::Zero(mandant.waehrung);
    for (const Store::KontoSaldo& k : salden) {
        std::printf("%-8s %-34s %13s %13s %13s\n", k.konto.c_str(),
                    k.bezeichnung.substr(0, 34).c_str(), k.soll.ToString().c_str(),
                    k.haben.ToString().c_str(), k.saldo.ToString().c_str());
        summeSoll  = summeSoll  + k.soll;
        summeHaben = summeHaben + k.haben;
    }
    std::printf("%-8s %-34s %13s %13s\n", "", "Summe", summeSoll.ToString().c_str(),
                summeHaben.ToString().c_str());

    // The one line that says whether the ledger is a ledger.
    const Money differenz = store.Buchungskreisdifferenz(mandant.id, von, bis);
    if (differenz.IsZero())
        std::printf("\nSoll und Haben gleichen sich aus.\n");
    else
        std::printf("\nACHTUNG: Soll und Haben weichen um %s voneinander ab.\n",
                    differenz.ToString().c_str());
    return 0;
}

int KettePruefen(int argc, char** argv) {
    const std::string datei = Positional(argc, argv, 0);
    Store store;
    if (!OpenStore(store, datei)) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;

    const HashKettenPruefung ergebnis = store.PruefeHashKette(mandant.id);
    std::printf("%lld Buchung(en) geprüft.\n",
                static_cast<long long>(ergebnis.geprueft));
    if (ergebnis.ok) {
        std::printf("Die Prüfsummenkette des Journals ist unversehrt.\n"
                    "\nHinweis: Das ist ein Nachweis der Unveränderbarkeit im Sinne\n"
                    "der GoBD, keine qualifizierte elektronische Signatur.\n");
        return 0;
    }
    std::printf("FEHLER: %s\n", ergebnis.fehler.c_str());
    if (ergebnis.ersteFehlerhafteNummer > 0)
        std::printf("Betroffen ist die laufende Nummer %lld (Buchung %lld).\n",
                    static_cast<long long>(ergebnis.ersteFehlerhafteNummer),
                    static_cast<long long>(ergebnis.ersteFehlerhafteId));
    return 1;
}

int RechnungDrucken(int argc, char** argv) {
    const std::string datei  = Positional(argc, argv, 0);
    const std::string nummer = Positional(argc, argv, 1);
    if (nummer.empty()) {
        std::printf("Fehler: Belegnummer fehlt.\n");
        return 2;
    }
    Store store;
    if (!OpenStore(store, datei)) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;

    Beleg beleg;
    if (!store.BelegByNummer(mandant.id, nummer, beleg)) {
        std::printf("Fehler: Beleg \"%s\" nicht gefunden.\n", nummer.c_str());
        return 1;
    }

    Partner empfaenger;
    if (beleg.partnerId != 0 && !store.PartnerById(beleg.partnerId, empfaenger)) {
        std::printf("Fehler: Der Partner des Belegs wurde nicht gefunden.\n");
        return 1;
    }

    std::string ziel = Option(argc, argv, "--datei");
    if (ziel.empty()) ziel = beleg.nummer + ".pdf";

    // The tax keys as they stood on the Belegdatum, so a zero-rated line names
    // the right exemption even after the table has moved on.
    const std::vector<Steuerschluessel> schluessel =
        store.SteuerschluesselListe(mandant.id, beleg.datum);

    RechnungLayout layout;
    layout.fusszeileZusatz = Option(argc, argv, "--fusszeile");
    const std::string hinweis = Option(argc, argv, "--zahlungshinweis");
    if (!hinweis.empty()) layout.zahlungshinweis = hinweis;

    const RechnungPdfErgebnis ergebnis =
        SchreibeRechnungPdf(mandant, beleg, empfaenger, schluessel, ziel, layout);
    if (!ergebnis.ok) {
        std::printf("Fehler: %s\n", ergebnis.fehler.c_str());
        return 1;
    }

    std::printf("%s %s nach \"%s\" geschrieben (%s).\n", BelegArtLabel(beleg.art).c_str(),
                beleg.nummer.c_str(), ziel.c_str(), beleg.brutto.ToString().c_str());

    // A deficient invoice is the recipient's problem as much as ours: without
    // the mandatory fields they cannot deduct the input tax. So this is a
    // warning on the way out, not a footnote in a log.
    if (!ergebnis.VollstaendigNachUStG()) {
        std::printf("\nACHTUNG: Der Rechnung fehlen Pflichtangaben nach § 14 UStG.\n"
                    "Der Empfänger kann daraus keinen Vorsteuerabzug geltend machen.\n");
        for (const std::string& fehlt : ergebnis.fehlendePflichtangaben)
            std::printf("  - %s\n", fehlt.c_str());
        return 1;
    }
    return 0;
}

// ===== DATEV =====

bool LadeDatevDefinition(const std::string& dateiname, DatevDefinition& out) {
    const std::string pfad = DatevDefinitionPfad(dateiname);
    std::string fehler;
    if (!out.Laden(pfad, fehler)) {
        std::printf("Fehler: %s\n", fehler.c_str());
        return false;
    }
    return true;
}

void ZeigeDatevErgebnis(const DatevErgebnis& ergebnis) {
    std::printf("\"%s\" geschrieben, %d Zeile(n).\n", ergebnis.datei.c_str(),
                ergebnis.zeilen);
    for (const std::string& warnung : ergebnis.warnungen)
        std::printf("  ACHTUNG: %s\n", warnung.c_str());
}

int DatevExport(int argc, char** argv) {
    const std::string datei = Positional(argc, argv, 0);
    Store store;
    if (!OpenStore(store, datei)) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;
    const Akteur akteur = AkteurFor(store);

    const std::string ziel = Option(argc, argv, "--ziel", ".");

    // Kontenbeschriftungen: the whole chart, not tied to a month.
    if (HasOption(argc, argv, "--konten")) {
        DatevDefinition definition;
        if (!LadeDatevDefinition("DATEV-Sachkontenbeschriftungen-v700.csv", definition))
            return 1;
        std::vector<Geschaeftsjahr> jahre = store.Geschaeftsjahre(mandant.id);
        if (jahre.empty()) {
            std::printf("Fehler: Es ist kein Geschäftsjahr angelegt.\n");
            return 1;
        }
        const DatevErgebnis ergebnis = SchreibeKontenbeschriftungen(
            mandant, jahre.back(), store.Konten(mandant.id), definition, ziel,
            akteur.anmeldename);
        if (!ergebnis.ok) { std::printf("Fehler: %s\n", ergebnis.fehler.c_str()); return 1; }
        ZeigeDatevErgebnis(ergebnis);
        return ergebnis.warnungen.empty() ? 0 : 1;
    }

    // A Buchungsstapel is always one calendar month: the Belegdatum field
    // carries no year, so a stack that spans one would be mis-booked.
    const std::string monatText = Option(argc, argv, "--monat");
    if (monatText.empty()) {
        std::printf("Fehler: --monat <JJJJ-MM> fehlt.\n"
                    "Ein Buchungsstapel umfasst immer genau einen Kalendermonat:\n"
                    "das Feld Belegdatum trägt nur TTMM, das Jahr leitet DATEV aus\n"
                    "dem Wirtschaftsjahr ab. Für alle Monate: --alle\n");
        return 2;
    }

    int jahrZahl = 0, monatZahl = 0;
    if (monatText.size() >= 7 && monatText[4] == '-') {
        jahrZahl  = std::atoi(monatText.substr(0, 4).c_str());
        monatZahl = std::atoi(monatText.substr(5, 2).c_str());
    }
    if (jahrZahl < 1900 || monatZahl < 1 || monatZahl > 12) {
        std::printf("Fehler: \"%s\" ist kein Monat (erwartet JJJJ-MM).\n",
                    monatText.c_str());
        return 2;
    }

    Geschaeftsjahr jahr;
    if (!store.GeschaeftsjahrAt(mandant.id, Date(jahrZahl, monatZahl, 1), jahr)) {
        std::printf("Fehler: Zum %02d/%d ist kein Geschäftsjahr angelegt.\n",
                    monatZahl, jahrZahl);
        return 1;
    }

    DatevDefinition definition;
    if (!LadeDatevDefinition("DATEV-Buchungsstapel-v700.csv", definition)) return 1;

    const DatevErgebnis ergebnis = SchreibeBuchungsstapel(
        mandant, jahr, store.Journal(mandant.id), definition, jahrZahl, monatZahl,
        ziel, akteur.anmeldename);
    if (!ergebnis.ok) { std::printf("Fehler: %s\n", ergebnis.fehler.c_str()); return 1; }
    ZeigeDatevErgebnis(ergebnis);
    return ergebnis.warnungen.empty() ? 0 : 1;
}

int DatevPruefen(int argc, char** argv) {
    const std::string datevDatei = Positional(argc, argv, 0);
    if (datevDatei.empty()) {
        std::printf("Fehler: Keine DATEV-Datei angegeben.\n"
                    "Aufruf: ultrafibu datev-pruefen <EXTF_Datei.csv>\n");
        return 2;
    }

    // Which definition to compare against follows from the file's own
    // Format-Kategorie, so the user does not have to know it.
    DatevDefinition stapel;
    if (!LadeDatevDefinition("DATEV-Buchungsstapel-v700.csv", stapel)) return 1;
    DatevPruefung pruefung = PruefeDateiGegenDefinition(datevDatei, stapel);

    if (pruefung.fehler.empty() && pruefung.kategorie == 20) {
        DatevDefinition konten;
        if (!LadeDatevDefinition("DATEV-Sachkontenbeschriftungen-v700.csv", konten))
            return 1;
        pruefung = PruefeDateiGegenDefinition(datevDatei, konten);
    }

    if (!pruefung.fehler.empty()) {
        std::printf("Fehler: %s\n", pruefung.fehler.c_str());
        return 1;
    }

    std::printf("%s, Version %d, Kategorie %d, Formatversion %d\n",
                pruefung.kennzeichen.c_str(), pruefung.versionsnummer,
                pruefung.kategorie, pruefung.formatversion);
    std::printf("Spalten in der Datei: %d, in der Definition: %d\n",
                static_cast<int>(pruefung.spaltenInDatei),
                static_cast<int>(pruefung.spaltenInDefinition));

    if (pruefung.ok) {
        std::printf("\nDie Spaltendefinition stimmt mit dieser Datei überein.\n");
        return 0;
    }
    std::printf("\n%d Abweichung(en):\n",
                static_cast<int>(pruefung.abweichungen.size()));
    for (const std::string& abweichung : pruefung.abweichungen)
        std::printf("  %s\n", abweichung.c_str());
    std::printf("\nBitte die Definition in data/ entsprechend korrigieren. Der\n"
                "Export schreibt über den Spaltennamen, die Werte wandern also mit.\n");
    return 1;
}

int DatevImport(int argc, char** argv) {
    const std::string datei      = Positional(argc, argv, 0);
    const std::string datevDatei = Positional(argc, argv, 1);
    if (datevDatei.empty()) {
        std::printf("Fehler: Keine DATEV-Datei angegeben.\n"
                    "Aufruf: ultrafibu datev-import <datei> <EXTF_Datei.csv>\n");
        return 2;
    }
    Store store;
    if (!OpenStore(store, datei)) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;
    const Akteur akteur = AkteurFor(store);

    std::vector<Geschaeftsjahr> jahre = store.Geschaeftsjahre(mandant.id);
    if (jahre.empty()) {
        std::printf("Fehler: Es ist kein Geschäftsjahr angelegt.\n");
        return 1;
    }

    const DatevImportBericht bericht = LeseBuchungsstapel(
        datevDatei, mandant, jahre.back(),
        store.SteuerschluesselListe(mandant.id));

    std::printf("%s, Version %d, Kategorie %d\n", bericht.kennzeichen.c_str(),
                bericht.versionsnummer, bericht.kategorie);
    if (!bericht.fehler.empty() && !bericht.ok) {
        std::printf("Fehler: %s\n", bericht.fehler.c_str());
        return 1;
    }
    std::printf("Berater %s, Mandant %s, Wirtschaftsjahr ab %s\n",
                bericht.beraternummer.c_str(), bericht.mandantennummer.c_str(),
                FormatDateGerman(bericht.wjBeginn).c_str());
    std::printf("Zeitraum %s - %s, \"%s\"\n",
                FormatDateGerman(bericht.von).c_str(),
                FormatDateGerman(bericht.bis).c_str(),
                bericht.bezeichnung.c_str());
    std::printf("%d Zeile(n) gelesen, %d übernehmbar, %d übersprungen, "
                "%d mit Steueraufteilung\n",
                bericht.gelesen, bericht.uebernommen, bericht.uebersprungen,
                bericht.mitSteuer);

    for (const std::string& zeile : bericht.fehlerZeilen)
        std::printf("  FEHLER: %s\n", zeile.c_str());
    for (const std::string& warnung : bericht.warnungen)
        std::printf("  ACHTUNG: %s\n", warnung.c_str());

    // A Sachkonto that is in the file and not in the chart is usually a wrong
    // account rather than a missing one, and after the import it is only a
    // nameless number in the Saldenliste. Said here, while nothing is written
    // yet, it is one line to check.
    {
        const std::vector<std::string> fehlend =
            UnbekannteSachkonten(bericht, store.Konten(mandant.id));
        if (!fehlend.empty()) {
            std::string liste;
            for (const std::string& konto : fehlend) {
                if (!liste.empty()) liste += ", ";
                liste += konto;
            }
            std::printf("  ACHTUNG: Diese Sachkonten stehen in der Datei, aber "
                        "nicht im Kontenrahmen: %s. Sie erscheinen nach dem "
                        "Import ohne Bezeichnung in der Saldenliste.\n",
                        liste.c_str());
        }
    }

    // A first look at somebody else's file should never write. Importing is
    // the explicit second step.
    if (!HasOption(argc, argv, "--uebernehmen")) {
        std::printf("\nEs wurde nichts geschrieben. Zum Übernehmen mit "
                    "--uebernehmen wiederholen.\n");
        return bericht.ok ? 0 : 1;
    }

    int geschrieben = 0;
    const StoreResult importiert = store.ImportiereDatevStapel(
        bericht, datevDatei, akteur, HasOption(argc, argv, "--nochmal"), geschrieben);
    if (!importiert) {
        std::printf("\nFehler: %s\n", importiert.fehler.c_str());
        return 1;
    }
    std::printf("\n%d Buchung(en) übernommen.\n", geschrieben);
    return 0;
}

int DatevImporte(int argc, char** argv) {
    const std::string datei = Positional(argc, argv, 0);
    Store store;
    if (!OpenStore(store, datei)) return 1;
    Mandant mandant;
    if (!ErsterMandant(store, mandant)) return 1;

    const std::vector<Store::DatevImportEintrag> liste = store.DatevImporte(mandant.id);
    if (liste.empty()) { std::printf("Es wurde noch nichts importiert.\n"); return 0; }
    std::printf("%-30s %-10s %-10s %8s  %s\n", "Datei", "von", "bis", "Zeilen",
                "Benutzer");
    for (const Store::DatevImportEintrag& eintrag : liste) {
        std::printf("%-30s %-10s %-10s %8d  %s\n",
                    eintrag.dateiname.substr(0, 30).c_str(),
                    FormatDateGerman(eintrag.von).c_str(),
                    FormatDateGerman(eintrag.bis).c_str(),
                    eintrag.zeilen, eintrag.benutzer.c_str());
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { PrintUsage(); return 2; }
    const std::string befehl = argv[1];

    if (befehl == "--help" || befehl == "-h" || befehl == "hilfe") { PrintUsage(); return 0; }
    if (befehl == "--version") { std::printf("%s\n", ULTRAFIBU_CLI_VERSION); return 0; }
    if (befehl == "einrichten")    return Einrichten(argc, argv);
    if (befehl == "info")          return Info(argc, argv);
    if (befehl == "konten")        return Konten(argc, argv);
    if (befehl == "partner")       return PartnerListeZeigen(argc, argv);
    if (befehl == "partner-neu")   return PartnerNeu(argc, argv);
    if (befehl == "ustid")         return UstIdPruefen(argc, argv);
    if (befehl == "termine")       return Termine(argc, argv);
    if (befehl == "perioden")      return Perioden(argc, argv);
    if (befehl == "festschreiben") return Festschreiben(argc, argv);
    if (befehl == "protokoll")     return Protokoll(argc, argv);
    if (befehl == "beleg-neu")     return BelegNeu(argc, argv);
    if (befehl == "belege")        return BelegeZeigen(argc, argv);
    if (befehl == "buchen")        return BelegBuchen(argc, argv);
    if (befehl == "storno")        return BelegStornieren(argc, argv);
    if (befehl == "zahlung")       return ZahlungBuchen(argc, argv);
    if (befehl == "journal")       return JournalZeigen(argc, argv);
    if (befehl == "salden")        return SaldenZeigen(argc, argv);
    if (befehl == "pruefen")       return KettePruefen(argc, argv);
    if (befehl == "rechnung-pdf")  return RechnungDrucken(argc, argv);
    if (befehl == "datev-export")  return DatevExport(argc, argv);
    if (befehl == "datev-pruefen") return DatevPruefen(argc, argv);
    if (befehl == "datev-import")  return DatevImport(argc, argv);
    if (befehl == "datev-importe") return DatevImporte(argc, argv);

    std::printf("Unbekannter Befehl: %s\n\n", befehl.c_str());
    PrintUsage();
    return 2;
}
