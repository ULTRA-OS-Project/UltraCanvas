- **ChatCube Direct: a relay behind a redirect.** `RelayClient` no longer
  lets libcurl follow redirects - on a 301/302 libcurl sends a POST again
  without its body, which the relay answered with 422. It follows them
  itself (up to three), body and all, and keeps the new base address, which
  `DirectProvider` stores after sign-in. `SetBaseUrl` takes an address
  without a scheme as `https://`, and a 422's `detail` becomes the error
  message. DirectProviderTests 0.1.1 checks both against the relay behind
  a redirector.
