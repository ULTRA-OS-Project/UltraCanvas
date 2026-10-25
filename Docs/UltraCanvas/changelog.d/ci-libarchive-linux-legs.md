- **CI installs libarchive on the Linux legs.** VirtualFS's only archive
  provider is libarchive; without it the build configures with no provider
  and UltraStore's install tests skip. Named in the apt list beside SQLite
  rather than relied on from the runner image, for the same reason.
