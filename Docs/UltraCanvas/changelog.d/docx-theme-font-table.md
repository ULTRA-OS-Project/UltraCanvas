- **A .docx saved again lost its theme and font table.** The DOCX writer
  builds its package from the document model, which held neither, so the
  colour scheme and heading and body fonts Word offers (`theme1.xml`) and
  the font table's families and alternatives (`fontTable.xml`) were gone
  after a save. The reader now keeps both in `UCRichDocument::keptParts`
  and the writer writes them back unchanged, named in `[Content_Types].xml`
  and the main part's relationships.
  - A kept part is never one that refers to other parts: a font table's
    embedded fonts (`w:embedRegular` and its kin) are taken out, and a
    theme with a picture is not kept. Neither is a part in the Strict
    namespaces.
  - Saved as OpenDocument, the parts are not written.

  Covered in `WordFormatsTest`.
