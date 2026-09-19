// Apps/UltraFIBU/cli/main.cpp
// ultrafibu - the command-line front end to the UltraFIBU engine.
//
// Everything the engine can do today, without waiting for the UI: set up a
// company with a fiscal year that starts on any date, import a chart of
// accounts and a set of tax keys, keep customers and suppliers with their
// European VAT numbers, check a VAT number, list the filing deadlines and
// freeze a period.
//
// The output is German, like the application it belongs to. It is also the
// engine's first real caller, which is what keeps the engine honest about
// being usable without a window.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
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

    std::printf("Unbekannter Befehl: %s\n\n", befehl.c_str());
    PrintUsage();
    return 2;
}
