"""Skrill Quick Checkout: the form that sends the buyer to Skrill, and the
status report Skrill posts back.

The buyer is sent by an HTML form POSTed to https://pay.skrill.com with
the fields below (Skrill's Quick Checkout integration guide). Skrill then
posts the outcome to status_url, server to server, with md5sig =
MD5(merchant_id + transaction_id + MD5(secret word).upper() + mb_amount +
mb_currency + status).upper(); that signature, with the amount and the
currency compared to the order, is what this service trusts. The browser's
return to return_url proves nothing and changes nothing."""

from __future__ import annotations

import hashlib
import html

STATUS_PROCESSED = "2"
STATUS_PENDING = "0"
STATUS_CANCELLED = "-1"
STATUS_FAILED = "-2"
STATUS_CHARGEBACK = "-3"

STATUS_NAMES = {STATUS_PROCESSED: "paid", STATUS_PENDING: "pending", STATUS_CANCELLED: "cancelled",
                STATUS_FAILED: "failed", STATUS_CHARGEBACK: "chargeback"}


def checkout_fields(*, pay_to_email: str, base_url: str, order_id: str, amount: str, currency: str,
                    app_name: str, app_id: str, language: str = "EN", recipient: str = "UltraStore") -> dict[str, str]:
    """The Quick Checkout form fields for one order."""
    return {
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


def checkout_page(pay_url: str, fields: dict[str, str], app_name: str, amount: str, currency: str) -> str:
    """A page that sends the buyer on to Skrill at once, with a button for
    a browser that does not run scripts."""
    inputs = "\n".join(f'<input type="hidden" name="{html.escape(k)}" value="{html.escape(v)}">' for k, v in fields.items())
    return (
        "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        f"<title>UltraStore - paying for {html.escape(app_name)}</title>\n"
        "<style>body{font-family:sans-serif;max-width:36em;margin:3em auto;padding:0 1em;color:#1f2937}"
        "button{background:#2563eb;color:#fff;border:0;padding:.6em 1.2em;border-radius:.4em;font-size:1em}</style>\n</head>\n<body>\n"
        f"<h1>{html.escape(app_name)}</h1>\n<p>{html.escape(amount)} {html.escape(currency)} - you are being taken to Skrill to pay.</p>\n"
        f'<form id="pay" method="post" action="{html.escape(pay_url)}">\n{inputs}\n<button type="submit">Continue to Skrill</button>\n</form>\n'
        "<script>document.getElementById('pay').submit();</script>\n</body>\n</html>\n"
    )


def status_signature(merchant_id: str, transaction_id: str, secret_word: str, mb_amount: str, mb_currency: str, status: str) -> str:
    inner = hashlib.md5(secret_word.encode()).hexdigest().upper()   # noqa: S324 - Skrill's scheme
    return hashlib.md5((merchant_id + transaction_id + inner + mb_amount + mb_currency + status).encode()).hexdigest().upper()   # noqa: S324


def verify_status(fields: dict[str, str], merchant_id: str, secret_word: str) -> bool:
    """Whether a status report's md5sig is Skrill's, for our secret word."""
    expected = status_signature(merchant_id, fields.get("transaction_id", ""), secret_word, fields.get("mb_amount", ""),
                                fields.get("mb_currency", ""), fields.get("status", ""))
    return expected == fields.get("md5sig", "").upper()
