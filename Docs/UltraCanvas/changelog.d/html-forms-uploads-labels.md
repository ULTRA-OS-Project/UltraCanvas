- **HTML reader: file uploads and label clicks in forms.**
  - **File inputs** were drawn for display only. Now one is a "Choose file…"
    button beside the names chosen.
    - The button asks the host through the new
      `BuildOptions::onChooseFiles` (several with `multiple`, the `accept`
      list as written); without it the button is disabled.
    - The files are read when the form is sent into the new
      `FormSubmission::files`: file name, a type from the extension, and the
      bytes. They go out as multipart file parts, and as their names outside
      multipart. With nothing chosen, one empty part is sent, as browsers do.
    - Over 64 MB together (`FormController::kMaxUploadBytes`), or
      unreadable, a file goes empty and is named in
      `FormSubmission::warnings`.
    - A choice survives a rebuild; reset clears it.
    - `UltraCanvasWebView::onChooseFiles` passes the request to the host,
      and UltraWeb answers with the file dialog, filtered by `accept`.
  - **Labels:** a click on a `<label>`'s text acts on its control - the one
    its `for="id"` names, else the first inside it (new
    `FormController::ActivateControl`).
    - A checkbox toggles, a radio is chosen through its group, a button is
      pressed, a file input opens its chooser, any other control takes the
      focus.
    - The text is a new *quiet* link (`LabelTextLink::quiet`), with no
      tooltip and no hover report.
  - Tests in `HTMLFormsTest` (file parts, choosing, reset, rebuild, label
    clicks) and `WebViewTest` (an upload end to end).
