# The ULTRA OS OAuth sign-in service

A small [FastAPI](https://fastapi.tiangolo.com/) service that runs at
`https://oauth-callback.ultraos.eu` and stands between UltraMail and the
OAuth2 providers of mail accounts - **Google** (Gmail), **Microsoft**
(Outlook.com, Microsoft 365) and **Yahoo** - and, for the composer's cloud
links (UltraCloud), **Google Drive**, **OneDrive** and **Dropbox**, under the
same `google` and `microsoft` registrations plus a `dropbox` one.

It exists for two reasons:

- **UltraMail ships no client secret.** The OAuth client registrations - the
  client id and, where the provider issues one, the client secret - live
  here, in a file on the server, and nowhere in the application. UltraMail
  carries only this service's address.
- **The providers redirect to a public https address.** A desktop
  application can only listen on `http://127.0.0.1`, which Yahoo refuses as a
  redirect URI (it forces https) and which made Yahoo a paste-the-code flow.
  With the service as the redirect, every provider sends the browser to
  `https://oauth-callback.ultraos.eu/callback/<provider>`, and the user's
  browser simply says "Signed in - you can close this tab".

The user's safety does not rest on the service: PKCE stays end to end. The
application makes the verifier, sends only the challenge, and the code the
service hands back is useless to anyone without that verifier. The service
stores nothing: a sign-in in flight lives in memory for ten minutes at most,
tokens pass through the token endpoint and are never kept, and nothing is
written to disk.

## The protocol

Every answer the application reads is a **flat JSON object** (no nesting, no
arrays), because UltraNet parses it with its token-response scanner rather
than a JSON engine. Requests are `application/x-www-form-urlencoded`.
Errors are `{"error": "...", "error_description": "..."}` with a 4xx or 5xx
status, in the shape of an OAuth2 token error.

| | Who calls | What |
|---|---|---|
| `POST /v1/sessions` | the application | Start a sign-in. Form: `provider` (`google`, `microsoft`, `yahoo`, `dropbox`), `code_challenge` (PKCE S256), `code_challenge_method=S256`, optional `login_hint` (the address), `scope` (space-separated, from the provider's allow-list; empty = the mail scopes) and `client` (a name for the log). Answer: `{"session_id": "...", "authorization_url": "https://accounts.google.com/...", "expires_in": 600}`. The application opens the URL in the browser. |
| `GET /callback/{provider}` | the provider, through the browser | The registered redirect URI. Records the `code` (or `error`) for the sign-in the `state` names and shows a "Signed in" page. |
| `GET /v1/sessions/{id}?wait=N` | the application | Collect the answer, waiting up to N seconds (30 at most) for the browser. `{"status": "pending", "expires_in": 412}`, `{"status": "ready", "code": "..."}` or `{"status": "error", "error": "access_denied", "error_description": "..."}`. A ready or error answer is delivered once; afterwards, and after the sign-in expired, the session is 404 `unknown_session`. |
| `POST /v1/token` | the application | The provider's token endpoint with the registration added. Form: `provider`, `grant_type=authorization_code` with `code` and `code_verifier`, or `grant_type=refresh_token` with `refresh_token`. The provider's answer comes back verbatim, with the provider's status code (`access_token`, `refresh_token`, `expires_in`, or `error` / `error_description`). |
| `GET /v1/providers` | anyone | `{"providers": "google microsoft yahoo"}` - the providers this instance has a registration for. |
| `GET /healthz` | monitoring | `{"status": "ok", "version": "1.0.0", "providers": "...", "sessions": 3}` |

The `state` in the consent URL and the `session_id` are two different random
values: the browser sees the state, only the application knows the session
id, so a leaked state lets nobody collect the code (not that it would help
them without the verifier). A code is handed out once, a redirect cannot be
replayed, a redirect for another provider's session is refused, and a
sign-in with a scope outside the provider's allow-list is refused - the
consent page carries our application's name, so the service must not be
usable to ask a user for anything we did not decide on.

The counterpart in the framework is `UltraNet/UltraNetOAuth2Service.h`
(`UltraNet_OAuth2AuthorizeViaService`, `UltraNet_OAuth2ServiceRefresh`), and
UltraNet's app registry says where the service is
(`UltraNet_OAuth2SignInServiceUrl`: built in as the `ULTRANET_OAUTH_SERVICE_URL`
CMake option, overridden by the environment or an `oauth.ini`). UltraMail
and UltraCloud both read that one setting.

## Registering the clients

Register one client per provider with the redirect URI
`https://oauth-callback.ultraos.eu/callback/<provider>`; put the ids and
secrets into the service's environment file (below).

- **Google.** [Google Cloud console](https://console.cloud.google.com/) →
  *APIs & Services → Credentials → Create credentials → OAuth client ID*,
  application type **Web application**, authorised redirect URI
  `https://oauth-callback.ultraos.eu/callback/google`. The consent screen
  must list `ultraos.eu` under *Authorised domains* and carry the
  `https://mail.google.com/` scope (a *restricted* scope: while the consent
  screen is in *Testing* only its test users can sign in, and Google revokes
  their refresh tokens after seven days; publishing it needs Google's CASA
  security assessment). A Web client has a secret; it goes in
  `OAUTH_CALLBACK_GOOGLE_CLIENT_SECRET`.
- **Microsoft.** [Microsoft Entra admin center](https://entra.microsoft.com/)
  → *App registrations*, supported account types *Accounts in any
  organizational directory and personal Microsoft accounts*. Under
  *Authentication* add the platform **Web** with the redirect URI
  `https://oauth-callback.ultraos.eu/callback/microsoft`; a Web platform is
  a confidential client, so make a client secret under *Certificates &
  secrets* and put it in `OAUTH_CALLBACK_MICROSOFT_CLIENT_SECRET`. *API
  permissions*: the delegated `IMAP.AccessAsUser.All` and `SMTP.Send` from
  *Office 365 Exchange Online*, plus `offline_access`.
- **Yahoo.** [Yahoo developer console](https://developer.yahoo.com/apps/) →
  create an app with the redirect URI
  `https://oauth-callback.ultraos.eu/callback/yahoo`; the *Client ID
  (Consumer Key)* and *Client Secret (Consumer Secret)* go in the
  environment file. The app must hold the **Mail** API permission, which
  Yahoo grants on request (<https://senders.yahooinc.com/developer/developer-access/>);
  without it Yahoo issues a token that IMAP and SMTP reject, and UltraMail
  users fall back to an app password.
- **Dropbox.** [Dropbox App Console](https://www.dropbox.com/developers/apps)
  → create a scoped app with the redirect URI
  `https://oauth-callback.ultraos.eu/callback/dropbox` and the permissions
  `account_info.read`, `files.metadata.read`, `files.content.read`,
  `files.content.write`, `sharing.read` and `sharing.write`; the *App key*
  and *App secret* go in the environment file. Only the composer's cloud
  links need it; leave it empty otherwise.

## Running it

```sh
cd Server/OAuthCallback
python3 -m venv venv && . venv/bin/activate
pip install -e '.[test]'
python -m pytest -q                      # the tests need no network
cp .env.example .env                     # fill in the registrations
python -m oauth_callback --port 8787     # http://127.0.0.1:8787/healthz
```

For a quick local try of UltraMail against it, start it with a public URL
of `http://127.0.0.1:8787` in `.env` and run UltraMail with
`ULTRAMAIL_OAUTH_SERVICE_URL=http://127.0.0.1:8787` - the redirect URI
registered with the provider must then be
`http://127.0.0.1:8787/callback/<provider>` (Google and Microsoft accept a
loopback http redirect for a Web client; Yahoo does not).

### On the server

```sh
sudo useradd --system --home /opt/oauth-callback --shell /usr/sbin/nologin oauth-callback
sudo git clone <this repository> /opt/oauth-callback-src   # or copy Server/OAuthCallback
sudo cp -r /opt/oauth-callback-src/Server/OAuthCallback /opt/oauth-callback
cd /opt/oauth-callback
sudo python3 -m venv venv && sudo venv/bin/pip install .
sudo install -m 0600 -o root -g root .env.example /etc/oauth-callback.env   # then edit it
sudo cp deploy/oauth-callback.service /etc/systemd/system/
sudo systemctl daemon-reload && sudo systemctl enable --now oauth-callback
sudo cp deploy/logrotate /etc/logrotate.d/oauth-callback
sudo cp deploy/nginx.conf /etc/nginx/sites-available/oauth-callback.ultraos.eu
sudo ln -s /etc/nginx/sites-available/oauth-callback.ultraos.eu /etc/nginx/sites-enabled/
sudo certbot --nginx -d oauth-callback.ultraos.eu
sudo nginx -t && sudo systemctl reload nginx
curl https://oauth-callback.ultraos.eu/healthz
```

The service listens on `127.0.0.1:8787` only; nginx terminates TLS, adds the
security headers, limits each address to 30 requests a minute (the service
has its own limit of 60 behind it) and keeps the redirect's query string -
the authorization code - out of its access log, which `deploy/logrotate`
deletes after 14 days, as UltraMail's privacy notice promises. **One worker**: the sign-ins
in flight are in the process's memory, so `--workers 2` would lose half of
them. The log never carries a code or a token, only the provider and the
outcome.

### Settings

All through the environment (or the `.env` file `OAUTH_CALLBACK_ENV_FILE`
points at; the environment wins over the file), see
[`.env.example`](.env.example): `OAUTH_CALLBACK_PUBLIC_URL`,
`OAUTH_CALLBACK_<PROVIDER>_CLIENT_ID` / `_CLIENT_SECRET` for `GOOGLE`,
`MICROSOFT`, `YAHOO` and `DROPBOX`, and the optional `SESSION_TTL`, `RESULT_TTL`,
`RATE_LIMIT`, `TRUST_PROXY` and `PROVIDER_TIMEOUT`. A provider whose client
id is empty is not served (404 `unknown_provider`), and
`/v1/providers` says which are.

## Layout

```
Server/OAuthCallback/
├── oauth_callback/
│   ├── app.py          the routes
│   ├── config.py       settings from the environment / .env
│   ├── providers.py    endpoints, scopes and consent parameters per provider
│   ├── sessions.py     the sign-ins in flight (memory, TTL, long-poll)
│   ├── ratelimit.py    per-address sliding window
│   └── __main__.py     `python -m oauth_callback`
├── tests/test_app.py   the whole protocol against mocked providers
├── deploy/             nginx.conf, oauth-callback.service, logrotate
├── .env.example
└── pyproject.toml
```
