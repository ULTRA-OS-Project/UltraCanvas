#### 2026-11-20 *0.1.3*
- **Two-factor sign-in and the forgotten password**: the authenticator set up and turned on from *Your own account* on the Operators page (the recovery codes shown once), the sign-in's *Authenticator code* field, *Forgot the password* on Settings with the mailed code and the new password, an admin's *Reset 2FA* on another account; `ultrastoreop signin --code`, `twofactor setup | enable | disable | recovery-codes`, `password forgot | reset`, `operator reset-2fa <id>`.
- **Download build**: a package the merchant uploaded into the package store is fetched per package from the submission (into the builds folder under the data folder); one not yet uploaded is marked so; `ultrastoreop review download <submission> [--to <folder>]`.

#### 2026-11-20 *0.1.2*
- **Sign in with an operator account**: the Settings page's *Sign in* (email and password at the marketplace service) keeps the session as the token, the name and the role in the settings; the sidebar says *Signed in as Alex, admin*; the account service is reached on the same session, so `account_token` is for the bootstrap admin only. The *Operators* page: the roles, the accounts with a role picker and disable/enable (an admin's), a new account, one's own name and password. `ultrastoreop signin <email> [--password]`, `signout`, `whoami`, `operator list | add <email> <role> | set <id> | disable <id> | enable <id>`; `settings set account_url|account_token` work as the usage said; `--page users|operators`.

#### 2026-11-19 *0.1.1*
- **The applications for a shop** on the Merchants page: the queue with the name, contact, email, website, description and the key the application proved; *Approve* with the merchant id to give (or one made) and a message, *Reject* with the reason the applicant reads; the merchant file shows the profile. `ultrastoreop merchant applications [--state] | approve <application> [--id] [--message] | reject <application> --message`.
- **The Users page** (the design's `SuperUsers`), on the account service (`Server/UltraStoreAccount`) with its own token - `account_url` and `account_token` in the settings, `ULTRASTORE_OPERATOR_ACCOUNT_URL` and `_ACCOUNT_TOKEN` in the environment: the counts (accounts and two-factor, the archive requests and those due, the deletion requests, the suspended), the users by state with a search, one user's file (the devices, the licences without their tokens, the archives, the notices, the history, the sign-ins) and the actions with their reason - a notice, sign out every device, reset two-factor after the identity was checked, release the archive, suspend or reinstate, delete on request. `ultrastoreop users list | show | stats | notice | signout | reset-2fa | suspend | reinstate | delete | release-archive | maintenance | audit`; *Check the account service* in Settings. The artboard now says on its face what of it the service backs.

#### 2026-11-11 *0.1.0*
- **The operator's window and command line.** `ultrastoreop`, the
  marketplace service's operator side (the design's `Super*` pages): the
  Dashboard with the numbers, the review queue, the open reports, the
  compliance lines and the latest operator actions; Merchants with a
  merchant's file (its submissions, acceptances and suspensions), a notice
  or a warning sent to it, a suspension and a reinstatement, and a new
  merchant registered with its key; App review with the queue by state,
  a submission's automatic checks, the card as the store will show it,
  the packages, the decisions so far and the decision - start the review,
  request changes, approve and release, reject - and the publish that
  signs the catalog from the released submissions; Reports with the
  reporter the operator alone sees, the assignment, the findings and the
  outcome line that closes one, and a report opened from mail; Terms and
  notices with the versions and who accepted, a new version, the
  enforcement policy, a notice to any or every merchant, and the notices
  sent; the Audit log with its chain verified; Settings with the service's
  URL, the operator token and the operator's name. The same from the
  command line: `merchant`, `app`, `review`, `released`, `publish`,
  `key`, `report`, `notice`, `terms`, `policy`, `audit`, `health`,
  `settings`.
