# UltraAI ServerAdmin

The super administrator's tool for an [UltraAI model server](../../Server/UltraAIModelServer/README.md):
which AI models UltraAI clients may install from your server, the files
behind them, which models are offered for which topic, and who may change
any of it. A window, and the same operations on the command line.

## The window

Start `ultraai-serveradmin` without arguments. *Connection*: the server's
address (`https://models.example.org`; a client's gallery address works
too) and your token - the server's `ULTRAAI_MODELS_SUPER_TOKEN`, or an
account's. The address is remembered; the token never is
(`ULTRAAI_SERVER_TOKEN` in the environment fills it in). Plain `http://` is
refused except to this machine, since the token would travel in clear.
Once signed in, the page shows the key the server signs its lists with:
UltraAI clients pin it on their first refresh, so it is the key to give a
user who wants to check theirs.

| Page | What it does |
|---|---|
| **Models** | Every model with its state (draft, published, withdrawn), size and topics. The form holds everything the catalogue carries: id, name, service, engine, licence, description, memory and GPU memory needed (typed as `8 GB`), context window, embedding dimensions, tool calls, pictures, tags, engine arguments. *Save* creates a draft or changes the model; *Check* says what stands in the way of publishing; *Publish* puts it in the catalogue clients read; *Withdraw* takes it out (clients that installed it keep it); *Delete* (super) removes it. *Files*: *Upload file* (a GGUF from this computer: hashed here, checked by the server, not sent again when the server has it), *Add by address* (an https URL, the size and the SHA-256; a Hugging Face file may leave the hash to the host), *Remove file*. |
| **Topics** | What each kind of work is offered: the models of the topic's service, best first, one marked as the default (`*`). A client that follows the server uses the default and downloads it when it changes. |
| **Engines** | The gallery's engines with their bundles per platform and accelerator, edited as JSON (super). |
| **Uploads** | The files stored on the server, their size and how many models use each; *Delete unused* (super). |
| **Administrators** | The accounts (super): an *editor* manages models, files and topics; a *super* account also engines, accounts and deletions. *Create, or give a new token* shows the token once. |
| **Audit log** | Who changed what, when. |

Every request runs on a worker thread; the status line says what is
running and what came back.

## The command line

```
ultraai-serveradmin [--server URL] [--token TOKEN] <command>
```

The server and the token default to `ULTRAAI_SERVER_URL` and
`ULTRAAI_SERVER_TOKEN`. Each command prints the server's JSON answer and
exits 0, or prints the error and exits 1 (2 for a usage error).

```
export ULTRAAI_SERVER_URL=https://models.example.org ULTRAAI_SERVER_TOKEN=...
ultraai-serveradmin model-put qwen3-14b-q4 --name "Qwen3 14B (Q4_K_M)" --capability textllm \
    --engine llama-server --license Apache-2.0 --memory 11000000000 --gpu 10000000000 \
    --context 32768 --tools --tag chat --tag tools --arg --ctx-size --arg 8192
ultraai-serveradmin upload qwen3-14b-q4 ~/models/Qwen3-14B-Q4_K_M.gguf
ultraai-serveradmin check qwen3-14b-q4
ultraai-serveradmin publish qwen3-14b-q4
ultraai-serveradmin topic-put code --name Programming --capability textllm qwen3-14b-q4* qwen3-8b-q4
ultraai-serveradmin admin-put alice editor
```

`models`, `model <id>`, `add-url <id> <name> <url> --size N [--sha256 H]`,
`remove-file`, `withdraw`, `draft`, `delete`, `topics`, `topic-delete`,
`engines`, `uploads`, `prune`, `admins`, `admin-delete`, `whoami` and
`audit [since]` complete the set; `ultraai-serveradmin help` lists them.

## Build

`BUILD_ULTRAAISERVERADMIN` (ON) builds `ultraai-serveradmin` wherever the
shared core has UltraNet and UltraCrypt. `UltraAIServerAdminCore`
(`Apps/UltraAIServerAdmin/core/ModelServerClient.h`) is the admin API's
client both the window and the commands use.
