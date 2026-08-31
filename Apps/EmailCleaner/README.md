# EmailCleaner

Loads several mail accounts into an **analysis database** and shows who is
filling the mailbox: a **map view** of sender blocks, a **timetable** of when
each sender writes, and the keyword evidence behind every verdict — product
advertising, adult content, dating and romance scams, phishing, financial
fraud, and messages carrying executable attachments.

Full concept: [`Docs/EmailCleaner/Concept.md`](../../Docs/EmailCleaner/Concept.md).

> **Status (Phase 1):** the headless **analysis engine** is implemented and
> tested — the shared text pipeline, the keyword rule set and its file format,
> the classifier, the analysis store on **UltraDatabase**, the ingest over real
> RFC 5322 messages, and the analytics that shape the store's aggregates into
> the views. The **UI** has the account bar (account picker, load / re-analyse,
> category and search filters), the **sender map** (treemap, nested by sending
> domain, sized by messages / bytes / attachments / unwanted count), the
> **timetable** (weekday x hour heatmap plus traffic over calendar time) and the
> **messages** detail (summary, keyword evidence, attachment types, the message
> list). Still to come: acting on what the map shows — unsubscribe, delete and
> block from the selected block, and a rule editor in the UI rather than a
> text file.

## It does not fetch mail — UltraMail does

[`Apps/UltraMail`](../UltraMail) already owns accounts: auto-discovery, the
credential vault, the IMAP sync engine, and a cache of every message body at

```
<UltraMail data dir>/mail/<accountId>/<folder>/<uid>.eml
```

EmailCleaner reads that cache. Configure and sync an account in UltraMail, then
press **Load mail** here. The two apps share one mailbox without either
reaching into the other's tables: EmailCleaner mirrors the account list into
its own database and never writes to UltraMail's.

Point it at a different mailbox with `EMAILCLEANER_MAIL_DIR`.

## Layout

```
Apps/EmailCleaner/
  engine/                            headless — UltraDatabase + UltraNet MIME
    EmailCleanerTypes.{h,cpp}        categories, analysed message / sender block /
                                     timetable / timeline types, address + RFC 5322
                                     date parsing, UTC calendar arithmetic
    EmailCleanerText.{h,cpp}         the normalisation pipeline both a rule term and
                                     a message run through (HTML stripping, leet
                                     folding, "v.i.a.g.r.a" de-obfuscation)
    EmailCleanerRules.{h,cpp}        the keyword table + its editable text format
    EmailCleanerClassifier.{h,cpp}   keyword matching + the structural signals
                                     (risky attachments, spoofed display names,
                                     Reply-To mismatch, shouting subjects, bulk
                                     headers) -> category + 0..100 score + evidence
    EmailCleanerStore.{h,cpp}        the analysis database: messages, attachments,
                                     keyword hits, ingest state; the sender/domain
                                     rollups, the weekday x hour grid, the timeline
    EmailCleanerIngest.{h,cpp}       raw message -> analysed row; walks UltraMail's
                                     body cache, incrementally or as a full re-scan
    EmailCleanerAnalytics.{h,cpp}    store aggregates -> view models: the map
                                     hierarchy with its "Other" pooling, the shared
                                     category palette, the summary sentences
    EmailCleanerUnsubscribe.{h,cpp}  List-Unsubscribe / List-Unsubscribe-Post
                                     parsing, and the judgement about when taking
                                     the offer is a good idea (never for spam)
    EmailCleanerActions.{h,cpp}      ActionPlanner (pure: what *would* happen) and
                                     ActionExecutor (carries a plan out through
                                     IActionBackend)
    EmailCleanerMailBackend.{h,cpp}  the outward half: Trash resolution and the
                                     UID MOVE, the RFC 8058 one-click POST, the
                                     unsubscribe mail
  ui/                                UltraCanvas UI layer
    EmailCleanerApp.{h,cpp}          app manager: owns store + ingest + window
    EmailCleanerAccountBar.{h,cpp}   account picker, Load mail / Re-analyse, the
                                     filters every view shares, the status line
    EmailCleanerMapView.{h,cpp}      the map: UltraCanvasTreeMapElement over the
                                     analytics hierarchy, metric + grouping
                                     pickers, category legend
    EmailCleanerTimetableView.{h,cpp} UltraCanvasHeatmapChart (weekday x hour) +
                                     UltraCanvasBarChartElement (over time)
    EmailCleanerDetailView.{h,cpp}   selected block: summary, keyword evidence,
                                     attachment types, message list
    EmailCleanerActionsPanel.{h,cpp} Block / Unsubscribe / Move to Trash for the
                                     selected block: the live plan preview, the
                                     confirmation, and the blocklist
  main.cpp                           entry point
  CMakeLists.txt                     EmailCleanerEngine + the EmailCleaner app
```

## How a message is judged

Two kinds of evidence are combined, and both are recorded so the detail view
can explain a verdict rather than assert it:

1. **Keyword rules** — a weighted term list per category. The built-in table
   covers product spam, adult content, dating scams, phishing, financial fraud,
   malware lures, newsletters and transactional notifications.
2. **Structural signals** a keyword list cannot see — an executable or
   macro-bearing attachment (including `invoice.pdf.exe` double extensions), a
   display name hiding a different address, a `Reply-To` on another domain, a
   subject in capitals, a machine-generated sender address, and the bulk
   headers (`List-Unsubscribe`, `Precedence: bulk`, `Auto-Submitted`).

Both sides of a keyword match run through the **same normalisation**, which is
what makes a short term list hold up against real spam: `V1AGRA`,
`v.i.a.g.r.a` and `<b>vi</b>agra` all normalise to `viagra`, and a rule written
as `no-reply@` still matches after `@` has been folded to `a`.

The strongest category above the decision threshold wins, with the unwanted
families beating Newsletter and Notification — a bulk sender advertising pills
is spam, not a newsletter. The 0..100 score always reports unwanted-ness,
whatever category won, so the map can shade a block by it.

## Acting on a block

Selecting a block on the map arms the actions strip above the message list:
**Block sender**, **Unsubscribe** and **Move to Trash**, in any combination.
The strip shows the resulting plan as you tick — how many messages, from where,
and every warning — and **Apply** repeats the whole thing in a confirmation
before anything happens.

Three rules the panel will not bend:

- **Unsubscribing from spam is refused.** For every unwanted family the offer
  is a liveness probe, not a courtesy: taking it confirms a human reads the
  address. A well-formed RFC 8058 one-click link in a phishing message is more
  dangerous, not less, so the panel recommends *block and delete* instead.
  Where the offer is genuine, one-click (https only) is preferred, then a
  `mailto:` the app sends itself; a bare link is reported for you to open.
- **Deleting means moving to Trash**, resolved from the account's own folder
  list (the SPECIAL-USE role first, then `Trash` / `[Gmail]/Bin` /
  `Deleted Items` / `Papierkorb` / ...). If no Trash can be identified the move
  is refused rather than guessed at.
- **Blocking is local and reversible.** It changes what the map shows, never
  the server, and every entry can be taken back from **Blocked senders…**.

The mail half needs UltraNet's IMAP plug-in and the account's password from
UltraMail's vault. Without them the panel says which is missing and the local
half still works.

## Editing the rules

The rules are data. On first run the app writes `rules.txt` into its data
directory; anything added there is layered **on top of** the built-in table, so
editing it can only sharpen detection:

```
# category | weight | field | phrase      ('*' = no word boundary)
product-spam  | 3.0 | subject | half price
dating-scam   | 4.0 | any     | *lonely hearts*
adult         | 2.5 | body    | live show
```

`weight` and `field` may be omitted (defaults: `1.0`, `any`). A bad line is
reported and skipped rather than costing the whole file. Press **Re-analyse**
to re-read the rules and re-classify the stored corpus.

## Building & testing

The engine is headless, so it builds and tests without a display or a network:

```sh
cmake -S . -B build -DULTRACANVAS_BUILD_EMAILCLEANER_TESTS=ON
cmake --build build --target EmailCleanerEngineTests
ctest --test-dir build -R EmailCleanerEngine --output-on-failure
```

The GUI target (`EmailCleaner`, built whenever `BUILD_EMAILCLEANER` is ON and
the UltraCanvas UI library is available) needs the UI toolkit.
