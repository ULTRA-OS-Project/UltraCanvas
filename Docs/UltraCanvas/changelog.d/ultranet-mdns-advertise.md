- **UltraNet mDNS: announcing services, not only finding them.** The mDNS
  plug-in gains `IServiceDiscoveryPlugin` (`UltraNet/UltraNetPlugins.h`):
  `Advertise(UltraNetServiceAdvertisement{serviceType, instanceName, port,
  txt}, handle)` announces a service of the program's own until
  `StopAdvertising` - an Avahi entry group on a threaded poll (renamed on a
  name clash, re-announced when the daemon restarts), Bonjour's
  `DNSServiceRegister`, or Windows' `DnsServiceRegister` (1703+, bound at
  run time like the browse calls). Browsing is unchanged. ChatCube's local
  network is its first user.
