- **The list of local models is downloaded, from where LocalAI gets its
  own.** The Local models page offered the nine models compiled into the
  framework; *Refresh catalogue* asked a repository that was never made
  (`UltraCanvas-AI-Engines`) and could only fail. The runtime now has
  **catalogue sources** (`LocalRuntimeSettings::catalogueSources`,
  `RefreshCatalogueSources`, `ListCatalogueSources`): each is an index at an
  address - a LocalAI model gallery, or one of our `catalogue.json`
  documents - and a new installation starts with LocalAI's own gallery
  (`github:mudler/LocalAI/gallery/index.yaml@master`). Of its 2327 entries
  on 2026-11-15 the two engines run 1614 - 1557 chat models, 36 embedding
  models, 21 whisper models - each with the SHA-256 of its files; the other
  713 need a backend UltraAI does not have and are counted, not offered.
  The files themselves come from Hugging Face, as LocalAI's do, and the
  engine a model needs is downloaded with it as before.
  - A LocalAI entry is a `llama-server` model when its files are GGUFs for
    `llama-cpp` (embeddings when it says so, the vision projector marked,
    every part of a split model, no draft model) and a `whisper-server`
    model when it is a whisper ggml `.bin`. The gallery's YAML is read by
    the runtime itself; it agrees with PyYAML on LocalAI's three index
    files and refuses the constructs it does not read.
  - A source is somebody else's list: its files must come over `https://`
    and land under plain names inside the model's folder, and a third
    party's `catalogue.json` may hand the engine a few arguments only. Its
    models are never chosen on their own - `Recommend()` and the topics
    keep to the curated catalogue - and an installed one stays listed and
    removable after its source drops it or is turned off.
  - A LocalAI gallery states no sizes: selecting a model asks its host for
    the size once (`ProbeModel`), so *Size* and *Fits* are known before
    anything is downloaded.
  - The default gallery address is gone. `RefreshGallery()` fetches a model
    server's index when the settings name one and does nothing otherwise;
    the Topics page says so instead of reporting a server it could not read.
- **Search Hugging Face.** *Add from Hugging Face...* asked for
  "organisation/repository:file.gguf" to be typed from memory. It is a
  dialog now: a search of huggingface.co's repositories, most downloaded
  first (`SearchHuggingFace`), and for the one selected its runnable files
  with quantisation, size and whether each fits this computer
  (`ListHuggingFaceFiles`) - Q4_K_M preselected when it fits. *Add* or *Add
  and install* registers the file, with the further parts of a split GGUF
  and the repository's vision projector when wanted
  (`AddHuggingFaceModel(HuggingFaceModelRequest)`); a file in a folder of
  the repository works, and a gated repository says that its download will
  be refused. Typing a repository's name or address lists its files at once.
- **The engine line shows the engine of the selected model.** It and the
  *Acceleration* choice were written for `llama-server` alone, so with a
  whisper model selected - even a running one - the page still read
  "Engine: llama-server", and *Install engine* installed that. Both follow
  the selection now ("Engine for Whisper Small: whisper-server v1.9.0 - CPU
  only, installed"), *Install engine* / *Remove engine* act on that engine,
  and the accelerator is kept per engine
  (`LocalRuntimeSettings::engineAccelerators`, `PreferredAccelerator`): the
  one setting for every engine made a Vulkan choice for llama-server pick
  whisper-server's optional Vulkan build too, which needs glibc 2.39. An
  engine with one build for this computer shows it with the choice
  disabled: there is nothing to choose, and it is installed with the first
  of its models.
- **The Local models page finds a model among sixteen hundred.** A search
  field (every word must be in the name, the id, a tag or the licence), a
  service filter and a source filter (recommended, each catalogue source,
  your own, installed) above the table; installed models first, then the
  recommended ones. *Sources* lists the catalogue sources with their model
  counts and dates, turns one off or on, removes one and adds one by its
  address. The result of an action has a line of its own - it was cut off
  in the corner beside the buttons - and names what each source returned.
  *Forget* is offered for the models the user added only; the buttons no
  longer end in "...".
- Checked against the live services on 2026-11-15: a whisper model and a
  GGUF chat model picked from LocalAI's gallery were installed from the
  page - engine first, the files' hashes as the gallery states them - and
  `llama-server` answered with the chat model; Hugging Face's search, file
  lists and a repository that does not exist (it answers 401, not 404)
  behave as the runtime expects.
- An installed model's `model.json` records the sizes the host reported,
  and `AddHuggingFaceModel` records the repository as the model's homepage
  and its licence when the caller knows it.
