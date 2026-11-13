"""The look of the pages a buyer sees from this service: the checkout hand-off,
the thank-you and cancel pages, the receipt. One stylesheet and one page
frame, the ULTRA OS mark at the top, the store's colours (StoreTheme.h) and
a dark look where the browser asks for one. The admin backend's partner
landing pages (Affiliates.cpp, CodeLandingPage) carry the same stylesheet,
so everything a buyer reads in a browser looks like one product.

Version: 0.2.0 - the receipt of one payment (a renewal, a refund, a chargeback)
Version: 0.1.0
"""
from __future__ import annotations

import html
from pathlib import Path

STATIC_DIR = Path(__file__).with_name("static")

# Shared with CodeLandingPage in Apps/UltraStoreAdmin/core/Affiliates.cpp:
# change both together.
STYLESHEET = """\
:root{--bg:#f5f6f8;--surface:#fff;--border:#e2e5ea;--text:#181c23;--muted:#68707d;--accent:#2563eb;--accent-hover:#1d4ed8;--ok:#16a34a}
@media (prefers-color-scheme:dark){:root{--bg:#111418;--surface:#1a1f27;--border:#2a303a;--text:#e6e8ec;--muted:#9aa3b2;--accent:#3b82f6;--accent-hover:#60a5fa}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
.wrap{max-width:38em;margin:0 auto;padding:1.5em 1em 3em}
header{display:flex;align-items:center;gap:.75em;padding:.25em 0 1em}
header img{height:40px;width:auto}
header .brand{font-weight:600;letter-spacing:.02em}
header .brand small{display:block;font-weight:400;color:var(--muted);font-size:.8em}
main{background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:1.5em 1.75em}
h1{font-size:1.4em;margin:0 0 .5em}
p{margin:.6em 0}
code{font-size:1.3em;background:var(--bg);border:1px solid var(--border);padding:.15em .5em;border-radius:.3em}
.amount{font-size:1.6em;font-weight:600;margin:.4em 0}
.muted{color:var(--muted)}
.ok{color:var(--ok)}
.button,button{display:inline-block;background:var(--accent);color:#fff;border:0;padding:.6em 1.2em;border-radius:.5em;font-size:1em;text-decoration:none;cursor:pointer}
.button:hover,button:hover{background:var(--accent-hover)}
table.receipt{border-collapse:collapse;width:100%;margin:.5em 0}
table.receipt th{text-align:left;font-weight:500;color:var(--muted);padding:.3em 1em .3em 0;white-space:nowrap;vertical-align:top}
table.receipt td{padding:.3em 0}
table.receipt tr.total td,table.receipt tr.total th{border-top:1px solid var(--border);padding-top:.6em;font-weight:600;color:var(--text)}
footer{padding:1em .25em;color:var(--muted);font-size:.85em}
footer a{color:inherit}
"""

LOGO_LIGHT = "logo-light.png"
LOGO_DARK = "logo-dark.png"


def logo_html(static_base: str) -> str:
    """The mark: the light picture, the dark one where the browser prefers a dark look."""
    base = html.escape(static_base.rstrip("/"))
    return (
        f'<picture><source srcset="{base}/{LOGO_DARK}" media="(prefers-color-scheme: dark)">'
        f'<img src="{base}/{LOGO_LIGHT}" alt="ULTRA OS" width="77" height="40"></picture>'
    )


def page(title: str, body: str, *, static_base: str = "/pay/static", heading: str | None = None, footer: str = "") -> str:
    """A whole page: the frame, the mark, the title as the heading (or
    `heading` instead) and the body HTML inside the card."""
    h1 = html.escape(title if heading is None else heading)
    foot = footer or 'UltraStore, the ULTRA OS app store. Questions about a purchase: the receipt names the order.'
    return (
        "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        f"<title>{html.escape(title)} - UltraStore</title>\n"
        f"<link rel=\"stylesheet\" href=\"{html.escape(static_base.rstrip('/'))}/style.css\">\n"
        "</head>\n<body>\n<div class=\"wrap\">\n"
        f"<header>{logo_html(static_base)}<div class=\"brand\">UltraStore<small>ULTRA OS</small></div></header>\n"
        f"<main>\n<h1>{h1}</h1>\n{body}\n</main>\n"
        f"<footer>{foot}</footer>\n</div>\n</body>\n</html>\n"
    )


KIND_NAMES = {"sale": "Purchase", "renewal": "Renewal", "refund": "Refund", "chargeback": "Chargeback"}


def _receipt_rows(order, payment) -> list[tuple[str, str]]:
    kind = payment.kind if payment else "sale"
    rows = [("Date", payment.at if payment else order.paid_at), ("Order", order.id),
            ("App", f"{order.app_name} ({order.app})"), ("Kind", KIND_NAMES.get(kind, kind))]
    if kind == "sale":
        rows.append(("List price", f"{order.list_price} {order.currency}"))
        if order.code_applied:
            rows.append(("Code", f"{order.code} applied"))
    if order.cycle:
        rows.append(("Billing", f"{order.list_price} {order.currency} every {order.cycle}"))
        if kind in ("sale", "renewal") and order.paid_until:
            rows.append(("Paid until", order.paid_until))
    rows += [("Skrill transaction", (payment.skrill_transaction if payment else order.skrill_transaction) or "-"), ("Licence", order.licence_id)]
    return rows


def _receipt_total(order, payment) -> tuple[str, str]:
    kind = payment.kind if payment else "sale"
    amount = payment.amount if payment else order.amount
    return ("Refunded" if kind in ("refund", "chargeback") else "Paid"), f"{amount} {order.currency}"


def receipt_html(order, payment=None) -> str:
    """The receipt of one payment as a page, for a browser (the sale when
    `payment` is not given); receipt_text is the same for a terminal."""
    no = payment.receipt_no if payment else order.receipt_no
    table = "".join(f"<tr><th>{html.escape(k)}</th><td>{html.escape(v)}</td></tr>" for k, v in _receipt_rows(order, payment))
    label, total = _receipt_total(order, payment)
    table += f"<tr class=\"total\"><th>{label}</th><td>{html.escape(total)}</td></tr>"
    body = f"<table class=\"receipt\">{table}</table>\n<p class=\"muted\">Thank you for buying through UltraStore. Print or save this page for your records.</p>"
    return page(f"Receipt no. {no}", body)


def receipt_text(order, payment=None) -> str:
    no = payment.receipt_no if payment else order.receipt_no
    lines = [f"UltraStore receipt no. {no}"] + [f"{k}: {v}" for k, v in _receipt_rows(order, payment)]
    label, total = _receipt_total(order, payment)
    lines += [f"{label}: {total}", "", "Thank you for buying through UltraStore."]
    return "\n".join(lines) + "\n"


def wants_html(accept: str | None) -> bool:
    """A browser asks for text/html first; a store, a script or a test does not."""
    if not accept:
        return False
    for part in accept.split(","):
        kind = part.split(";")[0].strip().lower()
        if kind == "text/html":
            return True
        if kind in ("text/plain", "application/json", "*/*"):
            return False
    return False
