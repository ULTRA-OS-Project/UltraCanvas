// Apps/UltraPassword/core/CsvExchange.h
// Plain CSV in and out, for moving passwords between UltraPassword and other
// password managers and browsers.
//
// The encrypted vault file is the normal export (VaultFile::ExportCopy). CSV
// is the one deliberate exception: every password readable by anything that
// opens the file. The window asks for confirmation before writing one; this
// layer only does the conversion.
//
// Export columns: name,url,username,password,note,group,signin_method,sso_provider
// — the first five are the ones Chrome, Edge, Firefox and Bitwarden read, so
// the file imports there as it is.
//
// Import accepts the column names those programs write (name/title,
// url/login_uri, username/login_username, password/login_password,
// note/notes/extra, group/folder/grouping) in any order, and RFC 4180 quoting
// with embedded commas, quotes and line breaks. A "group" value of
// "Work / Cloud" creates nested groups.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef ULTRAPASSWORD_CSVEXCHANGE_H
#define ULTRAPASSWORD_CSVEXCHANGE_H

#include "PasswordVault.h"

#include <string>

namespace UltraPassword {

// Stable text keys for the sign-in method column ("passkey", "password+totp").
std::string SignInMethodKey(SignInMethod method);
bool SignInMethodFromKey(const std::string& key, SignInMethod& out);

// The whole vault as CSV text. The caller wipes the string (WipeString) once
// it has been written out.
std::string ExportCsv(const PasswordVault& vault);

// Adds the rows of `csv` to `vault` under `intoGroupId`, creating the groups
// the rows name. Returns the number of entries added, or -1 with `error` set
// when the text has no recognisable header row.
int ImportCsv(const std::string& csv, PasswordVault& vault,
              const std::string& intoGroupId, std::string& error);

// File helpers. Write is atomic and owner-only, like the vault file.
bool WriteCsvFile(const std::string& path, const PasswordVault& vault, std::string& error);
int  ReadCsvFile(const std::string& path, PasswordVault& vault,
                 const std::string& intoGroupId, std::string& error);

} // namespace UltraPassword

#endif // ULTRAPASSWORD_CSVEXCHANGE_H
