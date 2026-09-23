# UltraCalendar — a stand-alone calendar for ULTRA OS (Investigation and Proposal)

**Date:** 2026-09-20
**Status:** Proposal — investigation only, no implementation yet
**Scope:** a new application, `Apps/UltraCalendar`, that is *not* part of
UltraMail; a headless sibling module, `UltraCalendar`, that the application
and other ULTRA OS programs share; the option to keep using whatever calendar
service the user already has *or* to move to an ULTRA OS-hosted one; and the
migration between the two, in both directions.

This document is written in the style of the other investigations in this
directory — [`UltraFIBUDesignProposal.md`](UltraFIBUDesignProposal.md) and
[`UltraMessageDesignProposal.md`](UltraMessageDesignProposal.md) are the closest
siblings in shape, because both ask *which parts belong in the framework and
which in the app*. Everything said below about the repository was verified
against the tree on this date; file references are to the current `main`.
Everything said about third-party services and protocols is from their
published documentation as known to the author; the few places where a detail
could not be checked from this environment are marked **[unverified]** rather
than guessed.

---

## 1. Recommendation

**Build the calendar as its own application, `Apps/UltraCalendar`, on top of a
new headless module, `UltraCalendar`, that is a sibling of UltraCloud and
UltraMail's engine. Speak CalDAV as the one calendar protocol, so that the
ULTRA OS cloud, a self-hosted Nextcloud, iCloud, Google, Fastmail and the
German mail providers are all *one* provider implementation; add Microsoft as
the single non-CalDAV provider (Graph API). Let the user choose on first run
between "use my existing calendar", "use the ULTRA OS cloud" and "keep it on
this computer", and give them a migration wizard — in both directions — that
copies calendars rather than bridging them.**

Why these are the right calls, in one line each; the rest of the document is
the evidence:

| Decision | Reason |
|---|---|
| A separate app, not a UltraMail view (§4.1) | The user asked for it, and the tree agrees: a calendar has more consumers than mail (UltraMail's invitations, SmartHome schedules, a desktop agenda, UltraFIBU's deadlines). UltraMail's own concept already defers `.ics` handling to "phase 4 — ecosystem" and says the address book should become a shared module "if a dialer / calendar wants it" (`Apps/UltraMail/README.md:135`). |
| A headless `UltraCalendar` module + a thin app (§4.1) | Exactly the UltraCloud split (`UltraCloud` + `UltraCloudUI`) and the UltraMail split (engine library + GUI). The engine builds and tests without a display. |
| CalDAV first, Graph second, nothing else (§4.2) | One CalDAV client covers the ULTRA OS cloud *and* every provider that matters except Microsoft. Microsoft has no CalDAV and is retiring EWS; Graph is the only door. |
| The ULTRA OS cloud is a CalDAV/CardDAV/WebDAV server behind one ULTRA account (§8) | UltraCloud's roadmap already names "the ULTRA OS own storage service as one more provider" (`Docs/Modules/UltraCloud/README.md:256`). Files, calendars and contacts are one account and one sign-in; the client needs no ULTRA-specific protocol. |
| Wrap libical, do not write an iCalendar engine (§4.4) | `RRULE` expansion and time-zone handling are the two places every home-grown calendar goes wrong. libical is the reference implementation, MPL-2.0 / LGPL-2.1 dual-licensed, packaged everywhere, and the repository's rule for engines is "wrap, never expose". |
| Local-first: raw `.ics` per event + an UltraDatabase index + a pending-change queue (§5) | The exact shape UltraMail uses for mail (`.eml` + index + outbox). Offline works; sync is a background activity; the store is portable. |
| Copy, don't bridge, when migrating (§7) | A two-way bridge between two servers is a sync engine with two masters and is where data gets duplicated or lost. A copy plus an optional one-way mirror for a grace period is what people actually need, and it is explainable. |
| Two new framework elements: a day/week time grid and a month grid with events (§6.2) | Neither exists. `UltraCanvasCalendarView` is a *date picker* grid. A schedule view is an element by the house rule ("if it presents a value, it is an element") and SmartHome, a booking app and the desktop will want it. |

**Do not** build a proprietary "ULTRA sync protocol" for calendars, and **do
not** put the calendar engine inside UltraMail. Both would be more code for
less interoperability.

---

## 2. What was asked, restated

1. A **calendar application** for ULTRA OS, **separate** from the mail
   program, although most mail programs bundle one.
2. It must work with the **existing infrastructure** the user already has —
   their current calendar service — *as an option*.
3. ULTRA OS should **offer its own cloud-based storage** as an independent
   alternative.
4. The **user decides**: stay on the current system, or **migrate** to the
   ULTRA OS solution.

Point 4 implies a fifth requirement the request does not state but a user
will: the door must open **both ways**. Someone who moves to the ULTRA OS
cloud and later leaves must be able to take their calendars with them. An
open-source OS that locks calendars in would be judged by that.

---

## 3. Survey — where calendars live today, and how a client reaches them

### 3.1 The standards

| Standard | What it is | Needed for |
|---|---|---|
| **iCalendar** — RFC 5545 | The text format: `VCALENDAR` / `VEVENT` / `VTODO` / `VALARM` / `VTIMEZONE`, recurrence rules (`RRULE`, `EXDATE`, `RECURRENCE-ID`) | Everything. Every server and every export file speaks it |
| RFC 7986 | Newer iCalendar properties: `COLOR`, `NAME`, `IMAGE`, `CONFERENCE`, `REFRESH-INTERVAL` | Calendar colours and video-call links |
| **iTIP** — RFC 5546 | Scheduling semantics: `METHOD:REQUEST` / `REPLY` / `CANCEL` / `COUNTER` | Invitations and RSVPs |
| **iMIP** — RFC 6047 | iTIP carried in e-mail (`text/calendar` MIME parts) | Invitations from people on other systems; the UltraMail hand-off (§4.7) |
| **CalDAV** — RFC 4791 | Calendars as WebDAV collections; `REPORT calendar-query` / `calendar-multiget`; `MKCALENDAR` | The one protocol client (§4.2) |
| RFC 6578 | WebDAV `sync-collection` REPORT with a sync token | Cheap incremental sync |
| RFC 6764 | Service discovery: DNS `SRV _caldavs._tcp`, `/.well-known/caldav`, `current-user-principal` → `calendar-home-set` | "Type your address and password, nothing else" |
| RFC 6638 | CalDAV scheduling: the server delivers invitations and collects replies | Server-side invitations where supported (ULTRA cloud, Nextcloud, iCloud, Google) |
| `getctag` / `calendar-color` (Apple extensions) | A per-collection change tag; a colour property | Fast "did anything change?"; colours on servers that predate RFC 7986 |
| **CardDAV** — RFC 6352, **vCard** — RFC 6350 | Contacts, the same way | Not this app, but the same account (§8) |
| jCal — RFC 7265, JSCalendar — RFC 8984, JMAP Calendars — IETF draft | JSON representations | Not needed now. UltraNet's `jmap` plug-in is mail-only today; JMAP Calendars is still a draft |

Two things follow. First, **CalDAV plus iCalendar is the whole client-side
problem** for every provider but one; there is no second protocol to learn
for the ULTRA OS cloud, because the ULTRA OS cloud will simply *be* a CalDAV
server (§8). Second, the format is line-based text with a well-known but
deceptively intricate grammar — folding, escaping, parameters, time zones,
and a recurrence language with `BYDAY=-1FR`, `BYSETPOS`, `WKST` and
`RDATE` — which is why §4.4 argues for the reference library.

### 3.2 The providers

What each hosted service offers a third-party desktop client. "App password"
means a secondary password generated in the account's security settings, the
same thing UltraMail already documents for mail (`Docs/UltraMail/AccountSetup.md`).

| Provider | Calendar protocol | Sign-in | Notes |
|---|---|---|---|
| **ULTRA OS cloud** (to be built, §8) | CalDAV + CardDAV + WebDAV | ULTRA account (OAuth2 / OIDC, or password) | One account for files, calendars, contacts |
| **Nextcloud / ownCloud** | CalDAV at `<server>/remote.php/dav/` (sabre/dav) | App password (Basic) | The *same* server URL and app password UltraCloud's `nextcloud` provider already stores — a user with a Nextcloud file account has a calendar account with zero extra setup |
| **iCloud** | CalDAV at `https://caldav.icloud.com` | App-specific password (2FA required) | Principal discovery redirects to a per-user host (`pNN-caldav.icloud.com`); follow the `current-user-principal` chain rather than hard-coding it |
| **Google Calendar** | CalDAV v2 at `https://apidata.googleusercontent.com/caldav/v2/` (also a REST API v3) | **OAuth2 only**, scope `https://www.googleapis.com/auth/calendar` | The same Google OAuth client UltraMail already registers (`OAuthApps`) can carry the calendar scope. That scope is Google's *sensitive* class — it needs consent-screen verification but **not** the paid CASA assessment the restricted Gmail scope needs |
| **Outlook.com / Microsoft 365** | **No CalDAV.** Microsoft Graph (`/me/calendars`, `/me/events`, `calendarView` with delta queries) | OAuth2 only, scopes `Calendars.ReadWrite offline_access` | EWS is deprecated and Microsoft has announced it will be blocked for Exchange Online from October 2026 — Graph is the only door. The Microsoft app registration UltraMail already documents gains one more delegated permission |
| **Fastmail** | CalDAV at `caldav.fastmail.com` (also JMAP Calendars) | App password | |
| **Yahoo** | CalDAV at `caldav.calendar.yahoo.com` | App password | **[unverified]** whether new app passwords still cover CalDAV |
| **GMX / WEB.DE** | CalDAV (`caldav.gmx.net`, `caldav.web.de`) | Account password (app password with 2FA) | **[unverified]** host names |
| **mailbox.org** | CalDAV at `https://dav.mailbox.org/caldav/` | Account password | |
| **Posteo** | CalDAV at `https://posteo.de:8443/calendars/<user>/` | Account password | **[unverified]** port |
| Zoho, Kolab, SOGo, Zimbra, Synology, Radicale, Baïkal, DAViCal, Cyrus, Stalwart | CalDAV | Password | Any RFC 4791 server works with the generic path |
| **Exchange on-premises** | EWS / ActiveSync only | — | Out of scope. A Graph-only Microsoft provider does not reach it; say so in the wizard |

### 3.3 Where a user's calendar might be when they *have no* server

The "existing infrastructure" is sometimes a file, not a service:

| Program | Where the data is | How it gets out |
|---|---|---|
| Thunderbird (Lightning) | `local.sqlite` / `cache.sqlite` in the profile | Export calendar → `.ics` |
| Evolution | `~/.local/share/evolution/calendar/` | Export → `.ics` |
| KDE (Akonadi / KOrganizer) | `.ics` resources or Akonadi database | Save as `.ics` |
| Apple Calendar | CalDAV to iCloud, or local | Export → `.ics` |
| Outlook desktop | `.pst` | Save calendar → `.ics` (one calendar at a time) or `.csv` |
| Google, Nextcloud web | on the server | "Export" → a `.ics` per calendar |

Every one of them ends in a `.ics` file, so **`.ics` import is the universal
migration path** for anything that is not a live CalDAV or Graph account, and
`.ics` export is the universal way out. Both are the iCalendar engine plus a
file dialog; they cost nothing extra once the engine exists.

### 3.4 What the mail-bundled calendars actually do, and why the split is sound

Thunderbird, Outlook, Evolution and Apple Mail-plus-Calendar all bundle a
calendar, but the bundling is at the *account* and *invitation* level, not the
engine level: the calendar engine is a separate library in every one of them
(Thunderbird's `calendar/`, Evolution Data Server, Apple's CalendarStore).
What users get from the bundle is (a) one place to enter an account and (b)
an invitation in the inbox that becomes an event with one click. §4.5 and
§4.7 give UltraCalendar both without living inside UltraMail: a shared account
concept and a message-channel hand-off.

---

## 4. Design decisions

### 4.1 One module, one application

```
Apps/UltraCalendar/            the GUI (views, editor, wizards)           links UltraCanvas UI
UltraCalendar/                 the headless module                        links UltraNet, UltraDatabase, UltraVault, libical
  include/UltraCalendar/       public headers, namespace UltraCalendar
  core/                        model, iCalendar codec, store, sync engine, scheduler
  providers/                   CalDavProvider, GraphCalendarProvider, MemoryProvider (tests)
  ui/                          shared dialogs (add account, migration) — UltraCalendarUI, like UltraCloudUI
```

The split mirrors UltraCloud (`UltraCloud` + `UltraCloudUI`, `Docs/Modules/UltraCloud/README.md`)
rather than UltraMail, where the engine lives under `Apps/UltraMail/engine/`.
The difference is deliberate: UltraMail's engine has one consumer, the mail
app. A calendar has several, and each is already visible in the tree:

- **UltraMail** — "calendar-invite (.ics) preview" is on its roadmap
  (`Docs/UltraMail/Concept.md` §7, phase 4). It needs to *parse* an invitation
  and *ask the calendar* whether the slot is free; it should not own either.
- **SmartHome** — scenes and schedules ("heating on at 06:30 on weekdays") are
  `RRULE`s. The module currently has no recurrence engine of its own.
- **UltraFIBU** — UStVA deadlines, payment terms, the *Geschäftsjahr* boundary
  (`Docs/Research/UltraFIBUDesignProposal.md` §10) are dated obligations a
  desktop agenda should show.
- **The desktop** — the ULTRA OS launcher's clock / "today" panel and the
  message centre UltraMessage envisions (`Docs/Research/UltraMessageDesignProposal.md` §11).
- **ULTRA OS on Android** (`UltraCanvas/OS/Android/`) — later, the same
  engine behind the platform's `CalendarContract` provider.

Registry entry: a new numbered section in `Masterfile_modules.md` and a
`Docs/Modules/UltraCalendar/README.md`, exactly as UltraCloud has.

### 4.2 CalDAV is the client; Graph is the one exception

`ICalendarProvider` is stateless like `ICloudProvider`
(`UltraCloud/include/UltraCloud/UltraCloudProvider.h`): every call receives
the account and resolved credentials, nothing touches the stores.

```cpp
namespace UltraCalendar {

class ICalendarProvider {
public:
    virtual std::string Id() const = 0;             // "caldav", "google", "icloud", "nextcloud", "ultraos", "microsoft"
    virtual std::string DisplayName() const = 0;
    virtual ProviderCapabilities Capabilities() const = 0;   // syncToken, scheduling, colors, createCalendar, tasks

    // RFC 6764 discovery for CalDAV; a fixed base for Graph. Fills the account's
    // principal / home URLs. Cheap sign-in check included.
    virtual Result Discover(Account& account, const Credentials& credentials) = 0;

    virtual Result ListCalendars(const Account&, const Credentials&, std::vector<CalendarInfo>& out) = 0;
    virtual Result CreateCalendar(const Account&, const Credentials&, const CalendarInfo& info, CalendarInfo& created) = 0;

    // Incremental sync: with an empty `sinceToken` everything; returns changed and
    // deleted hrefs plus the new token (RFC 6578 sync-collection, or a ctag/etag
    // PROPFIND walk on servers without it; a Graph delta link for Microsoft).
    virtual Result Changes(const Account&, const Credentials&, const CalendarInfo& calendar,
                           const std::string& sinceToken, ChangeSet& out) = 0;

    // Fetch full iCalendar objects by href (calendar-multiget). Graph answers in
    // JSON; the provider converts to iCalendar so the store sees one format.
    virtual Result Fetch(const Account&, const Credentials&, const CalendarInfo&,
                         const std::vector<std::string>& hrefs, std::vector<CalendarObject>& out) = 0;

    // Write with optimistic concurrency (If-Match: etag; If-None-Match: * for new).
    // Precondition failure → ResultCode::Conflict with the server's current object.
    virtual Result Put(const Account&, const Credentials&, const CalendarInfo&,
                       CalendarObject& object /* href/etag updated */) = 0;
    virtual Result Delete(const Account&, const Credentials&, const CalendarInfo&,
                          const std::string& href, const std::string& etag) = 0;

    // OAuth2 providers only (same shape as UltraCloud's OAuthProviderBase).
    virtual Result SignIn(const Account&, const std::function<void(const std::string& url)>& openUrl, Credentials& out);
    virtual Result RefreshCredentials(const Account&, Credentials&);
};

} // namespace UltraCalendar
```

One `CalDavProvider` class implements the CalDAV path; the named providers
(`icloud`, `google`, `nextcloud`, `ultraos`, `fastmail`, …) are **presets**
over it — a base URL, a sign-in kind and a display name — the way UltraMail's
`AutoDiscovery::FromPresets` is a table over one IMAP engine. `google` adds
`OAuthProviderBase` behaviour for the bearer token. `microsoft` is the one
real second implementation, over Graph JSON, and converts to and from
iCalendar at its edge so the store, the views and the migration code never
see a second data model.

What the transport already provides, verified in the tree:

| Need | Already there |
|---|---|
| HTTP with arbitrary verbs (`PROPFIND`, `REPORT`, `MKCALENDAR`) and custom headers (`Depth`, `If-Match`, `Prefer`) | `UltraNetHttpMethod::Custom` + `customMethod` (`UltraCanvas/include/UltraNet/UltraNetHttp.h:16`); UltraCloud's WebDAV provider already does `PROPFIND` and `MKCOL` this way (`UltraCloud/providers/UltraCloudWebDav.cpp:210`, `:233`) |
| Multistatus XML parsing | `UltraCloud::ParseMultistatus` (`UltraCloudWebDav.h`) — extend for `calendar-data`, `getetag`, `sync-token`, `getctag`; the FIBU proposal's `UltraCanvasXML` facade over tinyxml2 would be the right home if it lands first |
| OAuth2 + PKCE through the system browser, refresh | `UltraNet_OAuth2AuthorizeInteractive` (`UltraNetOAuth2.h`); the per-provider app registration pattern in `UltraMail::OAuthApps` and `UltraCloud::SetOAuthApp` |
| DNS SRV for RFC 6764 | `UltraNet_DnsResolveAsync` (used by UltraMail's discovery plan) |
| TLS on by default, verification on | UltraNet's defaults |

### 4.3 Local-first store, mirrored from UltraMail

UltraMail keeps raw `.eml` files as the truth and an UltraDatabase index for
everything the UI reads (`Apps/UltraMail/engine/UltraMailLocalStore.h`). The
calendar does the same with `.ics`:

```
~/.local/share/UltraCalendar/         (per-platform user-data dir)
  calendar.db                          accounts, calendars, event index, instance cache, sync state, pending changes
  objects/<accountId>/<calendarId>/<uid>.ics    the raw iCalendar object as last seen from (or sent to) the server
  vault/                               `ultracalendar.vault` + `device.key` (UltraVault::DeviceKeyVault)
```

Raw objects survive round trips byte-for-byte where the server allows it,
which matters: a server-side property this client does not understand
(`X-APPLE-STRUCTURED-LOCATION`, `X-MICROSOFT-CDO-BUSYSTATUS`) must come back
unchanged after the user edits the title. The index is what the month view,
the agenda and the reminder scheduler read; §5 gives its schema.

The **pending-change queue** is UltraMail's outbox for calendars: every local
edit is written to the store immediately, appended to `pending_changes`, and
pushed by the sync worker when online, with retries and a conflict state.
Send never blocks and survives restarts.

### 4.4 The iCalendar engine: wrap libical

The two hard parts of any calendar are **recurrence expansion** and **time
zones**. RFC 5545's recurrence grammar has `FREQ`, `INTERVAL`, `COUNT`/`UNTIL`,
`BYMONTH`, `BYWEEKNO`, `BYYEARDAY`, `BYMONTHDAY`, `BYDAY` with ordinals,
`BYHOUR`/`BYMINUTE`/`BYSECOND`, `BYSETPOS` and `WKST`, and they *combine*
("the last weekday of every other month" is `FREQ=MONTHLY;INTERVAL=2;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1`).
Overrides (`RECURRENCE-ID`, `THISANDFUTURE`) and exclusions sit on top. The
time-zone side needs `VTIMEZONE` blocks in files to be reconciled with IANA
zones and daylight-saving transitions, including events created in one zone
and viewed in another.

**libical** (`libical/libical`, C, MPL-2.0 *or* LGPL-2.1 at the user's
choice) is the reference implementation, used by Evolution, KDE, Cyrus,
GNOME Calendar and many others; it parses, serialises, expands recurrences
(`icalrecur_iterator`) and ships its own zoneinfo with the option to use the
system's. It is packaged as `libical-dev` (Debian/Ubuntu, 3.0.x), `libical`
(Homebrew) and `libical` (vcpkg), so it fits the repository's three-OS
dependency lists (`.github/workflows/build.yml`) with no vendoring.

Per the house rule, the public surface is UltraCalendar-owned and **no libical
type appears in a header**: `Event`, `Recurrence`, `Alarm`, `Attendee`,
`DateTime` (UTC instant + optional `tzid` + `allDay` / floating flag),
`ParseICalendar(text) → Calendar`, `SerializeICalendar`, `ExpandInstances(event, from, to)`.
`std::chrono` time-zone support is *not* a substitute: GCC 13 has it, MSVC
has it, libc++ 18 does not, and the CI builds with Clang on Linux against
whichever libstdc++ the runner carries — one library with one behaviour beats
three standard libraries with two.

Consequences to book: a row in `Docs/Dependencies.md` and
`master_dependencies.yaml`, and a licence entry in `THIRD_PARTY_LICENSES.md`
(house rule 5). Numbers in `.ics` are dot-decimal (`GEO:48.85;2.35`) — parse
with `TryParseFloat`, never `std::stof`, as AGENTS.md warns.

### 4.5 Accounts and secrets: one ULTRA account, shared where the server is shared

Three account stores already exist and this is the moment to stop adding a
fourth kind:

| Store | Owner | Persisted in |
|---|---|---|
| Mail accounts | `UltraMail::LocalStore` | `mail.db` |
| Cloud storage accounts | `UltraCloud::AccountStore` | per-app `cloud.db` (roadmap item 1: one system-wide store) |
| Contacts | `UltraMail::ContactStore` | `contacts.db` (README: promote to `UltraContacts` "if a dialer / calendar wants it") |

The proposal does not block on unifying them, but it lines the calendar up so
that unification is one step later, not a rewrite:

- `UltraCalendar::Account` carries `providerId`, `serverUrl`, `username`,
  `displayName` and — the new bit — an optional **`linkedCloudAccountId`**.
  When set, credentials are resolved through UltraCloud's `CloudService`
  (which already refreshes OAuth tokens and reads UltraVault), so a Nextcloud
  or ULTRA OS account added in *any* app is offered as "use this account" in
  the calendar's add-account dialog with nothing to type.
- Otherwise the account has its own secret in an
  **`UltraVault::DeviceKeyVault`**
  (`UltraCanvas/include/UltraVault/UltraVaultDeviceKeyVault.h`, framework
  0.9.23) with the profile `{"ultracalendar.vault", "calendar.ultracalendar."}`
  — the one encrypted-file vault with a device-key auto-unlock that
  UltraMail, UltraSocial, UltraFiler and EmailCleaner already share; it
  stores a password or an OAuth token set per account and answers
  `MethodFor(account)`. The calendar adds no vault code of its own.
- The Google and Microsoft OAuth *app registrations* are the ones UltraMail
  bakes in (`UltraMailOAuthDefaults.h.in`, `cmake/oauth.local.cmake`), with
  the calendar scopes added to the same registration. One consent screen,
  one CASA question (mail's restricted scope needs it; calendar's sensitive
  scope does not).

### 4.6 The stay-or-migrate choice is a first-class flow

**First run.** As with UltraMail, the start page shows only what there is to
do — but here that is a decision, so it is three tiles, not one button:

```
┌──────────────────────────────────────────────────────────────────────┐
│  UltraCalendar                                                _ □ ✕  │
├──────────────────────────────────────────────────────────────────────┤
│                                                                      │
│              Where should your appointments live?                    │
│                                                                      │
│   ┌──────────────────┐ ┌──────────────────┐ ┌──────────────────┐     │
│   │  ☁  ULTRA OS     │ │  🔗 My existing  │ │  💻 Only on this │     │
│   │     cloud        │ │     calendar     │ │     computer     │     │
│   │ Sign in or create│ │ Google, iCloud,  │ │ No account. Can  │     │
│   │ an ULTRA account │ │ Nextcloud, Out-  │ │ move to a cloud  │     │
│   │                  │ │ look, CalDAV, or │ │ later            │     │
│   │                  │ │ an .ics file     │ │                  │     │
│   └──────────────────┘ └──────────────────┘ └──────────────────┘     │
│                                                                      │
│              You can add more accounts or move later.                │
└──────────────────────────────────────────────────────────────────────┘
```

A plain `UltraCanvasContainer` with three `CreateIconButton` tiles and two
`UltraCanvasLabel`s — the visual language UltraMail's concept planned for the
Toolbox (`Docs/UltraMail/Concept.md` §2.1), and the same open question about a
reusable `UltraCanvasTileGrid` applies.

**"My existing calendar"** runs a wizard that is UltraMail's account wizard
with calendar discovery behind it: name, e-mail address, password (or empty
for the OAuth providers, with the same hint text logic as
`ProviderNeedsAppPassword` / `ProviderAcceptsPassword`). Discovery order:
provider presets keyed by domain → RFC 6764 (`SRV _caldavs._tcp.<domain>`,
`https://<domain>/.well-known/caldav`) → principal walk → the list of
calendars found, each with a checkbox. The fallback is a manual page with
one field, the CalDAV URL. An `.ics` file is the fourth entry in the same
wizard: pick a file, name the calendar, choose where it goes (local, or any
account that can create calendars).

**"ULTRA OS cloud"** opens the ULTRA account sign-in in the system browser
(OAuth2 + PKCE + loopback, the flow UltraNet already implements), with a
"create an account" link on the provider's page; the app never hosts a
sign-up form. On return it lists the account's calendars (a new account has
one, "Personal") and registers the account in UltraCloud's store too, so
UltraMail's "attach cloud link" and UltraFiler's cloud drive see it.

**"Only on this computer"** creates a local calendar and no account. It is
not a lesser mode: everything works, and *Move to a cloud* (§7) is one menu
item away.

**Later.** The account list in Preferences has *Add account*, *Move
calendars…* (§7) and *Export…*. Nothing in the app implies that one choice is
final.

### 4.7 Invitations: through UltraMail, over the message channel

An invitation arrives as e-mail (iMIP). Mail is UltraMail's; the calendar is
UltraCalendar's; the two are separate processes. The hand-off is what
UltraMessage was built for (`Docs/Modules/UltraMessage/README.md`):

1. UltraMail detects a `text/calendar` part with `METHOD:REQUEST` / `CANCEL`
   / `REPLY`, shows the invitation card in the reading pane (its own roadmap
   item), and *asks the calendar* over the bus: a `Request` on
   `calendar.freebusy` for the slot, and — on the user's *Accept* /
   *Tentative* / *Decline* — a `PostRecorded` on `calendar.invitation` with
   the iCalendar text and the answer. UltraCalendar stores the event, sends
   the `REPLY` (§4.8) and acknowledges; UltraMail marks the message answered.
2. If UltraCalendar is not running, the *recorded* post bounces after its
   TTL and UltraMail says "open UltraCalendar to answer this invitation" —
   the RISC OS semantics the channel copied on purpose.
3. UltraCalendar, in turn, needs to *send* mail (invitations it originates,
   replies to iMIP invitations on accounts without server scheduling). It
   posts `mail.send.request` with a fully built message (the `text/calendar`
   part built by the engine, `MIME` by UltraMail's `MimeCodec` — or a
   `.ics` attachment on a plain message) and UltraMail's outbox does the rest.
   Neither topic exists yet; both are schema registrations, not new
   machinery.

Both topics are registered under the ULTRA vendor prefix with schemas
(`UltraMsg_RegisterSchema`) so a third-party mail client could take
UltraMail's role — which is the point of a separate calendar.

### 4.8 Sending invitations: server scheduling first, iMIP second

Where the server implements RFC 6638 (the ULTRA OS cloud will; Nextcloud,
iCloud and Google do), creating an event with `ATTENDEE`s is enough: the
server delivers the invitations and collects the replies into the event.
The client detects support via `calendar-auto-schedule` in the DAV header /
`schedule-outbox-URL`. Where it does not (a plain Radicale, an `.ics`-only
setup, a local calendar), the engine builds the iTIP message and hands it to
UltraMail (§4.7). The same choice is made per account for replies. The user
never sees the difference except in the "sent from" line.

### 4.9 Reminders

`VALARM`s are expanded with the instances (§5.3) into `alarm_schedule`. A
timer in the app fires them as `system.notification` messages on the bus
(`category: "calendar.reminder"`, actions *Snooze 5 min* / *Dismiss*), which
is the persistent, journaled topic UltraMessage already defines; the
freedesktop / Windows / macOS notification adapters are UltraMessage's Phase 2
and are *not built yet*, so until then the app shows the reminder in-window
plus a window badge. A reminder while the app is closed needs a process that
is running: propose `ultracalendar --agent`, the engine with no window,
autostarted per platform (an XDG autostart entry, a Login Item, a Run key).
It is the same binary and the same store; it is phase 3 (§10).

### 4.10 Natural-language entry (optional, later)

"Lunch with Max next Tuesday 12:30 at Luigi's" typed into the search field
can become an event through UltraAI's `ITextLLM` *structured output* — the
Anthropic, OpenAI and llama.cpp adapters all implement schema-constrained
answers (`Docs/Modules/UltraAI/README.md`), so it works with a local model
and never sends the calendar anywhere. It is an opt-in convenience, off by
default, and phase 4.

---

## 5. Data model

### 5.1 Entities

```cpp
namespace UltraCalendar {

struct Account {
    std::string accountId;            // "icloud-erika-example-com"
    std::string providerId;           // "caldav" | "icloud" | "google" | "nextcloud" | "ultraos" | "microsoft" | "local"
    std::string displayName;
    std::string serverUrl;            // discovered or typed base URL
    std::string principalUrl;         // CalDAV: current-user-principal
    std::string homeSetUrl;           // CalDAV: calendar-home-set
    std::string username;             // sign-in name
    std::string email;                // the user's address on this account (for ATTENDEE matching)
    std::string linkedCloudAccountId; // credentials via UltraCloud when set
    bool        schedulingOnServer = false;   // RFC 6638 detected
    bool        readOnly = false;             // e.g. a subscribed .ics URL
};

struct CalendarInfo {
    std::string calendarId;           // "<accountId>/<slug>"
    std::string accountId;
    std::string href;                 // collection URL (CalDAV) or Graph id
    std::string displayName;
    std::string color;                // "#RRGGBB"
    std::string syncToken;            // RFC 6578 token, ctag, or Graph deltaLink
    bool        supportsEvents = true, supportsTasks = false;
    bool        readOnly = false, visible = true;
    int         order = 0;
};

struct DateTime {                     // one representation for every date in the module
    int64_t     utc = 0;              // epoch seconds (all-day: midnight UTC of the civil date)
    std::string tzid;                 // IANA zone the user chose; empty = floating or all-day
    bool        allDay = false;
};

struct Event {
    std::string uid;                  // iCalendar UID — the identity across servers and migrations
    std::string calendarId;
    std::string href, etag;           // server addressing (empty on local calendars)
    std::string summary, location, description, url;
    DateTime    start, end;           // end exclusive, RFC 5545 semantics
    std::string recurrenceRule;       // RRULE text, empty = single
    std::vector<DateTime> exceptions;         // EXDATE
    std::optional<DateTime> recurrenceId;     // this object overrides one instance of `uid`
    std::string status;               // CONFIRMED | TENTATIVE | CANCELLED
    std::string transparency;         // OPAQUE (busy) | TRANSPARENT (free)
    std::vector<Attendee> attendees;  // address, name, role, partstat, rsvp
    std::optional<Attendee> organizer;
    std::vector<Alarm> alarms;        // trigger offset or absolute, action DISPLAY/AUDIO/EMAIL
    std::vector<std::string> categories;
    int         sequence = 0;
    int64_t     lastModified = 0;
};

struct Instance {                     // one occurrence, what the views draw
    std::string uid, calendarId;
    DateTime    start, end;
    std::optional<DateTime> recurrenceId;
    bool        isOverride = false, isCancelled = false;
};

}
```

`uid` is deliberately the identity, not a database row id: it is what CalDAV
addresses an object by, what an `.ics` export carries, and what makes
migration (§7) preserve invitations and overrides.

### 5.2 Schema (`calendar.db`, UltraDatabase, versioned migrations)

| Table | Purpose |
|---|---|
| `accounts` | one row per `Account`; no secrets |
| `calendars` | one row per `CalendarInfo`, including `syncToken` |
| `objects` | one row per iCalendar object (`uid` + `recurrenceId` per calendar): href, etag, the parsed envelope columns (summary, start/end UTC, tzid, all-day, rrule, status, organizer, sequence, last-modified), a `dirty` flag, and a `localVersion` counter; the raw text is the `.ics` file on disk |
| `attendees` | address, name, partstat, role per object — queried for "waiting for replies" |
| `instances` | the expansion cache: (object, occurrence start/end UTC) for a rolling window, e.g. −1 year … +2 years, rebuilt lazily per calendar when an object or the window changes; every view reads this table only |
| `alarms` | expanded alarm times for instances in the next N days, with `fired` / `snoozedUntil` |
| `pending_changes` | the queue: object, operation (put / delete), the etag the change was based on, attempts, last error, state (`queued` / `sent` / `conflict`) |
| `invitations` | iTIP objects received and not yet answered, with the source (mail message id) |
| `settings` | default calendar, default reminder, week start, working hours, time zone override |

Everything is parameterised through `UltraDb_*` (house rule); the schema is
brought up by `UltraDb_Migrate` like UltraMail's `LocalStore::Open`.

### 5.3 Instance expansion

`ExpandInstances` runs in the engine thread whenever an object changes, the
window shifts, or a time-zone rule changes. The base event's `RRULE` is
iterated by libical within [from, to); `EXDATE`s are removed; override
objects (same `uid`, a `RECURRENCE-ID`) replace the instance they name, and
a `RANGE=THISANDFUTURE` override replaces every later one. Cancelled
overrides stay in `instances` with `isCancelled = true` so the views can
show "cancelled" instead of a hole, which is what users expect after they
have deleted one occurrence of a series. Time zones are resolved at
expansion, so a change of the machine's zone re-expands rather than
rewriting objects.

---

## 6. The application

### 6.1 Windows and views

One main window, Texter-style manager class owning it:

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ [+ New event] [Today] [◀] [▶]   September 2026      [Day][Week][Month][Agenda] 🔍 │
├────────────────┬─────────────────────────────────────────────────────────────┤
│ ◀ Sep 2026 ▶   │  Mon 14   Tue 15   Wed 16   Thu 17   Fri 18   Sat 19  Sun 20│
│ Mo Tu We Th …  │ all-day: [Berlin trip ─────────────]                          │
│  1  2  3  4 …  │ 08 ─────────────────────────────────────────────────────────│
│  …             │ 09  ┌Standup┐                                                │
│ (mini month)   │ 10  └───────┘         ┌Dentist────┐                          │
├────────────────┤ 11                    └───────────┘         ┌Budget review┐  │
│ ☑ ● Personal   │ 12  ──── now ─────────────────────────────  │ (Carol)     │  │
│ ☑ ● Work       │ 13                                          └─────────────┘  │
│ ☐ ● Family     │ …                                                            │
│ ☑ ● Holidays DE│                                                              │
│ [Add account…] │                                                              │
└────────────────┴─────────────────────────────────────────────────────────────┘
```

| Part | Element(s) | Notes |
|---|---|---|
| Toolbar | `UltraCanvasToolbar`; the view switch is an `UltraCanvasSegmentedControl` | |
| Mini month | `UltraCanvasCalendarView` (`UltraCanvasDatePicker.h:180`) — exists; needs one addition, a per-day *marker* callback (dots for days with events), see §6.2 | |
| Calendar list | `UltraCanvasTreeView` with checkbox items, grouped by account; colour swatch per row | |
| Day / Week | **new** `UltraCanvasScheduleView` (§6.2) | |
| Month | **new** `UltraCanvasMonthView` (§6.2) | |
| Agenda | `UltraCanvasListView` with `IListModel` over `instances`, grouped by day, sorted and filtered by `UltraCanvasListSortFilterProxy` | virtualised, so a year of instances is fine |
| Search | `UltraCanvasTextInput` feeding the same proxy (summary / location / attendee) | |
| Event editor | `UltraCanvasModalDialog`: `UltraCanvasTextInput` (title, location), `UltraCanvasDatePicker` + `UltraCanvasTimePicker` ×2, `UltraCanvasSwitch` (all day), `UltraCanvasDropdown` (calendar; repeat: none / daily / weekly / monthly / yearly / custom…), `UltraCanvasTagInput` with `UltraCanvasAutoComplete` fed by the contacts store for attendees, `UltraCanvasDropdown` (reminder), `UltraCanvasTextArea` (notes), `UltraCanvasButton`s | the *custom repeat* sub-dialog exposes the RRULE fields with radio buttons and spinners |
| Invitation card | `UltraCanvasContainer` with `UltraCanvasBadge` (Accepted / Tentative / Declined) and three buttons | shared with UltraMail's reading pane by living in `UltraCalendarUI` |
| Popover on click | `UltraCanvasTooltipManager`-style card: title, time, location, *Edit* / *Delete* | |

Keyboard: `N` new, `T` today, `D`/`W`/`M`/`A` views, `←`/`→` previous / next
period, `Del` delete, `Ctrl+F` search, `Ctrl+Z` undo (edits are operations on
the store and undo re-applies the previous object text).

### 6.2 Two elements the framework is missing

Both go in `UltraCanvas/{include,core}` with a catalogue row and a doc, so
the next caller finds them; both may paint their *content* (they are
self-rendered views by the AGENTS.md exception) and add real elements for
anything that takes input.

**`UltraCanvasScheduleView`** — a day / multi-day time grid. Columns are
days, rows are hours; an all-day band on top; a "now" line; events as
rounded blocks packed side by side when they overlap (the same lane packing
`UltraCanvasTimelineChart` does horizontally, turned vertical); drag on
empty space creates (with a live ghost block), drag a block moves it, drag
its bottom edge resizes it, all snapped to a configurable step (15 min);
wheel scrolls, `Ctrl`+wheel zooms the hour height. Callbacks:
`onCreateRequested(start, end)`, `onEventMoved(uid, start, end)`,
`onEventActivated(uid)`, `onVisibleRangeChanged`. Data comes from an
`IScheduleModel` (instances in a range, colour, title, busy/free) so the
element knows nothing about iCalendar — SmartHome can put device schedules
in it, a booking screen can put rooms in it, and the DemoApp gets an example.

**`UltraCanvasMonthView`** — the six-week grid with event *chips* per cell,
a "+3 more" overflow that opens the day, multi-day events drawn as bars
across cells, weekend tint and week numbers as `UltraCanvasCalendarView`
already has, and the same drag-to-move for whole days. It shares
`UCDate`, the header rendering and the localisation hooks with
`UltraCanvasCalendarView`; the two should share one internal month-layout
helper rather than duplicate it.

**One addition to `UltraCanvasCalendarView`**: `SetDayMarkerProvider(fn)`
returning 0–3 colours for a date, drawn as dots under the number — the mini
month needs it, and any date picker in a booking flow wants it.

### 6.3 Preferences

Accounts (add / remove / *Move calendars…* / *Export…*), default calendar,
default reminder, week starts on, working hours (shaded in the schedule
view), time zone (follow the system, or fixed — the travel case), show week
numbers, and *Start on: last view*. All in one `UltraCanvasTabbedContainer`.

---

## 7. Migration — copy, then decide what to do with the source

`Preferences → Accounts → Move calendars…` (also offered as a card on the
start page when there is exactly one non-ULTRA account, once the ULTRA cloud
exists):

1. **Pick the source** — any account, or *the calendars on this computer*.
   Every calendar of it is listed with a checkbox and its event count.
2. **Pick the target** — an account that can create calendars (the ULTRA OS
   cloud, Nextcloud, Google, Microsoft, any CalDAV server with
   `MKCALENDAR`) or *this computer*. For each source calendar the target
   calendar is created with the same name and colour, or the user maps it to
   an existing one.
3. **Copy.** The engine has every object already (it is synced), so this is
   a local walk: for each source object, `Put` into the target with the
   *same `UID`*, the raw text unchanged apart from what the target rejects
   (the wizard reports those — a Google-specific `X-` property Nextcloud
   ignores is not an error). Overrides go after their base event.
   Attachments referenced by URL stay URLs; inline `ATTACH` binaries are
   copied with the object. Progress in an `UltraCanvasProgressDialog` (ring, percentage, Cancel); the copy
   is resumable because every object copied is marked in `migrations`.
4. **Then decide** — three radio buttons, defaulting to the first:
   - *Keep the old calendars connected, read-only, for 30 days* — the
     target is now the default; anything that still lands on the old server
     (a colleague's late reply, an invitation sent to the old address) is
     visible, and the wizard shows a "N new events on the old calendar,
     copy them?" banner. One-way, old → new, on request. After the period
     the account is offered for removal.
   - *Disconnect the old account now* — removed from the app; **nothing is
     deleted on the old server**. The wizard says so in those words.
   - *Keep both, fully* — two independent accounts, as before.

**What migration cannot do, said in the wizard rather than discovered
later:** events the user *organised* keep their old `ORGANIZER` address, so
replies from attendees still go to the old server; the wizard offers to
re-send invitations from the new account for future events with attendees.
Shared calendars owned by someone else are copied as a snapshot, not
re-shared. A Google-specific feature (appointment schedules, "focus time"
markers) does not exist on the target.

**Export** — *Preferences → Accounts → Export…* writes one `.ics` per
calendar, or one file with every calendar as separate `VCALENDAR` blocks,
through the same engine. This is the exit door and it works on the ULTRA OS
cloud exactly as on anyone else's.

**Import** is the wizard's `.ics` path (§4.6) and is also the drop target of
the main window: drop a `.ics` on it, choose the calendar, done. Dropping a
single `VEVENT` with `METHOD:REQUEST` is treated as an invitation.

---

## 8. The ULTRA OS cloud — what the server side must be

Nothing in the tree specifies this service yet: the only mention is
UltraCloud's roadmap line 4. The calendar makes the first concrete demand on
it, so the demand should be written down. From the *client's* point of view
the service is fully described by:

| Requirement | Why |
|---|---|
| **CalDAV** (RFC 4791 + 6578 sync-collection + 6764 discovery + 6638 scheduling) | So the client is the same `CalDavProvider`, preset `ultraos` |
| **CardDAV** (RFC 6352) | The contacts the calendar's attendee field and UltraMail both need — one account, one address book |
| **WebDAV files** + a share-link API | UltraCloud's storage provider (its roadmap item 4); attachments on events |
| **One ULTRA account** with OAuth2 + PKCE + loopback redirect (OpenID Connect for identity), app passwords as the fallback for clients without a browser | The flow UltraNet and both existing account wizards already implement |
| Account creation on a web page the app opens, not in the app | The app never hosts a sign-up form; the provider's page owns terms, CAPTCHA and e-mail verification |
| Per-user export (the `.ics` / `.vcf` / files download) and account deletion | The exit door, on the server side too |
| Quotas and a free tier | Product decision; the client shows the quota in Preferences when the server reports it (`quota-available-bytes`, RFC 4331) |
| EU hosting, GDPR posture, a published privacy policy | The audience ULTRA OS names (consumer and professional, German-speaking market by the FIBU proposal) |
| Optional: JMAP for mail on the same account | UltraNet's `jmap` plug-in exists; a server that offers CalDAV *and* JMAP mail makes "one ULTRA account" true for UltraMail as well |

Candidate servers, all standards-compliant and all run as **separate
processes on a server the project operates** — the client links none of
them, so their licences are not the framework's concern:

| Server | Licence | Gives | Notes |
|---|---|---|---|
| **Stalwart** | AGPL-3.0 (Rust, single binary) | IMAP, JMAP, SMTP, **CalDAV, CardDAV, WebDAV** (since 0.12, 2025), OIDC | The one that makes "one ULTRA account for mail, files, calendar, contacts" a single deployment |
| **Nextcloud** | AGPL-3.0 (PHP) | CalDAV, CardDAV, WebDAV files with OCS share links, OIDC via app | UltraCloud already speaks it fully; the ULTRA cloud could simply *be* a Nextcloud, which is the fastest path to a product |
| Radicale | GPL-3.0 (Python) | CalDAV, CardDAV | Tiny, no files, no sharing |
| Baïkal | GPL-3.0 (PHP, sabre/dav) | CalDAV, CardDAV | Same engine as Nextcloud's DAV |
| Cyrus IMAP | BSD-style | IMAP, JMAP, CalDAV, CardDAV | Mature; heavier to operate |

This document does not choose; it is a hosting and product decision. The
client-side cost of the choice is **zero** as long as the answer is a
standards server: `ultraos` is a preset with a base URL and an OAuth client
id, nothing more. That is the strongest argument for CalDAV over anything
invented here.

---

## 9. Application skeleton and file layout

```
UltraCalendar/
  CMakeLists.txt                     targets UltraCalendar (headless) and UltraCalendarUI
  include/UltraCalendar/
    UltraCalendar.h                  umbrella
    UltraCalendarTypes.h             Result, Account, CalendarInfo, DateTime, Event, Instance, Attendee, Alarm
    UltraCalendarICal.h              ParseICalendar / SerializeICalendar / ExpandInstances (libical behind it)
    UltraCalendarProvider.h          ICalendarProvider, registry, presets, plug-in host (as UltraCloud)
    UltraCalendarCalDav.h            CalDavProvider + pure helpers (discovery XML, multistatus, REPORT bodies)
    UltraCalendarGraph.h             GraphCalendarProvider (JSON ↔ iCalendar)
    UltraCalendarStore.h             the UltraDatabase store (§5.2)
    UltraCalendarSync.h              SyncEngine (Changes → Fetch → store; pending_changes → Put/Delete; conflicts)
    UltraCalendarScheduler.h         per-account cadence (UltraMail's SyncScheduler, generalised)
    UltraCalendarAlarms.h            alarm expansion and firing
    UltraCalendarScheduling.h        iTIP: build REQUEST / REPLY / CANCEL; apply an incoming one
    UltraCalendarMigration.h         the copy engine of §7 (pure over the store + a provider)
    UltraCalendarAccounts.h          AccountStore; secrets through UltraVault::DeviceKeyVault (§4.5)
  core/                              one .cpp per header
  providers/                         CalDav, Graph, Memory (tests)
  ui/
    UltraCalendarAccountDialog.{h,cpp}     add-account wizard (presets, discovery, manual URL, .ics)
    UltraCalendarMigrationDialog.{h,cpp}   §7
    UltraCalendarInvitationCard.{h,cpp}    shared with UltraMail's reading pane
Apps/UltraCalendar/
  main.cpp                           app init, start page vs. main window, --agent
  ui/UltraCalendarApp.{h,cpp}        manager class
  ui/UltraCalendarStartPage.{h,cpp}  §4.6
  ui/UltraCalendarMainWindow.{h,cpp} §6.1
  ui/UltraCalendarEventEditor.{h,cpp}
  ui/UltraCalendarPreferences.{h,cpp}
  UltraCalendar.desktop, CMakeLists.txt
UltraCanvas/include/UltraCanvasScheduleView.h, UltraCanvasMonthView.h        §6.2 (+ core/*.cpp, docs, DemoApp examples)
Tests/UltraCalendar/                 iCalendar round trips, RRULE cases from RFC 5545 §3.8.5.3 examples,
                                     time-zone cases, CalDAV against a fake HttpFn, migration over MemoryProvider
Docs/Modules/UltraCalendar/README.md
Docs/UltraCalendar/{Concept.md, AccountSetup.md, CHANGELOG.md}
media/appicon/UltraCalendar.png
```

Versioning follows AGENTS.md: one
`_ultracanvas_declare_product(ULTRACALENDAR "Docs/UltraCalendar/CHANGELOG.md")`
in `cmake/UltraCanvasVersion.cmake`, and the app reads
`ULTRACALENDAR_VERSION` for its About box; no literal anywhere else. Build
options `BUILD_ULTRACALENDAR` (module + app) and
`ULTRACANVAS_BUILD_ULTRACALENDAR_TESTS`, gated on `TARGET UltraDatabase`
exactly as `Apps/UltraMail/CMakeLists.txt` is.

**Threading** is UltraMail's: one sync worker per account plus one shared
worker for discovery, migration and alarms; results marshalled with
`UltraCanvasApplication::PostToUIThread`. The UI never waits on the network.

---

## 10. Roadmap

| Phase | Deliverable | Depends on |
|---|---|---|
| **1 — Walking skeleton** | `UltraCalendar` module with the iCalendar codec over libical, the store, `ExpandInstances`; the two framework elements; the app with local calendars only; start page (the *existing* and *ULTRA cloud* tiles present but greyed with "coming in the next release"); `.ics` import / export; event editor with recurrence and reminders (in-window). *Usable as a local calendar end-to-end; the engine and views are testable without a server.* | libical in the dependency lists |
| **2 — Existing infrastructure** | `CalDavProvider` with RFC 6764 discovery, sync-collection and the ctag fallback, the pending-change queue and conflict handling; presets for Nextcloud (linked to the UltraCloud account), iCloud, Fastmail, GMX / WEB.DE, mailbox.org, Posteo, generic CalDAV; Google over CalDAV + OAuth2 with the shared app registration; the add-account wizard. | UltraNet HTTP custom methods (exist) |
| **3 — Microsoft, invitations, reminders that fire** | `GraphCalendarProvider`; iTIP over server scheduling and over UltraMail through the two UltraMessage topics; invitation card in both apps; `--agent` and system notifications once UltraMessage's adapters land. | UltraMessage Phase 2 adapters; UltraMail's invitation card |
| **4 — The ULTRA OS cloud and migration** | `ultraos` preset over the chosen server; account creation flow; the migration wizard in both directions; quota display; registering the account in UltraCloud's store. | the server (§8) and the shared account list (UltraCloud roadmap 1) |
| **5 — Comfort** | Free/busy lookup for attendees, natural-language quick add via UltraAI, tasks (`VTODO`) with a Kanban view (`UltraCanvasKanbanBoard` exists), holiday calendars by subscription URL, printing, Android `CalendarContract` adapter. | |

Phases 1–2 are the public preview: a user with any CalDAV account, or none,
has a working calendar. Phase 4 is where the user's "stay or migrate"
decision becomes real, and it is deliberately after the client is solid on
other people's servers — the migration wizard is only trustworthy if the
sync engine is.

---

## 11. Gap analysis — what the tree has, what it lacks

| Piece | State | Action |
|---|---|---|
| HTTP with custom verbs and headers, TLS, DNS SRV | ✅ UltraNet | none |
| OAuth2 + PKCE + loopback, token refresh | ✅ UltraNet; app-registration pattern in UltraMail and UltraCloud | add calendar scopes to the Google / Microsoft registrations |
| WebDAV multistatus parsing | ✅ `UltraCloud::ParseMultistatus` (props limited to files) | extend, or replace with the proposed `UltraCanvasXML` facade |
| Named SQLite connections, migrations, bound parameters | ✅ UltraDatabase | none |
| Secret storage with a device-key unlock | ✅ `UltraVault::DeviceKeyVault` (framework 0.9.23; UltraMail and UltraSocial are profiles of it) | use with a calendar profile |
| Account store shared by apps | ⚠️ UltraCloud has one per app; roadmap item 1 makes it system-wide | link calendar accounts to it (§4.5); do not block on it |
| Message channel for the mail hand-off | ✅ UltraMessage Phase 1; ❌ notification adapters (Phase 2) | register two topics; in-window reminders until adapters exist |
| iCalendar parse / serialise, RRULE, time zones | ❌ nothing in the tree | wrap libical (§4.4) |
| CalDAV client, Graph calendar client | ❌ | build (§4.2) |
| Date value types | ⚠️ three exist: `UCDate` (`UltraCanvasDatePicker.h`), `UltraCanvasCalendarDate.h` (charts), `UltraFIBUDate` (FIBU) | the module adds `DateTime` for instants with zones and *uses `UCDate` for civil dates*; the duplication is a separate clean-up (see recommendations) |
| Month grid with events; day / week time grid | ❌ (`UltraCanvasCalendarView` is a picker) | two new elements (§6.2) |
| Date and time pickers, list view with sort / filter, tag input, autocomplete, modal dialog, segmented control, tree with checkboxes | ✅ | use |
| Contacts for attendees | ⚠️ `UltraMail::ContactStore`, app-local | read it through a small shared interface now; promote to `UltraContacts` with CardDAV later, as UltraMail's README already anticipates |
| The ULTRA OS cloud service | ❌ only a roadmap line | decide the server (§8); the client needs a preset |
| Structured-output LLM for quick add | ✅ UltraAI | phase 5 |

---

## 12. Open questions

1. **Which server is the ULTRA OS cloud?** Stalwart gives one account for
   mail, files, calendar and contacts in one binary; Nextcloud is what
   UltraCloud already speaks end-to-end and is the fastest path to a
   product. The client is indifferent; the product is not.
2. **Where does the shared account list live?** UltraCloud's roadmap says
   "one store under the user's config dir shared by all apps". The calendar
   is the third app to want it (after UltraMail and UltraFiler). Should that
   land *before* phase 2 so the calendar never grows its own?
3. **Move `OAuthApps` out of `Apps/UltraMail/engine/`** — the vault already
   moved (`UltraVault::DeviceKeyVault`), but the OAuth app-registration
   lookup (baked-in default, environment, `oauth.ini`) is still UltraMail's
   and UltraCloud has a second, simpler one (`SetOAuthApp`). One
   implementation, so the Google and Microsoft registrations are configured
   once for mail, files and calendar?
4. **Tasks.** `VTODO` on CalDAV is cheap in the engine and Nextcloud, iCloud
   and Fastmail all store tasks beside events. Phase 5, or phase 1 because
   the model is the same?
5. **Should the `.ics`-file calendar be a *provider*** (`providerId =
   "file"`, the file is the server, re-read on change) rather than an
   import? It would make "my calendar is a file in my Dropbox" work with
   sync-folder discovery (`GetCloudStorageFolders()`), which many
   Thunderbird users have today.
6. **Microsoft on-premises Exchange** — leave out, as proposed, or accept
   EWS as a third provider knowing Microsoft is retiring it online?
7. **Privacy switch for discovery** — RFC 6764 lookups reveal the user's
   domain to DNS only; no third-party ISPDB is involved, unlike mail's
   Thunderbird lookup. Is a switch still wanted for symmetry with UltraMail's
   open question 4?

## See also

- `Docs/Modules/UltraCloud/README.md` — the account, secret and provider
  pattern this proposal copies; roadmap item 4 names the ULTRA OS storage
  service
- `Docs/UltraMail/Concept.md`, `Docs/UltraMail/AccountSetup.md` — the wizard,
  the discovery pipeline, the OAuth app registrations and the vault posture
- `Docs/Modules/UltraMessage/README.md` — the channel for the mail ↔ calendar
  hand-off and for reminders
- `Docs/UltraCanvas/UltraCanvasDatePicker.md` — `UCDate`,
  `UltraCanvasCalendarView`, `UltraCanvasDatePicker`
- `Docs/UltraCanvas/UltraCanvasUIElements.md` — the element catalogue the
  two new views join
- `Docs/Research/UltraFIBUDesignProposal.md` — the sibling investigation
  whose `UltraCanvasXML` facade this proposal would reuse
