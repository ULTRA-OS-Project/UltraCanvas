- **UltraIM: a group managed after it is made, and a conversation left.**
  `UltraIM_RenameGroup`, `UltraIM_AddGroupMembers` and
  `UltraIM_RemoveGroupMember` (capability `ManageGroups`), and
  `UltraIM_LeaveConversation` (`LeaveConversation`: out of a group, or a 1:1
  conversation deleted with its history on this device);
  `UltraIMConversation::ownerId` names a group's owner where the network has
  one. Provider ABI 4 (`ULTRAIM_PLUGIN_HOST_ABI_VERSION`): modules built for
  3 are skipped until rebuilt. ChatCube Direct does it through the relay
  (`PATCH /v1/groups/{id}` renames, ChatCube relay 1.3.0), Matrix through
  `m.room.name`, invite, kick, leave and forget; Telegram does not offer it
  yet.
- **UltraIM Matrix: a room renamed by someone else is renamed in the list.**
  A new `m.room.name` or alias arriving in a live sync changed the room
  without a `ConversationUpdate`, so the old title stayed until a restart.
- **UltraCanvasListView: `SetSelectsOnContextMenu(false)`.** A right press
  then hands `onContextMenu` the row under the pointer without selecting it,
  for a list where selecting does something of its own (a messenger's chat
  list opens the chat).
