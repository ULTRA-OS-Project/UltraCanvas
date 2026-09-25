// Tests/MdnsNamesTest.cpp
// DNS-SD name and record arithmetic, which is what the Windows mDNS backend
// has to do for itself.
//
// Avahi and Bonjour hand a caller the instance, type and domain already
// separated. Win32 does not: a browse comes back as one wire name, and taking
// it apart means honouring the escapes DNS-SD puts in it. Splitting on '.'
// works for every scanner until someone names one "Lab.Scanner", so the
// splitting is tested here rather than discovered there. None of it needs
// Windows, or a network.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "MdnsNames.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas::Mdns;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

void CheckEqual(const std::string& got, const std::string& want,
                const std::string& what) {
    const bool same = got == want;
    std::cout << (same ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!same) {
        std::cout << "         wanted \"" << want << "\"\n"
                  << "         got    \"" << got << "\"\n";
        ++g_failures;
    }
}

} // namespace

int main() {
    std::cout << "\n=== DNS-SD names ===\n";

    // ---- escapes inside one label ----------------------------------------
    CheckEqual(UnescapeLabel("Brother\\032MFC"), "Brother MFC",
               "\\032 is a space");
    CheckEqual(UnescapeLabel("Lab\\.Scanner"), "Lab.Scanner",
               "\\. is a literal dot");
    CheckEqual(UnescapeLabel("back\\\\slash"), "back\\slash",
               "\\\\ is a literal backslash");
    CheckEqual(UnescapeLabel("plain"), "plain",
               "a label with no escapes is left alone");
    CheckEqual(UnescapeLabel("\\065\\066\\067"), "ABC",
               "consecutive decimal escapes");
    CheckEqual(UnescapeLabel("50\\% off"), "50% off",
               "a backslash before an ordinary character just means that "
               "character");
    // \256 is out of range for a byte, so it is not a decimal escape at all.
    CheckEqual(UnescapeLabel("\\256"), "256",
               "a three-digit escape above 255 is not an escape");
    CheckEqual(UnescapeLabel("odd\\"), "odd\\",
               "a trailing backslash is kept rather than eating a byte");

    // ---- splitting a name into labels ------------------------------------
    {
        const std::vector<std::string> labels =
            SplitName("Lab\\.Scanner._uscan._tcp.local");
        Check(labels.size() == 4, "an escaped dot does not start a new label");
        if (labels.size() == 4) {
            CheckEqual(labels[0], "Lab.Scanner", "  the instance label");
            CheckEqual(labels[1], "_uscan", "  the service label");
            CheckEqual(labels[2], "_tcp", "  the protocol label");
            CheckEqual(labels[3], "local", "  the domain label");
        }
    }
    {
        const std::vector<std::string> labels = SplitName("a.b.c.");
        Check(labels.size() == 3,
              "a trailing dot is a fully-qualified name, not an empty label");
    }

    // ---- taking a browse result apart ------------------------------------
    {
        std::string instance, domain;
        const bool ok = SplitInstanceName(
            "Brother\\032MFC-L2750DW\\.series._uscan._tcp.local",
            "_uscan._tcp", instance, domain);
        Check(ok, "a name containing the browsed type splits");
        CheckEqual(instance, "Brother MFC-L2750DW.series",
                   "  the instance keeps its space and its dot");
        CheckEqual(domain, "local", "  and the domain comes out");
    }
    {
        std::string instance, domain;
        const bool ok = SplitInstanceName("Office Scanner._USCAN._TCP.local",
                                          "_uscan._tcp", instance, domain);
        Check(ok, "the type matches whatever case the responder used");
        CheckEqual(instance, "Office Scanner", "  and the instance survives it");
    }
    {
        // A responder is free to answer about a service nobody asked for.
        // Dropping it is right; mangling it into an entry is not.
        std::string instance, domain;
        Check(!SplitInstanceName("Printer._ipp._tcp.local", "_uscan._tcp",
                                 instance, domain),
              "a name for another service type is refused, not mangled");
    }
    {
        std::string instance, domain;
        Check(!SplitInstanceName("_uscan._tcp.local", "_uscan._tcp",
                                 instance, domain),
              "the bare service type is not an instance");
    }
    {
        // Pathological but legal: an instance named after a service type. The
        // real type is the rightmost match, so the instance must survive whole.
        std::string instance, domain;
        const bool ok = SplitInstanceName("_uscan._tcp._uscan._tcp.local",
                                          "_uscan._tcp", instance, domain);
        Check(ok, "an instance named like a service type still splits");
        CheckEqual(instance, "_uscan._tcp",
                   "  and the rightmost match is the real type");
    }
    {
        std::string instance, domain;
        const bool ok = SplitInstanceName("Scanner._uscan._tcp.local",
                                          "_uscan._tcp.local", instance, domain);
        Check(ok, "a service type given with .local on it still works");
        CheckEqual(instance, "Scanner", "  and yields the same instance");
    }

    // ---- what goes back to the resolver ----------------------------------
    {
        // The trap this whole file exists for: resolving must use the escaped
        // name. "Lab\.Scanner._uscan._tcp.local" and "Lab.Scanner._uscan..."
        // are different names - the second has an extra label - and only the
        // first is the service that answered.
        const std::string browsed = "Lab\\.Scanner._uscan._tcp.local";
        CheckEqual(ResolveNameFor(browsed), browsed,
                   "the resolver is given the escaped name, not the pretty one");
        std::string instance, domain;
        SplitInstanceName(browsed, "_uscan._tcp", instance, domain);
        Check(ResolveNameFor(browsed) != instance + "._uscan._tcp.local",
              "  which is not what re-joining the unescaped instance gives");
    }

    // ---- the name to browse for ------------------------------------------
    CheckEqual(BrowseQueryName("_uscan._tcp"), "_uscan._tcp.local",
               "a bare service type gets .local appended");
    CheckEqual(BrowseQueryName("_uscan._tcp.local"), "_uscan._tcp.local",
               "one that already has it is left alone");
    CheckEqual(BrowseQueryName("_uscan._tcp.LOCAL"), "_uscan._tcp.LOCAL",
               "including when it is spelled in another case");
    Check(BrowseQueryName("").empty(), "an empty type asks nothing");

    CheckEqual(ServiceTypeOnly("_uscan._tcp.local"), "_uscan._tcp",
               "the type on its own drops a trailing .local");
    CheckEqual(ServiceTypeOnly("_uscan._tcp"), "_uscan._tcp",
               "and leaves one that never had it");

    CheckEqual(TrimTrailingDot("scanner.local."), "scanner.local",
               "a host name loses its root dot, which a URL cannot use");
    CheckEqual(TrimTrailingDot("scanner.local"), "scanner.local",
               "one without a root dot is unchanged");
    CheckEqual(TrimTrailingDot("."), ".",
               "the root itself is left alone rather than emptied");

    std::cout << "\n=== TXT records ===\n";
    CheckEqual(TxtPair("ty", "Acme MegaScan"), "ty=Acme MegaScan",
               "a key with a value");
    CheckEqual(TxtPair("rs", ""), "rs=",
               "a key set to nothing keeps its '='");
    CheckEqual(TxtPair("mopria-certified", nullptr), "mopria-certified",
               "a key with no value at all has no '=' - it is a flag, and the "
               "difference is what tells the two apart");

    std::cout << "\n=== addresses ===\n";
    {
        // Network byte order, which is how Win32 carries it: the first byte
        // on the wire is the first number printed.
        const uint8_t octets[4] = {192, 168, 1, 50};
        uint32_t packed = 0;
        std::memcpy(&packed, octets, 4);
        CheckEqual(IPv4ToString(packed), "192.168.1.50",
                   "IPv4 comes out in the order it went in");
    }
    {
        const uint8_t addr[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
                                  0, 0, 0, 0, 0, 0, 0, 0x01};
        CheckEqual(IPv6ToString(addr), "2001:db8::1",
                   "IPv6 collapses its longest zero run");
    }
    {
        const uint8_t loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0, 0, 0, 0, 1};
        CheckEqual(IPv6ToString(loopback), "::1", "the loopback address");
    }
    {
        const uint8_t any[16] = {0};
        CheckEqual(IPv6ToString(any), "::", "the unspecified address");
    }
    {
        // Two runs of zeros, the second longer. RFC 5952 compresses the
        // longest, not the first.
        const uint8_t addr[16] = {0x20, 0x01, 0, 0, 0, 0, 0x00, 0x01,
                                  0, 0, 0, 0, 0, 0, 0, 0x02};
        CheckEqual(IPv6ToString(addr), "2001:0:0:1::2",
                   "the longest zero run is the one collapsed");
    }
    {
        // A single zero group is written out; "::" is for two or more.
        const uint8_t addr[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0x00, 0x01,
                                  0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05};
        CheckEqual(IPv6ToString(addr), "2001:db8:0:1:2:3:4:5",
                   "a lone zero group is spelled out, not collapsed");
    }
    {
        const uint8_t addr[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0,
                                  0x02, 0x11, 0x22, 0xff, 0xfe, 0x33, 0x44, 0x55};
        CheckEqual(IPv6ToString(addr), "fe80::211:22ff:fe33:4455",
                   "a link-local address, lower case and without leading zeros");
    }

    std::cout << "\n";
    if (g_failures == 0) {
        std::cout << "All mDNS name tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cout << g_failures << " FAILED\n";
    return EXIT_FAILURE;
}
