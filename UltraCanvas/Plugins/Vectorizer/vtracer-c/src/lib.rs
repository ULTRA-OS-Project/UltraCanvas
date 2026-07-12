// vtracer_c — C ABI shim around the VTracer raster-to-SVG tracer.
// Version: 0.1.0
// Last Modified: 2026-05-21
// Author: UltraCanvas Framework

#![allow(non_camel_case_types)]

use std::ffi::CString;
use std::os::raw::c_char;
use std::panic::{self, AssertUnwindSafe};
use std::ptr;
use std::slice;

use visioncortex::ColorImage;
use vtracer::{ColorMode, Config, Hierarchical, PathSimplifyMode};

const VTRACER_VERSION: &str = concat!("vtracer 0.6.11 / vtracer_c ", env!("CARGO_PKG_VERSION"), "\0");

#[repr(C)]
pub struct UCVectorizerConfig {
    pub color_mode: u8,        // 0 = Binary, 1 = Color
    pub hierarchy: u8,         // 0 = Stacked, 1 = Cutout
    pub path_mode: u8,         // 0 = Pixel,  1 = Polygon, 2 = Spline
    pub _reserved: u8,
    pub filter_speckle: i32,
    pub color_precision: i32,
    pub layer_difference: i32,
    pub corner_threshold: i32,
    pub length_threshold: f64,
    pub splice_threshold: i32,
    pub max_iterations: i32,
}

#[repr(C)]
pub struct UCVectorizerOutput {
    pub svg_utf8: *mut c_char,
    pub svg_len: usize,
    pub error_utf8: *mut c_char,
}

impl Default for UCVectorizerOutput {
    fn default() -> Self {
        Self { svg_utf8: ptr::null_mut(), svg_len: 0, error_utf8: ptr::null_mut() }
    }
}

fn to_color_mode(v: u8) -> ColorMode {
    match v { 0 => ColorMode::Binary, _ => ColorMode::Color }
}

fn to_hierarchical(v: u8) -> Hierarchical {
    match v { 1 => Hierarchical::Cutout, _ => Hierarchical::Stacked }
}

fn to_path_mode(v: u8) -> PathSimplifyMode {
    match v {
        0 => PathSimplifyMode::None,        // VTracer's "Pixel"  == no simplify
        1 => PathSimplifyMode::Polygon,
        _ => PathSimplifyMode::Spline,
    }
}

fn into_cstring(s: String) -> *mut c_char {
    match CString::new(s.replace('\0', " ")) {
        Ok(c) => c.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

fn write_error(out: &mut UCVectorizerOutput, msg: impl Into<String>) {
    out.svg_utf8 = ptr::null_mut();
    out.svg_len = 0;
    out.error_utf8 = into_cstring(msg.into());
}

#[no_mangle]
pub unsafe extern "C" fn ultracanvas_vtracer_from_rgba(
    rgba: *const u8,
    width: i32,
    height: i32,
    config: *const UCVectorizerConfig,
    out: *mut UCVectorizerOutput,
) -> i32 {
    if out.is_null() {
        return -1;
    }
    let out_ref: &mut UCVectorizerOutput = &mut *out;
    *out_ref = UCVectorizerOutput::default();

    if rgba.is_null() || config.is_null() || width <= 0 || height <= 0 {
        write_error(out_ref, "invalid arguments");
        return -2;
    }

    let w = width as usize;
    let h = height as usize;
    let byte_len = w
        .checked_mul(h)
        .and_then(|p| p.checked_mul(4))
        .unwrap_or(0);
    if byte_len == 0 {
        write_error(out_ref, "image too large");
        return -3;
    }

    let cfg = &*config;
    let pixels = slice::from_raw_parts(rgba, byte_len).to_vec();

    // Build the vtracer Config from the C struct.
    let mut vt_cfg = Config::default();
    vt_cfg.color_mode = to_color_mode(cfg.color_mode);
    vt_cfg.hierarchical = to_hierarchical(cfg.hierarchy);
    vt_cfg.mode = to_path_mode(cfg.path_mode);
    if cfg.filter_speckle >= 0   { vt_cfg.filter_speckle = cfg.filter_speckle as usize; }
    if cfg.color_precision > 0   { vt_cfg.color_precision = cfg.color_precision; }
    if cfg.layer_difference >= 0 { vt_cfg.layer_difference = cfg.layer_difference; }
    if cfg.corner_threshold > 0  { vt_cfg.corner_threshold = cfg.corner_threshold; }
    if cfg.length_threshold > 0.0 { vt_cfg.length_threshold = cfg.length_threshold; }
    if cfg.splice_threshold > 0  { vt_cfg.splice_threshold = cfg.splice_threshold; }
    if cfg.max_iterations > 0    { vt_cfg.max_iterations = cfg.max_iterations as usize; }

    let img = ColorImage { pixels, width: w, height: h };

    // VTracer can panic on certain malformed configurations; we catch and report.
    let result = panic::catch_unwind(AssertUnwindSafe(|| vtracer::convert(img, vt_cfg)));

    match result {
        Ok(Ok(svg_file)) => {
            let svg = svg_file.to_string();
            let len = svg.len();
            out_ref.svg_len = len;
            out_ref.svg_utf8 = into_cstring(svg);
            if out_ref.svg_utf8.is_null() {
                write_error(out_ref, "failed to allocate output string");
                return -4;
            }
            0
        }
        Ok(Err(err)) => {
            write_error(out_ref, format!("vtracer error: {err}"));
            -5
        }
        Err(_) => {
            write_error(out_ref, "vtracer panicked");
            -6
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn ultracanvas_vtracer_free(out: *mut UCVectorizerOutput) {
    if out.is_null() {
        return;
    }
    let out_ref = &mut *out;
    if !out_ref.svg_utf8.is_null() {
        drop(CString::from_raw(out_ref.svg_utf8));
        out_ref.svg_utf8 = ptr::null_mut();
    }
    if !out_ref.error_utf8.is_null() {
        drop(CString::from_raw(out_ref.error_utf8));
        out_ref.error_utf8 = ptr::null_mut();
    }
    out_ref.svg_len = 0;
}

#[no_mangle]
pub extern "C" fn ultracanvas_vtracer_version() -> *const c_char {
    VTRACER_VERSION.as_ptr() as *const c_char
}
