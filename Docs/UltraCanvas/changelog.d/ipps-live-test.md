- **Trusting a printer on first use is tested on every Linux CI run.**
  `IODevicePrinterIPPLiveTest` already printed to CUPS's reference printer
  (`ippeveprinter`, from `cups-ipp-utils`, which CI installs) over `ipp://`;
  it now reaches the same printer over `ipps://` too, where it presents the
  self-signed certificate it made itself. It checks that the printer's key is
  learned on first contact and kept under its name, that a document prints
  through the pinned connection, that a key differing from the one kept is
  refused and does not replace it, that forgetting the key lets the same key
  be learned again, and that with learning switched off the printer is
  refused. The keys go to a file of the test's own, never the user's.
