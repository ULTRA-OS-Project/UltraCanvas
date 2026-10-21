- **Device TLS trust: a verification that times out goes on to first-use
  learning.** The ordinary verified request a device gets first, to spare a
  device with a vouched-for certificate any pinning, returned its result
  whenever it was not a certificate refusal - a timeout included. On the
  Windows runners that first request to the eSCL live test's self-signed
  scanner on 127.0.0.1 timed out at its 10 s twice (CI runs 3938 and 4091),
  with no request ever reaching the scanner, the way a chain build that
  stalls on an unknown root does. A timeout now goes on to the first-use
  probe and the pinned request, as a refused chain does; a device that is
  simply away still times out, once more.
