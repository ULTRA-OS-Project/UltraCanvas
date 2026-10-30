- **The media viewer shows an HTML page rendered, not its source.** An
  `.html`, `.htm`, `.xhtml`, `.xht` or `.shtml` file - and an `.xml` whose
  content is XHTML (an `<html>` root or an HTML DOCTYPE) - is now shown as a
  browser shows it, through `UltraCanvasWebView`, in UltraFiler's preview
  pane, its View window and every other host of `UltraCanvasMediaViewer`.
  Before, it opened in the read-only text view as highlighted markup, which
  is the Texter's job. The preview reads only local files: the page's style
  sheets and pictures come from the disk beside it, a link to another local
  page opens in place, and a web or `mailto:` link opens in the system's
  browser or mail client; forms send nothing and media shows its fallback.
  XML that is not a page (data, configuration, feeds) stays in the text view.
  `UltraCanvasMediaViewer::IsHtmlFile` says which files are pages; the info
  bar calls them `HTML`.
- **The media viewer shows an e-mail message (`.eml`) as the message.**
  Before, the viewer did not open an `.eml` at all, so UltraFiler's preview
  pane stayed empty. It now shows a header block (subject, From, To, Cc,
  Date - encoded words decoded - and the attachments by name and size)
  above the body: the HTML one rendered with its own styles, else the text.
  Pictures the message carries (`cid:`) are shown; remote pictures are never
  fetched, so a tracking pixel learns nothing. Read through UltraNet's MIME
  reader (`UltraCanvasMediaViewer::IsMailMessageFile`); a build without
  UltraNet shows the message's text.
- **Saved web pages (`.mht` / `.mhtml`) are shown as the page, Apple Mail
  messages (`.emlx`) as messages.** An MHTML archive - Chromium's, Edge's or
  Internet Explorer's *save as single file* - shows the page it saved: its
  links read against the address it was saved from, and its style sheets
  and pictures answered from the archive's own parts (by
  `Content-Location` or `cid:`), never from the network; a link to a place
  in the page scrolls there (`UltraCanvasMediaViewer::IsWebArchiveFile`).
  An `.emlx` is the message inside it, shown as an `.eml` is.
- **Outlook messages (`.msg`) are shown as messages, and a message's
  attachments open and save from the preview.** The new
  `UltraCanvasOutlookMessage` reads a `.msg` - subject, sender, recipients,
  date, body, attachments - and gets its HTML body back even when Outlook
  kept it only inside the compressed RTF (`UCDecompressRtf`, LZFu;
  `UCHtmlFromEncapsulatedRtf`, `\fromhtml1`). A `.msg` that is not a
  compound file stays text. In every message (`.eml`, `.emlx`, `.msg`) an
  attachment's name opens it - in a viewer window when the viewer shows its
  kind, else in the system's application - and *Save* writes it where the
  Save dialog says. A program or script (`.exe`, `.js`, `.lnk`, `.sh`, ...)
  is never opened from the preview; it is offered to be saved instead.
- **`FileAssociations::RunsCodeWhenOpened`:** whether opening a file of that
  name would run code - a program, script, installer, shortcut or disk image
  on Windows, macOS or Linux - by name alone, for the attachments and
  downloads that have no execute permission to look at. The media viewer's
  message attachments and UltraMail's attachments ask it.
- **`UCOutlookMessage` keeps the message's `messageId` and `inReplyTo`**, so a
  `.msg` built again as MIME (UltraMail's Import) keeps its identity and its
  place in a conversation.
- **UltraCanvasCompoundFile 1.2.0: streams and storages below the root**
  (`ReadStreamAt`, `HasStreamAt`, `StreamNamesAt`, `StorageNamesAt`), for the
  `.msg` reader's recipients and attachments.
- **`DecodeWindowsCodePage` (UltraCanvasTextUtils):** the `.xls` reader's
  Windows code-page tables (1250, 1251, 1252), moved to be shared with the
  `.msg` reader, and UTF-8 (65001).
- **UltraCanvasWebView 1.6.0: `SetOpensWebLinks`, `ScrollBy`, `Clear`.** A
  viewer of local files can send `http` / `https` links to `onExternalLink`
  instead of following them, scroll the page from its own keys, and let a
  page, its players and the history go when it moves on.
