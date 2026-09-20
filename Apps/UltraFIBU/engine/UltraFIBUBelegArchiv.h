// Apps/UltraFIBU/engine/UltraFIBUBelegArchiv.h
// Taking receipts in as files and keeping them.
//
// The store could already record a document's file, but it recorded a **path**
// to wherever the user happened to have it. That is enough to print a link and
// not enough to satisfy anything: move the folder, empty the Downloads
// directory, or swap the laptop, and the receipt behind a ten-year-old posting
// is gone. § 147 AO wants the document retained for ten years and GoBD wants it
// unaltered, so the file has to be **copied in**, not pointed at.
//
// Three decisions follow from that, and each is here rather than in the caller:
//
//  1. **The archive is content-addressed.** A file is stored under its own
//     SHA-256, so importing the same receipt twice costs nothing and produces
//     one file - which matters because dragging a folder in twice is the normal
//     way people use an import button. The hash is also the integrity check the
//     store already records, so the two cannot drift apart.
//  2. **A PDF is recognised by its bytes, not its name.** `.pdf` on a JPEG is
//     something a phone does routinely. The magic number is checked, because
//     an archive that accepts anything is an archive nobody can rely on.
//  3. **An encrypted PDF is refused with the reason.** It can be stored, but
//     it cannot be read back in ten years without a password nobody recorded -
//     which is exactly the failure that only shows up when it matters.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraFIBUTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraFIBU {

// What one file turned into.
struct ArchivEintrag {
    bool        ok = false;
    std::string fehler;

    std::string quelle;        // where it came from, for the report only
    std::string dateiname;     // the original name, kept because it often
                               // carries the supplier and the invoice number
    std::string hash;          // SHA-256, which is also the name in the archive
    std::string pfad;          // where it now lives
    int64_t     groesse = 0;
    // True when this exact file was already in the archive. Not an error: it
    // is the answer to dragging the same folder in twice, and saying so is
    // more useful than silently doing nothing.
    bool        schonVorhanden = false;
    std::vector<std::string> warnungen;
};

struct ArchivBericht {
    bool ok = false;
    std::string fehler;

    int gelesen     = 0;
    int abgelegt    = 0;   // newly stored
    int bekannt     = 0;   // already in the archive
    int abgelehnt   = 0;   // not a readable PDF

    std::vector<ArchivEintrag> eintraege;
    std::vector<std::string>   warnungen;
};

// True when these bytes begin with a PDF header. Checked rather than trusted,
// because an extension is a claim and a magic number is evidence.
bool IstPdf(const std::string& inhalt);

// True when the PDF declares encryption. Such a file can be stored but not
// read back without its password, so an archive that quietly accepted it would
// be holding something unreadable.
bool IstVerschluesseltesPdf(const std::string& inhalt);

// The file store for one Mandant's documents.
//
// Laid out as `<wurzel>/<jahr>/<hash>.pdf`: the year keeps directories from
// growing without bound over a decade, and the hash makes the name unique and
// the content checkable.
class BelegArchiv {
public:
    explicit BelegArchiv(std::string wurzel) : wurzel_(std::move(wurzel)) {}

    const std::string& Wurzel() const { return wurzel_; }

    // Copy one file in. `jahr` decides the sub-directory; pass the document's
    // year rather than today's, so a receipt filed late still lands with its
    // own year.
    ArchivEintrag Ablegen(const std::string& quellPfad, int jahr);

    // Copy several in - which is the point of the whole file: an import button
    // and a drop target both hand over a list.
    ArchivBericht AblegenAlle(const std::vector<std::string>& quellPfade, int jahr);

    // Where a hash lives, whether or not it is there yet.
    std::string PfadFuer(const std::string& hash, int jahr) const;

    // True when the archive holds this hash and the file still matches it.
    bool Enthaelt(const std::string& hash, int jahr, std::string& outPfad) const;

private:
    std::string wurzel_;
};

// Where a database's documents belong: beside it, in `<name>-belege/`. Derived
// from the database path rather than configured, so a database and its
// documents move together - separating them is how an archive goes missing.
std::string BelegArchivPfadFuer(const std::string& datenbankPfad);

} // namespace UltraFIBU
