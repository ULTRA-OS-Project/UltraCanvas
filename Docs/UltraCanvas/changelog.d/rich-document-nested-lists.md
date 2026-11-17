- **`UCRichDocument::ToHTML` writes a nested list inside its parent item.**
  It wrote the inner `<ul>` beside the item it belonged to, directly inside
  the outer list, which HTML does not allow (browsers showed it, a
  validator and a screen reader's list count did not agree). Each item now
  stays open until the next item at its level or the end of its list, so
  the inner list sits in it: `<li>two<ul><li>nested</li></ul></li>`. A
  list that starts deeper gets an empty item to hold it. A flat list is
  written byte for byte as before.
