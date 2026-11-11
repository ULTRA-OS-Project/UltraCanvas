# UltraCanvasOutlookMessage — Outlook .msg messages

<!-- doc-check: std::string path; std::vector<uint8_t> bytes; -->

`UltraCanvasOutlookMessage.h` reads the file Outlook writes for *Save as* and
for a message dragged out of it (`.msg`, [MS-OXMSG]) into what a reader shows:
the subject, the sender, the recipients, the date, the body and the
attachments. Headless and read-only. The
[media viewer](UltraCanvasMediaViewer.md) shows a `.msg` through it, as it
shows an `.eml`, and UltraMail's *Import* builds a standard message from it.

```cpp
#include "UltraCanvasOutlookMessage.h"

UltraCanvas::UCOutlookMessage msg;
std::string error;
if (msg.Load(path, error)) {                   // UTF-8 path; or LoadFromMemory(bytes, error)
    std::string title = msg.subject;
    std::string sender = msg.from;             // "Name <address>"
    for (const std::string& to : msg.to) { (void)to; }
    for (const std::string& cc : msg.cc) { (void)cc; }
    std::string when = msg.date;               // "Thu, 29 Oct 2026 09:00:00 +0000"
    std::string body = msg.bodyHtml.empty() ? msg.bodyText : msg.bodyHtml;   // UTF-8
    for (const UltraCanvas::UCOutlookAttachment& a : msg.attachments) {
        // a.filename, a.mediaType, a.data; a.contentId and a.hidden for a
        // picture the HTML body shows (cid:); a.embeddedMessage for an
        // attached message, listed by name with no bytes
        (void)a;
    }
} else {
    // error: "Not an OLE2 compound file", "the compound file holds no Outlook message", ...
}
```

## What is read, and from where

A `.msg` is a [compound file](UltraCanvasCompoundFile.md): every property of
the message is a stream named after its tag (`__substg1.0_0037001F` is the
subject, as UTF-16), the fixed-size ones (times, the code page) are entries of
the `__properties_version1.0` stream, and every recipient and attachment is a
storage of its own (`__recip_version1.0_#00000000`,
`__attach_version1.0_#00000000`) laid out the same way.

| Field | Read from |
|---|---|
| `subject` | `PR_SUBJECT` |
| `messageId`, `inReplyTo` | `PR_INTERNET_MESSAGE_ID`, `PR_IN_REPLY_TO_ID` (empty for a message that never went through the Internet, a draft) |
| `from` | `PR_SENDER_NAME` with `PR_SENDER_SMTP_ADDRESS` (else `PR_SENDER_EMAIL_ADDRESS` unless it is an Exchange address, `/O=...`); a message with neither: `PR_SENT_REPRESENTING_*` |
| `to`, `cc` | each recipient's display name and SMTP address, by `PR_RECIPIENT_TYPE` (Bcc left out); `PR_DISPLAY_TO` / `PR_DISPLAY_CC` when there are no recipient storages |
| `date` | delivered (`PR_MESSAGE_DELIVERY_TIME`), else sent (`PR_CLIENT_SUBMIT_TIME`); a draft's last change, else its creation; UTC |
| `bodyHtml` | `PR_HTML`; else the HTML encapsulated in the compressed RTF body (below) |
| `bodyText` | `PR_BODY` |
| attachments | long name, else 8.3 name, else display name; `PR_ATTACH_MIME_TAG` (lower-cased), `PR_ATTACH_CONTENT_ID` (without `<>`), `PR_ATTACHMENT_HIDDEN`; the bytes of `PR_ATTACH_DATA_BIN`; an attached message (method 5, or a `3701000D` storage) is `embeddedMessage` |

Strings stored as UTF-16 - what Outlook writes since 2003 - are read as they
are. 8-bit strings (`001E`), a binary `PR_HTML` and RTF text are read in the
message's code page (`PR_INTERNET_CPID`, else `PR_MESSAGE_CODEPAGE`) through
`DecodeWindowsCodePage` (`UltraCanvasTextUtils.h`): 1250, 1251, 1252 and
UTF-8; any other code page as Latin-1.

## The HTML body inside the RTF

Most messages that arrived as HTML keep their HTML only inside the compressed
RTF body (`PR_RTF_COMPRESSED`), which is why `PR_HTML` is often missing. Two
functions get it back, and are public for anyone with such a stream:

- `UCDecompressRtf(compressed, rtf)` — [MS-OXRTFCP]: an `LZFu` stream (the
  4096-byte dictionary, its 207-byte initial text, references and literals)
  or a `MELA` one stored uncompressed. The CRC is not checked: a damaged
  stream gives what it holds, up to its stated size.
- `UCHtmlFromEncapsulatedRtf(rtf, html)` — [MS-OXRTFEX]: for RTF whose header
  says `\fromhtml1`, the `{\*\htmltag ...}` groups as written plus the text
  outside `\htmlrtf ... \htmlrtf0`, with `\par`, `\tab`, `\'hh` (in the
  `\ansicpg` code page), `\u` (its `\uc` fallback skipped) and the escaped
  `\{ \} \\`; `\fonttbl`, `\colortbl` and the other non-HTML destinations, and
  `\*\mhtmltag`, are left out. False for RTF that never was HTML (a message
  written in Outlook's rich text), whose plain text `PR_BODY` always holds.

## Limits

- Code pages beyond 1250, 1251, 1252 and UTF-8 (Japanese, Chinese, Greek,
  ...) are read as Latin-1 in 8-bit text; their UTF-16 strings are fine.
- An attached message is listed, not read: its own storage is a message of
  its own that a caller could read the same way.
- Signed or encrypted messages (S/MIME, `.p7m` attachments) come out as the
  attachment they are.

## Test

`Tests/OutlookMessageTest.cpp` builds messages with `UCCompoundFileWriter` as
Outlook lays them out - three recipients, an Exchange sender address beside
the SMTP one, an LZFu body with a dictionary reference holding encapsulated
HTML, a `PR_HTML` body, an attachment by value, a hidden picture with an 8-bit
name and a Content-ID, an attached message - and reads them back, with the
compound file's paths below the root and `DecodeWindowsCodePage`. Two files
Outlook wrote (the msg-extractor project's `unicode.msg`, with two TIFF
attachments, and `strangeDate.msg`, a draft whose 41 KB of HTML is only in its
RTF body) were checked by hand when the reader was written.
