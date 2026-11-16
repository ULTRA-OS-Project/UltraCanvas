- **A group's new name no longer flips back for a moment.** Renaming a group
  right after it was made could show its first name again until the next
  change arrived: the relay's notice of the group being made could reach
  ChatCube after the answer to the rename. With ChatCube relay 1.6.0 every
  change to a group is numbered, and ChatCube ignores a notice older than
  what it already shows - which also keeps a group you left from coming
  back. With an older relay ChatCube works as before. Framework changelog:
  ChatCube relay 1.6.0, group revisions.
