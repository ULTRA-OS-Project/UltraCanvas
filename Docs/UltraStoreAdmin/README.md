# UltraStoreAdmin

The store's admin backend: the program the people who publish run. It keeps
the apps, runtimes and libraries with every setting the catalog carries,
the publishers, the files behind the packages and the signing key; it
translates the texts into every official language; and it generates the
catalog, signs it and lays it out for the web server - verified with the
store's own verifier before anything is called published. One program,
`ultrastoreadmin`: the window when started plainly, the commands when given
one.

This page is the design and the record of the decisions behind it. The
store itself is [`Docs/UltraStore/README.md`](../UltraStore/README.md).

## Decisions

| Question | Decision |
|---|---|
| Where do the apps and their settings live? | **In one UltraDatabase (SQLite) file, `admin.sqlite`**, each app or component kept whole as the JSON object the catalog writes (`Catalog.h`'s `SerializeComponent`), beside the columns the lists need. The catalog's component object *is* the settings record, so what the backend keeps is by construction what the store reads, and a setting the catalog grows is kept without a schema change. What is queried on its own - the files behind the packages, the keys, the settings - is a table. |
| Where do the packages live? | **In the package store under their hash**, `<store>/packages/<sha256>.<format>`: the admin names a build, the backend copies it, measures size and SHA-256 and suggests the entry from the archive's listing. Two releases that ship the same bytes share one file. |
| Where is the signing key? | **Generated on the publishing machine, its seed sealed in the database** with a passphrase: Argon2id derives the key, XChaCha20-Poly1305 seals the 32-byte seed with the cost and the salt in the authenticated header (UltraPassword's vault scheme, through UltraCrypt). The seed is in memory for the moment of signing. The public half is exported as a `.pub` file for `Apps/UltraStore/keys/`. A second machine means a second key, which the store trusts beside the first; a key is retired here and dropped from the store's next release. Every publish lists the active keys in the catalog's `trusted_keys` (schema 2), so a new key is announced by the first publish after `key new` and signs from the next one on, and the stores that saw that catalog accept it before the binary that carries it ships; a retired key drops out of the list. |
| How is the packages server reached? (the store's open question) | **By what the catalog says, with the paid packages behind the server.** The publish folder has `packages/` for the packages of free apps - plain files, fetched by URL, which is what a signed catalog needs and no more - and `restricted/` for the packages of paid apps. The web server serves `packages/` and `media/` to everyone and `restricted/` to nobody directly: a buyer's store gets a **short-lived signed URL** for a restricted file from the payment service (the payment milestone), the way the other stores do it (a CDN with signed URLs per user and download, tokens that expire). No bearer token in the binary: there is nothing to leak, and a leaked signed URL dies in minutes. The catalog itself, the media and the free packages are public and need no secret. |
| How do the files reach the server? | **The publish folder is the server's document root**, or is copied to it (rsync, SFTP, the host's deploy hook): the backend lays the folder out and stops there. An upload of its own (UltraNet SFTP or HTTPS PUT) is a later step once the host is chosen. |
| Translations | **UltraAI**, the `ITranslator` of the module, with the provider the settings name (`ai.provider`: `qwen`, `llama-cpp`, a configured endpoint; empty follows the routing policy, local first). The creator writes at least one language; `translate` fills every language of `texts.languages` (the official languages, by default) the component lacks, from its `source_language`, and lists them under `machine_translated`. A text a person wrote is never overwritten; a machine one is redone on `--redo`. |
| Which languages? | The EU's 24 official languages, the EFTA countries' and the UK's, then the other markets the apps ship to (Ukrainian, Turkish, Serbian, Russian, Arabic, Hebrew, Hindi, Indonesian, Japanese, Korean, Thai, Vietnamese, Chinese simplified and traditional, Brazilian Portuguese): `Translation.cpp`, narrowed or widened by the setting. |

## The pieces

```
Apps/UltraStoreAdmin/
  core/                          UltraStoreAdminCore (static), on UltraStoreCore, UltraDatabase, UltraCrypt, UltraAI
    AdminDatabase.h/.cpp         admin.sqlite: settings, publishers, components (whole), package files, keys, the sequence
    KeyVault.h/.cpp              the seed sealed with the passphrase; a new pair
    PackageStore.h/.cpp          a build into the package store under its hash; the archive's listing; the entry
    CatalogPublisher.h/.cpp      the catalog from the database, laid out, signed, verified; the publish folder
    Translation.h/.cpp           the official languages; TranslateComponent over an ITextTranslator; the UltraAI one
  cli/Commands.h/.cpp            ultrastoreadmin <command>: Cli::Run(args)
  ui/AdminWindow.h/.cpp          the window's shell and its store pages (Overview, Publish, Keys, Settings); the jobs
  ui/AdminComponentPage.cpp      the form of one app or component: every setting, the texts, the pictures, the releases
  ui/AdminTheme.h                the store window's theme, with Field (a labelled control)
  ui/main.cpp                    the window, or the commands; one copy per user
Tests/UltraStoreAdmin/           the core's tests (ctest: UltraStoreAdminCore)
Docs/UltraStoreAdmin/            this page, CHANGELOG.md, changelog.d/
```

The core opens no window, so the command line, the tests and a headless CI
leg use it as it is; the window is a client of the same core, so what it
does and what a command does never differ.

## The window

`ultrastoreadmin` alone, or `ultrastoreadmin --store <dir>` alone, opens
the window on the store (the default store is
`<data folder>/UltraStoreAdmin`, created on the first start). A sidebar
(`UltraCanvasSidebarNavigation`) lists the store's pages and then every
app and every runtime or library the database has, an item marked
*no release* until it has one; the page on the right is one of:

| Page | What it does |
|---|---|
| Overview | The store folder and database, the counts, the last publish; *Add an app or a component* (id, name, kind) creates the record and opens its form. |
| An app or component | The form: every setting of the table in the store's README - identity (id, kind, name, publisher, license, homepage, support, privacy, open source, source, categories, tags), the store settings (visibility, lifecycle and successor, age rating, account, monetary model with price, currency and trial days, free update period with its date), the texts per language (the source language, the language shown, *Add language*, *Translate the missing languages* through UltraAI, the machine ones marked), the pictures (the icon and the screenshots copied into `media/<id>/` by `ImportMedia`), the releases newest first with their packages, an inline editor for a new release and for a new package (the build's file into the package store, OS, processor, variants, minimum OS version, the entry suggested from the archive, public or restricted). Save writes the draft through `PutComponent`, which reads it back the way the store does; Revert reloads; Delete asks first. Leaving a form with unsaved edits asks. |
| Publish | The publish folder, the key, the passphrase, whether to copy the packages and media; *Check* builds the catalog and lists the warnings; *Publish* runs `Publish()` on a worker thread and reports the sequence, the counts and the warnings. |
| Keys | The keys with their public halves, *Export .pub…* and *Retire*; a new key from a name and a passphrase typed twice. |
| Settings | The store's settings (the URLs, the languages, the UltraAI provider) and the publishers. |

The slow operations (a key's Argon2id seal, a publish, a translation) run
on a worker thread and report through `PostToUIThread`; the window refuses
to close while one runs. The status line at the bottom shows the outcome
of the last action, red when it failed.

## On disk

| What | Where |
|---|---|
| the admin store: `admin.sqlite`, `packages/`, `media/<id>/<file>` | `UltraCanvasDataFolderRoot() / "UltraStoreAdmin"` (`~/.local/share/UltraStoreAdmin`, `~/Library/Application Support/UltraStoreAdmin`, `%APPDATA%\UltraStoreAdmin`), or `--store <dir>` |
| the publish folder | wherever `publish <outdir>` says: `catalog.json`, `catalog.json.sig`, `media/`, `packages/`, `restricted/` |

The media paths in the catalog (`media/<id>/<file>`) are relative to the
catalog's URL, as the store expects, and to the admin store folder on this
side: `app media <id> icon <file>` copies the file there and records the
relative path, `publish` copies it into the publish folder under the same
path.

The settings in `admin.sqlite`:

| key | |
|---|---|
| `catalog.base_url` | where `catalog.json` will be served: `https://store.example/v1` |
| `packages.base_url` | where `packages/` and `restricted/` are served; the catalog's URL when unset |
| `texts.languages` | the languages `translate` fills, codes separated by spaces; set by `init` to the official list |
| `ai.provider`, `ai.base_url`, `ai.model` | the UltraAI translator's provider, server and model |
| `catalog.sequence`, `catalog.published_at` | the last publish; set by `publish`, never by hand |

## The command line

```
ultrastoreadmin [--store <dir>] <command>

init                                                     the database and the folders
settings [set <key> <value>]
key new <name> [--passphrase <p>] | list | export <id> <file.pub> | retire <id>
publisher add <id> <name> [<url>] | list | remove <id>
app import <component.json>                              an app or component, the catalog's object
app new <id> <name> [--kind app|runtime|library] [--publisher <id>]
app set <id> <field> <value>                             name publisher license homepage support_url privacy_url
                                                         source_url icon source_language categories tags screenshots
                                                         visibility lifecycle successor age_rating requires_account
                                                         open_source pricing.model price currency trial_days
                                                         updates.period updates.until
app text <id> summary|description <lang> <text or @file>
app media <id> icon|screenshot <file>
app show <id> | list | export <id> <file> | remove <id>
release add <id> <version> [--channel beta] [--date <d>] [--depends <id>@<version> ...] [--notes <lang> <text>]
release remove <id> <version>
package add <id> <version> <file> --os <os> --arch <arch> [--variant <v> ...] [--min-os <v>] [--entry <path>] [--access public|restricted]
package entries <file> [--os <os>]                       the listing and the suggested entry
translate [<id>] [--languages <l> ...] [--redo]
build [-o <catalog.json>]                                the catalog, unsigned, for a look
publish <outdir> --key <id> [--passphrase <p>] [--no-copy]
```

A passphrase comes from `--passphrase`, else `ULTRASTOREADMIN_PASSPHRASE`,
else a line on standard input. `package add` takes the access from the
app's pricing (`restricted` for a paid app) unless `--access` says.

A first catalog, end to end:

```
ultrastoreadmin init
ultrastoreadmin settings set catalog.base_url https://store.example/v1
ultrastoreadmin key new production                       # asks for the passphrase
ultrastoreadmin key export 1 Apps/UltraStore/keys/production.pub
ultrastoreadmin publisher add ultra-os-project "ULTRA OS Project" https://ultraos.eu
ultrastoreadmin app new org.ultraos.hello Hello --publisher ultra-os-project
ultrastoreadmin app text org.ultraos.hello summary en "Says hello"
ultrastoreadmin app text org.ultraos.hello description en @hello.md
ultrastoreadmin app media org.ultraos.hello icon hello.png
ultrastoreadmin release add org.ultraos.hello 1.0.0 --date 2026-10-27 --depends org.ultraos.runtime@0.9.410
ultrastoreadmin package add org.ultraos.hello 1.0.0 hello-1.0.0-linux-x86_64.zip --os linux --arch x86_64 --variant glibc-2.31
ultrastoreadmin translate
ultrastoreadmin publish /srv/store/v1 --key 1
```

`publish` builds the catalog with the next sequence, copies the packages
and the media, signs the exact bytes, reads and verifies them back the way
the store does, and only then records the sequence. The store fetches
`https://store.example/v1/catalog.json`, trusts `production.pub`, and lists
Hello.

## What publish checks

- The catalog parses with the store's `ParseCatalog`: every id is reverse
  DNS, every channel stable or beta, every package has an absolute URL.
- A package with no file in the package store and no URL of its own is
  left out with a warning; a release left without a package, and a
  component left without a release, likewise. An app without a summary, a
  publisher the database does not know, a missing picture: warnings.
- The signature verifies with the key's public half; a retired key does not
  sign; a wrong passphrase stops before anything is written.
- The catalog is schema 2 and announces every active key (`trusted_keys`):
  to rotate, `key new`, publish once with the old key, then with the new
  one; `key retire` the old one afterwards.
- The sequence is recorded only after the files are in place, so a failed
  publish leaves the next one at the same number.

## Milestones

1. **0.1.0** core and command line (this page).
2. **The window** (*The window* above): the apps on the left, the form with
   every setting on the right, the releases and packages, publish with the
   warnings, the keys - on `UltraStoreAdminCore`, so the window and the
   commands never disagree.
3. **Upload** to the host (UltraNet SFTP or HTTPS PUT), once the host is
   chosen.
4. **Payment**, with the signed URLs for `restricted/` (the store's milestone 6).
