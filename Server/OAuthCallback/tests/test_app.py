"""The service end to end, with the providers' token endpoints mocked."""

from __future__ import annotations

import base64
import hashlib
import json
import secrets
from urllib.parse import parse_qs, urlsplit

import httpx
import pytest
from fastapi.testclient import TestClient

from oauth_callback.app import create_app
from oauth_callback.config import ClientRegistration, Settings, load_env_file, load_settings
from oauth_callback.providers import PROVIDERS
from oauth_callback.sessions import SessionStore


def pkce() -> tuple[str, str]:
    verifier = secrets.token_urlsafe(32)
    digest = hashlib.sha256(verifier.encode()).digest()
    return verifier, base64.urlsafe_b64encode(digest).rstrip(b"=").decode()


def settings(**overrides) -> Settings:
    return Settings(
        public_url="https://oauth-callback.example",
        rate_limit_per_minute=overrides.pop("rate_limit_per_minute", 0),
        clients={
            "google": ClientRegistration("g-id.apps.googleusercontent.com", "g-secret"),
            "microsoft": ClientRegistration("ms-id"),
            "yahoo": ClientRegistration("y-id", "y-secret"),
            "dropbox": ClientRegistration("d-id", "d-secret"),
        },
        **overrides,
    )


class FakeProvider:
    """Answers the providers' token endpoints; records what was posted."""

    def __init__(self):
        self.requests: list[httpx.Request] = []
        self.status = 200
        self.body: dict = {"access_token": "at", "refresh_token": "rt",
                           "expires_in": 3599, "token_type": "Bearer"}
        self.fail = False

    def transport(self) -> httpx.MockTransport:
        def handle(request: httpx.Request) -> httpx.Response:
            self.requests.append(request)
            if self.fail:
                raise httpx.ConnectError("down")
            return httpx.Response(self.status, json=self.body)
        return httpx.MockTransport(handle)

    def last_form(self) -> dict[str, str]:
        return {k: v[0] for k, v in parse_qs(self.requests[-1].content.decode()).items()}


@pytest.fixture
def provider():
    return FakeProvider()


@pytest.fixture
def client(provider):
    app = create_app(settings(), provider_transport=provider.transport())
    with TestClient(app) as c:
        yield c


def start(client, prov="google", **extra) -> tuple[dict, str]:
    verifier, challenge = pkce()
    r = client.post("/v1/sessions", data={"provider": prov, "code_challenge": challenge,
                                          "login_hint": "erika@example.com", **extra})
    assert r.status_code == 200, r.text
    return r.json(), verifier


# ---- starting a sign-in -----------------------------------------------------

def test_session_carries_the_consent_url_for_the_registered_client(client):
    body, _ = start(client)
    assert body["session_id"]
    assert body["expires_in"] == 600
    url = urlsplit(body["authorization_url"])
    assert url.scheme == "https" and url.netloc == "accounts.google.com"
    q = {k: v[0] for k, v in parse_qs(url.query).items()}
    assert q["client_id"] == "g-id.apps.googleusercontent.com"
    assert q["redirect_uri"] == "https://oauth-callback.example/callback/google"
    assert q["scope"] == "https://mail.google.com/"
    assert q["code_challenge_method"] == "S256"
    assert q["login_hint"] == "erika@example.com"
    assert q["access_type"] == "offline" and q["prompt"] == "consent"
    assert len(q["state"]) >= 32
    # The state is not the session id: the browser sees one, the app the other.
    assert q["state"] != body["session_id"]


def test_microsoft_and_yahoo_consent_urls(client):
    body, _ = start(client, "microsoft")
    q = {k: v[0] for k, v in parse_qs(urlsplit(body["authorization_url"]).query).items()}
    assert "IMAP.AccessAsUser.All" in q["scope"] and "offline_access" in q["scope"]
    assert q["prompt"] == "select_account"
    body, _ = start(client, "yahoo")
    url = urlsplit(body["authorization_url"])
    q = {k: v[0] for k, v in parse_qs(url.query).items()}
    assert url.netloc == "api.login.yahoo.com"
    assert "scope" not in q           # Yahoo refuses a scope parameter
    assert q["redirect_uri"] == "https://oauth-callback.example/callback/yahoo"


def test_dropbox_asks_for_offline_access_and_the_cloud_scopes(client, provider):
    body, _ = start(client, "dropbox")
    url = urlsplit(body["authorization_url"])
    q = {k: v[0] for k, v in parse_qs(url.query).items()}
    assert url.netloc == "www.dropbox.com"
    assert q["token_access_type"] == "offline"
    assert "files.content.write" in q["scope"] and "sharing.write" in q["scope"]
    assert q["redirect_uri"] == "https://oauth-callback.example/callback/dropbox"
    r = client.post("/v1/token", data={"provider": "dropbox", "grant_type": "refresh_token",
                                       "refresh_token": "rt"})
    assert r.status_code == 200
    assert provider.last_form()["client_secret"] == "d-secret"
    # UltraCloud's OneDrive scopes pass the Microsoft allow-list.
    _, challenge = pkce()
    r = client.post("/v1/sessions", data={"provider": "microsoft", "code_challenge": challenge,
                                          "scope": "Files.ReadWrite User.Read offline_access"})
    assert r.status_code == 200


def test_scopes_outside_the_allowlist_are_refused(client):
    _, challenge = pkce()
    r = client.post("/v1/sessions", data={"provider": "google", "code_challenge": challenge,
                                          "scope": "https://www.googleapis.com/auth/contacts"})
    assert r.status_code == 400 and r.json()["error"] == "invalid_scope"
    r = client.post("/v1/sessions", data={"provider": "google", "code_challenge": challenge,
                                          "scope": "https://www.googleapis.com/auth/drive"})
    assert r.status_code == 200
    assert "auth%2Fdrive" in r.json()["authorization_url"]


def test_bad_requests_are_named(client):
    r = client.post("/v1/sessions", data={"provider": "google", "code_challenge": "short"})
    assert r.status_code == 400 and r.json()["error"] == "invalid_request"
    _, challenge = pkce()
    r = client.post("/v1/sessions", data={"provider": "google", "code_challenge": challenge,
                                          "code_challenge_method": "plain"})
    assert r.status_code == 400
    r = client.post("/v1/sessions", data={"provider": "aol", "code_challenge": challenge})
    assert r.status_code == 404 and r.json()["error"] == "unknown_provider"


def test_a_provider_without_a_client_id_is_not_served(provider):
    s = settings()
    s.clients["microsoft"] = ClientRegistration("")
    app = create_app(s, provider_transport=provider.transport())
    with TestClient(app) as c:
        assert c.get("/v1/providers").json() == {"providers": "dropbox google yahoo"}
        _, challenge = pkce()
        r = c.post("/v1/sessions", data={"provider": "microsoft", "code_challenge": challenge})
        assert r.status_code == 404
        r = c.post("/v1/token", data={"provider": "microsoft", "grant_type": "refresh_token",
                                      "refresh_token": "x"})
        assert r.status_code == 404
        assert c.get("/healthz").json()["status"] == "ok"


# ---- the redirect and the pick-up ------------------------------------------

def test_the_code_reaches_the_application_once(client):
    body, _ = start(client)
    state = parse_qs(urlsplit(body["authorization_url"]).query)["state"][0]
    sid = body["session_id"]

    r = client.get(f"/v1/sessions/{sid}")
    assert r.json()["status"] == "pending"

    r = client.get(f"/callback/google?code=4%2Fabc&state={state}")
    assert r.status_code == 200 and "Signed in with Google" in r.text

    r = client.get(f"/v1/sessions/{sid}?wait=5")
    assert r.json() == {"status": "ready", "code": "4/abc"}
    # Delivered once; the session is gone.
    assert client.get(f"/v1/sessions/{sid}").status_code == 404
    # The redirect cannot be replayed into a new answer either.
    r = client.get(f"/callback/google?code=again&state={state}")
    assert "expired" in r.text


def test_a_refusal_reaches_the_application(client):
    body, _ = start(client)
    state = parse_qs(urlsplit(body["authorization_url"]).query)["state"][0]
    r = client.get(f"/callback/google?error=access_denied&error_description=no&state={state}")
    assert "not completed" in r.text
    r = client.get(f"/v1/sessions/{body['session_id']}")
    assert r.json() == {"status": "error", "error": "access_denied", "error_description": "no"}


def test_a_redirect_for_another_provider_or_unknown_state_is_ignored(client):
    body, _ = start(client, "yahoo")
    state = parse_qs(urlsplit(body["authorization_url"]).query)["state"][0]
    r = client.get(f"/callback/google?code=c&state={state}")   # wrong provider
    assert "expired" in r.text
    assert client.get(f"/v1/sessions/{body['session_id']}").json()["status"] == "pending"
    r = client.get("/callback/google?code=c&state=nope")
    assert "expired" in r.text
    r = client.get("/callback/google")
    assert "not a sign-in" in r.text
    assert client.get("/v1/sessions/not-a-session-id").status_code == 404
    assert client.get("/v1/sessions/" + "a" * 40).status_code == 404


def test_html_escapes_what_the_provider_sends(client):
    r = client.get("/callback/<script>alert(1)</script>?code=c&state=s")
    assert "<script>" not in r.text


# ---- token exchange and refresh -------------------------------------------

def test_code_exchange_adds_the_secret_and_returns_the_provider_answer(client, provider):
    body, verifier = start(client)
    r = client.post("/v1/token", data={"provider": "google", "grant_type": "authorization_code",
                                       "code": "4/abc", "code_verifier": verifier})
    assert r.status_code == 200
    assert r.headers["content-type"].startswith("application/json")
    assert r.json()["access_token"] == "at" and r.json()["refresh_token"] == "rt"
    form = provider.last_form()
    assert str(provider.requests[-1].url) == PROVIDERS["google"].token_endpoint
    assert form["grant_type"] == "authorization_code"
    assert form["code"] == "4/abc" and form["code_verifier"] == verifier
    assert form["client_id"] == "g-id.apps.googleusercontent.com"
    assert form["client_secret"] == "g-secret"
    assert form["redirect_uri"] == "https://oauth-callback.example/callback/google"
    assert "authorization" not in provider.requests[-1].headers


def test_yahoo_sends_the_secret_as_http_basic(client, provider):
    r = client.post("/v1/token", data={"provider": "yahoo", "grant_type": "refresh_token",
                                       "refresh_token": "rt"})
    assert r.status_code == 200
    form = provider.last_form()
    assert "client_secret" not in form and form["refresh_token"] == "rt"
    expected = "Basic " + base64.b64encode(b"y-id:y-secret").decode()
    assert provider.requests[-1].headers["authorization"] == expected


def test_microsoft_public_client_sends_scopes_and_no_secret(client, provider):
    r = client.post("/v1/token", data={"provider": "microsoft", "grant_type": "refresh_token",
                                       "refresh_token": "rt"})
    assert r.status_code == 200
    form = provider.last_form()
    assert "client_secret" not in form
    assert "IMAP.AccessAsUser.All" in form["scope"]


def test_provider_errors_pass_through_with_their_status(client, provider):
    provider.status = 400
    provider.body = {"error": "invalid_grant", "error_description": "Token has been expired or revoked."}
    r = client.post("/v1/token", data={"provider": "google", "grant_type": "refresh_token",
                                       "refresh_token": "dead"})
    assert r.status_code == 400
    assert r.json()["error"] == "invalid_grant"


def test_provider_outage_is_a_502(client, provider):
    provider.fail = True
    r = client.post("/v1/token", data={"provider": "google", "grant_type": "refresh_token",
                                       "refresh_token": "rt"})
    assert r.status_code == 502 and r.json()["error"] == "provider_unreachable"


def test_token_request_validation(client):
    r = client.post("/v1/token", data={"provider": "google", "grant_type": "authorization_code",
                                       "code": "c"})
    assert r.status_code == 400
    r = client.post("/v1/token", data={"provider": "google", "grant_type": "refresh_token"})
    assert r.status_code == 400
    r = client.post("/v1/token", data={"provider": "google", "grant_type": "password"})
    assert r.status_code == 400 and r.json()["error"] == "unsupported_grant_type"


# ---- limits and settings ---------------------------------------------------

def test_rate_limit_counts_per_forwarded_address(provider):
    app = create_app(settings(rate_limit_per_minute=2), provider_transport=provider.transport())
    with TestClient(app) as c:
        _, challenge = pkce()
        data = {"provider": "google", "code_challenge": challenge}
        a = {"x-forwarded-for": "198.51.100.1, 10.0.0.1"}
        b = {"x-forwarded-for": "198.51.100.2"}
        assert c.post("/v1/sessions", data=data, headers=a).status_code == 200
        assert c.post("/v1/sessions", data=data, headers=a).status_code == 200
        assert c.post("/v1/sessions", data=data, headers=a).status_code == 429
        assert c.post("/v1/sessions", data=data, headers=b).status_code == 200


def test_sessions_expire_and_a_late_redirect_is_refused():
    store = SessionStore(session_ttl=600, result_ttl=120)
    s = store.create("google", "c" * 43, now=1000)
    assert store.get(s.session_id, now=1599) is s
    assert store.answer("google", s.state, "code", "", "", now=1601) is None
    assert store.get(s.session_id, now=1601) is None
    # An answer keeps only briefly for the pick-up.
    s = store.create("google", "c" * 43, now=2000)
    assert store.answer("google", s.state, "code", "", "", now=2100) is s
    assert store.get(s.session_id, now=2219) is s
    assert store.get(s.session_id, now=2221) is None
    assert len(store) == 0


def test_settings_come_from_the_environment_and_an_env_file(tmp_path):
    env_file = tmp_path / ".env"
    env_file.write_text("# comment\nOAUTH_CALLBACK_GOOGLE_CLIENT_ID=\"file-id\"\n"
                        "OAUTH_CALLBACK_GOOGLE_CLIENT_SECRET='file-secret'\n"
                        "OAUTH_CALLBACK_PUBLIC_URL=https://cb.example/\n")
    assert load_env_file(env_file)["OAUTH_CALLBACK_GOOGLE_CLIENT_ID"] == "file-id"
    s = load_settings({"OAUTH_CALLBACK_ENV_FILE": str(env_file),
                       "OAUTH_CALLBACK_GOOGLE_CLIENT_ID": "env-id",   # wins over the file
                       "OAUTH_CALLBACK_YAHOO_CLIENT_ID": "y",
                       "OAUTH_CALLBACK_RATE_LIMIT": "12",
                       "OAUTH_CALLBACK_TRUST_PROXY": "no"})
    assert s.public_url == "https://cb.example"
    assert s.client("google").client_id == "env-id"
    assert s.client("google").client_secret == "file-secret"
    assert s.client("microsoft") is None
    assert s.configured_providers() == ["google", "yahoo"]
    assert s.redirect_uri("yahoo") == "https://cb.example/callback/yahoo"
    assert s.rate_limit_per_minute == 12 and s.trust_proxy is False
    assert load_settings({}).public_url == "https://oauth-callback.ultraos.eu"


def test_every_answer_the_application_reads_is_flat_json(client):
    """UltraNet parses the service's answers with its flat token-response
    scanner: top-level scalars only."""
    body, _ = start(client)
    for payload in (body, client.get("/v1/providers").json(),
                    client.get(f"/v1/sessions/{body['session_id']}").json()):
        assert all(not isinstance(v, (dict, list)) for v in payload.values()), payload
    assert json.dumps(body)  # serialisable
