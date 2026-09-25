- **mDNS discovery works on Windows.** `Plugins/UltraNet/mdns` browsed with a
  raw `DnsQuery_W` for PTR records and stopped there. That names the services
  on the network and cannot say where any of them is, which is worse than it
  sounds: entries came back, every one of them without a host, and every one
  was dropped by the caller. Discovery looked implemented and found nothing.
  eSCL scanners were auto-discoverable on Linux and macOS and not on Windows.
- **Browsing is half of DNS-SD; the other half is resolving.** A browse
  answers what is out there, a resolve answers where it is, and only the
  second produces something a caller can connect to. The backend now does
  both - `DnsServiceBrowse` then `DnsServiceResolve` per instance - and fills
  in `host`, `port`, `ip` and the TXT keys exactly as the Avahi and Bonjour
  backends do.
- **The entry points are bound with `GetProcAddress`, not imported.** They
  arrived in Windows 10 1703. Importing them would stop the module loading at
  all on anything older and take the whole plug-in down with it, so an older
  Windows keeps the PTR-only query instead: names without addresses, which a
  caller skips. Verified rather than assumed - the linked DLL's import table
  lists only `DnsQuery_W` and `DnsFree` from `dnsapi`.
- **The name arithmetic moved somewhere it can be tested**
  (`Plugins/UltraNet/mdns/MdnsNames.{h,cpp}`). Avahi and Bonjour hand back the
  instance, type and domain already separated; Win32 hands back one escaped
  wire name, and splitting that on `.` works until a device is called
  "Lab.Scanner". The splitting, the RFC 1035 unescaping (`\.`, `\\`, `\032`),
  the TXT formatting and the RFC 5952 address formatting live in a
  translation unit with no platform in it.
- **The escaped name is what goes back to the resolver.** Unescaping first and
  re-joining asks about a different name - one label deeper - that no service
  answers to. `Mdns::ResolveNameFor` exists to say so in one place, and the
  test asserts the two forms differ.
- A valueless TXT key is kept distinct from a key set to an empty value:
  DNS-SD uses the first as a boolean flag, Windows reports it as a null value,
  and `key` and `key=` are not the same record.
- `Tests/MdnsNamesTest`: 46 assertions, none needing Windows or a network.
- **Not yet run on Windows.** The translation unit compiles and links under
  CI's own defines and the tested half passes everywhere, but nobody has
  browsed a real network with it. Recorded in `Gaps.md` with the second thing
  found on the way: Bonjour puts the *escaped* instance in `dn` where the
  other two backends put the unescaped one.
