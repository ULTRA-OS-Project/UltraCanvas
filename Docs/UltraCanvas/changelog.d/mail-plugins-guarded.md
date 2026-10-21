- **No exception leaves the IMAP, SMTP or POP3 plug-in either.** The guard
  the JMAP plug-in got (an exception inside an entry point becomes a
  `PluginError` result naming the call and the reason, instead of reaching
  a caller's worker thread with no handler and ending the process) now
  lives in `Plugins/UltraNet/common/UltraNetPluginGuard.h`
  (`ultranet_plugin::Guarded` / `GuardedVoid`) and wraps every entry point
  of the four mail plug-ins - sending, the bulk fetch, folders, status,
  envelopes, messages, flags, moves, appends, expunge, the folder
  reconcile, creating and deleting folders, and Shutdown.
