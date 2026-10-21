- **UltraNet: WebSockets failed on every Linux machine whose libcurl was
  built without them, Ubuntu 24.04's included.** The configure check looked
  only for the `curl_ws_send` symbol, which libcurl exports either way, so it
  kept the system library, and every `UltraNet_WebSocketConnect` then failed
  ("not built in"). It now asks the library which protocols it was built
  with (`curl_version_info`), and without `ws` builds the vendored curl in
  `third_party/curl`, as it already did for a curl without versioned symbols.
  Found by ChatCube Direct's relay socket.
