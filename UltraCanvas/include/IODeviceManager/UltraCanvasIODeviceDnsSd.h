// include/IODeviceManager/UltraCanvasIODeviceDnsSd.h
// Reading DNS-SD service names, for the backends that find devices over mDNS.
//
// The mDNS plugin reports a discovered service's *full* name as an entry's
// `dn` on every platform - "Office Printer._ipp._tcp.local" - and its
// platform backends differ in escaping: Avahi and Win32 hand the name back
// readable, Bonjour in DNS presentation form ("Office\032Printer._ipp._tcp.local.").
// What a user should see is the instance part alone, "Office Printer".
//
// Pure functions over strings: no sockets, no plugin.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace UltraCanvas {

// Where the service type starts in DNS-SD name `name`: the last occurrence of
// any of `serviceTypes` (each written "._ipp._tcp", compared ignoring case)
// that is followed by a dot or ends the name, and is not at its very start.
// The last, because an instance may itself contain those characters; the
// real type is the one nearest the domain. npos when there is none.
size_t DnsSdServiceTypeStart(const std::string& name,
                             const std::vector<std::string>& serviceTypes);

// DNS presentation-format escapes undone, as Bonjour leaves them in an
// instance name: \032 is a byte by decimal value, \X is X.
std::string DnsSdUnescape(const std::string& text);

// The instance name - "Office Printer" - out of a full DNS-SD service name:
// cut where DnsSdServiceTypeStart() says the type starts, with escapes undone.
// A name with none of `serviceTypes` in it is taken to be the instance
// already, and only unescaped.
std::string DnsSdInstanceName(const std::string& serviceName,
                              const std::vector<std::string>& serviceTypes);

}  // namespace UltraCanvas
