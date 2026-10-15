"""The HTTP surface of the sign-in service. See README.md for the protocol.

    POST /v1/sessions                 start a sign-in: consent URL + session id
    GET  /callback/{provider}         where the provider sends the browser
    GET  /v1/sessions/{id}?wait=N     the application collects the code
    POST /v1/token                    code -> tokens, refresh -> tokens
    GET  /v1/providers                which providers this instance serves
    GET  /healthz

Every answer the application reads is a flat JSON object - no nesting, no
arrays - because the desktop side parses it with UltraNet's token-response
scanner rather than a JSON engine.
"""

from __future__ import annotations

import asyncio
import base64
import contextlib
import html
import logging
import re
import time
from urllib.parse import urlencode

import httpx
from fastapi import FastAPI, Form, Query, Request
from fastapi.responses import HTMLResponse, JSONResponse, Response

from . import __version__
from .config import Settings, load_settings
from .providers import PROVIDERS, Provider
from .ratelimit import RateLimiter
from .sessions import SessionStore

log = logging.getLogger("oauth_callback")

# RFC 7636 section 4.2: the challenge is base64url of a SHA-256, 43 chars.
_CHALLENGE_RE = re.compile(r"^[A-Za-z0-9\-._~]{43,128}$")
_SESSION_ID_RE = re.compile(r"^[A-Za-z0-9\-_]{20,128}$")
_PRINTABLE_RE = re.compile(r"^[^\x00-\x1f\x7f]*$")
MAX_WAIT_SECONDS = 30


def _error(status: int, error: str, description: str) -> JSONResponse:
    return JSONResponse({"error": error, "error_description": description}, status_code=status)


def _client_address(request: Request, trust_proxy: bool) -> str:
    if trust_proxy:
        forwarded = request.headers.get("x-forwarded-for", "")
        if forwarded:
            return forwarded.split(",")[0].strip()
    return request.client.host if request.client else "unknown"


def _page(title: str, heading: str, text: str) -> str:
    return (
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        f"<title>{html.escape(title)}</title>"
        "<style>body{font-family:system-ui,sans-serif;text-align:center;margin:15% 16px;"
        "color:#222;background:#fafafa}h1{font-size:1.5em}p{color:#555}</style></head>"
        f"<body><h1>{html.escape(heading)}</h1><p>{html.escape(text)}</p></body></html>"
    )


def create_app(settings: Settings | None = None,
               provider_transport: httpx.BaseTransport | None = None) -> FastAPI:
    """Build the application. `provider_transport` replaces the HTTP transport
    used to reach the providers' token endpoints (tests hand in a mock)."""
    settings = settings or load_settings()

    @contextlib.asynccontextmanager
    async def lifespan(app: FastAPI):
        app.state.http = httpx.AsyncClient(
            timeout=settings.provider_timeout, transport=provider_transport,
            headers={"user-agent": f"ultraos-oauth-callback/{__version__}"})

        async def sweep() -> None:
            while True:
                await asyncio.sleep(60)
                app.state.sessions.expire()

        sweeper = asyncio.create_task(sweep())
        try:
            yield
        finally:
            sweeper.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await sweeper
            await app.state.http.aclose()

    app = FastAPI(title="ULTRA OS OAuth sign-in service", version=__version__,
                  docs_url=None, redoc_url=None, openapi_url=None, lifespan=lifespan)
    app.state.settings = settings
    app.state.sessions = SessionStore(settings.session_ttl, settings.result_ttl)
    app.state.limiter = RateLimiter(settings.rate_limit_per_minute)
    app.state.http = None

    def provider_for(provider_id: str) -> tuple[Provider | None, JSONResponse | None]:
        provider = PROVIDERS.get(provider_id)
        if provider is None or settings.client(provider_id) is None:
            return None, _error(404, "unknown_provider",
                                f"this service does not sign in to {provider_id!r}")
        return provider, None

    def limited(request: Request) -> JSONResponse | None:
        if app.state.limiter.allow(_client_address(request, settings.trust_proxy)):
            return None
        return _error(429, "rate_limited", "too many requests from this address; try again in a minute")

    # ---- informational -----------------------------------------------------

    @app.get("/", response_class=HTMLResponse)
    async def index() -> str:
        return _page("ULTRA OS sign-in service", "ULTRA OS sign-in service",
                     "This address receives the sign-in confirmations of ULTRA OS "
                     "applications such as UltraMail. There is nothing to see here.")

    @app.get("/healthz")
    async def healthz() -> dict:
        return {"status": "ok", "version": __version__,
                "providers": " ".join(settings.configured_providers()),
                "sessions": len(app.state.sessions)}

    @app.get("/v1/providers")
    async def providers() -> dict:
        # A space-separated string, not an array: the desktop side reads
        # flat JSON only.
        return {"providers": " ".join(settings.configured_providers())}

    # ---- 1. the application starts a sign-in -------------------------------

    @app.post("/v1/sessions")
    async def start_session(request: Request,
                            provider: str = Form(...),
                            code_challenge: str = Form(...),
                            code_challenge_method: str = Form("S256"),
                            login_hint: str = Form(""),
                            scope: str = Form(""),
                            client: str = Form("")) -> Response:
        if (r := limited(request)) is not None:
            return r
        prov, r = provider_for(provider)
        if r is not None:
            return r
        if code_challenge_method != "S256":
            return _error(400, "invalid_request", "code_challenge_method must be S256")
        if not _CHALLENGE_RE.match(code_challenge):
            return _error(400, "invalid_request", "code_challenge is not a PKCE S256 challenge")
        if len(login_hint) > 320 or not _PRINTABLE_RE.match(login_hint):
            return _error(400, "invalid_request", "login_hint is not an address")
        try:
            scopes = prov.scopes_for(scope)
        except ValueError as e:
            return _error(400, "invalid_scope", str(e))

        registration = settings.client(prov.id)
        assert registration is not None
        try:
            session = app.state.sessions.create(prov.id, code_challenge)
        except OverflowError as e:
            return _error(503, "temporarily_unavailable", str(e))

        params = {
            "response_type": "code",
            "client_id": registration.client_id,
            "redirect_uri": settings.redirect_uri(prov.id),
        }
        if scopes:
            params["scope"] = " ".join(scopes)
        params["state"] = session.state
        params["code_challenge"] = code_challenge
        params["code_challenge_method"] = "S256"
        if login_hint:
            params["login_hint"] = login_hint
        params.update(prov.extra_auth_params)
        separator = "&" if "?" in prov.authorization_endpoint else "?"
        url = prov.authorization_endpoint + separator + urlencode(params)
        log.info("sign-in started: provider=%s client=%s", prov.id, client[:64] or "-")
        return JSONResponse({"session_id": session.session_id, "authorization_url": url,
                             "expires_in": settings.session_ttl})

    # ---- 2. the provider sends the browser back ----------------------------

    @app.get("/callback/{provider}", response_class=HTMLResponse)
    async def callback(provider: str, state: str = Query(""), code: str = Query(""),
                       error: str = Query(""), error_description: str = Query("")) -> str:
        prov = PROVIDERS.get(provider)
        name = prov.display_name if prov else provider
        if not state or (not code and not error):
            return _page("Sign-in", "This is not a sign-in",
                         "Start the sign-in from the application again.")
        session = app.state.sessions.answer(provider, state, code, error, error_description)
        if session is None:
            return _page("Sign-in expired", "This sign-in has expired",
                         "Start the sign-in from the application again.")
        if error:
            log.info("sign-in refused: provider=%s error=%s", provider, error[:64])
            return _page("Sign-in refused", f"The {name} sign-in was not completed",
                         "You can close this tab. The application will say what happened.")
        log.info("sign-in answered: provider=%s", provider)
        return _page("Signed in", f"Signed in with {name}",
                     "You can close this tab and return to the application.")

    # ---- 3. the application collects the answer ----------------------------

    @app.get("/v1/sessions/{session_id}")
    async def poll_session(session_id: str, wait: int = Query(0, ge=0)) -> Response:
        if not _SESSION_ID_RE.match(session_id):
            return _error(404, "unknown_session", "no such sign-in")
        store: SessionStore = app.state.sessions
        session = store.get(session_id)
        if session is None:
            return _error(404, "unknown_session", "no such sign-in, or it has expired")
        if not session.is_answered and wait > 0:
            with contextlib.suppress(asyncio.TimeoutError):
                await asyncio.wait_for(session.answered.wait(), timeout=min(wait, MAX_WAIT_SECONDS))
            session = store.get(session_id)
            if session is None:
                return _error(404, "unknown_session", "no such sign-in, or it has expired")
        if not session.is_answered:
            return JSONResponse({"status": "pending",
                                 "expires_in": max(0, int(session.expires_at - time.time()))})
        store.take(session)
        if session.error:
            return JSONResponse({"status": "error", "error": session.error,
                                 "error_description": session.error_description})
        return JSONResponse({"status": "ready", "code": session.code})

    # ---- 4. token exchange and refresh, with the secret added here ----------

    @app.post("/v1/token")
    async def token(request: Request,
                    provider: str = Form(...),
                    grant_type: str = Form(...),
                    code: str = Form(""),
                    code_verifier: str = Form(""),
                    refresh_token: str = Form(""),
                    scope: str = Form("")) -> Response:
        if (r := limited(request)) is not None:
            return r
        prov, r = provider_for(provider)
        if r is not None:
            return r
        registration = settings.client(prov.id)
        assert registration is not None

        form: dict[str, str] = {"client_id": registration.client_id}
        if grant_type == "authorization_code":
            if not code or not code_verifier:
                return _error(400, "invalid_request", "code and code_verifier are required")
            form.update(grant_type="authorization_code", code=code,
                        code_verifier=code_verifier, redirect_uri=settings.redirect_uri(prov.id))
        elif grant_type == "refresh_token":
            if not refresh_token:
                return _error(400, "invalid_request", "refresh_token is required")
            form.update(grant_type="refresh_token", refresh_token=refresh_token)
        else:
            return _error(400, "unsupported_grant_type",
                          "grant_type must be authorization_code or refresh_token")
        if prov.scopes_on_token_request:
            try:
                scopes = prov.scopes_for(scope)
            except ValueError as e:
                return _error(400, "invalid_scope", str(e))
            if scopes:
                form["scope"] = " ".join(scopes)

        headers = {"accept": "application/json"}
        if registration.client_secret:
            if prov.secret_in_body:
                form["client_secret"] = registration.client_secret
            else:
                basic = f"{registration.client_id}:{registration.client_secret}".encode()
                headers["authorization"] = "Basic " + base64.b64encode(basic).decode()

        client: httpx.AsyncClient = app.state.http
        try:
            upstream = await client.post(prov.token_endpoint, data=form, headers=headers)
        except httpx.HTTPError as e:
            log.warning("token endpoint unreachable: provider=%s %s", prov.id, type(e).__name__)
            return _error(502, "provider_unreachable",
                          f"{prov.display_name}'s token endpoint did not answer")
        log.info("token %s: provider=%s status=%s", grant_type, prov.id, upstream.status_code)
        # The provider's answer, verbatim: the application parses it as it
        # would parse the provider's own.
        return Response(content=upstream.content, status_code=upstream.status_code,
                        media_type="application/json")

    return app


# `uvicorn oauth_callback.app:app`
app = create_app()
