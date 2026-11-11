- **UltraStore is translated into the suite's 29 languages.** Every text the
  window shows goes through `Tr` now - the navigation, the filters, the
  buttons and badges, the facts and the information column, the download
  queue, the prices ("4.90 EUR / month", "(30-day trial)"), the code
  messages and the sizes - and `media/locale/<lang>/UltraStore.json` carries
  the store's own texts for ar, ca, cs, da, de, el, es, fi, fr, he, hi, hu,
  id, it, ja, ko, nb, nl, pl, pt, ro, ru, sv, th, tr, uk, vi, zh-Hans and
  zh-Hant (machine drafts, marked for review).
- **The core's messages are whole sentences in the person's language.** A
  refused install ("UltraVideo is a paid app: buy it first, then install"),
  a missing dependency, a catalog that does not verify, a failed download
  or a registry error was glued from English pieces; each is one `TrF`
  sentence with its names as placeholders, so a translation can order them
  as its grammar needs. The command line stays English (it runs before a
  language is loaded).
- **Dates and sizes follow the language.** A release date is the standard
  set's short month in the language's order ("22. Okt. 2026",
  "2026年10月22日") instead of "22 Oct 2026" everywhere; KB, MB and GB are the
  language's units ("12.3 Mo", "1.5 ГБ").
- **Fixed: Update all counted an update to buy by its English message.** An
  update outside a purchase is now told by the licence's terms, so the "to
  buy anew" count stays right in every language.
- **Fixed: a heading in another script.** The small upper-case headings
  (CATEGORY, PRICE, VERSION HISTORY) upper-cased ASCII letters only, so
  "Catégorie" came out "CATéGORIE"; they upper-case any letter now. A
  shortened summary or release note no longer cuts a character in two.
