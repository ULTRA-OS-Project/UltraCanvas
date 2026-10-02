// Apps/UltraMail/engine/UltraMailRichComposer.h
// Replies and forwards that keep an HTML message's formatting. The original's
// HTML becomes an editable UCRichDocument (HTMLRichDocumentImporter): headings,
// lists, tables, colours, links and pictures stay what they were, a reply's
// quote is the document's quote level (a bar at the left, <blockquote
// type="cite"> when sent). For sending, the document is written back out as
// HTML with its pictures as cid: parts, plus a plain-text version.
// Version: 0.2.0 - MakeRichEdit: a queued HTML message back in the editor
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailComposer.h"

namespace UltraMail {

// Gives a reply built by Composer::Reply the original's formatting: when
// `src` has an HTML body, draft.richBody holds an empty line to write in,
// the attribution line ("On <date>, <sender> wrote:") and the original as a
// quote, its pictures included. Returns false - the draft unchanged - for a
// plain-text message, whose reply stays the "> "-quoted text.
bool MakeRichReply(Draft& draft, const SourceMessage& src);

// The same for a forward: an empty line, the forwarded-message header (From,
// Date, Subject, To), then the original as it was - not quoted.
bool MakeRichForward(Draft& draft, const SourceMessage& src);

// A message written earlier and opened again to correct it (the outbox
// window's Edit): when its body is HTML, draft.richBody holds it as an
// editable document, its cid: pictures taken from draft.inlineParts. Returns
// false - the draft unchanged - for a plain-text body.
bool MakeRichEdit(Draft& draft);

// For sending: body = draft.richBody as an HTML page whose pictures point to
// cid: parts, inlineParts = those parts, textBody = the plain text (quotes
// as "> "), bodyIsHtml = true. Does nothing without a richBody.
void RenderRichBody(Draft& draft);

} // namespace UltraMail
