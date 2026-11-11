- **The payment service runs on PostgreSQL in production**; SQLite is for
  tests and development only. `ULTRASTORE_PAY_DB` is a database URL, one
  database layer hides the two engines, and the tests run the whole
  purchase on both, PostgreSQL through a service container in CI.
