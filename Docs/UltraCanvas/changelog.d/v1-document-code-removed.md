- **The v1 document code is removed.**
  `UltraCanvas/Plugins/Documents/UltraCanvasDocument.h` and `.cpp` - the first
  UCD format's reader and writer, with a template manager and a form
  renderer - were never in the build and could not have been: the header
  used a type before declaring it and a window class the framework does not
  have (11 errors), the source another 88, and it included a JSON library
  the build does not carry. Its one part worth keeping left it long ago -
  the encryption, rewritten on UltraCrypt as `UCDCryptoEnvelope`, compiled,
  tested and used by UltraAuthenticator - and the format itself was replaced
  by UCD v2 (`UltraCanvasUCDContainer.h`). The decision notes in
  `Docs/Modules/UltraCrypt/README.md` already called the file likely to be
  deleted; it is, 2,059 lines. What other documents took from it they now
  carry themselves: the UCD v2 specification says what a `WNDW` and a `PAGE`
  section hold instead of naming the removed structs, the ODT / DOCX
  proposal describes the media-resource shape it meant to reuse, and the
  UltraCrypt, UltraAuthenticator and JMAP notes that tell the file's story
  say that it is gone. `UltraCanvasVirtualFSBridge.h`'s comment no longer
  says its compression types extend two enums, one of which was only in
  that file and the other nowhere. The docs check no longer notes a header
  it cannot compile.
