- **A tree view's last row can be scrolled fully into view.** The tree
  measured the room it has for rows as its whole height less the header band,
  although `Render` draws them in the content box - the element without its
  border and padding. A tree with the default 1 px border therefore counted
  two pixels it does not show: scrolled to the end, the last row sat a border's
  width under the content edge and no scrolling could bring it out, Page Up and
  Page Down moved one row more than a page really shows, and the floating "go to
  the selected entry" button hid itself while the row it pointed at was still
  partly cut off. All of them now measure what `Render` clips the rows to, so
  the scroll range reaches the last row and a page is the rows a page shows.
  A tree given padding gains the most, having had that much more phantom room.
