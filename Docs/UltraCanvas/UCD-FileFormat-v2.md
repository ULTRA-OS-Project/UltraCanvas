# UltraCanvas Document File Format v2.0 (UCD v2)

Status: **Draft specification** — reference for the v2 implementation of
`UltraCanvas/Plugins/Documents/UltraCanvasDocument.*`.

UCD v2 is a multipurpose container for documents, forms, vector graphics and
window/UI descriptions, with room for bitmap graphics and video in the future.
One container format serves all content types:

- window content for a programming IDE
- single- and multi-page documents (PDF-like)
- saveable/fillable forms
- vector graphics (XAR-like feature set)
- website-like pages with CSS-style layout and text styles
- future: bitmap graphics, video

Design goals:

- **Detectable by content.** A human-readable ASCII signature at offset 0, so
  `file(1)`, filers and hex viewers can identify the format and content type
  without any parsing.
- **Near real-time preview.** An optional HEIC thumbnail stored *uncompressed*
  directly after the fixed header, so a filer can extract a preview by reading
  a handful of bytes — no decompression, no decryption, no body parsing.
- **Compressed by default.** The body is compressed (per section); the
  thumbnail is not (HEIC is already internally compressed).
- **Binary by default, text for debugging.** The body payload is a binary
  serialization by default; the same structure can be written as XML/JSON text
  for debugging, signalled by one header byte.
- **Forward compatible.** Fixed-offset header fields, a header-extension
  length, and length-prefixed body sections that old readers can skip.

All multi-byte integers are **little-endian**. All offsets are from the start
of the file unless stated otherwise.

---

## 1. File layout overview

```
+--------------------------------------+
| Fixed header (28 bytes)              |  never compressed, never encrypted
+--------------------------------------+
| Header extension (optional)          |  length given in fixed header
+--------------------------------------+
| Thumbnail (optional, raw HEIC/PNG)   |  never compressed, never encrypted
+--------------------------------------+
| Section 0 (usually INDX)             |  each section is independently
| Section 1                            |  compressed and (optionally)
| ...                                  |  encrypted
| Section n                            |
+--------------------------------------+
```

## 2. Signature and fixed header

### 2.1 Signature (bytes 0–11)

The 12-byte signature follows the PNG pattern — every byte has a job:

| Offset | Size | Value | Purpose |
|---|---|---|---|
| 0 | 1 | `0x89` | High bit set: file cannot be mistaken for ASCII text; detects 7-bit stripping. |
| 1 | 8 | ASCII type descriptor, NUL-padded | Human-readable content type (see 2.2). |
| 9 | 2 | `0x0D 0x0A` (`\r\n`) | Detects DOS↔Unix line-ending conversion by text-mode transfers. |
| 11 | 1 | `0x1A` | Stops accidental console output (`type`) on legacy systems. |

### 2.2 Type descriptors

The descriptor names the *primary* content type of the file. It is pure
ASCII, at most 8 characters, padded with `0x00` to exactly 8 bytes.

| Descriptor | Bytes 1–8 (hex) | Primary content |
|---|---|---|
| `UCDoc` | `55 43 44 6F 63 00 00 00` | Multi-page document (PDF-like) |
| `UCForm` | `55 43 46 6F 72 6D 00 00` | Saveable / fillable form |
| `UCVector` | `55 43 56 65 63 74 6F 72` | Vector graphics (XAR-like) |
| `UCWindow` | `55 43 57 69 6E 64 6F 77` | Window/UI description (IDE templates) |
| `UCBitmap` | `55 43 42 69 74 6D 61 70` | Bitmap graphics (future) |
| `UCVideo` | `55 43 56 69 64 65 6F 00` | Video (future) |
| `UCAudio` | `55 43 41 75 64 69 6F 00` | Audio (future) |
| `UC3D` | `55 43 33 44 00 00 00 00` | 3D scenes/models (future) |

Detection rules:

- All UltraCanvas files share the 3-byte prefix `0x89 'U' 'C'` and the fixed
  trailer `0x0D 0x0A 0x1A` at offsets 9–11. Matching prefix + trailer
  identifies "an UltraCanvas container"; the descriptor at offset 1
  distinguishes the content type. Both live at fixed offsets, so standard
  magic databases (`file(1)`, shared-mime-info, filer sniffers) can match
  them with a single fixed-offset rule per type.
- The descriptor selects the *default viewer/editor*; it does not restrict
  the sections a file may contain (a `UCDoc` may embed vector and form
  sections, for example).

### 2.3 Fixed header fields (bytes 12–27)

| Offset | Size | Field | Values / meaning |
|---|---|---|---|
| 12 | 1 | Version major | `2` for this specification |
| 13 | 1 | Version minor | `0` |
| 14 | 1 | Body encoding | `0` = binary (default), `1` = XML text (debug), `2` = JSON text (debug) |
| 15 | 1 | Default body compression | `0` = none, `1` = deflate, `2` = gzip, `3` = LZMA. Applies to sections that do not override it. Default when writing: `1` (deflate). |
| 16 | 1 | Encryption type | `0` = none, `1` = AES-256-GCM, `2` = ChaCha20-Poly1305, `3` = SuperVault remote authorization (see 4.4) |
| 17 | 1 | Flags | See 2.4 |
| 18 | 2 | Reserved | Must be `0x0000`; readers must ignore |
| 20 | 4 | Thumbnail length | uint32, bytes of raw thumbnail data; `0` if no thumbnail |
| 24 | 4 | Header extension length | uint32, bytes of extension data following the fixed header; `0` in v2.0 |

A reader needs exactly the first 28 bytes to know: format + content type,
version, whether it can show a preview (and how many bytes to read for it),
and whether a password will be required for the body.

### 2.4 Flags byte (offset 17)

| Bit | Name | Meaning |
|---|---|---|
| 0 | `THUMBNAIL` | A thumbnail is present. Must agree with thumbnail length > 0. |
| 1 | `THUMB_FORMAT` | `0` = HEIC, `1` = PNG (fallback for writers without a HEIC encoder). |
| 2 | `ENCRYPTED` | The document body is password-protected. Mirrors *encryption type ≠ 0* so filers can test one bit without reading offset 16. |
| 3 | `PRIVATE` | Confidential document: writers must **omit** the thumbnail and must not expose metadata outside the encrypted body. When set, bit 0 must be `0` and thumbnail length must be `0`. |
| 4–7 | reserved | Writers set `0`; readers ignore. |

Notes:

- `ENCRYPTED` without `PRIVATE` is explicitly allowed and is the default for
  password-protected files: the thumbnail (and the plain-text `META` section,
  if the writer chooses to keep it unencrypted) remain readable so the filer
  can still preview the document. `PRIVATE` is the opt-in for hiding even the
  preview.
- The fixed header, header extension and thumbnail are **never** encrypted or
  compressed. Encryption applies per body section (see 4.3).

## 3. Thumbnail

Purpose: near real-time display in a filer, especially for vector graphics
where full rendering may be slow.

- Located immediately after the fixed header + header extension.
- Stored **raw** (HEIC by default, PNG fallback per flag bit 1). It is never
  wrapped in the container compression — HEIC/PNG are already compressed and
  re-compressing wastes CPU for no gain.
- Length is given at header offset 20, so a filer reads
  `28 + extLength + thumbLength` bytes total and hands the raw data to the
  system HEIC/PNG decoder. Image dimensions come from the image data itself.

Writer policy (application-level, not enforced by the format):

- Thumbnail size is **configurable** (default suggestion: 256 px on the
  longest edge; the writer may choose any size).
- For vector graphics, a thumbnail should be generated once the element count
  exceeds a **configurable threshold** (suggestion: 50 elements); below the
  threshold a filer can render the vector content directly at interactive
  speed, so no thumbnail is required.
- Writers must regenerate the thumbnail on save whenever visible content
  changed; a stale thumbnail is worse than none.

## 4. Body sections

The body is a sequence of length-prefixed sections. Sections give random
access (render page 40 without touching pages 1–39), let old readers skip
unknown content, and allow large media to be streamed.

### 4.1 Section header (12 bytes, never compressed/encrypted)

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 4 | Type ID | Four ASCII characters (see 4.2) |
| 4 | 4 | Payload length | uint32, bytes of payload as stored in the file (i.e. after compression/encryption) |
| 8 | 1 | Compression | `0xFF` = use header default (offset 15); otherwise same codes as offset 15 |
| 9 | 1 | Section flags | bit 0 = payload encrypted; bits 1–7 reserved |
| 10 | 2 | Reserved | `0x0000` |

Sections follow each other back-to-back; the file ends after the last
section. A reader iterates by jumping `12 + payloadLength` per section.

### 4.2 Section types

| ID | Content | Notes |
|---|---|---|
| `INDX` | Section index: array of (type ID, absolute offset, stored length, uncompressed length) | Optional but recommended as the **first** section; enables random access without scanning. |
| `META` | Document metadata: title, author, dates, custom properties, file UUID (random, set at creation), default locale (see 4.6) | May be left unencrypted in an `ENCRYPTED` file unless `PRIVATE` is set. |
| `STYL` | Stylesheet: named styles for fonts, padding, margins, colors — CSS-like, referencing the `UltraCanvas::CSSLayout` property set | Shared styles referenced by ID from pages/components. |
| `WNDW` | Window definitions (`UCWindowData`) | IDE templates / window content. |
| `PAGE` | One page (`UCPageData`) including its component tree | One section per page → per-page random access. |
| `FORM` | Form field definitions, validation rules, saved field values | |
| `VECT` | Vector graphics document (serialized `VectorStorage::VectorDocument`: layers, paths, gradients, masks, filters, text-on-path …) | XAR-like feature set. Payload starts with the sub-format preamble (4.5). |
| `MEDI` | One embedded media resource (image/audio/video/font), MIME-typed | One section per resource; already-compressed media should set compression `0` (none). |
| `BMAP` | One native bitmap image | Payload starts with the sub-format preamble (4.5). |
| `VIDS` | One native video stream | Payload starts with the sub-format preamble (4.5). |
| `AUDS` | One native audio stream | Payload starts with the sub-format preamble (4.5). |
| `SC3D` | One 3D scene/model | Payload starts with the sub-format preamble (4.5). |
| `NAVI` | Navigation: page order, bookmarks, table of contents, transitions | |
| `LOCL` | Localization string table for one locale (see 4.6) | One section per locale; loaded on demand via `INDX`. |
| `SECU` | Security parameters: KDF salt, iteration count, permission bits (print/copy/edit/form-fill) | Never encrypted (it is the input to decryption). |
| `SVLT` | SuperVault remote-authorization record (see 4.4) | Never encrypted or compressed (it is the input to the authorization request); required when encryption type = `3`. |
| `XTND` | Reserved for vendor/application extensions | Readers must skip unknown types. |

Rules:

- Unknown section types must be skipped using the payload length — this is
  the forward-compatibility mechanism.
- Multiple sections of the same type are allowed where it makes sense
  (`PAGE`, `MEDI`, `VECT`, `WNDW`, `LOCL`).
- Each `WNDW` section contains exactly **one** window definition; the window
  ID lives inside the payload and must be unique file-wide. Windows
  reference each other (e.g. a main window opening a child) by window ID.
- The content-type descriptor in the signature declares the primary sections
  a file is expected to contain (e.g. `UCVector` → at least one `VECT`), but
  mixed documents are valid.

### 4.3 Compression and encryption pipeline

Per section, in this order:

1. Serialize the payload (binary by default; XML/JSON in debug mode).
2. Compress (unless compression = none for this section).
3. Encrypt (only if header encryption type ≠ 0 and the section's *encrypted*
   flag is set), using the key derived via the `SECU` parameters
   (recommended KDF: Argon2id, fallback PBKDF2-HMAC-SHA256).

Compression **per section** (rather than one blob over the whole body, as in
v1) is what makes the thumbnail-at-front and random page access possible.

### 4.4 SuperVault remote-authorization encryption (encryption type `3`)

SuperVault protects a file so that it can only be opened after the **original
owner confirms the request in real time** (or via a pre-granted policy)
through the SuperVault app/service. The file itself contains **no key
material at all** — offline brute force is pointless because there is nothing
in the file to brute-force. What the file does contain is the identity data
needed to *ask*: who owns it, who is allowed to ask, and a fingerprint that
proves the file is the original.

#### `SVLT` record layout

Stored as a `SVLT` section, always uncompressed and unencrypted. All strings
are UTF-8; IDs are fixed 32-byte SuperVault account identifiers (shorter IDs
are NUL-padded).

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | Record version | `1` |
| 1 | 1 | Hash algorithm | `0` = SHA-256 (default), `1` = BLAKE3-256. (MD5 is deliberately **not** assigned a code point — it is collision-broken and a forged file with a matching MD5 is practical today. SHA-256 fills the same role securely.) |
| 2 | 2 | Reserved | `0x0000` |
| 4 | 16 | File UUID | Random, generated once at creation; the primary key under which the SuperVault service stores this file's record. |
| 20 | 32 | Owner ID | SuperVault account of the creator — the person contacted to confirm each open request. |
| 52 | 2 | Receiver count | uint16, ≥ 1 |
| 54 | 32 × n | Receiver IDs | Accounts allowed to *request* opening. Requests from any other account are rejected by the service before the owner is even notified — this blocks fake opening requests. |
| … | 32 | Creation hash | Computed at creation (see below) and simultaneously registered with the service. |
| … | 4 | Key-material length hint | uint32, expected size of the personal key material the service will return (32 bytes – 1 MiB / `0x00100000`). |
| … | 16 | KDF salt | Random per file; input to HKDF (below). |

#### Creation hash

Computed once when the file is written, over: the fixed header + header
extension + thumbnail bytes, the `SVLT` record with the hash field zeroed,
and the ciphertext of every encrypted section. The same value is stored in
the file **and** registered with the SuperVault service at creation. At open
time both the app and the service recompute/compare it, which detects:

- tampered or re-encrypted files masquerading under a stolen `SVLT` record,
- a forged `SVLT` record pointing at someone else's registration,
- corruption before the owner is disturbed with a confirmation prompt.

#### Open flow

1. Reader app parses the (plain) `SVLT` section.
2. App authenticates to the SuperVault service as one of the listed
   **receiver IDs** and sends: file UUID, owner ID, creation hash, and a
   fresh challenge nonce (nonce prevents replaying a captured request).
3. Service checks: UUID exists, requester is in the receiver list, creation
   hash matches the record registered at creation. Any mismatch → refused,
   owner never notified.
4. Service asks the **owner** to confirm (push notification in the SuperVault
   app); the owner may also have pre-granted this receiver.
5. On confirmation the service returns the **personal key material** — an
   opaque blob of 32 bytes up to 1 MiB, generated and stored by the service
   at file creation, never embedded in the file — over the mutually
   authenticated encrypted channel.
6. The app derives the actual section cipher key:
   `key = HKDF-SHA256(keyMaterial, salt = KDF salt, info = "UCD-SVLT-v1" ‖ fileUUID)`
   and decrypts the encrypted sections (AES-256-GCM). Key material is held in
   memory only and discarded when the document closes.

#### Design notes

- The variable-size key material (up to 1 MiB) is supported as specified;
  note that beyond 32 random bytes the *cryptographic* strength of the
  derived AES-256 key no longer increases — the large blob's value is
  operational (the service can rotate/partition it, embed policy, or pad it
  so blob size reveals nothing), not brute-force resistance.
- `PRIVATE` flag semantics apply as usual; SuperVault files may still carry a
  thumbnail unless `PRIVATE` is set.
- Availability trade-off is intentional and absolute: no service + owner
  confirmation → no access. If the owner account is not accessible, dormant,
  or deleted, the file **cannot be opened — permanently**. There is no key
  escrow, no recovery path, and no offline fallback; this is a deliberate
  security property, not a limitation to be worked around. Writer
  applications must state this clearly to the user when a file is first
  saved with SuperVault protection.

### 4.5 Sub-format identification

Content sections that can carry more than one encoding (`VECT`, `BMAP`,
`VIDS`, `AUDS`, `SC3D`) begin their payload with a common **8-byte
sub-format preamble**, so every present and future media section identifies
its encoding the same way. `MEDI` is the exception — it keeps its MIME-type
string, since it exists precisely to wrap arbitrary foreign formats.

#### Preamble layout (first 8 bytes of the payload)

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 4 | Sub-format FourCC | Four ASCII characters, space-padded (e.g. `'PNG '`) |
| 4 | 1 | Sub-format version | Version of the *payload encoding*, starting at `1` |
| 5 | 3 | Reserved | `0x000000` |

The preamble is part of the payload: it is compressed and encrypted together
with the content it describes (a codec identifier is only needed when the
content is actually being decoded, so hiding it inside an encrypted section
is correct — it leaks nothing about protected content).

A reader that does not support a section's sub-format must treat the section
like an unknown section type: skip it, and report the content as
unavailable rather than failing the whole file.

#### Sub-format registry

| Section | FourCC | Encoding |
|---|---|---|
| `VECT` | `UCVS` | Native serialized `VectorStorage::VectorDocument` (default) |
| `VECT` | `SVG ` | Embedded SVG document (pass-through, UTF-8 text) |
| `BMAP` | `UCRW` | Native raw pixels: uint16 width, uint16 height, uint8 pixel format (`0` = RGBA8, `1` = RGB8, `2` = GRAY8, `3` = RGBA16), 3 reserved bytes, then rows top-down, unpadded |
| `BMAP` | `PNG ` / `JPEG` / `HEIC` / `WEBP` / `AVIF` | Embedded image file (pass-through) |
| `VIDS` | `WEBM` / `MP4 ` | Embedded container file (pass-through) |
| `VIDS` | `AV1 ` / `H264` / `H265` / `VP9 ` | Elementary stream (timing/index data to be defined with the UCVideo payload spec) |
| `AUDS` | `OPUS` / `FLAC` / `MP3 ` / `AAC ` / `VORB` | Embedded/elementary audio (pass-through) |
| `AUDS` | `PCM ` | Raw audio: uint32 sample rate, uint8 channels, uint8 bits per sample (16/24/32, little-endian signed), 2 reserved bytes, then interleaved samples |
| `SC3D` | `GLTF` / `GLB ` | Embedded glTF (JSON / binary) scene (pass-through) |
| `SC3D` | `OBJ ` / `STL ` | Embedded mesh file (pass-through) |
| `SC3D` | `UC3S` | Native UltraCanvas 3D scene serialization (to be defined) |

Rules:

- FourCCs are case-sensitive ASCII; codes consisting only of uppercase
  letters, digits and spaces are reserved for this specification. Vendor
  experiments must use a lowercase first character.
- Pass-through sub-formats embed a complete file of the named format —
  decoding is delegated to the corresponding UltraCanvas plugin (the same
  loaders used for standalone files), so sub-format support automatically
  tracks plugin support.
- Already-compressed sub-formats (everything except `UCRW`, `PCM `, `UCVS`,
  `SVG `, `GLTF`, `OBJ `, `STL `) should be stored with section
  compression = none.
- Every future content-section definition (e.g. the full `UCVideo` payload
  spec) must keep this preamble as its first 8 bytes; new FourCCs are added
  to this registry rather than inventing per-section mechanisms.

### 4.6 Localization

Localized text is **never embedded literally** in window, page or form
definitions. Components reference strings by ID, and the strings live in
per-locale string tables (`LOCL` sections). This is what allows one layout
to serve every language, translations to ship separately, and the CSS
layout engine to reflow per locale (text lengths and RTL direction differ).

#### String references

Any localizable text property may hold either a literal (non-localized
documents stay simple) or a string reference of the form `@str.<id>`, where
`<id>` is a dotted ASCII identifier, e.g. `@str.mainwindow.title`.
Placeholders use `{0}`, `{1}`, … positional syntax; plural-form handling is
an application concern, not a format feature.

#### `LOCL` payload layout

One section per locale. Strings are UTF-8; the section is compressed like
any other (string tables compress extremely well).

| Field | Type | Meaning |
|---|---|---|
| Record version | uint8 | `1` |
| Flags | uint8 | bit 0 = RTL locale; bits 1–7 reserved |
| Locale tag length | uint8 | |
| Locale tag | ASCII | BCP-47, e.g. `en-GB`, `de-DE`, `ar` |
| String count | uint32 | |
| Entries × n | | uint16 ID length + ID, uint32 value length + UTF-8 value |
| Media override count | uint32 | may be `0` |
| Overrides × n | | uint16 ID length + resource ID, uint16 length + replacement `MEDI` resource ID — for locale-specific images and similar assets |

#### Fallback and resolution

- The `META` section names the document's **default locale**; its `LOCL`
  table must be complete (every referenced string ID present). Other
  locales may be partial.
- Lookup order for a requested locale: external overlay `LOCL` (see below)
  → embedded `LOCL` for the locale → embedded `LOCL` for the default
  locale. A missing ID after fallback is a validation error
  (`UCDocumentUtils::ValidateDocument` should check that the default table
  covers every `@str.` reference in the file).
- A runtime loads only the `LOCL` section(s) it needs, located via `INDX`;
  unused languages cost nothing at load time.

#### External translation overlays (language packs)

The same mechanism ships translations separately: an **overlay file** is an
ordinary UCD file whose body contains only `META` and `LOCL` sections. Its
`META` carries two binding properties:

- `TranslatesFileUUID` — the file UUID of the base document (every UCD file
  gets a random UUID in `META` at creation);
- `TranslatesFileModified` — the base file's modification timestamp at the
  time the translation was made.

A loader that is handed a base file plus overlay files checks the UUID
(wrong file → overlay ignored) and compares the timestamp (older than the
base file's current modification date → overlay is flagged as possibly
stale, at the application's discretion). Overlay tables take precedence
over embedded ones in the lookup order above.

Embedded tables are the **default and recommended** distribution form —
single file, atomic, translations can never desynchronize from the layout.
Overlays exist for the language-pack workflow (adding or fixing a
translation without re-shipping the document).

## 5. Binary vs. text body encoding

Header byte 14 selects the encoding of *all* structured section payloads:

- **`0` — binary (default).** Compact tag-length-value serialization of the
  document model. This is the production format.
- **`1` — XML / `2` — JSON (debug).** Identical structure serialized as text,
  intended for debugging, diffing and hand-inspection. Typically combined
  with compression = none so the sections are directly readable in an editor
  after the 28-byte header. Not intended for interchange; writers should
  always produce binary for end users.

The section layer (headers, index, thumbnail, encryption) is identical in
both modes — only the payload bytes differ.

## 6. Reader/writer requirements

- Writers set every reserved field to zero; readers ignore reserved fields.
- Readers must validate all 12 signature bytes (guard byte, descriptor,
  `\r\n`, `0x1A`) before trusting anything else.
- Readers encountering a higher *minor* version must read the file, skipping
  unknown sections/flags. A higher *major* version is not readable.
- Writers must keep the descriptor consistent with the dominant content so
  filers pick the right icon/preview strategy.

## 7. Changes from v1 (`UCD\x01`)

| Aspect | v1 | v2 |
|---|---|---|
| Signature | `UCD\x01` (4 bytes, not self-describing) | 12-byte PNG-style ASCII signature with per-type descriptor |
| Thumbnail | none | raw HEIC/PNG straight after header, length in fixed header |
| Body | single XML blob, compressed then encrypted as a whole | independent length-prefixed sections, compressed/encrypted per section |
| Random access | none (full decompress required) | per section via `INDX` |
| Encoding | XML text only | binary default, XML/JSON debug modes |
| Vector graphics | not representable | first-class `VECT` section (VectorStorage model) |
| Styles | flat per-component properties | shared `STYL` stylesheet section |
| Privacy | n/a | `ENCRYPTED` + `PRIVATE` flag bits |

File extension stays `.ucd`. v1 files are distinguished by their old 4-byte
signature; loaders should keep the v1 read path during a transition period.
