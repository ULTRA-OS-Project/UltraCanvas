// include/UltraCanvasOfficeEncryption.h
// Opens password-protected Office Open XML files: an .xlsx, .docx or .pptx
// saved with "Encrypt with Password" by Office 2010 or later (and by
// LibreOffice and others that write the same thing).
//
// Such a file is not a ZIP package. It is an OLE2 compound file holding two
// streams: "EncryptionInfo", which says how the key is derived, and
// "EncryptedPackage", the original ZIP package encrypted. DecryptOfficePackage
// turns the second back into the ZIP, which the .xlsx and .docx readers then
// read as usual.
//
// Supported: ECMA-376 agile encryption ([MS-OFFCRYPTO] 2.3.4.10-2.3.4.15) with
// AES-128/192/256 in CBC mode and SHA-256 or SHA-512 - what Office writes by
// default since 2013. In order:
//   1. the password key: SHA-512 (or SHA-256) of the salt and the UTF-16LE
//      password, rehashed spinCount times (100000 by default);
//   2. the password verifier, which tells a wrong password from a right one;
//   3. the intermediate key the password key unwraps;
//   4. the dataIntegrity HMAC over the whole EncryptedPackage stream, checked
//      BEFORE anything is decrypted - a changed file is refused, never
//      decrypted into something else;
//   5. the package, in 4096-byte segments, each with its own IV (the hash of
//      the salt and the segment's index).
// Not supported, each refused as Unsupported with the reason: the older
// "standard" encryption (Office 2007, AES-ECB), extensible encryption,
// certificate-only key encryptors, and agile files keyed with SHA-1 (Office
// 2010's default): UltraCrypt offers SHA-1 for HMAC only.
//
// Cryptography is UltraCrypt's (UltraCryptCore.h): its constant-time AES-CBC,
// SHA-2 and HMAC. Without a crypto backend every decryption reports
// NoCryptoBackend.
//
// Version: 1.0.0
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

class UCCompoundFileReader;

enum class OfficeDecryptStatus {
    Decrypted,          // outPackage holds the ZIP package
    NotEncrypted,       // not an encrypted Office package: read it as it is
    PasswordRequired,   // encrypted, and the password given was empty
    WrongPassword,      // the password verifier does not match
    Tampered,           // the password is right but the integrity HMAC does
                        // not match: the file was changed after it was saved
    Unsupported,        // an encryption this reader does not implement
    Damaged,            // the streams are truncated or malformed
    NoCryptoBackend     // UltraCrypt was built without a crypto backend
};

// True when the compound file holds both EncryptionInfo and EncryptedPackage.
bool IsEncryptedOfficePackage(const UCCompoundFileReader& compoundFile);
// The same for a file on disk (`filePath` is UTF-8): false when it cannot be
// read or is not a compound file at all - an ordinary .xlsx is a ZIP.
bool IsEncryptedOfficePackage(const std::string& filePath);

// Decrypts the two streams' contents. `password` is UTF-8, as typed. On any
// status other than Decrypted, outPackage is empty and `error` says why in
// words a user can be shown.
OfficeDecryptStatus DecryptOfficePackage(const std::vector<uint8_t>& encryptionInfo,
                                         const std::vector<uint8_t>& encryptedPackage,
                                         const std::string& password,
                                         std::vector<uint8_t>& outPackage,
                                         std::string& error);
// The same, reading the streams from an open compound file. NotEncrypted when
// it does not hold them.
OfficeDecryptStatus DecryptOfficePackage(const UCCompoundFileReader& compoundFile,
                                         const std::string& password,
                                         std::vector<uint8_t>& outPackage,
                                         std::string& error);
// The same for a file on disk. NotEncrypted for a file that is not a compound
// file (an ordinary ZIP package); Damaged when it cannot be read.
OfficeDecryptStatus DecryptOfficePackageFile(const std::string& filePath,
                                             const std::string& password,
                                             std::vector<uint8_t>& outPackage,
                                             std::string& error);

// Checks a password without decrypting the file: the key derivation and the
// password verifier only, for a prompt that asks again after a wrong one.
// Decrypted here means "the password is right"; every other status is what
// DecryptOfficePackageFile would return (Tampered excepted: the integrity
// check needs the whole file and runs when it is opened).
OfficeDecryptStatus CheckOfficePassword(const std::string& filePath, const std::string& password,
                                        std::string& error);

} // namespace UltraCanvas
