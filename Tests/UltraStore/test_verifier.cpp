// Tests/UltraStore/test_verifier.cpp
// A catalog signed with a seed verifies with its public key and with the
// spare beside it, and fails for a byte flipped anywhere, a wrong key, a
// wrong size; hashes agree with the catalog's.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "fixtures.h"

#include "Verifier.h"

using namespace UltraStore;
using namespace ultrastore_test;

TEST(a_signed_catalog_verifies_and_a_tampered_one_does_not) {
    std::vector<uint8_t> seed, pk;
    REQUIRE(GenerateSigningKey(seed, pk));
    REQUIRE_EQ(seed.size(), size_t(32));
    REQUIRE_EQ(pk.size(), size_t(32));
    const auto derived = PublicKeyOfSeed(seed);
    REQUIRE(derived && derived.Value() == pk);

    const std::string catalog = SerializeCatalog(FixtureCatalog());
    const auto signature = SignCatalog(catalog, seed);
    REQUIRE(signature);
    REQUIRE_EQ(signature.Value().size(), size_t(64));

    REQUIRE(VerifyCatalogSignature(catalog, signature.Value(), {pk}));

    // The spare key beside the active one: either accepts.
    std::vector<uint8_t> spareSeed, sparePk;
    REQUIRE(GenerateSigningKey(spareSeed, sparePk));
    REQUIRE(VerifyCatalogSignature(catalog, signature.Value(), {sparePk, pk}));
    REQUIRE(VerifyCatalogSignature(catalog, signature.Value(), {pk, sparePk}));

    // A wrong key alone.
    auto wrong = VerifyCatalogSignature(catalog, signature.Value(), {sparePk});
    REQUIRE(!wrong && wrong.error.code == ErrorCode::SignatureInvalid);

    // One byte of the catalog changed.
    std::string tampered = catalog;
    tampered[tampered.find("42")] = '4';
    tampered[tampered.find("42") + 1] = '3';
    auto bad = VerifyCatalogSignature(tampered, signature.Value(), {pk});
    REQUIRE(!bad && bad.error.code == ErrorCode::SignatureInvalid);

    // One byte of the signature changed.
    std::vector<uint8_t> flipped = signature.Value();
    flipped[7] ^= 0x01;
    REQUIRE(!VerifyCatalogSignature(catalog, flipped, {pk}));

    // Wrong sizes are refused before anything is checked.
    std::vector<uint8_t> shortSig(signature.Value().begin(), signature.Value().end() - 1);
    auto size = VerifyCatalogSignature(catalog, shortSig, {pk});
    REQUIRE(!size && size.error.code == ErrorCode::InvalidArgument);
    std::vector<uint8_t> shortKey(pk.begin(), pk.end() - 1);
    auto keySize = VerifyCatalogSignature(catalog, signature.Value(), {shortKey});
    REQUIRE(!keySize && keySize.error.code == ErrorCode::InvalidArgument);
    auto noKeys = VerifyCatalogSignature(catalog, signature.Value(), {});
    REQUIRE(!noKeys && noKeys.error.code == ErrorCode::InvalidArgument);
}

TEST(signature_files_are_raw_or_hex) {
    std::vector<uint8_t> seed, pk;
    REQUIRE(GenerateSigningKey(seed, pk));
    const auto signature = SignCatalog("bytes", seed);
    REQUIRE(signature);
    std::vector<uint8_t> parsed;
    REQUIRE(ParseSignatureFile(std::string(signature.Value().begin(), signature.Value().end()), parsed));
    REQUIRE(parsed == signature.Value());
    REQUIRE(ParseSignatureFile("  " + ToHex(signature.Value()) + "\n", parsed));
    REQUIRE(parsed == signature.Value());
    REQUIRE(!ParseSignatureFile("not a signature", parsed));
    REQUIRE(!ParseSignatureFile(ToHex(signature.Value()).substr(2), parsed));
}

TEST(hashes_match_in_constant_time_and_case_insensitively) {
    const std::string abc = Sha256Hex("abc");
    REQUIRE_EQ(abc, std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    REQUIRE(PackageHashMatches(abc, "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"));
    REQUIRE(!PackageHashMatches(abc, Hash('a')));
    REQUIRE(!PackageHashMatches(abc, "ba78"));
    REQUIRE(Sha256HexOfFile("/no/such/file/anywhere").empty());
}

TEST(a_seed_of_the_wrong_size_is_refused) {
    const std::vector<uint8_t> shortSeed(16, 1);
    REQUIRE(!PublicKeyOfSeed(shortSeed));
    REQUIRE(!SignCatalog("x", shortSeed));
}
