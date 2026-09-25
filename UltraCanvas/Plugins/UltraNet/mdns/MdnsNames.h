// UltraCanvas/Plugins/UltraNet/mdns/MdnsNames.h
// DNS-SD name and record arithmetic, with no platform in it.
//
// The Windows backend has to do by hand what Avahi and Bonjour do for it:
// take a wire name like
//
//     Brother\032MFC-L2750DW\.series._uscan._tcp.local
//
// and get back the three parts a caller wants - the instance
// ("Brother MFC-L2750DW.series"), the service type ("_uscan._tcp") and the
// domain ("local"). Splitting that on '.' is wrong, and wrong in a way that
// only shows up on the printers whose names contain a dot. So the splitting,
// the unescaping and the TXT and address formatting live here, where a test
// can drive them on any machine, and the backend is left holding nothing but
// the Win32 calls.
//
// Everything here is presentation-format DNS (RFC 1035 s5.1) as DNS-SD
// (RFC 6763) uses it: within a label, `\.` is a literal dot, `\\` a literal
// backslash, `\123` the byte with that decimal value, and `\X` for any other
// X is just X.
// Version: 1.0.0
// Author: UltraCanvas Framework / ULTRA OS

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace Mdns {

// Splits a presentation-format DNS name into its labels, undoing the escapes
// inside each one. The separating dots are the unescaped ones only, so
// "a\.b.c" is two labels, "a.b" and "c" - not three.
std::vector<std::string> SplitName(const std::string& name);

// Undoes the escapes in a single label. Exposed because the escaped form is
// what has to go back to the resolver: see ResolveNameFor().
std::string UnescapeLabel(const std::string& label);

// Separates a browse result into its parts, using the service type that was
// browsed for as the marker - which is what makes it reliable, since the
// instance may contain any number of dots and the type never does outside its
// own two labels.
//
//   fullName    "Office\032Scanner._uscan._tcp.local"
//   serviceType "_uscan._tcp"  (a trailing ".local" is tolerated)
//   -> instance "Office Scanner", domain "local"
//
// Returns false when fullName does not contain the type at all, which is how
// a stray record from another service gets dropped rather than mangled.
bool SplitInstanceName(const std::string& fullName,
                       const std::string& serviceType,
                       std::string& outInstance,
                       std::string& outDomain);

// The name to browse for: the service type with ".local" appended when the
// caller has not already done it.
std::string BrowseQueryName(const std::string& serviceType);

// The name to hand back to the resolver for one browse result.
//
// This is deliberately NOT the unescaped instance name. A resolver takes a
// wire name, so an instance called "Lab.Scanner" has to go back as
// "Lab\.Scanner._uscan._tcp.local" or the query asks about a different
// service, one label deeper, that does not exist. The browse gives the
// escaped form already, so the right answer is to pass it through untouched;
// this function exists to say so in one place.
std::string ResolveNameFor(const std::string& browsedFullName);

// The service type with any trailing ".local" removed, which is the form that
// belongs in the middle of an assembled "instance.type.domain".
std::string ServiceTypeOnly(const std::string& serviceType);

// A host name without its root dot. DNS is entitled to say "scanner.local."
// and mean the same host as "scanner.local", but the first one goes on to
// build a URL that some HTTP stacks will not accept, so it is trimmed once
// here rather than in every caller.
std::string TrimTrailingDot(const std::string& host);

// One TXT record as the other two backends format it: "key=value", or bare
// "key" for the valueless keys DNS-SD uses as booleans. Windows reports those
// as a null value, which is not the same as an empty one ("key=").
std::string TxtPair(const std::string& key, const char* value);

// Addresses, for the "ip" attribute. The IPv4 form takes the address in
// network byte order, which is how Win32 carries it. The IPv6 form follows
// RFC 5952: lower case, leading zeros dropped, and the longest run of zero
// groups - two or more - collapsed to "::", preferring the leftmost run.
std::string IPv4ToString(uint32_t networkOrderAddress);
std::string IPv6ToString(const uint8_t bytes[16]);

} // namespace Mdns
} // namespace UltraCanvas
