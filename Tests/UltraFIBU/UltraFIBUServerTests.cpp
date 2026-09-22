// Tests/UltraFIBU/UltraFIBUServerTests.cpp
// The multi-user acceptance test: two independent processes on one PostgreSQL
// database, drawing document numbers at the same time.
//
// WHY THIS IS A SEPARATE BINARY. Everything else in the UltraFIBU suite runs
// against SQLite in one process, and it all passed while the server mode was
// broken. SQLite's BEGIN IMMEDIATE serialises writers, so a read-then-write
// counter is safe there and unsafe on PostgreSQL, where two transactions at
// READ COMMITTED both read the same value and both write back the same
// successor. The first run of this test produced eighty numbers of which forty
// were distinct - two invoices per number, which is not a bug a bookkeeping
// program survives, and it is invisible to any single-threaded test.
//
// It therefore forks. That is the point: nothing short of real concurrency
// against a real server proves the locking.
//
// HOW TO RUN IT. Point it at a PostgreSQL server the test may create and drop
// a database on:
//
//     export ULTRAFIBU_TEST_PG_HOST=/var/run/postgresql   # or a hostname
//     export ULTRAFIBU_TEST_PG_PORT=5432
//     export ULTRAFIBU_TEST_PG_USER=fibu
//     export ULTRAFIBU_TEST_PG_DB=ultrafibu_test          # created by the caller
//     ./UltraFIBUServerTests
//
// Without ULTRAFIBU_TEST_PG_DB it reports SKIP and exits 0, so a machine with
// no server does not fail a build over a test it cannot run. It never invents
// a connection: silently testing nothing is worse than testing nothing loudly.
//
// Set ULTRAFIBU_TEST_PG_REQUIRED=1 and the skip becomes a failure. CI sets it,
// because there a skip is indistinguishable from a pass and this is precisely
// the test whose silence would be expensive.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraFIBUStore.h"

#include <UltraDatabase/UltraDatabaseCore.h>
#include <UltraDatabase/UltraDatabaseConnection.h>

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using namespace UltraFIBU;

namespace {

const char* Env(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? v : fallback;
}

// How many numbers each child draws. Large enough that an unlocked counter
// loses a race on any machine, small enough to stay quick.
constexpr int kProNutzer = 40;
constexpr int kNutzer    = 2;

bool Verbinde(Store& store, const std::string& name) {
    UltraDbConnectionConfig cfg;
    cfg.name     = name;
    cfg.driver   = "postgresql";
    cfg.database = Env("ULTRAFIBU_TEST_PG_DB", "");
    cfg.host     = Env("ULTRAFIBU_TEST_PG_HOST", "/var/run/postgresql");
    cfg.port     = std::atoi(Env("ULTRAFIBU_TEST_PG_PORT", "5432"));
    cfg.user     = Env("ULTRAFIBU_TEST_PG_USER", "");
    // A local test server, not a production connection: the driver defaults to
    // verify-full and this is the one place that deliberately does not.
    cfg.tls      = UltraDbTls::Disable;
    if (!UltraDb_RegisterConnection(cfg)) return false;
    if (!UltraDb_OpenConnection(cfg.name)) return false;
    return store.Open(cfg.name, cfg.database).ok;
}

// One client: draw kProNutzer numbers and write them, one per line, to `fd`.
int ZieheNummern(int fd, const std::string& name, int64_t mandantId) {
    Store store;
    if (!Verbinde(store, name)) {
        std::fprintf(stderr, "  child %s: connect failed\n", name.c_str());
        return 1;
    }
    for (int i = 0; i < kProNutzer; ++i) {
        std::string nummer;
        const StoreResult r =
            store.NextBelegnummer(mandantId, "rechnung", Date(2026, 6, 15), nummer);
        if (!r) {
            std::fprintf(stderr, "  child %s: %s\n", name.c_str(), r.fehler.c_str());
            return 1;
        }
        nummer.push_back('\n');
        if (::write(fd, nummer.data(), nummer.size()) < 0) return 1;
    }
    return 0;
}

} // namespace

int main() {
    const bool verlangt = std::string(Env("ULTRAFIBU_TEST_PG_REQUIRED", "")) == "1";
    if (std::string(Env("ULTRAFIBU_TEST_PG_DB", "")).empty()) {
        if (verlangt) {
            std::fprintf(stderr, "FAIL: ULTRAFIBU_TEST_PG_REQUIRED is set but no server is "
                                 "configured - the multi-user check would not have run\n");
            return 1;
        }
        std::printf("SKIP: no PostgreSQL configured "
                    "(set ULTRAFIBU_TEST_PG_DB and friends - see the file header)\n");
        return 0;
    }

    std::printf("Mehrbenutzerbetrieb: Nummernvergabe unter echter Nebenlaeufigkeit\n");

    // ---- set up: one Mandant with one number range ----
    int64_t mandantId = 0;
    {
        Store store;
        if (!Verbinde(store, "fibu-server-setup")) {
            std::fprintf(stderr, "FAIL: cannot reach the configured PostgreSQL server\n");
            return 1;
        }
        Akteur setup;
        Benutzer admin;
        admin.anmeldename = "testchef";
        store.SaveBenutzer(admin, setup);

        Akteur a;
        a.benutzerId   = admin.id;
        a.anmeldename  = admin.anmeldename;
        a.rolle        = admin.rolle;

        Mandant m;
        m.name = "Nebenlaeufigkeits-Test";
        const StoreResult ms = store.SaveMandant(m, a);
        if (!ms) { std::fprintf(stderr, "FAIL: %s\n", ms.fehler.c_str()); return 1; }
        mandantId = m.id;

        Nummernkreis k;
        k.mandantId = m.id;
        k.kreis     = "rechnung";
        k.praefix   = "R-";
        k.stellen   = 5;
        const StoreResult ks = store.SaveNummernkreis(k, a);
        if (!ks) { std::fprintf(stderr, "FAIL: %s\n", ks.fehler.c_str()); return 1; }
    }

    // ---- the race ----
    int rohr[2];
    if (::pipe(rohr) != 0) { std::fprintf(stderr, "FAIL: pipe\n"); return 1; }

    std::vector<pid_t> kinder;
    for (int n = 0; n < kNutzer; ++n) {
        const pid_t pid = ::fork();
        if (pid < 0) { std::fprintf(stderr, "FAIL: fork\n"); return 1; }
        if (pid == 0) {
            ::close(rohr[0]);
            // A fresh connection name per child: they are separate processes
            // with separate pools, which is the situation being tested.
            const int rc = ZieheNummern(rohr[1], "fibu-server-" + std::to_string(n), mandantId);
            ::close(rohr[1]);
            ::_exit(rc);
        }
        kinder.push_back(pid);
    }
    ::close(rohr[1]);

    // Read while they run, or a full pipe would deadlock them.
    std::string gesammelt;
    char puffer[4096];
    ssize_t gelesen = 0;
    while ((gelesen = ::read(rohr[0], puffer, sizeof puffer)) > 0)
        gesammelt.append(puffer, static_cast<size_t>(gelesen));
    ::close(rohr[0]);

    bool kinderOk = true;
    for (const pid_t pid : kinder) {
        int status = 0;
        ::waitpid(pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) kinderOk = false;
    }

    // ---- the verdict ----
    std::vector<std::string> nummern;
    for (size_t i = 0, j = 0; i <= gesammelt.size(); ++i) {
        if (i == gesammelt.size() || gesammelt[i] == '\n') {
            if (i > j) nummern.push_back(gesammelt.substr(j, i - j));
            j = i + 1;
        }
    }

    const std::set<std::string> eindeutig(nummern.begin(), nummern.end());
    const size_t erwartet = static_cast<size_t>(kNutzer) * kProNutzer;

    int fehler = 0;
    auto pruefe = [&fehler](bool ok, const std::string& was) {
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", was.c_str());
        if (!ok) ++fehler;
    };

    pruefe(kinderOk, "beide Clients konnten durchgaengig Nummern ziehen");
    pruefe(nummern.size() == erwartet,
           "es wurden " + std::to_string(erwartet) + " Nummern vergeben (" +
           std::to_string(nummern.size()) + ")");
    // The whole point. A duplicate here means two documents carry one number.
    pruefe(eindeutig.size() == nummern.size(),
           "jede Nummer wurde genau einmal vergeben (" +
           std::to_string(eindeutig.size()) + " von " + std::to_string(nummern.size()) + ")");

    // German numbering has to be gap-free as well as unique: a missing number
    // is something a tax auditor asks about, and it cannot be explained later.
    std::vector<std::string> sortiert(eindeutig.begin(), eindeutig.end());
    bool lueckenlos = sortiert.size() == erwartet;
    for (size_t i = 0; lueckenlos && i < sortiert.size(); ++i) {
        char erwartetName[32];
        std::snprintf(erwartetName, sizeof erwartetName, "R-%05zu", i + 1);
        if (sortiert[i] != erwartetName) lueckenlos = false;
    }
    pruefe(lueckenlos, "die Nummern sind lueckenlos R-00001 .. R-" +
                       std::string(erwartet < 10000 ? "000" : "") + std::to_string(erwartet));

    if (fehler != 0 && !nummern.empty()) {
        std::printf("\n  vergeben: %zu, davon eindeutig: %zu\n", nummern.size(), eindeutig.size());
        int gezeigt = 0;
        for (const std::string& n : eindeutig) {
            if (std::count(nummern.begin(), nummern.end(), n) > 1 && gezeigt++ < 5)
                std::printf("  doppelt: %s\n", n.c_str());
        }
    }

    std::printf("\n%d Pruefung(en) fehlgeschlagen\n", fehler);
    return fehler == 0 ? 0 : 1;
}
