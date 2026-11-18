- **UltraSite: the swap navigation.** `SiteConfig::navigation =
  Navigation::Swap` gives a site the swap navigation. In browsers that do
  not prerender pages (Firefox, Safari), `swap.js` fetches the page behind
  a link and puts it in place of this one instead of loading it. Back,
  Forward and the scroll position work as with page loads; the new `<h1>`
  gets the focus and the title is announced. A failed fetch, a 404 or a
  page it cannot swap is loaded as before. The library's modules (toc,
  tabs, copy, search, the theme toggle) start again on every swapped page
  (the `us:page` event); an app's own module says it does with
  `AddScript(name, source, true)`.
- **UltraSite: the files it writes into a site begin with `uc-`.** The
  stylesheet (`assets/uc-site.<hash>.css`), the script modules
  (`assets/js/uc-core.<hash>.js`, `uc-search…`, `uc-toc…`) and the search
  index (`search/uc-en.<hash>.json`), so a host or a visitor can see they
  come from UltraCanvas. The next build of a site replaces the old names.
