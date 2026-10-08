- **`ULTRACANVAS_DEVICE_BACKENDS` chooses which device backends are
  searched.** A comma-separated list of backend names (`eSCL,IPP`; case
  ignored). When it is set, `IODeviceManager::EnumerateDevices()` runs only
  those. That leaves out a backend that is slow and finds nothing wanted,
  such as SANE probing every port it knows of when only network scanners are
  used. A category none of whose backends is named is reported as an error,
  as one with no backend is, and its devices are left as they were rather
  than dropped. `IODeviceScannerESCLLiveTest` sets it to `eSCL`, which brings
  the test from 5 seconds or more down to under 2. Tested in
  `IODeviceManagerTest`.
- **Trust on first use is tested on macOS and Windows TLS as well as
  Linux.** `IODeviceScannerESCLLiveTest` was Linux-only, so the device trust
  had only been run on libcurl over OpenSSL. It now builds on Windows too,
  starting its scanner and `openssl` there through `CreateProcessW` and
  making its certificates from a configuration file of its own. The new
  `ULTRACANVAS_BUILD_DEVICE_TLS_TESTS` builds it without the full test
  suite, and the macOS rows (Apple's system libcurl) and Windows rows
  (Schannel, MSYS2's `curl-winssl`) run it with a skip treated as a failure.
  CMake now finds Python 3 and `openssl` and passes them to the test.
