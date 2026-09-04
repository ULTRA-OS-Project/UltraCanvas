# UltraCloud

**Cloud storage accounts, providers and share links for every ULTRA OS app.**
Sibling of `UltraCanvas` (UI), `UltraNet` (networking), `UltraDatabase`
(storage), `UltraVault` (secrets) and `VirtualFS` (archives).

UltraCloud gives an app one call for "put this file in my cloud and give me a
link to it", and one place for the accounts behind that call. A cloud account
is set up once and every app — UltraMail attaching a link, UltraFiler sharing
from the context menu, UltraSocial hosting media — uses the same list and the
same default.

> **Status (2026-09, v0.2):** engine and dialogs implemented for
> **Nextcloud / ownCloud** (WebDAV + OCS share links), **generic WebDAV**
> (browse and upload; links only through a public web-folder URL),
> **Dropbox**, **OneDrive** and **Google Drive** (OAuth2 + PKCE through the
> system browser, tokens refreshed automatically) and an **in-memory demo
> provider**. iCloud Drive cannot mint links from an app at all. Providers
> can also ship as plug-in libraries. Sources under
> `UltraCloud/{include,core,providers,ui}`, targets `UltraCloud` (headless)
> and `UltraCloudUI` (dialogs), header `<UltraCloud/UltraCloud.h>`,
> `namespace UltraCloud`.

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
| Dropbox | `files/upload` (single call, ≤ 150 MB) | `sharing/create_shared_link_with_settings` (password + expiry on paid plans); an existing link is reused | OAuth2 + PKCE, offline access | `https://www.dropbox.com/scl/fi/…` |
| OneDrive | Graph `PUT …:/content` up to 4 MB, an upload session in 10 MiB chunks above | Graph `createLink` (anonymous view; password + expiry on personal accounts) | OAuth2 + PKCE, `offline_access` | `https://1drv.ms/…` |
| Google Drive | multipart `files.create` (or a media update when the name exists) | `permissions.create` (anyone with the link) + the file's `webViewLink`; no password / expiry | OAuth2 + PKCE, `access_type=offline` | `https://drive.google.com/file/d/<id>/view` |
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
          │                 + LoadProviderPlugins() for provider DSOs
          ├── HttpProviderBase                      one HttpFn, Basic / Bearer auth, HTTP → Result
          │     ├── WebDavProvider                  DAV over UltraNet HTTP
          │     │     └── NextcloudProvider         + OCS share API
          │     └── OAuthProviderBase               sign-in + refresh over UltraNet OAuth2
          │           ├── DropboxProvider           Dropbox API v2
          │           ├── OneDriveProvider          Microsoft Graph
          │           └── GoogleDriveProvider       Drive API v3 (paths resolved to ids)
          └── MemoryProvider                        in-process fake
```

Providers are stateless: every call receives the `Account` and the resolved
`Credentials`, so a provider never touches the stores. The HTTP call is a
seam (`HttpFn`) and so are the OAuth authorization and refresh (`OAuthHooks`),
which is how the tests drive every provider through fake servers.

### OAuth2 sign-in

The hosted providers sign in through the system browser: `CloudService::
SignInAccount` asks the provider for its OAuth2 config, UltraNet runs the
PKCE authorization-code flow against a loopback redirect
(`UltraNet_OAuth2AuthorizeInteractive`), the tokens land in the secret
store, and the provider's account-info call fills the user name. An expired
access token is renewed from the refresh token inside `CloudService::Resolve`
before any call uses it, and the renewal is stored.

Each provider needs an **OAuth app registration** — a client id (and, for
confidential clients, a secret) the ULTRA OS project or the app vendor
registers with Dropbox, Microsoft and Google. It is configuration, never a
literal in the code:

```cpp
UltraCloud::OAuthApp app;
app.clientId    = "…";
app.redirectUri = "http://127.0.0.1:53682/callback";   // must match the registration
UltraCloud::SetOAuthApp("dropbox", app);
```

or, without code, the environment: `ULTRACLOUD_DROPBOX_CLIENT_ID`,
`ULTRACLOUD_ONEDRIVE_CLIENT_ID`, `ULTRACLOUD_GOOGLEDRIVE_CLIENT_ID` (plus
`_CLIENT_SECRET` and `_REDIRECT_URI` where needed). Until one is set the
add-account dialog says so and the provider's `SignIn` returns `Unsupported`.

Scopes requested: Dropbox `account_info.read files.metadata.read
files.content.read files.content.write sharing.read sharing.write`
(`token_access_type=offline`); OneDrive `Files.ReadWrite User.Read
offline_access`; Google `https://www.googleapis.com/auth/drive`
(`access_type=offline`, `prompt=consent`).

### Provider plug-ins

A cloud storage can also ship as a separate shared library, the way UltraNet
protocol plug-ins do. The library exports

```cpp
extern "C" void UltraCloud_PluginInit(const UltraCloud::UltraCloudPluginHost* host) {
    host->registerProvider(std::make_shared<MyCloudProvider>());
}
```

and `UltraCloud::LoadProviderPlugins()` dlopens every library in the plug-in
directory (`ULTRACLOUD_PLUGIN_DIR`, else `<executable dir>/plugins/ultracloud`)
and calls it. A plug-in derives from `HttpProviderBase` or `OAuthProviderBase`
like the built-in providers do.

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

For an OAuth provider the account is created with a browser sign-in instead
of a password:

```cpp
Account d;  d.providerId = "dropbox";
cloud.SignInAccount(d, [](const std::string& url) { UltraCanvas::OpenURL(url); });
// d.username / d.displayName / d.accountId are filled from the provider
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
selected file. When no account exists yet it offers the add-account dialog,
whose "Add account" button becomes "Sign in in browser" for the OAuth
providers.

## Public surface

| Header | Contents |
|---|---|
| `UltraCloudTypes.h` | `Result` / `ResultCode`, `Account`, `Credentials` (password or token + refresh token + expiry), `Entry`, `ShareLinkOptions`, `ShareLink`, `ProviderCapabilities` |
| `UltraCloudProvider.h` | `ICloudProvider` (Verify, List, MakeDirectory, Upload, Download, CreateShareLink, SignIn, RefreshCredentials, AccountInfo); `RegisterProvider`, `GetProvider`, `ListProviders`, `RegisterBuiltInProviders`; `UltraCloudPluginHost`, `LoadProviderPlugins`, `Get/SetPluginDirectory` |
| `UltraCloudHttp.h` | `HttpFn`, `HttpProviderBase` (auth from credentials, HTTP → Result) |
| `UltraCloudOAuth.h` | `OAuthApp`, `SetOAuthApp` / `GetOAuthApp` / `HasOAuthApp`, `OAuthHooks`, `OAuthProviderBase` |
| `UltraCloudDropbox.h` | `DropboxProvider`, `DropboxPath` |
| `UltraCloudOneDrive.h` | `OneDriveProvider`, `OneDriveItemUrl` |
| `UltraCloudGoogleDrive.h` | `GoogleDriveProvider` (+ `ResolveId`), `GoogleDriveChildQuery` |
| `UltraCloudAccounts.h` | `AccountStore` (Open, Upsert, Remove, Get, List, SetDefault, GetDefault), `MakeAccountId` |
| `UltraCloudSecrets.h` | `ISecretStore`, `FileSecretStore`, `VaultSecretStore` (with UltraVault) |
| `UltraCloudService.h` | `CloudService` (AddAccount, SignInAccount, RemoveAccount, List, Upload, CreateShareLink, UploadAndShare) |
| `UltraCloudWebDav.h` | `WebDavProvider` and the helpers `EncodePath`, `JoinUrl`, `NormalizePath`, `ParseMultistatus`, `PublicFolderLink` |
| `UltraCloudNextcloud.h` | `NextcloudProvider`, `NextcloudDavUrl`, `BuildOcsShareForm`, `ParseOcsShareResponse` |
| `UltraCloudMemory.h` | `MemoryProvider` (+ `Seed` / `Clear`) |

## Building and testing

```sh
cmake -S . -B build -DBUILD_ULTRACLOUD=ON -DULTRACANVAS_BUILD_ULTRACLOUD_TESTS=ON
cmake --build build --target UltraCloudTests
ctest --test-dir build -R UltraCloud --output-on-failure
```

The suite is headless: the account store runs on an in-memory SQLite
database, the secret store on a temp directory, the providers against a fake
`HttpFn` that records the requests (PROPFIND with `Depth: 1`, MKCOL, PUT, the
OCS POST, Dropbox's `Dropbox-API-Arg`, Graph's upload session and
`Content-Range` chunks, Drive's path queries and multipart create) and
answers with canned bodies, and the OAuth flow through `OAuthHooks`.

## Roadmap

1. **System-wide account list**: one store under the user's config dir
   shared by all apps (today each app opens its own `cloud.db`), secrets in
   UltraVault once the platform-native backends land.
2. **VirtualFS provider** `cloud://<account>/…` so any app that walks
   VirtualFS can walk a cloud account.
3. **Sync-folder shortcut**: offer `GetCloudStorageFolders()` results as a
   place to pick a local file from, then upload + link through the matching
   account.
4. **Large uploads everywhere**: Dropbox upload sessions above 150 MB and a
   resumable upload for Google Drive (today one request each).
5. The ULTRA OS own storage service as one more provider, shipped as the
   default account.
