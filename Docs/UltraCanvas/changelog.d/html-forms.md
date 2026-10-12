- **HTML reader: forms with real widgets (`BuildOptions::interactiveForms`).**
  A form's controls were drawn for display only, as text in a box. With the
  option on they are the framework's widgets, so the reader can fill a form
  in and send it (caret, selection, clipboard, focus).
  - **Widgets:**
    - text-like `<input>`s and `password` are `UltraCanvasTextInput`s;
    - `<textarea>` is an `UltraCanvasTextArea`;
    - checkboxes and radios are `UltraCanvasCheckbox` / `UltraCanvasRadio`, with one radio group per name in a form;
    - `<select>` is an `UltraCanvasDropdown`;
    - buttons are `UltraCanvasButton`s.
  - **Placement and size:** controls are `inline-block`, as in browsers (the
    user-agent style said `block`): on the line with their labels, or flex
    and grid items. They are sized by CSS, else by `size`, `cols` / `rows`
    and the font.
  - **Attributes:** `disabled`, a disabled `<fieldset>`, `readonly`,
    `maxlength`, `placeholder` and `form="id"` are honoured.
  - **`HTML::FormController`** (new `HTMLReader/HTMLForms.h`, one per
    `<form>`, in `BuildResult::forms`) builds HTML's entry list
    (`Collect`), submits (`Submit`, from a submit button or Enter in a
    field) to `BuildOptions::onFormSubmit`, resets, and saves / restores
    what was typed across a rebuild.
  - **`HTML::FormSubmission`** encodes the entry list urlencoded,
    multipart or as plain text.
  - **`PageLoader::LoadPost`** and `PageLoaderServices::post` load the page
    a form's POST answers with.
  - **`UltraCanvasWebView`** turns forms on (`SetInteractiveForms`) and
    sends them (`SubmitForm`): GET as a navigation with the fields as the
    query, POST through the loader. A rebuild of the page keeps what was
    typed and the focus.
  - Mail keeps the option off: its controls stay display-only, each on a
    line of its own.
  - Not yet: file uploads; a click on a `<label>`'s text does not reach its
    control.
  - New `HTMLFormsTest`; form round trips in `WebViewTest`.
