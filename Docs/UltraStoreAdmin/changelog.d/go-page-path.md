- **A code's QR link opened nothing.** The QR code and `code link` give
  `<catalog base>/go/<CODE>`, but `publish` wrote the landing page as
  `go/<CODE>.html`, which a static web server does not serve for that URL
  without a rewrite rule. The page is now `go/<CODE>/index.html`, which
  every web server serves for `go/<CODE>` as it is.
