# ultrastore-db

The database layer of the UltraStore services - the payment service
(`Server/UltraStorePay`) and the marketplace service
(`Server/UltraStoreMarket`): one small interface over **MySQL** or
**PostgreSQL** in production and **SQLite** for tests and development only.

The URL names the engine: `mysql://user:password@host:3306/db` (PyMySQL,
the `mysql` extra; `?unix_socket=/path` for a socket), `postgresql://user:
password@host:5432/db` (psycopg 3, the `postgres` extra), or
`sqlite:///path/to/file.sqlite3` (a bare path means the same). A service
writes one SQL dialect with `?` placeholders; what cannot be hidden in the
statement is a method here:

| | |
|---|---|
| `serial` | the column type of an integer that counts up on its own (`BIGINT AUTO_INCREMENT`, `BIGSERIAL`, `INTEGER`) |
| `text_key` | a text column that is a key or indexed (`VARCHAR(191)` on MySQL, `TEXT` elsewhere) |
| `upsert(table, key, value)` | the INSERT that replaces the value when the key is there (`ON DUPLICATE KEY UPDATE`, `ON CONFLICT DO UPDATE`) |
| `ident(name)` | a name quoted, for one that is a reserved word somewhere (`key` on MySQL) |
| `concat(a, b, …)` | one string from these expressions (`CONCAT()`, `\|\|`) |
| `create_index(name, table, columns)` | an index, made once (MySQL has no `IF NOT EXISTS` for it) |
| `columns(table)` | the table's columns, for a schema upgrade |
| `execute`, `script`, `fetchone`, `fetchall`, `last_id`, `begin`, `commit`, `rollback`, `close` | the statements; rows are dicts |

Install it beside a service: `pip install -e ../UltraStoreDb[postgres,mysql]`.

`ultrastore_db.operator_session` is the one other thing the services
share: the operator's signed session (`issue`, `verify`: `op1.<payload>.
<hmac>` with a secret both services are configured with) and the roles
with what each may do (`PERMISSIONS`, `allowed`) - the marketplace service
keeps the operator accounts and signs the sessions, the account service
verifies them, so one sign-in names the person and the role to both.

The tests run on SQLite always and on PostgreSQL and MySQL too when
`ULTRASTORE_TEST_PG_URL` and `ULTRASTORE_TEST_MYSQL_URL` name one each:

```
python3 -m venv .venv && .venv/bin/pip install -e '.[test,postgres,mysql]'
ULTRASTORE_TEST_MYSQL_URL=mysql://ultrastore:ultrastore@127.0.0.1:3306/ultrastore_test .venv/bin/python -m pytest -q
```
