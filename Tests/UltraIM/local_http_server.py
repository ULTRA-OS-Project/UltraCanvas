#!/usr/bin/env python3
"""The HTTP server of the stand-in servers here (mastodon, mattermost, signal,
xmpp, zulip): http.server's ThreadingHTTPServer without the name lookup.

HTTPServer's own server_bind asks socket.getfqdn() for the name of the address
it listens on - a reverse DNS lookup. On a macOS CI runner that lookup stalls
for fifteen seconds and more (Tests/IODeviceScannerESCLLiveScanner.py met it
first), and it is paid at every start: SignalProviderTests starts its
stand-in a dozen times, and ran past its 300 seconds there while every start
on Linux takes a fraction of one. The name is used for nothing here - the
servers print the address they listen on - so it is not asked.

Standard library only.
"""

import socketserver
from http.server import ThreadingHTTPServer


class LocalHTTPServer(ThreadingHTTPServer):
    daemon_threads = True

    def server_bind(self):
        socketserver.TCPServer.server_bind(self)
        self.server_name, self.server_port = self.server_address[:2]
