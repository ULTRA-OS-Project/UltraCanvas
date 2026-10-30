// Apps/UltraMail/ui/UltraMailMessageImport.h
// Import: a message saved to a file - an .eml, Apple Mail's .emlx, an
// Outlook .msg - as the RFC 5322 message UltraMail uploads into a folder of
// the account on screen (IMailboxProtocolPlugin::AppendMessage), where the
// next sync of that folder brings it into the list like any other message.
//
// Headless; it lives with the app rather than in the engine because the .msg
// reader (UltraCanvasOutlookMessage.h) is part of the UltraCanvas library,
// which only the app links.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraMail {

// The extensions Import reads ("eml", "emlx", "msg"), for the Open dialog.
const std::vector<std::string>& ImportableMessageExtensions();

// Whether Import reads a file of this name (by its extension, any case).
bool IsImportableMessageFile(const std::string& fileName);

// A message file's bytes as an RFC 5322 message:
//   - an .eml as it is;
//   - the message inside an .emlx (after its length line, before Mail's
//     property list);
//   - an Outlook .msg built again as MIME: its sender, recipients, subject,
//     date and Message-ID, the HTML body with the plain text as its
//     alternative (or the plain text alone), the pictures the HTML shows as
//     inline parts with their Content-ID, the attachments. An attached
//     message (Outlook's "attach item") is not carried over.
// False, with `error` saying why, when the file is not a message.
bool MessageFileToRfc822(const std::string& fileName, const std::vector<uint8_t>& bytes,
                         std::string& raw, std::string& error);

} // namespace UltraMail
