// Apps/UltraMail/engine/UltraMailSignature.h
// An account's signature in the messages it writes. The signature goes below
// the line the message is written on and above anything quoted or forwarded,
// so it follows the answer rather than the whole thread.
//   * Plain text: a "-- " line (the usenet signature separator, which mail
//     programs recognise and fold away), then the text.
//   * HTML: the signature's own formatting - fonts, colours, links, pictures -
//     which makes the message a formatted one: a plain-text draft becomes a
//     rich document first, its "> " quotes turned into quote levels.
// Pure logic, no UI.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailComposer.h"   // Draft
#include "UltraMailTypes.h"      // Signature

#include <memory>
#include <string>

namespace UltraCanvas { class UCRichDocument; }

namespace UltraMail {

// Which message a draft is: new mail, or an answer to / forward of another.
enum class DraftPurpose {
    NewMessage,
    ReplyOrForward
};

// Puts `signature` into `draft`. Returns false - the draft unchanged - when
// the signature is not active, or is kept off replies and forwards and the
// draft is one.
bool ApplySignature(Draft& draft, const Signature& signature, DraftPurpose purpose);

// The HTML signature as an editable document (what the signature editor
// shows). Its pictures come from their data: URIs.
std::shared_ptr<UltraCanvas::UCRichDocument> SignatureDocument(const std::string& html);

// The document the signature editor holds, as the HTML stored with the
// account: a fragment, its pictures inline as data: URIs. "" when the
// document shows nothing - no text and no picture.
std::string SignatureHtmlFrom(const UltraCanvas::UCRichDocument& document);

// A plain-text body as a rich document: one paragraph per run of lines,
// a blank line between paragraphs, and "> " quotes as the quote level. The
// first block is always the line the message is written on.
std::shared_ptr<UltraCanvas::UCRichDocument> PlainBodyToRichDocument(const std::string& body);

// The signature for the account settings page, in a few words:
// "None", "Plain text · Erika Example", "HTML · Erika Example" (its first
// line, shortened).
std::string DescribeSignature(const Signature& signature);

} // namespace UltraMail
