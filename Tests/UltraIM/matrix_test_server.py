#!/usr/bin/env python3
# Tests/UltraIM/matrix_test_server.py
# A throwaway Synapse homeserver for MatrixProviderTests: generates a config
# in --dir, listens on 127.0.0.1:--port with open registration and no rate
# limits, registers --users (password "<user>-pass"), prints one JSON line
# {"url": ..., "server_name": ...} once it is ready, and runs until it gets
# SIGTERM.
#
#   python3 matrix_test_server.py --dir /tmp/hs --port 18008 --users alice,bob
#
# Needs a python with matrix-synapse installed (pip install matrix-synapse).
# Version: 0.1.0
# Last Modified: 2026-10-21
# Author: UltraCanvas Framework

import argparse
import json
import os
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request

SETTINGS = """
enable_registration: true
enable_registration_without_verification: true
suppress_key_server_warning: true
trusted_key_servers: []
presence:
  enabled: true
rc_message: {per_second: 1000, burst_count: 1000}
rc_registration: {per_second: 1000, burst_count: 1000}
rc_login:
  address: {per_second: 1000, burst_count: 1000}
  account: {per_second: 1000, burst_count: 1000}
  failed_attempts: {per_second: 1000, burst_count: 1000}
rc_joins:
  local: {per_second: 1000, burst_count: 1000}
  remote: {per_second: 1000, burst_count: 1000}
rc_invites:
  per_room: {per_second: 1000, burst_count: 1000}
  per_user: {per_second: 1000, burst_count: 1000}
"""


def request(url, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method="POST" if data else "GET",
                                 headers={"Content-Type": "application/json"})
    # The test talks to 127.0.0.1: never through a proxy.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(req, timeout=10) as reply:
        return json.loads(reply.read() or b"{}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", required=True)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--users", default="")
    args = parser.parse_args()

    os.makedirs(args.dir, exist_ok=True)
    config = os.path.join(args.dir, "homeserver.yaml")
    subprocess.run([sys.executable, "-m", "synapse.app.homeserver", "--server-name", "localhost",
                    "--config-path", config, "--data-directory", args.dir,
                    "--generate-config", "--report-stats=no"],
                   cwd=args.dir, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    lines = []
    with open(config, encoding="utf-8") as f:
        for line in f.read().splitlines():
            # IPv4 loopback only (a container may have no ::1), on our port.
            if line.strip() == "- ::1":
                continue
            if line.strip() == "port: 8008":
                line = line.replace("8008", str(args.port))
            lines.append(line)
    with open(config, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n" + SETTINGS)

    log = open(os.path.join(args.dir, "synapse.log"), "w")
    server = subprocess.Popen([sys.executable, "-m", "synapse.app.homeserver", "--config-path", config],
                              cwd=args.dir, stdout=log, stderr=subprocess.STDOUT)

    def stop(*_):
        server.terminate()
        try:
            server.wait(timeout=10)
        except subprocess.TimeoutExpired:
            server.kill()
        sys.exit(0)

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)

    url = "http://127.0.0.1:%d" % args.port
    for _ in range(120):
        if server.poll() is not None:
            print(json.dumps({"error": "synapse exited", "log": os.path.join(args.dir, "synapse.log")}), flush=True)
            sys.exit(1)
        try:
            request(url + "/_matrix/client/versions")
            break
        except (urllib.error.URLError, OSError):
            time.sleep(0.5)
    else:
        print(json.dumps({"error": "synapse did not start"}), flush=True)
        stop()

    for user in [u for u in args.users.split(",") if u]:
        request(url + "/_matrix/client/v3/register",
                {"username": user, "password": user + "-pass", "auth": {"type": "m.login.dummy"},
                 "inhibit_login": True})

    print(json.dumps({"url": url, "server_name": "localhost"}), flush=True)
    server.wait()       # until SIGTERM stops it (or it stops by itself)


if __name__ == "__main__":
    main()
