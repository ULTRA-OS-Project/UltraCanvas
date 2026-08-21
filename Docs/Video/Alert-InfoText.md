# UltraCanvas Demo — Info Text: Alert / Message Box

YouTuber-style narration for the "Alert / Message Box" page of the UltraCanvas
demo application (Basic UI Elements section).

---

Okay, next element — and this one is all about getting the user's attention:
the Alert, also known as the message box. Sometimes your application simply has
to interrupt: a file couldn't be saved, an export just finished, or you need a
straight yes-or-no before deleting something. That's what this is for.

An UltraCanvas Alert is a modal, always-on-top dialog. Modal means the rest of
the window is blocked until the user answers — it can't be scrolled past, it
can't be ignored, it sits right in the middle of your screen. That's the whole
point: an alert is deliberately interruptive. And a quick pro tip right away:
if you *don't* want to interrupt — just a quick "saved!" that fades away — the
framework has Toasts for that. Alerts are for the moments that really need an
answer.

The demo page is simple: a column of buttons, each one fires a different kind
of alert, and down at the bottom a status line shows you which button the user
actually pressed — live, through the callback.

Let's go through them. The first four are the severities: Info, Success,
Warning, and Error. Each one gets its own icon and accent color — Info is a
blue "i", Success a green check mark, Warning an amber exclamation mark, Error
a red X. So the user reads the situation before reading a single word. And in
code? These are literally one-liners: `UltraCanvasAlert::Info`, `Successful`,
`Warning`, `Error` — pass the message, done. The title is optional; leave it
empty and it's derived from the severity.

Quick fun fact for the C++ nerds: the success one is called `Successful`, not
`Success` — because on Linux, X11 has had `#define Success 0` since roughly
forever, and nobody wins a fight against a thirty-year-old macro.

Important detail: even though the *dialog* is modal, the *API call* is
non-blocking. Your main loop keeps running; the user's answer arrives through a
callback. No frozen event loop, no re-entrancy tricks — you just get a
`DialogResult` when they click.

Button five is the classic confirmation: `UltraCanvasAlert::Confirm`. Question
icon, Yes and No buttons, and your callback gets a plain bool — true, they
confirmed; false, they didn't. "Delete this item permanently?" — that's the
one-liner you'll use a hundred times.

Now for the good stuff: the rich form. Instead of a one-liner you fill an
`AlertOptions` struct — severity, title, message, an extra details line, which
buttons to show, which one is the default — and call `Show`. That's what you
see in this "Update available" dialog: Warning severity, OK/Cancel buttons,
custom title. But look closer at the text — see the bold "UltraCanvas", the
code-styled version number, the italics? The message is rendered as
**Markdown**. You just write Markdown straight into the string and the dialog
formats it.

And the last button pushes that to the limit: a full release-notes document —
headers, bullet lists, numbered steps, a block quote — inside an alert. The
dialog auto-sizes its height to fit the text, and if the content would grow
past your screen, the message area gets its own scrollbar instead of clipping.
Long text just works.

Keyboard support comes for free, by the way: Enter fires the default button,
Escape cancels, and every button has an underlined mnemonic letter — you can
see the underlined O on the OK button. And one more thing: alerts go through
the UltraCanvas dialog manager, so with one global switch they render either as
native OS message boxes or as these fully styled internal UltraCanvas dialogs —
same code either way.

So that's the Alert: five severities, one-line calls for the simple cases, a
rich options form with Markdown, auto-sizing and scrolling for the big ones,
Yes/No confirmation with a bool callback — and all of it non-blocking, modal,
and impossible to miss. Exactly what a message box should be.
