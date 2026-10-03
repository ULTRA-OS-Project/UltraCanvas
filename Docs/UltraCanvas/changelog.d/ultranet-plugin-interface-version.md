- **UltraNet refuses a plug-in built against older plug-in interfaces instead
  of crashing on it.** A method added to a plug-in interface - even last, with
  a default, like `IMailboxProtocolPlugin::ExpungeMessage` - is a vtable slot
  an older plug-in does not have, and the host calling it there jumped to
  whatever followed the vtable. There is now a plug-in interface version,
  `ULTRANET_PLUGIN_INTERFACE_VERSION` (3), raised with every such change. The
  host shim every plug-in compiles in exports
  `UltraNet_PluginInterfaceVersion()`, so plug-ins report the version they were
  built against without a change to their own sources, and
  `UltraNet_RefreshPlugins` refuses one that reports none or an older one before
  calling `UltraNet_PluginInit`. A newer plug-in is loaded. New
  `UltraNet_GetRefusedPlugins()` lists the libraries refused and why ("built
  against UltraNet plug-in interface 1; this application needs 3 - the plug-in
  is out of date and has to be rebuilt"). Test plug-in
  `Tests/UltraNet/oldiface` and test
  `plugin_loader_refuses_a_plugin_built_against_older_interfaces`.
- **New `IMailboxProtocolPlugin::EmptyFolder(serverUrl, folder, options)`**
  (interface version 3): every message in the folder flagged `\Deleted` and
  expunged - emptying the Trash. The default works one message at a time over
  `FetchAllFlags` / `FetchEnvelopes`, `StoreFlags` and `ExpungeMessage`; the
  IMAP plug-in does it in two commands whatever the number of messages
  (`UID STORE 1:* +FLAGS.SILENT (\Deleted)`, then `EXPUNGE`; nothing for an
  empty folder), so a message that arrives meanwhile stays. Helper
  `MarkAllDeletedCommand` in `ImapParse.h`, tested. Checked against Dovecot 2.3.
