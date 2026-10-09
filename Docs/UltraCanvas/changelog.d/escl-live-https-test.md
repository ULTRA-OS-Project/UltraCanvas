- **Trusting a scanner on first use is tested on every Linux CI run.** The new
  `IODeviceScannerESCLLiveTest` scans over `https://` from a scanner the test
  starts itself, `Tests/IODeviceScannerESCLLiveScanner.py`. There is no
  reference eSCL scanner to run the way `ippeveprinter` is run for IPP, so
  this one answers the four eSCL calls over TLS only, with self-signed
  certificates the test makes with `openssl`, and logs every request it gets.
  The test checks that:
  - first contact sends nothing but a bare `HEAD /` before the scanner's key
    is known;
  - the key kept is the one in its certificate, under the make and model the
    scanner reports;
  - a feeder run, a flatbed page and a grey page are scanned over the pinned
    connection;
  - the scanner restarted with a different certificate is refused before any
    request reaches it, and the key kept is not replaced;
  - forgetting the key lets the new one be learned;
  - with learning switched off, the scanner is refused.

  CI's Linux rows set `ULTRACANVAS_TEST_ESCL_REQUIRED`, so a skip (no
  Python 3 or `openssl`) fails the run. The keys go to a file of the test's
  own, never the user's.
