- **UltraLocalisation, phase L3: the drafts, the drafting tool, the
  Language page.** `media/locale/` now holds the standard set and the
  sixteen base sets in German, French, Spanish, Italian, Dutch,
  Portuguese, Japanese and Simplified Chinese - 1,546 entries each,
  machine drafts marked `"@translated-by": "machine draft ..., reviewed
  by -"` until a person reviews them - so the framework's own dialogs show
  in those languages on a desktop set to one. `ultralocalisation-draft`
  (`Apps/UltraLocalisationDraft`, built with the UltraAI module) drafts a
  catalogue through UltraAI's translator with any configured provider,
  keeps what a target already has, sends every plural form the language
  needs and refuses a draft that lost a placeholder;
  `scripts/translate_catalogue.py` runs it over a language's sets with a
  context per set. UOS-Settings gains a Language page (its 0.7.0): the
  language for every application, *System* or any with a standard set,
  written to `Language.conf`, and the translator's name for the catalogues
  the editor will save.
