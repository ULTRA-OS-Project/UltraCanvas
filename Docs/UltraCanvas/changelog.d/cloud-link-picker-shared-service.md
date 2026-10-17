- **The cloud link picker can hold a shared `CloudService` too.** Its
  listings, uploads and share links run on detached threads, and its *Add
  account…* opens the add-account dialog, whose browser sign-in can wait
  minutes - but the picker only kept a raw `CloudService*`, so an upload
  still running when the compose window or the application closed went on
  using a freed service and freed stores. `ShowCloudLinkPicker` now has a
  `std::shared_ptr<CloudService>` form: the picker's threads hold the service
  until they end, and its *Add account…* uses the add-account dialog's
  shared form. The `CloudService&` form stays, with the caller keeping the
  service alive as before. UltraMail moves to the shared form (UltraMail
  changelog).
- **`CloudService::TransfersInProgress`** lists the uploads and downloads a
  service is running, on any thread - each with its account, local file and
  remote path. `Upload`, `Download` and `UploadAndShare` count from the
  moment they start until they return. A host asks before it quits: a
  transfer ends with the process and the file does not arrive. UltraMail uses
  it to ask *An upload is still running — quit anyway?* when its last window
  closes during one (UltraMail changelog). `UltraCloudTests`: an upload held
  inside a fake server is listed, and the list empties when it returns.
