// Tests/UltraStoreAdmin/test_keys.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "admin_helpers.h"

#include "KeyVault.h"
#include "Verifier.h"

#include "UltraCrypt/UltraCryptCore.h"

using namespace UltraStoreAdmin;
using namespace admin_test;

TEST(a_sealed_seed_opens_with_its_passphrase_only) {
    if (!UltraCrypt_IsAvailable()) SKIP("no crypto backend");
    const auto key = NewSigningKey("correct horse", KeyCost::Fast());
    REQUIRE(key.Ok());
    REQUIRE_EQ(key.Value().publicKeyHex.size(), size_t(64));
    const std::vector<uint8_t> bytes = key.Value().sealed.Bytes();
    SealedSeed parsed;
    REQUIRE(SealedSeed::Parse(bytes, parsed));
    REQUIRE_EQ(parsed.iterations, uint32_t(1));
    REQUIRE_EQ(parsed.salt.size(), size_t(16));
    REQUIRE_EQ(parsed.nonce.size(), size_t(24));
    const auto seed = OpenSeed(parsed, "correct horse");
    REQUIRE(seed.Ok());
    REQUIRE_EQ(seed.Value().size(), size_t(32));
    // The seed is the whole identity: its public half is the record's.
    const auto pk = UltraStore::PublicKeyOfSeed(seed.Value());
    REQUIRE(pk.Ok());
    REQUIRE_EQ(UltraStore::ToHex(pk.Value()), key.Value().publicKeyHex);
    // A wrong passphrase and an altered byte are one answer.
    REQUIRE(!OpenSeed(parsed, "wrong").Ok());
    REQUIRE(OpenSeed(parsed, "wrong").error.code == UltraStore::ErrorCode::SignatureInvalid);
    std::vector<uint8_t> altered = bytes;
    altered[12] ^= 1;   // the cost in the authenticated header
    SealedSeed parsedAltered;
    REQUIRE(SealedSeed::Parse(altered, parsedAltered));
    REQUIRE(!OpenSeed(parsedAltered, "correct horse").Ok());
    altered = bytes;
    altered.back() ^= 1;
    REQUIRE(SealedSeed::Parse(altered, parsedAltered));
    REQUIRE(!OpenSeed(parsedAltered, "correct horse").Ok());
    REQUIRE(!SealedSeed::Parse({1, 2, 3}, parsedAltered));
    REQUIRE(!NewSigningKey("", KeyCost::Fast()).Ok());
}
