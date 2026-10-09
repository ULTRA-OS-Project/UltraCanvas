- **A list model that outlives its view no longer calls into it.**
  `UltraCanvasListView` puts its callbacks on the model it shows, and they
  pointed back at the view; nothing took them off when the view was
  destroyed, so a model the application kept and changed afterwards called
  into freed memory. They now hold a weak handle to the view and do nothing
  once it is gone. They are made harmless rather than removed from the model:
  a `UltraCanvasListSortFilterProxy` chained on after the view keeps a copy of
  them, and removing them would have cut the proxy off from its source.
  - `OnModelChanged()` (protected, virtual): called after the view has taken
    in each change its model signals. A subclass that keeps something derived
    from the rows overrides it instead of wrapping the model's callbacks,
    which would outlive it the same way - UltraMail's message list does so
    for its fitted Date column.
  - `ListViewScrollTest` destroys a view while its model and a proxy on it
    live on: the model's later changes reach the proxy and not the view
    (with the old callbacks the test aborts, "pure virtual method called").
