- **The preview pane shows a loading ring while a file opens.** Selecting a
  file showed the previous one until the new one had been read and decoded -
  for a large picture, PDF or video long enough to look like the wrong file
  was selected, with the window not answering meanwhile. The pane now shows
  a turning ring and the file's name at once and reads the file after that
  frame (the viewer's `OpenFileWhenShown`); selections made while one loads
  replace it instead of queuing behind it, so holding an arrow key through a
  folder decodes only the file it stops on. The pane is compared with the
  file on its way (`PreviewPanePath`), so a refresh during the load does not
  start it over.
