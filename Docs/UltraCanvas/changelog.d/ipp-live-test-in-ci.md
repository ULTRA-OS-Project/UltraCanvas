- **CI runs the live IPP printer test.** `IODevicePrinterIPPLiveTest` prints
  through the IPP backend to CUPS's reference printer, `ippeveprinter`, and
  skipped wherever that program was missing - which included every CI run, so
  the only test of the backend against a real IPP implementation never ran
  there.
  - The Linux rows now install `cups-ipp-utils` and `avahi-daemon`, and start
    Avahi before the tests. `ippeveprinter` will not start without a DNS-SD
    daemon, even with advertising off ("Unable to initialize DNS-SD"), and
    the new step says so by name if Avahi cannot start. It also warns when
    the runner has no IPv6 loopback, which `ippeveprinter` needs as well
    ("Unable to create IPv6 listener").
  - The test step sets `ULTRACANVAS_TEST_IPP_REQUIRED`. With it set, every
    skip in the test becomes a failure, because in CI a skip looks like a
    pass - the same reason `ULTRAFIBU_TEST_PG_REQUIRED` exists.
  - `Gaps.md` now says what the test needs to run, including IPv6: a
    container without it always skips.
