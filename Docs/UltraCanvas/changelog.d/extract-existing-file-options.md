- **Extracting into a folder that is there: replace when newer, older,
  bigger or different.** The file display's "folder name is already taken"
  question for an extract has the foldable **Options** panel the copy
  question has: what *Extract into existing* does with each file of the
  archive that exists already - replace always (as before), replace only when
  the archive's file is newer, older, bigger, or the files differ, or skip
  identical files and keep both for the rest ("name (2).ext"). The files it
  leaves as they were are listed when the extract is through. The context
  menu's Extract dialog now asks this question too when the folder named in
  it exists and one archive is extracted; it used to pick "name (2)" without
  asking, so there was no way to unpack into the folder that was there.
- **The extract question no longer comes back after its answer.** Keep both
  and Extract into existing started the extraction and then went on with the
  queue at once, found the same archive still first in it, and asked about it
  a second time; the second window stayed on the screen after the work was
  done.
- **VirtualFS: `VirtualFSExtractOptions::onExistingFile`.** `ExtractAll` asks
  the callback about each regular file that exists already at the
  destination - overwrite, skip, or write under another name - instead of
  always writing over it; `UCVFSBridge::ExtractArchive` takes it as its last
  argument.
