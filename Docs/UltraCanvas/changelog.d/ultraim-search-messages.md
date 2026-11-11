- **UltraIM: searching the history, one call for every network.**
  `UltraIM_SearchMessages(provider, search, callback)` finds messages by
  their words, or everything of one kind - pictures, videos, pictures and
  videos, files, links (`UltraIMSearchKind`) - newest first, a page at a
  time with a cursor for the older ones (`UltraIMMessageSearch`,
  `UltraIMSearchPage`). A provider says it can with the new `SearchMessages`
  capability and answers through the new `SearchMessages` method of the
  provider interface, each from wherever its history lives:
  - **ChatCube Direct** queries its own database - one conversation or all
    of them. Which messages hold a link is noted in a new `links` table by
    the message's id alone; the address stays in the message's text and is
    read out of it when shown, so it is not stored twice. The table is
    filled as messages are stored, and once for the existing history when
    an older store is opened (schema 3).
  - **Matrix** reads a room's history back page by page and matches it on
    this device, so an encrypted room - which the homeserver's own search
    cannot read - is searched like any other.
  - **Telegram** asks Telegram's own search (`searchChatMessages`, with its
    filters), which covers the whole history without downloading it and
    matches whole words as Telegram's apps do.
- **UltraIM: the pieces of a search, for providers and lists.**
  `UltraIM_MessageMatchesSearch` is the one matcher the on-device providers
  share (every word, anywhere in the text or the attachment's file name, in
  any case - Latin, Greek and Cyrillic folded by `UltraIM_FoldCase`, never
  by the locale); `UltraIM_ExtractLinks` gives the links in a text;
  `UltraIM_SearchByFetching` answers `SearchMessages` for any provider from
  nothing but its `FetchMessages`.
- **UltraIM plug-in ABI 4.** The provider interface gained a method and
  `UltraIMTypes.h` its types, so `ULTRAIM_PLUGIN_HOST_ABI_VERSION` is 4: a
  provider module built for 3 is skipped until it is rebuilt.
