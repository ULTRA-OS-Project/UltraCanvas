#!/usr/bin/env python3
# Tests/UltraIM/matrix_nio_peer.py
# Another Matrix client for MatrixProviderTests: matrix-nio with end-to-end
# encryption (an implementation of the key handling independent of ours).
# Signs in, joins the room it is invited to, waits for an encrypted message,
# answers it encrypted, then imports a key export file of ours and writes
# one of its own. One line of JSON per step on stdout:
#   {"ready": true} {"joined": room} {"received": text} {"sent": text}
#   {"imported": true} {"exported": path}   or   {"error": why}
# Version: 0.1.0
# Last Modified: 2026-10-21
# Author: UltraCanvas Framework

import argparse
import asyncio
import json
import sys


def say(**fields):
    print(json.dumps(fields), flush=True)


async def run(args):
    from nio import AsyncClient, AsyncClientConfig, InviteMemberEvent, RoomMessageText

    config = AsyncClientConfig(encryption_enabled=True, store_sync_tokens=True)
    client = AsyncClient(args.url, args.user, store_path=args.store, config=config)
    login = await client.login(args.password, device_name="nio peer")
    if not getattr(login, "access_token", None):
        say(error="login failed: %s" % login)
        return 1
    received = asyncio.Event()
    joined = {}

    async def on_invite(room, event):
        if event.state_key == client.user_id and event.membership == "invite" and room.room_id not in joined:
            joined[room.room_id] = True
            await client.join(room.room_id)
            say(joined=room.room_id)

    async def on_message(room, event):
        if event.body == args.expect:
            joined["room"] = room.room_id
            received.set()

    client.add_event_callback(on_invite, InviteMemberEvent)
    client.add_event_callback(on_message, RoomMessageText)

    # The device keys on the server before anyone looks for them.
    await client.sync(timeout=0, full_state=True)
    if client.should_upload_keys:
        await client.keys_upload()
    say(ready=True)
    syncing = asyncio.ensure_future(client.sync_forever(timeout=1000, full_state=True))
    try:
        await asyncio.wait_for(received.wait(), args.wait)
    except asyncio.TimeoutError:
        say(error="no message '%s' in %d s" % (args.expect, args.wait))
        return 1
    say(received=args.expect)
    response = await client.room_send(
        joined["room"], "m.room.message", {"msgtype": "m.text", "body": args.reply}, ignore_unverified_devices=True
    )
    if not getattr(response, "event_id", None):
        say(error="send failed: %s" % response)
        return 1
    say(sent=args.reply)
    if args.import_file:
        await client.import_keys(args.import_file, args.passphrase)
        say(imported=True)
    if args.export_file:
        await client.export_keys(args.export_file, args.passphrase)
        say(exported=args.export_file)
    syncing.cancel()
    await client.close()
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", required=True)
    parser.add_argument("--user", required=True)
    parser.add_argument("--password", required=True)
    parser.add_argument("--store", required=True)
    parser.add_argument("--expect", required=True)
    parser.add_argument("--reply", required=True)
    parser.add_argument("--import-file", default="")
    parser.add_argument("--export-file", default="")
    parser.add_argument("--passphrase", default="")
    parser.add_argument("--wait", type=int, default=90)
    args = parser.parse_args()
    try:
        sys.exit(asyncio.run(run(args)))
    except Exception as exc:  # the test reads the reason
        say(error="%s: %s" % (type(exc).__name__, exc))
        sys.exit(1)


if __name__ == "__main__":
    main()
