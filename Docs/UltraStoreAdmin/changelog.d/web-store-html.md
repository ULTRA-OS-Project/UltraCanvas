- **The web store as HTML + JavaScript.** Publish lays the web store beside
  the catalog as complete, styled pages written through UltraSite
  (`core/WebStoreHtml.h`).
  - **Pages.** Discover, the categories with their free and paid lists,
    each category and each app, all after the store's design with a dark
    look.
  - **Languages.** The same pages are written in every language of the
    apps' texts. The words come from the store's own translations, and a
    language switcher joins the trees.
  - **Search and actions.** A search box, and *Install* / *Try* / *Buy* as
    `ultrastore://` links with the store's download beside them.
  - **Size.** Discover is about 11 KB compressed on a first visit, its
    pictures aside.
- **The new setting `web.output`** chooses the web store: `html`, `wasm`
  (the WebAssembly store from `web.site_dir`, as before) or `off`.
  - Unset, a store with `web.site_dir` keeps the WebAssembly store, and
    any other now gets the HTML one.
  - Nothing else in the publish folder is touched.
  - A missing picture or a page over its budget is a warning in the
    report, never a failed publish.
  - The report names the output (`webOutput`) and the languages
    (`webLanguages`).
