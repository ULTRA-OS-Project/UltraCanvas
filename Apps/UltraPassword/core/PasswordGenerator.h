// Apps/UltraPassword/core/PasswordGenerator.h
// Random passwords for new entries, drawn from the OS CSPRNG (UltraCrypt) —
// never from rand() or a seeded PRNG — without modulo bias, and with at least
// one character from every class asked for, since many sites insist on it.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef ULTRAPASSWORD_PASSWORDGENERATOR_H
#define ULTRAPASSWORD_PASSWORDGENERATOR_H

#include <string>

namespace UltraPassword {

struct GeneratorOptions {
    int  length    = 20;
    bool lowercase = true;
    bool uppercase = true;
    bool digits    = true;
    bool symbols   = true;
    // Leave out characters that are easy to misread: l 1 I O 0.
    bool avoidAmbiguous = true;
};

// Returns an empty string when the random generator fails or no character
// class is selected. `length` is clamped to 8..128.
std::string GeneratePassword(const GeneratorOptions& options = GeneratorOptions());

// A rough strength estimate in bits (length x log2 of the alphabet the
// password draws from), for the hint under a password field.
double EstimateEntropyBits(const std::string& password);

} // namespace UltraPassword

#endif // ULTRAPASSWORD_PASSWORDGENERATOR_H
