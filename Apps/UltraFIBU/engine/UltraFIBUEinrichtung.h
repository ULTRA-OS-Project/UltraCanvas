// Apps/UltraFIBU/engine/UltraFIBUEinrichtung.h
// Setting up a new bookkeeping file: company, fiscal year, administrator,
// number ranges, chart of accounts and tax keys.
//
// This used to live in the command-line tool only, woven into its argument
// parsing and its printf calls. The screens could not create a file at all -
// and worse, opening a path that did not exist created one anyway, empty and
// unusable, because SQLite creates a file it is asked to open. The UI told the
// user to run "ultrafibu einrichten" on a file it had just produced itself.
// Both front ends now call this, so there is one definition of what a new
// bookkeeping file contains.
//
// **A setup either completes or leaves nothing behind.** The file is built
// under a temporary name beside the target and renamed into place only once
// every step has succeeded. The steps are many - a user, a company, a fiscal
// year, three number ranges, well over a thousand accounts, the tax keys - and
// before this a failure halfway left a file with a company and no accounts,
// which the next attempt then refused because "a company is already set up".
// That file could not be repaired from the program at all.
//
// **Nothing is overwritten that holds a bookkeeping.** A target that already
// contains a company is refused. One that exists but is empty - the leftover
// of an earlier open of a mistyped path - is replaced, because refusing it
// would leave the user a file they cannot use and cannot get rid of here.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraFIBUGeschaeftsjahr.h"
#include "UltraFIBUTypes.h"

#include <string>
#include <vector>

namespace UltraFIBU {

// What a new bookkeeping needs to be told. Only the company name and the start
// of the fiscal year are required; everything else can be completed later,
// and `rechnung-pdf` names what is still missing before it prints.
struct EinrichtungsDaten {
    std::string firma;                  // required
    Date        gjBeginn;               // required: the first of a month
    std::string skr      = "SKR03";     // SKR03 | SKR04
    std::string benutzer = "admin";     // the first administrator's login

    // Address, tax numbers, bank, Kanzlei numbers. `name` is ignored - it is
    // `firma` - and `land` defaults to DE when left empty.
    Mandant     stammdaten;
};

struct EinrichtungsBericht {
    bool        ok = false;
    std::string fehler;

    Mandant        mandant;
    Geschaeftsjahr jahr;
    Benutzer       admin;
    int            konten           = 0;
    int            steuerschluessel = 0;

    // Things that did not stop the setup but that the user should read -
    // chart rows that were skipped, for instance.
    std::vector<std::string> warnungen;
};

// What is wrong with these data, or an empty string when nothing is. Touches
// no file, so a form can call it on every keystroke and grey out its button
// with the reason next to it.
std::string PruefeEinrichtung(const EinrichtungsDaten& daten);

// True when `pfad` names a file that already holds a bookkeeping - which the
// setup will refuse. A file that exists but has no company is not one.
bool EnthaeltBuchhaltung(const std::string& pfad);

// Create a new bookkeeping file at `ziel`. See the header for the guarantees:
// all or nothing, and never over a file that already holds one.
EinrichtungsBericht RichteBuchhaltungEin(const std::string& ziel,
                                         const EinrichtungsDaten& daten);

} // namespace UltraFIBU
