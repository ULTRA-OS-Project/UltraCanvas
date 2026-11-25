- **New from template.** The Start tab shows a gallery of templates - a
  card each, with a picture of its first page, its sheet or its first slide -
  and the File menu lists them under *New from template...*, with *Other
  template...* for one anywhere else. A template opens as a new, untitled
  document ("Document 3"), whether it is picked there, opened with *Open...*,
  from the recent list or from the command line: Save asks for a name, and
  the template itself is never written over.
  - Shipped: a letter, a report with headings and a memo; a budget, an
    invoice and a simple list; a plain and a dark presentation
    (`media/templates/office/`, made by the framework's own writers through
    `Tests/OfficeTemplatesTest.cpp --write`).
  - The user's own are in `UltraOffice/templates/` in the settings folder.
- **Save as template...** (File) writes the open document as a template of
  its kind - .dotx / .ott, .xltx / .ots, .potx / .otp, in the format new files
  are saved as - into the user's templates folder. The document keeps its own
  file, name and unsaved changes, and the gallery shows the template at once.
- The Start tab scrolls when the window is shorter than it.
- A spreadsheet's *Save as* no longer offers the template types or .xls,
  which it cannot write; the templates are *Save as template*'s.
