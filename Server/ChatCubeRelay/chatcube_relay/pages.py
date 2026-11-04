"""The pages the e-mailed links open: confirming an address, approving a
recovery, and what came of it. One small HTML page in ChatCube's look,
self-contained (no script, no font or picture from elsewhere - the relay may
sit on a network that reaches nothing else), light or dark as the browser is.
Every text put in is escaped. The referrer policy is same-origin, not
no-referrer: under no-referrer a browser sends the approval form with
"Origin: null", and the relay refuses a form whose origin is not its own."""

from __future__ import annotations

from html import escape

_PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="referrer" content="same-origin">
<title>{title} - ChatCube</title>
<style>
:root {{ --ground:#f4f5f2; --panel:#ffffff; --ink:#1d2321; --muted:#5d6763; --line:#dfe3df;
        --accent:#22894a; --onaccent:#ffffff; --danger:#b3261e; }}
@media (prefers-color-scheme: dark) {{
  :root {{ --ground:#151917; --panel:#1e2421; --ink:#e7ebe8; --muted:#a3ada8; --line:#323a36;
          --accent:#3fae69; --onaccent:#0b130e; --danger:#f2b8b5; }}
}}
* {{ box-sizing: border-box; }}
body {{ margin:0; min-height:100vh; display:flex; align-items:center; justify-content:center;
       background:var(--ground); color:var(--ink); padding:16px;
       font: 16px/1.5 system-ui, -apple-system, "Segoe UI", Ubuntu, sans-serif; }}
main {{ background:var(--panel); border:1px solid var(--line); border-radius:14px; padding:28px 28px 22px;
       max-width:440px; width:100%; }}
.brand {{ font-weight:700; color:var(--accent); letter-spacing:.02em; margin:0 0 14px; }}
h1 {{ font-size:22px; line-height:1.3; margin:0 0 10px; }}
h1.bad {{ color:var(--danger); }}
p {{ margin:0 0 12px; color:var(--muted); }}
p strong {{ color:var(--ink); }}
button {{ font:inherit; font-weight:600; width:100%; margin-top:8px; padding:11px 16px; border:0;
         border-radius:8px; background:var(--accent); color:var(--onaccent); cursor:pointer; }}
button:focus-visible {{ outline:3px solid var(--ink); outline-offset:2px; }}
</style>
</head>
<body>
<main>
<p class="brand">ChatCube</p>
{body}
</main>
</body>
</html>
"""


def page(title: str, paragraphs: list[str], bad: bool = False, form: str = "") -> str:
    """`paragraphs` are plain text (escaped here); `form` is HTML made by
    approve_form(), which escapes what it puts in."""
    heading = f'<h1 class="bad">{escape(title)}</h1>' if bad else f"<h1>{escape(title)}</h1>"
    body = heading + "".join(f"<p>{escape(text)}</p>" for text in paragraphs) + form
    return _PAGE.format(title=escape(title), body=body)


def approve_form(token: str) -> str:
    # A POST, so that opening the link - which mail scanners do by themselves
    # - approves nothing.
    return ('<form method="post" action="recover">'
            f'<input type="hidden" name="token" value="{escape(token, quote=True)}">'
            '<button type="submit">Yes - sign that computer in as my ChatCube ID</button></form>')
