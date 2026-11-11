- **Categories from the store's fixed list.** The form offers the store's
  categories as chips to pick (Music and audio, Video, Graphics and photos,
  Office, ...) instead of free words typed into a field, and
  `PutComponent` and `app set <id> categories` refuse a word that is not on
  the list, naming the list - the store names each category in the buyer's
  language, which it cannot do for a word it does not know. A component
  saved before keeps loading; its old words are not offered and a save keeps
  only the list's.
