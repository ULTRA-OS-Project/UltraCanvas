# UltraCanvasMessageCenter — The Desktop Message Centre

**Status:** Phase 2 of UltraMessage (first UI consumer of the feed)
**Version:** 0.1.0
**Author:** UltraCanvas Framework / ULTRA OS
**Last Modified:** 2026-09-23

`UltraCanvasMessageCenter` is the desktop message centre as one composite
element: every chat message, mail and system notification on the UltraMessage
feed (`Masterfile_modules.md` §13, `Docs/Modules/UltraMessage/README.md`) in
one structured view, for any application to embed — the ULTRA OS desktop,
UltraMail's toolbox, the DemoApp page. It is built from catalogue elements
only and it is a client of the bus and nothing else: it never talks to a
messenger or a mail server.

**File location:** `include/Plugins/UltraMessage/UltraCanvasMessageCenter.h`,
`Plugins/UltraMessage/UltraCanvasMessageCenter.cpp`
**Library target:** `UltraMessageCenter` (links the UltraCanvas library and
`UltraMessage`; built whenever UltraMessage is)
**Demo:** `Apps/DemoApp/UltraCanvasMessageCenterExamples.cpp` (Extended
functionality › Message Centre)
**Tests:** `Tests/UltraMessage/test_messagecenter.cpp` (target
`UltraMessageCenterTests`, in-tree)

---

## 1. What it shows

```
┌ All | Chats | Mail | System ──── (3) ──────── [Search messages] [Refresh] ┐
│ [Unread] [telegram] [mail] [org.ultraos.filer]                              │
├───────────────┬──────────────────────────────────┬─────────────────────────┤
│ Sources       │ ● Ada Lovelace            09:12  │ Ada Lovelace            │
│  All messages │   engine review at 9?            │ Ada Lovelace · telegram │
│  Chats (2)    │ ● Konrad Zuse             Mon    │   · 09:12 · via Demo    │
│   Ada (1)     │   Z3 relay order                 │                         │
│   Engine (1)  │   Downloads               21.09. │ Are we still on for the │
│  Mail (1)     │   Download finished — archive…   │ engine review at 9?     │
│   erika@…     │                                  │                         │
│  System       │                                  │ [Open] [Mark unread]    │
│   Downloads   │                                  │ [Dismiss]               │
└───────────────┴──────────────────────────────────┴─────────────────────────┘
 Connected to /run/user/1000/ultramessage/bus.sock
```

| Part | Element | What it does |
|---|---|---|
| Sections | `UltraCanvasSegmentedControl` | *All / Chats / Mail / System*: the three feed topics, or all of them |
| Unread count | `UltraCanvasBadge` | every unread row, whatever the filters |
| Search | `UltraCanvasTextInput` | matches sender, title, snippet, body and source, case-insensitively |
| Filters | `UltraCanvasChip` | *Unread*, and one chip per service seen (`telegram`, `mail`, an application id) |
| Sources | `UltraCanvasTreeView` | *All messages*, then each section with its sources: conversations, mail accounts, applications, with unread counts |
| Rows | `UltraCanvasListView` | newest first; unread mark (red when urgent), who, what, time — the row delegate paints the two lines |
| Detail | `UltraCanvasLabel` ×3 + `UltraCanvasButton` | title, source · service · time · via, the text; *Open*, *Mark read / unread*, *Dismiss*, and one button per notification action |
| Status | `UltraCanvasLabel` | the bus it is on, or why it is not |

A row is one of:

| Topic | Who (first line) | What (second line) | Source |
|---|---|---|---|
| `messaging.message` | the sender (`· group` for a group chat) | the text's first line | the conversation (`<service>:<conversationId>`) |
| `mail.message` | the sender | the subject | the account |
| `system.notification` | the application | summary — body | the application |

A chat or mail row an adapter *mirrored* from a notification (its body has
`mirrorOf`) replaces the notification's own row, so a Telegram toast is listed
once, as a chat; dismissing it also closes the toast. A message that
`replaces` another takes its row.

## 2. Quick start

```cpp
#include "Plugins/UltraMessage/UltraCanvasMessageCenter.h"
using namespace UltraCanvas;

auto center = CreateMessageCenter("messages", 0, 0, 0, 0);
window->AddChild(center);
center->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

center->onOpen = [](const MessageCenterEntry& e) {
    // "Open" or a double-click: e.section says Chats / Mail / System,
    // e.message is the journaled UltraMsgMessage — launch UltraMail, focus
    // the messenger, ... The element marks the row read first.
};
center->onUnreadCountChanged = [](int unread) { /* the dock badge */ };

center->Connect();   // the per-user bus; hosts the broker when none runs
```

`Connect()` opens an endpoint (`org.ultraos.messagecenter`), reads the last
`GetHistoryLimit()` rows of the three feed topics from the journal and
subscribes to `messaging.message`, `mail.message`, `system.notification`,
`system.notification.dismissed`, `feed.read` and `feed.dismissed`. It installs
the UltraCanvas UI dispatcher (`UltraMsg_UseUltraCanvasApplication`) when none
is installed yet, so deliveries arrive on the UI thread; a program without an
application (a test) pumps `UltraMsg_ProcessPending` itself. A private bus for
a demo or a test is `DefaultConnectOptions()` with `busPath` and
`journalPath = ":memory:"` set.

## 3. API

| Call | Purpose |
|---|---|
| `Connect(options)`, `Disconnect()`, `IsConnected()`, `GetEndpoint()` | the bus; `DefaultConnectOptions()` is the starting point |
| `Refresh()`, `SetHistoryLimit(n)` | re-read the journal (default 500 rows) |
| `SetSection(s)`, `SetSource(key)`, `SetServiceFilter(service)`, `SetUnreadOnly(b)`, `SetSearchText(t)` | what is shown; the controls call these, a host may too |
| `GetUnreadCount()`, `GetVisibleCount()`, `GetEntries()`, `GetSelectedEntry()` | state |
| `Ingest(message)`, `RemoveEntry(id)`, `Clear()` | rows without a bus (a host feeding its own, tests) |
| `MarkRead(id, read)`, `Dismiss(id)`, `InvokeAction(id, actionId)`, `Open(id)` | what the buttons do |
| `onOpen`, `onSelectionChanged`, `onUnreadCountChanged`, `onError` | callbacks |
| `SetStyle(MessageCenterStyle)` | colours, sizes, and `showSources` / `showDetail` / `showSearch` / `showFilters` for a compact embedding |
| `GetListView()`, `GetSourcesTree()`, `GetSectionControl()`, `GetSearchInput()`, `GetDetailPane()`, `GetStatusLabel()` | the parts, for a host that restyles them |
| `BuildEntry(message, out)`, `SectionForTopic(topic)`, `FormatTime(ms, nowMs)` | the translation, static, for tests |

What the element posts back, so sources and other feeds stay in step:

| The user | Journal | Bus |
|---|---|---|
| selects or reads a row | `UltraMsg_MarkRead` | `feed.read {messageId}` |
| marks it unread | `UltraMsg_MarkUnread` | — |
| dismisses a row | `UltraMsg_Dismiss` | `feed.dismissed {messageId}`; for a notification (or a mirror of one) also `system.notification.dismissed {notificationId}`, which the adapter turns into `NotificationClosed` |
| presses a notification's button | marks read | `system.notification.action {notificationId, actionId}` |
| opens a row | marks read | `onOpen`, else the notification's first action |

Selecting a row marks it read, as a mail client does; *Mark unread* undoes it.

## 4. Threading and cost

Deliveries run on the UI thread. `Refresh`, `MarkRead`, `Dismiss` and the
posts are short blocking round trips to the local broker (a Unix socket or
named pipe on the same machine); they are called from the UI thread. Every
row is kept in memory; with the default history of 500 rows that is trivial,
and the ListView virtualises what it paints.

## 5. Rules kept

No control is painted by hand: the only direct drawing is the ListView row
delegate, which is the view's licence (`UltraCanvasUIElements.md` § *The two
legitimate exceptions*). Everything around it — segments, chips, the search
field, the tree, the labels, every button — is a catalogue element with its
keyboard, focus and theming behaviour.
