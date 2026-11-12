"""Skrill Quick Checkout: the form that sends the buyer to Skrill, the
status report Skrill posts back, the recurring billing of a subscription,
and the Automated Payments Interface for what the merchant asks of Skrill
(a refund, the end of a recurring payment).

The buyer is sent by an HTML form POSTed to https://pay.skrill.com with
the fields below (Skrill's Quick Checkout integration guide). Skrill then
posts the outcome to status_url, server to server, with md5sig =
MD5(merchant_id + transaction_id + MD5(secret word).upper() + mb_amount +
mb_currency + status).upper(); that signature, with the amount and the
currency compared to the order, is what this service trusts. The browser's
return to return_url proves nothing and changes nothing.

A subscription is Quick Checkout's recurring billing: the form names
rec_amount, rec_period and rec_cycle, and Skrill posts each later payment
to rec_status_url with the same signature scheme and rec_payment_id.

The merchant's own requests go to the Automated Payments Interface with
the API password (the merchant portal: Developer settings), MD5-hashed:
a refund is refund.pl, prepared and then executed with the session id the
preparation gave; a recurring payment is ended through query.pl. The
answers are XML. The transport is a function the caller hands in, so the
tests use a fake and nothing here touches the network in a test.

Version: 0.2.0 - recurring billing, the API (refund, cancel recurring)
Version: 0.1.0
"""

from __future__ import annotations

import hashlib
import html
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from typing import Callable

from .pages import page

STATUS_PROCESSED = "2"
STATUS_PENDING = "0"
STATUS_CANCELLED = "-1"
STATUS_FAILED = "-2"
STATUS_CHARGEBACK = "-3"

STATUS_NAMES = {STATUS_PROCESSED: "paid", STATUS_PENDING: "pending", STATUS_CANCELLED: "cancelled",
                STATUS_FAILED: "failed", STATUS_CHARGEBACK: "chargeback"}

CYCLES = {"monthly": "month", "yearly": "year"}   # the catalog's pricing model -> rec_cycle


def checkout_fields(*, pay_to_email: str, base_url: str, order_id: str, amount: str, currency: str,
                    app_name: str, app_id: str, language: str = "EN", recipient: str = "UltraStore",
                    cycle: str = "", rec_amount: str = "", rec_grace_days: int = 7) -> dict[str, str]:
    """The Quick Checkout form fields for one order; with `cycle` ("month"
    or "year") the recurring billing of a subscription, `rec_amount` per
    period after the first payment."""
    fields = {
        "pay_to_email": pay_to_email,
        "recipient_description": recipient,
        "transaction_id": order_id,
        "return_url": f"{base_url}/pay/{order_id}/return",
        "return_url_text": "Back to UltraStore",
        "cancel_url": f"{base_url}/pay/{order_id}/cancel",
        "status_url": f"{base_url}/v1/skrill/status",
        "language": language,
        "amount": amount,
        "currency": currency,
        "detail1_description": "App:",
        "detail1_text": app_name,
        "detail2_description": "Id:",
        "detail2_text": app_id,
        "merchant_fields": "order_id",
        "order_id": order_id,
    }
    if cycle:
        fields.update({
            "rec_amount": rec_amount or amount,
            "rec_period": "1",
            "rec_cycle": cycle,
            "rec_grace_period": str(rec_grace_days),
            "rec_status_url": f"{base_url}/v1/skrill/recurring",
            "detail3_description": "Billing:",
            "detail3_text": f"{rec_amount or amount} {currency} every {cycle}",
        })
    return fields


def checkout_page(pay_url: str, fields: dict[str, str], app_name: str, amount: str, currency: str, cycle: str = "", rec_amount: str = "") -> str:
    """A page that sends the buyer on to Skrill at once, with a button for
    a browser that does not run scripts."""
    inputs = "\n".join(f'<input type="hidden" name="{html.escape(k)}" value="{html.escape(v)}">' for k, v in fields.items())
    billing = ""
    if cycle:
        billing = (f"<p class=\"muted\">A subscription: {html.escape(rec_amount or amount)} {html.escape(currency)} every {html.escape(cycle)} "
                   "after this payment, until you cancel it in UltraStore or in your Skrill account.</p>\n")
    body = (
        f"<p class=\"amount\">{html.escape(amount)} {html.escape(currency)}</p>\n{billing}"
        "<p>You are being taken to Skrill to pay. Skrill takes cards and the local payment methods of your country; "
        "the store never sees your card.</p>\n"
        f'<form id="pay" method="post" action="{html.escape(pay_url)}">\n{inputs}\n<button type="submit">Continue to Skrill</button>\n</form>\n'
        "<script>document.getElementById('pay').submit();</script>"
    )
    return page(f"Paying for {app_name}", body, heading=app_name)


def status_signature(merchant_id: str, transaction_id: str, secret_word: str, mb_amount: str, mb_currency: str, status: str) -> str:
    inner = hashlib.md5(secret_word.encode()).hexdigest().upper()   # noqa: S324 - Skrill's scheme
    return hashlib.md5((merchant_id + transaction_id + inner + mb_amount + mb_currency + status).encode()).hexdigest().upper()   # noqa: S324


def verify_status(fields: dict[str, str], merchant_id: str, secret_word: str) -> bool:
    """Whether a status report's md5sig is Skrill's, for our secret word."""
    expected = status_signature(merchant_id, fields.get("transaction_id", ""), secret_word, fields.get("mb_amount", ""),
                                fields.get("mb_currency", ""), fields.get("status", ""))
    return expected == fields.get("md5sig", "").upper()


# ----- the Automated Payments Interface -----

ApiCall = Callable[[str, dict[str, str]], str]   # POST url with form fields, the body back


def http_api_call(url: str, fields: dict[str, str]) -> str:
    data = urllib.parse.urlencode(fields).encode()
    with urllib.request.urlopen(url, data=data, timeout=30) as r:   # noqa: S310 - Skrill's own https URL
        return r.read().decode("utf-8", "replace")


class SkrillApiError(Exception):
    pass


def parse_response(text: str) -> dict[str, str]:
    """The fields of an API answer (<response><sid>..</sid></response>);
    SkrillApiError for an <error> answer or something that is not XML."""
    try:
        root = ET.fromstring(text)
    except ET.ParseError as e:
        raise SkrillApiError(f"Skrill's answer is not XML: {text[:200]!r}") from e
    error = root.find("error")
    if error is not None:
        message = (error.findtext("error_msg") or "").strip() or ET.tostring(error, encoding="unicode")
        raise SkrillApiError(f"Skrill refused: {message}")
    return {child.tag: (child.text or "").strip() for child in root}


class SkrillApi:
    """The merchant's requests to Skrill, signed with the API password."""

    def __init__(self, base_url: str, email: str, api_password: str, call: ApiCall = http_api_call) -> None:
        self.base_url = base_url.rstrip("/")
        self.email = email
        self.password_md5 = hashlib.md5(api_password.encode()).hexdigest()   # noqa: S324 - Skrill's scheme
        self.call = call

    @property
    def configured(self) -> bool:
        return bool(self.email and self.password_md5 and self.password_md5 != hashlib.md5(b"").hexdigest())   # noqa: S324

    def refund(self, skrill_transaction: str, *, amount: str = "", note: str = "") -> dict[str, str]:
        """Refund a processed payment, whole or `amount` of it: prepared,
        then executed with the session id; the answer's status is 2 when
        the money went back."""
        fields = {"action": "prepare", "email": self.email, "password": self.password_md5, "mb_transaction_id": skrill_transaction}
        if amount:
            fields["amount"] = amount
        if note:
            fields["refund_note"] = note[:250]
        prepared = parse_response(self.call(f"{self.base_url}/refund.pl", fields))
        sid = prepared.get("sid", "")
        if not sid:
            raise SkrillApiError("Skrill's refund preparation gave no session id")
        done = parse_response(self.call(f"{self.base_url}/refund.pl", {"action": "refund", "sid": sid}))
        if done.get("status", "") != STATUS_PROCESSED:
            raise SkrillApiError(f"Skrill did not process the refund (status {done.get('status', '?')}" +
                                 (f", {done['error']}" if done.get("error") else "") + ")")
        return done

    def cancel_recurring(self, rec_payment_id: str) -> dict[str, str]:
        """End a recurring payment: Skrill charges the buyer no more."""
        fields = {"action": "cancel_rec_payment", "email": self.email, "password": self.password_md5, "rec_payment_id": rec_payment_id}
        return parse_response(self.call(f"{self.base_url}/query.pl", fields))
