// Apps/UltraFIBU/engine/UltraFIBUStore.cpp
// Schema, migrations and queries. See the header for the four rules that keep
// this file portable between SQLite and PostgreSQL; they are why there is a
// `sequenz` table instead of AUTOINCREMENT and ISO text instead of a date type.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUStore.h"

#include <sys/stat.h>
#include "UltraFIBUUstIdNr.h"

#include <UltraDatabase/UltraDatabaseConnection.h>
#include <UltraDatabase/UltraDatabaseMigrate.h>
#include <UltraDatabase/UltraDatabaseQuery.h>
#include <UltraDatabase/UltraDatabaseTransaction.h>

#include <UltraCrypt/UltraCryptCore.h>

#include <ctime>

namespace UltraFIBU {

namespace {

int64_t NowSeconds() {
    return static_cast<int64_t>(std::time(nullptr));
}

std::string Number(int64_t value) {
    std::string digits;
    int64_t v = value < 0 ? -value : value;
    if (v == 0) digits = "0";
    while (v > 0) {
        digits.insert(digits.begin(), static_cast<char>('0' + (v % 10)));
        v /= 10;
    }
    if (value < 0) digits.insert(digits.begin(), '-');
    return digits;
}

std::string ToLower(const std::string& text) {
    std::string out = text;
    for (char& c : out) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// An ISO date column, read back. An empty or unreadable column yields an
// invalid Date, which every caller already treats as "not set".
Date DateFrom(const UltraDbValue& value) {
    Date date;
    if (value.IsNull()) return date;
    Date::TryParseIso(value.AsString(), date);
    return date;
}

// A date as a column value: ISO text, or NULL when it is not set. Never an
// empty string, so "not set" is one thing in the database and not two.
UltraDbValue DateValue(const Date& date) {
    if (!date.Valid()) return UltraDbValue::Null();
    return UltraDbValue(date.ToIso());
}

const char* const kSchemaV1 =
    // Surrogate keys and document counters. One mechanism for both, portable
    // to every engine, and the only place a "next number" lives.
    "CREATE TABLE sequenz("
    "  name TEXT PRIMARY KEY,"
    "  naechste BIGINT NOT NULL);"

    "CREATE TABLE mandant("
    "  id BIGINT PRIMARY KEY,"
    "  name TEXT NOT NULL,"
    "  rechtsform TEXT,"
    "  strasse TEXT, plz TEXT, ort TEXT, land TEXT,"
    "  steuernummer TEXT, ust_idnr TEXT,"
    "  finanzamt TEXT, finanzamt_nummer TEXT,"
    "  beraternummer TEXT, mandantennummer TEXT,"
    "  waehrung TEXT,"
    "  besteuerung TEXT, gewinnermittlung TEXT, ustva_rhythmus TEXT,"
    "  dauerfristverlaengerung INTEGER DEFAULT 0,"
    "  kleinunternehmer INTEGER DEFAULT 0,"
    "  oss_registriert INTEGER DEFAULT 0,"
    "  ioss_registriert INTEGER DEFAULT 0,"
    "  telefon TEXT, email TEXT, webseite TEXT,"
    "  iban TEXT, bic TEXT, bank TEXT);"

    // The fiscal year. beginn/ende are ISO dates, so a 1 April year is an
    // ordinary row and no column implies January.
    "CREATE TABLE geschaeftsjahr("
    "  id BIGINT PRIMARY KEY,"
    "  mandant_id BIGINT NOT NULL,"
    "  beginn TEXT NOT NULL,"
    "  ende TEXT NOT NULL,"
    "  status TEXT NOT NULL,"
    "  festschreibung_bis TEXT,"
    "  skr TEXT,"
    "  sachkontenlaenge INTEGER DEFAULT 4,"
    "  bezeichnung TEXT);"
    "CREATE INDEX ix_gj_mandant ON geschaeftsjahr(mandant_id, beginn);"

    "CREATE TABLE konto("
    "  id BIGINT PRIMARY KEY,"
    "  mandant_id BIGINT NOT NULL,"
    "  nummer TEXT NOT NULL,"
    "  bezeichnung TEXT,"
    "  typ TEXT,"
    "  skr TEXT,"
    "  steuerschluessel TEXT,"
    "  euer_zeile TEXT,"
    "  bwa_position TEXT,"
    "  bilanz_position TEXT,"
    "  aktiv INTEGER DEFAULT 1,"
    "  notiz TEXT);"
    "CREATE UNIQUE INDEX ux_konto ON konto(mandant_id, nummer);"

    "CREATE TABLE steuerschluessel("
    "  id BIGINT PRIMARY KEY,"
    "  mandant_id BIGINT NOT NULL,"
    "  schluessel TEXT NOT NULL,"
    "  bezeichnung TEXT,"
    "  art TEXT,"
    "  satz_promille INTEGER DEFAULT 0,"
    "  land TEXT,"
    "  vorsteuer INTEGER DEFAULT 0,"
    "  datev_bu TEXT,"
    "  kz_bemessung TEXT,"
    "  kz_steuer TEXT,"
    "  konto_umsatz TEXT,"
    "  konto_steuer TEXT,"
    "  gueltig_von TEXT,"
    "  gueltig_bis TEXT);"
    "CREATE UNIQUE INDEX ux_steuerschluessel ON steuerschluessel(mandant_id, schluessel, gueltig_von);"

    "CREATE TABLE partner("
    "  id BIGINT PRIMARY KEY,"
    "  mandant_id BIGINT NOT NULL,"
    "  typ TEXT NOT NULL,"
    "  konto TEXT,"
    "  name TEXT NOT NULL,"
    "  name2 TEXT, anrede TEXT, kontaktperson TEXT, abteilung TEXT,"
    "  strasse TEXT, plz TEXT, ort TEXT, land TEXT,"
    "  steuerkategorie TEXT,"
    "  ust_idnr TEXT, ust_idnr_status TEXT, ust_idnr_geprueft_am TEXT,"
    "  ust_idnr_protokoll TEXT,"
    "  steuernummer TEXT,"
    "  email TEXT, telefon TEXT, webseite TEXT,"
    "  iban TEXT, bic TEXT,"
    "  zahlungsfrist_tage INTEGER DEFAULT 14,"
    "  skonto_promille INTEGER DEFAULT 0,"
    "  skonto_tage INTEGER DEFAULT 0,"
    "  sprache TEXT, notiz TEXT,"
    "  aktiv INTEGER DEFAULT 1,"
    "  angelegt_am BIGINT DEFAULT 0,"
    "  geaendert_am BIGINT DEFAULT 0,"
    "  version BIGINT DEFAULT 1);"
    "CREATE UNIQUE INDEX ux_partner_konto ON partner(mandant_id, konto);"
    "CREATE INDEX ix_partner_name ON partner(mandant_id, name);"
    "CREATE INDEX ix_partner_ustid ON partner(mandant_id, ust_idnr);"

    // Users from the first schema, not from the day the server arrives: GoBD
    // attribution cannot be backfilled.
    "CREATE TABLE benutzer("
    "  id BIGINT PRIMARY KEY,"
    "  anmeldename TEXT NOT NULL,"
    "  anzeigename TEXT,"
    "  email TEXT,"
    "  rolle TEXT NOT NULL,"
    "  aktiv INTEGER DEFAULT 1,"
    "  angelegt_am BIGINT DEFAULT 0,"
    "  letzter_login BIGINT DEFAULT 0,"
    "  passwort_hash TEXT,"
    "  passwort_salt TEXT,"
    "  kdf_algorithmus TEXT,"
    "  kdf_iterationen INTEGER DEFAULT 0,"
    "  kdf_speicher_kib INTEGER DEFAULT 0,"
    "  kdf_laenge INTEGER DEFAULT 32);"
    "CREATE UNIQUE INDEX ux_benutzer_name ON benutzer(anmeldename);"

    "CREATE TABLE nummernkreis("
    "  id BIGINT PRIMARY KEY,"
    "  mandant_id BIGINT NOT NULL,"
    "  kreis TEXT NOT NULL,"
    "  praefix TEXT,"
    "  naechste BIGINT DEFAULT 1,"
    "  stellen INTEGER DEFAULT 4,"
    "  jaehrlich_zuruecksetzen INTEGER DEFAULT 0,"
    "  letztes_jahr INTEGER DEFAULT 0);"
    "CREATE UNIQUE INDEX ux_nummernkreis ON nummernkreis(mandant_id, kreis);"

    "CREATE TABLE audit("
    "  id BIGINT PRIMARY KEY,"
    "  zeit BIGINT NOT NULL,"
    "  benutzer_id BIGINT DEFAULT 0,"
    "  benutzer TEXT,"
    "  tabelle TEXT,"
    "  row_id BIGINT DEFAULT 0,"
    "  aktion TEXT,"
    "  details TEXT);"
    "CREATE INDEX ix_audit_zeit ON audit(zeit DESC);";

// Schema 2 adds the documents and the journal. Two things in it are worth
// noticing before the columns:
//
//  - `buchung` has no UPDATE path in this file except `storniert_durch` and
//    `festgeschrieben`, and both are deliberately outside the hashed canonical
//    form (see UltraFIBUBuchung.h) because they are set after a row is sealed.
//  - `laufende_nummer` is unique per Mandant and is what the hash chain walks.
//    It is allocated in the same transaction as the row, so two writers cannot
//    both be told they are number 41.
const char* const kSchemaV2 =
    "CREATE TABLE beleg("
    "  id BIGINT PRIMARY KEY,"
    "  mandant_id BIGINT NOT NULL,"
    "  geschaeftsjahr_id BIGINT NOT NULL,"
    "  art TEXT NOT NULL,"
    "  nummer TEXT NOT NULL,"
    "  externe_nummer TEXT,"
    "  datum TEXT NOT NULL,"
    "  leistung_von TEXT, leistung_bis TEXT, faellig_am TEXT,"
    "  partner_id BIGINT DEFAULT 0,"
    "  partner_konto TEXT, partner_name TEXT,"
    "  waehrung TEXT,"
    "  netto BIGINT DEFAULT 0, steuer BIGINT DEFAULT 0,"
    "  brutto BIGINT DEFAULT 0, bezahlt BIGINT DEFAULT 0,"
    "  status TEXT NOT NULL,"
    "  buchungstext TEXT, notiz TEXT,"
    "  datei_pfad TEXT, datei_hash TEXT,"
    "  storno_von BIGINT DEFAULT 0, storniert_durch BIGINT DEFAULT 0,"
    "  festgeschrieben INTEGER DEFAULT 0,"
    "  erfasst_von BIGINT DEFAULT 0, erfasst_von_name TEXT,"
    "  erfasst_am BIGINT DEFAULT 0, geaendert_am BIGINT DEFAULT 0,"
    "  version BIGINT DEFAULT 1);"
    // Unique per Mandant across every art: a document number identifies a
    // document, and two documents sharing one number is the gap-free-numbering
    // problem in its other direction.
    "CREATE UNIQUE INDEX ux_beleg_nummer ON beleg(mandant_id, nummer);"
    "CREATE INDEX ix_beleg_datum ON beleg(mandant_id, datum);"
    "CREATE INDEX ix_beleg_partner ON beleg(mandant_id, partner_id);"
    "CREATE INDEX ix_beleg_status ON beleg(mandant_id, status, faellig_am);"

    "CREATE TABLE beleg_position("
    "  id BIGINT PRIMARY KEY,"
    "  beleg_id BIGINT NOT NULL,"
    "  position INTEGER NOT NULL,"
    "  bezeichnung TEXT,"
    "  menge_tausendstel BIGINT DEFAULT 1000,"
    "  einheit TEXT,"
    "  einzelpreis BIGINT DEFAULT 0,"
    "  rabatt_promille INTEGER DEFAULT 0,"
    "  konto TEXT, steuerschluessel TEXT, satz_promille INTEGER DEFAULT 0,"
    "  netto BIGINT DEFAULT 0, steuer BIGINT DEFAULT 0, brutto BIGINT DEFAULT 0,"
    "  kostenstelle TEXT, kostentraeger TEXT,"
    "  waehrung TEXT);"
    "CREATE INDEX ix_belegpos ON beleg_position(beleg_id, position);"

    "CREATE TABLE buchung("
    "  id BIGINT PRIMARY KEY,"
    "  mandant_id BIGINT NOT NULL,"
    "  geschaeftsjahr_id BIGINT NOT NULL,"
    "  periode INTEGER DEFAULT 0,"
    "  belegdatum TEXT NOT NULL,"
    "  beleg_id BIGINT DEFAULT 0,"
    "  belegfeld1 TEXT, belegfeld2 TEXT,"
    "  umsatz BIGINT DEFAULT 0,"
    "  soll_haben TEXT NOT NULL,"
    "  konto TEXT NOT NULL, gegenkonto TEXT NOT NULL,"
    "  bu_schluessel TEXT,"
    "  steuerschluessel TEXT, steuer_seite TEXT, satz_promille INTEGER DEFAULT 0,"
    "  netto BIGINT DEFAULT 0, steuer BIGINT DEFAULT 0, steuerkonto TEXT,"
    "  buchungstext TEXT, kost1 TEXT, kost2 TEXT, waehrung TEXT,"
    "  storno_von BIGINT DEFAULT 0, storniert_durch BIGINT DEFAULT 0,"
    "  festgeschrieben INTEGER DEFAULT 0,"
    "  erfasst_von BIGINT DEFAULT 0, erfasst_von_name TEXT,"
    "  erfasst_am BIGINT DEFAULT 0,"
    "  laufende_nummer BIGINT NOT NULL,"
    "  prev_hash TEXT, hash TEXT);"
    "CREATE UNIQUE INDEX ux_buchung_lfd ON buchung(mandant_id, laufende_nummer);"
    "CREATE INDEX ix_buchung_datum ON buchung(mandant_id, belegdatum);"
    "CREATE INDEX ix_buchung_beleg ON buchung(beleg_id);"
    "CREATE INDEX ix_buchung_konto ON buchung(mandant_id, konto);"
    "CREATE INDEX ix_buchung_gegenkonto ON buchung(mandant_id, gegenkonto);"

    "CREATE TABLE zahlung("
    "  id BIGINT PRIMARY KEY,"
    "  mandant_id BIGINT NOT NULL,"
    "  beleg_id BIGINT NOT NULL,"
    "  datum TEXT NOT NULL,"
    "  betrag BIGINT DEFAULT 0,"
    "  waehrung TEXT,"
    "  geldkonto TEXT,"
    "  buchung_id BIGINT DEFAULT 0,"
    "  notiz TEXT,"
    "  erfasst_von BIGINT DEFAULT 0,"
    "  erfasst_am BIGINT DEFAULT 0);"
    "CREATE INDEX ix_zahlung_beleg ON zahlung(beleg_id, datum);";

// Schema 3 records which DATEV files have been imported. Without it the same
// Buchungsstapel imported twice would silently double a month, and the only
// evidence would be a balance that is exactly wrong by one stack.
const char* const kSchemaV3 =
    "CREATE TABLE datev_import("
    "  id BIGINT PRIMARY KEY,"
    "  mandant_id BIGINT NOT NULL,"
    "  dateiname TEXT,"
    "  datei_hash TEXT NOT NULL,"
    "  zeitpunkt BIGINT NOT NULL,"
    "  benutzer TEXT,"
    "  zeilen INTEGER DEFAULT 0,"
    "  von TEXT, bis TEXT);"
    "CREATE INDEX ix_datev_import ON datev_import(mandant_id, datei_hash);";

// Schema 4 is the bank: the accounts, the statement lines, and which document
// each line pays.
//
// The UNIQUE on (bankkonto_id, referenz) is the whole idempotency story in one
// constraint. A reference is the bank's own, unique within an account but not
// between accounts, so the account is part of the key. It is a backstop rather
// than the mechanism - the import checks first so it can *count* what it
// skipped - but a constraint that cannot be argued with is what makes the rule
// true even if a future caller forgets to check.
const char* const kSchemaV4 =
    "CREATE TABLE bankkonto("
    "  id BIGINT PRIMARY KEY,"
    "  mandant_id BIGINT NOT NULL,"
    "  bezeichnung TEXT NOT NULL,"
    "  iban TEXT, bic TEXT, bank TEXT,"
    "  konto TEXT NOT NULL,"
    "  waehrung TEXT DEFAULT 'EUR',"
    "  csv_profil TEXT,"
    "  aktiv INTEGER DEFAULT 1,"
    "  letzter_import_bis TEXT,"
    "  version BIGINT DEFAULT 1);"
    "CREATE INDEX ix_bankkonto_mandant ON bankkonto(mandant_id, aktiv);"

    "CREATE TABLE bankumsatz("
    "  id BIGINT PRIMARY KEY,"
    "  bankkonto_id BIGINT NOT NULL,"
    "  mandant_id BIGINT NOT NULL,"
    "  buchungstag TEXT NOT NULL,"
    "  valuta TEXT,"
    "  betrag BIGINT NOT NULL,"
    "  waehrung TEXT DEFAULT 'EUR',"
    "  gegen_iban TEXT, gegen_bic TEXT, gegen_name TEXT,"
    "  verwendungszweck TEXT,"
    "  e2e_ref TEXT, mandatsreferenz TEXT, glaeubiger_id TEXT,"
    "  buchungstext TEXT,"
    "  referenz TEXT NOT NULL,"
    "  teilbuchungen INTEGER DEFAULT 0,"
    "  import_id BIGINT DEFAULT 0,"
    "  importiert_am BIGINT DEFAULT 0,"
    "  CONSTRAINT uq_bankumsatz_ref UNIQUE (bankkonto_id, referenz));"
    "CREATE INDEX ix_bankumsatz_konto ON bankumsatz(bankkonto_id, buchungstag);"
    "CREATE INDEX ix_bankumsatz_mandant ON bankumsatz(mandant_id, buchungstag);"

    "CREATE TABLE zuordnung("
    "  id BIGINT PRIMARY KEY,"
    "  bankumsatz_id BIGINT NOT NULL,"
    "  beleg_id BIGINT NOT NULL,"
    "  betrag BIGINT NOT NULL,"
    "  waehrung TEXT DEFAULT 'EUR',"
    "  zahlung_id BIGINT DEFAULT 0,"
    "  erfasst_von BIGINT DEFAULT 0,"
    "  erfasst_am BIGINT DEFAULT 0);"
    "CREATE INDEX ix_zuordnung_umsatz ON zuordnung(bankumsatz_id);"
    "CREATE INDEX ix_zuordnung_beleg ON zuordnung(beleg_id);"

    "CREATE TABLE bank_import("
    "  id BIGINT PRIMARY KEY,"
    "  mandant_id BIGINT NOT NULL,"
    "  bankkonto_id BIGINT NOT NULL,"
    "  dateiname TEXT,"
    "  datei_hash TEXT,"
    "  format TEXT,"
    "  zeitpunkt BIGINT NOT NULL,"
    "  benutzer TEXT,"
    "  gelesen INTEGER DEFAULT 0,"
    "  neu INTEGER DEFAULT 0,"
    "  bekannt INTEGER DEFAULT 0,"
    "  von TEXT, bis TEXT);"
    "CREATE INDEX ix_bank_import ON bank_import(mandant_id, zeitpunkt);";

// Schema 5 is the submission log: every return this program produced, the hash
// of exactly what was written, and the Transferticket that proves it arrived.
//
// `kennzahlen_json` holds the figures AS FILED rather than a reference to the
// journal. Recomputing a return two years later does not prove what was sent -
// the journal may legitimately have moved on - and "what was sent" is the only
// question that gets asked when a figure is disputed.
const char* const kSchemaV5 =
    "CREATE TABLE meldung("
    "  id BIGINT PRIMARY KEY,"
    "  mandant_id BIGINT NOT NULL,"
    "  art TEXT NOT NULL,"
    "  jahr INTEGER NOT NULL,"
    "  zeitraum TEXT NOT NULL,"
    "  status TEXT NOT NULL,"
    "  zahllast BIGINT DEFAULT 0,"
    "  kennzahlen_json TEXT,"
    "  datei TEXT,"
    "  xml_hash TEXT,"
    "  transferticket TEXT,"
    "  berichtigt INTEGER DEFAULT 0,"
    "  echtfall INTEGER DEFAULT 0,"
    "  erzeugt_am BIGINT DEFAULT 0,"
    "  eingereicht_am BIGINT DEFAULT 0,"
    "  benutzer TEXT);"
    "CREATE INDEX ix_meldung ON meldung(mandant_id, art, jahr, zeitraum);";

// The VAT rates of the other member states, as an editable table rather than
// only the shipped CSV - rates are set by twenty-six parliaments and change on
// their own timetable, so this has to be maintainable without a new release.
//
// **The primary key is (land, art, gueltig_von), and that is the design.** A
// changed rate is a new row starting on the day it changes; the previous row
// keeps its span and is closed the day before. Updating a rate in place would
// silently change what an already-filed return recomputes to - the one thing
// a set of books must never do.
const char* const kSchemaV6 =
    "CREATE TABLE eu_steuersatz("
    "  id BIGINT PRIMARY KEY,"
    "  land TEXT NOT NULL,"
    "  art TEXT NOT NULL,"
    "  satz_promille INTEGER NOT NULL,"
    "  gueltig_von TEXT NOT NULL,"
    "  gueltig_bis TEXT,"
    "  geprueft INTEGER NOT NULL DEFAULT 0,"
    "  quelle TEXT,"
    "  erfasst_am BIGINT DEFAULT 0,"
    "  benutzer TEXT);"
    "CREATE UNIQUE INDEX ix_eu_steuersatz ON eu_steuersatz(land, art, gueltig_von);";

// Three facts about a document that were previously derived and should not
// have been.
//
//  - `preise_brutto`: whether the prices on it were entered gross. A receipt
//    states gross, and a draft reopened next week has to show the number on
//    the receipt rather than a converted one.
//  - `steuer_vorgegeben` / `vorgegebene_steuer`: the tax the document itself
//    states. On an incoming document that is a fact, not a computation - see
//    the comment on Beleg::vorgegebeneSteuer for the receipt that proved it.
//  - `leistungsart`: which of the § 14 Abs. 4 Nr. 6 facts the dates state.
//    "Geliefert am" and "geleistet im Zeitraum" are different statements, and
//    the column existed for neither.
const char* const kSchemaV7 =
    "ALTER TABLE beleg ADD COLUMN preise_brutto INTEGER DEFAULT 0;"
    "ALTER TABLE beleg ADD COLUMN steuer_vorgegeben INTEGER DEFAULT 0;"
    "ALTER TABLE beleg ADD COLUMN vorgegebene_steuer BIGINT DEFAULT 0;"
    "ALTER TABLE beleg ADD COLUMN leistungsart TEXT;";

// Was der DATEV-Kontenrahmen ueber ein Konto sagt und wofuer es bisher keine
// Spalte gab. Die Quelle ist die Legende des Kontenrahmen-PDF:
//
//  - `funktion`: KU/V/M sind Zusatzfunktionen ueber einer Kontenklasse,
//    AV/AM/S/F/R Hauptfunktionen vor einem Konto; kombiniert auch "S/AV".
//    **AV und AM sind die Automatikkonten** - die Angabe, aus der sich
//    ergibt, welches Konto seinen Steuersatz selbst mitbringt.
//  - `abschlusszweck`: HB nur Handelsbilanz, SB nur Steuerbilanz, EUeR nur
//    Einnahmen-Ueberschuss-Rechnung. Ein Konto, das nur fuer die Steuerbilanz
//    gedacht ist, gehoert nicht in die Handelsbilanz - das ist eine Auswertung
//    und keine Kosmetik.
//  - `programmverbindung`: U/G/K, die Weitergabe an Umsatzsteuererklaerung,
//    Gewerbesteuer und Koerperschaftsteuer.
//  - `nummer_bis`: gesetzt, wenn die Zeile einen Kontenbereich beschreibt.
//    Das PDF druckt "0040-42" fuer einen Block ohne Einzelbeschriftung;
//    ihn als ein Konto ohne Namen abzulegen waere falsch.
//
// `bilanz_position` gab es schon und bleibt, wo es ist.
const char* const kSchemaV8 =
    "ALTER TABLE konto ADD COLUMN funktion TEXT;"
    "ALTER TABLE konto ADD COLUMN abschlusszweck TEXT;"
    "ALTER TABLE konto ADD COLUMN programmverbindung TEXT;"
    "ALTER TABLE konto ADD COLUMN nummer_bis TEXT;";

} // namespace

// ===== BELEGE UND BUCHUNGEN =====

namespace {

// Money out of a BIGINT minor-units column. The currency comes from the row's
// own column rather than a default, so a document in CHF reads back as CHF.
Money MoneyFrom(const UltraDbValue& value, const std::string& waehrung) {
    return Money::FromMinor(value.AsInt64(), waehrung.empty() ? "EUR" : waehrung);
}

// Which side the document's settlement account stands on. This is the one
// place the direction of a document becomes accounting, and it is a table
// rather than a sign test: inferring debit or credit from whether an amount
// happens to be negative is how a credit note ends up increasing revenue.
SollHaben PersonenkontoSeite(BelegArt art) {
    switch (art) {
        case BelegArt::Ausgangsrechnung:   return SollHaben::Soll;   // receivable up
        case BelegArt::Ausgangsgutschrift: return SollHaben::Haben;  // receivable down
        case BelegArt::Eingangsrechnung:   return SollHaben::Haben;  // payable up
        case BelegArt::Eingangsgutschrift: return SollHaben::Soll;   // payable down
        // A cash receipt is money leaving the till against an expense. Cash
        // *taken* is entered as an Ausgangsrechnung whose settlement account is
        // the till - explicit, rather than decided by the sign of the total.
        case BelegArt::Kassenbeleg:        return SollHaben::Haben;
        case BelegArt::Sonstiges:          break;
    }
    return SollHaben::Soll;
}

Beleg BelegFromRow(const UltraDbRow& row) {
    Beleg beleg;
    beleg.id               = row["id"].AsInt64();
    beleg.mandantId        = row["mandant_id"].AsInt64();
    beleg.geschaeftsjahrId = row["geschaeftsjahr_id"].AsInt64();
    BelegArtFromText(row["art"].AsString(), beleg.art);
    beleg.nummer         = row["nummer"].AsString();
    beleg.externeNummer  = row["externe_nummer"].AsString();
    beleg.datum          = DateFrom(row["datum"]);
    beleg.leistungVon    = DateFrom(row["leistung_von"]);
    beleg.leistungBis    = DateFrom(row["leistung_bis"]);
    beleg.faelligAm      = DateFrom(row["faellig_am"]);
    beleg.partnerId      = row["partner_id"].AsInt64();
    beleg.partnerKonto   = row["partner_konto"].AsString();
    beleg.partnerName    = row["partner_name"].AsString();
    beleg.waehrung       = row["waehrung"].AsString();
    if (beleg.waehrung.empty()) beleg.waehrung = "EUR";
    beleg.netto   = MoneyFrom(row["netto"],   beleg.waehrung);
    beleg.steuer  = MoneyFrom(row["steuer"],  beleg.waehrung);
    beleg.brutto  = MoneyFrom(row["brutto"],  beleg.waehrung);
    beleg.bezahlt = MoneyFrom(row["bezahlt"], beleg.waehrung);
    BelegStatusFromText(row["status"].AsString(), beleg.status);
    beleg.buchungstext    = row["buchungstext"].AsString();
    beleg.notiz           = row["notiz"].AsString();
    beleg.dateiPfad       = row["datei_pfad"].AsString();
    beleg.dateiHash       = row["datei_hash"].AsString();
    beleg.stornoVon       = row["storno_von"].AsInt64();
    beleg.storniertDurch  = row["storniert_durch"].AsInt64();
    beleg.festgeschrieben = row["festgeschrieben"].AsInt() != 0;
    beleg.erfasstVon      = row["erfasst_von"].AsInt64();
    beleg.erfasstVonName  = row["erfasst_von_name"].AsString();
    beleg.erfasstAm       = row["erfasst_am"].AsInt64();
    beleg.geaendertAm     = row["geaendert_am"].AsInt64();
    beleg.version         = row["version"].AsInt64();
    beleg.preiseSindBrutto  = row["preise_brutto"].AsInt() != 0;
    beleg.steuerVorgegeben  = row["steuer_vorgegeben"].AsInt() != 0;
    beleg.vorgegebeneSteuer = MoneyFrom(row["vorgegebene_steuer"], beleg.waehrung);
    // A row written before schema v7 has no value here. Leistungsdatum is the
    // safe reading: it is what the old single date field meant.
    if (!LeistungszeitpunktFromText(row["leistungsart"].AsString(), beleg.leistungsart))
        beleg.leistungsart = Leistungszeitpunkt::Leistungsdatum;
    return beleg;
}

const char* const kBelegSpalten =
    "id, mandant_id, geschaeftsjahr_id, art, nummer, externe_nummer, datum,"
    " leistung_von, leistung_bis, faellig_am, partner_id, partner_konto,"
    " partner_name, waehrung, netto, steuer, brutto, bezahlt, status,"
    " buchungstext, notiz, datei_pfad, datei_hash, storno_von, storniert_durch,"
    " festgeschrieben, erfasst_von, erfasst_von_name, erfasst_am, geaendert_am,"
    " version, preise_brutto, steuer_vorgegeben, vorgegebene_steuer, leistungsart";

BelegPosition PositionFromRow(const UltraDbRow& row) {
    BelegPosition pos;
    const std::string waehrung = row["waehrung"].AsString();
    pos.id               = row["id"].AsInt64();
    pos.belegId          = row["beleg_id"].AsInt64();
    pos.position         = row["position"].AsInt();
    pos.bezeichnung      = row["bezeichnung"].AsString();
    pos.mengeTausendstel = row["menge_tausendstel"].AsInt64();
    pos.einheit          = row["einheit"].AsString();
    pos.einzelpreis      = MoneyFrom(row["einzelpreis"], waehrung);
    pos.rabattPromille   = row["rabatt_promille"].AsInt();
    pos.konto            = row["konto"].AsString();
    pos.steuerschluessel = row["steuerschluessel"].AsString();
    pos.satzPromille     = row["satz_promille"].AsInt();
    pos.netto            = MoneyFrom(row["netto"],  waehrung);
    pos.steuer           = MoneyFrom(row["steuer"], waehrung);
    pos.brutto           = MoneyFrom(row["brutto"], waehrung);
    pos.kostenstelle     = row["kostenstelle"].AsString();
    pos.kostentraeger    = row["kostentraeger"].AsString();
    return pos;
}

const char* const kPositionSpalten =
    "id, beleg_id, position, bezeichnung, menge_tausendstel, einheit,"
    " einzelpreis, rabatt_promille, konto, steuerschluessel, satz_promille,"
    " netto, steuer, brutto, kostenstelle, kostentraeger, waehrung";

Buchung BuchungFromRow(const UltraDbRow& row) {
    Buchung b;
    const std::string waehrung = row["waehrung"].AsString();
    b.id               = row["id"].AsInt64();
    b.mandantId        = row["mandant_id"].AsInt64();
    b.geschaeftsjahrId = row["geschaeftsjahr_id"].AsInt64();
    b.periode          = row["periode"].AsInt();
    b.belegdatum       = DateFrom(row["belegdatum"]);
    b.belegId          = row["beleg_id"].AsInt64();
    b.belegfeld1       = row["belegfeld1"].AsString();
    b.belegfeld2       = row["belegfeld2"].AsString();
    b.umsatz           = MoneyFrom(row["umsatz"], waehrung);
    SollHabenFromText(row["soll_haben"].AsString(), b.sollHaben);
    b.konto            = row["konto"].AsString();
    b.gegenkonto       = row["gegenkonto"].AsString();
    b.buSchluessel     = row["bu_schluessel"].AsString();
    b.steuerschluessel = row["steuerschluessel"].AsString();
    SteuerSeiteFromText(row["steuer_seite"].AsString(), b.steuerSeite);
    b.satzPromille     = row["satz_promille"].AsInt();
    b.netto            = MoneyFrom(row["netto"],  waehrung);
    b.steuer           = MoneyFrom(row["steuer"], waehrung);
    b.steuerkonto      = row["steuerkonto"].AsString();
    b.buchungstext     = row["buchungstext"].AsString();
    b.kost1            = row["kost1"].AsString();
    b.kost2            = row["kost2"].AsString();
    b.waehrung         = waehrung.empty() ? "EUR" : waehrung;
    b.stornoVon        = row["storno_von"].AsInt64();
    b.storniertDurch   = row["storniert_durch"].AsInt64();
    b.festgeschrieben  = row["festgeschrieben"].AsInt() != 0;
    b.erfasstVon       = row["erfasst_von"].AsInt64();
    b.erfasstVonName   = row["erfasst_von_name"].AsString();
    b.erfasstAm        = row["erfasst_am"].AsInt64();
    b.laufendeNummer   = row["laufende_nummer"].AsInt64();
    b.prevHash         = row["prev_hash"].AsString();
    b.hash             = row["hash"].AsString();
    return b;
}

const char* const kBuchungSpalten =
    "id, mandant_id, geschaeftsjahr_id, periode, belegdatum, beleg_id,"
    " belegfeld1, belegfeld2, umsatz, soll_haben, konto, gegenkonto,"
    " bu_schluessel, steuerschluessel, steuer_seite, satz_promille, netto,"
    " steuer, steuerkonto, buchungstext, kost1, kost2, waehrung, storno_von,"
    " storniert_durch, festgeschrieben, erfasst_von, erfasst_von_name,"
    " erfasst_am, laufende_nummer, prev_hash, hash";

// ---- In-transaction helpers ----
// UltraDatabase has no nested transactions, so posting a document - which
// allocates a number, writes a header, its positions and several journal rows -
// has to do all of it through one handle. These are the pieces of the public
// calls that had to be re-expressed that way; they exist so that a failure
// halfway through leaves nothing behind rather than half an invoice.

bool NextSequenzInTx(UltraDbHandle tx, const std::string& name, int64_t& out,
                     std::string& fehler, const std::string& rowLock) {
    UltraDbResultSet rs;
    // **The row is locked while it is read**, or two clients read the same
    // value and both write back the same successor. On SQLite the suffix is
    // empty because BEGIN IMMEDIATE already serialises writers; on PostgreSQL
    // it is FOR UPDATE. Without it, two people posting at once get the same
    // id - which was exactly what happened the first time this ran against a
    // real server with two processes.
    const UltraDbResult read = UltraDb_QueryInTx(
        tx, "SELECT naechste FROM sequenz WHERE name = ?" + rowLock, { name }, rs);
    if (!read) { fehler = read.message; return false; }

    if (rs.Empty()) {
        const UltraDbResult insert = UltraDb_ExecInTx(
            tx, "INSERT INTO sequenz(name, naechste) VALUES(?, ?)", { name, int64_t(2) });
        if (!insert) { fehler = insert.message; return false; }
        out = 1;
        return true;
    }
    out = rs.Row(0)["naechste"].AsInt64();
    const UltraDbResult bump = UltraDb_ExecInTx(
        tx, "UPDATE sequenz SET naechste = naechste + 1 WHERE name = ?", { name });
    if (!bump) { fehler = bump.message; return false; }
    return true;
}

bool NextBelegnummerInTx(UltraDbHandle tx, int64_t mandantId, const std::string& kreisName,
                         const Date& datum, std::string& out, std::string& fehler,
                         const std::string& rowLock) {
    UltraDbResultSet rs;
    // Locked for the same reason as the sequence above: a document number
    // handed to two people is the failure that makes multi-user bookkeeping
    // unusable, and a gap-free numbering cannot be repaired afterwards.
    const UltraDbResult read = UltraDb_QueryInTx(
        tx,
        "SELECT id, kreis, praefix, naechste, stellen, jaehrlich_zuruecksetzen,"
        " letztes_jahr FROM nummernkreis WHERE mandant_id = ? AND kreis = ?" + rowLock,
        { mandantId, kreisName }, rs);
    if (!read) { fehler = read.message; return false; }
    if (rs.Empty()) {
        fehler = "Der Nummernkreis \"" + kreisName + "\" ist nicht angelegt.";
        return false;
    }

    const UltraDbRow& row = rs.Row(0);
    Nummernkreis kreis;
    kreis.id        = row["id"].AsInt64();
    kreis.mandantId = mandantId;
    kreis.kreis     = row["kreis"].AsString();
    kreis.praefix   = row["praefix"].AsString();
    kreis.naechste  = row["naechste"].AsInt64();
    kreis.stellen   = row["stellen"].AsInt();
    kreis.jaehrlichZuruecksetzen = row["jaehrlich_zuruecksetzen"].AsInt() != 0;
    kreis.letztesJahr = row["letztes_jahr"].AsInt();

    int64_t wert = kreis.naechste;
    if (kreis.jaehrlichZuruecksetzen && kreis.letztesJahr != datum.year) wert = 1;

    const UltraDbResult bump = UltraDb_ExecInTx(
        tx, "UPDATE nummernkreis SET naechste = ?, letztes_jahr = ? WHERE id = ?",
        { wert + 1, datum.year, kreis.id });
    if (!bump) { fehler = bump.message; return false; }

    out = FormatNummer(kreis, wert, datum);
    return true;
}

bool AuditInTx(UltraDbHandle tx, const Akteur& akteur, const std::string& tabelle,
               int64_t rowId, const std::string& aktion, const std::string& details,
               std::string& fehler, const std::string& rowLock) {
    int64_t id = 0;
    if (!NextSequenzInTx(tx, "audit", id, fehler, rowLock)) return false;
    const UltraDbResult inserted = UltraDb_ExecInTx(
        tx,
        "INSERT INTO audit(id, zeit, benutzer_id, benutzer, tabelle, row_id, aktion,"
        " details) VALUES(?,?,?,?,?,?,?,?)",
        { id, NowSeconds(), akteur.benutzerId, akteur.anmeldename, tabelle, rowId,
          aktion, details });
    if (!inserted) { fehler = inserted.message; return false; }
    return true;
}

// Append one posting to the Mandant's chain. Allocates the id and the
// laufende Nummer, reads the chain head, computes the hash over the finished
// row, and inserts. Everything that decides the hash is settled before it is
// taken, which is why this is one function and not three.
bool InsertBuchungInTx(UltraDbHandle tx, Buchung& b, std::string& fehler,
                       const std::string& rowLock) {
    if (!NextSequenzInTx(tx, "buchung", b.id, fehler, rowLock)) return false;

    UltraDbResultSet head;
    const UltraDbResult read = UltraDb_QueryInTx(
        tx,
        "SELECT laufende_nummer, hash FROM buchung WHERE mandant_id = ?"
        " ORDER BY laufende_nummer DESC LIMIT 1",
        { b.mandantId }, head);
    if (!read) { fehler = read.message; return false; }

    if (head.Empty()) {
        b.laufendeNummer = 1;
        b.prevHash.clear();
    } else {
        b.laufendeNummer = head.Row(0)["laufende_nummer"].AsInt64() + 1;
        b.prevHash       = head.Row(0)["hash"].AsString();
    }

    b.hash = BerechneHash(b.prevHash, b);
    if (b.hash.empty()) {
        fehler = "Die Prüfsumme der Buchung konnte nicht berechnet werden.";
        return false;
    }

    const UltraDbResult inserted = UltraDb_ExecInTx(
        tx,
        "INSERT INTO buchung(id, mandant_id, geschaeftsjahr_id, periode, belegdatum,"
        " beleg_id, belegfeld1, belegfeld2, umsatz, soll_haben, konto, gegenkonto,"
        " bu_schluessel, steuerschluessel, steuer_seite, satz_promille, netto, steuer,"
        " steuerkonto, buchungstext, kost1, kost2, waehrung, storno_von,"
        " storniert_durch, festgeschrieben, erfasst_von, erfasst_von_name, erfasst_am,"
        " laufende_nummer, prev_hash, hash)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        { b.id, b.mandantId, b.geschaeftsjahrId, b.periode, b.belegdatum.ToIso(),
          b.belegId, b.belegfeld1, b.belegfeld2, b.umsatz.Minor(),
          SollHabenToText(b.sollHaben), b.konto, b.gegenkonto, b.buSchluessel,
          b.steuerschluessel, SteuerSeiteToText(b.steuerSeite), b.satzPromille,
          b.netto.Minor(), b.steuer.Minor(), b.steuerkonto, b.buchungstext,
          b.kost1, b.kost2, b.waehrung, b.stornoVon, b.storniertDurch,
          b.festgeschrieben ? 1 : 0, b.erfasstVon, b.erfasstVonName, b.erfasstAm,
          b.laufendeNummer, b.prevHash, b.hash });
    if (!inserted) { fehler = inserted.message; return false; }
    return true;
}

} // namespace

// ===== OPENING =====

// The migrations, in one place.
//
// Local and server mode run **this same list**. That is the whole of the
// "one schema, two storage modes" claim, and keeping one list is what makes
// it true rather than aspirational: a step added for SQLite and forgotten for
// PostgreSQL is a database that silently lacks a table.
static std::vector<UltraDbMigration> MigrationSchritte() {
    return {
        { 1, "UltraFIBU Stammdaten", kSchemaV1 },
        { 2, "UltraFIBU Belege und Buchungen", kSchemaV2 },
        { 3, "UltraFIBU DATEV-Importprotokoll", kSchemaV3 },
        { 4, "UltraFIBU Bank: Konten, Umsaetze, Zuordnungen", kSchemaV4 },
        { 5, "UltraFIBU Steuermeldungen", kSchemaV5 },
        { 6, "UltraFIBU EU-Steuersaetze", kSchemaV6 },
        { 7, "UltraFIBU Brutto-Erfassung und Leistungszeitpunkt", kSchemaV7 },
        { 8, "UltraFIBU Kontenrahmen: Funktion, Abschlusszweck, Programmverbindung", kSchemaV8 }
    };
}

std::string Store::RowLock() const {
    if (connection_.empty()) return std::string();
    return UltraDb_RowLockSuffix(connection_);
}

bool DateiExistiert(const std::string& pfad) {
    if (pfad.empty()) return false;
    struct stat st;
    return ::stat(pfad.c_str(), &st) == 0;
}

StoreResult Store::Open(const std::string& connectionName, const std::string& databasePath,
                        bool anlegen) {
    datenbankPfad_ = databasePath;
    // Re-registering a name replaces the pooled entry and drops the physical
    // connection - which for ":memory:" would throw the database away. So it
    // only happens when the name is new or now points somewhere else.
    bool needsRegistration = true;
    if (UltraDb_HasConnection(connectionName)) {
        UltraDbConnectionInfo info;
        if (UltraDb_GetConnectionInfo(connectionName, info) && info.database == databasePath)
            needsRegistration = false;
    }
    if (needsRegistration) {
        // Only here, where this store itself registers a SQLite file: a
        // connection registered beforehand - a PostgreSQL server - names a
        // database, not a path, and has nothing to be missing on disk.
        if (!anlegen && databasePath != ":memory:" && !DateiExistiert(databasePath))
            return StoreResult::Fail(
                "Die Datei \"" + databasePath + "\" gibt es nicht. Eine neue "
                "Buchhaltung wird eingerichtet, nicht durch Öffnen angelegt.");
        UltraDbConnectionConfig cfg;
        cfg.name     = connectionName;
        cfg.driver   = "sqlite";
        cfg.database = databasePath;
        const UltraDbResult reg = UltraDb_RegisterConnection(cfg);
        if (!reg)
            return StoreResult::Fail("Die Datenbank konnte nicht geöffnet werden: " +
                                     reg.message);
    }
    connection_ = connectionName;

    // An old binary must never write into a newer schema: the columns it does
    // not know about would silently stay empty, and in a ledger that is
    // corruption rather than inconvenience.
    const int existing = SchemaVersion();
    if (existing > kSchemaVersion) {
        const std::string fehler =
            "Die Datenbank hat Schema-Version " + Number(existing) +
            ", dieses Programm kennt nur " + Number(kSchemaVersion) +
            ". Bitte UltraFIBU aktualisieren.";
        connection_.clear();
        return StoreResult::Fail(fehler);
    }

    const UltraDbResult migrated = UltraDb_Migrate(connection_, MigrationSchritte());
    if (!migrated) {
        connection_.clear();
        return StoreResult::Fail("Das Datenbank-Schema konnte nicht angelegt werden: " +
                                 migrated.message);
    }
    return StoreResult::Ok();
}

StoreResult Store::OpenServer(const std::string& connectionName,
                              const std::string& host, int port,
                              const std::string& database,
                              const std::string& user,
                              const std::string& credentialsRef) {
    if (host.empty() || database.empty() || user.empty())
        return StoreResult::Fail("Für den Mehrplatz-Betrieb werden Host, Datenbank "
                                 "und Benutzer gebraucht.");
    // **A password never comes from a configuration file.** It would end up in
    // a backup, a log or a screenshot; the key names an UltraVault entry and
    // the driver resolves it.
    if (credentialsRef.rfind("vault:", 0) != 0)
        return StoreResult::Fail("Server-Zugangsdaten müssen als UltraVault-Schlüssel "
                                 "angegeben werden (vault:...), nicht als Passwort.");

    const std::vector<std::string> treiber = UltraDatabase_GetSupportedDrivers();
    bool hatPostgres = false;
    for (const std::string& t : treiber)
        if (t == "postgresql") hatPostgres = true;
    if (!hatPostgres) {
        // Explicit rather than a silent fallback to SQLite: an installation
        // that quietly became single-user would be discovered by two people
        // overwriting each other's work.
        return StoreResult::Fail(
            "Der PostgreSQL-Treiber ist in diesem Build nicht enthalten (libpq "
            "fehlte beim Übersetzen). Das Schema ist dasselbe wie lokal, ein "
            "Wechsel erfordert also keine Änderung an den Daten - aber dieser "
            "Build kann sich nicht mit einem Server verbinden.");
    }

    datenbankPfad_ = database;

    UltraDbConnectionConfig cfg;
    cfg.name        = connectionName;
    cfg.driver      = "postgresql";
    cfg.database    = database;
    cfg.host        = host;
    cfg.port        = port > 0 ? port : 5432;
    cfg.user        = user;
    cfg.credentials = credentialsRef;
    // Verified TLS by default. A shared accounting database is exactly the
    // case where a downgrade has to be a deliberate act rather than a default.
    cfg.tls         = UltraDbTls::VerifyFull;

    UltraDbResult registriert = UltraDb_RegisterConnection(cfg);
    if (!registriert)
        return StoreResult::Fail("Die Serververbindung konnte nicht eingerichtet "
                                 "werden: " + registriert.message);

    connection_ = connectionName;
    UltraDbResult geoeffnet = UltraDb_OpenConnection(connectionName);
    if (!geoeffnet) {
        connection_.clear();
        return StoreResult::Fail("Der Server ist nicht erreichbar: " +
                                 geoeffnet.message);
    }

    // The same migrations as locally. That they run unchanged is the whole
    // claim of "one schema, two storage modes", and it is checked here rather
    // than asserted.
    const std::vector<UltraDbMigration> steps = MigrationSchritte();
    const UltraDbResult migrated = UltraDb_Migrate(connection_, steps);
    if (!migrated) {
        connection_.clear();
        return StoreResult::Fail("Das Schema konnte auf dem Server nicht angelegt "
                                 "werden: " + migrated.message);
    }
    if (SchemaIsNewerThanCode()) {
        const int version = SchemaVersion();
        connection_.clear();
        return StoreResult::Fail(
            "Die Datenbank hat Schema-Version " + Number(version) +
            ", dieses Programm kennt nur " + Number(kSchemaVersion) +
            ". Ein älteres Programm darf nicht in ein neueres Schema schreiben - "
            "bitte dieses Programm aktualisieren.");
    }
    return StoreResult::Ok();
}

void Store::Close() {
    if (connection_.empty()) return;
    UltraDb_CloseConnection(connection_);
    connection_.clear();
}

int Store::SchemaVersion() const {
    if (connection_.empty()) return 0;
    int version = 0;
    if (!UltraDb_GetSchemaVersion(connection_, version)) return 0;
    return version;
}

// ===== QUERY HELPERS =====

StoreResult Store::Exec(const std::string& sql, const UltraDbParams& params,
                        const std::string& wobei) const {
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");
    const UltraDbResult result = UltraDb_Exec(connection_, sql, params);
    if (!result) return StoreResult::Fail(wobei + ": " + result.message);
    return StoreResult::Ok();
}

bool Store::Query(const std::string& sql, const UltraDbParams& params,
                  UltraDbResultSet& out) const {
    if (connection_.empty()) return false;
    return static_cast<bool>(UltraDb_Query(connection_, sql, params, out));
}

bool Store::QueryOne(const std::string& sql, const UltraDbParams& params,
                     UltraDbRow& out) const {
    UltraDbResultSet rs;
    if (!Query(sql, params, rs) || rs.Empty()) return false;
    out = rs.Row(0);
    return true;
}

// ===== SEQUENCES AND NUMBERS =====

StoreResult Store::NextSequenceValue(const std::string& name, int64_t& out) {
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");

    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(connection_, &error);
    if (tx == UltraDbInvalidHandle)
        return StoreResult::Fail("Transaktion konnte nicht gestartet werden: " + error.message);

    // Deliberately the same code as the posting path uses, rather than a second
    // copy of it. The copy that used to stand here had no row lock, so two
    // clients on one PostgreSQL server read the same counter and both wrote
    // back the same successor - and the duplicate only appeared under real
    // concurrency, long after the "fix" that had touched the other copy.
    std::string fehler;
    if (!NextSequenzInTx(tx, name, out, fehler, RowLock())) {
        UltraDb_Rollback(tx);
        return StoreResult::Fail("Nummernvergabe fehlgeschlagen: " + fehler);
    }

    const UltraDbResult commit = UltraDb_Commit(tx);
    if (!commit) return StoreResult::Fail("Nummernvergabe nicht bestätigt: " + commit.message);
    return StoreResult::Ok();
}

StoreResult Store::NextBelegnummer(int64_t mandantId, const std::string& kreisName,
                                   const Date& datum, std::string& out) {
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");
    if (!datum.Valid()) return StoreResult::Fail("Ohne gültiges Datum kann keine Nummer "
                                                 "vergeben werden.");

    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(connection_, &error);
    if (tx == UltraDbInvalidHandle)
        return StoreResult::Fail("Transaktion konnte nicht gestartet werden: " + error.message);

    // Same shared implementation, same reason as above: a document number
    // handed to two people cannot be repaired afterwards, so there is exactly
    // one place that hands one out.
    std::string fehler;
    if (!NextBelegnummerInTx(tx, mandantId, kreisName, datum, out, fehler, RowLock())) {
        UltraDb_Rollback(tx);
        return StoreResult::Fail(fehler);
    }

    const UltraDbResult commit = UltraDb_Commit(tx);
    if (!commit) return StoreResult::Fail("Nummernvergabe nicht bestätigt: " + commit.message);
    return StoreResult::Ok();
}

StoreResult Store::SaveNummernkreis(Nummernkreis& kreis, const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Nummernkreise ändern.");
    if (!kreis.Valid()) return StoreResult::Fail("Der Nummernkreis braucht einen Namen.");

    if (kreis.id == 0) {
        const StoreResult seq = NextSequenceValue("nummernkreis", kreis.id);
        if (!seq) return seq;
        const StoreResult inserted = Exec(
            "INSERT INTO nummernkreis(id, mandant_id, kreis, praefix, naechste, stellen,"
            " jaehrlich_zuruecksetzen, letztes_jahr) VALUES(?,?,?,?,?,?,?,?)",
            { kreis.id, kreis.mandantId, kreis.kreis, kreis.praefix, kreis.naechste,
              kreis.stellen, kreis.jaehrlichZuruecksetzen ? 1 : 0, kreis.letztesJahr },
            "Nummernkreis konnte nicht angelegt werden");
        if (!inserted) return inserted;
    } else {
        const StoreResult updated = Exec(
            "UPDATE nummernkreis SET praefix = ?, stellen = ?, jaehrlich_zuruecksetzen = ? "
            "WHERE id = ?",
            { kreis.praefix, kreis.stellen, kreis.jaehrlichZuruecksetzen ? 1 : 0, kreis.id },
            "Nummernkreis konnte nicht geändert werden");
        if (!updated) return updated;
    }
    return WriteAudit(akteur, "nummernkreis", kreis.id, "speichern", kreis.kreis);
}

std::vector<Nummernkreis> Store::Nummernkreise(int64_t mandantId) const {
    std::vector<Nummernkreis> liste;
    UltraDbResultSet rs;
    if (!Query("SELECT id, mandant_id, kreis, praefix, naechste, stellen,"
               " jaehrlich_zuruecksetzen, letztes_jahr FROM nummernkreis "
               "WHERE mandant_id = ? ORDER BY kreis", { mandantId }, rs))
        return liste;
    for (const UltraDbRow& row : rs) {
        Nummernkreis kreis;
        kreis.id        = row["id"].AsInt64();
        kreis.mandantId = row["mandant_id"].AsInt64();
        kreis.kreis     = row["kreis"].AsString();
        kreis.praefix   = row["praefix"].AsString();
        kreis.naechste  = row["naechste"].AsInt64();
        kreis.stellen   = row["stellen"].AsInt();
        kreis.jaehrlichZuruecksetzen = row["jaehrlich_zuruecksetzen"].AsInt() != 0;
        kreis.letztesJahr = row["letztes_jahr"].AsInt();
        liste.push_back(kreis);
    }
    return liste;
}

// ===== AUDIT =====

StoreResult Store::WriteAudit(const Akteur& akteur, const std::string& tabelle,
                              int64_t rowId, const std::string& aktion,
                              const std::string& details) {
    int64_t id = 0;
    const StoreResult seq = NextSequenceValue("audit", id);
    if (!seq) return seq;
    return Exec("INSERT INTO audit(id, zeit, benutzer_id, benutzer, tabelle, row_id, aktion,"
                " details) VALUES(?,?,?,?,?,?,?,?)",
                { id, NowSeconds(), akteur.benutzerId, akteur.anmeldename, tabelle, rowId,
                  aktion, details },
                "Der Protokolleintrag konnte nicht geschrieben werden");
}

std::vector<Store::AuditEintrag> Store::AuditListe(size_t limit) const {
    std::vector<AuditEintrag> liste;
    UltraDbResultSet rs;
    if (!Query("SELECT id, zeit, benutzer_id, benutzer, tabelle, row_id, aktion, details "
               "FROM audit ORDER BY zeit DESC, id DESC LIMIT ?",
               { static_cast<int64_t>(limit) }, rs))
        return liste;
    for (const UltraDbRow& row : rs) {
        AuditEintrag eintrag;
        eintrag.id         = row["id"].AsInt64();
        eintrag.zeit       = row["zeit"].AsInt64();
        eintrag.benutzerId = row["benutzer_id"].AsInt64();
        eintrag.benutzer   = row["benutzer"].AsString();
        eintrag.tabelle    = row["tabelle"].AsString();
        eintrag.rowId      = row["row_id"].AsInt64();
        eintrag.aktion     = row["aktion"].AsString();
        eintrag.details    = row["details"].AsString();
        liste.push_back(eintrag);
    }
    return liste;
}

// ===== MANDANT =====

StoreResult Store::SaveMandant(Mandant& mandant, const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Stammdaten ändern.");
    if (!mandant.Valid()) return StoreResult::Fail("Der Mandant braucht einen Namen.");

    const UltraDbParams felder = {
        mandant.name, mandant.rechtsform, mandant.strasse, mandant.plz, mandant.ort,
        mandant.land, mandant.steuernummer, mandant.ustIdNr, mandant.finanzamt,
        mandant.finanzamtNummer, mandant.beraternummer, mandant.mandantennummer,
        mandant.waehrung, BesteuerungsartToText(mandant.besteuerung),
        GewinnermittlungToText(mandant.gewinnermittlung),
        UstvaRhythmusToText(mandant.ustvaRhythmus),
        mandant.dauerfristverlaengerung ? 1 : 0, mandant.kleinunternehmer ? 1 : 0,
        mandant.ossRegistriert ? 1 : 0, mandant.iossRegistriert ? 1 : 0,
        mandant.telefon, mandant.email, mandant.webseite,
        mandant.iban, mandant.bic, mandant.bank
    };

    if (mandant.id == 0) {
        const StoreResult seq = NextSequenceValue("mandant", mandant.id);
        if (!seq) return seq;
        UltraDbParams params = { mandant.id };
        params.insert(params.end(), felder.begin(), felder.end());
        const StoreResult inserted = Exec(
            "INSERT INTO mandant(id, name, rechtsform, strasse, plz, ort, land, steuernummer,"
            " ust_idnr, finanzamt, finanzamt_nummer, beraternummer, mandantennummer, waehrung,"
            " besteuerung, gewinnermittlung, ustva_rhythmus, dauerfristverlaengerung,"
            " kleinunternehmer, oss_registriert, ioss_registriert, telefon, email, webseite,"
            " iban, bic, bank) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            params, "Der Mandant konnte nicht angelegt werden");
        if (!inserted) return inserted;
    } else {
        UltraDbParams params = felder;
        params.push_back(mandant.id);
        const StoreResult updated = Exec(
            "UPDATE mandant SET name = ?, rechtsform = ?, strasse = ?, plz = ?, ort = ?,"
            " land = ?, steuernummer = ?, ust_idnr = ?, finanzamt = ?, finanzamt_nummer = ?,"
            " beraternummer = ?, mandantennummer = ?, waehrung = ?, besteuerung = ?,"
            " gewinnermittlung = ?, ustva_rhythmus = ?, dauerfristverlaengerung = ?,"
            " kleinunternehmer = ?, oss_registriert = ?, ioss_registriert = ?, telefon = ?,"
            " email = ?, webseite = ?, iban = ?, bic = ?, bank = ? WHERE id = ?",
            params, "Der Mandant konnte nicht geändert werden");
        if (!updated) return updated;
    }
    return WriteAudit(akteur, "mandant", mandant.id, "speichern", mandant.name);
}

namespace {

Mandant MandantFromRow(const UltraDbRow& row) {
    Mandant m;
    m.id              = row["id"].AsInt64();
    m.name            = row["name"].AsString();
    m.rechtsform      = row["rechtsform"].AsString();
    m.strasse         = row["strasse"].AsString();
    m.plz             = row["plz"].AsString();
    m.ort             = row["ort"].AsString();
    m.land            = row["land"].AsString();
    m.steuernummer    = row["steuernummer"].AsString();
    m.ustIdNr         = row["ust_idnr"].AsString();
    m.finanzamt       = row["finanzamt"].AsString();
    m.finanzamtNummer = row["finanzamt_nummer"].AsString();
    m.beraternummer   = row["beraternummer"].AsString();
    m.mandantennummer = row["mandantennummer"].AsString();
    m.waehrung        = row["waehrung"].AsString();
    BesteuerungsartFromText(row["besteuerung"].AsString(), m.besteuerung);
    GewinnermittlungFromText(row["gewinnermittlung"].AsString(), m.gewinnermittlung);
    UstvaRhythmusFromText(row["ustva_rhythmus"].AsString(), m.ustvaRhythmus);
    m.dauerfristverlaengerung = row["dauerfristverlaengerung"].AsInt() != 0;
    m.kleinunternehmer        = row["kleinunternehmer"].AsInt() != 0;
    m.ossRegistriert          = row["oss_registriert"].AsInt() != 0;
    m.iossRegistriert         = row["ioss_registriert"].AsInt() != 0;
    m.telefon  = row["telefon"].AsString();
    m.email    = row["email"].AsString();
    m.webseite = row["webseite"].AsString();
    m.iban     = row["iban"].AsString();
    m.bic      = row["bic"].AsString();
    m.bank     = row["bank"].AsString();
    return m;
}

const char* const kMandantColumns =
    "id, name, rechtsform, strasse, plz, ort, land, steuernummer, ust_idnr, finanzamt,"
    " finanzamt_nummer, beraternummer, mandantennummer, waehrung, besteuerung,"
    " gewinnermittlung, ustva_rhythmus, dauerfristverlaengerung, kleinunternehmer,"
    " oss_registriert, ioss_registriert, telefon, email, webseite, iban, bic, bank";

} // namespace

bool Store::LoadMandant(int64_t id, Mandant& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kMandantColumns + " FROM mandant WHERE id = ?",
                  { id }, row))
        return false;
    out = MandantFromRow(row);
    return true;
}

std::vector<Mandant> Store::Mandanten() const {
    std::vector<Mandant> liste;
    UltraDbResultSet rs;
    if (!Query(std::string("SELECT ") + kMandantColumns + " FROM mandant ORDER BY name", {}, rs))
        return liste;
    for (const UltraDbRow& row : rs) liste.push_back(MandantFromRow(row));
    return liste;
}

// ===== GESCHAEFTSJAHR =====

namespace {

Geschaeftsjahr GeschaeftsjahrFromRow(const UltraDbRow& row) {
    Geschaeftsjahr jahr;
    jahr.id        = row["id"].AsInt64();
    jahr.mandantId = row["mandant_id"].AsInt64();
    jahr.beginn    = DateFrom(row["beginn"]);
    jahr.ende      = DateFrom(row["ende"]);
    GeschaeftsjahrStatusFromText(row["status"].AsString(), jahr.status);
    jahr.festschreibungBis = DateFrom(row["festschreibung_bis"]);
    jahr.skr               = row["skr"].AsString();
    jahr.sachkontenlaenge  = row["sachkontenlaenge"].AsInt();
    jahr.bezeichnung       = row["bezeichnung"].AsString();
    return jahr;
}

const char* const kGjColumns =
    "id, mandant_id, beginn, ende, status, festschreibung_bis, skr, sachkontenlaenge,"
    " bezeichnung";

} // namespace

StoreResult Store::SaveGeschaeftsjahr(Geschaeftsjahr& jahr, const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf kein Geschäftsjahr anlegen.");
    if (!jahr.Valid())
        return StoreResult::Fail(
            "Das Geschäftsjahr muss am Monatsersten beginnen, am Monatsletzten enden "
            "und darf höchstens zwölf Monate umfassen.");
    if (jahr.bezeichnung.empty()) jahr.bezeichnung = jahr.DefaultBezeichnung();

    // No overlap, and no gap after the latest existing year: a posting that
    // belongs to two fiscal years, or to none, has no correct report.
    const std::vector<Geschaeftsjahr> vorhanden = Geschaeftsjahre(jahr.mandantId);
    const Geschaeftsjahr* letztes = nullptr;
    for (const Geschaeftsjahr& other : vorhanden) {
        if (other.id == jahr.id) continue;
        if (GeschaeftsjahreOverlap(other, jahr))
            return StoreResult::Fail("Das Geschäftsjahr überschneidet sich mit \"" +
                                     other.bezeichnung + "\" (" + FormatDateGerman(other.beginn) +
                                     " - " + FormatDateGerman(other.ende) + ").");
        if (!letztes || other.ende > letztes->ende) letztes = &other;
    }
    if (letztes && jahr.beginn > letztes->ende && !GeschaeftsjahreContiguous(*letztes, jahr))
        return StoreResult::Fail("Zwischen \"" + letztes->bezeichnung + "\" (endet " +
                                 FormatDateGerman(letztes->ende) + ") und dem neuen "
                                 "Geschäftsjahr liegt eine Lücke. "
                                 "Geschäftsjahre müssen lückenlos "
                                 "aufeinander folgen.");

    if (jahr.id == 0) {
        const StoreResult seq = NextSequenceValue("geschaeftsjahr", jahr.id);
        if (!seq) return seq;
        const StoreResult inserted = Exec(
            "INSERT INTO geschaeftsjahr(id, mandant_id, beginn, ende, status,"
            " festschreibung_bis, skr, sachkontenlaenge, bezeichnung) "
            "VALUES(?,?,?,?,?,?,?,?,?)",
            { jahr.id, jahr.mandantId, jahr.beginn.ToIso(), jahr.ende.ToIso(),
              GeschaeftsjahrStatusToText(jahr.status), DateValue(jahr.festschreibungBis),
              jahr.skr, jahr.sachkontenlaenge, jahr.bezeichnung },
            "Das Geschäftsjahr konnte nicht angelegt werden");
        if (!inserted) return inserted;
    } else {
        const StoreResult updated = Exec(
            "UPDATE geschaeftsjahr SET beginn = ?, ende = ?, status = ?, skr = ?,"
            " sachkontenlaenge = ?, bezeichnung = ? WHERE id = ?",
            { jahr.beginn.ToIso(), jahr.ende.ToIso(), GeschaeftsjahrStatusToText(jahr.status),
              jahr.skr, jahr.sachkontenlaenge, jahr.bezeichnung, jahr.id },
            "Das Geschäftsjahr konnte nicht geändert werden");
        if (!updated) return updated;
    }
    return WriteAudit(akteur, "geschaeftsjahr", jahr.id, "speichern",
                      jahr.bezeichnung + " " + jahr.beginn.ToIso() + ".." + jahr.ende.ToIso());
}

std::vector<Geschaeftsjahr> Store::Geschaeftsjahre(int64_t mandantId) const {
    std::vector<Geschaeftsjahr> liste;
    UltraDbResultSet rs;
    if (!Query(std::string("SELECT ") + kGjColumns +
               " FROM geschaeftsjahr WHERE mandant_id = ? ORDER BY beginn", { mandantId }, rs))
        return liste;
    for (const UltraDbRow& row : rs) liste.push_back(GeschaeftsjahrFromRow(row));
    return liste;
}

bool Store::GeschaeftsjahrById(int64_t id, Geschaeftsjahr& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kGjColumns + " FROM geschaeftsjahr WHERE id = ?",
                  { id }, row))
        return false;
    out = GeschaeftsjahrFromRow(row);
    return true;
}

bool Store::GeschaeftsjahrAt(int64_t mandantId, const Date& datum, Geschaeftsjahr& out) const {
    if (!datum.Valid()) return false;
    // String comparison on ISO dates is chronological, which is exactly why
    // the dates are stored this way - and it works identically on both engines.
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kGjColumns +
                  " FROM geschaeftsjahr WHERE mandant_id = ? AND beginn <= ? AND ende >= ?",
                  { mandantId, datum.ToIso(), datum.ToIso() }, row))
        return false;
    out = GeschaeftsjahrFromRow(row);
    return true;
}

StoreResult Store::Festschreiben(int64_t geschaeftsjahrId, const Date& bis,
                                 const Akteur& akteur) {
    if (!akteur.Darf(Recht::Festschreiben))
        return StoreResult::Fail("Diese Rolle darf nicht festschreiben.");

    Geschaeftsjahr jahr;
    if (!GeschaeftsjahrById(geschaeftsjahrId, jahr))
        return StoreResult::Fail("Das Geschäftsjahr wurde nicht gefunden.");
    if (!jahr.Contains(bis))
        return StoreResult::Fail("Das Datum liegt nicht im Geschäftsjahr \"" +
                                 jahr.bezeichnung + "\".");
    // Festschreibung only ever moves forward. Un-freezing is not a feature
    // that was forgotten - it is what the GoBD forbid, and a correction is a
    // Storno.
    if (jahr.festschreibungBis.Valid() && bis <= jahr.festschreibungBis) {
        const StoreResult refused = StoreResult::Fail(
            "Bis " + FormatDateGerman(jahr.festschreibungBis) +
            " ist bereits festgeschrieben. Eine Festschreibung kann nicht "
            "zurückgenommen werden - Korrekturen erfolgen über eine Stornierung.");
        WriteAudit(akteur, "geschaeftsjahr", jahr.id, "abgelehnt",
                   "Festschreibung rückwärts auf " + bis.ToIso());
        return refused;
    }

    // The date on the Geschaeftsjahr is what every write checks against, so it
    // alone would be enough to make the period immutable. The per-row markers
    // go with it because DATEV carries the same concept in its stack and a
    // Kanzlei asks per document whether it is festgeschrieben - and because a
    // row that has been exported as frozen must still read as frozen if it is
    // ever restored beside a Geschaeftsjahr row that has not been.
    //
    // All three writes are one transaction: a year marked frozen whose rows
    // were not, or the reverse, is a state nothing downstream could interpret.
    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(connection_, &error);
    if (tx == UltraDbInvalidHandle)
        return StoreResult::Fail("Transaktion konnte nicht gestartet werden: " +
                                 error.message);

    auto abbrechen = [&](const std::string& text, const std::string& detail) {
        UltraDb_Rollback(tx);
        return StoreResult::Fail(text + ": " + detail);
    };

    const UltraDbResult jahrGesetzt = UltraDb_ExecInTx(
        tx, "UPDATE geschaeftsjahr SET festschreibung_bis = ?, status = ? WHERE id = ?",
        { bis.ToIso(), GeschaeftsjahrStatusToText(GeschaeftsjahrStatus::Festgeschrieben),
          jahr.id });
    if (!jahrGesetzt)
        return abbrechen("Die Festschreibung konnte nicht gespeichert werden",
                         jahrGesetzt.message);

    const UltraDbResult buchungen = UltraDb_ExecInTx(
        tx,
        "UPDATE buchung SET festgeschrieben = 1 WHERE mandant_id = ?"
        " AND belegdatum <= ? AND festgeschrieben = 0",
        { jahr.mandantId, bis.ToIso() });
    if (!buchungen)
        return abbrechen("Die Buchungen konnten nicht festgeschrieben werden",
                         buchungen.message);

    const UltraDbResult belege = UltraDb_ExecInTx(
        tx,
        "UPDATE beleg SET festgeschrieben = 1 WHERE mandant_id = ?"
        " AND datum <= ? AND status <> ? AND festgeschrieben = 0",
        { jahr.mandantId, bis.ToIso(), BelegStatusToText(BelegStatus::Entwurf) });
    if (!belege)
        return abbrechen("Die Belege konnten nicht festgeschrieben werden",
                         belege.message);

    std::string fehler;
    if (!AuditInTx(tx, akteur, "geschaeftsjahr", jahr.id, "festschreiben", bis.ToIso(),
                   fehler, RowLock()))
        return abbrechen("Der Protokolleintrag konnte nicht geschrieben werden", fehler);

    const UltraDbResult commit = UltraDb_Commit(tx);
    if (!commit)
        return StoreResult::Fail("Die Festschreibung wurde nicht bestätigt: " +
                                 commit.message);
    return StoreResult::Ok();
}

// ===== KONTENRAHMEN =====

namespace {

Konto KontoFromRow(const UltraDbRow& row) {
    Konto konto;
    konto.id               = row["id"].AsInt64();
    konto.mandantId        = row["mandant_id"].AsInt64();
    konto.nummer           = row["nummer"].AsString();
    konto.bezeichnung      = row["bezeichnung"].AsString();
    KontoTypFromText(row["typ"].AsString(), konto.typ);
    konto.skr              = row["skr"].AsString();
    konto.steuerschluessel = row["steuerschluessel"].AsString();
    konto.eurZeile         = row["euer_zeile"].AsString();
    konto.bwaPosition      = row["bwa_position"].AsString();
    konto.bilanzPosition   = row["bilanz_position"].AsString();
    konto.funktion           = row["funktion"].AsString();
    konto.abschlusszweck     = row["abschlusszweck"].AsString();
    konto.programmverbindung = row["programmverbindung"].AsString();
    konto.nummerBis          = row["nummer_bis"].AsString();
    konto.aktiv            = row["aktiv"].AsInt() != 0;
    konto.notiz            = row["notiz"].AsString();
    return konto;
}

const char* const kKontoColumns =
    "id, mandant_id, nummer, bezeichnung, typ, skr, steuerschluessel, euer_zeile,"
    " bwa_position, bilanz_position, funktion, abschlusszweck, programmverbindung,"
    " nummer_bis, aktiv, notiz";

} // namespace

StoreResult Store::SaveKonto(Konto& konto, const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf den Kontenrahmen nicht ändern.");
    if (!konto.Valid())
        return StoreResult::Fail("Ein Konto braucht Nummer und Bezeichnung.");

    Konto vorhanden;
    if (konto.id == 0 && KontoByNummer(konto.mandantId, konto.nummer, vorhanden))
        konto.id = vorhanden.id;                  // same number, same account

    if (konto.id == 0) {
        const StoreResult seq = NextSequenceValue("konto", konto.id);
        if (!seq) return seq;
        return Exec(
            "INSERT INTO konto(id, mandant_id, nummer, bezeichnung, typ, skr,"
            " steuerschluessel, euer_zeile, bwa_position, bilanz_position,"
            " funktion, abschlusszweck, programmverbindung, nummer_bis, aktiv, notiz) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            { konto.id, konto.mandantId, konto.nummer, konto.bezeichnung,
              KontoTypToText(konto.typ), konto.skr, konto.steuerschluessel, konto.eurZeile,
              konto.bwaPosition, konto.bilanzPosition, konto.funktion,
              konto.abschlusszweck, konto.programmverbindung, konto.nummerBis,
              konto.aktiv ? 1 : 0, konto.notiz },
            "Das Konto konnte nicht angelegt werden");
    }
    return Exec(
        "UPDATE konto SET bezeichnung = ?, typ = ?, skr = ?, steuerschluessel = ?,"
        " euer_zeile = ?, bwa_position = ?, bilanz_position = ?, funktion = ?,"
        " abschlusszweck = ?, programmverbindung = ?, nummer_bis = ?,"
        " aktiv = ?, notiz = ? "
        "WHERE id = ?",
        { konto.bezeichnung, KontoTypToText(konto.typ), konto.skr, konto.steuerschluessel,
          konto.eurZeile, konto.bwaPosition, konto.bilanzPosition, konto.funktion,
          konto.abschlusszweck, konto.programmverbindung, konto.nummerBis,
          konto.aktiv ? 1 : 0, konto.notiz, konto.id },
        "Das Konto konnte nicht geändert werden");
}

StoreResult Store::ImportKonten(int64_t mandantId, const std::vector<Konto>& konten,
                                const Akteur& akteur, int& outWritten) {
    outWritten = 0;
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keinen Kontenrahmen importieren.");

    for (const Konto& source : konten) {
        Konto konto = source;
        konto.mandantId = mandantId;
        const StoreResult saved = SaveKonto(konto, akteur);
        if (!saved)
            return StoreResult::Fail("Konto " + source.nummer + ": " + saved.fehler);
        ++outWritten;
    }
    return WriteAudit(akteur, "konto", 0, "import",
                      "Kontenrahmen: " + std::to_string(outWritten) + " Konten");
}

std::vector<Konto> Store::Konten(int64_t mandantId) const {
    std::vector<Konto> liste;
    UltraDbResultSet rs;
    if (!Query(std::string("SELECT ") + kKontoColumns +
               " FROM konto WHERE mandant_id = ? ORDER BY nummer", { mandantId }, rs))
        return liste;
    for (const UltraDbRow& row : rs) liste.push_back(KontoFromRow(row));
    return liste;
}

bool Store::KontoByNummer(int64_t mandantId, const std::string& nummer, Konto& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kKontoColumns +
                  " FROM konto WHERE mandant_id = ? AND nummer = ?", { mandantId, nummer }, row))
        return false;
    out = KontoFromRow(row);
    return true;
}

// ===== STEUERSCHLUESSEL =====

namespace {

Steuerschluessel SteuerschluesselFromRow(const UltraDbRow& row) {
    Steuerschluessel key;
    key.id           = row["id"].AsInt64();
    key.mandantId    = row["mandant_id"].AsInt64();
    key.schluessel   = row["schluessel"].AsString();
    key.bezeichnung  = row["bezeichnung"].AsString();
    SteuerArtFromText(row["art"].AsString(), key.art);
    key.satzPromille = row["satz_promille"].AsInt();
    key.land         = row["land"].AsString();
    key.vorsteuer    = row["vorsteuer"].AsInt() != 0;
    key.datevBu      = row["datev_bu"].AsString();
    key.kzBemessung  = row["kz_bemessung"].AsString();
    key.kzSteuer     = row["kz_steuer"].AsString();
    key.kontoUmsatz  = row["konto_umsatz"].AsString();
    key.kontoSteuer  = row["konto_steuer"].AsString();
    key.gueltigVon   = DateFrom(row["gueltig_von"]);
    key.gueltigBis   = DateFrom(row["gueltig_bis"]);
    return key;
}

const char* const kSteuerColumns =
    "id, mandant_id, schluessel, bezeichnung, art, satz_promille, land, vorsteuer,"
    " datev_bu, kz_bemessung, kz_steuer, konto_umsatz, konto_steuer, gueltig_von, gueltig_bis";

} // namespace

StoreResult Store::SaveSteuerschluessel(Steuerschluessel& key, const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Steuerschlüssel ändern.");
    if (!key.Valid()) return StoreResult::Fail("Ein Steuerschlüssel braucht einen Schlüssel.");

    // The same key with the same validity start is the same row: a rate change
    // is a new row with a new gueltig_von, which is what keeps a closed year
    // reportable after the change.
    if (key.id == 0) {
        UltraDbRow row;
        if (QueryOne(std::string("SELECT ") + kSteuerColumns +
                     " FROM steuerschluessel WHERE mandant_id = ? AND schluessel = ?"
                     " AND gueltig_von IS ?",
                     { key.mandantId, key.schluessel, DateValue(key.gueltigVon) }, row))
            key.id = row["id"].AsInt64();
    }

    if (key.id == 0) {
        const StoreResult seq = NextSequenceValue("steuerschluessel", key.id);
        if (!seq) return seq;
        return Exec(
            "INSERT INTO steuerschluessel(id, mandant_id, schluessel, bezeichnung, art,"
            " satz_promille, land, vorsteuer, datev_bu, kz_bemessung, kz_steuer, konto_umsatz,"
            " konto_steuer, gueltig_von, gueltig_bis) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            { key.id, key.mandantId, key.schluessel, key.bezeichnung, SteuerArtToText(key.art),
              key.satzPromille, key.land, key.vorsteuer ? 1 : 0, key.datevBu, key.kzBemessung,
              key.kzSteuer, key.kontoUmsatz, key.kontoSteuer, DateValue(key.gueltigVon),
              DateValue(key.gueltigBis) },
            "Der Steuerschlüssel konnte nicht angelegt werden");
    }
    return Exec(
        "UPDATE steuerschluessel SET bezeichnung = ?, art = ?, satz_promille = ?, land = ?,"
        " vorsteuer = ?, datev_bu = ?, kz_bemessung = ?, kz_steuer = ?, konto_umsatz = ?,"
        " konto_steuer = ?, gueltig_bis = ? WHERE id = ?",
        { key.bezeichnung, SteuerArtToText(key.art), key.satzPromille, key.land,
          key.vorsteuer ? 1 : 0, key.datevBu, key.kzBemessung, key.kzSteuer, key.kontoUmsatz,
          key.kontoSteuer, DateValue(key.gueltigBis), key.id },
        "Der Steuerschlüssel konnte nicht geändert werden");
}

StoreResult Store::ImportSteuerschluessel(int64_t mandantId,
                                          const std::vector<Steuerschluessel>& schluessel,
                                          const Akteur& akteur, int& outWritten) {
    outWritten = 0;
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Steuerschlüssel importieren.");
    for (const Steuerschluessel& source : schluessel) {
        Steuerschluessel key = source;
        key.mandantId = mandantId;
        const StoreResult saved = SaveSteuerschluessel(key, akteur);
        if (!saved)
            return StoreResult::Fail("Steuerschlüssel " + source.schluessel + ": " +
                                     saved.fehler);
        ++outWritten;
    }
    return WriteAudit(akteur, "steuerschluessel", 0, "import",
                      std::to_string(outWritten) + " Schlüssel");
}

std::vector<Steuerschluessel> Store::SteuerschluesselListe(int64_t mandantId,
                                                           const Date& gueltigAm) const {
    std::vector<Steuerschluessel> liste;
    UltraDbResultSet rs;
    if (!Query(std::string("SELECT ") + kSteuerColumns +
               " FROM steuerschluessel WHERE mandant_id = ? ORDER BY schluessel, gueltig_von",
               { mandantId }, rs))
        return liste;
    for (const UltraDbRow& row : rs) {
        const Steuerschluessel key = SteuerschluesselFromRow(row);
        // Validity is decided in C++, not in SQL: the rule involves an open
        // end and an unset start, and expressing that in portable SQL costs
        // more than it saves.
        if (gueltigAm.Valid() && !key.GueltigAm(gueltigAm)) continue;
        liste.push_back(key);
    }
    return liste;
}

bool Store::SteuerschluesselByKey(int64_t mandantId, const std::string& schluessel,
                                  const Date& gueltigAm, Steuerschluessel& out) const {
    const std::vector<Steuerschluessel> liste = SteuerschluesselListe(mandantId, gueltigAm);
    for (const Steuerschluessel& key : liste) {
        if (key.schluessel == schluessel) { out = key; return true; }
    }
    return false;
}

bool Store::SteuerschluesselById(int64_t id, Steuerschluessel& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kSteuerColumns +
                  " FROM steuerschluessel WHERE id = ?", { id }, row))
        return false;
    out = SteuerschluesselFromRow(row);
    return true;
}

// ===== STEUERSCHLUESSEL BEARBEITEN =====

namespace {

// Uppercased ISO country code, for a field a user types by hand. Used by the tax
// keys below and the EU rates further down.
std::string LandNormal(const std::string& land) {
    std::string out;
    for (const char c : land)
        if (!std::isspace(static_cast<unsigned char>(c)))
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return out;
}

// A span of days. An invalid date at either end means "unbounded" there,
// which is how the table stores an open-ended key.
struct Tage {
    Date von;
    Date bis;
};

// Far enough out to stand for "no bound" and still leave room to add a day.
constexpr int64_t kKeinAnfang = -(int64_t(1) << 40);
constexpr int64_t kKeinEnde   =  (int64_t(1) << 40);

int64_t AnfangTag(const Date& d) { return d.Valid() ? d.ToEpochDay() : kKeinAnfang; }
int64_t EndeTag(const Date& d)   { return d.Valid() ? d.ToEpochDay() : kKeinEnde; }

Tage AusTagen(int64_t von, int64_t bis) {
    Tage t;
    if (von > kKeinAnfang) t.von = Date::FromEpochDay(von);
    if (bis < kKeinEnde)   t.bis = Date::FromEpochDay(bis);
    return t;
}

bool Ueberschneiden(const Tage& a, const Tage& b) {
    return AnfangTag(a.von) <= EndeTag(b.bis) && AnfangTag(b.von) <= EndeTag(a.bis);
}

std::string TageText(const Tage& t) {
    if (!t.von.Valid() && !t.bis.Valid()) return "unbefristet";
    if (!t.bis.Valid()) return "ab " + FormatDateGerman(t.von);
    if (!t.von.Valid()) return "bis " + FormatDateGerman(t.bis);
    return FormatDateGerman(t.von) + " - " + FormatDateGerman(t.bis);
}

// The days on which a version spanning `alt` and one spanning `neu` disagree:
// the days one of them covers and the other does not. Those are the days a
// change of validity touches, and the only ones that need asking about.
std::vector<Tage> GeaenderteTage(const Tage& alt, const Tage& neu) {
    const int64_t a1 = AnfangTag(alt.von), b1 = EndeTag(alt.bis);
    const int64_t a2 = AnfangTag(neu.von), b2 = EndeTag(neu.bis);
    std::vector<Tage> tage;
    if (b1 < a2 || b2 < a1) {           // no common day: both spans change entirely
        tage.push_back(alt);
        tage.push_back(neu);
        return tage;
    }
    if (a1 != a2) tage.push_back(AusTagen(std::min(a1, a2), std::max(a1, a2) - 1));
    if (b1 != b2) tage.push_back(AusTagen(std::min(b1, b2) + 1, std::max(b1, b2)));
    return tage;
}

// Has this Mandant filed a return for any of these days? Same caution as for
// the EU rates: a period whose bounds cannot be worked out counts as filed.
bool MeldungFuerTage(const std::vector<Store::Meldung>& meldungen, int64_t mandantId,
                     const Tage& tage, std::string& outBeschreibung) {
    for (const Store::Meldung& m : meldungen) {
        if (m.mandantId != 0 && mandantId != 0 && m.mandantId != mandantId) continue;
        Tage zeitraum;
        bool bekannt = false;
        if (m.art == "oss" || m.art == "ioss") {
            bekannt = OssZeitraumGrenzen(m.art == "oss" ? OssVerfahren::Oss : OssVerfahren::Ioss,
                                         m.jahr, m.zeitraum, zeitraum.von, zeitraum.bis);
        } else {
            bekannt = UstvaZeitraumGrenzen(m.jahr, m.zeitraum, zeitraum.von, zeitraum.bis);
        }
        if (!bekannt || Ueberschneiden(zeitraum, tage)) {
            outBeschreibung = m.art + " " + Number(m.jahr) + "/" + m.zeitraum;
            if (!m.transferticket.empty())
                outBeschreibung += " (Transferticket " + m.transferticket + ")";
            return true;
        }
    }
    return false;
}

bool NurZiffern(const std::string& text) {
    for (char c : text) if (c < '0' || c > '9') return false;
    return true;
}

// Everything the tax depends on. A version whose days carry postings may not
// change any of it - only its description.
bool GleicheWirkung(const Steuerschluessel& a, const Steuerschluessel& b) {
    return a.art == b.art && a.satzPromille == b.satzPromille && a.land == b.land &&
           a.vorsteuer == b.vorsteuer && a.datevBu == b.datevBu &&
           a.kzBemessung == b.kzBemessung && a.kzSteuer == b.kzSteuer &&
           a.kontoUmsatz == b.kontoUmsatz && a.kontoSteuer == b.kontoSteuer;
}

std::string Prozent(int promille) {
    std::string text = Number(promille / 10);
    if (promille % 10 != 0) text += "," + Number(promille % 10);
    return text + " %";
}

std::string WirkungText(const Steuerschluessel& k) {
    std::string text = SteuerArtToText(k.art) + ", " + Prozent(k.satzPromille);
    if (k.vorsteuer) text += ", Vorsteuer";
    if (!k.land.empty()) text += ", " + k.land;
    if (!k.datevBu.empty()) text += ", BU " + k.datevBu;
    if (!k.kzBemessung.empty()) text += ", Kz " + k.kzBemessung;
    if (!k.kzSteuer.empty()) text += ", Kz Steuer " + k.kzSteuer;
    if (!k.kontoUmsatz.empty()) text += ", Konto " + k.kontoUmsatz;
    if (!k.kontoSteuer.empty()) text += ", Steuerkonto " + k.kontoSteuer;
    return text;
}

} // namespace

Store::SteuerschluesselNutzung Store::SteuerschluesselGebucht(int64_t mandantId,
                                                             const std::string& schluessel,
                                                             const Date& von,
                                                             const Date& bis) const {
    SteuerschluesselNutzung nutzung;
    // ISO dates compare as text, so the range is plain SQL on every engine.
    std::string sql = "SELECT COUNT(*) AS n, MIN(belegdatum) AS erste, MAX(belegdatum) AS letzte"
                      " FROM buchung WHERE mandant_id = ? AND steuerschluessel = ?";
    UltraDbParams params = { mandantId, schluessel };
    if (von.Valid()) { sql += " AND belegdatum >= ?"; params.push_back(von.ToIso()); }
    if (bis.Valid()) { sql += " AND belegdatum <= ?"; params.push_back(bis.ToIso()); }
    UltraDbRow row;
    if (!QueryOne(sql, params, row)) return nutzung;
    nutzung.buchungen = static_cast<int>(row["n"].AsInt64());
    nutzung.erste     = DateFrom(row["erste"]);
    nutzung.letzte    = DateFrom(row["letzte"]);
    return nutzung;
}

// Is this key sound as data, before any question about what it would change?
// `bisher` is the version being edited or superseded, or null for a new key:
// an account is only checked when it is new, so a key imported with an account
// this chart does not have can still be described differently.
std::string Store::SteuerschluesselPruefen(const Steuerschluessel& k,
                                           const Steuerschluessel* bisher) const {
    if (k.schluessel.empty()) return "Der Steuerschlüssel braucht einen Namen, z. B. USt19.";
    if (k.schluessel.size() > 20) return "Der Name des Steuerschlüssels ist länger als 20 Zeichen.";
    for (char c : k.schluessel) {
        const bool erlaubt = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                             (c >= '0' && c <= '9') || c == '-' || c == '_';
        // The name is typed into positions ("Text;Menge;Preis;Konto;USt19")
        // and written into DATEV and CSV files; a space or a semicolon in it
        // would split the line somewhere nobody meant.
        if (!erlaubt)
            return "Der Name \"" + k.schluessel + "\" enthält Zeichen außer Buchstaben, "
                   "Ziffern, - und _.";
    }
    if (k.bezeichnung.empty()) return "Die Bezeichnung fehlt.";
    if (k.satzPromille < 0 || k.satzPromille > 1000)
        return "Der Steuersatz liegt zwischen 0 und 100 Prozent.";
    if (!k.land.empty() && (k.land.size() != 2 || k.land[0] < 'A' || k.land[0] > 'Z' ||
                            k.land[1] < 'A' || k.land[1] > 'Z'))
        return "Das Land ist ein ISO-Kürzel aus zwei Buchstaben, z. B. DE, oder leer.";
    // Required, unlike in the shipped file's history: the day a key starts to
    // apply is the decision this editor exists for, and "since always" is how
    // a new rate ends up under last year's postings.
    if (!k.gueltigVon.Valid()) return "Der Steuerschlüssel braucht ein Datum, ab dem er gilt.";
    if (k.gueltigBis.Valid() && k.gueltigBis < k.gueltigVon)
        return "Das Ende (" + FormatDateGerman(k.gueltigBis) + ") liegt vor dem Beginn (" +
               FormatDateGerman(k.gueltigVon) + ").";
    if (!k.datevBu.empty() && (!NurZiffern(k.datevBu) || k.datevBu.size() > 4))
        return "Der DATEV-BU-Schlüssel besteht aus höchstens vier Ziffern.";
    for (const std::string* kz : { &k.kzBemessung, &k.kzSteuer }) {
        if (!kz->empty() && (!NurZiffern(*kz) || kz->size() < 2 || kz->size() > 3))
            return "Eine UStVA-Kennzahl besteht aus zwei oder drei Ziffern, nicht \"" +
                   *kz + "\".";
    }
    if (k.satzPromille > 0 && k.kontoSteuer.empty())
        return "Ein Steuersatz über 0 % braucht ein Steuerkonto, auf das die Steuer gebucht wird.";
    // The rule an outgoing invoice already enforces, applied where the key is
    // made: an exemption with a rate contradicts itself on every document.
    if (!k.vorsteuer && IstNullsatzImAusgang(k.art) && k.satzPromille != 0)
        return "Die Art \"" + SteuerArtToText(k.art) + "\" berechnet keine deutsche "
               "Umsatzsteuer; ihr Satz ist 0 %.";
    for (const auto& [konto, bisherKonto] :
         { std::pair<std::string, std::string>{ k.kontoUmsatz, bisher ? bisher->kontoUmsatz : "" },
           std::pair<std::string, std::string>{ k.kontoSteuer, bisher ? bisher->kontoSteuer : "" } }) {
        if (konto.empty() || (bisher != nullptr && konto == bisherKonto)) continue;
        Konto gefunden;
        if (!KontoByNummer(k.mandantId, konto, gefunden))
            return "Das Konto " + konto + " gibt es im Kontenrahmen nicht.";
    }

    // One version per day.
    UltraDbResultSet rs;
    if (Query(std::string("SELECT ") + kSteuerColumns +
              " FROM steuerschluessel WHERE mandant_id = ? AND schluessel = ?",
              { k.mandantId, k.schluessel }, rs)) {
        for (const UltraDbRow& row : rs) {
            const Steuerschluessel andere = SteuerschluesselFromRow(row);
            if (andere.id == k.id) continue;
            if (Ueberschneiden({ andere.gueltigVon, andere.gueltigBis },
                               { k.gueltigVon, k.gueltigBis }))
                return "\"" + k.schluessel + "\" gilt bereits " +
                       TageText({ andere.gueltigVon, andere.gueltigBis }) +
                       ". Eine Fassung darf sich mit keiner anderen desselben "
                       "Schlüssels überschneiden - ein geänderter Satz ist eine neue "
                       "Fassung ab dem Tag der Änderung.";
        }
    }
    return std::string();
}

// Would touching these days of this key change something already on the
// books or already filed? Returns why, or an empty string.
std::string Store::SteuerschluesselTageBelegt(const Steuerschluessel& k,
                                              const Date& von, const Date& bis,
                                              const std::string& was) const {
    const Tage tage { von, bis };
    const SteuerschluesselNutzung n =
        SteuerschluesselGebucht(k.mandantId, k.schluessel, tage.von, tage.bis);
    if (n.buchungen > 0)
        return was + " würde " + Number(n.buchungen) + " Buchung(en) mit \"" + k.schluessel +
               "\" betreffen (" + FormatDateGerman(n.erste) +
               (n.letzte != n.erste ? " - " + FormatDateGerman(n.letzte) : std::string()) +
               "). Gebuchtes behält den Schlüssel, unter dem es gebucht wurde; ein "
               "geänderter Satz ist eine neue Fassung ab einem Tag nach der letzten Buchung.";
    std::string meldung;
    if (MeldungFuerTage(EingereichteMeldungen(), k.mandantId, tage, meldung))
        return was + " fällt in einen Zeitraum, für den bereits eine Meldung eingereicht "
               "ist (" + meldung + "). Sie würde nachträglich anders rechnen; zu korrigieren "
               "ist das über eine berichtigte Meldung.";
    return std::string();
}

StoreResult Store::SteuerschluesselAnlegen(Steuerschluessel& k, const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Steuerschlüssel ändern.");
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");
    if (k.id != 0) return StoreResult::Fail("Ein neuer Steuerschlüssel hat noch keine Nummer.");

    k.land = LandNormal(k.land);
    const std::string fehler = SteuerschluesselPruefen(k, nullptr);
    if (!fehler.empty()) return StoreResult::Fail(fehler);
    // A key name nothing is posted under yet - but a DATEV import may have
    // brought postings in under a name this file did not know, and a return
    // over them would suddenly count them.
    const std::string belegt = SteuerschluesselTageBelegt(
        k, k.gueltigVon, k.gueltigBis, "Der neue Schlüssel");
    if (!belegt.empty()) return StoreResult::Fail(belegt);

    const StoreResult r = SaveSteuerschluessel(k, akteur);
    if (!r) return r;
    return WriteAudit(akteur, "steuerschluessel", k.id, "anlegen",
                      k.schluessel + " " + TageText({ k.gueltigVon, k.gueltigBis }) + ": " +
                          WirkungText(k));
}

StoreResult Store::SteuerschluesselAendern(const Steuerschluessel& eingabe,
                                           const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Steuerschlüssel ändern.");
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");

    Steuerschluessel bisher;
    if (eingabe.id == 0 || !SteuerschluesselById(eingabe.id, bisher))
        return StoreResult::Fail("Diesen Steuerschlüssel gibt es nicht (mehr).");

    Steuerschluessel k = eingabe;
    k.mandantId = bisher.mandantId;
    k.land      = LandNormal(k.land);
    if (k.schluessel != bisher.schluessel)
        return StoreResult::Fail("Der Name eines Steuerschlüssels bleibt, wie er ist - "
                                 "Buchungen verweisen darauf. Ein anderer Name ist ein "
                                 "neuer Schlüssel.");

    const std::string fehler = SteuerschluesselPruefen(k, &bisher);
    if (!fehler.empty()) return StoreResult::Fail(fehler);

    const Tage alt { bisher.gueltigVon, bisher.gueltigBis };
    const Tage neu { k.gueltigVon, k.gueltigBis };
    const bool wirkungGeaendert = !GleicheWirkung(bisher, k);
    if (wirkungGeaendert) {
        // Every day either version covers: what was posted under the old
        // values, and whatever the new span would now claim.
        for (const Tage& tage : { alt, neu }) {
            const std::string belegt = SteuerschluesselTageBelegt(k, tage.von, tage.bis,
                                                                  "Die Änderung");
            if (!belegt.empty()) return StoreResult::Fail(belegt);
        }
    } else {
        for (const Tage& tage : GeaenderteTage(alt, neu)) {
            const std::string belegt = SteuerschluesselTageBelegt(
                k, tage.von, tage.bis, "Die geänderte Gültigkeit (" + TageText(tage) + ")");
            if (!belegt.empty()) return StoreResult::Fail(belegt);
        }
    }

    const StoreResult r = Exec(
        "UPDATE steuerschluessel SET bezeichnung = ?, art = ?, satz_promille = ?, land = ?,"
        " vorsteuer = ?, datev_bu = ?, kz_bemessung = ?, kz_steuer = ?, konto_umsatz = ?,"
        " konto_steuer = ?, gueltig_von = ?, gueltig_bis = ? WHERE id = ?",
        { k.bezeichnung, SteuerArtToText(k.art), k.satzPromille, k.land,
          k.vorsteuer ? 1 : 0, k.datevBu, k.kzBemessung, k.kzSteuer, k.kontoUmsatz,
          k.kontoSteuer, DateValue(k.gueltigVon), DateValue(k.gueltigBis), k.id },
        "Der Steuerschlüssel konnte nicht geändert werden");
    if (!r) return r;

    std::string details = k.schluessel;
    if (alt.von != neu.von || alt.bis != neu.bis)
        details += ", Gültigkeit " + TageText(alt) + " -> " + TageText(neu);
    if (wirkungGeaendert) details += ", " + WirkungText(bisher) + " -> " + WirkungText(k);
    if (bisher.bezeichnung != k.bezeichnung)
        details += ", Bezeichnung \"" + bisher.bezeichnung + "\" -> \"" + k.bezeichnung + "\"";
    return WriteAudit(akteur, "steuerschluessel", k.id, "aendern", details);
}

StoreResult Store::SteuerschluesselNeueFassung(int64_t id, const Date& ab,
                                               Steuerschluessel& neu, const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Steuerschlüssel ändern.");
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");

    Steuerschluessel alt;
    if (!SteuerschluesselById(id, alt))
        return StoreResult::Fail("Diesen Steuerschlüssel gibt es nicht (mehr).");
    if (!ab.Valid()) return StoreResult::Fail("Ab wann gilt die neue Fassung?");
    if (!alt.GueltigAm(ab) || (alt.gueltigVon.Valid() && ab == alt.gueltigVon))
        return StoreResult::Fail(
            "Eine neue Fassung beginnt innerhalb der bisherigen (" +
            TageText({ alt.gueltigVon, alt.gueltigBis }) + ") und nicht an ihrem ersten "
            "Tag - dort wäre sie keine Änderung, sondern ein Ersatz.");

    neu.id         = 0;
    neu.mandantId  = alt.mandantId;
    neu.schluessel = alt.schluessel;
    neu.gueltigVon = ab;
    neu.gueltigBis = alt.gueltigBis;
    neu.land       = LandNormal(neu.land);
    if (GleicheWirkung(alt, neu))
        return StoreResult::Fail("Die neue Fassung rechnet genauso wie die bisherige - "
                                 "für eine geänderte Bezeichnung genügt \"Bearbeiten\".");

    // Checked against the other versions as they will be: the old one ending
    // the day before, which is exactly what makes room for the new one.
    Steuerschluessel altGekuerzt = alt;
    altGekuerzt.gueltigBis = ab.AddDays(-1);
    {
        Steuerschluessel pruefling = neu;
        pruefling.id = alt.id;  // the only version it may touch is the one it shortens
        const std::string fehler = SteuerschluesselPruefen(pruefling, &alt);
        if (!fehler.empty()) return StoreResult::Fail(fehler);
    }
    const std::string belegt = SteuerschluesselTageBelegt(
        alt, ab, alt.gueltigBis, "Eine neue Fassung ab " + FormatDateGerman(ab));
    if (!belegt.empty()) return StoreResult::Fail(belegt);

    const StoreResult zu = Exec("UPDATE steuerschluessel SET gueltig_bis = ? WHERE id = ?",
                                { DateValue(altGekuerzt.gueltigBis), alt.id },
                                "Die bisherige Fassung konnte nicht beendet werden");
    if (!zu) return zu;
    const StoreResult r = SaveSteuerschluessel(neu, akteur);
    if (!r) {
        // Put the old end back rather than leave the key with no version at
        // all from `ab` on.
        Exec("UPDATE steuerschluessel SET gueltig_bis = ? WHERE id = ?",
             { DateValue(alt.gueltigBis), alt.id }, "");
        return r;
    }
    return WriteAudit(akteur, "steuerschluessel", neu.id, "neue-fassung",
                      neu.schluessel + " ab " + FormatDateGerman(ab) + ": " +
                          WirkungText(alt) + " -> " + WirkungText(neu));
}

StoreResult Store::SteuerschluesselLoeschen(int64_t id, const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Steuerschlüssel ändern.");
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");

    Steuerschluessel k;
    if (!SteuerschluesselById(id, k))
        return StoreResult::Fail("Diesen Steuerschlüssel gibt es nicht (mehr).");
    const std::string belegt = SteuerschluesselTageBelegt(
        k, k.gueltigVon, k.gueltigBis, "Das Löschen");
    if (!belegt.empty()) return StoreResult::Fail(belegt);

    const StoreResult r = Exec("DELETE FROM steuerschluessel WHERE id = ?", { id },
                               "Der Steuerschlüssel konnte nicht gelöscht werden");
    if (!r) return r;

    // The version this one had ended, if any, carries on in its place.
    std::string details = k.schluessel + " " + TageText({ k.gueltigVon, k.gueltigBis });
    if (k.gueltigVon.Valid()) {
        UltraDbRow vorher;
        if (QueryOne(std::string("SELECT ") + kSteuerColumns +
                     " FROM steuerschluessel WHERE mandant_id = ? AND schluessel = ?"
                     " AND gueltig_bis = ?",
                     { k.mandantId, k.schluessel, k.gueltigVon.AddDays(-1).ToIso() }, vorher)) {
            const StoreResult auf = Exec(
                "UPDATE steuerschluessel SET gueltig_bis = ? WHERE id = ?",
                { DateValue(k.gueltigBis), vorher["id"].AsInt64() },
                "Die vorherige Fassung konnte nicht verlängert werden");
            if (!auf) return auf;
            details += "; die vorherige Fassung gilt wieder " +
                       TageText({ DateFrom(vorher["gueltig_von"]), k.gueltigBis });
        }
    }
    return WriteAudit(akteur, "steuerschluessel", id, "loeschen", details);
}

// ===== PARTNER =====

namespace {

Partner PartnerFromRow(const UltraDbRow& row) {
    Partner p;
    p.id            = row["id"].AsInt64();
    p.mandantId     = row["mandant_id"].AsInt64();
    PartnerTypFromText(row["typ"].AsString(), p.typ);
    p.konto         = row["konto"].AsString();
    p.name          = row["name"].AsString();
    p.name2         = row["name2"].AsString();
    p.anrede        = row["anrede"].AsString();
    p.kontaktperson = row["kontaktperson"].AsString();
    p.abteilung     = row["abteilung"].AsString();
    p.strasse       = row["strasse"].AsString();
    p.plz           = row["plz"].AsString();
    p.ort           = row["ort"].AsString();
    p.land          = row["land"].AsString();
    SteuerkategorieFromText(row["steuerkategorie"].AsString(), p.steuerkategorie);
    p.ustIdNr           = row["ust_idnr"].AsString();
    p.ustIdNrStatus     = row["ust_idnr_status"].AsString();
    p.ustIdNrGeprueftAm = DateFrom(row["ust_idnr_geprueft_am"]);
    p.ustIdNrProtokoll  = row["ust_idnr_protokoll"].AsString();
    p.steuernummer      = row["steuernummer"].AsString();
    p.email    = row["email"].AsString();
    p.telefon  = row["telefon"].AsString();
    p.webseite = row["webseite"].AsString();
    p.iban     = row["iban"].AsString();
    p.bic      = row["bic"].AsString();
    p.zahlungsfristTage = row["zahlungsfrist_tage"].AsInt();
    p.skontoPromille    = row["skonto_promille"].AsInt();
    p.skontoTage        = row["skonto_tage"].AsInt();
    p.sprache     = row["sprache"].AsString();
    p.notiz       = row["notiz"].AsString();
    p.aktiv       = row["aktiv"].AsInt() != 0;
    p.angelegtAm  = row["angelegt_am"].AsInt64();
    p.geaendertAm = row["geaendert_am"].AsInt64();
    p.version     = row["version"].AsInt64();
    return p;
}

const char* const kPartnerColumns =
    "id, mandant_id, typ, konto, name, name2, anrede, kontaktperson, abteilung, strasse,"
    " plz, ort, land, steuerkategorie, ust_idnr, ust_idnr_status, ust_idnr_geprueft_am,"
    " ust_idnr_protokoll, steuernummer, email, telefon, webseite, iban, bic,"
    " zahlungsfrist_tage, skonto_promille, skonto_tage, sprache, notiz, aktiv, angelegt_am,"
    " geaendert_am, version";

} // namespace

StoreResult Store::NextPersonenkonto(int64_t mandantId, PartnerTyp typ,
                                     int sachkontenlaenge, std::string& out) const {
    if (sachkontenlaenge < 4 || sachkontenlaenge > 8)
        return StoreResult::Fail("Die Sachkontenlänge muss zwischen 4 und 8 liegen.");

    // Person accounts are one digit wider than the G/L accounts, and the two
    // ranges are the customary DATEV ones scaled to that width: with 4-digit
    // G/L accounts, customers run from 10000 and suppliers from 70000. Derived
    // from the setting rather than hard-coded, so a company with 5- or 6-digit
    // accounts gets the right ranges too.
    const int width = sachkontenlaenge + 1;
    int64_t faktor = 1;
    for (int i = 0; i < width - 1; ++i) faktor *= 10;
    const int64_t von = (typ == PartnerTyp::Lieferant) ? 7 * faktor : 1 * faktor;
    const int64_t bis = (typ == PartnerTyp::Lieferant) ? 10 * faktor - 1 : 7 * faktor - 1;

    // The highest number already used in the range, so a deleted partner's
    // number is not handed out twice - person accounts appear in exported
    // bookings and must stay unique over the life of the books.
    UltraDbResultSet rs;
    int64_t hoechste = von - 1;
    if (Query("SELECT konto FROM partner WHERE mandant_id = ?", { mandantId }, rs)) {
        for (const UltraDbRow& row : rs) {
            const std::string konto = row["konto"].AsString();
            if (konto.empty()) continue;
            bool digits = true;
            for (char c : konto) if (c < '0' || c > '9') { digits = false; break; }
            if (!digits) continue;
            int64_t value = 0;
            for (char c : konto) value = value * 10 + (c - '0');
            if (value >= von && value <= bis && value > hoechste) hoechste = value;
        }
    }
    const int64_t naechste = hoechste + 1;
    if (naechste > bis)
        return StoreResult::Fail("Der Nummernbereich für " +
                                 std::string(typ == PartnerTyp::Lieferant ? "Kreditoren"
                                                                          : "Debitoren") +
                                 " ist erschöpft.");

    std::string digits;
    int64_t v = naechste;
    while (v > 0) { digits.insert(digits.begin(), static_cast<char>('0' + (v % 10))); v /= 10; }
    out = digits;
    return StoreResult::Ok();
}

StoreResult Store::SavePartner(Partner& partner, const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Stammdaten ändern.");
    if (!partner.Valid()) return StoreResult::Fail("Der Name darf nicht leer sein.");

    // Check the VAT number offline and record what came out. This is not the
    // BZSt confirmation - that is a separate, stored protocol - but it catches
    // the typing errors before they reach an invoice.
    if (!partner.ustIdNr.empty()) {
        const UstIdNrPruefung pruefung = PruefeUstIdNr(partner.ustIdNr);
        partner.ustIdNr       = pruefung.normalisiert;
        partner.ustIdNrStatus = UstIdNrStatusToText(pruefung.status);
        if (!pruefung.Plausibel())
            return StoreResult::Fail("USt-IdNr.: " + pruefung.hinweis);
        // An EU VAT number and a domestic tax category contradict each other;
        // the category decides the taxation, so it is worth saying out loud
        // rather than booking 19 % onto an intra-community supply.
        if (!pruefung.land.empty() && pruefung.land != "DE" &&
            partner.steuerkategorie == Steuerkategorie::Inland)
            return StoreResult::Fail("Die USt-IdNr. gehört zu " + pruefung.land +
                                     ", die Steuerkategorie ist aber \"Inland\". "
                                     "Bitte die Kategorie anpassen.");
    }

    const int64_t now = NowSeconds();
    if (partner.id == 0) {
        if (partner.konto.empty()) {
            Geschaeftsjahr jahr;
            int laenge = 4;
            const std::vector<Geschaeftsjahr> jahre = Geschaeftsjahre(partner.mandantId);
            if (!jahre.empty()) laenge = jahre.back().sachkontenlaenge;
            const StoreResult konto = NextPersonenkonto(partner.mandantId, partner.typ,
                                                        laenge, partner.konto);
            if (!konto) return konto;
        }
        const StoreResult seq = NextSequenceValue("partner", partner.id);
        if (!seq) return seq;
        partner.angelegtAm  = now;
        partner.geaendertAm = now;
        partner.version     = 1;
        const StoreResult inserted = Exec(
            "INSERT INTO partner(id, mandant_id, typ, konto, name, name2, anrede,"
            " kontaktperson, abteilung, strasse, plz, ort, land, steuerkategorie, ust_idnr,"
            " ust_idnr_status, ust_idnr_geprueft_am, ust_idnr_protokoll, steuernummer, email,"
            " telefon, webseite, iban, bic, zahlungsfrist_tage, skonto_promille, skonto_tage,"
            " sprache, notiz, aktiv, angelegt_am, geaendert_am, version) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            { partner.id, partner.mandantId, PartnerTypToText(partner.typ), partner.konto,
              partner.name, partner.name2, partner.anrede, partner.kontaktperson,
              partner.abteilung, partner.strasse, partner.plz, partner.ort, partner.land,
              SteuerkategorieToText(partner.steuerkategorie), partner.ustIdNr,
              partner.ustIdNrStatus, DateValue(partner.ustIdNrGeprueftAm),
              partner.ustIdNrProtokoll, partner.steuernummer, partner.email, partner.telefon,
              partner.webseite, partner.iban, partner.bic, partner.zahlungsfristTage,
              partner.skontoPromille, partner.skontoTage, partner.sprache, partner.notiz,
              partner.aktiv ? 1 : 0, partner.angelegtAm, partner.geaendertAm, partner.version },
            "Der Partner konnte nicht angelegt werden");
        if (!inserted) return inserted;
        return WriteAudit(akteur, "partner", partner.id, "anlegen",
                          partner.konto + " " + partner.name);
    }

    // Optimistic locking: the update only bites when the row still carries the
    // version we read. Zero rows changed means somebody else saved in the
    // meantime, and overwriting their work silently is the one outcome nobody
    // wants.
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");
    const UltraDbResult updated = UltraDb_Exec(connection_,
        "UPDATE partner SET typ = ?, konto = ?, name = ?, name2 = ?, anrede = ?,"
        " kontaktperson = ?, abteilung = ?, strasse = ?, plz = ?, ort = ?, land = ?,"
        " steuerkategorie = ?, ust_idnr = ?, ust_idnr_status = ?, ust_idnr_geprueft_am = ?,"
        " ust_idnr_protokoll = ?, steuernummer = ?, email = ?, telefon = ?, webseite = ?,"
        " iban = ?, bic = ?, zahlungsfrist_tage = ?, skonto_promille = ?, skonto_tage = ?,"
        " sprache = ?, notiz = ?, aktiv = ?, geaendert_am = ?, version = version + 1 "
        "WHERE id = ? AND version = ?",
        { PartnerTypToText(partner.typ), partner.konto, partner.name, partner.name2,
          partner.anrede, partner.kontaktperson, partner.abteilung, partner.strasse,
          partner.plz, partner.ort, partner.land,
          SteuerkategorieToText(partner.steuerkategorie), partner.ustIdNr,
          partner.ustIdNrStatus, DateValue(partner.ustIdNrGeprueftAm),
          partner.ustIdNrProtokoll, partner.steuernummer, partner.email, partner.telefon,
          partner.webseite, partner.iban, partner.bic, partner.zahlungsfristTage,
          partner.skontoPromille, partner.skontoTage, partner.sprache, partner.notiz,
          partner.aktiv ? 1 : 0, now, partner.id, partner.version });
    if (!updated)
        return StoreResult::Fail("Der Partner konnte nicht geändert werden: " +
                                 updated.message);
    if (updated.affectedRows == 0) {
        Partner aktuell;
        const bool exists = PartnerById(partner.id, aktuell);
        if (!exists) return StoreResult::Fail("Der Partner existiert nicht mehr.");
        return StoreResult::Fail("Dieser Partner wurde zwischenzeitlich von jemand anderem "
                                 "geändert (Version " + std::to_string(aktuell.version) +
                                 "). Bitte neu laden und die Änderung wiederholen.");
    }
    partner.geaendertAm = now;
    ++partner.version;
    return WriteAudit(akteur, "partner", partner.id, "aendern",
                      partner.konto + " " + partner.name);
}

StoreResult Store::SpeichereUstIdNrBestaetigung(int64_t partnerId,
                                                const Bestaetigung& bestaetigung,
                                                const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Bestätigungen speichern.");
    if (!bestaetigung.ok)
        return StoreResult::Fail("Eine fehlgeschlagene Abfrage wird nicht als Bestätigung "
                                 "gespeichert: " + bestaetigung.fehler);

    Partner partner;
    if (!PartnerById(partnerId, partner))
        return StoreResult::Fail("Der Partner wurde nicht gefunden.");

    // The status records *what kind* of confirmation this was, because a VIES
    // "valid" and a BZSt qualified confirmation protect entirely different
    // things.
    std::string status = BestaetigungsQuelleToText(bestaetigung.quelle);
    status += bestaetigung.gueltig ? ":gueltig" : ":ungueltig";
    if (bestaetigung.IstQualifiziert()) status += ":qualifiziert";

    const StoreResult updated = Exec(
        "UPDATE partner SET ust_idnr_status = ?, ust_idnr_geprueft_am = ?,"
        " ust_idnr_protokoll = ?, geaendert_am = ?, version = version + 1 WHERE id = ?",
        { status, DateValue(bestaetigung.anfrageDatum), bestaetigung.protokoll,
          NowSeconds(), partnerId },
        "Die Bestätigung konnte nicht gespeichert werden");
    if (!updated) return updated;
    return WriteAudit(akteur, "partner", partnerId, "ustidnr-bestaetigung",
                      status + " " + bestaetigung.Zusammenfassung());
}

bool Store::PartnerById(int64_t id, Partner& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kPartnerColumns + " FROM partner WHERE id = ?",
                  { id }, row))
        return false;
    out = PartnerFromRow(row);
    return true;
}

bool Store::PartnerByKonto(int64_t mandantId, const std::string& konto, Partner& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kPartnerColumns +
                  " FROM partner WHERE mandant_id = ? AND konto = ?", { mandantId, konto }, row))
        return false;
    out = PartnerFromRow(row);
    return true;
}

std::vector<Partner> Store::PartnerListe(int64_t mandantId, PartnerTyp typ,
                                         const std::string& suche) const {
    std::vector<Partner> liste;
    UltraDbResultSet rs;
    if (!Query(std::string("SELECT ") + kPartnerColumns +
               " FROM partner WHERE mandant_id = ? ORDER BY name", { mandantId }, rs))
        return liste;

    // Filtering happens here rather than in SQL because case-insensitive
    // matching over several columns is where SQLite and PostgreSQL disagree
    // most (LIKE, ILIKE, collations), and a master-data list is never large
    // enough for that to matter. The journal, when it comes, will need indexed
    // SQL filtering and its own portable predicate.
    const std::string needle = ToLower(suche);
    for (const UltraDbRow& row : rs) {
        const Partner partner = PartnerFromRow(row);
        const bool typOk = (typ == PartnerTyp::Beides) ||
                           (typ == PartnerTyp::Kunde     && partner.IstKunde()) ||
                           (typ == PartnerTyp::Lieferant && partner.IstLieferant());
        if (!typOk) continue;
        if (!needle.empty()) {
            const std::string haystack = ToLower(partner.name + " " + partner.name2 + " " +
                                                 partner.ort + " " + partner.ustIdNr + " " +
                                                 partner.konto + " " + partner.kontaktperson);
            if (haystack.find(needle) == std::string::npos) continue;
        }
        liste.push_back(partner);
    }
    return liste;
}

// ===== BENUTZER =====

namespace {

Benutzer BenutzerFromRow(const UltraDbRow& row) {
    Benutzer user;
    user.id           = row["id"].AsInt64();
    user.anmeldename  = row["anmeldename"].AsString();
    user.anzeigename  = row["anzeigename"].AsString();
    user.email        = row["email"].AsString();
    BenutzerRolleFromText(row["rolle"].AsString(), user.rolle);
    user.aktiv        = row["aktiv"].AsInt() != 0;
    user.angelegtAm   = row["angelegt_am"].AsInt64();
    user.letzterLogin = row["letzter_login"].AsInt64();
    return user;
}

const char* const kBenutzerColumns =
    "id, anmeldename, anzeigename, email, rolle, aktiv, angelegt_am, letzter_login";

// Argon2id through UltraCrypt. The parameters are stored per user so they can
// be raised later without invalidating existing passwords.
bool DeriveePasswort(const std::string& passwort, UltraCryptKdfParams& params,
                     std::string& outHex) {
    static bool initialised = false;
    if (!initialised) { UltraCrypt_Initialize(); initialised = true; }

    const UltraCryptSecureBuffer secret(passwort.data(), passwort.size());
    UltraCryptSecureBuffer key;
    const UltraCryptResult result = UltraCrypt_DeriveKeyFromPassword(secret, params, key);
    if (!result) return false;
    outHex = UltraCrypt_ToHex(std::vector<uint8_t>(key.Data(), key.Data() + key.GetSize()));
    return true;
}

} // namespace

StoreResult Store::SaveBenutzer(Benutzer& benutzer, const Akteur& akteur) {
    // The first user is created before anybody can be authorised to do it -
    // otherwise a fresh installation could never be set up.
    const bool erstbenutzer = !HatBenutzer();
    if (!erstbenutzer && !akteur.Darf(Recht::BenutzerVerwalten))
        return StoreResult::Fail("Diese Rolle darf keine Benutzer verwalten.");
    if (!benutzer.Valid()) return StoreResult::Fail("Der Benutzer braucht einen Anmeldenamen.");
    if (erstbenutzer && benutzer.rolle != BenutzerRolle::Administrator)
        benutzer.rolle = BenutzerRolle::Administrator;

    if (benutzer.id == 0) {
        Benutzer vorhanden;
        if (BenutzerByName(benutzer.anmeldename, vorhanden))
            return StoreResult::Fail("Der Anmeldename \"" + benutzer.anmeldename +
                                     "\" ist bereits vergeben.");
        const StoreResult seq = NextSequenceValue("benutzer", benutzer.id);
        if (!seq) return seq;
        benutzer.angelegtAm = NowSeconds();
        const StoreResult inserted = Exec(
            "INSERT INTO benutzer(id, anmeldename, anzeigename, email, rolle, aktiv,"
            " angelegt_am, letzter_login) VALUES(?,?,?,?,?,?,?,?)",
            { benutzer.id, benutzer.anmeldename, benutzer.anzeigename, benutzer.email,
              BenutzerRolleToText(benutzer.rolle), benutzer.aktiv ? 1 : 0,
              benutzer.angelegtAm, benutzer.letzterLogin },
            "Der Benutzer konnte nicht angelegt werden");
        if (!inserted) return inserted;
    } else {
        const StoreResult updated = Exec(
            "UPDATE benutzer SET anzeigename = ?, email = ?, rolle = ?, aktiv = ? WHERE id = ?",
            { benutzer.anzeigename, benutzer.email, BenutzerRolleToText(benutzer.rolle),
              benutzer.aktiv ? 1 : 0, benutzer.id },
            "Der Benutzer konnte nicht geändert werden");
        if (!updated) return updated;
    }
    return WriteAudit(akteur, "benutzer", benutzer.id, "speichern",
                      benutzer.anmeldename + " (" + BenutzerRolleToText(benutzer.rolle) + ")");
}

StoreResult Store::SetPasswort(int64_t benutzerId, const std::string& passwort,
                               const Akteur& akteur) {
    // A user may always set their own password; changing somebody else's needs
    // the user-management right.
    if (akteur.benutzerId != benutzerId && !akteur.Darf(Recht::BenutzerVerwalten))
        return StoreResult::Fail("Diese Rolle darf fremde Passwörter nicht ändern.");
    if (passwort.size() < 8)
        return StoreResult::Fail("Das Passwort muss mindestens 8 Zeichen lang sein.");
    if (!UltraCrypt_IsAvailable())
        return StoreResult::Fail("Die Kryptobibliothek ist nicht verfügbar, es kann kein "
                                 "Passwort gesetzt werden.");

    UltraCryptKdfParams params = UltraCrypt_RecommendedKdfParams();
    std::string hex;
    if (!DeriveePasswort(passwort, params, hex))
        return StoreResult::Fail("Das Passwort konnte nicht verschlüsselt werden.");

    const StoreResult updated = Exec(
        "UPDATE benutzer SET passwort_hash = ?, passwort_salt = ?, kdf_algorithmus = ?,"
        " kdf_iterationen = ?, kdf_speicher_kib = ?, kdf_laenge = ? WHERE id = ?",
        { hex, UltraCrypt_ToHex(params.salt), std::string("argon2id"),
          static_cast<int64_t>(params.iterations), static_cast<int64_t>(params.memoryKiB),
          static_cast<int64_t>(params.outputLength), benutzerId },
        "Das Passwort konnte nicht gespeichert werden");
    if (!updated) return updated;
    return WriteAudit(akteur, "benutzer", benutzerId, "passwort", std::string());
}

bool Store::Anmelden(const std::string& anmeldename, const std::string& passwort,
                     Benutzer& out) {
    UltraDbRow row;
    if (!QueryOne("SELECT id, anmeldename, anzeigename, email, rolle, aktiv, angelegt_am,"
                  " letzter_login, passwort_hash, passwort_salt, kdf_iterationen,"
                  " kdf_speicher_kib, kdf_laenge FROM benutzer WHERE anmeldename = ?",
                  { anmeldename }, row))
        return false;                      // unknown user and wrong password look alike
    if (row["aktiv"].AsInt() == 0) return false;

    const std::string stored = row["passwort_hash"].AsString();
    if (stored.empty()) return false;

    UltraCryptKdfParams params = UltraCrypt_RecommendedKdfParams();
    std::vector<uint8_t> salt;
    if (!UltraCrypt_FromHex(row["passwort_salt"].AsString(), salt)) return false;
    params.salt         = salt;
    params.iterations   = static_cast<uint32_t>(row["kdf_iterationen"].AsInt64());
    params.memoryKiB    = static_cast<uint32_t>(row["kdf_speicher_kib"].AsInt64());
    params.outputLength = static_cast<size_t>(row["kdf_laenge"].AsInt64());
    if (params.outputLength == 0) params.outputLength = 32;

    std::string hex;
    if (!DeriveePasswort(passwort, params, hex)) return false;
    // Constant-time comparison: the hex strings are the same length whenever
    // the parameters are, and a timing signal on a password check is free to
    // avoid.
    if (hex.size() != stored.size()) return false;
    if (!UltraCrypt_ConstantTimeEquals(hex.data(), hex.size(),
                                      stored.data(), stored.size())) return false;

    out = BenutzerFromRow(row);
    out.letzterLogin = NowSeconds();
    Exec("UPDATE benutzer SET letzter_login = ? WHERE id = ?",
         { out.letzterLogin, out.id }, "Anmeldezeit");
    return true;
}

bool Store::BenutzerById(int64_t id, Benutzer& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kBenutzerColumns + " FROM benutzer WHERE id = ?",
                  { id }, row))
        return false;
    out = BenutzerFromRow(row);
    return true;
}

bool Store::BenutzerByName(const std::string& anmeldename, Benutzer& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kBenutzerColumns +
                  " FROM benutzer WHERE anmeldename = ?", { anmeldename }, row))
        return false;
    out = BenutzerFromRow(row);
    return true;
}

std::vector<Benutzer> Store::BenutzerListe() const {
    std::vector<Benutzer> liste;
    UltraDbResultSet rs;
    if (!Query(std::string("SELECT ") + kBenutzerColumns + " FROM benutzer ORDER BY anmeldename",
               {}, rs))
        return liste;
    for (const UltraDbRow& row : rs) liste.push_back(BenutzerFromRow(row));
    return liste;
}

bool Store::HatBenutzer() const {
    UltraDbRow row;
    if (!QueryOne("SELECT COUNT(*) AS anzahl FROM benutzer", {}, row)) return false;
    return row["anzahl"].AsInt64() > 0;
}


StoreResult Store::SaveBeleg(Beleg& beleg, const std::string& kreis, const Akteur& akteur) {
    if (!akteur.Darf(Recht::BelegErfassen))
        return StoreResult::Fail("Diese Rolle darf keine Belege erfassen.");
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");
    if (!beleg.datum.Valid())
        return StoreResult::Fail("Der Beleg braucht ein gültiges Belegdatum.");
    // **A draft may be empty; a posted document may not.**
    //
    // This used to refuse every document with no positions, which made it
    // impossible to file a received PDF before somebody had read it and typed
    // the amounts in - and the alternative, inventing a placeholder position,
    // would put figures in a ledger that nobody entered. The rule that
    // matters is enforced where it belongs: Buchen() still refuses to post a
    // document with no positions, so an empty draft can never become a
    // posting.
    if (beleg.positionen.empty() && beleg.status != BelegStatus::Entwurf)
        return StoreResult::Fail("Ein gebuchter Beleg ohne Positionen kann nicht "
                                 "gespeichert werden.");
    for (const BelegPosition& pos : beleg.positionen) {
        if (!pos.Valid())
            return StoreResult::Fail("Die Position \"" + pos.bezeichnung +
                                     "\" ist unvollständig.");
        if (pos.konto.empty())
            return StoreResult::Fail("Der Position \"" + pos.bezeichnung +
                                     "\" fehlt das Konto.");
    }

    // The fiscal year is found from the date, never passed in: a document whose
    // year and date disagree is the bug this makes impossible.
    Geschaeftsjahr jahr;
    if (!GeschaeftsjahrAt(beleg.mandantId, beleg.datum, jahr))
        return StoreResult::Fail("Zum " + FormatDateGerman(beleg.datum) +
                                 " ist kein Geschäftsjahr angelegt.");
    if (!jahr.AcceptsPostings())
        return StoreResult::Fail("Das Geschäftsjahr \"" + jahr.bezeichnung +
                                 "\" ist abgeschlossen.");
    if (jahr.IsFrozen(beleg.datum))
        return StoreResult::Fail("Bis " + FormatDateGerman(jahr.festschreibungBis) +
                                 " ist festgeschrieben. Zum " +
                                 FormatDateGerman(beleg.datum) +
                                 " kann kein Beleg mehr erfasst oder geändert werden.");
    beleg.geschaeftsjahrId = jahr.id;

    // A due date nobody typed comes from the partner's payment terms. Derived
    // once, on save, and then stored - so changing a customer's terms next year
    // does not silently move when last year's invoices were due.
    if (!beleg.faelligAm.Valid() && beleg.partnerId != 0) {
        Partner partner;
        if (PartnerById(beleg.partnerId, partner) && partner.zahlungsfristTage > 0)
            beleg.faelligAm = beleg.datum.AddDays(partner.zahlungsfristTage);
    }

    // An existing document may only be touched while it is a draft. After
    // Buchen() a change is a Storno - refused here rather than in the UI,
    // because in server mode the database is reachable without the UI.
    Beleg vorher;
    const bool istAenderung = beleg.id != 0;
    if (istAenderung) {
        if (!BelegById(beleg.id, vorher))
            return StoreResult::Fail("Der Beleg wurde nicht gefunden.");
        if (!vorher.IstAenderbar()) {
            WriteAudit(akteur, "beleg", beleg.id, "abgelehnt",
                       "Änderung an Beleg " + vorher.nummer + " im Status " +
                       BelegStatusLabel(vorher.status));
            return StoreResult::Fail(
                "Der Beleg " + vorher.nummer + " ist bereits gebucht. Der Inhalt ist "
                "nicht mehr änderbar - Änderungen sind nur über eine Stornierung "
                "möglich.");
        }
        if (vorher.version != beleg.version)
            return StoreResult::Fail(
                "Der Beleg wurde zwischenzeitlich von jemand anderem geändert. "
                "Bitte neu laden und die Änderung wiederholen.");
    }

    // Cost the document against the tax keys as they stood on the Belegdatum.
    // A key that is unknown *on that date* is an error, not a zero rate.
    std::string unbekannt;
    const bool costed = beleg.Summieren(
        [&](const std::string& schluessel, int& satzPromille) {
            if (schluessel.empty()) { satzPromille = 0; return true; }
            Steuerschluessel key;
            if (!SteuerschluesselByKey(beleg.mandantId, schluessel, beleg.datum, key)) {
                unbekannt = schluessel;
                return false;
            }
            satzPromille = key.satzPromille;
            return true;
        });
    if (!costed) {
        if (!unbekannt.empty())
            return StoreResult::Fail("Der Steuerschlüssel \"" + unbekannt +
                                     "\" ist zum " + FormatDateGerman(beleg.datum) +
                                     " nicht gültig.");
        return StoreResult::Fail("Die Beträge des Belegs konnten nicht berechnet werden.");
    }

    const int64_t jetzt = NowSeconds();

    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(connection_, &error);
    if (tx == UltraDbInvalidHandle)
        return StoreResult::Fail("Transaktion konnte nicht gestartet werden: " +
                                 error.message);

    std::string fehler;
    auto abbrechen = [&](const std::string& text) {
        UltraDb_Rollback(tx);
        return StoreResult::Fail(text + (fehler.empty() ? "" : ": " + fehler));
    };

    // The number is allocated here, inside the transaction that uses it, and
    // only when the document does not have one - so an abandoned draft consumes
    // no number and two writers cannot be given the same one.
    if (beleg.nummer.empty()) {
        if (!NextBelegnummerInTx(tx, beleg.mandantId, kreis, beleg.datum,
                                 beleg.nummer, fehler, RowLock()))
            return abbrechen("Die Belegnummer konnte nicht vergeben werden");
    }

    if (!istAenderung) {
        if (!NextSequenzInTx(tx, "beleg", beleg.id, fehler, RowLock()))
            return abbrechen("Die Belegnummer konnte nicht vergeben werden");
        beleg.erfasstVon     = akteur.benutzerId;
        beleg.erfasstVonName = akteur.anmeldename;
        beleg.erfasstAm      = jetzt;
        beleg.version        = 1;
    } else {
        beleg.version = vorher.version + 1;
    }
    beleg.geaendertAm = jetzt;

    if (!istAenderung) {
        const UltraDbResult inserted = UltraDb_ExecInTx(
            tx,
            "INSERT INTO beleg(id, mandant_id, geschaeftsjahr_id, art, nummer,"
            " externe_nummer, datum, leistung_von, leistung_bis, faellig_am,"
            " partner_id, partner_konto, partner_name, waehrung, netto, steuer,"
            " brutto, bezahlt, status, buchungstext, notiz, datei_pfad, datei_hash,"
            " storno_von, storniert_durch, festgeschrieben, erfasst_von,"
            " erfasst_von_name, erfasst_am, geaendert_am, version, preise_brutto,"
            " steuer_vorgegeben, vorgegebene_steuer, leistungsart)"
            " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,"
            "?,?,?,?)",
            { beleg.id, beleg.mandantId, beleg.geschaeftsjahrId,
              BelegArtToText(beleg.art), beleg.nummer, beleg.externeNummer,
              beleg.datum.ToIso(), DateValue(beleg.leistungVon),
              DateValue(beleg.leistungBis), DateValue(beleg.faelligAm),
              beleg.partnerId, beleg.partnerKonto, beleg.partnerName, beleg.waehrung,
              beleg.netto.Minor(), beleg.steuer.Minor(), beleg.brutto.Minor(),
              beleg.bezahlt.Valid() ? beleg.bezahlt.Minor() : int64_t(0),
              BelegStatusToText(beleg.status), beleg.buchungstext, beleg.notiz,
              beleg.dateiPfad, beleg.dateiHash, beleg.stornoVon, beleg.storniertDurch,
              beleg.festgeschrieben ? 1 : 0, beleg.erfasstVon, beleg.erfasstVonName,
              beleg.erfasstAm, beleg.geaendertAm, beleg.version,
              beleg.preiseSindBrutto ? 1 : 0, beleg.steuerVorgegeben ? 1 : 0,
              beleg.vorgegebeneSteuer.Valid() ? beleg.vorgegebeneSteuer.Minor()
                                              : int64_t(0),
              LeistungszeitpunktToText(beleg.leistungsart) });
        if (!inserted) { fehler = inserted.message; return abbrechen("Der Beleg konnte nicht angelegt werden"); }
    } else {
        // The optimistic-locking WHERE: if somebody else bumped the version
        // between the read above and this write, no row matches and the update
        // is refused rather than silently lost.
        const UltraDbResult updated = UltraDb_ExecInTx(
            tx,
            "UPDATE beleg SET geschaeftsjahr_id = ?, art = ?, nummer = ?,"
            " externe_nummer = ?, datum = ?, leistung_von = ?, leistung_bis = ?,"
            " faellig_am = ?, partner_id = ?, partner_konto = ?, partner_name = ?,"
            " waehrung = ?, netto = ?, steuer = ?, brutto = ?, buchungstext = ?,"
            " notiz = ?, geaendert_am = ?, version = ?, preise_brutto = ?,"
            " steuer_vorgegeben = ?, vorgegebene_steuer = ?, leistungsart = ?"
            " WHERE id = ? AND version = ?",
            { beleg.geschaeftsjahrId, BelegArtToText(beleg.art), beleg.nummer,
              beleg.externeNummer, beleg.datum.ToIso(), DateValue(beleg.leistungVon),
              DateValue(beleg.leistungBis), DateValue(beleg.faelligAm),
              beleg.partnerId, beleg.partnerKonto, beleg.partnerName, beleg.waehrung,
              beleg.netto.Minor(), beleg.steuer.Minor(), beleg.brutto.Minor(),
              beleg.buchungstext, beleg.notiz, beleg.geaendertAm, beleg.version,
              beleg.preiseSindBrutto ? 1 : 0, beleg.steuerVorgegeben ? 1 : 0,
              beleg.vorgegebeneSteuer.Valid() ? beleg.vorgegebeneSteuer.Minor()
                                              : int64_t(0),
              LeistungszeitpunktToText(beleg.leistungsart),
              beleg.id, vorher.version });
        if (!updated) { fehler = updated.message; return abbrechen("Der Beleg konnte nicht geändert werden"); }

        const UltraDbResult cleared = UltraDb_ExecInTx(
            tx, "DELETE FROM beleg_position WHERE beleg_id = ?", { beleg.id });
        if (!cleared) { fehler = cleared.message; return abbrechen("Die Positionen konnten nicht ersetzt werden"); }
    }

    int nummer = 1;
    for (BelegPosition& pos : beleg.positionen) {
        pos.belegId  = beleg.id;
        pos.position = nummer++;
        if (!NextSequenzInTx(tx, "beleg_position", pos.id, fehler, RowLock()))
            return abbrechen("Die Position konnte nicht angelegt werden");
        const UltraDbResult inserted = UltraDb_ExecInTx(
            tx,
            "INSERT INTO beleg_position(id, beleg_id, position, bezeichnung,"
            " menge_tausendstel, einheit, einzelpreis, rabatt_promille, konto,"
            " steuerschluessel, satz_promille, netto, steuer, brutto, kostenstelle,"
            " kostentraeger, waehrung) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            { pos.id, pos.belegId, pos.position, pos.bezeichnung,
              pos.mengeTausendstel, pos.einheit, pos.einzelpreis.Minor(),
              pos.rabattPromille, pos.konto, pos.steuerschluessel, pos.satzPromille,
              pos.netto.Minor(), pos.steuer.Minor(), pos.brutto.Minor(),
              pos.kostenstelle, pos.kostentraeger, beleg.waehrung });
        if (!inserted) { fehler = inserted.message; return abbrechen("Die Position konnte nicht angelegt werden"); }
    }

    if (!AuditInTx(tx, akteur, "beleg", beleg.id, istAenderung ? "aendern" : "anlegen",
                   beleg.nummer + " " + beleg.brutto.ToString(), fehler, RowLock()))
        return abbrechen("Der Protokolleintrag konnte nicht geschrieben werden");

    const UltraDbResult commit = UltraDb_Commit(tx);
    if (!commit)
        return StoreResult::Fail("Der Beleg wurde nicht bestätigt: " + commit.message);
    return StoreResult::Ok();
}

bool Store::BelegById(int64_t id, Beleg& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kBelegSpalten + " FROM beleg WHERE id = ?",
                  { id }, row))
        return false;
    out = BelegFromRow(row);
    out.positionen = BelegPositionen(id);
    return true;
}

bool Store::BelegByNummer(int64_t mandantId, const std::string& nummer, Beleg& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kBelegSpalten +
                      " FROM beleg WHERE mandant_id = ? AND nummer = ?",
                  { mandantId, nummer }, row))
        return false;
    out = BelegFromRow(row);
    out.positionen = BelegPositionen(out.id);
    return true;
}

std::vector<BelegPosition> Store::BelegPositionen(int64_t belegId) const {
    std::vector<BelegPosition> liste;
    UltraDbResultSet rs;
    if (!Query(std::string("SELECT ") + kPositionSpalten +
                   " FROM beleg_position WHERE beleg_id = ? ORDER BY position",
               { belegId }, rs))
        return liste;
    for (const UltraDbRow& row : rs) liste.push_back(PositionFromRow(row));
    return liste;
}

std::vector<Beleg> Store::BelegListe(const BelegFilter& filter) const {
    std::vector<Beleg> liste;

    std::string sql = std::string("SELECT ") + kBelegSpalten +
                      " FROM beleg WHERE mandant_id = ?";
    UltraDbParams params = { filter.mandantId };

    if (filter.artGesetzt) {
        sql += " AND art = ?";
        params.push_back(BelegArtToText(filter.art));
    }
    if (filter.statusGesetzt) {
        sql += " AND status = ?";
        params.push_back(BelegStatusToText(filter.status));
    }
    if (filter.partnerId != 0) {
        sql += " AND partner_id = ?";
        params.push_back(filter.partnerId);
    }
    if (filter.von.Valid()) {
        sql += " AND datum >= ?";
        params.push_back(filter.von.ToIso());
    }
    if (filter.bis.Valid()) {
        sql += " AND datum <= ?";
        params.push_back(filter.bis.ToIso());
    }
    if (filter.nurOffene || filter.nurUeberfaellig) {
        // "Open" is posted and not settled, which is two statuses rather than a
        // comparison of amounts: a credit note is settled at a negative total.
        sql += " AND status IN (?, ?)";
        params.push_back(BelegStatusToText(BelegStatus::Gebucht));
        params.push_back(BelegStatusToText(BelegStatus::TeilweiseBezahlt));
    }
    if (filter.nurUeberfaellig && filter.heute.Valid()) {
        sql += " AND faellig_am IS NOT NULL AND faellig_am < ?";
        params.push_back(filter.heute.ToIso());
    }
    if (!filter.suche.empty()) {
        // Case-insensitive contains over the three fields somebody actually
        // searches by. LOWER() is in both engines' core; LIKE's case rules are
        // not, which is why the fold is explicit.
        sql += " AND (LOWER(nummer) LIKE ? OR LOWER(externe_nummer) LIKE ?"
               " OR LOWER(partner_name) LIKE ?)";
        const std::string muster = "%" + ToLower(filter.suche) + "%";
        params.push_back(muster);
        params.push_back(muster);
        params.push_back(muster);
    }

    sql += " ORDER BY datum DESC, nummer DESC";
    if (filter.limit > 0) {
        sql += " LIMIT ?";
        params.push_back(static_cast<int64_t>(filter.limit));
    }

    UltraDbResultSet rs;
    if (!Query(sql, params, rs)) return liste;
    for (const UltraDbRow& row : rs) liste.push_back(BelegFromRow(row));
    return liste;
}

StoreResult Store::DeleteBeleg(int64_t id, const Akteur& akteur) {
    if (!akteur.Darf(Recht::BelegErfassen))
        return StoreResult::Fail("Diese Rolle darf keine Belege löschen.");

    Beleg beleg;
    if (!BelegById(id, beleg)) return StoreResult::Fail("Der Beleg wurde nicht gefunden.");
    if (!beleg.IstAenderbar()) {
        WriteAudit(akteur, "beleg", id, "abgelehnt",
                   "Löschen von " + beleg.nummer + " im Status " +
                   BelegStatusLabel(beleg.status));
        return StoreResult::Fail(
            "Gebuchte Belege werden storniert, nicht gelöscht. Eine Lücke in den "
            "Belegnummern ist das Erste, wonach eine Prüfung fragt.");
    }

    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(connection_, &error);
    if (tx == UltraDbInvalidHandle)
        return StoreResult::Fail("Transaktion konnte nicht gestartet werden: " +
                                 error.message);

    const UltraDbResult positionen =
        UltraDb_ExecInTx(tx, "DELETE FROM beleg_position WHERE beleg_id = ?", { id });
    if (!positionen) {
        UltraDb_Rollback(tx);
        return StoreResult::Fail("Die Positionen konnten nicht gelöscht werden: " +
                                 positionen.message);
    }
    const UltraDbResult kopf =
        UltraDb_ExecInTx(tx, "DELETE FROM beleg WHERE id = ?", { id });
    if (!kopf) {
        UltraDb_Rollback(tx);
        return StoreResult::Fail("Der Beleg konnte nicht gelöscht werden: " + kopf.message);
    }

    std::string fehler;
    if (!AuditInTx(tx, akteur, "beleg", id, "loeschen", beleg.nummer, fehler, RowLock())) {
        UltraDb_Rollback(tx);
        return StoreResult::Fail("Der Protokolleintrag konnte nicht geschrieben werden: " +
                                 fehler);
    }

    const UltraDbResult commit = UltraDb_Commit(tx);
    if (!commit)
        return StoreResult::Fail("Das Löschen wurde nicht bestätigt: " + commit.message);
    return StoreResult::Ok();
}

StoreResult Store::BelegDateiAnhaengen(int64_t belegId, const std::string& dateiPfad,
                                       const Akteur& akteur) {
    if (!akteur.Darf(Recht::BelegErfassen))
        return StoreResult::Fail("Diese Rolle darf keine Belege bearbeiten.");
    Beleg beleg;
    if (!BelegById(belegId, beleg))
        return StoreResult::Fail("Der Beleg wurde nicht gefunden.");

    std::vector<uint8_t> digest;
    const UltraCryptResult hashed = UltraCrypt_HashFile(
        UltraCryptHashAlgorithm::SHA256, dateiPfad, digest);
    if (!hashed)
        return StoreResult::Fail("Die Datei konnte nicht gelesen werden: " + hashed.message);
    const std::string hash = UltraCrypt_ToHex(digest);

    // The path and the hash are writable on a posted document: attaching the
    // scan of an invoice that was posted from its data does not change the
    // booking, and refusing it would push people to keep the scan outside the
    // system, which is the worse outcome.
    const StoreResult updated = Exec(
        "UPDATE beleg SET datei_pfad = ?, datei_hash = ?, geaendert_am = ? WHERE id = ?",
        { dateiPfad, hash, NowSeconds(), belegId },
        "Die Datei konnte nicht hinterlegt werden");
    if (!updated) return updated;
    return WriteAudit(akteur, "beleg", belegId, "datei",
                      beleg.nummer + " " + dateiPfad + " sha256:" + hash);
}

bool Store::PruefeBelegDatei(const Beleg& beleg, std::string& fehler) const {
    if (beleg.dateiPfad.empty() || beleg.dateiHash.empty()) {
        fehler = "Zu diesem Beleg ist keine Datei hinterlegt.";
        return false;
    }
    std::vector<uint8_t> digest;
    const UltraCryptResult hashed = UltraCrypt_HashFile(
        UltraCryptHashAlgorithm::SHA256, beleg.dateiPfad, digest);
    if (!hashed) {
        fehler = "Die hinterlegte Datei \"" + beleg.dateiPfad +
                 "\" ist nicht lesbar: " + hashed.message;
        return false;
    }
    if (UltraCrypt_ToHex(digest) != beleg.dateiHash) {
        fehler = "Die hinterlegte Datei \"" + beleg.dateiPfad +
                 "\" stimmt nicht mehr mit der Prüfsumme überein, die beim Anhängen "
                 "gespeichert wurde.";
        return false;
    }
    fehler.clear();
    return true;
}

// ===== BUCHEN =====

namespace {

// One journal row in the making: the positions of a document that share an
// account and a tax key, summed. Grouping is what turns a twelve-line invoice
// into the two or three postings a bookkeeper expects to see, and it is done by
// key rather than by rate so that two keys at the same percentage - domestic
// revenue and a reverse-charge key, say - stay apart, because they post to
// different accounts and different UStVA boxes.
struct Buchungsgruppe {
    std::string konto;
    std::string steuerschluessel;
    int         satzPromille = 0;
    Money       netto;
    Money       steuer;
    std::string kost1;
    std::string kost2;
};

std::vector<Buchungsgruppe> GruppiereBeleg(const Beleg& beleg) {
    std::vector<Buchungsgruppe> gruppen;
    for (const BelegPosition& pos : beleg.positionen) {
        Buchungsgruppe* treffer = nullptr;
        for (Buchungsgruppe& g : gruppen) {
            if (g.konto == pos.konto && g.steuerschluessel == pos.steuerschluessel) {
                treffer = &g;
                break;
            }
        }
        if (treffer == nullptr) {
            Buchungsgruppe g;
            g.konto            = pos.konto;
            g.steuerschluessel = pos.steuerschluessel;
            g.satzPromille     = pos.satzPromille;
            g.netto            = Money::Zero(beleg.waehrung);
            g.steuer           = Money::Zero(beleg.waehrung);
            // The cost centre of the first position of the group. A group whose
            // positions disagree loses the distinction - which is why the group
            // key would have to grow if per-position cost centres are ever
            // wanted in the journal rather than only on the document.
            g.kost1 = pos.kostenstelle;
            g.kost2 = pos.kostentraeger;
            gruppen.push_back(g);
            treffer = &gruppen.back();
        }
        treffer->netto  = treffer->netto + pos.netto;
        treffer->steuer = treffer->steuer + pos.steuer;
    }
    return gruppen;
}

} // namespace

StoreResult Store::Buchen(Beleg& beleg, const Akteur& akteur) {
    if (!akteur.Darf(Recht::Buchen))
        return StoreResult::Fail("Diese Rolle darf nicht buchen.");
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");

    // Always post what is in the database, never what the caller happens to be
    // holding: an editor that has been open for ten minutes is not a source of
    // truth about amounts.
    Beleg aktuell;
    if (!BelegById(beleg.id, aktuell))
        return StoreResult::Fail("Der Beleg wurde nicht gefunden.");
    if (aktuell.status != BelegStatus::Entwurf)
        return StoreResult::Fail("Der Beleg " + aktuell.nummer + " ist bereits gebucht.");
    if (aktuell.positionen.empty())
        return StoreResult::Fail("Ein Beleg ohne Positionen kann nicht gebucht werden.");
    if (aktuell.partnerKonto.empty())
        return StoreResult::Fail(
            "Dem Beleg fehlt das Gegenkonto (Debitor, Kreditor oder Kasse), gegen das "
            "gebucht wird.");

    Geschaeftsjahr jahr;
    if (!GeschaeftsjahrAt(aktuell.mandantId, aktuell.datum, jahr))
        return StoreResult::Fail("Zum " + FormatDateGerman(aktuell.datum) +
                                 " ist kein Geschäftsjahr angelegt.");
    if (!jahr.AcceptsPostings())
        return StoreResult::Fail("Das Geschäftsjahr \"" + jahr.bezeichnung +
                                 "\" ist abgeschlossen.");
    if (jahr.IsFrozen(aktuell.datum)) {
        WriteAudit(akteur, "beleg", aktuell.id, "abgelehnt",
                   "Buchen in festgeschriebenen Zeitraum, " + aktuell.datum.ToIso());
        return StoreResult::Fail("Bis " + FormatDateGerman(jahr.festschreibungBis) +
                                 " ist festgeschrieben. Zum " +
                                 FormatDateGerman(aktuell.datum) +
                                 " kann nicht mehr gebucht werden.");
    }

    // **Does the document's tax treatment agree with itself?** Checked here
    // rather than only when printing, because posting is the irreversible step:
    // afterwards the entry can only be reversed, never corrected, and a posting
    // whose invoice charged 19 % while declaring the customer liable has gone
    // into the journal, the UStVA and the customer's books before anyone looks.
    {
        Partner partner;
        PartnerByKonto(aktuell.mandantId, aktuell.partnerKonto, partner);
        const std::vector<SteuerBefund> befunde = PruefeSteuerlicheStimmigkeit(
            aktuell, partner, SteuerschluesselListe(aktuell.mandantId));
        if (HatBlockierendenBefund(befunde)) {
            std::string text = "Die Umsatzsteuer des Belegs ist nicht stimmig:";
            for (const SteuerBefund& b : befunde) {
                if (!b.blockierend) continue;
                text += "\n  - ";
                if (!b.position.empty()) text += "Position \"" + b.position + "\": ";
                text += b.text;
            }
            WriteAudit(akteur, "beleg", aktuell.id, "abgelehnt",
                       "Steuerlich nicht stimmig: " + aktuell.nummer);
            return StoreResult::Fail(text);
        }
    }

    const std::vector<Buchungsgruppe> gruppen = GruppiereBeleg(aktuell);
    if (gruppen.empty())
        return StoreResult::Fail("Der Beleg ergibt keine Buchung.");

    const SollHaben seite   = PersonenkontoSeite(aktuell.art);
    const int       periode = jahr.PeriodOf(aktuell.datum);
    const int64_t   jetzt   = NowSeconds();

    // Resolve every tax key once, before the transaction opens, so a missing
    // key is a clean refusal rather than a rollback.
    std::vector<Steuerschluessel> schluessel(gruppen.size());
    for (size_t i = 0; i < gruppen.size(); ++i) {
        if (gruppen[i].steuerschluessel.empty()) continue;
        if (!SteuerschluesselByKey(aktuell.mandantId, gruppen[i].steuerschluessel,
                                   aktuell.datum, schluessel[i]))
            return StoreResult::Fail("Der Steuerschlüssel \"" +
                                     gruppen[i].steuerschluessel + "\" ist zum " +
                                     FormatDateGerman(aktuell.datum) + " nicht gültig.");
        if (!gruppen[i].steuer.IsZero() && schluessel[i].kontoSteuer.empty())
            return StoreResult::Fail("Dem Steuerschlüssel \"" +
                                     gruppen[i].steuerschluessel +
                                     "\" fehlt das Steuerkonto.");
    }

    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(connection_, &error);
    if (tx == UltraDbInvalidHandle)
        return StoreResult::Fail("Transaktion konnte nicht gestartet werden: " +
                                 error.message);

    std::string fehler;
    auto abbrechen = [&](const std::string& text) {
        UltraDb_Rollback(tx);
        return StoreResult::Fail(text + (fehler.empty() ? "" : ": " + fehler));
    };

    std::vector<Buchung> geschrieben;
    for (size_t i = 0; i < gruppen.size(); ++i) {
        const Buchungsgruppe& g = gruppen[i];

        Buchung b;
        b.mandantId        = aktuell.mandantId;
        b.geschaeftsjahrId = jahr.id;
        b.periode          = periode;
        b.belegdatum       = aktuell.datum;
        b.belegId          = aktuell.id;
        b.belegfeld1       = aktuell.nummer;
        b.belegfeld2       = aktuell.externeNummer;
        // The settlement account carries the gross and the Soll/Haben flag; the
        // revenue or expense account is the net side, and the tax is the
        // automatic posting between them. See UltraFIBUBuchung.h.
        b.konto      = aktuell.partnerKonto;
        b.gegenkonto = g.konto;
        b.sollHaben  = seite;
        b.netto      = g.netto;
        b.steuer     = g.steuer;
        b.umsatz     = g.netto + g.steuer;
        if (!b.umsatz.Valid()) { fehler = "Betrag ungültig"; return abbrechen("Die Buchung konnte nicht gebildet werden"); }
        b.steuerschluessel = g.steuerschluessel;
        b.satzPromille     = g.satzPromille;
        if (g.steuer.IsZero()) {
            // A zero-rated key still names itself - the UStVA needs it, and so
            // does the DATEV export - but there is no automatic posting, so
            // there is no net side and no tax account.
            b.steuerSeite  = SteuerSeite::Keine;
            b.steuerkonto.clear();
        } else {
            b.steuerSeite = SteuerSeite::Gegenkonto;
            b.steuerkonto = schluessel[i].kontoSteuer;
        }
        b.buSchluessel  = schluessel[i].datevBu;
        b.buchungstext  = aktuell.buchungstext.empty()
                              ? (BelegArtLabel(aktuell.art) + " " + aktuell.partnerName)
                              : aktuell.buchungstext;
        b.kost1         = g.kost1;
        b.kost2         = g.kost2;
        b.waehrung      = aktuell.waehrung;
        b.erfasstVon    = akteur.benutzerId;
        b.erfasstVonName = akteur.anmeldename;
        b.erfasstAm     = jetzt;

        if (!b.Valid()) {
            fehler = "Konto und Gegenkonto müssen verschieden und gesetzt sein";
            return abbrechen("Die Buchung konnte nicht gebildet werden");
        }
        if (!InsertBuchungInTx(tx, b, fehler, RowLock()))
            return abbrechen("Die Buchung konnte nicht geschrieben werden");
        geschrieben.push_back(b);
    }

    const UltraDbResult updated = UltraDb_ExecInTx(
        tx,
        "UPDATE beleg SET status = ?, geschaeftsjahr_id = ?, geaendert_am = ?,"
        " version = version + 1 WHERE id = ? AND status = ?",
        { BelegStatusToText(BelegStatus::Gebucht), jahr.id, jetzt, aktuell.id,
          BelegStatusToText(BelegStatus::Entwurf) });
    if (!updated) { fehler = updated.message; return abbrechen("Der Beleg konnte nicht gebucht werden"); }

    if (!AuditInTx(tx, akteur, "beleg", aktuell.id, "buchen",
                   aktuell.nummer + " " + aktuell.brutto.ToString() + ", " +
                   Number(static_cast<int64_t>(geschrieben.size())) + " Buchung(en)",
                   fehler, RowLock()))
        return abbrechen("Der Protokolleintrag konnte nicht geschrieben werden");

    const UltraDbResult commit = UltraDb_Commit(tx);
    if (!commit)
        return StoreResult::Fail("Die Buchung wurde nicht bestätigt: " + commit.message);

    beleg = aktuell;
    beleg.status = BelegStatus::Gebucht;
    beleg.geschaeftsjahrId = jahr.id;
    beleg.geaendertAm = jetzt;
    beleg.version = aktuell.version + 1;
    return StoreResult::Ok();
}

// ===== STORNO =====

StoreResult Store::StorniereBeleg(int64_t belegId, const Date& stornoDatum,
                                  const std::string& grund, const std::string& kreis,
                                  const Akteur& akteur, Beleg& outStorno) {
    if (!akteur.Darf(Recht::Buchen))
        return StoreResult::Fail("Diese Rolle darf nicht stornieren.");
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");
    if (!stornoDatum.Valid())
        return StoreResult::Fail("Die Stornierung braucht ein gültiges Datum.");

    Beleg original;
    if (!BelegById(belegId, original))
        return StoreResult::Fail("Der Beleg wurde nicht gefunden.");
    if (original.status == BelegStatus::Entwurf)
        return StoreResult::Fail(
            "Der Beleg " + original.nummer + " ist noch nicht gebucht und kann "
            "geändert oder gelöscht werden - eine Stornierung wäre eine Buchung "
            "ohne Vorgang.");
    if (original.IstStorniert())
        return StoreResult::Fail("Der Beleg " + original.nummer +
                                 " ist bereits storniert.");

    // The reversal is dated into an open period. Its *original* may well lie in
    // a frozen one - that is the normal case and exactly what a Storno is for.
    Geschaeftsjahr jahr;
    if (!GeschaeftsjahrAt(original.mandantId, stornoDatum, jahr))
        return StoreResult::Fail("Zum " + FormatDateGerman(stornoDatum) +
                                 " ist kein Geschäftsjahr angelegt.");
    if (!jahr.AcceptsPostings())
        return StoreResult::Fail("Das Geschäftsjahr \"" + jahr.bezeichnung +
                                 "\" ist abgeschlossen.");
    if (jahr.IsFrozen(stornoDatum))
        return StoreResult::Fail("Bis " + FormatDateGerman(jahr.festschreibungBis) +
                                 " ist festgeschrieben. Die Stornierung muss auf ein "
                                 "späteres Datum gebucht werden.");

    // Only the document's *own* postings are reversed. The payment postings
    // that also hang off this document are deliberately left alone: the money
    // really did arrive, and reversing the bank leg would make the bank balance
    // disagree with the bank statement, which is the one number in a
    // bookkeeping system that is checked against the outside world. What
    // remains afterwards is a credit on the person account - the customer paid
    // for an invoice that no longer exists and is owed the money - which is
    // both true and the starting point for a refund or a reallocation.
    std::vector<int64_t> zahlungsbuchungen;
    for (const Zahlung& z : Zahlungen(belegId)) {
        if (z.buchungId != 0) zahlungsbuchungen.push_back(z.buchungId);
    }

    std::vector<Buchung> original_buchungen;
    for (const Buchung& b : BuchungenZuBeleg(belegId)) {
        bool istZahlung = false;
        for (int64_t id : zahlungsbuchungen) {
            if (id == b.id) { istZahlung = true; break; }
        }
        if (!istZahlung) original_buchungen.push_back(b);
    }
    if (original_buchungen.empty())
        return StoreResult::Fail("Zu diesem Beleg gibt es keine stornierbaren Buchungen.");

    const int     periode = jahr.PeriodOf(stornoDatum);
    const int64_t jetzt   = NowSeconds();

    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(connection_, &error);
    if (tx == UltraDbInvalidHandle)
        return StoreResult::Fail("Transaktion konnte nicht gestartet werden: " +
                                 error.message);

    std::string fehler;
    auto abbrechen = [&](const std::string& text) {
        UltraDb_Rollback(tx);
        return StoreResult::Fail(text + (fehler.empty() ? "" : ": " + fehler));
    };

    // 1. The reversing document: the same kind, the same partner, its own
    //    number, every amount negated. It is created already in the Gebucht
    //    state, because its postings are written in this same transaction.
    Beleg storno = original;
    storno.id = 0;
    storno.nummer.clear();
    storno.datum            = stornoDatum;
    storno.faelligAm        = stornoDatum;
    storno.geschaeftsjahrId = jahr.id;
    storno.status           = BelegStatus::Gebucht;
    storno.stornoVon        = original.id;
    storno.storniertDurch   = 0;
    storno.bezahlt          = Money::Zero(original.waehrung);
    storno.festgeschrieben  = false;
    storno.buchungstext     = "Storno " + original.nummer +
                              (grund.empty() ? "" : ": " + grund);
    storno.notiz            = grund;
    storno.dateiPfad.clear();
    storno.dateiHash.clear();
    storno.erfasstVon       = akteur.benutzerId;
    storno.erfasstVonName   = akteur.anmeldename;
    storno.erfasstAm        = jetzt;
    storno.geaendertAm      = jetzt;
    storno.version          = 1;
    for (BelegPosition& pos : storno.positionen) {
        pos.id = 0;
        pos.belegId = 0;
        pos.einzelpreis = -pos.einzelpreis;
    }
    // The positions already carry the rates that applied to the original, so
    // this re-costs against those and not against today's table - a Storno of a
    // 2026 invoice must reverse 19 %, whatever the rate is when it is written.
    if (!storno.Summieren()) {
        fehler = "Beträge nicht berechenbar";
        return abbrechen("Der Stornobeleg konnte nicht gebildet werden");
    }

    if (!NextBelegnummerInTx(tx, storno.mandantId, kreis, stornoDatum, storno.nummer,
                             fehler, RowLock()))
        return abbrechen("Die Belegnummer für die Stornierung konnte nicht vergeben werden");
    if (!NextSequenzInTx(tx, "beleg", storno.id, fehler, RowLock()))
        return abbrechen("Der Stornobeleg konnte nicht angelegt werden");

    const UltraDbResult insertedKopf = UltraDb_ExecInTx(
        tx,
        "INSERT INTO beleg(id, mandant_id, geschaeftsjahr_id, art, nummer,"
        " externe_nummer, datum, leistung_von, leistung_bis, faellig_am, partner_id,"
        " partner_konto, partner_name, waehrung, netto, steuer, brutto, bezahlt,"
        " status, buchungstext, notiz, datei_pfad, datei_hash, storno_von,"
        " storniert_durch, festgeschrieben, erfasst_von, erfasst_von_name, erfasst_am,"
        " geaendert_am, version)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        { storno.id, storno.mandantId, storno.geschaeftsjahrId,
          BelegArtToText(storno.art), storno.nummer, storno.externeNummer,
          storno.datum.ToIso(), DateValue(storno.leistungVon),
          DateValue(storno.leistungBis), DateValue(storno.faelligAm), storno.partnerId,
          storno.partnerKonto, storno.partnerName, storno.waehrung,
          storno.netto.Minor(), storno.steuer.Minor(), storno.brutto.Minor(),
          int64_t(0), BelegStatusToText(storno.status), storno.buchungstext,
          storno.notiz, std::string(), std::string(), storno.stornoVon, int64_t(0),
          int64_t(0), storno.erfasstVon, storno.erfasstVonName, storno.erfasstAm,
          storno.geaendertAm, storno.version });
    if (!insertedKopf) { fehler = insertedKopf.message; return abbrechen("Der Stornobeleg konnte nicht angelegt werden"); }

    int posNr = 1;
    for (BelegPosition& pos : storno.positionen) {
        pos.belegId  = storno.id;
        pos.position = posNr++;
        if (!NextSequenzInTx(tx, "beleg_position", pos.id, fehler, RowLock()))
            return abbrechen("Die Stornoposition konnte nicht angelegt werden");
        const UltraDbResult inserted = UltraDb_ExecInTx(
            tx,
            "INSERT INTO beleg_position(id, beleg_id, position, bezeichnung,"
            " menge_tausendstel, einheit, einzelpreis, rabatt_promille, konto,"
            " steuerschluessel, satz_promille, netto, steuer, brutto, kostenstelle,"
            " kostentraeger, waehrung) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            { pos.id, pos.belegId, pos.position, pos.bezeichnung, pos.mengeTausendstel,
              pos.einheit, pos.einzelpreis.Minor(), pos.rabattPromille, pos.konto,
              pos.steuerschluessel, pos.satzPromille, pos.netto.Minor(),
              pos.steuer.Minor(), pos.brutto.Minor(), pos.kostenstelle,
              pos.kostentraeger, storno.waehrung });
        if (!inserted) { fehler = inserted.message; return abbrechen("Die Stornoposition konnte nicht angelegt werden"); }
    }

    // 2. The reversing postings: the same amounts, Soll and Haben exchanged.
    //    Not re-derived from the document - taken from the rows that were
    //    actually written, so a Storno reverses what happened rather than what
    //    would happen if the document were posted again today.
    for (const Buchung& ursprung : original_buchungen) {
        if (ursprung.IstStorniert()) continue;

        Buchung gegen = ursprung;
        gegen.id              = 0;
        gegen.belegId         = storno.id;
        gegen.belegfeld1      = storno.nummer;
        gegen.belegdatum      = stornoDatum;
        gegen.geschaeftsjahrId = jahr.id;
        gegen.periode         = periode;
        gegen.sollHaben       = SollHabenUmgekehrt(ursprung.sollHaben);
        gegen.stornoVon       = ursprung.id;
        gegen.storniertDurch  = 0;
        gegen.festgeschrieben = false;
        gegen.buchungstext    = "Storno " + original.nummer +
                                (grund.empty() ? "" : ": " + grund);
        gegen.erfasstVon      = akteur.benutzerId;
        gegen.erfasstVonName  = akteur.anmeldename;
        gegen.erfasstAm       = jetzt;
        gegen.laufendeNummer  = 0;
        gegen.prevHash.clear();
        gegen.hash.clear();

        if (!InsertBuchungInTx(tx, gegen, fehler, RowLock()))
            return abbrechen("Die Stornobuchung konnte nicht geschrieben werden");

        // The back-reference on the sealed row. This column is deliberately
        // outside the hashed canonical form (UltraFIBUBuchung.h): it is set
        // after the fact, and a chain that broke every time a row was reversed
        // would be a chain nobody could check.
        const UltraDbResult markiert = UltraDb_ExecInTx(
            tx, "UPDATE buchung SET storniert_durch = ? WHERE id = ?",
            { gegen.id, ursprung.id });
        if (!markiert) { fehler = markiert.message; return abbrechen("Die Ursprungsbuchung konnte nicht markiert werden"); }
    }

    // 3. The original document, marked.
    const UltraDbResult markiert = UltraDb_ExecInTx(
        tx,
        "UPDATE beleg SET status = ?, storniert_durch = ?, geaendert_am = ?,"
        " version = version + 1 WHERE id = ?",
        { BelegStatusToText(BelegStatus::Storniert), storno.id, jetzt, original.id });
    if (!markiert) { fehler = markiert.message; return abbrechen("Der Ursprungsbeleg konnte nicht markiert werden"); }

    if (!AuditInTx(tx, akteur, "beleg", original.id, "stornieren",
                   original.nummer + " storniert durch " + storno.nummer +
                   (grund.empty() ? "" : " (" + grund + ")"), fehler, RowLock()))
        return abbrechen("Der Protokolleintrag konnte nicht geschrieben werden");

    const UltraDbResult commit = UltraDb_Commit(tx);
    if (!commit)
        return StoreResult::Fail("Die Stornierung wurde nicht bestätigt: " + commit.message);

    outStorno = storno;
    return StoreResult::Ok();
}

// ===== ZAHLUNGEN =====

StoreResult Store::ZahlungErfassen(int64_t belegId, const Date& datum, const Money& betrag,
                                   const std::string& geldkonto, const std::string& notiz,
                                   const Akteur& akteur) {
    if (!akteur.Darf(Recht::Buchen))
        return StoreResult::Fail("Diese Rolle darf keine Zahlungen buchen.");
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");
    if (!datum.Valid()) return StoreResult::Fail("Die Zahlung braucht ein gültiges Datum.");
    if (geldkonto.empty())
        return StoreResult::Fail("Der Zahlung fehlt das Geldkonto (Bank oder Kasse).");
    // `betrag` is the amount that moved, always positive. Which way it moved
    // follows from the kind of document, not from a sign somebody typed.
    if (!betrag.Valid() || !betrag.IsPositive())
        return StoreResult::Fail("Der Zahlbetrag muss größer als null sein.");

    Beleg beleg;
    if (!BelegById(belegId, beleg))
        return StoreResult::Fail("Der Beleg wurde nicht gefunden.");
    if (beleg.status != BelegStatus::Gebucht &&
        beleg.status != BelegStatus::TeilweiseBezahlt)
        return StoreResult::Fail("Zu einem Beleg im Status \"" +
                                 BelegStatusLabel(beleg.status) +
                                 "\" kann keine Zahlung gebucht werden.");
    if (beleg.partnerKonto.empty())
        return StoreResult::Fail("Dem Beleg fehlt das Personenkonto.");
    if (beleg.partnerKonto == geldkonto)
        return StoreResult::Fail("Geldkonto und Personenkonto des Belegs sind dasselbe.");

    // Compare magnitudes: a credit note's total is negative and its "payment"
    // is a refund, but in both cases what is tracked is how much of the
    // document has been settled.
    const int64_t offenMinor  = beleg.brutto.Minor() < 0 ? -beleg.brutto.Minor()
                                                         : beleg.brutto.Minor();
    const int64_t bisherMinor = beleg.bezahlt.Valid() ? beleg.bezahlt.Minor() : 0;
    if (bisherMinor + betrag.Minor() > offenMinor)
        return StoreResult::Fail(
            "Die Zahlung übersteigt den offenen Betrag des Belegs " + beleg.nummer +
            ". Offen sind " +
            Money::FromMinor(offenMinor - bisherMinor, beleg.waehrung).ToString() + ".");

    Geschaeftsjahr jahr;
    if (!GeschaeftsjahrAt(beleg.mandantId, datum, jahr))
        return StoreResult::Fail("Zum " + FormatDateGerman(datum) +
                                 " ist kein Geschäftsjahr angelegt.");
    if (!jahr.AcceptsPostings())
        return StoreResult::Fail("Das Geschäftsjahr \"" + jahr.bezeichnung +
                                 "\" ist abgeschlossen.");
    if (jahr.IsFrozen(datum))
        return StoreResult::Fail("Bis " + FormatDateGerman(jahr.festschreibungBis) +
                                 " ist festgeschrieben. Die Zahlung muss auf ein "
                                 "späteres Datum gebucht werden.");

    const int64_t neuMinor = bisherMinor + betrag.Minor();
    const BelegStatus neuerStatus = (neuMinor == offenMinor)
                                        ? BelegStatus::Bezahlt
                                        : BelegStatus::TeilweiseBezahlt;
    const int64_t jetzt = NowSeconds();

    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(connection_, &error);
    if (tx == UltraDbInvalidHandle)
        return StoreResult::Fail("Transaktion konnte nicht gestartet werden: " +
                                 error.message);

    std::string fehler;
    auto abbrechen = [&](const std::string& text) {
        UltraDb_Rollback(tx);
        return StoreResult::Fail(text + (fehler.empty() ? "" : ": " + fehler));
    };

    // The money account moves the same way the person account did when the
    // document was posted: a receivable that was a debit is collected into a
    // bank debit. No tax: under Soll-Versteuerung the VAT fell due with the
    // invoice, and under Ist-Versteuerung it is the projection that keys on
    // this date, not a second tax posting.
    Buchung b;
    b.mandantId        = beleg.mandantId;
    b.geschaeftsjahrId = jahr.id;
    b.periode          = jahr.PeriodOf(datum);
    b.belegdatum       = datum;
    b.belegId          = beleg.id;
    b.belegfeld1       = beleg.nummer;
    b.umsatz           = betrag;
    b.sollHaben        = PersonenkontoSeite(beleg.art);
    b.konto            = geldkonto;
    b.gegenkonto       = beleg.partnerKonto;
    b.steuerSeite      = SteuerSeite::Keine;
    b.netto            = betrag;
    b.steuer           = Money::Zero(beleg.waehrung);
    b.buchungstext     = notiz.empty() ? ("Zahlung " + beleg.nummer) : notiz;
    b.waehrung         = beleg.waehrung;
    b.erfasstVon       = akteur.benutzerId;
    b.erfasstVonName   = akteur.anmeldename;
    b.erfasstAm        = jetzt;

    if (!InsertBuchungInTx(tx, b, fehler, RowLock()))
        return abbrechen("Die Zahlung konnte nicht gebucht werden");

    int64_t zahlungId = 0;
    if (!NextSequenzInTx(tx, "zahlung", zahlungId, fehler, RowLock()))
        return abbrechen("Die Zahlung konnte nicht gespeichert werden");
    const UltraDbResult inserted = UltraDb_ExecInTx(
        tx,
        "INSERT INTO zahlung(id, mandant_id, beleg_id, datum, betrag, waehrung,"
        " geldkonto, buchung_id, notiz, erfasst_von, erfasst_am)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?)",
        { zahlungId, beleg.mandantId, beleg.id, datum.ToIso(), betrag.Minor(),
          beleg.waehrung, geldkonto, b.id, notiz, akteur.benutzerId, jetzt });
    if (!inserted) { fehler = inserted.message; return abbrechen("Die Zahlung konnte nicht gespeichert werden"); }

    const UltraDbResult updated = UltraDb_ExecInTx(
        tx,
        "UPDATE beleg SET bezahlt = ?, status = ?, geaendert_am = ?,"
        " version = version + 1 WHERE id = ?",
        { neuMinor, BelegStatusToText(neuerStatus), jetzt, beleg.id });
    if (!updated) { fehler = updated.message; return abbrechen("Der Zahlungsstand konnte nicht fortgeschrieben werden"); }

    if (!AuditInTx(tx, akteur, "zahlung", zahlungId, "buchen",
                   beleg.nummer + " " + betrag.ToString() + " auf " + geldkonto, fehler, RowLock()))
        return abbrechen("Der Protokolleintrag konnte nicht geschrieben werden");

    const UltraDbResult commit = UltraDb_Commit(tx);
    if (!commit)
        return StoreResult::Fail("Die Zahlung wurde nicht bestätigt: " + commit.message);
    return StoreResult::Ok();
}

std::vector<Store::Zahlung> Store::Zahlungen(int64_t belegId) const {
    std::vector<Zahlung> liste;
    UltraDbResultSet rs;
    if (!Query("SELECT id, beleg_id, datum, betrag, waehrung, geldkonto, buchung_id,"
               " notiz, erfasst_von, erfasst_am FROM zahlung WHERE beleg_id = ?"
               " ORDER BY datum, id", { belegId }, rs))
        return liste;
    for (const UltraDbRow& row : rs) {
        Zahlung z;
        z.id         = row["id"].AsInt64();
        z.belegId    = row["beleg_id"].AsInt64();
        z.datum      = DateFrom(row["datum"]);
        z.betrag     = MoneyFrom(row["betrag"], row["waehrung"].AsString());
        z.geldkonto  = row["geldkonto"].AsString();
        z.buchungId  = row["buchung_id"].AsInt64();
        z.notiz      = row["notiz"].AsString();
        z.erfasstVon = row["erfasst_von"].AsInt64();
        z.erfasstAm  = row["erfasst_am"].AsInt64();
        liste.push_back(z);
    }
    return liste;
}

// ===== JOURNAL =====

StoreResult Store::BuchungErfassen(Buchung& buchung, const Akteur& akteur) {
    if (!akteur.Darf(Recht::Buchen))
        return StoreResult::Fail("Diese Rolle darf nicht buchen.");
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");
    if (!buchung.belegdatum.Valid())
        return StoreResult::Fail("Die Buchung braucht ein gültiges Belegdatum.");
    if (buchung.konto.empty() || buchung.gegenkonto.empty())
        return StoreResult::Fail("Konto und Gegenkonto müssen angegeben sein.");
    if (buchung.konto == buchung.gegenkonto)
        return StoreResult::Fail("Konto und Gegenkonto dürfen nicht dasselbe sein.");
    if (!buchung.umsatz.Valid() || buchung.umsatz.IsNegative())
        return StoreResult::Fail(
            "Der Umsatz einer Buchung ist nie negativ - die Richtung sagt das "
            "Soll-/Haben-Kennzeichen.");

    Geschaeftsjahr jahr;
    if (!GeschaeftsjahrAt(buchung.mandantId, buchung.belegdatum, jahr))
        return StoreResult::Fail("Zum " + FormatDateGerman(buchung.belegdatum) +
                                 " ist kein Geschäftsjahr angelegt.");
    if (!jahr.AcceptsPostings())
        return StoreResult::Fail("Das Geschäftsjahr \"" + jahr.bezeichnung +
                                 "\" ist abgeschlossen.");
    if (jahr.IsFrozen(buchung.belegdatum)) {
        WriteAudit(akteur, "buchung", 0, "abgelehnt",
                   "Buchen in festgeschriebenen Zeitraum, " +
                   buchung.belegdatum.ToIso());
        return StoreResult::Fail("Bis " + FormatDateGerman(jahr.festschreibungBis) +
                                 " ist festgeschrieben. Zum " +
                                 FormatDateGerman(buchung.belegdatum) +
                                 " kann nicht mehr gebucht werden.");
    }

    // The tax split is the caller's to state, but it has to add up: a row whose
    // netto and steuer do not sum to its umsatz would put a Saldenliste out by
    // exactly that difference, and nothing downstream would notice.
    if (buchung.steuerSeite != SteuerSeite::Keine) {
        if (buchung.steuerkonto.empty())
            return StoreResult::Fail("Einer Buchung mit Steueranteil fehlt das Steuerkonto.");
        const Money summe = buchung.netto + buchung.steuer;
        if (!summe.Valid() || summe.Minor() != buchung.umsatz.Minor())
            return StoreResult::Fail("Netto und Steuer ergeben nicht den Umsatz der Buchung.");
    } else {
        buchung.netto  = buchung.umsatz;
        buchung.steuer = Money::Zero(buchung.waehrung);
        buchung.steuerkonto.clear();
    }

    buchung.geschaeftsjahrId = jahr.id;
    buchung.periode          = jahr.PeriodOf(buchung.belegdatum);
    buchung.erfasstVon       = akteur.benutzerId;
    buchung.erfasstVonName   = akteur.anmeldename;
    buchung.erfasstAm        = NowSeconds();
    buchung.festgeschrieben  = false;
    buchung.storniertDurch   = 0;

    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(connection_, &error);
    if (tx == UltraDbInvalidHandle)
        return StoreResult::Fail("Transaktion konnte nicht gestartet werden: " +
                                 error.message);

    std::string fehler;
    if (!InsertBuchungInTx(tx, buchung, fehler, RowLock())) {
        UltraDb_Rollback(tx);
        return StoreResult::Fail("Die Buchung konnte nicht geschrieben werden: " + fehler);
    }
    if (!AuditInTx(tx, akteur, "buchung", buchung.id, "buchen",
                   buchung.konto + " an " + buchung.gegenkonto + " " +
                   buchung.umsatz.ToString(), fehler, RowLock())) {
        UltraDb_Rollback(tx);
        return StoreResult::Fail("Der Protokolleintrag konnte nicht geschrieben werden: " +
                                 fehler);
    }

    const UltraDbResult commit = UltraDb_Commit(tx);
    if (!commit)
        return StoreResult::Fail("Die Buchung wurde nicht bestätigt: " + commit.message);
    return StoreResult::Ok();
}

StoreResult Store::StorniereBuchung(int64_t buchungId, const Date& stornoDatum,
                                    const Akteur& akteur, Buchung& outStorno) {
    if (!akteur.Darf(Recht::Buchen))
        return StoreResult::Fail("Diese Rolle darf nicht stornieren.");
    if (!stornoDatum.Valid())
        return StoreResult::Fail("Die Stornierung braucht ein gültiges Datum.");

    Buchung original;
    if (!BuchungById(buchungId, original))
        return StoreResult::Fail("Die Buchung wurde nicht gefunden.");
    if (original.IstStorniert())
        return StoreResult::Fail("Diese Buchung ist bereits storniert.");

    Geschaeftsjahr jahr;
    if (!GeschaeftsjahrAt(original.mandantId, stornoDatum, jahr))
        return StoreResult::Fail("Zum " + FormatDateGerman(stornoDatum) +
                                 " ist kein Geschäftsjahr angelegt.");
    if (!jahr.AcceptsPostings())
        return StoreResult::Fail("Das Geschäftsjahr \"" + jahr.bezeichnung +
                                 "\" ist abgeschlossen.");
    if (jahr.IsFrozen(stornoDatum))
        return StoreResult::Fail("Bis " + FormatDateGerman(jahr.festschreibungBis) +
                                 " ist festgeschrieben. Die Stornierung muss auf ein "
                                 "späteres Datum gebucht werden.");

    Buchung gegen = original;
    gegen.id               = 0;
    gegen.belegdatum       = stornoDatum;
    gegen.geschaeftsjahrId = jahr.id;
    gegen.periode          = jahr.PeriodOf(stornoDatum);
    gegen.sollHaben        = SollHabenUmgekehrt(original.sollHaben);
    gegen.stornoVon        = original.id;
    gegen.storniertDurch   = 0;
    gegen.festgeschrieben  = false;
    gegen.buchungstext     = "Storno: " + original.buchungstext;
    gegen.erfasstVon       = akteur.benutzerId;
    gegen.erfasstVonName   = akteur.anmeldename;
    gegen.erfasstAm        = NowSeconds();
    gegen.laufendeNummer   = 0;
    gegen.prevHash.clear();
    gegen.hash.clear();

    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(connection_, &error);
    if (tx == UltraDbInvalidHandle)
        return StoreResult::Fail("Transaktion konnte nicht gestartet werden: " +
                                 error.message);

    std::string fehler;
    auto abbrechen = [&](const std::string& text) {
        UltraDb_Rollback(tx);
        return StoreResult::Fail(text + (fehler.empty() ? "" : ": " + fehler));
    };

    if (!InsertBuchungInTx(tx, gegen, fehler, RowLock()))
        return abbrechen("Die Stornobuchung konnte nicht geschrieben werden");

    const UltraDbResult markiert = UltraDb_ExecInTx(
        tx, "UPDATE buchung SET storniert_durch = ? WHERE id = ?",
        { gegen.id, original.id });
    if (!markiert) { fehler = markiert.message; return abbrechen("Die Ursprungsbuchung konnte nicht markiert werden"); }

    if (!AuditInTx(tx, akteur, "buchung", original.id, "stornieren",
                   "storniert durch Buchung " + Number(gegen.id), fehler, RowLock()))
        return abbrechen("Der Protokolleintrag konnte nicht geschrieben werden");

    const UltraDbResult commit = UltraDb_Commit(tx);
    if (!commit)
        return StoreResult::Fail("Die Stornierung wurde nicht bestätigt: " + commit.message);

    outStorno = gegen;
    return StoreResult::Ok();
}

bool Store::BuchungById(int64_t id, Buchung& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kBuchungSpalten + " FROM buchung WHERE id = ?",
                  { id }, row))
        return false;
    out = BuchungFromRow(row);
    return true;
}

std::vector<Buchung> Store::Journal(int64_t mandantId, const Date& von,
                                    const Date& bis) const {
    std::vector<Buchung> liste;
    std::string sql = std::string("SELECT ") + kBuchungSpalten +
                      " FROM buchung WHERE mandant_id = ?";
    UltraDbParams params = { mandantId };
    if (von.Valid()) { sql += " AND belegdatum >= ?"; params.push_back(von.ToIso()); }
    if (bis.Valid()) { sql += " AND belegdatum <= ?"; params.push_back(bis.ToIso()); }
    // Date first, then the order the rows were written - the only stable
    // tiebreak, and the one a journal print has to reproduce.
    sql += " ORDER BY belegdatum, laufende_nummer";

    UltraDbResultSet rs;
    if (!Query(sql, params, rs)) return liste;
    for (const UltraDbRow& row : rs) liste.push_back(BuchungFromRow(row));
    return liste;
}

std::vector<Buchung> Store::BuchungenZuBeleg(int64_t belegId) const {
    std::vector<Buchung> liste;
    UltraDbResultSet rs;
    if (!Query(std::string("SELECT ") + kBuchungSpalten +
                   " FROM buchung WHERE beleg_id = ? ORDER BY laufende_nummer",
               { belegId }, rs))
        return liste;
    for (const UltraDbRow& row : rs) liste.push_back(BuchungFromRow(row));
    return liste;
}

HashKettenPruefung Store::PruefeHashKette(int64_t mandantId) const {
    HashKettenPruefung ergebnis;

    UltraDbResultSet rs;
    if (!Query(std::string("SELECT ") + kBuchungSpalten +
                   " FROM buchung WHERE mandant_id = ? ORDER BY laufende_nummer",
               { mandantId }, rs)) {
        ergebnis.ok = false;
        ergebnis.fehler = "Das Journal konnte nicht gelesen werden.";
        return ergebnis;
    }

    std::string erwartetPrev;
    int64_t     erwarteteNummer = 1;

    for (const UltraDbRow& row : rs) {
        const Buchung b = BuchungFromRow(row);
        ergebnis.geprueft++;

        auto scheitern = [&](const std::string& text) {
            ergebnis.ok = false;
            ergebnis.ersteFehlerhafteId = b.id;
            ergebnis.ersteFehlerhafteNummer = b.laufendeNummer;
            ergebnis.fehler = text;
            return ergebnis;
        };

        // A gap in the numbering means a row was deleted - which the chain
        // would otherwise only reveal at the next link.
        if (b.laufendeNummer != erwarteteNummer)
            return scheitern("Im Journal fehlt die laufende Nummer " +
                             Number(erwarteteNummer) + "; die nächste vorhandene "
                             "Buchung trägt " + Number(b.laufendeNummer) + ".");
        if (b.prevHash != erwartetPrev)
            return scheitern("Die Buchung mit der laufenden Nummer " +
                             Number(b.laufendeNummer) +
                             " verweist nicht auf ihre Vorgängerin.");

        const std::string neu = BerechneHash(b.prevHash, b);
        if (neu.empty())
            return scheitern("Die Prüfsumme der Buchung " + Number(b.laufendeNummer) +
                             " konnte nicht berechnet werden.");
        if (neu != b.hash)
            return scheitern("Die Buchung mit der laufenden Nummer " +
                             Number(b.laufendeNummer) +
                             " wurde nach dem Erfassen verändert.");

        erwartetPrev = b.hash;
        erwarteteNummer++;
    }
    return ergebnis;
}

// ===== BANK =====

namespace {

Bankkonto BankkontoAusZeile(const UltraDbRow& row) {
    Bankkonto k;
    k.id          = row["id"].AsInt64();
    k.mandantId   = row["mandant_id"].AsInt64();
    k.bezeichnung = row["bezeichnung"].AsString();
    k.iban        = row["iban"].AsString();
    k.bic         = row["bic"].AsString();
    k.bank        = row["bank"].AsString();
    k.konto       = row["konto"].AsString();
    k.waehrung    = row["waehrung"].AsString();
    if (k.waehrung.empty()) k.waehrung = "EUR";
    k.csvProfil   = row["csv_profil"].AsString();
    k.aktiv       = row["aktiv"].AsInt() != 0;
    k.letzterImportBis = DateFrom(row["letzter_import_bis"]);
    k.version     = row["version"].AsInt64();
    return k;
}

Bankumsatz UmsatzAusZeile(const UltraDbRow& row) {
    Bankumsatz u;
    u.id              = row["id"].AsInt64();
    u.bankkontoId     = row["bankkonto_id"].AsInt64();
    u.buchungstag     = DateFrom(row["buchungstag"]);
    u.valuta          = DateFrom(row["valuta"]);
    const std::string waehrung =
        row["waehrung"].AsString().empty() ? "EUR" : row["waehrung"].AsString();
    u.betrag          = MoneyFrom(row["betrag"], waehrung);
    u.gegenIban       = row["gegen_iban"].AsString();
    u.gegenBic        = row["gegen_bic"].AsString();
    u.gegenName       = row["gegen_name"].AsString();
    u.verwendungszweck = row["verwendungszweck"].AsString();
    u.endToEndId      = row["e2e_ref"].AsString();
    u.mandatsreferenz = row["mandatsreferenz"].AsString();
    u.glaeubigerId    = row["glaeubiger_id"].AsString();
    u.buchungstext    = row["buchungstext"].AsString();
    u.referenz        = row["referenz"].AsString();
    u.teilbuchungen   = row["teilbuchungen"].AsInt();
    return u;
}

const char* const kUmsatzSpalten =
    "id, bankkonto_id, mandant_id, buchungstag, valuta, betrag, waehrung,"
    " gegen_iban, gegen_bic, gegen_name, verwendungszweck, e2e_ref,"
    " mandatsreferenz, glaeubiger_id, buchungstext, referenz, teilbuchungen";

} // namespace

StoreResult Store::SaveBankkonto(Bankkonto& konto, const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Stammdaten ändern.");
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");
    if (!konto.Valid())
        return StoreResult::Fail("Ein Bankkonto braucht eine Bezeichnung und ein "
                                 "Sachkonto, auf das seine Bewegungen gebucht werden.");

    // Two bank accounts with one IBAN would make every import ambiguous, and
    // the ambiguity would only show as lines landing on the wrong account.
    if (!konto.iban.empty()) {
        Bankkonto vorhanden;
        if (BankkontoByIban(konto.mandantId, konto.iban, vorhanden) &&
            vorhanden.id != konto.id) {
            return StoreResult::Fail("Die IBAN " + konto.iban +
                                     " gehört bereits zum Bankkonto \"" +
                                     vorhanden.bezeichnung + "\".");
        }
    }

    if (konto.id == 0) {
        const StoreResult id = NextSequenceValue("bankkonto", konto.id);
        if (!id) return id;
        const StoreResult r = Exec(
            "INSERT INTO bankkonto(id, mandant_id, bezeichnung, iban, bic, bank,"
            " konto, waehrung, csv_profil, aktiv, letzter_import_bis, version)"
            " VALUES(?,?,?,?,?,?,?,?,?,?,?,1)",
            { konto.id, konto.mandantId, konto.bezeichnung, konto.iban, konto.bic,
              konto.bank, konto.konto, konto.waehrung, konto.csvProfil,
              konto.aktiv ? 1 : 0, konto.letzterImportBis.ToIso() },
            "Bankkonto anlegen");
        if (!r) return r;
        konto.version = 1;
        WriteAudit(akteur, "bankkonto", konto.id, "insert", konto.bezeichnung);
        return StoreResult::Ok();
    }

    const StoreResult r = Exec(
        "UPDATE bankkonto SET bezeichnung = ?, iban = ?, bic = ?, bank = ?,"
        " konto = ?, waehrung = ?, csv_profil = ?, aktiv = ?,"
        " letzter_import_bis = ?, version = version + 1"
        " WHERE id = ? AND version = ?",
        { konto.bezeichnung, konto.iban, konto.bic, konto.bank, konto.konto,
          konto.waehrung, konto.csvProfil, konto.aktiv ? 1 : 0,
          konto.letzterImportBis.ToIso(), konto.id, konto.version },
        "Bankkonto speichern");
    if (!r) return r;
    ++konto.version;
    WriteAudit(akteur, "bankkonto", konto.id, "update", konto.bezeichnung);
    return StoreResult::Ok();
}

std::vector<Bankkonto> Store::Bankkonten(int64_t mandantId, bool nurAktive) const {
    std::vector<Bankkonto> liste;
    UltraDbResultSet rs;
    const std::string sql =
        std::string("SELECT id, mandant_id, bezeichnung, iban, bic, bank, konto,"
                    " waehrung, csv_profil, aktiv, letzter_import_bis, version"
                    " FROM bankkonto WHERE mandant_id = ?") +
        (nurAktive ? " AND aktiv = 1" : "") + " ORDER BY bezeichnung";
    if (!Query(sql, { mandantId }, rs)) return liste;
    for (const UltraDbRow& row : rs) liste.push_back(BankkontoAusZeile(row));
    return liste;
}

bool Store::BankkontoById(int64_t id, Bankkonto& out) const {
    UltraDbRow row;
    if (!QueryOne("SELECT id, mandant_id, bezeichnung, iban, bic, bank, konto,"
                  " waehrung, csv_profil, aktiv, letzter_import_bis, version"
                  " FROM bankkonto WHERE id = ?", { id }, row))
        return false;
    out = BankkontoAusZeile(row);
    return true;
}

bool Store::BankkontoByIban(int64_t mandantId, const std::string& iban,
                            Bankkonto& out) const {
    if (iban.empty()) return false;
    UltraDbRow row;
    if (!QueryOne("SELECT id, mandant_id, bezeichnung, iban, bic, bank, konto,"
                  " waehrung, csv_profil, aktiv, letzter_import_bis, version"
                  " FROM bankkonto WHERE mandant_id = ? AND iban = ?",
                  { mandantId, iban }, row))
        return false;
    out = BankkontoAusZeile(row);
    return true;
}

StoreResult Store::ImportiereBankauszug(int64_t bankkontoId,
                                        const BankLeseBericht& bericht,
                                        const std::string& dateiname,
                                        const Akteur& akteur,
                                        int& outNeu, int& outBekannt) {
    outNeu = 0;
    outBekannt = 0;
    if (!akteur.Darf(Recht::Buchen))
        return StoreResult::Fail("Diese Rolle darf keine Umsätze einlesen.");
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");
    if (!bericht.ok)
        return StoreResult::Fail("Die Datei enthält keine übernehmbaren Umsätze.");

    Bankkonto konto;
    if (!BankkontoById(bankkontoId, konto))
        return StoreResult::Fail("Das Bankkonto gibt es nicht.");

    // A statement for a different IBAN than the account it is being read into
    // is refused rather than warned about: the lines would be right, the
    // account would be wrong, and nothing afterwards would look odd.
    for (const Bankauszug& auszug : bericht.auszuege) {
        if (auszug.iban.empty() || konto.iban.empty()) continue;
        if (auszug.iban != konto.iban) {
            return StoreResult::Fail(
                "Die Datei gehört zur IBAN " + auszug.iban + ", das Bankkonto \"" +
                konto.bezeichnung + "\" zur IBAN " + konto.iban +
                ". Es wurde nichts eingelesen.");
        }
    }
    // A statement that does not add up has been read wrong. Importing it would
    // put a wrong bank balance in the books, and a wrong bank balance is found
    // by the next reconciliation at the earliest.
    for (const Bankauszug& auszug : bericht.auszuege) {
        Money differenz;
        if (auszug.saldenGelesen && !auszug.Stimmt(differenz)) {
            return StoreResult::Fail(
                "Auszug " + auszug.auszugsnummer + " geht nicht auf: es fehlen " +
                differenz.ToString() + " zwischen Anfangssaldo, Buchungen und "
                "Endsaldo. Die Datei wurde nicht eingelesen.");
        }
    }

    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(connection_, &error);
    if (tx == UltraDbInvalidHandle)
        return StoreResult::Fail("Transaktion konnte nicht gestartet werden: " +
                                 error.message);
    std::string fehler;
    auto abbrechen = [&](const std::string& text) {
        UltraDb_Rollback(tx);
        return StoreResult::Fail(text + (fehler.empty() ? "" : ": " + fehler));
    };

    int64_t importId = 0;
    if (!NextSequenzInTx(tx, "bank_import", importId, fehler, RowLock()))
        return abbrechen("Die Importnummer konnte nicht vergeben werden");

    const int64_t jetzt = NowSeconds();
    Date von, bis;
    int gelesen = 0;

    for (const Bankauszug& auszug : bericht.auszuege) {
        for (const Bankumsatz& umsatz : auszug.umsaetze) {
            ++gelesen;
            if (!umsatz.Valid()) continue;

            // **The idempotency check, per line.** A user who downloads "the
            // last 30 days" every week hands over the same lines four times;
            // skipping the known ones is what makes that harmless.
            UltraDbResultSet vorhanden;
            UltraDb_QueryInTx(
                tx, "SELECT id FROM bankumsatz WHERE bankkonto_id = ? AND referenz = ?",
                { bankkontoId, umsatz.referenz }, vorhanden);
            if (!vorhanden.Empty()) {
                ++outBekannt;
                continue;
            }

            int64_t id = 0;
            if (!NextSequenzInTx(tx, "bankumsatz", id, fehler, RowLock()))
                return abbrechen("Eine Umsatznummer konnte nicht vergeben werden");

            const UltraDbResult r = UltraDb_ExecInTx(
                tx,
                "INSERT INTO bankumsatz(id, bankkonto_id, mandant_id, buchungstag,"
                " valuta, betrag, waehrung, gegen_iban, gegen_bic, gegen_name,"
                " verwendungszweck, e2e_ref, mandatsreferenz, glaeubiger_id,"
                " buchungstext, referenz, teilbuchungen, import_id, importiert_am)"
                " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                { id, bankkontoId, konto.mandantId, umsatz.buchungstag.ToIso(),
                  umsatz.valuta.ToIso(), umsatz.betrag.Minor(),
                  umsatz.betrag.Currency(), umsatz.gegenIban, umsatz.gegenBic,
                  umsatz.gegenName, umsatz.verwendungszweck, umsatz.endToEndId,
                  umsatz.mandatsreferenz, umsatz.glaeubigerId, umsatz.buchungstext,
                  umsatz.referenz, umsatz.teilbuchungen, importId, jetzt });
            if (!r) { fehler = r.message; return abbrechen("Ein Umsatz konnte nicht geschrieben werden"); }
            ++outNeu;

            if (!von.Valid() || umsatz.buchungstag < von) von = umsatz.buchungstag;
            if (!bis.Valid() || bis < umsatz.buchungstag) bis = umsatz.buchungstag;
        }
    }

    const UltraDbResult protokoll = UltraDb_ExecInTx(
        tx,
        "INSERT INTO bank_import(id, mandant_id, bankkonto_id, dateiname, datei_hash,"
        " format, zeitpunkt, benutzer, gelesen, neu, bekannt, von, bis)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)",
        { importId, konto.mandantId, bankkontoId, dateiname, bericht.dateiHash,
          BankFormatToText(bericht.format), jetzt, akteur.anmeldename, gelesen,
          outNeu, outBekannt, von.ToIso(), bis.ToIso() });
    if (!protokoll) { fehler = protokoll.message; return abbrechen("Der Import konnte nicht protokolliert werden"); }

    // How far the statements have been read, so the next import knows where it
    // should start and a gap is visible rather than assumed away.
    if (bis.Valid() && (!konto.letzterImportBis.Valid() || konto.letzterImportBis < bis)) {
        UltraDb_ExecInTx(tx, "UPDATE bankkonto SET letzter_import_bis = ? WHERE id = ?",
                            { bis.ToIso(), bankkontoId });
    }

    if (!AuditInTx(tx, akteur, "bank_import", importId, "insert",
                   dateiname + ", " + Number(outNeu) + " neu, " +
                   Number(outBekannt) + " bereits vorhanden", fehler, RowLock()))
        return abbrechen("Der Import konnte nicht protokolliert werden");

    const UltraDbResult commit = UltraDb_Commit(tx);
    if (!commit)
        return StoreResult::Fail("Der Import konnte nicht abgeschlossen werden: " +
                                 commit.message);
    return StoreResult::Ok();
}

std::vector<Store::BankImportEintrag> Store::BankImporte(int64_t mandantId) const {
    std::vector<BankImportEintrag> liste;
    UltraDbResultSet rs;
    if (!Query("SELECT id, bankkonto_id, dateiname, datei_hash, format, zeitpunkt,"
               " benutzer, gelesen, neu, bekannt, von, bis FROM bank_import"
               " WHERE mandant_id = ? ORDER BY zeitpunkt DESC", { mandantId }, rs))
        return liste;
    for (const UltraDbRow& row : rs) {
        BankImportEintrag e;
        e.id          = row["id"].AsInt64();
        e.bankkontoId = row["bankkonto_id"].AsInt64();
        e.dateiname   = row["dateiname"].AsString();
        e.dateiHash   = row["datei_hash"].AsString();
        e.format      = row["format"].AsString();
        e.zeitpunkt   = row["zeitpunkt"].AsInt64();
        e.benutzer    = row["benutzer"].AsString();
        e.gelesen     = row["gelesen"].AsInt();
        e.neu         = row["neu"].AsInt();
        e.bekannt     = row["bekannt"].AsInt();
        e.von         = DateFrom(row["von"]);
        e.bis         = DateFrom(row["bis"]);
        liste.push_back(e);
    }
    return liste;
}

std::vector<Bankumsatz> Store::Umsaetze(const UmsatzFilter& filter) const {
    std::vector<Bankumsatz> liste;
    std::string sql = std::string("SELECT ") + kUmsatzSpalten + " FROM bankumsatz WHERE 1=1";
    UltraDbParams params;
    if (filter.bankkontoId != 0) { sql += " AND bankkonto_id = ?"; params.push_back(filter.bankkontoId); }
    if (filter.mandantId != 0)   { sql += " AND mandant_id = ?";   params.push_back(filter.mandantId); }
    if (filter.von.Valid())      { sql += " AND buchungstag >= ?"; params.push_back(filter.von.ToIso()); }
    if (filter.bis.Valid())      { sql += " AND buchungstag <= ?"; params.push_back(filter.bis.ToIso()); }
    if (!filter.suche.empty()) {
        sql += " AND (gegen_name LIKE ? OR verwendungszweck LIKE ? OR referenz LIKE ?)";
        const std::string muster = "%" + filter.suche + "%";
        params.push_back(muster); params.push_back(muster); params.push_back(muster);
    }
    if (filter.nurOffene) {
        // Unassigned means: nothing assigned, or less assigned than arrived.
        // The second half is what keeps a part payment visible until the rest
        // of it is dealt with.
        sql += " AND (SELECT COALESCE(SUM(ABS(betrag)), 0) FROM zuordnung"
               " WHERE zuordnung.bankumsatz_id = bankumsatz.id) < ABS(bankumsatz.betrag)";
    }
    sql += " ORDER BY buchungstag, id";
    if (filter.limit > 0) sql += " LIMIT " + Number(static_cast<int64_t>(filter.limit));

    UltraDbResultSet rs;
    if (!Query(sql, params, rs)) return liste;
    for (const UltraDbRow& row : rs) liste.push_back(UmsatzAusZeile(row));
    return liste;
}

bool Store::UmsatzById(int64_t id, Bankumsatz& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kUmsatzSpalten +
                  " FROM bankumsatz WHERE id = ?", { id }, row))
        return false;
    out = UmsatzAusZeile(row);
    return true;
}

Money Store::OffenerBetrag(int64_t bankumsatzId) const {
    Bankumsatz umsatz;
    if (!UmsatzById(bankumsatzId, umsatz)) return Money::Invalid();
    int64_t zugeordnet = 0;
    UltraDbResultSet rs;
    if (Query("SELECT betrag FROM zuordnung WHERE bankumsatz_id = ?", { bankumsatzId }, rs))
        for (const UltraDbRow& row : rs) {
            const int64_t b = row["betrag"].AsInt64();
            zugeordnet += (b < 0 ? -b : b);
        }
    const int64_t gesamt = umsatz.betrag.Minor() < 0 ? -umsatz.betrag.Minor()
                                                     : umsatz.betrag.Minor();
    const int64_t rest = gesamt - zugeordnet;
    // The sign follows the line, so "open" on an outgoing payment stays
    // negative and cannot be confused with money in.
    return Money::FromMinor(umsatz.betrag.Minor() < 0 ? -rest : rest,
                            umsatz.betrag.Currency());
}

std::vector<Zuordnungsvorschlag> Store::Zuordnungsvorschlaege(int64_t bankumsatzId) const {
    std::vector<Zuordnungsvorschlag> leer;
    Bankumsatz umsatz;
    if (!UmsatzById(bankumsatzId, umsatz)) return leer;
    Bankkonto konto;
    if (!BankkontoById(umsatz.bankkontoId, konto)) return leer;

    // Only what is still open on the line can be assigned, so the candidates
    // are scored against the remainder rather than the original amount.
    const Money offen = OffenerBetrag(bankumsatzId);
    if (!offen.Valid() || offen.Minor() == 0) return leer;
    Bankumsatz rest = umsatz;
    rest.betrag = offen;

    BelegFilter filter;
    filter.mandantId = konto.mandantId;
    filter.nurOffene = true;
    const std::vector<Beleg> belege = BelegListe(filter);

    std::vector<ZuordnungKandidat> kandidaten;
    kandidaten.reserve(belege.size());
    for (const Beleg& b : belege) {
        ZuordnungKandidat k;
        k.belegId       = b.id;
        k.belegnummer   = b.nummer;
        k.externeNummer = b.externeNummer;
        k.belegdatum    = b.datum;
        k.brutto        = b.brutto;
        k.offen         = b.brutto - b.bezahlt;
        k.geldAbgang    = GeldAbgangBeimAusgleich(b.art);
        k.partnerId     = b.partnerId;
        k.partnerName   = b.partnerName;
        Partner partner;
        if (b.partnerId != 0 && PartnerById(b.partnerId, partner)) {
            k.partnerIban = partner.iban;
            if (k.partnerName.empty()) k.partnerName = partner.name;
        }
        kandidaten.push_back(std::move(k));
    }
    return SchlageZuordnungVor(rest, kandidaten);
}

StoreResult Store::ZuordnungBuchen(int64_t bankumsatzId, int64_t belegId,
                                   const Money& betrag, const Akteur& akteur) {
    if (!akteur.Darf(Recht::Buchen))
        return StoreResult::Fail("Diese Rolle darf nicht buchen.");
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");

    Bankumsatz umsatz;
    if (!UmsatzById(bankumsatzId, umsatz))
        return StoreResult::Fail("Den Bankumsatz gibt es nicht.");
    Bankkonto konto;
    if (!BankkontoById(umsatz.bankkontoId, konto))
        return StoreResult::Fail("Das Bankkonto des Umsatzes gibt es nicht.");
    Beleg beleg;
    if (!BelegById(belegId, beleg))
        return StoreResult::Fail("Den Beleg gibt es nicht.");

    const int64_t wunsch = betrag.Minor() < 0 ? -betrag.Minor() : betrag.Minor();
    if (wunsch <= 0)
        return StoreResult::Fail("Ein zugeordneter Betrag muss größer als null sein.");

    // More than the line carries cannot be assigned from it. Without this, one
    // bank line could pay three invoices that together cost more than arrived,
    // and every document involved would look settled.
    const Money offen = OffenerBetrag(bankumsatzId);
    const int64_t offenBetrag = offen.Minor() < 0 ? -offen.Minor() : offen.Minor();
    if (wunsch > offenBetrag) {
        return StoreResult::Fail(
            "Auf dem Bankumsatz sind nur noch " +
            Money::FromMinor(offenBetrag, umsatz.betrag.Currency()).ToString() +
            " offen, zugeordnet werden sollen " + betrag.ToString() + ".");
    }

    // **The direction has to agree.** Money that arrived cannot pay an invoice
    // we received. The matcher already refuses these, but a caller can assign
    // by hand, and the store is where the rule has to hold.
    const bool geldEin = umsatz.betrag.Minor() > 0;
    if (GeldAbgangBeimAusgleich(beleg.art) == geldEin) {
        return StoreResult::Fail(
            geldEin ? "Auf dem Konto ist Geld eingegangen; eine Eingangsrechnung "
                      "wird damit nicht bezahlt."
                    : "Vom Konto ist Geld abgegangen; eine Ausgangsrechnung wird "
                      "damit nicht bezahlt.");
    }

    // The payment goes through the existing path, which already knows about
    // over-payment, frozen periods, the document's new status and the hash
    // chain. A second payment path would drift from this one.
    const Money zahlbetrag = Money::FromMinor(wunsch, umsatz.betrag.Currency());
    const std::string notiz =
        "Bankumsatz " + FormatDateGerman(umsatz.buchungstag) +
        (umsatz.gegenName.empty() ? "" : ", " + umsatz.gegenName);
    const StoreResult gebucht = ZahlungErfassen(belegId, umsatz.buchungstag, zahlbetrag,
                                                konto.konto, notiz, akteur);
    if (!gebucht) return gebucht;

    // Which payment row it produced, so the assignment points at it.
    int64_t zahlungId = 0;
    for (const Zahlung& z : Zahlungen(belegId))
        if (z.id > zahlungId) zahlungId = z.id;

    int64_t id = 0;
    const StoreResult neueId = NextSequenceValue("zuordnung", id);
    if (!neueId) return neueId;
    const StoreResult r = Exec(
        "INSERT INTO zuordnung(id, bankumsatz_id, beleg_id, betrag, waehrung,"
        " zahlung_id, erfasst_von, erfasst_am) VALUES(?,?,?,?,?,?,?,?)",
        { id, bankumsatzId, belegId, wunsch, umsatz.betrag.Currency(), zahlungId,
          akteur.benutzerId, NowSeconds() },
        "Zuordnung speichern");
    if (!r) return r;
    WriteAudit(akteur, "zuordnung", id, "insert",
               "Bankumsatz " + Number(bankumsatzId) + " -> Beleg " + beleg.nummer +
               ", " + zahlbetrag.ToString());
    return StoreResult::Ok();
}

std::vector<Store::BankZuordnung> Store::Zuordnungen(int64_t bankumsatzId) const {
    std::vector<BankZuordnung> liste;
    UltraDbResultSet rs;
    if (!Query("SELECT z.id, z.bankumsatz_id, z.beleg_id, z.betrag, z.waehrung,"
               " z.zahlung_id, z.erfasst_von, z.erfasst_am, b.nummer AS nummer"
               " FROM zuordnung z LEFT JOIN beleg b ON b.id = z.beleg_id"
               " WHERE z.bankumsatz_id = ? ORDER BY z.id", { bankumsatzId }, rs))
        return liste;
    for (const UltraDbRow& row : rs) {
        BankZuordnung z;
        z.id           = row["id"].AsInt64();
        z.bankumsatzId = row["bankumsatz_id"].AsInt64();
        z.belegId      = row["beleg_id"].AsInt64();
        const std::string waehrung =
            row["waehrung"].AsString().empty() ? "EUR" : row["waehrung"].AsString();
        z.betrag       = MoneyFrom(row["betrag"], waehrung);
        z.zahlungId    = row["zahlung_id"].AsInt64();
        z.belegnummer  = row["nummer"].AsString();
        z.erfasstVon   = row["erfasst_von"].AsInt64();
        z.erfasstAm    = row["erfasst_am"].AsInt64();
        liste.push_back(z);
    }
    return liste;
}

// ===== BELEGE AUS DATEIEN =====

std::string Store::BelegArchivPfad() const {
    return BelegArchivPfadFuer(datenbankPfad_);
}

bool Store::BelegMitDateiHash(int64_t mandantId, const std::string& hash,
                              Beleg& out) const {
    if (hash.empty()) return false;
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kBelegSpalten +
                  " FROM beleg WHERE mandant_id = ? AND datei_hash = ?"
                  " ORDER BY id LIMIT 1", { mandantId, hash }, row))
        return false;
    out = BelegFromRow(row);
    return true;
}

Store::BelegImportBericht Store::ImportiereBelegDateien(
        int64_t mandantId, const std::vector<std::string>& pfade, BelegArt art,
        const Date& datum, const std::string& kreis, const Akteur& akteur) {
    BelegImportBericht bericht;
    if (!akteur.Darf(Recht::BelegErfassen)) {
        bericht.fehler = "Diese Rolle darf keine Belege erfassen.";
        return bericht;
    }
    if (connection_.empty()) {
        bericht.fehler = "Es ist keine Datenbank geöffnet.";
        return bericht;
    }
    if (pfade.empty()) {
        bericht.fehler = "Es wurde keine Datei angegeben.";
        return bericht;
    }
    if (!datum.Valid()) {
        bericht.fehler = "Ohne Belegdatum lässt sich der Beleg keinem "
                         "Geschäftsjahr zuordnen.";
        return bericht;
    }

    BelegArchiv archiv(BelegArchivPfad());
    for (const std::string& pfad : pfade) {
        ++bericht.gelesen;
        BelegImportEintrag eintrag;

        // Copy the file in first. A document row pointing at an archive entry
        // that was never written would be worse than no row at all.
        const ArchivEintrag abgelegt = archiv.Ablegen(pfad, datum.year);
        eintrag.dateiname = abgelegt.dateiname;
        eintrag.hash      = abgelegt.hash;
        eintrag.pfad      = abgelegt.pfad;
        eintrag.warnungen = abgelegt.warnungen;
        if (!abgelegt.ok) {
            eintrag.fehler = abgelegt.fehler;
            ++bericht.abgelehnt;
            bericht.eintraege.push_back(std::move(eintrag));
            continue;
        }

        // **The same receipt twice is one receipt.** Dragging a folder in
        // again is how an import button is actually used, and creating a
        // second draft for a file already booked is how duplicate expenses
        // get into a ledger.
        Beleg vorhanden;
        if (BelegMitDateiHash(mandantId, abgelegt.hash, vorhanden)) {
            eintrag.ok               = true;
            eintrag.schonVorhanden   = true;
            eintrag.vorhandenerBeleg = vorhanden.id;
            eintrag.belegnummer      = vorhanden.nummer;
            ++bericht.bekannt;
            bericht.eintraege.push_back(std::move(eintrag));
            continue;
        }

        Beleg beleg;
        beleg.mandantId = mandantId;
        beleg.art       = art;
        beleg.datum     = datum;
        beleg.waehrung  = "EUR";
        // The file name is usually the only thing known about the document at
        // this point, and it often carries the supplier and the number. It is
        // put where a list will show it rather than discarded.
        beleg.buchungstext = abgelegt.dateiname;
        beleg.dateiPfad    = abgelegt.pfad;
        beleg.dateiHash    = abgelegt.hash;

        const StoreResult gespeichert = SaveBeleg(beleg, kreis, akteur);
        if (!gespeichert) {
            eintrag.fehler = gespeichert.fehler;
            ++bericht.abgelehnt;
            bericht.eintraege.push_back(std::move(eintrag));
            continue;
        }
        eintrag.ok          = true;
        eintrag.belegId     = beleg.id;
        eintrag.belegnummer = beleg.nummer;
        ++bericht.angelegt;
        WriteAudit(akteur, "beleg", beleg.id, "datei-import",
                   abgelegt.dateiname + " sha256:" + abgelegt.hash);
        bericht.eintraege.push_back(std::move(eintrag));
    }

    for (const BelegImportEintrag& e : bericht.eintraege)
        for (const std::string& w : e.warnungen) bericht.warnungen.push_back(w);

    if (bericht.angelegt > 0)
        bericht.warnungen.push_back(
            Number(bericht.angelegt) + " Beleg(e) sind als Entwurf angelegt. "
            "Betrag, Konto und Steuerschlüssel stehen noch nicht darin - aus "
            "dem Beleg wird nichts ausgelesen, und erfundene Zahlen in einem "
            "Hauptbuch wären schlimmer als gar keine.");

    bericht.ok = bericht.angelegt > 0 || bericht.bekannt > 0;
    if (!bericht.ok && bericht.fehler.empty())
        bericht.fehler = "Keine der Dateien konnte übernommen werden.";
    return bericht;
}

// ===== STEUERMELDUNGEN =====

namespace {

std::string MeldungStatusToText(Store::MeldungStatus status) {
    switch (status) {
        case Store::MeldungStatus::Entwurf:     return "entwurf";
        case Store::MeldungStatus::Erzeugt:     return "erzeugt";
        case Store::MeldungStatus::Eingereicht: return "eingereicht";
        case Store::MeldungStatus::Bestaetigt:  return "bestaetigt";
    }
    return "entwurf";
}

Store::MeldungStatus MeldungStatusFromText(const std::string& text) {
    if (text == "erzeugt")     return Store::MeldungStatus::Erzeugt;
    if (text == "eingereicht") return Store::MeldungStatus::Eingereicht;
    if (text == "bestaetigt")  return Store::MeldungStatus::Bestaetigt;
    return Store::MeldungStatus::Entwurf;
}

Store::Meldung MeldungAusZeile(const UltraDbRow& row) {
    Store::Meldung m;
    m.id             = row["id"].AsInt64();
    m.mandantId      = row["mandant_id"].AsInt64();
    m.art            = row["art"].AsString();
    m.jahr           = row["jahr"].AsInt();
    m.zeitraum       = row["zeitraum"].AsString();
    m.status         = MeldungStatusFromText(row["status"].AsString());
    m.zahllast       = MoneyFrom(row["zahllast"], "EUR");
    m.kennzahlenJson = row["kennzahlen_json"].AsString();
    m.datei          = row["datei"].AsString();
    m.xmlHash        = row["xml_hash"].AsString();
    m.transferticket = row["transferticket"].AsString();
    m.berichtigt     = row["berichtigt"].AsInt() != 0;
    m.echtfall       = row["echtfall"].AsInt() != 0;
    m.erzeugtAm      = row["erzeugt_am"].AsInt64();
    m.eingereichtAm  = row["eingereicht_am"].AsInt64();
    m.benutzer       = row["benutzer"].AsString();
    return m;
}

const char* const kMeldungSpalten =
    "id, mandant_id, art, jahr, zeitraum, status, zahllast, kennzahlen_json,"
    " datei, xml_hash, transferticket, berichtigt, echtfall, erzeugt_am,"
    " eingereicht_am, benutzer";

} // namespace

UstvaBerechnung Store::BerechneUstvaFuer(int64_t mandantId, int jahr,
                                         const std::string& zeitraum,
                                         const UstvaMapping& mapping) const {
    UstvaBerechnung leer;
    Date von, bis;
    if (!UstvaZeitraumGrenzen(jahr, zeitraum, von, bis)) {
        leer.fehler = "\"" + zeitraum + "\" ist kein Voranmeldungszeitraum "
                      "(01-12 für einen Monat, 41-44 für ein Quartal).";
        return leer;
    }
    return BerechneUstva(mandantId, jahr, zeitraum, von, bis,
                         Journal(mandantId, von, bis),
                         SteuerschluesselListe(mandantId), mapping);
}

StoreResult Store::MeldungEintragen(Meldung& meldung, const Akteur& akteur) {
    if (!akteur.Darf(Recht::SteuerMelden))
        return StoreResult::Fail("Diese Rolle darf keine Steuermeldungen abgeben.");
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");
    if (meldung.art.empty() || meldung.zeitraum.empty() || meldung.jahr == 0)
        return StoreResult::Fail("Der Meldung fehlen Art, Jahr oder Zeitraum.");

    // **A filed return is never replaced.** What was sent has to stay provable,
    // and the mechanism for changing it is a berichtigte Meldung - the same
    // rule as Storno on a posting.
    Meldung vorhanden;
    if (MeldungFuerZeitraum(meldung.mandantId, meldung.art, meldung.jahr,
                            meldung.zeitraum, vorhanden) &&
        vorhanden.id != meldung.id) {
        if (!vorhanden.transferticket.empty() && !meldung.berichtigt) {
            return StoreResult::Fail(
                "Für " + meldung.art + " " + Number(meldung.jahr) + "/" +
                meldung.zeitraum + " wurde bereits eine Meldung eingereicht "
                "(Transferticket " + vorhanden.transferticket + "). Eine Änderung "
                "ist eine berichtigte Meldung, keine zweite Erstmeldung.");
        }
    }

    const int64_t jetzt = NowSeconds();
    if (meldung.id == 0) {
        const StoreResult id = NextSequenceValue("meldung", meldung.id);
        if (!id) return id;
        meldung.erzeugtAm = jetzt;
        meldung.benutzer  = akteur.anmeldename;
        const StoreResult r = Exec(
            "INSERT INTO meldung(id, mandant_id, art, jahr, zeitraum, status,"
            " zahllast, kennzahlen_json, datei, xml_hash, transferticket,"
            " berichtigt, echtfall, erzeugt_am, eingereicht_am, benutzer)"
            " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            { meldung.id, meldung.mandantId, meldung.art, meldung.jahr,
              meldung.zeitraum, MeldungStatusToText(meldung.status),
              meldung.zahllast.Minor(), meldung.kennzahlenJson, meldung.datei,
              meldung.xmlHash, meldung.transferticket, meldung.berichtigt ? 1 : 0,
              meldung.echtfall ? 1 : 0, meldung.erzeugtAm, meldung.eingereichtAm,
              meldung.benutzer },
            "Meldung eintragen");
        if (!r) return r;
        WriteAudit(akteur, "meldung", meldung.id, "insert",
                   meldung.art + " " + Number(meldung.jahr) + "/" + meldung.zeitraum +
                   ", " + meldung.zahllast.ToString());
        return StoreResult::Ok();
    }

    Meldung alt;
    if (MeldungById(meldung.id, alt) && !alt.transferticket.empty()) {
        return StoreResult::Fail(
            "Diese Meldung wurde bereits eingereicht (Transferticket " +
            alt.transferticket + ") und kann nicht mehr geändert werden.");
    }
    const StoreResult r = Exec(
        "UPDATE meldung SET status = ?, zahllast = ?, kennzahlen_json = ?,"
        " datei = ?, xml_hash = ?, berichtigt = ?, echtfall = ? WHERE id = ?",
        { MeldungStatusToText(meldung.status), meldung.zahllast.Minor(),
          meldung.kennzahlenJson, meldung.datei, meldung.xmlHash,
          meldung.berichtigt ? 1 : 0, meldung.echtfall ? 1 : 0, meldung.id },
        "Meldung speichern");
    if (!r) return r;
    WriteAudit(akteur, "meldung", meldung.id, "update", meldung.art);
    return StoreResult::Ok();
}

StoreResult Store::MeldungQuittung(int64_t meldungId, const std::string& transferticket,
                                   const Date& eingereichtAm, const Akteur& akteur) {
    if (!akteur.Darf(Recht::SteuerMelden))
        return StoreResult::Fail("Diese Rolle darf keine Steuermeldungen abgeben.");
    if (transferticket.empty())
        return StoreResult::Fail("Ohne Transferticket ist nicht belegt, dass die "
                                 "Meldung angekommen ist.");
    Meldung m;
    if (!MeldungById(meldungId, m))
        return StoreResult::Fail("Diese Meldung gibt es nicht.");
    if (!m.transferticket.empty())
        return StoreResult::Fail(
            "Für diese Meldung ist bereits das Transferticket " + m.transferticket +
            " eingetragen.");

    const StoreResult r = Exec(
        "UPDATE meldung SET transferticket = ?, status = ?, eingereicht_am = ?"
        " WHERE id = ?",
        { transferticket, MeldungStatusToText(MeldungStatus::Eingereicht),
          eingereichtAm.Valid() ? eingereichtAm.ToEpochDay() * 86400 : NowSeconds(),
          meldungId },
        "Transferticket eintragen");
    if (!r) return r;
    WriteAudit(akteur, "meldung", meldungId, "eingereicht", transferticket);
    return StoreResult::Ok();
}

std::vector<Store::Meldung> Store::Meldungen(int64_t mandantId,
                                             const std::string& art) const {
    std::vector<Meldung> liste;
    std::string sql = std::string("SELECT ") + kMeldungSpalten +
                      " FROM meldung WHERE mandant_id = ?";
    UltraDbParams params{ mandantId };
    if (!art.empty()) { sql += " AND art = ?"; params.push_back(art); }
    sql += " ORDER BY jahr DESC, zeitraum DESC, id DESC";
    UltraDbResultSet rs;
    if (!Query(sql, params, rs)) return liste;
    for (const UltraDbRow& row : rs) liste.push_back(MeldungAusZeile(row));
    return liste;
}

bool Store::MeldungById(int64_t id, Meldung& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kMeldungSpalten +
                  " FROM meldung WHERE id = ?", { id }, row))
        return false;
    out = MeldungAusZeile(row);
    return true;
}

bool Store::MeldungFuerZeitraum(int64_t mandantId, const std::string& art, int jahr,
                                const std::string& zeitraum, Meldung& out) const {
    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kMeldungSpalten +
                  " FROM meldung WHERE mandant_id = ? AND art = ? AND jahr = ?"
                  " AND zeitraum = ? ORDER BY id DESC LIMIT 1",
                  { mandantId, art, jahr, zeitraum }, row))
        return false;
    out = MeldungAusZeile(row);
    return true;
}

// ===== EU-STEUERSAETZE =====
//
// The rates the OSS return checks an invoice against, as a table a user can
// maintain. Twenty-six member states set them, they change on notice measured
// in weeks, and waiting for a release to enter one is not workable.
//
// The rule that shapes everything here: **a rate is added, never edited.** A
// change is a new row from the day it takes effect, and the row it supersedes
// keeps its own span, closed the day before. Any other arrangement means a
// return filed last quarter stops reproducing the figures that were filed -
// which is not a bug anyone would notice until a member state asked.

namespace {

EuSteuersatz EuSatzAusZeile(const UltraDbRow& row) {
    EuSteuersatz satz;
    satz.id           = row["id"].AsInt64();
    satz.land         = row["land"].AsString();
    satz.art          = row["art"].AsString();
    satz.satzPromille = row["satz_promille"].AsInt();
    Date::TryParseIso(row["gueltig_von"].AsString(), satz.gueltigVon);
    Date::TryParseIso(row["gueltig_bis"].AsString(), satz.gueltigBis);
    satz.geprueft     = row["geprueft"].AsInt() != 0;
    satz.quelle       = row["quelle"].AsString();
    return satz;
}

const char* const kEuSatzSpalten =
    "id, land, art, satz_promille, gueltig_von, gueltig_bis, geprueft, quelle";

} // namespace

std::vector<EuSteuersatz> Store::EuSteuersaetzeAlle() const {
    std::vector<EuSteuersatz> liste;
    UltraDbResultSet rs;
    if (!Query(std::string("SELECT ") + kEuSatzSpalten +
               " FROM eu_steuersatz ORDER BY land, art, gueltig_von", {}, rs))
        return liste;
    for (size_t i = 0; i < rs.Size(); ++i) liste.push_back(EuSatzAusZeile(rs.Row(i)));
    return liste;
}

EuSteuersaetze Store::EuSteuersaetzeGeladen() const {
    EuSteuersaetze saetze;
    saetze.Setze(EuSteuersaetzeAlle());
    return saetze;
}

namespace {

// Does a return that has already been filed cover a day on or after `ab`?
//
// This is the question that decides whether a rate may be entered or removed.
// A rate starting in the future changes nothing that was filed; one starting
// before the end of a filed period changes what that period recomputes to, and
// the filed figures are then no longer reproducible from the books.
//
// Deliberately generous about which returns count: only eingereicht and
// bestaetigt, because a draft is not yet a claim about anything.
bool FilingBetroffen(const std::vector<Store::Meldung>& meldungen, const Date& ab,
                     std::string& outBeschreibung) {
    for (const Store::Meldung& m : meldungen) {
        Date von, bis;
        bool bekannt = false;
        if (m.art == "oss" || m.art == "ioss") {
            bekannt = OssZeitraumGrenzen(m.art == "oss" ? OssVerfahren::Oss : OssVerfahren::Ioss,
                                         m.jahr, m.zeitraum, von, bis);
        } else {
            bekannt = UstvaZeitraumGrenzen(m.jahr, m.zeitraum, von, bis);
        }
        // A period whose bounds cannot be worked out is treated as affected.
        // Guessing "probably fine" about a filed return is the wrong way round.
        if (!bekannt || !ab.Valid() || !(bis < ab)) {
            outBeschreibung = m.art + " " + Number(m.jahr) + "/" + m.zeitraum;
            if (!m.transferticket.empty())
                outBeschreibung += " (Transferticket " + m.transferticket + ")";
            return true;
        }
    }
    return false;
}

} // namespace

// Every return that has actually been filed, across all Mandanten. The rate
// table is not per-Mandant - a member state's VAT rate is the same for every
// set of books in the database - so the question "would this change something
// already filed" has to be asked of all of them.
std::vector<Store::Meldung> Store::EingereichteMeldungen() const {
    std::vector<Meldung> liste;
    UltraDbResultSet rs;
    if (!Query(std::string("SELECT ") + kMeldungSpalten +
               " FROM meldung WHERE status IN ('eingereicht','bestaetigt')", {}, rs))
        return liste;
    for (const UltraDbRow& row : rs) liste.push_back(MeldungAusZeile(row));
    return liste;
}

StoreResult Store::EuSteuersatzSetzen(EuSteuersatz& satz, const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Steuersätze ändern.");
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");

    satz.land = LandNormal(satz.land);
    if (satz.land.size() != 2)
        return StoreResult::Fail("Das Land ist ein ISO-Kürzel aus zwei Buchstaben, "
                                 "zum Beispiel AT für Österreich.");
    if (satz.art.empty()) satz.art = "standard";
    if (satz.art != "standard" && satz.art != "ermaessigt")
        return StoreResult::Fail("Die Art ist \"standard\" oder \"ermaessigt\".");
    if (satz.satzPromille < 0 || satz.satzPromille > 1000)
        return StoreResult::Fail("Der Steuersatz liegt zwischen 0 und 100 Prozent.");
    // **The start date is the point of the whole table**, so it is required
    // rather than defaulted to today: a rate entered a fortnight after it took
    // effect would otherwise silently apply from the wrong day.
    if (!satz.gueltigVon.Valid())
        return StoreResult::Fail("Ein Steuersatz braucht ein Datum, ab dem er gilt.");

    // Same country, same kind, same day: that is an edit of an existing rate
    // rather than a new one, and an edit is what this table exists to prevent.
    UltraDbRow doppelt;
    if (QueryOne(std::string("SELECT ") + kEuSatzSpalten +
                 " FROM eu_steuersatz WHERE land = ? AND art = ? AND gueltig_von = ?",
                 { satz.land, satz.art, satz.gueltigVon.ToIso() }, doppelt)) {
        return StoreResult::Fail(
            "Für " + satz.land + " (" + satz.art + ") gibt es bereits einen Satz ab " +
            FormatDateGerman(satz.gueltigVon) + ". Ein geänderter Satz bekommt das Datum, "
            "ab dem er gilt; ein falsch erfasster wird gelöscht und neu angelegt.");
    }

    // A filed return must keep recomputing to what was filed.
    std::string betroffen;
    if (FilingBetroffen(EingereichteMeldungen(), satz.gueltigVon, betroffen)) {
        return StoreResult::Fail(
            "Ab " + FormatDateGerman(satz.gueltigVon) + " ist bereits eine Meldung "
            "eingereicht (" + betroffen + "). Ein Satz, der diesen Zeitraum verändert, "
            "würde eine abgegebene Meldung nachträglich anders rechnen lassen. "
            "Zu korrigieren ist das über eine berichtigte Meldung.");
    }

    const StoreResult id = NextSequenceValue("eu_steuersatz", satz.id);
    if (!id) return id;

    const StoreResult r = Exec(
        "INSERT INTO eu_steuersatz(id, land, art, satz_promille, gueltig_von,"
        " gueltig_bis, geprueft, quelle, erfasst_am, benutzer)"
        " VALUES(?,?,?,?,?,?,?,?,?,?)",
        { satz.id, satz.land, satz.art, satz.satzPromille, satz.gueltigVon.ToIso(),
          satz.gueltigBis.Valid() ? satz.gueltigBis.ToIso() : std::string(),
          satz.geprueft ? 1 : 0, satz.quelle, NowSeconds(), akteur.anmeldename },
        "Der Steuersatz konnte nicht gespeichert werden");
    if (!r) return r;

    // Close the row this one supersedes: the last one for the same country and
    // kind that started earlier and is still open. It keeps its own rate and
    // its own span - it just stops the day before the new one starts.
    UltraDbRow vorher;
    if (QueryOne(std::string("SELECT ") + kEuSatzSpalten +
                 " FROM eu_steuersatz WHERE land = ? AND art = ? AND gueltig_von < ?"
                 " AND (gueltig_bis IS NULL OR gueltig_bis = '')"
                 " ORDER BY gueltig_von DESC LIMIT 1",
                 { satz.land, satz.art, satz.gueltigVon.ToIso() }, vorher)) {
        const Date bis = satz.gueltigVon.AddDays(-1);
        const StoreResult zu = Exec(
            "UPDATE eu_steuersatz SET gueltig_bis = ? WHERE id = ?",
            { bis.ToIso(), vorher["id"].AsInt64() },
            "Der bisherige Satz konnte nicht abgeschlossen werden");
        if (!zu) return zu;
    }

    return WriteAudit(akteur, "eu_steuersatz", satz.id, "anlegen",
                      satz.land + " " + satz.art + " " +
                      Number(satz.satzPromille) + "‰ ab " +
                      FormatDateGerman(satz.gueltigVon));
}

StoreResult Store::EuSteuersatzLoeschen(int64_t id, const Akteur& akteur) {
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Steuersätze ändern.");
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");

    UltraDbRow row;
    if (!QueryOne(std::string("SELECT ") + kEuSatzSpalten +
                  " FROM eu_steuersatz WHERE id = ?", { id }, row))
        return StoreResult::Fail("Diesen Steuersatz gibt es nicht.");
    const EuSteuersatz satz = EuSatzAusZeile(row);

    std::string betroffen;
    if (FilingBetroffen(EingereichteMeldungen(), satz.gueltigVon, betroffen)) {
        return StoreResult::Fail(
            "Der Satz gilt ab " + FormatDateGerman(satz.gueltigVon) +
            ", und für diesen Zeitraum ist bereits eine Meldung eingereicht (" +
            betroffen + "). Er gehört zur abgegebenen Meldung und wird nicht gelöscht.");
    }

    const StoreResult r = Exec("DELETE FROM eu_steuersatz WHERE id = ?", { id },
                               "Der Steuersatz konnte nicht gelöscht werden");
    if (!r) return r;

    // The row this one had closed becomes open again - otherwise deleting a
    // mistyped rate would leave its predecessor ending on a day that no longer
    // means anything, and the country would have no rate at all from then on.
    const Date bisVorher = satz.gueltigVon.AddDays(-1);
    const StoreResult auf = Exec(
        "UPDATE eu_steuersatz SET gueltig_bis = '' WHERE land = ? AND art = ?"
        " AND gueltig_bis = ?",
        { satz.land, satz.art, bisVorher.ToIso() },
        "Der vorherige Satz konnte nicht wieder geöffnet werden");
    if (!auf) return auf;

    return WriteAudit(akteur, "eu_steuersatz", id, "loeschen",
                      satz.land + " " + satz.art + " ab " +
                      FormatDateGerman(satz.gueltigVon));
}

StoreResult Store::EuSteuersaetzeAusDatei(const std::string& dateipfad, const Akteur& akteur,
                                          int& outNeu, int& outBekannt) {
    outNeu = 0;
    outBekannt = 0;
    if (!akteur.Darf(Recht::StammdatenSchreiben))
        return StoreResult::Fail("Diese Rolle darf keine Steuersätze ändern.");
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");

    EuSteuersaetze datei;
    std::string fehler;
    if (!datei.Laden(dateipfad, fehler)) return StoreResult::Fail(fehler);

    for (const EuSteuersatz& ausDatei : datei.Alle()) {
        UltraDbRow vorhanden;
        if (QueryOne(std::string("SELECT ") + kEuSatzSpalten +
                     " FROM eu_steuersatz WHERE land = ? AND art = ? AND gueltig_von = ?",
                     { ausDatei.land, ausDatei.art, ausDatei.gueltigVon.ToIso() },
                     vorhanden)) {
            // **Leave it exactly as it is.** Someone may have verified this rate
            // by hand since the file shipped; re-seeding must not undo that.
            ++outBekannt;
            continue;
        }
        int64_t id = 0;
        const StoreResult seq = NextSequenceValue("eu_steuersatz", id);
        if (!seq) return seq;
        const StoreResult r = Exec(
            "INSERT INTO eu_steuersatz(id, land, art, satz_promille, gueltig_von,"
            " gueltig_bis, geprueft, quelle, erfasst_am, benutzer)"
            " VALUES(?,?,?,?,?,?,?,?,?,?)",
            { id, ausDatei.land, ausDatei.art, ausDatei.satzPromille,
              ausDatei.gueltigVon.ToIso(),
              ausDatei.gueltigBis.Valid() ? ausDatei.gueltigBis.ToIso() : std::string(),
              ausDatei.geprueft ? 1 : 0, ausDatei.quelle, NowSeconds(), akteur.anmeldename },
            "Der Steuersatz konnte nicht übernommen werden");
        if (!r) return r;
        ++outNeu;
    }

    if (outNeu > 0) {
        const StoreResult a = WriteAudit(akteur, "eu_steuersatz", 0, "importieren",
                                         Number(outNeu) + " Sätze aus " + dateipfad);
        if (!a) return a;
    }
    return StoreResult::Ok();
}

// ===== DATEV-IMPORT =====

bool Store::DatevDateiSchonImportiert(int64_t mandantId, const std::string& dateiHash,
                                      DatevImportEintrag& out) const {
    if (dateiHash.empty()) return false;
    UltraDbRow row;
    if (!QueryOne("SELECT id, dateiname, datei_hash, zeitpunkt, benutzer, zeilen,"
                  " von, bis FROM datev_import WHERE mandant_id = ? AND datei_hash = ?"
                  " ORDER BY zeitpunkt DESC LIMIT 1",
                  { mandantId, dateiHash }, row))
        return false;
    out.id        = row["id"].AsInt64();
    out.dateiname = row["dateiname"].AsString();
    out.dateiHash = row["datei_hash"].AsString();
    out.zeitpunkt = row["zeitpunkt"].AsInt64();
    out.benutzer  = row["benutzer"].AsString();
    out.zeilen    = row["zeilen"].AsInt();
    out.von       = DateFrom(row["von"]);
    out.bis       = DateFrom(row["bis"]);
    return true;
}

std::vector<Store::DatevImportEintrag> Store::DatevImporte(int64_t mandantId) const {
    std::vector<DatevImportEintrag> liste;
    UltraDbResultSet rs;
    if (!Query("SELECT id, dateiname, datei_hash, zeitpunkt, benutzer, zeilen,"
               " von, bis FROM datev_import WHERE mandant_id = ?"
               " ORDER BY zeitpunkt DESC", { mandantId }, rs))
        return liste;
    for (const UltraDbRow& row : rs) {
        DatevImportEintrag eintrag;
        eintrag.id        = row["id"].AsInt64();
        eintrag.dateiname = row["dateiname"].AsString();
        eintrag.dateiHash = row["datei_hash"].AsString();
        eintrag.zeitpunkt = row["zeitpunkt"].AsInt64();
        eintrag.benutzer  = row["benutzer"].AsString();
        eintrag.zeilen    = row["zeilen"].AsInt();
        eintrag.von       = DateFrom(row["von"]);
        eintrag.bis       = DateFrom(row["bis"]);
        liste.push_back(eintrag);
    }
    return liste;
}

StoreResult Store::ImportiereDatevStapel(const DatevImportBericht& bericht,
                                         const std::string& dateiname,
                                         const Akteur& akteur, bool nochmal,
                                         int& outGeschrieben) {
    outGeschrieben = 0;
    if (!akteur.Darf(Recht::Buchen))
        return StoreResult::Fail("Diese Rolle darf nicht buchen.");
    if (connection_.empty()) return StoreResult::Fail("Es ist keine Datenbank geöffnet.");
    if (!bericht.ok || bericht.zeilen.empty())
        return StoreResult::Fail("Der Import enthält keine übernehmbaren Buchungen.");

    const int64_t mandantId = bericht.zeilen.front().buchung.mandantId;

    // The same stack twice would double a month, and the only evidence would be
    // a balance wrong by exactly one stack.
    if (!nochmal) {
        DatevImportEintrag frueher;
        if (DatevDateiSchonImportiert(mandantId, bericht.dateiHash, frueher)) {
            return StoreResult::Fail(
                "Diese Datei wurde bereits importiert (\"" + frueher.dateiname +
                "\", " + Number(frueher.zeilen) + " Buchungen). Ein zweiter Import "
                "würde den Zeitraum verdoppeln. Wenn er wirklich wiederholt werden "
                "soll, mit --nochmal.");
        }
    }

    // Everything is checked before anything is written: a half-imported stack
    // is worse than none, and the two things that can stop a posting - a
    // frozen period and a missing Geschaeftsjahr - are knowable up front.
    std::vector<Buchung> fertig;
    fertig.reserve(bericht.zeilen.size());
    for (const DatevImportZeile& zeile : bericht.zeilen) {
        Buchung b = zeile.buchung;
        Geschaeftsjahr jahr;
        if (!GeschaeftsjahrAt(b.mandantId, b.belegdatum, jahr)) {
            return StoreResult::Fail(
                "Zeile " + Number(zeile.zeileNr) + ": Zum " +
                FormatDateGerman(b.belegdatum) + " ist kein Geschäftsjahr angelegt. "
                "Es wurde nichts importiert.");
        }
        if (!jahr.AcceptsPostings()) {
            return StoreResult::Fail(
                "Zeile " + Number(zeile.zeileNr) + ": Das Geschäftsjahr \"" +
                jahr.bezeichnung + "\" ist abgeschlossen. Es wurde nichts importiert.");
        }
        if (jahr.IsFrozen(b.belegdatum)) {
            WriteAudit(akteur, "datev_import", 0, "abgelehnt",
                       "Import in festgeschriebenen Zeitraum, " +
                       b.belegdatum.ToIso());
            return StoreResult::Fail(
                "Zeile " + Number(zeile.zeileNr) + ": Bis " +
                FormatDateGerman(jahr.festschreibungBis) + " ist festgeschrieben, "
                "die Buchung ist auf den " + FormatDateGerman(b.belegdatum) +
                " datiert. Es wurde nichts importiert.");
        }
        b.geschaeftsjahrId = jahr.id;
        b.periode          = jahr.PeriodOf(b.belegdatum);
        b.erfasstVon       = akteur.benutzerId;
        b.erfasstVonName   = akteur.anmeldename;
        b.erfasstAm        = NowSeconds();
        b.festgeschrieben  = false;
        b.storniertDurch   = 0;
        if (!b.Valid()) {
            return StoreResult::Fail(
                "Zeile " + Number(zeile.zeileNr) +
                ": Die Buchung ist unvollständig. Es wurde nichts importiert.");
        }
        fertig.push_back(std::move(b));
    }

    UltraDbResult error;
    UltraDbHandle tx = UltraDb_Begin(connection_, &error);
    if (tx == UltraDbInvalidHandle)
        return StoreResult::Fail("Transaktion konnte nicht gestartet werden: " +
                                 error.message);

    std::string fehler;
    auto abbrechen = [&](const std::string& text) {
        UltraDb_Rollback(tx);
        return StoreResult::Fail(text + (fehler.empty() ? "" : ": " + fehler));
    };

    // Imported postings go on the same append-only path as every other one, so
    // they take their place in the hash chain and a later PruefeHashKette
    // covers them too.
    for (Buchung& b : fertig) {
        if (!InsertBuchungInTx(tx, b, fehler, RowLock()))
            return abbrechen("Eine importierte Buchung konnte nicht geschrieben werden");
    }

    int64_t importId = 0;
    if (!NextSequenzInTx(tx, "datev_import", importId, fehler, RowLock()))
        return abbrechen("Der Importeintrag konnte nicht angelegt werden");
    const UltraDbResult protokoll = UltraDb_ExecInTx(
        tx,
        "INSERT INTO datev_import(id, mandant_id, dateiname, datei_hash, zeitpunkt,"
        " benutzer, zeilen, von, bis) VALUES(?,?,?,?,?,?,?,?,?)",
        { importId, mandantId, dateiname, bericht.dateiHash, NowSeconds(),
          akteur.anmeldename, static_cast<int>(fertig.size()),
          DateValue(bericht.von), DateValue(bericht.bis) });
    if (!protokoll) { fehler = protokoll.message; return abbrechen("Der Importeintrag konnte nicht angelegt werden"); }

    if (!AuditInTx(tx, akteur, "datev_import", importId, "importieren",
                   dateiname + ", " + Number(static_cast<int64_t>(fertig.size())) +
                   " Buchung(en), " + FormatDateGerman(bericht.von) + " - " +
                   FormatDateGerman(bericht.bis), fehler, RowLock()))
        return abbrechen("Der Protokolleintrag konnte nicht geschrieben werden");

    const UltraDbResult commit = UltraDb_Commit(tx);
    if (!commit)
        return StoreResult::Fail("Der Import wurde nicht bestätigt: " + commit.message);

    outGeschrieben = static_cast<int>(fertig.size());
    return StoreResult::Ok();
}

// ===== SALDEN =====

std::vector<Store::KontoSaldo> Store::SummenUndSalden(int64_t mandantId, const Date& von,
                                                      const Date& bis) const {
    std::vector<KontoSaldo> liste;

    const std::vector<Buchung> journal = Journal(mandantId, von, bis);
    if (journal.empty()) return liste;

    const std::string waehrung = journal.front().waehrung;

    // Accumulate into a vector rather than a map: a chart of accounts is a few
    // hundred rows, the linear scan is not worth a container that would have to
    // be sorted afterwards anyway, and the order stays the insertion order
    // until the sort below.
    auto eintrag = [&](const std::string& konto) -> KontoSaldo& {
        for (KontoSaldo& k : liste) {
            if (k.konto == konto) return k;
        }
        KontoSaldo neu;
        neu.konto = konto;
        neu.soll  = Money::Zero(waehrung);
        neu.haben = Money::Zero(waehrung);
        neu.saldo = Money::Zero(waehrung);
        liste.push_back(neu);
        return liste.back();
    };
    auto buchen = [&](const std::string& konto, SollHaben seite, const Money& betrag) {
        if (konto.empty() || !betrag.Valid() || betrag.IsZero()) return;
        KontoSaldo& k = eintrag(konto);
        if (seite == SollHaben::Soll) k.soll  = k.soll  + betrag;
        else                          k.haben = k.haben + betrag;
    };

    for (const Buchung& b : journal) {
        const SollHaben gegenSeite = SollHabenUmgekehrt(b.sollHaben);
        if (b.steuerSeite == SteuerSeite::Keine) {
            buchen(b.konto,      b.sollHaben,  b.umsatz);
            buchen(b.gegenkonto, gegenSeite,   b.umsatz);
            continue;
        }
        // Expand the automatic tax posting into the leg it always was: the
        // gross account keeps the full amount, the net account gets the net,
        // and the tax account gets the difference on the same side as the net
        // account. This is what makes the three columns balance.
        const bool kontoIstNetto = (b.steuerSeite == SteuerSeite::Konto);
        if (kontoIstNetto) {
            buchen(b.konto,       b.sollHaben, b.netto);
            buchen(b.steuerkonto, b.sollHaben, b.steuer);
            buchen(b.gegenkonto,  gegenSeite,  b.umsatz);
        } else {
            buchen(b.konto,       b.sollHaben, b.umsatz);
            buchen(b.gegenkonto,  gegenSeite,  b.netto);
            buchen(b.steuerkonto, gegenSeite,  b.steuer);
        }
    }

    // Names, and the saldo. One query for the whole chart beats one per line.
    // A Personenkonto is not in the chart of accounts - it belongs to a
    // customer or a supplier - so the partner's name stands in for it, which is
    // what makes a Saldenliste readable rather than a column of numbers.
    const std::vector<Konto> konten = Konten(mandantId);
    for (KontoSaldo& k : liste) {
        bool benannt = false;
        for (const Konto& konto : konten) {
            if (konto.nummer == k.konto) {
                k.bezeichnung = konto.bezeichnung;
                benannt = true;
                break;
            }
        }
        if (!benannt) {
            Partner partner;
            if (PartnerByKonto(mandantId, k.konto, partner)) k.bezeichnung = partner.name;
        }
        k.saldo = k.soll - k.haben;
    }

    // Account number order, which is how every Saldenliste is read. Numbers are
    // strings (leading zeros carry meaning), so shorter sorts before longer and
    // equal lengths compare lexicographically - which for a chart of one width
    // is numeric order.
    for (size_t i = 1; i < liste.size(); ++i) {
        KontoSaldo current = liste[i];
        size_t j = i;
        while (j > 0 && (liste[j - 1].konto.size() > current.konto.size() ||
                         (liste[j - 1].konto.size() == current.konto.size() &&
                          liste[j - 1].konto > current.konto))) {
            liste[j] = liste[j - 1];
            --j;
        }
        liste[j] = current;
    }
    return liste;
}

Money Store::Buchungskreisdifferenz(int64_t mandantId, const Date& von,
                                    const Date& bis) const {
    const std::vector<KontoSaldo> salden = SummenUndSalden(mandantId, von, bis);
    if (salden.empty()) return Money::Zero("EUR");

    Money differenz = Money::Zero(salden.front().soll.Currency());
    for (const KontoSaldo& k : salden) {
        differenz = differenz + k.soll - k.haben;
    }
    return differenz;
}

} // namespace UltraFIBU
