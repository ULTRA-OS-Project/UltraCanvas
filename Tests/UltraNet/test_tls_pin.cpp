// Tests/UltraNet/test_tls_pin.cpp
// UltraNet_PublicKeyPinOf: the certificate public-key pin that
// UltraNetHttpOptions::pinnedPublicKey takes. The expected pin is what openssl
// prints for the same certificate -
//   openssl x509 -pubkey -noout | openssl pkey -pubin -outform der |
//   openssl dgst -sha256 -binary | base64
// - so a mistake in the DER walk or the hash cannot agree with it by accident.
// The bytes come from whoever answered a connection, so malformed and
// truncated input must give an empty pin, never a read past the end.
#include "test_framework.h"

#include <UltraNet/UltraNetMime.h>
#include <UltraNet/UltraNetTls.h>

#include <string>
#include <vector>

namespace {

// A self-signed P-256 certificate (openssl req -x509 -newkey ec ...). 401
// bytes of DER, so the outer lengths are in long form.
const char* const kCertificatePem =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBjTCCATOgAwIBAgIUWe4erv20gIokJIkWf8IcgkDlPlkwCgYIKoZIzj0EAwIw\n"
    "HDEaMBgGA1UEAwwRVWx0cmFOZXQgcGluIHRlc3QwHhcNMjYxMDA1MTAzMzU5WhcN\n"
    "MzYxMDAyMTAzMzU5WjAcMRowGAYDVQQDDBFVbHRyYU5ldCBwaW4gdGVzdDBZMBMG\n"
    "ByqGSM49AgEGCCqGSM49AwEHA0IABFDwGiUgowysCFy8rnB6L1cnhmiTq/qC27g1\n"
    "CfqOjbKqmHg1JJk7ozhmQN9L3+6+tcUChCsyuMsaT1nI6PYvxGSjUzBRMB0GA1Ud\n"
    "DgQWBBQLOGoFHTxbEZohgxUn3W1bqgmNejAfBgNVHSMEGDAWgBQLOGoFHTxbEZoh\n"
    "gxUn3W1bqgmNejAPBgNVHRMBAf8EBTADAQH/MAoGCCqGSM49BAMCA0gAMEUCIFb6\n"
    "cu0rcT/i6axHvnYC+pyxpY2ozduwACcM2jHmeuY4AiEA/rI6+Ro2/m22jV5VqWiA\n"
    "PQ4A+GbYMLMgX8b0ziHO9Z4=\n"
    "-----END CERTIFICATE-----\n";

const char* const kExpectedPin = "sha256//EXZWCU5rn8MYG8MMbem9Op0MlXkL0YcPAEjXh0kPAOM=";

std::vector<uint8_t> Bytes(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::vector<uint8_t> CertificateDer() {
    const std::string pem = kCertificatePem;
    const std::string begin = "-----BEGIN CERTIFICATE-----";
    const size_t start = pem.find(begin) + begin.size();
    const size_t end = pem.find("-----END CERTIFICATE-----");
    std::vector<uint8_t> der;
    UltraNet_Base64Decode(pem.substr(start, end - start), der);
    return der;
}

}  // namespace

TEST(tls_pin_matches_openssl_from_pem) {
    REQUIRE_EQ(UltraNet_PublicKeyPinOf(Bytes(kCertificatePem)), std::string(kExpectedPin));
}

TEST(tls_pin_matches_openssl_from_der) {
    const std::vector<uint8_t> der = CertificateDer();
    REQUIRE_EQ(der.size(), static_cast<size_t>(401));
    REQUIRE_EQ(UltraNet_PublicKeyPinOf(der), std::string(kExpectedPin));
}

TEST(tls_pin_takes_the_first_certificate_of_a_chain) {
    // libcurl's CERTINFO "Cert" field, or a chain file: the first block wins.
    const std::string chain = std::string("Cert:") + kCertificatePem + kCertificatePem;
    REQUIRE_EQ(UltraNet_PublicKeyPinOf(Bytes(chain)), std::string(kExpectedPin));
}

TEST(tls_pin_is_empty_for_what_is_not_a_certificate) {
    CHECK(UltraNet_PublicKeyPinOf({}).empty());
    CHECK(UltraNet_PublicKeyPinOf(Bytes("hello")).empty());
    CHECK(UltraNet_PublicKeyPinOf(Bytes("-----BEGIN CERTIFICATE-----\nMIIB")).empty());
    CHECK(UltraNet_PublicKeyPinOf({0x30, 0x80, 0x00, 0x00}).empty());   // indefinite length
    CHECK(UltraNet_PublicKeyPinOf({0x02, 0x01, 0x00}).empty());         // not a SEQUENCE
}

TEST(tls_pin_never_reads_past_a_truncated_certificate) {
    const std::vector<uint8_t> der = CertificateDer();
    REQUIRE(!der.empty());
    // Every prefix short of the key's end is refused; the key ends well
    // before the signature, so prefixes from there on still carry it.
    int refused = 0;
    for (size_t length = 0; length < der.size(); ++length) {
        const std::vector<uint8_t> prefix(der.begin(), der.begin() + length);
        if (UltraNet_PublicKeyPinOf(prefix).empty()) ++refused;
    }
    // The outer SEQUENCE claims 401 bytes, so every prefix is short of it.
    REQUIRE_EQ(refused, static_cast<int>(der.size()));
}

TEST(tls_pin_refuses_a_length_that_overruns) {
    std::vector<uint8_t> der = CertificateDer();
    REQUIRE(der.size() > 8);
    // tbsCertificate's length (bytes 6-7, after 30 82 01 8d 30 82) made huge.
    der[6] = 0xFF;
    der[7] = 0xFF;
    CHECK(UltraNet_PublicKeyPinOf(der).empty());
}
