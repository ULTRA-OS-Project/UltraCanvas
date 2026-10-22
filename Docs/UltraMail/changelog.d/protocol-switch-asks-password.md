- **Switching an account between IMAP and JMAP asks for the password again.**
  The account settings page kept the stored password when the *Protocol*
  changed, so an account switched to JMAP - whose server often wants its own
  app password or API token - could be saved but not connect. Choosing the
  other protocol on a password account now shows a warning under the fields
  and makes the password field *Required*; *Save* and *Save anyway* wait for
  it and put the cursor there. Switching back to the saved protocol lifts the
  demand; an account signed in with OAuth2 keeps its tokens and is not asked.
