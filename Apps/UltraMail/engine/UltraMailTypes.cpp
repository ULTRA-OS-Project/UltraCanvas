// Apps/UltraMail/engine/UltraMailTypes.cpp
// FolderRole / MailSecurity / authentication method <-> string mapping.
// Version: 0.2.0 - authentication method
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailTypes.h"

namespace UltraMail {

std::string ToString(MailSecurity security) {
    switch (security) {
        case MailSecurity::Plain:     return "none";
        case MailSecurity::StartTls: return "starttls";
        case MailSecurity::SslTls:   return "ssl";
    }
    return "ssl";
}

MailSecurity MailSecurityFromString(const std::string& s) {
    if (s == "none")     return MailSecurity::Plain;
    if (s == "starttls") return MailSecurity::StartTls;
    return MailSecurity::SslTls;
}

std::string ToString(FolderRole role) {
    switch (role) {
        case FolderRole::Inbox:   return "inbox";
        case FolderRole::Sent:    return "sent";
        case FolderRole::Drafts:  return "drafts";
        case FolderRole::Junk:    return "junk";
        case FolderRole::Trash:   return "trash";
        case FolderRole::Archive: return "archive";
        case FolderRole::Normal:
        default:                  return "normal";
    }
}

std::string ToString(UltraNetMailAuth auth) {
    switch (auth) {
        case UltraNetMailAuth::Password:          return "password";
        case UltraNetMailAuth::EncryptedPassword: return "encrypted";
        case UltraNetMailAuth::OAuth2:            return "oauth2";
        case UltraNetMailAuth::Kerberos:          return "kerberos";
        case UltraNetMailAuth::NTLM:              return "ntlm";
        case UltraNetMailAuth::None:              return "none";
        case UltraNetMailAuth::Any:
        default:                                  return "auto";
    }
}

UltraNetMailAuth MailAuthFromString(const std::string& s) {
    if (s == "password")  return UltraNetMailAuth::Password;
    if (s == "encrypted") return UltraNetMailAuth::EncryptedPassword;
    if (s == "oauth2")    return UltraNetMailAuth::OAuth2;
    if (s == "kerberos")  return UltraNetMailAuth::Kerberos;
    if (s == "ntlm")      return UltraNetMailAuth::NTLM;
    if (s == "none")      return UltraNetMailAuth::None;
    return UltraNetMailAuth::Any;
}

void ApplyConnection(const MailServerSettings& server, UltraNetMailOptions& options) {
    options.useTls      = server.security != MailSecurity::Plain;
    options.implicitTls = server.security == MailSecurity::SslTls;
    options.auth        = server.auth;
}

FolderRole FolderRoleFromString(const std::string& s) {
    if (s == "inbox")   return FolderRole::Inbox;
    if (s == "sent")    return FolderRole::Sent;
    if (s == "drafts")  return FolderRole::Drafts;
    if (s == "junk")    return FolderRole::Junk;
    if (s == "trash")   return FolderRole::Trash;
    if (s == "archive") return FolderRole::Archive;
    return FolderRole::Normal;
}

} // namespace UltraMail
