// Apps/UltraDBAdmin/ui/DbAdminConnectionDialog.h
// Add or edit a connection: a SQLite file, or a PostgreSQL or MySQL /
// MariaDB server with its user, password and TLS posture - reached directly
// or through an SSH tunnel: the bastion, its user, how to sign in there (the
// SSH agent, a password, a private key file) and the host key trusted. The form shows the rows of the chosen type
// only. A password typed here goes to the caller, which puts it into the
// vault (ConnectionStore::Put); editing a connection leaves its stored
// password alone unless a new one is typed or "Forget" is ticked - the
// password itself is never shown back.
//
// The dialog stays open on a mistake: Save asks the caller, and closes only
// when it reports no error.
// Version: 0.3.0 - SSH tunnels
// Version: 0.2.0 - MySQL / MariaDB
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminConnections.h"

#include <functional>
#include <string>

namespace UltraCanvas { class UltraCanvasWindowBase; }

namespace UltraDBAdmin {

// The edited connection and the secrets to change in the vault (a field left
// empty keeps the stored one). Returns an error to show, or "" to close.
using SaveConnectionFn = std::function<std::string(const ConnectionSpec& spec,
                                                   const ConnectionStore::SecretChanges& secrets)>;

// `existing` null: a new connection. Non-blocking; `onSave` runs on Save.
void ShowConnectionDialog(UltraCanvas::UltraCanvasWindowBase* parent,
                          const ConnectionSpec* existing,
                          SaveConnectionFn onSave);

} // namespace UltraDBAdmin
