"""The database layer moved to Server/UltraStoreDb (ultrastore-db): MySQL or
PostgreSQL in production, SQLite for tests and development only. This
module keeps the old import working.

Version: 0.2.0 - the layer is ultrastore_db's
Version: 0.1.1 - columns(), for the schema upgrade
Version: 0.1.0
"""
from ultrastore_db import Database

__all__ = ["Database"]
