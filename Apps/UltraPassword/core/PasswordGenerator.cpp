// Apps/UltraPassword/core/PasswordGenerator.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "PasswordGenerator.h"

#include "UltraCrypt/UltraCryptCore.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

namespace UltraPassword {
namespace {

std::string Filter(const std::string& set, bool avoidAmbiguous) {
    if (!avoidAmbiguous) return set;
    std::string out;
    for (char c : set)
        if (std::string("l1IO0o").find(c) == std::string::npos) out += c;
    return out;
}

bool PickIndex(size_t bound, size_t& out) {
    uint32_t r = 0;
    if (!UltraCrypt_RandomUInt32(static_cast<uint32_t>(bound), r)) return false;
    out = r;
    return true;
}

} // namespace

std::string GeneratePassword(const GeneratorOptions& options) {
    const int length = std::clamp(options.length, 8, 128);
    std::vector<std::string> classes;
    if (options.lowercase) classes.push_back(Filter("abcdefghijklmnopqrstuvwxyz", options.avoidAmbiguous));
    if (options.uppercase) classes.push_back(Filter("ABCDEFGHIJKLMNOPQRSTUVWXYZ", options.avoidAmbiguous));
    if (options.digits)    classes.push_back(Filter("0123456789", options.avoidAmbiguous));
    if (options.symbols)   classes.push_back("!#$%&*+-=?@^_~.,:;");
    if (classes.empty()) return {};

    std::string all;
    for (const auto& c : classes) all += c;

    std::string out;
    out.reserve(static_cast<size_t>(length));
    // One from each class first, then the rest from the union.
    for (const auto& c : classes) {
        size_t i = 0;
        if (!PickIndex(c.size(), i)) return {};
        out += c[i];
    }
    while (static_cast<int>(out.size()) < length) {
        size_t i = 0;
        if (!PickIndex(all.size(), i)) return {};
        out += all[i];
    }
    // Fisher-Yates, so the guaranteed characters are not always up front.
    for (size_t i = out.size() - 1; i > 0; --i) {
        size_t j = 0;
        if (!PickIndex(i + 1, j)) return {};
        std::swap(out[i], out[j]);
    }
    return out;
}

double EstimateEntropyBits(const std::string& password) {
    if (password.empty()) return 0.0;
    bool lower = false, upper = false, digit = false, other = false;
    for (unsigned char c : password) {
        if (std::islower(c)) lower = true;
        else if (std::isupper(c)) upper = true;
        else if (std::isdigit(c)) digit = true;
        else other = true;
    }
    int alphabet = (lower ? 26 : 0) + (upper ? 26 : 0) + (digit ? 10 : 0) + (other ? 33 : 0);
    return static_cast<double>(password.size()) * std::log2(static_cast<double>(std::max(alphabet, 2)));
}

} // namespace UltraPassword
