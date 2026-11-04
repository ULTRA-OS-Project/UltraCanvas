- **check_locale_catalogues: the standard set must be translated in every
  language.** An entry of `media/locale/en/Standard.json` that a language
  lacks now fails the `locale-catalogues` CI gate (a regional variant such
  as `pt-BR` counts its parent's, as the runtime's fallback walk does): it
  would show in English to everyone using that language. 111 standard
  texts that the localisation editor, the text bar, the menu customiser
  and the media viewer had added untranslated are drafted in all thirty
  languages, so the gate starts clean.
