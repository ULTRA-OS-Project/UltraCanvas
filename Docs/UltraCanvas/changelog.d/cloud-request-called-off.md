- **UltraCloud: a request its caller called off is `ResultCode::Cancelled`,
  not a network failure** (`HttpProviderBase::FromHttp`, UltraCloudHttp
  0.5.0). A cloud drive's download that the thread's abort check
  (`UltraNet_SetThreadAbortCheck`) ended - UltraFiler calling off a preview
  copy nobody waits for any more - reported `Network: Callback aborted`, also
  when the service had already answered 200 and the body was cut short. It
  now reads "called off", as an FTP transfer called off already did.
  `test_dropbox.cpp` checks it, and that nothing is written.
