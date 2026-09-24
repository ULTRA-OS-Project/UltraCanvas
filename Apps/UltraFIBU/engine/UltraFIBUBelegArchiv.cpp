// Apps/UltraFIBU/engine/UltraFIBUBelegArchiv.cpp
// The document archive. See the header for why files are copied in rather than
// pointed at, and why the name is the hash.
//
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraFIBUBelegArchiv.h"

#include <UltraCrypt/UltraCryptCore.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
  #include <direct.h>
  #define ULTRAFIBU_MKDIR(p) _mkdir(p)
#else
  #include <unistd.h>
  #define ULTRAFIBU_MKDIR(p) ::mkdir((p), 0700)
#endif

namespace UltraFIBU {

namespace {

std::string Zahl(int64_t wert) { return std::to_string(wert); }

bool LiesGanz(const std::string& pfad, std::string& inhalt, std::string& fehler) {
    std::FILE* datei = std::fopen(pfad.c_str(), "rb");
    if (datei == nullptr) {
        fehler = "Die Datei \"" + pfad + "\" ist nicht lesbar.";
        return false;
    }
    char puffer[65536];
    size_t n = 0;
    while ((n = std::fread(puffer, 1, sizeof(puffer), datei)) > 0) inhalt.append(puffer, n);
    std::fclose(datei);
    if (inhalt.empty()) {
        fehler = "Die Datei \"" + pfad + "\" ist leer.";
        return false;
    }
    return true;
}

bool SchreibeGanz(const std::string& pfad, const std::string& inhalt,
                  std::string& fehler) {
    std::FILE* datei = std::fopen(pfad.c_str(), "wb");
    if (datei == nullptr) {
        fehler = "Die Datei \"" + pfad + "\" ist nicht schreibbar.";
        return false;
    }
    const size_t geschrieben = std::fwrite(inhalt.data(), 1, inhalt.size(), datei);
    const bool geschlossen = (std::fclose(datei) == 0);
    if (geschrieben != inhalt.size() || !geschlossen) {
        fehler = "Die Datei \"" + pfad + "\" konnte nicht vollständig geschrieben "
                 "werden - vermutlich ist der Datenträger voll.";
        std::remove(pfad.c_str());
        return false;
    }
    return true;
}

// Create a directory and everything above it. No <filesystem>: this engine is
// built in configurations where it is not available, and the operation is two
// lines either way.
bool ErzeugeVerzeichnis(const std::string& pfad) {
    if (pfad.empty()) return true;
    std::string teil;
    for (size_t i = 0; i <= pfad.size(); ++i) {
        if (i == pfad.size() || pfad[i] == '/') {
            if (!teil.empty() && teil != ".") {
                struct stat st;
                if (::stat(teil.c_str(), &st) != 0) {
                    if (ULTRAFIBU_MKDIR(teil.c_str()) != 0) {
                        // A parallel importer may have created it between the
                        // stat and the mkdir; that is success, not failure.
                        if (::stat(teil.c_str(), &st) != 0) return false;
                    }
                }
            }
        }
        if (i < pfad.size()) teil.push_back(pfad[i]);
    }
    return true;
}

std::string Dateiname(const std::string& pfad) {
    const size_t schraeg = pfad.find_last_of("/\\");
    return schraeg == std::string::npos ? pfad : pfad.substr(schraeg + 1);
}

std::string OhneEndung(const std::string& pfad) {
    const size_t punkt = pfad.find_last_of('.');
    const size_t schraeg = pfad.find_last_of("/\\");
    if (punkt == std::string::npos) return pfad;
    if (schraeg != std::string::npos && punkt < schraeg) return pfad;
    return pfad.substr(0, punkt);
}

} // namespace

// ===== WHAT EACH KIND LOOKS LIKE =====

namespace {

// Do these bytes start with this signature? Kept explicit rather than using
// rfind/compare so an embedded NUL in a signature (TIFF, PNG) is not a
// terminator.
bool BeginntMit(const std::string& inhalt, const char* muster, size_t laenge) {
    return inhalt.size() >= laenge && std::memcmp(inhalt.data(), muster, laenge) == 0;
}

// A HEIF/AVIF-family file is an ISO-BMFF box: a four-byte length, then "ftyp",
// then the brand. The brand is what separates a HEIC photo from an MP4, so it
// is checked rather than accepting every ftyp box as an image.
bool IstHeif(const std::string& inhalt) {
    if (inhalt.size() < 12) return false;
    if (std::memcmp(inhalt.data() + 4, "ftyp", 4) != 0) return false;
    static const char* const marken[] = {
        "heic", "heix", "heim", "heis",   // HEVC-coded stills
        "hevc", "hevm", "hevs",           // HEVC-coded sequences
        "mif1", "msf1",                   // the generic image / sequence brands
        "avif", "avis",                   // AV1-coded, what newer Androids write
    };
    for (const char* marke : marken)
        if (std::memcmp(inhalt.data() + 8, marke, 4) == 0) return true;
    return false;
}

} // namespace

DateiArt ErkenneDateiArt(const std::string& inhalt) {
    // Offset-zero signatures first. `IstPdf` deliberately tolerates junk ahead
    // of its header, so asking it first would let an image whose metadata
    // happens to contain "%PDF-" be filed as a document.
    if (BeginntMit(inhalt, "\xFF\xD8\xFF", 3))                  return DateiArt::Jpeg;
    if (BeginntMit(inhalt, "\x89PNG\r\n\x1A\n", 8))             return DateiArt::Png;
    if (BeginntMit(inhalt, "II*\0", 4) ||
        BeginntMit(inhalt, "MM\0*", 4))                          return DateiArt::Tiff;
    if (IstHeif(inhalt))                                         return DateiArt::Heif;
    if (BeginntMit(inhalt, "RIFF", 4) && inhalt.size() >= 12 &&
        std::memcmp(inhalt.data() + 8, "WEBP", 4) == 0)          return DateiArt::WebP;
    if (IstPdf(inhalt))                                          return DateiArt::Pdf;
    return DateiArt::Unbekannt;
}

std::string EndungFuer(DateiArt art) {
    switch (art) {
        case DateiArt::Pdf:  return "pdf";
        case DateiArt::Jpeg: return "jpg";
        case DateiArt::Png:  return "png";
        case DateiArt::Tiff: return "tiff";
        case DateiArt::Heif: return "heic";
        case DateiArt::WebP: return "webp";
        case DateiArt::Unbekannt: break;
    }
    return "bin";
}

std::string BezeichnungFuer(DateiArt art) {
    switch (art) {
        case DateiArt::Pdf:  return "PDF";
        case DateiArt::Jpeg: return "JPEG";
        case DateiArt::Png:  return "PNG";
        case DateiArt::Tiff: return "TIFF";
        case DateiArt::Heif: return "HEIF/HEIC";
        case DateiArt::WebP: return "WebP";
        case DateiArt::Unbekannt: break;
    }
    return "unbekannt";
}

const std::vector<std::string>& AlleEndungen() {
    // PDF first: it is still the great majority of what is filed, and a lookup
    // by hash should not stat five missing names to find the usual one.
    static const std::vector<std::string> endungen = {
        "pdf", "jpg", "png", "tiff", "heic", "webp"
    };
    return endungen;
}

bool IstPdf(const std::string& inhalt) {
    // "%PDF-" at the start. Some producers put a few bytes of junk in front,
    // which readers tolerate, so a short window is searched rather than only
    // offset zero - but not the whole file, or any document that merely
    // mentions a PDF would pass.
    const size_t fenster = std::min<size_t>(inhalt.size(), 1024);
    return inhalt.compare(0, 5, "%PDF-") == 0 ||
           inhalt.substr(0, fenster).find("%PDF-") != std::string::npos;
}

bool IstVerschluesseltesPdf(const std::string& inhalt) {
    // /Encrypt in the trailer. Looked for near the end, where the trailer is,
    // so the word appearing inside a content stream does not raise a false
    // alarm.
    const size_t schwanz = std::min<size_t>(inhalt.size(), 8192);
    const std::string ende = inhalt.substr(inhalt.size() - schwanz);
    return ende.find("/Encrypt") != std::string::npos;
}

// ===== THE ARCHIVE =====

std::string BelegArchiv::PfadFuer(const std::string& hash, int jahr,
                                  DateiArt art) const {
    return wurzel_ + "/" + Zahl(jahr) + "/" + hash + "." + EndungFuer(art);
}

bool BelegArchiv::Enthaelt(const std::string& hash, int jahr,
                           std::string& outPfad, DateiArt* outArt) const {
    // A caller with a hash out of the journal has no reason to know which
    // format it was, so every extension the archive uses is tried. The hash is
    // the identity; the extension only decides the name.
    const std::string stamm = wurzel_ + "/" + Zahl(jahr) + "/" + hash + ".";
    for (const std::string& endung : AlleEndungen()) {
        const std::string pfad = stamm + endung;
        std::vector<uint8_t> digest;
        if (!UltraCrypt_HashFile(UltraCryptHashAlgorithm::SHA256, pfad, digest))
            continue;
        // The name claims a hash; this checks the contents still match it. A
        // file that was altered in place is not the document that was filed.
        if (UltraCrypt_ToHex(digest) != hash) continue;
        outPfad = pfad;
        if (outArt != nullptr) {
            *outArt = DateiArt::Unbekannt;
            for (int i = static_cast<int>(DateiArt::Pdf);
                 i <= static_cast<int>(DateiArt::WebP); ++i) {
                const DateiArt kandidat = static_cast<DateiArt>(i);
                if (EndungFuer(kandidat) == endung) { *outArt = kandidat; break; }
            }
        }
        return true;
    }
    return false;
}

ArchivEintrag BelegArchiv::Ablegen(const std::string& quellPfad, int jahr) {
    ArchivEintrag eintrag;
    eintrag.quelle    = quellPfad;
    eintrag.dateiname = Dateiname(quellPfad);

    std::string inhalt;
    if (!LiesGanz(quellPfad, inhalt, eintrag.fehler)) return eintrag;
    eintrag.groesse = static_cast<int64_t>(inhalt.size());

    eintrag.art = ErkenneDateiArt(inhalt);
    if (eintrag.art == DateiArt::Unbekannt) {
        eintrag.fehler = "\"" + eintrag.dateiname + "\" ist kein Beleg, den das "
                         "Archiv lesen kann. Die Endung sagt nichts; entscheidend "
                         "ist der Inhalt. Abgelegt werden PDF, JPEG, PNG, TIFF, "
                         "HEIF/HEIC und WebP - ein abfotografierter Beleg also "
                         "auch.";
        return eintrag;
    }
    if (eintrag.art == DateiArt::Pdf && IstVerschluesseltesPdf(inhalt)) {
        // Stored, but the user has to know: in ten years nobody will have the
        // password, and that is the moment the document is needed.
        eintrag.warnungen.push_back(
            "\"" + eintrag.dateiname + "\" ist ein verschlüsseltes PDF. Es wird "
            "abgelegt, ist aber ohne Passwort nicht lesbar - und in zehn Jahren "
            "hat das Passwort niemand mehr. Besser unverschlüsselt archivieren.");
    }

    std::vector<uint8_t> digest;
    if (!UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA256, inhalt.data(), inhalt.size(),
                         digest)) {
        eintrag.fehler = "Die Prüfsumme konnte nicht gebildet werden.";
        return eintrag;
    }
    eintrag.hash = UltraCrypt_ToHex(digest);

    std::string vorhanden;
    // The kind stays the one the bytes gave: it is what decided the extension
    // this lookup just found, and letting the lookup write it back could only
    // ever replace a right answer with a worse one.
    if (Enthaelt(eintrag.hash, jahr, vorhanden)) {
        // Byte-identical to something already filed. Nothing to write.
        eintrag.ok             = true;
        eintrag.pfad           = vorhanden;
        eintrag.schonVorhanden = true;
        return eintrag;
    }

    const std::string verzeichnis = wurzel_ + "/" + Zahl(jahr);
    if (!ErzeugeVerzeichnis(verzeichnis)) {
        eintrag.fehler = "Das Archivverzeichnis \"" + verzeichnis +
                         "\" konnte nicht angelegt werden.";
        return eintrag;
    }

    const std::string ziel = PfadFuer(eintrag.hash, jahr, eintrag.art);
    if (!SchreibeGanz(ziel, inhalt, eintrag.fehler)) return eintrag;

    // Read it back and hash it again. A copy that was truncated by a full disk
    // is exactly the failure this archive exists to prevent, and it is cheap
    // to rule out now rather than discover in ten years.
    std::vector<uint8_t> nachher;
    if (!UltraCrypt_HashFile(UltraCryptHashAlgorithm::SHA256, ziel, nachher) ||
        UltraCrypt_ToHex(nachher) != eintrag.hash) {
        std::remove(ziel.c_str());
        eintrag.fehler = "Die Kopie im Archiv stimmt nicht mit dem Original "
                         "überein und wurde wieder entfernt.";
        return eintrag;
    }

    eintrag.ok   = true;
    eintrag.pfad = ziel;
    return eintrag;
}

ArchivBericht BelegArchiv::AblegenAlle(const std::vector<std::string>& quellPfade,
                                       int jahr) {
    ArchivBericht bericht;
    if (quellPfade.empty()) {
        bericht.fehler = "Es wurde keine Datei angegeben.";
        return bericht;
    }
    for (const std::string& pfad : quellPfade) {
        ++bericht.gelesen;
        ArchivEintrag eintrag = Ablegen(pfad, jahr);
        if (!eintrag.ok)                 ++bericht.abgelehnt;
        else if (eintrag.schonVorhanden) ++bericht.bekannt;
        else                             ++bericht.abgelegt;
        for (const std::string& w : eintrag.warnungen) bericht.warnungen.push_back(w);
        bericht.eintraege.push_back(std::move(eintrag));
    }
    bericht.ok = bericht.abgelegt > 0 || bericht.bekannt > 0;
    if (!bericht.ok)
        bericht.fehler = "Keine der Dateien konnte abgelegt werden.";
    return bericht;
}

std::string BelegArchivPfadFuer(const std::string& datenbankPfad) {
    // An in-memory database has nowhere to put documents beside it; the tests
    // use that, so it gets a working directory rather than a refusal.
    if (datenbankPfad.empty() || datenbankPfad == ":memory:") return "belege";
    return OhneEndung(datenbankPfad) + "-belege";
}

} // namespace UltraFIBU
