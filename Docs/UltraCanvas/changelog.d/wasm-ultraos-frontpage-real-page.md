- **WebAssembly: the front-page demo is the real ultraos.eu page.** The
  first version of `UltraCanvas/OS/WASM/demo/ultraos-frontpage.cpp` was
  reconstructed from search snippets; it now follows the published page,
  top to bottom: the header with the site's drop-down navigation as popup
  menus anchored under their buttons, the hero, the desktop and mobile
  cards, the eight reasons for ULTRA OS, the newsletter form with its
  privacy checkbox, the status with the three audiences, the two pre-order
  offers, the donation links, the social networks, the ULTRA OS variants
  with the picture alternating sides, the eight purpose-based editions, the
  FAQ as an accordion that opens and closes its answers, the contact form
  with a text area, and the footer with its columns and a second newsletter
  form. The page's own texts are tables at the top of the file. The site's
  photos are placeholder tiles except three the repository now carries in
  `media/images/ultraos/`: the ULTRA OS logo, the UltraCanvas logo and the
  chip photo, as PNG because the wasm sysroot decodes neither JPEG nor WebP.
  Checked as a desktop application under Xvfb at 1280 px wide; the browser
  build preloads the three pictures beside the fonts.
