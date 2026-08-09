# UltraSocial — Investigation: automatic posting to social networks

**Status:** Concept + Phase 1 engine implemented (see below).
**Date:** 2026-08-09

> **Progress (2026-08-09):** the UltraNet OAuth2 helper (§3.1) shipped in
> *0.3.37*, the **Phase-1 headless engine** in *0.3.38*
> (`Apps/UltraSocial/engine/`: types, `ISocialConnector`, the composer,
> credential vault, account/history store, and the Mastodon / Bluesky /
> Telegram connectors — all covered by `Tests/UltraSocial`), and the
> **Phase-1 UI** in *0.3.39* (compose window with live per-network
> counters + warnings, media chips, account wizard, history strip).
> The scheduling outbox (§5 phase 2) and the Tier-2 connectors are still
> open.

This document answers the question *"can we build an ULTRA OS app that posts
automatically to different social networks?"* — network by network, and against
what the framework already ships. Short answer: **yes**, with a clear
three-tier rollout. The framework already provides every generic building
block (HTTPS with async + uploads, multipart MIME, a TCP listener for the
OAuth loopback redirect, JSON, a local database, a credential-vault pattern, a
persistent-outbox + scheduler pattern, and all required UI elements). What has
to be written is the app itself: per-network connectors, one OAuth2/PKCE
helper, and a compose-once → adapt-per-network engine.

---

## 1. Network-by-network feasibility

The decisive constraint is not technical — every network below is "HTTPS +
JSON + OAuth-ish token" — it is **API policy**: what each operator lets a
third-party desktop app do, and at what price. Assessment as of mid-2026;
re-verify each network's terms at implementation time, this is the part that
churns.

| Network | Posting API | Auth | Cost / gatekeeping | Tier |
|---|---|---|---|---|
| **Mastodon** (+ Fediverse: GoToSocial, Pleroma…) | `POST /api/v1/statuses`, media via `POST /api/v2/media` | OAuth2; **dynamic client registration** (`POST /api/v1/apps`) — no pre-registered keys needed | Free, no review | **1** |
| **Bluesky** (AT Protocol) | `com.atproto.repo.createRecord` with an `app.bsky.feed.post` record; blobs via `com.atproto.repo.uploadBlob` | App password (`createSession`) today; OAuth rolling out | Free, no review | **1** |
| **Telegram channels** | Bot API `sendMessage` / `sendPhoto` / `sendVideo` to a channel the bot administers | Bot token from @BotFather (pasted once) | Free | **1** |
| **Reddit** | OAuth2 `POST /api/submit` | OAuth2 code flow; "installed app" type = public client, no secret | Free tier (~100 requests/min per client); subreddit rules apply | **2** |
| **X (Twitter)** | `POST /2/tweets` (+ media upload endpoint) | OAuth 2.0 + PKCE (public client supported for native apps) | Free tier caps writes to roughly hundreds of posts/month; paid tiers above that; pricing/limits have changed repeatedly | **2** |
| **LinkedIn** | Versioned `/rest/posts` | OAuth2 `w_member_social` via the "Share on LinkedIn" product | App must be created and product-approved in the LinkedIn developer portal; confidential client (needs a secret) | **3** |
| **Facebook Pages** | Graph API `POST /{page-id}/feed` with a Page access token | Meta OAuth | **Pages only** — personal-profile auto-posting is not offered at all. Public distribution requires app review + business verification; unreviewed dev-mode works only for the developer's own accounts | **3** |
| **Instagram** (professional accounts) | Graph API content publishing | Meta OAuth | Same review burden as Pages, **plus** the API fetches media from a public URL — a desktop app must first host the image somewhere reachable | **3** |
| **TikTok / YouTube** | Content Posting API / Data API video upload | OAuth2 | Video-first pipelines; TikTok requires an API audit, YouTube upload quota is tight | out of scope v1 |

Tier 1 (Mastodon, Bluesky, Telegram) needs **no operator approval of any
kind** and no distributed secrets — a build-it-this-week target. Tier 2 works
today but with per-user app registration or tight free-tier write caps.
Tier 3 requires developer-portal review processes the *user* (or Cloverleaf,
as app publisher) must go through, and is where auto-posting to personal
profiles is partly impossible by policy, not by engineering.

### Per-network content constraints the composer must know

| Network | Text limit | Media |
|---|---|---|
| X | 280 weighted chars (more on premium) | 4 images or 1 video |
| Mastodon | 500 (server-configurable — read from instance info) | 4 images or 1 video |
| Bluesky | 300 graphemes | up to 4 images ≤ ~1 MB each (resize before upload); video supported |
| Telegram | 4096 message / 1024 media caption | effectively unconstrained for this use |
| Reddit | 300 title + 40k selftext | link/image/video post kinds |
| LinkedIn | ~3000 | images, one video |
| Facebook Page | ~63k | rich |

---

## 2. What the framework already provides (verified)

| Need | Existing piece |
|---|---|
| HTTPS GET/POST, async, custom headers, TLS verification on by default | `UltraNet/UltraNetHttp.h` (`UltraNet_HttpRequestAsync`, `UltraNet_HttpUploadFile`) |
| multipart/form-data bodies for media upload | `UltraNet/UltraNetMime.h` (`UltraNet_MimeBuild`, multipart parse/build) |
| Loopback listener for the OAuth redirect (`http://127.0.0.1:<port>/callback`) | `UltraNet/UltraNetSocket.h` (`UltraNet_TcpListen` / `UltraNet_TcpAccept`) |
| URL building / query-string encoding | `UltraNet/UltraNetUrl.h` |
| Opening the system browser for the consent page | `UltraCanvas::OpenURL` (`UltraCanvasUtils.h:50`) |
| JSON request/response bodies | `UltraCanvasJSON` (yyjson wrapper) |
| Local store for accounts, queue, history | UltraDatabase |
| Secrets out of the config file | `Apps/UltraMail/engine/UltraMailCredentialVault.{h,cpp}` pattern today; UltraVault module (`UltraAI/Docs/UltraVault.md`) when it lands |
| Persistent send queue with retry | `Apps/UltraMail/engine/UltraMailOutbox.{h,cpp}` pattern (enqueue → flush → sent-remove / fail-retry) |
| Background scheduling on a UI timer | `Apps/UltraMail/engine/UltraMailSyncScheduler.{h,cpp}` pattern |
| Image resize/re-encode before upload (Bluesky's ~1 MB blob cap) | FileLoader / PixelFX |
| Compose UI: text area, tag input, media strip, per-network tabs, status chips, date/time scheduling | `UltraCanvasTextArea`, `UltraCanvasTagInput`, `UltraCanvasImageElement`, `UltraCanvasTabbedContainer`, `UltraCanvasBadge`/`Chip`, `UltraCanvasDatePicker` — all in the element catalogue |
| Optional: adapt one long draft into per-network tone/length | UltraAI LLM capability |

**UltraMail is the architectural template.** It is the same shape of app —
headless engine (store + credential vault + protocol plugins + outbox +
scheduler) under a thin UltraCanvas UI — already accepted in this repository.
UltraSocial should copy that layout, not invent one.

## 3. What is missing

1. ~~An OAuth2 authorization-code + PKCE helper.~~ **Done — now in UltraNet**
   (`UltraNet/UltraNetOAuth2.h`, added 0.3.37): PKCE generation, consent-URL
   building, the loopback redirect listener, code exchange, refresh, and the
   one-call `UltraNet_OAuth2AuthorizeInteractive` orchestrator. UltraSocial
   connectors call it directly; UltraMail (OAuth IMAP: Gmail/Outlook) and
   UltraAI adapters can share it.
2. **Per-network connectors** behind one interface (section 4). These are
   REST-over-HTTPS clients, not new wire protocols, so they belong to the app
   engine — not to UltraNet's `I<Category>ProtocolPlugin` DSO system. If a
   third connector consumer ever appears, the interface can migrate.
3. **The app itself** (`Apps/UltraSocial/`).

## 4. Proposed architecture

```
Apps/UltraSocial/
  engine/                          headless — depends on UltraDatabase + UltraNet
    UltraSocialTypes.{h,cpp}       Network enum, Account, PostDraft, PostTarget,
                                   PostResult, DeliveryStatus
    UltraSocialStore.{h,cpp}       accounts / queue / post history on UltraDatabase
    UltraSocialCredentialVault.{h,cpp}  tokens & secrets (UltraMail vault pattern;
                                   swaps to UltraVault when it exists)
    UltraSocialConnector.h         ISocialConnector interface (auth flows go
                                   through UltraNet/UltraNetOAuth2.h directly)
    connectors/
      MastodonConnector.{h,cpp}    phase 1 (covers the whole Fediverse)
      BlueskyConnector.{h,cpp}     phase 1
      TelegramConnector.{h,cpp}    phase 1
      RedditConnector.{h,cpp}      phase 2
      XConnector.{h,cpp}           phase 2
    UltraSocialComposer.{h,cpp}    one master draft -> per-network adapted
                                   variants (truncation policy, link handling,
                                   media resize via FileLoader/PixelFX)
    UltraSocialOutbox.{h,cpp}      persistent queue: Enqueue(draft, targets,
                                   scheduledAt); Flush() posts per target with
                                   retry/backoff and partial-success tracking
    UltraSocialScheduler.{h,cpp}   UI-timer driven, fires due queue entries
  ui/
    UltraSocialApp.{h,cpp}         UltraCanvasApplication bootstrap
    UltraSocialAccountWizard.{h,cpp}  per-network guided sign-in
    UltraSocialComposeWindow.{h,cpp}  TextArea + media strip + per-network
                                   preview tabs with live char-count badges +
                                   DatePicker "post later"
    UltraSocialQueueView.{h,cpp}   scheduled queue + history with status chips
```

The connector interface, mirroring `IMailProtocolPlugin`'s result style:

```cpp
class ISocialConnector {
public:
    virtual SocialNetwork Network() const = 0;
    virtual SocialCapabilities Capabilities() const = 0;   // limits of section 1
    virtual UltraNetResult Authenticate(Account&) = 0;      // wizard-driven
    virtual UltraNetResult RefreshAuth(Account&) = 0;
    virtual std::vector<std::string> ValidateDraft(const AdaptedPost&) = 0;
    virtual UltraNetResult PublishPost(const Account&, const AdaptedPost&,
                                       PostResult& out) = 0;  // out: id + URL
};
```

**Posting flow:** compose once → `UltraSocialComposer` produces one
`AdaptedPost` per selected target (validated live in the preview tabs) →
`Outbox.Enqueue` (now, or `scheduledAt`) → scheduler fires → per-target
publish with independent retry, so one network failing never blocks the
others → history row with the resulting post URL per network.

**Auth model.** A desktop app is a *public* OAuth client — it cannot keep a
client secret. Per network: Mastodon self-registers its client at first login
(no keys at all); Bluesky and Telegram are pasted app-password / bot-token;
Reddit and X use PKCE public clients (ship a client id, or let the user
supply their own to escape shared rate caps); LinkedIn/Meta require the user
to register their own developer app, which is exactly why they are Tier 3.
All tokens go through the credential vault, never into the config file.

**Compliance.** Every network's ToS bans undisclosed automation/spam. The app
posts *the user's own content to the user's own accounts* — the legitimate
case — but the outbox should still enforce per-network rate limits and honor
`429` back-off, and connectors must not work around API gates (e.g. no
scraping fallbacks where an API is refused).

## 5. Suggested phasing

1. **Phase 1 — prove the spine:** engine skeleton, vault, Mastodon + Bluesky +
   Telegram connectors, compose window with per-network preview, immediate
   posting. No approval processes, no secrets, covers Fediverse + AT Protocol +
   broadcast channels.
2. **Phase 2 — the "automatically" part:** outbox + scheduler + history view,
   OAuth/PKCE helper, Reddit + X connectors.
3. **Phase 3 — the gated networks + polish:** LinkedIn, Facebook Pages /
   Instagram (behind their review processes), optional UltraAI draft
   adaptation, promotion of OAuth helper / connector interface to shared
   modules if a second consumer appears.

---

*Part of ULTRA OS · MIT license · Cloverleaf UG*
