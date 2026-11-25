- **Office templates are read and written for documents and spreadsheets,
  as they already were for decks.** A template is its document's package
  under the template's media type, and the writers now write one by the
  extension:
  - `UCWordDocumentIO`: an .ott is an .odt with the
    `application/vnd.oasis.opendocument.text-template` mimetype (and
    manifest), a .dotx a .docx whose main part is
    `wordprocessingml.template.main+xml`; both read back as documents. A
    .dotm (macro-enabled) is read, its macros neither run nor kept, and
    refused on save. `IsWordTemplateExtension`.
  - `UltraCanvasSpreadsheet::LoadFromFile` / `SaveToFile`: .ots
    (`...spreadsheet-template`) and .xltx (`spreadsheetml.template.main+xml`,
    with a password too) written and read; .xltm read.
    `IsSpreadsheetTemplateExtension`.
  - The format inventory lists every template as a type of its own: .ott,
    .dotx, .dotm (read), .ots, .xltx, .xltm (read), and .potx / .potm and .otp,
    which leave the PowerPoint and OpenDocument presentation rows.
- **`UltraCanvasFileLoader`: templates.** `DetectOfficeDocumentKind` gives a
  template its kind; `IsOfficeTemplate(path)` says whether a file is one -
  by its extension, or by its package (an OpenDocument `-template` mimetype,
  an Office Open XML template content type) when the name says nothing - so
  an office suite opens it as a new, untitled document;
  `IsOfficeTemplateExtension` asks the name alone. `AddOfficeTemplateSaveTypes
  (opts, kind)` is "Save as template"'s list (.ott and .dotx, .ots and .xltx,
  .potx and .otp), and `AddSpreadsheetSaveTypes(opts)` a workbook's Save
  list. `AddTextDocumentSaveTypes` and `AddPresentationSaveTypes` no longer
  offer the templates, which the template list does. Covered by
  `FileLoaderOfficeKindTest`, `WordFormatsTest` and the new
  `OfficeTemplatesTest`, which also makes UltraOffice's shipped templates
  (`media/templates/office/`, `--write`) and checks they load.
- **File previews: a workbook's first printed page.** The built-in providers
  draw the thumbnail of an .ods, .xlsx, .ots, .xltx or .xltm - the sheet on
  show cut into pages by its print settings, as its PDF is - so the Filer's
  tiles show it rather than the type's glyph (a workbook over 2 MB keeps the
  glyph); the templates .ott, .dotx and .dotm preview as their documents do.
