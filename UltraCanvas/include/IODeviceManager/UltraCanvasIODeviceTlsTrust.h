// include/IODeviceManager/UltraCanvasIODeviceTlsTrust.h
// Trust on first use for network devices with self-signed certificates.
//
// A scanner or printer that speaks HTTPS (eSCL's `_uscans._tcp`, IPP's
// `ipps://`) almost always presents a certificate it signed itself, which no
// certificate authority vouches for, so ordinary verification refuses it. A
// device that offered only HTTPS was therefore unreachable. What is done
// instead is what SSH does with host keys:
//
//   1. A device whose certificate passes ordinary verification is used as it
//      is, and nothing is remembered about it.
//   2. The first time one fails it, its public key is fetched - over a
//      connection that sends no request, so nothing of the user's reaches an
//      unverified peer - and remembered against its address. The request then
//      goes ahead pinned to that key.
//   3. From then on that address is reached only when it presents the same
//      key (UltraNetHttpOptions::pinnedPublicKey). A different key fails the
//      request with TlsPublicKeyMismatch and is never learned over the old
//      one: the device was reset or replaced, or something else is answering,
//      and only the user can tell which. IODeviceForgetCertificate() is how
//      they say it was the first.
//
// The first contact is the one moment the key is taken on trust - the same
// bargain SSH makes, and a far better one than the plain HTTP these devices
// are reached over otherwise.
//
// The keys live in DeviceCertificates.conf in the UltraCanvas settings folder
// (UltraCanvasSettingsFolder.h), one "host:port=sha256//..." line per device,
// shared by every application. ULTRACANVAS_DEVICE_CERTIFICATES names another
// file; ULTRACANVAS_DEVICE_TLS_TOFU=0 stops new keys being learned (devices
// already listed are still reached).
// Version: 1.0.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#if defined(ULTRACANVAS_HAS_NET)
#include "UltraNet/UltraNetHttp.h"
#endif

namespace UltraCanvas {

struct IODeviceTrustedCertificate {
    std::string address;   // "host:port", as IODeviceTlsAddress() gives it
    std::string pin;       // "sha256//<base64>"
};

// Every device whose key has been learned.
std::vector<IODeviceTrustedCertificate> IODeviceTrustedCertificates();

// Forgets the key remembered for `address` - "host:port", or any https://
// URL of the device - so the next connection learns it afresh. For a device
// that was reset or replaced and now presents a new key. False when none was
// remembered, or the file could not be written.
bool IODeviceForgetCertificate(const std::string& address);

// The file the keys are kept in. Empty when there is nowhere to keep them.
std::filesystem::path IODeviceTrustedCertificatesFile();

// ----------------------------------------------------------------------------
// Pure helpers - strings in, strings out - exposed for the tests.
// ----------------------------------------------------------------------------

// "host:port" for an https:// URL, the key a device's certificate is kept
// under: the host lower-case, a user name dropped, an IPv6 literal kept in
// brackets, the port written out (443 when the URL leaves it off). Also
// accepts "host:port" itself. Empty for anything else, plain http:// included.
std::string IODeviceTlsAddress(const std::string& urlOrAddress);

// DeviceCertificates.conf, read and written. Blank lines and lines starting
// with '#' are skipped, as is any line whose value is not a sha256// pin.
std::map<std::string, std::string> ParseIODeviceTrustedCertificates(const std::string& text);
std::string FormatIODeviceTrustedCertificates(const std::map<std::string, std::string>& pins);

#if defined(ULTRACANVAS_HAS_NET)
namespace Internal {

// UltraNet_HttpRequest, with the trust described above applied to an
// https:// URL. A plain http:// request goes through untouched.
UltraNetResult DeviceHttpRequest(UltraNetHttpRequest request, UltraNetResponse& response);

}  // namespace Internal
#endif

}  // namespace UltraCanvas
