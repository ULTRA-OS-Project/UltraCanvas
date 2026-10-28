// core/UltraDatabase/UltraDatabaseSqliteCipher.cpp
// SQLite files encrypted at rest (UltraDatabaseEncryption.h): a VFS in front
// of the platform's that seals every page of a database, of its WAL and of
// its rollback journal with XChaCha20-Poly1305 as it is written, and opens
// it as it is read. SQLite above it sees an ordinary database whose pages
// keep 40 bytes "reserved" (SQLITE_FCNTL_RESERVE_BYTES): those bytes hold
// the page's nonce and tag on disk.
//
// Where the page content is, in each file:
//   database      page n at (n-1) * 4096. Page 1's first 16 bytes - "SQLite
//                 format 3\0", the same in every database - are stored as
//                 kMagic instead, so an encrypted file is recognised by its
//                 first bytes and the rest of page 1 (the header) is sealed.
//   WAL           a 32-byte header, then frames of a 24-byte header and a page.
//   journal       a header per sector, then records of a 4-byte page number,
//                 the page and a 4-byte checksum: a page is a read or write
//                 of exactly one page at an offset 4 past a multiple of 8
//                 (headers are sector-aligned, records 4104 bytes long).
// Everything else in the WAL and journal - their headers, frame headers,
// page numbers, checksums, salts - SQLite needs to find its way and carries
// no row data; it passes as it is.
//
// A page is sealed with a fresh random 24-byte nonce each time it is written;
// its associated data is the kind of file and, in the database, the page
// number, so a page moved within the file or from the WAL into the database
// does not open. A page of the database that does not open is an error
// (SQLITE_IOERR_DATA). In the WAL and the journal one is read as zeros:
// recovery reads past the last good frame or record, by design, and SQLite's
// own checksum then rejects it as it rejects a torn write.
//
// The key reaches the VFS from the driver: RegisterPendingKey(full path) just
// before sqlite3_open_v2, taken by xOpen of that database file; its WAL and
// journal find their database's file through sqlite3_database_file_object.
// A database file opened through this VFS without a key - an ATTACHed plain
// file - passes through untouched; temporary files are refused, since an
// encrypted connection keeps them in memory (temp_store=MEMORY).
//
// Version: 0.1.1 - builds against SQLite 3.39 (no sqlite3_filename)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraDatabaseInternal.h"

#include "UltraDatabase/UltraDatabaseEncryption.h"
#include "UltraCanvasPathUtf8.h"

#include <sqlite3.h>

#if defined(ULTRADATABASE_HAS_CRYPT)
  #include "UltraCrypt/UltraCryptCore.h"
#endif

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#if defined(ULTRADATABASE_HAS_CRYPT)

namespace {

constexpr int    kPageSize   = 4096;
constexpr int    kNonceSize  = 24;
constexpr int    kTagSize    = 16;
constexpr int    kReserve    = kNonceSize + kTagSize;      // 40
constexpr int    kSealed     = kPageSize - kReserve;       // 4056: what is encrypted (page 1: from 16)
constexpr int    kHeaderSkip = 16;                         // page 1's "SQLite format 3\0"
constexpr int    kWalHeader  = 32;
constexpr int    kWalFrameHeader = 24;
constexpr const char kVfsName[] = "ultradb-cipher";
constexpr char   kMagic[kHeaderSkip + 1]  = "UltraDB:XC20P:1";   // 15 characters and its NUL: 16 bytes
constexpr char   kSqliteMagic[kHeaderSkip + 1] = "SQLite format 3";

using Key = std::shared_ptr<const UltraCryptSecureBuffer>;

enum class Kind : char { Plain = 0, Database = 'D', Wal = 'W', Journal = 'J' };

// The file SQLite holds: ours, then the platform's right behind it.
struct CipherFile {
    sqlite3_file  base;            // its pMethods: kCipherMethods or kPlainMethods
    sqlite3_file* real = nullptr;  // the platform's file, in the same allocation
    Kind          kind = Kind::Plain;
    Key           key;
};

sqlite3_vfs* g_real = nullptr;     // the platform's VFS this one is in front of
std::mutex   g_pendingLock;
std::map<std::string, Key> g_pending;   // full path -> key, until its xOpen

size_t RealOffset() {
    return (sizeof(CipherFile) + 7) & ~static_cast<size_t>(7);
}

CipherFile* Cipher(sqlite3_file* f) { return reinterpret_cast<CipherFile*>(f); }

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

std::vector<uint8_t> Associated(Kind kind, uint32_t pageNumber) {
    std::vector<uint8_t> ad = { 'U', 'D', 'B', 'p', static_cast<uint8_t>(kind) };
    PutU32(ad, pageNumber);
    if (pageNumber == 1)
        ad.insert(ad.end(), kMagic, kMagic + kHeaderSkip);
    return ad;
}

// `plain` (kPageSize bytes, as SQLite wrote it) sealed into `disk`.
bool SealPage(const UltraCryptSecureBuffer& key, Kind kind, uint32_t pageNumber,
              const uint8_t* plain, uint8_t* disk) {
    const int from = pageNumber == 1 ? kHeaderSkip : 0;
    UltraCryptAeadParams params;
    params.associatedData = Associated(kind, pageNumber);
    std::vector<uint8_t> sealed;
    if (!UltraCrypt_AeadSeal(key, params, plain + from, static_cast<size_t>(kSealed - from), sealed))
        return false;
    if (sealed.size() != static_cast<size_t>(kSealed - from + kTagSize)
        || params.nonce.size() != static_cast<size_t>(kNonceSize))
        return false;
    if (from) std::memcpy(disk, kMagic, kHeaderSkip);
    std::memcpy(disk + from, sealed.data(), static_cast<size_t>(kSealed - from));
    std::memcpy(disk + kSealed, params.nonce.data(), kNonceSize);
    std::memcpy(disk + kSealed + kNonceSize, sealed.data() + (kSealed - from), kTagSize);
    UltraCrypt_SecureZero(sealed.data(), sealed.size());
    return true;
}

// `disk` opened into `plain`, the reserved bytes zero; false when it does not open.
bool OpenPage(const UltraCryptSecureBuffer& key, Kind kind, uint32_t pageNumber,
              const uint8_t* disk, uint8_t* plain) {
    const int from = pageNumber == 1 ? kHeaderSkip : 0;
    if (from && std::memcmp(disk, kMagic, kHeaderSkip) != 0) return false;
    UltraCryptAeadParams params;
    params.associatedData = Associated(kind, pageNumber);
    params.nonce.assign(disk + kSealed, disk + kSealed + kNonceSize);
    std::vector<uint8_t> sealed(disk + from, disk + kSealed);
    sealed.insert(sealed.end(), disk + kSealed + kNonceSize, disk + kPageSize);
    UltraCryptSecureBuffer opened;
    const bool ok = static_cast<bool>(UltraCrypt_AeadOpen(key, params, sealed.data(), sealed.size(), opened))
                    && opened.GetSize() == static_cast<size_t>(kSealed - from);
    UltraCrypt_SecureZero(sealed.data(), sealed.size());
    if (!ok) return false;
    if (from) std::memcpy(plain, kSqliteMagic, kHeaderSkip);
    std::memcpy(plain + from, opened.Data(), opened.GetSize());
    std::memset(plain + kSealed, 0, kReserve);
    return true;
}

// Where the pages are: for a byte range of the file, the page slot that
// holds `offset` - its start in the file and its page number (database) -
// or none (a header, a checksum: not page content).
struct Slot {
    bool          page = false;
    sqlite3_int64 start = 0;      // the slot's first byte in the file
    sqlite3_int64 end = 0;        // past this, the next slot (or header) begins
    uint32_t      number = 0;     // the database's page number; 0 elsewhere
};

Slot SlotAt(Kind kind, sqlite3_int64 offset, int amount) {
    Slot s;
    switch (kind) {
        case Kind::Database:
            s.page = true;
            s.start = offset - offset % kPageSize;
            s.end = s.start + kPageSize;
            s.number = static_cast<uint32_t>(s.start / kPageSize + 1);
            break;
        case Kind::Wal: {
            if (offset < kWalHeader) { s.end = kWalHeader; break; }
            const sqlite3_int64 frame = kWalFrameHeader + kPageSize;
            const sqlite3_int64 in = (offset - kWalHeader) % frame;
            const sqlite3_int64 frameStart = offset - in;
            if (in < kWalFrameHeader) { s.start = frameStart; s.end = frameStart + kWalFrameHeader; break; }
            s.page = true;
            s.start = frameStart + kWalFrameHeader;
            s.end = s.start + kPageSize;
            break;
        }
        case Kind::Journal:
            if (amount == kPageSize && offset % 8 == 4) {
                s.page = true;
                s.start = offset;
                s.end = offset + kPageSize;
            } else {
                s.start = offset;
                s.end = offset + amount;
            }
            break;
        case Kind::Plain:
            s.start = offset;
            s.end = offset + amount;
            break;
    }
    return s;
}

// One whole page slot read from disk and opened. SQLITE_IOERR_SHORT_READ:
// nothing there (yet), `plain` zero. A page that does not open is
// SQLITE_IOERR_DATA, or zeros when `lenient`.
int ReadSlot(CipherFile* f, const Slot& s, uint8_t* plain, bool lenient) {
    uint8_t disk[kPageSize];
    const int rc = f->real->pMethods->xRead(f->real, disk, kPageSize, s.start);
    if (rc == SQLITE_IOERR_SHORT_READ) {
        // A page cut short is no page: SQLite sees the zeros it expects past
        // the end of a file.
        std::memset(plain, 0, kPageSize);
        return SQLITE_IOERR_SHORT_READ;
    }
    if (rc != SQLITE_OK) return rc;
    if (OpenPage(*f->key, f->kind, s.number, disk, plain)) return SQLITE_OK;
    std::memset(plain, 0, kPageSize);
    return lenient ? SQLITE_OK : SQLITE_IOERR_DATA;
}

int WriteSlot(CipherFile* f, const Slot& s, const uint8_t* plain) {
    if (f->kind == Kind::Database && s.number == 1) {
        // Only the page geometry this file format has: SQLite's header
        // says 4096-byte pages (bytes 16-17, big-endian) and 40 reserved
        // bytes (byte 20). Anything else would be written but not readable.
        const int size = (plain[16] << 8) | plain[17];
        if (size != kPageSize || plain[20] != kReserve) return SQLITE_IOERR_WRITE;
    }
    uint8_t disk[kPageSize];
    if (!SealPage(*f->key, f->kind, s.number, plain, disk)) return SQLITE_IOERR_WRITE;
    return f->real->pMethods->xWrite(f->real, disk, kPageSize, s.start);
}

// ---- the io methods of an encrypted file ----------------------------------

int CipherClose(sqlite3_file* file) {
    CipherFile* f = Cipher(file);
    int rc = SQLITE_OK;
    if (f->real && f->real->pMethods) rc = f->real->pMethods->xClose(f->real);
    f->~CipherFile();
    return rc;
}

int CipherRead(sqlite3_file* file, void* buffer, int amount, sqlite3_int64 offset) {
    CipherFile* f = Cipher(file);
    uint8_t* out = static_cast<uint8_t*>(buffer);
    bool shortRead = false;
    // A page that does not open is an error - but not where SQLite reads
    // past what it knows to be good on purpose: the WAL's recovery, which
    // reads whole frames (header and page at once), and a journal's playback,
    // which stops at the first record whose checksum fails.
    const sqlite3_int64 frame = kWalFrameHeader + kPageSize;
    const bool lenient = f->kind == Kind::Journal
                         || (f->kind == Kind::Wal && amount == frame && offset >= kWalHeader
                             && (offset - kWalHeader) % frame == 0);
    while (amount > 0) {
        const Slot s = SlotAt(f->kind, offset, amount);
        const int n = static_cast<int>(std::min<sqlite3_int64>(amount, s.end - offset));
        if (!s.page) {
            const int rc = f->real->pMethods->xRead(f->real, out, n, offset);
            if (rc == SQLITE_IOERR_SHORT_READ) shortRead = true;
            else if (rc != SQLITE_OK) return rc;
        } else {
            uint8_t plain[kPageSize];
            const int rc = ReadSlot(f, s, plain, lenient);
            if (rc == SQLITE_IOERR_SHORT_READ) shortRead = true;
            else if (rc != SQLITE_OK) return rc;
            std::memcpy(out, plain + (offset - s.start), static_cast<size_t>(n));
            UltraCrypt_SecureZero(plain, sizeof plain);
        }
        out += n;
        offset += n;
        amount -= n;
    }
    return shortRead ? SQLITE_IOERR_SHORT_READ : SQLITE_OK;
}

int CipherWrite(sqlite3_file* file, const void* buffer, int amount, sqlite3_int64 offset) {
    CipherFile* f = Cipher(file);
    const uint8_t* in = static_cast<const uint8_t*>(buffer);
    while (amount > 0) {
        const Slot s = SlotAt(f->kind, offset, amount);
        const int n = static_cast<int>(std::min<sqlite3_int64>(amount, s.end - offset));
        int rc;
        if (!s.page) {
            rc = f->real->pMethods->xWrite(f->real, in, n, offset);
        } else if (offset == s.start && n == kPageSize) {
            rc = WriteSlot(f, s, in);
        } else {
            // Part of a page: what is there, changed where written. SQLite
            // writes whole pages; this is for whatever does not.
            uint8_t plain[kPageSize];
            rc = ReadSlot(f, s, plain, false);
            if (rc == SQLITE_IOERR_SHORT_READ) rc = SQLITE_OK;
            if (rc == SQLITE_OK) {
                std::memcpy(plain + (offset - s.start), in, static_cast<size_t>(n));
                rc = WriteSlot(f, s, plain);
            }
            UltraCrypt_SecureZero(plain, sizeof plain);
        }
        if (rc != SQLITE_OK) return rc;
        in += n;
        offset += n;
        amount -= n;
    }
    return SQLITE_OK;
}

int CipherTruncate(sqlite3_file* f, sqlite3_int64 size) { return Cipher(f)->real->pMethods->xTruncate(Cipher(f)->real, size); }
int CipherSync(sqlite3_file* f, int flags) { return Cipher(f)->real->pMethods->xSync(Cipher(f)->real, flags); }
int CipherFileSize(sqlite3_file* f, sqlite3_int64* size) { return Cipher(f)->real->pMethods->xFileSize(Cipher(f)->real, size); }
int CipherLock(sqlite3_file* f, int level) { return Cipher(f)->real->pMethods->xLock(Cipher(f)->real, level); }
int CipherUnlock(sqlite3_file* f, int level) { return Cipher(f)->real->pMethods->xUnlock(Cipher(f)->real, level); }
int CipherCheckReserved(sqlite3_file* f, int* out) { return Cipher(f)->real->pMethods->xCheckReservedLock(Cipher(f)->real, out); }
int CipherSectorSize(sqlite3_file* f) { return Cipher(f)->real->pMethods->xSectorSize(Cipher(f)->real); }

int CipherFileControl(sqlite3_file* file, int op, void* arg) {
    CipherFile* f = Cipher(file);
    if (op == SQLITE_FCNTL_VFSNAME) {
        const int rc = f->real->pMethods->xFileControl(f->real, op, arg);
        if (rc == SQLITE_OK && arg) {
            char** name = static_cast<char**>(arg);
            char* both = sqlite3_mprintf("%s/%z", kVfsName, *name);
            *name = both;
        } else if (arg) {
            *static_cast<char**>(arg) = sqlite3_mprintf("%s", kVfsName);
        }
        return SQLITE_OK;
    }
    if (op == SQLITE_FCNTL_MMAP_SIZE && f->base.pMethods->iVersion < 3) {
        // Encrypted pages are never mapped: SQLite reads them through xRead.
        if (arg) *static_cast<sqlite3_int64*>(arg) = 0;
        return SQLITE_OK;
    }
    return f->real->pMethods->xFileControl(f->real, op, arg);
}

int CipherDeviceCharacteristics(sqlite3_file* file) {
    // A write of one sector never disturbs its neighbours: true of the
    // platform VFSs' default (SQLITE_POWERSAFE_OVERWRITE), and the condition
    // under which the WAL writes a page in one piece, never padded and split.
    return (Cipher(file)->real->pMethods->xDeviceCharacteristics(Cipher(file)->real)
            | SQLITE_IOCAP_POWERSAFE_OVERWRITE)
           & ~SQLITE_IOCAP_BATCH_ATOMIC;
}

int CipherShmMap(sqlite3_file* f, int region, int size, int extend, void volatile** out) {
    return Cipher(f)->real->pMethods->xShmMap(Cipher(f)->real, region, size, extend, out);
}
int CipherShmLock(sqlite3_file* f, int offset, int n, int flags) {
    return Cipher(f)->real->pMethods->xShmLock(Cipher(f)->real, offset, n, flags);
}
void CipherShmBarrier(sqlite3_file* f) { Cipher(f)->real->pMethods->xShmBarrier(Cipher(f)->real); }
int CipherShmUnmap(sqlite3_file* f, int deleteFlag) {
    return Cipher(f)->real->pMethods->xShmUnmap(Cipher(f)->real, deleteFlag);
}

// Version 2: the wal-index (shared memory) - it holds page numbers, never
// pages - but no xFetch, so SQLite never maps a sealed page into memory.
const sqlite3_io_methods kCipherMethods = {
    2,
    CipherClose, CipherRead, CipherWrite, CipherTruncate, CipherSync, CipherFileSize,
    CipherLock, CipherUnlock, CipherCheckReserved, CipherFileControl, CipherSectorSize,
    CipherDeviceCharacteristics, CipherShmMap, CipherShmLock, CipherShmBarrier, CipherShmUnmap,
    nullptr, nullptr
};

// ---- a file with no key: straight through ---------------------------------

int PlainRead(sqlite3_file* f, void* b, int n, sqlite3_int64 o) { return Cipher(f)->real->pMethods->xRead(Cipher(f)->real, b, n, o); }
int PlainWrite(sqlite3_file* f, const void* b, int n, sqlite3_int64 o) { return Cipher(f)->real->pMethods->xWrite(Cipher(f)->real, b, n, o); }
int PlainFileControl(sqlite3_file* f, int op, void* arg) { return Cipher(f)->real->pMethods->xFileControl(Cipher(f)->real, op, arg); }
int PlainDeviceCharacteristics(sqlite3_file* f) {
    return Cipher(f)->real->pMethods->xDeviceCharacteristics(Cipher(f)->real);
}
int PlainFetch(sqlite3_file* f, sqlite3_int64 o, int n, void** p) {
    sqlite3_file* real = Cipher(f)->real;
    if (real->pMethods->iVersion < 3 || !real->pMethods->xFetch) { *p = nullptr; return SQLITE_OK; }
    return real->pMethods->xFetch(real, o, n, p);
}
int PlainUnfetch(sqlite3_file* f, sqlite3_int64 o, void* p) {
    sqlite3_file* real = Cipher(f)->real;
    if (real->pMethods->iVersion < 3 || !real->pMethods->xUnfetch) return SQLITE_OK;
    return real->pMethods->xUnfetch(real, o, p);
}

const sqlite3_io_methods kPlainMethods = {
    3,
    CipherClose, PlainRead, PlainWrite, CipherTruncate, CipherSync, CipherFileSize,
    CipherLock, CipherUnlock, CipherCheckReserved, PlainFileControl, CipherSectorSize,
    PlainDeviceCharacteristics, CipherShmMap, CipherShmLock, CipherShmBarrier, CipherShmUnmap,
    PlainFetch, PlainUnfetch
};

// ---- the VFS ---------------------------------------------------------------

Key TakePendingKey(const char* name) {
    if (!name) return nullptr;
    std::lock_guard<std::mutex> lock(g_pendingLock);
    auto it = g_pending.find(name);
    return it == g_pending.end() ? nullptr : it->second;
}

// `const char*`, not sqlite3_filename: that typedef is SQLite 3.41+, and
// Emscripten's port (the WebAssembly build) is 3.39.
int VfsOpen(sqlite3_vfs*, const char* name, sqlite3_file* file, int flags, int* outFlags) {
    CipherFile* f = new (file) CipherFile();
    f->real = reinterpret_cast<sqlite3_file*>(reinterpret_cast<char*>(file) + RealOffset());
    f->base.pMethods = nullptr;

    if (flags & SQLITE_OPEN_MAIN_DB) {
        f->key = TakePendingKey(name);
        if (f->key) f->kind = Kind::Database;
    } else if (flags & (SQLITE_OPEN_WAL | SQLITE_OPEN_MAIN_JOURNAL)) {
        sqlite3_file* db = name ? sqlite3_database_file_object(name) : nullptr;
        if (db && (db->pMethods == &kCipherMethods || db->pMethods == &kPlainMethods)
            && Cipher(db)->kind == Kind::Database) {
            f->key = Cipher(db)->key;
            f->kind = (flags & SQLITE_OPEN_WAL) ? Kind::Wal : Kind::Journal;
        }
    } else if (flags & (SQLITE_OPEN_TEMP_DB | SQLITE_OPEN_TEMP_JOURNAL | SQLITE_OPEN_SUBJOURNAL
                        | SQLITE_OPEN_TRANSIENT_DB)) {
        // Only an encrypted connection uses this VFS for its own files, and it
        // keeps them in memory: one here would be its data in the clear.
        f->~CipherFile();
        return SQLITE_CANTOPEN;
    }

    const int rc = g_real->xOpen(g_real, name, f->real, flags, outFlags);
    if (rc != SQLITE_OK) {
        // The platform's file needs no close when its open failed.
        f->real->pMethods = nullptr;
        f->~CipherFile();
        return rc;
    }
    f->base.pMethods = f->kind == Kind::Plain ? &kPlainMethods : &kCipherMethods;
    return SQLITE_OK;
}

int VfsDelete(sqlite3_vfs*, const char* name, int syncDir) { return g_real->xDelete(g_real, name, syncDir); }
int VfsAccess(sqlite3_vfs*, const char* name, int flags, int* out) { return g_real->xAccess(g_real, name, flags, out); }
int VfsFullPathname(sqlite3_vfs*, const char* name, int n, char* out) { return g_real->xFullPathname(g_real, name, n, out); }
void* VfsDlOpen(sqlite3_vfs*, const char* name) { return g_real->xDlOpen(g_real, name); }
void VfsDlError(sqlite3_vfs*, int n, char* out) { g_real->xDlError(g_real, n, out); }
void (*VfsDlSym(sqlite3_vfs*, void* h, const char* sym))(void) { return g_real->xDlSym(g_real, h, sym); }
void VfsDlClose(sqlite3_vfs*, void* h) { g_real->xDlClose(g_real, h); }
int VfsRandomness(sqlite3_vfs*, int n, char* out) { return g_real->xRandomness(g_real, n, out); }
int VfsSleep(sqlite3_vfs*, int us) { return g_real->xSleep(g_real, us); }
int VfsCurrentTime(sqlite3_vfs*, double* out) { return g_real->xCurrentTime(g_real, out); }
int VfsGetLastError(sqlite3_vfs*, int n, char* out) {
    return g_real->xGetLastError ? g_real->xGetLastError(g_real, n, out) : 0;
}
int VfsCurrentTimeInt64(sqlite3_vfs*, sqlite3_int64* out) {
    if (g_real->iVersion >= 2 && g_real->xCurrentTimeInt64) return g_real->xCurrentTimeInt64(g_real, out);
    double now = 0;
    const int rc = g_real->xCurrentTime(g_real, &now);
    *out = static_cast<sqlite3_int64>(now * 86400000.0);
    return rc;
}

sqlite3_vfs g_vfs;

// The VFS, registered once (not as the default); nullptr when SQLite has none to sit on.
sqlite3_vfs* CipherVfs() {
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] {
        if (sqlite3_initialize() != SQLITE_OK) return;
        g_real = sqlite3_vfs_find(nullptr);
        if (!g_real || !UltraCrypt_Initialize()) return;
        std::memset(&g_vfs, 0, sizeof g_vfs);
        g_vfs.iVersion = 2;
        g_vfs.szOsFile = static_cast<int>(RealOffset()) + g_real->szOsFile;
        g_vfs.mxPathname = g_real->mxPathname;
        g_vfs.zName = kVfsName;
        g_vfs.xOpen = VfsOpen;
        g_vfs.xDelete = VfsDelete;
        g_vfs.xAccess = VfsAccess;
        g_vfs.xFullPathname = VfsFullPathname;
        g_vfs.xDlOpen = VfsDlOpen;
        g_vfs.xDlError = VfsDlError;
        g_vfs.xDlSym = VfsDlSym;
        g_vfs.xDlClose = VfsDlClose;
        g_vfs.xRandomness = VfsRandomness;
        g_vfs.xSleep = VfsSleep;
        g_vfs.xCurrentTime = VfsCurrentTime;
        g_vfs.xGetLastError = VfsGetLastError;
        g_vfs.xCurrentTimeInt64 = VfsCurrentTimeInt64;
        ok = sqlite3_vfs_register(&g_vfs, 0) == SQLITE_OK;
    });
    return ok ? &g_vfs : nullptr;
}

// The platform's full path of `path` - the name xOpen will be given.
std::string FullPath(const std::string& path) {
    std::vector<char> out(static_cast<size_t>(g_real->mxPathname) + 1, '\0');
    if (g_real->xFullPathname(g_real, path.c_str(), static_cast<int>(out.size()), out.data()) != SQLITE_OK
        && out[0] == '\0')
        return path;
    return std::string(out.data());
}

// The first page of `path`, read through the platform's VFS (which takes the
// UTF-8 path every platform's SQLite takes). False when there is no such
// file; `size` the bytes read, up to kPageSize.
bool ReadFirstPage(const std::string& path, uint8_t* page, int& size) {
    size = 0;
    const std::string full = FullPath(path);
    int exists = 0;
    if (g_real->xAccess(g_real, full.c_str(), SQLITE_ACCESS_EXISTS, &exists) != SQLITE_OK || !exists)
        return false;
    std::vector<char> storage(static_cast<size_t>(g_real->szOsFile) + 8);
    sqlite3_file* file = reinterpret_cast<sqlite3_file*>(storage.data());
    int outFlags = 0;
    if (g_real->xOpen(g_real, full.c_str(), file, SQLITE_OPEN_READONLY | SQLITE_OPEN_MAIN_DB, &outFlags)
        != SQLITE_OK)
        return false;
    sqlite3_int64 length = 0;
    file->pMethods->xFileSize(file, &length);
    size = static_cast<int>(std::min<sqlite3_int64>(length, kPageSize));
    std::memset(page, 0, kPageSize);
    const int rc = size > 0 ? file->pMethods->xRead(file, page, size, 0) : SQLITE_OK;
    file->pMethods->xClose(file);
    return rc == SQLITE_OK || rc == SQLITE_IOERR_SHORT_READ;
}

UltraDbResult Fail(UltraDbResultCode code, const std::string& message) {
    return UltraDbResult::Error(code, message, "sqlite");
}

UltraDbResult Exec(sqlite3* db, const char* sql, const std::string& what) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) == SQLITE_OK) return UltraDbResult::Ok();
    std::string message = what + ": " + (error ? error : sqlite3_errmsg(db));
    sqlite3_free(error);
    return Fail(UltraDbResultCode::QueryFailed, message);
}

// A copy of `key` the VFS can hold.
Key Shared(const UltraCryptSecureBuffer& key) {
    return std::make_shared<const UltraCryptSecureBuffer>(key.Clone());
}

} // namespace

namespace ultradb_internal {

const char* SqliteCipherVfsName() { return CipherVfs() ? kVfsName : nullptr; }

std::string SqliteCipherFullPath(const std::string& path) {
    return CipherVfs() ? FullPath(path) : path;
}

void SqliteCipherSetPendingKey(const std::string& fullPath, std::shared_ptr<const UltraCryptSecureBuffer> key) {
    std::lock_guard<std::mutex> lock(g_pendingLock);
    if (key) g_pending[fullPath] = std::move(key);
    else g_pending.erase(fullPath);
}

UltraDbResult SqliteCipherPrepare(const std::string& path, const UltraCryptSecureBuffer& key, bool& isNew) {
    isNew = false;
    if (!CipherVfs())
        return Fail(UltraDbResultCode::Unsupported, "SQLite encryption is not available (no UltraCrypt backend)");
    if (key.GetSize() != 32)
        return Fail(UltraDbResultCode::InvalidArgument, "an encryption key is 32 bytes");
    uint8_t page[kPageSize];
    int size = 0;
    if (!ReadFirstPage(path, page, size) || size == 0) { isNew = true; return UltraDbResult::Ok(); }
    if (size >= kHeaderSkip && std::memcmp(page, kSqliteMagic, kHeaderSkip) == 0)
        return Fail(UltraDbResultCode::InvalidArgument,
                    "'" + path + "' is not encrypted; encrypt it first (UltraDb_SetSqliteFileEncryption)");
    if (size < kPageSize || std::memcmp(page, kMagic, kHeaderSkip) != 0)
        return Fail(UltraDbResultCode::ConnectionFailed, "'" + path + "' is not an encrypted database");
    uint8_t plain[kPageSize];
    const bool opens = OpenPage(key, Kind::Database, 1, page, plain);
    UltraCrypt_SecureZero(plain, sizeof plain);
    if (!opens)
        return Fail(UltraDbResultCode::CredentialsUnavailable,
                    "the key does not open '" + path + "' (another key, or the file is damaged)");
    return UltraDbResult::Ok();
}

UltraDbResult SqliteCipherConfigure(sqlite3* db, bool isNew) {
    if (isNew) {
        // The geometry before the first page is written: 4096-byte pages,
        // 40 bytes of each kept for the nonce and the tag.
        if (UltraDbResult r = Exec(db, "PRAGMA page_size=4096;", "setting the page size"); !r) return r;
        int reserve = kReserve;
        sqlite3_file_control(db, "main", SQLITE_FCNTL_RESERVE_BYTES, &reserve);
    }
    return Exec(db, "PRAGMA temp_store=MEMORY;", "keeping temporary data in memory");
}

} // namespace ultradb_internal

bool UltraDb_SqliteEncryptionAvailable() { return CipherVfs() != nullptr; }

UltraDbSqliteFileKind UltraDb_InspectSqliteFile(const std::string& path) {
    if (!CipherVfs()) {
        // Without the VFS there is still the platform's to read with.
        sqlite3_initialize();
        g_real = sqlite3_vfs_find(nullptr);
        if (!g_real) return UltraDbSqliteFileKind::Other;
    }
    uint8_t page[kPageSize];
    int size = 0;
    if (!ReadFirstPage(path, page, size)) return UltraDbSqliteFileKind::Missing;
    if (size == 0) return UltraDbSqliteFileKind::Missing;
    if (size < kHeaderSkip) return UltraDbSqliteFileKind::Other;
    if (std::memcmp(page, kSqliteMagic, kHeaderSkip) == 0) return UltraDbSqliteFileKind::Plain;
    if (std::memcmp(page, kMagic, kHeaderSkip) == 0) return UltraDbSqliteFileKind::Encrypted;
    return UltraDbSqliteFileKind::Other;
}

bool UltraDb_SqliteKeyOpens(const std::string& path, const UltraCryptSecureBuffer& key) {
    bool isNew = false;
    return UltraDb_InspectSqliteFile(path) == UltraDbSqliteFileKind::Encrypted
           && static_cast<bool>(ultradb_internal::SqliteCipherPrepare(path, key, isNew)) && !isNew;
}

UltraDbResult UltraDb_SetSqliteFileEncryption(const std::string& pathIn,
                                              const UltraCryptSecureBuffer* fromKey,
                                              const UltraCryptSecureBuffer* toKey) {
    using namespace ultradb_internal;
    if (!CipherVfs())
        return Fail(UltraDbResultCode::Unsupported, "SQLite encryption is not available (no UltraCrypt backend)");
    const std::string path = ExpandUserPath(pathIn);
    const std::string next = path + ".rekey";
    const std::string full = FullPath(path), fullNext = FullPath(next);
    g_real->xDelete(g_real, fullNext.c_str(), 0);
    g_real->xDelete(g_real, (fullNext + "-journal").c_str(), 0);

    const UltraDbSqliteFileKind kind = UltraDb_InspectSqliteFile(path);
    if (kind == UltraDbSqliteFileKind::Missing) return UltraDbResult::Ok();
    if (kind == UltraDbSqliteFileKind::Other)
        return Fail(UltraDbResultCode::InvalidArgument, "'" + path + "' is not a database");
    if ((kind == UltraDbSqliteFileKind::Encrypted) != (fromKey != nullptr))
        return Fail(UltraDbResultCode::InvalidArgument,
                    kind == UltraDbSqliteFileKind::Encrypted ? "'" + path + "' is encrypted: its key is needed"
                                                             : "'" + path + "' is not encrypted");
    if (fromKey) {
        bool isNew = false;
        if (UltraDbResult r = SqliteCipherPrepare(path, *fromKey, isNew); !r) return r;
    }
    if (toKey && toKey->GetSize() != 32)
        return Fail(UltraDbResultCode::InvalidArgument, "an encryption key is 32 bytes");

    // Read the database as it is - its WAL too - and write it whole to the
    // new file with the new geometry: VACUUM INTO opens its output through
    // the connection's VFS, which finds the new key waiting for that name.
    sqlite3* db = nullptr;
    if (fromKey) SqliteCipherSetPendingKey(full, Shared(*fromKey));
    if (toKey) SqliteCipherSetPendingKey(fullNext, Shared(*toKey));
    int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE, kVfsName);
    SqliteCipherSetPendingKey(full, nullptr);
    UltraDbResult result;
    if (rc != SQLITE_OK) {
        result = Fail(UltraDbResultCode::ConnectionFailed,
                      "opening '" + path + "': " + (db ? sqlite3_errmsg(db) : sqlite3_errstr(rc)));
    } else {
        sqlite3_busy_timeout(db, 5000);
        result = Exec(db, "PRAGMA temp_store=MEMORY;", "keeping temporary data in memory");
        if (result && toKey) result = Exec(db, "PRAGMA page_size=4096;", "setting the page size");
        if (result) {
            int reserve = toKey ? kReserve : 0;
            sqlite3_file_control(db, "main", SQLITE_FCNTL_RESERVE_BYTES, &reserve);
            sqlite3_stmt* stmt = nullptr;
            rc = sqlite3_prepare_v2(db, "VACUUM INTO ?1", -1, &stmt, nullptr);
            if (rc == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, next.c_str(), -1, SQLITE_TRANSIENT);
                rc = sqlite3_step(stmt);
            }
            if (rc != SQLITE_DONE)
                result = Fail(UltraDbResultCode::QueryFailed,
                              "writing '" + next + "': " + sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
        }
    }
    if (db) sqlite3_close_v2(db);   // the last connection: its WAL checkpointed and removed
    db = nullptr;
    SqliteCipherSetPendingKey(fullNext, nullptr);

    // The new file, opened as it will be: checked whole with its key, and
    // synced - VACUUM INTO writes without syncing.
    if (result) {
        if (toKey) SqliteCipherSetPendingKey(fullNext, Shared(*toKey));
        rc = sqlite3_open_v2(next.c_str(), &db, SQLITE_OPEN_READWRITE, kVfsName);
        SqliteCipherSetPendingKey(fullNext, nullptr);
        if (rc != SQLITE_OK) {
            result = Fail(UltraDbResultCode::ConnectionFailed, "opening '" + next + "' to check it");
        } else {
            result = Exec(db, "PRAGMA temp_store=MEMORY;", "keeping temporary data in memory");
            sqlite3_stmt* stmt = nullptr;
            std::string verdict;
            if (result && sqlite3_prepare_v2(db, "PRAGMA quick_check", -1, &stmt, nullptr) == SQLITE_OK
                && sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* text = sqlite3_column_text(stmt, 0);
                verdict = text ? reinterpret_cast<const char*>(text) : "";
            }
            sqlite3_finalize(stmt);
            if (result && verdict != "ok")
                result = Fail(UltraDbResultCode::Internal, "'" + next + "' did not check: " + verdict);
            if (result)
                result = Exec(db, "PRAGMA synchronous=FULL; BEGIN IMMEDIATE; "
                                  "PRAGMA user_version=user_version; COMMIT;",
                              "syncing '" + next + "'");
        }
        if (db) sqlite3_close_v2(db);
    }
    if (!result) {
        g_real->xDelete(g_real, fullNext.c_str(), 0);
        return result;
    }

    // The swap. A WAL or journal of the old file would be the old file's: it
    // was checkpointed by the close above, and anything left is removed.
    std::error_code moved;
    std::filesystem::rename(UltraCanvas::PathFromUtf8(next), UltraCanvas::PathFromUtf8(path), moved);
    if (moved)
        return Fail(UltraDbResultCode::Internal,
                    "could not move '" + next + "' over '" + path + "': " + moved.message());
    g_real->xDelete(g_real, (full + "-wal").c_str(), 0);
    g_real->xDelete(g_real, (full + "-shm").c_str(), 0);
    g_real->xDelete(g_real, (full + "-journal").c_str(), 0);
    return UltraDbResult::Ok();
}

#else // !ULTRADATABASE_HAS_CRYPT

namespace ultradb_internal {
const char* SqliteCipherVfsName() { return nullptr; }
std::string SqliteCipherFullPath(const std::string& path) { return path; }
void SqliteCipherSetPendingKey(const std::string&, std::shared_ptr<const UltraCryptSecureBuffer>) {}
UltraDbResult SqliteCipherPrepare(const std::string&, const UltraCryptSecureBuffer&, bool& isNew) {
    isNew = false;
    return UltraDbResult::Error(UltraDbResultCode::Unsupported,
                                "SQLite encryption is not built in (UltraCrypt is not linked)", "sqlite");
}
UltraDbResult SqliteCipherConfigure(sqlite3*, bool) { return UltraDbResult::Ok(); }
} // namespace ultradb_internal

bool UltraDb_SqliteEncryptionAvailable() { return false; }

UltraDbSqliteFileKind UltraDb_InspectSqliteFile(const std::string& path) {
    std::FILE* f = UltraCanvas::OpenFileUtf8(ultradb_internal::ExpandUserPath(path), "rb");
    if (!f) return UltraDbSqliteFileKind::Missing;
    char head[16] = {};
    const size_t n = std::fread(head, 1, sizeof head, f);
    std::fclose(f);
    if (n == 0) return UltraDbSqliteFileKind::Missing;
    if (n == sizeof head && std::memcmp(head, "SQLite format 3", 16) == 0) return UltraDbSqliteFileKind::Plain;
    if (n == sizeof head && std::memcmp(head, "UltraDB:XC20P:1", 16) == 0) return UltraDbSqliteFileKind::Encrypted;
    return UltraDbSqliteFileKind::Other;
}

bool UltraDb_SqliteKeyOpens(const std::string&, const UltraCryptSecureBuffer&) { return false; }

UltraDbResult UltraDb_SetSqliteFileEncryption(const std::string&, const UltraCryptSecureBuffer*,
                                              const UltraCryptSecureBuffer*) {
    return UltraDbResult::Error(UltraDbResultCode::Unsupported,
                                "SQLite encryption is not built in (UltraCrypt is not linked)", "sqlite");
}

#endif
