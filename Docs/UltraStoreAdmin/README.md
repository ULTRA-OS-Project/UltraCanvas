# UltraStoreAdmin

The store's admin backend: the program the people who publish run. It keeps
the apps, runtimes and libraries with every setting the catalog carries,
the publishers, the files behind the packages and the signing key; it
translates the texts into every official language; and it generates the
catalog, signs it and lays it out for the web server - verified with the
store's own verifier before anything is called published. One program,
`ultrastoreadmin`: the commands now, the window next.

This page is the design and the record of the decisions behind it. The
store itself is [`Docs/UltraStore/README.md`](../UltraStore/README.md).

## Decisions

| Question | Decision |
|---|---|
| Where do the apps and their settings live? | **In one UltraDatabase (SQLite) file, `admin.sqlite`**, each app or component kept whole as the JSON object the catalog writes (`Catalog.h`'s `SerializeComponent`), beside the columns the lists need. The catalog's component object *is* the settings record, so what the backend keeps is by construction what the store reads, and a setting the catalog grows is kept without a schema change. What is queried on its own - the files behind the packages, the keys, the settings - is a table. |
| Where do the packages live? | **In the package store under their hash**, `<store>/packages/<sha256>.<format>`: the admin names a build, the backend copies it, measures size and SHA-256 and suggests the entry from the archive's listing. Two releases that ship the same bytes share one file. |
| Where is the signing key? | **Generated on the publishing machine, its seed sealed in the database** with a passphrase: Argon2id derives the key, XChaCha20-Poly1305 seals the 32-byte seed with the cost and the salt in the authenticated header (UltraPassword's vault scheme, through UltraCrypt). The seed is in memory for the moment of signing. The public half is exported as a `.pub` file for `Apps/UltraStore/keys/`. A second machine means a second key, which the store trusts beside the first; a key is retired here and dropped from the store's next release. |
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
  cli/main.cpp                   ultrastoreadmin <command>
Tests/UltraStoreAdmin/           the core's tests (ctest: UltraStoreAdminCore)
Docs/UltraStoreAdmin/            this page, CHANGELOG.md, changelog.d/
```

The core opens no window, so the command line, the tests and a headless CI
leg use it as it is.

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
- The sequence is recorded only after the files are in place, so a failed
  publish leaves the next one at the same number.

## Milestones

1. **0.1.0** core and command line (this page).
2. **The window:** the list of apps on the left, the settings form on the
   right (every field of the table in the store's README), the releases and
   packages, a publish button with the warnings, a keys dialog - on
   `UltraStoreAdminCore`, so the window and the commands never disagree.
3. **Upload** to the host (UltraNet SFTP or HTTPS PUT), once the host is
   chosen.
4. **Payment**, with the signed URLs for `restricted/` (the store's milestone 6).
