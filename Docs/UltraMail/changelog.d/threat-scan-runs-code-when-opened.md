- **The threat scanner names the same attachments UltraMail refuses to
  open.** Whether an attachment is a program is now
  `FileAssociations::RunsCodeWhenOpened`, the question *Open* asks, instead of
  the scanner's own shorter, Windows-only list. Scripts (`.sh`, `.py`,
  `.ps1`), shortcuts (`.url`, `.desktop`), installers (`.pkg`, `.deb`,
  `.msix`), disk images (`.dmg`, `.vhd`) and macOS and Linux programs
  (`.app`, `.AppImage`) are now called programs too, and so is a name that
  hides its extension behind trailing dots or spaces (`setup.exe.`). As
  before, a program sent as a program is *attachment-executable*
  (suspicious) and one dressed as a document *attachment-disguised-executable*.
