// core/UltraCrypt/UltraCryptSystemRoots.cpp
// The root certificates the operating system trusts, as DER, for
// UltraCrypt_CheckCertificate's system roots (UltraCryptX509.cpp):
//
//  - Windows: the ROOT certificate store (the current user's view, which
//    includes the machine's) and the machine's AuthRoot store - the
//    third-party roots Windows fetches through its automatic root update -
//    less any certificate in the Disallowed store. A root Windows has not
//    fetched yet (it does so the first time its own chain building needs
//    one) is not there.
//  - macOS: the system's anchors, and the certificates an administrator or
//    the user marked as trusted roots in Keychain Access (trust settings of
//    the admin and user domains; one they marked "Never Trust" is left out).
//  - Elsewhere: none - OpenSSL's default store is the system's there
//    (/etc/ssl/certs and the like), and UltraCryptX509.cpp reads it itself.
//
// Kept apart from UltraCryptX509.cpp so that neither <wincrypt.h> (whose
// X509_NAME and friends are macros that collide with OpenSSL's types) nor
// Apple's frameworks meet OpenSSL's headers in one file.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include <algorithm>
#include <cstdint>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

namespace ultracrypt_detail {

#if defined(_WIN32)
namespace {

using Der = std::vector<uint8_t>;

void ReadStore(HCERTSTORE store, std::vector<Der>& out) {
    if (!store) return;
    PCCERT_CONTEXT cert = nullptr;
    while ((cert = CertEnumCertificatesInStore(store, cert)) != nullptr)
        out.emplace_back(cert->pbCertEncoded, cert->pbCertEncoded + cert->cbCertEncoded);
    CertCloseStore(store, 0);
}

HCERTSTORE OpenMachineStore(const wchar_t* name) {
    return CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                         CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_READONLY_FLAG | CERT_STORE_OPEN_EXISTING_FLAG,
                         name);
}

} // namespace
#endif

#if defined(__APPLE__)
namespace {

void AppendDer(SecCertificateRef cert, std::vector<std::vector<uint8_t>>& out) {
    CFDataRef der = SecCertificateCopyData(cert);
    if (!der) return;
    const UInt8* bytes = CFDataGetBytePtr(der);
    out.emplace_back(bytes, bytes + CFDataGetLength(der));
    CFRelease(der);
}

// Whether the trust settings of `domain` make `cert` a root to trust: no
// constraints at all means "always trust", a constraint with the result
// TrustRoot or TrustAsRoot (a missing result counts as TrustRoot) trusts it,
// and Deny anywhere rules it out.
bool TrustedAsRoot(SecCertificateRef cert, SecTrustSettingsDomain domain) {
    CFArrayRef settings = nullptr;
    if (SecTrustSettingsCopyTrustSettings(cert, domain, &settings) != errSecSuccess || !settings) return false;
    bool trusted = CFArrayGetCount(settings) == 0;
    for (CFIndex i = 0; i < CFArrayGetCount(settings); ++i) {
        CFDictionaryRef entry = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(settings, i));
        SInt32 result = kSecTrustSettingsResultTrustRoot;
        if (CFNumberRef value = static_cast<CFNumberRef>(CFDictionaryGetValue(entry, kSecTrustSettingsResult)))
            CFNumberGetValue(value, kCFNumberSInt32Type, &result);
        if (result == kSecTrustSettingsResultDeny) {
            trusted = false;
            break;
        }
        if (result == kSecTrustSettingsResultTrustRoot || result == kSecTrustSettingsResultTrustAsRoot) trusted = true;
    }
    CFRelease(settings);
    return trusted;
}

} // namespace
#endif

std::vector<std::vector<uint8_t>> PlatformRootsDer() {
    std::vector<std::vector<uint8_t>> out;
#if defined(_WIN32)
    std::vector<Der> roots, disallowed;
    ReadStore(CertOpenSystemStoreW(0, L"ROOT"), roots);
    ReadStore(OpenMachineStore(L"AuthRoot"), roots);
    ReadStore(CertOpenSystemStoreW(0, L"Disallowed"), disallowed);
    ReadStore(OpenMachineStore(L"Disallowed"), disallowed);
    std::sort(disallowed.begin(), disallowed.end());
    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    for (Der& der : roots)
        if (!std::binary_search(disallowed.begin(), disallowed.end(), der)) out.push_back(std::move(der));
#elif defined(__APPLE__)
    CFArrayRef anchors = nullptr;
    if (SecTrustCopyAnchorCertificates(&anchors) == errSecSuccess && anchors) {
        for (CFIndex i = 0; i < CFArrayGetCount(anchors); ++i)
            AppendDer(static_cast<SecCertificateRef>(const_cast<void*>(CFArrayGetValueAtIndex(anchors, i))), out);
        CFRelease(anchors);
    }
    for (SecTrustSettingsDomain domain : { kSecTrustSettingsDomainAdmin, kSecTrustSettingsDomainUser }) {
        CFArrayRef certs = nullptr;
        if (SecTrustSettingsCopyCertificates(domain, &certs) != errSecSuccess || !certs) continue;
        for (CFIndex i = 0; i < CFArrayGetCount(certs); ++i) {
            SecCertificateRef cert = static_cast<SecCertificateRef>(const_cast<void*>(CFArrayGetValueAtIndex(certs, i)));
            if (TrustedAsRoot(cert, domain)) AppendDer(cert, out);
        }
        CFRelease(certs);
    }
#endif
    return out;
}

} // namespace ultracrypt_detail
