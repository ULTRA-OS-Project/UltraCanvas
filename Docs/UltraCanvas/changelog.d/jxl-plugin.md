- **JPEG XL in every build: the framework's own loader and saver on libjxl.**
  A libvips built without libjxl (Ubuntu 22.04's) read and wrote no `.jxl`,
  and ImageFormatsTest had to excuse that one runner. `VipsJxlPlugin`
  (`libspecific/Cairo/`) now registers `jxlload`, `jxlload_buffer`,
  `jxlload_source`, `jxlsave`, `jxlsave_buffer` and `jxlsave_target` - the
  names libvips gives its own, so `UCImageRaster`, the export encoder and
  the format inventory need no other spelling - wherever libvips has none,
  on the libjxl CMake finds (`HAS_LIBJXL`). The loader takes the still
  picture at 8 or 16 bits a channel, grey or RGB, with or without alpha;
  the saver writes 8-bit RGB or RGBA with libvips' `Q`, `distance`,
  `effort` and `lossless` arguments. A libvips with its own JPEG XL keeps
  it; `UC_VIPS_JXL_PLUGIN=force` registers the plug-in above it, which is
  how it is tested on a machine that has both. CI's ubuntu-22.04 rows
  build libjxl 0.10.3 from source (cached per runner) and install it under
  `/usr/local`, so their packages carry JPEG XL for the first time, and
  the JPEG XL test runs on every row. `libjxl-dev` joins the dependency
  lists.
