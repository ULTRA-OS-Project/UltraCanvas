# Privacy declarations

The privacy declarations ULTRA OS Development GmbH publishes at
www.ultraos.eu, one per product or service, kept here next to the code they
describe so that a change to a data flow and the change to its declaration
land in the same pull request.

## One declaration per product or service

A single declaration for everything cannot be accurate: a desktop application
processes data on the user's device and the company sees none of it, a hosted
service processes it on the company's servers and the company is the
controller, and an app store listing asks for a policy URL that describes
that one app. So:

| Kind | What it covers | Controller | Example |
|---|---|---|---|
| Desktop application | What the app stores on the device, which servers it contacts and why, the settings that turn each contact off | The user, for the local processing; the operators of the contacted servers for theirs. The company is not a controller unless the app talks to a company server | `UltraMail.md` |
| Hosted service | The data the company's servers receive and keep, retention, processors (hosting, e-mail delivery), transfers outside the EU | ULTRA OS Development GmbH | a future ULTRA OS cloud or account service |
| App store listing | Points at the application's declaration; the store itself (Apple, Microsoft, Google, Flathub) has its own policy for the download and payment | the store, for the download | the store listing's privacy-policy URL |
| The website | Cookies, server logs, contact forms, embedded content, analytics if any | ULTRA OS Development GmbH | the site's own `Datenschutzerklärung`, not kept here |

Each application whose data flows differ gets its own file. Two applications
that contact nobody but the user's own servers may share one file only when
every statement in it is true of both.

## Files

| File | Product | Status |
|---|---|---|
| `UltraMail.md` | UltraMail, English | draft 1.0, needs the bracketed items and legal review |
| `UltraMail.de.md` | UltraMail, German (the version a German consumer can rely on) | draft 1.0, same |
| `Template.md` | The section skeleton for the next product | — |

The English and German versions of one product say the same thing; a change
goes into both in the same commit.

## Writing one

1. Start from `Template.md`.
2. Read the application's source, not only its documentation: list every
   host it connects to (`grep -rEo 'https?://[^"]+'` over the app, plus the
   protocol plug-ins it loads), every file it writes, and every setting that
   governs a connection. The declaration names each host, what the request
   carries (an e-mail address, a domain, only an IP address), who operates it,
   the purpose, the legal basis and the setting.
3. Say plainly what the application does *not* do (telemetry, accounts,
   update checks, advertising) when that is true; it is what readers look for.
4. Leave anything you cannot verify in square brackets rather than guessing,
   and keep the "draft for legal review" line until a lawyer has signed off.
5. Write the German version too for a product sold or offered in Germany.

## Keeping them true

A pull request that adds or changes a network connection, a stored file or a
privacy setting in an application updates that application's declaration in
the same change, and bumps its version and date at the top. The reviewer
checks both.

## Publishing

The website shows the current text; the repository keeps the history. Convert
with `pandoc Docs/Legal/Privacy/UltraMail.md -o UltraMail.html` or paste the
Markdown into the site's editor. Fill the bracketed items first; a published
declaration with `[…]` in it is worse than none.

Store listings (Microsoft Store, Mac App Store, Google Play, Flathub) and the
Google OAuth consent screen each ask for a privacy-policy URL: give them the
URL of the product's page on www.ultraos.eu, not the repository file, so the
link survives a rename here.
