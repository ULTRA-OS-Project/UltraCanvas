- **Started with nothing, the program opens a start window instead of
  ending.** `ultrapress` with no argument, or with a folder that holds no
  site, used to print its usage into a console that closed before it could
  be read - from a double-click on Windows that looked like a crash. Now a
  window says why, offers the folder dialog onto an existing site, and a
  name and a folder for a new one (the starter site `ultrapress new`
  writes); the editor opens from there. The window is the framework's
  `UltraCanvasStartPage`, with UltraPress's words and its one field, laid
  out row by row by the page (`AddField`, `AddButton`, `AddNote`); the
  sites opened last are offered first as buttons (the five newest of the
  eight remembered, the whole path in a tooltip, a folder that holds no
  site any more said so and dropped). The window is built without a
  display too (`StartWindow::BuildPage`), and the render tests build it.
