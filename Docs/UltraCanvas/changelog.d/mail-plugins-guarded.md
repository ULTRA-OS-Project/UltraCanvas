- **No exception leaves any UltraNet plug-in.** The guard the JMAP plug-in
  got (an exception inside an entry point becomes a `PluginError` result
  naming the call and the reason, instead of reaching a caller's worker
  thread with no handler and ending the process) now lives in
  `Plugins/UltraNet/common/UltraNetPluginGuard.h` (`ultranet_plugin::Guarded`,
  `GuardedHandle`, `GuardedVoid`) and wraps every entry point of every
  plug-in: the mail four (IMAP, SMTP, POP3, JMAP - sending, the bulk fetch,
  folders, status, envelopes, messages, flags, moves, appends, expunge, the
  folder reconcile, creating and deleting folders), the messaging ones
  (MQTT, AMQP, CoAP: Connect, Publish, Subscribe), gRPC, the directories
  (LDAP, mDNS, SNMP: Search), the streams (RTSP, RTMP, RTP: OpenStream,
  ReadFrame), the remote shells (SSH, Telnet, SIP: OpenShell,
  ExecuteCommand), WebDAV's file operations, and every Initialize and
  Shutdown. A connect or open that throws reads as `UltraNetInvalidHandle`,
  as every failure of those calls does.
