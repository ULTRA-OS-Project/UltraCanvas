- **A Contacts page: the address book against the mail you sent.** UltraMail
  used to collect every sender it synced into its address book, so one kept
  for a while holds every shop and stranger that ever wrote. The new **Contacts** tab lists
  UltraMail's address book with, per contact, the number of messages you sent
  to it (counted from the To and Cc lines of your sent mail, over every
  account) and, as the second figure, the number it sent you; the list is
  ordered by mail sent. Each contact is tagged **Keep** when you wrote to it,
  **Delete** when you never did and UltraMail only collected it (section Other
  or Services), or **Review** when you never wrote to it but filed it yourself
  (Family, Friends, Work, Leisure or a group of your own) or it has no address.
  The Delete rows start selected, every selected row is marked red, and
  **Delete selected…** removes them from the address book after a
  confirmation that names them, all in one go (`ContactStore::RemoveAll`),
  and forgets their addresses (`ContactStore::Forget`) so UltraMail does not
  collect them again. The mail itself is not touched.
- **Your own sent mail is read for its recipients and kept off the map.** A
  message from the account's own address, or in the Sent folder (by its
  SPECIAL-USE role, else by name: "Sent", "Sent Items", "[Gmail]/Sent Mail",
  "Gesendet", ...), is no longer analysed as a sender block: only whom it went
  to is stored (analysis schema 6). An account added in EmailCleaner now
  downloads its Sent folder too, beside the inbox and the junk folder.
