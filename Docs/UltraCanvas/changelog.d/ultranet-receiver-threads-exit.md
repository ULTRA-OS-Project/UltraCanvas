- **WebSocket, CoAP and AMQP receiver threads no longer abort or outlive
  shutdown.** Each receiver held a `shared_ptr` to its own connection, and
  its `Stop()` always joined:
  - A callback that closed its own socket (`UltraNet_WebSocketClose` from
    `onText`, for example) joined the receiver from itself:
    `std::system_error` ("Resource deadlock avoided") and `std::terminate`.
    The same happened when the receiver dropped the last reference itself.
    `Stop()` now detaches when called on the receiver, and a small lock makes
    starting and stopping the thread race-free.
  - `UltraNet_Shutdown` left open WebSockets running on libcurl through
    `curl_global_cleanup`. It now stops every receiver and frees every easy
    handle first.
  - The CoAP and AMQP plug-ins' `Shutdown()` only emptied their session
    tables, so no receiver was ever told to stop. CoAP's worker even kept
    running `coap_io_process` through `coap_cleanup`. `Shutdown()` now stops
    and joins every receiver before the sessions and the library go.
  - Receivers still running at exit (no shutdown call) read connection and
    callback tables that static destruction had already freed. Those tables
    are now allocated once and never destroyed.
- **`UltraNet_ParseUrl` accepts schemes libcurl does not speak.** It passed
  URLs to libcurl without `CURLU_NON_SUPPORT_SCHEME`, so `coap://`,
  `amqp://`, `sip://`, `rtp://` and `grpc://` URLs were rejected as invalid.
  The plug-ins for those schemes could never connect. `UltraNet_BuildUrl`
  had the same restriction.
