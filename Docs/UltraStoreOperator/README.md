# UltraStoreOperator

The operator's side of the UltraStore marketplace: the program the ULTRA
OS Project runs as the operator of a store in which other publishers
(merchants) open shops. It is a client of the marketplace service
([`Server/UltraStoreMarket`](../../Server/UltraStoreMarket/README.md)):
the merchants and where they stand with the terms, the review of what they
submit and the publish that releases it into the one signed catalog, the
reports from users and merchants, the notices and the strikes, the terms
and the enforcement policy, the audit log - and, with its own token, of
the account service ([`Server/UltraStoreAccount`](../../Server/UltraStoreAccount/README.md)):
the people with an account, their requests, the actions on them. One program, `ultrastoreop`:
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
| How does the program speak to the service? | **With the operator's bearer**, `Authorization: Bearer <token>`: the session a sign-in gave (an operator account at the service, *Who is signed in?*), or the service's static `ULTRASTORE_MARKET_OPERATOR_TOKEN`, the bootstrap admin; over HTTPS; the HTTP call is the merchant backend's (`Market.h`, `MarketCall`), extended to `PUT`, so the tests use a fake service and nothing touches the network in a test. An action the service refuses (a release of a merchant not active, a decision in the wrong state) comes back as its `error_description`. |
| Where are the settings? | **The program's own file**, `settings.json` under the data folder (`UltraStoreOperator/`), readable by the user alone: the service's URL, the token (the static one, or the session), the operator's name and role, the account service's URL and token. `ULTRASTORE_OPERATOR_URL`, `_TOKEN`, `_NAME`, `_ACCOUNT_URL` and `_ACCOUNT_TOKEN` in the environment override it; `--url` and `--token` on the command line override both. The name is shown in the window and is the assignee a report gets by default. |
| Who is signed in? | **An operator account with a role** (2026-11-19; the store's README, *The operator tier*, *Who sits at the operator's window?*). The Settings page's *Sign in* (or `ultrastoreop signin <email>`) sends the email and the password to the marketplace service, which answers a signed session that becomes the settings' token, with the account's name and role; the sidebar says *Signed in as Alex, admin*. The session lasts the service's hours (12) and reaches the account service too, which verifies it with the secret the two services share, so `account_token` is for the bootstrap admin only. The roles: `admin` everything, the accounts included; `reviewer` the review and the publish; `support` the reports, the notices, acting for a user; `auditor` reads, changes nothing. The service refuses what a role does not allow (its `error_description` names the role); the window hides the admin's controls on the *Operators* page from the others. With the static token the window is the bootstrap admin, whose one job is the first admin account. *Sign out* forgets the session here; it ends at the service by itself. |
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
  ui/OperatorUsersPage.cpp     Users, on the account service
  ui/OperatorOperatorsPage.cpp the Settings page's sign-in, who is signed in, the Operators page
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
| Merchants | the applications for a shop waiting for a decision (the name, the contact, the email, the website, the description, the key the application proved); every merchant with its standing, strikes, terms accepted of in force; a merchant's file: the profile, the key, the submissions with their state, the acceptances and the suspensions | *Approve* an application (the merchant id to give, or one made; a message) or *Reject* with the reason the applicant reads; *Open file*; a notice or a warning to the merchant (the kind, the subject, the clause, the text), *Suspend the shop* (the clause, the reason, the days; 0 until reinstated), *Reinstate*; *Register* a merchant by hand with its name, id, contact and the public half of its key |
| App review | the catalog's last publish and how many released apps go into the next one; the submissions by *Waiting*, *Changes requested*, *Released*, *All*; one submission: the automatic checks with their outcomes, the card as the store will show it, the packages, the decisions so far | *Publish the catalog*; *Start the review*, *Request changes*, *Approve and release* (a sequence of your own, or the next), *Reject*, each with the message to the merchant; a release of a submission with a failed check needs a message saying why |
| Reports | the reports by *Open*, *Mine*, *Closed*, *All*; one report: where it came from, the reporter (the operator's to see), the text and the attachments, the findings and the outcome once closed | *Assign*; *Resolve as upheld* or *Dismiss*, with the findings for the file and the outcome line for the reporter; *Open a report from mail* |
| Terms and notices | the versions with who accepted them and which is current; the merchants not on the version in force; the enforcement policy; the notices sent | *Publish this version* (the text, what changed, the day it comes into force); *Save the policy*; *Send* a notice to named merchants or everyone, a warning with its clause |
| Users | the design's `SuperUsers`, on the account service: the counts (accounts and two-factor, the archive requests and those due, the deletion requests, the suspended), the users by *All*, *Active*, *Suspended*, *Deleting*, *With a request* with a search; one user's file: the devices, the licences without their tokens, the archives, the notices, the history and the sign-ins, and what the operator cannot see | *Open file*; a notice (the kind, the subject, the text); *Sign out every device*; *Reset two-factor (identity checked)*; *Release* an archive; *Suspend the account* (the reason, the days) or *Reinstate*; *Delete now* for a deletion the person asked for - each with the reason, which goes into the account service's audit log |
| Audit log | the latest rows, newest first | *Verify the chain*: the service walks the hashes and names the first row that does not fit |
| Settings | the service's URL, the token, the operator's name; the account service's URL and its operator token | *Save*; *Check the service* (`GET /healthz`); *Check the account service* |

## The command line

```
ultrastoreop [--settings <file>] [--url <base>] [--token <t>] <command>

settings [set url|token|name|account_url|account_token <value>]   the settings file
health                                                   the service's healthz
merchant list | show <id>                                every merchant; one with its file
merchant add <name> --key <hex> [--id <id>] [--contact <c>]
merchant set <id> [--name <n>] [--contact <c>] [--key <hex>]   the key given is the rotation
merchant applications [--state applied|approved|rejected]    the applications for a shop
merchant approve <application> [--id <merchant-id>] [--message <m>] | reject <application> --message <m>
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
users list [--state active|suspended|deleting|requests] [--q <text>] | show <id> | stats
users notice <id> <subject> [--kind notice|security|warning|information_request] [--text <t|@file>]
users signout <id> [--reason <r>] | reset-2fa <id> [--reason <r>]
users suspend <id> [--reason <r>] [--days <n>] | reinstate <id> [--reason <r>]
users delete <id> [--reason <r>]                         a deletion the person asked for, carried out now
users release-archive <archive-id> | maintenance | audit [--since <id>] [--limit <n>] | audit verify
```

The `users` commands drive the account service, named by `account_url`
and `account_token` in the settings (`ULTRASTORE_OPERATOR_ACCOUNT_URL`
and `_ACCOUNT_TOKEN` in the environment); without them the Users page
says so and the commands refuse before any call.

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

2. **The users page** (the design's `SuperUsers`), on the account service
   with its own token: the counts, the states and the search, the file,
   the notice, sign out everywhere, reset two-factor, the archive released,
   suspend and reinstate, the deletion on request; `ultrastoreop users`.
3. **The applications for a shop** on the Merchants page: approve (the
   merchant registered with the key the application proved) or reject
   with a reason; `ultrastoreop merchant applications | approve |
   reject`.
4. **Operator accounts with roles** (the design's `SuperOperators`): the
   sign-in on the Settings page with the session as the token and the role
   in the sidebar, the *Operators* page (the accounts, a new one, a role
   changed, disable and enable, one's own name and password), the account
   service on the same session; `ultrastoreop signin | signout | whoami |
   operator list | add | set | disable | enable`. Next: the payouts page,
   which needs the payment service's figures per merchant.