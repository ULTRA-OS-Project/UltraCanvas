# UltraDBAdmin icons

The interface icons of `Apps/UltraDBAdmin`, loaded by name through
`IconPath` in `Apps/UltraDBAdmin/ui/DbAdminTheme.h`. All are drawn on a
16 px grid.

- **Buttons** (`add`, `edit`, `delete`, `refresh`, `previous`, `next`,
  `import`, `export`, `preview`, `revert`, `apply`, `run`, `stop`,
  `rename`, `key`, `filter`, `clear`, `copy`): one colour, a 1.5 px stroke with round caps and joins.
  A button draws them as a mask in its text colour, so the colour in the
  file does not matter.
- **Tree** (`node-connection`, `node-connection-production`, `node-schema`,
  `node-table`, `node-view`, `node-materialized-view`,
  `node-foreign-table`, `node-sequence`): in colour, drawn as they are,
  one colour per kind.
- **Tabs** (`tab-browse`, `tab-structure`, `tab-sql`): one muted grey,
  drawn as they are.

The packaged app icon is `media/appicon/UltraDBAdmin.{png,svg}`, not here.
