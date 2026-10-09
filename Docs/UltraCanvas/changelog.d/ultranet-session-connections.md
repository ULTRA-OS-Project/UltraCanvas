- **UltraNet sessions no longer share one connection pool between threads.**
  A session (`UltraNetCookies.h`) kept its connections in its libcurl share
  (`CURL_LOCK_DATA_CONNECT`), which libcurl documents as not supported
  between threads that transfer at the same time - and a session's requests
  may come from several. The share now holds only what libcurl does support
  sharing that way: the cookies, the DNS cache and the TLS sessions. The
  connections live in the session's easy handles: a request takes an idle
  handle for itself and gives it back with its connection still open, so
  requests one after another still share one connection, and requests at
  the same time each use their own. Up to 8 idle handles are kept; a session
  created with `reuseConnections = false` closes each one after its request.
  - A cookie jar (`persistCookies`) is written after every request; it used
    to be written when the request's handle was destroyed, which a pooled
    handle is not.
  - `UltraNet_DestroySession` no longer frees the share while a request on
    another thread is still using it: the session is closed when its last
    request returns. `UltraNet_Shutdown` closes every session still open,
    and a session never closed is left alone at exit rather than calling
    into libcurl from a static destructor.
  - Tests: `test_session.cpp` runs a keep-alive HTTP/1.1 server on loopback
    that counts connections - five requests in a row are one connection;
    forty requests from four threads at once all get their answer and the
    cookie another request was given, on no more connections than requests
    in flight; `reuseConnections = false` connects every time.
- **An FTP folder holding nothing but its own entries is listed once.** Many
  servers send the folder's own "." and ".." (MLSD `cdir` / `pdir`, or LIST
  in `ls -la` style) before its entries. A folder with nothing else in it
  read as a listing that could not be parsed, and was asked for again with
  LIST - and on LIST, again with NLST. Both formats now take such a listing
  for the empty folder it is. An MLSD `cdir` named by the folder's path
  (`type=cdir; /pub`) is no longer listed as a subfolder called "/pub".
  Tests in `test_ftp_parser.cpp` and `test_ftp_log.cpp`.
