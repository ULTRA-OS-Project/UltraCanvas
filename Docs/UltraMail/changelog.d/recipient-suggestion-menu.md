- **No-reply addresses are no longer suggested.** The To / Cc / Bcc fields
  offered every address in the address book, so a collected
  `noreply@...`, `no-reply@...`, `do-not-reply@...` or `noreply.billing@...`
  sender came up beside the people one writes to. An address whose local
  part reads "noreply" or "donotreply" (ignoring `-`, `_` and `.`) is now
  left out; it stays in the address book.
- **A right click on a suggestion has a menu.** *Edit contact…* opens the
  contact editor for that address; *Delete address* removes it from its
  contact, after asking (a contact left with no address is deleted); *Set as
  SPAM* puts the sender on the blocked list, as the sender menu's *Block this
  sender* does, so its mail is judged again. The field then shows its
  suggestions as they now are.
- **Blocked senders are not suggested**, whether blocked from that menu, the
  sender menu or *Settings > Warnings > Trusted & blocked*.
- **Edit contact edits the address it was opened for.** The contact editor
  always showed - and saved over - a contact's primary address, so *Edit
  contact…* on a contact's second address, from a suggestion or from a
  message that address sent, showed another one. It now shows that address
  and changes only it, keeping its label and the contact's other addresses;
  emptied, the address is removed, and changed to another of the contact's
  addresses, the two become one.
- **Incoming mail no longer fills the address book with strangers.** Every
  sender of new mail in the inbox was added, spam included - so spammers
  were offered as recipients, and their next mail read as from someone in
  the address book instead of a *New sender*. Now only senders in the
  known-sender registry (shops, platforms, services) are added from incoming
  mail, under Services; a person joins the address book when you write to
  them, or add them. Contacts collected before stay until deleted.
- **A deleted address stays deleted.** Deleting a contact, or an address
  from the suggestion menu, remembers it, and incoming mail does not add it
  back; neither is a blocked sender added. Writing to it still adds it.
- **Writing to a picked suggestion no longer makes a broken contact.** A
  recipient written as "Anna Schmidt <anna@example.com>" was remembered with
  that whole text as its address - a second contact beside the real one. Its
  name and address are now read apart.
