// Tests/UCDCryptoEnvelopeTests.cpp
// Unit tests for the UCD document encryption envelope.
//
// These exist because the code they replace was the framework's clearest
// example of security code that silently did nothing: UltraCanvasDocument's
// EncryptData/DecryptData were gated on a macro no build file defined, so the
// compiled path passed plaintext through and reported success, and Load()
// accepted any password at all. Every regression test below would have failed
// against that implementation.
//
// Self-contained: no test framework, no UI stack, no document object model.
//
// Author: UltraCanvas Framework / ULTRA OS
#include "Plugins/Documents/UCDCryptoEnvelope.h"
#include "UltraCrypt/UltraCryptCore.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int g_failures = 0;
static int g_checks   = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

static std::vector<uint8_t> Bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

// The 8-byte UCD file header, exactly as SaveToFile writes it.
static std::vector<uint8_t> FileHeader(uint8_t compression, uint8_t encryption) {
    return {'U', 'C', 'D', 0x01, compression, encryption, 0x00, 0x00};
}

int main() {
    std::printf("UCD crypto envelope tests — backend: %s\n\n",
                UltraCrypt_GetBackendName().c_str());

    if (!UltraCrypt_IsAvailable()) {
        std::printf("FAIL: no crypto backend; the envelope cannot operate.\n");
        return 1;
    }

    const std::vector<uint8_t> body =
        Bytes("<ucd><page id=\"1\">confidential document body</page></ucd>");
    const std::string password = "correct horse battery staple";
    const std::vector<uint8_t> header = FileHeader(1, 1);

    // ===== Round trip =====
    std::printf("Round trip\n");
    std::vector<uint8_t> sealed;
    std::string error;
    Check(UCDCrypto::Seal(body, password, header, sealed, error),
          "Seal succeeds: " + error);
    Check(error.empty(), "Seal leaves no error message on success");

    // The regression that matters most: the output must not be the input.
    Check(sealed.size() != body.size(), "sealed output is not the same size as the input");
    Check(sealed.size() == UCDCrypto::GetEnvelopeHeaderSize() + body.size() + 16,
          "sealed output is header + plaintext + tag");
    Check(std::search(sealed.begin(), sealed.end(), body.begin(), body.end()) ==
              sealed.end(),
          "the plaintext does not appear anywhere in the sealed output");
    Check(std::memcmp(sealed.data(), "UCDE", 4) == 0, "envelope magic is present");
    Check(sealed[4] == 1, "envelope version is 1");
    Check(sealed[5] == 1, "KDF id is Argon2id");
    Check(sealed[6] == 1, "AEAD id is XChaCha20-Poly1305");

    std::vector<uint8_t> opened;
    Check(UCDCrypto::Open(sealed, password, header, opened, error),
          "Open succeeds: " + error);
    Check(opened == body, "round trip recovers the body exactly");

    // Sealing twice must not produce identical output (fresh salt and nonce).
    std::vector<uint8_t> sealed2;
    UCDCrypto::Seal(body, password, header, sealed2, error);
    Check(sealed != sealed2, "sealing the same body twice yields different bytes");

    // ===== Wrong password =====
    std::printf("Wrong password\n");
    std::vector<uint8_t> out;
    Check(!UCDCrypto::Open(sealed, "wrong password", header, out, error),
          "a wrong password is rejected");
    Check(out.empty(), "a wrong password yields no output");
    const std::string wrongPasswordError = error;

    Check(!UCDCrypto::Open(sealed, "", header, out, error),
          "an empty password is rejected");

    // ===== Tampering =====
    std::printf("Tamper detection\n");
    auto expectRejected = [&](std::vector<uint8_t> data,
                              const std::vector<uint8_t>& aad,
                              const std::string& what) {
        std::vector<uint8_t> result;
        std::string err;
        Check(!UCDCrypto::Open(data, password, aad, result, err),
              what + " is rejected");
        Check(result.empty(), what + " yields no output");
    };

    std::vector<uint8_t> t1 = sealed;
    t1.back() ^= 0x01;
    expectRejected(t1, header, "a flipped tag bit");

    std::vector<uint8_t> t2 = sealed;
    t2[UCDCrypto::GetEnvelopeHeaderSize()] ^= 0x01;
    expectRejected(t2, header, "a flipped ciphertext bit");

    std::vector<uint8_t> t3 = sealed;
    t3[20] ^= 0x01;                       // inside the salt
    expectRejected(t3, header, "a flipped salt bit");

    std::vector<uint8_t> t4 = sealed;
    t4[35] ^= 0x01;                       // inside the nonce
    expectRejected(t4, header, "a flipped nonce bit");

    std::vector<uint8_t> t5 = sealed;
    t5[8] = 2;                            // declared Argon2id passes
    expectRejected(t5, header, "an altered iteration count");

    std::vector<uint8_t> truncated(sealed.begin(), sealed.end() - 1);
    expectRejected(truncated, header, "a truncated envelope");

    std::vector<uint8_t> headerOnly(sealed.begin(),
                                    sealed.begin() + UCDCrypto::GetEnvelopeHeaderSize());
    expectRejected(headerOnly, header, "an envelope with no ciphertext");

    expectRejected(std::vector<uint8_t>(), header, "an empty envelope");

    std::vector<uint8_t> badMagic = sealed;
    badMagic[0] = 'X';
    expectRejected(badMagic, header, "a bad magic value");

    std::vector<uint8_t> badVersion = sealed;
    badVersion[4] = 99;
    expectRejected(badVersion, header, "an unknown envelope version");

    std::vector<uint8_t> badAlg = sealed;
    badAlg[6] = 2;
    expectRejected(badAlg, header, "an unknown AEAD id");

    // Absurd cost parameters must be refused before any allocation is tried.
    std::vector<uint8_t> hugeMemory = sealed;
    hugeMemory[12] = 0xFF; hugeMemory[13] = 0xFF;
    hugeMemory[14] = 0xFF; hugeMemory[15] = 0xFF;
    expectRejected(hugeMemory, header, "an absurd declared memory cost");

    // ===== Associated data binds the file header =====
    std::printf("Associated-data binding\n");
    expectRejected(sealed, FileHeader(2, 1), "a modified compression byte in the header");
    expectRejected(sealed, FileHeader(1, 0), "a modified encryption byte in the header");
    expectRejected(sealed, {}, "a missing file header");

    // ===== No oracle =====
    std::printf("Failure messages\n");
    std::vector<uint8_t> tampered = sealed;
    tampered[UCDCrypto::GetEnvelopeHeaderSize()] ^= 0x02;
    std::vector<uint8_t> ignored;
    std::string tamperError;
    UCDCrypto::Open(tampered, password, header, ignored, tamperError);
    Check(tamperError == wrongPasswordError,
          "a wrong password and a modified file report the same message");

    // ===== Edge cases =====
    std::printf("Edge cases\n");
    std::vector<uint8_t> emptySealed;
    Check(UCDCrypto::Seal({}, password, header, emptySealed, error),
          "an empty body can be sealed");
    std::vector<uint8_t> emptyOpened;
    Check(UCDCrypto::Open(emptySealed, password, header, emptyOpened, error),
          "an empty body can be opened");
    Check(emptyOpened.empty(), "an empty body round-trips to empty");

    Check(!UCDCrypto::Seal(body, "", header, out, error),
          "sealing without a password is refused");
    Check(out.empty(), "a refused seal produces no output");

    const std::vector<uint8_t> large(512 * 1024, 0x5A);
    std::vector<uint8_t> largeSealed, largeOpened;
    Check(UCDCrypto::Seal(large, password, header, largeSealed, error),
          "a 512 KiB body can be sealed");
    Check(UCDCrypto::Open(largeSealed, password, header, largeOpened, error),
          "a 512 KiB body can be opened");
    Check(largeOpened == large, "a 512 KiB body round-trips exactly");

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
