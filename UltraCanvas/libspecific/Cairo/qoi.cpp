// libspecific/Cairo/qoi.cpp
// Standalone QOI (Quite OK Image) codec implementation.
//
// This is the single translation unit that compiles the header-only qoi.h
// implementation, so qoi_encode / qoi_decode / qoi_read / qoi_write are
// available to the whole library WITHOUT depending on any optional component.
// QOI is a self-contained codec — it must not be coupled to libvips (the vips
// foreign loader in VipsQOILoader.cpp is a separate, vips-gated consumer of
// these symbols, not their owner). Everything else just includes qoi.h for the
// declarations and links against the symbols defined here.
//
// Version: 1.0.0
// Author: UltraCanvas Framework

#define QOI_IMPLEMENTATION
#include "qoi.h"
