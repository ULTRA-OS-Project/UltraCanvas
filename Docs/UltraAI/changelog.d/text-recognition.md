- **UltraAI reads the words in pictures for UltraCanvas applications.**
  `RegisterUltraAITextRecognition()` (`UltraAITextRecognition.h`, in
  `UltraAI_UI`) installs UltraAI in front of `UltraCanvasFileLoader`'s text
  recognizer: when a vision provider on the default route declares
  `VisionTask::Ocr` - the one chosen for `"visionanalyzer"`, then the ones on
  this computer, a cloud one only when chosen or allowed, never `mock` - it
  reads pictures and scanned PDF pages (a PNG, with the model that declares
  OCR; the OCR text, else the spans, else the document regions in reading
  order). Without one, or when it fails, the recognizer installed before it
  (the OCR plugin's Tesseract) reads the picture. The provider is chosen at
  every picture, so one set up while the application runs is used from the
  next. `FindOcrProvider` and `LastTextRecognitionProvider` say which.
  `UltraAI_UI` now links `UltraAI_Core`. No adapter but the mock implements
  `IVisionAnalyzer` yet, so the built-in OCR keeps reading until one does.
  `UltraAITextRecognitionTest` covers the choice with stand-in providers.
