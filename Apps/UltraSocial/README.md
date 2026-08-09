# UltraSocial

The ULTRA OS cross-posting application: compose once, post to several social
networks. Full concept and design:
[`Docs/UltraSocial/Concept.md`](../../Docs/UltraSocial/Concept.md).

UltraSocial is built on **UltraCanvas** (UI, JSON), **UltraNet** (HTTP,
OAuth2 — `UltraNet/UltraNetOAuth2.h`) and **UltraDatabase** (local store).

> **Status (Phase 1, engine):** the headless **engine** is implemented and
> tested — data types, the compose-once → adapt-per-network composer
> (code-point counting, word-boundary truncation, media trimming, caption
> limits), the credential vault (UltraMail's file backend pattern), the
> account + post-history store on UltraDatabase, and three connectors behind
> `ISocialConnector`:
>
> - **Mastodon** — dynamic OAuth client registration on the instance
>   (`/api/v1/apps`) + the UltraNetOAuth2 interactive flow (or a pasted
>   access token), media via `/api/v2/media` (multipart, 202-processing
>   poll), statuses with an `Idempotency-Key`.
> - **Bluesky** — app-password session (`createSession`), image blobs via
>   `uploadBlob`, `app.bsky.feed.post` records, transparent `ExpiredToken`
>   refresh with the rewritten credential blob handed back for the vault.
> - **Telegram** — Bot API (`getMe`/`getChat` at sign-in), `sendMessage`,
>   single photo + caption via `sendPhoto` (multipart); `t.me` permalinks
>   for channels with a public username.
>
> The **UI** (target `UltraSocial`) has the compose window — one text area,
> per-account target checkboxes with live character counters that flip to a
> warning badge over the limit, adaptation warnings, media chips via the
> file picker, a Post button, and the recent-history strip — plus the
> add-account wizard (network picker with per-network fields and hints).
> Sign-in and publishing run on worker threads and marshal back through a
> main-thread timer queue, so the window stays live while the browser
> consent or a slow network round-trip is in flight.
>
> Still to come (see the concept's phasing): the scheduling outbox
> (phase 2) and the Tier-2 connectors (Reddit, X).

## Layout

```
Apps/UltraSocial/
  engine/                              headless — UltraDatabase + UltraNet + JSON
    UltraSocialTypes.{h,cpp}           SocialNetwork, Account, PostDraft,
                                       AdaptedPost, SocialCapabilities, PostResult
    UltraSocialConnector.h/.cpp        ISocialConnector + AuthInput + factory
    UltraSocialComposer.{h,cpp}        adapt-per-network + validation
    UltraSocialCredentialVault.{h,cpp} per-account secrets out of the config
    UltraSocialStore.{h,cpp}           accounts + post history on UltraDatabase
    UltraSocialWebUtil.{h,cpp}         JSON requests, multipart bodies, media IO
    connectors/
      UltraSocialMastodonConnector.{h,cpp}
      UltraSocialBlueskyConnector.{h,cpp}
      UltraSocialTelegramConnector.{h,cpp}
  ui/
    UltraSocialApp.{h,cpp}             app manager: store + vault + windows,
                                       worker-thread sign-in/publish
    UltraSocialAccountWizard.{h,cpp}   add-account dialog
    UltraSocialComposeView.{h,cpp}     compose surface + targets + history
  main.cpp                             UltraCanvasApplication bootstrap
```

## Building and testing

The engine builds whenever UltraDatabase and UltraNet are in the tree
(`BUILD_ULTRASOCIAL`, default ON). Tests:

```bash
cmake -B build -DULTRACANVAS_BUILD_ULTRASOCIAL_TESTS=ON
cmake --build build --target UltraSocialEngineTests
./build/UltraSocialEngineTests
```

The connector tests run against scripted loopback HTTP fakes — no external
network, no real accounts.
