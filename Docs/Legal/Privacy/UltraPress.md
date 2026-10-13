# Privacy declaration — UltraPress

**Version 0.1 (draft for legal review) · 13 October 2026**

This declaration explains what personal data the **UltraPress** application
processes, where that processing happens, which third parties it contacts and
why, and what rights you have under the General Data Protection Regulation
(GDPR). It is published at www.ultraos.eu and covers UltraPress 0.1 - the
`ultrapress` command line - on Linux, Windows, macOS and ULTRA OS. It does
not cover the website itself, other ULTRA OS applications, any ULTRA OS
online service, or **the websites you build with UltraPress**: a site you
publish is yours, served by the host you chose, and needs a declaration of
its own.

> **Review status.** This text was drafted from UltraPress's source code
> (`Apps/UltraPress`) and documentation. The items in square brackets (`[…]`)
> must be filled in, and the whole text must be reviewed by a lawyer before it
> is published. Later versions of UltraPress add features that contact
> servers - the editor's publishing onto a web host, the server tier with
> forms, members, shop and newsletter, the website-builder assistant - and
> each of those changes this declaration before it ships.

## 1. Who is responsible

ULTRA OS Development GmbH
An den Klippen 23
57462 Olpe
Germany

E-mail: [privacy@ultraos.eu]
Managing director(s): […]
Commercial register: […]

The company has not appointed a data protection officer, because it is not
required to [confirm with legal review].

## 2. The short version

- UltraPress 0.1 is a command-line program that runs **on your own
  computer**. It reads a site folder you give it and writes files into a
  folder you name. Nothing leaves your device.
- It contacts **no server at all**: not ours, not anyone's. It has no
  account, no sign-in, no update check, no telemetry and no advertising.
- The pages it writes contain only what you put into the site folder. Where
  the pages reference a server - a contact form, a newsletter sign-up - that
  server is one you will run or choose; this version runs none.
- ULTRA OS Development GmbH receives no data from your use of UltraPress and
  holds none.

## 3. What UltraPress stores on your device

| Data | What it is | Where |
|---|---|---|
| The site folder | `site.json`, `theme.json`, `menus.json`, `pages/*.json` and `media/`: the site's name, address, owner, menus, pages, pictures and downloads, all written by you | the folder you name, e.g. `ultrapress new mysite` |
| The published site | one HTML file per page, `sitemap.xml`, `robots.txt`, `feed.xml`, `.htaccess` and a copy of `media/` | the output folder you name, e.g. `ultrapress publish mysite out` |

UltraPress writes no settings, caches, logs or history files of its own. Its
messages go to the terminal it was started from.

The site folder and the published site contain whatever you wrote into
them. If you put personal data there - your own name and address in the
site's owner field, a person's picture, an e-mail address on a contact page -
it is in those files and in every copy of the published site, including the
copy on your web host once you upload it. UltraPress does not read it for any
purpose but writing the pages.

### 3.1 How sensitive data is protected

UltraPress stores no passwords, keys or tokens. The files it writes are
protected by your operating system's file permissions and nothing else; they
are meant to be published.

### 3.2 Who can read this data

You, and anyone you give the folders to. UltraPress does not send them
anywhere.

### 3.3 Our role

All processing happens on your device under your control. ULTRA OS
Development GmbH is not the controller of that processing within the meaning
of Art. 4(7) GDPR; you are. For the website you publish, you are the
controller towards its visitors, and your web host is your processor under
its own terms.

## 4. Data UltraPress sends to servers

None. UltraPress 0.1 opens no network connection.

## 5. Other servers UltraPress contacts, and why

None.

The HTML pages UltraPress writes reference, by address only, three things a
visitor's browser may request once the site is online: the site's own
pictures and downloads (from your host), a form endpoint at `/api/form/<id>`
and a newsletter endpoint at `/api/newsletter/subscribe` (both on your host,
and answered only by a server you run - this version includes none), and,
when you ask for it with `--renderer`, a script file you name. None of these
is a server of ULTRA OS Development GmbH, and none exists unless you put it
there.

## 6. What we do not do

UltraPress does not:

- create or require an account with ULTRA OS Development GmbH;
- send usage statistics, analytics, crash reports or diagnostic data to us or
  anyone else;
- check for updates by contacting our servers;
- show advertising or contain third-party advertising or tracking code, in
  the program or in the pages it writes;
- put analytics, tracking pixels, cookies or third-party scripts into the
  pages it writes;
- share, sell or otherwise disclose any of your data.

Because no data reaches us, we hold no personal data about UltraPress users
and cannot identify you from your use of the application.

## 7. Your rights

Under the GDPR you have the rights of access (Art. 15), rectification
(Art. 16), erasure (Art. 17), restriction of processing (Art. 18), data
portability (Art. 20) and objection (Art. 21), and the right to lodge a
complaint with a supervisory authority (Art. 77). The authority responsible
for us is the Landesbeauftragte für Datenschutz und Informationsfreiheit
Nordrhein-Westfalen, Kavalleriestraße 2–4, 40213 Düsseldorf, Germany.

Because UltraPress keeps your data only in folders on your own device, you
exercise these rights directly: the files are plain JSON and HTML that you
can read, change, copy and delete yourself. For the website you publish,
your visitors exercise their rights towards you and your web host.

For any question about this declaration, write to [privacy@ultraos.eu].

## 8. Children

UltraPress is a tool for building websites and is not directed at children.
Since it collects no data for us, no age verification takes place.

## 9. Changes to this declaration

We update this declaration whenever UltraPress's data flows change: the next
changes already planned are publishing onto a web host from the editor
(your host's credentials, kept in the system's credential store), the server
tier (visitors' form entries, member accounts, orders and newsletter
subscriptions on a server you run) and the website-builder assistant (your
answers sent to an AI provider you choose). The version and date at the top
identify the current text; earlier versions are available in the project's
source repository.
