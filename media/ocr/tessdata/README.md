# Bundled Tesseract language data

The OCR plugin (`UltraCanvasOCRPlugin`, Tesseract backend) looks for its
`*.traineddata` language files here at runtime. This folder is copied into
the application bundle by the `copy_assets` build target, so whatever you
drop in here ships with the app.

## What to place here

At minimum, for the default English demo:

- `eng.traineddata`
- `osd.traineddata` (orientation / script detection — optional but recommended)

Download the *fast* tier (~2 MB for `eng`) from the upstream repo:

    https://github.com/tesseract-ocr/tessdata_fast/raw/main/eng.traineddata
    https://github.com/tesseract-ocr/tessdata_fast/raw/main/osd.traineddata

Additional languages use the same file naming (`deu.traineddata`,
`fra.traineddata`, …). Add them here to make them available offline.

> These binary files are intentionally **not** committed to the repository
> (they are large and licensed separately). CI / packaging steps are
> responsible for fetching them into this directory before creating a
> release archive.

## All languages, on demand

Only English ships in the bundle, but the OCR plugin exposes the **full**
Tesseract language catalogue (~130 languages) and fetches any other language's
data the first time it is used — you do not have to pre-bundle every pack.

* `UltraCanvasOCR::SupportedLanguages()` — the complete catalogue (code +
  English name), whether or not installed.
* `UltraCanvasOCR::InstalledLanguages()` — codes whose `*.traineddata` is
  present locally right now.
* `UltraCanvasOCR::EnsureLanguages({"deu","fra"}, err)` — seeds each pack from
  a local copy if one exists, otherwise downloads it, consolidates them into
  one directory and reconfigures the engine. Call this before recognising.
* `UltraCanvasOCR::DownloadLanguage("jpn", OCRDataTier::Fast, err)` — fetch a
  single pack explicitly.

Downloaded packs are cached in a writable per-user directory
(`UltraCanvasOCR::LanguageDataDir()`), so the download happens only once:

| Platform | Location |
|---|---|
| Linux   | `$XDG_DATA_HOME/UltraCanvas/ocr/tessdata` (or `~/.local/share/…`) |
| macOS   | `~/Library/Application Support/UltraCanvas/ocr/tessdata` |
| Windows | `%LOCALAPPDATA%\UltraCanvas\ocr\tessdata` |

Packs come from the upstream tessdata repositories, selected by `OCRDataTier`:
`Fast` → `tessdata_fast` (default), `Standard` → `tessdata`, `Best` →
`tessdata_best`. Downloading requires network support (UltraNet); on a build
without it, drop the `*.traineddata` file into the per-user directory (or this
folder) manually and it will be picked up.

## How discovery works

`TesseractOCREngine::ResolveDataPath()` probes, in order:

1. `OCRConfig::dataPath` (explicit override)
2. the per-user pack directory (`UltraCanvasOCR::LanguageDataDir()`), where
   on-demand downloads land — probed early so a freshly downloaded language
   wins over a stale bundle
3. the `TESSDATA_PREFIX` environment variable
4. `<Resources>/media/ocr/` and `<Resources>/media/ocr/tessdata/`
5. `<executable dir>/media/ocr/`, `<executable dir>/ocr/`, next to the exe

For each candidate it accepts either the directory itself or a `tessdata/`
subfolder underneath it. This is what makes a **portable Windows build**
work without setting any environment variables — previously the engine only
consulted `TESSDATA_PREFIX` and Tesseract's compiled-in default path, which
does not exist on a portable Windows install, producing the
*"OCR failed: Engine not initialised"* error.
