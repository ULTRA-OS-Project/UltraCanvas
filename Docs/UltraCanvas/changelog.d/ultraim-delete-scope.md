- **UltraIM: a message deleted for oneself or for everyone, by each
  messenger's own rules.** `UltraIM_DeleteMessage` takes a scope
  (`UltraIMDeleteScope::ForEveryone`, as before, or `ForMe`), and the new
  `UltraIM_GetDeleteChoices(provider, conversation, message)` says what is
  possible for one message now, so an application's menu offers exactly
  that instead of deciding it. A delete outside the rules answers
  `PermissionDenied`.
  - **ChatCube Direct can delete messages** (it could not at all; the
    `DeleteMessage` capability is new for it). Anyone deletes any message
    for themselves. For everyone, the sender deletes a text until the other
    person has read it, a picture, video or file at any time, and anything
    in a group at any time. The others are told with a signed `delete`
    body, over the link or through the relay's queue; the receiving side
    checks the same rules itself, so a text already read there stays. The
    file a deleted message brought into the cache is removed with it -
    never the sender's own file - and what was deleted before it had fully
    arrived is dropped when the rest comes (store schema 4).
  - **Matrix** deletes one's own messages for everyone, as before, and
    answers `CapabilityMissing` to "for me", which it does not have.
  - **Telegram** now deletes for oneself too; until now every delete was
    for everyone.
- **UltraIM plug-in ABI 5.** `DeleteMessage` gained the scope and the
  interface `GetDeleteChoices`, so `ULTRAIM_PLUGIN_HOST_ABI_VERSION` is 5.
