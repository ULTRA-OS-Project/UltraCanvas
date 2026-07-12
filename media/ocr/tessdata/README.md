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

## How discovery works

`TesseractOCREngine::ResolveDataPath()` probes, in order:

1. `OCRConfig::dataPath` (explicit override)
2. the `TESSDATA_PREFIX` environment variable
3. `<Resources>/media/ocr/` and `<Resources>/media/ocr/tessdata/`
4. `<executable dir>/media/ocr/`, `<executable dir>/ocr/`, next to the exe

For each candidate it accepts either the directory itself or a `tessdata/`
subfolder underneath it. This is what makes a **portable Windows build**
work without setting any environment variables — previously the engine only
consulted `TESSDATA_PREFIX` and Tesseract's compiled-in default path, which
does not exist on a portable Windows install, producing the
*"OCR failed: Engine not initialised"* error.
