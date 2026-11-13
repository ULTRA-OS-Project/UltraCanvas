#### 2026-11-12 *0.1.1*
- The Connection page shows the key the server signs its lists with, to
  give to users who want to check what their client pinned.
- Every text of the window goes through `Tr` (`media/locale/<lang>/UltraAIServerAdmin.json`).

#### 2026-11-06 *0.1.0*
- **UltraAI ServerAdmin, the super administrator's tool for an UltraAI model
  server** (`Server/UltraAIModelServer`): which AI models UltraAI clients may
  install from the server. A window and a command line
  (`ultraai-serveradmin <command>`) on one client of the server's admin API.
  - *Models*: every model with its state (draft, published, withdrawn); the
    form with everything the catalogue carries (service, engine, licence,
    memory and GPU memory needed, context window, tool calls, pictures,
    tags, engine arguments); its files, uploaded from this computer (hashed
    first, verified by the server, one it already has not sent again) or
    hosted elsewhere by address; what still stands in the way of
    publishing; Publish, Withdraw, Delete.
  - *Topics*: what each kind of work (chat, code, translation, summaries,
    pictures, search, or a topic of your own) is offered, best first, with
    its default.
  - *Engines* (the gallery's bundles, as JSON), *Uploads* (delete the ones
    no model uses), *Administrators* (editor and super accounts, a new token
    shown once) and the *Audit log*.
