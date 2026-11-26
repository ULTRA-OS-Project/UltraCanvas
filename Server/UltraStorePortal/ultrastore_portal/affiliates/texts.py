"""A text message to a phone: the code that proves an affiliate's number.

The portal has no provider of its own. With TEXT_GATEWAY_URL set, a text
is one HTTPS POST to it - `to` and `text` as form fields, the bearer token
TEXT_GATEWAY_TOKEN - which is what the gateways of the SMS providers take,
or a few lines in front of one. Without it nothing can be sent: on a
developer's machine and in the tests the text is kept in `outbox`, and in
production the affiliate's number is confirmed by the operator at the
review instead.

Version: 0.10.0
"""

from __future__ import annotations

import logging
import urllib.error
import urllib.parse
import urllib.request

from django.conf import settings

log = logging.getLogger(__name__)
outbox: list[tuple[str, str]] = []   # (number, text): what a developer's machine and the tests "sent"


class TextError(Exception):
    pass


def can_send() -> bool:
    return bool(settings.TEXT_GATEWAY_URL) or settings.DEBUG or settings.RUNNING_TESTS


def send_text(number: str, text: str) -> None:
    if not settings.TEXT_GATEWAY_URL:
        if not (settings.DEBUG or settings.RUNNING_TESTS):
            raise TextError("The store cannot send text messages yet")
        outbox.append((number, text))
        log.info("Text to %s: %s", number, text)
        return
    request = urllib.request.Request(
        settings.TEXT_GATEWAY_URL, data=urllib.parse.urlencode({"to": number, "text": text}).encode(),
        headers={"Authorization": f"Bearer {settings.TEXT_GATEWAY_TOKEN}"} if settings.TEXT_GATEWAY_TOKEN else {})
    try:
        with urllib.request.urlopen(request, timeout=15) as response:   # noqa: S310 - the operator's own configured https address
            if response.status >= 300:
                raise TextError(f"The text gateway answered {response.status}")
    except (urllib.error.URLError, OSError) as error:
        raise TextError(f"The text could not be sent: {error}") from error
