"""The mail the marketplace service sends: an operator's password-reset
code, a security notice to an operator. One function, `send(to, subject,
text)`; the SMTP one from the settings' URL, or the log one on a machine
without a mail server (a development machine, the tests capture it). The
same shape as the account service's mail.py."""

from __future__ import annotations

import logging
import smtplib
from email.message import EmailMessage
from typing import Callable
from urllib.parse import unquote, urlparse

Mailer = Callable[[str, str, str], None]
log = logging.getLogger("ultrastore_market.mail")


def log_mailer(mail_from: str) -> Mailer:
    def send(to: str, subject: str, text: str) -> None:
        log.info("mail to %s from %s: %s\n%s", to, mail_from, subject, text)
    return send


def smtp_mailer(url: str, mail_from: str) -> Mailer:
    parts = urlparse(url)
    host, port = parts.hostname or "localhost", parts.port or (465 if parts.scheme == "smtps" else 587)
    user, password = unquote(parts.username or ""), unquote(parts.password or "")

    def send(to: str, subject: str, text: str) -> None:
        message = EmailMessage()
        message["From"], message["To"], message["Subject"] = mail_from, to, subject
        message.set_content(text)
        if parts.scheme == "smtps":
            server: smtplib.SMTP = smtplib.SMTP_SSL(host, port, timeout=20)
        else:
            server = smtplib.SMTP(host, port, timeout=20)
            server.starttls()
        with server:
            if user:
                server.login(user, password)
            server.send_message(message)
    return send


def mailer_from_settings(smtp_url: str, mail_from: str) -> Mailer:
    return smtp_mailer(smtp_url, mail_from) if smtp_url else log_mailer(mail_from)
