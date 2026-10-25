- **UltraDatabase: the server's status, for a monitor.**
  `UltraDb_ServerStatus` (`UltraDatabaseServer.h`) gives a row per measure a
  PostgreSQL or MySQL / MariaDB server keeps - the sessions connected and
  running, and counters of statements, transactions, rows read and written,
  buffer cache reads and misses and client traffic - to be read every few
  seconds and charted as rates.
- **Chart engine: `UltraCanvasEngineLineChart::SetValueAtLeast`.** The value
  axis reaches at least the value given whatever the data, so a live chart of
  small counts or rates at rest reads 0 to that value instead of an axis
  fitted to a flat zero.
