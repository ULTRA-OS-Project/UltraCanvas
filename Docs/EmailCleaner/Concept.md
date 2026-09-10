# EmailCleaner — concept

**Status:** Phase 3 implemented (analysis engine, the three views, acting on a
selected block, and correcting what it decides).
**Version:** 0.3.0
**Author:** UltraCanvas Framework / ULTRA OS

## The problem

A mailbox that has been in use for a decade is not a list of messages, it is a
*shape*: a few people you correspond with, a couple of dozen services that
notify you, a long tail of newsletters you half-remember signing up for, and —
usually the largest part by volume — senders you never agreed to hear from at
all. Scrolling a message list tells you nothing about that shape. You cannot
see that one marketing platform accounts for a fifth of everything you have
received, or that a "shop" has been mailing you at 03:00 every Tuesday for two
years.

EmailCleaner exists to make that shape visible, and to name the parts of it
worth removing.

## The three questions it answers

1. **Who is filling my mailbox?** — the **map view**. Every sender is a block,
   sized by how much of the mailbox it accounts for and coloured by what it
   mostly sends. Senders nest under their sending domain, so a mailing house
   writing from twenty addresses reads as one block until you open it.
2. **When do they write?** — the **timetable**. A weekday x hour grid, plus the
   same traffic over calendar time. A person writes across office hours; a
   sending platform fires in a narrow slot, every week, until the campaign
   stops. The grid says which one you are looking at, and the calendar chart
   says when it started and whether it ever ended.
3. **What is it, and why do you say so?** — the **detail view**. The numbers
   behind the block, the terms that fired, the attachment types, and the
   messages themselves. Every verdict is explained rather than asserted.

## Architecture

```
UltraMail                        EmailCleaner
─────────                        ────────────
accounts + credentials
IMAP sync ──> mail.db
          └─> mail/<account>/<folder>/<uid>.eml
                                  │
                                  │  ingest: parse (UltraNet MIME),
                                  │  classify, store
                                  v
                            analysis.db  (UltraDatabase / SQLite)
                              messages · attachments · keyword_hits
                                  │
                                  │  aggregate queries
                                  v
                            Analytics  (view models)
                                  │
                    ┌─────────────┼─────────────┐
                    v             v             v
                map view      timetable    detail view
              (TreeMap)     (Heatmap +    (list + evidence)
                             BarChart)
```

**EmailCleaner does not speak IMAP.** UltraMail already owns accounts,
auto-discovery, the credential vault and the sync engine, and it caches every
message body on disk. Re-implementing that would mean two mailbox
implementations to keep correct and two sets of credentials to protect.
EmailCleaner reads UltraMail's body cache and mirrors its account list; it
never writes to UltraMail's tables.

### Layers

| Layer | Depends on | Responsibility |
|---|---|---|
| `EmailCleanerText` | — | The normalisation pipeline both a rule term and a message run through. |
| `EmailCleanerTypes` | — | The taxonomy, the record and aggregate shapes, address/date parsing, UTC calendar arithmetic. |
| `EmailCleanerRules` | Text, Types | The keyword table and its editable text format. |
| `EmailCleanerClassifier` | Rules | Keyword matching plus the structural signals; produces a category, a score and its evidence. |
| `EmailCleanerStore` | UltraDatabase | The analysis database and the aggregate queries the views consume. |
| `EmailCleanerIngest` | Store, Classifier, UltraNet MIME | Raw message in, analysed row out; walks the mail cache. |
| `EmailCleanerAnalytics` | Store | Shapes aggregates into view models; owns the palette and the summary sentences. |
| `EmailCleanerUnsubscribe` | Types | Reads `List-Unsubscribe` / `List-Unsubscribe-Post`, and judges whether taking the offer is wise. |
| `EmailCleanerActions` | Store, Unsubscribe | Plans an action on a selection (pure), and carries the plan out through a backend interface. |
| `EmailCleanerMailBackend` | Actions, UltraNet | The half that leaves the machine: the Trash move and the unsubscribe request. |
| `EmailCleanerAttachments` | Types, Classifier, UltraNet MIME | Reads one attachment back out of the cached message — and refuses the risky ones. |
| `ui/` | UltraCanvas | Draws. Runs no SQL of its own and makes no classification decisions. |

Everything below `ui/` is headless and unit-tested: no display, no network.

## Detection

Two kinds of evidence, combined and both recorded:

**Keyword rules** — a weighted term list per category, matched against the
subject, the body, the sender, or attachment names, with word boundaries by
default (so "sex" does not fire inside "Essex"). The built-in table covers:

| Category | What it catches |
|---|---|
| `product-spam` | Unsolicited advertising: pharmacy, replicas, casino, weight loss, "make money fast". |
| `adult` | Pornography and adult services. |
| `dating-scam` | "Singles in your area", romance-scam openers, bride scams. |
| `phishing` | Account-suspension and credential-harvesting bait. |
| `financial-scam` | Advance-fee, inheritance, lottery, crypto-profit fraud. |
| `malware-risk` | The social engineering that travels with a hostile attachment. |
| `newsletter` | Opt-in bulk mail — legitimate, but worth separating. |
| `notification` | Transactional automation: receipts, confirmations, alerts. |

**Structural signals** a keyword list cannot see: an executable, script or
macro-bearing attachment (including `invoice.pdf.exe` double extensions), a
display name hiding a different address than the envelope sender, a `Reply-To`
on another domain, a subject in capitals, exclamation-mark pile-ups, a
machine-generated sender local part, and the bulk headers
(`List-Unsubscribe`, `List-Id`, `Precedence: bulk`, `Auto-Submitted`).

### Why normalisation is the load-bearing part

Real spam does not spell its keywords. A term list that matches only literal
text is defeated by `V1AGRA`, by `v.i.a.g.r.a`, and by `<b>vi</b>agra`. So both
the message *and every rule term* go through one pipeline:

1. Strip HTML — but only **block-level** elements become a word boundary.
   Inline formatting (`<b>`, `<span>`, `<a>`) is removed without a space,
   because that is precisely the trick being used.
2. Lowercase and fold the leet substitutions: `0→o 1→i 3→e 4→a 5→s 7→t $→s @→a`.
3. Collapse letter-separator runs: `v.i.a.g.r.a` and `v i a g r a` become
   `viagra`, while `e-mail`, `J.R. Tolkien` and `a b c d` are left alone
   (a run needs three letters, or five when the separator is a space).
4. Collapse whitespace, so a multi-word term matches across a line break.

Applying it to **both sides** is what makes a rule like `no-reply@` work: the
term folds to `no-replya` exactly as the address does. Running it on only one
side would silently break every rule containing `@`, a digit or a hyphen.

### The verdict

Rule weights accumulate per category. The strongest **unwanted** family above
the decision threshold wins; failing that, the strongest benign one; failing
that, the message falls back to Newsletter (if it has bulk headers), Personal
(if it is addressed to the account owner) or Unclassified. The unwanted
families deliberately beat Newsletter and Notification — a bulk sender
advertising pills is spam, not a newsletter.

The 0..100 score always reports **unwanted-ness**, whatever category won, so a
block can be shaded by it even when its dominant category is benign.

## The database

One SQLite file per installation, through UltraDatabase, schema managed by
versioned migrations:

| Table | Holds |
|---|---|
| `accounts` | The mail accounts, mirrored from UltraMail. No secrets. |
| `messages` | One row per analysed message: sender, domain, subject, date, size, flags, attachment counts, category, score. |
| `attachments` | Per-attachment metadata — filename, media type, size, inline, risky. Never the bytes. |
| `keyword_hits` | The terms that fired, per message: the evidence behind a verdict. |
| `ingest_state` | How far the ingest has got per account/folder. |

Attachments and hits are **derived data**: an upsert replaces them wholesale,
so re-analysing after a rule change can never leave stale evidence behind. A
message write is one transaction, and a batch load is one transaction for the
batch, which is what makes a first full-mailbox load fast.

Every query binds its parameters; no caller-supplied string is ever
concatenated into SQL.

### Aggregates are queries, not loops

The UI never walks the message table. The store answers exactly the shapes the
views need — sender and domain rollups with their dominant category, the
weekday x hour grid, the timeline with its empty buckets filled in, category
and attachment-type totals, the top keywords. That keeps the drawing code free
of analysis logic and lets a mailbox of a hundred thousand messages redraw from
indexed aggregates rather than a scan.

Time bucketing is done in **UTC**, deliberately: a timetable that shifted
because the reader changed timezone would not be comparable to the one they
looked at yesterday.

## Acting on a block (Phase 2)

Seeing that one sender is 20% of the mailbox is half the job. The other half is
doing something about it, and the selection on the map is what it acts on: an
actions strip above the message list offers **Block sender**, **Unsubscribe**
and **Move to Trash**, in any combination.

The shape of it is deliberate, because these are the only irreversible things
the app does:

**Plan, then execute.** `ActionPlanner` is pure — given the selection and the
database it says exactly what *would* happen (which messages, from which
folders, whether an unsubscribe offer exists and whether it is worth taking,
and what the user should be warned about) without touching anything. The strip
shows that plan continuously as the tick boxes change, so the consequence is on
screen before the button is pressed; pressing **Apply** repeats it, with every
warning, in a confirmation dialog. `ActionExecutor` then runs the plan, block
first (local, and cannot fail outward), then the unsubscribe, then the moves.

**Unsubscribing from spam is refused, not offered.** This is the judgement the
`EmailCleanerUnsubscribe` module exists for. An unsubscribe link works for bulk
mail you once opted into; in a spam, phishing or dating-scam message it is a
*liveness probe*, and taking it confirms a human reads the address. So for
every unwanted family the advice is `RefuseSpam` — **whether or not the offer
is well formed**. A perfectly valid RFC 8058 one-click link in a phishing
message is more dangerous, not less, and the panel says so instead of acting.
Where the offer is genuine, one-click (`POST List-Unsubscribe=One-Click`, https
only) is preferred, then a `mailto:` the app can send unattended; a bare link
is reported for the user to open, never followed.

**Deleting means moving to Trash**, resolved from the account's own folder
list — the IMAP SPECIAL-USE `\Trash` role first, then the names servers
actually use (`Trash`, `[Gmail]/Bin`, `Deleted Items`, `Papierkorb`,
`Corbeille`, ...). If none can be identified the move is *refused* rather than
guessed at: a wrong guess scatters mail into a folder nobody looks in. A
selection is never emptied wholesale either — `unwantedOnly` is on by default
for a domain target, and any message the analysis calls wanted is counted in a
warning before the confirmation.

**Blocking is local and reversible.** The blocklist lives in the analysis
database and changes what the map shows and what the counts say; it never
touches the server. Every entry can be seen and taken back from **Blocked
senders…**, because a block the user cannot undo is not a block, it is a
mistake waiting.

The mail half needs UltraNet's IMAP plug-in and a password from UltraMail's
vault. When either is missing the panel says which, and the local half still
works — that is the normal state on a machine where UltraMail has not been set
up, not an error.

## Correcting it, and looking inside (Phase 3)

Three things the earlier phases left open, and the judgement in each.

**The user's verdict beats the classifier's.** "This is fine" and "This is
spam" record what the user says about a sender — one address, or a whole
domain — and that decides their mail from then on. Two properties make it safe
to use: the classifier's own verdict is kept alongside in the store, so *taking
a correction back restores what it actually said*, message by message, rather
than leaving a hand-set value with nothing to return to; and an address
correction beats a domain one, so "all of this domain is spam except this one
address" is sayable. It is deliberately not the same control as **Block**: "I
do not want to hear from them" and "your verdict about them is wrong" are
different statements, and a sender can warrant either, both, or neither.

**The rules are editable where they are used.** They were always data — a text
file layered over the built-in table — but editing meant finding `rules.txt` in
the data directory and opening it in something else. **Rules…** is that file, in
a dialog: the user's own rules listed, added and removed, seeded with the
strongest term behind the current selection, saved and re-analysed in one step.
The built-ins stay out of reach on purpose. They are the floor the user's rules
are layered on, and a rule set the user can break is one they can also silently
disarm. A phrase typed here goes through the same parse and the same
normalisation as a hand-edited line, so the two cannot mean different things.

**Attachments can be looked at — except the ones that matter.** The index holds
metadata only; the bytes stay in the .eml UltraMail cached, and are read back
out on demand. Executable, script and macro-bearing attachments are **not**
opened and **not** copied anywhere: nothing is written to disk and no button is
offered, only a note saying why. An application whose subject is unwanted mail —
one that classifies partly *on* those attachment types — must not be the thing
that opens them. The refusal is checked twice, against the index row and against
the part the message really carries, so neither a stale index nor a misleading
filename can get one through.

## What Phase 3 leaves out

- **Learning terms, not just senders.** A correction is scoped to the sender
  who prompted it. Inferring rules from a marked block would generalise to the
  next address a spammer rotates to, but auto-generated keyword rules are how a
  detector starts flagging wanted mail; the rule editor keeps that a decision.
- **Attachment scanning.** Risky types are refused by extension and media type.
  Actually inspecting content (an archive's members, a document's macros) is a
  different problem and would want a real scanner behind it.
