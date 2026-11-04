"""Sending the codes: e-mail through an SMTP server, SMS through Twilio or a
generic HTTP webhook - or, for development and the tests, into an outbox file
(one JSON object per line) instead of either.

What is configured decides what the relay offers: a channel with no sender
gets no codes, and account recovery needs both (see app.py)."""

from __future__ import annotations

import base64
import json
import logging
import smtplib
import ssl
import threading
import time
import urllib.parse
import urllib.request
from email.message import EmailMessage

from .config import Settings

log = logging.getLogger("chatcube_relay.notify")


class Notifier:
    """Sends a text to an e-mail address or a phone number. Blocking: the
    app calls it on a worker thread, after it has answered."""

    def __init__(self, settings: Settings) -> None:
        self.settings = settings
        self._outbox_lock = threading.Lock()

    # ---- what is available ---------------------------------------------------

    @property
    def can_email(self) -> bool:
        s = self.settings
        return bool(s.outbox) or bool(s.smtp_host and s.smtp_from)

    @property
    def can_sms(self) -> bool:
        s = self.settings
        if s.outbox:
            return True
        if s.sms == "twilio":
            return bool(s.twilio_account_sid and s.twilio_auth_token and s.twilio_from)
        if s.sms == "webhook":
            return bool(s.sms_webhook_url)
        return False

    # ---- sending ----------------------------------------------------------------

    def send_email(self, to: str, subject: str, text: str) -> bool:
        if self.settings.outbox:
            return self._to_outbox({"channel": "email", "to": to, "subject": subject, "text": text})
        if not self.can_email:
            return False
        s = self.settings
        message = EmailMessage()
        message["From"] = s.smtp_from
        message["To"] = to
        message["Subject"] = subject
        message.set_content(text)
        try:
            if s.smtp_security == "ssl":
                with smtplib.SMTP_SSL(s.smtp_host, s.smtp_port, context=ssl.create_default_context(),
                                      timeout=30) as smtp:
                    self._login_and_send(smtp, message)
            else:
                with smtplib.SMTP(s.smtp_host, s.smtp_port, timeout=30) as smtp:
                    if s.smtp_security == "starttls":
                        smtp.starttls(context=ssl.create_default_context())
                    self._login_and_send(smtp, message)
            return True
        except (OSError, smtplib.SMTPException) as error:
            # Never the code or the address in full: the log is not private.
            log.warning("e-mail to %s not sent: %s", mask_email(to), error)
            return False

    def _login_and_send(self, smtp: smtplib.SMTP, message: EmailMessage) -> None:
        if self.settings.smtp_user:
            smtp.login(self.settings.smtp_user, self.settings.smtp_password)
        smtp.send_message(message)

    def send_sms(self, to: str, text: str) -> bool:
        if self.settings.outbox:
            return self._to_outbox({"channel": "phone", "to": to, "text": text})
        if not self.can_sms:
            return False
        s = self.settings
        try:
            if s.sms == "twilio":
                url = f"https://api.twilio.com/2010-04-01/Accounts/{urllib.parse.quote(s.twilio_account_sid)}/Messages.json"
                body = urllib.parse.urlencode({"To": to, "From": s.twilio_from, "Body": text}).encode()
                request = urllib.request.Request(url, data=body, method="POST")
                credentials = f"{s.twilio_account_sid}:{s.twilio_auth_token}".encode()
                request.add_header("Authorization", "Basic " + base64.b64encode(credentials).decode())
                request.add_header("Content-Type", "application/x-www-form-urlencoded")
            else:
                body = json.dumps({"to": to, "text": text}).encode()
                request = urllib.request.Request(s.sms_webhook_url, data=body, method="POST")
                request.add_header("Content-Type", "application/json")
                if s.sms_webhook_token:
                    request.add_header("Authorization", "Bearer " + s.sms_webhook_token)
            with urllib.request.urlopen(request, timeout=30) as response:
                return 200 <= response.status < 300
        except OSError as error:
            log.warning("SMS to %s not sent: %s", mask_phone(to), error)
            return False

    def _to_outbox(self, entry: dict) -> bool:
        entry["at"] = int(time.time())
        with self._outbox_lock:
            with open(self.settings.outbox, "a", encoding="utf-8") as out:
                out.write(json.dumps(entry) + "\n")
        return True


def mask_email(address: str) -> str:
    """"erin@example.org" -> "e***@example.org": enough to recognise, not to read."""
    name, _, domain = address.partition("@")
    return (name[:1] + "***@" + domain) if domain else "***"


def mask_phone(number: str) -> str:
    """"+491512345678" -> "+49 ••• 78"."""
    digits = "".join(c for c in number if c.isdigit())
    return f"+{digits[:2]} ••• {digits[-2:]}" if len(digits) >= 4 else "•••"
