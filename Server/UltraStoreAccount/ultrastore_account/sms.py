"""The text messages the service sends: a code to verify a phone number, a
code that releases a recovery key. A text costs money, so the service sends
one only for an account on a paid plan (store.PLANS).

One function, `send(to, text)`; the gateway one from the settings - an HTTPS
endpoint that takes `{"to", "from", "text"}` as JSON with
`Authorization: Bearer <token>`, the shape a small relay in front of any SMS
provider is given - or the log one on a machine without a gateway (a
development machine; the tests capture it)."""

from __future__ import annotations

import json
import logging
import urllib.request
from typing import Callable

Texter = Callable[[str, str], None]
log = logging.getLogger("ultrastore_account.sms")


def log_texter(sender: str) -> Texter:
    def send(to: str, text: str) -> None:
        log.info("text to %s from %s: %s", to, sender, text)
    return send


def gateway_texter(url: str, token: str, sender: str) -> Texter:
    if not url.startswith("https://"):
        raise ValueError("ULTRASTORE_ACCOUNT_SMS_URL must be https://")

    def send(to: str, text: str) -> None:
        body = json.dumps({"to": to, "from": sender, "text": text}).encode()
        request = urllib.request.Request(url, data=body, method="POST",
                                         headers={"Content-Type": "application/json", "Authorization": "Bearer " + token})
        with urllib.request.urlopen(request, timeout=20) as answer:   # noqa: S310 - https only, checked above
            if answer.status >= 300:
                raise RuntimeError(f"the SMS gateway answered {answer.status}")
    return send


def texter_from_settings(url: str, token: str, sender: str) -> Texter:
    return gateway_texter(url, token, sender) if url else log_texter(sender)
