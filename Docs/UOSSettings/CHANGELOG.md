#### 2026-11-22 *0.28.0*
- **Desktop → Tool applications.** A new row chooses whether UltraClipboard,
  UltraSnip and UltraToDo show only on UltraDesktop's tool group (the
  default) or on the taskbar too (`toolAppsOnTaskbar`).
- **The Look page's controls start at one x.** Each row's caption was as
  wide as its own text, so every control began at a different place.
  The captions now share one column as wide as the widest of them,
  measured in the font they are drawn in (`FitCaptionColumn`, as on the
  Desktop page), in every language.
- **The preview shows all of itself.** Its label stands on a line of its
  own above the button, field and choice: on one line a translated label
  ("Eine Beschriftung im Aussehen") was cut short at a fixed 160 px, and
  at full width pushed the choice past the panel's edge.
- **The page scrolls** instead of cutting off the notes at its foot.
- **The Translations page moves out, into UOS-Localisation.** The
  localisation editor was squeezed into a settings page about 620 px wide;
  it is now a program of its own, UOS-Localisation, laid out for a whole
  window. The *Language* page's new **Translate applications...** button
  starts it in the language chosen there, and says so on the page when it
  cannot be found. The *Translator* name stays on the *Language* page
  (Language.conf), and UOS-Localisation writes it into what it saves.

#### 2026-11-21 *0.27.0*
- **Why the look could not be saved.** Appearance's "Could not write
  ...\Appearance.conf." now gives the system's reason after the path (the file
  in use, access denied). The Desktop page, which also saves the light / dark
  choice, used to drop that failure silently and report "Saved"; it now says
  the look could not be saved, with the reason.
- **The settings tables follow a change of look.** The tables of Last used
  folder, Notification registrations and Trusted certificates took their
  background, header and rule colours once, when the page was built, while
  the labels in them follow a theme change - so after a switch of look a
  dark table stayed in a light window, its text turned dark on dark. The
  tables, their headers, rows and scrollbars now take the theme's colours
  again after every change (`FollowKitLook`).
- **The tables and the certificate page are translated.** The column
  headers (Application, Click starts, Opens in, Last used folder, Device,
  Address, Key), the "Click starts" cells, the certificate page's notes and
  status line, the Last used folder notes and "Saved to" went into the
  window untranslated; they go through `Tr` now, translated in all thirty
  languages (machine drafts). The activator's CLSID moved from the cell to
  its tooltip. pt-BR also gets the eight older strings it lacked.

#### 2026-11-16 *0.26.0*
- **The Desktop page's captions and three switches can be translated.**
  The row captions (Taskbar, Theme, Dropped files ... At sign-in), the
  Global / Individual and One common folder / Their own folders switches of
  File dialogs and the Colour / Black and white / System switch of Emoji
  were written straight into the window and stayed English in every
  language - beside notes that named them in the reader's own. They go
  through `Tr` now and are translated in all thirty languages, in the
  wording the notes quote.
- **The Sub-categories choices are translated too** ("Never", "Above 8
  applications"), and the Desktop page's caption column is as wide as its
  widest caption, measured in the font it is drawn in (between 130 and
  320 px at 12 pt), so a translated caption ("Virtuelle Schreibtische",
  "Abgelegte Dateien") is not cut short as it was in a column 130 px wide.
- **The info bar's icons and the programs that start with the desktop are
  named in the reader's language.** Their names come from the desktop's
  own source, so UOS-Settings now takes the desktop's catalogue as a second
  set (`UltraLocalisation::AddSet("UltraDesktop")`); the info bar's
  checkboxes passed a variable to `Tr`, which no catalogue ever held.
- **UOS-Settings is translated throughout.** The thirty other languages had
  a quarter of the window's texts - the tree and the Language page - and
  showed the rest in English: the Desktop, File dialogs, Appearance,
  Notifications and Devices pages with their notes. The 79 missing texts
  are now in every language's catalogue (machine drafts, as the rest,
  until a person reviews them).

#### 2026-11-15 *0.25.0*
- **Appearance > Look has a Light text row.** *Firm*, the default, draws
  light text on a dark ground a little stronger in every application, so a
  dark window reads as clearly as a light one; *Plain* draws every text
  once. Written to `Appearance.conf` as `lighttext=`; a running application
  follows within a moment.

#### 2026-11-14 *0.24.0*
- **Desktop > Battery**: *Icon and percentage* (the default) or *Icon
  only* - whether UltraDesktop's battery shows its percentage as a badge.
- **Dark look**: the scrolled pages (Desktop, Info bar) and the tables
  (Last used folder, Trusted certificates, Notification registrations) take
  the theme's scrollbars instead of a light default.
- **Desktop > Theme is the system look.** It writes Appearance.conf, the
  same as *Appearance > Look*, and offers *As the system* too: the
  desktop's bars and every application turn dark or light together.
- **Desktop > Info bar**: a tick box for each icon of UltraDesktop's info
  panel, and the tray icons; unticked, the icon leaves the desktop's bar.
- **`--page <page>`** opens the window on that page (`desktop/info-bar`), and
  a running UOS-Settings shows it when started so - UltraDesktop's *Info bar
  icons...* uses it.
- The note on the RAM disc says what `ultrafiler` means.
- **Desktop > RAM disc says what it takes**: its tooltip explains
  `ultrafiler` (UltraFiler shows its RAM disc, or offers to make one), a
  folder, and empty (no RAM disc button).

#### 2026-11-12 *0.23.0*
- **The desktop's look, and every text translatable.** The window is light
  or dark as the desktop is - or as its own *Appearance > Look* page says -
  in the desktop's font and size: its colours and sizes are the settings
  page kit's, which are the framework theme's roles now. Every text the
  pages show goes through `Tr`, the notes and status lines included, and
  the English set (`media/locale/en/UOS-Settings.json`) is extracted from
  them; the *Language > Translations* page edits it like any other.

#### 2026-11-11 *0.22.0*
- **Appearance > Look** - the look every application takes, over what the
  computer says: *System*, *Light* or *Dark*; the accent (*System*, a named
  colour, or any `#rrggbb`); the font (*System* or an installed family) and
  its size; the text scale (100 % to 200 %); the contrast. A preview strip
  under the rows is styled from the theme the choices give. Written at once
  to `Appearance.conf` in the settings folder; a running application follows
  it within a moment.

#### 2026-11-11 *0.21.0*
- **Network > Proxy.** The proxy every ULTRA OS application connects
  through: the system's (the default), none, or an HTTP or SOCKS proxy with
  the hosts reached directly and a sign-in, set in the Proxy settings
  dialog and used by every application from its next connection.

#### 2026-11-09 *0.20.0*
- **Desktop: a *Theme* choice** - *Dark* or *Light* bars for UltraDesktop
  (`theme` in `desktop.json`), under *Taskbar*. A running desktop follows it
  within a second.
- **Desktop: *Dropped files*** - where files dropped on UltraDesktop's
  wallpaper go: the Sticker board's Unsorted category or the Desktop folder,
  shown as icons (`dropFilesOn`).

#### 2026-11-07 *0.19.0*
- **Built on the framework's settings page kit** (`UltraCanvasSettingsPageKit`,
  framework changelog `settings-page-kit-options`): the window, the tree of
  pages, the bottom bar and the page switching come from it, where they were
  this window's own; the ULTRA OS logo is the kit's sidebar header. Nothing
  changes on screen.

#### 2026-11-06 *0.18.0*
- **Desktop: the Sticker board's cards and hover preview.** Two more rows on
  the *Desktop* page: *Sticker board cards* (*Text only*, *Thumbnail and
  text*, *Thumbnail and text in a row*) and *Hover preview* (*Small*,
  *Medium*, *Large*). The desktop takes a change within a second.

#### 2026-11-06 *0.17.0*
- **Desktop > Start with the desktop** lists UltraClipboard,
  UltraNotifications and the Sticker board as well (UltraDesktop's own
  list): the two programs start with no window, the board is made ready
  unseen.

#### 2026-11-05 *0.16.0*
- **UOS-Settings takes the Computer base set.** Its catalogue names
  `Computer` under `@bases`, as the localisation proposal's Appendix B
  assigns it: "Notifications" and "System" now come from that set, worded
  as UltraFiler and the other system tools word them, instead of from a
  copy in UOS-Settings' own catalogue.

#### 2026-11-05 *0.15.0*
- **Settings in every language.** UOS-Settings' own catalogue
  (`media/locale/<lang>/UOS-Settings.json`) now exists in all thirty
  languages: its page names (Appearance, Notifications, Translations,
  Trusted certificates ...), the language page with its notes, and the
  localisation editor's translator field showed in English whatever the
  language - on the very page where the language is chosen.

#### 2026-11-03 *0.14.0*
- **Language > Translations: the localisation editor.** A page under
  Language hosts `UltraCanvasLocalisationEditor`: every set an
  application's texts come from - its own set, the base sets it takes, the
  standard set - in one grid for any application and any language, with
  the Sets picker, promote / demote / merge, Undo, a compare column, and a
  save into the source tree where the catalogues are writable or into the
  UltraCanvas settings folder otherwise (`Docs/UltraCanvas/UltraCanvasLocalisationEditor.md`).
  The translator's name from the Language page goes into the files it saves.
- **The Language and Translations pages, the page tree and the window's
  buttons go through `Tr`**, so they follow the chosen language; the
  extraction writes UOS-Settings' own English set
  (`media/locale/en/UOS-Settings.json`). The other pages follow in their
  own pass.

#### 2026-11-02 *0.13.0*
- **Language: the language every application shows.** A new top-level page
  chooses the language for all UltraCanvas applications - *System*, or any
  language a translation of the standard set ships for, listed by its own
  name - and writes `Language.conf` in the UltraCanvas settings folder,
  which every application reads when it starts (UltraLocalisation,
  `Docs/Modules/UltraLocalisation/README.md`). A *Translator* field keeps
  the name the localisation editor will write into the catalogues it saves.

#### 2026-11-01 *0.12.0*
- Desktop: the empty Wallpaper field names the default it stands for,
  "ULTRA OS A (the default picture)".

#### 2026-11-01 *0.11.0*
- **Desktop: the Apps window's view, sub-categories and Recent count.**
  Three new choices on the Desktop page: the Apps window's view (tiles, or
  the categories and a list), above how many applications a category is
  split into its sub-categories (or never), and how many applications
  Recent shows.

#### 2026-11-01 *0.10.0*
- **The ULTRA OS logo over the page tree.** The sidebar starts with the
  ULTRA OS logo - the version drawn for a light ground
  (`media/icons/desktop/ultraos-logo-light.png`), as UltraDesktop's dark
  taskbar shows the one for a dark ground.

#### 2026-11-01 *0.9.0*
- **Desktop > Start with the desktop**: ChatCube and UltraMail, each a
  checkbox. A ticked program starts minimized a few seconds after
  UltraDesktop, unless it runs already.

#### 2026-10-22 *0.8.0*
- **Desktop: a packaged copy says why *At sign-in* is missing.** Run from the
  Microsoft Store (MSIX) package, the Desktop page cannot offer UltraDesktop in
  place of the Windows desktop, because the package's registry changes never
  reach sign-in. It now says so, and points to the UltraCanvas installer,
  instead of leaving the choice out without a word.

#### 2026-10-21 *0.7.0*
- **Notifications → Registrations lists and removes what Windows keeps for
  the applications' notifications** - each application's name and icon and
  the program a click on its toast starts. **Remove all** does what the
  installer's uninstaller does, for a folder unpacked from the ZIP package,
  which has none. `UOS-Settings --remove-notification-registrations` does it
  with no window; the ZIP's `uc-remove-registrations.cmd` runs that (framework
  changelog: `notification-registrations-cleanup`).

#### 2026-10-20 *0.6.0*
- **Appearance → Emoji: colour, black and white, or the system's.** A new
  page chooses how every application draws emoji: *Colour* (Noto Color Emoji,
  the default), *Black and white* (Noto Emoji, in the text's colour) or
  *System* (the computer's own emoji font, Noto Color Emoji where it has
  none). Both fonts now ship with ULTRA OS, so an emoji - the 👤 at the start
  of a LinkedIn subject in UltraMail - no longer draws as a box of hex digits
  on a computer without an emoji font. Saved to `Emoji.conf` at once;
  applications started afterwards use it (framework changelog:
  `bundled-emoji-fonts`).

#### 2026-10-15 *0.5.0*
- **Desktop → At sign-in (Windows) is a choice of three:** *The Windows
  desktop*, *UltraDesktop beside Explorer* (what the switch did before:
  started now and at every sign-in, Explorer's taskbar hidden) and
  *UltraDesktop instead of Explorer* - the whole desktop from the next
  sign-in, with the tray icons and sign out on UltraDesktop itself, and
  Explorer started in its place should UltraDesktop fail (framework changelog:
  `desktopshell-instead-of-explorer`). The choice always shows what is set, and
  a change that could not be made is said and undone on the page.

#### 2026-10-14 *0.4.1*
- **The desktop settings file is found in Application Support on macOS.**
  UltraDesktop keeps `desktop.json` there now (UltraDesktop changelog 0.3.1);
  the settings app reads and writes the same file through the same framework
  rule, so the two never disagree about where it is.

#### 2026-10-13 *0.4.0*
- **Desktop → At sign-in (Windows): UltraDesktop in place of the Windows
  desktop.** On switches over at once and for every sign-in after. Off brings
  the Windows desktop back at once, with the taskbar as it was. A note on the
  page tells you how to get back when the desktop does not come up. The row
  only appears on Windows.
- **Desktop → RAM disc can be emptied.** An empty field now means no RAM disc
  button, instead of keeping the previous folder.

#### 2026-10-12 *0.3.1*
- **One UOS-Settings at a time.** Starting UOS-Settings while it is already
  running brings the open window forward (restored if minimised; an open
  dialog comes forward with it) instead of opening a second copy. Two starts
  at the same moment cannot both stay, and a copy that does not answer is
  reported with *"UOS-Settings is already running"*. Command-line modes
  (`--version`) are unaffected. Framework: `UltraCanvasSingleInstance`
  (framework changelog: `instance-lock`).

#### 2026-10-05 *0.3.0*
- **Trusted certificates names each device.** The list showed only the
  address a key is kept under, so two printers on one subnet read as two
  numbers. It now shows the name the device was discovered under -
  "Office Printer", "Front Desk Scanner" - with the address in a column of
  its own, and *Forget* says which device it forgot by name. A key learned
  before any name was known reads *(name not known)* until the device is
  listed again.

#### 2026-10-05 *0.2.0*
- **Devices > Trusted certificates.** A new page lists the network scanners
  and printers whose self-signed HTTPS certificate was trusted the first time
  they were reached: the device's address and the SHA-256 of its key (in
  full in the tooltip), each with a *Forget* button. A device that was reset
  or replaced presents a new certificate and is refused until its old key is
  forgotten; *Forget* does that here instead of by editing
  `DeviceCertificates.conf`, and says which device it forgot. The page notes
  that the new key is trusted on the same terms as the first, so forget a
  device only when you know why its key changed.
- The two table pages and the notes boxes are built by the same code now,
  so the tables look and scroll alike.

#### 2026-10-01 *0.1.0*
- **UOS-Settings, the ULTRA OS settings application.** Settings that belong
  to the system rather than to one application get their own program,
  `UOS-Settings` (`Apps/UOSSettings`), laid out like UltraFiler's settings: a
  tree of pages on the left, the page on the right, Close at the foot. Every
  change is saved at once.
  - **Desktop.** UltraDesktop's settings - the taskbar's edge, the
    wallpaper (Browse... opens the file dialog), the RAM disc, the file
    manager, the number of virtual desktops - moved here from the desktop's
    own window. They are written to UltraDesktop's settings file (only those
    fields; the sticky notes in it stay the desktop's), and a running desktop
    takes them over within a second. The desktop's *ULTRA OS settings*
    button opens UOS-Settings on this page.
  - **File dialogs > Last used folder.** A switch decides whether the file
    dialogs of all applications share one last used folder (*One common
    folder*) or keep their own (*Their own folders*). With their own, a table
    with a scrollbar lists the applications - the ones that use the
    framework's file dialog, plus any other that has opened one - each with
    the folder it opens in and its own *Global | Individual* switch.
  - The setting is `FileDialog.conf` in the UltraCanvas settings folder,
    read and written through `UltraCanvasFileDialogSettings.h`, so the file
    dialog and the settings agree on it.
