- **The marketplace service** (`Server/UltraStoreMarket`, milestone 8):
  the operator tier's records in one service - the merchants with their
  keys, signing every request; the submissions with the service's own
  checks and every decision appended; the reports through the three doors
  (the store, a merchant, the operator), the reporter kept from the
  merchant; the notices, the strikes and the enforcement policy that
  turns them into a suspension; suspensions by hand and by the policy;
  the terms versions and acceptances, a merchant paused without the one in
  force; the hash-chained audit log with `GET /v1/audit/verify`;
  `GET /v1/released` for the catalog. Tested on SQLite, MySQL and
  PostgreSQL.
- **One database layer for the services** (`Server/UltraStoreDb`,
  `ultrastore-db`): MySQL joins PostgreSQL as a production engine, SQLite
  stays for tests and development. The marketplace service runs on MySQL
  (decided 2026-11-11); the payment service takes the layer too and runs
  its tests on all three engines (`ULTRASTORE_PAY_TEST_MYSQL_URL`).
