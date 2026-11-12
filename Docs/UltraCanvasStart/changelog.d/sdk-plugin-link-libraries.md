- **On Linux with the SDK, Install also sets up the CDR plug-in's link
  libraries.** The SDK's plug-ins are static archives that the skeleton
  links. The CDR plug-in names librevenge, lcms2 and ICU, which were in the
  optional CDR group. On a clean Ubuntu 22.04 the skeleton stopped at
  `-lrevenge-0.0`. With the SDK chosen on Linux, the Tools step now checks
  and installs `librevenge-dev`, `liblcms2-dev` and `libicu-dev` whatever
  groups are chosen. A build from source, and macOS and Windows, whose SDKs
  carry them in `deps/`, are unchanged (`StartPackages` 0.1.3,
  `DependenciesFor`; test `DependenciesFor_adds_the_sdk_plugins_link_libraries_on_linux`).
- **The Linux guide no longer says to set `LD_LIBRARY_PATH`.** The Linux SDK
  carries the run-time libraries its core needs, beside it, with an
  `$ORIGIN` runpath (framework changelog: `sdk-linux-clean-ubuntu`), so an
  application built against it starts from its build folder as it is.
