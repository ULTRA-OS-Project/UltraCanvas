- **`UltraCanvasFileLoader::GetImageTextRecognizer`**: the text recognizer
  installed now, so one installed after it keeps it as its fallback - UltraAI's
  (`RegisterUltraAITextRecognition`, UltraAI changelog) hands a picture to the
  OCR plugin's Tesseract when no AI provider reads text. FileLoader 1.12.0.
