# Third-party front-end libraries

The portal has no front-end build: these are the distribution files of two
libraries, copied as published and served with the portal's own stylesheet
and script (`../portal.css`, `../portal.js`). Nothing is fetched from another
host when a page loads.

| Library | Version | Files | Licence |
|---|---|---|---|
| [Bootstrap](https://getbootstrap.com/) | 5.3.9 | `bootstrap/bootstrap.min.css`, `bootstrap/bootstrap.bundle.min.js` (with Popper) | MIT (`bootstrap/LICENSE`) |
| [Dropzone](https://www.dropzone.dev/) | 6.3.5 | `dropzone/dropzone.min.js` | MIT (`dropzone/LICENSE`) |

The only change to the files is the last line taken off - the
`sourceMappingURL` comment, which names a `.map` file that is not copied here.

To update one: take the files from the package's `dist/` folder on npm
(`npm pack bootstrap@<version>`, `npm pack dropzone@<version>`), take the
`sourceMappingURL` line off again, and change the version in this table.
Dropzone's own stylesheet is not used: the upload areas are drawn with
Bootstrap's classes (`portal.css`, *Uploads*).
