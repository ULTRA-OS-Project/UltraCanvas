- **The site's settings in the window.** *Site settings…* in the side
  column puts the site itself into the property pane: its name, tagline,
  address (the published site's root, kept without a trailing slash),
  language and owner, each written into the model as typed and the page
  re-rendered; selecting a page or a block returns to them. Until now the
  name came from the start window or `ultrapress new --name` and the
  address stayed `https://example.org` unless `site.json` was edited.
- **The address is checked as typed.** `IsSiteAddress` in the model says
  what a site's address has to be - `https://` or `http://`, a host with a
  dot in it or `localhost`, no spaces, no trailing slash - and the Site
  settings pane says under the field, in the error colour, when the typed
  one is not that yet; `ultrapress check` and the editor's validation
  report it too, so a slip is seen before the first publish writes it into
  every page's canonical link, the sitemap and the feed. `ultrapress
  import` says when an export's `<link>` is not an address, or names none,
  instead of writing it into every page unseen.
