"""The senders: what goes to an SMS webhook, to Twilio and to an SMTP server."""

from __future__ import annotations

import base64
import json
import smtplib
import threading
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer

from chatcube_relay.config import Settings
from chatcube_relay.notify import Notifier, mask_email, mask_phone


def test_nothing_configured_sends_nothing():
    n = Notifier(Settings())
    assert not n.can_email and not n.can_sms
    assert not n.send_email("a@example.org", "s", "t") and not n.send_sms("+15550001", "t")


def test_sms_webhook_posts_json_with_its_token():
    received = {}

    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            received["body"] = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            received["auth"] = self.headers.get("Authorization")
            self.send_response(204)
            self.end_headers()

        def log_message(self, *args):
            pass

    server = HTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.handle_request, daemon=True).start()
    url = f"http://127.0.0.1:{server.server_address[1]}/sms"
    n = Notifier(Settings(sms="webhook", sms_webhook_url=url, sms_webhook_token="t0ken"))
    assert n.can_sms and n.send_sms("+4915123456789", "code 123456")
    server.server_close()
    assert received["body"] == {"to": "+4915123456789", "text": "code 123456"}
    assert received["auth"] == "Bearer t0ken"


def test_twilio_gets_its_form_and_basic_auth(monkeypatch):
    seen = {}

    class Response:
        status = 201

        def __enter__(self):
            return self

        def __exit__(self, *args):
            return False

    def fake_urlopen(request, timeout=0):
        seen["url"] = request.full_url
        seen["auth"] = request.get_header("Authorization")
        seen["form"] = urllib.parse.parse_qs(request.data.decode())
        return Response()

    monkeypatch.setattr(urllib.request, "urlopen", fake_urlopen)
    n = Notifier(Settings(sms="twilio", twilio_account_sid="AC123", twilio_auth_token="secret",
                          twilio_from="+15550000000"))
    assert n.can_sms and n.send_sms("+4915123456789", "code 123456")
    assert seen["url"] == "https://api.twilio.com/2010-04-01/Accounts/AC123/Messages.json"
    assert seen["auth"] == "Basic " + base64.b64encode(b"AC123:secret").decode()
    assert seen["form"] == {"To": ["+4915123456789"], "From": ["+15550000000"], "Body": ["code 123456"]}


def test_smtp_starttls_login_and_message(monkeypatch):
    calls = []

    class FakeSMTP:
        def __init__(self, host, port, timeout=0):
            calls.append(("connect", host, port))

        def __enter__(self):
            return self

        def __exit__(self, *args):
            return False

        def starttls(self, context=None):
            calls.append(("starttls",))

        def login(self, user, password):
            calls.append(("login", user, password))

        def send_message(self, message):
            calls.append(("send", message["From"], message["To"], message["Subject"], message.get_content()))

    monkeypatch.setattr(smtplib, "SMTP", FakeSMTP)
    n = Notifier(Settings(smtp_host="mail.example.org", smtp_from="ChatCube <no-reply@example.org>",
                          smtp_user="relay", smtp_password="pw"))
    assert n.can_email and n.send_email("erin@example.org", "Your code", "123456")
    assert calls[0] == ("connect", "mail.example.org", 587)
    assert ("starttls",) in calls and ("login", "relay", "pw") in calls
    assert calls[-1][:4] == ("send", "ChatCube <no-reply@example.org>", "erin@example.org", "Your code")
    assert calls[-1][4].strip() == "123456"


def test_a_failing_server_is_a_false_not_an_exception(monkeypatch):
    def refuse(*args, **kwargs):
        raise OSError("connection refused")

    monkeypatch.setattr(smtplib, "SMTP", refuse)
    assert not Notifier(Settings(smtp_host="mail.example.org", smtp_from="x@example.org")).send_email("a@b.c", "s", "t")


def test_masks():
    assert mask_email("erin@example.org") == "e***@example.org"
    assert mask_phone("+49 151 2345 6789") == "+49 ••• 89"
