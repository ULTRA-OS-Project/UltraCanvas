# UltraStoreOperator

The operator's side of the UltraStore marketplace: the program the ULTRA
OS Project runs as the operator of a store in which other publishers
(merchants) open shops. It is a client of the marketplace service
([`Server/UltraStoreMarket`](../../Server/UltraStoreMarket/README.md)):
the merchants and where they stand with the terms, the review of what they
submit and the publish that releases it into the one signed catalog, the
reports from users and merchants, the notices and the strikes, the terms
and the enforcement policy, the audit log. One program, `ultrastoreop`:
the window when started plainly, the commands when given one. The design
it follows is the `Super*` pages of
[`Docs/UltraStore/design/README.md`](../UltraStore/design/README.md);
where its records live is settled in the store's README, *The operator
tier*.

The store itself is [`Docs/UltraStore/README.md`](../UltraStore/README.md);
a merchant's program is [`Docs/UltraStoreAdmin/README.md`](../UltraStoreAdmin/README.md).

## Decisions

| Question | Decision |
|---|---|
| Where is the state? | **Nowhere here.** Every record is the service's (its MySQL database); the window and the command line hold nothing but the settings and what they last fetched. Two operators at two machines see the same queue, and the audit log is one. |
| How does the program speak to the service? | **With the operator token**, `Authorization: Bearer <token>` (the service's `ULTRASTORE_MARKET_OPERATOR_TOKEN`), over HTTPS; the HTTP call is the merchant backend's (`Market.h`, `MarketCall`), extended to `PUT`, so the tests use a fake service and nothing touches the network in a test. An action the service refuses (a release of a merchant not active, a decision in the wrong state) comes back as its `error_description`. |
| Where are the settings? | **The program's own file**, `settings.json` under the data folder (`UltraStoreOperator/`), readable by the user alone: the service's URL, the token, the operator's name. `ULTRASTORE_OPERATOR_URL`, `_TOKEN` and `_NAME` in the environment override it; `--url` and `--token` on the command line override both. The name is shown in the window and is the assignee a report gets by default; the service records every action as `operator`. |
| What asks before it acts? | **Everything that cannot be taken back**: a release, a rejection, a warning (it is a strike), a suspension, a publish, a new terms version, a notice to every merchant, a dismissal. A request for changes, an assignment, a reinstatement and a resolution ask nothing: each is answered or undone by the next step. |
| Who sees the reporter? | **The operator only**, as the service already holds it: the report's page shows the reporter beside a line saying so; the outcome the operator writes is the one line the reporter and the merchant learn. |

## The pieces

```
Apps/UltraStoreOperator/
  core/Operator.h/.cpp         UltraStoreOperatorCore (static), on UltraStoreAdminCore: the settings file,
                               the records (Merchant, Submission, Report, Notice, TermsVersion, Policy,
                               AuditRow, Released, PublishReport), OperatorClient over the service's API
  cli/Commands.h/.cpp          ultrastoreop <command>: Cli::Run(args)
  ui/OperatorTheme.h           the admin backend's theme with the operator tier's marks: the dark sidebar,
                               the stat cards, the state pills
  ui/OperatorWindow.h/.cpp     the shell, the jobs on the worker, the Dashboard, the Audit log, the Settings
  ui/OperatorPages.cpp         Merchants, App review, Reports, Terms and notices
  ui/main.cpp                  the window, or the commands
Tests/UltraStoreOperator/      the client against a fake service; the settings file; the age of a timestamp
Docs/UltraStoreOperator/       this page, the changelog (its first line is the version)
```

## The window

`ultrastoreop` started plainly (or with `--settings <file>` and `--page
<id>`) opens the window; `Apps/UltraStoreOperator/ui/`. It wears the shop
design's tokens as the admin backend does (`AdminTheme.h`: a white ground,
ink text, the UltraStore blue, Manrope over Inter, pills) under the dark
sidebar the design gives the operator tier, so nobody mistakes it for a
merchant's backend. The sidebar carries the review queue's count and the
open reports' count as badges, and under them the catalog sequence, the
merchants and the apps live, the service and who is signed in. Every page
asks the service on a worker thread, one request at a time, and says what
it is doing in the status line; an answer the service refuses is an alert.
Every text goes through `Tr`.

| Page | Shows | Does |
|---|---|---|
| Dashboard | the merchants (suspended, paused by the terms), the submissions waiting with the oldest's age and the failed checks, the open reports with the urgent ones (malware), the merchants not on the terms in force, the catalog sequence and the apps the next publish carries; the review queue; the open reports; the compliance lines; the latest operator actions from the audit log | *Start reviewing*, *Open* on a queue row or a report |
| Merchants | every merchant with its standing, strikes, terms accepted of in force; a merchant's file: the key, the submissions with their state, the acceptances and the suspensions | *Open file*; a notice or a warning to the merchant (the kind, the subject, the clause, the text), *Suspend the shop* (the clause, the reason, the days; 0 until reinstated), *Reinstate*; *Register* a merchant with its name, id, contact and the public half of its key |
| App review | the catalog's last publish and how many released apps go into the next one; the submissions by *Waiting*, *Changes requested*, *Released*, *All*; one submission: the automatic checks with their outcomes, the card as the store will show it, the packages, the decisions so far | *Publish the catalog*; *Start the review*, *Request changes*, *Approve and release* (a sequence of your own, or the next), *Reject*, each with the message to the merchant; a release of a submission with a failed check needs a message saying why |
| Reports | the reports by *Open*, *Mine*, *Closed*, *All*; one report: where it came from, the reporter (the operator's to see), the text and the attachments, the findings and the outcome once closed | *Assign*; *Resolve as upheld* or *Dismiss*, with the findings for the file and the outcome line for the reporter; *Open a report from mail* |
| Terms and notices | the versions with who accepted them and which is current; the merchants not on the version in force; the enforcement policy; the notices sent | *Publish this version* (the text, what changed, the day it comes into force); *Save the policy*; *Send* a notice to named merchants or everyone, a warning with its clause |
| Audit log | the latest rows, newest first | *Verify the chain*: the service walks the hashes and names the first row that does not fit |
| Settings | the service's URL, the token, the operator's name | *Save*; *Check the service* (`GET /healthz`) |

## The command line

```
ultrastoreop [--settings <file>] [--url <base>] [--token <t>] <command>

settings [set url|token|name <value>]                    the settings file
health                                                   the service's healthz
merchant list | show <id>                                every merchant; one with its file
merchant add <name> --key <hex> [--id <id>] [--contact <c>]
merchant set <id> [--name <n>] [--contact <c>] [--key <hex>]   the key given is the rotation
merchant suspend <id> [--clause <c>] [--reason <r>] [--days <n>] | reinstate <id> [--reason <r>]
app suspend <app-id> [--clause <c>] [--reason <r>] [--days <n>] | reinstate <app-id> [--reason <r>]
review list [--state <s>] [--merchant <id>] [--app <id>] | show <submission>
review start | release | changes | reject <submission> [--message <m>] [--sequence <n>]
released                                                 what the next publish carries, the last publish's sequence
publish                                                  the catalog from the released submissions, signed into the publish folder
key                                                      the operator's signing key (public half) and the trusted keys
report list [--state <s>] | show <id>
report open app|release|merchant <target> <kind> <text|@file> [--reporter <r>]
report assign <id> <assignee> | resolve <id> [--findings <f>] [--outcome <o>] | dismiss <id> [--findings <f>] [--outcome <o>]
notice list | send <kind> <subject> --to <merchant|*> [--to ...] [--text <t|@file>] [--clause <c>]
terms [add <in-force-from> <text|@file> [--changes <c>] [--version <n>]]
policy [set <key> <value>]                               strike_expiry_days strikes_before_suspension suspension_days
                                                         answer_deadline_days clauses_that_suspend ("9.1 12", "-" for none)
audit [--since <id>] [--limit <n>] | audit verify
```

The command line stays English: it runs before any language is loaded.
Notice kinds: `announcement`, `reminder`, `information_request`,
`warning` (names its clause, counts as a strike), `suspension`. A
submission's decisions: `start` (`start_review`), `release`, `changes`
(`request_changes`), `reject`.

A marketplace opened, end to end, beside a merchant's `ultrastoreadmin`:

```
ultrastoreop settings set url https://market.example
ultrastoreop settings set token <the service's operator token>
ultrastoreop settings set name Alex
ultrastoreop terms add 2026-01-01 @terms.md --changes "first public version"
ultrastoreop merchant add "Pixelwerk Studio" --id pixelwerk --key $(cat pixelwerk.pub) --contact https://pixelwerk.example
# the merchant: ultrastoreadmin settings set market.url / market.merchant_id, market terms accept 1 --by ..., market submit <app-id>
ultrastoreop review list
ultrastoreop review start s-00000001
ultrastoreop review release s-00000001 --message "Looks good and runs on all three platforms"
ultrastoreop publish                                     # catalog.json and its signature into the publish folder
ultrastoreop report open app org.pixelwerk.notenkiste malware @mail.txt --reporter "u@example"
ultrastoreop report assign r-00000001 Alex
ultrastoreop report resolve r-00000001 --findings "reproduced on a clean VM" --outcome "The app was withdrawn"
ultrastoreop notice send warning "Support unanswered" --to pixelwerk --clause 7.2 --text "Answer within 5 working days"
ultrastoreop audit verify
```

## Building and testing

`UltraStoreOperatorCore` and `ultrastoreop` are built wherever the admin
backend's core is (`BUILD_ULTRASTOREOPERATOR`, on by default; off on the
phones and in the browser with the store). The tests,
`UltraStoreOperatorCoreTests` (`ULTRACANVAS_BUILD_ULTRASTOREOPERATOR_TESTS`),
run the client against a fake service that answers from a map: the bearer
header on every request, the bodies as the service's models read them, the
records parsed from its answers, the publish report, the settings file and
its permissions, the age of a timestamp. The service's own tests
(`Server/UltraStoreMarket/tests`) cover the other side of every call.

## Milestones

1. **0.1.0** the window and the command line on the service: the
   merchants, the review and the publish, the reports, the notices, the
   terms and the policy, the audit log. Next: the packages uploaded into
   the operator's package store with a submission (the review's *Download
   build*), the review pipeline behind the checks marked *not run* (the
   scans, the entry, the smoke run, the listing), and the payouts and
   users pages of the design, which need the payment service's figures
   per merchant.
