"""The OpenID Connect endpoints, at the issuer (the account's host).

    GET       /.well-known/openid-configuration   what the account can do, and where
    GET       /oauth/jwks                         the keys the identity tokens verify with
    GET, POST /oauth/authorize                    a service asks to sign a person in
    POST      /oauth/authorize/decide             the person's answer on the consent page
    POST      /oauth/token                        the code, or a refresh token, for tokens
    GET, POST /oauth/userinfo                     the person, for an access token
    POST      /oauth/introspect                   is this token good? (a resource server asks)
    POST      /oauth/revoke                       a token given back
    GET, POST /oauth/logout                       a service signs the person out at the account
    POST      /oauth/logout/confirm               ... after the person said so

What a service's server or an application calls (token, userinfo,
introspect, revoke, the two documents) carries no cookie and may be asked
from any origin. What a person's browser opens (authorize, logout) uses the
account's session, and whatever changes something there - the consent, the
sign-out - is a form of the account's own, with its CSRF token.

Version: 0.4.0
"""

from __future__ import annotations

import secrets
from urllib.parse import parse_qsl, urlencode, urlsplit, urlunsplit

from django.contrib.auth import logout as end_session
from django.core.cache import cache
from django.http import HttpResponse, HttpResponseRedirect, JsonResponse
from django.shortcuts import render
from django.urls import reverse
from django.utils import timezone
from django.views.decorators.cache import never_cache
from django.views.decorators.csrf import csrf_exempt
from django.views.decorators.http import require_GET, require_http_methods, require_POST

from ..web.utils import client_address
from . import hosts, keys, provider, sessions
from .models import SCOPES, Client, Consent

PENDING = "oidc_pending"
FAILURES, FAILURE_WINDOW = 30, 300
JUST_SIGNED_IN = 30


class Redirect(HttpResponseRedirect):
    # A service's address: https, or http on the person's own machine.
    allowed_schemes = ["https", "http"]


# -- answers ------------------------------------------------------------------

def _json(data: dict, status: int = 200) -> JsonResponse:
    response = JsonResponse(data, status=status)
    response["Cache-Control"] = "no-store"
    response["Pragma"] = "no-cache"
    response["Access-Control-Allow-Origin"] = "*"
    return response


def _preflight() -> HttpResponse:
    response = HttpResponse(status=204)
    response["Access-Control-Allow-Origin"] = "*"
    response["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"
    response["Access-Control-Allow-Headers"] = "Authorization, Content-Type"
    response["Access-Control-Max-Age"] = "600"
    return response


def _error(error: provider.OAuthError, bearer: bool = False) -> JsonResponse:
    response = _json({"error": error.error, "error_description": error.description}, error.status)
    if error.status == 401:
        response["WWW-Authenticate"] = f'Bearer error="{error.error}"' if bearer else 'Basic realm="account"'
    return response


def _with_query(uri: str, **params) -> str:
    parts = urlsplit(uri)
    query = parse_qsl(parts.query, keep_blank_values=True) + [(key, value) for key, value in params.items() if value != ""]
    return urlunsplit((parts.scheme, parts.netloc, parts.path, urlencode(query), ""))


def _back(redirect_uri: str, **params) -> HttpResponse:
    """The person sent back to the service, with the code or with what went
    wrong, and with who is answering (RFC 9207)."""
    response = Redirect(_with_query(redirect_uri, **params, iss=provider.issuer()))
    response["Cache-Control"] = "no-store"
    return response


def _problem(request, message: str, status: int = 400) -> HttpResponse:
    return render(request, "identity/error.html", {"message": message}, status=status)


def _too_many_failures(request, client_id: str) -> bool:
    return cache.get(f"oidc-fail:{client_address(request)}:{client_id}", 0) >= FAILURES


def _count_failure(request, client_id: str) -> None:
    name = f"oidc-fail:{client_address(request)}:{client_id}"
    if not cache.add(name, 1, FAILURE_WINDOW):
        try:
            cache.incr(name)
        except ValueError:
            pass


# -- the two documents --------------------------------------------------------

def _endpoint(name: str) -> str:
    return hosts.account_url("oidc:" + name)


@hosts.issuer_only
@require_GET
def discovery(request):
    methods = ["client_secret_basic", "client_secret_post", "none"]
    response = _json({
        "issuer": provider.issuer(),
        "authorization_endpoint": _endpoint("authorize"),
        "token_endpoint": _endpoint("token"),
        "userinfo_endpoint": _endpoint("userinfo"),
        "jwks_uri": _endpoint("jwks"),
        "introspection_endpoint": _endpoint("introspect"),
        "revocation_endpoint": _endpoint("revoke"),
        "end_session_endpoint": _endpoint("logout"),
        "response_types_supported": ["code"],
        "response_modes_supported": ["query"],
        "grant_types_supported": ["authorization_code", "refresh_token"],
        "code_challenge_methods_supported": ["S256"],
        "subject_types_supported": ["public", "pairwise"],
        "id_token_signing_alg_values_supported": [keys.ALGORITHM],
        "scopes_supported": list(SCOPES),
        "claims_supported": ["sub", "iss", "aud", "exp", "iat", "auth_time", "nonce", "sid", "at_hash", "name", "email", "email_verified"],
        "token_endpoint_auth_methods_supported": methods,
        "introspection_endpoint_auth_methods_supported": methods[:2],
        "revocation_endpoint_auth_methods_supported": methods,
        "claims_parameter_supported": False,
        "request_parameter_supported": False,
        "request_uri_parameter_supported": False,
        "authorization_response_iss_parameter_supported": True,
    })
    response["Cache-Control"] = "public, max-age=3600"
    del response["Pragma"]
    return response


@hosts.issuer_only
@require_GET
def jwks(request):
    response = _json(keys.jwks())
    response["Cache-Control"] = "public, max-age=3600"
    del response["Pragma"]
    return response


# -- a service asks to sign a person in ----------------------------------------

def _sign_in_first(request, params: dict, auth_request: provider.AuthRequest, again: bool) -> HttpResponse:
    """To the sign-in page, and from there back to this request - without
    the prompt the sign-in answers, or the two would take turns for ever."""
    params = dict(params)
    prompt = " ".join(word for word in params.pop("prompt", "").split() if word not in ("login", "select_account"))
    if prompt:
        params["prompt"] = prompt
    query = {"next": reverse("oidc:authorize") + "?" + urlencode(params)}
    if again:
        query["reauth"] = "1"
    if auth_request.login_hint:
        query["login_hint"] = auth_request.login_hint
    return HttpResponseRedirect(reverse("account:signin") + "?" + urlencode(query))


def _grant(request, auth_request: provider.AuthRequest) -> HttpResponse:
    sid, auth_time = sessions.current(request)
    code = provider.authorize(auth_request, request.user, sid, auth_time)
    return _back(auth_request.redirect_uri, code=code, state=auth_request.state)


@hosts.issuer_only
@csrf_exempt   # a request, not a change: the person's answer is `decide`, which is not exempt
@never_cache
@require_http_methods(["GET", "POST"])
def authorize(request):
    params = (request.POST if request.method == "POST" else request.GET).dict()
    try:
        auth_request = provider.read_request(params)
    except provider.Refused as refused:
        return _problem(request, str(refused))
    except provider.RedirectError as error:
        return _back(error.redirect_uri, error=error.error, error_description=error.description, state=error.state)
    client, prompt = auth_request.client, auth_request.prompt

    signed_in = request.user.is_authenticated
    stale = False
    if signed_in and auth_request.max_age is not None:
        # Someone who signed in a moment ago has just done what max_age asks
        # for, max_age=0 included: sending them to the password again would
        # never end. The token's auth_time says when it was, either way.
        _, auth_time = sessions.current(request)
        stale = (timezone.now() - auth_time).total_seconds() > max(auth_request.max_age, JUST_SIGNED_IN)
    if not signed_in or stale or prompt & {"login", "select_account"}:
        if "none" in prompt:
            return _back(auth_request.redirect_uri, error="login_required", state=auth_request.state)
        return _sign_in_first(request, params, auth_request, again=signed_in)

    scopes = set(auth_request.scope)
    if not client.trusted:
        consent = Consent.objects.filter(user=request.user, client=client).first()
        if "consent" in prompt or consent is None or not consent.covers(scopes):
            if "none" in prompt:
                return _back(auth_request.redirect_uri, error="consent_required", state=auth_request.state)
            key = secrets.token_urlsafe(16)
            pending = dict(list(request.session.get(PENDING, {}).items())[-4:])   # a few tabs, not a pile
            pending[key] = params
            request.session[PENDING] = pending
            return render(request, "identity/consent.html", {
                "client": client, "key": key, "scopes": [(scope, SCOPES[scope]) for scope in auth_request.scope if scope != "openid"],
                "host": urlsplit(auth_request.redirect_uri).hostname})
    return _grant(request, auth_request)


@hosts.issuer_only
@never_cache
@require_POST
def decide(request):
    pending = dict(request.session.get(PENDING, {}))
    params = pending.pop(request.POST.get("request", ""), None)
    request.session[PENDING] = pending
    if params is None:
        return _problem(request, "This request is no longer open. Go back to the service and start again.")
    try:
        auth_request = provider.read_request(params)
    except provider.Refused as refused:
        return _problem(request, str(refused))
    except provider.RedirectError as error:
        return _back(error.redirect_uri, error=error.error, error_description=error.description, state=error.state)
    if not request.user.is_authenticated:
        return HttpResponseRedirect(reverse("oidc:authorize") + "?" + urlencode(params))
    if request.POST.get("decision") != "allow":
        return _back(auth_request.redirect_uri, error="access_denied", state=auth_request.state)
    consent, _ = Consent.objects.get_or_create(user=request.user, client=auth_request.client, defaults={"scope": ""})
    consent.scope = " ".join(sorted(set(consent.scope.split()) | set(auth_request.scope)))
    consent.granted = timezone.now()
    consent.save()
    return _grant(request, auth_request)


# -- what a service's server asks ----------------------------------------------

def _client(request) -> Client:
    client_id = request.POST.get("client_id", "")
    if _too_many_failures(request, client_id):
        raise provider.OAuthError("slow_down", "Too many refused requests. Wait a few minutes.", 429)
    try:
        return provider.authenticate_client(request.headers.get("Authorization", ""), request.POST)
    except provider.OAuthError:
        _count_failure(request, client_id)
        raise


@hosts.issuer_only
@csrf_exempt
@require_http_methods(["POST", "OPTIONS"])
def token(request):
    if request.method == "OPTIONS":
        return _preflight()
    try:
        client = _client(request)
        grant_type = request.POST.get("grant_type", "")
        try:
            if grant_type == "authorization_code":
                grant = provider.redeem_code(client, request.POST.get("code", ""), request.POST.get("redirect_uri", ""), request.POST.get("code_verifier", ""))
                return _json(provider.issue_tokens(grant))
            if grant_type == "refresh_token":
                return _json(provider.refresh(client, request.POST.get("refresh_token", ""), request.POST.get("scope", "")))
        except provider.OAuthError:
            _count_failure(request, client.client_id)
            raise
        raise provider.OAuthError("unsupported_grant_type", "The grant is authorization_code or refresh_token.")
    except provider.OAuthError as error:
        return _error(error)


@hosts.issuer_only
@csrf_exempt
@require_http_methods(["GET", "POST", "OPTIONS"])
def userinfo(request):
    if request.method == "OPTIONS":
        return _preflight()
    header = request.headers.get("Authorization", "")
    value = header[7:].strip() if header.lower().startswith("bearer ") else request.POST.get("access_token", "")
    try:
        return _json(provider.userinfo(value))
    except provider.OAuthError as error:
        return _error(error, bearer=True)


@hosts.issuer_only
@csrf_exempt
@require_POST
def introspect(request):
    try:
        return _json(provider.introspect(_client(request), request.POST.get("token", "")))
    except provider.OAuthError as error:
        return _error(error)


@hosts.issuer_only
@csrf_exempt
@require_http_methods(["POST", "OPTIONS"])
def revoke(request):
    if request.method == "OPTIONS":
        return _preflight()
    try:
        provider.revoke(_client(request), request.POST.get("token", ""))
    except provider.OAuthError as error:
        return _error(error)
    return _json({})


# -- a service signs the person out at the account -------------------------------

def _signed_out(request, target: str) -> HttpResponse:
    if target:
        return Redirect(target)
    return render(request, "identity/signed_out.html")


@hosts.issuer_only
@csrf_exempt   # asked by a service; without the session's own token it only ever asks the person
@never_cache
@require_http_methods(["GET", "POST"])
def logout(request):
    """A service that signed the person in says they signed out. It proves
    which sign-in it means with the identity token it was given; without
    that proof the person is asked first, so that no page can sign anyone
    out by linking here."""
    params = (request.POST if request.method == "POST" else request.GET).dict()
    claims = keys.verify(params.get("id_token_hint", ""), include_retired=True) if params.get("id_token_hint") else None
    if claims is not None and claims.get("iss") != provider.issuer():
        claims = None
    client = Client.objects.filter(client_id=(claims or {}).get("aud") or params.get("client_id", ""), active=True).first()
    target = params.get("post_logout_redirect_uri", "")
    if not (client and target and client.allows_redirect(target, post_logout=True)):
        target = ""
    elif params.get("state"):
        target = _with_query(target, state=params["state"])

    if not request.user.is_authenticated:
        return _signed_out(request, target)
    if claims is not None and claims.get("sid") and claims["sid"] == request.session.get(sessions.SID):
        end_session(request)
        return _signed_out(request, target)
    key = secrets.token_urlsafe(16)
    request.session["oidc_logout"] = {key: target}
    return render(request, "identity/logout.html", {"key": key, "client": client})


@hosts.issuer_only
@never_cache
@require_POST
def logout_confirm(request):
    target = request.session.get("oidc_logout", {}).get(request.POST.get("request", ""), "")
    end_session(request)
    return _signed_out(request, target)
