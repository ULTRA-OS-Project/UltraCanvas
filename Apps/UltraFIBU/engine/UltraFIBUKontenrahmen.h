// Apps/UltraFIBU/engine/UltraFIBUKontenrahmen.h
// Loading a chart of accounts and a set of tax keys from data files.
//
// Charts and tax rules are **data, not code** (proposal §4.3, §6.5): SKR03 and
// SKR04 are files, a rate change is a new row with a new validity date, and the
// company's real chart - exported from the Kanzlei as DATEV format category 20 -
// overrides whatever shipped. Nothing in the engine branches on an account
// number or a country.
//
// The files are semicolon-separated UTF-8 with a header line and '#' comments,
// read through the framework's CSV parser (UltraCanvasCSVImport.h, header-only,
// so this stays linkable without the UI). Columns are addressed *by name* from
// the header line, so adding a column to a data file never shifts the meaning
// of the others.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraFIBUTypes.h"

#include <string>
#include <vector>

namespace UltraFIBU {

// What a load produced: the rows, and every line that could not be read.
// Warnings are not failures - a chart with one unreadable line is still worth
// importing, and silently dropping the line is what would be unacceptable.
struct LadeErgebnis {
    bool                     ok = false;
    std::string              fehler;      // set when nothing could be read at all
    std::vector<std::string> warnungen;   // one per skipped or repaired line
    int                      zeilen = 0;  // data rows accepted

    explicit operator bool() const { return ok; }
};

// Read a chart of accounts. `skr` labels the rows ("SKR03"), because a Konto
// records which chart it came from.
LadeErgebnis LadeKontenrahmen(const std::string& pfad, const std::string& skr,
                              std::vector<Konto>& out);

// Read a set of tax keys.
LadeErgebnis LadeSteuerschluesselDatei(const std::string& pfad,
                                       std::vector<Steuerschluessel>& out);

// Where the shipped data files live. Searched in this order:
//   1. $ULTRAFIBU_DATA_DIR                     (an explicit override wins)
//   2. <directory of the running binary>/data  (an installed or packaged build)
//   3. ./Apps/UltraFIBU/data                   (running from the source tree)
//   4. ../Apps/UltraFIBU/data, ../../Apps/UltraFIBU/data (a build directory)
// Returns an empty string when the file is nowhere to be found, so the caller
// can say which name it was looking for.
std::string FindeDatenDatei(const std::string& dateiname);

} // namespace UltraFIBU
