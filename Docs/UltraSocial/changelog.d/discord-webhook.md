- **Discord.** UltraSocial posts to a Discord channel through a channel
  webhook: create one under the channel's settings → Integrations → Webhooks
  and paste its URL into *Add social account* → *Discord channel*. Nothing
  else to sign in or register - the webhook is checked when it is added, and
  a deleted one is reported as Discord's *Unknown Webhook*. Text (up to 2,000
  characters) and up to ten images, with their alt text, go out as one
  message, and Recent posts links to it. Posts show the webhook's name and
  avatar: Discord allows no posting as a user
  (`engine/connectors/UltraSocialDiscordConnector.h`).
