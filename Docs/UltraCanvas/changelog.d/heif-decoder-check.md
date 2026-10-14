- **HEIC and AVIF are offered to open only where they decode.** libvips'
  HEIF loader takes `.heic`, `.heif` and `.avif` wherever it is built with
  libheif, but libheif decodes them only with codec plug-ins it loads
  separately: HEVC (libde265) for a HEIC, AV1 (aomdec, dav1d) for an AVIF.
  Without the HEVC plug-in, FileLoader listed HEIC as readable, so Open
  dialogs and UltraVideo's import offered phone photos that then failed to
  load.
  - The new `VipsCanDecode` (`ImageCairo.h`) asks libheif which decoders it
    has (`heif_have_decoder_for_format`). FileLoader's format inventory now
    reports a bitmap type's `canLoad` with it, as `VipsCanEncode` already
    decides `canSave`.
  - ImageMagick no longer stands in for such a type: libvips hands the file
    to its HEIF loader, and ImageMagick's HEIC reader is libheif as well.
  - The framework links libheif when its development files are installed
    (`HAS_LIBHEIF`). Without them, the inventory goes by the loader's file
    endings, as before.
  - `FileLoaderTypeListsTest` decodes `media/images/test_small.heic` and
    `.avif`, and checks that each type is offered exactly when its sample
    decodes.
