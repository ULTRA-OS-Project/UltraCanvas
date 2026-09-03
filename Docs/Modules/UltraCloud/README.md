# UltraCloud

**Cloud storage accounts, providers and share links for every ULTRA OS app.**
Sibling of `UltraCanvas` (UI), `UltraNet` (networking), `UltraDatabase`
(storage), `UltraVault` (secrets) and `VirtualFS` (archives).

UltraCloud gives an app one call for "put this file in my cloud and give me a
link to it", and one place for the accounts behind that call. A cloud account
is set up once and every app — UltraMail attaching a link, UltraFiler sharing
from the context menu, UltraSocial hosting media — uses the same list and the
same default.

> **Status (2026-09, v0.1):** engine and dialogs implemented for
> **Nextcloud / ownCloud** (WebDAV + OCS share links), **generic WebDAV**
> (browse and upload; links only through a public web-folder URL) and an
> **in-memory demo provider**. Dropbox, OneDrive and Google Drive are the
> next providers (OAuth2 over UltraNet); iCloud Drive cannot mint links from
> an app at all. Sources under `UltraCloud/{include,core,providers,ui}`,
> targets `UltraCloud` (headless) and `UltraCloudUI` (dialogs), header
> `<UltraCloud/UltraCloud.h>`, `namespace UltraCloud`.

---

## Why a module, and why not VirtualFS

Copying a file into a synced OneDrive or Dropbox folder uploads it eventually,
but it never produces a link: a share link is minted by the provider's server
when asked, from a file id or a random token, and the local path carries no
mapping to it. Every provider therefore needs an **account** (credentials,
token refresh), an **API call** for the link, and a place to remember which
account is the default. None of that is an archive concern, which is what
VirtualFS owns. VirtualFS can later expose an account as a `cloud://` folder
*on top of* UltraCloud; the accounts and the links stay here.

What already existed and stays where it is:

- `UltraCanvas::GetCloudStorageFolders()` — discovery of the *local sync
  folders* (OneDrive, Google Drive, Dropbox, iCloud Drive) on this machine.
  UltraFiler's "Cloud Storage" places section. A good place to *browse for a
  file*; not an account and not a link.
- UltraNet's WebDAV plug-in (`IFileShareProtocolPlugin`) and HTTP client.
  UltraCloud's WebDAV provider speaks DAV directly over `UltraNet_HttpRequest`
  (PROPFIND, PUT, GET, MKCOL), so it needs no plug-in DSO on the path.

## How a link is generated, per provider

| Provider | Upload | Link comes from | Auth | Link shape |
|---|---|---|---|---|
| Nextcloud / ownCloud | WebDAV PUT under `/remote.php/dav/files/<user>/` | OCS Share API (`shareType=3`), with password, expiry, label | app password (Basic) | `https://cloud.example.com/s/<token>` |
| WebDAV server | WebDAV PUT | `publicBaseUrl` + path, when the DAV root is also served as a web folder | Basic | `https://files.example.org/pub/<path>` |
| Dropbox *(planned)* | `files/upload` | `sharing/create_shared_link_with_settings` | OAuth2 | `https://www.dropbox.com/scl/fi/…` |
| OneDrive *(planned)* | Graph upload session | Graph `createLink` | OAuth2 | `https://1drv.ms/…` |
| Google Drive *(planned)* | Drive `files.create` | `permissions.create` (anyone with the link) | OAuth2 | `https://drive.google.com/file/d/<id>/view` |
| iCloud Drive | — | Finder / share sheet only | — | not possible from an app |
| Demo (in memory) | in process | in process | — | `https://demo.ultra-os.local/s/<n>` |

## Architecture

```
  app (UltraMail, UltraFiler, UltraSocial)
        │  ShowAddAccountDialog / ShowCloudLinkPicker      UltraCloudUI
        ▼
  CloudService ──────────── AddAccount / List / Upload / CreateShareLink / UploadAndShare
    ├── AccountStore        the account list + default      (UltraDatabase, SQLite)
    ├── ISecretStore        passwords / tokens by accountId
    │     ├── VaultSecretStore   UltraVault ("cloud.<id>.password")
    │     └── FileSecretStore    obfuscated files (per-app fallback)
    └── provider registry   RegisterProvider / GetProvider / RegisterBuiltInProviders
          ├── NextcloudProvider : WebDavProvider   DAV + OCS share API
          ├── WebDavProvider                        DAV over UltraNet HTTP
          └── MemoryProvider                        in-process fake
```

Providers are stateless: every call receives the `Account` and the resolved
`Credentials`, so a provider never touches the stores. The HTTP call is a
seam (`HttpFn`), which is how the tests drive the Nextcloud provider through
a fake server.

## Quick examples

```cpp
#include <UltraCloud/UltraCloud.h>
using namespace UltraCloud;

// Once per process.
RegisterBuiltInProviders();
AccountStore accounts;  accounts.Open("myapp-cloud", dataDir + "/cloud.db");
FileSecretStore secrets(dataDir + "/cloud-vault");   // or VaultSecretStore
CloudService cloud(accounts, secrets);

// Set up an account (the first one becomes the default).
Account a;
a.providerId = "nextcloud";
a.serverUrl  = "https://cloud.example.com";
a.username   = "erika";
Credentials c; c.password = "app-password";
if (Result r = cloud.AddAccount(a, c, /*verify=*/true); !r) Report(r.message);

// Share a file through the default account: upload + link in one call.
Account def;  accounts.GetDefault(def);
ShareLink link;
ShareLinkOptions opts;  opts.expiresAt = now + 7 * 86400;
if (cloud.UploadAndShare(def.accountId, "/home/erika/Q3.pdf", "", opts, link))
    body += "\nQ3.pdf: " + link.url;

// Browse.
std::vector<Entry> entries;
cloud.List(def.accountId, "/Documents", entries);
```

The dialogs (`UltraCloudUI`):

```cpp
#include "UltraCloudAccountDialog.h"
#include "UltraCloudPickerDialog.h"

UltraCloud::ShowAddAccountDialog(window, cloud, [](const Account& added) { ... });
UltraCloud::ShowCloudLinkPicker(window, cloud, [](const CloudLinkPick& pick) {
    InsertIntoBody(pick.entry.name + ": " + pick.link.url);
});
```

The picker lists the accounts (default preselected), browses the chosen one,
uploads a local file into the current folder, and returns a share link for the
selected file. When no account exists yet it offers the add-account dialog.

## Public surface

| Header | Contents |
|---|---|
| `UltraCloudTypes.h` | `Result` / `ResultCode`, `Account`, `Credentials`, `Entry`, `ShareLinkOptions`, `ShareLink`, `ProviderCapabilities` |
| `UltraCloudProvider.h` | `ICloudProvider` (Verify, List, MakeDirectory, Upload, Download, CreateShareLink); `RegisterProvider`, `GetProvider`, `ListProviders`, `RegisterBuiltInProviders` |
| `UltraCloudAccounts.h` | `AccountStore` (Open, Upsert, Remove, Get, List, SetDefault, GetDefault), `MakeAccountId` |
| `UltraCloudSecrets.h` | `ISecretStore`, `FileSecretStore`, `VaultSecretStore` (with UltraVault) |
| `UltraCloudService.h` | `CloudService` (AddAccount, RemoveAccount, List, Upload, CreateShareLink, UploadAndShare) |
| `UltraCloudWebDav.h` | `WebDavProvider`, `HttpFn`, and the helpers `EncodePath`, `JoinUrl`, `NormalizePath`, `ParseMultistatus`, `PublicFolderLink` |
| `UltraCloudNextcloud.h` | `NextcloudProvider`, `NextcloudDavUrl`, `BuildOcsShareForm`, `ParseOcsShareResponse` |
| `UltraCloudMemory.h` | `MemoryProvider` (+ `Seed` / `Clear`) |

## Building and testing

```sh
cmake -S . -B build -DBUILD_ULTRACLOUD=ON -DULTRACANVAS_BUILD_ULTRACLOUD_TESTS=ON
cmake --build build --target UltraCloudTests
ctest --test-dir build -R UltraCloud --output-on-failure
```

The suite is headless: the account store runs on an in-memory SQLite
database, the secret store on a temp directory, and the providers against a
fake `HttpFn` that records the requests (PROPFIND with `Depth: 1`, MKCOL,
PUT, the OCS POST) and answers with canned bodies.

## Roadmap

1. **Dropbox** (OAuth2 PKCE through `UltraNet_OAuth2`, `/2/files/upload`,
   `/2/sharing/create_shared_link_with_settings`).
2. **OneDrive** (Graph) and **Google Drive** (Drive v3).
3. **System-wide account list**: one store under the user's config dir
   shared by all apps (today each app opens its own `cloud.db`), secrets in
   UltraVault once the platform-native backends land.
4. **VirtualFS provider** `cloud://<account>/…` so any app that walks
   VirtualFS can walk a cloud account.
5. **Sync-folder shortcut**: offer `GetCloudStorageFolders()` results as a
   place to pick a local file from, then upload + link through the matching
   account.
6. The ULTRA OS own storage service as one more provider, shipped as the
   default account.
