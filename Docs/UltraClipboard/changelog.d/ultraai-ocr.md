- **Extract text uses UltraAI when it can.** A picture is read by an UltraAI
  vision provider that reads text, when one is set up - a model on this
  computer, or a cloud service you chose in UltraAI - instead of the built-in
  OCR, which stays the fallback (no provider, offline, an error). The status
  bar then says which provider read it ("read by ..."). In a build with
  UltraAI's UI library (UltraAI changelog: `text-recognition`).
