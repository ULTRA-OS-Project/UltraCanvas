// include/UltraDatabase/UltraDatabaseTransaction.h
// All-or-nothing transactions. A transaction handle is bound to one named
// connection; statements run through it participate in the same transaction
// until Commit or Rollback. In Stage 1 a connection's transaction should be
// driven from a single thread.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabaseCore.h"
#include "UltraDatabaseValue.h"

// Begin a transaction on the named connection (BEGIN IMMEDIATE for SQLite).
// Returns a transaction handle, or UltraDbInvalidHandle on failure.
UltraDbHandle UltraDb_Begin(const std::string& connection,
                            UltraDbResult* error = nullptr);

// Run a non-row-returning statement inside the transaction.
UltraDbResult UltraDb_ExecInTx(UltraDbHandle transaction,
                               const std::string& sql,
                               const UltraDbParams& params = {});

// Run a row-returning statement inside the transaction.
UltraDbResult UltraDb_QueryInTx(UltraDbHandle transaction,
                                const std::string& sql,
                                const UltraDbParams& params,
                                UltraDbResultSet& out);

// Commit the transaction and release the handle.
UltraDbResult UltraDb_Commit(UltraDbHandle transaction);

// Roll the transaction back and release the handle.
UltraDbResult UltraDb_Rollback(UltraDbHandle transaction);
