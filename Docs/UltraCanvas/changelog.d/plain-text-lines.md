- **HTML as text a person reads: `HTML::ExtractPlainText(html,
  PlainTextLayout::Lines)`.** `ExtractPlainText` put a whole page on one
  line, which suits a search index but not text someone reads or quotes, so
  callers that needed lines wrote their own tag stripper. The new layout
  parses the page and writes it the way a browser's `innerText` does,
  simplified: block elements on lines of their own, a blank line around
  paragraphs, headings, quotes and `<pre>`, `<br>` a line break, table cells
  a tab apart, list items as `- ` or `1. `, `<pre>` as written, a no-break
  space as a space - and what a browser does not show left out (`<head>`,
  `<script>`, `<style>`, the `hidden` attribute, an inline `display: none`
  such as a mail's preheader). The default stays one line, so existing
  callers are unchanged. UltraMail's own `HtmlToText` is replaced by it and
  leaves the html-reuse baseline.
