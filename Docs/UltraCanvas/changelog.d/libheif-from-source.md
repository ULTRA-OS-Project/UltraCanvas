- **The Linux packages carry a current libheif without x265.** CI's
  ubuntu-22.04 rows, whose libheif is 1.12 with the GPL x265 linked in,
  build libheif 1.19.8 from source the way the Windows row does: HEVC and
  AV1 decoding (libde265, dav1d, aom), AV1 encoding (aom), JPEG and JPEG
  2000, HEIC encoded by kvazaar (BSD-3-Clause; built from source where
  apt has no package) and never by x265, the codecs linked in rather than
  loaded as plug-ins. Installed under `/usr/local` ahead of the distro's,
  so libvips' `heifload` uses it, and the package closure ships it - the
  first Linux packages that write HEIC, and without a GPL encoder in the
  core. The install step fails if the distro's libheif still shadows it,
  if it links x264 or x265, or if kvazaar is missing; the HEIF/AVIF test
  now runs on the Linux rows with the Windows row's expectations.
