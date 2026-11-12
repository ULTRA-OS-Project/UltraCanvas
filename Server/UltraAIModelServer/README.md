# The UltraAI model server

The server UltraAI clients install their local models from, and the
database behind it: a small [FastAPI](https://fastapi.tiangolo.com/)
service on SQLite that

- publishes what every UltraAI client reads - the **gallery index** (the
  inference engines and their bundles per platform), the **model
  catalogue** (the models a client may install) and the **topics** (which
  models are offered for chat, code, translation, summaries, pictures,
  search, ..., and the default of each) - in the formats of
  [`Docs/Modules/UltraAI/LocalRuntime.md`](../../Docs/Modules/UltraAI/LocalRuntime.md);
- stores and serves the **model files** the administrator uploads (hashed
  as they arrive, served with range requests so a client resumes a broken
  download), beside files hosted elsewhere (Hugging Face, a CDN) by address;
- has the **admin API** that [UltraAI ServerAdmin](../../Docs/UltraAIServerAdmin/README.md)
  works through: models from draft to published to withdrawn, their files,
  the topics, the engines, the administrator accounts and an audit log.

A client is pointed at the server under UltraAI Settings > Model topics >
Model server (`https://<server>/gallery/index.json`, or just the server's
address); *Refresh* fetches the three documents and downloads what the
user's topic settings call for.

## The life of a model

1. **Draft.** An administrator creates the model (`PUT /v1/admin/models/{id}`)
   with what the catalogue carries: name, service (`textllm`, `embeddings`,
   ...), engine, licence, memory needs, features, tags, engine arguments.
   Clients do not see it.
2. **Files.** Uploaded (`PUT .../files/{name}/upload` with the bytes;
   `X-Content-SHA256` is checked when sent, and an upload the server already
   has is not stored twice) or by address (`PUT .../files/{name}` with
   `{url, sha256, size}`; only a Hugging Face file may leave the hash to the
   host). The first file is the one the engine loads.
3. **Published** (`POST .../publish`): refused while the model has no files,
   a file has no size or no hash, or its engine is not in the gallery or does
   not serve its service (`GET .../check` says which). It is in
   `catalogue.json` from then on, and its uploaded files are served.
4. **Withdrawn** (`POST .../withdraw`): out of the catalogue and the topics,
   its files no longer served, the record kept. A client that installed it
   keeps it. Publishing it again brings it back.

A topic lists published models only; when its default is withdrawn, its
first remaining model becomes the default a client sees.

## API

What a client reads (no authentication):

| | |
|---|---|
| `GET /gallery/index.json` | the engines and their bundles (formatVersion 1) |
| `GET /gallery/catalogue.json` | the published models; an uploaded file's `url` is `<base>/files/<sha256>/<name>` |
| `GET /gallery/topics.json` | `{formatVersion: 1, published, topics: [{id, name, description, capability, models: [{id, default?}]}]}` |
| `GET /gallery/<document>.sig` | the Ed25519 signature of `index.json`, `catalogue.json` or `topics.json`: 128 hex characters |
| `GET /gallery/keys.json` | `{formatVersion: 1, algorithm: "ed25519", keys: [hex, ...]}`: the keys a client pins |
| `GET /files/{sha256}/{name}` | an uploaded file of a published model (Range answered) |
| `GET /healthz` | `ok`, or what is not configured |

The admin API (`Authorization: Bearer <token>`; errors are
`{"error", "error_description"}` with a 4xx or 503 status):

| | |
|---|---|
| `GET /v1/admin/whoami` | `{name, role, server, published, revision, baseUrl}` |
| `GET /v1/admin/models` / `GET /v1/admin/models/{id}` | every model, any state, with its files and topics |
| `PUT /v1/admin/models/{id}` | create a draft, or change one (its files are left alone) |
| `DELETE /v1/admin/models/{id}` | super |
| `GET /v1/admin/models/{id}/check` | `{publishable, problems}` |
| `POST /v1/admin/models/{id}/publish` \| `withdraw` \| `draft` | |
| `PUT /v1/admin/models/{id}/files/{name}` | `{url, sha256, size}`: a file hosted elsewhere |
| `PUT /v1/admin/models/{id}/files/{name}/upload` | the file's bytes → `{sha256, size, stored}` |
| `DELETE /v1/admin/models/{id}/files/{name}` | a published model keeps at least one |
| `GET /v1/admin/topics`, `PUT /v1/admin/topics/{id}` | `{name, description, capability, position?, models: [{id, default?}]}`; models of the topic's service only |
| `DELETE /v1/admin/topics/{id}` | super |
| `GET /v1/admin/engines`, `PUT` / `DELETE /v1/admin/engines/{id}` | super for the changes; an engine a published model runs on stays |
| `POST /v1/admin/import` | super: `{index?, catalogue?, topics?, publish?}` in the client formats |
| `GET /v1/admin/blobs`, `POST /v1/admin/blobs/prune` | the uploads and how often each is used; prune (super) deletes the unused |
| `GET /v1/admin/admins`, `PUT` / `DELETE /v1/admin/admins/{name}` | super: `{role: "super" \| "editor"}` → `{token}`, shown once |
| `GET /v1/admin/audit?since=N` | `{entries: [{seq, at, admin, action, target, detail}], next}` |

## Signatures

`index.json`, `catalogue.json` and `topics.json` are signed with the
server's Ed25519 key over the exact bytes served (one compact, key-ordered
serialisation), and the signature travels in the document's
`X-UltraAI-Signature` header - so a document and its signature are always
of one revision - and as `<document>.sig` for a mirror that copies files.
An UltraAI client pins the keys of `keys.json` on its first refresh and
from then on refuses a document that no pinned key signed
(`Docs/Modules/UltraAI/LocalRuntime.md`, *Signed galleries*).

The key is `ULTRAAI_MODELS_SIGNING_SEED` (32 bytes, hex), or one made on
the first start and kept in the database. Keep it: a client that pinned it
refuses anything signed with another. A client pins every key `keys.json`
lists, so a key listed in `ULTRAAI_MODELS_NEXT_PUBLIC_KEYS` well before the
change is trusted by the clients that first refreshed after it was listed;
a client that pinned only the old key must have its pin reset (Model topics
> Model server, set the address again) or be given the new key. `whoami` and UltraAI ServerAdmin's Connection
page show the public key.

## Who may do what

The **super administrator** signs in with `ULTRAAI_MODELS_SUPER_TOKEN` from
the environment - the one secret the server is configured with - and may do
everything, including making **accounts**. An account is a name, a role and
the hash of its token; the token is shown once when the account is made (or
given a new one) and never again. An **editor** manages models, files and
topics; a **super** account also the engines, the accounts, deletions and
the pruning of uploads. Every change is in the audit log with who made it.

## Settings

From the environment (`deploy/ultraai-model-server.env.example`):

| Variable | Default | |
|---|---|---|
| `ULTRAAI_MODELS_SUPER_TOKEN` | — | the super administrator's token; without it (and no account) the admin API answers 503 |
| `ULTRAAI_MODELS_BASE_URL` | `http://127.0.0.1:8793` | where clients reach the server; the files' URLs are built on it |
| `ULTRAAI_MODELS_DB` | `ultraai-models.sqlite3` | the database |
| `ULTRAAI_MODELS_DATA` | `ultraai-models-data` | the uploads (`blobs/<aa>/<sha256>`) and uploads in flight (`tmp/`) |
| `ULTRAAI_MODELS_HOST`, `_PORT` | `127.0.0.1`, `8793` | where the service listens (behind nginx) |
| `ULTRAAI_MODELS_MAX_UPLOAD` | 80 GiB | the largest upload |
| `ULTRAAI_MODELS_SIGNING_SEED` | — (made and kept) | the Ed25519 seed (64 hex characters) the client documents are signed with |
| `ULTRAAI_MODELS_NEXT_PUBLIC_KEYS` | — | public keys (hex, comma-separated) `keys.json` also lists |
| `ULTRAAI_MODELS_SEED` | `1` | fill an empty database with the framework's built-in gallery, catalogue and topics, published |

The seed is `ultraai_models/seed/`, the same documents
`UltraAI/runtime/src/LocalRuntimeBuiltinData.cpp` carries - a test compares
them - so a new server publishes what a client without one already has.

## Running

```
python3 -m venv .venv && .venv/bin/pip install -e '.[test]'
.venv/bin/python -m pytest -q
ULTRAAI_MODELS_SUPER_TOKEN=... ULTRAAI_MODELS_BASE_URL=https://models.example.org .venv/bin/ultraai-model-server
```

`deploy/`: the systemd unit, the nginx site (no body-size limit and no
request buffering on the admin API, so a 40 GB upload streams through),
and the environment file to fill in.
